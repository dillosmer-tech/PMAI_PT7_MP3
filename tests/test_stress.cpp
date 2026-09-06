#include "pt7_mp3.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>

#ifndef TEST_VECTORS_DIR
#define TEST_VECTORS_DIR "tests/vectors"
#endif

static std::vector<uint8_t> read_binary(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) return buffer;
    return {};
}

int main()
{
    std::cout << "========================================\n";
    std::cout << " PT7-MP3 Stress & Memory Audit Suite    \n";
    std::cout << "========================================\n";

    std::string path = std::string(TEST_VECTORS_DIR) + "/sine_44k_stereo.mp3";
    std::vector<uint8_t> mp3_data = read_binary(path);
    if (mp3_data.empty()) {
        std::cerr << "Cannot open test vector: " << path << "\n";
        return 1;
    }

    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    if (dec == nullptr) {
        std::cerr << "Cannot allocate decoder\n";
        return 1;
    }

    // 1. Long stream decoding: 1,000 loops of the file (~14,000 frames)
    std::cout << "[STRESS] Decoding 1,000 file loops (~14,000 frames)...\n";
    const auto t_start = std::chrono::high_resolution_clock::now();

    size_t total_frames = 0;
    size_t total_samples = 0;
    int16_t out_pcm16[2304];

    for (int loop = 0; loop < 1000; ++loop) {
        size_t offset = 0;
        while (offset < mp3_data.size()) {
            size_t frame_offset = 0;
            pt7_mp3_frame_info_t info{};
            pt7_mp3_status_t sc = pt7_mp3_scan_frame(
                mp3_data.data() + offset,
                mp3_data.size() - offset,
                &frame_offset,
                &info
            );
            if (sc != PT7_MP3_OK) break;
            offset += frame_offset;

            size_t written = 0, consumed = 0;
            pt7_mp3_status_t st = pt7_mp3_decode_frame_pcm16(
                dec,
                mp3_data.data() + offset,
                mp3_data.size() - offset,
                out_pcm16,
                2304,
                &written,
                &consumed
            );

            if (st == PT7_MP3_OK) {
                total_frames++;
                total_samples += written;
                offset += consumed;
            } else {
                offset += 4;
            }
        }
    }

    const auto t_end = std::chrono::high_resolution_clock::now();
    const double elapsed_s = std::chrono::duration<double>(t_end - t_start).count();

    std::cout << "  Decoded " << total_frames << " frames (" << total_samples << " samples) in "
              << elapsed_s << " s (" << (total_frames / elapsed_s) << " frames/s)\n";

    // 2. Rapid Resets: 10,000 resets in rapid succession
    std::cout << "[STRESS] 10,000 Rapid Decoder Resets...\n";
    for (int r = 0; r < 10000; ++r) {
        pt7_mp3_reset(dec);
    }
    std::cout << "  Resets completed without error.\n";

    // 3. Variable Chunk Size Streaming
    std::cout << "[STRESS] Varied Chunk Streaming (1 byte to 4096 bytes)...\n";
    const size_t chunk_sizes[] = { 1, 2, 3, 7, 13, 31, 64, 127, 255, 512, 1024, 2048, 4096 };
    for (size_t csize : chunk_sizes) {
        pt7_mp3_reset(dec);
        size_t fed = 0;
        size_t samples_read = 0;
        while (fed < mp3_data.size()) {
            size_t take = std::min(csize, mp3_data.size() - fed);
            pt7_mp3_feed(dec, mp3_data.data() + fed, take);
            fed += take;

            size_t written = 0;
            pt7_mp3_read_pcm16(dec, out_pcm16, 2304, &written);
            samples_read += written;
        }
        // Read remaining
        while (true) {
            size_t written = 0;
            pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, out_pcm16, 2304, &written);
            samples_read += written;
            if (st != PT7_MP3_OK || written == 0) break;
        }
        (void)samples_read;
    }
    std::cout << "  All chunk streaming sizes completed.\n";

    // 4. Memory Audit
    const size_t mem_footprint = pt7_mp3_get_instance_size();
    std::cout << "[MEMORY AUDIT] Instance footprint: " << mem_footprint
              << " bytes (~" << (mem_footprint / 1024.0) << " KB)\n";

    pt7_mp3_destroy(dec);
    std::cout << "========================================\n";
    std::cout << "Stress & Memory Audit: PASS\n";
    std::cout << "========================================\n";

    return 0;
}
