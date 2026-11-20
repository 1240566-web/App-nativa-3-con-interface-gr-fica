#include "MainFrame.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <string>
#include <vector>

#include <shellapi.h>

#include "FfmpegExporter.h"
#include "MediaProbe.h"
#include "PreviewAudio.h"
#include "TextRenderer.h"
#include "Theme.h"

#pragma comment(lib, "shell32.lib")

namespace {

constexpr UINT_PTR kPlayTimer = 1;
constexpr int kOutputFps = 30;

std::wstring FormatDuration(double seconds)
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int total = static_cast<int>(seconds + 0.5);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%02d:%02d:%02d", total / 3600, (total % 3600) / 60, total % 60);
    return buffer;
}

std::wstring FormatTimecode(double seconds, int fps)
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    if (fps < 1) {
        fps = 30;
    }

    int totalFrames = static_cast<int>(std::floor(seconds * fps + 0.5));
    if (totalFrames < 0) {
        totalFrames = 0;
    }

    const int frames = totalFrames % fps;
    const int totalSeconds = totalFrames / fps;

    wchar_t buffer[40]{};
    swprintf_s(buffer, L"%02d:%02d:%02d:%02d", totalSeconds / 3600, (totalSeconds % 3600) / 60,
               totalSeconds % 60, frames);
    return buffer;
}

std::wstring FormatResolution(const ClipInfo& clip)
{
    wchar_t buffer[48]{};
    if (clip.width > 0 && clip.height > 0) {
        if (clip.fps > 0.0) {
            swprintf_s(buffer, L"%dx%d %.0f", clip.width, clip.height, clip.fps);
        }
        else {
            swprintf_s(buffer, L"%dx%d", clip.width, clip.height);
        }
    }
    return buffer;
}

int EvenDimension(int value)
{
    if (value < 2) {
        return 2;
    }
    return (value / 2) * 2;
}

std::wstring FormatSpeed(double speed)
{
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%.4gx", speed);
    return buffer;
}

std::wstring FormatClockShort(double seconds)
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int minutes = static_cast<int>(seconds) / 60;
    const double rest = seconds - minutes * 60.0;
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%d:%04.1f", minutes, rest);
    return buffer;
}

std::wstring TrackName(int track)
{
    if (track == kAudioTrack) {
        return L"Audio";
    }
    wchar_t buffer[24]{};
    swprintf_s(buffer, L"Capa %d", track + 1);
    return buffer;
}

// Imán para mover clips: al acercarse a un borde o al centro del lienzo el
// centro del clip queda alineado. Una vez alineado se mantiene mientras el
// cursor siga dentro del margen de liberacion; si el usuario continua
// arrastrando hacia fuera, se suelta y puede engancharse en otro borde/esquina.
double SnapAxis(double raw, double extent, double size,
                bool& active, int& kind, double& snapValue,
                double snapThreshold, double escapeThreshold)
{
    if (active && std::fabs(raw - snapValue) <= escapeThreshold) {
        return snapValue;
    }
    active = false;
    kind = -1;

    // 0 = borde inicial (izquierda/arriba), 1 = borde final, 2 = centro.
    const double candidates[3] = {size * 0.5, extent - size * 0.5, extent * 0.5};
    double best = raw;
    int bestKind = -1;
    double bestDistance = snapThreshold + 1.0;
    for (int i = 0; i < 3; ++i) {
        const double distance = std::fabs(raw - candidates[i]);
        if (distance <= snapThreshold && distance < bestDistance) {
            bestDistance = distance;
            best = candidates[i];
            bestKind = i;
        }
    }
    if (bestKind >= 0) {
        active = true;
        kind = bestKind;
        snapValue = best;
        return best;
    }
    return raw;
}

std::wstring SnapDescription(int xKind, int yKind)
{
    if (xKind == 0 && yKind == 0) {
        return L"la esquina superior izquierda";
    }
    if (xKind == 1 && yKind == 0) {
        return L"la esquina superior derecha";
    }
    if (xKind == 0 && yKind == 1) {
        return L"la esquina inferior izquierda";
    }
    if (xKind == 1 && yKind == 1) {
        return L"la esquina inferior derecha";
    }
    if (xKind == 0) {
        return L"el borde izquierdo";
    }
    if (xKind == 1) {
        return L"el borde derecho";
    }
    if (yKind == 0) {
        return L"el borde superior";
    }
    if (yKind == 1) {
        return L"el borde inferior";
    }
    return L"el centro del lienzo";
}

} // namespace

BEGIN_MESSAGE_MAP(CDarkTabCtrl, CTabCtrl)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
END_MESSAGE_MAP()

void CDarkTabCtrl::OnPaint()
{
    CPaintDC dc(this);

    CRect client;
    GetClientRect(&client);

    const int dpi = dc.GetDeviceCaps(LOGPIXELSY);
    const auto px = [dpi](int value) { return MulDiv(value, dpi, 96); };

    // El panel ocupa toda la barra lateral; la tira de lenguetas se recorta
    // arriba para diferenciarla del area de la pestana.
    dc.FillSolidRect(&client, Theme::kPanel);

    const int count = GetItemCount();
    const int stripBottom = StripBottom(dpi);
    dc.FillSolidRect(client.left, client.top, client.Width(), stripBottom, Theme::kBackground);
    dc.FillSolidRect(client.left, client.top + stripBottom, client.Width(), 1, Theme::kBorderSoft);
    dc.FillSolidRect(client.left, client.top + stripBottom + 1, client.Width(),
                     client.Height() - stripBottom - 1, Theme::kPanel);

    CFont* font = GetFont();
    CFont* oldFont = font != nullptr ? dc.SelectObject(font) : nullptr;
    dc.SetBkMode(TRANSPARENT);

    for (int i = 0; i < count; ++i) {
        CRect item;
        if (!GetItemRect(i, &item)) {
            continue;
        }

        CRect pill = item;
        pill.DeflateRect(px(2), px(3));
        if (pill.Height() < px(16)) {
            pill.top = item.top + px(2);
        }

        const bool selected = GetCurSel() == i;
        const bool hover = !selected && m_hover == i;

        COLORREF fill = Theme::kBackground;
        COLORREF border = Theme::kBorderSoft;
        COLORREF text = Theme::kTextDim;
        if (selected) {
            fill = Theme::kAccentSoft;
            border = Theme::kAccent;
            text = Theme::kText;
        }
        else if (hover) {
            fill = Theme::kPanelAlt;
            border = Theme::kBorder;
            text = Theme::kText;
        }

        CBrush brush(fill);
        CPen pen(PS_SOLID, 1, border);
        CBrush* oldBrush = dc.SelectObject(&brush);
        CPen* oldPen = dc.SelectObject(&pen);
        const int radius = px(8);
        dc.RoundRect(pill, CPoint(radius, radius));
        dc.SelectObject(oldPen);
        dc.SelectObject(oldBrush);

        if (selected) {
            dc.FillSolidRect(pill.left + px(7), pill.bottom - px(2),
                             pill.Width() - px(14), px(2), Theme::kAccent);
        }

        wchar_t caption[128]{};
        TCITEMW tabItem{};
        tabItem.mask = TCIF_TEXT;
        tabItem.pszText = caption;
        tabItem.cchTextMax = _countof(caption);
        GetItem(i, &tabItem);

        dc.SetTextColor(text);
        CRect textRect = pill;
        dc.DrawTextW(caption, &textRect,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (oldFont != nullptr) {
        dc.SelectObject(oldFont);
    }
}

BOOL CDarkTabCtrl::OnEraseBkgnd(CDC* /*pDC*/)
{
    // OnPaint pinta toda la superficie; evitamos el parpadeo del fondo claro.
    return TRUE;
}

int CDarkTabCtrl::StripBottom(int dpi) const
{
    const auto px = [dpi](int value) { return MulDiv(value, dpi, 96); };

    // La tira termina donde acaba la lengueta mas alta, con un pequeno margen.
    int stripBottom = px(30);
    for (int i = 0; i < GetItemCount(); ++i) {
        CRect item;
        if (GetItemRect(i, &item) && item.bottom > stripBottom) {
            stripBottom = item.bottom;
        }
    }
    return stripBottom + px(4);
}

// Repinta solo la franja de lenguetas. Invalidar la barra completa hacia que
// el control se pintara sobre los controles de la pagina (hermanos suyos).
void CDarkTabCtrl::InvalidateStrip()
{
    if (GetSafeHwnd() == nullptr) {
        return;
    }

    CClientDC dc(this);
    CRect strip;
    GetClientRect(&strip);
    strip.bottom = strip.top + StripBottom(dc.GetDeviceCaps(LOGPIXELSY));
    InvalidateRect(&strip, FALSE);
}

void CDarkTabCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
    TCHITTESTINFO info{};
    info.pt = point;
    const int index = HitTest(&info);

    if (index != m_hover) {
        m_hover = index;
        InvalidateStrip();
    }

    TRACKMOUSEEVENT track{};
    track.cbSize = sizeof(track);
    track.dwFlags = TME_LEAVE;
    track.hwndTrack = GetSafeHwnd();
    ::TrackMouseEvent(&track);

    CTabCtrl::OnMouseMove(nFlags, point);
}

void CDarkTabCtrl::OnMouseLeave()
{
    if (m_hover != -1) {
        m_hover = -1;
        InvalidateStrip();
    }
    CWnd::OnMouseLeave();
}

BEGIN_MESSAGE_MAP(CDarkCheckBox, CButton)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
END_MESSAGE_MAP()

void CDarkCheckBox::OnPaint()
{
    CPaintDC dc(this);

    CRect client;
    GetClientRect(&client);
    dc.FillSolidRect(&client, Theme::kBackground);

    const int dpi = dc.GetDeviceCaps(LOGPIXELSY);
    const auto px = [dpi](int value) { return MulDiv(value, dpi, 96); };

    const bool disabled = IsWindowEnabled() == FALSE;
    const bool checked = GetCheck() == BST_CHECKED;
    const bool pressed = (GetState() & BST_PUSHED) != 0;

    const int boxSize = px(17);
    CRect box(client.left, client.top + (client.Height() - boxSize) / 2,
              client.left + boxSize, client.top + (client.Height() - boxSize) / 2 + boxSize);

    COLORREF boxFill = checked ? Theme::kAccent : Theme::kPanelAlt;
    COLORREF boxBorder = checked ? Theme::kAccentHover : Theme::kBorder;

    if (disabled) {
        boxFill = RGB(28, 30, 36);
        boxBorder = Theme::kBorderSoft;
    }
    else if (checked) {
        boxFill = pressed ? Theme::kAccentPressed : (m_hover ? Theme::kAccentHover : Theme::kAccent);
    }
    else if (pressed) {
        boxFill = Theme::kPanelHover;
    }
    else if (m_hover) {
        boxBorder = Theme::kTextFaint;
    }

    CBrush boxBrush(boxFill);
    CPen boxPen(PS_SOLID, 1, boxBorder);
    CBrush* oldBrush = dc.SelectObject(&boxBrush);
    CPen* oldPen = dc.SelectObject(&boxPen);
    const int radius = px(4);
    dc.RoundRect(box, CPoint(radius, radius));
    dc.SelectObject(oldPen);
    dc.SelectObject(oldBrush);

    if (checked) {
        CPen tickPen(PS_SOLID, px(2), disabled ? Theme::kTextFaint : RGB(255, 255, 255));
        CPen* oldTick = dc.SelectObject(&tickPen);
        const int step = (std::max)(1, boxSize / 6);
        dc.MoveTo(box.left + step, box.top + boxSize / 2);
        dc.LineTo(box.left + boxSize / 2 - step / 2, box.bottom - step * 2);
        dc.LineTo(box.right - step, box.top + step * 2);
        dc.SelectObject(oldTick);
    }

    if ((GetFocus() == this) && !disabled) {
        CRect focus = client;
        focus.DeflateRect(px(1), px(1));
        dc.DrawFocusRect(&focus);
    }

    wchar_t caption[256]{};
    GetWindowTextW(caption, _countof(caption));

    CFont* font = GetFont();
    CFont* oldFont = font != nullptr ? dc.SelectObject(font) : nullptr;

    CRect textRect = client;
    textRect.left = box.right + px(10);

    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(disabled ? Theme::kTextFaint : Theme::kText);
    dc.DrawTextW(caption, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (oldFont != nullptr) {
        dc.SelectObject(oldFont);
    }
}

BOOL CDarkCheckBox::OnEraseBkgnd(CDC* /*pDC*/)
{
    return TRUE;
}

void CDarkCheckBox::OnMouseMove(UINT nFlags, CPoint point)
{
    if (!m_hover) {
        m_hover = true;
        Invalidate(FALSE);
    }

    TRACKMOUSEEVENT track{};
    track.cbSize = sizeof(track);
    track.dwFlags = TME_LEAVE;
    track.hwndTrack = GetSafeHwnd();
    ::TrackMouseEvent(&track);

    CButton::OnMouseMove(nFlags, point);
}

void CDarkCheckBox::OnMouseLeave()
{
    if (m_hover) {
        m_hover = false;
        Invalidate(FALSE);
    }
    CWnd::OnMouseLeave();
}

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_DROPFILES()
    ON_WM_CTLCOLOR()
    ON_WM_ERASEBKGND()
    ON_WM_DRAWITEM()
    ON_WM_TIMER()
    ON_WM_HSCROLL()
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_BTN_ADD, &CMainFrame::OnAddFiles)
    ON_BN_CLICKED(IDC_BTN_REMOVE, &CMainFrame::OnRemoveClip)
    ON_BN_CLICKED(IDC_BTN_UP, &CMainFrame::OnMoveUp)
    ON_BN_CLICKED(IDC_BTN_DOWN, &CMainFrame::OnMoveDown)
    ON_BN_CLICKED(IDC_BTN_SPLIT, &CMainFrame::OnSplit)
    ON_BN_CLICKED(IDC_BTN_UNDO, &CMainFrame::OnUndo)
    ON_BN_CLICKED(IDC_BTN_REDO, &CMainFrame::OnRedo)
    ON_BN_CLICKED(IDC_BTN_CENTER, &CMainFrame::OnCenterClip)
    ON_BN_CLICKED(IDC_CHK_NEGATE, &CMainFrame::OnFilterChanged)
    ON_BN_CLICKED(IDC_CHK_FLIP_H, &CMainFrame::OnFilterChanged)
    ON_BN_CLICKED(IDC_CHK_FLIP_V, &CMainFrame::OnFilterChanged)
    ON_BN_CLICKED(IDC_BTN_PREV, &CMainFrame::OnPrevFrame)
    ON_BN_CLICKED(IDC_BTN_NEXT, &CMainFrame::OnNextFrame)
    ON_BN_CLICKED(IDC_BTN_PLAY, &CMainFrame::OnPlayPause)
    ON_BN_CLICKED(IDC_BTN_EXPORT, &CMainFrame::OnExport)
    ON_BN_CLICKED(IDC_BTN_CANCEL, &CMainFrame::OnCancelExport)
    ON_BN_CLICKED(IDC_BTN_TRANS_ADD, &CMainFrame::OnAddTransition)
    ON_BN_CLICKED(IDC_BTN_TRANS_REMOVE, &CMainFrame::OnRemoveTransition)
    ON_NOTIFY(TCN_SELCHANGE, IDC_TRANS_DIRECTION_TABS, &CMainFrame::OnTransDirectionChanged)
    ON_BN_CLICKED(IDC_TXT_ADD, &CMainFrame::OnTextAdd)
    ON_BN_CLICKED(IDC_TXT_REMOVE, &CMainFrame::OnTextRemove)
    ON_BN_CLICKED(IDC_TXT_BOLD, &CMainFrame::OnTextStyleChanged)
    ON_BN_CLICKED(IDC_TXT_ITALIC, &CMainFrame::OnTextStyleChanged)
    ON_BN_CLICKED(IDC_TXT_UNDERLINE, &CMainFrame::OnTextStyleChanged)
    ON_EN_CHANGE(IDC_TXT_CONTENT, &CMainFrame::OnTextContentChanged)
    ON_EN_SETFOCUS(IDC_TXT_CONTENT, &CMainFrame::OnTextEditFocus)
    ON_EN_SETFOCUS(IDC_TXT_SIZE, &CMainFrame::OnTextEditFocus)
    ON_EN_CHANGE(IDC_TXT_SIZE, &CMainFrame::OnTextSizeChanged)
    ON_CBN_SELCHANGE(IDC_TXT_ALIGN_COMBO, &CMainFrame::OnTextAlignChanged)
    ON_CBN_SELCHANGE(IDC_TXT_TRACK_COMBO, &CMainFrame::OnTextTrackChanged)
    ON_LBN_SELCHANGE(IDC_TXT_LIST, &CMainFrame::OnTextListSelChange)
    ON_CBN_SELCHANGE(IDC_SPEED_COMBO, &CMainFrame::OnSpeedChanged)
    ON_CBN_SELCHANGE(IDC_TRACK_COMBO, &CMainFrame::OnTrackChanged)
    ON_MESSAGE(LN_SELCHANGED, &CMainFrame::OnLibrarySelectionChanged)
    ON_MESSAGE(LN_ADD, &CMainFrame::OnLibraryAdd)
    ON_MESSAGE(LN_DOUBLECLICK, &CMainFrame::OnLibraryDoubleClick)
    ON_NOTIFY(TCN_SELCHANGE, IDC_TABS, &CMainFrame::OnTabChanged)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_TABS, &CMainFrame::OnTabsCustomDraw)
    ON_MESSAGE(WM_EXPORT_PROGRESS, &CMainFrame::OnExportProgress)
    ON_MESSAGE(WM_EXPORT_FINISHED, &CMainFrame::OnExportFinished)
END_MESSAGE_MAP()

CMainFrame::CMainFrame() = default;

CMainFrame::~CMainFrame()
{
    JoinExportThread();
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CFrameWnd::PreCreateWindow(cs)) {
        return FALSE;
    }

    cs.dwExStyle &= ~WS_EX_CLIENTEDGE;
    cs.lpszClass = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    return TRUE;
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) {
        return -1;
    }

    m_background.CreateSolidBrush(Theme::kBackground);
    m_panelBrush.CreateSolidBrush(Theme::kPanel);

    CreateFonts();
    CreateControls();
    ApplyFonts();

    // Tema oscuro: barra de titulo, controles comunes y barra de estado.
    Theme::EnableDarkTitleBar(GetSafeHwnd());
    Theme::EnableDarkControls(GetSafeHwnd());

    DragAcceptFiles(TRUE);
    SetWindowText(L"Editor de Video");
    SetStatus(L"Agrega videos a la biblioteca y pulsa + para colocarlos en una capa.");

    m_libraryView.SetItems(&m_library);
    m_libraryView.SetTimelineCount(0);
    m_previewView.GetSelectionRect = [this](double& left, double& top, double& right,
                                            double& bottom) {
        return GetSelectionCanvasRect(left, top, right, bottom);
    };

    m_timeline.Attach(&m_clips);
    m_timeline.AttachTransitions(&m_transitions);
    m_timeline.AttachTexts(&m_texts);
    SyncClipControls();
    UpdateTimeLabel();
    UpdateTransitionTarget();
    UpdateTransitionList();
    ShowTabPage(m_tabIndex);
    UpdateUndoButtons();

    return 0;
}

