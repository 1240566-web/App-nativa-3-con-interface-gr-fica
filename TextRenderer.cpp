#include "TextRenderer.h"

#include <gdiplus.h>

#include <algorithm>
#include <cstdio>
#include <mutex>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace {

constexpr double kCanvasWidth = 1280.0;
constexpr double kCanvasHeight = 720.0;

std::once_flag g_gdiplusOnce;
ULONG_PTR g_gdiplusToken = 0;

void EnsureGdiplus()
{
    std::call_once(g_gdiplusOnce, [] {
        GdiplusStartupInput input;
        GdiplusStartup(&g_gdiplusToken, &input, nullptr);
    });
}

struct DrawItem {
    std::wstring word;
    REAL x = 0.0f;
    REAL y = 0.0f;
};

struct TextLine {
    std::vector<std::wstring> words;
    std::vector<REAL> widths;
    bool justify = false;
};

std::vector<std::wstring> SplitParagraphs(const std::wstring& text)
{
    std::vector<std::wstring> result;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'\n') {
            std::wstring line = text.substr(start, i - start);
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            result.push_back(line);
            start = i + 1;
        }
    }
    return result;
}

std::vector<std::wstring> SplitWords(const std::wstring& text)
{
    std::vector<std::wstring> result;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == L' ' || text[i] == L'\t')) {
            ++i;
        }
        const size_t start = i;
        while (i < text.size() && text[i] != L' ' && text[i] != L'\t') {
            ++i;
        }
        if (i > start) {
            result.push_back(text.substr(start, i - start));
        }
    }
    return result;
}

int StyleOf(const TextInfo& text)
{
    int style = FontStyleRegular;
    if (text.bold) {
        style |= FontStyleBold;
    }
    if (text.italic) {
        style |= FontStyleItalic;
    }
    if (text.underline) {
        style |= FontStyleUnderline;
    }
    return style;
}

REAL MeasureWidth(Graphics& graphics, const std::wstring& text, Font& font,
                  const StringFormat& format)
{
    if (text.empty()) {
        return 0.0f;
    }
    RectF box;
    graphics.MeasureString(text.c_str(), static_cast<INT>(text.size()), &font,
                           PointF(0.0f, 0.0f), &format, &box);
    return box.Width;
}

void DrawOne(Graphics& graphics, const TextInfo& text, REAL scaleX, REAL scaleY)
{
    const REAL bx = static_cast<REAL>(text.x * scaleX);
    const REAL by = static_cast<REAL>(text.y * scaleY);
    const REAL bw = static_cast<REAL>(text.width * scaleX);
    const REAL bh = static_cast<REAL>(text.height * scaleY);
    if (bw <= 1.0f || bh <= 1.0f) {
        return;
    }

    const REAL fontPx = static_cast<REAL>(text.EffectiveFontSize()) * static_cast<REAL>(scaleY);
    FontFamily family(L"Segoe UI");
    Font font(&family, fontPx, static_cast<FontStyle>(StyleOf(text)), UnitPixel);

    StringFormat format;
    format.SetAlignment(StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentNear);
    format.SetFormatFlags(StringFormatFlagsNoWrap);

    const REAL pad = (std::max)(2.0f, fontPx * 0.10f);
    const REAL available = (std::max)(1.0f, bw - pad * 2.0f);
    const REAL spaceW = MeasureWidth(graphics, L" ", font, format);
    const REAL lineHeight = (std::max)(1.0f, font.GetHeight(&graphics) * 1.16f);

    std::vector<TextLine> lines;
    for (const std::wstring& paragraph : SplitParagraphs(text.text)) {
        const std::vector<std::wstring> words = SplitWords(paragraph);
        if (words.empty()) {
            lines.push_back(TextLine{});
            continue;
        }

        TextLine current;
        REAL currentWidth = 0.0f;
        for (const std::wstring& word : words) {
            const REAL wordWidth = MeasureWidth(graphics, word, font, format);
            if (!current.words.empty() &&
                currentWidth + spaceW + wordWidth > available) {
                current.justify = true;
                lines.push_back(current);
                current = TextLine{};
                currentWidth = 0.0f;
            }
            if (!current.words.empty()) {
                currentWidth += spaceW;
            }
            current.words.push_back(word);
            current.widths.push_back(wordWidth);
            currentWidth += wordWidth;
        }
        current.justify = false;  // la ultima linea de un parrafo no se justifica
        lines.push_back(current);
    }

    // Centrado vertical dentro de la caja.
    const REAL totalHeight = static_cast<REAL>(lines.size()) * lineHeight;
    REAL y = by + (totalHeight < bh ? (bh - totalHeight) * 0.5f : pad * 0.5f);

    std::vector<DrawItem> items;
    for (const TextLine& line : lines) {
        if (line.words.empty()) {
            y += lineHeight;
            continue;
        }

        REAL wordsWidth = 0.0f;
        for (const REAL width : line.widths) {
            wordsWidth += width;
        }
        const size_t count = line.words.size();
        const REAL naturalWidth =
            wordsWidth + static_cast<REAL>(count - 1) * spaceW;

        REAL x = bx + pad;
        REAL gap = spaceW;
        switch (text.align) {
        case TextAlign::Center:
            x = bx + (bw - naturalWidth) * 0.5f;
            break;
        case TextAlign::Right:
            x = bx + bw - pad - naturalWidth;
            break;
        case TextAlign::Justify:
            if (line.justify && count > 1) {
                gap = (available - wordsWidth) / static_cast<REAL>(count - 1);
            }
            break;
        case TextAlign::Left:
        default:
            break;
        }

        for (size_t i = 0; i < count; ++i) {
            items.push_back({ line.words[i], x, y });
            x += line.widths[i] + gap;
        }
        y += lineHeight;
    }

    // Sombra suave para que el texto se lea sobre cualquier video.
    const REAL shadow = (std::max)(1.0f, fontPx * 0.055f);
    graphics.SetClip(RectF(bx, by, bw, bh));
    SolidBrush shadowBrush(Color(170, 0, 0, 0));
    for (const DrawItem& item : items) {
        graphics.DrawString(item.word.c_str(), static_cast<INT>(item.word.size()), &font,
                            PointF(item.x + shadow, item.y + shadow), &format, &shadowBrush);
    }

    SolidBrush textBrush(Color(255, 255, 255, 255));
    for (const DrawItem& item : items) {
        graphics.DrawString(item.word.c_str(), static_cast<INT>(item.word.size()), &font,
                            PointF(item.x, item.y), &format, &textBrush);
    }
    graphics.ResetClip();
}

