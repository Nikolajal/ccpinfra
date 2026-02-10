namespace cppinfra
{
    class logger
    {
    public:
        enum class colour_tag : int
        {
            BLACK = 30,
            RED = 31,
            GREEN = 32,
            YELLOW = 33,
            BLUE = 34,
            MAGENTA = 35,
            CYAN = 36,
            WHITE = 37,
            BRIGHT_BLACK = 90,
            BRIGHT_RED = 91,
            BRIGHT_GREEN = 92,
            BRIGHT_YELLOW = 93,
            BRIGHT_BLUE = 94,
            BRIGHT_MAGENTA = 95,
            BRIGHT_CYAN = 96,
            BRIGHT_WHITE = 97,
            RESET = 0
        };
        enum class style_tag : int
        {
            NONE = 0,
            BOLD = 1,
            DIM = 2,
            ITALIC = 3,
            UNDERLINE = 4,
            BLINK_SLOW = 5,
            BLINK_FAST = 6,
            REVERSED = 7,
            HIDDEN = 8,
            STRIKETHROUGH = 9
        };

        enum class level_tag
        {
            ERROR,
            WARNING,
            INFO,
            DEBUG
        };

        using style_list = std::vector<style_tag>;
        using text_config = std::pair<colour_tag, style_list>;

        static inline std::string ansi(colour_tag colour = colour_tag::RESET, style_list styles = {style_tag::NONE})
        {
            std::string code = "";
            if (!styles.empty())
                for (auto current_style : styles)
                    code += std::to_string(static_cast<int>(current_style)) + ";";
            code += std::to_string(static_cast<int>(colour)); // append colour
            return "\033[" + code + "m";
        }
        static inline void log(const std::string_view msg, colour_tag c = colour_tag::RESET, style_list s = {style_tag::NONE}) { std::cout << ansi(c, s) << msg << ansi() << "\n"; }
        static inline void log(level_tag tag, std::string_view msg, bool flush = true)
        {
            std::ostream &out = std::cout; // could switch to cerr for errors if you like

            // Build prefix and style
            std::string styled_msg;

            switch (tag)
            {
            case level_tag::ERROR:
                styled_msg = ansi(colour_tag::RED, {style_tag::BOLD, style_tag::UNDERLINE}) + "[ERROR]" + ansi(colour_tag::RED, {style_tag::NONE}) + "    " + std::string(msg) + ansi();
                break;
            case level_tag::WARNING:
                styled_msg = ansi(colour_tag::YELLOW, {style_tag::BOLD, style_tag::UNDERLINE}) + "[WARNING]" + ansi(colour_tag::YELLOW, {style_tag::NONE}) + "  " + std::string(msg) + ansi();
                break;
            case level_tag::DEBUG:
                styled_msg = ansi(colour_tag::CYAN, {style_tag::BOLD, style_tag::UNDERLINE}) + "[DEBUG]" + ansi(colour_tag::CYAN, {style_tag::NONE}) + "    " + std::string(msg) + ansi();
                break;
            case level_tag::INFO:
                styled_msg = ansi(colour_tag::BRIGHT_BLUE, {style_tag::BOLD, style_tag::UNDERLINE}) + "[INFO]" + ansi(colour_tag::BRIGHT_BLUE, {style_tag::NONE}) + "     " + std::string(msg) + ansi();
                break;
            default:
                styled_msg = msg; // no extra styling
                break;
            }

            // Output in one shot, newline included
            out << styled_msg << "\n";

            // Optional flush
            if (flush)
                out << std::flush;
        }
        static inline void log_error(std::string_view msg, bool flush = true) { return log(level_tag::ERROR, msg, flush); }
        static inline void log_warning(std::string_view msg, bool flush = true) { return log(level_tag::WARNING, msg, flush); }
        static inline void log_info(std::string_view msg, bool flush = true) { return log(level_tag::INFO, msg, flush); }
        static inline void log_debug(std::string_view msg, bool flush = true) { return log(level_tag::DEBUG, msg, flush); }
    };
} // namespace cppinfra