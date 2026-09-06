#ifndef PT7_MP3_H
#define PT7_MP3_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Status codes returned by PT7-MP3 functions.
 */
typedef enum pt7_mp3_status {
    PT7_MP3_OK = 0,
    PT7_MP3_NEED_MORE_DATA = 1,
    PT7_MP3_END_OF_STREAM = 2,
    PT7_MP3_ERR_INVALID_HEADER = -1,
    PT7_MP3_ERR_UNSUPPORTED = -2,
    PT7_MP3_ERR_INVALID_ARG = -3,
    PT7_MP3_ERR_SYNC_NOT_FOUND = -4,
    PT7_MP3_ERR_RESERVOIR_UNDERFLOW = -5,
    PT7_MP3_ERR_HUFFMAN_FAIL = -6,
    PT7_MP3_ERR_BUFFER_TOO_SMALL = -7,
    PT7_MP3_ERR_DECODE = -8,
    PT7_MP3_ERR_SEEK_NOT_SUPPORTED = -9,
    PT7_MP3_ERR_SEEK_OUT_OF_RANGE = -10
} pt7_mp3_status_t;

/**
 * @brief MPEG Audio standard version.
 */
typedef enum pt7_mp3_version {
    PT7_MP3_VERSION_MPEG2_5 = 0, /**< MPEG-2.5 (unofficial low sample rate extension) */
    PT7_MP3_VERSION_RESERVED = 1, /**< Reserved / invalid version */
    PT7_MP3_VERSION_MPEG2   = 2, /**< MPEG-2 (ISO/IEC 13818-3 LSF) */
    PT7_MP3_VERSION_MPEG1   = 3  /**< MPEG-1 (ISO/IEC 11172-3) */
} pt7_mp3_version_t;

/**
 * @brief MPEG Audio Layer description.
 */
typedef enum pt7_mp3_layer {
    PT7_MP3_LAYER_RESERVED = 0,
    PT7_MP3_LAYER_III      = 1, /**< Layer III (MP3) */
    PT7_MP3_LAYER_II       = 2, /**< Layer II */
    PT7_MP3_LAYER_I        = 3  /**< Layer I */
} pt7_mp3_layer_t;

/**
 * @brief Audio channel mode.
 */
typedef enum pt7_mp3_channel_mode {
    PT7_MP3_CHANNEL_STEREO       = 0, /**< Standard 2-channel stereo */
    PT7_MP3_CHANNEL_JOINT_STEREO = 1, /**< Joint stereo (intensity / MS stereo) */
    PT7_MP3_CHANNEL_DUAL_CHANNEL = 2, /**< Dual mono / dual independent channels */
    PT7_MP3_CHANNEL_SINGLE_CHANNEL = 3 /**< Single channel (mono) */
} pt7_mp3_channel_mode_t;

/**
 * @brief Emphasis settings.
 */
typedef enum pt7_mp3_emphasis {
    PT7_MP3_EMPHASIS_NONE      = 0,
    PT7_MP3_EMPHASIS_50_15_US  = 1,
    PT7_MP3_EMPHASIS_RESERVED  = 2,
    PT7_MP3_EMPHASIS_CCITT_J17 = 3
} pt7_mp3_emphasis_t;

/**
 * @brief MP3 Frame information extracted from the 32-bit frame header.
 */
