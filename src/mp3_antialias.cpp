#include "mp3_antialias.h"
#include <cstddef>

namespace pt7::mp3 {

namespace {

// Standard ISO/IEC 11172-3 Table B.9 butterfly coefficients
constexpr float CI[8] = {
    0.857492926f,
    0.881741997f,
    0.949628649f,
    0.983314592f,
    0.995517816f,
    0.999160558f,
    0.999899195f,
    0.999993155f
};

constexpr float SI[8] = {
    -0.514495755f,
    -0.471731969f,
    -0.313377454f,
    -0.181913199f,
    -0.094574193f,
    -0.040965583f,
    -0.014198569f,
    -0.003699975f
};

} // anonymous namespace

void AntialiasFilter::apply(
    const pt7_mp3_granule_info_t& gi,
    float xr[576])
{
    // Short blocks without mixed_block: no antialias
    if (gi.window_switching_flag != 0 && gi.block_type == 2 && gi.mixed_block_flag == 0) {
        return;
    }

    // For mixed blocks: only the first subband boundary (sb = 0) is antialiased
    // For long blocks: all 31 boundaries (sb = 0..30) are antialiased
    const size_t num_boundaries = (gi.window_switching_flag != 0 && gi.block_type == 2) ? 1U : 31U;

    for (size_t sb = 0; sb < num_boundaries; ++sb) {
        const size_t base_a = sb * 18U + 17U;
        const size_t base_b = (sb + 1U) * 18U;

        for (size_t i = 0; i < 8U; ++i) {
            const size_t idx_a = base_a - i;
            const size_t idx_b = base_b + i;

            const float a = xr[idx_a];
            const float b = xr[idx_b];

            xr[idx_a] = a * CI[i] - b * SI[i];
            xr[idx_b] = b * CI[i] + a * SI[i];
        }
    }
}

} // namespace pt7::mp3
