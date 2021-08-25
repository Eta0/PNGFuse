#ifndef PNGFUSE_FILEIO_H
#define PNGFUSE_FILEIO_H

#include <filesystem>
#include <fstream>
#include <vector>
#include <span>

#include "nativeunicode.h"

/**
 * Reads the contents of the file at @p in as <tt>unsigned char</tt>s.
 * @param in Path to a file whose contents are to be read
 * @return The binary contents of the file, in the form of <tt>unsigned char</tt>s
 * @throw @c native_runtime_error if @p in could not be read
 */
static std::vector<unsigned char> read(const std::filesystem::path &in) {
    // std::basic_ifstream<unsigned char> is not guaranteed to exist, and compiles on MSVC but not GCC
    // So instead std::ifstream is used alongside reinterpret_cast, which yields equivalent results
    std::ifstream input_file(in, std::ios::in | std::ios::binary);
    if (input_file) {
        std::vector<unsigned char> contents;
        input_file.seekg(0, std::ios::end);
        const auto size = input_file.tellg();
        input_file.seekg(0, std::ios::beg);
        contents.resize(static_cast<decltype(contents)::size_type>(size));
        input_file.read(reinterpret_cast<char *>(contents.data()), static_cast<std::streamsize>(size));
        if (input_file)
            return contents;
        else
            throw native_runtime_error(NATIVE_WIDTH("Failed to read file ") + in.native() + NATIVE_WIDTH('.'));
    } else
        throw native_runtime_error(NATIVE_WIDTH("Could not open input file ") + in.native() + NATIVE_WIDTH('.'));
}

/**
 * Writes @p contents to the file at @p out.
 * @param out Path to a file to which to write @p contents
 * @param contents Bytes to write into @c out, in the form of <tt>unsigned char</tt>s
 * @throw @c native_runtime_error if @p out could not be written to
 */
static void write(const std::filesystem::path &out, const std::span<const unsigned char> contents) {
    std::ofstream output_file(out, std::ios::out | std::ios::binary);
    if (output_file) {
        output_file.write(reinterpret_cast<const char *>(contents.data()), static_cast<std::streamsize>(contents.size()));
        if (!output_file)
            throw native_runtime_error(NATIVE_WIDTH("Failed to write to file ") + out.native() + NATIVE_WIDTH('.'));
    } else
        throw native_runtime_error(NATIVE_WIDTH("Could not open output file ") + out.native() + NATIVE_WIDTH('.'));
}

#endif //PNGFUSE_FILEIO_H
