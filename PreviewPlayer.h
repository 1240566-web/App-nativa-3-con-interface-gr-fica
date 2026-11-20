#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "VideoEditorTypes.h"

// Compone el timeline por capas (3 capas de video) sobre un lienzo BGRA.
// Cada capa mantiene su propio decodificador y se dibuja de abajo hacia arriba
// con la escala y la posicion propias de cada clip.
class PreviewPlayer {
public:
    PreviewPlayer();
    ~PreviewPlayer();

    bool Open(const std::vector<ClipInfo>& clips, std::wstring& error);
    bool Open(const std::vector<ClipInfo>& clips,
              const std::vector<TransitionInfo>& transitions,
              std::wstring& error);
    void Close();

    bool IsOpen() const;
    double Duration() const;
    double CurrentTime() const;

    // Dibuja el frame compuesto correspondiente al tiempo indicado.
    bool RenderAt(double outputTime);

    // Avanza o retrocede un frame de salida (1/30 s).
    bool StepFrame(int direction);

    // Actualiza escala y posicion de un clip sin reabrir los decodificadores.
    // La version de 4 argumentos mantiene la escala uniforme (compatibilidad).
    void SetClipTransform(int clipIndex, double scaleX, double scaleY,
                          double posX, double posY);
    void SetClipTransform(int clipIndex, double scale, double posX, double posY)
    {
        SetClipTransform(clipIndex, scale, scale, posX, posY);
    }

    // Textos superpuestos. No requiere reabrir los decodificadores.
    void SetTexts(const std::vector<TextInfo>& texts);

    int Width() const;
    int Height() const;
    const std::vector<uint8_t>& Pixels() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
