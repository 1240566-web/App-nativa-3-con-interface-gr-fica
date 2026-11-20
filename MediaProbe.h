#pragma once

#include <string>
#include "VideoEditorTypes.h"

// Lee duracion, resolucion, fps y presencia de audio usando libavformat.
// Acepta videos y archivos solo de audio (estos quedan en kAudioTrack).
// Devuelve false y rellena 'error' si el archivo no se puede abrir.
bool ProbeClip(const std::wstring& path, ClipInfo& clip, std::wstring& error);
