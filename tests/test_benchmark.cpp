// PT7-MP3 Phase E Benchmark Suite
// Measures: decode time, realtime factor, throughput, CPU time, memory, frame count
// Also benchmarks streaming with different chunk sizes and seeking performance.

#include "pt7_mp3.h"

#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {

struct BenchResult {
    double decode_time_sec;
    double cpu_time_sec;
    double audio_duration_sec;
    double realtime_factor;  // audio_duration / decode_time
    double throughput_mbs;   // MB/s of MP3 data
    size_t file_size_bytes;
    size_t frame_count;
    size_t sample_count;     // per-channel
    size_t peak_memory_kb;
    const char* filename;
};

struct PcmComparison {
    size_t sample_count_ref;
    size_t sample_count_test;
    int64_t max_abs_error;
    double rms_error;
    double snr_db;
    size_t clip_count_ref;
    size_t clip_count_test;
    size_t nan_inf_count;
    bool identical;
};

std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    std::streamsize sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

size_t get_peak_memory_kb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.PeakWorkingSetSize / 1024;
    }
    return 0;
#else
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return static_cast<size_t>(ru.ru_maxrss);
#endif
}

// Decode entire file, return PCM16 and timing
BenchResult benchmark_decode(const char* filename, const std::vector<uint8_t>& mp3) {
    BenchResult r{};
    r.filename = filename;
    r.file_size_bytes = mp3.size();

    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_set_source(dec, mp3.data(), mp3.size());

    // Get stream info for duration
    pt7_mp3_stream_info_t info{};
    pt7_mp3_get_info(dec, &info);
    r.audio_duration_sec = info.duration_seconds;

    std::vector<int16_t> pcm(4608);
    size_t total_samples = 0;

#ifdef _WIN32
    FILETIME creation, exit, kernel_start, user_start;
    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel_start, &user_start);
#endif

    auto wall_start = std::chrono::steady_clock::now();

    while (true) {
        size_t written = 0;
        pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
        if (written > 0) total_samples += written;
        if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && written == 0)) break;
        if (st != PT7_MP3_OK && st != PT7_MP3_ERR_DECODE) break;
    }

    auto wall_end = std::chrono::steady_clock::now();
    r.decode_time_sec = std::chrono::duration<double>(wall_end - wall_start).count();
    r.frame_count = 0;
    pt7_mp3_get_info(dec, &info);
    r.frame_count = static_cast<size_t>(info.frame_count);
    r.sample_count = total_samples / info.channels;
    r.peak_memory_kb = get_peak_memory_kb();

#ifdef _WIN32
    FILETIME kernel_end, user_end;
    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel_end, &user_end);
    ULARGE_INTEGER ks, ke, us, ue;
    ks.LowPart = kernel_start.dwLowDateTime; ks.HighPart = kernel_start.dwHighDateTime;
    ke.LowPart = kernel_end.dwLowDateTime; ke.HighPart = kernel_end.dwHighDateTime;
    us.LowPart = user_start.dwLowDateTime; us.HighPart = user_start.dwHighDateTime;
    ue.LowPart = user_end.dwLowDateTime; ue.HighPart = user_end.dwHighDateTime;
    r.cpu_time_sec = static_cast<double>((ke.QuadPart - ks.QuadPart) + (ue.QuadPart - us.QuadPart)) / 1e7;
#else
    r.cpu_time_sec = r.decode_time_sec; // approximation
#endif

    if (r.decode_time_sec > 0) {
        r.realtime_factor = r.audio_duration_sec / r.decode_time_sec;
        r.throughput_mbs = static_cast<double>(r.file_size_bytes) / (1024.0 * 1024.0) / r.decode_time_sec;
    }

    pt7_mp3_destroy(dec);
    return r;
}

// Decode and return all PCM16 samples
std::vector<int16_t> decode_all_pcm16(const std::vector<uint8_t>& mp3) {
    std::vector<int16_t> result;
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_set_source(dec, mp3.data(), mp3.size());

    std::vector<int16_t> pcm(4608);
    while (true) {
        size_t written = 0;
        pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
        if (written > 0) {
            result.insert(result.end(), pcm.begin(), pcm.begin() + written);
        }
        if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && written == 0)) break;
        if (st != PT7_MP3_OK && st != PT7_MP3_ERR_DECODE) break;
    }
    pt7_mp3_destroy(dec);
    return result;
}

