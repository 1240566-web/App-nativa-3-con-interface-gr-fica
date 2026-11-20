#include "PreviewAudio.h"

#include <windows.h>
#include <mmsystem.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

std::string ToUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(),
                        size, nullptr, nullptr);
    return result;
}

// atempo solo acepta 0.5 .. 2.0 por instancia, igual que en la exportacion.
std::vector<double> TempoSteps(double speed)
{
    std::vector<double> steps;
    double remaining = speed;

    while (remaining > 2.0) {
        steps.push_back(2.0);
        remaining /= 2.0;
    }
    while (remaining < 0.5) {
        steps.push_back(0.5);
        remaining /= 0.5;
    }
    steps.push_back(remaining);
    return steps;
}

constexpr int kMixRate = AudioMixer::SampleRate;
constexpr int kMixChannels = 2;

int ClampSample(int32_t value)
{
    if (value < -32768) {
        return -32768;
    }
    return value > 32767 ? 32767 : static_cast<int>(value);
}

// Decodifica el audio completo de un clip aplicando su recorte y su velocidad,
// y lo deja en PCM estereo 48 kHz 16 bits. Se hace una sola vez al abrir el
// mezclador; luego la mezcla de clips superpuestos es una simple suma.
bool RenderClipAudio(const ClipInfo& clip, std::vector<int16_t>& out)
{
    out.clear();
    if (!clip.hasAudio || clip.path.empty()) {
        return false;
    }

    const int64_t expected =
        static_cast<int64_t>(std::llround(clip.OutputDuration() * kMixRate));
    if (expected <= 0) {
        return false;
    }

    AVFormatContext* format = nullptr;
    const std::string path = ToUtf8(clip.path);
    if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) {
        return false;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        avformat_close_input(&format);
        return false;
    }

    const AVCodec* decoder = nullptr;
    const int audioStream =
        av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (audioStream < 0 || decoder == nullptr) {
        avformat_close_input(&format);
        return false;
    }

    AVCodecContext* codec = avcodec_alloc_context3(decoder);
    if (codec == nullptr) {
        avformat_close_input(&format);
        return false;
    }
    avcodec_parameters_to_context(codec, format->streams[audioStream]->codecpar);

    if (avcodec_open2(codec, decoder, nullptr) < 0) {
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        return false;
    }

    AVFilterGraph* graph = avfilter_graph_alloc();
    AVFilterContext* srcCtx = nullptr;
    AVFilterContext* sinkCtx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* sinkFrame = nullptr;
    AVChannelLayout inputLayout{};
    bool layoutSet = false;
    bool built = false;

    do {
        if (graph == nullptr) {
            break;
        }

        // En FFmpeg 9 el abuffer debe recibir sus parametros antes de inicializarse.
        srcCtx = avfilter_graph_alloc_filter(graph, avfilter_get_by_name("abuffer"), "src");
        if (srcCtx == nullptr) {
            break;
        }

        if (codec->ch_layout.nb_channels > 0 &&
            codec->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) {
            if (av_channel_layout_copy(&inputLayout, &codec->ch_layout) < 0) {
                break;
            }
        }
        else {
            av_channel_layout_default(
                &inputLayout, codec->ch_layout.nb_channels > 0 ? codec->ch_layout.nb_channels
                                                               : kMixChannels);
        }
        layoutSet = true;

        AVBufferSrcParameters* parameters = av_buffersrc_parameters_alloc();
        if (parameters == nullptr) {
            break;
        }
        parameters->format = codec->sample_fmt;
        parameters->sample_rate = codec->sample_rate;
        parameters->time_base = AVRational{ 1, codec->sample_rate };
        av_channel_layout_copy(&parameters->ch_layout, &inputLayout);

        const int configured = av_buffersrc_parameters_set(srcCtx, parameters);
        av_channel_layout_uninit(&parameters->ch_layout);
        av_free(parameters);
        if (configured < 0) {
            break;
        }
        if (avfilter_init_dict(srcCtx, nullptr) < 0) {
            break;
        }

        AVFilterContext* last = srcCtx;
        int index = 0;
        for (const double tempo : TempoSteps(clip.EffectiveSpeed())) {
            char name[32]{};
            char value[32]{};
            std::snprintf(name, sizeof(name), "tempo%d", index++);
            std::snprintf(value, sizeof(value), "%.6g", tempo);

            AVFilterContext* filter = nullptr;
            if (avfilter_graph_create_filter(&filter, avfilter_get_by_name("atempo"), name, value,
                                             nullptr, graph) < 0) {
                break;
            }
            if (avfilter_link(last, 0, filter, 0) < 0) {
                break;
            }
            last = filter;
        }

        AVFilterContext* formatFilter = nullptr;
        if (avfilter_graph_create_filter(
                &formatFilter, avfilter_get_by_name("aformat"), "aformat",
                "sample_fmts=s16:sample_rates=48000:channel_layouts=stereo", nullptr,
                graph) < 0) {
            break;
        }
        if (avfilter_link(last, 0, formatFilter, 0) < 0) {
            break;
        }

        if (avfilter_graph_create_filter(&sinkCtx, avfilter_get_by_name("abuffersink"), "sink",
                                         nullptr, nullptr, graph) < 0) {
            break;
        }
        if (avfilter_link(formatFilter, 0, sinkCtx, 0) < 0) {
            break;
        }
        if (avfilter_graph_config(graph, nullptr) < 0) {
            break;
        }

        built = true;
    } while (false);

    if (!built) {
        if (graph != nullptr) {
            avfilter_graph_free(&graph);
        }
        if (layoutSet) {
            av_channel_layout_uninit(&inputLayout);
        }
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        return false;
    }

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    sinkFrame = av_frame_alloc();
    if (packet == nullptr || frame == nullptr || sinkFrame == nullptr) {
        if (sinkFrame != nullptr) {
            av_frame_free(&sinkFrame);
        }
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
        if (packet != nullptr) {
            av_packet_free(&packet);
        }
        avfilter_graph_free(&graph);
        av_channel_layout_uninit(&inputLayout);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        return false;
    }

    double timeBase = av_q2d(format->streams[audioStream]->time_base);
    if (timeBase <= 0.0) {
        timeBase = 1.0 / (codec->sample_rate > 0 ? codec->sample_rate : kMixRate);
    }

    const double startSource = clip.TrimStart();
    const double sourceEnd = clip.TrimEnd();

    if (timeBase > 0.0) {
        const int64_t target = static_cast<int64_t>(std::llround(startSource / timeBase));
        int seeked = avformat_seek_file(format, audioStream, INT64_MIN, target, INT64_MAX,
                                        AVSEEK_FLAG_BACKWARD);
        if (seeked < 0) {
            seeked = av_seek_frame(format, audioStream, target, AVSEEK_FLAG_BACKWARD);
        }
        if (seeked >= 0) {
            avcodec_flush_buffers(codec);
        }
    }

    out.reserve(static_cast<size_t>(expected) * kMixChannels);

    bool discardDone = false;
    bool inputDone = false;
    bool flushSent = false;
    bool sourceClosed = false;
    int64_t nextInputPts = 0;

    const auto pull = [&]() -> int {
        const int pulled = av_buffersink_get_frame(sinkCtx, sinkFrame);
        if (pulled == 0) {
            if (sinkFrame->format == AV_SAMPLE_FMT_S16 &&
                sinkFrame->ch_layout.nb_channels == kMixChannels) {
                const int64_t have = static_cast<int64_t>(out.size() / kMixChannels);
                const int64_t remaining = expected - have;
                const int frames = static_cast<int>(
                    (std::min)(static_cast<int64_t>(sinkFrame->nb_samples), remaining));
                if (frames > 0) {
                    const int16_t* source = reinterpret_cast<const int16_t*>(sinkFrame->data[0]);
                    out.insert(out.end(), source,
                               source + static_cast<size_t>(frames) * kMixChannels);
                }
            }
            av_frame_unref(sinkFrame);
            return 1;
        }
        return pulled == AVERROR(EAGAIN) ? 0 : -1;
    };

    const auto feedOne = [&]() -> bool {
        while (!inputDone) {
            const int received = avcodec_receive_frame(codec, frame);
            if (received == 0) {
                int64_t stamp = frame->best_effort_timestamp;
                if (stamp == AV_NOPTS_VALUE) {
                    stamp = frame->pts;
                }
                const double pts =
                    (stamp == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE
                                              : static_cast<double>(stamp) * timeBase;
                const double frameDuration =
                    (codec->sample_rate > 0)
                        ? static_cast<double>(frame->nb_samples) / codec->sample_rate
                        : 0.0;

                if (!discardDone) {
                    if (pts != AV_NOPTS_VALUE && pts + frameDuration <= startSource) {
                        av_frame_unref(frame);
                        continue;
                    }
                    discardDone = true;
                }

                if (pts != AV_NOPTS_VALUE && pts >= sourceEnd - 1e-6) {
                    av_frame_unref(frame);
                    inputDone = true;
                    return false;
                }

                if (layoutSet &&
                    av_channel_layout_compare(&frame->ch_layout, &inputLayout) != 0) {
                    av_channel_layout_uninit(&frame->ch_layout);
                    av_channel_layout_copy(&frame->ch_layout, &inputLayout);
                }
                frame->sample_rate = codec->sample_rate;
                frame->pts = nextInputPts;
                frame->duration = frame->nb_samples;
                nextInputPts += frame->nb_samples;

                const int added = av_buffersrc_add_frame_flags(srcCtx, frame,
                                                               AV_BUFFERSRC_FLAG_KEEP_REF);
                av_frame_unref(frame);
                if (added < 0) {
                    inputDone = true;
                    return false;
                }
                return true;
            }

            if (received == AVERROR(EAGAIN)) {
                if (flushSent) {
                    inputDone = true;
                    return false;
                }

                const int read = av_read_frame(format, packet);
                if (read < 0) {
                    avcodec_send_packet(codec, nullptr);
                    flushSent = true;
                    continue;
                }
                if (packet->stream_index != audioStream) {
                    av_packet_unref(packet);
                    continue;
                }

                const int sent = avcodec_send_packet(codec, packet);
                av_packet_unref(packet);
                if (sent == AVERROR(EAGAIN)) {
                    continue;
                }
                if (sent < 0) {
                    inputDone = true;
                    return false;
                }
                continue;
            }

            inputDone = true;
            return false;
        }
        return false;
    };

    int idle = 0;
    long guard = 0;
    while (static_cast<int64_t>(out.size() / kMixChannels) < expected && guard++ < 2000000) {
        const int pulled = pull();
        if (pulled == 1) {
            idle = 0;
            continue;
        }
        if (pulled < 0) {
            break;
        }

        if (feedOne()) {
            idle = 0;
            continue;
        }

        if (!sourceClosed) {
            av_buffersrc_add_frame_flags(srcCtx, nullptr, 0);
            sourceClosed = true;
            idle = 0;
            continue;
        }

        if (++idle > 8) {
            break;
        }
    }

    av_frame_free(&sinkFrame);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avfilter_graph_free(&graph);
    av_channel_layout_uninit(&inputLayout);
    avcodec_free_context(&codec);
    avformat_close_input(&format);

    // Ajusta exactamente a la duracion de salida (recorta o rellena con silencio).
    out.resize(static_cast<size_t>(expected) * kMixChannels, 0);
    return true;
}

} // namespace

