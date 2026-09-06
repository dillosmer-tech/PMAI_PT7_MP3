#ifndef PT7_MP3_RESERVOIR_H
#define PT7_MP3_RESERVOIR_H

#include "pt7_mp3.h"
#include <vector>
#include <cstdint>
#include <cstddef>

namespace pt7::mp3 {

/**
 * @brief Manages Layer III bit reservoir history and continuity across frames.
 */
class BitReservoir {
public:
    BitReservoir();

    void reset();

    /**
     * @brief Checks if reservoir has at least main_data_begin bytes available.
     */
    [[nodiscard]] bool can_assemble(uint32_t main_data_begin) const;

    /**
     * @brief Assembles the main data buffer for the current frame.
     */
    pt7_mp3_status_t assemble(
        uint32_t main_data_begin,
        const uint8_t* frame_main_data,
        size_t frame_main_data_len,
        std::vector<uint8_t>& out_assembled
    );

    /**
     * @brief Updates reservoir buffer after granules decoding.
     */
    void update(
        const std::vector<uint8_t>& assembled,
        size_t bits_consumed
    );

    [[nodiscard]] size_t size() const { return m_buffer.size(); }

private:
    std::vector<uint8_t> m_buffer;
    static constexpr size_t MAX_RESERVOIR_SIZE = 2048;
};

} // namespace pt7::mp3

#endif // PT7_MP3_RESERVOIR_H
