#include "pt7_mp3.h"
#include "mp3_frame.h"
#include "mp3_side_info.h"
#include "mp3_scalefactors.h"
#include "mp3_huffman.h"
#include "mp3_reservoir.h"
#include "mp3_requant.h"
#include "mp3_stereo.h"
#include "mp3_antialias.h"
#include "mp3_imdct.h"
#include "mp3_overlap.h"
#include "mp3_synth.h"
#include "mp3_id3.h"
#include "mp3_vbr.h"

#include <new>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// Internal frame index for seeking (Phase D)
// ---------------------------------------------------------------------------

struct FrameIndexEntry {
    uint64_t byte_offset;       // offset of frame header from start of source
    uint32_t frame_size;        // frame size in bytes
    uint32_t samples_per_frame; // samples per channel in this frame
    uint32_t channels;          // channel count
    uint64_t sample_offset;     // cumulative per-channel sample count at frame start
};

struct pt7_mp3_decoder {
    pt7_mp3_frame_info_t last_frame_info{};
    pt7::mp3::SideInfo last_side_info{};
    pt7::mp3::BitReservoir reservoir;
    float overlap[2][32][18]{}; // [ch][sb][18]
    pt7_mp3_subband_samples_t subband_samples[2][2]{}; // [gr][ch]
    pt7::mp3::SynthesisState synth_state{};
    float pcm_frame[1152 * 2]{};
    size_t pcm_frame_samples{0};
    uint64_t frames_processed{0};
    bool has_frame_info{false};
    bool has_decoded_frame{false};

    // VBR / Gapless metadata
    pt7_mp3_vbr_info_t vbr_info{};
    bool has_vbr_info{false};

    // Incremental stream buffer
    std::vector<uint8_t> stream_in{};
    size_t stream_pos{0};

    // Gapless trimming state (Phase C)
    bool gapless_enabled{false};
    uint64_t gapless_front_skipped{0};
    uint64_t gapless_end_trimmed{0};
    uint64_t total_pcm_written{0};
    std::vector<int16_t> gapless_tail_pcm16{};
    std::vector<float> gapless_tail_float{};

    // Reusable buffer for reservoir assembly (avoids per-frame heap allocation)
    std::vector<uint8_t> assembled_buf{};

    // Seekable source (Phase D)
    const uint8_t* source_data{nullptr};
    size_t source_size{0};
    bool seekable{false};
    std::vector<FrameIndexEntry> frame_index{};
    bool frame_index_built{false};
    uint64_t total_samples_indexed{0};
};

