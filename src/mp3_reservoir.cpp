#include "mp3_reservoir.h"
#include <algorithm>

namespace pt7::mp3 {

BitReservoir::BitReservoir()
{
    m_buffer.reserve(MAX_RESERVOIR_SIZE);
}

void BitReservoir::reset()
{
    m_buffer.clear();
}

bool BitReservoir::can_assemble(uint32_t main_data_begin) const
{
    return m_buffer.size() >= main_data_begin;
}

pt7_mp3_status_t BitReservoir::assemble(
    uint32_t main_data_begin,
    const uint8_t* frame_main_data,
    size_t frame_main_data_len,
    std::vector<uint8_t>& out_assembled)
{
    if (frame_main_data == nullptr && frame_main_data_len > 0) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    if (m_buffer.size() < main_data_begin) {
        // Reservoir underflow: accumulate current frame data for subsequent frames
        if (frame_main_data != nullptr && frame_main_data_len > 0) {
            m_buffer.insert(m_buffer.end(), frame_main_data, frame_main_data + frame_main_data_len);
            if (m_buffer.size() > MAX_RESERVOIR_SIZE) {
                const size_t discard = m_buffer.size() - MAX_RESERVOIR_SIZE;
                m_buffer.erase(m_buffer.begin(), m_buffer.begin() + discard);
            }
        }
        return PT7_MP3_ERR_RESERVOIR_UNDERFLOW;
    }

    out_assembled.clear();
    out_assembled.reserve(main_data_begin + frame_main_data_len);

    if (main_data_begin > 0) {
        const size_t offset = m_buffer.size() - main_data_begin;
        out_assembled.insert(out_assembled.end(), m_buffer.begin() + offset, m_buffer.end());
    }

    if (frame_main_data != nullptr && frame_main_data_len > 0) {
        out_assembled.insert(out_assembled.end(), frame_main_data, frame_main_data + frame_main_data_len);
    }

    return PT7_MP3_OK;
}

void BitReservoir::update(
    const std::vector<uint8_t>& assembled,
    size_t bits_consumed)
{
    const size_t bytes_consumed = (bits_consumed + 7) / 8;
    if (bytes_consumed < assembled.size()) {
        m_buffer.assign(assembled.begin() + bytes_consumed, assembled.end());
    } else {
        m_buffer.clear();
    }

    if (m_buffer.size() > MAX_RESERVOIR_SIZE) {
        const size_t discard = m_buffer.size() - MAX_RESERVOIR_SIZE;
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + discard);
    }
}

} // namespace pt7::mp3
