#include "mp3_frame.h"
#include "mp3_id3.h"
#include <cstring>

namespace pt7::mp3 {

namespace {

constexpr uint32_t MPEG1_L3_BITRATES[16] = {
    0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
};

constexpr uint32_t MPEG2_L3_BITRATES[16] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0
};

constexpr uint32_t SAMPLE_RATES[4][4] = {
    // 00: MPEG-2.5
    { 11025, 12000, 8000, 0 },
    // 01: Reserved
    { 0, 0, 0, 0 },
    // 10: MPEG-2
    { 22050, 24000, 16000, 0 },
    // 11: MPEG-1
    { 44100, 48000, 32000, 0 }
};

} // anonymous namespace

pt7_mp3_status_t FrameParser::decode_header_word(
    uint32_t header_word,
    pt7_mp3_frame_info_t* out_info)
{
    if (out_info == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    // Sync word: 11 bits set to 1 (0xFFE00000)
    if ((header_word & 0xFFE00000U) != 0xFFE00000U) {
        return PT7_MP3_ERR_INVALID_HEADER;
    }

    const uint32_t version_bits = (header_word >> 19) & 0x03U;
    if (version_bits == 1) {
        // Version 01 is reserved in MPEG specification
        return PT7_MP3_ERR_INVALID_HEADER;
    }

    const uint32_t layer_bits = (header_word >> 17) & 0x03U;
    if (layer_bits == 0) {
        // Layer 00 is reserved
        return PT7_MP3_ERR_INVALID_HEADER;
    }
    if (layer_bits != 1) {
        // Layer I (3) or Layer II (2) - not supported by MP3 (Layer III) decoder
        return PT7_MP3_ERR_UNSUPPORTED;
    }

    const uint32_t protection_bit = (header_word >> 16) & 0x01U;
    const uint32_t bitrate_idx   = (header_word >> 12) & 0x0FU;
    const uint32_t srate_idx     = (header_word >> 10) & 0x03U;
    const uint32_t padding_bit   = (header_word >> 9)  & 0x01U;
    const uint32_t private_bit   = (header_word >> 8)  & 0x01U;
    const uint32_t channel_mode  = (header_word >> 6)  & 0x03U;
    const uint32_t mode_ext      = (header_word >> 4)  & 0x03U;
    const uint32_t copyright_bit = (header_word >> 3)  & 0x01U;
    const uint32_t original_bit  = (header_word >> 2)  & 0x01U;
    const uint32_t emphasis_bits = header_word & 0x03U;

    // Bitrate checks
    if (bitrate_idx == 0x0F) {
        // 1111 is invalid
        return PT7_MP3_ERR_INVALID_HEADER;
    }
    if (bitrate_idx == 0x00) {
        // 0000 is free bitrate (unsupported)
        return PT7_MP3_ERR_UNSUPPORTED;
    }

    // Sample rate check
    if (srate_idx == 3) {
        // 11 is reserved
        return PT7_MP3_ERR_INVALID_HEADER;
    }

    // Emphasis check
    if (emphasis_bits == 2) {
        // 10 is reserved
        return PT7_MP3_ERR_INVALID_HEADER;
    }

    const auto version = static_cast<pt7_mp3_version_t>(version_bits);
    const auto layer = static_cast<pt7_mp3_layer_t>(layer_bits);

    uint32_t bitrate_kbps = 0;
    if (version == PT7_MP3_VERSION_MPEG1) {
        bitrate_kbps = MPEG1_L3_BITRATES[bitrate_idx];
    } else {
        bitrate_kbps = MPEG2_L3_BITRATES[bitrate_idx];
    }

    const uint32_t sample_rate_hz = SAMPLE_RATES[version_bits][srate_idx];
    if (sample_rate_hz == 0 || bitrate_kbps == 0) {
        return PT7_MP3_ERR_INVALID_HEADER;
    }

    const uint32_t samples_per_frame = (version == PT7_MP3_VERSION_MPEG1) ? 1152U : 576U;

    // Calculate frame length in bytes according to ISO standard
    size_t frame_size = 0;
    if (version == PT7_MP3_VERSION_MPEG1) {
        frame_size = static_cast<size_t>((144000ULL * bitrate_kbps) / sample_rate_hz + padding_bit);
    } else {
        frame_size = static_cast<size_t>((72000ULL * bitrate_kbps) / sample_rate_hz + padding_bit);
    }

    const uint32_t channels = (channel_mode == 3) ? 1U : 2U;
    size_t side_info_bytes = 0;
    if (version == PT7_MP3_VERSION_MPEG1) {
        side_info_bytes = (channels == 1) ? 17U : 32U;
    } else {
        side_info_bytes = (channels == 1) ? 9U : 17U;
    }

    const uint32_t has_crc = (protection_bit == 0) ? 1U : 0U;
    const size_t header_bytes = has_crc ? 6U : 4U;

    if (frame_size < header_bytes + side_info_bytes) {
        return PT7_MP3_ERR_INVALID_HEADER;
    }

    out_info->version = version;
    out_info->layer = layer;
    out_info->bitrate_kbps = bitrate_kbps;
    out_info->sample_rate_hz = sample_rate_hz;
    out_info->channels = channels;
    out_info->channel_mode = static_cast<pt7_mp3_channel_mode_t>(channel_mode);
    out_info->mode_extension = mode_ext;
    out_info->has_crc = has_crc;
    out_info->padding = padding_bit;
    out_info->private_bit = private_bit;
    out_info->copyright = copyright_bit;
    out_info->original = original_bit;
    out_info->emphasis = static_cast<pt7_mp3_emphasis_t>(emphasis_bits);
    out_info->frame_size_bytes = frame_size;
    out_info->samples_per_frame = samples_per_frame;
    out_info->header_bytes = header_bytes;
    out_info->side_info_bytes = side_info_bytes;

    return PT7_MP3_OK;
}

pt7_mp3_status_t FrameParser::parse_header(
    const uint8_t* buffer,
    size_t buffer_size,
    pt7_mp3_frame_info_t* out_info)
{
    if (buffer == nullptr || out_info == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    if (buffer_size < 4) {
        return PT7_MP3_NEED_MORE_DATA;
    }

    const uint32_t header_word = (static_cast<uint32_t>(buffer[0]) << 24) |
                                 (static_cast<uint32_t>(buffer[1]) << 16) |
                                 (static_cast<uint32_t>(buffer[2]) << 8)  |
                                  static_cast<uint32_t>(buffer[3]);

    const pt7_mp3_status_t status = decode_header_word(header_word, out_info);
    if (status != PT7_MP3_OK) {
        return status;
    }

    if (buffer_size < out_info->frame_size_bytes) {
        return PT7_MP3_NEED_MORE_DATA;
    }

    return PT7_MP3_OK;
}

#include "mp3_id3.h"

pt7_mp3_status_t FrameParser::scan_frame(
    const uint8_t* buffer,
    size_t buffer_size,
    size_t* out_offset,
    pt7_mp3_frame_info_t* out_info)
{
    if (buffer == nullptr || out_offset == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    if (buffer_size < 4) {
        // Less than 4 bytes: check if starting an ID3 tag or sync word
        if (buffer_size >= 1 && (buffer[0] == 'I' || buffer[0] == 0xFF)) {
            *out_offset = 0;
            return PT7_MP3_NEED_MORE_DATA;
        }
        return PT7_MP3_NEED_MORE_DATA;
    }

    size_t start_idx = 0;

    // Check for ID3v2 tag at start of buffer
    size_t id3_size = 0;
    const pt7_mp3_status_t id3_st = Id3Parser::detect_and_size(buffer, buffer_size, id3_size);
    if (id3_st == PT7_MP3_OK) {
        if (buffer_size < id3_size) {
            *out_offset = 0;
            return PT7_MP3_NEED_MORE_DATA;
        }
        start_idx = id3_size;
    } else if (id3_st == PT7_MP3_NEED_MORE_DATA) {
        *out_offset = 0;
        return PT7_MP3_NEED_MORE_DATA;
    }

    pt7_mp3_frame_info_t temp_info;
    bool found_truncated_candidate = false;
    size_t truncated_offset = 0;

    for (size_t i = start_idx; i <= buffer_size - 4; ++i) {
        // Fast skip of embedded ID3 tag if encountered
        if (buffer[i] == 'I' && i + 10 <= buffer_size && buffer[i+1] == 'D' && buffer[i+2] == '3') {
            size_t embedded_id3 = 0;
            if (Id3Parser::detect_and_size(buffer + i, buffer_size - i, embedded_id3) == PT7_MP3_OK) {
                if (buffer_size - i < embedded_id3) {
                    *out_offset = i;
                    return PT7_MP3_NEED_MORE_DATA;
                }
                i += (embedded_id3 > 0 ? embedded_id3 - 1 : 0);
                continue;
            }
        }

        // Fast sync check on first byte
        if (buffer[i] != 0xFF) {
            continue;
        }

        // Second byte must have top 3 bits set (0xE0)
        if ((buffer[i + 1] & 0xE0U) != 0xE0U) {
            continue;
        }

        const uint32_t header_word = (static_cast<uint32_t>(buffer[i]) << 24) |
                                     (static_cast<uint32_t>(buffer[i + 1]) << 16) |
                                     (static_cast<uint32_t>(buffer[i + 2]) << 8)  |
                                      static_cast<uint32_t>(buffer[i + 3]);

        const pt7_mp3_status_t dec_status = decode_header_word(header_word, &temp_info);
        if (dec_status == PT7_MP3_OK) {
            // Valid header candidate found at offset i
            const size_t remaining = buffer_size - i;
            if (remaining < temp_info.frame_size_bytes) {
                // Buffer is truncated for this frame
                if (!found_truncated_candidate) {
                    found_truncated_candidate = true;
                    truncated_offset = i;
                    if (out_info != nullptr) {
                        *out_info = temp_info;
                    }
                }
                break;
            }

            // Multi-frame confirmation heuristic for resynchronization robustness:
            // If enough buffer remains for the next frame header, verify consistency
            if (remaining >= temp_info.frame_size_bytes + 4) {
                const uint8_t* next_p = buffer + i + temp_info.frame_size_bytes;
                if (next_p[0] == 0xFF && (next_p[1] & 0xE0U) == 0xE0U) {
                    const uint32_t next_word = (static_cast<uint32_t>(next_p[0]) << 24) |
                                               (static_cast<uint32_t>(next_p[1]) << 16) |
                                               (static_cast<uint32_t>(next_p[2]) << 8)  |
                                                static_cast<uint32_t>(next_p[3]);
                    pt7_mp3_frame_info_t next_info{};
                    if (decode_header_word(next_word, &next_info) == PT7_MP3_OK) {
                        // Matching version/layer confirms frame with high confidence
                        if (next_info.version == temp_info.version && next_info.layer == temp_info.layer) {
                            *out_offset = i;
                            if (out_info != nullptr) {
                                *out_info = temp_info;
                            }
                            return PT7_MP3_OK;
                        }
                    }
                }
                // If the next frame header is not valid, this candidate might be false sync in audio data;
                // but if no better frame is found, we can accept it if no multi-frame stream is available.
            }

            *out_offset = i;
            if (out_info != nullptr) {
                *out_info = temp_info;
            }
            return PT7_MP3_OK;
        }
    }

    if (found_truncated_candidate) {
        *out_offset = truncated_offset;
        return PT7_MP3_NEED_MORE_DATA;
    }

    // Check if buffer ends with potential sync bytes (e.g. trailing 0xFF or 0xFF 0xEx)
    for (size_t i = (buffer_size >= 3 ? buffer_size - 3 : 0); i < buffer_size; ++i) {
        if (buffer[i] == 0xFF) {
            if (i + 1 < buffer_size) {
                if ((buffer[i + 1] & 0xE0U) == 0xE0U) {
                    *out_offset = i;
                    return PT7_MP3_NEED_MORE_DATA;
                }
            } else {
                *out_offset = i;
                return PT7_MP3_NEED_MORE_DATA;
            }
        }
    }

    return PT7_MP3_ERR_SYNC_NOT_FOUND;
}

} // namespace pt7::mp3
