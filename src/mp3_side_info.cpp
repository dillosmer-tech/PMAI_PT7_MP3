#include "mp3_side_info.h"
#include "mp3_bitstream.h"
#include <cstring>

namespace pt7::mp3 {

pt7_mp3_status_t SideInfoParser::parse(
    const uint8_t* buffer,
    size_t buffer_size,
    const pt7_mp3_frame_info_t& frame_info,
    SideInfo& out_side_info)
{
    if (buffer == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    if (buffer_size < frame_info.side_info_bytes) {
        return PT7_MP3_NEED_MORE_DATA;
    }

    out_side_info = SideInfo{};

    BitstreamReader bs(buffer, frame_info.side_info_bytes);
    const uint32_t num_channels = frame_info.channels;
    const bool is_mpeg1 = (frame_info.version == PT7_MP3_VERSION_MPEG1);
    const uint32_t num_granules = is_mpeg1 ? 2U : 1U;

    if (is_mpeg1) {
        // MPEG-1: main_data_begin = 9 bits
        out_side_info.main_data_begin = bs.read_bits(9);

        // private_bits: 5 bits (mono), 3 bits (stereo)
        const uint32_t priv_bits_len = (num_channels == 1) ? 5U : 3U;
        out_side_info.private_bits = bs.read_bits(priv_bits_len);

        // scfsi: 4 bits per channel
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            for (uint32_t band = 0; band < 4; ++band) {
                out_side_info.scfsi[ch][band] = bs.read_bit();
            }
        }

        // Granule information
        for (uint32_t gr = 0; gr < num_granules; ++gr) {
            for (uint32_t ch = 0; ch < num_channels; ++ch) {
                pt7_mp3_granule_info_t& gi = out_side_info.granules[gr][ch];

                gi.part2_3_length = bs.read_bits(12);
                gi.big_values = bs.read_bits(9);
                gi.global_gain = bs.read_bits(8);
                gi.scalefac_compress = bs.read_bits(4);
                gi.window_switching_flag = bs.read_bit();

                if (gi.window_switching_flag != 0) {
                    gi.block_type = bs.read_bits(2);
                    gi.mixed_block_flag = bs.read_bit();

                    gi.table_select[0] = bs.read_bits(5);
                    gi.table_select[1] = bs.read_bits(5);
                    gi.table_select[2] = 0;

                    gi.subblock_gain[0] = bs.read_bits(3);
                    gi.subblock_gain[1] = bs.read_bits(3);
                    gi.subblock_gain[2] = bs.read_bits(3);

                    if (gi.block_type == 2 && gi.mixed_block_flag == 0) {
                        gi.region0_count = 8;
                        gi.region1_count = 0;
                    } else {
                        gi.region0_count = 7;
                        gi.region1_count = 0;
                    }
                } else {
                    gi.block_type = 0;
                    gi.mixed_block_flag = 0;

                    gi.table_select[0] = bs.read_bits(5);
                    gi.table_select[1] = bs.read_bits(5);
                    gi.table_select[2] = bs.read_bits(5);

                    gi.subblock_gain[0] = 0;
                    gi.subblock_gain[1] = 0;
                    gi.subblock_gain[2] = 0;

                    gi.region0_count = bs.read_bits(4);
                    gi.region1_count = bs.read_bits(3);
                }

                gi.preflag = bs.read_bit();
                gi.scalefac_scale = bs.read_bit();
                gi.count1table_select = bs.read_bit();

                // Validation: big_values cannot exceed 288 (since total coefficients is 576, pairs is 288)
                if (gi.big_values > 288) {
                    return PT7_MP3_ERR_INVALID_HEADER;
                }
            }
        }
    } else {
        // MPEG-2 / MPEG-2.5: main_data_begin = 8 bits
        out_side_info.main_data_begin = bs.read_bits(8);

        // private_bits: 1 bit (mono), 2 bits (stereo)
        const uint32_t priv_bits_len = (num_channels == 1) ? 1U : 2U;
        out_side_info.private_bits = bs.read_bits(priv_bits_len);

        // Only 1 granule for MPEG-2/2.5
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            pt7_mp3_granule_info_t& gi = out_side_info.granules[0][ch];

            gi.part2_3_length = bs.read_bits(12);
            gi.big_values = bs.read_bits(9);
            gi.global_gain = bs.read_bits(8);
            gi.scalefac_compress = bs.read_bits(9);
            gi.window_switching_flag = bs.read_bit();

            if (gi.window_switching_flag != 0) {
                gi.block_type = bs.read_bits(2);
                gi.mixed_block_flag = bs.read_bit();

                gi.table_select[0] = bs.read_bits(5);
                gi.table_select[1] = bs.read_bits(5);
                gi.table_select[2] = 0;

                gi.subblock_gain[0] = bs.read_bits(3);
                gi.subblock_gain[1] = bs.read_bits(3);
                gi.subblock_gain[2] = bs.read_bits(3);

                if (gi.block_type == 2 && gi.mixed_block_flag == 0) {
                    gi.region0_count = 8;
                    gi.region1_count = 0;
                } else {
                    gi.region0_count = 7;
                    gi.region1_count = 0;
                }
            } else {
                gi.block_type = 0;
                gi.mixed_block_flag = 0;

                gi.table_select[0] = bs.read_bits(5);
                gi.table_select[1] = bs.read_bits(5);
                gi.table_select[2] = bs.read_bits(5);

                gi.subblock_gain[0] = 0;
                gi.subblock_gain[1] = 0;
                gi.subblock_gain[2] = 0;

                gi.region0_count = bs.read_bits(4);
                gi.region1_count = bs.read_bits(3);
            }

            gi.preflag = 0;
            gi.scalefac_scale = bs.read_bit();
            gi.count1table_select = bs.read_bit();

            if (gi.big_values > 288) {
                return PT7_MP3_ERR_INVALID_HEADER;
            }
        }
    }

    if (bs.has_error()) {
        return PT7_MP3_NEED_MORE_DATA;
    }

    return PT7_MP3_OK;
}

} // namespace pt7::mp3
