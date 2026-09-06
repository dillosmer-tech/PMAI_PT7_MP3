#include "pt7_mp3.h"
#include "../src/mp3_bitstream.h"
#include "../src/mp3_frame.h"

#include <iostream>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cassert>

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

uint32_t build_header_word(
    uint32_t version_bits,
    uint32_t layer_bits,
    uint32_t protection_bit,
    uint32_t bitrate_idx,
    uint32_t srate_idx,
    uint32_t padding_bit,
    uint32_t private_bit,
    uint32_t channel_mode,
    uint32_t mode_ext,
    uint32_t copyright,
    uint32_t original,
    uint32_t emphasis)
{
    uint32_t h = (0x7FFU << 21);
    h |= ((version_bits & 0x03U) << 19);
    h |= ((layer_bits & 0x03U) << 17);
    h |= ((protection_bit & 0x01U) << 16);
    h |= ((bitrate_idx & 0x0FU) << 12);
    h |= ((srate_idx & 0x03U) << 10);
    h |= ((padding_bit & 0x01U) << 9);
    h |= ((private_bit & 0x01U) << 8);
    h |= ((channel_mode & 0x03U) << 6);
    h |= ((mode_ext & 0x03U) << 4);
    h |= ((copyright & 0x01U) << 3);
    h |= ((original & 0x01U) << 2);
    h |= (emphasis & 0x03U);
    return h;
}

void write_header_bytes(uint32_t header_word, uint8_t* out_4bytes)
{
    out_4bytes[0] = static_cast<uint8_t>((header_word >> 24) & 0xFFU);
    out_4bytes[1] = static_cast<uint8_t>((header_word >> 16) & 0xFFU);
    out_4bytes[2] = static_cast<uint8_t>((header_word >> 8)  & 0xFFU);
    out_4bytes[3] = static_cast<uint8_t>(header_word & 0xFFU);
}

} // anonymous namespace

// ============================================================================
// 1. Bitstream Reader Tests
// ============================================================================
void test_bitstream_reader()
{
    std::cout << "[TEST] Bitstream Reader...\n";

    // Test 1.1: Basic bit reads and alignment
    {
        const uint8_t data[] = { 0b10110001, 0b11110000, 0xAA, 0x55 };
        pt7::mp3::BitstreamReader reader(data, sizeof(data));

        TEST_CHECK(reader.total_bits() == 32);
        TEST_CHECK(reader.total_bytes() == 4);
        TEST_CHECK(reader.bits_remaining() == 32);
        TEST_CHECK(reader.bytes_remaining() == 4);
        TEST_CHECK(!reader.is_eof());
        TEST_CHECK(!reader.has_error());

        // Read single bit: 1
        TEST_CHECK(reader.read_bit() == 1);
        TEST_CHECK(reader.bit_position() == 1);
        TEST_CHECK(reader.bits_remaining() == 31);

        // Read 3 bits: 0b011 = 3
        TEST_CHECK(reader.read_bits(3) == 0b011);
        TEST_CHECK(reader.bit_position() == 4);

        // Read 4 bits: 0b0001 = 1 (completes first byte)
        TEST_CHECK(reader.read_bits(4) == 0b0001);
        TEST_CHECK(reader.bit_position() == 8);
        TEST_CHECK(reader.byte_position() == 1);

        // Peek 4 bits from second byte (0b1111)
        TEST_CHECK(reader.peek_bits(4) == 0b1111);
        TEST_CHECK(reader.bit_position() == 8); // position must not change on peek

        // Read cross-byte boundary: 4 bits from byte 1 (0b1111) and 4 bits from byte 1 (0b0000)
        TEST_CHECK(reader.read_bits(8) == 0xF0);

        // Skip 8 bits (skips 0xAA)
        TEST_CHECK(reader.skip_bits(8));
        TEST_CHECK(reader.byte_position() == 3);

        // Read remaining byte (0x55)
        TEST_CHECK(reader.read_bits(8) == 0x55);
        TEST_CHECK(reader.is_eof());
        TEST_CHECK(reader.bits_remaining() == 0);
        TEST_CHECK(!reader.has_error());

        // Attempt read past EOF
        TEST_CHECK(reader.read_bit() == 0);
        TEST_CHECK(reader.has_error());
    }

    // Test 1.2: Multi-byte crossing (up to 32 bits)
    {
        const uint8_t data[] = { 0x12, 0x34, 0x56, 0x78 };
        pt7::mp3::BitstreamReader reader(data, sizeof(data));

        TEST_CHECK(reader.read_bits(32) == 0x12345678U);
        TEST_CHECK(reader.is_eof());
    }

    // Test 1.3: Align to byte
    {
        const uint8_t data[] = { 0xFF, 0x42 };
        pt7::mp3::BitstreamReader reader(data, sizeof(data));

        TEST_CHECK(reader.read_bits(3) == 0b111);
        TEST_CHECK(reader.bit_position() == 3);
        reader.align_to_byte();
        TEST_CHECK(reader.bit_position() == 8);
        TEST_CHECK(reader.read_bits(8) == 0x42);
    }

    // Test 1.4: Null and empty buffer safety
    {
        pt7::mp3::BitstreamReader reader_empty(nullptr, 0);
        TEST_CHECK(reader_empty.is_eof());
        TEST_CHECK(reader_empty.bits_remaining() == 0);
        TEST_CHECK(reader_empty.read_bit() == 0);
        TEST_CHECK(reader_empty.read_bits(16) == 0);
        TEST_CHECK(reader_empty.peek_bits(8) == 0);
        TEST_CHECK(!reader_empty.skip_bits(1));
    }
}

