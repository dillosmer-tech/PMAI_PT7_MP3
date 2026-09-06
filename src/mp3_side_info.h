#ifndef PT7_MP3_SIDE_INFO_H
#define PT7_MP3_SIDE_INFO_H

#include "pt7_mp3.h"
#include <cstdint>
#include <cstddef>

namespace pt7::mp3 {

struct SideInfo {
    uint32_t main_data_begin{0};
    uint32_t private_bits{0};
    uint32_t scfsi[2][4]{}; // [ch][scfsi_band]
    pt7_mp3_granule_info_t granules[2][2]{}; // [gr][ch]
};

class SideInfoParser {
public:
    static pt7_mp3_status_t parse(
        const uint8_t* buffer,
        size_t buffer_size,
        const pt7_mp3_frame_info_t& frame_info,
        SideInfo& out_side_info
    );
};

} // namespace pt7::mp3

#endif // PT7_MP3_SIDE_INFO_H
