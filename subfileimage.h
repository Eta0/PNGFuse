#ifndef PNGFUSE_SUBFILEIMAGE_H
#define PNGFUSE_SUBFILEIMAGE_H

#include "image.h"

/**
 * A class representing a file and its contents from either the filesystem or an embedded @c fuSe chunk.
 * @details A @c SubFile has a serialized ("merged") representation in the format <tt>"[filename]NUL[binary contents]"</tt>,
 *     where @c filename is encoded in UTF-8.
 */
struct SubFile {
    /**
     * The filename that is recorded in the @c fuSe chunk.
     */
    path name;

    /**
     * The uncompressed file data associated with this subfile..
     */
    std::vector<unsigned char> contents;

    /**
     * Creates a @c SubFile by loading the contents of @p file from the filesystem.
     * @param file A path to a file to load to create the @c SubFile
     * @return A @c SubFile with contents identical to @p file and name equal to the filename component of @p file
     */
    static SubFile from_file(const path &file) {
        return SubFile{file.filename(), read(file)};
    }

    /**
     * Saves the contents of this @c SubFile at the path stored in @c name.
     */
    inline void save() const {
        write(name, contents);
    }

    /**
     * Decodes a @c SubFile from its merged representation.
     * @param data The merged representation of a @c SubFile
     * @return The deserialized form of @p data as a @c SubFile object
     */
    static SubFile from_merged(std::basic_string_view<unsigned char> data) {
        const auto end_of_filename = static_cast<const unsigned char *>(std::memchr(data.data(), '\0', data.size()));
        const path filename = std::u8string{data.data(), end_of_filename};
        const std::vector<unsigned char> contents = {end_of_filename + 1, data.data() + data.size()};
        return {filename, contents};
    }

    /**
     * Encodes a @c SubFile into its merged representation.
     * @return The merged representation of a @c SubFile in the format <tt>"[filename]NUL[binary contents]"</tt>
     */
    std::vector<unsigned char> merged() && {
        const auto filename = name.u8string();
        std::vector<unsigned char> combined(filename.cbegin(), filename.cend());
        combined.reserve(combined.size() + 1 + contents.size());
        combined.push_back('\0');
        std::move(contents.begin(), contents.end(), std::back_inserter(combined));
        return combined;
    }
};


/**
 * A class that handles the decoding and encoding of the private @c fuSe chunk type.
 * @details
 *     A @c fuSe chunk has a mostly compatible format with a @c zTXt chunk, with a few alterations:\n
 *     1. The keyword is always "PNGFuse"\n
 *     2. The value is of the form <tt>[filename]NUL[binary contents]</tt>\n
 *     Like a @c zTXt chunk, the keyword and value are separated by two @c NUL bytes, and the value is zlib-compressed.
 *     Unlike in a @c zTXt chunk, where the contents are human-readable Latin-1 encoded text,
 *     @c filename is encoded in UTF-8, and the data following the filename is a sequence of bytes with no particular encoding.
 */
struct FuseChunk final : public TextChunk<std::vector<unsigned char>> {
    static constexpr std::string_view key = "PNGFuse";

    /**
     * The PNG chunk type.
     * @return The PNG chunk type for a @c fuSe chunk, i.e. @c "fuSe"
     */
    static consteval const char *type() { return "fuSe"; }

    /**
     * Compresses and encodes the keyword ("PNGFuse") and file info as a @c fuSe chunk with a chunk header.
     * @return The file info encoded into a @c fuSe chunk
     */
    [[nodiscard]] ImageImplementation::ManagedByteSpan encode() const final {
        return ImageImplementation::chunk_encode(encode_data(), FuseChunk::type());
    }

    /**
     * Initializes a @c fuSe chunk's @c value by serializing a @c SubFile object.
     * @param data The @c SubFile data to be converted into a @c fuSe chunk
     */
    explicit FuseChunk(SubFile &&data) : TextChunk(key, std::move(data).merged()) {}

    /**
     * Decode the @c fuSe chunk data pointed to by @p chunk.
     * @param chunk A pointer to the beginning of an encoded PNG chunk's header
     */
    explicit FuseChunk(const unsigned char *chunk) : TextChunk(chunk) {}

    /**
     * Constructs a @c SubFile object by deserializing a @c fuSe chunk's @c value.
     * @return The @c SubFile object that was encoded in the @c fuSe chunk
     */
    [[nodiscard]] inline SubFile to_subfile() const {
        return SubFile::from_merged({value.data(), value.size()});
    }

    /**
     * Determines if a pointer refers to a valid @c fuSe chunk header, without checking CRC validity.
     * @param chunk A pointer to the beginning of an encoded PNG chunk's header
     * @return @c true if @p chunk points to a chunk with the @c fuSe type for its header and the "PNGFuse" keyword, @c false otherwise
     */
    [[nodiscard]] static inline bool is_valid(const unsigned char *chunk) {
        return lodepng_chunk_type_equals(chunk, FuseChunk::type())
               && !std::memcmp(key.data(), lodepng_chunk_data_const(chunk), key.size());
    }
};


/**
 * A class that handles translating between @c fuSe chunks in images and higher-level data types.
 * @details
 *     Namely, a @c SubFileImage supports loading, compressing, encoding, and writing @c fuSe chunks directly from supplied filesystem paths,
 *     and decompressing and deserializing @c fuSe chunks within an image into an enumeration of @c SubFile objects.
 */
struct SubFileImage : public Image<FuseChunk> {
    /**
     * Loads PNG image data from a file.
     * @param file The file from which to load the image data
     */
    explicit SubFileImage(const path &file) : Image(file) {}

    /**
     * Loads the contents of @p file from the filesystem, serializes it into a @c fuSe chunk, and inserts it into the image data..
     * @param file A path to a file to load to create the @c fuSe chunk
     * @details This adds the new chunk immediately following the end of the last @c IDAT chunk.
     */
    void add_sub_file(const path &file) {
        add_chunk(FuseChunk(SubFile::from_file(file)));
    }

    /**
     * Loads the contents of several files from the filesystem, serializing them into @c fuSe chunks in parallel, and inserting them into the image data.
     * @param files A vector of @c path objects to load to create the @c fuSe chunks
     * @details This adds new chunks immediately following the end of the last @c IDAT chunk.
     */
    void add_sub_file(const std::vector<path> &files) {
        std::vector<FuseChunk> chunks;
        chunks.reserve(files.size());
        for (const auto &file : files)
            chunks.emplace_back(FuseChunk(SubFile::from_file(file)));
        add_chunk(chunks);
    }

    /**
     * Enumerates the @c SubFiles encoded in @c fuSe chunks in the image.
     * @return A vector of @c SubFile objects decoded from the @c fuSe chunks in the image
     */
    [[nodiscard]] std::vector<SubFile> get_sub_files() const {
        std::vector<SubFile> sub_files;
        const auto chunks = get_chunks();
        sub_files.reserve(chunks.size());
        for (const auto &chunk : chunks)
            sub_files.emplace_back(chunk.to_subfile());
        return sub_files;
    }
};

#endif //PNGFUSE_SUBFILEIMAGE_H
