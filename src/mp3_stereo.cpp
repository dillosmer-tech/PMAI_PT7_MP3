#include "mp3_stereo.h"
#include "mp3_huffman_tables.h"
#include <cmath>
#include <algorithm>

namespace pt7::mp3 {

namespace {

constexpr float INV_SQRT2 = 0.7071067811865475f;

// Standard MPEG-1 intensity stereo ratio table
constexpr float IS_RATIO_L[7] = {
    1.0f,
    0.788675135f,
    0.633974596f,
    0.5f,
    0.366025404f,
    0.211324865f,
    0.0f
};

constexpr float IS_RATIO_R[7] = {
    0.0f,
    0.211324865f,
    0.366025404f,
    0.5f,
    0.633974596f,
    0.788675135f,
    1.0f
};

} // anonymous namespace

void StereoProcessor::process(
    const pt7_mp3_frame_info_t& frame_info,
    const pt7_mp3_granule_info_t& gi_l,
    const pt7_mp3_granule_info_t& gi_r,
    float xr[2][576])
{
    if (frame_info.channels < 2 || frame_info.channel_mode != PT7_MP3_CHANNEL_JOINT_STEREO) {
        return;
    }

    const bool ms_stereo = (frame_info.mode_extension & 0x02U) != 0;
    const bool is_stereo = (frame_info.mode_extension & 0x01U) != 0;

    if (!ms_stereo && !is_stereo) {
        return;
    }

    // Pure MS stereo (no Intensity stereo)
    if (ms_stereo && !is_stereo) {
        for (size_t i = 0; i < 576; ++i) {
            const float m = xr[0][i];
            const float s = xr[1][i];
            xr[0][i] = (m + s) * INV_SQRT2;
            xr[1][i] = (m - s) * INV_SQRT2;
        }
        return;
    }

    // Intensity stereo (possibly with MS stereo below IS boundary)
    const uint16_t* band_l = BAND_INDEX_L_44;
    const uint16_t* band_s = BAND_INDEX_S_44;
    switch (frame_info.sample_rate_hz) {
        case 44100:
            band_l = BAND_INDEX_L_44;
            band_s = BAND_INDEX_S_44;
            break;
        case 48000:
            band_l = BAND_INDEX_L_48;
            band_s = BAND_INDEX_S_48;
            break;
        case 32000:
            band_l = BAND_INDEX_L_32;
            band_s = BAND_INDEX_S_32;
            break;
        case 22050: case 11025:
            band_l = BAND_INDEX_L_22;
            band_s = BAND_INDEX_S_22;
            break;
        case 24000: case 12000:
            band_l = BAND_INDEX_L_24;
            band_s = BAND_INDEX_S_24;
            break;
        case 16000: case 8000:
            band_l = BAND_INDEX_L_22;
            band_s = BAND_INDEX_S_22;
            break;
        default:
            band_l = BAND_INDEX_L_44;
            band_s = BAND_INDEX_S_44;
            break;
    }

    const size_t is_boundary = gi_r.zero_start;

    // Check for short blocks or mixed blocks
    if (gi_r.window_switching_flag != 0 && gi_r.block_type == 2) {
        if (gi_r.mixed_block_flag != 0) {
            // Mixed blocks: lower 2 subbands (first 36 lines) are long blocks
            size_t sfb = 0;
            for (size_t i = 0; i < 36; ++i) {
                while (sfb < 21 && i >= band_l[sfb + 1]) {
                    ++sfb;
                }

                if (i < is_boundary) {
                    if (ms_stereo) {
                        const float m = xr[0][i];
                        const float s = xr[1][i];
                        xr[0][i] = (m + s) * INV_SQRT2;
                        xr[1][i] = (m - s) * INV_SQRT2;
                    }
                } else {
                    const uint32_t is_pos = (sfb < 21) ? gi_r.scalefac_l[sfb] : 7U;
                    if (is_pos < 7U) {
                        const float m = xr[0][i];
                        xr[0][i] = m * IS_RATIO_L[is_pos];
                        xr[1][i] = m * IS_RATIO_R[is_pos];
                    }
                    // is_pos == 7: band does not use intensity stereo, leave intact
                }
            }

            // Remaining lines (36..575) are short blocks for sfb 3..11
            for (size_t s = 3; s < 12; ++s) {
                const size_t band_start = static_cast<size_t>(band_s[s]) * 3U;
                const size_t width = static_cast<size_t>(band_s[s + 1] - band_s[s]);

                for (size_t j = 0; j < width; ++j) {
                    const size_t f = static_cast<size_t>(band_s[s]) + j;
                    for (size_t w = 0; w < 3; ++w) {
                        const size_t in_idx = band_start + j * 3U + w;
                        const size_t out_idx = (f / 6U) * 18U + w * 6U + (f % 6U);
                        if (in_idx >= 576 || out_idx >= 576) {
                            continue;
                        }

                        if (in_idx < is_boundary) {
                            if (ms_stereo) {
                                const float m = xr[0][out_idx];
                                const float s = xr[1][out_idx];
                                xr[0][out_idx] = (m + s) * INV_SQRT2;
                                xr[1][out_idx] = (m - s) * INV_SQRT2;
                            }
                        } else {
                            const uint32_t is_pos = (s < 12) ? gi_r.scalefac_s[s][w] : 7U;
                            if (is_pos < 7U) {
                                const float m = xr[0][out_idx];
                                xr[0][out_idx] = m * IS_RATIO_L[is_pos];
                                xr[1][out_idx] = m * IS_RATIO_R[is_pos];
                            }
                            // is_pos == 7: leave intact
                        }
                    }
                }
            }
        } else {
            // Pure short blocks
            for (size_t s = 0; s < 12; ++s) {
                const size_t band_start = static_cast<size_t>(band_s[s]) * 3U;
                const size_t width = static_cast<size_t>(band_s[s + 1] - band_s[s]);

                for (size_t j = 0; j < width; ++j) {
                    const size_t f = static_cast<size_t>(band_s[s]) + j;
                    for (size_t w = 0; w < 3; ++w) {
                        const size_t in_idx = band_start + j * 3U + w;
                        const size_t out_idx = (f / 6U) * 18U + w * 6U + (f % 6U);
                        if (in_idx >= 576 || out_idx >= 576) {
                            continue;
                        }

                        if (in_idx < is_boundary) {
                            if (ms_stereo) {
                                const float m = xr[0][out_idx];
                                const float s = xr[1][out_idx];
                                xr[0][out_idx] = (m + s) * INV_SQRT2;
                                xr[1][out_idx] = (m - s) * INV_SQRT2;
                            }
                        } else {
                            const uint32_t is_pos = (s < 12) ? gi_r.scalefac_s[s][w] : 7U;
                            if (is_pos < 7U) {
                                const float m = xr[0][out_idx];
                                xr[0][out_idx] = m * IS_RATIO_L[is_pos];
                                xr[1][out_idx] = m * IS_RATIO_R[is_pos];
                            }
                            // is_pos == 7: leave intact
                        }
                    }
                }
            }
        }
    } else {
        // Pure long blocks
        size_t sfb = 0;
        for (size_t i = 0; i < 576; ++i) {
            while (sfb < 21 && i >= band_l[sfb + 1]) {
                ++sfb;
            }

            if (i < is_boundary) {
                if (ms_stereo) {
                    const float m = xr[0][i];
                    const float s = xr[1][i];
                    xr[0][i] = (m + s) * INV_SQRT2;
                    xr[1][i] = (m - s) * INV_SQRT2;
                }
            } else {
                // In IS region
                const uint32_t is_pos = (sfb < 21) ? gi_r.scalefac_l[sfb] : 7U;
                if (is_pos < 7U) {
                    const float m = xr[0][i];
                    xr[0][i] = m * IS_RATIO_L[is_pos];
                    xr[1][i] = m * IS_RATIO_R[is_pos];
                }
                // is_pos == 7: leave intact
            }
        }
    }

    (void)gi_l;
}

} // namespace pt7::mp3
