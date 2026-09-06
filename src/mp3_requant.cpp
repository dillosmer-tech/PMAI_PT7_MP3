#include "mp3_requant.h"
#include "mp3_huffman_tables.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace pt7::mp3 {

namespace {

constexpr uint8_t PRETAB[21] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 2
};

inline float pow43(int32_t val)
{
    if (val == 0) {
        return 0.0f;
    }
    const float fval = static_cast<float>(std::abs(val));
    return std::pow(fval, 4.0f / 3.0f);
}

} // anonymous namespace

void Requantizer::requantize(
    const pt7_mp3_frame_info_t& frame_info,
    const pt7_mp3_granule_info_t& gi,
    float out_xr[576])
{
    std::memset(out_xr, 0, 576 * sizeof(float));

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

    const float global_exp = 0.25f * (static_cast<float>(gi.global_gain) - 214.0f);
    const float scale_factor_mult = 0.5f * (1.0f + static_cast<float>(gi.scalefac_scale));

    // Short blocks or Mixed blocks
    if (gi.window_switching_flag != 0 && gi.block_type == 2) {
        if (gi.mixed_block_flag != 0) {
            // Mixed blocks: lower 2 subbands (first 36 lines) are long blocks
            size_t sfb = 0;
            for (size_t i = 0; i < 36; ++i) {
                while (sfb < 21 && i >= band_l[sfb + 1]) {
                    ++sfb;
                }
                const int32_t val = gi.is[i];
                if (val == 0) {
                    out_xr[i] = 0.0f;
                    continue;
                }
                const float pre = (gi.preflag != 0 && sfb < 21) ? static_cast<float>(PRETAB[sfb]) : 0.0f;
                const float sf = static_cast<float>(gi.scalefac_l[sfb]) + pre;
                const float exp = global_exp - scale_factor_mult * sf;
                const float sign = (val < 0) ? -1.0f : 1.0f;
                out_xr[i] = sign * pow43(val) * std::exp2f(exp);
            }

            // Remaining lines (36..575) are short blocks for sfb 3..11
            for (size_t s = 3; s < 12; ++s) {
                const size_t band_start = static_cast<size_t>(band_s[s]) * 3U;
                const size_t width = static_cast<size_t>(band_s[s + 1] - band_s[s]);

                for (size_t j = 0; j < width; ++j) {
                    const size_t f = static_cast<size_t>(band_s[s]) + j;
                    for (size_t w = 0; w < 3; ++w) {
                        const size_t in_idx = band_start + j * 3U + w;
                        const size_t out_idx = (f / 6U) * 18U + (f % 6U) * 3U + w;
                        if (in_idx >= 576 || out_idx >= 576) {
                            continue;
                        }

                        const int32_t val = gi.is[in_idx];
                        if (val == 0) {
                            out_xr[out_idx] = 0.0f;
                            continue;
                        }

                        const float sub_gain = 2.0f * static_cast<float>(gi.subblock_gain[w]);
                        const float sf = static_cast<float>(gi.scalefac_s[s][w]);
                        const float exp = global_exp - sub_gain - scale_factor_mult * sf;
                        const float sign = (val < 0) ? -1.0f : 1.0f;
                        out_xr[out_idx] = sign * pow43(val) * std::exp2f(exp);
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
                        const size_t in_idx = band_start + w * width + j;
                        const size_t out_idx = (f / 6U) * 18U + (f % 6U) * 3U + w;
                        if (in_idx >= 576 || out_idx >= 576) {
                            continue;
                        }

                        const int32_t val = gi.is[in_idx];
                        if (val == 0) {
                            out_xr[out_idx] = 0.0f;
                            continue;
                        }

                        const float sub_gain = 2.0f * static_cast<float>(gi.subblock_gain[w]);
                        const float sf = static_cast<float>(gi.scalefac_s[s][w]);
                        const float exp = global_exp - sub_gain - scale_factor_mult * sf;
                        const float sign = (val < 0) ? -1.0f : 1.0f;
                        out_xr[out_idx] = sign * pow43(val) * std::exp2f(exp);
                    }
                }
            }
        }
    } else {
        // Pure long blocks (block_type 0, 1, 3)
        size_t sfb = 0;
        for (size_t i = 0; i < 576; ++i) {
            while (sfb < 21 && i >= band_l[sfb + 1]) {
                ++sfb;
            }
            const int32_t val = gi.is[i];
            if (val == 0) {
                out_xr[i] = 0.0f;
                continue;
            }

            const float pre = (gi.preflag != 0 && sfb < 21) ? static_cast<float>(PRETAB[sfb]) : 0.0f;
            const float sf = static_cast<float>(gi.scalefac_l[sfb]) + pre;
            const float exp = global_exp - scale_factor_mult * sf;
            const float sign = (val < 0) ? -1.0f : 1.0f;
            out_xr[i] = sign * pow43(val) * std::exp2f(exp);
        }
    }
}

} // namespace pt7::mp3
