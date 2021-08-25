#ifndef PNGFUSE_IMAGE_H
#define PNGFUSE_IMAGE_H

#define LODEPNG_NO_COMPILE_DISK
#include "lodepng.h"
#include <cstring>
#include <string>
#include <span>
#include <ranges>
#include <thread>
#include <future>

#include "fileio.h"
#include "nativeunicode.h"

using std::filesystem::path;

namespace ImageImplementation {
    /**
     * A wrapper that takes ownership of an external array allocated via malloc().
     * This is used to interface with LodePNG's functions that return malloc()-ed C arrays.
     * @tparam T The type of data stored in the array
     */
    template <typename T>
    struct ManagedSpan {

        inline ManagedSpan(T *buffer, std::size_t size) : buffer(buffer), _data(buffer, size) {}
        inline ~ManagedSpan() { free(buffer); }

        ManagedSpan() = default;

        ManagedSpan& operator=(ManagedSpan &&other) noexcept {
            buffer = other.buffer;
            _data = std::move(other._data);
            other.buffer = nullptr;
            return *this;
        }

        ManagedSpan(ManagedSpan &&other) noexcept : buffer(other.buffer), _data(std::move(other._data)) {
            other.buffer = nullptr;
        }

        [[nodiscard]] constexpr auto begin() const { return _data.begin(); }
        [[nodiscard]] constexpr auto end()   const { return _data.end(); }
        [[nodiscard]] constexpr auto size()  const { return _data.size(); }
        [[nodiscard]] constexpr auto data()  const { return _data; }
    private:
        T *buffer;
        std::span<T> _data;
    };
    
    using ManagedByteSpan = ManagedSpan<unsigned char>;

    /**
     * Converts LodePNG error codes to thrown C++ exceptions.
     * @param error A LodePNG error code
     */
    inline void check_error(unsigned int error) {
        if (error)
            throw std::runtime_error(lodepng_error_text(error));
    }

    /**
     * Decompresses the zlib-compressed data in @p compressed.
     * @param compressed zlib-compressed bytes to be decompressed
     * @return A @c ManagedByteSpan holding uncompressed data extracted from @p compressed
     */
    ManagedByteSpan decompress(std::span<const unsigned char> compressed) {
        unsigned char *buffer = nullptr;
        std::size_t buffer_size = 0;
        auto settings = LodePNGDecompressSettings{};
        const auto error = lodepng_zlib_decompress(&buffer, &buffer_size, compressed.data(), compressed.size(), &settings);
        ManagedByteSpan decompressed{buffer, buffer_size};
        check_error(error);
        return decompressed;
    }

    /**
     * A factory function to produce a @c LodePNGCompressSettings object tuned for optimal compression ratios.
     * @return A @c LodePNGCompressSettings object tuned for optimal compressions ratios
     */
    consteval LodePNGCompressSettings best_compression() {
        LodePNGCompressSettings settings{};
        settings.btype = 2;
        settings.use_lz77 = true;
        settings.windowsize = 32768;
        settings.minmatch = 3;
        settings.nicematch = 258;
        settings.lazymatching = true;
        return settings;
    }

    /**
     * Compresses the data in @p data with zlib compression.
     * @param data Bytes to be compressed
     * @return A @c ManagedByteSpan holding a compressed form of @p data
     */
    ManagedByteSpan compress(std::span<const unsigned char> data) {
        unsigned char *buffer = nullptr;
        std::size_t buffer_size = 0;
        auto settings = best_compression();
        const auto error = lodepng_zlib_compress(&buffer, &buffer_size, data.data(), data.size(), &settings);
        ManagedByteSpan compressed{buffer, buffer_size};
        check_error(error);
        return compressed;
    }

    /**
     * Encodes data into the general PNG chunk format by combining its 4-character type code and data, and computing its CRC.
     * @param data Data to be encoded
     * @param type The 4-character type code for the chunk, e.g. zTXt
     * @return A @c ManagedByteSpan holding the encoded chunk data
     */
    ManagedByteSpan chunk_encode(std::span<const unsigned char> data, const char *type) {
        unsigned char *buffer = nullptr;
        std::size_t buffer_size = 0;
        const auto error = lodepng_chunk_create(&buffer, &buffer_size, data.size(), type, data.data());
        ManagedByteSpan chunk{buffer, buffer_size};
        check_error(error);
        return chunk;
    }
}


/**
 * A class that handles the decoding and encoding of standard PNG zTXt chunks.
 * @tparam ByteSequence A container type to hold the decoded zTXt data
 */
template<class ByteSequence=std::basic_string<unsigned char>>
        requires std::ranges::sized_range<ByteSequence> && std::ranges::input_range<ByteSequence> && std::ranges::contiguous_range<ByteSequence>
struct TextChunk {
    ByteSequence key;
    ByteSequence value;

    /**
     * The PNG chunk type.
     * @return The PNG chunk type for a @c zTXt chunk, i.e. @c "zTXt"
     */
    static consteval const char *type() { return "zTXt"; }

