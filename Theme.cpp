#include "Theme.h"

#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace {

// Ordinales no documentados de uxtheme usados por el modo oscuro de Windows.
using SetPreferredAppModeFn = int(WINAPI*)(int);
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
using FlushMenuThemesFn = void(WINAPI*)();

constexpr int kForceDark = 2;

AllowDarkModeForWindowFn g_allowDarkMode = nullptr;

} // namespace

namespace Theme {

void ApplyDarkAppMode()
{
    HMODULE uxtheme = ::LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (uxtheme == nullptr) {
        return;
    }

    const auto setPreferred = reinterpret_cast<SetPreferredAppModeFn>(
        ::GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    const auto flushMenus = reinterpret_cast<FlushMenuThemesFn>(
        ::GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    g_allowDarkMode = reinterpret_cast<AllowDarkModeForWindowFn>(
        ::GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)));

    if (setPreferred != nullptr) {
        setPreferred(kForceDark);
    }
    if (flushMenus != nullptr) {
        flushMenus();
    }
}

void EnableDarkControl(HWND hwnd)
{
    if (hwnd == nullptr || !::IsWindow(hwnd)) {
        return;
    }

    if (g_allowDarkMode != nullptr) {
        g_allowDarkMode(hwnd, TRUE);
    }

    wchar_t className[64]{};
    ::GetClassNameW(hwnd, className, _countof(className));
    if (_wcsicmp(className, L"SysHeader32") == 0) {
        ::SetWindowTheme(hwnd, L"DarkMode_ItemsView", nullptr);
    }
    else if (_wcsicmp(className, L"ComboBox") == 0) {
        // Tema oscuro especifico de los cuadros combinados de Explorer.
        ::SetWindowTheme(hwnd, L"DarkMode_CFD", nullptr);
    }
    else {
        ::SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    }

    ::SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
}

void EnableDarkControls(HWND root)
{
    if (root == nullptr) {
        return;
    }

    EnableDarkControl(root);
    ::EnumChildWindows(
        root,
        [](HWND child, LPARAM) -> BOOL {
            EnableDarkControl(child);
            return TRUE;
        },
        0);
}

void EnableDarkTitleBar(HWND hwnd)
{
    if (hwnd == nullptr) {
        return;
    }

    const BOOL dark = TRUE;
    if (FAILED(::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
        ::DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
}

} // namespace Theme
