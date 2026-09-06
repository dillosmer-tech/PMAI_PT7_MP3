#include "wasapi_sink.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

namespace pt7::player {

struct WasapiSink::Impl {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render_client = nullptr;
    bool com_initialized = false;
    bool is_playing = false;

    std::vector<float> float_buffer;
    std::vector<int16_t> int16_buffer;

    void cleanup() {
        if (client && is_playing) {
            client->Stop();
            is_playing = false;
        }
        if (render_client) {
            render_client->Release();
            render_client = nullptr;
        }
        if (client) {
            client->Release();
            client = nullptr;
        }
        if (device) {
            device->Release();
            device = nullptr;
        }
        if (enumerator) {
            enumerator->Release();
            enumerator = nullptr;
        }
        if (com_initialized) {
            CoUninitialize();
            com_initialized = false;
        }
    }
};

WasapiSink::WasapiSink()
    : m_impl(new Impl())
{
}

WasapiSink::~WasapiSink()
{
    close();
    delete m_impl;
}

void WasapiSink::close()
{
    if (m_impl) {
        m_impl->cleanup();
    }
    m_initialized = false;
    m_src_rate = 0;
    m_src_channels = 0;
    m_buffer_frame_count = 0;
    m_needs_conversion = false;
}

bool WasapiSink::open(uint32_t sample_rate, uint32_t channels, std::string* error_msg)
{
    close();

    if (sample_rate == 0 || channels == 0 || channels > 2) {
        if (error_msg) *error_msg = "Invalid audio parameters (rate=" + std::to_string(sample_rate) + ", channels=" + std::to_string(channels) + ")";
        return false;
    }

    m_src_rate = sample_rate;
    m_src_channels = channels;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        m_impl->com_initialized = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
        // Already initialized on this thread in different mode; acceptable
        m_impl->com_initialized = false;
    } else {
        if (error_msg) *error_msg = "CoInitializeEx failed (0x" + std::to_string(hr) + ")";
        return false;
    }

    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&m_impl->enumerator)
    );
    if (FAILED(hr) || !m_impl->enumerator) {
        if (error_msg) *error_msg = "Cannot create MMDeviceEnumerator. Audio service might be stopped.";
        close();
        return false;
    }

    hr = m_impl->enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_impl->device);
    if (FAILED(hr) || !m_impl->device) {
        if (error_msg) *error_msg = "No default audio output endpoint found. Please check audio device connection.";
        close();
        return false;
    }

    hr = m_impl->device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(&m_impl->client)
    );
    if (FAILED(hr) || !m_impl->client) {
        if (error_msg) *error_msg = "Failed to activate IAudioClient interface.";
        close();
        return false;
    }

    // Try Method 1: direct initialization with AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = static_cast<WORD>(channels);
    wfx.nSamplesPerSec = sample_rate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = static_cast<WORD>(channels * 2);
    wfx.nAvgBytesPerSec = sample_rate * wfx.nBlockAlign;
    wfx.cbSize = 0;

    const REFERENCE_TIME buffer_duration = 2000000; // 200 ms buffer
    DWORD stream_flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    hr = m_impl->client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        stream_flags,
        buffer_duration,
        0,
        &wfx,
        nullptr
    );

    if (SUCCEEDED(hr)) {
        m_needs_conversion = false;
    } else {
        // Method 2: Fallback to device MixFormat
        m_impl->client->Release();
        m_impl->client = nullptr;

        hr = m_impl->device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(&m_impl->client)
        );
        if (FAILED(hr) || !m_impl->client) {
            if (error_msg) *error_msg = "Failed to reactivate IAudioClient.";
            close();
            return false;
        }

        WAVEFORMATEX* mix_format = nullptr;
        hr = m_impl->client->GetMixFormat(&mix_format);
        if (FAILED(hr) || !mix_format) {
            if (error_msg) *error_msg = "Failed to get device mix format.";
            close();
            return false;
        }

        hr = m_impl->client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            0,
            buffer_duration,
            0,
            mix_format,
            nullptr
        );

        if (FAILED(hr)) {
            CoTaskMemFree(mix_format);
            if (error_msg) *error_msg = "Failed to initialize IAudioClient with mix format.";
            close();
            return false;
        }

        m_needs_conversion = true;
        m_dst_rate = mix_format->nSamplesPerSec;
        m_dst_channels = mix_format->nChannels;
        m_dst_is_float = (mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                         (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                          reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix_format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        m_resample_ratio = static_cast<double>(m_dst_rate) / static_cast<double>(m_src_rate);
        m_resample_phase = 0.0;

        CoTaskMemFree(mix_format);
    }

    hr = m_impl->client->GetBufferSize(&m_buffer_frame_count);
    if (FAILED(hr)) {
        if (error_msg) *error_msg = "Failed to get buffer frame count.";
        close();
        return false;
    }

    hr = m_impl->client->GetService(
        __uuidof(IAudioRenderClient),
        reinterpret_cast<void**>(&m_impl->render_client)
    );
    if (FAILED(hr) || !m_impl->render_client) {
        if (error_msg) *error_msg = "Failed to get IAudioRenderClient service.";
        close();
        return false;
    }

    m_initialized = true;
    return true;
}

bool WasapiSink::start()
{
    if (!m_initialized || !m_impl->client) return false;
    if (!m_impl->is_playing) {
        HRESULT hr = m_impl->client->Start();
        if (SUCCEEDED(hr)) {
            m_impl->is_playing = true;
            return true;
        }
        return false;
    }
    return true;
}

bool WasapiSink::pause()
{
    if (!m_initialized || !m_impl->client) return false;
    if (m_impl->is_playing) {
        HRESULT hr = m_impl->client->Stop();
        if (SUCCEEDED(hr)) {
            m_impl->is_playing = false;
            return true;
        }
        return false;
    }
    return true;
}