typedef struct pt7_mp3_frame_info {
    pt7_mp3_version_t version;           /**< MPEG version (MPEG-1, MPEG-2, MPEG-2.5) */
    pt7_mp3_layer_t layer;               /**< Layer (Layer III) */
    uint32_t bitrate_kbps;               /**< Bitrate in kilobits per second */
    uint32_t sample_rate_hz;             /**< Sampling frequency in Hz */
    uint32_t channels;                   /**< Number of audio channels (1 or 2) */
    pt7_mp3_channel_mode_t channel_mode; /**< Channel mode enum */
    uint32_t mode_extension;             /**< Joint stereo mode extension bits (0..3) */
    uint32_t has_crc;                    /**< 1 if 16-bit CRC follows header, 0 otherwise */
    uint32_t padding;                    /**< 1 if frame contains padding slot, 0 otherwise */
    uint32_t private_bit;                /**< Private bit flag */
    uint32_t copyright;                  /**< Copyright flag */
    uint32_t original;                   /**< Original home/media flag */
    pt7_mp3_emphasis_t emphasis;         /**< Emphasis enum */
    size_t frame_size_bytes;             /**< Total frame size in bytes (header + payload) */
    uint32_t samples_per_frame;          /**< Audio samples per frame (1152 for MPEG-1, 576 for MPEG-2/2.5) */
    size_t header_bytes;                 /**< Header size in bytes (4, or 6 if CRC present) */
    size_t side_info_bytes;              /**< Side information size in bytes */
} pt7_mp3_frame_info_t;

/**
 * @brief Decoded granule information and spectral coefficients ready for requantization (Task 2).
 */
typedef struct pt7_mp3_granule_info {
    uint32_t part2_3_length;         /**< Total length of scalefactors and Huffman data in bits */
    uint32_t big_values;             /**< Number of big_values pairs (up to 288) */
    uint32_t global_gain;            /**< Quantizer step size / global gain (0..255) */
    uint32_t scalefac_compress;      /**< Scalefactor compression setting */
    uint32_t window_switching_flag;  /**< 1 if window switching is used */
    uint32_t block_type;             /**< 0: normal/long, 1: start, 2: short, 3: stop */
    uint32_t mixed_block_flag;       /**< 1 if lower frequencies use long window in short block */
    uint32_t table_select[3];        /**< Huffman table index for regions 0, 1, 2 */
    uint32_t subblock_gain[3];       /**< Gain offset for short windows */
    uint32_t region0_count;          /**< Number of scale factor bands in region 0 */
    uint32_t region1_count;          /**< Number of scale factor bands in region 1 */
    uint32_t preflag;                /**< Pre-emphasis flag */
    uint32_t scalefac_scale;         /**< Scale factor step size (0.5 or 1.0) */
    uint32_t count1table_select;     /**< Count1 quadruples Huffman table (0 or 1) */

    uint32_t scalefac_l[22];         /**< Decoded long scalefactors (sfb 0..21) */
    uint32_t scalefac_s[13][3];      /**< Decoded short scalefactors (sfb 0..12, window 0..2) */

    int32_t is[576];                 /**< Decoded integer spectral coefficients */
    uint32_t zero_start;             /**< Frequency bin index where coefficients become all zero */
    uint32_t non_zero_count;         /**< Count of non-zero spectral coefficients */
} pt7_mp3_granule_info_t;

/**
 * @brief Reconstructed subband samples ready for synthesis filterbank (Task 3 output).
 * 18 time samples for each of the 32 polyphase filterbank subbands.
 */
typedef struct pt7_mp3_subband_samples {
    float samples[18][32];           /**< samples[time_index][subband_index] */
} pt7_mp3_subband_samples_t;

/**
 * @brief VBR / CBR Xing, Info, and VBRI stream metadata (Task 5).
 */
typedef struct pt7_mp3_vbr_info {
    int has_vbr_header;              /**< 1 if Xing, Info, or VBRI header is detected */
    int is_vbr;                      /**< 1 if variable bitrate (Xing/VBRI), 0 if CBR (Info) */
    uint32_t total_frames;           /**< Total audio frames in stream (from VBR header) */
    uint32_t total_bytes;            /**< Total bytes in stream (from VBR header) */
    uint32_t quality;                /**< Quality indicator (0-100) */
    int has_toc;                     /**< 1 if 100-entry TOC is present */
    uint8_t toc[100];                /**< TOC seek table (percentages) */
    int has_gapless;                 /**< 1 if LAME gapless metadata is present */
    uint32_t encoder_delay;          /**< Encoder delay / priming in samples */
    uint32_t end_padding;            /**< Encoder padding at end of stream in samples */
    char encoder[10];                /**< Encoder string, e.g. "LAME3.100" */
} pt7_mp3_vbr_info_t;

