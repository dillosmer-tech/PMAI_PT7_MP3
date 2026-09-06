#include "mp3_bitstream.h"

namespace pt7::mp3 {

BitstreamReader::BitstreamReader()
    : m_data(nullptr)
    , m_size_bytes(0)
    , m_total_bits(0)
    , m_bit_pos(0)
    , m_error(false)
{
}

BitstreamReader::BitstreamReader(const uint8_t* data, size_t size_bytes)
{
    reset(data, size_bytes);
}

void BitstreamReader::reset(const uint8_t* data, size_t size_bytes)
{
    m_data = data;
    m_size_bytes = (data != nullptr) ? size_bytes : 0;
    m_total_bits = m_size_bytes * 8;
    m_bit_pos = 0;
    m_error = (data == nullptr && size_bytes > 0);
}

uint32_t BitstreamReader::read_bit()
{
    return read_bits(1);
}

uint32_t BitstreamReader::read_bits(size_t n)
{
    if (n == 0) {
        return 0;
    }
    if (n > 32 || m_bit_pos + n > m_total_bits || m_data == nullptr) {
        m_error = true;
        return 0;
    }

    uint32_t value = 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t byte_idx = (m_bit_pos + i) / 8;
        const size_t bit_idx = 7 - ((m_bit_pos + i) % 8);
        const uint32_t bit = static_cast<uint32_t>((m_data[byte_idx] >> bit_idx) & 1U);
        value = (value << 1) | bit;
    }

    m_bit_pos += n;
    return value;
}

uint32_t BitstreamReader::peek_bits(size_t n) const
{
    if (n == 0) {
        return 0;
    }
    if (n > 32 || m_bit_pos + n > m_total_bits || m_data == nullptr) {
        m_error = true;
        return 0;
    }

    uint32_t value = 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t byte_idx = (m_bit_pos + i) / 8;
        const size_t bit_idx = 7 - ((m_bit_pos + i) % 8);
        const uint32_t bit = static_cast<uint32_t>((m_data[byte_idx] >> bit_idx) & 1U);
        value = (value << 1) | bit;
    }

    return value;
}

bool BitstreamReader::skip_bits(size_t n)
{
    if (m_bit_pos + n > m_total_bits) {
        m_bit_pos = m_total_bits;
        m_error = true;
        return false;
    }
    m_bit_pos += n;
    return true;
}

void BitstreamReader::align_to_byte()
{
    const size_t rem = m_bit_pos % 8;
    if (rem != 0) {
        skip_bits(8 - rem);
    }
}

size_t BitstreamReader::bits_remaining() const
{
    if (m_bit_pos >= m_total_bits) {
        return 0;
    }
    return m_total_bits - m_bit_pos;
}

size_t BitstreamReader::bytes_remaining() const
{
    return bits_remaining() / 8;
}

} // namespace pt7::mp3
