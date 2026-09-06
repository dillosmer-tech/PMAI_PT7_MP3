#include "mp3_scalefactors.h"
#include "mp3_huffman_tables.h"
#include <cstring>

namespace pt7::mp3 {

pt7_mp3_status_t ScalefactorDecoder::decode(
    BitstreamReader& bs,
    const pt7_mp3_frame_info_t& frame_info,
    const SideInfo& side_info,
    uint32_t gr,
    uint32_t ch,
    pt7_mp3_granule_info_t& gi,
    size_t& out_bits_read)
{
    const size_t start_bit_pos = bs.bit_position();
    std::memset(gi.scalefac_l, 0, sizeof(gi.scalefac_l));
    std::memset(gi.scalefac_s, 0, sizeof(gi.scalefac_s));

    if (frame_info.version == PT7_MP3_VERSION_MPEG1) {
        const uint32_t sc = gi.scalefac_compress & 0x0FU;
        const uint8_t slen1 = SLEN_TABLE[sc][0];
        const uint8_t slen2 = SLEN_TABLE[sc][1];

        // Short or mixed blocks
        if (gi.block_type == 2 && gi.window_switching_flag != 0) {
            if (gi.mixed_block_flag != 0) {
                // Mixed blocks: lower 8 sfb are long
                for (uint32_t sfb = 0; sfb < 8; ++sfb) {
                    gi.scalefac_l[sfb] = (slen1 > 0) ? bs.read_bits(slen1) : 0;
                }
                for (uint32_t sfb = 3; sfb < 6; ++sfb) {
                    for (uint32_t w = 0; w < 3; ++w) {
                        gi.scalefac_s[sfb][w] = (slen1 > 0) ? bs.read_bits(slen1) : 0;
                    }
                }
            } else {
                // Pure short blocks
                for (uint32_t sfb = 0; sfb < 6; ++sfb) {
                    for (uint32_t w = 0; w < 3; ++w) {
                        gi.scalefac_s[sfb][w] = (slen1 > 0) ? bs.read_bits(slen1) : 0;
                    }
                }
            }
            for (uint32_t sfb = 6; sfb < 12; ++sfb) {
                for (uint32_t w = 0; w < 3; ++w) {
                    gi.scalefac_s[sfb][w] = (slen2 > 0) ? bs.read_bits(slen2) : 0;
                }
            }
        } else {
            // Long blocks
            if (gr == 0) {
                for (uint32_t sfb = 0; sfb < 11; ++sfb) {
                    gi.scalefac_l[sfb] = (slen1 > 0) ? bs.read_bits(slen1) : 0;
                }
                for (uint32_t sfb = 11; sfb < 21; ++sfb) {
                    gi.scalefac_l[sfb] = (slen2 > 0) ? bs.read_bits(slen2) : 0;
                }
            } else {
                // Granule 1: check scfsi bands
                // Band 0: sfb 0..5
                for (uint32_t sfb = 0; sfb < 6; ++sfb) {
                    if (side_info.scfsi[ch][0] != 0) {
                        gi.scalefac_l[sfb] = side_info.granules[0][ch].scalefac_l[sfb];
                    } else {
                        gi.scalefac_l[sfb] = (slen1 > 0) ? bs.read_bits(slen1) : 0;
                    }
                }
                // Band 1: sfb 6..10
                for (uint32_t sfb = 6; sfb < 11; ++sfb) {
                    if (side_info.scfsi[ch][1] != 0) {
                        gi.scalefac_l[sfb] = side_info.granules[0][ch].scalefac_l[sfb];
                    } else {
                        gi.scalefac_l[sfb] = (slen1 > 0) ? bs.read_bits(slen1) : 0;
                    }
                }
                // Band 2: sfb 11..15
                for (uint32_t sfb = 11; sfb < 16; ++sfb) {
                    if (side_info.scfsi[ch][2] != 0) {
                        gi.scalefac_l[sfb] = side_info.granules[0][ch].scalefac_l[sfb];
                    } else {
                        gi.scalefac_l[sfb] = (slen2 > 0) ? bs.read_bits(slen2) : 0;
                    }
                }
                // Band 3: sfb 16..20
                for (uint32_t sfb = 16; sfb < 21; ++sfb) {
                    if (side_info.scfsi[ch][3] != 0) {
                        gi.scalefac_l[sfb] = side_info.granules[0][ch].scalefac_l[sfb];
                    } else {
                        gi.scalefac_l[sfb] = (slen2 > 0) ? bs.read_bits(slen2) : 0;
                    }
                }
            }
        }
    } else {
        // MPEG-2 / MPEG-2.5 LSF
        const uint32_t sc = gi.scalefac_compress & 0x1FFU;
        uint32_t slen[4] = {0, 0, 0, 0};

        if (sc < 400) {
            slen[0] = (sc >> 4) / 5;
            slen[1] = (sc >> 4) % 5;
            slen[2] = (sc & 0x0FU) >> 2;
            slen[3] = sc & 0x03U;
        } else if (sc < 500) {
            const uint32_t s = sc - 400;
            slen[0] = (s >> 2) / 5;
            slen[1] = (s >> 2) % 5;
            slen[2] = s & 0x03U;
            slen[3] = 0;
        } else {
            const uint32_t s = sc - 500;
            slen[0] = s / 3;
            slen[1] = s % 3;
            slen[2] = 0;
            slen[3] = 0;
        }

        if (gi.block_type == 2 && gi.window_switching_flag != 0) {
            for (uint32_t p = 0; p < 4; ++p) {
                const uint32_t len = slen[p];
                for (uint32_t sfb = p * 3; sfb < (p + 1) * 3 && sfb < 12; ++sfb) {
                    for (uint32_t w = 0; w < 3; ++w) {
                        gi.scalefac_s[sfb][w] = (len > 0) ? bs.read_bits(len) : 0;
                    }
                }
            }
        } else {
            // Long blocks partitions: 6, 5, 5, 5
            const uint32_t part_bounds[5] = {0, 6, 11, 16, 21};
            for (uint32_t p = 0; p < 4; ++p) {
                const uint32_t len = slen[p];
                for (uint32_t sfb = part_bounds[p]; sfb < part_bounds[p + 1]; ++sfb) {
                    gi.scalefac_l[sfb] = (len > 0) ? bs.read_bits(len) : 0;
                }
            }
        }
    }

    if (bs.has_error()) {
        return PT7_MP3_NEED_MORE_DATA;
    }

    out_bits_read = bs.bit_position() - start_bit_pos;
    return PT7_MP3_OK;
}

} // namespace pt7::mp3
