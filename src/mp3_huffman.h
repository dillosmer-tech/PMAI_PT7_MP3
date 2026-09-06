#ifndef PT7_MP3_HUFFMAN_H
#define PT7_MP3_HUFFMAN_H

#include "pt7_mp3.h"
#include "mp3_bitstream.h"
#include <cstddef>

namespace pt7::mp3 {

class HuffmanDecoder {
public:
    /**
     * @brief Decodes big_values, count1, and zero region samples for a granule.
     *
     * @param bs Bitstream reader positioned at the start of Huffman bits.
     * @param frame_info Frame header metadata.
     * @param gi Granule information structure with side info and scalefactors.
     * @param huffman_bits_available Total number of bits allocated to Huffman data in this granule.
     * @return Status code.
     */
    static pt7_mp3_status_t decode(
        BitstreamReader& bs,
        const pt7_mp3_frame_info_t& frame_info,
        pt7_mp3_granule_info_t& gi,
        size_t huffman_bits_available
    );
};

} // namespace pt7::mp3

#endif // PT7_MP3_HUFFMAN_H