/**
 * @brief Bitrate mode classification.
 */
typedef enum pt7_mp3_bitrate_mode {
    PT7_MP3_BITRATE_UNKNOWN = 0, /**< Bitrate mode not yet determined */
    PT7_MP3_BITRATE_CBR     = 1, /**< Constant bitrate (Info header or no VBR header) */
    PT7_MP3_BITRATE_VBR     = 2  /**< Variable bitrate (Xing or VBRI header) */
} pt7_mp3_bitrate_mode_t;

/**
 * @brief Stream-level technical information aggregated by the decoder.
 *
 * Populated incrementally as frames are decoded. Fields marked "when available"
 * may be zero until enough data has been processed or until a VBR/LAME header
 * is detected.
 */
typedef struct pt7_mp3_stream_info {
    /* Format */
    pt7_mp3_version_t version;          /**< MPEG version (1, 2, 2.5) */
    pt7_mp3_layer_t layer;              /**< Layer (always Layer III) */
    uint32_t sample_rate_hz;            /**< Sampling frequency in Hz */
    uint32_t channels;                  /**< Number of audio channels (1 or 2) */
    pt7_mp3_channel_mode_t channel_mode;/**< Channel mode enum */
    uint32_t bitrate_kbps;              /**< Bitrate of the most recent frame (kbps) */

    /* Bitrate mode */
    pt7_mp3_bitrate_mode_t bitrate_mode;/**< CBR, VBR, or Unknown */

    /* Counts (when available) */
    uint64_t frame_count;               /**< Frames decoded so far (or from VBR header) */
    uint64_t total_frames;              /**< Total frames declared by VBR header (0 if unknown) */
    uint64_t sample_count;              /**< Audio samples decoded so far (per channel) */
    uint64_t total_samples;             /**< Total samples declared (per channel, 0 if unknown) */
    double duration_seconds;            /**< Estimated or measured duration in seconds */

    /* Gapless (LAME) */
    int gapless_available;              /**< 1 if encoder delay / end padding are known */
    uint32_t encoder_delay;             /**< Samples to skip at start of stream */
    uint32_t end_padding;               /**< Samples to trim at end of stream */

    /* VBR header presence */
    int has_vbr_header;                 /**< 1 if Xing/Info/VBRI header was detected */
    int is_vbr;                         /**< 1 if stream is variable bitrate */
} pt7_mp3_stream_info_t;

/**
 * @brief Opaque PT7-MP3 decoder handle.
 */
typedef struct pt7_mp3_decoder pt7_mp3_decoder_t;

/**
 * @brief Allocates and initializes a new PT7-MP3 decoder instance.
 * @return Pointer to decoder instance, or NULL on allocation failure.
 */
pt7_mp3_decoder_t* pt7_mp3_create(void);

/**
 * @brief Destroys and frees a PT7-MP3 decoder instance.
 * @param decoder Decoder instance pointer (NULL-safe).
 */
void pt7_mp3_destroy(pt7_mp3_decoder_t* decoder);

/**
 * @brief Resets the internal state of the decoder for a new stream (clears reservoir & overlap).
 * @param decoder Decoder instance pointer.
 */
void pt7_mp3_reset(pt7_mp3_decoder_t* decoder);

/**
 * @brief Parses an MP3 frame header at the exact beginning of buffer.
 *
 * Checks sync word, version, layer, bitrate, sample rate, and calculates
 * frame length. Does not scan forward.
 *
 * @param buffer Pointer to input byte stream.
 * @param buffer_size Size of input buffer in bytes.
 * @param out_info Pointer to frame info structure to populate.
 * @return PT7_MP3_OK if header is valid and frame fits inside buffer,
 *         PT7_MP3_NEED_MORE_DATA if buffer is smaller than 4 bytes or smaller than frame length,
 *         PT7_MP3_ERR_INVALID_HEADER if header sync/fields are corrupt,
 *         PT7_MP3_ERR_UNSUPPORTED if version/layer/bitrate is unsupported (e.g. Layer I/II, free bitrate),
 *         PT7_MP3_ERR_INVALID_ARG if arguments are NULL.
 */
