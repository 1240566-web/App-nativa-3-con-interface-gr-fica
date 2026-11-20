#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Capas del proyecto: 3 capas de video (0 = abajo, 2 = arriba) y una capa
// independiente solo para audio.
enum : int {
    kVideoTrackCount = 3,
    kAudioTrack = 3,
    kTrackCount = 4
};

// Tamano de la miniatura generada al importar cada archivo (BGRA, arriba abajo).
enum : int {
    kThumbWidth = 192,
    kThumbHeight = 108
};

// Archivo disponible en la biblioteca de la pestana "Clips". No ocupa ninguna
// capa hasta que se pulsa su boton "+"; a partir de ahi cada copia en el
// timeline es un ClipInfo independiente que apunta a este elemento.
struct MediaItem {
    int id = 0;
    std::wstring path;
    std::wstring fileName;
    double duration = 0.0;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    bool hasAudio = false;
    std::vector<unsigned char> thumbnail;
    int thumbnailWidth = 0;
    int thumbnailHeight = 0;

    bool IsAudioOnly() const { return width <= 0 || height <= 0; }
    bool HasThumbnail() const
    {
        return thumbnailWidth > 0 && thumbnailHeight > 0 &&
               thumbnail.size() >= static_cast<size_t>(thumbnailWidth) *
                                       static_cast<size_t>(thumbnailHeight) * 4u;
    }
};

// Informacion de un clip colocado en el timeline, incluyendo su recorte,
// sus filtros, su velocidad y su posicion dentro del timeline por capas.
struct ClipInfo {
    int mediaId = -1;  // elemento de la biblioteca del que procede (-1 = legado)
    std::wstring path;
    std::wstring fileName;
    double duration = 0.0;  // duracion original del archivo (segundos)
    int width = 0;
    int height = 0;
    double fps = 0.0;
    bool hasAudio = false;

    // Capa (0..2 video, 3 audio) y momento en el que empieza en el timeline.
    int track = 0;
    double start = 0.0;

    // Transformacion propia del clip: escala (1 = ajustado al lienzo) y
    // desplazamiento relativo al centro (fraccion del ancho/alto del lienzo).
    // scale es la escala uniforme que ajustan las esquinas; scaleX/scaleY son
    // multiplicadores independientes que ajustan los tiradores de los bordes
    // para hacer el video mas ancho o mas alto sin conservar la proporcion.
    double scale = 1.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double posX = 0.0;
    double posY = 0.0;

    // Recorte dentro del archivo original (segundos).
    double inPoint = 0.0;
    double outPoint = 0.0;

    // Ajustes propios del clip.
    bool negate = false;  // invertir colores
    bool flipH = false;   // espejo horizontal
    bool flipV = false;   // espejo vertical
    double speed = 1.0;   // 0.25 .. 4.0
    double volume = 1.0;  // 0 .. 2 (0 % .. 200 %)

    double TrimStart() const { return inPoint < 0.0 ? 0.0 : inPoint; }

    double TrimEnd() const
    {
        if (outPoint <= 0.0 || outPoint > duration) {
            return duration;
        }
        return outPoint;
    }

    double TrimDuration() const
    {
        const double value = TrimEnd() - TrimStart();
        return value > 0.0 ? value : 0.0;
    }

    double EffectiveSpeed() const
    {
        if (speed < 0.25) {
            return 0.25;
        }
        return speed > 4.0 ? 4.0 : speed;
    }

    // Duracion del clip ya con la velocidad aplicada.
    double OutputDuration() const { return TrimDuration() / EffectiveSpeed(); }

    double End() const { return start + OutputDuration(); }

    bool IsAudioOnly() const { return width <= 0 || height <= 0; }

    bool IsVideoTrack() const { return track >= 0 && track < kVideoTrackCount; }

    int EffectiveTrack() const
    {
        if (IsAudioOnly()) {
            return kAudioTrack;
        }
        if (track < 0) {
            return 0;
        }
        return track > kVideoTrackCount - 1 ? kVideoTrackCount - 1 : track;
    }

    double EffectiveScale() const
    {
        if (scale < 0.05) {
            return 0.05;
        }
        return scale > 4.0 ? 4.0 : scale;
    }

    double EffectiveScaleX() const
    {
        const double value = scale * scaleX;
        if (value < 0.05) {
            return 0.05;
        }
        return value > 4.0 ? 4.0 : value;
    }

    double EffectiveScaleY() const
    {
        const double value = scale * scaleY;
        if (value < 0.05) {
            return 0.05;
        }
        return value > 4.0 ? 4.0 : value;
    }

    bool HasNonUniformScale() const
    {
        return std::fabs(EffectiveScaleX() - EffectiveScaleY()) > 0.005;
    }

    double EffectiveVolume() const
    {
        if (volume < 0.0) {
            return 0.0;
        }
        return volume > 2.0 ? 2.0 : volume;
    }

    bool IsTrimmed() const
    {
        return TrimStart() > 0.001 || TrimEnd() < duration - 0.001;
    }

    bool HasFilters() const { return negate || flipH || flipV; }

    void ResetTrim() { inPoint = 0.0; outPoint = duration; }

    void ResetFilters()
    {
        negate = false;
        flipH = false;
        flipV = false;
        speed = 1.0;
        volume = 1.0;
    }

    void ResetTransform()
    {
        scale = 1.0;
        scaleX = 1.0;
        scaleY = 1.0;
        posX = 0.0;
        posY = 0.0;
    }
};

// Alineacion del texto dentro de su caja.
enum class TextAlign : int {
    Left = 0,
    Center = 1,
    Right = 2,
    Justify = 3
};

