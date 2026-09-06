#ifndef PT7_MP3_BITSTREAM_H
#define PT7_MP3_BITSTREAM_H

#include <cstddef>
#include <cstdint>

namespace pt7::mp3 {

/**
 * @brief Safe, deterministic bitstream reader for MP3 bitstreams.
 *
 * Reads bitfields from 1 to 32 bits, big-endian (MSB first), with strict
 * bounds checking and zero out-of-bounds memory accesses.
 */
class BitstreamReader {
public:
    BitstreamReader();
    BitstreamReader(const uint8_t* data, size_t size_bytes);

    void reset(const uint8_t* data, size_t size_bytes);

    /**
     * @brief Reads 1 bit from the stream.
     * @return Bit value (0 or 1), or 0 if EOF/error.
     */
    uint32_t read_bit();

    /**
     * @brief Reads n bits from the stream (1 <= n <= 32).
     * @param n Number of bits to read.
     * @return Unsigned 32-bit value containing the bits (MSB first), or 0 if EOF/error.
     */
    uint32_t read_bits(size_t n);

    /**
     * @brief Peeks n bits from the stream without advancing the position.
     * @param n Number of bits to peek (1 <= n <= 32).
     * @return Unsigned 32-bit value containing the bits, or 0 if EOF/error.
     */
    uint32_t peek_bits(size_t n) const;

    /**
     * @brief Advances stream position by n bits.
     * @param n Number of bits to advance.
     * @return true if advanced within bounds, false if past EOF.
     */
    bool skip_bits(size_t n);

    /**
     * @brief Advances to the next byte boundary if not currently aligned.
     */
    void align_to_byte();

    [[nodiscard]] size_t bits_remaining() const;
    [[nodiscard]] size_t bytes_remaining() const;
    [[nodiscard]] size_t bit_position() const { return m_bit_pos; }
    [[nodiscard]] size_t byte_position() const { return m_bit_pos / 8; }
    [[nodiscard]] size_t total_bits() const { return m_total_bits; }
    [[nodiscard]] size_t total_bytes() const { return m_size_bytes; }
    [[nodiscard]] bool is_eof() const { return m_bit_pos >= m_total_bits; }
    [[nodiscard]] bool has_error() const { return m_error; }

private:
    const uint8_t* m_data{nullptr};
    size_t m_size_bytes{0};
    size_t m_total_bits{0};
    size_t m_bit_pos{0};
    mutable bool m_error{false};
};

} // namespace pt7::mp3

#endif // PT7_MP3_BITSTREAM_H