CMainFrame::ProjectSnapshot CMainFrame::CaptureSnapshot() const
{
    ProjectSnapshot snapshot;
    snapshot.clips = m_clips;
    snapshot.transitions = m_transitions;
    snapshot.texts = m_texts;
    snapshot.selection = m_selection;
    snapshot.textSelection = m_textSelection;
    snapshot.playhead = m_preview.IsOpen() ? m_preview.CurrentTime() : m_timeline.Playhead();
    return snapshot;
}

void CMainFrame::CommitUndo(ProjectSnapshot before)
{
    if (m_exporting) {
        return;
    }
    m_undoStack.push_back(std::move(before));
    if (m_undoStack.size() > kMaxUndoSteps) {
        m_undoStack.erase(m_undoStack.begin());
    }
    // Una accion nueva descarta el historial de "rehacer".
    m_redoStack.clear();
    UpdateUndoButtons();
}

void CMainFrame::PushUndo()
{
    CommitUndo(CaptureSnapshot());
}

void CMainFrame::UpdateUndoButtons()
{
    const BOOL canUndo = (!m_exporting && !m_undoStack.empty()) ? TRUE : FALSE;
    const BOOL canRedo = (!m_exporting && !m_redoStack.empty()) ? TRUE : FALSE;
    if (m_btnUndo.GetSafeHwnd() != nullptr) {
        m_btnUndo.EnableWindow(canUndo);
    }
    if (m_btnRedo.GetSafeHwnd() != nullptr) {
        m_btnRedo.EnableWindow(canRedo);
    }
}

void CMainFrame::RestoreSnapshot(const ProjectSnapshot& snapshot, const wchar_t* action)
{
    StopPlayback();

    m_clips = snapshot.clips;
    m_transitions = snapshot.transitions;
    m_texts = snapshot.texts;
    m_selection = snapshot.selection;
    if (m_selection >= static_cast<int>(m_clips.size())) {
        m_selection = m_clips.empty() ? -1 : static_cast<int>(m_clips.size()) - 1;
    }
    m_textSelection = snapshot.textSelection;
    if (m_textSelection >= static_cast<int>(m_texts.size())) {
        m_textSelection = m_texts.empty() ? -1 : static_cast<int>(m_texts.size()) - 1;
    }

    UpdateTextList(m_textSelection);
    SyncTextControls();
    m_preview.SetTexts(m_texts);
    m_previewView.Refresh();

    UpdateTotalLabel();
    UpdateLibrary();
    UpdateTransitionTarget();
    UpdateTransitionList();
    SyncClipControls();
    m_timeline.SetSelection(m_selection, false);
    m_timeline.SetTextSelection(m_textSelection, false);
    if (m_selection >= 0 && m_selection < static_cast<int>(m_clips.size())) {
        m_timeline.SetSourceFps(m_clips[static_cast<size_t>(m_selection)].fps);
    }

    RebuildPreview(snapshot.playhead, m_clips.empty() && m_texts.empty());
    UpdateTimeLabel();
    UpdatePlayButton();
    SetStatus(action);
    UpdateUndoButtons();
}

void CMainFrame::OnUndo()
{
    if (m_exporting || m_undoStack.empty()) {
        return;
    }

    ProjectSnapshot current = CaptureSnapshot();
    ProjectSnapshot target = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    m_redoStack.push_back(std::move(current));
    RestoreSnapshot(target, L"Deshecho.");
}

void CMainFrame::OnRedo()
{
    if (m_exporting || m_redoStack.empty()) {
        return;
    }

    ProjectSnapshot current = CaptureSnapshot();
    ProjectSnapshot target = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    m_undoStack.push_back(std::move(current));
    RestoreSnapshot(target, L"Rehecho.");
}

void CMainFrame::CreateFonts()
{
    CClientDC dc(this);
    const int dpi = dc.GetDeviceCaps(LOGPIXELSY);

    const auto makeFont = [dpi](CFont& font, const wchar_t* face, int points, int weight) {
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(points, dpi, 72);
        lf.lfWeight = weight;
        lf.lfQuality = CLEARTYPE_QUALITY;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, _countof(lf.lfFaceName), face);
        font.CreateFontIndirect(&lf);
    };

    makeFont(m_fontUi, L"Segoe UI", 9, FW_NORMAL);
    makeFont(m_fontBold, L"Segoe UI", 9, FW_SEMIBOLD);
    makeFont(m_fontSection, L"Segoe UI", 10, FW_SEMIBOLD);
    makeFont(m_fontTitle, L"Segoe UI", 13, FW_SEMIBOLD);
    makeFont(m_fontMono, L"Consolas", 9, FW_NORMAL);
}

void CMainFrame::CreateControls()
{
    const CRect empty(0, 0, 10, 10);

    m_title.Create(L"Editor de Video", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_APP_TITLE);
    m_total.Create(L"Duracion: 00:00:00", WS_CHILD | WS_VISIBLE | SS_RIGHT, empty, this, IDC_TOTAL_LABEL);

    m_tabs.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS | TCS_FIXEDWIDTH,
                  empty, this, IDC_TABS);
    m_tabs.InsertItem(0, L"Clips");
    m_tabs.InsertItem(1, L"Ajustes");
    m_tabs.InsertItem(2, L"Transiciones");
    m_tabs.InsertItem(3, L"Exportar");
    m_tabs.InsertItem(4, L"Textos");
    m_tabs.SetCurSel(0);
    m_tabIndex = 0;

    m_btnAdd.Create(L"+  Agregar videos", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this, IDC_BTN_ADD);
    m_btnRemove.Create(L"Quitar", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this, IDC_BTN_REMOVE);
    m_btnUp.Create(L"Subir capa", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this, IDC_BTN_UP);
    m_btnDown.Create(L"Bajar capa", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this, IDC_BTN_DOWN);
    m_btnSplit.Create(L"Cortar (X)", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this, IDC_BTN_SPLIT);

    m_btnUndo.Create(L"Deshacer", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this, IDC_BTN_UNDO);
    m_btnRedo.Create(L"Rehacer", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this, IDC_BTN_REDO);

    m_clipsLabel.Create(L"Biblioteca de clips", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                        IDC_CLIPS_LABEL);
    m_libraryView.CreateView(this, IDC_CLIP_LIST);

    m_filterLabel.Create(L"Ajustes", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_FILTER_LABEL);
    m_chkNegate.Create(L"Invertir colores", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                       empty, this, IDC_CHK_NEGATE);
    m_chkFlipH.Create(L"Espejo horizontal", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                      empty, this, IDC_CHK_FLIP_H);
    m_chkFlipV.Create(L"Espejo vertical", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                      empty, this, IDC_CHK_FLIP_V);

    m_trackLabel.Create(L"Capa:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_TRACK_LABEL);
    m_trackCombo.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, empty, this,
                        IDC_TRACK_COMBO);
    m_trackCombo.AddString(L"Capa 1");
    m_trackCombo.AddString(L"Capa 2");
    m_trackCombo.AddString(L"Capa 3");
    m_trackCombo.AddString(L"Audio");
    m_trackCombo.SetCurSel(0);

    m_scaleLabel.Create(L"Escala:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_SCALE_LABEL);
    m_scaleValue.Create(L"100%", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_SCALE_VALUE);

    m_volumeLabel.Create(L"Volumen:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                         IDC_VOLUME_LABEL);
    m_volumeSlider.Create(WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, empty, this,
                          IDC_VOLUME_SLIDER);
    m_volumeSlider.SetRange(0, 200, TRUE);
    m_volumeSlider.SetPos(100);
    m_volumeValue.Create(L"100%", WS_CHILD | WS_VISIBLE | SS_RIGHT, empty, this, IDC_VOLUME_VALUE);

    m_transformHint.Create(L"Arrastra el video para moverlo (se alinea con bordes\n"
                           L"y esquinas). Esquinas: tamaño; puntos medios de los\n"
                           L"bordes: hacerlo mas ancho o mas alto.",
                           WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_TRANSFORM_HINT);

    // Pestana "Textos".
    m_txtLabel.Create(L"Textos", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_TXT_LABEL);
    m_txtList.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY |
                         LBS_NOINTEGRALHEIGHT,
                     empty, this, IDC_TXT_LIST);
    m_txtAdd.Create(L"+  Nuevo texto", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this,
                    IDC_TXT_ADD);
    m_txtRemove.Create(L"Quitar", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this,
                       IDC_TXT_REMOVE);
    m_txtContentLabel.Create(L"Contenido:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                             IDC_TXT_CONTENT_LABEL);
    m_txtContent.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE |
                            ES_AUTOVSCROLL | ES_WANTRETURN,
                        empty, this, IDC_TXT_CONTENT);
    m_txtSizeLabel.Create(L"Tamaño:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                          IDC_TXT_SIZE_LABEL);
    m_txtSize.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, empty, this,
                     IDC_TXT_SIZE);
    m_txtBold.Create(L"Negrita", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, empty, this,
                     IDC_TXT_BOLD);
    m_txtItalic.Create(L"Cursiva", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, empty, this,
                       IDC_TXT_ITALIC);
    m_txtUnderline.Create(L"Subrayado", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, empty, this,
                          IDC_TXT_UNDERLINE);
    m_txtAlignLabel.Create(L"Alineacion:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                           IDC_TXT_ALIGN_LABEL);
    m_txtAlignCombo.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, empty, this,
                           IDC_TXT_ALIGN_COMBO);
    m_txtAlignCombo.AddString(L"Izquierda");
    m_txtAlignCombo.AddString(L"Centro");
    m_txtAlignCombo.AddString(L"Derecha");
    m_txtAlignCombo.AddString(L"Justificado");
    m_txtAlignCombo.SetCurSel(0);
    m_txtTrackLabel.Create(L"Capa:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                           IDC_TXT_TRACK_LABEL);
    m_txtTrackCombo.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, empty, this,
                           IDC_TXT_TRACK_COMBO);
    for (int track = 0; track < kVideoTrackCount; ++track) {
        m_txtTrackCombo.AddString(TrackName(track).c_str());
    }
    m_txtTrackCombo.SetCurSel(kVideoTrackCount - 1);

    m_txtDurLabel.Create(L"Duracion:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                         IDC_TXT_DUR_LABEL);
    m_txtDurSlider.Create(WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, empty, this,
                          IDC_TXT_DUR_SLIDER);
    m_txtDurSlider.SetRange(1, 600, TRUE);
    m_txtDurSlider.SetPos(50);
    m_txtDurValue.Create(L"5.0 s", WS_CHILD | WS_VISIBLE | SS_RIGHT, empty, this,
                         IDC_TXT_DUR_VALUE);

    m_txtHint.Create(L"En la linea de tiempo arrastra el texto entre capas y recorta su "
                     L"duracion.\nEn la vista previa mueve y redimensiona su caja.",
                     WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_TXT_HINT);

    m_btnCenter.Create(L"Centrar y restablecer", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this,
                       IDC_BTN_CENTER);

    // Pestana "Transiciones".
    m_transLabel.Create(L"Transiciones", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                        IDC_TRANS_LABEL);
    m_transDirectionTabs.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS |
                                    TCS_FIXEDWIDTH,
                                empty, this, IDC_TRANS_DIRECTION_TABS);
    m_transDirectionTabs.InsertItem(0, L"Salida");
    m_transDirectionTabs.InsertItem(1, L"Entrada");
    m_transDirectionTabs.SetCurSel(0);

    m_transSelectedLabel.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                                IDC_TRANS_SELECTED_LABEL);
    m_transTypeLabel.Create(L"Tipo:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                            IDC_TRANS_TYPE_LABEL);
    m_transTypeCombo.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, empty, this,
                            IDC_TRANS_TYPE_COMBO);
    m_transTypeCombo.AddString(L"Disolvencia");
    m_transTypeCombo.AddString(L"Fundido a negro");
    m_transTypeCombo.AddString(L"Deslizar a la izquierda");
    m_transTypeCombo.SetCurSel(0);

    m_transDurLabel.Create(L"Duracion:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                           IDC_TRANS_DUR_LABEL);
    m_transDurSlider.Create(WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, empty, this,
                            IDC_TRANS_DUR_SLIDER);
    m_transDurSlider.SetRange(1, 50, TRUE);
    m_transDurSlider.SetPos(10);
    m_transDurValue.Create(L"1.0 s", WS_CHILD | WS_VISIBLE | SS_RIGHT, empty, this,
                           IDC_TRANS_DUR_VALUE);

    m_btnTransAdd.Create(L"+  Agregar transicion", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty,
                         this, IDC_BTN_TRANS_ADD);
    m_btnTransRemove.Create(L"Quitar", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this,
                            IDC_BTN_TRANS_REMOVE);

    m_transList.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL |
                           LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                       empty, this, IDC_TRANS_LIST);
    m_transList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    m_transList.SetBkColor(Theme::kPanel);
    m_transList.SetTextBkColor(Theme::kPanel);
    m_transList.SetTextColor(Theme::kText);
    m_transList.InsertColumn(0, L"Salida", LVCFMT_LEFT, 84);
    m_transList.InsertColumn(1, L"Entrada", LVCFMT_LEFT, 84);
    m_transList.InsertColumn(2, L"Tipo", LVCFMT_LEFT, 96);
    m_transList.InsertColumn(3, L"Dur", LVCFMT_RIGHT, 40);

    m_transHint.Create(L"Selecciona un video en la linea de tiempo y elige la direccion.",
                       WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_TRANS_HINT);

    m_speedLabel.Create(L"Velocidad:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_SPEED_LABEL);
    m_speedCombo.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, empty, this, IDC_SPEED_COMBO);
    m_resLabel.Create(L"Resolucion:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_RES_LABEL);
    m_resCombo.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, empty, this, IDC_RES_COMBO);

    m_speedValues = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0 };
    for (const double value : m_speedValues) {
        CString text;
        if (value < 1.0) {
            text.Format(L"%.2gx", value);
        }
        else {
            text.Format(L"%.4gx", value);
        }
        m_speedCombo.AddString(text);
    }
    m_speedCombo.SetCurSel(3);

    m_resCombo.AddString(L"Original");
    m_resCombo.AddString(L"720p");
    m_resCombo.AddString(L"1080p");
    m_resCombo.AddString(L"1440p");
    m_resCombo.AddString(L"4K");
    m_resCombo.SetCurSel(2);

    m_btnExport.Create(L"Exportar video...", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this, IDC_BTN_EXPORT);
    m_btnCancel.Create(L"Cancelar", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this, IDC_BTN_CANCEL);
    m_btnCancel.EnableWindow(FALSE);
    m_exportLabel.Create(L"Exportar", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_EXPORT_LABEL);
    m_exportHint.Create(L"Elige la resolucion de salida y exporta el\n"
                        L"proyecto completo a un archivo MP4.",
                        WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_EXPORT_HINT);

    m_previewView.CreateView(this, IDC_PREVIEW);

    m_btnPrev.Create(L"|< Frame", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this, IDC_BTN_PREV);
    m_btnPlay.Create(L"Reproducir", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this, IDC_BTN_PLAY);
    m_btnNext.Create(L"Frame >|", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, empty, this, IDC_BTN_NEXT);
    m_timeLabel.Create(L"00:00:00:00 / 00:00:00:00", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_TIME_LABEL);

    m_timeline.CreateView(this, IDC_TIMELINE);

    m_zoomLabel.Create(L"Zoom:", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                       IDC_TIMELINE_ZOOM_LABEL);
    m_zoomSlider.Create(WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, empty, this,
                        IDC_TIMELINE_ZOOM_SLIDER);
    m_zoomSlider.SetRange(100, 1200, TRUE);
    m_zoomSlider.SetPos(100);
    m_zoomSlider.SetPageSize(50);
    m_zoomValue.Create(L"100%", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this,
                       IDC_TIMELINE_ZOOM_VALUE);

    m_status.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT, empty, this, IDC_STATUS);
    m_progress.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, empty, this, IDC_PROGRESS);
    m_progress.SetRange(0, 1000);
    m_progress.SetPos(0);

    m_timeline.OnSeek = [this]() {
        const double time = m_timeline.Playhead();
        if (m_playing) {
            // Al buscar mientras se reproduce, el reloj y el audio parten de nuevo.
            m_playStartTime = time;
            m_playStartTick = GetTickCount64();
            m_audio.Play(time);
        }
        SeekTo(time);
    };

    m_timeline.OnSelectionChanged = [this]() { SetListSelection(m_timeline.Selection()); };

    m_timeline.OnTextSelectionChanged = [this](int index) {
        if (index < 0 || index >= static_cast<int>(m_texts.size())) {
            return;
        }
        m_textSelection = index;
        m_textEditUndoPushed = false;
        UpdateTextList(index);
        SyncTextControls();
        m_previewView.Refresh();
    };

    m_timeline.OnZoomStep = [this](int direction) {
        ApplyTimelineZoom(m_zoomSlider.GetPos() + direction * 50);
    };

    m_previewView.OnDragBegin = [this](double x, double y, int zone) {
        return PreviewDragBegin(x, y, zone);
    };
    m_previewView.OnDragMove = [this](double x, double y) { PreviewDragMove(x, y); };
    m_previewView.OnDragEnd = [this]() { PreviewDragEnd(); };

    m_timeline.OnTrim = [this](bool committed) {
        if (!committed) {
            if (!m_timelineDragCaptured) {
                PushUndo();
                m_timelineDragCaptured = true;
            }
        }
        else {
            m_timelineDragCaptured = false;
        }
        UpdateTotalLabel();
        const int index = m_timeline.Selection();
        if (index >= 0 && index < static_cast<int>(m_clips.size())) {
            m_timeline.SetSourceFps(m_clips[static_cast<size_t>(index)].fps);
        }
        if (committed) {
            ApplyTransitionPlacements();
            UpdateTransitionList();
            if (m_textSelection >= 0 && m_textSelection < static_cast<int>(m_texts.size())) {
                UpdateTextList(m_textSelection);
                SyncTextControls();
            }
            RebuildPreview(m_preview.CurrentTime(), false);
            UpdateLibrary();
            SetListSelection(m_selection);
        }
    };
}