// Texto colocado en una capa del timeline. La caja se guarda en pixeles del
// lienzo de referencia (1280x720), igual que usa la vista previa, y se escala
// al exportar a la resolucion elegida.
struct TextInfo {
    std::wstring text = L"Escribe aqui tu texto";
    int fontSize = 36;              // alto de letra en pixeles del lienzo 1280x720
    bool bold = false;
    bool italic = false;
    bool underline = false;
    TextAlign align = TextAlign::Left;

    // Capa de video (0..2) y ventana de tiempo en la que aparece el texto.
    int track = kVideoTrackCount - 1;  // por defecto, la capa superior
    double start = 0.0;
    double duration = 5.0;

    double x = 340.0;
    double y = 300.0;
    double width = 600.0;
    double height = 120.0;

    int EffectiveTrack() const
    {
        if (track < 0) {
            return 0;
        }
        return track > kVideoTrackCount - 1 ? kVideoTrackCount - 1 : track;
    }

    double EffectiveDuration() const
    {
        if (duration < 0.1) {
            return 0.1;
        }
        return duration > 60.0 ? 60.0 : duration;
    }

    double End() const { return start + EffectiveDuration(); }

    bool IsActiveAt(double time) const
    {
        return time + 1e-6 >= start && time < End() - 1e-6;
    }

    int EffectiveFontSize() const
    {
        if (fontSize < 6) {
            return 6;
        }
        return fontSize > 240 ? 240 : fontSize;
    }

    // La caja siempre conserva un tamano minimo util y se mantiene dentro del
    // lienzo al moverla o redimensionarla.
    void Normalize()
    {
        if (width < 80.0) {
            width = 80.0;
        }
        if (height < 32.0) {
            height = 32.0;
        }
        if (width > 1280.0) {
            width = 1280.0;
        }
        if (height > 720.0) {
            height = 720.0;
        }
        if (x < 0.0) {
            x = 0.0;
        }
        if (y < 0.0) {
            y = 0.0;
        }
        if (x + width > 1280.0) {
            x = 1280.0 - width;
        }
        if (y + height > 720.0) {
            y = 720.0 - height;
        }
    }
};

// Etiqueta corta de la escala del clip: un solo porcentaje cuando conserva la
// proporcion y "ancho x alto" cuando se ha estirado algun borde.
inline std::wstring ScaleText(const ClipInfo& clip)
{
    const int sx = static_cast<int>(clip.EffectiveScaleX() * 100.0 + 0.5);
    const int sy = static_cast<int>(clip.EffectiveScaleY() * 100.0 + 0.5);
    if (sx == sy) {
        return std::to_wstring(sx) + L"%";
    }
    return std::to_wstring(sx) + L"% x " + std::to_wstring(sy) + L"%";
}

// Tipos de transicion disponibles entre dos clips de video.
enum class TransitionType : int {
    Crossfade = 0,  // disolvencia: el clip nuevo aparece sobre el anterior
    FadeBlack = 1,  // fundido a negro entre los dos clips
    SlideLeft = 2   // el clip nuevo entra deslizandose desde la derecha
};

// Transicion aplicada entre dos clips de video del timeline. Los clips se
// guardan por indice dentro del vector del proyecto.
struct TransitionInfo {
    int fromClip = -1;
    int toClip = -1;
    TransitionType type = TransitionType::Crossfade;
    double duration = 1.0;  // segundos

    double EffectiveDuration() const
    {
        if (duration < 0.1) {
            return 0.1;
        }
        return duration > 10.0 ? 10.0 : duration;
    }

    bool IsValid() const
    {
        return fromClip >= 0 && toClip >= 0 && fromClip != toClip;
    }
};

inline const wchar_t* TransitionName(TransitionType type)
{
    switch (type) {
    case TransitionType::FadeBlack:
        return L"Fundido a negro";
    case TransitionType::SlideLeft:
        return L"Deslizar a la izquierda";
    default:
        return L"Disolvencia";
    }
}

// Duracion total del proyecto: el final mas lejano de cualquier clip o texto.
inline double TimelineDuration(const std::vector<ClipInfo>& clips)
{
    double total = 0.0;
    for (const ClipInfo& clip : clips) {
        const double end = clip.End();
        if (end > total) {
            total = end;
        }
    }
    return total;
}

inline double TimelineDuration(const std::vector<ClipInfo>& clips,
                               const std::vector<TextInfo>& texts)
{
    double total = TimelineDuration(clips);
    for (const TextInfo& text : texts) {
        const double end = text.End();
        if (end > total) {
            total = end;
        }
    }
    return total;
}

// Ajustes globales del proyecto (solo formato de salida).
struct ExportSettings {
    int width = 1920;
    int height = 1080;
};

struct ExportRequest {
    std::vector<std::wstring> inputPaths;
    std::vector<double> inPoints;
    std::vector<double> outPoints;
    std::vector<bool> hasAudio;
    std::vector<double> speeds;
    std::vector<bool> negates;
    std::vector<bool> flipHs;
    std::vector<bool> flipVs;
    std::vector<int> tracks;
    std::vector<double> starts;
    std::vector<double> scales;
    std::vector<double> scaleXs;
    std::vector<double> scaleYs;
    std::vector<double> posXs;
    std::vector<double> posYs;
    std::vector<double> volumes;
    std::vector<TransitionInfo> transitions;

    // Textos del timeline. Cada uno lleva una PNG transparente para poder aplicar
    // su propia ventana de tiempo y su capa al componerlo con el filtro overlay.
    std::vector<TextInfo> texts;
    std::vector<std::wstring> textOverlayPaths;
    // Compatibilidad con exportaciones de una sola imagen con todos los textos.
    std::wstring textOverlayPath;

    ExportSettings settings;
    std::wstring outputPath;
    std::wstring ffmpegPath;
};
