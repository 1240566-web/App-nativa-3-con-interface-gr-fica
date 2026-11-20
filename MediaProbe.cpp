#include "MediaProbe.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}

#include <windows.h>

#include <string>

namespace {

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

std::wstring FromUtf8(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), size);
    return result;
}

std::wstring AvErrorText(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(code, buffer, sizeof(buffer)) < 0) {
        return L"error desconocido";
    }
    return FromUtf8(buffer);
}

bool IsAttachedPicture(const AVStream* stream)
{
    return (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
}

} // namespace

bool ProbeClip(const std::wstring& path, ClipInfo& clip, std::wstring& error)
{
    clip = ClipInfo{};
    clip.path = path;

    const size_t slash = path.find_last_of(L"\\/");
    clip.fileName = (slash == std::wstring::npos) ? path : path.substr(slash + 1);

    AVFormatContext* formatContext = nullptr;
    const std::string utf8Path = ToUtf8(path);

    int result = avformat_open_input(&formatContext, utf8Path.c_str(), nullptr, nullptr);
    if (result < 0) {
        error = L"No se pudo abrir el archivo: " + AvErrorText(result);
        return false;
    }

    result = avformat_find_stream_info(formatContext, nullptr);
    if (result < 0) {
        error = L"No se pudo leer la informacion del archivo: " + AvErrorText(result);
        avformat_close_input(&formatContext);
        return false;
    }

    const int videoIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audioIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    const AVStream* videoStream = nullptr;
    for (unsigned i = 0; i < formatContext->nb_streams; ++i) {
        const AVStream* stream = formatContext->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !IsAttachedPicture(stream)) {
            if (static_cast<int>(i) == videoIndex || videoStream == nullptr) {
                videoStream = stream;
                if (static_cast<int>(i) == videoIndex) {
                    break;
                }
            }
        }
    }

    // Un archivo sin video pero con audio se acepta en la capa de audio.
    const AVStream* audioStream = nullptr;
    if (audioIndex >= 0) {
        audioStream = formatContext->streams[audioIndex];
    }

    if (videoStream == nullptr) {
        if (audioStream == nullptr) {
            error = L"El archivo no contiene pistas de video ni de audio.";
            avformat_close_input(&formatContext);
            return false;
        }

        clip.track = kAudioTrack;
        clip.hasAudio = true;

        if (formatContext->duration != AV_NOPTS_VALUE && formatContext->duration > 0) {
            clip.duration = static_cast<double>(formatContext->duration) / AV_TIME_BASE;
        }
        else if (audioStream->duration != AV_NOPTS_VALUE && audioStream->duration > 0) {
            clip.duration =
                static_cast<double>(audioStream->duration) * av_q2d(audioStream->time_base);
        }

        avformat_close_input(&formatContext);
        return clip.duration > 0.0;
    }

    clip.track = 0;
    clip.width = videoStream->codecpar->width;
    clip.height = videoStream->codecpar->height;

    const AVRational frameRate = av_guess_frame_rate(formatContext, const_cast<AVStream*>(videoStream), nullptr);
    if (frameRate.num > 0 && frameRate.den > 0) {
        clip.fps = av_q2d(frameRate);
    }

    if (formatContext->duration != AV_NOPTS_VALUE && formatContext->duration > 0) {
        clip.duration = static_cast<double>(formatContext->duration) / AV_TIME_BASE;
    }
    else if (videoStream->duration != AV_NOPTS_VALUE && videoStream->duration > 0) {
        clip.duration = static_cast<double>(videoStream->duration) * av_q2d(videoStream->time_base);
    }

    clip.hasAudio = audioIndex >= 0;

    avformat_close_input(&formatContext);
    return true;
}
