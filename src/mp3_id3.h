#ifndef PT7_MP3_ID3_H
#define PT7_MP3_ID3_H

#include "pt7_mp3.h"
#include <cstddef>
#include <cstdint>

namespace pt7::mp3 {

/**
 * @brief Lightweight ID3v2 tag detector and skipper.
 *
 * Dedicated to identifying ID3v2 tags (v2.2, v2.3, v2.4) and calculating
 * their exact byte length (including 10-byte header, extended header,
 * synchsafe payload, and optional 10-byte footer) so audio decoders
 * never mistake ID3 metadata for MP3 audio frames.
 */
class Id3Parser {
public:
    /**
     * @brief Detects an ID3v2 tag at the start of buffer and calculates its total size.
     *
     * @param buffer Pointer to input byte stream.
     * @param buffer_size Number of bytes available.
     * @param out_tag_size Receives total tag size in bytes (header + payload + footer).
     * @return PT7_MP3_OK if an ID3v2 tag is present and valid,
     *         PT7_MP3_NEED_MORE_DATA if buffer is too small to determine tag size or payload is truncated,
     *         PT7_MP3_ERR_SYNC_NOT_FOUND if buffer does not begin with "ID3",
     *         PT7_MP3_ERR_INVALID_HEADER if tag header is malformed.
     */
    static pt7_mp3_status_t detect_and_size(
        const uint8_t* buffer,
        size_t buffer_size,
        size_t& out_tag_size
    );

    /**
     * @brief Decodes a 4-byte big-endian synchsafe integer (7 bits per byte).
     */
    static uint32_t decode_synchsafe_u32(const uint8_t bytes[4]);
};

} // namespace pt7::mp3

#endif // PT7_MP3_ID3_H