pt7_mp3_status_t pt7_mp3_parse_header(
    const uint8_t* buffer,
    size_t buffer_size,
    pt7_mp3_frame_info_t* out_info
);

/**
 * @brief Scans a buffer for the first valid MP3 frame header.
 *
 * @param buffer Pointer to input byte stream.
 * @param buffer_size Size of input buffer in bytes.
 * @param out_frame_offset Receives byte offset from start of buffer to the valid frame header.
 * @param out_info Optional pointer to frame info structure to populate (can be NULL).
 * @return PT7_MP3_OK on success,
 *         PT7_MP3_ERR_SYNC_NOT_FOUND if no valid frame header sync is found,
 *         PT7_MP3_NEED_MORE_DATA if a valid header is found but buffer is truncated,
 *         PT7_MP3_ERR_INVALID_ARG if arguments are invalid.
 */
pt7_mp3_status_t pt7_mp3_scan_frame(
    const uint8_t* buffer,
    size_t buffer_size,
    size_t* out_frame_offset,
    pt7_mp3_frame_info_t* out_info
);

/**
 * @brief Decodes an entire MP3 frame through Audio Reconstruction (Task 3).
 *
 * Processes:
 *   Frame Header -> Side Info -> Reservoir -> Scalefactors -> Huffman ->
 *   Requantization -> Stereo Processing -> Antialias -> IMDCT -> Overlap/Add -> Subband Samples
 *
 * @param decoder Initialized decoder instance.
 * @param buffer Input byte stream pointing to frame header.
 * @param buffer_size Number of available bytes in buffer.
 * @param out_bytes_consumed Optional pointer receiving bytes of the frame consumed.
 * @return Status code.
 */
pt7_mp3_status_t pt7_mp3_decode_frame(
    pt7_mp3_decoder_t* decoder,
    const uint8_t* buffer,
    size_t buffer_size,
    size_t* out_bytes_consumed
);

/**
 * @brief Retrieves decoded granule information for the most recently decoded frame.
 *
 * @param decoder Decoder instance.
 * @param gr Granule index (0 or 1 for MPEG-1; 0 for MPEG-2/2.5).
 * @param ch Channel index (0 for mono/left; 1 for right).
 * @return Pointer to granule info, or NULL if gr/ch invalid or no frame decoded.
 */
const pt7_mp3_granule_info_t* pt7_mp3_get_granule(
    const pt7_mp3_decoder_t* decoder,
    uint32_t gr,
    uint32_t ch
);

/**
 * @brief Retrieves reconstructed subband samples (Task 3) for the most recently decoded frame.
 *
 * @param decoder Decoder instance.
 * @param gr Granule index (0 or 1 for MPEG-1; 0 for MPEG-2/2.5).
 * @param ch Channel index (0 for mono/left; 1 for right).
 * @return Pointer to subband samples structure, or NULL if gr/ch invalid or no frame decoded.
 */
const pt7_mp3_subband_samples_t* pt7_mp3_get_subband_samples(
    const pt7_mp3_decoder_t* decoder,
    uint32_t gr,
    uint32_t ch
);

/**
 * @brief Returns the number of granules per frame (2 for MPEG-1, 1 for MPEG-2/2.5).
 * @param decoder Decoder instance.
 * @return Number of granules.
 */
uint32_t pt7_mp3_get_granules_per_frame(const pt7_mp3_decoder_t* decoder);

