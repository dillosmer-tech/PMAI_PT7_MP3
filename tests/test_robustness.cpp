// PT7-MP3 Robustness Test Suite (Phase B)
// Tests: partial input, corrupted frames, resync, garbage, EOF, reset, fuzz.
// No algorithmic tests — only robustness/error-handling verification.

#include "pt7_mp3.h"

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
        if (expr) { ++g_tests_passed; } \
        else { ++g_tests_failed; std::cerr << "FAIL: " << #expr << " line " << __LINE__ << std::endl; } \
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

// Decode all frames from a byte vector; returns total samples decoded.
size_t decode_all_pcm16(const std::vector<uint8_t>& mp3, size_t max_frames = 10000) {
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    if (!dec) return 0;
    pt7_mp3_feed(dec, mp3.data(), mp3.size());
    std::vector<int16_t> pcm(4608);
    size_t total = 0;
    size_t frames = 0;
    while (frames < max_frames) {
        size_t written = 0;
        pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
        if (written > 0) { total += written; frames++; }
        if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && written == 0)) break;
        if (st != PT7_MP3_OK && st != PT7_MP3_ERR_DECODE) break;
    }
    pt7_mp3_destroy(dec);
    return total;
}

// Decode all with chunked feed; returns total samples.
size_t decode_chunked(const std::vector<uint8_t>& mp3, size_t chunk_size, size_t max_frames = 10000) {
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    if (!dec) return 0;
    std::vector<int16_t> pcm(4608);
    size_t total = 0;
    size_t frames = 0;
    size_t pos = 0;
    while (pos < mp3.size() || frames == 0) {
        // Feed one chunk
        size_t feed_size = std::min(chunk_size, mp3.size() - pos);
        if (feed_size > 0) {
            pt7_mp3_feed(dec, mp3.data() + pos, feed_size);
            pos += feed_size;
        }
        // Read all available frames
        bool got_frame = true;
        while (got_frame && frames < max_frames) {
            size_t written = 0;
            pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
            if (written > 0) { total += written; frames++; }
            if (st == PT7_MP3_NEED_MORE_DATA) { got_frame = false; }
            else if (st == PT7_MP3_OK && written == 0) { got_frame = false; }
            else if (st != PT7_MP3_OK && st != PT7_MP3_ERR_DECODE) { got_frame = false; }
        }
        if (pos >= mp3.size()) break;
    }
    pt7_mp3_destroy(dec);
    return total;
}

} // anonymous namespace

// Test 1: Valid MP3 → complete decode
void test_valid_decode() {
    std::cout << "[TEST] Valid MP3 decode...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    size_t samples = decode_all_pcm16(mp3);
    TEST_CHECK(samples > 0);
}

// Test 2: Byte-per-byte feed
void test_byte_by_byte() {
    std::cout << "[TEST] Byte-per-byte feed...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    size_t samples = decode_chunked(mp3, 1);
    TEST_CHECK(samples > 0);
}

// Test 3: Random chunk sizes
void test_random_chunks() {
    std::cout << "[TEST] Random chunk sizes...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    // Use several fixed "random-ish" chunk sizes
    size_t sizes[] = {7, 13, 256, 3, 1000, 1, 500, 17};
    for (size_t sz : sizes) {
        size_t samples = decode_chunked(mp3, sz);
        TEST_CHECK(samples > 0);
    }
}

// Test 4: Frame split in half
void test_frame_split_half() {
    std::cout << "[TEST] Frame split in half...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    // Feed first half, then second half
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    TEST_CHECK(dec != nullptr);
    size_t half = mp3.size() / 2;
    pt7_mp3_feed(dec, mp3.data(), half);
    // Try read — should get some frames or NEED_MORE_DATA
    std::vector<int16_t> pcm(4608);
    size_t written = 0;
    pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
    TEST_CHECK(st == PT7_MP3_OK || st == PT7_MP3_NEED_MORE_DATA);
    // Feed second half
    pt7_mp3_feed(dec, mp3.data() + half, mp3.size() - half);
    size_t total = written;
    while (true) {
        written = 0;
        st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
        if (written > 0) total += written;
        if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && written == 0)) break;
        if (st != PT7_MP3_OK && st != PT7_MP3_ERR_DECODE) break;
    }
    TEST_CHECK(total > 0);
    pt7_mp3_destroy(dec);
}

// Test 5: Frame split into many tiny chunks
void test_frame_split_many() {
    std::cout << "[TEST] Frame split into many chunks...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    size_t samples = decode_chunked(mp3, 5);
    TEST_CHECK(samples > 0);
}

// Test 6: Truncated MP3
void test_truncated() {
    std::cout << "[TEST] Truncated MP3...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    // Truncate to 60% of file
    size_t trunc = mp3.size() * 6 / 10;
    std::vector<uint8_t> truncated(mp3.begin(), mp3.begin() + trunc);
    size_t samples = decode_all_pcm16(truncated);
    // Should decode some frames without crashing
    TEST_CHECK(samples > 0);
}

