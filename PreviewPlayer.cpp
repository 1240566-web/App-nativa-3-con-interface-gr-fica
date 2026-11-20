#include "PreviewPlayer.h"

#include <windows.h>

#include "TextRenderer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kPreviewWidth = 1280;
constexpr int kPreviewHeight = 720;
constexpr double kOutputFrame = 1.0 / 30.0;

std::string ToUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

int Even(int value)
{
    if (value < 2) {
        return 2;
    }
    return (value / 2) * 2;
}

void ApplyNegate(uint8_t* data, int width, int height, int stride)
{
    for (int y = 0; y < height; ++y) {
        uint8_t* row = data + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            row[x * 4] = static_cast<uint8_t>(255 - row[x * 4]);
            row[x * 4 + 1] = static_cast<uint8_t>(255 - row[x * 4 + 1]);
            row[x * 4 + 2] = static_cast<uint8_t>(255 - row[x * 4 + 2]);
        }
    }
}

void ApplyFlipH(uint8_t* data, int width, int height, int stride)
{
    for (int y = 0; y < height; ++y) {
        uint8_t* row = data + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width / 2; ++x) {
            uint8_t* left = row + static_cast<size_t>(x) * 4;
            uint8_t* right = row + static_cast<size_t>(width - 1 - x) * 4;
            for (int c = 0; c < 4; ++c) {
                std::swap(left[c], right[c]);
            }
        }
    }
}

void ApplyFlipV(uint8_t* data, int width, int height, int stride)
{
    std::vector<uint8_t> temp(static_cast<size_t>(stride));
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = data + static_cast<size_t>(y) * stride;
        uint8_t* bottom = data + static_cast<size_t>(height - 1 - y) * stride;
        memcpy(temp.data(), top, static_cast<size_t>(stride));
        memcpy(top, bottom, static_cast<size_t>(stride));
        memcpy(bottom, temp.data(), static_cast<size_t>(stride));
    }
}

} // namespace

struct PreviewPlayer::Impl {
    struct Segment {
        std::wstring path;
        double in = 0.0;
        double out = 0.0;
        double start = 0.0;
        double duration = 0.0;
        double speed = 1.0;
        int track = 0;
        int clipIndex = -1;
        double scaleX = 1.0;
        double scaleY = 1.0;
        double posX = 0.0;
        double posY = 0.0;
        bool negate = false;
        bool flipH = false;
        bool flipV = false;
    };

    struct TrackState {
        AVFormatContext* format = nullptr;
        AVCodecContext* codec = nullptr;
        AVPacket* packet = nullptr;
        AVFrame* pending = nullptr;
        AVFrame* display = nullptr;
        SwsContext* sws = nullptr;
        int videoStream = -1;
        double timeBase = 0.0;
        double sourceFps = 30.0;
        int openSegment = -1;

        bool hasPendingPacket = false;
        bool flushSent = false;
        bool hasPending = false;
        double pendingPts = 0.0;
        bool hasDisplay = false;
        double displayPts = 0.0;
        double lastPts = -1.0;

        std::vector<uint8_t> scratch;
    };

    // Opacidad y desplazamiento horizontal (fraccion del lienzo) que una
    // transicion aplica a un clip en un instante concreto.
    struct Effect {
        double opacity = 1.0;
        double offsetX = 0.0;
    };

    std::vector<Segment> segments;
    std::vector<TrackState> states;
    std::vector<TransitionInfo> transitions;
    std::vector<TextInfo> texts;

    bool open = false;
    double duration = 0.0;
    double currentTime = 0.0;
    double lastSource = -1.0;

    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    void CloseTrack(TrackState& state);
    void Close();
    bool OpenTrack(TrackState& state, int index);
    bool ReadFrame(TrackState& state, double& pts);
    bool FetchPending(TrackState& state);
    bool SeekTrack(TrackState& state, double sourceTime);
    bool TrackFrameAt(TrackState& state, const Segment& segment, double sourceTime);
    void Composite(const AVFrame* frame, const Segment& segment, TrackState& state,
                   double opacity, double offsetX);
    const Segment* FindSegment(int clipIndex) const;
    Effect EffectFor(const Segment& segment, double time) const;
    void ActiveSegments(int track, double time, std::vector<int>& out) const;