/**
 * @brief Decodes an MP3 frame directly to interleaved 16-bit signed integer PCM (Task 4).
 *
 * Samples are saturated / clamped to [-32768, 32767].
 * Stereo output is interleaved as L, R, L, R, ...
 * Mono output contains single-channel samples.
 *
 * @param decoder Decoder instance.
 * @param buffer Input byte stream starting at MP3 frame header.
 * @param buffer_size Input byte stream length in bytes.
 * @param out_pcm16 Output array for decoded int16_t samples.
 * @param pcm16_capacity_samples Maximum number of int16_t samples that out_pcm16 can hold.
 * @param out_samples_written Optional pointer to receive total number of samples written (channels * samples_per_channel).
 * @param out_bytes_consumed Optional pointer to receive bytes of the MP3 frame consumed from buffer.
 * @return PT7_MP3_OK on success,
 *         PT7_MP3_ERR_BUFFER_TOO_SMALL if pcm16_capacity_samples is too small,
 *         or other error status code.
 */
pt7_mp3_status_t pt7_mp3_decode_frame_pcm16(
    pt7_mp3_decoder_t* decoder,
    const uint8_t* buffer,
    size_t buffer_size,
    int16_t* out_pcm16,
    size_t pcm16_capacity_samples,
    size_t* out_samples_written,
    size_t* out_bytes_consumed
);

/**
 * @brief Decodes an MP3 frame directly to interleaved 32-bit floating point PCM (Task 4).
 *
 * Samples are normalized in [-1.0f, +1.0f].
 * Stereo output is interleaved as L, R, L, R, ...
 * Mono output contains single-channel samples.
 *
 * @param decoder Decoder instance.
 * @param buffer Input byte stream starting at MP3 frame header.
 * @param buffer_size Input byte stream length in bytes.
 * @param out_pcm_float Output array for decoded float samples.
 * @param pcm_float_capacity_samples Maximum number of float samples that out_pcm_float can hold.
 * @param out_samples_written Optional pointer to receive total number of samples written (channels * samples_per_channel).
 * @param out_bytes_consumed Optional pointer to receive bytes of the MP3 frame consumed from buffer.
 * @return PT7_MP3_OK on success,
 *         PT7_MP3_ERR_BUFFER_TOO_SMALL if pcm_float_capacity_samples is too small,
 *         or other error status code.
 */
pt7_mp3_status_t pt7_mp3_decode_frame_float(
    pt7_mp3_decoder_t* decoder,
    const uint8_t* buffer,
    size_t buffer_size,
    float* out_pcm_float,
    size_t pcm_float_capacity_samples,
    size_t* out_samples_written,
    size_t* out_bytes_consumed
);

/**
 * @brief Feeds arbitrary input chunks into the decoder's internal stream buffer (Task 4).
 * Supports partial frames, fragmented buffers, and multi-frame blocks.
 *
 * @param decoder Decoder instance.
 * @param data Input byte chunk.
 * @param data_size Size of chunk in bytes.
 * @return PT7_MP3_OK on success, PT7_MP3_ERR_INVALID_ARG if arguments are invalid.
 */
pt7_mp3_status_t pt7_mp3_feed(
    pt7_mp3_decoder_t* decoder,
    const uint8_t* data,
    size_t data_size
);

/**
 * @brief Decodes available complete frames from fed stream data and writes interleaved PCM16 (Task 4).
 *
 * @param decoder Decoder instance.
 * @param out_pcm16 Output array for decoded int16_t samples.
 * @param pcm16_capacity_samples Maximum number of int16_t samples that out_pcm16 can hold.
 * @param out_samples_written Pointer receiving the number of samples written.
 * @return PT7_MP3_OK if frames were decoded or stream buffer has no full frame,
 *         PT7_MP3_NEED_MORE_DATA if partial frame remains in buffer and more input is needed,
 *         PT7_MP3_ERR_BUFFER_TOO_SMALL if out_pcm16 capacity cannot hold the next frame.
 */