struct AudioMixer::Impl {
    struct Segment {
        double start = 0.0;
        double duration = 0.0;
        int64_t startFrame = 0;
        int clipIndex = -1;
        double volume = 1.0;
        std::vector<int16_t> samples;  // estereo entrelazado a 48 kHz
    };

    std::vector<Segment> segments;
    double duration = 0.0;
    int64_t totalFrames = 0;
    int64_t position = 0;
    bool anyAudio = false;

    void Close();
    bool Open(const std::vector<ClipInfo>& clips, std::wstring& error);
    bool Seek(double outputTime);
    int Read(int16_t* out, int frames);
    void SetClipVolume(int clipIndex, double volume);
};

void AudioMixer::Impl::Close()
{
    segments.clear();
    duration = 0.0;
    totalFrames = 0;
    position = 0;
    anyAudio = false;
}

bool AudioMixer::Impl::Open(const std::vector<ClipInfo>& clips, std::wstring&)
{
    Close();

    duration = TimelineDuration(clips);
    totalFrames =
        static_cast<int64_t>(std::llround(duration * static_cast<double>(kMixRate)));

    for (size_t i = 0; i < clips.size(); ++i) {
        const ClipInfo& clip = clips[i];
        const double length = clip.OutputDuration();
        if (length <= 0.0) {
            continue;
        }

        if (!clip.hasAudio) {
            continue;
        }

        Segment segment;
        segment.start = (std::max)(0.0, clip.start);
        segment.duration = length;
        segment.startFrame =
            static_cast<int64_t>(std::llround(segment.start * static_cast<double>(kMixRate)));
        segment.clipIndex = static_cast<int>(i);
        segment.volume = clip.EffectiveVolume();

        std::vector<int16_t> samples;
        if (!RenderClipAudio(clip, samples)) {
            continue;
        }

        segment.samples = std::move(samples);
        anyAudio = true;
        segments.push_back(std::move(segment));
    }

    position = 0;
    return true;
}