void CMainFrame::ApplyFonts()
{
    const auto setFont = [](CWnd& window, CFont& font) {
        if (window.GetSafeHwnd() != nullptr) {
            window.SendMessage(WM_SETFONT, reinterpret_cast<WPARAM>(font.GetSafeHandle()), TRUE);
        }
    };

    setFont(m_title, m_fontTitle);
    setFont(m_total, m_fontUi);
    setFont(m_clipsLabel, m_fontSection);
    setFont(m_filterLabel, m_fontSection);
    setFont(m_libraryView, m_fontUi);
    setFont(m_btnAdd, m_fontBold);
    setFont(m_btnRemove, m_fontUi);
    setFont(m_btnUp, m_fontUi);
    setFont(m_btnDown, m_fontUi);
    setFont(m_btnSplit, m_fontUi);
    setFont(m_btnUndo, m_fontUi);
    setFont(m_btnRedo, m_fontUi);
    setFont(m_chkNegate, m_fontUi);
    setFont(m_chkFlipH, m_fontUi);
    setFont(m_chkFlipV, m_fontUi);
    setFont(m_trackLabel, m_fontUi);
    setFont(m_trackCombo, m_fontUi);
    setFont(m_scaleLabel, m_fontUi);
    setFont(m_scaleValue, m_fontUi);
    setFont(m_volumeLabel, m_fontUi);
    setFont(m_volumeSlider, m_fontUi);
    setFont(m_volumeValue, m_fontUi);
    setFont(m_transformHint, m_fontUi);
    setFont(m_btnCenter, m_fontUi);
    setFont(m_zoomLabel, m_fontUi);
    setFont(m_zoomSlider, m_fontUi);
    setFont(m_zoomValue, m_fontUi);
    setFont(m_tabs, m_fontUi);
    setFont(m_transLabel, m_fontSection);
    setFont(m_transDirectionTabs, m_fontUi);
    setFont(m_transSelectedLabel, m_fontUi);
    setFont(m_transTypeLabel, m_fontUi);
    setFont(m_transTypeCombo, m_fontUi);
    setFont(m_transDurLabel, m_fontUi);
    setFont(m_transDurSlider, m_fontUi);
    setFont(m_transDurValue, m_fontUi);
    setFont(m_btnTransAdd, m_fontBold);
    setFont(m_btnTransRemove, m_fontUi);
    setFont(m_transList, m_fontUi);
    setFont(m_transHint, m_fontUi);
    setFont(m_txtLabel, m_fontSection);
    setFont(m_txtList, m_fontUi);
    setFont(m_txtAdd, m_fontBold);
    setFont(m_txtRemove, m_fontUi);
    setFont(m_txtContentLabel, m_fontUi);
    setFont(m_txtContent, m_fontUi);
    setFont(m_txtSizeLabel, m_fontUi);
    setFont(m_txtSize, m_fontUi);
    setFont(m_txtBold, m_fontUi);
    setFont(m_txtItalic, m_fontUi);
    setFont(m_txtUnderline, m_fontUi);
    setFont(m_txtAlignLabel, m_fontUi);
    setFont(m_txtAlignCombo, m_fontUi);
    setFont(m_txtTrackLabel, m_fontUi);
    setFont(m_txtTrackCombo, m_fontUi);
    setFont(m_txtDurLabel, m_fontUi);
    setFont(m_txtDurSlider, m_fontUi);
    setFont(m_txtDurValue, m_fontUi);
    setFont(m_txtHint, m_fontUi);
    setFont(m_exportLabel, m_fontSection);
    setFont(m_exportHint, m_fontUi);
    setFont(m_speedLabel, m_fontUi);
    setFont(m_speedCombo, m_fontUi);
    setFont(m_resLabel, m_fontUi);
    setFont(m_resCombo, m_fontUi);
    setFont(m_btnExport, m_fontBold);
    setFont(m_btnCancel, m_fontUi);
    setFont(m_btnPrev, m_fontUi);
    setFont(m_btnPlay, m_fontBold);
    setFont(m_btnNext, m_fontUi);
    setFont(m_timeLabel, m_fontMono);
    setFont(m_status, m_fontUi);
}

void CMainFrame::OnSize(UINT nType, int cx, int cy)
{
    CFrameWnd::OnSize(nType, cx, cy);

    if (m_libraryView.GetSafeHwnd() == nullptr) {
        return;
    }

    LayoutControls();
}

void CMainFrame::LayoutControls()
{
    CRect client;
    GetClientRect(&client);
    if (client.IsRectEmpty()) {
        return;
    }

    CClientDC dc(this);
    const int dpi = dc.GetDeviceCaps(LOGPIXELSY);
    const auto px = [dpi](int value) { return MulDiv(value, dpi, 96); };

    const int margin = px(12);
    const int width = client.Width() - margin * 2;

    m_title.MoveWindow(margin + px(14), px(6), px(190), px(26));
    m_btnUndo.MoveWindow(margin + px(206), px(8), px(88), px(24));
    m_btnRedo.MoveWindow(margin + px(300), px(8), px(88), px(24));
    m_total.MoveWindow(client.right - margin - px(360), px(12), px(360), px(18));

    const int statusTop = client.bottom - margin - px(38);
    const int timelineH = px(126);
    const int timelineTop = statusTop - px(10) - timelineH;
    const int zoomH = px(24);
    const int zoomTop = timelineTop - px(6) - zoomH;
    const int mainTop = px(74);
    const int mainBottom = zoomTop - px(8);

    m_timeline.MoveWindow(margin, timelineTop, width, timelineH);

    // Fila de zoom de la linea de tiempo.
    m_zoomLabel.MoveWindow(margin, zoomTop + px(3), px(44), px(18));
    m_zoomSlider.MoveWindow(margin + px(46), zoomTop, px(210), px(22));
    m_zoomValue.MoveWindow(margin + px(262), zoomTop + px(3), px(70), px(18));

    m_status.MoveWindow(margin, statusTop, width, px(18));
    m_progress.MoveWindow(margin, statusTop + px(22), width, px(12));

    const int sidebarW = px(380);
    const int sidebarX = client.right - margin - sidebarW;
    const int transportH = px(32);
    const int previewAreaW = (std::max)(px(200), sidebarX - px(10) - margin);
    const int previewAreaH = (std::max)(px(140), mainBottom - mainTop - transportH - px(8));

    int previewH = previewAreaH;
    int previewW = static_cast<int>(previewH * 16.0 / 9.0 + 0.5);
    if (previewW > previewAreaW) {
        previewW = previewAreaW;
        previewH = static_cast<int>(previewW * 9.0 / 16.0 + 0.5);
    }

    const int previewX = margin + (previewAreaW - previewW) / 2;
    const int previewY = mainTop;
    m_previewView.MoveWindow(previewX, previewY, previewW, previewH);

    const int transportY = previewY + previewH + px(8);
    m_btnPrev.MoveWindow(previewX, transportY, px(72), transportH);
    m_btnPlay.MoveWindow(previewX + px(78), transportY, px(108), transportH);
    m_btnNext.MoveWindow(previewX + px(192), transportY, px(72), transportH);
    m_timeLabel.MoveWindow(previewX + px(276), transportY + px(7),
                           (std::max)(px(160), previewW - px(276)), px(18));

    m_tabs.MoveWindow(sidebarX, mainTop, sidebarW, mainBottom - mainTop);

    // El control de lenguetas actua solo como fondo de la barra lateral. Lo
    // enviamos al fondo del orden Z para que sus repintados (hover, cambio de
    // pestana) nunca tapen los controles de la pagina, que son sus hermanos.
    m_tabs.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // Lenguetas mas altas y repartidas a lo ancho de la barra lateral.
    const int tabWidth = (std::max)(px(64), (sidebarW - px(16)) / 5);
    m_tabs.SetItemSize(CSize(tabWidth, px(28)));

    // Area interior de la pestana (debajo de las lenguetas).
    const CRect page(sidebarX + px(9), mainTop + px(30),
                     sidebarX + sidebarW - px(9), mainBottom - px(9));
    LayoutTabPages(page);
}

// Coloca los controles de cada pestana dentro del area indicada. Todas las
// paginas comparten el mismo rect; la visibilidad la controla ShowTabPage().
void CMainFrame::LayoutTabPages(const CRect& page)
{
    CClientDC dc(this);
    const int dpi = dc.GetDeviceCaps(LOGPIXELSY);
    const auto px = [dpi](int value) { return MulDiv(value, dpi, 96); };

    const int x = page.left;
    const int y = page.top;
    const int width = (std::max)(px(120), page.Width());

    const int labelH = px(18);
    const int rowH = px(30);
    const int sliderH = px(24);

    // ---- Pestana Clips ----
    m_clipsLabel.MoveWindow(x, y, width, labelH);
    m_trackLabel.MoveWindow(x, y + px(26), px(42), labelH);
    m_trackCombo.MoveWindow(x + px(44), y + px(22), px(112), px(200));

    const int buttonH = px(28);
    const int buttonsTop = page.bottom - buttonH * 2 - px(5);
    const int listTop = y + px(54);
    const int listH = (std::max)(px(60), buttonsTop - px(8) - listTop);
    m_libraryView.MoveWindow(x, listTop, width, listH);

    const int gap = px(6);
    const int halfW = (width - gap) / 2;
    m_btnAdd.MoveWindow(x, buttonsTop, halfW, buttonH);
    m_btnRemove.MoveWindow(x + halfW + gap, buttonsTop, width - halfW - gap, buttonH);

    const int thirdW = (width - gap * 2) / 3;
    const int row2 = buttonsTop + buttonH + px(5);
    m_btnUp.MoveWindow(x, row2, thirdW, buttonH);
    m_btnDown.MoveWindow(x + thirdW + gap, row2, thirdW, buttonH);
    m_btnSplit.MoveWindow(x + (thirdW + gap) * 2, row2,
                          width - (thirdW + gap) * 2, buttonH);

    // ---- Pestana Ajustes ----
    m_filterLabel.MoveWindow(x, y, width, labelH);
    m_chkNegate.MoveWindow(x, y + px(24), width, px(22));
    m_chkFlipH.MoveWindow(x, y + px(46), width, px(22));
    m_chkFlipV.MoveWindow(x, y + px(68), width, px(22));

    const int speedY = y + px(98);
    m_speedLabel.MoveWindow(x, speedY + px(4), px(66), labelH);
    m_speedCombo.MoveWindow(x + px(68), speedY, px(90), px(220));

    const auto layoutSlider = [&](int rowY, CStatic& label, CSliderCtrl& slider,
                                  CStatic& value) {
        label.MoveWindow(x, rowY + px(4), px(60), labelH);
        slider.MoveWindow(x + px(64), rowY, width - px(64) - px(54), sliderH);
        value.MoveWindow(x + width - px(50), rowY + px(4), px(50), labelH);
    };

    const int scaleY = speedY + px(34);
    m_scaleLabel.MoveWindow(x, scaleY + px(4), px(60), labelH);
    m_scaleValue.MoveWindow(x + px(64), scaleY + px(4), px(112), labelH);
    layoutSlider(scaleY + px(30), m_volumeLabel, m_volumeSlider, m_volumeValue);

    m_transformHint.MoveWindow(x, scaleY + px(64), width, px(54));
    m_btnCenter.MoveWindow(x, page.bottom - px(32), width, px(30));

    // ---- Pestana Transiciones ----
    m_transLabel.MoveWindow(x, y, width, labelH);

    const int dirTabW = (std::max)(px(70), width / 2);
    m_transDirectionTabs.MoveWindow(x, y + px(24), width, px(32));
    m_transDirectionTabs.SetItemSize(CSize(dirTabW, px(26)));

    m_transSelectedLabel.MoveWindow(x, y + px(60), width, px(30));

    const int comboW = (std::max)(px(80), width - px(58));
    m_transTypeLabel.MoveWindow(x, y + px(96), px(56), labelH);
    m_transTypeCombo.MoveWindow(x + px(58), y + px(92), comboW, px(220));

    const int durY = y + px(126);
    m_transDurLabel.MoveWindow(x, durY + px(4), px(70), labelH);
    m_transDurSlider.MoveWindow(x + px(74), durY, width - px(74) - px(52), sliderH);
    m_transDurValue.MoveWindow(x + width - px(48), durY + px(4), px(48), labelH);

    const int transButtonsY = durY + px(32);
    const int transHalfW = (width - gap) / 2;
    m_btnTransAdd.MoveWindow(x, transButtonsY, transHalfW + px(34), px(28));
    m_btnTransRemove.MoveWindow(x + transHalfW + px(34) + gap, transButtonsY,
                                width - transHalfW - px(34) - gap, px(28));

    const int transListTop = transButtonsY + px(38);
    const int hintH = px(30);
    const int transListH =
        (std::max)(px(60), static_cast<int>(page.bottom) - hintH - px(6) - transListTop);
    m_transList.MoveWindow(x, transListTop, width, transListH);
    m_transHint.MoveWindow(x, page.bottom - hintH, width, hintH);

    m_transList.SetColumnWidth(0, (std::max)(px(60), width / 4));
    m_transList.SetColumnWidth(1, (std::max)(px(60), width / 4));
    m_transList.SetColumnWidth(2, (std::max)(px(80), width / 3));
    m_transList.SetColumnWidth(3, px(42));

    // ---- Pestana Exportar ----
    m_exportLabel.MoveWindow(x, y, width, labelH);
    m_resLabel.MoveWindow(x, y + px(34), px(74), labelH);
    m_resCombo.MoveWindow(x + px(76), y + px(30), width - px(76), px(220));

    const int exportY = page.bottom - px(72);
    m_btnExport.MoveWindow(x, exportY, width - px(92), px(34));
    m_btnCancel.MoveWindow(x + width - px(84), exportY, px(84), px(34));
    m_exportHint.MoveWindow(x, y + px(72), width, px(50));

    // ---- Pestana Textos ----
    m_txtLabel.MoveWindow(x, y, width, labelH);
    const int txtButtonH = px(26);
    m_txtAdd.MoveWindow(x, y + px(24), halfW, txtButtonH);
    m_txtRemove.MoveWindow(x + halfW + gap, y + px(24),
                           width - halfW - gap, txtButtonH);

    const int txtListTop = y + px(56);
    const int txtListH = px(74);
    m_txtList.MoveWindow(x, txtListTop, width, txtListH);

    const int contentY = txtListTop + txtListH + px(8);
    m_txtContentLabel.MoveWindow(x, contentY, width, labelH);
    m_txtContent.MoveWindow(x, contentY + px(20), width, px(66));

    const int sizeY = contentY + px(92);
    m_txtSizeLabel.MoveWindow(x, sizeY + px(4), px(62), labelH);
    m_txtSize.MoveWindow(x + px(64), sizeY, px(70), px(24));

    const int styleY = sizeY + px(32);
    const int styleW = (width - gap * 2) / 3;
    m_txtBold.MoveWindow(x, styleY, styleW, px(22));
    m_txtItalic.MoveWindow(x + styleW + gap, styleY, styleW, px(22));
    m_txtUnderline.MoveWindow(x + (styleW + gap) * 2, styleY,
                              width - (styleW + gap) * 2, px(22));

    const int alignY = styleY + px(30);
    m_txtAlignLabel.MoveWindow(x, alignY + px(4), px(74), labelH);
    m_txtAlignCombo.MoveWindow(x + px(76), alignY, width - px(76), px(200));

    const int txtTrackY = alignY + px(30);
    m_txtTrackLabel.MoveWindow(x, txtTrackY + px(4), px(62), labelH);
    m_txtTrackCombo.MoveWindow(x + px(64), txtTrackY, width - px(64), px(160));

    const int txtDurY = txtTrackY + px(30);
    m_txtDurLabel.MoveWindow(x, txtDurY + px(4), px(70), labelH);
    m_txtDurSlider.MoveWindow(x + px(74), txtDurY, width - px(74) - px(52), sliderH);
    m_txtDurValue.MoveWindow(x + width - px(48), txtDurY + px(4), px(48), labelH);

    const int txtHintH = px(42);
    m_txtHint.MoveWindow(x, page.bottom - txtHintH, width, txtHintH);
}

void CMainFrame::ShowTabPage(int index)
{
    if (m_tabs.GetSafeHwnd() == nullptr) {
        return;
    }

    m_tabIndex = (std::max)(0, (std::min)(index, 4));
    m_tabs.SetCurSel(m_tabIndex);

    const auto show = [](CWnd& window, bool visible) {
        if (window.GetSafeHwnd() != nullptr) {
            window.ShowWindow(visible ? SW_SHOW : SW_HIDE);
        }
    };

    const bool clips = (m_tabIndex == 0);
    show(m_clipsLabel, clips);
    show(m_trackLabel, clips);
    show(m_trackCombo, clips);
    show(m_libraryView, clips);
    show(m_btnAdd, clips);
    show(m_btnRemove, clips);
    show(m_btnUp, clips);
    show(m_btnDown, clips);
    show(m_btnSplit, clips);

    const bool settings = (m_tabIndex == 1);
    show(m_filterLabel, settings);
    show(m_chkNegate, settings);
    show(m_chkFlipH, settings);
    show(m_chkFlipV, settings);
    show(m_speedLabel, settings);
    show(m_speedCombo, settings);
    show(m_scaleLabel, settings);
    show(m_scaleValue, settings);
    show(m_volumeLabel, settings);
    show(m_volumeSlider, settings);
    show(m_volumeValue, settings);
    show(m_transformHint, settings);
    show(m_btnCenter, settings);

    const bool transitions = (m_tabIndex == 2);
    show(m_transLabel, transitions);
    show(m_transDirectionTabs, transitions);
    show(m_transSelectedLabel, transitions);
    show(m_transTypeLabel, transitions);
    show(m_transTypeCombo, transitions);
    show(m_transDurLabel, transitions);
    show(m_transDurSlider, transitions);
    show(m_transDurValue, transitions);
    show(m_btnTransAdd, transitions);
    show(m_btnTransRemove, transitions);
    show(m_transList, transitions);
    show(m_transHint, transitions);

    const bool exportingTab = (m_tabIndex == 3);
    show(m_exportLabel, exportingTab);
    show(m_resLabel, exportingTab);
    show(m_resCombo, exportingTab);
    show(m_btnExport, exportingTab);
    show(m_btnCancel, exportingTab);
    show(m_exportHint, exportingTab);

    const bool texts = (m_tabIndex == 4);
    show(m_txtLabel, texts);
    show(m_txtList, texts);
    show(m_txtAdd, texts);
    show(m_txtRemove, texts);
    show(m_txtContentLabel, texts);
    show(m_txtContent, texts);
    show(m_txtSizeLabel, texts);
    show(m_txtSize, texts);
    show(m_txtBold, texts);
    show(m_txtItalic, texts);
    show(m_txtUnderline, texts);
    show(m_txtAlignLabel, texts);
    show(m_txtAlignCombo, texts);
    show(m_txtTrackLabel, texts);
    show(m_txtTrackCombo, texts);
    show(m_txtDurLabel, texts);
    show(m_txtDurSlider, texts);
    show(m_txtDurValue, texts);
    show(m_txtHint, texts);

    if (texts) {
        SyncTextControls();
        m_previewView.Refresh();
    }

    if (transitions) {
        UpdateTransitionTarget();
        UpdateTransitionList();
    }

    if (m_tabs.GetSafeHwnd() != nullptr) {
        m_tabs.Invalidate(FALSE);
    }
}

