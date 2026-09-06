#ifndef PT7_MP3_FRAME_H
#define PT7_MP3_FRAME_H

#include "pt7_mp3.h"
#include <cstddef>
#include <cstdint>

namespace pt7::mp3 {

/**
 * @brief Internal representation and validation of MP3 frame headers.
 */
class FrameParser {
public:
    /**
     * @brief Parses and validates an MP3 frame header at the beginning of the buffer.
     *
     * @param buffer Input bytes.
     * @param buffer_size Number of bytes in buffer.
     * @param out_info Destination for parsed frame information.
     * @return Status code (PT7_MP3_OK, PT7_MP3_NEED_MORE_DATA, error codes).
     */
    static pt7_mp3_status_t parse_header(
        const uint8_t* buffer,
        size_t buffer_size,
        pt7_mp3_frame_info_t* out_info
    );

    /**
     * @brief Scans buffer for the first valid MP3 frame.
     *
     * @param buffer Input bytes.
     * @param buffer_size Number of bytes in buffer.
     * @param out_offset Destination for the frame start offset.
     * @param out_info Optional destination for parsed frame information.
     * @return Status code.
     */
    static pt7_mp3_status_t scan_frame(
        const uint8_t* buffer,
        size_t buffer_size,
        size_t* out_offset,
        pt7_mp3_frame_info_t* out_info
    );

    /**
     * @brief Raw 32-bit header validation without buffer length check.
     */
    static pt7_mp3_status_t decode_header_word(
        uint32_t header_word,
        pt7_mp3_frame_info_t* out_info
    );
};

} // namespace pt7::mp3

#endif // PT7_MP3_FRAME_H
