// PT7-MP3 Standalone Decoder Example
// Decodes an MP3 file to a 16-bit PCM WAV file using only PT7-MP3 + standard C/C++.
// No PT7::Audio, no WASAPI, no external audio backends.
//
// Usage: decode_file <input.mp3> <output.wav>

#include "pt7_mp3.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

// Write a 16-bit little-endian value
static void write_u16_le(std::ofstream& f, uint16_t v) {
    uint8_t b[2] = { static_cast<uint8_t>(v & 0xFF),
                     static_cast<uint8_t>((v >> 8) & 0xFF) };
    f.write(reinterpret_cast<const char*>(b), 2);
}

// Write a 32-bit little-endian value
static void write_u32_le(std::ofstream& f, uint32_t v) {
    uint8_t b[4] = { static_cast<uint8_t>(v & 0xFF),
                     static_cast<uint8_t>((v >> 8) & 0xFF),
                     static_cast<uint8_t>((v >> 16) & 0xFF),
                     static_cast<uint8_t>((v >> 24) & 0xFF) };
    f.write(reinterpret_cast<const char*>(b), 4);
}

// Write a WAV header for 16-bit PCM
static void write_wav_header(std::ofstream& f, uint32_t sample_rate,
                             uint16_t channels, uint32_t total_samples) {
    const uint16_t bits_per_sample = 16;
    const uint16_t block_align = channels * (bits_per_sample / 8);
    const uint32_t byte_rate = sample_rate * block_align;
    const uint32_t data_size = total_samples * (bits_per_sample / 8);
    const uint32_t fmt_chunk_size = 16;
    const uint32_t riff_size = 4 + (8 + fmt_chunk_size) + (8 + data_size);

    f.write("RIFF", 4);
    write_u32_le(f, riff_size);
    f.write("WAVE", 4);

    // fmt chunk
    f.write("fmt ", 4);
    write_u32_le(f, fmt_chunk_size);
    write_u16_le(f, 1); // PCM format
    write_u16_le(f, channels);
    write_u32_le(f, sample_rate);
    write_u32_le(f, byte_rate);
    write_u16_le(f, block_align);
    write_u16_le(f, bits_per_sample);

    // data chunk
    f.write("data", 4);
    write_u32_le(f, data_size);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.mp3> <output.wav>\n";
        std::cerr << "Decodes an MP3 file to 16-bit PCM WAV using PT7-MP3.\n";
        return 1;
    }

    const char* input_path = argv[1];
    const char* output_path = argv[2];

    // Read input MP3 file
    std::ifstream in_file(input_path, std::ios::binary | std::ios::ate);
    if (!in_file) {
        std::cerr << "Error: cannot open input file '" << input_path << "'\n";
        return 1;
    }
    const std::streamsize file_size = in_file.tellg();
    in_file.seekg(0, std::ios::beg);
    std::vector<uint8_t> mp3_data(static_cast<size_t>(file_size));
    if (!in_file.read(reinterpret_cast<char*>(mp3_data.data()), file_size)) {
        std::cerr << "Error: failed to read input file\n";
        return 1;
    }
    in_file.close();

    std::cout << "PT7-MP3 Standalone Decoder\n";
    std::cout << "Input  : " << input_path << " (" << file_size << " bytes)\n";

    // Create decoder
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    if (dec == nullptr) {
        std::cerr << "Error: failed to create decoder\n";
        return 1;
    }

    // Feed entire MP3 data
    pt7_mp3_status_t st = pt7_mp3_feed(dec, mp3_data.data(), mp3_data.size());
    if (st != PT7_MP3_OK) {
        std::cerr << "Error: feed failed: " << pt7_mp3_status_string(st) << "\n";
        pt7_mp3_destroy(dec);
        return 1;
    }

    // Decode all frames and collect PCM16
    std::vector<int16_t> all_pcm;
    std::vector<int16_t> chunk(4608);
    pt7_mp3_frame_info_t info{};
    bool info_reported = false;
    size_t frames_decoded = 0;

    while (true) {
        size_t written = 0;
        st = pt7_mp3_read_pcm16(dec, chunk.data(), chunk.size(), &written);

        if (written > 0) {
            if (!info_reported) {
                pt7_mp3_get_frame_info(dec, &info);
                info_reported = true;
                std::cout << "Format : MPEG-" << (info.version == PT7_MP3_VERSION_MPEG1 ? "1" :
                                       info.version == PT7_MP3_VERSION_MPEG2 ? "2" : "2.5") << "\n";
                std::cout << "Rate   : " << info.sample_rate_hz << " Hz\n";
                std::cout << "Channels: " << info.channels << "\n";
                std::cout << "Bitrate: " << info.bitrate_kbps << " kbps\n";
            }
            all_pcm.insert(all_pcm.end(), chunk.begin(), chunk.begin() + written);
            frames_decoded++;
        }

        if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && written == 0)) {
            break;
        }
        if (st != PT7_MP3_OK) {
            std::cerr << "Warning: " << pt7_mp3_status_string(st) << "\n";
            break;
        }
    }

    pt7_mp3_destroy(dec);

    if (all_pcm.empty()) {
        std::cerr << "Error: no audio decoded\n";
        return 1;
    }

    // Write WAV file
    std::ofstream out_file(output_path, std::ios::binary);
    if (!out_file) {
        std::cerr << "Error: cannot create output file '" << output_path << "'\n";
        return 1;
    }

    const uint16_t channels = static_cast<uint16_t>(info.channels);
    const uint32_t sample_rate = info.sample_rate_hz;
    const uint32_t total_samples = static_cast<uint32_t>(all_pcm.size());

    write_wav_header(out_file, sample_rate, channels, total_samples);
    out_file.write(reinterpret_cast<const char*>(all_pcm.data()),
                   static_cast<std::streamsize>(total_samples * sizeof(int16_t)));
    out_file.close();

    const double duration = (channels > 0 && sample_rate > 0)
        ? static_cast<double>(total_samples / channels) / sample_rate : 0.0;

    std::cout << "Output : " << output_path << "\n";
    std::cout << "Frames : " << frames_decoded << "\n";
    std::cout << "Samples: " << total_samples << " (" << duration << " s)\n";
    std::cout << "Done.\n";

    return 0;
}