pt7_mp3_status_t pt7_mp3_read_pcm16(
    pt7_mp3_decoder_t* decoder,
    int16_t* out_pcm16,
    size_t pcm16_capacity_samples,
    size_t* out_samples_written
);

/**
 * @brief Decodes available complete frames from fed stream data and writes interleaved Float32 (Task 4).
 *
 * @param decoder Decoder instance.
 * @param out_pcm_float Output array for decoded float samples.
 * @param pcm_float_capacity_samples Maximum number of float samples that out_pcm_float can hold.
 * @param out_samples_written Pointer receiving the number of samples written.
 * @return PT7_MP3_OK if frames were decoded or stream buffer has no full frame,
 *         PT7_MP3_NEED_MORE_DATA if partial frame remains in buffer and more input is needed,
 *         PT7_MP3_ERR_BUFFER_TOO_SMALL if out_pcm_float capacity cannot hold the next frame.
 */
pt7_mp3_status_t pt7_mp3_read_pcm_float(
    pt7_mp3_decoder_t* decoder,
    float* out_pcm_float,
    size_t pcm_float_capacity_samples,
    size_t* out_samples_written
);

/**
 * @brief Flushes any remaining data in the decoder stream buffer and resets stream position (Task 4).
 * @param decoder Decoder instance.
 */
void pt7_mp3_flush(pt7_mp3_decoder_t* decoder);

/**
 * @brief Queries information about the most recently decoded frame (sample rate, channels, bitrate, etc.).
 * @param decoder Decoder instance.
 * @param out_info Pointer to frame info structure to populate.
 * @return PT7_MP3_OK on success, PT7_MP3_ERR_INVALID_ARG if no frame decoded yet or arguments null.
 */
pt7_mp3_status_t pt7_mp3_get_frame_info(
    const pt7_mp3_decoder_t* decoder,
    pt7_mp3_frame_info_t* out_info
);

/**
 * @brief Returns the memory footprint of a decoder instance in bytes.
 * @return Size of decoder struct in bytes.
 */
size_t pt7_mp3_get_instance_size(void);

/**
 * @brief Detects an ID3v2 tag at buffer start and returns its total byte size (Task 5).
 *
 * @param buffer Pointer to byte stream.
 * @param buffer_size Available bytes.
 * @param out_tag_size Receives total tag size in bytes to skip.
 * @return PT7_MP3_OK if ID3v2 tag is validly identified,
 *         PT7_MP3_NEED_MORE_DATA if buffer is too small to determine size,
 *         PT7_MP3_ERR_SYNC_NOT_FOUND if no ID3 tag is present,
 *         PT7_MP3_ERR_INVALID_HEADER if tag header is malformed.
 */
pt7_mp3_status_t pt7_mp3_detect_id3v2(
    const uint8_t* buffer,
    size_t buffer_size,
    size_t* out_tag_size
);

/**
 * @brief Retrieves VBR / CBR metadata (Xing, Info, VBRI, LAME gapless) if present in stream (Task 5).
 *
 * @param decoder Decoder instance.
 * @param out_vbr Pointer to VBR info struct to populate.
 * @return PT7_MP3_OK on success,
 *         PT7_MP3_ERR_SYNC_NOT_FOUND if stream does not contain VBR header,
 *         PT7_MP3_ERR_INVALID_ARG if arguments are invalid.
 */
pt7_mp3_status_t pt7_mp3_get_vbr_info(
    const pt7_mp3_decoder_t* decoder,
    pt7_mp3_vbr_info_t* out_vbr
);

/**
 * @brief Retrieves aggregated stream information (Phase C).
 *
 * Combines format info, bitrate mode, frame/sample counts, duration estimate,
 * and gapless metadata into a single stable C structure.
 *
 * @param decoder Decoder instance.
 * @param out_info Pointer to stream info structure to populate.
 * @return PT7_MP3_OK on success, PT7_MP3_ERR_INVALID_ARG if arguments are invalid.
 */