    void EnsureCanvas();
};

void PreviewPlayer::Impl::CloseTrack(TrackState& state)
{
    if (state.sws != nullptr) {
        sws_freeContext(state.sws);
        state.sws = nullptr;
    }
    if (state.display != nullptr) {
        av_frame_free(&state.display);
    }
    if (state.pending != nullptr) {
        av_frame_free(&state.pending);
    }
    if (state.packet != nullptr) {
        av_packet_free(&state.packet);
    }
    if (state.codec != nullptr) {
        avcodec_free_context(&state.codec);
    }
    if (state.format != nullptr) {
        avformat_close_input(&state.format);
    }

    state.videoStream = -1;
    state.timeBase = 0.0;
    state.openSegment = -1;
    state.hasPendingPacket = false;
    state.flushSent = false;
    state.hasPending = false;
    state.pendingPts = 0.0;
    state.hasDisplay = false;
    state.displayPts = 0.0;
    state.lastPts = -1.0;
    state.scratch.clear();
}

void PreviewPlayer::Impl::Close()
{
    for (TrackState& state : states) {
        CloseTrack(state);
    }
    states.clear();
    segments.clear();
    transitions.clear();
    open = false;
    duration = 0.0;
    currentTime = 0.0;
    lastSource = -1.0;
    width = 0;
    height = 0;
    pixels.clear();
}