int CMainFrame::TransitionNeighborClip(int clipIndex, bool outgoing) const
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(m_clips.size())) {
        return -1;
    }

    const ClipInfo& selected = m_clips[static_cast<size_t>(clipIndex)];
    if (selected.IsAudioOnly() || selected.OutputDuration() <= 0.05) {
        return -1;
    }

    int best = -1;
    double bestScore = 1e100;
    for (int i = 0; i < static_cast<int>(m_clips.size()); ++i) {
        if (i == clipIndex) {
            continue;
        }

        const ClipInfo& other = m_clips[static_cast<size_t>(i)];
        if (other.IsAudioOnly() || other.OutputDuration() <= 0.05) {
            continue;
        }

        const bool valid = outgoing ? (other.start > selected.start + 1e-6)
                                    : (other.start < selected.start - 1e-6);
        if (!valid) {
            continue;
        }

        // El vecino mas cercano en tiempo gana; la capa solo desempata.
        const double score = std::fabs(other.start - selected.start) +
                             std::abs(other.EffectiveTrack() - selected.EffectiveTrack()) * 0.001;
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

void CMainFrame::UpdateTransitionTarget()
{
    if (m_transDirectionTabs.GetSafeHwnd() == nullptr) {
        return;
    }

    const bool outgoing = m_transDirectionTabs.GetCurSel() == 0;
    const int selected = m_selection;

    CString selectedText;
    if (selected < 0 || selected >= static_cast<int>(m_clips.size()) ||
        m_clips[static_cast<size_t>(selected)].IsAudioOnly()) {
        selectedText = L"Selecciona un clip de video en la linea de tiempo.";
    }
    else {
        selectedText.Format(L"Video seleccionado: %s",
                            m_clips[static_cast<size_t>(selected)].fileName.c_str());
    }
    m_transSelectedLabel.SetWindowText(selectedText);

    const int neighbor = TransitionNeighborClip(selected, outgoing);
    if (neighbor >= 0) {
        const int from = outgoing ? selected : neighbor;
        const int to = outgoing ? neighbor : selected;
        CString hint;
        hint.Format(L"%s: %s  ->  %s", outgoing ? L"Salida" : L"Entrada",
                    m_clips[static_cast<size_t>(from)].fileName.c_str(),
                    m_clips[static_cast<size_t>(to)].fileName.c_str());
        m_transHint.SetWindowText(hint);
    }
    else if (selected < 0 || selected >= static_cast<int>(m_clips.size()) ||
             m_clips[static_cast<size_t>(selected)].IsAudioOnly()) {
        m_transHint.SetWindowText(L"Selecciona un video para aplicarle la transicion.");
    }
    else {
        m_transHint.SetWindowText(outgoing
                                      ? L"Salida: no hay otro video despues del seleccionado."
                                      : L"Entrada: no hay otro video antes del seleccionado.");
    }
}

void CMainFrame::UpdateTransitionList(int selectIndex)
{
    if (m_transList.GetSafeHwnd() == nullptr) {
        return;
    }

    m_transList.DeleteAllItems();

    for (size_t i = 0; i < m_transitions.size(); ++i) {
        const TransitionInfo& transition = m_transitions[i];
        if (!transition.IsValid() || transition.fromClip >= static_cast<int>(m_clips.size()) ||
            transition.toClip >= static_cast<int>(m_clips.size())) {
            continue;
        }

        const ClipInfo& from = m_clips[static_cast<size_t>(transition.fromClip)];
        const ClipInfo& to = m_clips[static_cast<size_t>(transition.toClip)];
        const int row = m_transList.InsertItem(static_cast<int>(i), from.fileName.c_str());
        m_transList.SetItemText(row, 1, to.fileName.c_str());
        m_transList.SetItemText(row, 2, TransitionName(transition.type));

        wchar_t duration[32]{};
        swprintf_s(duration, L"%.1f s", transition.EffectiveDuration());
        m_transList.SetItemText(row, 3, duration);
        m_transList.SetItemData(row, static_cast<DWORD_PTR>(i));
    }

    if (selectIndex >= 0 && selectIndex < m_transList.GetItemCount()) {
        m_transList.SetItemState(selectIndex, LVIS_SELECTED | LVIS_FOCUSED,
                                 LVIS_SELECTED | LVIS_FOCUSED);
        m_transList.EnsureVisible(selectIndex, FALSE);
    }
}

void CMainFrame::InvalidateTransitionState()
{
    const int clipCount = static_cast<int>(m_clips.size());
    const auto removeIt = std::remove_if(
        m_transitions.begin(), m_transitions.end(),
        [this, clipCount](const TransitionInfo& transition) {
            return !transition.IsValid() || transition.fromClip >= clipCount ||
                   transition.toClip >= clipCount ||
                   m_clips[static_cast<size_t>(transition.fromClip)].IsAudioOnly() ||
                   m_clips[static_cast<size_t>(transition.toClip)].IsAudioOnly();
        });
    m_transitions.erase(removeIt, m_transitions.end());
}

void CMainFrame::ApplyTransitionPlacements(int anchorClip)
{
    InvalidateTransitionState();
    if (m_transitions.empty()) {
        return;
    }

    // Varias pasadas: al mover el clip destino puede cambiar el inicio de otra
    // transicion encadenada.
    for (int pass = 0; pass < 3; ++pass) {
        std::vector<size_t> order(m_transitions.size());
        for (size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::sort(order.begin(), order.end(), [this](size_t a, size_t b) {
            return m_clips[static_cast<size_t>(m_transitions[a].fromClip)].start <
                   m_clips[static_cast<size_t>(m_transitions[b].fromClip)].start;
        });

        for (const size_t index : order) {
            TransitionInfo& transition = m_transitions[index];
            ClipInfo& from = m_clips[static_cast<size_t>(transition.fromClip)];
            ClipInfo& to = m_clips[static_cast<size_t>(transition.toClip)];

            const double fromLength = from.OutputDuration();
            const double toLength = to.OutputDuration();
            if (fromLength <= 0.05 || toLength <= 0.05) {
                continue;
            }

            double span = transition.EffectiveDuration();
            span = (std::min)(span, fromLength * 0.95);
            span = (std::min)(span, toLength * 0.95);
            span = (std::max)(0.1, span);
            transition.duration = span;

            // La transicion se hace en la capa del video seleccionado. Sin un
            // ancla explicita (por ejemplo al recortar) se conserva el origen.
            if (anchorClip == transition.toClip) {
                from.track = to.track;
            }
            else {
                to.track = from.track;
            }

            if (transition.type == TransitionType::FadeBlack) {
                to.start = from.End();
            }
            else {
                to.start = (std::max)(0.0, from.End() - span);
            }
        }
    }
}

void CMainFrame::RemapTransitionsAfterInsert(int insertIndex)
{
    for (TransitionInfo& transition : m_transitions) {
        if (transition.fromClip >= insertIndex) {
            ++transition.fromClip;
        }
        if (transition.toClip >= insertIndex) {
            ++transition.toClip;
        }
    }
}

void CMainFrame::RemapTransitionsAfterRemove(int removeIndex)
{
    const auto removeIt = std::remove_if(
        m_transitions.begin(), m_transitions.end(),
        [removeIndex](const TransitionInfo& transition) {
            return transition.fromClip == removeIndex || transition.toClip == removeIndex;
        });
    m_transitions.erase(removeIt, m_transitions.end());

    for (TransitionInfo& transition : m_transitions) {
        if (transition.fromClip > removeIndex) {
            --transition.fromClip;
        }
        if (transition.toClip > removeIndex) {
            --transition.toClip;
        }
    }
}

// Al cambiar de capa un clip a mano, sus transiciones dejan de tener sentido
// (se hacen siempre dentro de una misma capa).
void CMainFrame::DropTransitionsForClip(int index)
{
    const auto removeIt = std::remove_if(
        m_transitions.begin(), m_transitions.end(),
        [index](const TransitionInfo& transition) {
            return transition.fromClip == index || transition.toClip == index;
        });

    if (removeIt == m_transitions.end()) {
        return;
    }

    m_transitions.erase(removeIt, m_transitions.end());
    UpdateTransitionTarget();
    UpdateTransitionList();
}

bool CMainFrame::AddTransitionFromControls(std::wstring* error)
{
    const int selected = m_selection;
    if (selected < 0 || selected >= static_cast<int>(m_clips.size()) ||
        m_clips[static_cast<size_t>(selected)].IsAudioOnly()) {
        if (error != nullptr) {
            *error = L"Selecciona un clip de video en la linea de tiempo.";
        }
        return false;
    }

    const bool outgoing = m_transDirectionTabs.GetCurSel() == 0;
    const int neighbor = TransitionNeighborClip(selected, outgoing);
    if (neighbor < 0) {
        if (error != nullptr) {
            *error = outgoing
                         ? L"No hay otro video despues del seleccionado para la salida."
                         : L"No hay otro video antes del seleccionado para la entrada.";
        }
        return false;
    }

    const int from = outgoing ? selected : neighbor;
    const int to = outgoing ? neighbor : selected;

    const int typeSelection = m_transTypeCombo.GetCurSel();
    TransitionType type = TransitionType::Crossfade;
    if (typeSelection == 1) {
        type = TransitionType::FadeBlack;
    }
    else if (typeSelection == 2) {
        type = TransitionType::SlideLeft;
    }

    double span = m_transDurSlider.GetPos() / 10.0;
    const double fromLength = m_clips[static_cast<size_t>(from)].OutputDuration();
    const double toLength = m_clips[static_cast<size_t>(to)].OutputDuration();
    span = (std::min)(span, fromLength * 0.95);
    span = (std::min)(span, toLength * 0.95);
    if (span < 0.1) {
        if (error != nullptr) {
            *error = L"Los clips son demasiado cortos para esa duracion de transicion.";
        }
        return false;
    }

    // Si ya existia una transicion entre el mismo par, se reemplaza.
    const auto duplicate = std::remove_if(
        m_transitions.begin(), m_transitions.end(),
        [from, to](const TransitionInfo& transition) {
            return transition.fromClip == from && transition.toClip == to;
        });
    m_transitions.erase(duplicate, m_transitions.end());

    TransitionInfo transition;
    transition.fromClip = from;
    transition.toClip = to;
    transition.type = type;
    transition.duration = span;
    m_transitions.push_back(transition);

    ApplyTransitionPlacements(selected);
    return true;
}

void CMainFrame::RestoreTransitionGap(int transitionIndex)
{
    if (transitionIndex < 0 || transitionIndex >= static_cast<int>(m_transitions.size())) {
        return;
    }

    const TransitionInfo& transition = m_transitions[static_cast<size_t>(transitionIndex)];
    if (!transition.IsValid() || transition.fromClip >= static_cast<int>(m_clips.size()) ||
        transition.toClip >= static_cast<int>(m_clips.size())) {
        return;
    }

    ClipInfo& from = m_clips[static_cast<size_t>(transition.fromClip)];
    ClipInfo& to = m_clips[static_cast<size_t>(transition.toClip)];
    to.start = (std::max)(to.start, from.End());
}

void CMainFrame::OnTabChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    if (pNMHDR != nullptr) {
        ShowTabPage(m_tabs.GetCurSel());
    }
    *pResult = 0;
}

void CMainFrame::OnTransDirectionChanged(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
    UpdateTransitionTarget();
    *pResult = 0;
}

void CMainFrame::OnAddTransition()
{
    if (m_exporting) {
        return;
    }

    StopPlayback();

    std::wstring error;
    ProjectSnapshot before = CaptureSnapshot();
    if (!AddTransitionFromControls(&error)) {
        SetStatus(error, true);
        return;
    }
    CommitUndo(std::move(before));

    UpdateTotalLabel();
    UpdateTransitionTarget();
    UpdateTransitionList(static_cast<int>(m_transitions.size()) - 1);

    const TransitionInfo& transition = m_transitions.back();
    const double center =
        (std::max)(0.0, m_clips[static_cast<size_t>(transition.fromClip)].End() -
                            transition.EffectiveDuration() * 0.5);
    UpdateLibrary();
    m_timeline.Refresh();
    RebuildPreview(center, false);

    CString status;
    status.Format(L"Transicion agregada (%s): %s  ->  %s (%s, %.1f s)",
                  m_transDirectionTabs.GetCurSel() == 0 ? L"Salida" : L"Entrada",
                  m_clips[static_cast<size_t>(transition.fromClip)].fileName.c_str(),
                  m_clips[static_cast<size_t>(transition.toClip)].fileName.c_str(),
                  TransitionName(transition.type), transition.EffectiveDuration());
    SetStatus(status.GetString());
}

void CMainFrame::OnRemoveTransition()
{
    if (m_exporting || m_transitions.empty()) {
        return;
    }
    int selected = m_transList.GetNextItem(-1, LVNI_SELECTED);
    if (selected < 0) {
        // Si no hay seleccion explicita, quitar la ultima de la lista.
        selected = static_cast<int>(m_transitions.size()) - 1;
    }
    const int index = static_cast<int>(m_transList.GetItemData(selected));
    if (index < 0 || index >= static_cast<int>(m_transitions.size())) {
        SetStatus(L"Selecciona una transicion de la lista para quitarla.", true);
        return;
    }

    PushUndo();
    StopPlayback();
    RestoreTransitionGap(index);
    m_transitions.erase(m_transitions.begin() + index);

    UpdateTotalLabel();
    UpdateTransitionTarget();
    UpdateTransitionList();
    m_timeline.Refresh();
    RebuildPreview(m_preview.CurrentTime(), false);
    SetStatus(L"Transicion eliminada.");
}

void CMainFrame::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
    CFrameWnd::OnGetMinMaxInfo(lpMMI);

    CClientDC dc(this);
    const int dpi = dc.GetDeviceCaps(LOGPIXELSY);
    lpMMI->ptMinTrackSize.x = MulDiv(1000, dpi, 96);
    lpMMI->ptMinTrackSize.y = MulDiv(720, dpi, 96);
}

ExportSettings CMainFrame::CurrentSettings() const
{
    ExportSettings settings;

    int width = 1920;
    int height = 1080;
    BuildOutputResolution(width, height);
    settings.width = width;
    settings.height = height;
    return settings;
}

TextInfo* CMainFrame::SelectedText()
{
    if (m_textSelection < 0 || m_textSelection >= static_cast<int>(m_texts.size())) {
        return nullptr;
    }
    return &m_texts[static_cast<size_t>(m_textSelection)];
}

const TextInfo* CMainFrame::SelectedText() const
{
    if (m_textSelection < 0 || m_textSelection >= static_cast<int>(m_texts.size())) {
        return nullptr;
    }
    return &m_texts[static_cast<size_t>(m_textSelection)];
}

void CMainFrame::UpdateTextList(int selectIndex)
{
    if (selectIndex >= -1 && selectIndex < static_cast<int>(m_texts.size())) {
        m_textSelection = selectIndex;
    }
    else if (m_textSelection >= static_cast<int>(m_texts.size())) {
        m_textSelection = m_texts.empty() ? -1 : static_cast<int>(m_texts.size()) - 1;
    }

    if (m_txtList.GetSafeHwnd() == nullptr) {
        return;
    }

    m_syncingText = true;
    m_txtList.ResetContent();
    for (size_t i = 0; i < m_texts.size(); ++i) {
        std::wstring label = m_texts[i].text;
        const size_t newline = label.find(L'\n');
        if (newline != std::wstring::npos) {
            label = label.substr(0, newline);
        }
        if (label.empty()) {
            label = L"(texto vacio)";
        }
        m_txtList.AddString(label.c_str());
    }
    m_txtList.SetCurSel(m_textSelection);
    m_syncingText = false;
}

void CMainFrame::SyncTextControls()
{
    if (m_txtContent.GetSafeHwnd() == nullptr) {
        return;
    }

    const TextInfo* text = SelectedText();
    const BOOL listEnabled = m_exporting ? FALSE : TRUE;
    const BOOL enabled = (text != nullptr && !m_exporting) ? TRUE : FALSE;

    m_syncingText = true;
    m_txtList.EnableWindow(listEnabled);
    m_txtAdd.EnableWindow(listEnabled);
    m_txtRemove.EnableWindow(enabled);
    m_txtContent.EnableWindow(enabled);
    m_txtSize.EnableWindow(enabled);
    m_txtBold.EnableWindow(enabled);
    m_txtItalic.EnableWindow(enabled);
    m_txtUnderline.EnableWindow(enabled);
    m_txtAlignCombo.EnableWindow(enabled);
    m_txtTrackCombo.EnableWindow(enabled);
    m_txtDurSlider.EnableWindow(enabled);

    if (text == nullptr) {
        m_txtContent.SetWindowText(L"");
        m_txtSize.SetWindowText(L"36");
        m_txtBold.SetCheck(BST_UNCHECKED);
        m_txtItalic.SetCheck(BST_UNCHECKED);
        m_txtUnderline.SetCheck(BST_UNCHECKED);
        m_txtAlignCombo.SetCurSel(0);
        m_txtTrackCombo.SetCurSel(kVideoTrackCount - 1);
        m_txtDurSlider.SetPos(50);
        m_txtDurValue.SetWindowText(L"5.0 s");
    }
    else {
        m_txtContent.SetWindowText(text->text.c_str());
        CString size;
        size.Format(L"%d", text->EffectiveFontSize());
        m_txtSize.SetWindowText(size);
        m_txtBold.SetCheck(text->bold ? BST_CHECKED : BST_UNCHECKED);
        m_txtItalic.SetCheck(text->italic ? BST_CHECKED : BST_UNCHECKED);
        m_txtUnderline.SetCheck(text->underline ? BST_CHECKED : BST_UNCHECKED);
        m_txtAlignCombo.SetCurSel(static_cast<int>(text->align));
        m_txtTrackCombo.SetCurSel(text->EffectiveTrack());

        const int ticks = static_cast<int>(text->EffectiveDuration() * 10.0 + 0.5);
        m_txtDurSlider.SetPos((std::max)(1, (std::min)(600, ticks)));
        CString duration;
        duration.Format(L"%.1f s", text->EffectiveDuration());
        m_txtDurValue.SetWindowText(duration);
    }
    m_syncingText = false;
}

void CMainFrame::RefreshTextPreview()
{
    UpdateTotalLabel();
    m_timeline.Refresh();

    if (!m_preview.IsOpen()) {
        RebuildPreview(0.0, true);
        return;
    }

    // Cambiar el contenido, la capa o la duracion exige volver a componer los
    // pixeles: invalidar la vista sola dejaba el frame anterior en pantalla.
    const double time = m_preview.CurrentTime();
    m_preview.SetTexts(m_texts);
    m_preview.RenderAt(time);
    m_previewView.Refresh();
    UpdateTimeLabel();
}

void CMainFrame::OnTextAdd()
{
    if (m_exporting) {
        return;
    }

    PushUndo();

    TextInfo text;
    const int index = static_cast<int>(m_texts.size());
    text.y = (std::min)(620.0, text.y + index * 28.0);
    text.Normalize();
    m_texts.push_back(text);
    m_textSelection = index;
    m_timeline.SetTextSelection(index, false);

    UpdateTextList(m_textSelection);
    SyncTextControls();
    RefreshTextPreview();
    SetStatus(L"Texto agregado. Escribe su contenido y ajusta la caja en la vista previa.");
}

void CMainFrame::OnTextRemove()
{
    if (m_exporting || m_textSelection < 0 ||
        m_textSelection >= static_cast<int>(m_texts.size())) {
        return;
    }

    PushUndo();
    m_texts.erase(m_texts.begin() + m_textSelection);
    if (m_textSelection >= static_cast<int>(m_texts.size())) {
        m_textSelection = m_texts.empty() ? -1 : static_cast<int>(m_texts.size()) - 1;
    }
    m_timeline.SetTextSelection(m_textSelection, false);

    UpdateTextList(m_textSelection);
    SyncTextControls();
    RefreshTextPreview();
    SetStatus(m_texts.empty() ? L"No hay textos. Pulsa \"Nuevo texto\" para crear uno."
                              : L"Texto eliminado.");
}

void CMainFrame::OnTextListSelChange()
{
    if (m_syncingText || m_exporting) {
        return;
    }

    const int selection = m_txtList.GetCurSel();
    if (selection < 0 || selection >= static_cast<int>(m_texts.size())) {
        return;
    }

    m_textSelection = selection;
    m_textEditUndoPushed = false;
    m_timeline.SetTextSelection(selection, false);
    SyncTextControls();
    m_previewView.Refresh();
}

void CMainFrame::OnTextContentChanged()
{
    if (m_syncingText || m_exporting) {
        return;
    }

    TextInfo* text = SelectedText();
    if (text == nullptr) {
        return;
    }

    CString value;
    m_txtContent.GetWindowText(value);
    const std::wstring content(value.GetString());
    if (content == text->text) {
        return;
    }

    if (!m_textEditUndoPushed) {
        PushUndo();
        m_textEditUndoPushed = true;
    }
    text->text = content;
    UpdateTextList(m_textSelection);
    RefreshTextPreview();
}

