// PT7-MP3 API Test Suite
// Tests the public C API: create/destroy, feed/read, PCM16, Float32, reset,
// partial input handling, and frame info retrieval.
// No internal headers used — only include/pt7_mp3.h.

#include "pt7_mp3.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;

#define TEST_CHECK(expr) \
    do { \
        ++g_tests_run; \
        if (expr) { \
            ++g_tests_passed; \
        } else { \
            ++g_tests_failed; \
            std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << std::endl; \
        } \
    } while (0)

std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    std::streamsize sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

} // anonymous namespace

// Test 1: Create / Destroy
void test_create_destroy() {
    std::cout << "[TEST] Create / Destroy...\n";
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    TEST_CHECK(dec != nullptr);
    TEST_CHECK(pt7_mp3_get_instance_size() > 0);
    pt7_mp3_destroy(dec);
    // NULL-safe destroy
    pt7_mp3_destroy(nullptr);
}

// Test 2: Reset
void test_reset() {
    std::cout << "[TEST] Reset...\n";
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    TEST_CHECK(dec != nullptr);
    pt7_mp3_reset(dec);
    // Should be safe to reset a fresh decoder
    pt7_mp3_reset(dec);
    pt7_mp3_destroy(dec);
}

// Test 3: Feed + Read PCM16
void test_feed_read_pcm16() {
    std::cout << "[TEST] Feed + Read PCM16...\n";

    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;

    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    TEST_CHECK(dec != nullptr);

    // Feed entire file
    pt7_mp3_status_t st = pt7_mp3_feed(dec, mp3.data(), mp3.size());
    TEST_CHECK(st == PT7_MP3_OK);

    // Read PCM16
    std::vector<int16_t> pcm(4608);
    size_t total_samples = 0;
    size_t frames = 0;
    bool got_info = false;
    pt7_mp3_frame_info_t info{};

    while (true) {
        size_t written = 0;
        st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
        if (written > 0) {
            if (!got_info) {
                pt7_mp3_get_frame_info(dec, &info);
                got_info = true;
            }
            total_samples += written;
            frames++;
        }
        if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && written == 0)) break;
        if (st != PT7_MP3_OK) break;
    }

    TEST_CHECK(got_info);
    TEST_CHECK(info.sample_rate_hz == 44100);
    TEST_CHECK(info.channels == 2);
    TEST_CHECK(total_samples > 0);
    TEST_CHECK(frames > 0);

    // Verify no NaN/Inf in PCM (shouldn't happen for int16, but check)
    bool all_finite = true;
    for (size_t i = 0; i < total_samples && i < pcm.size(); ++i) {
        if (pcm[i] == -32768 && pcm[i] == 32767) { all_finite = false; break; }
    }
    TEST_CHECK(all_finite);

    pt7_mp3_destroy(dec);
}

// Test 4: Feed + Read Float32
void test_feed_read_float32() {
    std::cout << "[TEST] Feed + Read Float32...\n";

    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;

    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    TEST_CHECK(dec != nullptr);

    pt7_mp3_status_t st = pt7_mp3_feed(dec, mp3.data(), mp3.size());
    TEST_CHECK(st == PT7_MP3_OK);

    std::vector<float> pcm(4608);
    size_t total_samples = 0;
    bool has_nan = false;
    bool has_inf = false;
    float max_abs = 0.0f;

    while (true) {
        size_t written = 0;
        st = pt7_mp3_read_pcm_float(dec, pcm.data(), pcm.size(), &written);
        if (written > 0) {
            for (size_t i = 0; i < written; ++i) {
                if (std::isnan(pcm[i])) has_nan = true;
                if (std::isinf(pcm[i])) has_inf = true;
                float a = std::fabs(pcm[i]);
                if (a > max_abs) max_abs = a;
            }
            total_samples += written;
        }
        if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && written == 0)) break;
        if (st != PT7_MP3_OK) break;
    }

    TEST_CHECK(total_samples > 0);
    TEST_CHECK(!has_nan);
    TEST_CHECK(!has_inf);
    TEST_CHECK(max_abs > 0.0f);
    TEST_CHECK(max_abs < 10.0f); // reasonable bound

    pt7_mp3_destroy(dec);
}

// Test 5: Partial input (feed in small chunks)
void test_partial_input() {
    std::cout << "[TEST] Partial Input (chunked feed)...\n";

    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;

    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    TEST_CHECK(dec != nullptr);

    // Feed in 1-byte chunks to stress the partial input path
    size_t feed_pos = 0;
    std::vector<int16_t> pcm(2304);
    size_t total_samples = 0;

    while (feed_pos < mp3.size()) {
        // Feed 1 byte at a time (extreme case)
        size_t chunk_size = 1;
        if (feed_pos + chunk_size > mp3.size()) chunk_size = mp3.size() - feed_pos;

        pt7_mp3_status_t st = pt7_mp3_feed(dec, mp3.data() + feed_pos, chunk_size);
        TEST_CHECK(st == PT7_MP3_OK);
        feed_pos += chunk_size;

        // Try to read
        while (true) {
            size_t written = 0;
            st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
            if (written > 0) total_samples += written;
            if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && written == 0)) break;
            if (st != PT7_MP3_OK) break;
        }
    }

    TEST_CHECK(total_samples > 0);
    pt7_mp3_destroy(dec);
}