// Compare two PCM16 streams
PcmComparison compare_pcm16(const std::vector<int16_t>& ref,
                             const std::vector<int16_t>& test) {
    PcmComparison c{};
    c.sample_count_ref = ref.size();
    c.sample_count_test = test.size();
    c.identical = (ref.size() == test.size());
    c.max_abs_error = 0;
    c.nan_inf_count = 0;
    c.clip_count_ref = 0;
    c.clip_count_test = 0;

    double sum_sq_error = 0.0;
    double sum_sq_signal = 0.0;
    size_t min_len = std::min(ref.size(), test.size());

    for (size_t i = 0; i < min_len; ++i) {
        int64_t diff = static_cast<int64_t>(ref[i]) - static_cast<int64_t>(test[i]);
        int64_t abs_diff = std::abs(diff);
        if (abs_diff > c.max_abs_error) c.max_abs_error = abs_diff;
        sum_sq_error += static_cast<double>(diff) * diff;
        sum_sq_signal += static_cast<double>(ref[i]) * ref[i];

        if (ref[i] == 32767 || ref[i] == -32768) c.clip_count_ref++;
        if (test[i] == 32767 || test[i] == -32768) c.clip_count_test++;
    }

    if (c.identical && min_len > 0) {
        bool all_same = true;
        for (size_t i = 0; i < min_len; ++i) {
            if (ref[i] != test[i]) { all_same = false; break; }
        }
        c.identical = all_same;
    }

    if (min_len > 0) {
        c.rms_error = std::sqrt(sum_sq_error / min_len);
        double rms_signal = std::sqrt(sum_sq_signal / min_len);
        if (c.rms_error > 0 && rms_signal > 0) {
            c.snr_db = 20.0 * std::log10(rms_signal / c.rms_error);
        } else {
            c.snr_db = 999.0; // perfect
        }
    }

    return c;
}

void print_bench(const BenchResult& r) {
    std::printf("  %-45s %7.1fs audio  %7.3fs decode  %6.1fx  %6.1f MB/s  %5zu frames  %6zu KB\n",
                r.filename, r.audio_duration_sec, r.decode_time_sec,
                r.realtime_factor, r.throughput_mbs,
                r.frame_count, r.peak_memory_kb);
}

void print_comparison(const PcmComparison& c) {
    std::printf("  samples ref=%zu test=%zu  max_err=%lld  rms=%.4f  SNR=%.1f dB  clips ref=%zu test=%zu  NaN/Inf=%zu  identical=%s\n",
                c.sample_count_ref, c.sample_count_test,
                static_cast<long long>(c.max_abs_error),
                c.rms_error, c.snr_db,
                c.clip_count_ref, c.clip_count_test,
                c.nan_inf_count,
                c.identical ? "YES" : "NO");
}

// Streaming benchmark with different chunk sizes
void benchmark_streaming(const char* filename, const std::vector<uint8_t>& mp3) {
    const size_t chunk_sizes[] = {1, 16, 256, 4096, 65536};
    const int num_chunks = 5;

    std::printf("\n  Streaming benchmark: %s (%zu bytes)\n", filename, mp3.size());
    std::printf("  %-12s  %10s  %10s  %10s  %8s\n",
                "Chunk size", "Decode time", "Frames", "Realtime", "MB/s");

    for (int ci = 0; ci < num_chunks; ++ci) {
        const size_t chunk_size = chunk_sizes[ci];

        pt7_mp3_decoder_t* dec = pt7_mp3_create();

        auto start = std::chrono::steady_clock::now();

        size_t offset = 0;
        std::vector<int16_t> pcm(4608);

        while (offset < mp3.size() || true) {
            // Feed a chunk
            if (offset < mp3.size()) {
                size_t to_feed = std::min(chunk_size, mp3.size() - offset);
                pt7_mp3_feed(dec, mp3.data() + offset, to_feed);
                offset += to_feed;
            }

            // Read available PCM
            while (true) {
                size_t written = 0;
                pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
                if (st == PT7_MP3_NEED_MORE_DATA) {
                    if (offset >= mp3.size()) goto done;
                    break;
                }
                if (st == PT7_MP3_OK && written == 0) {
                    if (offset >= mp3.size()) goto done;
                    break;
                }
                if (st != PT7_MP3_OK && st != PT7_MP3_ERR_DECODE) goto done;
            }
        }
        done:

        auto end = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(end - start).count();

        pt7_mp3_stream_info_t info{};
        pt7_mp3_get_info(dec, &info);
        double rt = (dt > 0 && info.duration_seconds > 0) ? info.duration_seconds / dt : 0;
        double mb = static_cast<double>(mp3.size()) / (1024.0 * 1024.0);
        double mbs = (dt > 0) ? mb / dt : 0;

        std::printf("  %-12zu  %9.3fs  %9zu  %8.1fx  %7.1f\n",
                    chunk_size, dt, static_cast<size_t>(info.frame_count), rt, mbs);

        pt7_mp3_destroy(dec);
    }
}

