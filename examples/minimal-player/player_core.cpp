#include "player_core.h"
#include <fstream>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <cstdarg>

namespace pt7::player {

namespace {
// Toggle for diagnostic seek logging
constexpr bool SEEK_LOG = true;

void seek_log(const char* fmt, ...) {
    if (!SEEK_LOG) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}
} // anonymous namespace

AudioPlayerCore::AudioPlayerCore()
    : m_decoder(pt7_mp3_create())
{
    m_thread = std::thread(&AudioPlayerCore::playback_loop, this);
}

AudioPlayerCore::~AudioPlayerCore()
{
    m_running = false;
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_sink.close();

    if (m_decoder) {
        pt7_mp3_destroy(m_decoder);
        m_decoder = nullptr;
    }
}

std::wstring AudioPlayerCore::get_file_name() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file_path.empty()) return L"";
    size_t pos = m_file_path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return m_file_path;
    }
    return m_file_path.substr(pos + 1);
}

const char* AudioPlayerCore::get_state_string() const
{
    PlayerState s = get_state();
    switch (s) {
        case PlayerState::NoFile:  return "No file";
        case PlayerState::Loaded:  return "Loaded";
        case PlayerState::Playing: return "Playing";
        case PlayerState::Paused:  return "Paused";
        case PlayerState::Stopped: return "Stopped";
    }
    return "Unknown";
}

PlayerState AudioPlayerCore::get_state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

void AudioPlayerCore::reset_stream_position()
{
    m_file_offset = 0;
    m_pcm_backlog.clear();
    m_pcm_backlog_offset = 0;
    m_position_seconds.store(0.0);
    m_seek_base_seconds.store(0.0);
    m_total_frames_written.store(0);
    m_seek_pending.store(false);
    m_seek_generation.store(0);
    m_seek_applied_gen.store(0);
    if (m_decoder) {
        pt7_mp3_reset(m_decoder);
    }
}

bool AudioPlayerCore::load_file(const std::wstring& file_path, std::string* error_msg)
{
    // Stop any current playback
    stop();

    std::lock_guard<std::mutex> lock(m_mutex);

    std::ifstream file(file_path.c_str(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        if (error_msg) *error_msg = "Could not open file.";
        m_state = PlayerState::NoFile;
        m_file_path.clear();
        m_file_data.clear();
        return false;
    }

    const std::streamsize size = file.tellg();
    if (size < 4) {
        if (error_msg) *error_msg = "File is too small to be a valid MP3 (< 4 bytes).";
        m_state = PlayerState::NoFile;
        m_file_path.clear();
        m_file_data.clear();
        return false;
    }

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        if (error_msg) *error_msg = "Failed to read file data.";
        m_state = PlayerState::NoFile;
        m_file_path.clear();
        m_file_data.clear();
        return false;
    }

    // Verify stream & inspect header
    size_t frame_offset = 0;
    pt7_mp3_frame_info_t frame_info{};
    pt7_mp3_status_t st = pt7_mp3_scan_frame(data.data(), data.size(), &frame_offset, &frame_info);
    if (st != PT7_MP3_OK) {
        if (error_msg) {
            *error_msg = std::string("No valid MP3 frame found: ") + pt7_mp3_status_string(st);
        }
        m_state = PlayerState::NoFile;
        m_file_path.clear();
        m_file_data.clear();
        return false;
    }

    // Initialize audio output
    std::string sink_err;
    if (!m_sink.open(frame_info.sample_rate_hz, frame_info.channels, &sink_err)) {
        if (error_msg) {
            *error_msg = "Failed to open WASAPI audio device: " + sink_err;
        }
        m_state = PlayerState::NoFile;
        m_file_path.clear();
        m_file_data.clear();
        return false;
    }

    // Store stream info
    m_stream_info.sample_rate = frame_info.sample_rate_hz;
    m_stream_info.channels = frame_info.channels;
    m_stream_info.bitrate_kbps = frame_info.bitrate_kbps;
    switch (frame_info.version) {
        case PT7_MP3_VERSION_MPEG1: m_stream_info.mpeg_version = "MPEG-1"; break;
        case PT7_MP3_VERSION_MPEG2: m_stream_info.mpeg_version = "MPEG-2"; break;
        case PT7_MP3_VERSION_MPEG2_5: m_stream_info.mpeg_version = "MPEG-2.5"; break;
        default: m_stream_info.mpeg_version = "MPEG"; break;
    }

    m_file_path = file_path;
    m_file_data = std::move(data);
    reset_stream_position();

    // Set seekable source for random access (Phase D)
    pt7_mp3_status_t src_st = pt7_mp3_set_source(m_decoder,
        m_file_data.data(), m_file_data.size());
    m_seekable = (src_st == PT7_MP3_OK);

    // Get duration from stream info
    pt7_mp3_stream_info_t stream_info{};
    if (pt7_mp3_get_info(m_decoder, &stream_info) == PT7_MP3_OK) {
        m_stream_info.duration_sec = stream_info.duration_seconds;
        m_stream_info.is_vbr = (stream_info.bitrate_mode == PT7_MP3_BITRATE_VBR);
    }

    // Enable gapless if available
    if (pt7_mp3_enable_gapless(m_decoder) == PT7_MP3_OK) {
        m_gapless_enabled = true;
    }

    m_state = PlayerState::Loaded;
    return true;
}

