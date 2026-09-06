#include "pt7_mp3.h"
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <cstring>

static std::mt19937 g_rng(133742);

static uint8_t rand_u8()
{
    return static_cast<uint8_t>(g_rng() & 0xFF);
}

int main(int argc, char* argv[])
{
    int iterations = 2000;
    if (argc >= 2) {
        iterations = std::atoi(argv[1]);
        if (iterations <= 0) iterations = 2000;
    }

    std::cout << "========================================\n";
    std::cout << " PT7-MP3 Fuzz Testing Suite             \n";
    std::cout << " Iterations: " << iterations << "\n";
    std::cout << "========================================\n";

    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    if (dec == nullptr) {
        std::cerr << "Cannot create decoder\n";
        return 1;
    }

    int crashes = 0;
    int nan_infs = 0;

    const auto t_start = std::chrono::high_resolution_clock::now();

    for (int iter = 0; iter < iterations; ++iter) {
        // Mode 1: Pure random bytes (length 0 to 4096)
        {
            const size_t len = g_rng() % 4096;
            std::vector<uint8_t> rand_buf(len);
            for (size_t i = 0; i < len; ++i) rand_buf[i] = rand_u8();

            size_t off = 0;
            pt7_mp3_frame_info_t info{};
            pt7_mp3_scan_frame(rand_buf.data(), rand_buf.size(), &off, &info);

            int16_t pcm16[2304];
            size_t written = 0, consumed = 0;
            pt7_mp3_decode_frame_pcm16(dec, rand_buf.data(), rand_buf.size(), pcm16, 2304, &written, &consumed);

            pt7_mp3_reset(dec);
        }

        // Mode 2: Semi-valid frame headers with random payload
        {
            uint8_t semi_valid[1024];
            // Put sync word and plausible header bits
            semi_valid[0] = 0xFF;
            semi_valid[1] = 0xFB; // MPEG-1 Layer III
            semi_valid[2] = static_cast<uint8_t>((g_rng() & 0xF0) | ((g_rng() % 3) << 2)); // random bitrate, valid sample rate
            semi_valid[3] = rand_u8();

            for (size_t i = 4; i < sizeof(semi_valid); ++i) semi_valid[i] = rand_u8();

            size_t off = 0;
            pt7_mp3_frame_info_t info{};
            pt7_mp3_scan_frame(semi_valid, sizeof(semi_valid), &off, &info);

            float pcm_flt[2304];
            size_t written = 0, consumed = 0;
            pt7_mp3_status_t st = pt7_mp3_decode_frame_float(dec, semi_valid, sizeof(semi_valid), pcm_flt, 2304, &written, &consumed);

            if (st == PT7_MP3_OK) {
                for (size_t i = 0; i < written; ++i) {
                    if (std::isnan(pcm_flt[i]) || std::isinf(pcm_flt[i])) {
                        nan_infs++;
                    }
                }
            }

            pt7_mp3_reset(dec);
        }

        // Mode 3: Streaming random feeds and rapid reads
        {
            const size_t num_chunks = 1 + (g_rng() % 10);
            for (size_t c = 0; c < num_chunks; ++c) {
                const size_t chunk_len = 1 + (g_rng() % 512);
                std::vector<uint8_t> chunk(chunk_len);
                for (size_t i = 0; i < chunk_len; ++i) chunk[i] = rand_u8();

                pt7_mp3_feed(dec, chunk.data(), chunk.size());

                int16_t out_pcm[2304];
                size_t written = 0;
                pt7_mp3_read_pcm16(dec, out_pcm, 2304, &written);
            }
            pt7_mp3_flush(dec);
            pt7_mp3_reset(dec);
        }

        if ((iter + 1) % 500 == 0) {
            std::cout << "  Completed " << (iter + 1) << " / " << iterations << " iterations...\n";
        }
    }

    const auto t_end = std::chrono::high_resolution_clock::now();
    const double elapsed_s = std::chrono::duration<double>(t_end - t_start).count();

    std::cout << "----------------------------------------\n";
    std::cout << "Fuzzing Completed in " << elapsed_s << " s\n";
    std::cout << "Crashes:   " << crashes << "\n";
    std::cout << "NaNs/Infs: " << nan_infs << "\n";
    std::cout << "========================================\n";

    pt7_mp3_destroy(dec);
    return (crashes == 0 && nan_infs == 0) ? 0 : 1;
}