void CMainFrame::OnTextEditFocus()
{
    m_textEditUndoPushed = false;
}

void CMainFrame::OnTextSizeChanged()
{
    if (m_syncingText || m_exporting) {
        return;
    }

    TextInfo* text = SelectedText();
    if (text == nullptr) {
        return;
    }

    CString value;
    m_txtSize.GetWindowText(value);
    int size = _wtoi(value.GetString());
    if (size < 6) {
        size = 6;
    }
    else if (size > 240) {
        size = 240;
    }
    if (size == text->EffectiveFontSize()) {
        return;
    }

    if (!m_textEditUndoPushed) {
        PushUndo();
        m_textEditUndoPushed = true;
    }
    text->fontSize = size;
    RefreshTextPreview();
}

void CMainFrame::OnTextStyleChanged()
{
    if (m_syncingText || m_exporting) {
        return;
    }

    TextInfo* text = SelectedText();
    if (text == nullptr) {
        return;
    }

    const MSG* message = GetCurrentMessage();
    const UINT id = message != nullptr ? static_cast<UINT>(LOWORD(message->wParam)) : 0;
    bool* target = nullptr;
    if (id == IDC_TXT_BOLD) {
        target = &text->bold;
    }
    else if (id == IDC_TXT_ITALIC) {
        target = &text->italic;
    }
    else if (id == IDC_TXT_UNDERLINE) {
        target = &text->underline;
    }
    if (target == nullptr) {
        return;
    }

    PushUndo();
    *target = !*target;
    RefreshTextPreview();
}

void CMainFrame::OnTextAlignChanged()
{
    if (m_syncingText || m_exporting) {
        return;
    }

    TextInfo* text = SelectedText();
    if (text == nullptr) {
        return;
    }

    const int selection = m_txtAlignCombo.GetCurSel();
    if (selection < 0 || selection > 3) {
        return;
    }

    const TextAlign align = static_cast<TextAlign>(selection);
    if (align == text->align) {
        return;
    }

    PushUndo();
    text->align = align;
    RefreshTextPreview();
}

void CMainFrame::OnTextTrackChanged()
{
    if (m_syncingText || m_exporting) {
        return;
    }

    TextInfo* text = SelectedText();
    if (text == nullptr) {
        return;
    }

    const int selection = m_txtTrackCombo.GetCurSel();
    if (selection < 0 || selection >= kVideoTrackCount) {
        return;
    }
    if (selection == text->EffectiveTrack()) {
        return;
    }

    PushUndo();
    text->track = selection;
    RefreshTextPreview();
    m_timeline.SetTextSelection(m_textSelection, false);
    SetStatus(L"Texto movido a " + TrackName(selection) + L".");
}

void CMainFrame::UpdateLibrary(int selectIndex)
{
    m_libraryView.SetItems(&m_library);
    m_libraryView.SetTimelineCount(static_cast<int>(m_clips.size()));

    if (selectIndex >= 0 && selectIndex < static_cast<int>(m_library.size())) {
        SetLibrarySelection(selectIndex);
    }
    else if (m_librarySelection >= static_cast<int>(m_library.size())) {
        SetLibrarySelection(static_cast<int>(m_library.size()) - 1);
    }
}

void CMainFrame::SetLibrarySelection(int index)
{
    m_librarySelection =
        (index >= 0 && index < static_cast<int>(m_library.size())) ? index : -1;
    m_libraryView.SetSelection(m_librarySelection);
}

void CMainFrame::SetListSelection(int index)
{
    m_selection =
        (index >= 0 && index < static_cast<int>(m_clips.size())) ? index : -1;

    m_timeline.SetSelection(m_selection);
    if (m_selection >= 0) {
        m_timeline.SetSourceFps(m_clips[static_cast<size_t>(m_selection)].fps);

        // Resalta en la biblioteca el archivo del que procede este clip.
        const int mediaId = m_clips[static_cast<size_t>(m_selection)].mediaId;
        if (mediaId >= 0) {
            for (size_t i = 0; i < m_library.size(); ++i) {
                if (m_library[i].id == mediaId) {
                    SetLibrarySelection(static_cast<int>(i));
                    break;
                }
            }
        }
    }

    SyncClipControls();
    UpdateTransitionTarget();
    m_previewView.Refresh();
}

// Los controles de "Ajustes" muestran y editan el clip seleccionado.
void CMainFrame::SyncClipControls()
{
    const bool hasClip = m_selection >= 0 && m_selection < static_cast<int>(m_clips.size());
    const bool enabled = hasClip && !m_exporting;
    const bool videoClip =
        hasClip && !m_clips[static_cast<size_t>(m_selection)].IsAudioOnly();

    m_syncingClipSettings = true;

    if (hasClip) {
        const ClipInfo& clip = m_clips[static_cast<size_t>(m_selection)];
        m_chkNegate.SetCheck(clip.negate ? BST_CHECKED : BST_UNCHECKED);
        m_chkFlipH.SetCheck(clip.flipH ? BST_CHECKED : BST_UNCHECKED);
        m_chkFlipV.SetCheck(clip.flipV ? BST_CHECKED : BST_UNCHECKED);

        int selection = 3;
        for (int i = 0; i < static_cast<int>(m_speedValues.size()); ++i) {
            if (std::fabs(m_speedValues[static_cast<size_t>(i)] - clip.EffectiveSpeed()) < 1e-6) {
                selection = i;
                break;
            }
        }
        m_speedCombo.SetCurSel(selection);

        const int track = clip.EffectiveTrack();
        m_trackCombo.SetCurSel(track);

        const auto toSlider = [](double fraction, int minimum, int maximum) {
            int value = static_cast<int>(std::llround(fraction * 100.0));
            return (std::max)(minimum, (std::min)(value, maximum));
        };

        m_volumeSlider.SetPos(toSlider(clip.EffectiveVolume(), 0, 200));

        CString label;
        label.Format(L"Clip %d: %s", m_selection + 1, clip.fileName.c_str());
        m_filterLabel.SetWindowText(label);
    }
    else {
        m_chkNegate.SetCheck(BST_UNCHECKED);
        m_chkFlipH.SetCheck(BST_UNCHECKED);
        m_chkFlipV.SetCheck(BST_UNCHECKED);
        m_speedCombo.SetCurSel(3);
        m_trackCombo.SetCurSel(0);
        m_volumeSlider.SetPos(100);
        m_filterLabel.SetWindowText(L"Ajustes (sin clip)");
    }

    m_syncingClipSettings = false;
    UpdateSliderLabels();

    m_chkNegate.EnableWindow(enabled ? TRUE : FALSE);
    m_chkFlipH.EnableWindow(enabled ? TRUE : FALSE);
    m_chkFlipV.EnableWindow(enabled ? TRUE : FALSE);
    m_speedCombo.EnableWindow(enabled ? TRUE : FALSE);
    m_trackCombo.EnableWindow(videoClip && !m_exporting ? TRUE : FALSE);
    m_volumeSlider.EnableWindow(enabled ? TRUE : FALSE);
    m_btnCenter.EnableWindow(videoClip && !m_exporting ? TRUE : FALSE);
}

void CMainFrame::UpdateSliderLabels()
{
    CString text;

    std::wstring scaleText = L"100%";
    if (m_selection >= 0 && m_selection < static_cast<int>(m_clips.size())) {
        scaleText = ScaleText(m_clips[static_cast<size_t>(m_selection)]);
    }
    m_scaleValue.SetWindowText(scaleText.c_str());

    text.Format(L"%d%%", m_volumeSlider.GetPos());
    m_volumeValue.SetWindowText(text);
}

// Aplica la escala y la posicion de los deslizadores al frame que se esta
// viendo, sin reabrir los clips ni mover la seleccion.
void CMainFrame::ApplyTransformToPreview()
{
    if (m_selection < 0 || m_selection >= static_cast<int>(m_clips.size())) {
        return;
    }

    const ClipInfo& clip = m_clips[static_cast<size_t>(m_selection)];
    m_preview.SetClipTransform(m_selection, clip.EffectiveScaleX(), clip.EffectiveScaleY(),
                               clip.posX, clip.posY);

    if (m_preview.IsOpen()) {
        m_preview.RenderAt(m_preview.CurrentTime());
        m_previewView.Refresh();
    }
}

void CMainFrame::ApplyTimelineZoom(int position)
{
    position = (std::max)(100, (std::min)(position, 1200));

    m_syncingZoom = true;
    m_zoomSlider.SetPos(position);
    m_syncingZoom = false;

    m_timeline.SetZoom(position / 100.0);

    CString text;
    text.Format(L"%d%%", position);
    m_zoomValue.SetWindowText(text);
}

// Devuelve el clip de video mas alto que cubre el punto indicado en el lienzo
// de la vista previa (coordenadas 1280x720). Se usa para arrastrar el video
// con el raton en lugar de moverlo con deslizadores.
int CMainFrame::TopVideoClipAtCanvasPoint(double x, double y) const
{
    constexpr double canvasWidth = 1280.0;
    constexpr double canvasHeight = 720.0;

    if (!m_preview.IsOpen()) {
        return -1;
    }

    const double time = m_preview.CurrentTime();
    int best = -1;
    int bestTrack = -1;

    for (int i = 0; i < static_cast<int>(m_clips.size()); ++i) {
        const ClipInfo& clip = m_clips[static_cast<size_t>(i)];
        if (clip.IsAudioOnly() || clip.width <= 0 || clip.height <= 0) {
            continue;
        }

        if (time + 1e-6 < clip.start || time >= clip.End() - 1e-6) {
            continue;
        }

        const double fit = (std::min)(canvasWidth / clip.width, canvasHeight / clip.height);
        const double drawW = clip.width * fit * clip.EffectiveScaleX();
        const double drawH = clip.height * fit * clip.EffectiveScaleY();
        const double centerX = canvasWidth * 0.5 + clip.posX * canvasWidth;
        const double centerY = canvasHeight * 0.5 + clip.posY * canvasHeight;

        if (x + 1e-6 < centerX - drawW * 0.5 || x - 1e-6 > centerX + drawW * 0.5 ||
            y + 1e-6 < centerY - drawH * 0.5 || y - 1e-6 > centerY + drawH * 0.5) {
            continue;
        }

        const int track = clip.EffectiveTrack();
        if (track >= bestTrack) {
            bestTrack = track;
            best = i;
        }
    }

    return best;
}

bool CMainFrame::GetSelectionCanvasRect(double& left, double& top, double& right,
                                        double& bottom) const
{
    constexpr double canvasWidth = 1280.0;
    constexpr double canvasHeight = 720.0;

    left = 0.0;
    top = 0.0;
    right = 0.0;
    bottom = 0.0;

    // En la pestana de Textos el contorno rojo corresponde a la caja del texto.
    if (m_tabIndex == 4) {
        const TextInfo* text = SelectedText();
        if (text == nullptr) {
            return false;
        }
        left = text->x;
        top = text->y;
        right = text->x + text->width;
        bottom = text->y + text->height;
        return right > left && bottom > top;
    }

    if (!m_preview.IsOpen() || m_selection < 0 ||
        m_selection >= static_cast<int>(m_clips.size())) {
        return false;
    }

    const ClipInfo& clip = m_clips[static_cast<size_t>(m_selection)];
    if (clip.IsAudioOnly() || clip.width <= 0 || clip.height <= 0) {
        return false;
    }

    const double time = m_preview.CurrentTime();
    if (time + 1e-6 < clip.start || time >= clip.End() - 1e-6) {
        return false;
    }

    const double fit = (std::min)(canvasWidth / clip.width, canvasHeight / clip.height);
    const double drawW = clip.width * fit * clip.EffectiveScaleX();
    const double drawH = clip.height * fit * clip.EffectiveScaleY();
    const double centerX = canvasWidth * 0.5 + clip.posX * canvasWidth;
    const double centerY = canvasHeight * 0.5 + clip.posY * canvasHeight;

    left = centerX - drawW * 0.5;
    top = centerY - drawH * 0.5;
    right = centerX + drawW * 0.5;
    bottom = centerY + drawH * 0.5;
    return right > left && bottom > top;
}

bool CMainFrame::PreviewDragBegin(double x, double y, int zone)
{
    if (m_exporting) {
        return false;
    }

    if (m_tabIndex == 4) {
        TextInfo* text = SelectedText();
        if (text == nullptr || zone < 0 || zone > 8) {
            return false;
        }
        if (zone == 0 &&
            (x < text->x || x > text->x + text->width ||
             y < text->y || y > text->y + text->height)) {
            return false;
        }

        StopPlayback();
        m_snapXActive = false;
        m_snapYActive = false;
        m_previewView.SetSnapGuides(false, 0.0, false, 0.0);

        m_textDragging = true;
        m_dragText = m_textSelection;
        m_dragTextZone = zone;
        m_textDragStartX = x;
        m_textDragStartY = y;
        m_textDragBoxX = text->x;
        m_textDragBoxY = text->y;
        m_textDragBoxW = text->width;
        m_textDragBoxH = text->height;
        m_previewDragUndoPushed = false;

        if (zone > 0) {
            const double right = m_textDragBoxX + m_textDragBoxW;
            const double bottom = m_textDragBoxY + m_textDragBoxH;
            const double centerX = m_textDragBoxX + m_textDragBoxW * 0.5;
            const double centerY = m_textDragBoxY + m_textDragBoxH * 0.5;
            switch (zone) {
            case 1: m_resizeAnchorX = right; m_resizeAnchorY = bottom; break;
            case 2: m_resizeAnchorX = m_textDragBoxX; m_resizeAnchorY = bottom; break;
            case 3: m_resizeAnchorX = right; m_resizeAnchorY = m_textDragBoxY; break;
            case 4: m_resizeAnchorX = m_textDragBoxX; m_resizeAnchorY = m_textDragBoxY; break;
            case 5: m_resizeAnchorX = right; m_resizeAnchorY = centerY; break;
            case 6: m_resizeAnchorX = centerX; m_resizeAnchorY = bottom; break;
            case 7: m_resizeAnchorX = m_textDragBoxX; m_resizeAnchorY = centerY; break;
            default: m_resizeAnchorX = centerX; m_resizeAnchorY = m_textDragBoxY; break;
            }
        }

        SetStatus(zone == 0 ? L"Arrastra para mover la caja de texto."
                            : L"Arrastra para cambiar el tamano de la caja de texto.");
        return true;
    }

    int index = -1;
    if (zone == 0) {
        m_snapXActive = false;
        m_snapYActive = false;
        m_snapXKind = -1;
        m_snapYKind = -1;
        m_snapXValue = 0.0;
        m_snapYValue = 0.0;
        m_previewView.SetSnapGuides(false, 0.0, false, 0.0);

        index = TopVideoClipAtCanvasPoint(x, y);
        if (index < 0) {
            return false;
        }
        StopPlayback();
        if (index != m_selection) {
            SetListSelection(index);
        }
    }
    else {
        if (zone < 1 || zone > 8) {
            return false;
        }

        double left = 0.0;
        double top = 0.0;
        double right = 0.0;
        double bottom = 0.0;
        if (!GetSelectionCanvasRect(left, top, right, bottom)) {
            return false;
        }
        index = m_selection;
        if (index < 0 || index >= static_cast<int>(m_clips.size())) {
            return false;
        }
        StopPlayback();

        const ClipInfo& clip = m_clips[static_cast<size_t>(index)];
        m_dragStartScale = clip.scale;
        m_dragStartScaleX = clip.scaleX;
        m_dragStartScaleY = clip.scaleY;
        m_resizeStartW = right - left;
        m_resizeStartH = bottom - top;
        m_resizeStartCenterX = (left + right) * 0.5;
        m_resizeStartCenterY = (top + bottom) * 0.5;

        // El borde opuesto (o la esquina opuesta) queda fijo al arrastrar.
        switch (zone) {
        case 1: m_resizeAnchorX = right; m_resizeAnchorY = bottom; break;
        case 2: m_resizeAnchorX = left;  m_resizeAnchorY = bottom; break;
        case 3: m_resizeAnchorX = right; m_resizeAnchorY = top;    break;
        case 4: m_resizeAnchorX = left;  m_resizeAnchorY = top;    break;
        case 5: m_resizeAnchorX = right; m_resizeAnchorY = m_resizeStartCenterY; break;
        case 6: m_resizeAnchorX = m_resizeStartCenterX; m_resizeAnchorY = bottom; break;
        case 7: m_resizeAnchorX = left;  m_resizeAnchorY = m_resizeStartCenterY; break;
        default: m_resizeAnchorX = m_resizeStartCenterX; m_resizeAnchorY = top; break;
        }
    }

    const ClipInfo& clip = m_clips[static_cast<size_t>(index)];
    m_previewDragUndoPushed = false;
    m_previewDragging = true;
    m_dragClip = index;
    m_dragZone = zone;
    m_dragStartCanvasX = x;
    m_dragStartCanvasY = y;
    m_dragStartPosX = clip.posX;
    m_dragStartPosY = clip.posY;

    if (zone == 0) {
        SetStatus(L"Arrastra el clip para colocarlo donde quieras.");
    }
    else if (zone >= 5) {
        SetStatus(L"Arrastra el punto medio del borde para hacer el video mas ancho o mas alto.");
    }
    else {
        SetStatus(L"Arrastra la esquina para cambiar el tamaño del clip.");
    }
    return true;
}