// Seeking benchmark
void benchmark_seeking(const char* filename, const std::vector<uint8_t>& mp3) {
    std::printf("\n  Seeking benchmark: %s (%zu bytes)\n", filename, mp3.size());

    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_set_source(dec, mp3.data(), mp3.size());

    pt7_mp3_stream_info_t info{};
    pt7_mp3_get_info(dec, &info);

    if (!pt7_mp3_is_seekable(dec)) {
        std::printf("  (not seekable)\n");
        pt7_mp3_destroy(dec);
        return;
    }

    // Measure set_source time (index building)
    auto t0 = std::chrono::steady_clock::now();
    pt7_mp3_set_source(dec, mp3.data(), mp3.size());
    auto t1 = std::chrono::steady_clock::now();
    double index_time = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::printf("  Index build: %.1f ms (%" PRIu64 " frames)\n", index_time, info.total_frames);

    // Seek to various positions
    double positions[] = {0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 0.99};
    const int num_pos = 7;

    std::printf("  %-10s  %10s  %10s\n", "Target", "Actual", "Seek time");

    for (int i = 0; i < num_pos; ++i) {
        double target = info.duration_seconds * positions[i];
        double actual = 0;

        auto s = std::chrono::steady_clock::now();
        pt7_mp3_status_t st = pt7_mp3_seek(dec, target, &actual);
        auto e = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(e - s).count();

        std::printf("  %8.2fs   %8.2fs   %7.2f ms  %s\n",
                    target, actual, ms,
                    st == PT7_MP3_OK ? "OK" : pt7_mp3_status_string(st));
    }

    // Repeated seeks
    auto s = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        double target = info.duration_seconds * (i % 100) / 100.0;
        pt7_mp3_seek(dec, target, nullptr);
    }
    auto e = std::chrono::steady_clock::now();
    double repeated_ms = std::chrono::duration<double, std::milli>(e - s).count();

    std::printf("  100 repeated seeks: %.1f ms (%.2f ms/seek avg)\n",
                repeated_ms, repeated_ms / 100.0);

    pt7_mp3_destroy(dec);
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::printf("========================================\n");
    std::printf(" PT7-MP3 Phase E Benchmark Suite\n");
    std::printf("========================================\n\n");

    // Dataset
    struct DatasetEntry {
        const char* path;
        const char* label;
    };

    DatasetEntry dataset[] = {
        {"tests/vectors/sine_44k_stereo.mp3",       "MPEG-1 44k Stereo CBR"},
        {"tests/vectors/sine_44k_vbr_stereo.mp3",   "MPEG-1 44k Stereo VBR"},
        {"tests/vectors/sine_48k_mono.mp3",         "MPEG-1 48k Mono CBR"},
        {"tests/vectors/sine_32k_joint.mp3",        "MPEG-2 32k Joint CBR"},
        {"tests/vectors/sine_22k_stereo.mp3",       "MPEG-2 22k Stereo CBR"},
        {"tests/vectors/sine_11k_mpeg25_mono.mp3",  "MPEG-2.5 11k Mono CBR"},
        {"tests/vectors/diag_stereo.mp3",           "MPEG-1 Stereo diag"},
        {"tests/vectors/diag_joint.mp3",            "MPEG-1 Joint diag"},
        {"tests/vectors/diag_voice.mp3",            "MPEG-1 Voice diag"},
        {"tests/vectors/diag_chord.mp3",            "MPEG-1 Chord diag"},
        {"tests/vectors/diag_bass_80hz.mp3",        "MPEG-1 Bass diag"},
        {"tests/vectors/diag_treble_10k.mp3",       "MPEG-1 Treble diag"},
    };

    const char* long_file = "C:/Users/PMAI/Music/07 Thompson Twins - Hold Me Now (12'' Version).mp3";

    // ---- Baseline decode benchmark ----
    std::printf("=== Baseline Decode Benchmark ===\n\n");
    std::printf("  %-45s  %9s  %10s  %8s  %9s  %7s  %8s\n",
                "File", "Audio", "Decode", "Realtime", "Throughput", "Frames", "Mem");

    std::vector<BenchResult> results;

    for (const auto& entry : dataset) {
        std::vector<uint8_t> mp3 = read_file(entry.path);
        if (mp3.empty()) {
            // Try from parent dir
            std::string alt = std::string("../") + entry.path;
            mp3 = read_file(alt.c_str());
        }
        if (mp3.empty()) {
            std::printf("  %-45s  (not found)\n", entry.label);
            continue;
        }
        BenchResult r = benchmark_decode(entry.label, mp3);
        print_bench(r);
        results.push_back(r);
    }

    // Long file
    {
        std::vector<uint8_t> mp3 = read_file(long_file);
        if (!mp3.empty()) {
            BenchResult r = benchmark_decode("Thompson Twins (long)", mp3);
            print_bench(r);
            results.push_back(r);

            // Streaming benchmark on long file
            benchmark_streaming("Thompson Twins", mp3);

            // Seeking benchmark on long file
            benchmark_seeking("Thompson Twins", mp3);
        } else {
            std::printf("\n  (Long file not found: %s)\n", long_file);
        }
    }

    // Summary
    if (!results.empty()) {
        double total_decode = 0, total_audio = 0, total_bytes = 0;
        for (const auto& r : results) {
            total_decode += r.decode_time_sec;
            total_audio += r.audio_duration_sec;
            total_bytes += r.file_size_bytes;
        }
        double avg_rt = (total_decode > 0) ? total_audio / total_decode : 0;
        double avg_mbs = (total_decode > 0) ? total_bytes / (1024.0 * 1024.0) / total_decode : 0;

        std::printf("\n=== Summary ===\n");
        std::printf("  Total audio:     %.1f s\n", total_audio);
        std::printf("  Total decode:    %.3f s\n", total_decode);
        std::printf("  Avg realtime:    %.1fx\n", avg_rt);
        std::printf("  Avg throughput:  %.1f MB/s\n", avg_mbs);
        std::printf("  Peak memory:     %zu KB\n", get_peak_memory_kb());
    }

    // ---- PCM Comparison (golden baseline) ----
    std::printf("\n=== PCM Comparison (golden baseline) ===\n");
    std::printf("  (Decode twice and compare for determinism)\n\n");

    for (const auto& entry : dataset) {
        std::vector<uint8_t> mp3 = read_file(entry.path);
        if (mp3.empty()) {
            std::string alt = std::string("../") + entry.path;
            mp3 = read_file(alt.c_str());
        }
        if (mp3.empty()) continue;

        auto pcm1 = decode_all_pcm16(mp3);
        auto pcm2 = decode_all_pcm16(mp3);
        PcmComparison c = compare_pcm16(pcm1, pcm2);
        std::printf("  %-35s  ", entry.label);
        print_comparison(c);
    }

    {
        std::vector<uint8_t> mp3 = read_file(long_file);
        if (!mp3.empty()) {
            auto pcm1 = decode_all_pcm16(mp3);
            auto pcm2 = decode_all_pcm16(mp3);
            PcmComparison c = compare_pcm16(pcm1, pcm2);
            std::printf("  %-35s  ", "Thompson Twins");
            print_comparison(c);
        }
    }

    std::printf("\n========================================\n");
    std::printf(" Benchmark complete.\n");
    std::printf("========================================\n");

    (void)argc; (void)argv;
    return 0;
}
