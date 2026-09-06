// PT7-MP3 Phase C Test Suite: Metadata + Gapless Playback
// Tests stream info, Xing/Info/VBRI detection, LAME gapless, and PCM trimming.

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

// Decode entire file and return stream info + total samples
struct DecodeResult {
    pt7_mp3_stream_info_t info{};
    size_t total_samples;
    size_t frames;
};

DecodeResult decode_full(const std::vector<uint8_t>& mp3) {
    DecodeResult res{};
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    if (!dec) return res;
    pt7_mp3_feed(dec, mp3.data(), mp3.size());
    std::vector<int16_t> pcm(4608);
    while (true) {
        size_t written = 0;
        pt7_mp3_status_t st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &written);
        if (written > 0) res.total_samples += written;
        if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && written == 0)) break;
        if (st != PT7_MP3_OK && st != PT7_MP3_ERR_DECODE) break;
    }
    pt7_mp3_get_info(dec, &res.info);
    res.frames = res.info.frame_count;
    pt7_mp3_destroy(dec);
    return res;
}

} // anonymous namespace

// Test 1: MPEG-1 Layer III
void test_mpeg1() {
    std::cout << "[TEST] MPEG-1 Layer III...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    DecodeResult res = decode_full(mp3);
    TEST_CHECK(res.info.version == PT7_MP3_VERSION_MPEG1);
    TEST_CHECK(res.info.layer == PT7_MP3_LAYER_III);
    TEST_CHECK(res.info.sample_rate_hz == 44100);
}

// Test 2: MPEG-2 Layer III
void test_mpeg2() {
    std::cout << "[TEST] MPEG-2 Layer III...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_22k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_22k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    DecodeResult res = decode_full(mp3);
    TEST_CHECK(res.info.version == PT7_MP3_VERSION_MPEG2);
    TEST_CHECK(res.info.layer == PT7_MP3_LAYER_III);
    TEST_CHECK(res.info.sample_rate_hz == 22050);
}

// Test 3: MPEG-2.5 Layer III
void test_mpeg25() {
    std::cout << "[TEST] MPEG-2.5 Layer III...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_11k_mpeg25_mono.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_11k_mpeg25_mono.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    DecodeResult res = decode_full(mp3);
    TEST_CHECK(res.info.version == PT7_MP3_VERSION_MPEG2_5);
    TEST_CHECK(res.info.layer == PT7_MP3_LAYER_III);
    TEST_CHECK(res.info.sample_rate_hz == 11025);
}

// Test 4: Mono
void test_mono() {
    std::cout << "[TEST] Mono...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_48k_mono.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_48k_mono.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    DecodeResult res = decode_full(mp3);
    TEST_CHECK(res.info.channels == 1);
    TEST_CHECK(res.info.channel_mode == PT7_MP3_CHANNEL_SINGLE_CHANNEL);
}

// Test 5: Stereo
void test_stereo() {
    std::cout << "[TEST] Stereo...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    DecodeResult res = decode_full(mp3);
    TEST_CHECK(res.info.channels == 2);
    // Channel mode may be STEREO or JOINT_STEREO depending on encoder
    TEST_CHECK(res.info.channel_mode == PT7_MP3_CHANNEL_STEREO ||
               res.info.channel_mode == PT7_MP3_CHANNEL_JOINT_STEREO);
}

// Test 6: Joint Stereo
void test_joint_stereo() {
    std::cout << "[TEST] Joint Stereo...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_32k_joint.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_32k_joint.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    DecodeResult res = decode_full(mp3);
    TEST_CHECK(res.info.channels == 2);
    TEST_CHECK(res.info.channel_mode == PT7_MP3_CHANNEL_JOINT_STEREO);
}

// Test 7: CBR (Info header)
void test_cbr() {
    std::cout << "[TEST] CBR (Info header)...\n";
    // sine_44k_stereo.mp3 has an Info header (CBR)
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    DecodeResult res = decode_full(mp3);
    TEST_CHECK(res.info.has_vbr_header == 1);
    TEST_CHECK(res.info.is_vbr == 0);
    TEST_CHECK(res.info.bitrate_mode == PT7_MP3_BITRATE_CBR);
}

// Test 8: VBR (Xing header)
void test_vbr() {
    std::cout << "[TEST] VBR (Xing header)...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_vbr_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_vbr_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    DecodeResult res = decode_full(mp3);
    TEST_CHECK(res.info.has_vbr_header == 1);
    TEST_CHECK(res.info.is_vbr == 1);
    TEST_CHECK(res.info.bitrate_mode == PT7_MP3_BITRATE_VBR);
}

// Test 9: Xing header fields
void test_xing_fields() {
    std::cout << "[TEST] Xing header fields...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_vbr_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_vbr_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    (void)decode_full(mp3);
    pt7_mp3_vbr_info_t vbr{};
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_feed(dec, mp3.data(), mp3.size());
    std::vector<int16_t> pcm(4608);
    size_t w = 0;
    pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &w);
    pt7_mp3_status_t st = pt7_mp3_get_vbr_info(dec, &vbr);
    TEST_CHECK(st == PT7_MP3_OK);
    TEST_CHECK(vbr.has_vbr_header == 1);
    TEST_CHECK(vbr.is_vbr == 1);
    TEST_CHECK(vbr.total_frames > 0);
    pt7_mp3_destroy(dec);
}

