#pragma once
#include <mist/logger/progress_bar.h> // progress_bar, bar_style
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mist::logger
{
    class multi_bar;  // forward declaration for subtask_bar

    // =========================================================================
    // subtask_bar
    // =========================================================================
    /**
     * @brief Lightweight non-owning handle to one subtask row inside a
     *        multi_bar group.
     *
     * A subtask_bar holds a string tag and a back-reference to its parent
     * multi_bar. Every mutating call (update, finish) is delegated to the
     * parent, which holds the single mutex that guards the entire group —
     * the same pattern as std::string_view over std::string, but for
     * lifetime-managed terminal rows.
     *
     * @warning The parent multi_bar must outlive all subtask_bar handles.
     *          Calling any method after the parent is destroyed is UB.
     *
     * Typical usage:
     * @code{.cpp}
     * auto& s = mb.add_subtask("Hit finding");
     * for (int i = 0; i <= n; ++i)
     * {
     *     s.update(i, n);
     *     do_work(i);
     * }
     * s.finish();   // collapses this row; auto-ticks main bar by 1/N
     * @endcode
     */
    class subtask_bar
    {
    public:
        // --- update overloads ------------------------------------------------

        /**
         * @brief Drive by current + total (any integral type).
         *
         * The integral constraint is needed to resolve integer literals
         * unambiguously to this overload rather than the double one below.
         */
        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        void update(T current, T total, bool flush = true)
        {
            if (total <= 0) return;
            const float frac = static_cast<float>(current) / static_cast<float>(total);
            _update_impl(std::clamp(frac, 0.0f, 1.0f),
                         static_cast<int64_t>(current),
                         static_cast<int64_t>(total),
                         flush);
        }

        /** @brief Drive by pre-computed fraction in [0.0, 1.0]. */
        void update(double fraction, bool flush = true);

        /**
         * @brief Mark this subtask complete.
         *
         * Notifies the parent to:
         *  - remove this row from the active list (lines collapse upward),
         *  - increment the auto-progress counter on the main bar (1/N tick).
         *
         * Safe to call multiple times — no-op after the first.
         */
        void finish(bool flush = true);

        [[nodiscard]] bool        is_active() const { return active_; }
        [[nodiscard]] const std::string& tag() const { return tag_; }

    private:
        friend class multi_bar;

        /** Only multi_bar may construct subtask_bar handles. */
        subtask_bar(std::string tag, multi_bar& parent)
            : tag_(std::move(tag)), parent_(parent) {}

        // non-copyable, non-movable — handles are stored by unique_ptr inside
        // multi_bar, so their address is stable and we can take a reference.
        subtask_bar(const subtask_bar&)            = delete;
        subtask_bar& operator=(const subtask_bar&) = delete;
        subtask_bar(subtask_bar&&)                 = delete;
        subtask_bar& operator=(subtask_bar&&)      = delete;

        void _update_impl(float fraction,
                          std::optional<int64_t> current,
                          std::optional<int64_t> total,
                          bool flush);

        std::string tag_;
        multi_bar&  parent_;

        // Per-subtask state — only written through the parent mutex.
        float    fraction_ = 0.0f;
        int64_t  current_  = 0;
        int64_t  total_    = 0;
        bool     active_   = true;
    };


    // =========================================================================
    // multi_bar
    // =========================================================================
    /**
     * @brief Composite progress display: one main bar with N collapsible
     *        subtask rows rendered below it.
     *
     * ### Rendering model
     * The group owns the terminal "region" that spans:
     *   - 1 line  : main bar
     *   - 1 line  : separator
     *   - K lines : one per active subtask  (K ∈ [0, N])
     *
     * On every render the cursor is moved up by the previously-drawn line
     * count using the ANSI escape `\033[{n}A`, then all lines are redrawn
     * in-place. This is the same approach used by build tools such as Ninja
     * or Cargo. When a subtask finishes, K shrinks by one and the terminal
     * block contracts naturally on the next render.
     *
     * ### Progress driving
     * The main bar can be driven in two independent ways — both are always
     * available and compose additively:
     *
     *  - **Auto** (default): each `subtask.finish()` atomically increments an
     *    internal counter and maps it to `finished/total_subtasks` on the main
     *    bar.  No caller action required.
     *  - **Manual override**: call `multi_bar::update(current, total)` or
     *    `multi_bar::update(fraction)` at any time to set the main bar
     *    directly, overriding the auto value for that frame.
     *
     * ### Thread safety
     * A single `std::mutex` on `multi_bar` serialises all writes from any
     * subtask handle or direct caller. The mutex is recursive-safe because
     * `subtask_bar` methods lock it before delegating — this avoids a
     * double-lock if the caller ever calls `multi_bar::render_all` directly.
     *
     * ### Example
     * @code{.cpp}
     * mist::logger::multi_bar mb;
     * auto& calib  = mb.add_subtask("Calibration");
     * auto& hits   = mb.add_subtask("Hit finding");
     * auto& rings  = mb.add_subtask("Ring finder");
     *
     * // drive subtasks independently (e.g. from different threads)
     * calib.update(42, 1000);
     * hits.update(0.17);
     * rings.update(50, 500);
     *
     * calib.finish();   // row disappears; main bar auto-advances to 1/3
     *
     * mb.finish();      // commit everything; prints newline; stops clock
     * mist::logger::info("All done.");
     * @endcode
     */
    class multi_bar
    {
    public:
        explicit multi_bar(bar_style style = bar_style::BLOCK);

        // Prevent copy/move — the group owns terminal cursor state and
        // subtask_bar handles hold a reference back to *this.
        multi_bar(const multi_bar&)            = delete;
        multi_bar& operator=(const multi_bar&) = delete;
        multi_bar(multi_bar&&)                 = delete;
        multi_bar& operator=(multi_bar&&)      = delete;

        ~multi_bar();

        // --- subtask management ----------------------------------------------

        /**
         * @brief Register a new subtask and return a stable handle to it.
         *
         * The returned reference is valid for the lifetime of this multi_bar.
         * Subtasks are rendered in registration order.
         *
         * @param tag  Short descriptive label shown to the left of the bar
         *             (e.g. "Hit finding"). Padded/truncated to a fixed width
         *             on render so all bars stay aligned.
         */
        subtask_bar& add_subtask(std::string tag);

        // --- main bar manual override ----------------------------------------

        /** @brief Manually set the main bar from current + total (integral). */
        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        void update(T current, T total, bool flush = true)
        {
            if (total <= 0) return;
            const float frac = static_cast<float>(current) / static_cast<float>(total);
            _set_main_fraction(std::clamp(frac, 0.0f, 1.0f), flush);
        }

        /** @brief Manually set the main bar from a pre-computed fraction. */
        void update(double fraction, bool flush = true);

        /**
         * @brief Commit the entire group — main bar fills to 100 %, all
         *        remaining subtask rows are cleared, and a newline is emitted.
         *
         * Safe to call multiple times — no-op after the first.
         */
        void finish(bool flush = true);

        [[nodiscard]] bool is_active() const { return active_; }

    private:
        friend class subtask_bar;

        using clock_t     = std::chrono::steady_clock;
        using time_point  = std::chrono::time_point<clock_t>;

        // --- internal helpers called (under lock) by subtask_bar ------------

        /** Called by subtask_bar::_update_impl — renders a single frame. */
        void _subtask_updated(bool flush);

        /**
         * @brief Called by subtask_bar::finish.
         *
         * Removes the row from the active list and performs the auto-tick:
         * main_fraction = ++finished_count_ / total_subtasks_.
         */
        void _subtask_finished(subtask_bar* who, bool flush);

        // --- rendering -------------------------------------------------------

        /**
         * @brief Redraw the entire group in-place.
         *
         * Uses `\033[{n}A` (cursor-up) to return to the top of the previously
         * drawn region, then overwrites every line. The ANSI cursor-up escape
         * moves the terminal cursor N lines upward without clearing content,
         * so we overwrite each line explicitly with `\r` + full-width text.
         *
         * Must be called with mutex_ held.
         */
        void _render_all(bool flush);

        /** Render one row of the main bar into @p out. */
        void _render_main(std::string& out) const;

        /** Render one subtask row into @p out. */
        void _render_subtask(std::string& out, const subtask_bar& s) const;

        /** Overwrite a line and pad to terminal width to erase stale chars. */
        static void _emit_line(std::string& out, const std::string& line, int term_width);

        void _set_main_fraction(float frac, bool flush);

        [[nodiscard]] static int  _terminal_width();
        [[nodiscard]] static std::string _format_duration(double seconds);

        // --- state -----------------------------------------------------------
        mutable std::mutex mutex_;

        bar_style  style_;
        bool       active_       = false;
        time_point start_;

        // Main bar progress — either auto-driven or manually overridden.
        float    main_fraction_   = 0.0f;
        int      finished_count_  = 0;   ///< subtasks that called finish()
        int      total_subtasks_  = 0;   ///< total registered subtasks

        // Subtask handles — unique_ptr keeps addresses stable so subtask_bar
        // references remain valid after further add_subtask() calls.
        std::vector<std::unique_ptr<subtask_bar>> subtasks_;

        // Render bookkeeping — how many lines we drew last frame, so we know
        // how far to jump the cursor back up on the next frame.
        int last_line_count_ = 0;

        // Fixed render widths (computed once on first render).
        int tag_col_width_  = -1;   ///< max tag length + padding; -1 = uninit
        int suffix_width_   = -1;   ///< elapsed/eta suffix width;  -1 = uninit
    };

} // namespace mist::logger