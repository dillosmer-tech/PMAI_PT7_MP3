#include "mp3_imdct.h"
#include <cmath>
#include <cstring>

namespace pt7::mp3 {

namespace {

// 9-point DCT-III
static void dct3_9(float *y)
{
    float s0, s1, s2, s3, s4, s5, s6, s7, s8, t0, t2, t4;

    s0 = y[0]; s2 = y[2]; s4 = y[4]; s6 = y[6]; s8 = y[8];
    t0 = s0 + s6*0.5f;
    s0 -= s6;
    t4 = (s4 + s2)*0.93969262f;
    t2 = (s8 + s2)*0.76604444f;
    s6 = (s4 - s8)*0.17364818f;
    s4 += s8 - s2;

    s2 = s0 - s4*0.5f;
    y[4] = s4 + s0;
    s8 = t0 - t2 + s6;
    s0 = t0 - t4 + t2;
    s4 = t0 + t4 - s6;

    s1 = y[1]; s3 = y[3]; s5 = y[5]; s7 = y[7];

    s3 *= 0.86602540f;
    t0 = (s5 + s1)*0.98480775f;
    t4 = (s5 - s7)*0.34202014f;
    t2 = (s1 + s7)*0.64278761f;
    s1 = (s1 - s5 - s7)*0.86602540f;

    s5 = t0 - s3 - t2;
    s7 = t4 - s3 - t0;
    s3 = t4 + s3 - t2;

    y[0] = s4 - s7;
    y[1] = s2 + s1;
    y[2] = s0 - s3;
    y[3] = s8 + s5;
    y[5] = s8 - s5;
    y[6] = s0 + s3;
    y[7] = s2 - s1;
    y[8] = s4 + s7;
}

// Twiddle factors for 36-point IMDCT
static const float g_twid9[18] = {
    0.73727734f, 0.79335334f, 0.84339145f, 0.88701083f, 0.92387953f, 0.95371695f,
    0.97629601f, 0.99144486f, 0.99904822f,
    0.67559021f, 0.60876143f, 0.53729961f, 0.46174861f, 0.38268343f, 0.30070580f,
    0.21643961f, 0.13052619f, 0.04361938f
};

// MDCT windows for long blocks
// Index 0: normal long window, Index 1: stop window
static const float g_mdct_window[2][18] = {
    { 0.99904822f, 0.99144486f, 0.97629601f, 0.95371695f, 0.92387953f, 0.88701083f,
      0.84339145f, 0.79335334f, 0.73727734f,
      0.04361938f, 0.13052619f, 0.21643961f, 0.30070580f, 0.38268343f, 0.46174861f,
      0.53729961f, 0.60876143f, 0.67559021f },
    { 1, 1, 1, 1, 1, 1, 0.99144486f, 0.92387953f, 0.79335334f,
      0, 0, 0, 0, 0, 0, 0.13052619f, 0.38268343f, 0.60876143f }
};

// 36-point IMDCT with rolling 9-value overlap
// grbuf: 18 input coefficients, produces 18 output samples
// overlap: 9 values read and updated
// window: 18 window values
static void imdct36(float *grbuf, float *overlap, const float *window)
{
    float co[9], si[9];
    int i;

    co[0] = -grbuf[0];
    si[0] = grbuf[17];
    for (i = 0; i < 4; i++) {
        si[8 - 2*i] =   grbuf[4*i + 1] - grbuf[4*i + 2];
        co[1 + 2*i] =   grbuf[4*i + 1] + grbuf[4*i + 2];
        si[7 - 2*i] =   grbuf[4*i + 4] - grbuf[4*i + 3];
        co[2 + 2*i] = -(grbuf[4*i + 3] + grbuf[4*i + 4]);
    }
    dct3_9(co);
    dct3_9(si);

    si[1] = -si[1];
    si[3] = -si[3];
    si[5] = -si[5];
    si[7] = -si[7];

    for (i = 0; i < 9; i++) {
        float ovl  = overlap[i];
        float sum  = co[i]*g_twid9[9 + i] + si[i]*g_twid9[0 + i];
        overlap[i] = co[i]*g_twid9[0 + i] - si[i]*g_twid9[9 + i];
        grbuf[i]      = ovl*window[0 + i] - sum*window[9 + i];
        grbuf[17 - i] = ovl*window[9 + i] + sum*window[0 + i];
    }
}

// 3-point DCT-III
static void idct3(float x0, float x1, float x2, float *dst)
{
    float m1 = x1 * 0.86602540f;
    float a1 = x0 - x2 * 0.5f;
    dst[1] = x0 + x2;
    dst[0] = a1 + m1;
    dst[2] = a1 - m1;
}

// Twiddle factors for 12-point IMDCT
static const float g_twid3[6] = {
    0.79335334f, 0.92387953f, 0.99144486f,
    0.60876143f, 0.38268343f, 0.13052619f
};

// 12-point IMDCT with rolling 3-value overlap.
// x: pointer into 18-value buffer; accesses x[0], x[3], x[6], x[9], x[12], x[15] (stride 3).
// dst: 6 output samples. overlap[0..2] read and updated.
static void imdct12_short(const float *x, float *dst, float *overlap)
{
    float co[3], si[3];

    idct3(-x[0], x[6] + x[3], x[12] + x[9], co);
    idct3( x[15], x[12] - x[9], x[6] - x[3], si);
    si[1] = -si[1];

    for (int i = 0; i < 3; i++) {
        float ovl  = overlap[i];
        float sum  = co[i]*g_twid3[3 + i] + si[i]*g_twid3[0 + i];
        overlap[i] = co[i]*g_twid3[0 + i] - si[i]*g_twid3[3 + i];
        dst[i]     = ovl*g_twid3[2 - i] - sum*g_twid3[5 - i];
        dst[5 - i] = ovl*g_twid3[5 - i] + sum*g_twid3[2 - i];
    }
}

} // anonymous namespace

void ImdctEngine::transform(
    const pt7_mp3_granule_info_t& gi,
    const float in_xr[576],
    float io_overlap[32][18],
    float out_z[32][36])
{
    std::memset(out_z, 0, sizeof(float) * 32 * 36);

    const bool is_short = (gi.window_switching_flag != 0 && gi.block_type == 2);
    const bool is_mixed = (is_short && gi.mixed_block_flag != 0);

    // Determine the number of long-block subbands for mixed blocks
    const unsigned n_long_bands = (is_mixed) ? 2u : 0u;

    // Select window for long blocks:
    // block_type 0 (normal) or 1 (start) → g_mdct_window[0]
    // block_type 3 (stop)                → g_mdct_window[1]
    const float *long_window = g_mdct_window[(gi.block_type & 3u) == 3u ? 1 : 0];

    for (size_t sb = 0; sb < 32; ++sb) {
        float *xr_sb = const_cast<float *>(in_xr + sb * 18U);
        float *ovl = io_overlap[sb];

        if (is_short && sb >= n_long_bands) {
            // Short blocks: rolling-overlap IMDCT.
            //
            // PT7 stores short-block coefficients contiguously:
            //   xr_sb[0..5]  = window 0
            //   xr_sb[6..11] = window 1
            //   xr_sb[12..17] = window 2
            //
            // imdct12_short accesses x[0], x[3], x[6], x[9], x[12], x[15] (stride 3).
            // Three calls with tmp, tmp+1, tmp+2 process the 3 windows.
            float tmp[18];
            std::memcpy(tmp, xr_sb, 18 * sizeof(float));

            float working[3];
            working[0] = ovl[6];
            working[1] = ovl[7];
            working[2] = ovl[8];

            // Emit 6 samples from previous frame's overlap
            for (size_t k = 0; k < 6; ++k)
                out_z[sb][k] = ovl[k];

            // Three 12-point IMDCTs with rolling overlap
            imdct12_short(tmp,     out_z[sb] + 6,  working);
            imdct12_short(tmp + 1, out_z[sb] + 12, working);
            imdct12_short(tmp + 2, out_z[sb] + 18, working);

            // Store working state for next frame
            out_z[sb][24] = working[0];
            out_z[sb][25] = working[1];
            out_z[sb][26] = working[2];
        } else {
            // Long blocks (or lower subbands of mixed blocks)
            // imdct36 modifies grbuf in-place: input coefficients → output samples
            float tmp[18];
            std::memcpy(tmp, xr_sb, 18 * sizeof(float));
            imdct36(tmp, ovl, long_window);

            for (size_t k = 0; k < 18; ++k)
                out_z[sb][k] = tmp[k];

            // Store new overlap state (9 values)
            for (size_t k = 0; k < 9; ++k)
                out_z[sb][18 + k] = ovl[k];
        }

        // Mark overlap as consumed
        for (size_t k = 0; k < 18; ++k)
            io_overlap[sb][k] = 0.0f;
    }
}

} // namespace pt7::mp3