// Test 7: Corrupted header
void test_corrupted_header() {
    std::cout << "[TEST] Corrupted header...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    // Find first frame (skip ID3 if any)
    size_t id3_sz = 0;
    pt7_mp3_detect_id3v2(mp3.data(), mp3.size(), &id3_sz);
    size_t offset = id3_sz;
    // Corrupt the header bytes
    std::vector<uint8_t> corrupted = mp3;
    if (offset + 4 < corrupted.size()) {
        corrupted[offset] = 0x00;
        corrupted[offset + 1] = 0x00;
        corrupted[offset + 2] = 0x00;
        corrupted[offset + 3] = 0x00;
    }
    // Should not crash; may decode fewer frames but should recover
    (void)decode_all_pcm16(corrupted);
    // Decoder should survive (may produce 0 or some samples after resync)
    TEST_CHECK(true); // no crash = pass
}

// Test 8: Garbage bytes before stream
void test_garbage_prefix() {
    std::cout << "[TEST] Garbage before stream...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    // Prepend 100 bytes of garbage
    std::vector<uint8_t> prefixed;
    for (int i = 0; i < 100; ++i) prefixed.push_back(static_cast<uint8_t>(i * 7 + 3));
    prefixed.insert(prefixed.end(), mp3.begin(), mp3.end());
    size_t samples = decode_all_pcm16(prefixed);
    TEST_CHECK(samples > 0);
}

// Test 9: Corrupted frame followed by valid frame
void test_corrupt_then_valid() {
    std::cout << "[TEST] Corrupt frame then valid...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    // Find first frame
    size_t id3_sz = 0;
    pt7_mp3_detect_id3v2(mp3.data(), mp3.size(), &id3_sz);
    size_t offset = id3_sz;
    // Parse first frame to get its size
    pt7_mp3_frame_info_t info{};
    size_t frame_off = 0;
    pt7_mp3_scan_frame(mp3.data() + offset, mp3.size() - offset, &frame_off, &info);
    offset += frame_off;
    // Corrupt the middle of first frame (keep header valid but corrupt data)
    std::vector<uint8_t> corrupted = mp3;
    size_t mid = offset + info.frame_size_bytes / 2;
    for (size_t i = mid; i < mid + 20 && i < corrupted.size(); ++i) {
        corrupted[i] = 0xFF;
    }
    // Should recover and decode subsequent valid frames
    size_t samples = decode_all_pcm16(corrupted);
    // Should decode some frames (maybe not the first corrupted one)
    TEST_CHECK(samples > 0);
}

// Test 10: Multiple consecutive corrupted frames
void test_multiple_corrupt() {
    std::cout << "[TEST] Multiple corrupt frames...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    size_t id3_sz = 0;
    pt7_mp3_detect_id3v2(mp3.data(), mp3.size(), &id3_sz);
    size_t offset = id3_sz;
    // Corrupt first 3 frames worth of data
    std::vector<uint8_t> corrupted = mp3;
    for (size_t i = offset; i < offset + 2000 && i < corrupted.size(); ++i) {
        corrupted[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);
    }
    // Should not crash; may decode 0 or some frames after resync
    (void)decode_all_pcm16(corrupted);
    TEST_CHECK(true); // no crash = pass
}

// Test 11: EOF during a frame
void test_eof_during_frame() {
    std::cout << "[TEST] EOF during frame...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    // Find first frame, truncate in the middle of it
    size_t id3_sz = 0;
    pt7_mp3_detect_id3v2(mp3.data(), mp3.size(), &id3_sz);
    size_t offset = id3_sz;
    pt7_mp3_frame_info_t info{};
    size_t frame_off = 0;
    pt7_mp3_scan_frame(mp3.data() + offset, mp3.size() - offset, &frame_off, &info);
    offset += frame_off;
    size_t mid_frame = offset + info.frame_size_bytes / 2;
    std::vector<uint8_t> truncated(mp3.begin(), mp3.begin() + mid_frame);
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_feed(dec, truncated.data(), truncated.size());
    std::vector<int16_t> pcm(4608);
    size_t written = 0;
    pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
    // Should return NEED_MORE_DATA (partial frame) without crashing
    TEST_CHECK(st == PT7_MP3_NEED_MORE_DATA || st == PT7_MP3_OK);
    pt7_mp3_destroy(dec);
}

// Test 12: Reset after error then valid decode
void test_reset_after_error() {
    std::cout << "[TEST] Reset after error...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    TEST_CHECK(dec != nullptr);
    // Feed garbage
    std::vector<uint8_t> garbage(500, 0xAB);
    pt7_mp3_feed(dec, garbage.data(), garbage.size());
    std::vector<int16_t> pcm(4608);
    size_t written = 0;
    pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
    // Reset
    pt7_mp3_reset(dec);
    // Now feed valid MP3
    pt7_mp3_feed(dec, mp3.data(), mp3.size());
    size_t total = 0;
    while (true) {
        written = 0;
        pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
        if (written > 0) total += written;
        if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && written == 0)) break;
        if (st != PT7_MP3_OK && st != PT7_MP3_ERR_DECODE) break;
    }
    TEST_CHECK(total > 0);
    pt7_mp3_destroy(dec);
}