bool WasapiSink::stop()
{
    if (!m_initialized || !m_impl->client) return false;
    if (m_impl->is_playing) {
        m_impl->client->Stop();
        m_impl->is_playing = false;
    }
    m_impl->client->Reset();
    m_resample_phase = 0.0;
    return true;
}

bool WasapiSink::flush()
{
    if (!m_initialized || !m_impl->client) return false;
    // Reset discards all pending data in the buffer
    m_impl->client->Reset();
    m_resample_phase = 0.0;
    return true;
}

size_t WasapiSink::get_available_frames() const
{
    if (!m_initialized || !m_impl->client) return 0;
    UINT32 padding = 0;
    HRESULT hr = m_impl->client->GetCurrentPadding(&padding);
    if (FAILED(hr)) return 0;
    if (padding >= m_buffer_frame_count) return 0;

    size_t avail_hw = m_buffer_frame_count - padding;
    if (m_needs_conversion && m_resample_ratio > 0.0) {
        return static_cast<size_t>(static_cast<double>(avail_hw) / m_resample_ratio);
    }
    return avail_hw;
}

size_t WasapiSink::get_buffered_frames() const
{
    if (!m_initialized || !m_impl->client) return 0;
    UINT32 padding = 0;
    HRESULT hr = m_impl->client->GetCurrentPadding(&padding);
    if (FAILED(hr)) return 0;
    if (m_needs_conversion && m_resample_ratio > 0.0) {
        return static_cast<size_t>(static_cast<double>(padding) / m_resample_ratio);
    }
    return padding;
}

size_t WasapiSink::write_frames(const int16_t* pcm_data, size_t frame_count)
{
    if (!m_initialized || !m_impl->render_client || !pcm_data || frame_count == 0) {
        return 0;
    }

    UINT32 padding = 0;
    HRESULT hr = m_impl->client->GetCurrentPadding(&padding);
    if (FAILED(hr) || padding >= m_buffer_frame_count) return 0;

    UINT32 hw_avail = m_buffer_frame_count - padding;

    if (!m_needs_conversion) {
        UINT32 frames_to_write = std::min(hw_avail, static_cast<UINT32>(frame_count));
        if (frames_to_write == 0) return 0;

        BYTE* pData = nullptr;
        hr = m_impl->render_client->GetBuffer(frames_to_write, &pData);
        if (FAILED(hr) || !pData) return 0;

        const size_t bytes = frames_to_write * m_src_channels * sizeof(int16_t);
        std::memcpy(pData, pcm_data, bytes);

        m_impl->render_client->ReleaseBuffer(frames_to_write, 0);
        return frames_to_write;
    } else {
        // Fallback software conversion to match device mix format
        UINT32 dst_frames_avail = hw_avail;
        if (dst_frames_avail == 0) return 0;

        // How many source frames can we convert into at most dst_frames_avail?
        double max_src = static_cast<double>(dst_frames_avail) / m_resample_ratio;
        size_t src_frames_to_read = std::min(frame_count, static_cast<size_t>(max_src));
        if (src_frames_to_read == 0) return 0;

        UINT32 dst_frames = static_cast<UINT32>(std::round(src_frames_to_read * m_resample_ratio));
        if (dst_frames > dst_frames_avail) dst_frames = dst_frames_avail;
        if (dst_frames == 0) return 0;

        BYTE* pData = nullptr;
        hr = m_impl->render_client->GetBuffer(dst_frames, &pData);
        if (FAILED(hr) || !pData) return 0;

        float* out_f = m_dst_is_float ? reinterpret_cast<float*>(pData) : nullptr;
        int16_t* out_i = (!m_dst_is_float) ? reinterpret_cast<int16_t*>(pData) : nullptr;

        for (UINT32 i = 0; i < dst_frames; ++i) {
            double src_idx = static_cast<double>(i) / m_resample_ratio;
            size_t idx0 = static_cast<size_t>(src_idx);
            size_t idx1 = std::min(idx0 + 1, src_frames_to_read - 1);
            float frac = static_cast<float>(src_idx - static_cast<double>(idx0));

            float s_l0 = static_cast<float>(pcm_data[idx0 * m_src_channels]) / 32768.0f;
            float s_r0 = (m_src_channels > 1) ? static_cast<float>(pcm_data[idx0 * m_src_channels + 1]) / 32768.0f : s_l0;
            float s_l1 = static_cast<float>(pcm_data[idx1 * m_src_channels]) / 32768.0f;
            float s_r1 = (m_src_channels > 1) ? static_cast<float>(pcm_data[idx1 * m_src_channels + 1]) / 32768.0f : s_l1;

            float out_l = s_l0 + frac * (s_l1 - s_l0);
            float out_r = s_r0 + frac * (s_r1 - s_r0);

            if (m_dst_is_float) {
                if (m_dst_channels == 1) {
                    out_f[i] = 0.5f * (out_l + out_r);
                } else {
                    out_f[i * 2 + 0] = out_l;
                    out_f[i * 2 + 1] = out_r;
                }
            } else {
                auto clamp16 = [](float v) -> int16_t {
                    float val = v * 32767.0f;
                    if (val > 32767.0f) return 32767;
                    if (val < -32768.0f) return -32768;
                    return static_cast<int16_t>(std::lrint(val));
                };
                if (m_dst_channels == 1) {
                    out_i[i] = clamp16(0.5f * (out_l + out_r));
                } else {
                    out_i[i * 2 + 0] = clamp16(out_l);
                    out_i[i * 2 + 1] = clamp16(out_r);
                }
            }
        }

        m_impl->render_client->ReleaseBuffer(dst_frames, 0);
        return src_frames_to_read;
    }
}

} // namespace pt7::player
