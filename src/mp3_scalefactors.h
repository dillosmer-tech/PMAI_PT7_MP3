#ifndef PT7_MP3_SCALEFACTORS_H
#define PT7_MP3_SCALEFACTORS_H

#include "pt7_mp3.h"
#include "mp3_side_info.h"
#include "mp3_bitstream.h"
#include <cstddef>
#include <cstdint>

namespace pt7::mp3 {

class ScalefactorDecoder {
public:
    static pt7_mp3_status_t decode(
        BitstreamReader& bs,
        const pt7_mp3_frame_info_t& frame_info,
        const SideInfo& side_info,
        uint32_t gr,
        uint32_t ch,
        pt7_mp3_granule_info_t& gi,
        size_t& out_bits_read
    );
};

} // namespace pt7::mp3

#endif // PT7_MP3_SCALEFACTORS_H
