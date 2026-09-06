#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace pt7::player {

class WasapiSink {
public:
    WasapiSink();
    ~WasapiSink();

    WasapiSink(const WasapiSink&) = delete;
    WasapiSink& operator=(const WasapiSink&) = delete;

    // Initializes WASAPI default render endpoint for given sample rate & channels (PCM16)
    bool open(uint32_t sample_rate, uint32_t channels, std::string* error_msg = nullptr);

    // Starts playback
    bool start();

    // Pauses playback
    bool pause();

    // Stops playback and resets buffer
    bool stop();

    // Flushes the audio buffer without stopping playback.
    // Discards all pending PCM samples. Used after seek to avoid
    // playing audio from the old position.
    bool flush();

    // Writes interleaved PCM16 samples (samples_count = frames * channels)
    // Returns number of frames actually written
    size_t write_frames(const int16_t* pcm_data, size_t frame_count);

    // Get available buffer space in frames
    size_t get_available_frames() const;

    // Get the number of frames currently buffered (not yet played).
    // This is the WASAPI padding — frames written but still waiting
    // in the hardware buffer.
    size_t get_buffered_frames() const;

    // Checks if sink is initialized
    bool is_open() const { return m_initialized; }

    // Close and release COM interfaces
    void close();

private:
    bool m_initialized = false;
    uint32_t m_src_rate = 0;
    uint32_t m_src_channels = 0;
    uint32_t m_buffer_frame_count = 0;

    // If fallback conversion is needed
    bool m_needs_conversion = false;
    uint32_t m_dst_rate = 0;
    uint32_t m_dst_channels = 0;
    bool m_dst_is_float = false;
    double m_resample_ratio = 1.0;
    double m_resample_phase = 0.0;

    // Pointer to implementation details (to avoid Windows.h in public header if desired,
    // or internal struct)
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace pt7::player
