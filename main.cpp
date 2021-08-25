/**
 * Author: Eta
 * PNGFuse is an application that creates and reads custom "fuSe" chunks in PNGs containing full zlib-compressed files.
 *
 * Copyright (c) 2021 Eta, offered under the zlib license: https://opensource.org/licenses/Zlib
 */

#include <iostream>
#include <filesystem>
#include <string_view>
#include <vector>
#include <ranges>
#include <utility>
#include <algorithm>

#include "subfileimage.h"
#include "argumentparsing.h"

using std::filesystem::path;

constexpr static std::u8string_view PNG_EXTENSION = u8".png";


/**
 * Converts a UTF-8 encoded string to lowercase.
 * @param s A string to be converted to lowercase
 * @return The lowercase version of @p s
 */
static inline std::u8string string_to_lowercase(std::u8string s) {
    // via https://en.cppreference.com/w/cpp/string/byte/tolower
    std::transform(s.begin(), s.end(), s.begin(), [] (char8_t c) { return tolower(c); });
    return s;
}


/**
 * Determines which file from the list of paths should be used as a fusion target for the other files.
 * @param r A range of paths to search for a target
 * @return The fusion target file from @p r
 */
template <std::ranges::input_range PathRange>
auto find_target(const PathRange &r) {
    return std::ranges::find_if(r, [] (const path &p) { return string_to_lowercase(p.extension().u8string()) == PNG_EXTENSION; });
}


/**
 * Add subfiles to a specified file.
 * @param files A list of files to take part in the fusion. The first PNG listed is the target for the fusion
 * @param overwrite Whether to overwrite the target file with the result, or to determine a new filename automatically
 * @param output A custom output path to save the result. Overrides the @p overwrite parameter
 */
void fuse(std::vector<path> files, bool overwrite=false, const std::optional<path> &output=std::nullopt) {
    const auto target = find_target(files);
    if (target == files.cend())
        throw std::runtime_error("Could not find a target PNG to fuse into.");
    const path target_file = *target;
    files.erase(target);

#ifdef _DEBUG
    native_out << "Target file: " << target_file.native() << std::endl
              << "Files to fuse:" << std::endl;
    for (const auto &f : files) {
        native_out << f.native() << std::endl;
    }
#endif

    SubFileImage image(target_file);

    if (files.size() == 1)
        image.add_sub_file(files.front());
    else
        image.add_sub_file(files);

    path output_file = output.value_or(target_file);
    if (!output.has_value() && !overwrite) {
        // Generate a non-conflicting name
        output_file.replace_extension(".fused");
        output_file += target_file.extension();
    }

    image.save(output_file);
}


/**
 * Extract subfiles from a specified file.
 * @param source Path to a file from which to extract subfiles
 */
void sunder(const path &source) {
    for (const SubFile &file : SubFileImage(source).get_sub_files())
        file.save();
}


/**
 * List subfiles present in a specified file.
 * @param source Path to a file whose subfiles are to be listed
 */
void list(const path &source) {
    for (const SubFile &file : SubFileImage(source).get_sub_files()) {
        native_out << file.name.native() << " : " << file.contents.size() << " bytes" << std::endl;
    }
}


/**
 * Removes subfiles from a specified file.
 * @param source Path to a file from which to remove subfiles
 * @param overwrite Whether to overwrite the input file with the result, or to determine a new filename automatically
 * @param output A custom output path to save the result. Overrides the @p overwrite parameter
 */
void clean(const path &source, bool overwrite=false, const std::optional<path> &output=std::nullopt) {
    SubFileImage image(source);
    const std::size_t num_cleared = image.clear_chunks();
    native_out << num_cleared << " subfile" << (num_cleared == 1 ? "" : "s") << " removed." << std::endl;

    path output_path = output.value_or(source);
    if (!output.has_value() && !overwrite) {
        // Generate a non-conflicting name
        std::u8string new_stem = output_path.stem().u8string();
        std::u8string new_extension = output_path.extension().u8string();
        if (string_to_lowercase(new_stem).ends_with(u8".fused"))
            new_stem = {new_stem.begin(), std::next(new_stem.end(), -6)}; // skip ".fused"
        else
            new_extension = u8".unfused" + new_extension;
        output_path.replace_filename(new_stem + new_extension);
    }

    image.save(output_path);
}


/**
 * Prints the usage information for the program to the specified output stream.
 * @param program_path The path to the program executable, i.e. @c argv[0]
 * @param stream The stream to which to print usage info
 */
void print_usage(const path &program_path, decltype(native_out) &stream=native_out) {
    const native_string program_name = program_path.filename().native();
    stream << "usage: " << program_name << " [-h] [--list] [--clean] [--overwrite] [--output <PATH>] fuse-host.png [files to fuse...]" << std::endl
           << std::endl
           << "fuse subfiles into PNG metadata." << std::endl
           << std::endl
           << "Specify multiple files to perform a fusion into the first PNG listed," << std::endl
           << " or specify a single fused PNG to extract its subfiles (without removing them)." << std::endl
           << std::endl
           << "positional arguments:" << std::endl
           << "  fuse-host.png         path to a PNG in which to store subfiles" << std::endl
           << "  files to fuse         one or more files to be fused into fuse-host.png" << std::endl
           << std::endl
           << "optional arguments:" << std::endl
           << "  -h, --help            show this help message and exit" << std::endl
           << "  -l, --list            list the subfiles present in a fused PNG" << std::endl
           << "  -c, --clean           remove all subfiles from a fused PNG" << std::endl
           << "  -m, --overwrite       modify the input files when fusing or cleaning instead of creating new ones" << std::endl
           << "  -o, --output <PATH>   custom output path for the result of a fuse or clean operation" << std::endl;
}


int main(int argc, char **argv) try {
    init_unicode();
    Arguments args(argc, argv);

    if (args.num_args() == 0 || args.flags.help) {
        print_usage(Arguments::program_path.value());
        return 0;
    } else if (!(args.flags.list || args.flags.clean)) {
        if (args.num_args() == 1)
            sunder(args.args[0]);
        else
            fuse(args.args, args.flags.overwrite, args.flags.output);
    } else {
        for (const path &file : args.args) {
            if (args.num_args() > 1)
                // Provide context for which file out of multiple is being listed or cleaned
                native_out << file.filename().native() << ':' << std::endl;
            if (args.flags.list)
                list(file);
            if (args.flags.clean)
                clean(file, args.flags.overwrite, args.flags.output);
        }
    }
    return 0;
} catch (const native_runtime_error &e) {
    native_err << "Error: " << e.native_what() << std::endl;
    print_usage(Arguments::program_path.value_or(argv[0]), native_err);
    return -1;
} catch (const std::runtime_error &e) {
    native_err << "Error: " << e.what() << std::endl;
    print_usage(Arguments::program_path.value_or(argv[0]), native_err);
    return -1;
}