// Test 13: Feed after NEED_MORE_DATA completes a frame
void test_feed_after_need_more() {
    std::cout << "[TEST] Feed after NEED_MORE_DATA...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    // Find first frame
    size_t id3_sz = 0;
    pt7_mp3_detect_id3v2(mp3.data(), mp3.size(), &id3_sz);
    size_t offset = id3_sz;
    pt7_mp3_frame_info_t info{};
    size_t frame_off = 0;
    pt7_mp3_scan_frame(mp3.data() + offset, mp3.size() - offset, &frame_off, &info);
    size_t frame_start = offset + frame_off;
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    // Feed only first 10 bytes of frame
    pt7_mp3_feed(dec, mp3.data(), frame_start + 10);
    std::vector<int16_t> pcm(4608);
    size_t written = 0;
    pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
    TEST_CHECK(st == PT7_MP3_NEED_MORE_DATA || st == PT7_MP3_OK);
    // Feed rest of frame
    pt7_mp3_feed(dec, mp3.data() + frame_start + 10, mp3.size() - (frame_start + 10));
    st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
    TEST_CHECK(st == PT7_MP3_OK);
    TEST_CHECK(written > 0);
    pt7_mp3_destroy(dec);
}

// Test 14: No infinite loop on random input
void test_no_infinite_loop() {
    std::cout << "[TEST] No infinite loop on random input...\n";
    // Generate 10KB of pseudo-random bytes
    std::vector<uint8_t> random_data(10000);
    for (size_t i = 0; i < random_data.size(); ++i) {
        random_data[i] = static_cast<uint8_t>((i * 1103515245 + 12345) & 0xFF);
    }
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_feed(dec, random_data.data(), random_data.size());
    std::vector<int16_t> pcm(4608);
    size_t written = 0;
    // Should return quickly (not hang)
    pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
    TEST_CHECK(st == PT7_MP3_NEED_MORE_DATA || st == PT7_MP3_OK || st == PT7_MP3_ERR_SYNC_NOT_FOUND);
    pt7_mp3_destroy(dec);
}

// Test 15: Light fuzz — mutated bytes, random truncations
void test_light_fuzz() {
    std::cout << "[TEST] Light fuzz...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    // Run 50 iterations with random mutations
    for (int iter = 0; iter < 50; ++iter) {
        std::vector<uint8_t> mutated = mp3;
        // Mutate 10 random bytes
        for (int m = 0; m < 10; ++m) {
            size_t pos = static_cast<size_t>((iter * 31 + m * 17) % mutated.size());
            mutated[pos] = static_cast<uint8_t>((iter * 7 + m * 13) & 0xFF);
        }
        // Random truncation
        size_t trunc = (iter % 4 == 0) ? mutated.size() * 3 / 4 : mutated.size();
        std::vector<uint8_t> truncated(mutated.begin(), mutated.begin() + trunc);
        // Should not crash
        size_t samples = decode_all_pcm16(truncated, 100);
        (void)samples;
    }
    TEST_CHECK(true); // no crash = pass
}

// Test 16: Empty input
void test_empty_input() {
    std::cout << "[TEST] Empty input...\n";
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_feed(dec, nullptr, 0);
    std::vector<int16_t> pcm(4608);
    size_t written = 0;
    pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
    TEST_CHECK(st == PT7_MP3_NEED_MORE_DATA || st == PT7_MP3_OK);
    TEST_CHECK(written == 0);
    pt7_mp3_destroy(dec);
}

// Test 17: All zeros input
void test_all_zeros() {
    std::cout << "[TEST] All zeros input...\n";
    std::vector<uint8_t> zeros(2000, 0);
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_feed(dec, zeros.data(), zeros.size());
    std::vector<int16_t> pcm(4608);
    size_t written = 0;
    pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
    TEST_CHECK(st == PT7_MP3_NEED_MORE_DATA || st == PT7_MP3_OK || st == PT7_MP3_ERR_SYNC_NOT_FOUND);
    pt7_mp3_destroy(dec);
}

int main() {
    std::cout << "========================================\n";
    std::cout << " PT7-MP3 Robustness Test Suite (Phase B)\n";
    std::cout << "========================================\n";

    test_valid_decode();
    test_byte_by_byte();
    test_random_chunks();
    test_frame_split_half();
    test_frame_split_many();
    test_truncated();
    test_corrupted_header();
    test_garbage_prefix();
    test_corrupt_then_valid();
    test_multiple_corrupt();
    test_eof_during_frame();
    test_reset_after_error();
    test_feed_after_need_more();
    test_no_infinite_loop();
    test_light_fuzz();
    test_empty_input();
    test_all_zeros();

    std::cout << "========================================\n";
    std::cout << " Tests run: " << g_tests_run << "\n";
    std::cout << " Passed   : " << g_tests_passed << "\n";
    std::cout << " Failed   : " << g_tests_failed << "\n";
    std::cout << "========================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
