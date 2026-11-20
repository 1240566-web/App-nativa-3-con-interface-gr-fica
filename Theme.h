#pragma once

#include <windows.h>

// Paleta y utilidades del tema oscuro de la aplicacion.
namespace Theme {

constexpr COLORREF kBackground = RGB(14, 15, 19);
constexpr COLORREF kPanel = RGB(23, 25, 31);
constexpr COLORREF kPanelAlt = RGB(32, 35, 43);
constexpr COLORREF kPanelHover = RGB(42, 46, 56);
constexpr COLORREF kBorder = RGB(52, 57, 68);
constexpr COLORREF kBorderSoft = RGB(38, 42, 51);
constexpr COLORREF kText = RGB(230, 233, 240);
constexpr COLORREF kTextDim = RGB(150, 156, 170);
constexpr COLORREF kTextFaint = RGB(108, 114, 128);
constexpr COLORREF kAccent = RGB(96, 140, 255);
constexpr COLORREF kAccentHover = RGB(124, 162, 255);
constexpr COLORREF kAccentPressed = RGB(72, 110, 214);
constexpr COLORREF kAccentSoft = RGB(48, 70, 128);
constexpr COLORREF kDanger = RGB(255, 122, 122);
constexpr COLORREF kOk = RGB(104, 210, 152);
constexpr COLORREF kCanvas = RGB(10, 11, 14);

// Activa el modo oscuro de los controles comunes (Windows 10/11). Debe
// llamarse antes de crear la ventana principal.
void ApplyDarkAppMode();

// Aplica el tema oscuro a una ventana y a todos sus hijos.
void EnableDarkControl(HWND hwnd);
void EnableDarkControls(HWND root);

// Pinta la barra de titulo de la ventana en oscuro.
void EnableDarkTitleBar(HWND hwnd);

} // namespace Theme