// Test 10: Info header fields
void test_info_fields() {
    std::cout << "[TEST] Info header fields...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_feed(dec, mp3.data(), mp3.size());
    std::vector<int16_t> pcm(4608);
    size_t w = 0;
    pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &w);
    pt7_mp3_vbr_info_t vbr{};
    pt7_mp3_status_t st = pt7_mp3_get_vbr_info(dec, &vbr);
    TEST_CHECK(st == PT7_MP3_OK);
    TEST_CHECK(vbr.has_vbr_header == 1);
    TEST_CHECK(vbr.is_vbr == 0); // Info = CBR
    pt7_mp3_destroy(dec);
}

// Test 11: LAME gapless metadata
void test_lame_gapless() {
    std::cout << "[TEST] LAME gapless metadata...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_feed(dec, mp3.data(), mp3.size());
    std::vector<int16_t> pcm(4608);
    size_t w = 0;
    pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &w);
    pt7_mp3_vbr_info_t vbr{};
    pt7_mp3_get_vbr_info(dec, &vbr);
    TEST_CHECK(vbr.has_gapless == 1);
    // LAME delay is typically 576 for MPEG-1
    TEST_CHECK(vbr.encoder_delay > 0);
    pt7_mp3_stream_info_t info{};
    pt7_mp3_get_info(dec, &info);
    TEST_CHECK(info.gapless_available == 1);
    TEST_CHECK(info.encoder_delay == vbr.encoder_delay);
    TEST_CHECK(info.end_padding == vbr.end_padding);
    pt7_mp3_destroy(dec);
}

// Test 12: File without gapless metadata (no VBR/LAME header)
void test_no_gapless() {
    std::cout << "[TEST] File without gapless metadata...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo_nogap.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo_nogap.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_feed(dec, mp3.data(), mp3.size());
    std::vector<int16_t> pcm(4608);
    size_t w = 0;
    pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &w);
    pt7_mp3_stream_info_t info{};
    pt7_mp3_get_info(dec, &info);
    TEST_CHECK(info.gapless_available == 0);
    TEST_CHECK(info.encoder_delay == 0);
    TEST_CHECK(info.end_padding == 0);
    // Enabling gapless should fail
    pt7_mp3_status_t st = pt7_mp3_enable_gapless(dec);
    TEST_CHECK(st == PT7_MP3_ERR_SYNC_NOT_FOUND);
    pt7_mp3_destroy(dec);
}

// Test 13: Duration consistency
void test_duration() {
    std::cout << "[TEST] Duration consistency...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    DecodeResult res = decode_full(mp3);
    TEST_CHECK(res.info.duration_seconds > 0.0);
    // Duration from total_samples should match decoded samples
    if (res.info.total_samples > 0 && res.info.sample_rate_hz > 0) {
        double expected = static_cast<double>(res.info.total_samples) / res.info.sample_rate_hz;
        TEST_CHECK(std::fabs(res.info.duration_seconds - expected) < 0.01);
    }
}

// Test 14: Frame/sample count consistency
void test_frame_sample_count() {
    std::cout << "[TEST] Frame/sample count consistency...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    DecodeResult res = decode_full(mp3);
    // For MPEG-1, 1152 samples per frame per channel
    // total interleaved = frames * 1152 * channels
    const uint64_t expected = res.frames * 1152ULL * res.info.channels;
    TEST_CHECK(res.total_samples == expected);
    // VBR header may declare total_frames including the VBR header frame itself,
    // so allow a difference of at most 1 frame
    if (res.info.total_frames > 0) {
        const int64_t diff = static_cast<int64_t>(res.info.total_frames) - static_cast<int64_t>(res.frames);
        TEST_CHECK(std::abs(diff) <= 1);
    }
}

