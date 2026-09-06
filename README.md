# PT7-MP3

**PT7-MP3** is a standalone MP3 Layer III decoder library written in **C++20** with a stable **C API** and **zero external dependencies**.

> PT7-MP3 is a standalone MP3 Layer III decoder and does not require the PT7 framework, PT7::Audio, PMAI, Qt, WASAPI, Media Foundation, FFmpeg, or any other external decoder.

---

## What it does

PT7-MP3 decodes MPEG-1 / MPEG-2 / MPEG-2.5 Layer III (`MP3`) bitstreams into:

- **PCM16** — interleaved 16-bit signed integer audio
- **Float32** — interleaved 32-bit floating-point audio (normalized `[-1.0, +1.0]`)

The full decoding pipeline is implemented from scratch:

```
MP3 bitstream → ID3v2 skip → Frame sync → Header parse → Side info
→ Scalefactors → Huffman → Requantization → Stereo (MS/Intensity)
→ Antialias → IMDCT (long/short) → Overlap/Add → Polyphase Synthesis
→ PCM output (PCM16 / Float32)
```

---

## Supported formats

| Category | Feature | Status |
|:---|:---|:---:|
| MPEG | MPEG-1 Layer III (ISO 11172-3) | PASS |
| MPEG | MPEG-2 Layer III (ISO 13818-3) | PASS |
| MPEG | MPEG-2.5 Layer III | PASS |
| Channels | Mono, Stereo, Joint Stereo (MS/Intensity), Dual | PASS |
| Sample rates | 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 Hz | PASS |
| Bitrates | 8–320 kbps (all ISO rates) | PASS |
| Block types | Long, Start, Short, Stop, Mixed | PASS |
| Rate control | CBR and VBR (Xing/Info/VBRI) | PASS |
| Metadata | ID3v2 skip, LAME gapless | PASS |
| Output | PCM16 interleaved, Float32 interleaved | PASS |

---

## Public API

The entire public API is in [`include/pt7_mp3.h`](include/pt7_mp3.h). It uses:

- Opaque decoder handle (`pt7_mp3_decoder_t`)
- `extern "C"` linkage
- Standard C types only (`uint8_t`, `size_t`, `int16_t`, `float`)
- No C++ classes, no internal structures exposed

### Core functions

```c
// Lifecycle
pt7_mp3_decoder_t* pt7_mp3_create(void);
void               pt7_mp3_destroy(pt7_mp3_decoder_t* dec);
void               pt7_mp3_reset(pt7_mp3_decoder_t* dec);

// Streaming (incremental feed)
pt7_mp3_status_t   pt7_mp3_feed(pt7_mp3_decoder_t* dec,
                                const uint8_t* data, size_t size);
pt7_mp3_status_t   pt7_mp3_read_pcm16(pt7_mp3_decoder_t* dec,
                                      int16_t* out, size_t cap,
                                      size_t* out_written);
pt7_mp3_status_t   pt7_mp3_read_pcm_float(pt7_mp3_decoder_t* dec,
                                          float* out, size_t cap,
                                          size_t* out_written);

// Direct frame decode
pt7_mp3_status_t   pt7_mp3_decode_frame_pcm16(pt7_mp3_decoder_t* dec,
                                              const uint8_t* in, size_t in_size,
                                              int16_t* out, size_t cap,
                                              size_t* out_written,
                                              size_t* out_consumed);
pt7_mp3_status_t   pt7_mp3_decode_frame_float(pt7_mp3_decoder_t* dec,
                                             const uint8_t* in, size_t in_size,
                                             float* out, size_t cap,
                                             size_t* out_written,
                                             size_t* out_consumed);

// Info
pt7_mp3_status_t   pt7_mp3_get_frame_info(const pt7_mp3_decoder_t* dec,
                                          pt7_mp3_frame_info_t* out_info);
pt7_mp3_status_t   pt7_mp3_get_vbr_info(const pt7_mp3_decoder_t* dec,
                                       pt7_mp3_vbr_info_t* out_vbr);
pt7_mp3_status_t   pt7_mp3_get_info(const pt7_mp3_decoder_t* dec,
                                    pt7_mp3_stream_info_t* out_info);

// Gapless playback (LAME delay/padding trimming)
pt7_mp3_status_t   pt7_mp3_enable_gapless(pt7_mp3_decoder_t* dec);
uint64_t           pt7_mp3_get_gapless_trimmed_front(const pt7_mp3_decoder_t* dec);
uint64_t           pt7_mp3_get_gapless_trimmed_end(const pt7_mp3_decoder_t* dec);

// Seeking (Phase D)
pt7_mp3_status_t   pt7_mp3_set_source(pt7_mp3_decoder_t* dec,
                                      const uint8_t* data, size_t size);
pt7_mp3_status_t   pt7_mp3_seek(pt7_mp3_decoder_t* dec,
                                double position_seconds,
                                double* out_actual_seconds);
int                pt7_mp3_is_seekable(const pt7_mp3_decoder_t* dec);

const char*        pt7_mp3_status_string(pt7_mp3_status_t status);
```