bool AudioPlayerCore::play()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_state == PlayerState::NoFile) {
        return false;
    }

    if (m_state == PlayerState::Loaded || m_state == PlayerState::Stopped) {
        reset_stream_position();
        if (!m_sink.start()) return false;
        m_state = PlayerState::Playing;
        m_cv.notify_all();
        return true;
    }

    if (m_state == PlayerState::Paused) {
        if (!m_sink.start()) return false;
        m_state = PlayerState::Playing;
        m_cv.notify_all();
        return true;
    }

    return true; // Already playing
}

bool AudioPlayerCore::pause()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_state == PlayerState::Playing) {
        m_sink.pause();
        m_state = PlayerState::Paused;
        return true;
    }

    return false;
}

bool AudioPlayerCore::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_state == PlayerState::Playing || m_state == PlayerState::Paused) {
        m_sink.stop();
        reset_stream_position();
        m_state = PlayerState::Stopped;
        return true;
    }

    if (m_state == PlayerState::Loaded || m_state == PlayerState::Stopped) {
        reset_stream_position();
        m_state = PlayerState::Stopped;
        return true;
    }

    return false;
}

bool AudioPlayerCore::seek(double position_seconds)
{
    if (position_seconds < 0.0) return false;
    if (!m_seekable) return false;

    // Latest-seek-wins: increment generation and set target.
    // The playback loop checks m_seek_generation vs m_seek_applied_gen.
    const uint32_t gen = m_seek_generation.fetch_add(1) + 1;
    m_seek_target.store(position_seconds);
    m_seek_pending.store(true);
    m_cv.notify_all();

    seek_log("SEEK request: %.2fs (gen %u)\n", position_seconds, gen);
    return true;
}

double AudioPlayerCore::get_position_seconds() const
{
    // Calculate actual playback position:
    //   actual = seek_base + (total_written - buffered) / sample_rate
    // where `buffered` is the WASAPI padding (frames written but not yet played).
    const double base = m_seek_base_seconds.load();
    const uint64_t written = m_total_frames_written.load();
    const size_t buffered = m_sink.get_buffered_frames();
    const uint32_t sr = m_stream_info.sample_rate;

    if (sr == 0) return base;

    const int64_t played = static_cast<int64_t>(written) - static_cast<int64_t>(buffered);
    const double pos = base + static_cast<double>(played > 0 ? played : 0) / sr;

    // Clamp to duration
    const double dur = m_stream_info.duration_sec;
    if (dur > 0.0 && pos > dur) return dur;
    if (pos < 0.0) return 0.0;
    return pos;
}

double AudioPlayerCore::get_duration_seconds() const
{
    return m_stream_info.duration_sec;
}

void AudioPlayerCore::apply_seek(double target_seconds)
{
    // Flush old PCM from audio buffer
    m_sink.flush();

    // Discard any pending PCM backlog
    m_pcm_backlog.clear();
    m_pcm_backlog_offset = 0;

    // Perform the seek
    double actual = 0.0;
    pt7_mp3_status_t st = pt7_mp3_seek(m_decoder, target_seconds, &actual);
    if (st != PT7_MP3_OK) {
        seek_log("SEEK failed: %s (gen %u)\n", pt7_mp3_status_string(st),
                 m_seek_applied_gen.load() + 1);
        return;
    }

    // Update position
    m_position_seconds.store(actual);
    m_seek_base_seconds.store(actual);
    m_total_frames_written.store(0);
    m_file_offset = m_file_data.size(); // data already in decoder via set_source

    seek_log("SEEK applied: %.2fs (gen %u)\n", actual, m_seek_applied_gen.load() + 1);
}

