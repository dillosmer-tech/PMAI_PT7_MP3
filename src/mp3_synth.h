#ifndef PT7_MP3_SYNTH_H
#define PT7_MP3_SYNTH_H

#include "pt7_mp3.h"
#include <cstddef>
#include <cstdint>

namespace pt7::mp3 {

/**
 * @brief Persistent state for the polyphase synthesis filterbank.
 * Holds the 1024-element FIFO buffer per audio channel.
 * Must persist across granules and frames.
 */
struct SynthesisState {
    float fifo[2][1024]{};

    void reset();
};

/**
 * @brief Polyphase Synthesis Filterbank Engine (ISO/IEC 11172-3 Layer III).
 *
 * Converts 32 subband frequency samples (18 time steps per granule)
 * into 576 time-domain PCM samples per channel.
 */
class SynthesisFilterbank {
public:
    /**
     * @brief Processes one granule of subband samples for a single channel.
     *
     * @param in_subbands 18 time samples x 32 subband inputs from Task 3.
     * @param io_fifo 1024-sample persistent FIFO buffer for this channel.
     * @param out_pcm Array of 576 output time-domain float samples.
     */
    static void process_granule(
        const pt7_mp3_subband_samples_t& in_subbands,
        float io_fifo[1024],
        float out_pcm[576]
    );

    /**
     * @brief Processes a single subband time-step (32 subband values -> 32 PCM samples).
     *
     * @param s 32 subband samples for current time step.
     * @param io_fifo 1024-sample persistent FIFO buffer for this channel.
     * @param out_pcm_32 32 output time-domain float samples.
     */
    static void process_subband_step(
        const float s[32],
        float io_fifo[1024],
        float out_pcm_32[32]
    );
};

} // namespace pt7::mp3

#endif // PT7_MP3_SYNTH_H