### Stream info

`pt7_mp3_get_info()` returns a `pt7_mp3_stream_info_t` containing:

- MPEG version, layer, sample rate, channels, channel mode
- Bitrate (current frame) and bitrate mode (CBR/VBR/Unknown)
- Frame count (decoded so far) and total frames (from VBR header)
- Sample count and total samples
- Estimated duration in seconds
- Gapless metadata: encoder delay, end padding, availability flag

### Gapless playback

When LAME gapless metadata is present, call `pt7_mp3_enable_gapless()` after feeding the first frame. Subsequent `read_pcm16` / `read_pcm_float` calls will automatically:

1. Skip `encoder_delay` samples at the start of the stream
2. Trim `end_padding` samples at the end of the stream

The function is idempotent — calling it multiple times does not apply trimming twice. If no gapless metadata is available, the function returns `PT7_MP3_ERR_SYNC_NOT_FOUND` and no trimming is applied.

### Seeking

For random access / temporal seeking, provide the complete MP3 data via `pt7_mp3_set_source()`. This builds an internal frame index and enables seeking:

```c
pt7_mp3_set_source(dec, mp3_data, mp3_size);

double actual = 0;
pt7_mp3_seek(dec, 30.0, &actual);  // seek to 30 seconds

int16_t pcm[2304];
size_t written = 0;
pt7_mp3_read_pcm16(dec, pcm, 2304, &written);
```

The seek performs:
1. Frame index lookup (binary search on time → frame → byte offset)
2. Full decoder state reset (reservoir, overlap, synthesis)
3. Warmup decode of preceding frames to fill the bit reservoir
4. PCM output starts at the requested position

Seeking requires a seekable source. Streaming-only decoders (using `pt7_mp3_feed()`) return `PT7_MP3_ERR_SEEK_NOT_SUPPORTED` from `pt7_mp3_seek()`.

---

## Minimal example

```c
#include "pt7_mp3.h"
#include <stdio.h>

int main(void) {
    pt7_mp3_decoder_t* dec = pt7_mp3_create();

    /* feed MP3 data (any chunk size) */
    pt7_mp3_feed(dec, mp3_buffer, mp3_size);

    /* read decoded PCM16 */
    int16_t pcm[2304];
    size_t written = 0;
    while (pt7_mp3_read_pcm16(dec, pcm, 2304, &written) == PT7_MP3_OK
           && written > 0) {
        /* use pcm[0..written-1] — interleaved L,R,L,R,... */
    }

    pt7_mp3_destroy(dec);
    return 0;
}
```

A complete `MP3 → WAV` example is in [`examples/decode_file/`](examples/decode_file/).

---

## Build with CMake

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build library, example, and tests
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure
```

### Install

```bash
cmake --install build --prefix /opt/pt7mp3
```

This installs:

```
/opt/pt7mp3/
├── include/
│   └── pt7_mp3.h
├── lib/
│   ├── libPT7MP3.a
│   └── cmake/PT7MP3/
│       ├── PT7MP3Config.cmake
│       ├── PT7MP3ConfigVersion.cmake
│       └── PT7MP3Targets.cmake
```

---

## Integration in external projects

After installation, consume PT7-MP3 from any CMake project:

```cmake
find_package(PT7MP3 CONFIG REQUIRED)

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE PT7MP3::PT7MP3)
```

Or add as a subdirectory (no install needed):

```cmake
add_subdirectory(extern/PMAI_PT7_MP3)
target_link_libraries(my_app PRIVATE PT7MP3::PT7MP3)
```

---

## Dependencies

**None.** PT7-MP3 has zero external dependencies:

- No PT7 / PT7::Audio / PMAI
- No Qt, no WASAPI, no Media Foundation, no DirectX
- No FFmpeg, no libmad, no mpg123
- Only the C/C++ standard library

The decoder core compiles autonomously on any platform with a C++20 compiler.

---

## Library structure

```
include/
    pt7_mp3.h              ← Public C API (the only header consumers need)