void AudioPlayerCore::playback_loop()
{
    constexpr size_t CHUNK_SIZE = 4096;
    constexpr size_t PCM_READ_SAMPLES = 2304;
    int16_t pcm_buf[PCM_READ_SAMPLES];

    while (m_running) {
        std::unique_lock<std::mutex> lock(m_mutex);

        // Check for pending seek (applies in playback thread)
        if (m_seek_pending.load()) {
            const uint32_t pending_gen = m_seek_generation.load();
            // Only apply if not already applied
            if (pending_gen != m_seek_applied_gen.load()) {
                const double target = m_seek_target.load();
                m_seek_applied_gen.store(pending_gen);
                m_seek_pending.store(false);
                apply_seek(target);
            }
        }

        if (m_state != PlayerState::Playing) {
            m_cv.wait_for(lock, std::chrono::milliseconds(50), [this] {
                return !m_running || m_state == PlayerState::Playing ||
                       m_seek_pending.load();
            });
            continue;
        }

        const uint32_t channels = m_stream_info.channels;
        if (channels == 0) {
            m_state = PlayerState::Stopped;
            continue;
        }

        // 1. Drain backlog if any
        if (m_pcm_backlog_offset < m_pcm_backlog.size()) {
            size_t remaining_samples = m_pcm_backlog.size() - m_pcm_backlog_offset;
            size_t remaining_frames = remaining_samples / channels;
            const int16_t* pcm_ptr = m_pcm_backlog.data() + m_pcm_backlog_offset;

            size_t written_frames = m_sink.write_frames(pcm_ptr, remaining_frames);
            m_pcm_backlog_offset += written_frames * channels;

            // Track total frames written for position calculation
            m_total_frames_written.fetch_add(written_frames);

            if (m_pcm_backlog_offset >= m_pcm_backlog.size()) {
                m_pcm_backlog.clear();
                m_pcm_backlog_offset = 0;
            } else {
                // Buffer is full; yield CPU briefly
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
        }

        // 2. Feed chunk if more file data exists
        if (m_file_offset < m_file_data.size()) {
            size_t to_feed = std::min(CHUNK_SIZE, m_file_data.size() - m_file_offset);
            pt7_mp3_feed(m_decoder, m_file_data.data() + m_file_offset, to_feed);
            m_file_offset += to_feed;
        }

        // 3. Read ALL available decoded PCM samples from decoder
        while (m_running && m_state == PlayerState::Playing) {
            // Check for pending seek — abort current decode loop
            if (m_seek_pending.load()) {
                const uint32_t pending_gen = m_seek_generation.load();
                if (pending_gen != m_seek_applied_gen.load()) {
                    const double target = m_seek_target.load();
                    m_seek_applied_gen.store(pending_gen);
                    m_seek_pending.store(false);
                    apply_seek(target);
                }
                break; // restart outer loop after seek
            }

            size_t samples_read = 0;
            pt7_mp3_status_t read_st = pt7_mp3_read_pcm16(m_decoder, pcm_buf, PCM_READ_SAMPLES, &samples_read);

            if (samples_read > 0) {
                size_t total_frames = samples_read / channels;
                size_t written_frames = m_sink.write_frames(pcm_buf, total_frames);

                // Track total frames written for position calculation
                m_total_frames_written.fetch_add(written_frames);

                if (written_frames < total_frames) {
                    // Push unwritten samples to backlog
                    size_t unwritten_offset = written_frames * channels;
                    size_t unwritten_count = samples_read - unwritten_offset;
                    m_pcm_backlog.assign(pcm_buf + unwritten_offset, pcm_buf + unwritten_offset + unwritten_count);
                    m_pcm_backlog_offset = 0;
                    break; // Backlog full, yield to audio device
                }
            }

            if (read_st != PT7_MP3_OK || samples_read == 0) {
                break;
            }
        }

        if (m_file_offset >= m_file_data.size() && m_pcm_backlog.empty()) {
            // End of file reached and no more decoded samples ready
            pt7_mp3_flush(m_decoder);
            while (m_running && m_state == PlayerState::Playing) {
                size_t flush_read = 0;
                pt7_mp3_read_pcm16(m_decoder, pcm_buf, PCM_READ_SAMPLES, &flush_read);
                if (flush_read == 0) break;
                size_t flush_frames = flush_read / channels;
                size_t written_frames = m_sink.write_frames(pcm_buf, flush_frames);
                m_total_frames_written.fetch_add(written_frames);
                if (written_frames < flush_frames) {
                    size_t unwritten_offset = written_frames * channels;
                    size_t unwritten_count = flush_read - unwritten_offset;
                    m_pcm_backlog.assign(pcm_buf + unwritten_offset, pcm_buf + unwritten_offset + unwritten_count);
                    m_pcm_backlog_offset = 0;
                    break;
                }
            }

            if (m_pcm_backlog.empty()) {
                // Playback finished completely! Wait for hardware buffer to drain
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                lock.lock();
                if (m_state == PlayerState::Playing) {
                    // Set position to duration (100% on seek bar)
                    m_position_seconds.store(m_stream_info.duration_sec);
                    m_sink.stop();
                    reset_stream_position();
                    m_state = PlayerState::Stopped;
                }
                continue;
            }
        }

        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace pt7::player