bool PreviewPlayer::Impl::OpenTrack(TrackState& state, int index)
{
    CloseTrack(state);

    if (index < 0 || index >= static_cast<int>(segments.size())) {
        return false;
    }

    const Segment& segment = segments[static_cast<size_t>(index)];
    const std::string path = ToUtf8(segment.path);

    if (avformat_open_input(&state.format, path.c_str(), nullptr, nullptr) < 0) {
        CloseTrack(state);
        return false;
    }
    if (avformat_find_stream_info(state.format, nullptr) < 0) {
        CloseTrack(state);
        return false;
    }

    const AVCodec* decoder = nullptr;
    state.videoStream =
        av_find_best_stream(state.format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (state.videoStream < 0 || decoder == nullptr) {
        CloseTrack(state);
        return false;
    }

    state.codec = avcodec_alloc_context3(decoder);
    if (state.codec == nullptr) {
        CloseTrack(state);
        return false;
    }

    avcodec_parameters_to_context(state.codec, state.format->streams[state.videoStream]->codecpar);
    state.codec->thread_count = 0;

    if (avcodec_open2(state.codec, decoder, nullptr) < 0) {
        CloseTrack(state);
        return false;
    }

    state.packet = av_packet_alloc();
    state.pending = av_frame_alloc();
    state.display = av_frame_alloc();
    if (state.packet == nullptr || state.pending == nullptr || state.display == nullptr) {
        CloseTrack(state);
        return false;
    }

    state.timeBase = av_q2d(state.format->streams[state.videoStream]->time_base);
    const AVRational frameRate =
        av_guess_frame_rate(state.format, state.format->streams[state.videoStream], nullptr);
    state.sourceFps = (frameRate.num > 0 && frameRate.den > 0) ? av_q2d(frameRate) : 30.0;
    if (state.sourceFps < 1.0 || state.sourceFps > 480.0) {
        state.sourceFps = 30.0;
    }

    state.openSegment = index;
    state.hasPendingPacket = false;
    state.flushSent = false;
    state.hasPending = false;
    state.hasDisplay = false;
    state.lastPts = -1.0;
    return true;
}

bool PreviewPlayer::Impl::ReadFrame(TrackState& state, double& pts)
{
    for (;;) {
        const int received = avcodec_receive_frame(state.codec, state.pending);
        if (received == 0) {
            int64_t stamp = state.pending->best_effort_timestamp;
            if (stamp == AV_NOPTS_VALUE) {
                stamp = state.pending->pts;
            }
            if (stamp == AV_NOPTS_VALUE) {
                stamp = 0;
            }
            pts = static_cast<double>(stamp) * state.timeBase;
            return true;
        }
        if (received == AVERROR_EOF) {
            return false;
        }
        if (received != AVERROR(EAGAIN)) {
            return false;
        }

        if (!state.hasPendingPacket) {
            const int read = av_read_frame(state.format, state.packet);
            if (read < 0) {
                if (!state.flushSent) {
                    avcodec_send_packet(state.codec, nullptr);
                    state.flushSent = true;
                    continue;
                }
                return false;
            }
            if (state.packet->stream_index != state.videoStream) {
                av_packet_unref(state.packet);
                continue;
            }
            state.hasPendingPacket = true;
        }

        const int sent = avcodec_send_packet(state.codec, state.packet);
        if (sent == AVERROR(EAGAIN)) {
            continue;
        }

        av_packet_unref(state.packet);
        state.hasPendingPacket = false;

        if (sent == AVERROR_EOF) {
            state.flushSent = true;
            return false;
        }
        if (sent < 0) {
            return false;
        }
    }
}

bool PreviewPlayer::Impl::FetchPending(TrackState& state)
{
    if (state.hasPending) {
        return true;
    }
    if (state.pending == nullptr) {
        return false;
    }

    av_frame_unref(state.pending);
    double pts = 0.0;
    if (!ReadFrame(state, pts)) {
        return false;
    }
    state.pendingPts = pts;
    state.hasPending = true;
    return true;
}

bool PreviewPlayer::Impl::SeekTrack(TrackState& state, double sourceTime)
{
    if (state.format == nullptr || state.videoStream < 0) {
        return false;
    }

    const int64_t target = static_cast<int64_t>(sourceTime / state.timeBase);
    int result = avformat_seek_file(state.format, state.videoStream, INT64_MIN, target, INT64_MAX,
                                    AVSEEK_FLAG_BACKWARD);
    if (result < 0) {
        result = av_seek_frame(state.format, state.videoStream, target, AVSEEK_FLAG_BACKWARD);
    }
    if (result < 0) {
        return false;
    }

    avcodec_flush_buffers(state.codec);
    if (state.hasPendingPacket) {
        av_packet_unref(state.packet);
        state.hasPendingPacket = false;
    }
    av_frame_unref(state.pending);
    av_frame_unref(state.display);
    state.flushSent = false;
    state.hasPending = false;
    state.hasDisplay = false;
    state.lastPts = -1.0;
    return true;
}

bool PreviewPlayer::Impl::TrackFrameAt(TrackState& state, const Segment& segment,
                                       double sourceTime)
{
    const double frameTime = 1.0 / (state.sourceFps > 0.0 ? state.sourceFps : 30.0);

    sourceTime = (std::max)(sourceTime, segment.in);
    sourceTime = (std::min)(sourceTime, (std::max)(segment.in, segment.out - 0.001));

    // Si el objetivo quedo muy atras (o muy adelante) del ultimo frame
    // decodificado, conviene volver a buscar para no recorrer el archivo entero.
    const bool needSeek =
        state.lastPts < 0.0 || sourceTime < state.lastPts - frameTime * 0.5 ||
        sourceTime > state.lastPts + 1.5 ||
        (state.hasPending && sourceTime < state.pendingPts - 2.0);
    if (needSeek && !SeekTrack(state, (std::max)(segment.in, sourceTime - 0.25))) {
        // Si la busqueda falla se intenta leer desde donde este el decodificador.
    }

    for (;;) {
        if (!FetchPending(state)) {
            break;
        }

        // El frame pendiente ya pertenece al futuro: el frame mostrado sigue siendo
        // el ultimo que estaba en el objetivo.
        if (state.pendingPts > sourceTime + frameTime * 0.5 + 1e-6) {
            break;
        }

        av_frame_unref(state.display);
        if (av_frame_ref(state.display, state.pending) < 0) {
            state.hasPending = false;
            break;
        }
        state.displayPts = state.pendingPts;
        state.hasDisplay = true;
        state.hasPending = false;
    }

    // Si el primer frame tras la busqueda ya esta en el futuro, se usa igualmente
    // para no dejar la capa en negro.
    if (!state.hasDisplay && state.hasPending) {
        av_frame_unref(state.display);
        if (av_frame_ref(state.display, state.pending) >= 0) {
            state.displayPts = state.pendingPts;
            state.hasDisplay = true;
            state.hasPending = false;
        }
    }

    if (!state.hasDisplay || state.display->width <= 0 || state.display->height <= 0) {
        return false;
    }

    state.lastPts = state.displayPts;
    return true;
}

void PreviewPlayer::Impl::Composite(const AVFrame* frame, const Segment& segment,
                                    TrackState& state, double opacity, double offsetX)
{
    if (pixels.empty() || frame == nullptr || frame->width <= 0 || frame->height <= 0) {
        return;
    }

    opacity = (std::max)(0.0, (std::min)(opacity, 1.0));
    if (opacity <= 0.002) {
        return;
    }

    const double fit = (std::min)(static_cast<double>(kPreviewWidth) / frame->width,
                                  static_cast<double>(kPreviewHeight) / frame->height);
    const double factorX = fit * segment.scaleX;
    const double factorY = fit * segment.scaleY;

    int dstW = Even(static_cast<int>(std::floor(frame->width * factorX + 0.5)));
    int dstH = Even(static_cast<int>(std::floor(frame->height * factorY + 0.5)));
    dstW = (std::min)(dstW, kPreviewWidth * 4);
    dstH = (std::min)(dstH, kPreviewHeight * 4);

    state.sws = sws_getCachedContext(state.sws, frame->width, frame->height,
                                    static_cast<AVPixelFormat>(frame->format), dstW, dstH,
                                    AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (state.sws == nullptr) {
        return;
    }

    const size_t stride = static_cast<size_t>(dstW) * 4;
    const size_t needed = stride * static_cast<size_t>(dstH);
    if (state.scratch.size() < needed) {
        state.scratch.resize(needed);
    }

    uint8_t* destination[4] = { state.scratch.data(), nullptr, nullptr, nullptr };
    int destinationStride[4] = { static_cast<int>(stride), 0, 0, 0 };
    sws_scale(state.sws, frame->data, frame->linesize, 0, frame->height, destination,
              destinationStride);

    if (segment.negate) {
        ApplyNegate(state.scratch.data(), dstW, dstH, static_cast<int>(stride));
    }
    if (segment.flipH) {
        ApplyFlipH(state.scratch.data(), dstW, dstH, static_cast<int>(stride));
    }
    if (segment.flipV) {
        ApplyFlipV(state.scratch.data(), dstW, dstH, static_cast<int>(stride));
    }

    const int x = (kPreviewWidth - dstW) / 2 +
                  static_cast<int>(std::llround((segment.posX + offsetX) * kPreviewWidth));
    const int y = (kPreviewHeight - dstH) / 2 +
                  static_cast<int>(std::llround(segment.posY * kPreviewHeight));

    const int srcX = (std::max)(0, -x);
    const int srcY = (std::max)(0, -y);
    const int dstX = (std::max)(0, x);
    const int dstY = (std::max)(0, y);
    const int copyW = (std::min)(dstW - srcX, kPreviewWidth - dstX);
    const int copyH = (std::min)(dstH - srcY, kPreviewHeight - dstY);
    if (copyW <= 0 || copyH <= 0) {
        return;
    }

    if (opacity >= 0.998) {
        for (int row = 0; row < copyH; ++row) {
            const uint8_t* sourceRow =
                state.scratch.data() + static_cast<size_t>(srcY + row) * stride +
                static_cast<size_t>(srcX) * 4;
            uint8_t* destinationRow =
                pixels.data() + (static_cast<size_t>(dstY + row) * kPreviewWidth + dstX) * 4;
            memcpy(destinationRow, sourceRow, static_cast<size_t>(copyW) * 4);
        }
        return;
    }

    // Mezcla alfa sobre el lienzo: se usa para disolvencias y fundidos. El
    // canal alfa del lienzo se deja opaco porque la vista previa lo ignora.
    for (int row = 0; row < copyH; ++row) {
        const uint8_t* sourceRow =
            state.scratch.data() + static_cast<size_t>(srcY + row) * stride +
            static_cast<size_t>(srcX) * 4;
        uint8_t* destinationRow =
            pixels.data() + (static_cast<size_t>(dstY + row) * kPreviewWidth + dstX) * 4;

        for (int column = 0; column < copyW; ++column) {
            const uint8_t* source = sourceRow + static_cast<size_t>(column) * 4;
            uint8_t* destination = destinationRow + static_cast<size_t>(column) * 4;

            const double sourceAlpha =
                opacity * (static_cast<double>(source[3]) / 255.0);
            if (sourceAlpha >= 0.998) {
                destination[0] = source[0];
                destination[1] = source[1];
                destination[2] = source[2];
                destination[3] = 255;
                continue;
            }

            const double inverse = 1.0 - sourceAlpha;
            destination[0] = static_cast<uint8_t>(
                std::llround(source[0] * sourceAlpha + destination[0] * inverse));
            destination[1] = static_cast<uint8_t>(
                std::llround(source[1] * sourceAlpha + destination[1] * inverse));
            destination[2] = static_cast<uint8_t>(
                std::llround(source[2] * sourceAlpha + destination[2] * inverse));
            destination[3] = 255;
        }
    }
}

const PreviewPlayer::Impl::Segment* PreviewPlayer::Impl::FindSegment(int clipIndex) const
{
    for (const Segment& segment : segments) {
        if (segment.clipIndex == clipIndex) {
            return &segment;
        }
    }
    return nullptr;
}

PreviewPlayer::Impl::Effect PreviewPlayer::Impl::EffectFor(const Segment& segment,
                                                           double time) const
{
    Effect effect;

    for (const TransitionInfo& transition : transitions) {
        if (!transition.IsValid()) {
            continue;
        }

        const Segment* from = FindSegment(transition.fromClip);
        const Segment* to = FindSegment(transition.toClip);
        if (from == nullptr || to == nullptr) {
            continue;
        }

        const double span = transition.EffectiveDuration();
        if (span <= 0.0) {
            continue;
        }

        const double windowStart = to->start;
        const double windowEnd = windowStart + span;

        if (transition.type == TransitionType::Crossfade) {
            // El clip anterior se mantiene opaco y el nuevo aparece encima con
            // alfa creciente, que es como lo compone el filtro overlay.
            if (segment.clipIndex == transition.toClip && time >= windowStart) {
                const double progress = (time - windowStart) / span;
                if (progress < 1.0) {
                    effect.opacity *= (std::max)(0.0, (std::min)(progress, 1.0));
                }
            }
        }
        else if (transition.type == TransitionType::FadeBlack) {
            if (segment.clipIndex == transition.fromClip) {
                const double end = from->start + from->duration;
                if (time >= end - span) {
                    const double progress = (end - time) / span;
                    if (progress < 1.0) {
                        effect.opacity *= (std::max)(0.0, (std::min)(progress, 1.0));
                    }
                }
            }
            else if (segment.clipIndex == transition.toClip) {
                if (time <= windowEnd) {
                    const double progress = (time - windowStart) / span;
                    if (progress < 1.0) {
                        effect.opacity *= (std::max)(0.0, (std::min)(progress, 1.0));
                    }
                }
            }
        }
        else if (transition.type == TransitionType::SlideLeft) {
            if (time >= windowStart && time < windowEnd) {
                const double progress = (time - windowStart) / span;
                if (segment.clipIndex == transition.fromClip) {
                    effect.offsetX -= progress;
                }
                else if (segment.clipIndex == transition.toClip) {
                    effect.offsetX += (1.0 - progress);
                }
            }
        }
    }

    return effect;
}

void PreviewPlayer::Impl::ActiveSegments(int track, double time, std::vector<int>& out) const
{
    out.clear();
    for (size_t i = 0; i < segments.size(); ++i) {
        const Segment& segment = segments[i];
        if (segment.track != track) {
            continue;
        }
        if (time + 1e-6 >= segment.start &&
            time < segment.start + segment.duration - 1e-6) {
            out.push_back(static_cast<int>(i));
        }
    }

    // El clip que empezo primero se dibuja antes; si coinciden, el que esta
    // mas adelante en la lista queda encima.
    std::sort(out.begin(), out.end(), [this](int a, int b) {
        const Segment& left = segments[static_cast<size_t>(a)];
        const Segment& right = segments[static_cast<size_t>(b)];
        if (std::fabs(left.start - right.start) > 1e-9) {
            return left.start < right.start;
        }
        return left.clipIndex < right.clipIndex;
    });
}

void PreviewPlayer::Impl::EnsureCanvas()
{
    const size_t expected = static_cast<size_t>(kPreviewWidth) * kPreviewHeight * 4;
    if (pixels.size() != expected) {
        pixels.assign(expected, 0);
    }
    width = kPreviewWidth;
    height = kPreviewHeight;
}

PreviewPlayer::PreviewPlayer()
    : m_impl(std::make_unique<Impl>())
{
}

PreviewPlayer::~PreviewPlayer() = default;

bool PreviewPlayer::Open(const std::vector<ClipInfo>& clips, std::wstring& error)
{
    return Open(clips, {}, error);
}

bool PreviewPlayer::Open(const std::vector<ClipInfo>& clips,
                         const std::vector<TransitionInfo>& transitions,
                         std::wstring& error)
{
    Impl& data = *m_impl;
    data.Close();

    double duration = 0.0;

    for (const TextInfo& text : data.texts) {
        duration = (std::max)(duration, text.End());
    }

    for (size_t index = 0; index < clips.size(); ++index) {
        const ClipInfo& clip = clips[index];
        const double length = clip.OutputDuration();
        if (length <= 0.001) {
            continue;
        }

        duration = (std::max)(duration, clip.start + length);

        if (clip.IsAudioOnly()) {
            continue;
        }

        Impl::Segment segment;
        segment.path = clip.path;
        segment.in = clip.TrimStart();
        segment.out = clip.TrimEnd();
        segment.start = (std::max)(0.0, clip.start);
        segment.duration = length;
        segment.speed = clip.EffectiveSpeed();
        segment.track = clip.EffectiveTrack();
        segment.clipIndex = static_cast<int>(index);
        segment.scaleX = clip.EffectiveScaleX();
        segment.scaleY = clip.EffectiveScaleY();
        segment.posX = clip.posX;
        segment.posY = clip.posY;
        segment.negate = clip.negate;
        segment.flipH = clip.flipH;
        segment.flipV = clip.flipV;

        data.segments.push_back(segment);
    }

    data.duration = (std::max)(duration, TimelineDuration(clips, data.texts));
    data.currentTime = 0.0;
    data.lastSource = -1.0;

    for (const TransitionInfo& transition : transitions) {
        if (transition.IsValid()) {
            data.transitions.push_back(transition);
        }
    }

    if (data.duration <= 0.0) {
        // Un proyecto formado solo por texto tambien tiene vista previa: se
        // muestra el texto sobre un lienzo negro durante unos segundos.
        if (data.texts.empty()) {
            error = L"No hay clips con duracion valida.";
            data.Close();
            return false;
        }
        data.duration = 5.0;
    }

    data.states.assign(data.segments.size(), Impl::TrackState{});
    data.open = true;
    data.EnsureCanvas();
    std::fill(data.pixels.begin(), data.pixels.end(), static_cast<uint8_t>(0));
    return true;
}

void PreviewPlayer::Close()
{
    m_impl->Close();
}

bool PreviewPlayer::IsOpen() const
{
    return m_impl->open;
}

double PreviewPlayer::Duration() const
{
    return m_impl->duration;
}

double PreviewPlayer::CurrentTime() const
{
    return m_impl->currentTime;
}

int PreviewPlayer::Width() const
{
    return m_impl->width;
}

int PreviewPlayer::Height() const
{
    return m_impl->height;
}

const std::vector<uint8_t>& PreviewPlayer::Pixels() const
{
    return m_impl->pixels;
}

bool PreviewPlayer::RenderAt(double outputTime)
{
    Impl& data = *m_impl;
    if (!data.open) {
        return false;
    }

    outputTime = (std::max)(0.0, (std::min)(outputTime, data.duration));
    data.EnsureCanvas();
    std::fill(data.pixels.begin(), data.pixels.end(), static_cast<uint8_t>(0));
    data.lastSource = -1.0;

    // De abajo hacia arriba: la capa 0 es la base y la 2 queda encima. Dentro
    // de una capa pueden convivir dos clips durante una transicion. Cada texto
    // se dibuja justo despues de los clips de su propia capa, igual que hace la
    // exportacion, para que al cambiarlo de capa cambie tambien que lo tapa.
    std::vector<int> active;
    std::vector<TextInfo> activeTexts;
    for (int track = 0; track < kVideoTrackCount; ++track) {
        data.ActiveSegments(track, outputTime, active);

        for (const int index : active) {
            if (index < 0 || index >= static_cast<int>(data.segments.size()) ||
                index >= static_cast<int>(data.states.size())) {
                continue;
            }

            Impl::Segment& segment = data.segments[static_cast<size_t>(index)];
            Impl::TrackState& state = data.states[static_cast<size_t>(index)];

            if (state.openSegment != index) {
                if (!data.OpenTrack(state, index)) {
                    continue;
                }
                data.SeekTrack(state, segment.in);
            }

            const double source =
                segment.in + (outputTime - segment.start) * segment.speed;
            if (!data.TrackFrameAt(state, segment, source)) {
                continue;
            }

            const Impl::Effect effect = data.EffectFor(segment, outputTime);
            data.Composite(state.display, segment, state, effect.opacity, effect.offsetX);
            data.lastSource = state.displayPts;
        }

        activeTexts.clear();
        for (const TextInfo& text : data.texts) {
            if (text.EffectiveTrack() == track && text.IsActiveAt(outputTime)) {
                activeTexts.push_back(text);
            }
        }
        if (!activeTexts.empty()) {
            DrawTextsBgra(data.pixels.data(), data.width, data.height, activeTexts);
        }
    }

    data.currentTime = outputTime;
    return true;
}

void PreviewPlayer::SetTexts(const std::vector<TextInfo>& texts)
{
    m_impl->texts = texts;
}

void PreviewPlayer::SetClipTransform(int clipIndex, double scaleX, double scaleY,
                                     double posX, double posY)
{
    for (Impl::Segment& segment : m_impl->segments) {
        if (segment.clipIndex != clipIndex) {
            continue;
        }
        segment.scaleX = (std::max)(0.05, (std::min)(scaleX, 4.0));
        segment.scaleY = (std::max)(0.05, (std::min)(scaleY, 4.0));
        segment.posX = (std::max)(-1.0, (std::min)(posX, 1.0));
        segment.posY = (std::max)(-1.0, (std::min)(posY, 1.0));
    }
}

bool PreviewPlayer::StepFrame(int direction)
{
    Impl& data = *m_impl;
    if (!data.open || direction == 0) {
        return false;
    }

    const double limit = (std::max)(0.0, data.duration - 0.001);
    double target = data.currentTime + (direction > 0 ? kOutputFrame : -kOutputFrame);
    target = (std::max)(0.0, (std::min)(target, limit));

    if (std::fabs(target - data.currentTime) < 1e-9) {
        return false;
    }

    return RenderAt(target);
}