void CMainFrame::PreviewDragMove(double x, double y)
{
    constexpr double canvasWidth = 1280.0;
    constexpr double canvasHeight = 720.0;

    if (m_textDragging) {
        if (m_dragText < 0 || m_dragText >= static_cast<int>(m_texts.size())) {
            return;
        }
        if (!m_previewDragUndoPushed) {
            PushUndo();
            m_previewDragUndoPushed = true;
        }

        TextInfo& text = m_texts[static_cast<size_t>(m_dragText)];
        if (m_dragTextZone == 0) {
            text.x = m_textDragBoxX + (x - m_textDragStartX);
            text.y = m_textDragBoxY + (y - m_textDragStartY);
            text.Normalize();
            RefreshTextPreview();
            SetStatus(L"Texto movido dentro del lienzo.");
            return;
        }

        const double signX = (m_dragTextZone == 1 || m_dragTextZone == 3 ||
                              m_dragTextZone == 5) ? -1.0 : 1.0;
        const double signY = (m_dragTextZone == 1 || m_dragTextZone == 2 ||
                              m_dragTextZone == 6) ? -1.0 : 1.0;
        const bool corner = m_dragTextZone >= 1 && m_dragTextZone <= 4;
        const bool horizontal = m_dragTextZone == 5 || m_dragTextZone == 7;
        const bool vertical = m_dragTextZone == 6 || m_dragTextZone == 8;

        const double dx = (std::max)(8.0, (x - m_resizeAnchorX) * signX);
        const double dy = (std::max)(8.0, (y - m_resizeAnchorY) * signY);

        if (corner) {
            const double ratio = (std::max)(dx / (std::max)(1.0, m_textDragBoxW),
                                            dy / (std::max)(1.0, m_textDragBoxH));
            text.width = m_textDragBoxW * ratio;
            text.height = m_textDragBoxH * ratio;
        }
        else if (horizontal) {
            text.width = dx;
        }
        else {
            text.height = dy;
        }

        if (horizontal) {
            text.x = signX > 0.0 ? m_resizeAnchorX : m_resizeAnchorX - text.width;
            text.y = m_textDragBoxY;
        }
        else if (vertical) {
            text.x = m_textDragBoxX;
            text.y = signY > 0.0 ? m_resizeAnchorY : m_resizeAnchorY - text.height;
        }
        else {
            text.x = signX > 0.0 ? m_resizeAnchorX : m_resizeAnchorX - text.width;
            text.y = signY > 0.0 ? m_resizeAnchorY : m_resizeAnchorY - text.height;
        }
        text.Normalize();

        RefreshTextPreview();
        CString status;
        status.Format(L"Caja de texto: %d x %d", static_cast<int>(text.width + 0.5),
                      static_cast<int>(text.height + 0.5));
        SetStatus(status.GetString());
        return;
    }

    if (!m_previewDragging || m_dragClip < 0 ||
        m_dragClip >= static_cast<int>(m_clips.size())) {
        return;
    }

    if (!m_previewDragUndoPushed) {
        PushUndo();
        m_previewDragUndoPushed = true;
    }

    ClipInfo& clip = m_clips[static_cast<size_t>(m_dragClip)];

    if (m_dragZone == 0) {
        const double rawX = m_dragStartPosX + (x - m_dragStartCanvasX) / canvasWidth;
        const double rawY = m_dragStartPosY + (y - m_dragStartCanvasY) / canvasHeight;

        const double fit = (std::min)(canvasWidth / clip.width, canvasHeight / clip.height);
        const double drawW = clip.width * fit * clip.EffectiveScaleX();
        const double drawH = clip.height * fit * clip.EffectiveScaleY();

        double centerX = canvasWidth * 0.5 + rawX * canvasWidth;
        double centerY = canvasHeight * 0.5 + rawY * canvasHeight;

        // El iman se activa al acercarse a un borde o al centro del lienzo y se
        // libera si el cursor sigue avanzando mas alla del margen.
        constexpr double snapThreshold = 28.0;
        constexpr double escapeThreshold = 80.0;
        centerX = SnapAxis(centerX, canvasWidth, drawW, m_snapXActive, m_snapXKind,
                           m_snapXValue, snapThreshold, escapeThreshold);
        centerY = SnapAxis(centerY, canvasHeight, drawH, m_snapYActive, m_snapYKind,
                           m_snapYValue, snapThreshold, escapeThreshold);

        clip.posX = (std::max)(-1.0, (std::min)((centerX - canvasWidth * 0.5) / canvasWidth, 1.0));
        clip.posY = (std::max)(-1.0, (std::min)((centerY - canvasHeight * 0.5) / canvasHeight, 1.0));

        m_previewView.SetSnapGuides(m_snapXActive, m_snapXValue,
                                    m_snapYActive, m_snapYValue);
        ApplyTransformToPreview();

        const bool aligned = m_snapXActive || m_snapYActive;
        CString status;
        if (aligned) {
            status.Format(L"Alineado con %s. Posicion: X %d%%   Y %d%%",
                          SnapDescription(m_snapXKind, m_snapYKind).c_str(),
                          static_cast<int>(std::lround(clip.posX * 100.0)),
                          static_cast<int>(std::lround(clip.posY * 100.0)));
        }
        else {
            status.Format(L"Posicion: X %d%%   Y %d%%",
                          static_cast<int>(std::lround(clip.posX * 100.0)),
                          static_cast<int>(std::lround(clip.posY * 100.0)));
        }
        SetStatus(status.GetString());
        return;
    }

    if (clip.width <= 0 || clip.height <= 0) {
        return;
    }

    const bool corner = m_dragZone >= 1 && m_dragZone <= 4;
    const bool horizontal = m_dragZone == 5 || m_dragZone == 7;
    const double signX = (m_dragZone == 1 || m_dragZone == 3 || m_dragZone == 5) ? -1.0 : 1.0;
    const double signY = (m_dragZone == 1 || m_dragZone == 2 || m_dragZone == 6) ? -1.0 : 1.0;

    const double fit = (std::min)(canvasWidth / clip.width, canvasHeight / clip.height);
    const double baseW = clip.width * fit;
    const double baseH = clip.height * fit;

    double drawW = (std::max)(1.0, m_resizeStartW);
    double drawH = (std::max)(1.0, m_resizeStartH);
    double centerX = m_resizeStartCenterX;
    double centerY = m_resizeStartCenterY;

    if (corner) {
        // Las esquinas conservan la proporcion usando el eje mas movido.
        const double dx = (std::max)(8.0, (x - m_resizeAnchorX) * signX);
        const double dy = (std::max)(8.0, (y - m_resizeAnchorY) * signY);
        const double ratioX = dx / drawW;
        const double ratioY = dy / drawH;
        const double ratio = (std::max)(ratioX, ratioY);
        clip.scale = (std::max)(0.05, (std::min)(m_dragStartScale * ratio, 4.0));
        drawW = baseW * clip.EffectiveScaleX();
        drawH = baseH * clip.EffectiveScaleY();
        centerX = m_resizeAnchorX + signX * drawW * 0.5;
        centerY = m_resizeAnchorY + signY * drawH * 0.5;
    }
    else if (horizontal) {
        const double dx = (std::max)(8.0, (x - m_resizeAnchorX) * signX);
        clip.scaleX = (std::max)(0.05, (std::min)(m_dragStartScaleX * dx / drawW, 4.0));
        drawW = baseW * clip.EffectiveScaleX();
        centerX = m_resizeAnchorX + signX * drawW * 0.5;
    }
    else {
        const double dy = (std::max)(8.0, (y - m_resizeAnchorY) * signY);
        clip.scaleY = (std::max)(0.05, (std::min)(m_dragStartScaleY * dy / drawH, 4.0));
        drawH = baseH * clip.EffectiveScaleY();
        centerY = m_resizeAnchorY + signY * drawH * 0.5;
    }

    clip.posX = (std::max)(-1.0, (std::min)((centerX - canvasWidth * 0.5) / canvasWidth, 1.0));
    clip.posY = (std::max)(-1.0, (std::min)((centerY - canvasHeight * 0.5) / canvasHeight, 1.0));

    UpdateSliderLabels();
    ApplyTransformToPreview();

    const std::wstring label = ScaleText(clip);
    CString status;
    status.Format(L"Tamaño: %s", label.c_str());
    SetStatus(status.GetString());
}

void CMainFrame::PreviewDragEnd()
{
    if (m_textDragging) {
        m_textDragging = false;
        m_dragText = -1;
        m_dragTextZone = 0;
        SetStatus(L"Caja de texto actualizada. Arrastra esquinas o bordes para ajustarla.");
        return;
    }

    if (!m_previewDragging) {
        return;
    }

    const bool resized = m_dragZone != 0;
    m_previewDragging = false;
    m_dragClip = -1;
    m_dragZone = 0;

    if (m_snapXActive || m_snapYActive) {
        m_snapXActive = false;
        m_snapYActive = false;
        m_previewView.SetSnapGuides(false, 0.0, false, 0.0);
    }

    if (resized) {
        const std::wstring label = m_selection >= 0 &&
                                           m_selection < static_cast<int>(m_clips.size())
                                       ? ScaleText(m_clips[static_cast<size_t>(m_selection)])
                                       : L"100%";
        CString status;
        status.Format(L"Tamaño aplicado: %s. Arrastra esquinas o puntos medios para ajustar.",
                      label.c_str());
        SetStatus(status.GetString());
    }
    else {
        SetStatus(L"Clip movido. Arrastra una esquina o un punto medio del contorno rojo "
                  L"para cambiar su tamaño.");
    }
}

void CMainFrame::UpdateTotalLabel()
{
    const double total = TimelineDuration(m_clips, m_texts);

    CString text;
    text.Format(L"Duracion: %s", FormatDuration(total).c_str());
    m_total.SetWindowText(text);
}

void CMainFrame::UpdateTimeLabel()
{
    const double duration = m_preview.IsOpen() ? m_preview.Duration() : 0.0;
    const double current = m_preview.IsOpen() ? m_preview.CurrentTime() : 0.0;

    CString text;
    text.Format(L"%s / %s", FormatTimecode(current, kOutputFps).c_str(),
                FormatTimecode(duration, kOutputFps).c_str());
    m_timeLabel.SetWindowText(text);
}

void CMainFrame::UpdatePlayButton()
{
    m_btnPlay.SetWindowText(m_playing ? L"Pausar" : L"Reproducir");
}

// Clip visible bajo el playhead: prioriza la capa de video mas alta.
int CMainFrame::ClipIndexAt(double outputTime) const
{
    int best = -1;
    int bestScore = -1;

    for (size_t i = 0; i < m_clips.size(); ++i) {
        const ClipInfo& clip = m_clips[i];
        if (outputTime + 1e-6 < clip.start || outputTime >= clip.End() - 1e-6) {
            continue;
        }
        const bool video = !clip.IsAudioOnly();
        const int score = video ? clip.EffectiveTrack() + 10 : 0;
        if (score >= bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }

    return best;
}

double CMainFrame::ClipStartTime(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_clips.size())) {
        return 0.0;
    }
    return (std::max)(0.0, m_clips[static_cast<size_t>(index)].start);
}

// Final del ultimo clip de una capa; se usa para colocar clips nuevos.
double CMainFrame::TrackEndTime(int track) const
{
    double end = 0.0;
    for (const ClipInfo& clip : m_clips) {
        if (clip.EffectiveTrack() != track) {
            continue;
        }
        end = (std::max)(end, clip.End());
    }
    return end;
}

bool CMainFrame::RebuildPreview(double time, bool resetTime)
{
    StopPlayback();

    std::wstring audioError;
    m_audio.Open(m_clips, audioError);

    if (resetTime) {
        time = 0.0;
    }

    m_preview.Close();
    m_previewView.SetPlayer(nullptr);
    m_previewView.Refresh();

    if (m_clips.empty() && m_texts.empty()) {
        m_timeline.SetPlayhead(0);
        UpdateTimeLabel();
        UpdatePlayButton();
        return false;
    }

    // Se pasa antes de Open para permitir un proyecto solo de texto.
    m_preview.SetTexts(m_texts);

    std::wstring error;
    if (!m_preview.Open(m_clips, m_transitions, error)) {
        SetStatus(error, true);
        UpdateTimeLabel();
        return false;
    }

    m_preview.SetTexts(m_texts);
    m_previewView.SetPlayer(&m_preview);
    SeekTo(time);
    UpdatePlayButton();
    return true;
}

void CMainFrame::SeekTo(double time)
{
    if (!m_preview.IsOpen()) {
        UpdateTimeLabel();
        return;
    }

    if (!m_preview.RenderAt(time)) {
        return;
    }

    const double actual = m_preview.CurrentTime();
    m_timeline.SetPlayhead(actual);
    m_previewView.Refresh();
    UpdateTimeLabel();
}

// True cuando el foco esta en uno de los campos de transformacion, para no
// interpretar las flechas como paso de frames mientras se escribe.
bool CMainFrame::IsTransformEditFocused() const
{
    const CWnd* focus = GetFocus();
    if (focus == nullptr) {
        return false;
    }

    const HWND handle = focus->GetSafeHwnd();
    return handle == m_volumeSlider.GetSafeHwnd() ||
           handle == m_zoomSlider.GetSafeHwnd() ||
           handle == m_transDurSlider.GetSafeHwnd() ||
           handle == m_txtContent.GetSafeHwnd() ||
           handle == m_txtSize.GetSafeHwnd();
}

// Mantiene seleccionado el clip que esta bajo el playhead, para que los
// ajustes (filtros y velocidad) se apliquen al frame/clip que se esta viendo.
void CMainFrame::SyncSelectionToTime(double time)
{
    const int index = ClipIndexAt(time);
    if (index >= 0 && index != m_selection) {
        SetListSelection(index);
    }
}

void CMainFrame::StepFrame(int direction)
{
    if (!m_preview.IsOpen()) {
        return;
    }

    StopPlayback();

    if (!m_preview.StepFrame(direction)) {
        return;
    }

    const double actual = m_preview.CurrentTime();
    SyncSelectionToTime(actual);
    m_timeline.SetPlayhead(actual);
    m_previewView.Refresh();
    UpdateTimeLabel();
}

void CMainFrame::TogglePlayback()
{
    if (!m_preview.IsOpen()) {
        if (m_clips.empty()) {
            MessageBox(L"Agrega al menos un video para reproducir.", L"Sin clips",
                       MB_OK | MB_ICONINFORMATION);
        }
        return;
    }

    if (m_playing) {
        StopPlayback();
        SetStatus(L"Reproduccion en pausa.");
        return;
    }

    if (m_preview.CurrentTime() >= m_preview.Duration() - 0.05) {
        SeekTo(0.0);
    }

    const double start = m_preview.CurrentTime();
    m_audio.Play(start);

    m_playing = true;
    m_playStartTick = GetTickCount64();
    m_playStartTime = start;
    SetTimer(kPlayTimer, 33, nullptr);
    UpdatePlayButton();
    SetStatus(L"Reproduciendo...");
}

void CMainFrame::StopPlayback()
{
    if (!m_playing) {
        return;
    }

    m_playing = false;
    KillTimer(kPlayTimer);
    m_audio.Pause();
    UpdatePlayButton();
}

void CMainFrame::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent != kPlayTimer) {
        CFrameWnd::OnTimer(nIDEvent);
        return;
    }

    if (!m_playing || !m_preview.IsOpen()) {
        StopPlayback();
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const double elapsed = static_cast<double>(now - m_playStartTick) / 1000.0;
    const double target = m_playStartTime + elapsed;

    if (target >= m_preview.Duration()) {
        SeekTo(m_preview.Duration() - 0.001);
        StopPlayback();
        SetStatus(L"Fin de la reproduccion.");
        return;
    }

    SeekTo(target);
    SyncSelectionToTime(m_preview.CurrentTime());
    m_audio.Sync(target);
    m_audio.Pump();
}

void CMainFrame::AddInitialFiles(const std::vector<std::wstring>& paths)
{
    if (paths.empty()) {
        return;
    }

    std::vector<std::wstring> valid;
    valid.reserve(paths.size());
    for (const std::wstring& path : paths) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            valid.push_back(path);
        }
    }

    AddPaths(valid);
}

void CMainFrame::GenerateThumbnail(const MediaItem& item, std::vector<unsigned char>& pixels,
                                   int& width, int& height)
{
    pixels.clear();
    width = 0;
    height = 0;

    if (item.IsAudioOnly()) {
        return;
    }

    ClipInfo clip;
    clip.path = item.path;
    clip.fileName = item.fileName;
    clip.duration = item.duration;
    clip.width = item.width;
    clip.height = item.height;
    clip.fps = item.fps;
    clip.hasAudio = item.hasAudio;

    std::vector<ClipInfo> single{clip};
    PreviewPlayer player;
    std::wstring error;
    if (!player.Open(single, error)) {
        return;
    }

    const double at = (std::min)(0.25, (std::max)(0.02, item.duration * 0.15));
    if (!player.RenderAt(at)) {
        return;
    }

    const std::vector<unsigned char>& source = player.Pixels();
    const int sourceWidth = player.Width();
    const int sourceHeight = player.Height();
    if (sourceWidth <= 0 || sourceHeight <= 0 ||
        source.size() < static_cast<size_t>(sourceWidth) * sourceHeight * 4u) {
        return;
    }

    width = kThumbWidth;
    height = kThumbHeight;
    pixels.assign(static_cast<size_t>(width) * height * 4u, 0);

    for (int y = 0; y < height; ++y) {
        const int sourceY = (std::min)(sourceHeight - 1, y * sourceHeight / height);
        for (int x = 0; x < width; ++x) {
            const int sourceX = (std::min)(sourceWidth - 1, x * sourceWidth / width);
            const size_t from = (static_cast<size_t>(sourceY) * sourceWidth + sourceX) * 4u;
            const size_t to = (static_cast<size_t>(y) * width + x) * 4u;
            std::copy_n(source.begin() + from, 4, pixels.begin() + to);
        }
    }
}

void CMainFrame::AddPaths(const std::vector<std::wstring>& paths)
{
    if (paths.empty()) {
        return;
    }

    std::wstring errors;
    int added = 0;
    int lastIndex = -1;

    for (const std::wstring& path : paths) {
        ClipInfo clip;
        std::wstring error;
        if (ProbeClip(path, clip, error)) {
            MediaItem item;
            item.id = m_nextMediaId++;
            item.path = clip.path;
            item.fileName = clip.fileName;
            item.duration = clip.duration;
            item.width = clip.width;
            item.height = clip.height;
            item.fps = clip.fps;
            item.hasAudio = clip.hasAudio;
            GenerateThumbnail(item, item.thumbnail, item.thumbnailWidth, item.thumbnailHeight);

            m_library.push_back(std::move(item));
            lastIndex = static_cast<int>(m_library.size()) - 1;
            ++added;
        }
        else {
            const size_t slash = path.find_last_of(L"\\/");
            const std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
            errors += L"- " + name + L": " + error + L"\n";
        }
    }

    if (!errors.empty()) {
        CString message;
        message.Format(L"No se pudieron agregar %d archivo(s):\n\n%s",
                       static_cast<int>(paths.size()) - added, errors.c_str());
        MessageBox(message, L"Archivos no validos", MB_OK | MB_ICONWARNING);
    }

    if (added > 0) {
        UpdateLibrary(lastIndex);

        CString status;
        status.Format(L"%d archivo(s) en la biblioteca. Pulsa + para agregarlos a una capa.",
                      added);
        SetStatus(status.GetString());
    }
}

void CMainFrame::AddMediaToTimeline(int libraryIndex)
{
    if (m_exporting || libraryIndex < 0 || libraryIndex >= static_cast<int>(m_library.size())) {
        return;
    }

    PushUndo();

    const MediaItem& item = m_library[static_cast<size_t>(libraryIndex)];

    int track = kAudioTrack;
    if (!item.IsAudioOnly()) {
        // El "+" coloca el video en la capa que se esta editando. Si no hay un
        // clip seleccionado se usa el selector "Capa:" de la pestana Clips.
        if (m_selection >= 0 && m_selection < static_cast<int>(m_clips.size())) {
            track = m_clips[static_cast<size_t>(m_selection)].EffectiveTrack();
        }
        else {
            track = m_trackCombo.GetCurSel();
        }
        if (track < 0 || track >= kVideoTrackCount) {
            track = 0;
        }
    }

    ClipInfo clip;
    clip.mediaId = item.id;
    clip.path = item.path;
    clip.fileName = item.fileName;
    clip.duration = item.duration;
    clip.width = item.width;
    clip.height = item.height;
    clip.fps = item.fps;
    clip.hasAudio = item.hasAudio;
    clip.track = track;
    clip.start = TrackEndTime(track);

    StopPlayback();
    m_clips.push_back(std::move(clip));
    const int index = static_cast<int>(m_clips.size()) - 1;

    const double time = m_preview.IsOpen() ? m_preview.CurrentTime() : 0.0;
    UpdateTotalLabel();
    UpdateLibrary(libraryIndex);
    UpdateTransitionTarget();
    UpdateTransitionList();
    RebuildPreview(time, m_clips.size() == 1);
    SetListSelection(index);

    CString status;
    status.Format(L"%s agregado a %s.", item.fileName.c_str(), TrackName(track).c_str());
    SetStatus(status.GetString());
}

