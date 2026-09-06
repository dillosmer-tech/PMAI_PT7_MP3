#ifndef PT7_MP3_OVERLAP_H
#define PT7_MP3_OVERLAP_H

#include "pt7_mp3.h"

namespace pt7::mp3 {

class OverlapAddEngine {
public:
    /**
     * @brief Performs Overlap/Add across granules/frames and frequency inversion for polyphase synthesis.
     *
     * @param in_z Input 32 subbands of 36 windowed samples each.
     * @param io_overlap Overlap buffer for this channel (32 subbands x 18 samples), updated in-place.
     * @param out_subbands Output reconstructed subband samples struct (18 time steps x 32 subbands).
     */
    static void process(
        const float in_z[32][36],
        float io_overlap[32][18],
        pt7_mp3_subband_samples_t& out_subbands
    );
};

} // namespace pt7::mp3

#endif // PT7_MP3_OVERLAP_H
