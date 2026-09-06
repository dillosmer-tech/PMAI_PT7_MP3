#ifndef PT7_MP3_REQUANT_H
#define PT7_MP3_REQUANT_H

#include "pt7_mp3.h"
#include <cstddef>

namespace pt7::mp3 {

class Requantizer {
public:
    /**
     * @brief Performs Layer III requantization and short-block reordering.
     *
     * Converts integer spectral values gi.is[576] into floating-point
     * dequantized spectrum out_xr[576].
     *
     * @param frame_info Frame metadata.
     * @param gi Decoded granule information with side info & scalefactors.
     * @param out_xr Destination array of 576 floats.
     */
    static void requantize(
        const pt7_mp3_frame_info_t& frame_info,
        const pt7_mp3_granule_info_t& gi,
        float out_xr[576]
    );
};

} // namespace pt7::mp3

#endif // PT7_MP3_REQUANT_H