extern "C" {

pt7_mp3_decoder_t* pt7_mp3_create(void)
{
    auto* dec = new (std::nothrow) pt7_mp3_decoder();
    if (dec != nullptr) {
        pt7_mp3_reset(dec);
    }
    return dec;
}

void pt7_mp3_destroy(pt7_mp3_decoder_t* decoder)
{
    delete decoder;
}

void pt7_mp3_reset(pt7_mp3_decoder_t* decoder)
{
    if (decoder == nullptr) {
        return;
    }
    decoder->last_frame_info = pt7_mp3_frame_info_t{};
    decoder->last_side_info = pt7::mp3::SideInfo{};
    decoder->reservoir.reset();
    std::memset(decoder->overlap, 0, sizeof(decoder->overlap));
    std::memset(decoder->subband_samples, 0, sizeof(decoder->subband_samples));
    decoder->synth_state.reset();
    std::memset(decoder->pcm_frame, 0, sizeof(decoder->pcm_frame));
    decoder->pcm_frame_samples = 0;
    decoder->frames_processed = 0;
    decoder->has_frame_info = false;
    decoder->has_decoded_frame = false;
    decoder->vbr_info = pt7_mp3_vbr_info_t{};
    decoder->has_vbr_info = false;
    decoder->stream_in.clear();
    decoder->stream_pos = 0;
    decoder->gapless_enabled = false;
    decoder->gapless_front_skipped = 0;
    decoder->gapless_end_trimmed = 0;
    decoder->total_pcm_written = 0;
    decoder->gapless_tail_pcm16.clear();
    decoder->gapless_tail_float.clear();
    // Note: source_data, source_size, seekable, frame_index are preserved across reset
    // because they describe the file, not the decode position.
}

pt7_mp3_status_t pt7_mp3_parse_header(
    const uint8_t* buffer,
    size_t buffer_size,
    pt7_mp3_frame_info_t* out_info)
{
    return pt7::mp3::FrameParser::parse_header(buffer, buffer_size, out_info);
}

pt7_mp3_status_t pt7_mp3_scan_frame(
    const uint8_t* buffer,
    size_t buffer_size,
    size_t* out_frame_offset,
    pt7_mp3_frame_info_t* out_info)
{
    return pt7::mp3::FrameParser::scan_frame(buffer, buffer_size, out_frame_offset, out_info);
}

pt7_mp3_status_t pt7_mp3_decode_frame(
    pt7_mp3_decoder_t* decoder,
    const uint8_t* buffer,
    size_t buffer_size,
    size_t* out_bytes_consumed)
{
    if (decoder == nullptr || buffer == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    pt7_mp3_frame_info_t frame_info{};
    const pt7_mp3_status_t hdr_st = pt7_mp3_parse_header(buffer, buffer_size, &frame_info);
    if (hdr_st != PT7_MP3_OK) {
        return hdr_st;
    }

    pt7::mp3::SideInfo side_info{};
    const pt7_mp3_status_t si_st = pt7::mp3::SideInfoParser::parse(
        buffer + frame_info.header_bytes,
        buffer_size - frame_info.header_bytes,
        frame_info,
        side_info
    );
    if (si_st != PT7_MP3_OK) {
        // Side info error: still update reservoir with current frame main data
        // so subsequent frames don't cascade-fail due to stale reservoir state.
        const size_t header_plus_side = frame_info.header_bytes + frame_info.side_info_bytes;
        const size_t frame_main_len = (frame_info.frame_size_bytes > header_plus_side)
            ? (frame_info.frame_size_bytes - header_plus_side) : 0;
        const uint8_t* frame_main_data = buffer + header_plus_side;
        if (frame_main_len > 0) {
            std::vector<uint8_t> tmp(frame_main_data, frame_main_data + frame_main_len);
            decoder->reservoir.update(tmp, tmp.size() * 8);
        }
        return si_st;
    }

    const size_t header_plus_side = frame_info.header_bytes + frame_info.side_info_bytes;
    const size_t frame_main_len = frame_info.frame_size_bytes - header_plus_side;
    const uint8_t* frame_main_data = buffer + header_plus_side;

    std::vector<uint8_t>& assembled = decoder->assembled_buf;
    const pt7_mp3_status_t res_st = decoder->reservoir.assemble(
        side_info.main_data_begin,
        frame_main_data,
        frame_main_len,
        assembled
    );
    if (res_st != PT7_MP3_OK) {
        return res_st;
    }

    if (out_bytes_consumed != nullptr) {
        *out_bytes_consumed = frame_info.frame_size_bytes;
    }

    pt7::mp3::BitstreamReader bs(assembled.data(), assembled.size());
    const uint32_t num_granules = (frame_info.version == PT7_MP3_VERSION_MPEG1) ? 2U : 1U;
    const uint32_t num_channels = frame_info.channels;

    auto update_reservoir_on_error = [&]() {
        size_t frame_main_bits = 0;
        for (uint32_t g = 0; g < num_granules; ++g) {
            for (uint32_t c = 0; c < num_channels; ++c) {
                frame_main_bits += side_info.granules[g][c].part2_3_length;
            }
        }
        const size_t bits_to_update = std::max(bs.bit_position(), frame_main_bits);
        decoder->reservoir.update(assembled, bits_to_update);
    };

    // Step 1: Huffman & Scalefactor unpacking per granule
    for (uint32_t gr = 0; gr < num_granules; ++gr) {
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            pt7_mp3_granule_info_t& gi = side_info.granules[gr][ch];
            const size_t gr_start_bit = bs.bit_position();

            size_t scalefac_bits = 0;
            const pt7_mp3_status_t sc_st = pt7::mp3::ScalefactorDecoder::decode(
                bs,
                frame_info,
                side_info,
                gr,
                ch,
                gi,
                scalefac_bits
            );
            if (sc_st != PT7_MP3_OK) {
                update_reservoir_on_error();
                return sc_st;
            }

            if (scalefac_bits > gi.part2_3_length) {
                update_reservoir_on_error();
                return PT7_MP3_ERR_HUFFMAN_FAIL;
            }

            const size_t huffman_bits = gi.part2_3_length - scalefac_bits;
            const pt7_mp3_status_t huff_st = pt7::mp3::HuffmanDecoder::decode(
                bs,
                frame_info,
                gi,
                huffman_bits
            );
            if (huff_st != PT7_MP3_OK) {
                update_reservoir_on_error();
                return huff_st;
            }

            const size_t total_consumed = bs.bit_position() - gr_start_bit;
            if (total_consumed < gi.part2_3_length) {
                bs.skip_bits(gi.part2_3_length - total_consumed);
            }
        }
    }

    // Step 2: Audio Reconstruction pipeline (Task 3) per granule
    size_t pcm_write_idx = 0;

    for (uint32_t gr = 0; gr < num_granules; ++gr) {
        float xr[2][576];

        // 2a. Requantization
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            pt7::mp3::Requantizer::requantize(
                frame_info,
                side_info.granules[gr][ch],
                xr[ch]
            );
        }

        // 2b. Stereo Processing
        if (num_channels == 2) {
            pt7::mp3::StereoProcessor::process(
                frame_info,
                side_info.granules[gr][0],
                side_info.granules[gr][1],
                xr
            );
        }

        // 2c. Antialias filter
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            pt7::mp3::AntialiasFilter::apply(
                side_info.granules[gr][ch],
                xr[ch]
            );
        }

        // 2d. IMDCT & Windowing + 2e. Overlap/Add
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            float z[32][36];
            pt7::mp3::ImdctEngine::transform(
                side_info.granules[gr][ch],
                xr[ch],
                decoder->overlap[ch],
                z
            );

            pt7::mp3::OverlapAddEngine::process(
                z,
                decoder->overlap[ch],
                decoder->subband_samples[gr][ch]
            );
        }

        // Step 3: Polyphase Synthesis Filterbank & Interleaving (Task 4)
        float gr_pcm[2][576];
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            pt7::mp3::SynthesisFilterbank::process_granule(
                decoder->subband_samples[gr][ch],
                decoder->synth_state.fifo[ch],
                gr_pcm[ch]
            );
        }

        // Interleave PCM output
        for (size_t s = 0; s < 576; ++s) {
            for (uint32_t ch = 0; ch < num_channels; ++ch) {
                decoder->pcm_frame[pcm_write_idx++] = gr_pcm[ch][s];
            }
        }
    }

    decoder->pcm_frame_samples = pcm_write_idx;

    // Track total per-channel samples for stream info (Phase C)
    decoder->total_pcm_written += (pcm_write_idx / num_channels);

    // Detect Xing, Info, or VBRI metadata on the first frame
    if (decoder->frames_processed == 0 && !decoder->has_vbr_info) {
        if (pt7::mp3::VbrParser::detect(buffer, buffer_size, frame_info, decoder->vbr_info) == PT7_MP3_OK) {
            decoder->has_vbr_info = true;
        }
    }

    decoder->reservoir.update(assembled, bs.bit_position());
    decoder->last_frame_info = frame_info;
    decoder->last_side_info = side_info;
    decoder->has_frame_info = true;
    decoder->has_decoded_frame = true;
    decoder->frames_processed++;

    if (out_bytes_consumed != nullptr) {
        *out_bytes_consumed = frame_info.frame_size_bytes;
    }

    return PT7_MP3_OK;
}

