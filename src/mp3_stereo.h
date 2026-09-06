#ifndef PT7_MP3_STEREO_H
#define PT7_MP3_STEREO_H

#include "pt7_mp3.h"

namespace pt7::mp3 {

class StereoProcessor {
public:
    /**
     * @brief Applies MS and Intensity stereo decoding in-place to dequantized spectrum.
     *
     * @param frame_info Frame metadata (channels, channel_mode, mode_extension).
     * @param gi_l Left / Mid channel granule info.
     * @param gi_r Right / Side channel granule info.
     * @param xr Spectrum array xr[2][576].
     */
    static void process(
        const pt7_mp3_frame_info_t& frame_info,
        const pt7_mp3_granule_info_t& gi_l,
        const pt7_mp3_granule_info_t& gi_r,
        float xr[2][576]
    );
};

} // namespace pt7::mp3

#endif // PT7_MP3_STEREO_H