bool AudioMixer::Impl::Seek(double outputTime)
{
    if (outputTime < 0.0) {
        outputTime = 0.0;
    }

    position = static_cast<int64_t>(
        std::llround(outputTime * static_cast<double>(kMixRate)));
    position = (std::max)(static_cast<int64_t>(0), (std::min)(position, totalFrames));
    return position < totalFrames;
}

int AudioMixer::Impl::Read(int16_t* out, int frames)
{
    if (out == nullptr || frames <= 0) {
        return 0;
    }

    int done = 0;
    while (done < frames && position < totalFrames) {
        int32_t left = 0;
        int32_t right = 0;

        for (const Segment& segment : segments) {
            const int64_t index = position - segment.startFrame;
            const int64_t count =
                static_cast<int64_t>(segment.samples.size() / kMixChannels);
            if (index < 0 || index >= count) {
                continue;
            }
            const int32_t leftSample =
                segment.samples[static_cast<size_t>(index) * kMixChannels];
            const int32_t rightSample =
                segment.samples[static_cast<size_t>(index) * kMixChannels + 1];
            if (segment.volume == 1.0) {
                left += leftSample;
                right += rightSample;
            }
            else {
                left += static_cast<int32_t>(std::llround(leftSample * segment.volume));
                right += static_cast<int32_t>(std::llround(rightSample * segment.volume));
            }
        }

        out[static_cast<size_t>(done) * kMixChannels] = static_cast<int16_t>(ClampSample(left));
        out[static_cast<size_t>(done) * kMixChannels + 1] =
            static_cast<int16_t>(ClampSample(right));

        ++position;
        ++done;
    }

    return done;
}

