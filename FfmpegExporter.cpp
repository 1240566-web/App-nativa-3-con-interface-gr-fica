#include "FfmpegExporter.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

std::wstring Num(double value)
{
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%.6f", value);
    std::wstring text(buffer);

    // Quita ceros y punto final: "1.500000" -> "1.5", "2.000000" -> "2"
    if (text.find(L'.') != std::wstring::npos) {
        while (!text.empty() && text.back() == L'0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == L'.') {
            text.pop_back();
        }
    }
    return text;
}

// Reglas de quoting de Windows para CreateProcess.
std::wstring Quote(const std::wstring& value)
{
    std::wstring out = L"\"";
    size_t backslashes = 0;

    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            out += ch;
        }
        else if (ch == L'"') {
            out.append(backslashes + 1, L'\\');
            out += ch;
            backslashes = 0;
        }
        else {
            backslashes = 0;
            out += ch;
        }
    }

    out.append(backslashes, L'\\');
    out += L'"';
    return out;
}

// atempo solo acepta 0.5 .. 2.0 por instancia, asi que se encadena.
std::wstring BuildAtempoChain(double speed)
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

    std::wstring chain;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (i != 0) {
            chain += L",";
        }
        chain += L"atempo=" + Num(steps[i]);
    }
    return chain;
}

std::wstring BitrateForHeight(int height)
{
    if (height >= 2000) {
        return L"28M";
    }
    if (height >= 1400) {
        return L"16M";
    }
    if (height >= 1000) {
        return L"8M";
    }
    return L"5M";
}

} // namespace

