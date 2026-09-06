#include "mp3_huffman.h"
#include "mp3_huffman_tables.h"
#include <cstring>

namespace pt7::mp3 {

pt7_mp3_status_t HuffmanDecoder::decode(
    BitstreamReader& bs,
    const pt7_mp3_frame_info_t& frame_info,
    pt7_mp3_granule_info_t& gi,
    size_t huffman_bits_available)
{
    const size_t start_bit_pos = bs.bit_position();
    std::memset(gi.is, 0, sizeof(gi.is));
    gi.zero_start = 0;
    gi.non_zero_count = 0;

    if (huffman_bits_available == 0) {
        return PT7_MP3_OK;
    }

    const uint16_t* band_l = BAND_INDEX_L_44;
    switch (frame_info.sample_rate_hz) {
        case 44100: band_l = BAND_INDEX_L_44; break;
        case 48000: band_l = BAND_INDEX_L_48; break;
        case 32000: band_l = BAND_INDEX_L_32; break;
        case 22050: case 11025: band_l = BAND_INDEX_L_22; break;
        case 24000: case 12000: band_l = BAND_INDEX_L_24; break;
        case 16000: case 8000:  band_l = BAND_INDEX_L_22; break;
        default:    band_l = BAND_INDEX_L_44; break;
    }

    size_t region0_end = 0;
    size_t region1_end = 0;
    if (gi.window_switching_flag != 0) {
        // For all window-switching blocks (short, start, stop), use fixed
        // region sizes as specified by the ISO/IEC 11172-3 standard, which is always
        // called when blocksplit_flag is set, regardless of block_type.
        const uint32_t sr = frame_info.sample_rate_hz;
        if (gi.block_type == 2) {
            // Pure short blocks
            region0_end = (sr == 8000) ? 72 : 36;
        } else {
            // Start/stop blocks
            if (sr == 44100 || sr == 48000 || sr == 32000) {
                region0_end = 36;
            } else if (sr == 8000) {
                region0_end = 108;
            } else {
                region0_end = 54;
            }
        }
        region1_end = 576;
    } else {
        const size_t idx0 = gi.region0_count + 1;
        const size_t idx1 = idx0 + gi.region1_count + 1;
        region0_end = (idx0 < 23) ? band_l[idx0] : 576;
        region1_end = (idx1 < 23) ? band_l[idx1] : 576;
    }

    // Big values region
    const size_t big_values_pairs = gi.big_values;
    const size_t big_values_samples = big_values_pairs * 2;
    size_t sample = 0;

    for (; sample < big_values_samples && sample < 576; sample += 2) {
        uint32_t table_idx = 0;
        if (sample < region0_end) {
            table_idx = gi.table_select[0];
        } else if (sample < region1_end) {
            table_idx = gi.table_select[1];
        } else {
            table_idx = gi.table_select[2];
        }

        if (table_idx >= 32) {
            return PT7_MP3_ERR_INVALID_HEADER;
        }

        const auto& cb = HUFFMAN_CODEBOOKS[table_idx];
        if (cb.tree == nullptr) {
            gi.is[sample] = 0;
            gi.is[sample + 1] = 0;
            continue;
        }

        int16_t node = 0;
        while (node >= 0) {
            if (bs.bit_position() - start_bit_pos >= huffman_bits_available) {
                return PT7_MP3_ERR_HUFFMAN_FAIL;
            }
            const uint32_t bit = bs.read_bit();
            if (bs.has_error()) {
                return PT7_MP3_ERR_HUFFMAN_FAIL;
            }
            node = cb.tree[node].child[bit];
        }

        if (node == -32768) {
            return PT7_MP3_ERR_HUFFMAN_FAIL;
        }

        const uint16_t leaf = static_cast<uint16_t>(-node - 1);
        int32_t x = static_cast<int32_t>(leaf >> 4);
        int32_t y = static_cast<int32_t>(leaf & 0x0FU);

        if (cb.linbits > 0 && x == 15) {
            if (bs.bit_position() - start_bit_pos + cb.linbits > huffman_bits_available) {
                return PT7_MP3_ERR_HUFFMAN_FAIL;
            }
            x += static_cast<int32_t>(bs.read_bits(cb.linbits));
        }
        if (x > 0) {
            if (bs.bit_position() - start_bit_pos + 1 > huffman_bits_available) {
                return PT7_MP3_ERR_HUFFMAN_FAIL;
            }
            if (bs.read_bit() != 0) {
                x = -x;
            }
        }

        if (cb.linbits > 0 && y == 15) {
            if (bs.bit_position() - start_bit_pos + cb.linbits > huffman_bits_available) {
                return PT7_MP3_ERR_HUFFMAN_FAIL;
            }
            y += static_cast<int32_t>(bs.read_bits(cb.linbits));
        }
        if (y > 0) {
            if (bs.bit_position() - start_bit_pos + 1 > huffman_bits_available) {
                return PT7_MP3_ERR_HUFFMAN_FAIL;
            }
            if (bs.read_bit() != 0) {
                y = -y;
            }
        }

        gi.is[sample] = x;
        gi.is[sample + 1] = y;
    }

    // Count1 quadruples region
    for (; sample + 4 <= 576; sample += 4) {
        if (bs.bit_position() - start_bit_pos >= huffman_bits_available) {
            break;
        }

        int32_t v = 0;
        int32_t w = 0;
        int32_t x = 0;
        int32_t y = 0;

        if (gi.count1table_select == 1) {
            // Table B: 4 inverted bits
            if (bs.bit_position() - start_bit_pos + 4 > huffman_bits_available) {
                break;
            }
            const uint32_t raw = bs.read_bits(4);
            v = (raw & 8U) ? 0 : 1;
            w = (raw & 4U) ? 0 : 1;
            x = (raw & 2U) ? 0 : 1;
            y = (raw & 1U) ? 0 : 1;
        } else {
            // Table A: QUAD_TREE_A
            int16_t node = 0;
            while (node >= 0) {
                if (bs.bit_position() - start_bit_pos >= huffman_bits_available) {
                    break;
                }
                const uint32_t bit = bs.read_bit();
                node = QUAD_TREE_A[node].child[bit];
            }
            if (node >= 0 || node == -32768) {
                break;
            }
            const uint16_t entry = static_cast<uint16_t>(-node - 1);
            v = (entry & 8U) ? 1 : 0;
            w = (entry & 4U) ? 1 : 0;
            x = (entry & 2U) ? 1 : 0;
            y = (entry & 1U) ? 1 : 0;
        }

        // Sign bits
        if (v > 0) {
            if (bs.bit_position() - start_bit_pos < huffman_bits_available && bs.read_bit() != 0) {
                v = -v;
            }
        }
        if (w > 0) {
            if (bs.bit_position() - start_bit_pos < huffman_bits_available && bs.read_bit() != 0) {
                w = -w;
            }
        }
        if (x > 0) {
            if (bs.bit_position() - start_bit_pos < huffman_bits_available && bs.read_bit() != 0) {
                x = -x;
            }
        }
        if (y > 0) {
            if (bs.bit_position() - start_bit_pos < huffman_bits_available && bs.read_bit() != 0) {
                y = -y;
            }
        }

        gi.is[sample + 0] = v;
        gi.is[sample + 1] = w;
        gi.is[sample + 2] = x;
        gi.is[sample + 3] = y;
    }

    // Zero region
    for (; sample < 576; ++sample) {
        gi.is[sample] = 0;
    }

    // Compute zero_start and non_zero_count
    size_t last_nonzero = 0;
    size_t count = 0;
    for (size_t i = 0; i < 576; ++i) {
        if (gi.is[i] != 0) {
            last_nonzero = i + 1;
            ++count;
        }
    }
    gi.zero_start = static_cast<uint32_t>(last_nonzero);
    gi.non_zero_count = static_cast<uint32_t>(count);

    // Skip any remaining stuffing bits
    const size_t bits_consumed = bs.bit_position() - start_bit_pos;
    if (bits_consumed < huffman_bits_available) {
        bs.skip_bits(huffman_bits_available - bits_consumed);
    }

    return PT7_MP3_OK;
}

} // namespace pt7::mp3