// ============================================================================
// 2. Valid Frame Header Tests
// ============================================================================
void test_valid_headers()
{
    std::cout << "[TEST] Valid Frame Headers...\n";

    // Test 2.1: MPEG-1 Layer III, 128 kbps, 44.1 kHz, Joint Stereo, No Padding, No CRC
    {
        // version=3(MPEG-1), layer=1(L3), prot=1(no CRC), bitrate_idx=9(128k), srate=0(44.1k), pad=0
        const uint32_t hdr = build_header_word(3, 1, 1, 9, 0, 0, 0, 1, 2, 0, 1, 0);
        std::vector<uint8_t> frame(1000, 0);
        write_header_bytes(hdr, frame.data());

        pt7_mp3_frame_info_t info{};
        const pt7_mp3_status_t status = pt7_mp3_parse_header(frame.data(), frame.size(), &info);

        TEST_CHECK(status == PT7_MP3_OK);
        TEST_CHECK(info.version == PT7_MP3_VERSION_MPEG1);
        TEST_CHECK(info.layer == PT7_MP3_LAYER_III);
        TEST_CHECK(info.bitrate_kbps == 128);
        TEST_CHECK(info.sample_rate_hz == 44100);
        TEST_CHECK(info.channels == 2);
        TEST_CHECK(info.channel_mode == PT7_MP3_CHANNEL_JOINT_STEREO);
        TEST_CHECK(info.mode_extension == 2);
        TEST_CHECK(info.has_crc == 0);
        TEST_CHECK(info.header_bytes == 4);
        TEST_CHECK(info.padding == 0);
        TEST_CHECK(info.samples_per_frame == 1152);
        // (144000 * 128) / 44100 = 417 bytes
        TEST_CHECK(info.frame_size_bytes == 417);
    }

    // Test 2.2: MPEG-1 Layer III with Padding and CRC
    {
        // version=3, layer=1, prot=0(has CRC), bitrate_idx=9(128k), srate=0(44.1k), pad=1
        const uint32_t hdr = build_header_word(3, 1, 0, 9, 0, 1, 0, 0, 0, 1, 1, 0);
        std::vector<uint8_t> frame(1000, 0);
        write_header_bytes(hdr, frame.data());

        pt7_mp3_frame_info_t info{};
        const pt7_mp3_status_t status = pt7_mp3_parse_header(frame.data(), frame.size(), &info);

        TEST_CHECK(status == PT7_MP3_OK);
        TEST_CHECK(info.has_crc == 1);
        TEST_CHECK(info.header_bytes == 6);
        TEST_CHECK(info.padding == 1);
        TEST_CHECK(info.channel_mode == PT7_MP3_CHANNEL_STEREO);
        TEST_CHECK(info.channels == 2);
        TEST_CHECK(info.copyright == 1);
        TEST_CHECK(info.original == 1);
        // 417 + 1 = 418 bytes
        TEST_CHECK(info.frame_size_bytes == 418);
    }

    // Test 2.3: MPEG-1 Layer III, 320 kbps, 48 kHz, Mono
    {
        // version=3, layer=1, prot=1, bitrate_idx=14(320k), srate=1(48k), pad=0, ch=3(mono)
        const uint32_t hdr = build_header_word(3, 1, 1, 14, 1, 0, 0, 3, 0, 0, 0, 0);
        std::vector<uint8_t> frame(1500, 0);
        write_header_bytes(hdr, frame.data());

        pt7_mp3_frame_info_t info{};
        const pt7_mp3_status_t status = pt7_mp3_parse_header(frame.data(), frame.size(), &info);

        TEST_CHECK(status == PT7_MP3_OK);
        TEST_CHECK(info.bitrate_kbps == 320);
        TEST_CHECK(info.sample_rate_hz == 48000);
        TEST_CHECK(info.channels == 1);
        TEST_CHECK(info.channel_mode == PT7_MP3_CHANNEL_SINGLE_CHANNEL);
        // (144000 * 320) / 48000 = 960 bytes
        TEST_CHECK(info.frame_size_bytes == 960);
    }

    // Test 2.4: MPEG-1 Layer III, 32 kHz, Dual Channel
    {
        // version=3, layer=1, prot=1, bitrate_idx=6(80k), srate=2(32k), pad=0, ch=2(dual channel)
        const uint32_t hdr = build_header_word(3, 1, 1, 6, 2, 0, 0, 2, 0, 0, 0, 0);
        std::vector<uint8_t> frame(1000, 0);
        write_header_bytes(hdr, frame.data());

        pt7_mp3_frame_info_t info{};
        const pt7_mp3_status_t status = pt7_mp3_parse_header(frame.data(), frame.size(), &info);

        TEST_CHECK(status == PT7_MP3_OK);
        TEST_CHECK(info.bitrate_kbps == 80);
        TEST_CHECK(info.sample_rate_hz == 32000);
        TEST_CHECK(info.channels == 2);
        TEST_CHECK(info.channel_mode == PT7_MP3_CHANNEL_DUAL_CHANNEL);
        // (144000 * 80) / 32000 = 360 bytes
        TEST_CHECK(info.frame_size_bytes == 360);
    }

    // Test 2.5: MPEG-2 Layer III, 64 kbps, 22.05 kHz, 576 samples
    {
        // version=2(MPEG-2), layer=1(L3), prot=1, bitrate_idx=8(64k), srate=0(22.05k), pad=0
        const uint32_t hdr = build_header_word(2, 1, 1, 8, 0, 0, 0, 0, 0, 0, 0, 0);
        std::vector<uint8_t> frame(500, 0);
        write_header_bytes(hdr, frame.data());

        pt7_mp3_frame_info_t info{};
        const pt7_mp3_status_t status = pt7_mp3_parse_header(frame.data(), frame.size(), &info);

        TEST_CHECK(status == PT7_MP3_OK);
        TEST_CHECK(info.version == PT7_MP3_VERSION_MPEG2);
        TEST_CHECK(info.samples_per_frame == 576);
        TEST_CHECK(info.sample_rate_hz == 22050);
        TEST_CHECK(info.bitrate_kbps == 64);
        // (72000 * 64) / 22050 = 208 bytes
        TEST_CHECK(info.frame_size_bytes == 208);
    }

    // Test 2.6: MPEG-2.5 Layer III, 8 kbps, 8 kHz, 576 samples
    {
        // version=0(MPEG-2.5), layer=1, prot=1, bitrate_idx=1(8k), srate=2(8k), pad=0
        const uint32_t hdr = build_header_word(0, 1, 1, 1, 2, 0, 0, 3, 0, 0, 0, 0);
        std::vector<uint8_t> frame(500, 0);
        write_header_bytes(hdr, frame.data());

        pt7_mp3_frame_info_t info{};
        const pt7_mp3_status_t status = pt7_mp3_parse_header(frame.data(), frame.size(), &info);

        TEST_CHECK(status == PT7_MP3_OK);
        TEST_CHECK(info.version == PT7_MP3_VERSION_MPEG2_5);
        TEST_CHECK(info.samples_per_frame == 576);
        TEST_CHECK(info.sample_rate_hz == 8000);
        TEST_CHECK(info.bitrate_kbps == 8);
        // (72000 * 8) / 8000 = 72 bytes
        TEST_CHECK(info.frame_size_bytes == 72);
    }
}

