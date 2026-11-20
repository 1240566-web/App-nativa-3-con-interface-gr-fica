#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

#include "VideoEditorTypes.h"

// Dibuja los textos sobre un canvas BGRA (arriba abajo) ya compuesto. scaleX y
// scaleY convierten las coordenadas del lienzo de referencia 1280x720 al tamano
// real del buffer.
void DrawTextsBgra(uint8_t* pixels, int width, int height,
                   const std::vector<TextInfo>& texts);

// Genera una PNG transparente del tamano indicado con todos los textos. Se usa
// al exportar para superponerla sobre el video con el filtro overlay.
bool SaveTextOverlayPng(const std::vector<TextInfo>& texts, int width, int height,
                        const std::wstring& path);

// Guarda un unico frame BGRA transparente con los textos. Es el formato que
// acepta el FFmpeg incluido (no tiene decodificador PNG); se reproduce en bucle
// con -stream_loop y se compone con el filtro overlay.
bool SaveTextOverlayBgra(const std::vector<TextInfo>& texts, int width, int height,
                         const std::wstring& path);
