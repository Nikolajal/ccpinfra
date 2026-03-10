#include <mist/logger/progress_bar_registry.h>
#include <mist/logger/progress_bar.h>
#include <mist/logger/multi_progress_bar.h>
#include <iostream>

namespace mist::logger::detail
{
    // =========================================================================
    // Singleton accessor
    // =========================================================================

    progress_bar_registry &progress_bar_registry::instance()
    {
        static progress_bar_registry inst;
        return inst;
    }

    // =========================================================================
    // Registration
    // =========================================================================

    void progress_bar_registry::register_bar(progress_bar *bar)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        current_.type       = active_bar_handle::kind::single;
        current_.ptr.single = bar;
    }

    void progress_bar_registry::register_bar(multi_progress_bar *bar)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        current_.type      = active_bar_handle::kind::multi;
        current_.ptr.multi = bar;
    }

    void progress_bar_registry::unregister_bar(progress_bar *bar)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (current_.type == active_bar_handle::kind::single &&
            current_.ptr.single == bar)
        {
            current_ = {};
        }
    }

    void progress_bar_registry::unregister_bar(multi_progress_bar *bar)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (current_.type == active_bar_handle::kind::multi &&
            current_.ptr.multi == bar)
        {
            current_ = {};
        }
    }

    // =========================================================================
    // Commit protocol
    // =========================================================================

    void progress_bar_registry::erase_active_bar_locked()
    {
        // Caller already holds mutex_ (via lock() or log_print_guard).
        // Erase the terminal lines occupied by the active bar so the logger
        // can print cleanly above it.
        //
        // For progress_bar (single line): a plain "\r\033[2K" is enough —
        // the bar's own render() always starts with that sequence, so we
        // just need to clear the current line.
        //
        // For multi_progress_bar (N lines): move up (N-1) lines and clear
        // each, leaving the cursor on the topmost cleared line.

        if (!current_.has_bar())
            return;

        if (current_.type == active_bar_handle::kind::single)
        {
            std::cout << "\r\033[2K";
        }
        else // multi
        {
            const int n = current_.ptr.multi->rendered_line_count();
            if (n <= 0)
                return;
            std::cout << "\r\033[2K";
            for (int i = 1; i < n; ++i)
                std::cout << "\033[1A\r\033[2K";
        }
    }

    void progress_bar_registry::redraw_active_bar_locked()
    {
        // Caller already holds mutex_.
        // Redraw the active bar after a log line has been printed.
        // render_unlocked() must NOT re-acquire the bar's own mutex if the
        // bar implementation is designed to be called from here (see the
        // lock-order comments in the headers).

        if (!current_.has_bar())
            return;

        if (current_.type == active_bar_handle::kind::single)
            current_.ptr.single->render_unlocked(false);
        else
            current_.ptr.multi->render_unlocked(false);

        std::cout << std::flush;
    }

    // =========================================================================
    // Mutex access
    // =========================================================================

    void progress_bar_registry::lock()
    {
        mutex_.lock();
    }

    void progress_bar_registry::unlock()
    {
        mutex_.unlock();
    }

    bool progress_bar_registry::has_active_bar() const
    {
        // Called from logger free functions — no lock taken here because this
        // is only a hint; the real guard is log_print_guard which locks first.
        return current_.has_bar();
    }

    // =========================================================================
    // log_print_guard
    // =========================================================================
    // RAII helper: on construction lock the registry and erase the active bar;
    // on destruction redraw the bar and unlock.
    //
    // This ensures that any log message printed between ctor and dtor appears
    // cleanly on its own line without corrupting the progress display.

    log_print_guard::log_print_guard()
    {
        auto &reg = progress_bar_registry::instance();
        reg.lock();
        reg.erase_active_bar_locked();
    }

    log_print_guard::~log_print_guard()
    {
        auto &reg = progress_bar_registry::instance();
        reg.redraw_active_bar_locked();
        reg.unlock();
    }

} // namespace mist::logger::detail