void DrawTextsCore(Graphics& graphics, const std::vector<TextInfo>& texts,
                   double scaleX, double scaleY)
{
    graphics.SetCompositingMode(CompositingModeSourceOver);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);

    for (const TextInfo& text : texts) {
        if (!text.text.empty()) {
            DrawOne(graphics, text, static_cast<REAL>(scaleX), static_cast<REAL>(scaleY));
        }
    }
}

int GetEncoderClsid(const WCHAR* format, CLSID* clsid)
{
    UINT count = 0;
    UINT size = 0;
    if (GetImageEncodersSize(&count, &size) != Ok || count == 0 || size == 0) {
        return -1;
    }

    std::vector<BYTE> buffer(size);
    auto* encoders = reinterpret_cast<ImageCodecInfo*>(buffer.data());
    if (GetImageEncoders(count, size, encoders) != Ok) {
        return -1;
    }

    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(encoders[i].MimeType, format) == 0) {
            *clsid = encoders[i].Clsid;
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

void DrawTextsBgra(uint8_t* pixels, int width, int height,
                   const std::vector<TextInfo>& texts)
{
    if (pixels == nullptr || width <= 0 || height <= 0 || texts.empty()) {
        return;
    }

    EnsureGdiplus();
    Bitmap canvas(width, height, static_cast<INT>(width) * 4, PixelFormat32bppARGB, pixels);
    if (canvas.GetLastStatus() != Ok) {
        return;
    }
    Graphics graphics(&canvas);
    if (graphics.GetLastStatus() != Ok) {
        return;
    }

    DrawTextsCore(graphics, texts,
                  static_cast<double>(width) / kCanvasWidth,
                  static_cast<double>(height) / kCanvasHeight);
}

bool SaveTextOverlayBgra(const std::vector<TextInfo>& texts, int width, int height,
                         const std::wstring& path)
{
    if (texts.empty() || width <= 0 || height <= 0 || path.empty()) {
        return false;
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
    DrawTextsBgra(pixels.data(), width, height, texts);

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) {
        return false;
    }
    const size_t written = fwrite(pixels.data(), 1, pixels.size(), file);
    const int closed = fclose(file);
    return written == pixels.size() && closed == 0;
}

bool SaveTextOverlayPng(const std::vector<TextInfo>& texts, int width, int height,
                        const std::wstring& path)
{
    if (texts.empty() || width <= 0 || height <= 0 || path.empty()) {
        return false;
    }

    EnsureGdiplus();
    Bitmap canvas(width, height, PixelFormat32bppARGB);
    if (canvas.GetLastStatus() != Ok) {
        return false;
    }
    {
        Graphics graphics(&canvas);
        if (graphics.GetLastStatus() != Ok) {
            return false;
        }
        graphics.Clear(Color(0, 0, 0, 0));
        DrawTextsCore(graphics, texts,
                      static_cast<double>(width) / kCanvasWidth,
                      static_cast<double>(height) / kCanvasHeight);
    }

    CLSID pngClsid{};
    if (GetEncoderClsid(L"image/png", &pngClsid) < 0) {
        return false;
    }
    return canvas.Save(path.c_str(), &pngClsid, nullptr) == Ok;
}