pt7_mp3_status_t pt7_mp3_decode_frame_pcm16(
    pt7_mp3_decoder_t* decoder,
    const uint8_t* buffer,
    size_t buffer_size,
    int16_t* out_pcm16,
    size_t pcm16_capacity_samples,
    size_t* out_samples_written,
    size_t* out_bytes_consumed)
{
    if (decoder == nullptr || buffer == nullptr || out_pcm16 == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    // Parse header first to check required capacity safely
    pt7_mp3_frame_info_t info{};
    const pt7_mp3_status_t hdr_st = pt7_mp3_parse_header(buffer, buffer_size, &info);
    if (hdr_st != PT7_MP3_OK) {
        return hdr_st;
    }

    const size_t required_samples = static_cast<size_t>(info.samples_per_frame) * info.channels;
    if (pcm16_capacity_samples < required_samples) {
        return PT7_MP3_ERR_BUFFER_TOO_SMALL;
    }

    size_t consumed = 0;
    const pt7_mp3_status_t dec_st = pt7_mp3_decode_frame(decoder, buffer, buffer_size, &consumed);
    if (dec_st != PT7_MP3_OK) {
        if (out_bytes_consumed != nullptr) {
            *out_bytes_consumed = (consumed > 0) ? consumed : info.frame_size_bytes;
        }
        return dec_st;
    }

    // Convert float to int16 with safe clamping / saturation
    for (size_t i = 0; i < decoder->pcm_frame_samples; ++i) {
        float v = decoder->pcm_frame[i] * 32768.0f;
        if (v > 32767.0f) {
            v = 32767.0f;
        } else if (v < -32768.0f) {
            v = -32768.0f;
        }
        out_pcm16[i] = static_cast<int16_t>(std::lrintf(v));
    }

    if (out_samples_written != nullptr) {
        *out_samples_written = decoder->pcm_frame_samples;
    }
    if (out_bytes_consumed != nullptr) {
        *out_bytes_consumed = consumed;
    }

    return PT7_MP3_OK;
}

pt7_mp3_status_t pt7_mp3_decode_frame_float(
    pt7_mp3_decoder_t* decoder,
    const uint8_t* buffer,
    size_t buffer_size,
    float* out_pcm_float,
    size_t pcm_float_capacity_samples,
    size_t* out_samples_written,
    size_t* out_bytes_consumed)
{
    if (decoder == nullptr || buffer == nullptr || out_pcm_float == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    // Parse header first to check required capacity safely
    pt7_mp3_frame_info_t info{};
    const pt7_mp3_status_t hdr_st = pt7_mp3_parse_header(buffer, buffer_size, &info);
    if (hdr_st != PT7_MP3_OK) {
        return hdr_st;
    }

    const size_t required_samples = static_cast<size_t>(info.samples_per_frame) * info.channels;
    if (pcm_float_capacity_samples < required_samples) {
        return PT7_MP3_ERR_BUFFER_TOO_SMALL;
    }

    size_t consumed = 0;
    const pt7_mp3_status_t dec_st = pt7_mp3_decode_frame(decoder, buffer, buffer_size, &consumed);
    if (dec_st != PT7_MP3_OK) {
        if (out_bytes_consumed != nullptr) {
            *out_bytes_consumed = (consumed > 0) ? consumed : info.frame_size_bytes;
        }
        return dec_st;
    }

    std::memcpy(out_pcm_float, decoder->pcm_frame, decoder->pcm_frame_samples * sizeof(float));

    if (out_samples_written != nullptr) {
        *out_samples_written = decoder->pcm_frame_samples;
    }
    if (out_bytes_consumed != nullptr) {
        *out_bytes_consumed = consumed;
    }

    return PT7_MP3_OK;
}

pt7_mp3_status_t pt7_mp3_feed(
    pt7_mp3_decoder_t* decoder,
    const uint8_t* data,
    size_t data_size)
{
    if (decoder == nullptr || (data == nullptr && data_size > 0)) {
        return PT7_MP3_ERR_INVALID_ARG;
    }
    if (data_size == 0) {
        return PT7_MP3_OK;
    }

    // Skip ID3v2 tag at the start of an empty stream if present
    if (decoder->stream_in.empty() && data_size >= 10) {
        size_t id3_sz = 0;
        if (pt7::mp3::Id3Parser::detect_and_size(data, data_size, id3_sz) == PT7_MP3_OK) {
            if (data_size >= id3_sz) {
                data += id3_sz;
                data_size -= id3_sz;
            }
        }
    }

    // Prune consumed stream bytes if large enough to save memory
    if (decoder->stream_pos > 4096) {
        decoder->stream_in.erase(
            decoder->stream_in.begin(),
            decoder->stream_in.begin() + static_cast<std::ptrdiff_t>(decoder->stream_pos)
        );
        decoder->stream_pos = 0;
    }

    decoder->stream_in.insert(decoder->stream_in.end(), data, data + data_size);
    return PT7_MP3_OK;
}

pt7_mp3_status_t pt7_mp3_read_pcm16(
    pt7_mp3_decoder_t* decoder,
    int16_t* out_pcm16,
    size_t pcm16_capacity_samples,
    size_t* out_samples_written)
{
    if (decoder == nullptr || out_pcm16 == nullptr || out_samples_written == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    *out_samples_written = 0;
    size_t total_written = 0;

    while (decoder->stream_pos < decoder->stream_in.size()) {
        const uint8_t* cur_ptr = decoder->stream_in.data() + decoder->stream_pos;
        const size_t cur_avail = decoder->stream_in.size() - decoder->stream_pos;

        size_t frame_offset = 0;
        pt7_mp3_frame_info_t info{};
        const pt7_mp3_status_t sc_st = pt7_mp3_scan_frame(
            cur_ptr,
            cur_avail,
            &frame_offset,
            &info
        );

        if (sc_st == PT7_MP3_NEED_MORE_DATA) {
            *out_samples_written = total_written;
            return (total_written > 0) ? PT7_MP3_OK : PT7_MP3_NEED_MORE_DATA;
        }
        if (sc_st == PT7_MP3_ERR_SYNC_NOT_FOUND) {
            const size_t keep = std::min<size_t>(3, cur_avail);
            decoder->stream_pos = decoder->stream_in.size() - keep;
            *out_samples_written = total_written;
            return (total_written > 0) ? PT7_MP3_OK : PT7_MP3_NEED_MORE_DATA;
        }
        if (sc_st != PT7_MP3_OK) {
            *out_samples_written = total_written;
            return sc_st;
        }

        const size_t req_samples = static_cast<size_t>(info.samples_per_frame) * info.channels;
        if (total_written + req_samples > pcm16_capacity_samples) {
            *out_samples_written = total_written;
            return (total_written == 0) ? PT7_MP3_ERR_BUFFER_TOO_SMALL : PT7_MP3_OK;
        }

        decoder->stream_pos += frame_offset;
        const uint8_t* frame_ptr = decoder->stream_in.data() + decoder->stream_pos;
        const size_t frame_avail = decoder->stream_in.size() - decoder->stream_pos;

        size_t samples_written = 0;
        size_t bytes_consumed = 0;
        const pt7_mp3_status_t dec_st = pt7_mp3_decode_frame_pcm16(
            decoder,
            frame_ptr,
            frame_avail,
            out_pcm16 + total_written,
            pcm16_capacity_samples - total_written,
            &samples_written,
            &bytes_consumed
        );

        if (dec_st == PT7_MP3_OK) {
            decoder->stream_pos += bytes_consumed;
            total_written += samples_written;
        } else {
            const size_t skip_bytes = (bytes_consumed > 0) ? bytes_consumed :
                                      (info.frame_size_bytes > 0 ? info.frame_size_bytes : 4U);
            decoder->stream_pos += skip_bytes;
        }
    }

    // Apply gapless trimming (Phase C)
    if (decoder->gapless_enabled && total_written > 0) {
        const uint32_t channels = decoder->last_frame_info.channels;
        const uint32_t delay = decoder->vbr_info.encoder_delay;
        const uint32_t padding = decoder->vbr_info.end_padding;
        const size_t tail_size = static_cast<size_t>(padding) * channels;

        // Front trimming: skip encoder_delay samples (per channel)
        size_t front_skip = 0;
        if (decoder->gapless_front_skipped < delay) {
            const uint64_t remaining = delay - decoder->gapless_front_skipped;
            const size_t skip_interleaved = static_cast<size_t>(
                std::min<uint64_t>(remaining, total_written / channels)) * channels;
            front_skip = skip_interleaved;
            decoder->gapless_front_skipped += skip_interleaved / channels;
        }

        // End trimming: hold back the last `padding*channels` samples
        // We buffer them so they can be trimmed if this is the end of stream.
        // Combine new samples with any previously held tail.
        size_t effective = total_written - front_skip;

        if (tail_size > 0 && effective > 0) {
            // Merge old tail + new effective samples
            std::vector<int16_t> combined;
            combined.reserve(decoder->gapless_tail_pcm16.size() + effective);
            combined.insert(combined.end(), decoder->gapless_tail_pcm16.begin(),
                            decoder->gapless_tail_pcm16.end());
            combined.insert(combined.end(), out_pcm16 + front_skip,
                            out_pcm16 + front_skip + effective);

            // If combined has more than tail_size, emit the excess; keep tail_size
            if (combined.size() > tail_size) {
                const size_t emit = combined.size() - tail_size;
                std::memcpy(out_pcm16, combined.data(), emit * sizeof(int16_t));
                // Update tail buffer
                decoder->gapless_tail_pcm16.assign(
                    combined.begin() + emit, combined.end());
                total_written = emit;
            } else {
                // Everything goes to tail, nothing emitted now
                decoder->gapless_tail_pcm16 = combined;
                total_written = 0;
            }
        } else {
            // No end trimming needed, just shift past front skip
            if (front_skip > 0) {
                std::memmove(out_pcm16, out_pcm16 + front_skip,
                             effective * sizeof(int16_t));
            }
            total_written = effective;
        }
    }

    *out_samples_written = total_written;
    return PT7_MP3_OK;
}

pt7_mp3_status_t pt7_mp3_read_pcm_float(
    pt7_mp3_decoder_t* decoder,
    float* out_pcm_float,
    size_t pcm_float_capacity_samples,
    size_t* out_samples_written)
{
    if (decoder == nullptr || out_pcm_float == nullptr || out_samples_written == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    *out_samples_written = 0;
    size_t total_written = 0;

    while (decoder->stream_pos < decoder->stream_in.size()) {
        const uint8_t* cur_ptr = decoder->stream_in.data() + decoder->stream_pos;
        const size_t cur_avail = decoder->stream_in.size() - decoder->stream_pos;

        size_t frame_offset = 0;
        pt7_mp3_frame_info_t info{};
        const pt7_mp3_status_t sc_st = pt7_mp3_scan_frame(
            cur_ptr,
            cur_avail,
            &frame_offset,
            &info
        );

        if (sc_st == PT7_MP3_NEED_MORE_DATA) {
            *out_samples_written = total_written;
            return (total_written > 0) ? PT7_MP3_OK : PT7_MP3_NEED_MORE_DATA;
        }
        if (sc_st == PT7_MP3_ERR_SYNC_NOT_FOUND) {
            const size_t keep = std::min<size_t>(3, cur_avail);
            decoder->stream_pos = decoder->stream_in.size() - keep;
            *out_samples_written = total_written;
            return (total_written > 0) ? PT7_MP3_OK : PT7_MP3_NEED_MORE_DATA;
        }
        if (sc_st != PT7_MP3_OK) {
            *out_samples_written = total_written;
            return sc_st;
        }

        const size_t req_samples = static_cast<size_t>(info.samples_per_frame) * info.channels;
        if (total_written + req_samples > pcm_float_capacity_samples) {
            *out_samples_written = total_written;
            return (total_written == 0) ? PT7_MP3_ERR_BUFFER_TOO_SMALL : PT7_MP3_OK;
        }

        decoder->stream_pos += frame_offset;
        const uint8_t* frame_ptr = decoder->stream_in.data() + decoder->stream_pos;
        const size_t frame_avail = decoder->stream_in.size() - decoder->stream_pos;

        size_t samples_written = 0;
        size_t bytes_consumed = 0;
        const pt7_mp3_status_t dec_st = pt7_mp3_decode_frame_float(
            decoder,
            frame_ptr,
            frame_avail,
            out_pcm_float + total_written,
            pcm_float_capacity_samples - total_written,
            &samples_written,
            &bytes_consumed
        );

        if (dec_st == PT7_MP3_OK) {
            decoder->stream_pos += bytes_consumed;
            total_written += samples_written;
        } else {
            // Decode error: skip and resync to next valid frame
            const size_t skip_bytes = (bytes_consumed > 0) ? bytes_consumed :
                                      (info.frame_size_bytes > 0 ? info.frame_size_bytes : 4U);
            decoder->stream_pos += skip_bytes;
        }
    }

    // Apply gapless trimming (Phase C)
    if (decoder->gapless_enabled && total_written > 0) {
        const uint32_t channels = decoder->last_frame_info.channels;
        const uint32_t delay = decoder->vbr_info.encoder_delay;
        const uint32_t padding = decoder->vbr_info.end_padding;
        const size_t tail_size = static_cast<size_t>(padding) * channels;

        // Front trimming
        size_t front_skip = 0;
        if (decoder->gapless_front_skipped < delay) {
            const uint64_t remaining = delay - decoder->gapless_front_skipped;
            const size_t skip_interleaved = static_cast<size_t>(
                std::min<uint64_t>(remaining, total_written / channels)) * channels;
            front_skip = skip_interleaved;
            decoder->gapless_front_skipped += skip_interleaved / channels;
        }

        size_t effective = total_written - front_skip;

        if (tail_size > 0 && effective > 0) {
            std::vector<float> combined;
            combined.reserve(decoder->gapless_tail_float.size() + effective);
            combined.insert(combined.end(), decoder->gapless_tail_float.begin(),
                            decoder->gapless_tail_float.end());
            combined.insert(combined.end(), out_pcm_float + front_skip,
                            out_pcm_float + front_skip + effective);

            if (combined.size() > tail_size) {
                const size_t emit = combined.size() - tail_size;
                std::memcpy(out_pcm_float, combined.data(), emit * sizeof(float));
                decoder->gapless_tail_float.assign(
                    combined.begin() + emit, combined.end());
                total_written = emit;
            } else {
                decoder->gapless_tail_float = combined;
                total_written = 0;
            }
        } else {
            if (front_skip > 0) {
                std::memmove(out_pcm_float, out_pcm_float + front_skip,
                             effective * sizeof(float));
            }
            total_written = effective;
        }
    }

    *out_samples_written = total_written;
    return PT7_MP3_OK;
}

void pt7_mp3_flush(pt7_mp3_decoder_t* decoder)
{
    if (decoder != nullptr) {
        decoder->stream_in.clear();
        decoder->stream_pos = 0;
    }
}

pt7_mp3_status_t pt7_mp3_get_frame_info(
    const pt7_mp3_decoder_t* decoder,
    pt7_mp3_frame_info_t* out_info)
{
    if (decoder == nullptr || out_info == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }
    if (!decoder->has_frame_info) {
        return PT7_MP3_ERR_INVALID_ARG;
    }
    *out_info = decoder->last_frame_info;
    return PT7_MP3_OK;
}

size_t pt7_mp3_get_instance_size(void)
{
    return sizeof(pt7_mp3_decoder);
}

pt7_mp3_status_t pt7_mp3_detect_id3v2(
    const uint8_t* buffer,
    size_t buffer_size,
    size_t* out_tag_size)
{
    if (buffer == nullptr || out_tag_size == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }
    size_t sz = 0;
    const pt7_mp3_status_t st = pt7::mp3::Id3Parser::detect_and_size(buffer, buffer_size, sz);
    if (st == PT7_MP3_OK) {
        *out_tag_size = sz;
    }
    return st;
}

pt7_mp3_status_t pt7_mp3_get_vbr_info(
    const pt7_mp3_decoder_t* decoder,
    pt7_mp3_vbr_info_t* out_vbr)
{
    if (decoder == nullptr || out_vbr == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }
    if (!decoder->has_vbr_info) {
        return PT7_MP3_ERR_SYNC_NOT_FOUND;
    }
    *out_vbr = decoder->vbr_info;
    return PT7_MP3_OK;
}

const pt7_mp3_granule_info_t* pt7_mp3_get_granule(
    const pt7_mp3_decoder_t* decoder,
    uint32_t gr,
    uint32_t ch)
{
    if (decoder == nullptr || !decoder->has_decoded_frame) {
        return nullptr;
    }
    const uint32_t max_gr = (decoder->last_frame_info.version == PT7_MP3_VERSION_MPEG1) ? 2U : 1U;
    if (gr >= max_gr || ch >= decoder->last_frame_info.channels) {
        return nullptr;
    }
    return &decoder->last_side_info.granules[gr][ch];
}

const pt7_mp3_subband_samples_t* pt7_mp3_get_subband_samples(
    const pt7_mp3_decoder_t* decoder,
    uint32_t gr,
    uint32_t ch)
{
    if (decoder == nullptr || !decoder->has_decoded_frame) {
        return nullptr;
    }
    const uint32_t max_gr = (decoder->last_frame_info.version == PT7_MP3_VERSION_MPEG1) ? 2U : 1U;
    if (gr >= max_gr || ch >= decoder->last_frame_info.channels) {
        return nullptr;
    }
    return &decoder->subband_samples[gr][ch];
}

uint32_t pt7_mp3_get_granules_per_frame(const pt7_mp3_decoder_t* decoder)
{
    if (decoder == nullptr || !decoder->has_frame_info) {
        return 0;
    }
    return (decoder->last_frame_info.version == PT7_MP3_VERSION_MPEG1) ? 2U : 1U;
}

const char* pt7_mp3_status_string(pt7_mp3_status_t status)
{
    switch (status) {
        case PT7_MP3_OK:
            return "Success";
        case PT7_MP3_NEED_MORE_DATA:
            return "Need more data (buffer truncated)";
        case PT7_MP3_END_OF_STREAM:
            return "End of stream reached";
        case PT7_MP3_ERR_INVALID_HEADER:
            return "Invalid MP3 frame header";
        case PT7_MP3_ERR_UNSUPPORTED:
            return "Unsupported MP3 format or feature";
        case PT7_MP3_ERR_INVALID_ARG:
            return "Invalid argument (NULL pointer)";
        case PT7_MP3_ERR_SYNC_NOT_FOUND:
            return "Frame synchronization word not found";
        case PT7_MP3_ERR_RESERVOIR_UNDERFLOW:
            return "Bit reservoir underflow";
        case PT7_MP3_ERR_HUFFMAN_FAIL:
            return "Huffman decoding failure";
        case PT7_MP3_ERR_BUFFER_TOO_SMALL:
            return "Output PCM buffer capacity too small";
        case PT7_MP3_ERR_DECODE:
            return "Decode error (corrupt frame data)";
        case PT7_MP3_ERR_SEEK_NOT_SUPPORTED:
            return "Seek not supported (no seekable source)";
        case PT7_MP3_ERR_SEEK_OUT_OF_RANGE:
            return "Seek position out of range";
        default:
            return "Unknown error";
    }
}

// ---------------------------------------------------------------------------
// Phase C: Stream info + Gapless playback
// ---------------------------------------------------------------------------

pt7_mp3_status_t pt7_mp3_get_info(
    const pt7_mp3_decoder_t* decoder,
    pt7_mp3_stream_info_t* out_info)
{
    if (decoder == nullptr || out_info == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    std::memset(out_info, 0, sizeof(pt7_mp3_stream_info_t));

    if (decoder->has_frame_info) {
        const auto& fi = decoder->last_frame_info;
        out_info->version = fi.version;
        out_info->layer = fi.layer;
        out_info->sample_rate_hz = fi.sample_rate_hz;
        out_info->channels = fi.channels;
        out_info->channel_mode = fi.channel_mode;
        out_info->bitrate_kbps = fi.bitrate_kbps;
    }

    // Bitrate mode
    if (decoder->has_vbr_info) {
        out_info->has_vbr_header = 1;
        out_info->is_vbr = decoder->vbr_info.is_vbr;
        out_info->bitrate_mode = decoder->vbr_info.is_vbr
            ? PT7_MP3_BITRATE_VBR
            : PT7_MP3_BITRATE_CBR;
    } else if (decoder->has_frame_info) {
        // No VBR header: assume CBR
        out_info->bitrate_mode = PT7_MP3_BITRATE_CBR;
    } else {
        out_info->bitrate_mode = PT7_MP3_BITRATE_UNKNOWN;
    }

    // Frame/sample counts
    out_info->frame_count = decoder->frames_processed;
    out_info->sample_count = decoder->total_pcm_written;

    if (decoder->has_vbr_info && decoder->vbr_info.total_frames > 0) {
        out_info->total_frames = decoder->vbr_info.total_frames;
        // Compute total samples from declared frame count
        const uint32_t spf = (out_info->version == PT7_MP3_VERSION_MPEG1) ? 1152U : 576U;
        out_info->total_samples = static_cast<uint64_t>(decoder->vbr_info.total_frames) * spf;
    } else if (decoder->frame_index_built && !decoder->frame_index.empty()) {
        // Use frame index when no VBR header is available
        out_info->total_frames = decoder->frame_index.size();
        out_info->total_samples = decoder->total_samples_indexed;
    }

    // Duration
    if (out_info->total_samples > 0 && out_info->sample_rate_hz > 0) {
        out_info->duration_seconds =
            static_cast<double>(out_info->total_samples) / out_info->sample_rate_hz;
    } else if (out_info->sample_count > 0 && out_info->sample_rate_hz > 0) {
        out_info->duration_seconds =
            static_cast<double>(out_info->sample_count) / out_info->sample_rate_hz;
    }

    // Gapless
    if (decoder->has_vbr_info && decoder->vbr_info.has_gapless) {
        out_info->gapless_available = 1;
        out_info->encoder_delay = decoder->vbr_info.encoder_delay;
        out_info->end_padding = decoder->vbr_info.end_padding;
    }

    return PT7_MP3_OK;
}

pt7_mp3_status_t pt7_mp3_enable_gapless(pt7_mp3_decoder_t* decoder)
{
    if (decoder == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }
    if (!decoder->has_vbr_info || !decoder->vbr_info.has_gapless) {
        return PT7_MP3_ERR_SYNC_NOT_FOUND;
    }
    // Idempotent: enabling twice does not apply trimming twice
    if (!decoder->gapless_enabled) {
        decoder->gapless_enabled = true;
        // Pre-allocate end-trim buffer
        const size_t channels = decoder->last_frame_info.channels > 0
            ? decoder->last_frame_info.channels : 2U;
        const size_t tail_size = static_cast<size_t>(decoder->vbr_info.end_padding) * channels;
        decoder->gapless_tail_pcm16.resize(tail_size);
        decoder->gapless_tail_float.resize(tail_size);
    }
    return PT7_MP3_OK;
}

uint64_t pt7_mp3_get_gapless_trimmed_front(const pt7_mp3_decoder_t* decoder)
{
    if (decoder == nullptr) return 0;
    return decoder->gapless_front_skipped;
}

uint64_t pt7_mp3_get_gapless_trimmed_end(const pt7_mp3_decoder_t* decoder)
{
    if (decoder == nullptr) return 0;
    return decoder->gapless_end_trimmed;
}

// ---------------------------------------------------------------------------
// Phase D: Seeking and random access
// ---------------------------------------------------------------------------

namespace {

// Build a frame index by scanning the entire source data.
// Returns false if no valid frames found.
bool build_frame_index(pt7_mp3_decoder* dec)
{
    if (dec->source_data == nullptr || dec->source_size == 0) {
        return false;
    }

    dec->frame_index.clear();
    dec->total_samples_indexed = 0;

    const uint8_t* data = dec->source_data;
    size_t size = dec->source_size;

    // Skip ID3v2
    size_t id3_sz = 0;
    if (pt7::mp3::Id3Parser::detect_and_size(data, size, id3_sz) == PT7_MP3_OK) {
        if (size < id3_sz) return false;
    }

    size_t pos = id3_sz;
    uint64_t cumulative_samples = 0;

    while (pos + 4 <= size) {
        size_t frame_off = 0;
        pt7_mp3_frame_info_t info{};
        const pt7_mp3_status_t st = pt7_mp3_scan_frame(
            data + pos, size - pos, &frame_off, &info);

        if (st != PT7_MP3_OK) {
            // Try to advance past garbage
            if (st == PT7_MP3_ERR_SYNC_NOT_FOUND) {
                // Scan for next 0xFF byte
                size_t next = pos + frame_off;
                while (next < size && data[next] != 0xFF) next++;
                pos = next;
                continue;
            }
            break;
        }

        pos += frame_off;
        if (pos + info.frame_size_bytes > size) break;

        FrameIndexEntry entry;
        entry.byte_offset = pos;
        entry.frame_size = static_cast<uint32_t>(info.frame_size_bytes);
        entry.samples_per_frame = info.samples_per_frame;
        entry.channels = info.channels;
        entry.sample_offset = cumulative_samples;

        dec->frame_index.push_back(entry);
        cumulative_samples += info.samples_per_frame;

        pos += info.frame_size_bytes;
    }

    dec->total_samples_indexed = cumulative_samples;
    dec->frame_index_built = !dec->frame_index.empty();

    // Parse first frame header to populate frame info for pt7_mp3_get_info
    if (dec->frame_index_built) {
        const auto& first = dec->frame_index[0];
        if (pt7_mp3_parse_header(dec->source_data + first.byte_offset,
                                 dec->source_size - first.byte_offset,
                                 &dec->last_frame_info) == PT7_MP3_OK) {
            dec->has_frame_info = true;
        }
    }

    // Detect VBR info from first frame
    if (dec->frame_index_built && !dec->has_vbr_info) {
        const auto& first = dec->frame_index[0];
        if (first.byte_offset + first.frame_size <= dec->source_size) {
            pt7_mp3_frame_info_t first_info{};
            if (pt7_mp3_parse_header(dec->source_data + first.byte_offset,
                                     dec->source_size - first.byte_offset,
                                     &first_info) == PT7_MP3_OK) {
                if (pt7::mp3::VbrParser::detect(
                        dec->source_data + first.byte_offset,
                        first.frame_size,
                        first_info,
                        dec->vbr_info) == PT7_MP3_OK) {
                    dec->has_vbr_info = true;
                }
            }
        }
    }

    return dec->frame_index_built;
}

// Find the frame index closest to the given per-channel sample offset.
// Returns index into frame_index, or SIZE_MAX if not found.
size_t find_frame_for_sample(const std::vector<FrameIndexEntry>& index,
                             uint64_t target_sample)
{
    if (index.empty()) return SIZE_MAX;

    // Binary search for the frame containing target_sample
    size_t lo = 0, hi = index.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (index[mid].sample_offset + index[mid].samples_per_frame <= target_sample) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo >= index.size()) lo = index.size() - 1;
    return lo;
}

// Decode warmup frames to fill the bit reservoir before the target frame.
// The MP3 bit reservoir can reference up to 4096 bytes back.
// We decode frames starting from `warmup_start` up to (but not including)
// `target_frame`, discarding all PCM output.
void decode_warmup_frames(pt7_mp3_decoder* dec, size_t warmup_start, size_t target_frame)
{
    for (size_t i = warmup_start; i < target_frame; ++i) {
        const auto& entry = dec->frame_index[i];
        if (entry.byte_offset + entry.frame_size > dec->source_size) break;

        size_t consumed = 0;
        pt7_mp3_decode_frame(dec,
            dec->source_data + entry.byte_offset,
            dec->source_size - entry.byte_offset,
            &consumed);
        // Discard PCM — we only need the reservoir/overlap state
    }
}

// Determine how many warmup frames we need before the target.
// The bit reservoir max is 4096 bytes. We go back until we've accumulated
// at least 4096 bytes of frame data, or hit the beginning of the stream.
size_t find_warmup_start(const std::vector<FrameIndexEntry>& index,
                         size_t target_frame)
{
    if (target_frame == 0) return 0;

    constexpr uint64_t RESERVOIR_MAX = 4096;
    uint64_t accumulated = 0;
    size_t i = target_frame;

    while (i > 0 && accumulated < RESERVOIR_MAX) {
        --i;
        accumulated += index[i].frame_size;
    }

    return i;
}

} // anonymous namespace

pt7_mp3_status_t pt7_mp3_set_source(
    pt7_mp3_decoder_t* decoder,
    const uint8_t* data,
    size_t data_size)
{
    if (decoder == nullptr || (data == nullptr && data_size > 0)) {
        return PT7_MP3_ERR_INVALID_ARG;
    }
    if (data_size == 0) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    decoder->source_data = data;
    decoder->source_size = data_size;
    decoder->seekable = true;
    decoder->frame_index_built = false;
    decoder->frame_index.clear();

    // Reset decode state FIRST (clears VBR info, reservoir, etc.)
    pt7_mp3_reset(decoder);

    // Build the frame index (also detects VBR info from first frame)
    if (!build_frame_index(decoder)) {
        decoder->seekable = false;
        return PT7_MP3_ERR_SYNC_NOT_FOUND;
    }

    // Set up stream buffer with all source data so read_pcm16 works
    // immediately after set_source (no feed() needed in file mode).
    decoder->stream_in.assign(data, data + data_size);
    decoder->stream_pos = 0;

    return PT7_MP3_OK;
}

int pt7_mp3_is_seekable(const pt7_mp3_decoder_t* decoder)
{
    if (decoder == nullptr) return 0;
    return decoder->seekable ? 1 : 0;
}

pt7_mp3_status_t pt7_mp3_seek(
    pt7_mp3_decoder_t* decoder,
    double position_seconds,
    double* out_actual_seconds)
{
    if (decoder == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }
    if (position_seconds < 0.0) {
        return PT7_MP3_ERR_INVALID_ARG;
    }
    if (!decoder->seekable || !decoder->frame_index_built) {
        return PT7_MP3_ERR_SEEK_NOT_SUPPORTED;
    }
    if (decoder->frame_index.empty()) {
        return PT7_MP3_ERR_SEEK_NOT_SUPPORTED;
    }

    // Get sample rate from first frame
    pt7_mp3_frame_info_t first_info{};
    if (pt7_mp3_parse_header(
            decoder->source_data + decoder->frame_index[0].byte_offset,
            decoder->source_size - decoder->frame_index[0].byte_offset,
            &first_info) != PT7_MP3_OK) {
        return PT7_MP3_ERR_SEEK_NOT_SUPPORTED;
    }

    const uint32_t sample_rate = first_info.sample_rate_hz;
    if (sample_rate == 0) {
        return PT7_MP3_ERR_SEEK_NOT_SUPPORTED;
    }

    // Convert position to per-channel sample offset
    // If gapless is enabled, adjust for encoder delay
    uint64_t target_sample = static_cast<uint64_t>(position_seconds * sample_rate);
    if (decoder->gapless_enabled && decoder->vbr_info.has_gapless) {
        target_sample += decoder->vbr_info.encoder_delay;
    }

    // Check if beyond end
    if (target_sample >= decoder->total_samples_indexed) {
        if (out_actual_seconds != nullptr) {
            *out_actual_seconds = static_cast<double>(decoder->total_samples_indexed) / sample_rate;
        }
        return PT7_MP3_ERR_SEEK_OUT_OF_RANGE;
    }

    // Find the target frame
    size_t target_frame = find_frame_for_sample(decoder->frame_index, target_sample);
    if (target_frame == SIZE_MAX) {
        return PT7_MP3_ERR_SEEK_OUT_OF_RANGE;
    }

    // Find warmup start for bit reservoir
    size_t warmup_start = find_warmup_start(decoder->frame_index, target_frame);

    // Full decoder reset (clears reservoir, overlap, synthesis, gapless state)
    pt7_mp3_reset(decoder);

    // Re-enable gapless if it was enabled (but reset trimming counters)
    bool was_gapless = decoder->gapless_enabled;
    if (was_gapless && decoder->has_vbr_info && decoder->vbr_info.has_gapless) {
        decoder->gapless_enabled = true;
        const size_t channels = first_info.channels;
        const size_t tail_size = static_cast<size_t>(decoder->vbr_info.end_padding) * channels;
        decoder->gapless_tail_pcm16.resize(tail_size);
        decoder->gapless_tail_float.resize(tail_size);
        // For seeking, we don't re-apply front trimming — the seek position
        // is already on the gapless-adjusted timeline. We set front_skipped
        // to the delay so the trimming logic doesn't try to skip again.
        if (target_sample >= decoder->vbr_info.encoder_delay) {
            decoder->gapless_front_skipped = decoder->vbr_info.encoder_delay;
        }
    }

    // Decode warmup frames (fills reservoir + overlap, discards PCM)
    decode_warmup_frames(decoder, warmup_start, target_frame);

    // Now the decoder is positioned at the target frame.
    // Set up stream buffer with all remaining data from the source
    // so subsequent read calls can decode multiple frames.
    const auto& entry = decoder->frame_index[target_frame];
    const size_t remaining = decoder->source_size - entry.byte_offset;
    decoder->stream_in.assign(
        decoder->source_data + entry.byte_offset,
        decoder->source_data + entry.byte_offset + remaining
    );
    decoder->stream_pos = 0;

    // Calculate actual position achieved
    if (out_actual_seconds != nullptr) {
        uint64_t actual_sample = entry.sample_offset;
        if (decoder->gapless_enabled && decoder->vbr_info.has_gapless) {
            if (actual_sample >= decoder->vbr_info.encoder_delay) {
                actual_sample -= decoder->vbr_info.encoder_delay;
            }
        }
        *out_actual_seconds = static_cast<double>(actual_sample) / sample_rate;
    }

    return PT7_MP3_OK;
}

} // extern "C"