LRESULT CMainFrame::OnLibrarySelectionChanged(WPARAM wParam, LPARAM)
{
    const int index = static_cast<int>(wParam);
    SetLibrarySelection(index);

    if (index >= 0 && index < static_cast<int>(m_library.size())) {
        const int mediaId = m_library[static_cast<size_t>(index)].id;
        for (size_t i = 0; i < m_clips.size(); ++i) {
            if (m_clips[i].mediaId == mediaId) {
                SetListSelection(static_cast<int>(i));
                break;
            }
        }
    }
    return 0;
}

LRESULT CMainFrame::OnLibraryAdd(WPARAM wParam, LPARAM)
{
    AddMediaToTimeline(static_cast<int>(wParam));
    return 0;
}

LRESULT CMainFrame::OnLibraryDoubleClick(WPARAM wParam, LPARAM)
{
    AddMediaToTimeline(static_cast<int>(wParam));
    return 0;
}

void CMainFrame::OnAddFiles()
{
    if (m_exporting) {
        return;
    }

    CFileDialog dialog(TRUE, nullptr, nullptr,
                       OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                       L"Videos y audio|*.mp4;*.mov;*.mkv;*.avi;*.webm;*.m4v;*.wmv;*.mpg;*.mpeg;*.ts;*.flv;*.3gp;*.mp3;*.wav;*.m4a;*.aac;*.flac;*.ogg|Todos los archivos|*.*||",
                       this);
    dialog.m_ofn.lpstrTitle = L"Agregar videos";

    if (dialog.DoModal() != IDOK) {
        return;
    }

    std::vector<std::wstring> paths;
    POSITION position = dialog.GetStartPosition();
    while (position != nullptr) {
        paths.push_back(dialog.GetNextPathName(position).GetString());
    }

    AddPaths(paths);
}

void CMainFrame::OnDropFiles(HDROP hDropInfo)
{
    const UINT count = DragQueryFileW(hDropInfo, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> paths;
    paths.reserve(count);

    for (UINT i = 0; i < count; ++i) {
        const UINT length = DragQueryFileW(hDropInfo, i, nullptr, 0);
        std::wstring path(length, L'\0');
        DragQueryFileW(hDropInfo, i, path.data(), length + 1);
        paths.push_back(path);
    }

    DragFinish(hDropInfo);
    AddPaths(paths);
}

void CMainFrame::OnRemoveClip()
{
    if (m_exporting || m_clips.empty()) {
        return;
    }

    int index = m_selection;
    if (index < 0 || index >= static_cast<int>(m_clips.size())) {
        MessageBox(L"Selecciona un clip en la linea de tiempo.", L"Quitar clip",
                   MB_OK | MB_ICONINFORMATION);
        return;
    }

    PushUndo();
    StopPlayback();
    m_clips.erase(m_clips.begin() + index);
    RemapTransitionsAfterRemove(index);
    UpdateTotalLabel();
    UpdateLibrary();
    UpdateTransitionTarget();
    UpdateTransitionList();
    RebuildPreview(0.0, true);
    SetStatus(L"Clip eliminado.");
}

void CMainFrame::OnMoveUp()
{
    const int index = m_selection;
    if (m_exporting || index < 0 || index >= static_cast<int>(m_clips.size())) {
        SetStatus(L"Selecciona un clip de video para subirlo de capa.", true);
        return;
    }

    ClipInfo& clip = m_clips[static_cast<size_t>(index)];
    if (clip.IsAudioOnly()) {
        SetStatus(L"El audio no se puede subir de capa.", true);
        return;
    }
    if (clip.EffectiveTrack() >= kVideoTrackCount - 1) {
        SetStatus(L"El clip ya esta en la capa mas alta (Capa 3).", true);
        return;
    }

    PushUndo();
    StopPlayback();
    const double time = m_preview.IsOpen() ? m_preview.CurrentTime() : clip.start;
    clip.track = clip.EffectiveTrack() + 1;
    DropTransitionsForClip(index);
    UpdateLibrary();
    RebuildPreview(time, false);
    SetStatus(L"Clip movido a " + TrackName(clip.EffectiveTrack()) + L".");
}

void CMainFrame::OnMoveDown()
{
    const int index = m_selection;
    if (m_exporting || index < 0 || index >= static_cast<int>(m_clips.size())) {
        SetStatus(L"Selecciona un clip de video para bajarlo de capa.", true);
        return;
    }

    ClipInfo& clip = m_clips[static_cast<size_t>(index)];
    if (clip.IsAudioOnly()) {
        SetStatus(L"El audio no se puede bajar de capa.", true);
        return;
    }
    if (clip.EffectiveTrack() <= 0) {
        SetStatus(L"El clip ya esta en la capa mas baja (Capa 1).", true);
        return;
    }

    PushUndo();
    StopPlayback();
    const double time = m_preview.IsOpen() ? m_preview.CurrentTime() : clip.start;
    clip.track = clip.EffectiveTrack() - 1;
    DropTransitionsForClip(index);
    UpdateLibrary();
    RebuildPreview(time, false);
    SetStatus(L"Clip movido a " + TrackName(clip.EffectiveTrack()) + L".");
}

void CMainFrame::OnSplit()
{
    if (m_exporting || m_clips.empty() || !m_preview.IsOpen()) {
        return;
    }

    StopPlayback();

    const double time = m_preview.CurrentTime();

    // Se divide el clip seleccionado si el playhead esta dentro; si no, el que
    // este visible bajo el playhead.
    int index = -1;
    if (m_selection >= 0 && m_selection < static_cast<int>(m_clips.size())) {
        const ClipInfo& selected = m_clips[static_cast<size_t>(m_selection)];
        if (time + 1e-6 >= selected.start && time < selected.End() - 1e-6) {
            index = m_selection;
        }
    }
    if (index < 0) {
        index = ClipIndexAt(time);
    }
    if (index < 0) {
        SetStatus(L"Coloca el playhead sobre un clip para cortarlo.", true);
        return;
    }

    ClipInfo& clip = m_clips[static_cast<size_t>(index)];
    const double start = clip.start;
    double split = clip.TrimStart() + (time - start) * clip.EffectiveSpeed();

    if (clip.fps > 0.0) {
        split = std::floor(split * clip.fps + 0.5) / clip.fps;
    }

    if (split - clip.TrimStart() < 0.1 || clip.TrimEnd() - split < 0.1) {
        SetStatus(L"No se puede cortar tan cerca del borde del clip.", true);
        return;
    }

    PushUndo();

    // El lado derecho hereda capa, posicion, escala, filtros y velocidad.
    ClipInfo right = clip;
    right.inPoint = split;
    right.outPoint = clip.TrimEnd();
    right.start = start + (split - clip.TrimStart()) / clip.EffectiveSpeed();
    clip.outPoint = split;

    m_clips.insert(m_clips.begin() + index + 1, right);
    DropTransitionsForClip(index);
    RemapTransitionsAfterInsert(index + 1);

    UpdateTotalLabel();
    UpdateLibrary();
    UpdateTransitionTarget();
    UpdateTransitionList();
    RebuildPreview(time, false);
    SetStatus(L"Clip cortado. Cada lado conserva sus propios ajustes.");
}

void CMainFrame::OnFilterChanged()
{
    if (m_syncingClipSettings || m_exporting) {
        return;
    }

    if (m_selection < 0 || m_selection >= static_cast<int>(m_clips.size())) {
        SetStatus(L"Selecciona un clip para aplicarle los filtros.", true);
        return;
    }

    PushUndo();
    ClipInfo& clip = m_clips[static_cast<size_t>(m_selection)];
    clip.negate = IsDlgButtonChecked(IDC_CHK_NEGATE) == BST_CHECKED;
    clip.flipH = IsDlgButtonChecked(IDC_CHK_FLIP_H) == BST_CHECKED;
    clip.flipV = IsDlgButtonChecked(IDC_CHK_FLIP_V) == BST_CHECKED;

    const double start = ClipStartTime(m_selection);
    const double end = start + clip.OutputDuration();
    double time = m_preview.IsOpen() ? m_preview.CurrentTime() : start;

    // Si el playhead esta fuera del clip editado, saltar a su inicio para que
    // el cambio de filtro se vea de inmediato.
    if (time < start || time >= end) {
        time = start;
    }

    RebuildPreview(time, false);

    CString status;
    status.Format(L"Filtros aplicados al clip %d: %s", m_selection + 1, clip.fileName.c_str());
    SetStatus(status.GetString());
}

void CMainFrame::OnSpeedChanged()
{
    if (m_syncingClipSettings || m_exporting) {
        return;
    }

    if (m_selection < 0 || m_selection >= static_cast<int>(m_clips.size())) {
        SetStatus(L"Selecciona un clip para cambiar su velocidad.", true);
        return;
    }

    const int choice = m_speedCombo.GetCurSel();
    if (choice < 0 || choice >= static_cast<int>(m_speedValues.size())) {
        return;
    }

    ClipInfo& clip = m_clips[static_cast<size_t>(m_selection)];
    const double newSpeed = m_speedValues[static_cast<size_t>(choice)];
    const double oldSpeed = clip.EffectiveSpeed();
    if (std::fabs(newSpeed - oldSpeed) < 1e-9) {
        return;
    }

    PushUndo();

    // Mantiene el punto de edicion dentro del mismo clip al cambiar la velocidad.
    double offset = 0.0;
    if (m_preview.IsOpen()) {
        offset = m_preview.CurrentTime() - ClipStartTime(m_selection);
        offset = (std::max)(0.0, (std::min)(offset, clip.OutputDuration()));
    }

    StopPlayback();
    clip.speed = newSpeed;

    const double newTime = ClipStartTime(m_selection) + offset * oldSpeed / newSpeed;

    UpdateTotalLabel();
    UpdateLibrary();
    RebuildPreview(newTime, false);

    CString status;
    status.Format(L"Velocidad %.4gx aplicada al clip %d: %s", clip.EffectiveSpeed(),
                  m_selection + 1, clip.fileName.c_str());
    SetStatus(status.GetString());
}

void CMainFrame::OnTrackChanged()
{
    if (m_syncingClipSettings || m_exporting) {
        return;
    }

    if (m_selection < 0 || m_selection >= static_cast<int>(m_clips.size())) {
        return;
    }

    ClipInfo& clip = m_clips[static_cast<size_t>(m_selection)];
    if (clip.IsAudioOnly()) {
        return;
    }

    const int track = m_trackCombo.GetCurSel();
    if (track < 0 || track >= kVideoTrackCount || track == clip.EffectiveTrack()) {
        return;
    }

    PushUndo();
    StopPlayback();
    const double time = m_preview.IsOpen() ? m_preview.CurrentTime() : clip.start;
    clip.track = track;
    DropTransitionsForClip(m_selection);
    UpdateLibrary();
    RebuildPreview(time, false);

    CString status;
    status.Format(L"Clip %d movido a %s.", m_selection + 1, TrackName(track).c_str());
    SetStatus(status.GetString());
}

void CMainFrame::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    if (pScrollBar == nullptr) {
        CFrameWnd::OnHScroll(nSBCode, nPos, pScrollBar);
        return;
    }

    const UINT id = static_cast<UINT>(pScrollBar->GetDlgCtrlID());

    // Zoom de la linea de tiempo: no toca los clips, solo la vista.
    if (id == IDC_TIMELINE_ZOOM_SLIDER) {
        if (!m_syncingZoom) {
            ApplyTimelineZoom(m_zoomSlider.GetPos());
        }
        return;
    }

    // La duracion de la transicion solo actualiza su etiqueta; la vista previa
    // se regenera al agregar la transicion.
    if (id == IDC_TRANS_DUR_SLIDER) {
        CString text;
        text.Format(L"%.1f s", m_transDurSlider.GetPos() / 10.0);
        m_transDurValue.SetWindowText(text);
        CFrameWnd::OnHScroll(nSBCode, nPos, pScrollBar);
        return;
    }

    // Duracion del texto: el deslizador se mide en decimas de segundo.
    if (id == IDC_TXT_DUR_SLIDER) {
        if (!m_syncingText && !m_exporting) {
            TextInfo* text = SelectedText();
            if (text != nullptr) {
                if (!m_textDurUndoPushed || !m_textDurDragging) {
                    PushUndo();
                    m_textDurUndoPushed = true;
                }
                text->duration = m_txtDurSlider.GetPos() / 10.0;

                CString value;
                value.Format(L"%.1f s", text->EffectiveDuration());
                m_txtDurValue.SetWindowText(value);
                RefreshTextPreview();
            }
        }
        CFrameWnd::OnHScroll(nSBCode, nPos, pScrollBar);
        return;
    }

    const bool volumeSlider = id == IDC_VOLUME_SLIDER;

    if (!volumeSlider || m_syncingClipSettings || m_exporting) {
        CFrameWnd::OnHScroll(nSBCode, nPos, pScrollBar);
        return;
    }

    if (m_selection < 0 || m_selection >= static_cast<int>(m_clips.size())) {
        return;
    }

    // Un arrastre del deslizador produce muchos mensajes: una sola instantanea
    // por gesto. Con teclado (sin arrastre) cada cambio cuenta como uno.
    if (!m_volumeUndoPushed || !m_volumeDragging) {
        PushUndo();
        m_volumeUndoPushed = true;
    }

    ClipInfo& clip = m_clips[static_cast<size_t>(m_selection)];
    clip.volume = m_volumeSlider.GetPos() / 100.0;

    UpdateSliderLabels();
    m_audio.SetClipVolume(m_selection, clip.EffectiveVolume());

    CString status;
    status.Format(L"Volumen del clip %d: %d%%", m_selection + 1, m_volumeSlider.GetPos());
    SetStatus(status.GetString());
}

void CMainFrame::OnCenterClip()
{
    if (m_exporting || m_selection < 0 || m_selection >= static_cast<int>(m_clips.size())) {
        SetStatus(L"Selecciona un clip para centrarlo.", true);
        return;
    }

    PushUndo();
    ClipInfo& clip = m_clips[static_cast<size_t>(m_selection)];
    StopPlayback();
    clip.ResetTransform();

    UpdateSliderLabels();
    ApplyTransformToPreview();
    SetStatus(L"Clip centrado y escala restablecida al 100%.");
}

void CMainFrame::OnPrevFrame()
{
    StepFrame(-1);
}

void CMainFrame::OnNextFrame()
{
    StepFrame(1);
}

void CMainFrame::OnPlayPause()
{
    TogglePlayback();
}

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
    // Un arrastre de los deslizadores genera muchos mensajes: se agrupan en
    // una sola entrada de deshacer usando los limites del gesto.
    const HWND volume = m_volumeSlider.GetSafeHwnd();
    if (volume != nullptr && pMsg->hwnd == volume) {
        if (pMsg->message == WM_LBUTTONDOWN) {
            m_volumeDragging = true;
            m_volumeUndoPushed = false;
        }
        else if (pMsg->message == WM_LBUTTONUP || pMsg->message == WM_CANCELMODE) {
            m_volumeDragging = false;
            m_volumeUndoPushed = false;
        }
    }

    const HWND textDuration = m_txtDurSlider.GetSafeHwnd();
    if (textDuration != nullptr && pMsg->hwnd == textDuration) {
        if (pMsg->message == WM_LBUTTONDOWN) {
            m_textDurDragging = true;
            m_textDurUndoPushed = false;
        }
        else if (pMsg->message == WM_LBUTTONUP || pMsg->message == WM_CANCELMODE) {
            m_textDurDragging = false;
            m_textDurUndoPushed = false;
        }
    }

    if (pMsg->message == WM_KEYDOWN && !m_exporting) {
        const bool transformFocused = IsTransformEditFocused();
        // Async para que el modificador se detecte tambien cuando el mensaje
        // de teclado llega de otro proceso (pruebas de automatizacion).
        const bool ctrl = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        if (ctrl && (pMsg->wParam == 'Z' || pMsg->wParam == 'z')) {
            if (shift) {
                OnRedo();
            }
            else {
                OnUndo();
            }
            return TRUE;
        }
        if (ctrl && (pMsg->wParam == 'Y' || pMsg->wParam == 'y')) {
            OnRedo();
            return TRUE;
        }
        if (pMsg->wParam == 'X' || pMsg->wParam == 'x') {
            OnSplit();
            return TRUE;
        }

        if (pMsg->wParam == VK_SPACE && !transformFocused) {
            TogglePlayback();
            return TRUE;
        }
        if ((pMsg->wParam == VK_LEFT || pMsg->wParam == VK_RIGHT) && !transformFocused) {
            const CWnd* focus = GetFocus();
            const bool listFocused = focus != nullptr &&
                                     focus->GetSafeHwnd() == m_libraryView.GetSafeHwnd();
            if (!listFocused) {
                StepFrame(pMsg->wParam == VK_RIGHT ? 1 : -1);
                return TRUE;
            }
        }
        if ((pMsg->wParam == 'K' || pMsg->wParam == 'k') && !transformFocused &&
            (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            OnSplit();
            return TRUE;
        }
        if (pMsg->wParam == VK_DELETE && m_timeline.GetSafeHwnd() == ::GetFocus()) {
            OnRemoveClip();
            return TRUE;
        }
    }

    return CFrameWnd::PreTranslateMessage(pMsg);
}

std::wstring CMainFrame::FindFfmpeg() const
{
    std::vector<std::filesystem::path> candidates;

    wchar_t executable[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0) {
        candidates.push_back(std::filesystem::path(executable).parent_path() / L"ffmpeg.exe");
    }

    wchar_t root[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"FFMPEG_ROOT", root, MAX_PATH) > 0) {
        candidates.push_back(std::filesystem::path(root) / L"bin" / L"ffmpeg.exe");
    }

    candidates.push_back(L"C:\\ffmpeg9\\msvc\\bin\\ffmpeg.exe");

    for (const std::filesystem::path& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate.wstring();
        }
    }

    return {};
}

std::wstring CMainFrame::BuildOutputResolution(int& width, int& height) const
{
    const int selection = m_resCombo.GetCurSel();

    switch (selection) {
    case 0: {
        // El lienzo de la vista previa es 16:9; "Original" conserva la
        // resolucion del primer video solo si tambien es 16:9.
        width = 1920;
        height = 1080;
        for (const ClipInfo& clip : m_clips) {
            if (clip.width <= 0 || clip.height <= 0) {
                continue;
            }
            const double aspect = static_cast<double>(clip.width) / clip.height;
            if (std::fabs(aspect - 16.0 / 9.0) < 0.02) {
                width = EvenDimension(clip.width);
                height = EvenDimension(clip.height);
            }
            break;
        }
        return L"Original";
    }
    case 1:
        width = 1280;
        height = 720;
        return L"720p";
    case 3:
        width = 2560;
        height = 1440;
        return L"1440p";
    case 4:
        width = 3840;
        height = 2160;
        return L"4K";
    default:
        width = 1920;
        height = 1080;
        return L"1080p";
    }
}

void CMainFrame::ClearTextOverlays()
{
    for (const std::wstring& path : m_textOverlayPaths) {
        ::DeleteFileW(path.c_str());
    }
    m_textOverlayPaths.clear();

    if (!m_textOverlayPath.empty()) {
        ::DeleteFileW(m_textOverlayPath.c_str());
        m_textOverlayPath.clear();
    }
}

