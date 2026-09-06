#pragma once

#include "pt7_mp3.h"
#include "wasapi_sink.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace pt7::player {

enum class PlayerState {
    NoFile,
    Loaded,
    Playing,
    Paused,
    Stopped
};

struct StreamInfo {
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    uint32_t bitrate_kbps = 0;
    std::string mpeg_version;
    bool is_vbr = false;
    double duration_sec = 0.0;
};

class AudioPlayerCore {
public:
    AudioPlayerCore();
    ~AudioPlayerCore();

    AudioPlayerCore(const AudioPlayerCore&) = delete;
    AudioPlayerCore& operator=(const AudioPlayerCore&) = delete;

    // Load an MP3 file by path
    bool load_file(const std::wstring& file_path, std::string* error_msg = nullptr);

    // Playback control
    bool play();
    bool pause();
    bool stop();

    // Seek to a position in seconds (async — applied in playback thread).
    // Latest seek wins. Safe to call from UI thread.
    bool seek(double position_seconds);

    // Get current playback position in seconds (gapless-adjusted timeline).
    // Safe to call from any thread.
    double get_position_seconds() const;

    // Get total duration in seconds.
    double get_duration_seconds() const;

    // State & Info queries
    PlayerState get_state() const;
    const std::wstring& get_file_path() const { return m_file_path; }
    std::wstring get_file_name() const;
    const StreamInfo& get_stream_info() const { return m_stream_info; }
    const char* get_state_string() const;

private:
    void playback_loop();
    void reset_stream_position();
    void apply_seek(double target_seconds);

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    std::atomic<bool> m_running{true};

    PlayerState m_state = PlayerState::NoFile;
    std::wstring m_file_path;
    std::vector<uint8_t> m_file_data;
    size_t m_file_offset = 0;

    StreamInfo m_stream_info;
    pt7_mp3_decoder_t* m_decoder = nullptr;
    WasapiSink m_sink;

    // Local buffer for unconsumed decoded PCM samples between write calls
    std::vector<int16_t> m_pcm_backlog;
    size_t m_pcm_backlog_offset = 0;

    // Seek support (Phase D player integration)
    std::atomic<bool> m_seek_pending{false};
    std::atomic<double> m_seek_target{0.0};
    std::atomic<uint32_t> m_seek_generation{0};       // latest-wins counter
    std::atomic<uint32_t> m_seek_applied_gen{0};       // last applied generation
    std::atomic<double> m_position_seconds{0.0};       // current playback position (actual)
    std::atomic<double> m_seek_base_seconds{0.0};       // position at seek time
    std::atomic<uint64_t> m_total_frames_written{0};    // frames written to sink since seek
    bool m_seekable = false;
    bool m_gapless_enabled = false;
};

} // namespace pt7::player