    /**
     * Creates a @c TextChunk holding an original key-value pair that may be encoded using @c TextChunk::encode().
     * @param key Byte data representing the key for the chunk
     * @param value Byte data representing the value for the chunk
     * @see @c TextChunk::encode()
     */
    TextChunk(ByteSequence key, ByteSequence value) : key(std::move(key)), value(std::move(value)) {}

    /**
     * Creates a @c TextChunk holding an original key-value pair that may be encoded using @c TextChunk::encode().
     * @tparam KeyContainer An alternate container type for a key (e.g. a string) that can be converted to ByteSequence
     * @param key Byte data representing the key for the chunk
     * @param value Byte data representing the value for the chunk
     * @see @c TextChunk::encode()
     */
    template<class KeyContainer>
    TextChunk(const KeyContainer &key, ByteSequence value) : key(key.cbegin(), key.cend()), value(std::move(value)) {}

    /**
     * Decode the @c zTXt chunk data pointed to by @p chunk.
     * @param chunk A pointer to the beginning of an encoded PNG chunk's header
     */
    explicit TextChunk(const unsigned char *chunk) {
        /* Via http://www.libpng.org/pub/png/spec/1.2/PNG-Chunks.html
         * A zTXt chunk contains:
         *     Keyword:            1-79 bytes (character string)
         *     Null separator:     1 byte
         *     Compression method: 1 byte
         *     Compressed text:    n bytes
         */
        const auto length = lodepng_chunk_length(chunk);
        const auto data = lodepng_chunk_data_const(chunk);
        const auto end_of_key = static_cast<const unsigned char *>(std::memchr(data, '\0', length - 1));
        if (!end_of_key)
            throw std::runtime_error("Encountered corrupt chunk");
        key = {data, end_of_key};
        const auto compression_method = std::next(end_of_key);
        if (*compression_method != 0)
            throw std::runtime_error("Encountered corrupt chunk");
        const auto compressed = std::next(compression_method);
        const auto compressed_length = static_cast<std::size_t>(length - std::distance(data, compressed));
        const auto decompressed = ImageImplementation::decompress({compressed, compressed_length});
        value = {decompressed.begin(), decompressed.end()};
    }

    /**
     * Compresses and encodes data members @c key and @c value together as a @c zTXt chunk with a chunk header.
     * @return @c key and @c value encoded into a @c zTXt chunk
     */
    [[nodiscard]] virtual ImageImplementation::ManagedByteSpan encode() const {
        return ImageImplementation::chunk_encode(encode_data(), TextChunk::type());
    }

    /**
     * Concatenates @c key with the zlib-compressed form of @c value.
     * @return A @c zTXt-compatible encoding of a key-value pair, without a PNG chunk header
     */
    [[nodiscard]] std::vector<unsigned char> encode_data() const {
        const auto compressed = ImageImplementation::compress(value);
        std::vector<unsigned char> encoded(key.cbegin(), key.cend());
        encoded.reserve(key.size() + 2 + compressed.size());
        encoded.push_back('\0');
        encoded.push_back('\0');
        std::copy(compressed.begin(), compressed.end(), std::back_inserter(encoded));
        return encoded;
    }

    /**
     * Determines if a pointer refers to a valid @c zTXt chunk header, without checking CRC validity.
     * @param chunk A pointer to the beginning of an encoded PNG chunk's header
     * @return @c true if @p chunk points to a chunk with the @c zTXt type for its header, @c false otherwise
     */
    [[nodiscard]] static inline bool is_valid(const unsigned char *chunk) {
        return lodepng_chunk_type_equals(chunk, TextChunk::type());
    }

    virtual ~TextChunk() = default;
};


/**
 * A class that handles enumerating, inserting, and deleting encoded PNG chunks of a given type in an image.
 * @tparam ChunkT A handler for the type of chunk to be targeted for reading, writing, and deleting.
 *    A @p ChunkT should support:
 *    @c ChunkT::encode() to return a form of itself suitable for insertion into PNG image data,
 *    @c ChunkT::is_valid() to determine if a chunk should be included in enumerations and deletions,
 *    and a <pre>ChunkT::ChunkT(unsigned const char *)</pre> constructor that can be called with a pointer to raw encoded chunk data.
 * @details
 *    This implementation only supports inserting chunks between the end of the @c IDAT chunks and the @c IEND chunk.
 *    This is because inserting large amounts of ancillary data before the @c IDAT chunk can slow down PNG viewing applications.
 *    \n
 *    Note, however, that enumerating chunks via @c Image<ChunkT>::get_chunks()
 *    and deleting chunks via @c Image<ChunkT>::clear_chunks() covers the whole image data, from @c IHDR to @c IEND.
 */
template <class ChunkT>
struct Image {
    /**
     * The raw PNG image data.
     */
    std::vector<unsigned char> image;

    /**
     * Loads PNG image data from a file.
     * @param file The file from which to load the image data
     */
    explicit Image(const path &file) : image(read(file)) {
        // Verify the PNG signature (see http://www.libpng.org/pub/png/spec/1.2/PNG-Structure.html)
        constexpr unsigned char PNG_SIGNATURE[8] {137, 80, 78, 71, 13, 10, 26, 10};
        if (image.size() < 8 || std::memcmp(image.data(), PNG_SIGNATURE, 8) != 0)
            throw native_runtime_error(file.native() + NATIVE_WIDTH(" is not a valid PNG file."));
        idat_end_pos = find_idat_end();
    }