void CMainFrame::OnExport()
{
    if (m_exporting) {
        return;
    }

    if (m_clips.empty() && m_texts.empty()) {
        MessageBox(L"Agrega al menos un video o un texto antes de exportar.", L"Sin contenido",
                   MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring ffmpegPath = FindFfmpeg();
    if (ffmpegPath.empty()) {
        MessageBox(L"No se encontro ffmpeg.exe.\n\n"
                   L"Copialo junto al ejecutable o comprueba que existe:\n"
                   L"C:\\ffmpeg9\\msvc\\bin\\ffmpeg.exe",
                   L"FFmpeg no encontrado", MB_OK | MB_ICONERROR);
        return;
    }

    CFileDialog dialog(FALSE, L"mp4", L"video_editado.mp4",
                       OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_EXPLORER,
                       L"Video MP4|*.mp4|Todos los archivos|*.*||", this);
    dialog.m_ofn.lpstrTitle = L"Guardar video exportado";

    if (dialog.DoModal() != IDOK) {
        return;
    }

    StopPlayback();

    ExportRequest request;
    request.settings = CurrentSettings();
    request.outputPath = dialog.GetPathName().GetString();
    request.ffmpegPath = ffmpegPath;
    request.inputPaths.reserve(m_clips.size());
    request.inPoints.reserve(m_clips.size());
    request.outPoints.reserve(m_clips.size());
    request.hasAudio.reserve(m_clips.size());
    request.speeds.reserve(m_clips.size());
    request.negates.reserve(m_clips.size());
    request.flipHs.reserve(m_clips.size());
    request.flipVs.reserve(m_clips.size());
    request.tracks.reserve(m_clips.size());
    request.starts.reserve(m_clips.size());
    request.scales.reserve(m_clips.size());
    request.scaleXs.reserve(m_clips.size());
    request.scaleYs.reserve(m_clips.size());
    request.posXs.reserve(m_clips.size());
    request.posYs.reserve(m_clips.size());
    request.volumes.reserve(m_clips.size());
    request.transitions = m_transitions;

    ClearTextOverlays();
    if (!m_texts.empty()) {
        wchar_t tempDirectory[MAX_PATH]{};
        if (::GetTempPathW(MAX_PATH, tempDirectory) == 0) {
            wcscpy_s(tempDirectory, MAX_PATH, L"C:\\");
        }

        for (size_t i = 0; i < m_texts.size(); ++i) {
            wchar_t fileName[96]{};
            swprintf_s(fileName, L"ve_text_%lu_%llu_%zu.bgra",
                       ::GetCurrentProcessId(),
                       static_cast<unsigned long long>(::GetTickCount64()), i);
            const std::wstring path = std::wstring(tempDirectory) + fileName;
            if (!SaveTextOverlayBgra({ m_texts[i] }, request.settings.width,
                                     request.settings.height, path)) {
                ClearTextOverlays();
                SetStatus(L"No se pudo generar la imagen de texto para la exportacion.", true);
                return;
            }
            m_textOverlayPaths.push_back(path);
        }
        request.texts = m_texts;
        request.textOverlayPaths = m_textOverlayPaths;
    }

    for (const ClipInfo& clip : m_clips) {
        request.inputPaths.push_back(clip.path);
        request.inPoints.push_back(clip.TrimStart());
        request.outPoints.push_back(clip.TrimEnd());
        request.hasAudio.push_back(clip.hasAudio);
        request.speeds.push_back(clip.EffectiveSpeed());
        request.negates.push_back(clip.negate);
        request.flipHs.push_back(clip.flipH);
        request.flipVs.push_back(clip.flipV);
        request.tracks.push_back(clip.EffectiveTrack());
        request.starts.push_back((std::max)(0.0, clip.start));
        request.scales.push_back(clip.EffectiveScale());
        request.scaleXs.push_back(clip.EffectiveScaleX());
        request.scaleYs.push_back(clip.EffectiveScaleY());
        request.posXs.push_back(clip.posX);
        request.posYs.push_back(clip.posY);
        request.volumes.push_back(clip.EffectiveVolume());
    }

    double totalDuration = 0.0;
    const std::wstring arguments = BuildFfmpegArguments(request, totalDuration);
    if (arguments.empty()) {
        ClearTextOverlays();
        return;
    }

    auto context = std::make_shared<ExportContext>();
    context->ffmpegPath = ffmpegPath;
    context->arguments = arguments;
    context->outputPath = request.outputPath;
    context->totalDuration = totalDuration;

    m_exportContext = context;
    SetExporting(true);
    m_progress.SetPos(0);

    CString status;
    status.Format(L"Exportando (%s)... 0%%", FormatDuration(totalDuration).c_str());
    SetStatus(status.GetString());

    const HWND window = GetSafeHwnd();
    m_exportThread = std::thread([context, window]() {
        const int code = RunFfmpeg(
            context->ffmpegPath,
            context->arguments,
            context->totalDuration,
            [window](double progress) {
                ::PostMessageW(window, WM_EXPORT_PROGRESS, 0,
                               static_cast<LPARAM>(progress * 1000.0));
            },
            context->cancel,
            context->process,
            context->log);

        context->exitCode = code;
        ::PostMessageW(window, WM_EXPORT_FINISHED, 0, 0);
    });
}

void CMainFrame::JoinExportThread()
{
    if (m_exportThread.joinable()) {
        m_exportThread.join();
    }
}

void CMainFrame::OnCancelExport()
{
    if (!m_exporting || !m_exportContext) {
        return;
    }

    m_exportContext->cancel = true;
    HANDLE process = m_exportContext->process.load();
    if (process != nullptr) {
        TerminateProcess(process, 1);
    }

    SetStatus(L"Cancelando exportacion...");
}

LRESULT CMainFrame::OnExportProgress(WPARAM, LPARAM lParam)
{
    const int value = (std::max)(0, (std::min)(1000, static_cast<int>(lParam)));
    m_progress.SetPos(value);

    CString status;
    status.Format(L"Exportando... %d%%", value / 10);
    SetStatus(status.GetString());

    return 0;
}

LRESULT CMainFrame::OnExportFinished(WPARAM, LPARAM)
{
    JoinExportThread();

    ClearTextOverlays();

    const std::shared_ptr<ExportContext> context = m_exportContext;
    m_exportContext.reset();
    SetExporting(false);

    if (!context) {
        return 0;
    }

    if (context->cancel.load()) {
        m_progress.SetPos(0);
        SetStatus(L"Exportacion cancelada.");
        return 0;
    }

    if (context->exitCode == 0) {
        m_progress.SetPos(1000);
        SetStatus(L"Video exportado correctamente: " + context->outputPath);

        CString message;
        message.Format(L"El video se exporto correctamente:\n\n%s\n\nQuieres abrir la carpeta?",
                       context->outputPath.c_str());
        if (MessageBox(message, L"Exportacion completada", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            const std::wstring parameters = L"/select,\"" + context->outputPath + L"\"";
            ::ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
        }
    }
    else {
        m_progress.SetPos(0);
        SetStatus(L"Error al exportar. Revisa el mensaje.", true);

        CString message;
        message.Format(L"ffmpeg termino con codigo %d.\n\n%s", context->exitCode,
                       context->log.empty() ? L"Sin detalles adicionales." : context->log.c_str());
        MessageBox(message, L"Error de exportacion", MB_OK | MB_ICONERROR);
    }

    return 0;
}

void CMainFrame::SetExporting(bool exporting)
{
    m_exporting = exporting;

    if (exporting) {
        StopPlayback();
    }

    const BOOL enable = exporting ? FALSE : TRUE;

    m_btnAdd.EnableWindow(enable);
    m_btnRemove.EnableWindow(enable);
    m_btnUp.EnableWindow(enable);
    m_btnDown.EnableWindow(enable);
    m_btnSplit.EnableWindow(enable);
    m_resCombo.EnableWindow(enable);
    m_btnExport.EnableWindow(enable);
    m_btnPrev.EnableWindow(enable);
    m_btnPlay.EnableWindow(enable);
    m_btnNext.EnableWindow(enable);
    m_libraryView.EnableWindow(enable);
    m_zoomSlider.EnableWindow(enable);
    if (m_txtList.GetSafeHwnd() != nullptr) {
        m_txtList.EnableWindow(enable);
        m_txtAdd.EnableWindow(enable);
        m_txtRemove.EnableWindow(enable);
        m_txtContent.EnableWindow(enable);
        m_txtSize.EnableWindow(enable);
        m_txtBold.EnableWindow(enable);
        m_txtItalic.EnableWindow(enable);
        m_txtUnderline.EnableWindow(enable);
        m_txtAlignCombo.EnableWindow(enable);
    }
    SyncClipControls();
    SyncTextControls();
    UpdateTransitionTarget();
    m_btnCancel.EnableWindow(exporting ? TRUE : FALSE);
    UpdateUndoButtons();
}

void CMainFrame::SetStatus(const std::wstring& text, bool error)
{
    m_statusColor = error ? Theme::kDanger : Theme::kTextDim;
    if (m_status.GetSafeHwnd() != nullptr) {
        m_status.SetWindowText(text.c_str());
        m_status.Invalidate();
    }
}

// Flecha curva tipica de historial: la de la izquierda (deshacer) recorre el
// arco en sentido antihorario y la de la derecha (rehacer) es su espejo.
void CMainFrame::DrawHistoryArrow(CDC& dc, const CRect& rect, bool undo, bool disabled)
{
    const int dpi = dc.GetDeviceCaps(LOGPIXELSY);
    const auto px = [dpi](int value) { return MulDiv(value, dpi, 96); };
    constexpr double kPi = 3.14159265358979323846;

    const int cx = rect.CenterPoint().x;
    const int cy = rect.CenterPoint().y;
    const double radius = px(6);

    const auto at = [&](double degrees) {
        const double rad = degrees * kPi / 180.0;
        double x = cx + radius * std::cos(rad);
        const double y = cy + radius * std::sin(rad);
        if (!undo) {
            x = 2.0 * cx - x;
        }
        return CPoint(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
    };

    CPen pen(PS_SOLID, px(2), disabled ? Theme::kTextFaint : Theme::kText);
    CPen* oldPen = dc.SelectObject(&pen);

    dc.MoveTo(at(45.0));
    for (double degrees = 35.0; degrees >= -175.0; degrees -= 10.0) {
        dc.LineTo(at(degrees));
    }

    // Tangente en el extremo para orientar la punta hacia abajo/afuera.
    const CPoint end = at(-175.0);
    const double rad = -175.0 * kPi / 180.0;
    double tx = std::sin(rad);
    const double ty = -std::cos(rad);
    if (!undo) {
        tx = -tx;
    }
    const double angle = std::atan2(ty, tx);
    const int head = px(5);
    for (const double spread : {2.6, -2.6}) {
        const double a = angle + spread;
        dc.MoveTo(end);
        dc.LineTo(CPoint(static_cast<int>(std::lround(end.x + head * std::cos(a))),
                         static_cast<int>(std::lround(end.y + head * std::sin(a)))));
    }

    dc.SelectObject(oldPen);
}

void CMainFrame::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (lpDrawItemStruct == nullptr) {
        return;
    }

    const UINT id = static_cast<UINT>(nIDCtl);
    const bool primary = id == IDC_BTN_ADD || id == IDC_BTN_EXPORT || id == IDC_BTN_PLAY ||
                         id == IDC_BTN_TRANS_ADD || id == IDC_BTN_SPLIT || id == IDC_TXT_ADD;
    const bool disabled = (lpDrawItemStruct->itemState & ODS_DISABLED) != 0;
    const bool pressed = (lpDrawItemStruct->itemState & ODS_SELECTED) != 0;

    CDC* dc = CDC::FromHandle(lpDrawItemStruct->hDC);
    if (dc == nullptr) {
        return;
    }

    const int dpi = dc->GetDeviceCaps(LOGPIXELSY);
    const auto px = [dpi](int value) { return MulDiv(value, dpi, 96); };

    CRect rect(lpDrawItemStruct->rcItem);

    COLORREF fill = Theme::kPanelAlt;
    COLORREF border = Theme::kBorder;
    COLORREF text = Theme::kText;

    if (disabled) {
        fill = RGB(30, 32, 39);
        border = Theme::kBorderSoft;
        text = Theme::kTextFaint;
    }
    else if (primary) {
        fill = pressed ? Theme::kAccentPressed : Theme::kAccent;
        border = fill;
        text = RGB(255, 255, 255);
    }
    else if (pressed) {
        fill = Theme::kPanelHover;
        border = Theme::kBorder;
    }

    dc->FillSolidRect(&rect, Theme::kBackground);

    const int radius = px(6);
    CBrush brush(fill);
    CPen pen(PS_SOLID, 1, border);
    CBrush* oldBrush = dc->SelectObject(&brush);
    CPen* oldPen = dc->SelectObject(&pen);
    dc->RoundRect(rect, CPoint(radius * 2, radius * 2));
    dc->SelectObject(oldPen);
    dc->SelectObject(oldBrush);

    if ((lpDrawItemStruct->itemState & ODS_FOCUS) != 0 && !disabled) {
        CRect focus = rect;
        focus.DeflateRect(px(2), px(2));
        CBrush focusBrush(primary ? Theme::kAccentHover : Theme::kBorder);
        CPen focusPen(PS_SOLID, 1, primary ? Theme::kAccentHover : Theme::kBorder);
        CBrush* previousBrush = dc->SelectObject(&focusBrush);
        CPen* previousPen = dc->SelectObject(&focusPen);
        dc->RoundRect(focus, CPoint(radius * 2, radius * 2));
        dc->SelectObject(previousPen);
        dc->SelectObject(previousBrush);
    }

    // Deshacer/rehacer son solo iconos; el texto del control se conserva para
    // accesibilidad y automatizacion, pero no se pinta.
    if (id == IDC_BTN_UNDO || id == IDC_BTN_REDO) {
        DrawHistoryArrow(*dc, rect, id == IDC_BTN_UNDO, disabled);
        return;
    }

    wchar_t caption[256]{};
    ::GetWindowTextW(lpDrawItemStruct->hwndItem, caption, _countof(caption));

    CFont* font = primary ? &m_fontBold : &m_fontUi;
    CFont* oldFont = dc->SelectObject(font);

    CRect textRect = rect;
    if (pressed) {
        textRect.OffsetRect(px(1), px(1));
    }

    dc->SetBkMode(TRANSPARENT);
    dc->SetTextColor(text);
    dc->DrawTextW(caption, &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    dc->SelectObject(oldFont);
}

void CMainFrame::OnTabsCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = CDRF_DODEFAULT;

    auto* custom = reinterpret_cast<LPNMCUSTOMDRAW>(pNMHDR);
    if (custom == nullptr || m_tabs.GetSafeHwnd() == nullptr) {
        return;
    }

    CDC* dc = CDC::FromHandle(custom->hdc);
    if (dc == nullptr) {
        return;
    }

    switch (custom->dwDrawStage) {
    case CDDS_PREPAINT: {
        CRect client;
        m_tabs.GetClientRect(&client);
        dc->FillSolidRect(&client, Theme::kBackground);
        dc->FillSolidRect(client.left, client.bottom - 1, client.Width(), 1, Theme::kBorderSoft);
        *pResult = CDRF_NOTIFYITEMDRAW;
        break;
    }
    case CDDS_ITEMPREPAINT: {
        const int index = static_cast<int>(custom->dwItemSpec);
        const bool selected = (m_tabs.GetCurSel() == index);
        CRect item(custom->rc);

        dc->FillSolidRect(&item, selected ? Theme::kPanel : Theme::kBackground);
        if (selected) {
            dc->FillSolidRect(item.left, item.bottom - 2, item.Width(), 2, Theme::kAccent);
        }

        wchar_t text[128]{};
        TCITEMW tabItem{};
        tabItem.mask = TCIF_TEXT;
        tabItem.pszText = text;
        tabItem.cchTextMax = _countof(text);
        m_tabs.GetItem(index, &tabItem);

        CFont* font = selected ? &m_fontBold : &m_fontUi;
        CFont* oldFont = dc->SelectObject(font);

        dc->SetBkMode(TRANSPARENT);
        dc->SetTextColor(selected ? Theme::kText : Theme::kTextDim);
        dc->DrawTextW(text, &item, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        dc->SelectObject(oldFont);

        *pResult = CDRF_SKIPDEFAULT;
        break;
    }
    default:
        break;
    }
}

HBRUSH CMainFrame::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    if (nCtlColor == CTLCOLOR_STATIC) {
        pDC->SetBkMode(TRANSPARENT);

        const UINT id = pWnd->GetDlgCtrlID();
        if (id == IDC_STATUS) {
            pDC->SetTextColor(m_statusColor);
        }
        else if (id == IDC_TOTAL_LABEL || id == IDC_TIME_LABEL) {
            pDC->SetTextColor(Theme::kTextDim);
        }
        else if (id == IDC_TRANS_HINT || id == IDC_EXPORT_HINT ||
                 id == IDC_TRANSFORM_HINT || id == IDC_TXT_HINT) {
            pDC->SetTextColor(Theme::kTextFaint);
        }
        else {
            pDC->SetTextColor(Theme::kText);
        }

        return static_cast<HBRUSH>(m_background.GetSafeHandle());
    }

    if (nCtlColor == CTLCOLOR_BTN) {
        pDC->SetBkMode(TRANSPARENT);
        pDC->SetTextColor(Theme::kText);
        return static_cast<HBRUSH>(m_background.GetSafeHandle());
    }

    if (nCtlColor == CTLCOLOR_EDIT || nCtlColor == CTLCOLOR_LISTBOX) {
        pDC->SetBkColor(Theme::kPanel);
        pDC->SetTextColor(Theme::kText);
        return static_cast<HBRUSH>(m_panelBrush.GetSafeHandle());
    }

    return CFrameWnd::OnCtlColor(pDC, pWnd, nCtlColor);
}

BOOL CMainFrame::OnEraseBkgnd(CDC* pDC)
{
    CRect rect;
    GetClientRect(&rect);
    pDC->FillSolidRect(&rect, Theme::kBackground);

    const int dpi = pDC->GetDeviceCaps(LOGPIXELSY);
    const auto px = [dpi](int value) { return MulDiv(value, dpi, 96); };

    // Banda superior con la marca de la aplicacion.
    const int bandHeight = px(46);
    const CRect band(rect.left, rect.top, rect.right, rect.top + bandHeight);
    pDC->FillSolidRect(&band, Theme::kPanel);
    pDC->FillSolidRect(rect.left, band.bottom, rect.Width(), 1, Theme::kBorderSoft);

    const int barHeight = px(18);
    pDC->FillSolidRect(px(12), rect.top + (bandHeight - barHeight) / 2, px(4), barHeight,
                       Theme::kAccent);

    return TRUE;
}

void CMainFrame::OnClose()
{
    if (m_exporting) {
        if (MessageBox(L"Hay una exportacion en curso.\n\nQuieres cancelarla y salir?",
                       L"Exportacion en curso", MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }

        if (m_exportContext) {
            m_exportContext->cancel = true;
            HANDLE process = m_exportContext->process.load();
            if (process != nullptr) {
                TerminateProcess(process, 1);
            }
        }
        JoinExportThread();
    }

    StopPlayback();
    CFrameWnd::OnClose();
}

void CMainFrame::OnDestroy()
{
    StopPlayback();
    JoinExportThread();
    m_audio.Close();
    m_preview.Close();
    ClearTextOverlays();
    CFrameWnd::OnDestroy();
}
