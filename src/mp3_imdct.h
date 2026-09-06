#ifndef PT7_MP3_IMDCT_H
#define PT7_MP3_IMDCT_H

#include "pt7_mp3.h"
#include <cstddef>

namespace pt7::mp3 {

class ImdctEngine {
public:
    /**
     * @brief Transforms 576 frequency coefficients into 32 subbands of 36 windowed samples each.
     *
     * For short blocks, the rolling overlap is applied internally: out_z[0..17]
     * contain the final subband samples (overlap already added) and out_z[18..35]
     * contain the new overlap state.  io_overlap is consumed and zeroed.
     *
     * For long blocks, out_z[0..35] is the raw windowed IMDCT; the caller must
     * still run OverlapAddEngine::process().
     *
     * @param gi Granule information (window_switching_flag, block_type, mixed_block_flag).
     * @param in_xr Antialiased spectrum of 576 floats (32 subbands x 18 lines).
     * @param io_overlap Overlap buffer (32 x 18).  Read and zeroed for short blocks; untouched for long blocks.
     * @param out_z Output 32 subbands x 36 windowed time samples.
     */
    static void transform(
        const pt7_mp3_granule_info_t& gi,
        const float in_xr[576],
        float io_overlap[32][18],
        float out_z[32][36]
    );
};

} // namespace pt7::mp3

#endif // PT7_MP3_IMDCT_H
