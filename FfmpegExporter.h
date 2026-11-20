#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "VideoEditorTypes.h"

// Construye la linea de argumentos de ffmpeg (sin el ejecutable) y calcula la
// duracion total del proyecto resultante.
std::wstring BuildFfmpegArguments(const ExportRequest& request, double& totalDuration);

// Ejecuta ffmpeg oculto, leyendo el progreso de stdout (-progress pipe:1).
// Devuelve el codigo de salida del proceso (-1 si no se pudo lanzar).
int RunFfmpeg(const std::wstring& executable,
              const std::wstring& arguments,
              double totalDuration,
              const std::function<void(double)>& onProgress,
              std::atomic<bool>& cancelFlag,
              std::atomic<HANDLE>& processHandle,
              std::wstring& logOutput);
