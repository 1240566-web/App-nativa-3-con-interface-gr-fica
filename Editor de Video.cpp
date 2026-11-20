// Editor de Video.cpp
// Punto de entrada de Editor de Video (aplicacion MFC).
//
// La app usa libavformat para leer los metadatos de los clips y ffmpeg.exe
// (incluido con el build de FFmpeg) para renderizar el proyecto final.

#include <afxwin.h>

#include <shellapi.h>

#include <string>
#include <vector>

#include "MainFrame.h"
#include "Theme.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
                        "language='*'\"")

class CVideoEditorApp : public CWinApp {
public:
    BOOL InitInstance() override;
};

CVideoEditorApp theApp;

BOOL CVideoEditorApp::InitInstance()
{
    // Escalado nativo en pantallas de alta densidad.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS |
                           ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&commonControls);

    CWinApp::InitInstance();

    // Modo oscuro global antes de crear la ventana principal.
    Theme::ApplyDarkAppMode();

    auto* frame = new CMainFrame();
    if (!frame->Create(nullptr, L"Editor de Video", WS_OVERLAPPEDWINDOW,
                       CRect(0, 0, 1200, 800))) {
        delete frame;
        return FALSE;
    }

    // Abre la ventana ocupando casi toda el area de trabajo, centrada.
    RECT work{};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) != FALSE) {
        const int workWidth = work.right - work.left;
        const int workHeight = work.bottom - work.top;

        int width = workWidth * 94 / 100;
        int height = workHeight * 94 / 100;
        if (width < 1200) {
            width = (workWidth > 1200) ? 1200 : workWidth;
        }
        if (height < 760) {
            height = (workHeight > 760) ? 760 : workHeight;
        }

        const int x = work.left + (workWidth - width) / 2;
        const int y = work.top + (workHeight - height) / 2;

        frame->SetWindowPos(nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    m_pMainWnd = frame;
    frame->ShowWindow(SW_SHOW);
    frame->UpdateWindow();

    // Archivos pasados por linea de comandos ("Abrir con").
    int argumentCount = 0;
    LPWSTR* arguments = ::CommandLineToArgvW(::GetCommandLineW(), &argumentCount);
    if (arguments != nullptr) {
        std::vector<std::wstring> files;
        for (int i = 1; i < argumentCount; ++i) {
            const std::wstring candidate = arguments[i];
            if (!candidate.empty() && candidate[0] != L'-') {
                files.push_back(candidate);
            }
        }
        ::LocalFree(arguments);

        if (!files.empty()) {
            frame->AddInitialFiles(files);
        }
    }

    return TRUE;
}
