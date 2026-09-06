#ifndef PT7_MP3_ANTIALIAS_H
#define PT7_MP3_ANTIALIAS_H

#include "pt7_mp3.h"

namespace pt7::mp3 {

class AntialiasFilter {
public:
    /**
     * @brief Applies 8-butterfly alias reduction filter in-place to the spectrum.
     *
     * @param gi Granule information (block_type, mixed_block_flag).
     * @param xr Spectrum array of 576 floats.
     */
    static void apply(
        const pt7_mp3_granule_info_t& gi,
        float xr[576]
    );
};

} // namespace pt7::mp3

#endif // PT7_MP3_ANTIALIAS_H