src/
    mp3_bitstream.*        ← Bitstream reader
    mp3_frame.*            ← Frame header parser & sync scanner
    mp3_side_info.*        ← Side information parser
    mp3_scalefactors.*     ← Scalefactor decoder
    mp3_huffman.*          ← Huffman decoder (all 32 ISO tables)
    mp3_huffman_tables.h   ← Huffman code tables
    mp3_reservoir.*        ← Bit reservoir (main_data assembly)
    mp3_requant.*          ← Nonlinear requantization + SHORT/MIXED reorder
    mp3_stereo.*           ← MS stereo / Intensity stereo
    mp3_antialias.*        ← Antialias butterfly filter
    mp3_imdct.*            ← IMDCT (36-point long, 12-point short)
    mp3_overlap.*          ← Overlap/add buffer
    mp3_synth.*            ← Polyphase synthesis filterbank (32 subbands)
    mp3_id3.*              ← ID3v2 tag detection & skip
    mp3_vbr.*              ← VBR/CBR metadata (Xing/Info/VBRI/LAME)
    mp3_decoder.cpp        ← Decoder instance & public API implementation

examples/
    decode_file/           ← MP3 → PCM16 WAV standalone example

tests/
    test_api.cpp           ← Public C API tests
    test_foundation.cpp    ← Frame header & sync tests
    test_task2.cpp         ← Side info & Huffman tests
    test_compatibility.cpp ← Format compatibility matrix
    test_fuzz.cpp          ← Fuzz / robustness tests
    test_stress.cpp        ← Stress / streaming tests
    test_robustness.cpp    ← Resync & corrupted frame tests
    test_metadata.cpp      ← Metadata & gapless tests
    test_seek.cpp          ← Seeking tests
    test_benchmark.cpp     ← Performance benchmark (Phase E)
    vectors/               ← Test MP3 files
```

---

## Performance

Benchmark results (Release build, MinGW GCC, Windows):

| Metric | Value |
|:---|:---|
| Realtime factor | ~80x (427s audio decoded in ~5.3s) |
| Throughput | ~3.1 MB/s |
| Peak memory | ~38 MB (17 MB file with seek index) |
| Seek time | ~3 ms average (after index build) |
| Index build | ~5 ms (16376 frames) |

Run the benchmark manually:

```bash
./test_benchmark
```

---

## Platforms

The decoder core is platform-independent and compiles on any system with a C++20 compiler.

| Platform | Compiler | Status |
|:---|:---|:---|
| Windows | MinGW GCC 14+ | Compiled and tested |
| Windows | MSVC 2022 | Compatible (CMake ready) |
| Linux | GCC 12+ | Source verified, not compiled in this environment |
| Linux | Clang 14+ | Source verified, not compiled in this environment |
| macOS | Apple Clang 14+ | Source verified, not compiled in this environment |

The core library uses only `<cstdint>`, `<cstddef>`, `<cmath>`, `<cstring>`, `<vector>`, and other standard C++ headers. No platform-specific headers are included in the core.

The Windows GUI player (`examples/minimal-player/`) is Windows-only and uses WASAPI/Win32. It is gated behind `if(WIN32)` in CMake and does not affect core portability.

---

## License

PT7-MP3 is released under the MIT License.
See the [LICENSE](LICENSE) file for the complete license text.

The MP3 decoding algorithms implemented in this library are based on the publicly available ISO/IEC 11172-3 standard specification. PT7-MP3 does not depend on, incorporate, or derive from any external decoder library.

---

## Tests

```bash
ctest --test-dir build --output-on-failure
```

| Test | Description |
|:---|:---|
| `test_api` | Public C API: create/destroy, feed/read, PCM16, Float32, reset, partial input |
| `test_robustness` | Corrupted frames, resync, garbage, EOF, fuzz, no infinite loops |
| `test_metadata` | Stream info, Xing/Info/VBRI, LAME gapless, duration, frame counts |
| `test_seek` | Seeking: CBR/VBR, Xing, gapless, repeated seeks, accuracy, non-seekable |
| `test_foundation` | Frame header parsing, sync scanner, bitstream reader |
| `test_task2` | Side information, scalefactors, Huffman decoding, bit reservoir |
| `test_compatibility` | ID3v2, VBR headers, resync, full MPEG format matrix |
| `test_fuzz` | Corrupted streams, truncated frames, random bytes |
| `test_stress` | Mass decode, rapid reset, variable chunk streaming |
