#ifndef PNGFUSE_ARGUMENTPARSING_H
#define PNGFUSE_ARGUMENTPARSING_H

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <optional>
#include <exception>

#include "nativeunicode.h"

using std::filesystem::path;


/**
 * A class for storing and parsing command line flags.
 */
struct Flags {
    bool help : 1 = false;
    bool list : 1 = false;
    bool clean : 1 = false;
    bool overwrite : 1 = false;
private:
    bool _ignore_rest : 1 = false;
public:
    std::optional<path> output;

    /**
     * Permissively parse command line flags.
     * @param argc Number of available arguments
     * @param argv Array of available arguments
     * @param index Argument index to parse
     * @return Number of extra arguments consumed (for flags with space-delimited values)
     */
    inline int process_flag(std::span<native_string> args, int index) {
        if (_ignore_rest)
            return 0;

        native_string arg{string_to_lowercase(args[index])};
        if (arg == NATIVE_WIDTH("--")) {
            _ignore_rest = true;
            return 0;
        }

        /**
         * Indicates whether the function reached ahead to the next value in @p argv to read a value.
         * This is used to indicate to the caller that they should skip parsing the next argument.
         */
        bool extra_value_consumed = false;

        // help flags = "-h", "--help"
        // list flags = "-l", "--list"
        // clean flags = "-c", "-r", "--clean", "--remove"
        // overwrite flags = "-m", "--overwrite", "--modify"
        // output flags = "-o", "--out.*"

        if (arg.starts_with(NATIVE_WIDTH("--"))) {
            arg = arg.substr(2);
            // Handle verbose flags, matching prefixes
            constexpr native_string_view
                help_flag        = NATIVE_WIDTH("help"),
                list_flag        = NATIVE_WIDTH("list"),
                clean_flag_1     = NATIVE_WIDTH("clean"),     clean_flag_2     = NATIVE_WIDTH("remove"),
                overwrite_flag_1 = NATIVE_WIDTH("overwrite"), overwrite_flag_2 = NATIVE_WIDTH("modify"),
                output_flag      = NATIVE_WIDTH("out");
            if      (help_flag       .starts_with(arg)) help = true;
            else if (list_flag       .starts_with(arg)) list = true;
            else if (clean_flag_1    .starts_with(arg)
                     || clean_flag_2 .starts_with(arg)) clean = true;
            else if (overwrite_flag_2.starts_with(arg)
                     || (arg.size() > 1 && overwrite_flag_1.starts_with(arg))) overwrite = true;

            // Since an equals-separator possibly being included would mess up matching, match on just the prefix before any equals
            else if (const auto arg_prefix = FlagValue::split_prefix(arg); output_flag.starts_with(arg_prefix) || arg_prefix.starts_with(output_flag)) {
                if (const auto [arg_value, reached_ahead] = FlagValue(args, index); arg_value.has_value()) {
                    output.emplace(arg_value.value());
                    extra_value_consumed |= reached_ahead;
                } else
                    throw std::runtime_error("Custom output flag was specified, but no path was given.");
            } else
                throw native_runtime_error(NATIVE_WIDTH("Unknown flag specified: ") + arg);
        }

        else if (arg.starts_with(NATIVE_WIDTH('-'))) {
            arg = arg.substr(1);
            // Handle grouped short flags
            bool equals_encountered = false;
            for (const auto short_flag : arg) {
                if (equals_encountered)
                    break;
                switch (short_flag) {
                    case NATIVE_WIDTH('='): equals_encountered = true; break;
                    case NATIVE_WIDTH('h'): help = true; break;
                    case NATIVE_WIDTH('l'): list = true; break;
                    case NATIVE_WIDTH('c'):
                    case NATIVE_WIDTH('r'): clean = true; break;
                    case NATIVE_WIDTH('m'): overwrite = true; break;
                    case NATIVE_WIDTH('o'):
                        if (const auto [arg_value, reached_ahead] = FlagValue(args, index); arg_value.has_value()) {
                            output.emplace(arg_value.value());
                            extra_value_consumed |= reached_ahead;
                        } else
                            throw std::runtime_error("Custom output flag was specified, but no path was given.");
                        break;
                    default:
                        throw native_runtime_error(NATIVE_WIDTH("Unknown flag specified: ") + native_string(1, short_flag));
                }
            }
        } else
            throw std::logic_error("Attempted to process an invalid flag format.");

        return extra_value_consumed;
    }

private:
    using native_string_view = std::basic_string_view<native_string::value_type>;

    /**
     * A class that extracts the value associated with a flag.
     */
    struct FlagValue {
        std::optional<native_string_view> value;
        bool reached_ahead = false;

        /**
         * Locates the value associated with a given flag in the input span @p args.
         * @param args The argument sequence containing the flag whose value is to be retrieved
         * @param index The index of the flag in the argument sequence whose value is to be retrieved
         */
        constexpr FlagValue(std::span<const native_string> args, int index) {
            // Get the associated output path
            // Equals-separated -f=value, uses original case string to read value
            if ((value = split_value(args[index])))
                return;
            // Space-separated -f value
            else if (index + 1 < args.size()) {
                reached_ahead = true;
                value.emplace(args[index + 1]);
            }
        }

        /**
         * Returns the '='-delimited value present in @p arg, if any.
         * @param arg The text of the flag whose value is to be retrieved
         * @return A @c native_string_view containing a substring of @p arg representing a value, if one is found
         */
        static constexpr std::optional<native_string_view> split_value(native_string_view arg) {
            constexpr auto equals_separator = NATIVE_WIDTH('=');
            const auto equals_pos = arg.find(equals_separator);
            if (equals_pos != native_string_view::npos && equals_pos + 1 < arg.size())
                return arg.substr(equals_pos + 1);
            else
                return {};
        }

        /**
         * Returns the '='-delimited prefix of @p arg representing the flag portion of a flag-with-possible-argument.
         * @param arg The flag whose prefix is to be extracted
         * @return A @c native_string_view containing a substring of @p arg representing the flag portion of @p arg
         * @details If no '=' delimiter is found, this function returns its input unaltered.
         */
        static constexpr native_string_view split_prefix(native_string_view arg) {
            constexpr auto equals_separator = NATIVE_WIDTH('=');
            const auto equals_pos = arg.find(equals_separator);
            return arg.substr(0, equals_pos);
        }
    };
};

/**
 * A class for reading and storing command line flags and arguments.
 */
struct Arguments {
    Flags flags;
    std::vector<path> args;

    static std::optional<path> program_path;
    // static to permit error handlers in main() to print this even if the rest of argument parsing fails,
    // optional to cleanly represent possible uninitialized state if native_argv() fails

    inline Arguments(int argc, char **argv) {
        auto unprocessed_args = native_argv(argc, argv);
        Arguments::program_path.emplace(unprocessed_args.front());
        for (int i = 1; i < unprocessed_args.size(); ++i) {
            if (unprocessed_args[i].front() == NATIVE_WIDTH('-'))
                i += flags.process_flag(unprocessed_args, i);
            else
                args.emplace_back(unprocessed_args[i]);
        }
        if (flags.overwrite && flags.output.has_value()) {
            throw std::runtime_error("Cannot specify both overwrite mode and a custom output path.");
        }
    }

    [[nodiscard]] inline std::size_t num_args() const { return args.size(); }
};

std::optional<path> Arguments::program_path = std::nullopt;

#endif //PNGFUSE_ARGUMENTPARSING_H