std::wstring BuildFfmpegArguments(const ExportRequest& request, double& totalDuration)
{
    totalDuration = 0.0;
    const int clipCount = static_cast<int>(request.inputPaths.size());

    const int width = (request.settings.width / 2) * 2;
    const int height = (request.settings.height / 2) * 2;
    const std::wstring canvasW = std::to_wstring(width);
    const std::wstring canvasH = std::to_wstring(height);

    struct ClipPlan {
        double in = 0.0;
        double length = 0.0;
        double speed = 1.0;
        int track = 0;
        double start = 0.0;
        double scaleX = 1.0;
        double scaleY = 1.0;
        double posX = 0.0;
        double posY = 0.0;
        double volume = 1.0;
        bool hasVideo = false;
        bool hasAudio = false;
        bool negate = false;
        bool flipH = false;
        bool flipV = false;
    };

    std::vector<ClipPlan> plans(static_cast<size_t>(clipCount));

    std::wstring args = L"-hide_banner -loglevel error -nostats -progress pipe:1 -y ";

    for (int i = 0; i < clipCount; ++i) {
        const size_t index = static_cast<size_t>(i);
        ClipPlan& plan = plans[index];

        double in = (index < request.inPoints.size()) ? request.inPoints[index] : 0.0;
        double out = (index < request.outPoints.size()) ? request.outPoints[index] : 0.0;
        if (in < 0.0) {
            in = 0.0;
        }
        plan.in = in;
        plan.length = (out > in) ? (out - in) : 0.0;

        double speed = (index < request.speeds.size()) ? request.speeds[index] : 1.0;
        plan.speed = (std::max)(0.25, (std::min)(4.0, speed));

        plan.track = (index < request.tracks.size()) ? request.tracks[index] : 0;
        plan.start = (index < request.starts.size()) ? (std::max)(0.0, request.starts[index]) : 0.0;

        double scaleX = (index < request.scaleXs.size()) ? request.scaleXs[index] : 1.0;
        double scaleY = (index < request.scaleYs.size()) ? request.scaleYs[index] : 1.0;
        if (index >= request.scaleXs.size() && index < request.scales.size()) {
            scaleX = request.scales[index];
        }
        if (index >= request.scaleYs.size() && index < request.scales.size()) {
            scaleY = request.scales[index];
        }
        plan.scaleX = (std::max)(0.05, (std::min)(4.0, scaleX));
        plan.scaleY = (std::max)(0.05, (std::min)(4.0, scaleY));
        if (index < request.posXs.size()) {
            plan.posX = (std::max)(-1.0, (std::min)(1.0, request.posXs[index]));
        }
        if (index < request.posYs.size()) {
            plan.posY = (std::max)(-1.0, (std::min)(1.0, request.posYs[index]));
        }
        if (index < request.volumes.size()) {
            plan.volume = (std::max)(0.0, (std::min)(2.0, request.volumes[index]));
        }

        plan.hasVideo = plan.track != kAudioTrack && plan.length > 0.0;
        plan.hasAudio = (index < request.hasAudio.size()) && request.hasAudio[index];
        plan.negate = index < request.negates.size() && request.negates[index];
        plan.flipH = index < request.flipHs.size() && request.flipHs[index];
        plan.flipV = index < request.flipVs.size() && request.flipVs[index];

        const double outputLength = plan.length / plan.speed;
        totalDuration = (std::max)(totalDuration, plan.start + outputLength);

        if (plan.length > 0.0) {
            args += L"-ss " + Num(plan.in) + L" -t " + Num(plan.length) + L" ";
        }
        args += L"-i " + Quote(request.inputPaths[index]) + L" ";
    }

    // Cada texto lleva su propia PNG para poder aplicar su capa y su ventana de
    // tiempo. Se mantiene el formato de una sola PNG para llamadas antiguas.
    struct TextPlan {
        int track = 0;
        double start = 0.0;
        double duration = 0.0;
        int input = 0;
    };

    std::vector<std::wstring> overlayPaths = request.textOverlayPaths;
    const bool legacySingleOverlay =
        overlayPaths.empty() && !request.textOverlayPath.empty();
    if (legacySingleOverlay) {
        overlayPaths.push_back(request.textOverlayPath);
    }
    const bool hasTexts = !request.texts.empty() && !overlayPaths.empty();
    const int textInputCount = hasTexts ? static_cast<int>(overlayPaths.size()) : 0;

    std::vector<TextPlan> textPlans;
    if (hasTexts) {
        for (size_t i = 0; i < request.texts.size(); ++i) {
            const TextInfo& text = request.texts[i];
            TextPlan plan;
            plan.track = text.EffectiveTrack();
            plan.start = (std::max)(0.0, text.start);
            plan.duration = text.EffectiveDuration();
            plan.input = clipCount + static_cast<int>(
                (std::min)(i, overlayPaths.size() - 1));
            textPlans.push_back(plan);
            totalDuration = (std::max)(totalDuration, plan.start + plan.duration);
        }

        for (const std::wstring& path : overlayPaths) {
            // El FFmpeg incluido no trae decodificador PNG: los textos se
            // guardan como un frame BGRA crudo que se repite en bucle. Se
            // mantiene -loop 1 para PNG de llamadas antiguas.
            const size_t dot = path.find_last_of(L'.');
            const bool raw = dot != std::wstring::npos &&
                             path.substr(dot) == L".bgra";
            if (raw) {
                args += L"-stream_loop -1 -f rawvideo -pixel_format bgra ";
                args += L"-video_size " + canvasW + L"x" + canvasH + L" ";
                args += L"-framerate 30 -i " + Quote(path) + L" ";
            }
            else {
                args += L"-loop 1 -i " + Quote(path) + L" ";
            }
        }
    }

    if (totalDuration <= 0.0) {
        if (hasTexts) {
            totalDuration = 5.0;
        }
        else {
            return {};
        }
    }

    // Transiciones validas: solo entre clips de video con duracion positiva.
    struct TransitionPlan {
        int from = -1;
        int to = -1;
        TransitionType type = TransitionType::Crossfade;
        double duration = 0.0;
    };

    std::vector<TransitionPlan> transitions;
    for (const TransitionInfo& transition : request.transitions) {
        if (!transition.IsValid() || transition.fromClip >= clipCount ||
            transition.toClip >= clipCount) {
            continue;
        }

        const ClipPlan& from = plans[static_cast<size_t>(transition.fromClip)];
        const ClipPlan& to = plans[static_cast<size_t>(transition.toClip)];
        if (!from.hasVideo || !to.hasVideo) {
            continue;
        }

        double span = transition.EffectiveDuration();
        span = (std::min)(span, from.length / from.speed * 0.95);
        span = (std::min)(span, to.length / to.speed * 0.95);
        if (span < 0.05) {
            continue;
        }

        TransitionPlan plan;
        plan.from = transition.fromClip;
        plan.to = transition.toClip;
        plan.type = transition.type;
        plan.duration = span;
        transitions.push_back(plan);
    }

    // Los clips de video se superponen de abajo hacia arriba: Capa 1 primero,
    // despues Capa 2 y al final Capa 3. Dentro de una capa gana el mas tardio.
    std::vector<int> videoOrder;
    for (int i = 0; i < clipCount; ++i) {
        if (plans[static_cast<size_t>(i)].hasVideo) {
            videoOrder.push_back(i);
        }
    }
    std::sort(videoOrder.begin(), videoOrder.end(), [&plans](int a, int b) {
        const ClipPlan& left = plans[static_cast<size_t>(a)];
        const ClipPlan& right = plans[static_cast<size_t>(b)];
        if (left.track != right.track) {
            return left.track < right.track;
        }
        if (std::fabs(left.start - right.start) > 1e-9) {
            return left.start < right.start;
        }
        return a < b;
    });

    std::wstring chain;

    // Lienzo negro del proyecto; el resto de capas se dibuja encima.
    chain += L"color=c=black:s=" + canvasW + L"x" + canvasH + L":r=30:d=" +
             Num(totalDuration) + L",format=yuv420p[base];";

    std::wstring topLabel = L"base";

    size_t overlayIndex = 0;
    for (int track = 0; track < kVideoTrackCount; ++track) {
        for (size_t order = 0; order < videoOrder.size(); ++order) {
            const int i = videoOrder[order];
            const ClipPlan& plan = plans[static_cast<size_t>(i)];
            if (plan.track != track) {
                continue;
            }
            const std::wstring input = std::to_wstring(i);

            const double localEnd = plan.length / plan.speed;

            double fadeIn = 0.0;
            double fadeOut = 0.0;
            std::wstring overlayX = L"(W-w)/2+" + Num(plan.posX) + L"*W";

            for (const TransitionPlan& transition : transitions) {
            if (transition.type == TransitionType::SlideLeft) {
                const double windowStart = plans[static_cast<size_t>(transition.to)].start;
                const double windowEnd = windowStart + transition.duration;
                const std::wstring position = L"(t-" + Num(windowStart) + L")/" +
                                              Num(transition.duration);
                if (transition.from == i) {
                    overlayX += L"-if(between(t," + Num(windowStart) + L"," +
                                Num(windowEnd) + L")," + position + L",0)*W";
                }
                else if (transition.to == i) {
                    overlayX += L"+if(between(t," + Num(windowStart) + L"," +
                                Num(windowEnd) + L"),1-" + position + L",0)*W";
                }
            }
            else if (transition.to == i) {
                // Disolvencia y fundido a negro: el clip nuevo entra con alfa.
                fadeIn = (std::max)(fadeIn, transition.duration);
            }
            else if (transition.from == i && transition.type == TransitionType::FadeBlack) {
                // El clip anterior se apaga; en la disolvencia se mantiene opaco.
                fadeOut = (std::max)(fadeOut, transition.duration);
            }
            }

            // Escala = ajuste al lienzo (fit) por la escala propia del clip. Se
            // calculan ancho y alto por separado para permitir estirar un solo eje.
            chain += L"[" + input + L":v]";
            chain += L"setpts=(PTS-STARTPTS)/" + Num(plan.speed) + L",fps=30,";
            chain += L"scale=w='trunc(min(" + canvasW + L"," + canvasH + L"*iw/ih)*" +
                 Num(plan.scaleX) + L"/2)*2':h='trunc(min(" + canvasH + L"," + canvasW +
                 L"*ih/iw)*" + Num(plan.scaleY) + L"/2)*2',setsar=1,";

            if (plan.flipH) {
            chain += L"hflip,";
            }
            if (plan.flipV) {
            chain += L"vflip,";
            }
            if (plan.negate) {
            chain += L"negate,";
            }

            chain += L"format=yuva420p";

            if (fadeIn > 0.0) {
            chain += L",fade=t=in:st=0:d=" + Num(fadeIn) + L":alpha=1";
            }
            if (fadeOut > 0.0) {
            const double fadeStart = (std::max)(0.0, localEnd - fadeOut);
            chain += L",fade=t=out:st=" + Num(fadeStart) + L":d=" + Num(fadeOut) + L":alpha=1";
            }

            // yuva420p conserva la transparencia de las zonas libres del lienzo y
            // el desplazamiento temporal coloca el clip en su segundo de inicio.
            const std::wstring overlay = std::to_wstring(overlayIndex++);
            chain += L",setpts=PTS-STARTPTS+" + Num(plan.start) + L"/TB[ov" +
                 overlay + L"];";

            const double end = plan.start + localEnd;

            chain += L"[" + topLabel + L"][ov" + overlay + L"]overlay=";
            chain += L"x=" + overlayX + L":y=(H-h)/2+" + Num(plan.posY) + L"*H";
            chain += L":enable='between(t," + Num(plan.start) + L"," + Num(end) + L")'";
            chain += L":eof_action=pass[v" + overlay + L"];";

            topLabel = L"v" + overlay;
        }

        // Los textos de una capa se componen justo despues de sus videos, antes
        // de pasar a la capa superior. Asi el cambio de capa se ve tambien en el
        // archivo exportado.
        if (hasTexts && !legacySingleOverlay) {
            for (size_t t = 0; t < textPlans.size(); ++t) {
                const TextPlan& plan = textPlans[static_cast<size_t>(t)];
                if (plan.track != track) {
                    continue;
                }
                const std::wstring source = L"txt" + std::to_wstring(t);
                const std::wstring output = L"txtv" + std::to_wstring(t);
                chain += L"[" + std::to_wstring(plan.input) + L":v]format=rgba[" +
                         source + L"];";
                chain += L"[" + topLabel + L"][" + source +
                         L"]overlay=0:0:format=auto:shortest=0:enable='between(t," +
                         Num(plan.start) + L"," + Num(plan.start + plan.duration) +
                         L")'[" + output + L"];";
                topLabel = output;
            }
        }
    }

    std::wstring videoLabel = L"vfinal";
    chain += L"[" + topLabel + L"]format=yuv420p[vfinal];";
    if (hasTexts && legacySingleOverlay) {
        chain += L"[" + std::to_wstring(clipCount) + L":v]format=rgba[txtsrc];";
        chain += L"[vfinal][txtsrc]overlay=0:0:format=auto:shortest=0[vtext];";
        videoLabel = L"vtext";
    }

    // Audio: cada clip con pista de audio se retrasa hasta su inicio y luego
    // se suman todos sin normalizar (igual que la mezcla de la vista previa).
    std::vector<int> audioOrder;
    for (int i = 0; i < clipCount; ++i) {
        const ClipPlan& plan = plans[static_cast<size_t>(i)];
        if (plan.hasAudio && plan.length > 0.0) {
            audioOrder.push_back(i);
        }
    }

    if (audioOrder.empty()) {
        // Proyecto sin audio: se genera silencio para que el MP4 tenga pista.
        args += L"-f lavfi -t " + Num(totalDuration) +
                L" -i anullsrc=channel_layout=stereo:sample_rate=48000 ";
        // Las PNG de texto se anaden despues de los clips; el silencio va tras todas.
        const std::wstring silence = std::to_wstring(clipCount + textInputCount);
        chain += L"[" + silence + L":a]aformat=sample_fmts=fltp:channel_layouts=stereo,"
                 L"apad[aout];";
    }
    else {
        for (size_t order = 0; order < audioOrder.size(); ++order) {
            const int i = audioOrder[order];
            const ClipPlan& plan = plans[static_cast<size_t>(i)];

            chain += L"[" + std::to_wstring(i) + L":a]aresample=48000,"
                     L"aformat=sample_fmts=fltp:channel_layouts=stereo,"
                     L"asetpts=PTS-STARTPTS," +
                     BuildAtempoChain(plan.speed);

            if (std::fabs(plan.volume - 1.0) > 1e-6) {
                chain += L",volume=" + Num(plan.volume);
            }

            if (plan.start > 0.0) {
                const long long milliseconds =
                    static_cast<long long>(plan.start * 1000.0 + 0.5);
                chain += L",adelay=" + std::to_wstring(milliseconds) + L":all=1";
            }
            chain += L"[a" + std::to_wstring(order) + L"];";
        }

        if (audioOrder.size() == 1) {
            chain += L"[a0]alimiter=limit=0.95:level=0,apad[aout];";
        }
        else {
            for (size_t order = 0; order < audioOrder.size(); ++order) {
                chain += L"[a" + std::to_wstring(order) + L"]";
            }
            chain += L"amix=inputs=" + std::to_wstring(audioOrder.size()) +
                     L":duration=longest:dropout_transition=0:normalize=0,"
                     L"alimiter=limit=0.95:level=0,apad[aout];";
        }
    }

    args += L"-filter_complex " + Quote(chain) + L" ";
    args += L"-map [" + videoLabel + L"] -map [aout] ";
    args += L"-t " + Num(totalDuration) + L" ";
    args += L"-c:v h264_mf -b:v " + BitrateForHeight(height) + L" -pix_fmt yuv420p ";
    args += L"-c:a aac -b:a 192k -ar 48000 -movflags +faststart ";
    args += Quote(request.outputPath);

    return args;
}