pt7_mp3_status_t pt7_mp3_get_info(
    const pt7_mp3_decoder_t* decoder,
    pt7_mp3_stream_info_t* out_info
);

/**
 * @brief Enables gapless trimming on subsequent read calls (Phase C).
 *
 * When enabled, the decoder skips `encoder_delay` samples at the start of
 * the stream and trims `end_padding` samples at the end, based on LAME
 * gapless metadata. If gapless metadata is not available, this is a no-op.
 * Calling this function multiple times does NOT apply trimming twice.
 *
 * @param decoder Decoder instance.
 * @return PT7_MP3_OK on success,
 *         PT7_MP3_ERR_INVALID_ARG if decoder is NULL,
 *         PT7_MP3_ERR_SYNC_NOT_FOUND if gapless metadata is not available.
 */
pt7_mp3_status_t pt7_mp3_enable_gapless(pt7_mp3_decoder_t* decoder);

/**
 * @brief Returns the number of samples that have been trimmed from the
 *        start of the stream by gapless playback (Phase C).
 *
 * @param decoder Decoder instance.
 * @return Number of skipped front samples (0 if gapless not enabled).
 */
uint64_t pt7_mp3_get_gapless_trimmed_front(const pt7_mp3_decoder_t* decoder);

/**
 * @brief Returns the number of samples that have been trimmed from the
 *        end of the stream by gapless playback (Phase C).
 *
 * @param decoder Decoder instance.
 * @return Number of trimmed end samples (0 if gapless not enabled).
 */
uint64_t pt7_mp3_get_gapless_trimmed_end(const pt7_mp3_decoder_t* decoder);

/**
 * @brief Sets a seekable data source for random access (Phase D).
 *
 * When a seekable source is set, the decoder can perform temporal seeking.
 * The data buffer must remain valid for the lifetime of the decoder or until
 * a new source is set. This switches the decoder from streaming mode to
 * file mode.
 *
 * @param decoder Decoder instance.
 * @param data Pointer to the complete MP3 file data.
 * @param data_size Size of the data in bytes.
 * @return PT7_MP3_OK on success, PT7_MP3_ERR_INVALID_ARG if arguments are invalid.
 */
pt7_mp3_status_t pt7_mp3_set_source(
    pt7_mp3_decoder_t* decoder,
    const uint8_t* data,
    size_t data_size
);

/**
 * @brief Seeks to a temporal position in the MP3 stream (Phase D).
 *
 * Requires a seekable source set via pt7_mp3_set_source(). The decoder state
 * is fully reset and repositioned. Bit reservoir dependencies are handled by
 * decoding warmup frames before the target. PCM output from subsequent
 * read calls will start at the requested position.
 *
 * If gapless trimming is enabled, the position is interpreted on the
 * gapless-adjusted timeline (i.e. position 0 = first usable sample).
 *
 * @param decoder Decoder instance.
 * @param position_seconds Target position in seconds (>= 0).
 * @param out_actual_seconds Optional: receives the actual position achieved.
 * @return PT7_MP3_OK on success,
 *         PT7_MP3_ERR_SEEK_NOT_SUPPORTED if no seekable source is set,
 *         PT7_MP3_ERR_SEEK_OUT_OF_RANGE if position is beyond stream end,
 *         PT7_MP3_ERR_INVALID_ARG if decoder is NULL or position is negative.
 */
pt7_mp3_status_t pt7_mp3_seek(
    pt7_mp3_decoder_t* decoder,
    double position_seconds,
    double* out_actual_seconds
);

/**
 * @brief Checks whether the decoder is currently seekable (Phase D).
 *
 * @param decoder Decoder instance.
 * @return 1 if seekable (source set), 0 otherwise.
 */
int pt7_mp3_is_seekable(const pt7_mp3_decoder_t* decoder);

/**
 * @brief Returns human-readable error description for a status code.
 * @param status Status code.
 * @return Static string description.
 */
const char* pt7_mp3_status_string(pt7_mp3_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* PT7_MP3_H */
