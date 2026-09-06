#include "mp3_id3.h"

namespace pt7::mp3 {

uint32_t Id3Parser::decode_synchsafe_u32(const uint8_t bytes[4])
{
    return ((static_cast<uint32_t>(bytes[0] & 0x7FU) << 21) |
            (static_cast<uint32_t>(bytes[1] & 0x7FU) << 14) |
            (static_cast<uint32_t>(bytes[2] & 0x7FU) << 7)  |
             static_cast<uint32_t>(bytes[3] & 0x7FU));
}

pt7_mp3_status_t Id3Parser::detect_and_size(
    const uint8_t* buffer,
    size_t buffer_size,
    size_t& out_tag_size)
{
    out_tag_size = 0;
    if (buffer == nullptr) {
        return PT7_MP3_ERR_INVALID_ARG;
    }
    if (buffer_size < 3) {
        return PT7_MP3_NEED_MORE_DATA;
    }

    // Check "ID3" identifier
    if (buffer[0] != 'I' || buffer[1] != 'D' || buffer[2] != '3') {
        return PT7_MP3_ERR_SYNC_NOT_FOUND;
    }

    // Need at least 10 bytes for full ID3v2 header
    if (buffer_size < 10) {
        return PT7_MP3_NEED_MORE_DATA;
    }

    const uint8_t major = buffer[3];
    const uint8_t flags = buffer[5];

    // Supported major versions: ID3v2.2, 2.3, 2.4
    if (major < 2 || major > 4) {
        return PT7_MP3_ERR_INVALID_HEADER;
    }

    // Check that synchsafe size bytes have top bit clear
    for (int i = 6; i <= 9; ++i) {
        if ((buffer[i] & 0x80U) != 0) {
            return PT7_MP3_ERR_INVALID_HEADER;
        }
    }

    const uint32_t payload_size = decode_synchsafe_u32(buffer + 6);
    size_t total_tag_size = 10U + payload_size;

    // In ID3v2.4, bit 4 indicates a 10-byte footer follows the payload
    if (major == 4 && (flags & 0x10U) != 0) {
        total_tag_size += 10U;
    }

    out_tag_size = total_tag_size;
    return PT7_MP3_OK;
}

} // namespace pt7::mp3
