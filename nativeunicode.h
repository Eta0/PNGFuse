/**
 * Creates generalizations to allow using Unicode path names as UTF-16 @c std::wstring on Windows and UTF-8 @c std::string on Unix.
 */

#ifndef PNGFUSE_NATIVEUNICODE_H
#define PNGFUSE_NATIVEUNICODE_H

#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <utility>


#ifdef _WINDOWS
#define WIN32_LEAN_AND_MEAN
// Required for native_argv()
#include <windows.h>
#include <shellapi.h>
// Required for init_locale()
#include <io.h>
#include <fcntl.h>

#define NATIVE_WIDTH(literal) L##literal  // e.g. converts "ABC" to L"ABC" or 'X' to L'X'
static std::wostream &native_out = std::wcout;
static std::wostream &native_err = std::wcerr;

#else

#define NATIVE_WIDTH(literal) literal
static std::ostream &native_out = std::cout;
static std::ostream &native_err = std::cerr;

#endif

/**
 * A string capable of representing native filesystem paths, either @c std::string or @c std::wstring.
 */
using native_string = std::filesystem::path::string_type;


/**
 * Initializes std::wcout and std::wcerr with Unicode locales on Windows.
 */
static inline void init_unicode() {
#ifdef _WINDOWS
    _setmode(_fileno(stdout), _O_WTEXT);
    _setmode(_fileno(stderr), _O_WTEXT);
#endif
}


/**
 * A class that allows runtime errors with platform-specific @c native_string message types,
 * suitable for reporting error messages containing filenames.
 */
class native_runtime_error : std::runtime_error {
    native_string message;
public:
    explicit native_runtime_error(native_string message) :
            std::runtime_error("A file error occurred."),  // Generic fallback, shouldn't be used
            message(std::move(message)) {}

    [[nodiscard]] const native_string &native_what() const {
        return message;
    }
};


/**
 * Retrieves the platform-specific Unicode @c argv array as a vector of @c native_string.
 * @param argc The original @c argc passed to @c main()
 * @param argv The original @c argv passed to @c main()
 * @return A vector containing the Unicode contents of @c argv as @c native_strings
 * @details
 *     When called on Windows, this function discards the contents of @p argc and @p argv
 *     and retrieves the true UTF-16 command line arguments via win32's GetCommandLineW().
 *     When called on other platforms, the resulting vector is constructed directly from the provided @p argv array.
 */
std::vector<native_string> native_argv(int argc, char **argv) {
    std::vector<native_string> vec_argv;
#ifdef _WINDOWS
    LPWSTR command_line = GetCommandLineW();
    if (!command_line)
        throw std::runtime_error("Could not read command line arguments.");

    static_assert (std::is_same_v<wchar_t *, LPWSTR>);
    wchar_t **utf16_argv = CommandLineToArgvW(command_line, &argc);
    if (!utf16_argv)
        throw std::runtime_error("Could not read command line arguments.");

    vec_argv.reserve(argc);
    for (int i = 0; i < argc; ++i)
        vec_argv.emplace_back(utf16_argv[i]);

    LocalFree(utf16_argv);
#else
    vec_argv.reserve(argc);
    for (int i = 0; i < argc; ++i)
        vec_argv.push_back(argv[i]);
#endif
    return vec_argv;
}

/**
 * Converts a native Unicode string type to lowercase.
 * @param s A string to be converted to lowercase
 * @return The lowercase version of @p s
 */
static inline native_string string_to_lowercase(native_string s) {
    // via https://en.cppreference.com/w/cpp/string/byte/tolower
    if constexpr (std::is_same_v<native_string, std::wstring>)
        std::transform(s.begin(), s.end(), s.begin(), [] (wchar_t c) { return towlower(c); });
    else
        std::transform(s.begin(), s.end(), s.begin(), [] (unsigned char c) { return tolower(c); });
    return s;
}

#endif //PNGFUSE_NATIVEUNICODE_H