// Test 6: Decode frame directly (PCM16)
void test_decode_frame_pcm16() {
    std::cout << "[TEST] Decode Frame PCM16 (direct)...\n";

    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_48k_mono.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_48k_mono.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;

    // Skip ID3v2 if present
    size_t id3_size = 0;
    pt7_mp3_detect_id3v2(mp3.data(), mp3.size(), &id3_size);
    size_t offset = id3_size;

    // Scan for first frame
    size_t frame_offset = 0;
    pt7_mp3_status_t st = pt7_mp3_scan_frame(mp3.data() + offset, mp3.size() - offset,
                                              &frame_offset, nullptr);
    TEST_CHECK(st == PT7_MP3_OK);
    if (st != PT7_MP3_OK) return;
    offset += frame_offset;

    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    TEST_CHECK(dec != nullptr);

    // Decode one frame directly
    int16_t pcm[1152];
    size_t samples_written = 0;
    size_t bytes_consumed = 0;
    st = pt7_mp3_decode_frame_pcm16(dec, mp3.data() + offset, mp3.size() - offset,
                                     pcm, 1152, &samples_written, &bytes_consumed);
    TEST_CHECK(st == PT7_MP3_OK);
    TEST_CHECK(samples_written > 0);
    TEST_CHECK(bytes_consumed > 0);

    // Check frame info
    pt7_mp3_frame_info_t info{};
    st = pt7_mp3_get_frame_info(dec, &info);
    TEST_CHECK(st == PT7_MP3_OK);
    TEST_CHECK(info.sample_rate_hz == 48000);
    TEST_CHECK(info.channels == 1);

    pt7_mp3_destroy(dec);
}

// Test 7: Decode frame directly (Float32)
void test_decode_frame_float() {
    std::cout << "[TEST] Decode Frame Float32 (direct)...\n";

    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;

    size_t id3_size = 0;
    pt7_mp3_detect_id3v2(mp3.data(), mp3.size(), &id3_size);
    size_t offset = id3_size;

    size_t frame_offset = 0;
    pt7_mp3_status_t st = pt7_mp3_scan_frame(mp3.data() + offset, mp3.size() - offset,
                                              &frame_offset, nullptr);
    TEST_CHECK(st == PT7_MP3_OK);
    if (st != PT7_MP3_OK) return;
    offset += frame_offset;

    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    TEST_CHECK(dec != nullptr);

    float pcm[2304];
    size_t samples_written = 0;
    size_t bytes_consumed = 0;
    st = pt7_mp3_decode_frame_float(dec, mp3.data() + offset, mp3.size() - offset,
                                     pcm, 2304, &samples_written, &bytes_consumed);
    TEST_CHECK(st == PT7_MP3_OK);
    TEST_CHECK(samples_written > 0);

    // Verify float range
    bool in_range = true;
    for (size_t i = 0; i < samples_written; ++i) {
        if (std::isnan(pcm[i]) || std::isinf(pcm[i])) { in_range = false; break; }
        if (std::fabs(pcm[i]) > 100.0f) { in_range = false; break; }
    }
    TEST_CHECK(in_range);

    pt7_mp3_destroy(dec);
}

// Test 8: Status string
void test_status_string() {
    std::cout << "[TEST] Status String...\n";
    TEST_CHECK(pt7_mp3_status_string(PT7_MP3_OK) != nullptr);
    TEST_CHECK(pt7_mp3_status_string(PT7_MP3_NEED_MORE_DATA) != nullptr);
    TEST_CHECK(pt7_mp3_status_string(PT7_MP3_ERR_INVALID_ARG) != nullptr);
    TEST_CHECK(strlen(pt7_mp3_status_string(PT7_MP3_OK)) > 0);
}

// Test 9: NULL safety
void test_null_safety() {
    std::cout << "[TEST] NULL Safety...\n";
    // All functions should handle NULL decoder gracefully
    pt7_mp3_reset(nullptr);
    pt7_mp3_destroy(nullptr);
    pt7_mp3_flush(nullptr);

    pt7_mp3_frame_info_t info{};
    TEST_CHECK(pt7_mp3_get_frame_info(nullptr, &info) == PT7_MP3_ERR_INVALID_ARG);

    TEST_CHECK(pt7_mp3_feed(nullptr, nullptr, 0) == PT7_MP3_ERR_INVALID_ARG);

    size_t written = 0;
    TEST_CHECK(pt7_mp3_read_pcm16(nullptr, nullptr, 0, &written) == PT7_MP3_ERR_INVALID_ARG);
}

int main() {
    std::cout << "========================================\n";
    std::cout << " PT7-MP3 API Test Suite\n";
    std::cout << "========================================\n";

    test_create_destroy();
    test_reset();
    test_feed_read_pcm16();
    test_feed_read_float32();
    test_partial_input();
    test_decode_frame_pcm16();
    test_decode_frame_float();
    test_status_string();
    test_null_safety();

    std::cout << "========================================\n";
    std::cout << " Tests run: " << g_tests_run << "\n";
    std::cout << " Passed   : " << g_tests_passed << "\n";
    std::cout << " Failed   : " << g_tests_failed << "\n";
    std::cout << "========================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
