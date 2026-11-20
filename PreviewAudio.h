#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "VideoEditorTypes.h"

// Mezcla el audio del timeline (clips ya recortados) en PCM estereo 48 kHz
// 16 bits, aplicando la velocidad de cada clip y rellenando silencio cuando
// un clip no tiene pista de audio.
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    bool Open(const std::vector<ClipInfo>& clips, std::wstring& error);
    void Close();

    bool HasAudio() const;
    double Duration() const;

    // Coloca la lectura en el tiempo de salida indicado.
    bool Seek(double outputTime);

    // Llena "out" con hasta "frames" muestras estereo. Devuelve las muestras
    // escritas; 0 significa que se llego al final del timeline.
    int Read(int16_t* out, int frames);

    // Cambia el volumen de un clip sin volver a decodificar su audio.
    void SetClipVolume(int clipIndex, double volume);

    static constexpr int SampleRate = 48000;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Reproduce el audio de la vista previa con waveOut y lo mantiene alineado
// con el reloj de reproduccion del video.
class PreviewAudio {
public:
    PreviewAudio();
    ~PreviewAudio();

    bool Open(const std::vector<ClipInfo>& clips, std::wstring& error);
    void Close();

    bool HasAudio() const;
    bool IsPlaying() const;

    // Empieza a reproducir desde el tiempo de salida indicado.
    void Play(double outputTime);

    // Detiene la reproduccion sin cerrar el dispositivo.
    void Pause();

    // Si el video salto a otro punto, reinicia el audio en ese tiempo.
    void Sync(double outputTime);

    // Mantiene llenas las colas de waveOut; se llama desde el timer de la UI.
    void Pump();

    // Ajusta el volumen de un clip de la mezcla en tiempo real.
    void SetClipVolume(int clipIndex, double volume);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