// Test 15: Gapless PCM trimming - no double trimming
void test_gapless_no_double_trim() {
    std::cout << "[TEST] Gapless no double trimming...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;

    // Decode with gapless enabled
    pt7_mp3_decoder_t* dec1 = pt7_mp3_create();
    pt7_mp3_feed(dec1, mp3.data(), mp3.size());
    // Read one frame to populate VBR info
    std::vector<int16_t> pcm(4608);
    size_t w = 0;
    pt7_mp3_read_pcm16(dec1, pcm.data(), pcm.size(), &w);
    // Enable gapless
    pt7_mp3_status_t st = pt7_mp3_enable_gapless(dec1);
    TEST_CHECK(st == PT7_MP3_OK);
    // Call enable_gapless again — should be idempotent
    st = pt7_mp3_enable_gapless(dec1);
    TEST_CHECK(st == PT7_MP3_OK);
    // Continue decoding
    size_t total_with_gapless = w;
    while (true) {
        w = 0;
        st = pt7_mp3_read_pcm16(dec1, pcm.data(), pcm.size(), &w);
        if (w > 0) total_with_gapless += w;
        if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && w == 0)) break;
        if (st != PT7_MP3_OK && st != PT7_MP3_ERR_DECODE) break;
    }
    uint64_t front_trim1 = pt7_mp3_get_gapless_trimmed_front(dec1);
    pt7_mp3_destroy(dec1);

    // Decode without gapless
    pt7_mp3_decoder_t* dec2 = pt7_mp3_create();
    pt7_mp3_feed(dec2, mp3.data(), mp3.size());
    size_t total_without = 0;
    while (true) {
        w = 0;
        st = pt7_mp3_read_pcm16(dec2, pcm.data(), pcm.size(), &w);
        if (w > 0) total_without += w;
        if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && w == 0)) break;
        if (st != PT7_MP3_OK && st != PT7_MP3_ERR_DECODE) break;
    }
    pt7_mp3_destroy(dec2);

    // With gapless, total should be <= without (front + end trimmed)
    TEST_CHECK(total_with_gapless <= total_without);
    // Front trim should be > 0 (LAME delay)
    TEST_CHECK(front_trim1 > 0);
}

// Test 16: Gapless on short stream
void test_gapless_short_stream() {
    std::cout << "[TEST] Gapless on short stream...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_vbr_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_vbr_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_feed(dec, mp3.data(), mp3.size());
    std::vector<int16_t> pcm(4608);
    size_t w = 0;
    pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &w);
    pt7_mp3_status_t st = pt7_mp3_enable_gapless(dec);
    // Should succeed if gapless metadata present
    if (st == PT7_MP3_OK) {
        // Decode rest — should not crash even if stream is very short
        while (true) {
            w = 0;
            st = pt7_mp3_read_pcm16(dec, pcm.data(), pcm.size(), &w);
            if (st == PT7_MP3_NEED_MORE_DATA || (st == PT7_MP3_OK && w == 0)) break;
            if (st != PT7_MP3_OK && st != PT7_MP3_ERR_DECODE) break;
        }
        TEST_CHECK(true); // no crash = pass
    } else {
        // No gapless metadata — that's also fine
        TEST_CHECK(st == PT7_MP3_ERR_SYNC_NOT_FOUND);
    }
    pt7_mp3_destroy(dec);
}

// Test 17: VBRI detection (if available in samples)
void test_vbri() {
    std::cout << "[TEST] VBRI detection...\n";
    // Our test vectors don't have VBRI (they're all LAME-encoded)
    // VBRI is typically from Fraunhofer encoders. We test the parser indirectly
    // by verifying that non-VBRI files correctly report no VBRI.
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    DecodeResult res = decode_full(mp3);
    // Xing/Info header should be detected, not VBRI
    TEST_CHECK(res.info.has_vbr_header == 1);
    // VBRI would set is_vbr=1; our file is CBR (Info header)
    TEST_CHECK(res.info.is_vbr == 0);
}

// Test 18: Stream info before any decode
void test_info_before_decode() {
    std::cout << "[TEST] Stream info before decode...\n";
    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    pt7_mp3_stream_info_t info{};
    pt7_mp3_status_t st = pt7_mp3_get_info(dec, &info);
    TEST_CHECK(st == PT7_MP3_OK);
    TEST_CHECK(info.bitrate_mode == PT7_MP3_BITRATE_UNKNOWN);
    TEST_CHECK(info.frame_count == 0);
    TEST_CHECK(info.gapless_available == 0);
    pt7_mp3_destroy(dec);
}

// Test 19: Bitrate mode VBR
void test_bitrate_mode_vbr() {
    std::cout << "[TEST] Bitrate mode VBR...\n";
    std::vector<uint8_t> mp3 = read_file("tests/vectors/sine_44k_vbr_stereo.mp3");
    if (mp3.empty()) mp3 = read_file("../tests/vectors/sine_44k_vbr_stereo.mp3");
    TEST_CHECK(!mp3.empty());
    if (mp3.empty()) return;
    DecodeResult res = decode_full(mp3);
    TEST_CHECK(res.info.bitrate_mode == PT7_MP3_BITRATE_VBR);
}

int main() {
    std::cout << "========================================\n";
    std::cout << " PT7-MP3 Phase C Test Suite\n";
    std::cout << " Metadata + Gapless Playback\n";
    std::cout << "========================================\n";

    test_mpeg1();
    test_mpeg2();
    test_mpeg25();
    test_mono();
    test_stereo();
    test_joint_stereo();
    test_cbr();
    test_vbr();
    test_xing_fields();
    test_info_fields();
    test_lame_gapless();
    test_no_gapless();
    test_duration();
    test_frame_sample_count();
    test_gapless_no_double_trim();
    test_gapless_short_stream();
    test_vbri();
    test_info_before_decode();
    test_bitrate_mode_vbr();

    std::cout << "========================================\n";
    std::cout << " Tests run: " << g_tests_run << "\n";
    std::cout << " Passed   : " << g_tests_passed << "\n";
    std::cout << " Failed   : " << g_tests_failed << "\n";
    std::cout << "========================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
