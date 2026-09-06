// PT7-MP3 Seek Demo
// Demonstrates: MP3 → seek(30s) → decode → PCM16 WAV
// Uses only PT7-MP3 + standard C/C++. No PT7::Audio or external backends.
//
// Usage: seek_demo <input.mp3> <output.wav> [seek_seconds]

#include "pt7_mp3.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

static void write_u16_le(std::ofstream& f, uint16_t v) {
    uint8_t b[2] = { static_cast<uint8_t>(v & 0xFF),
                     static_cast<uint8_t>((v >> 8) & 0xFF) };
    f.write(reinterpret_cast<const char*>(b), 2);
}

static void write_u32_le(std::ofstream& f, uint32_t v) {
    uint8_t b[4] = { static_cast<uint8_t>(v & 0xFF),
                     static_cast<uint8_t>((v >> 8) & 0xFF),
                     static_cast<uint8_t>((v >> 16) & 0xFF),
                     static_cast<uint8_t>((v >> 24) & 0xFF) };
    f.write(reinterpret_cast<const char*>(b), 4);
}

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
    f.write("fmt ", 4);
    write_u32_le(f, fmt_chunk_size);
    write_u16_le(f, 1);
    write_u16_le(f, channels);
    write_u32_le(f, sample_rate);
    write_u32_le(f, byte_rate);
    write_u16_le(f, block_align);
    write_u16_le(f, bits_per_sample);
    f.write("data", 4);
    write_u32_le(f, data_size);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.mp3> <output.wav> [seek_seconds]\n";
        return 1;
    }

    const char* input_path = argv[1];
    const char* output_path = argv[2];
    double seek_seconds = (argc >= 4) ? std::atof(argv[3]) : 30.0;

    // Read entire MP3 file
    std::ifstream in_file(input_path, std::ios::binary | std::ios::ate);
    if (!in_file) {
        std::cerr << "Error: cannot open input file\n";
        return 1;
    }
    const std::streamsize file_size = in_file.tellg();
    in_file.seekg(0, std::ios::beg);
    std::vector<uint8_t> mp3_data(static_cast<size_t>(file_size));
    in_file.read(reinterpret_cast<char*>(mp3_data.data()), file_size);
    in_file.close();

    std::cout << "PT7-MP3 Seek Demo\n";
    std::cout << "Input: " << input_path << " (" << file_size << " bytes)\n";

    // Create decoder and set seekable source
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    if (!dec) {
        std::cerr << "Error: cannot create decoder\n";
        return 1;
    }

    pt7_mp3_status_t st = pt7_mp3_set_source(dec, mp3_data.data(), mp3_data.size());
    if (st != PT7_MP3_OK) {
        std::cerr << "Error: set_source failed: " << pt7_mp3_status_string(st) << "\n";
        pt7_mp3_destroy(dec);
        return 1;
    }

    std::cout << "Seekable: " << (pt7_mp3_is_seekable(dec) ? "yes" : "no") << "\n";

    // Get stream info
    pt7_mp3_stream_info_t info{};
    pt7_mp3_get_info(dec, &info);
    std::cout << "Format: MPEG-" << (info.version == PT7_MP3_VERSION_MPEG1 ? "1" :
                          info.version == PT7_MP3_VERSION_MPEG2 ? "2" : "2.5") << "\n";
    std::cout << "Rate: " << info.sample_rate_hz << " Hz, Channels: " << info.channels << "\n";
    std::cout << "Duration: " << info.duration_seconds << " s\n";
    std::cout << "Frames: " << info.total_frames << "\n";

    // Seek to requested position
    double actual_pos = 0;
    std::cout << "Seeking to " << seek_seconds << " s...\n";
    st = pt7_mp3_seek(dec, seek_seconds, &actual_pos);
    if (st != PT7_MP3_OK) {
        std::cerr << "Error: seek failed: " << pt7_mp3_status_string(st) << "\n";
        pt7_mp3_destroy(dec);
        return 1;
    }
    std::cout << "Actual position: " << actual_pos << " s\n";

    // Decode 10 seconds of audio from seek position
    const double decode_duration = 10.0;
    const size_t max_samples = static_cast<size_t>(
        decode_duration * info.sample_rate_hz * info.channels);
    std::vector<int16_t> pcm(max_samples);
    size_t total_written = 0;

    while (total_written < max_samples) {
        size_t written = 0;
        size_t to_read = std::min<size_t>(4608, max_samples - total_written);
        st = pt7_mp3_read_pcm16(dec, pcm.data() + total_written, to_read, &written);
        if (written > 0) total_written += written;
        if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && written == 0)) break;
        if (st != PT7_MP3_OK && st != PT7_MP3_ERR_DECODE) break;
    }

    std::cout << "Decoded " << total_written << " samples ("
              << static_cast<double>(total_written / info.channels) / info.sample_rate_hz
              << " s)\n";

    // Write WAV
    std::ofstream out_file(output_path, std::ios::binary);
    if (!out_file) {
        std::cerr << "Error: cannot create output file\n";
        pt7_mp3_destroy(dec);
        return 1;
    }
    write_wav_header(out_file, info.sample_rate_hz,
                     static_cast<uint16_t>(info.channels),
                     static_cast<uint32_t>(total_written));
    out_file.write(reinterpret_cast<const char*>(pcm.data()),
                   static_cast<std::streamsize>(total_written * sizeof(int16_t)));
    out_file.close();

    std::cout << "Output: " << output_path << "\nDone.\n";

    pt7_mp3_destroy(dec);
    return 0;
}