int RunFfmpeg(const std::wstring& executable,
              const std::wstring& arguments,
              double totalDuration,
              const std::function<void(double)>& onProgress,
              std::atomic<bool>& cancelFlag,
              std::atomic<HANDLE>& processHandle,
              std::wstring& logOutput)
{
    logOutput.clear();

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        logOutput = L"No se pudo crear la tuberia de salida.";
        return -1;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nulInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &security, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = nulInput;

    PROCESS_INFORMATION process{};
    std::wstring commandLine = Quote(executable) + L" " + arguments;

    const BOOL created = CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr,
                                        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);

    CloseHandle(writePipe);
    if (nulInput != nullptr) {
        CloseHandle(nulInput);
    }

    if (!created) {
        CloseHandle(readPipe);
        logOutput = L"No se pudo iniciar ffmpeg (codigo " + std::to_wstring(GetLastError()) + L").";
        return -1;
    }

    processHandle = process.hProcess;

    std::string pending;
    std::string rawLog;
    double lastReported = -1.0;

    const auto processLine = [&](std::string line) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind("out_time_us=", 0) == 0) {
            const long long microseconds = _atoi64(line.c_str() + 12);
            if (totalDuration > 0.0 && microseconds >= 0) {
                double progress = (static_cast<double>(microseconds) / 1000000.0) / totalDuration;
                progress = (std::max)(0.0, (std::min)(1.0, progress));
                if (progress - lastReported >= 0.005 || progress >= 1.0) {
                    lastReported = progress;
                    if (onProgress) {
                        onProgress(progress);
                    }
                }
            }
        }
        else if (!line.empty() && line.rfind("progress=", 0) != 0 && line.rfind("frame=", 0) != 0 &&
                 line.rfind("fps=", 0) != 0 && line.rfind("bitrate=", 0) != 0 &&
                 line.rfind("total_size=", 0) != 0 && line.rfind("out_time=", 0) != 0 &&
                 line.rfind("out_time_ms=", 0) != 0 && line.rfind("dup_frames=", 0) != 0 &&
                 line.rfind("drop_frames=", 0) != 0 && line.rfind("speed=", 0) != 0) {
            rawLog += line;
            rawLog += '\n';
            if (rawLog.size() > 6000) {
                rawLog.erase(0, rawLog.size() - 6000);
            }
        }
    };

    bool running = true;
    char buffer[4096];

    while (running) {
        if (cancelFlag.load()) {
            TerminateProcess(process.hProcess, 1);
        }

        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD read = 0;
            if (ReadFile(readPipe, buffer, static_cast<DWORD>(std::min<size_t>(available, sizeof(buffer))),
                         &read, nullptr) && read > 0) {
                pending.append(buffer, read);
                size_t newline = 0;
                while ((newline = pending.find('\n')) != std::string::npos) {
                    processLine(pending.substr(0, newline));
                    pending.erase(0, newline + 1);
                }
            }
        }
        else {
            const DWORD wait = WaitForSingleObject(process.hProcess, 50);
            if (wait == WAIT_OBJECT_0) {
                running = false;
            }
        }
    }

    // Vacia lo que quede en la tuberia.
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) || read == 0) {
            break;
        }
        pending.append(buffer, read);
        size_t newline = 0;
        while ((newline = pending.find('\n')) != std::string::npos) {
            processLine(pending.substr(0, newline));
            pending.erase(0, newline + 1);
        }
    }
    if (!pending.empty()) {
        processLine(pending);
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(readPipe);
    processHandle = nullptr;

    if (exitCode == 0 && !cancelFlag.load() && onProgress) {
        onProgress(1.0);
    }

    if (!rawLog.empty()) {
        const int size = MultiByteToWideChar(CP_UTF8, 0, rawLog.c_str(),
                                             static_cast<int>(rawLog.size()), nullptr, 0);
        std::wstring wide(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, rawLog.c_str(), static_cast<int>(rawLog.size()),
                            wide.data(), size);
        logOutput = wide;
    }

    return static_cast<int>(exitCode);
}