// ============================================================================
// 3. Invalid / Corrupted Header Tests
// ============================================================================
void test_invalid_headers()
{
    std::cout << "[TEST] Invalid and Corrupted Headers...\n";

    pt7_mp3_frame_info_t info{};

    // Test 3.1: Null pointers
    TEST_CHECK(pt7_mp3_parse_header(nullptr, 100, &info) == PT7_MP3_ERR_INVALID_ARG);
    uint8_t dummy[4] = { 0xFF, 0xFB, 0x90, 0x00 };
    TEST_CHECK(pt7_mp3_parse_header(dummy, 100, nullptr) == PT7_MP3_ERR_INVALID_ARG);

    // Test 3.2: Buffer too small (< 4 bytes)
    TEST_CHECK(pt7_mp3_parse_header(dummy, 0, &info) == PT7_MP3_NEED_MORE_DATA);
    TEST_CHECK(pt7_mp3_parse_header(dummy, 1, &info) == PT7_MP3_NEED_MORE_DATA);
    TEST_CHECK(pt7_mp3_parse_header(dummy, 3, &info) == PT7_MP3_NEED_MORE_DATA);

    // Test 3.3: Bad sync word
    {
        uint8_t bad_sync[4] = { 0x7F, 0xFB, 0x90, 0x00 };
        TEST_CHECK(pt7_mp3_parse_header(bad_sync, 4, &info) == PT7_MP3_ERR_INVALID_HEADER);
        uint8_t zero_sync[4] = { 0x00, 0x00, 0x00, 0x00 };
        TEST_CHECK(pt7_mp3_parse_header(zero_sync, 4, &info) == PT7_MP3_ERR_INVALID_HEADER);
    }

    // Test 3.4: Reserved MPEG version (0b01)
    {
        uint32_t hdr = build_header_word(1, 1, 1, 9, 0, 0, 0, 0, 0, 0, 0, 0);
        uint8_t buf[10];
        write_header_bytes(hdr, buf);
        TEST_CHECK(pt7_mp3_parse_header(buf, sizeof(buf), &info) == PT7_MP3_ERR_INVALID_HEADER);
    }

    // Test 3.5: Wrong Layer (Layer I, Layer II, reserved Layer 0)
    {
        // Layer 0 (reserved)
        uint32_t hdr0 = build_header_word(3, 0, 1, 9, 0, 0, 0, 0, 0, 0, 0, 0);
        uint8_t buf[10];
        write_header_bytes(hdr0, buf);
        TEST_CHECK(pt7_mp3_parse_header(buf, sizeof(buf), &info) == PT7_MP3_ERR_INVALID_HEADER);

        // Layer II (unsupported)
        uint32_t hdr2 = build_header_word(3, 2, 1, 9, 0, 0, 0, 0, 0, 0, 0, 0);
        write_header_bytes(hdr2, buf);
        TEST_CHECK(pt7_mp3_parse_header(buf, sizeof(buf), &info) == PT7_MP3_ERR_UNSUPPORTED);

        // Layer I (unsupported)
        uint32_t hdr1 = build_header_word(3, 3, 1, 9, 0, 0, 0, 0, 0, 0, 0, 0);
        write_header_bytes(hdr1, buf);
        TEST_CHECK(pt7_mp3_parse_header(buf, sizeof(buf), &info) == PT7_MP3_ERR_UNSUPPORTED);
    }

    // Test 3.6: Invalid bitrate index (0xF) & free bitrate (0x0)
    {
        uint8_t buf[10];
        // 0xF (bad)
        uint32_t hdr_bad = build_header_word(3, 1, 1, 15, 0, 0, 0, 0, 0, 0, 0, 0);
        write_header_bytes(hdr_bad, buf);
        TEST_CHECK(pt7_mp3_parse_header(buf, sizeof(buf), &info) == PT7_MP3_ERR_INVALID_HEADER);

        // 0x0 (free)
        uint32_t hdr_free = build_header_word(3, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        write_header_bytes(hdr_free, buf);
        TEST_CHECK(pt7_mp3_parse_header(buf, sizeof(buf), &info) == PT7_MP3_ERR_UNSUPPORTED);
    }

    // Test 3.7: Invalid sample rate index (3)
    {
        uint8_t buf[10];
        uint32_t hdr = build_header_word(3, 1, 1, 9, 3, 0, 0, 0, 0, 0, 0, 0);
        write_header_bytes(hdr, buf);
        TEST_CHECK(pt7_mp3_parse_header(buf, sizeof(buf), &info) == PT7_MP3_ERR_INVALID_HEADER);
    }

    // Test 3.8: Invalid emphasis index (2)
    {
        uint8_t buf[10];
        uint32_t hdr = build_header_word(3, 1, 1, 9, 0, 0, 0, 0, 0, 0, 0, 2);
        write_header_bytes(hdr, buf);
        TEST_CHECK(pt7_mp3_parse_header(buf, sizeof(buf), &info) == PT7_MP3_ERR_INVALID_HEADER);
    }

    // Test 3.9: Truncated frame (valid header, but buffer shorter than frame_size_bytes)
    {
        // 128 kbps, 44.1 kHz, frame size = 417 bytes. Provide only 200 bytes.
        uint32_t hdr = build_header_word(3, 1, 1, 9, 0, 0, 0, 0, 0, 0, 0, 0);
        std::vector<uint8_t> truncated(200, 0);
        write_header_bytes(hdr, truncated.data());

        const pt7_mp3_status_t st = pt7_mp3_parse_header(truncated.data(), truncated.size(), &info);
        TEST_CHECK(st == PT7_MP3_NEED_MORE_DATA);
        // Header info must still be correctly decoded even though frame is truncated
        TEST_CHECK(info.frame_size_bytes == 417);
        TEST_CHECK(info.bitrate_kbps == 128);
    }
}

// ============================================================================
// 4. Frame Scanning Tests
// ============================================================================
void test_frame_scanning()
{
    std::cout << "[TEST] Frame Scanning...\n";

    // Test 4.1: Frame preceded by 50 bytes of garbage
    {
        const size_t garbage_len = 50;
        const uint32_t hdr = build_header_word(3, 1, 1, 9, 0, 0, 0, 0, 0, 0, 0, 0);
        std::vector<uint8_t> stream(600, 0x55); // initialize with filler

        write_header_bytes(hdr, stream.data() + garbage_len);

        size_t offset = 0;
        pt7_mp3_frame_info_t info{};
        const pt7_mp3_status_t st = pt7_mp3_scan_frame(stream.data(), stream.size(), &offset, &info);

        TEST_CHECK(st == PT7_MP3_OK);
        TEST_CHECK(offset == garbage_len);
        TEST_CHECK(info.bitrate_kbps == 128);
        TEST_CHECK(info.frame_size_bytes == 417);
    }

    // Test 4.2: False sync candidate followed by true frame
    {
        std::vector<uint8_t> stream(600, 0);
        // False sync: 0xFF 0xFB but with invalid bitrate 0x0F
        const uint32_t false_hdr = build_header_word(3, 1, 1, 15, 0, 0, 0, 0, 0, 0, 0, 0);
        write_header_bytes(false_hdr, stream.data() + 10);

        // Real frame at offset 30
        const uint32_t real_hdr = build_header_word(3, 1, 1, 9, 0, 0, 0, 0, 0, 0, 0, 0);
        write_header_bytes(real_hdr, stream.data() + 30);

        size_t offset = 0;
        pt7_mp3_frame_info_t info{};
        const pt7_mp3_status_t st = pt7_mp3_scan_frame(stream.data(), stream.size(), &offset, &info);

        TEST_CHECK(st == PT7_MP3_OK);
        TEST_CHECK(offset == 30);
        TEST_CHECK(info.frame_size_bytes == 417);
    }

    // Test 4.3: No sync anywhere in buffer
    {
        std::vector<uint8_t> stream(200, 0x12);
        size_t offset = 0;
        const pt7_mp3_status_t st = pt7_mp3_scan_frame(stream.data(), stream.size(), &offset, nullptr);
        TEST_CHECK(st == PT7_MP3_ERR_SYNC_NOT_FOUND);
    }

    // Test 4.4: Trailing candidate sync at end of buffer
    {
        std::vector<uint8_t> stream(20, 0);
        stream[19] = 0xFF; // trailing potential sync
        size_t offset = 0;
        const pt7_mp3_status_t st = pt7_mp3_scan_frame(stream.data(), stream.size(), &offset, nullptr);
        TEST_CHECK(st == PT7_MP3_NEED_MORE_DATA);
        TEST_CHECK(offset == 19);
    }
}

// ============================================================================
// 5. Security & Robustness (Fuzz & Lifecycle)
// ============================================================================
void test_security_and_robustness()
{
    std::cout << "[TEST] Security & Robustness...\n";

    // Test 5.1: Decoder lifecycle
    {
        pt7_mp3_decoder_t* dec = pt7_mp3_create();
        TEST_CHECK(dec != nullptr);
        pt7_mp3_reset(dec);
        pt7_mp3_destroy(dec);
        // NULL destroy safe
        pt7_mp3_destroy(nullptr);
    }

    // Test 5.2: Status string verification
    {
        TEST_CHECK(std::strcmp(pt7_mp3_status_string(PT7_MP3_OK), "Success") == 0);
        TEST_CHECK(std::strlen(pt7_mp3_status_string(PT7_MP3_NEED_MORE_DATA)) > 0);
        TEST_CHECK(std::strlen(pt7_mp3_status_string(PT7_MP3_ERR_INVALID_HEADER)) > 0);
        TEST_CHECK(std::strlen(pt7_mp3_status_string(PT7_MP3_ERR_UNSUPPORTED)) > 0);
        TEST_CHECK(std::strlen(pt7_mp3_status_string(PT7_MP3_ERR_INVALID_ARG)) > 0);
        TEST_CHECK(std::strlen(pt7_mp3_status_string(PT7_MP3_ERR_SYNC_NOT_FOUND)) > 0);
        TEST_CHECK(std::strcmp(pt7_mp3_status_string(static_cast<pt7_mp3_status_t>(999)), "Unknown error") == 0);
    }

    // Test 5.3: Pseudo-random fuzz buffer: no crash, no UB, no hang
    {
        std::vector<uint8_t> fuzz_data(8192);
        uint32_t seed = 0x12345678;
        for (size_t i = 0; i < fuzz_data.size(); ++i) {
            seed = seed * 1664525U + 1013904223U;
            fuzz_data[i] = static_cast<uint8_t>(seed >> 24);
        }

        pt7_mp3_frame_info_t info{};
        size_t offset = 0;

        // Header parsing across sliding window
        for (size_t i = 0; i + 4 <= 512; ++i) {
            (void)pt7_mp3_parse_header(fuzz_data.data() + i, fuzz_data.size() - i, &info);
        }

        // Frame scanning
        (void)pt7_mp3_scan_frame(fuzz_data.data(), fuzz_data.size(), &offset, &info);

        // Bitstream reader under random bounds
        pt7::mp3::BitstreamReader reader(fuzz_data.data(), fuzz_data.size());
        while (!reader.is_eof()) {
            const uint32_t n = (reader.read_bit() ? 7 : 13);
            (void)reader.read_bits(n);
        }
        TEST_CHECK(reader.is_eof());
    }
}

int main()
{
    std::cout << "========================================\n";
    std::cout << " PT7-MP3 Task 1 Foundation Test Suite   \n";
    std::cout << "========================================\n";

    test_bitstream_reader();
    test_valid_headers();
    test_invalid_headers();
    test_frame_scanning();
    test_security_and_robustness();

    std::cout << "========================================\n";
    std::cout << "Total Tests Run:    " << g_tests_run << "\n";
    std::cout << "Tests Passed:       " << g_tests_passed << "\n";
    std::cout << "Tests Failed:       " << g_tests_failed << "\n";
    std::cout << "========================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