void AudioMixer::Impl::SetClipVolume(int clipIndex, double volume)
{
    if (volume < 0.0) {
        volume = 0.0;
    }
    if (volume > 2.0) {
        volume = 2.0;
    }

    for (Segment& segment : segments) {
        if (segment.clipIndex == clipIndex) {
            segment.volume = volume;
        }
    }
}

// ----------------------------------------------------------------------------

struct PreviewAudio::Impl {
    static constexpr int kBufferFrames = 9600; // 0.2 s a 48 kHz
    static constexpr int kBufferCount = 3;

    struct Buffer {
        WAVEHDR header{};
        std::vector<uint8_t> data;
        bool prepared = false;
    };

    AudioMixer mixer;
    HWAVEOUT device = nullptr;
    std::vector<Buffer> buffers;
    bool playing = false;
    double baseTime = 0.0;
    ULONGLONG baseTick = 0;
    double queuedEnd = 0.0;

    ~Impl() { Close(); }

    bool EnsureDevice();
    void Queue(Buffer& buffer);
    void FillQueue();
    void StopDevice();
    void Close();
};

bool PreviewAudio::Impl::EnsureDevice()
{
    if (device != nullptr) {
        return true;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = kMixChannels;
    format.nSamplesPerSec = kMixRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    if (waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        device = nullptr;
        return false;
    }

    buffers.resize(kBufferCount);
    for (Buffer& buffer : buffers) {
        buffer.data.assign(static_cast<size_t>(kBufferFrames) * kMixChannels * sizeof(int16_t), 0);
        buffer.header = {};
        buffer.prepared = false;
    }
    return true;
}

void PreviewAudio::Impl::Queue(Buffer& buffer)
{
    if (device == nullptr || playing == false) {
        return;
    }
    if (queuedEnd >= mixer.Duration() - 1.0 / kMixRate) {
        return;
    }

    const int frames = mixer.Read(reinterpret_cast<int16_t*>(buffer.data.data()), kBufferFrames);
    if (frames <= 0) {
        return;
    }

    buffer.header = {};
    buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.data.data());
    buffer.header.dwBufferLength =
        static_cast<DWORD>(frames) * kMixChannels * sizeof(int16_t);

    if (waveOutPrepareHeader(device, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        return;
    }
    if (waveOutWrite(device, &buffer.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(device, &buffer.header, sizeof(WAVEHDR));
        return;
    }

    buffer.prepared = true;
    queuedEnd += static_cast<double>(frames) / kMixRate;
}

void PreviewAudio::Impl::FillQueue()
{
    if (device == nullptr || !playing) {
        return;
    }

    for (Buffer& buffer : buffers) {
        if (!buffer.prepared) {
            Queue(buffer);
            continue;
        }
        if ((buffer.header.dwFlags & WHDR_DONE) != 0) {
            waveOutUnprepareHeader(device, &buffer.header, sizeof(WAVEHDR));
            buffer.prepared = false;
            Queue(buffer);
        }
    }
}

void PreviewAudio::Impl::StopDevice()
{
    playing = false;
    if (device == nullptr) {
        return;
    }

    waveOutReset(device);
    for (Buffer& buffer : buffers) {
        if (buffer.prepared) {
            waveOutUnprepareHeader(device, &buffer.header, sizeof(WAVEHDR));
            buffer.prepared = false;
        }
    }
}

void PreviewAudio::Impl::Close()
{
    StopDevice();
    if (device != nullptr) {
        waveOutClose(device);
        device = nullptr;
    }
    buffers.clear();
    mixer.Close();
}

// ----------------------------------------------------------------------------

AudioMixer::AudioMixer() : m_impl(std::make_unique<Impl>()) {}
AudioMixer::~AudioMixer() = default;

bool AudioMixer::Open(const std::vector<ClipInfo>& clips, std::wstring& error)
{
    return m_impl->Open(clips, error);
}

void AudioMixer::Close()
{
    m_impl->Close();
}

bool AudioMixer::HasAudio() const
{
    return m_impl->anyAudio;
}

double AudioMixer::Duration() const
{
    return m_impl->duration;
}

bool AudioMixer::Seek(double outputTime)
{
    return m_impl->Seek(outputTime);
}

int AudioMixer::Read(int16_t* out, int frames)
{
    return m_impl->Read(out, frames);
}

void AudioMixer::SetClipVolume(int clipIndex, double volume)
{
    m_impl->SetClipVolume(clipIndex, volume);
}

// ----------------------------------------------------------------------------

PreviewAudio::PreviewAudio() : m_impl(std::make_unique<Impl>()) {}
PreviewAudio::~PreviewAudio() = default;

bool PreviewAudio::Open(const std::vector<ClipInfo>& clips, std::wstring& error)
{
    m_impl->StopDevice();
    return m_impl->mixer.Open(clips, error);
}

void PreviewAudio::Close()
{
    m_impl->Close();
}

bool PreviewAudio::HasAudio() const
{
    return m_impl->mixer.HasAudio();
}

bool PreviewAudio::IsPlaying() const
{
    return m_impl->playing;
}

void PreviewAudio::Play(double outputTime)
{
    if (!m_impl->mixer.HasAudio() || !m_impl->EnsureDevice()) {
        return;
    }

    m_impl->StopDevice();
    if (!m_impl->mixer.Seek(outputTime)) {
        return;
    }

    m_impl->queuedEnd = outputTime;
    m_impl->baseTime = outputTime;
    m_impl->playing = true;
    m_impl->FillQueue();
    m_impl->baseTick = GetTickCount64();
}

void PreviewAudio::Pause()
{
    m_impl->StopDevice();
}

void PreviewAudio::Sync(double outputTime)
{
    if (!m_impl->playing) {
        return;
    }

    const double elapsed = static_cast<double>(GetTickCount64() - m_impl->baseTick) / 1000.0;
    const double clock = m_impl->baseTime + elapsed;
    if (std::fabs(outputTime - clock) > 0.35) {
        Play(outputTime);
    }
}

void PreviewAudio::Pump()
{
    m_impl->FillQueue();
}

void PreviewAudio::SetClipVolume(int clipIndex, double volume)
{
    m_impl->mixer.SetClipVolume(clipIndex, volume);
}