    /**
     * Adds a new chunk into the image data.
     * @param chunk A @c ChunkT object supporting a @c ChunkT::encode() method that returns a properly formatted PNG chunk
     * @details This adds the new chunk immediately following the end of the last @c IDAT chunk.
     */
    void add_chunk(const ChunkT &chunk) {
        const auto encoded_chunk = chunk.encode();
        const auto idat_end_iterator = std::next(image.begin(), idat_end_pos);
        image.insert(idat_end_iterator, encoded_chunk.begin(), encoded_chunk.end());
    }

    /**
     * Adds several new chunks into the image data, encoding their data in parallel.
     * @param chunks A vector of @c ChunkT objects supporting a @c ChunkT::encode() method that returns a properly formatted PNG chunk
     * @details This adds new chunks immediately following the end of the last @c IDAT chunk.
     */
    void add_chunk(const std::vector<ChunkT> &chunks) {
        std::vector<std::future<ImageImplementation::ManagedByteSpan>> futures;
        std::vector<ImageImplementation::ManagedByteSpan> encoded;
        futures.reserve(chunks.size());
        encoded.reserve(chunks.size());
        for (const auto &chunk : chunks)
            futures.emplace_back(std::async(&ChunkT::encode, &chunk));
        std::size_t total_size{};
        for (auto &future : futures) {
            encoded.emplace_back(future.get());
            total_size += encoded.back().size();
        }
        futures.clear();
        image.reserve(image.size() + total_size);
        for (const auto &encoded_chunk : std::ranges::reverse_view(encoded)) {
            image.insert(std::next(image.begin(), idat_end_pos), encoded_chunk.begin(), encoded_chunk.end());
        }
    }

    /**
     * Enumerates chunks in the image data of type @c ChunkT::type() that satisfy @c ChunkT::is_valid().
     * @return A vector of @c ChunkT objects constructed from the chunks in the image data for which @c ChunkT::is_valid() returns @c true
     */
    [[nodiscard]] std::vector<ChunkT> get_chunks() const {
        const auto begin = image.data();
        const auto end = &image.back() + 1;
        std::vector<ChunkT> chunks;
        for (const unsigned char *chunk = begin + 8;
             chunk < end;
             chunk = lodepng_chunk_next_const(chunk, end)) {
            if (ChunkT::is_valid(chunk))
                chunks.emplace_back(chunk);
        }
        return chunks;
    }

    /**
     * Deletes all chunks found in the image data that satisfy @c ChunkT::is_valid().
     * @return The number of deleted chunks
     * @details Valid chunks are deleted in contiguous blocks from back-to-front to reduce copy/move operations.
     */
    size_t clear_chunks() {
        const auto begin = image.data();
        const auto end = &image.back() + 1;
        std::vector<std::pair<Offset, Offset>> ranges;
        std::optional<Offset> range_begin;
        std::size_t chunk_count = 0;
        // Find contiguous ranges of chunks
        for (unsigned char *chunk = begin + 8;
             chunk < end;
             chunk = lodepng_chunk_next(chunk, end)) {
            if (ChunkT::is_valid(chunk)) {
                ++chunk_count;
                if (!range_begin.has_value())
                    // Start of a range
                    range_begin = std::distance(begin, chunk);
            } else if (range_begin.has_value()) {
                // End of a range
                const auto range_end = std::distance(begin, chunk);
                ranges.emplace_back(range_begin.value(), range_end);
                range_begin.reset();
            }
        }

        const auto iterator_begin = image.begin();

        // Delete contiguous ranges of chunks back-to-front to not invalidate offsets and to perform fewer moves
        for (const auto [_range_begin, _range_end] : std::ranges::reverse_view(ranges)) {
            image.erase(std::next(iterator_begin, _range_begin), std::next(iterator_begin, _range_end));
        }

        return chunk_count;
    }

    /**
     * Saves the current state of the image data at the path pointed to by @p out.
     * @param out The file path at which to save the image
     */
    void save(const path &out) const {
        write(out, image);
    }

protected:
    /**
     * An iterator offset relative to @c image.begin() or @c image.data().
     */
    using Offset = typename decltype(Image<ChunkT>::image)::iterator::difference_type;
    /**
     * The position after the end of the last @c IDAT chunk, for insertions.
     */
    Offset idat_end_pos;

    /**
     * Locates the end offset of the @c IDAT chunks in the image data, to initialize @c idat_end_pos.
     * @return The offset representing the location of the end of the last @c IDAT chunk in @c image
     */
    Offset find_idat_end() {
        const auto end = image.data() + image.size();
        auto chunk = lodepng_chunk_find_const(image.data() + 8, end, "IDAT");
        while (lodepng_chunk_type_equals(chunk, "IDAT"))
            chunk = lodepng_chunk_next_const(chunk, end);
        return std::distance(static_cast<const unsigned char *>(image.data()), chunk);
    }
};


#endif //PNGFUSE_IMAGE_H
