#include "mp3_vbr.h"
#include <cstring>

namespace pt7::mp3 {

namespace {

inline uint32_t read_u32_be(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

inline uint16_t read_u16_be(const uint8_t* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) |
                                  static_cast<uint16_t>(p[1]));
}

size_t get_xing_offset(const pt7_mp3_frame_info_t& info)
{
    size_t off = info.header_bytes;
    if (info.version == PT7_MP3_VERSION_MPEG1) {
        off += (info.channels == 1) ? 17U : 32U;
    } else {
        off += (info.channels == 1) ? 9U : 17U;
    }
    return off;
}

} // anonymous namespace

pt7_mp3_status_t VbrParser::parse_xing(
    const uint8_t* frame_data,
    size_t frame_size,
    const pt7_mp3_frame_info_t& frame_info,
    pt7_mp3_vbr_info_t& out_vbr)
{
    if (frame_data == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    const size_t xing_off = get_xing_offset(frame_info);
    if (frame_size < xing_off + 8) {
        return PT7_MP3_NEED_MORE_DATA;
    }

    const uint8_t* p = frame_data + xing_off;
    bool is_xing = false;

    if (p[0] == 'X' && p[1] == 'i' && p[2] == 'n' && p[3] == 'g') {
        is_xing = true;
    } else if (p[0] == 'I' && p[1] == 'n' && p[2] == 'f' && p[3] == 'o') {
        is_xing = false;
    } else {
        return PT7_MP3_ERR_SYNC_NOT_FOUND;
    }

    out_vbr.has_vbr_header = 1;
    out_vbr.is_vbr = is_xing ? 1 : 0;

    const uint32_t flags = read_u32_be(p + 4);
    size_t cur = xing_off + 8;

    // Bit 0: Frames field (4 bytes)
    if ((flags & 0x0001U) != 0) {
        if (frame_size >= cur + 4) {
            out_vbr.total_frames = read_u32_be(frame_data + cur);
            cur += 4;
        }
    }

    // Bit 1: Bytes field (4 bytes)
    if ((flags & 0x0002U) != 0) {
        if (frame_size >= cur + 4) {
            out_vbr.total_bytes = read_u32_be(frame_data + cur);
            cur += 4;
        }
    }

    // Bit 2: TOC table (100 bytes)
    if ((flags & 0x0004U) != 0) {
        if (frame_size >= cur + 100) {
            std::memcpy(out_vbr.toc, frame_data + cur, 100);
            out_vbr.has_toc = 1;
            cur += 100;
        }
    }

    // Bit 3: Quality indicator (4 bytes)
    if ((flags & 0x0008U) != 0) {
        if (frame_size >= cur + 4) {
            out_vbr.quality = read_u32_be(frame_data + cur);
            cur += 4;
        }
    }

    // LAME / Lavc extension check (usually located right after Xing fields)
    if (frame_size >= cur + 24) {
        const uint8_t* lame = frame_data + cur;
        // Check for "LAME" or "Lavc" or "Lavf" signature
        if ((std::memcmp(lame, "LAME", 4) == 0) || (std::memcmp(lame, "Lavc", 4) == 0) || (std::memcmp(lame, "Lavf", 4) == 0)) {
            std::memcpy(out_vbr.encoder, lame, 9);
            out_vbr.encoder[9] = '\0';

            // LAME gapless delay and padding at offset 21 from LAME tag
            if (frame_size >= cur + 24) {
                const uint8_t* gapless = lame + 21;
                const uint32_t delay = (static_cast<uint32_t>(gapless[0]) << 4) |
                                       (static_cast<uint32_t>(gapless[1]) >> 4);
                const uint32_t padding = ((static_cast<uint32_t>(gapless[1]) & 0x0FU) << 8) |
                                          static_cast<uint32_t>(gapless[2]);
                out_vbr.has_gapless = 1;
                out_vbr.encoder_delay = delay;
                out_vbr.end_padding = padding;
            }
        }
    }

    return PT7_MP3_OK;
}

pt7_mp3_status_t VbrParser::parse_vbri(
    const uint8_t* frame_data,
    size_t frame_size,
    const pt7_mp3_frame_info_t& frame_info,
    pt7_mp3_vbr_info_t& out_vbr)
{
    (void)frame_info;
    if (frame_data == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }

    // VBRI is always at offset 36 from frame start (after 32 bytes of audio data)
    constexpr size_t vbri_off = 36;
    if (frame_size < vbri_off + 26) {
        return PT7_MP3_ERR_SYNC_NOT_FOUND;
    }

    const uint8_t* p = frame_data + vbri_off;
    if (p[0] != 'V' || p[1] != 'B' || p[2] != 'R' || p[3] != 'I') {
        return PT7_MP3_ERR_SYNC_NOT_FOUND;
    }

    out_vbr.has_vbr_header = 1;
    out_vbr.is_vbr = 1;
    out_vbr.quality = read_u16_be(p + 8);
    out_vbr.total_bytes = read_u32_be(p + 10);
    out_vbr.total_frames = read_u32_be(p + 14);

    return PT7_MP3_OK;
}

pt7_mp3_status_t VbrParser::detect(
    const uint8_t* frame_data,
    size_t frame_size,
    const pt7_mp3_frame_info_t& frame_info,
    pt7_mp3_vbr_info_t& out_vbr)
{
    std::memset(&out_vbr, 0, sizeof(out_vbr));

    pt7_mp3_status_t st = parse_xing(frame_data, frame_size, frame_info, out_vbr);
    if (st == PT7_MP3_OK) {
        return PT7_MP3_OK;
    }

    st = parse_vbri(frame_data, frame_size, frame_info, out_vbr);
    if (st == PT7_MP3_OK) {
        return PT7_MP3_OK;
    }

    return PT7_MP3_ERR_SYNC_NOT_FOUND;
}

} // namespace pt7::mp3
