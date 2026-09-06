#ifndef PT7_MP3_VBR_H
#define PT7_MP3_VBR_H

#include "pt7_mp3.h"
#include <cstddef>
#include <cstdint>

namespace pt7::mp3 {

/**
 * @brief Standalone parser for VBR/CBR headers (Xing, Info, VBRI) and LAME gapless metadata.
 */
class VbrParser {
public:
    /**
     * @brief Parses Xing or Info header inside the first audio frame payload.
     *
     * @param frame_data Pointer to start of MP3 frame header.
     * @param frame_size Available frame length in bytes.
     * @param frame_info Information of the first frame.
     * @param out_vbr Populated with parsed VBR / gapless metadata.
     * @return PT7_MP3_OK if Xing/Info header found and parsed,
     *         PT7_MP3_ERR_SYNC_NOT_FOUND if neither Xing nor Info header is present,
     *         PT7_MP3_NEED_MORE_DATA if frame buffer is truncated.
     */
    static pt7_mp3_status_t parse_xing(
        const uint8_t* frame_data,
        size_t frame_size,
        const pt7_mp3_frame_info_t& frame_info,
        pt7_mp3_vbr_info_t& out_vbr
    );

    /**
     * @brief Parses Fraunhofer VBRI header inside the first audio frame payload.
     *
     * @param frame_data Pointer to start of MP3 frame header.
     * @param frame_size Available frame length in bytes.
     * @param frame_info Information of the first frame.
     * @param out_vbr Populated with parsed VBRI metadata.
     * @return PT7_MP3_OK if VBRI header found and parsed,
     *         PT7_MP3_ERR_SYNC_NOT_FOUND if VBRI header is not present.
     */
    static pt7_mp3_status_t parse_vbri(
        const uint8_t* frame_data,
        size_t frame_size,
        const pt7_mp3_frame_info_t& frame_info,
        pt7_mp3_vbr_info_t& out_vbr
    );

    /**
     * @brief Detects and parses either Xing, Info, or VBRI in the first frame.
     */
    static pt7_mp3_status_t detect(
        const uint8_t* frame_data,
        size_t frame_size,
        const pt7_mp3_frame_info_t& frame_info,
        pt7_mp3_vbr_info_t& out_vbr
    );
};

} // namespace pt7::mp3

#endif // PT7_MP3_VBR_H
