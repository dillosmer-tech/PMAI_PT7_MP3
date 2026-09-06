#include "mp3_overlap.h"

namespace pt7::mp3 {

void OverlapAddEngine::process(
    const float in_z[32][36],
    float io_overlap[32][18],
    pt7_mp3_subband_samples_t& out_subbands)
{
    // The IMDCT (ImdctEngine::transform) now applies the rolling overlap
    // internally and stores the new overlap state in in_z[18..35].
    // We only need to copy the output and apply frequency inversion.
    for (size_t sb = 0; sb < 32; ++sb) {
        for (size_t k = 0; k < 18; ++k) {
            float s = in_z[sb][k];

            // Store new overlap state from in_z[18..35]
            io_overlap[sb][k] = in_z[sb][k + 18];

            // Frequency inversion for odd subbands: negate odd time samples
            if ((sb & 1U) != 0 && (k & 1U) != 0) {
                s = -s;
            }

            out_subbands.samples[k][sb] = s;
        }
    }
}

} // namespace pt7::mp3
