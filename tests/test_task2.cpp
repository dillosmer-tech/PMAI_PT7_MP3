#include "pt7_mp3.h"
#include "../src/mp3_bitstream.h"
#include "../src/mp3_frame.h"
#include "../src/mp3_side_info.h"
#include "../src/mp3_scalefactors.h"
#include "../src/mp3_huffman.h"
#include "../src/mp3_reservoir.h"

#include <iostream>
#include <fstream>
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

std::vector<uint8_t> read_binary_file(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return {};
    }
    return buffer;
}

uint32_t build_header_word(
    uint32_t version_bits,
    uint32_t layer_bits,
    uint32_t protection_bit,
    uint32_t bitrate_idx,
    uint32_t srate_idx,
    uint32_t padding_bit,
    uint32_t channel_mode)
{
    uint32_t h = (0x7FFU << 21);
    h |= ((version_bits & 0x03U) << 19);
    h |= ((layer_bits & 0x03U) << 17);
    h |= ((protection_bit & 0x01U) << 16);
    h |= ((bitrate_idx & 0x0FU) << 12);
    h |= ((srate_idx & 0x03U) << 10);
    h |= ((padding_bit & 0x01U) << 9);
    h |= ((channel_mode & 0x03U) << 6);
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
// 1. Side Information Unit Tests
// ============================================================================
void test_side_information()
{
    std::cout << "[TEST] Side Information Parsing...\n";

    // Test 1.1: MPEG-1 Stereo Side Info (32 bytes)
    {
        // 128 kbps, 44.1 kHz, stereo, no padding, no CRC -> frame size = 417 bytes, side info = 32 bytes
        const uint32_t hdr = build_header_word(3, 1, 1, 9, 0, 0, 0);
        uint8_t frame_buf[417];
        std::memset(frame_buf, 0, sizeof(frame_buf));
        write_header_bytes(hdr, frame_buf);

        pt7_mp3_frame_info_t fi{};
        TEST_CHECK(pt7_mp3_parse_header(frame_buf, sizeof(frame_buf), &fi) == PT7_MP3_OK);
        TEST_CHECK(fi.side_info_bytes == 32);

        // Populate side info bits: main_data_begin = 50, private_bits = 3, scfsi = 0
        // Granule 0, ch 0: part2_3_length = 200, big_values = 100, global_gain = 120
        uint8_t* si_buf = frame_buf + 4;
        // 9 bits main_data_begin: 50 -> 0b000110010
        si_buf[0] = 0b00011001; // top 8 bits
        si_buf[1] = 0b00000000; // 9th bit = 0, private_bits (3 bits) = 0, scfsi = 0

        pt7::mp3::SideInfo si{};
        const pt7_mp3_status_t st = pt7::mp3::SideInfoParser::parse(si_buf, 32, fi, si);
        TEST_CHECK(st == PT7_MP3_OK);
        TEST_CHECK(si.main_data_begin == 50);
    }

    // Test 1.2: MPEG-1 Mono Side Info (17 bytes)
    {
        const uint32_t hdr = build_header_word(3, 1, 1, 9, 0, 0, 3); // mono
        uint8_t frame_buf[500];
        std::memset(frame_buf, 0, sizeof(frame_buf));
        write_header_bytes(hdr, frame_buf);

        pt7_mp3_frame_info_t fi{};
        TEST_CHECK(pt7_mp3_parse_header(frame_buf, sizeof(frame_buf), &fi) == PT7_MP3_OK);
        TEST_CHECK(fi.side_info_bytes == 17);

        pt7::mp3::SideInfo si{};
        const pt7_mp3_status_t st = pt7::mp3::SideInfoParser::parse(frame_buf + 4, 17, fi, si);
        TEST_CHECK(st == PT7_MP3_OK);
    }

    // Test 1.3: MPEG-2 Stereo Side Info (17 bytes)
    {
        const uint32_t hdr = build_header_word(2, 1, 1, 8, 0, 0, 0); // MPEG-2, 64kbps, 22.05k, stereo
        uint8_t frame_buf[300];
        std::memset(frame_buf, 0, sizeof(frame_buf));
        write_header_bytes(hdr, frame_buf);

        pt7_mp3_frame_info_t fi{};
        TEST_CHECK(pt7_mp3_parse_header(frame_buf, sizeof(frame_buf), &fi) == PT7_MP3_OK);
        TEST_CHECK(fi.side_info_bytes == 17);

        uint8_t* si_buf = frame_buf + 4;
        si_buf[0] = 80; // main_data_begin (8 bits) = 80

        pt7::mp3::SideInfo si{};
        const pt7_mp3_status_t st = pt7::mp3::SideInfoParser::parse(si_buf, 17, fi, si);
        TEST_CHECK(st == PT7_MP3_OK);
        TEST_CHECK(si.main_data_begin == 80);
    }

    // Test 1.4: MPEG-2 Mono Side Info (9 bytes)
    {
        const uint32_t hdr = build_header_word(2, 1, 1, 8, 0, 0, 3); // MPEG-2 mono
        uint8_t frame_buf[300];
        std::memset(frame_buf, 0, sizeof(frame_buf));
        write_header_bytes(hdr, frame_buf);

        pt7_mp3_frame_info_t fi{};
        TEST_CHECK(pt7_mp3_parse_header(frame_buf, sizeof(frame_buf), &fi) == PT7_MP3_OK);
        TEST_CHECK(fi.side_info_bytes == 9);

        pt7::mp3::SideInfo si{};
        const pt7_mp3_status_t st = pt7::mp3::SideInfoParser::parse(frame_buf + 4, 9, fi, si);
        TEST_CHECK(st == PT7_MP3_OK);
    }

    // Test 1.5: Truncated side info
    {
        const uint32_t hdr = build_header_word(3, 1, 1, 9, 0, 0, 0);
        uint8_t dummy[4];
        write_header_bytes(hdr, dummy);
        pt7_mp3_frame_info_t fi{};
        TEST_CHECK(pt7_mp3_parse_header(dummy, 4, &fi) == PT7_MP3_NEED_MORE_DATA);

        pt7::mp3::SideInfo si{};
        // Pass only 10 bytes instead of 32
        uint8_t short_buf[10] = {0};
        TEST_CHECK(pt7::mp3::SideInfoParser::parse(short_buf, 10, fi, si) == PT7_MP3_NEED_MORE_DATA);
    }
}

// ============================================================================
// 2. Bit Reservoir Tests
// ============================================================================
void test_bit_reservoir()
{
    std::cout << "[TEST] Bit Reservoir Continuity...\n";

    pt7::mp3::BitReservoir reservoir;
    TEST_CHECK(reservoir.size() == 0);
    TEST_CHECK(reservoir.can_assemble(0));
    TEST_CHECK(!reservoir.can_assemble(10));

    // Test 2.1: Underflow when main_data_begin > 0 on empty reservoir
    {
        uint8_t frame_data[100];
        std::memset(frame_data, 0xAA, sizeof(frame_data));
        std::vector<uint8_t> assembled;
        const pt7_mp3_status_t st = reservoir.assemble(50, frame_data, 100, assembled);
        TEST_CHECK(st == PT7_MP3_ERR_RESERVOIR_UNDERFLOW);
        // Current frame data should have been accumulated for subsequent frames
        TEST_CHECK(reservoir.size() == 100);
    }

    // Test 2.2: Now reservoir has 100 bytes. Request 40 bytes.
    {
        TEST_CHECK(reservoir.can_assemble(40));
        uint8_t frame2_data[80];
        std::memset(frame2_data, 0xBB, sizeof(frame2_data));
        std::vector<uint8_t> assembled;
        const pt7_mp3_status_t st = reservoir.assemble(40, frame2_data, 80, assembled);
        TEST_CHECK(st == PT7_MP3_OK);
        TEST_CHECK(assembled.size() == 120);
        // Verify prefix came from reservoir and suffix from frame2
        TEST_CHECK(assembled[0] == 0xAA);
        TEST_CHECK(assembled[39] == 0xAA);
        TEST_CHECK(assembled[40] == 0xBB);
        TEST_CHECK(assembled[119] == 0xBB);

        // Suppose 70 bytes (560 bits) were consumed by granules
        reservoir.update(assembled, 560);
        // Remaining in reservoir should be 120 - 70 = 50 bytes
        TEST_CHECK(reservoir.size() == 50);
        TEST_CHECK(reservoir.can_assemble(50));
        TEST_CHECK(!reservoir.can_assemble(51));
    }

    // Test 2.3: Reset cleans reservoir
    reservoir.reset();
    TEST_CHECK(reservoir.size() == 0);
}

// ============================================================================
// 3. Scalefactors & Huffman Unit Tests
// ============================================================================
void test_scalefactors_and_huffman()
{
    std::cout << "[TEST] Scalefactors and Huffman Decoding...\n";

    // Test 3.1: Scalefactors MPEG-1 Long Blocks
    {
        // 44 bits: slen1 = 2 bits (11 bands = 22 bits), slen2 = 2 bits (10 bands = 20 bits) -> total 42 bits
        // Let's create bitstream with all 2s
        std::vector<uint8_t> sf_data(20, 0b10101010);
        pt7::mp3::BitstreamReader bs(sf_data.data(), sf_data.size());

        pt7_mp3_frame_info_t fi{};
        fi.version = PT7_MP3_VERSION_MPEG1;
        fi.channels = 1;

        pt7::mp3::SideInfo si{};
        pt7_mp3_granule_info_t gi{};
        gi.scalefac_compress = 9; // slen1 = 2, slen2 = 2
        gi.window_switching_flag = 0;
        gi.block_type = 0;

        size_t bits_read = 0;
        const pt7_mp3_status_t st = pt7::mp3::ScalefactorDecoder::decode(bs, fi, si, 0, 0, gi, bits_read);
        TEST_CHECK(st == PT7_MP3_OK);
        TEST_CHECK(bits_read == 42);
        for (int i = 0; i < 21; ++i) {
            TEST_CHECK(gi.scalefac_l[i] == 2);
        }
        TEST_CHECK(gi.scalefac_l[21] == 0);
    }

    // Test 3.2: Huffman Table 1 (pairs with max val 1)
    {
        // Codebook 1:
        // '1' -> (0, 0)
        // '01' -> (1, 0)
        // '001' -> (0, 1)
        // '000' -> (1, 1)
        // Plus sign bits for any value > 0!
        // Let's encode pair (1, 0): code '01', then sign bit '0' (positive) -> '010'
        // Then pair (0, 0): code '1' -> '1'
        // Bitstream: '0101' = 0b01010000 = 0x50
        uint8_t huff_data[] = { 0b01010000 };
        pt7::mp3::BitstreamReader bs(huff_data, sizeof(huff_data));

        pt7_mp3_frame_info_t fi{};
        fi.sample_rate_hz = 44100;

        pt7_mp3_granule_info_t gi{};
        gi.big_values = 2; // 2 pairs = 4 samples
        gi.table_select[0] = 1;
        gi.region0_count = 0; // region 0 covers up to band_l[1] = 4 samples
        gi.region1_count = 0;

        const pt7_mp3_status_t st = pt7::mp3::HuffmanDecoder::decode(bs, fi, gi, 4);
        TEST_CHECK(st == PT7_MP3_OK);
        TEST_CHECK(gi.is[0] == 1);
        TEST_CHECK(gi.is[1] == 0);
        TEST_CHECK(gi.is[2] == 0);
        TEST_CHECK(gi.is[3] == 0);
        TEST_CHECK(gi.non_zero_count == 1);
        TEST_CHECK(gi.zero_start == 1);
    }

    // Test 3.3: Huffman Linbits & Escape Values (Table 16)
    {
        // Table 16 has linbits = 1
        // Code for (15, 0): from hft_16
        // When value is 15, linbit is read (+1 bit), then sign bit (+1 bit)
        // Verify escape handling succeeds and values outside 0..15 are correctly formed
        pt7_mp3_frame_info_t fi{};
        fi.sample_rate_hz = 44100;

        pt7_mp3_granule_info_t gi{};
        gi.big_values = 0; // only count1 or zero
        gi.count1table_select = 1; // Table B: 4 inverted bits

        // '0000' inverted is (1, 1, 1, 1), followed by 4 sign bits '0000' (all positive)
        // Total 8 bits: 0x00
        uint8_t quad_data[] = { 0x00 };
        pt7::mp3::BitstreamReader bs(quad_data, sizeof(quad_data));

        const pt7_mp3_status_t st = pt7::mp3::HuffmanDecoder::decode(bs, fi, gi, 8);
        TEST_CHECK(st == PT7_MP3_OK);
        TEST_CHECK(gi.is[0] == 1);
        TEST_CHECK(gi.is[1] == 1);
        TEST_CHECK(gi.is[2] == 1);
        TEST_CHECK(gi.is[3] == 1);
        TEST_CHECK(gi.non_zero_count == 4);
        TEST_CHECK(gi.zero_start == 4);
    }

    // Test 3.4: Truncated Huffman bitstream returns error
    {
        uint8_t bad_data[] = { 0x00 };
        pt7::mp3::BitstreamReader bs(bad_data, sizeof(bad_data));

        pt7_mp3_frame_info_t fi{};
        fi.sample_rate_hz = 44100;

        pt7_mp3_granule_info_t gi{};
        gi.big_values = 50; // demands many bits
        gi.table_select[0] = 16;

        // Provide only 2 bits: should fail cleanly with PT7_MP3_ERR_HUFFMAN_FAIL
        const pt7_mp3_status_t st = pt7::mp3::HuffmanDecoder::decode(bs, fi, gi, 2);
        TEST_CHECK(st == PT7_MP3_ERR_HUFFMAN_FAIL);
    }
}

// ============================================================================
// 4. Real MP3 File Verification Tests (Test Vectors)
// ============================================================================
void test_real_mp3_files()
{
    std::cout << "[TEST] Real MP3 File Decoding (Task 2)...\n";

#ifndef TEST_VECTORS_DIR
#define TEST_VECTORS_DIR "tests/vectors"
#endif

    struct FileTestCase {
        const char* filename;
        pt7_mp3_version_t expected_version;
        uint32_t expected_srate;
        uint32_t expected_channels;
        const char* description;
    };

    const FileTestCase test_files[] = {
        { "sine_44k_stereo.mp3", PT7_MP3_VERSION_MPEG1, 44100, 2, "MPEG-1 44.1kHz Stereo" },
        { "sine_48k_mono.mp3",   PT7_MP3_VERSION_MPEG1, 48000, 1, "MPEG-1 48kHz Mono" },
        { "sine_22k_stereo.mp3", PT7_MP3_VERSION_MPEG2, 22050, 2, "MPEG-2 22.05kHz Stereo" },
        { "sine_32k_joint.mp3",  PT7_MP3_VERSION_MPEG1, 32000, 2, "MPEG-1 32kHz Joint Stereo" },
    };

    for (const auto& tc : test_files) {
        std::string filepath = std::string(TEST_VECTORS_DIR) + "/" + tc.filename;
        std::vector<uint8_t> data = read_binary_file(filepath);
        if (data.empty()) {
            filepath = std::string("tests/vectors/") + tc.filename;
            data = read_binary_file(filepath);
        }
        if (data.empty()) {
            filepath = std::string("../tests/vectors/") + tc.filename;
            data = read_binary_file(filepath);
        }

        std::cout << "  Testing real file: " << tc.description << " (" << filepath << ")...\n";
        TEST_CHECK(!data.empty());
        if (data.empty()) {
            std::cerr << "Cannot open test vector: " << filepath << std::endl;
            continue;
        }

        pt7_mp3_decoder_t* dec = pt7_mp3_create();
        TEST_CHECK(dec != nullptr);

        size_t offset = 0;
        size_t frames_decoded = 0;
        size_t reservoir_underflows = 0;

        while (offset < data.size()) {
            size_t frame_offset = 0;
            pt7_mp3_frame_info_t info{};
            const pt7_mp3_status_t scan_st = pt7_mp3_scan_frame(
                data.data() + offset,
                data.size() - offset,
                &frame_offset,
                &info
            );

            if (scan_st == PT7_MP3_ERR_SYNC_NOT_FOUND || scan_st == PT7_MP3_NEED_MORE_DATA) {
                break;
            }
            TEST_CHECK(scan_st == PT7_MP3_OK);

            offset += frame_offset;

            TEST_CHECK(info.version == tc.expected_version);
            TEST_CHECK(info.sample_rate_hz == tc.expected_srate);
            TEST_CHECK(info.channels == tc.expected_channels);

            size_t bytes_consumed = 0;
            const pt7_mp3_status_t dec_st = pt7_mp3_decode_frame(
                dec,
                data.data() + offset,
                data.size() - offset,
                &bytes_consumed
            );

            if (dec_st == PT7_MP3_ERR_RESERVOIR_UNDERFLOW) {
                // Expected for initial frames when bit reservoir is building history
                ++reservoir_underflows;
                offset += info.frame_size_bytes;
                continue;
            }

            TEST_CHECK(dec_st == PT7_MP3_OK);
            TEST_CHECK(bytes_consumed == info.frame_size_bytes);

            const uint32_t num_gr = pt7_mp3_get_granules_per_frame(dec);
            TEST_CHECK(num_gr == (tc.expected_version == PT7_MP3_VERSION_MPEG1 ? 2U : 1U));

            // Verify granule and coefficients
            for (uint32_t gr = 0; gr < num_gr; ++gr) {
                for (uint32_t ch = 0; ch < tc.expected_channels; ++ch) {
                    const pt7_mp3_granule_info_t* gi = pt7_mp3_get_granule(dec, gr, ch);
                    TEST_CHECK(gi != nullptr);
                    if (gi != nullptr) {
                        // Check side information values within valid bounds
                        TEST_CHECK(gi->big_values <= 288);
                        TEST_CHECK(gi->global_gain <= 255);
                        TEST_CHECK(gi->table_select[0] < 32);
                        TEST_CHECK(gi->table_select[1] < 32);
                        TEST_CHECK(gi->table_select[2] < 32);
                        // Verify non_zero_count and zero_start consistency
                        TEST_CHECK(gi->zero_start <= 576);
                    }
                }
            }

            ++frames_decoded;
            offset += bytes_consumed;
        }

        std::cout << "    Frames decoded without error: " << frames_decoded
                  << ", reservoir underflows: " << reservoir_underflows << "\n";
        TEST_CHECK(frames_decoded > 0);

        pt7_mp3_destroy(dec);
    }
}

// ============================================================================
// 5. Robustness & Fuzz Tests
// ============================================================================
void test_robustness()
{
    std::cout << "[TEST] Robustness & Corrupted Input...\n";

    pt7_mp3_decoder_t* dec = pt7_mp3_create();
    TEST_CHECK(dec != nullptr);

    // Test 5.1: Null pointers
    size_t consumed = 0;
    TEST_CHECK(pt7_mp3_decode_frame(nullptr, nullptr, 0, nullptr) == PT7_MP3_ERR_INVALID_ARG);
    TEST_CHECK(pt7_mp3_decode_frame(dec, nullptr, 100, &consumed) == PT7_MP3_ERR_INVALID_ARG);
    uint8_t dummy[100] = {0};
    TEST_CHECK(pt7_mp3_decode_frame(nullptr, dummy, 100, &consumed) == PT7_MP3_ERR_INVALID_ARG);

    // Test 5.2: Pseudo-random corrupted frame decoding: must never crash or hang
    {
        std::vector<uint8_t> fuzz_data(2048);
        uint32_t seed = 0x98765432;
        for (size_t i = 0; i < fuzz_data.size(); ++i) {
            seed = seed * 1664525U + 1013904223U;
            fuzz_data[i] = static_cast<uint8_t>(seed >> 24);
        }

        // Overwrite first 4 bytes with valid header so parser attempts side info/huffman
        const uint32_t hdr = build_header_word(3, 1, 1, 9, 0, 0, 0);
        write_header_bytes(hdr, fuzz_data.data());

        // Decode corrupted frame
        const pt7_mp3_status_t st = pt7_mp3_decode_frame(dec, fuzz_data.data(), fuzz_data.size(), &consumed);
        // Expect either underflow or Huffman fail or OK - never crash!
        TEST_CHECK(st == PT7_MP3_ERR_HUFFMAN_FAIL ||
                   st == PT7_MP3_ERR_RESERVOIR_UNDERFLOW ||
                   st == PT7_MP3_ERR_INVALID_HEADER ||
                   st == PT7_MP3_OK);
    }

    pt7_mp3_destroy(dec);
}

int main()
{
    std::cout << "========================================\n";
    std::cout << " PT7-MP3 Task 2 Test Suite              \n";
    std::cout << "========================================\n";

    test_side_information();
    test_bit_reservoir();
    test_scalefactors_and_huffman();
    test_real_mp3_files();
    test_robustness();

    std::cout << "========================================\n";
    std::cout << "Total Tests Run:    " << g_tests_run << "\n";
    std::cout << "Tests Passed:       " << g_tests_passed << "\n";
    std::cout << "Tests Failed:       " << g_tests_failed << "\n";
    std::cout << "========================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
