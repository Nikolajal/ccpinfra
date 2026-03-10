#pragma once
#include <mist/logger/logger_types.h>
#include <chrono>
#include <mutex>
#include <optional>
#include <cstdint>
#include <algorithm>

#if defined(__has_include) && __has_include(<sys/ioctl.h>)
#include <sys/ioctl.h>
#include <unistd.h>
#define MIST_HAS_IOCTL 1
#endif

namespace mist::logger
{
    enum class bar_style
    {
        ARROW, ///< [=====>    ]  classic ASCII
        BLOCK  ///< [██████░░░░]  Unicode block characters
    };

    /**
     * @brief In-place terminal progress bar with elapsed time and ETA.
     *
     * Owns its own state (start time, style, active flag) so multiple
     * independent bars can coexist. Integrates with the logger safety-commit
     * mechanism — a normal log() call while the bar is active will
     * automatically commit it before printing.
     *
     * The bar auto-detects terminal width via ioctl(TIOCGWINSZ), falling
     * back to 80 columns on non-POSIX platforms or if the query fails.
     *
     * ### Thread safety
     * All public methods are thread-safe. A single `std::mutex` guards all
     * mutable state so any number of threads may call `update()` or `finish()`
     * concurrently without external synchronisation.
     *
     * The lock is held only for the duration of each individual call, so
     * threads contend only at the render step — the actual work between
     * updates runs fully in parallel.
     *
     * Note: `multi_bar` uses the same single-mutex pattern and also exposes
     * a thread-safe API. Prefer `multi_bar` when you have distinct named
     * subtasks; use `progress_bar` for a single unified counter.
     *
     * Example (two threads sharing one bar):
     * @code{.cpp}
     * mist::logger::progress_bar bar;
     * std::atomic<int> counter{0};
     * const int total = 1000;
     *
     * auto worker = [&]() {
     *     while (true) {
     *         int i = counter.fetch_add(1);
     *         if (i >= total) break;
     *         bar.update(i, total);   // safe from any thread
     *         do_work(i);
     *     }
     * };
     *
     * std::thread t1(worker), t2(worker);
     * t1.join(); t2.join();
     * bar.finish();
     * mist::logger::info("All done.");
     * @endcode
     */
    class progress_bar
    {
    public:
        explicit progress_bar(bar_style style = bar_style::BLOCK);

        /**
         * @brief Drive by current + total — fraction computed internally.
         *
         * Thread-safe: acquires the internal mutex for the duration of the
         * call. Templated on any integral type so integer literals resolve
         * unambiguously to this overload rather than the floating-point one.
         */
        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        void update(T current, T total, bool flush = true)
        {
            if (total <= 0)
                return;
            const float fraction = static_cast<float>(current) / static_cast<float>(total);
            // Lock is acquired inside render() so the clamp + cast are done
            // cheaply before we enter the critical section.
            render(std::clamp(fraction, 0.0f, 1.0f),
                   static_cast<int64_t>(current),
                   static_cast<int64_t>(total),
                   flush);
        }

        /**
         * @brief Drive by pre-computed fraction in [0.0, 1.0].
         *
         * Thread-safe: acquires the internal mutex for the duration of the
         * call. Accepts float or double — floating-point literals resolve
         * unambiguously here.
         */
        void update(double fraction, bool flush = true);

        /**
         * @brief Commit the bar with a newline and stop the clock.
         *
         * Thread-safe. Safe to call multiple times — no-op after the first,
         * guarded by the same mutex so concurrent finish() calls cannot
         * double-commit.
         */
        void finish(bool flush = true);

        /**
         * @brief Returns true if the bar is still running.
         *
         * Note: the value may change between the call and any subsequent use,
         * so treat it as a snapshot rather than a guarantee. For sequencing
         * logic, prefer relying on finish() being idempotent.
         */
        [[nodiscard]] bool is_active() const
        {
            std::lock_guard<std::mutex> lk(mutex_);
            return active_;
        }

    private:
        using clock_t     = std::chrono::steady_clock;
        using time_point  = std::chrono::time_point<clock_t>;

        // Mutable so is_active() can lock in a const context.
        mutable std::mutex mutex_;

        bar_style  style_;
        bool       active_       = false;
        int        suffix_width_ = -1;  ///< Fixed on first render; -1 = uninitialised.
        time_point start_;

        [[nodiscard]] static int         terminal_width();
        [[nodiscard]] static std::string format_duration(double seconds);

        /**
         * @brief Core render — acquires mutex_, then draws the bar.
         *
         * Called by both update() overloads after the fraction is computed.
         * Separating the cheap arithmetic (done before the lock) from the
         * render (done under the lock) minimises contention: threads only
         * serialise at the actual terminal write, not at the division.
         */
        void render(float fraction,
                    std::optional<int64_t> current,
                    std::optional<int64_t> total,
                    bool flush);
    };

} // namespace mist::logger