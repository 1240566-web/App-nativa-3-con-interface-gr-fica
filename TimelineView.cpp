#include "TimelineView.h"

#include <algorithm>
#include <cmath>

#include "Theme.h"

namespace {

constexpr COLORREF kBackground = Theme::kBackground;
constexpr COLORREF kRulerBackground = RGB(26, 28, 35);
constexpr COLORREF kRulerText = RGB(150, 156, 170);
constexpr COLORREF kRulerTick = RGB(64, 69, 82);
constexpr COLORREF kClipBorder = RGB(16, 18, 23);
constexpr COLORREF kPlayhead = RGB(255, 92, 92);
constexpr COLORREF kEmptyText = RGB(116, 123, 138);
constexpr COLORREF kGutterBackground = RGB(29, 32, 39);
constexpr COLORREF kGutterText = RGB(160, 167, 182);
constexpr COLORREF kRowEven = RGB(27, 30, 37);
constexpr COLORREF kRowOdd = RGB(23, 26, 32);
constexpr COLORREF kRowAudio = RGB(23, 34, 29);
constexpr COLORREF kScrollBackground = RGB(20, 22, 27);
constexpr COLORREF kTextFill = RGB(128, 92, 190);
constexpr COLORREF kTextFillSelected = RGB(158, 120, 220);
constexpr COLORREF kTextBorder = RGB(226, 210, 255);

std::wstring FormatRulerTime(double seconds)
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int total = static_cast<int>(seconds + 0.5);
    wchar_t buffer[32]{};
    if (total >= 3600) {
        swprintf_s(buffer, L"%d:%02d:%02d", total / 3600, (total % 3600) / 60, total % 60);
    }
    else {
        swprintf_s(buffer, L"%d:%02d", total / 60, total % 60);
    }
    return buffer;
}

COLORREF TrackColor(int track, bool selected)
{
    COLORREF base = RGB(45, 106, 178);
    switch (track) {
    case 1:
        base = RGB(52, 128, 196);
        break;
    case 2:
        base = RGB(68, 150, 214);
        break;
    case kAudioTrack:
        base = RGB(50, 138, 96);
        break;
    default:
        break;
    }

    if (!selected) {
        return base;
    }
    const int r = (std::min)(255, GetRValue(base) + 28);
    const int g = (std::min)(255, GetGValue(base) + 28);
    const int b = (std::min)(255, GetBValue(base) + 28);
    return RGB(r, g, b);
}

} // namespace

BEGIN_MESSAGE_MAP(CTimelineView, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_SIZE()
    ON_WM_SETCURSOR()
    ON_WM_CAPTURECHANGED()
    ON_WM_HSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_MOUSEHWHEEL()
    ON_MESSAGE(TV_GETCLIPSTART, &CTimelineView::OnGetClipStart)
    ON_MESSAGE(TV_GETVIEWVAL, &CTimelineView::OnGetViewVal)
    ON_MESSAGE(TV_GETTEXTSTART, &CTimelineView::OnGetTextStart)
    ON_MESSAGE(TV_GETTEXTDURATION, &CTimelineView::OnGetTextDuration)
    ON_MESSAGE(TV_GETTEXTTRACK, &CTimelineView::OnGetTextTrack)
END_MESSAGE_MAP()

LRESULT CTimelineView::OnGetClipStart(WPARAM wParam, LPARAM)
{
    const int index = static_cast<int>(wParam);
    if (m_clips == nullptr || index < 0 ||
        index >= static_cast<int>(m_clips->size())) {
        return -1;
    }
    return static_cast<LRESULT>(std::llround((*m_clips)[static_cast<size_t>(index)].start * 1000.0));
}

LRESULT CTimelineView::OnGetViewVal(WPARAM wParam, LPARAM)
{
    const double value = (wParam == 1) ? m_viewStart : ViewDuration();
    return static_cast<LRESULT>(std::llround(value * 1000.0));
}

LRESULT CTimelineView::OnGetTextStart(WPARAM wParam, LPARAM)
{
    const int index = static_cast<int>(wParam);
    if (m_texts == nullptr || index < 0 ||
        index >= static_cast<int>(m_texts->size())) {
        return -1;
    }
    return static_cast<LRESULT>(
        std::llround((*m_texts)[static_cast<size_t>(index)].start * 1000.0));
}

LRESULT CTimelineView::OnGetTextDuration(WPARAM wParam, LPARAM)
{
    const int index = static_cast<int>(wParam);
    if (m_texts == nullptr || index < 0 ||
        index >= static_cast<int>(m_texts->size())) {
        return -1;
    }
    return static_cast<LRESULT>(std::llround(
        (*m_texts)[static_cast<size_t>(index)].EffectiveDuration() * 1000.0));
}

LRESULT CTimelineView::OnGetTextTrack(WPARAM wParam, LPARAM)
{
    const int index = static_cast<int>(wParam);
    if (m_texts == nullptr || index < 0 ||
        index >= static_cast<int>(m_texts->size())) {
        return -1;
    }
    return (*m_texts)[static_cast<size_t>(index)].EffectiveTrack();
}

void CTimelineView::CreateView(CWnd* parent, UINT id)
{
    const LPCWSTR className = AfxRegisterWndClass(CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW,
                                                  ::LoadCursor(nullptr, IDC_ARROW),
                                                  static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)),
                                                  nullptr);
    CreateEx(0, className, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, CRect(0, 0, 10, 10),
             parent, id);

    m_scroll.Create(WS_CHILD | SBS_HORZ, CRect(0, 0, 10, 10), this, 3001);
    m_scroll.ShowWindow(SW_HIDE);
    Theme::EnableDarkControl(m_scroll.GetSafeHwnd());
}

void CTimelineView::Attach(std::vector<ClipInfo>* clips)
{
    m_clips = clips;
    if (m_clips == nullptr || m_clips->empty()) {
        m_selection = -1;
        m_playhead = 0.0;
    }
    else if (m_selection >= static_cast<int>(m_clips->size())) {
        m_selection = static_cast<int>(m_clips->size()) - 1;
    }
    UpdateViewDuration();
    Refresh();
}

void CTimelineView::AttachTransitions(std::vector<TransitionInfo>* transitions)
{
    m_transitions = transitions;
    Refresh();
}

void CTimelineView::AttachTexts(std::vector<TextInfo>* texts)
{
    m_texts = texts;
    if (m_texts == nullptr || m_texts->empty()) {
        m_textSelection = -1;
    }
    else if (m_textSelection >= static_cast<int>(m_texts->size())) {
        m_textSelection = static_cast<int>(m_texts->size()) - 1;
    }
    Refresh();
}

void CTimelineView::SetTextSelection(int index, bool redraw)
{
    m_textSelection = index;
    if (redraw) {
        Refresh();
    }
}

void CTimelineView::Refresh()
{
    if (m_drag == DragMode::None) {
        UpdateViewDuration();
    }
    if (GetSafeHwnd() != nullptr) {
        Invalidate(FALSE);
    }
}

void CTimelineView::SetPlayhead(double seconds, bool redraw)
{
    m_playhead = (std::max)(0.0, seconds);

    // Al acercar el zoom, la vista sigue al playhead durante la reproduccion.
    if (m_zoom > 1.0 + 1e-6) {
        const double margin = ViewDuration() * 0.12;
        if (m_playhead > m_viewStart + ViewDuration() - margin ||
            m_playhead < m_viewStart + margin) {
            m_viewStart = m_playhead - ViewDuration() * 0.35;
            ClampViewStart();
            UpdateScrollBar();
        }
    }

    if (redraw) {
        Refresh();
    }
}

void CTimelineView::SetSelection(int index, bool redraw)
{
    m_selection = index;
    if (redraw) {
        Refresh();
    }
}

void CTimelineView::SetZoom(double zoom)
{
    const double focus = m_playhead;
    m_zoom = (std::max)(1.0, (std::min)(zoom, 12.0));
    UpdateViewDuration();

    // Mantiene el playhead cerca del centro al acercar o alejar.
    m_viewStart = focus - m_viewDuration * 0.5;
    ClampViewStart();
    UpdateScrollBar();
    Refresh();
}

double CTimelineView::BaseDuration() const
{
    if (m_clips == nullptr) {
        return 6.0;
    }

    const double total = TotalDuration();
    double view = (std::max)(6.0, total * 1.15);
    if (m_playhead > view) {
        view = m_playhead * 1.05;
    }
    return view;
}

double CTimelineView::MaxViewStart() const
{
    if (m_zoom <= 1.0 + 1e-6) {
        return 0.0;
    }
    return (std::max)(0.0, m_baseDuration - ViewDuration());
}

void CTimelineView::ClampViewStart()
{
    m_viewStart = (std::max)(0.0, (std::min)(m_viewStart, MaxViewStart()));
}

void CTimelineView::UpdateViewDuration()
{
    m_baseDuration = BaseDuration();
    m_viewDuration = m_baseDuration / m_zoom;
    ClampViewStart();
    UpdateScrollBar();
}

void CTimelineView::UpdateScrollBar()
{
    if (m_scroll.GetSafeHwnd() == nullptr) {
        return;
    }

    CRect client;
    GetClientRect(&client);
    if (client.Width() <= 0 || client.Height() <= 0) {
        return;
    }

    const int scrollHeight = Pixel(13);
    m_scroll.MoveWindow(TrackLeft(), client.bottom - scrollHeight - Pixel(2),
                        TrackWidth(), scrollHeight);

    const double maxStart = MaxViewStart();
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = 1000;
    info.nPage = (maxStart > 0.0)
                     ? static_cast<UINT>(std::llround(1000.0 * ViewDuration() / m_baseDuration))
                     : 1001;
    info.nPos = (maxStart > 0.0)
                    ? static_cast<int>(std::llround(1000.0 * m_viewStart / maxStart))
                    : 0;
    m_scroll.SetScrollInfo(&info, TRUE);
    m_scroll.ShowWindow(maxStart > 0.0 ? SW_SHOW : SW_HIDE);
}

int CTimelineView::Pixel(int value) const
{
    UINT dpi = 96;
    if (GetSafeHwnd() != nullptr) {
        dpi = ::GetDpiForWindow(GetSafeHwnd());
        if (dpi == 0) {
            dpi = 96;
        }
    }
    return MulDiv(value, static_cast<int>(dpi), 96);
}

int CTimelineView::LabelWidth() const
{
    return Pixel(58);
}

int CTimelineView::RulerHeight() const
{
    return Pixel(24);
}

int CTimelineView::TrackTop() const
{
    return RulerHeight() + Pixel(4);
}

int CTimelineView::RowHeight() const
{
    CRect client;
    GetClientRect(&client);
    const int available =
        (std::max)(0, client.Height() - TrackTop() - Pixel(20));
    return (std::max)(Pixel(18), available / kTrackCount);
}

int CTimelineView::TrackLeft() const
{
    return LabelWidth() + Pixel(8);
}

int CTimelineView::TrackWidth() const
{
    CRect client;
    GetClientRect(&client);
    return (std::max)(Pixel(20), client.Width() - TrackLeft() - Pixel(8));
}

// Fila visual (0 arriba) para una capa logica (0 abajo .. 3 audio).
int CTimelineView::TrackRow(int track) const
{
    if (track == kAudioTrack) {
        return 3;
    }
    if (track < 0) {
        track = 0;
    }
    if (track > kVideoTrackCount - 1) {
        track = kVideoTrackCount - 1;
    }
    return kVideoTrackCount - 1 - track;
}

int CTimelineView::TrackAtY(int y) const
{
    const int top = TrackTop();
    if (y < top) {
        return -1;
    }
    int row = (y - top) / RowHeight();
    row = (std::max)(0, (std::min)(row, kTrackCount - 1));
    if (row == 3) {
        return kAudioTrack;
    }
    return kVideoTrackCount - 1 - row;
}

CRect CTimelineView::RowRect(int row) const
{
    CRect client;
    GetClientRect(&client);
    const int top = TrackTop() + row * RowHeight();
    return CRect(LabelWidth(), top, client.right, top + RowHeight());
}

double CTimelineView::TotalDuration() const
{
    std::vector<ClipInfo> emptyClips;
    const std::vector<ClipInfo>& clips = (m_clips != nullptr) ? *m_clips : emptyClips;
    if (m_texts == nullptr) {
        return TimelineDuration(clips);
    }
    return TimelineDuration(clips, *m_texts);
}

int CTimelineView::TimeToX(double seconds) const
{
    const double view = ViewDuration();
    if (view <= 0.0) {
        return TrackLeft();
    }
    double ratio = (seconds - m_viewStart) / view;
    ratio = (std::max)(0.0, (std::min)(1.0, ratio));
    return TrackLeft() + static_cast<int>(ratio * TrackWidth() + 0.5);
}

double CTimelineView::XToTime(int x) const
{
    const double view = ViewDuration();
    const int width = TrackWidth();
    if (view <= 0.0 || width <= 0) {
        return 0.0;
    }

    double ratio = static_cast<double>(x - TrackLeft()) / width;
    ratio = (std::max)(0.0, (std::min)(1.0, ratio));
    return (std::max)(0.0, m_viewStart + ratio * view);
}

CRect CTimelineView::ClipRect(int index) const
{
    if (m_clips == nullptr || index < 0 || index >= static_cast<int>(m_clips->size())) {
        return CRect(0, 0, 0, 0);
    }

    const ClipInfo& clip = (*m_clips)[static_cast<size_t>(index)];
    const int row = TrackRow(clip.EffectiveTrack());
    const CRect rowRect = RowRect(row);
    const int left = TimeToX(clip.start);
    const int right = TimeToX(clip.start + clip.OutputDuration());
    return CRect(left, rowRect.top + Pixel(2), (std::max)(left + 2, right),
                 rowRect.bottom - Pixel(2));
}

CRect CTimelineView::TextRect(int index) const
{
    if (m_texts == nullptr || index < 0 || index >= static_cast<int>(m_texts->size())) {
        return CRect(0, 0, 0, 0);
    }

    const TextInfo& text = (*m_texts)[static_cast<size_t>(index)];
    const CRect rowRect = RowRect(TrackRow(text.EffectiveTrack()));
    const int left = TimeToX(text.start);
    const int right = TimeToX(text.End());
    return CRect(left, rowRect.top + Pixel(3),
                 (std::max)(left + Pixel(10), right), rowRect.bottom - Pixel(3));
}

int CTimelineView::HitClip(int x, int y) const
{
    if (m_clips == nullptr) {
        return -1;
    }

    for (int i = static_cast<int>(m_clips->size()) - 1; i >= 0; --i) {
        if (ClipRect(i).PtInRect(CPoint(x, y))) {
            return i;
        }
    }
    return -1;
}

int CTimelineView::HitText(int x, int y) const
{
    if (m_texts == nullptr) {
        return -1;
    }

    for (int i = static_cast<int>(m_texts->size()) - 1; i >= 0; --i) {
        if (TextRect(i).PtInRect(CPoint(x, y))) {
            return i;
        }
    }
    return -1;
}

void CTimelineView::SelectAtTime(double time)
{
    const bool hasClips = (m_clips != nullptr && !m_clips->empty());
    const bool hasTexts = (m_texts != nullptr && !m_texts->empty());
    if (!hasClips && !hasTexts) {
        return;
    }

    int best = -1;
    int bestTrack = -1;
    int bestIndex = -1;

    if (hasClips) {
        for (int i = 0; i < static_cast<int>(m_clips->size()); ++i) {
            const ClipInfo& clip = (*m_clips)[static_cast<size_t>(i)];
            if (time + 1e-6 < clip.start || time >= clip.End() - 1e-6) {
                continue;
            }
            const int track = clip.EffectiveTrack();
            // Prioriza la capa de video mas alta; el audio solo si no hay video.
            const bool video = track < kVideoTrackCount;
            const int score = video ? track + 10 : 0;
            if (score > bestTrack || (score == bestTrack && i > bestIndex)) {
                bestTrack = score;
                bestIndex = i;
                best = i;
            }
        }
    }

    if (best >= 0 && best != m_selection) {
        m_selection = best;
        Refresh();
        if (OnSelectionChanged) {
            OnSelectionChanged();
        }
        return;
    }

    // Si no hay video en ese instante, se selecciona un texto activo para
    // poder moverlo entre capas desde la linea de tiempo.
    if (hasTexts) {
        int textBest = -1;
        int textTrack = -1;
        for (int i = 0; i < static_cast<int>(m_texts->size()); ++i) {
            const TextInfo& text = (*m_texts)[static_cast<size_t>(i)];
            if (!text.IsActiveAt(time)) {
                continue;
            }
            const int track = text.EffectiveTrack();
            if (track >= textTrack) {
                textTrack = track;
                textBest = i;
            }
        }
        if (textBest >= 0 && textBest != m_textSelection) {
            m_textSelection = textBest;
            Refresh();
            if (OnTextSelectionChanged) {
                OnTextSelectionChanged(textBest);
            }
        }
    }
}

void CTimelineView::EnsureFont(CDC& dc)
{
    if (m_font.GetSafeHandle() == nullptr) {
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(8, dc.GetDeviceCaps(LOGPIXELSY), 72);
        lf.lfWeight = FW_NORMAL;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, _countof(lf.lfFaceName), L"Segoe UI");
        m_font.CreateFontIndirect(&lf);
    }
    if (m_fontSmall.GetSafeHandle() == nullptr) {
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(7, dc.GetDeviceCaps(LOGPIXELSY), 72);
        lf.lfWeight = FW_NORMAL;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, _countof(lf.lfFaceName), L"Segoe UI");
        m_fontSmall.CreateFontIndirect(&lf);
    }
}

void CTimelineView::DrawRuler(CDC& dc, const CRect& client)
{
    const CRect ruler(client.left, client.top, client.right, client.top + RulerHeight());
    dc.FillSolidRect(&ruler, kRulerBackground);
    dc.FillSolidRect(client.left, ruler.top, LabelWidth(), RulerHeight(), kGutterBackground);

    const double view = ViewDuration();
    if (view <= 0.0) {
        dc.FillSolidRect(client.left, ruler.bottom, client.Width(), 1, kRulerTick);
        return;
    }

    const double viewEnd = m_viewStart + view;
    const double pixelsPerSecond = static_cast<double>(TrackWidth()) / view;
    const double minimumSpacing = Pixel(64);

    const double candidates[] = { 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0,
                                  120.0, 300.0, 600.0, 1800.0, 3600.0 };
    double step = candidates[0];
    for (const double candidate : candidates) {
        step = candidate;
        if (candidate * pixelsPerSecond >= minimumSpacing) {
            break;
        }
    }

    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(kRulerText);

    const int saved = dc.SaveDC();
    dc.IntersectClipRect(TrackLeft(), ruler.top, client.right, ruler.bottom);

    double start = std::floor(m_viewStart / step) * step;
    for (double t = start; t <= viewEnd + 1e-6; t += step) {
        if (t < 0.0) {
            continue;
        }
        const int x = TimeToX(t);
        if (x < TrackLeft()) {
            continue;
        }
        dc.FillSolidRect(x, ruler.bottom - Pixel(6), 1, Pixel(6), kRulerTick);

        const std::wstring label = FormatRulerTime(t);
        dc.TextOutW(x + Pixel(3), ruler.top + Pixel(3), label.c_str());
    }

    dc.RestoreDC(saved);
    dc.FillSolidRect(client.left, ruler.bottom, client.Width(), 1, kRulerTick);
}

void CTimelineView::DrawTracks(CDC& dc, const CRect& client)
{
    for (int row = 0; row < kTrackCount; ++row) {
        const CRect rect = RowRect(row);
        COLORREF color = (row % 2 == 0) ? kRowEven : kRowOdd;
        if (row == 3) {
            color = kRowAudio;
        }
        dc.FillSolidRect(rect.left, rect.top, client.right - rect.left, rect.Height(), color);
        dc.FillSolidRect(client.left, rect.top, LabelWidth(), rect.Height(), kGutterBackground);
        dc.FillSolidRect(client.left, rect.bottom - 1, client.Width(), 1, RGB(16, 17, 21));

        int track = (row == 3) ? kAudioTrack : (kVideoTrackCount - 1 - row);
        const wchar_t* label = L"Audio";
        if (track == 2) {
            label = L"Capa 3";
        }
        else if (track == 1) {
            label = L"Capa 2";
        }
        else if (track == 0) {
            label = L"Capa 1";
        }

        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(kGutterText);
        CRect labelRect(client.left + Pixel(4), rect.top, LabelWidth() - Pixel(4), rect.bottom);
        dc.DrawText(label, labelRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

void CTimelineView::DrawClips(CDC& dc)
{
    if (m_clips == nullptr || m_clips->empty()) {
        CRect client;
        GetClientRect(&client);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(kEmptyText);
        dc.DrawText(L"Agrega videos o audio para empezar",
                    CRect(TrackLeft(), TrackTop(), client.right, client.bottom),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    for (int i = 0; i < static_cast<int>(m_clips->size()); ++i) {
        const ClipInfo& clip = (*m_clips)[static_cast<size_t>(i)];
        const CRect rect = ClipRect(i);
        if (rect.right - rect.left < 2) {
            continue;
        }

        const bool selected = (i == m_selection);
        const bool audio = clip.EffectiveTrack() == kAudioTrack;
        const COLORREF fill = TrackColor(clip.EffectiveTrack(), selected);

        CBrush brush(fill);
        CPen pen(PS_SOLID, 1, kClipBorder);
        CBrush* oldBrush = dc.SelectObject(&brush);
        CPen* oldPen = dc.SelectObject(&pen);
        dc.RoundRect(rect, CPoint(Pixel(5), Pixel(5)));
        dc.SelectObject(oldPen);
        dc.SelectObject(oldBrush);

        const int saved = dc.SaveDC();
        dc.IntersectClipRect(rect.left + Pixel(5), rect.top, rect.right - Pixel(4), rect.bottom);

        if (rect.Width() > Pixel(24)) {
            std::wstring text = clip.fileName;
            if (clip.EffectiveSpeed() != 1.0) {
                wchar_t speedText[32]{};
                swprintf_s(speedText, L"  %.4gx", clip.EffectiveSpeed());
                text += speedText;
            }
            else if (clip.EffectiveScaleX() != 1.0 || clip.EffectiveScaleY() != 1.0) {
                text += L"  " + ScaleText(clip);
            }

            dc.SetBkMode(TRANSPARENT);
            dc.SetTextColor(RGB(245, 248, 255));
            dc.TextOutW(rect.left + Pixel(7), rect.top + Pixel(3), text.c_str());
        }
        dc.RestoreDC(saved);

        if ((clip.negate || clip.flipH || clip.flipV) && rect.Width() > Pixel(40)) {
            const int dot = (std::max)(Pixel(4), 4);
            dc.FillSolidRect(rect.left + Pixel(7), rect.bottom - Pixel(13), dot, dot,
                             RGB(255, 214, 92));
        }

        if (audio && rect.Width() > Pixel(40)) {
            dc.SetBkMode(TRANSPARENT);
            dc.SetTextColor(RGB(214, 245, 226));
            dc.TextOutW(rect.left + Pixel(7), rect.bottom - Pixel(15), L"A");
        }

        if (selected) {
            const int handleWidth = Pixel(7);
            const CRect leftHandle(rect.left, rect.top, rect.left + handleWidth, rect.bottom);
            const CRect rightHandle(rect.right - handleWidth, rect.top, rect.right, rect.bottom);
            dc.FillSolidRect(&leftHandle, RGB(240, 244, 250));
            dc.FillSolidRect(&rightHandle, RGB(240, 244, 250));
        }
    }
}

void CTimelineView::DrawTexts(CDC& dc)
{
    if (m_texts == nullptr || m_texts->empty()) {
        return;
    }

    for (int i = 0; i < static_cast<int>(m_texts->size()); ++i) {
        const TextInfo& text = (*m_texts)[static_cast<size_t>(i)];
        const CRect rect = TextRect(i);
        if (rect.Width() < 2) {
            continue;
        }

        const bool selected = (i == m_textSelection);
        CBrush brush(selected ? kTextFillSelected : kTextFill);
        CPen pen(PS_SOLID, 1, kTextBorder);
        CBrush* oldBrush = dc.SelectObject(&brush);
        CPen* oldPen = dc.SelectObject(&pen);
        dc.RoundRect(rect, CPoint(Pixel(4), Pixel(4)));
        dc.SelectObject(oldPen);
        dc.SelectObject(oldBrush);

        const int saved = dc.SaveDC();
        dc.IntersectClipRect(rect.left + Pixel(3), rect.top,
                             rect.right - Pixel(3), rect.bottom);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(250, 246, 255));
        std::wstring label = text.text;
        const size_t newline = label.find(L'\n');
        if (newline != std::wstring::npos) {
            label = label.substr(0, newline);
        }
        if (label.empty()) {
            label = L"Texto";
        }
        dc.TextOutW(rect.left + Pixel(5), rect.top + Pixel(2), label.c_str());
        dc.RestoreDC(saved);

        if (selected) {
            const int handle = Pixel(6);
            dc.FillSolidRect(rect.left, rect.top, handle, rect.Height(), RGB(245, 240, 255));
            dc.FillSolidRect(rect.right - handle, rect.top, handle, rect.Height(),
                             RGB(245, 240, 255));
        }
    }
}

void CTimelineView::DrawTransitions(CDC& dc)
{
    if (m_clips == nullptr || m_transitions == nullptr || m_transitions->empty()) {
        return;
    }

    for (const TransitionInfo& transition : *m_transitions) {
        if (!transition.IsValid() || transition.fromClip < 0 || transition.toClip < 0 ||
            transition.fromClip >= static_cast<int>(m_clips->size()) ||
            transition.toClip >= static_cast<int>(m_clips->size())) {
            continue;
        }

        const ClipInfo& from = (*m_clips)[static_cast<size_t>(transition.fromClip)];
        const ClipInfo& to = (*m_clips)[static_cast<size_t>(transition.toClip)];
        const double span = transition.EffectiveDuration();

        // En el fundido a negro la ventana termina con el clip anterior; en la
        // disolvencia y el deslizamiento arranca con el clip nuevo.
        double start = to.start;
        if (transition.type == TransitionType::FadeBlack) {
            start = from.End() - span;
        }
        start = (std::max)(0.0, start);
        const double end = start + span;
        if (end <= start) {
            continue;
        }

        const CRect row = RowRect(TrackRow(from.EffectiveTrack()));
        const int left = TimeToX(start);
        const int right = TimeToX(end);
        if (right - left < 2) {
            continue;
        }

        CRect rect(left, row.top + Pixel(3), right, row.bottom - Pixel(3));
        rect.right = (std::max)(rect.right, rect.left + 2);

        CBrush brush(RGB(118, 88, 190));
        CPen pen(PS_SOLID, 1, RGB(214, 196, 255));
        CBrush* oldBrush = dc.SelectObject(&brush);
        CPen* oldPen = dc.SelectObject(&pen);
        dc.Rectangle(rect);
        dc.SelectObject(oldPen);
        dc.SelectObject(oldBrush);

        // Un par de diagonales ayudan a leer la ventana de la transicion.
        const int inset = (std::min)(Pixel(4), (rect.Width() - 1) / 2);
        CPen markPen(PS_SOLID, 1, RGB(226, 212, 255));
        oldPen = dc.SelectObject(&markPen);
        dc.MoveTo(rect.left + inset, rect.bottom - inset);
        dc.LineTo(rect.right - inset, rect.top + inset);
        dc.MoveTo(rect.left + inset, rect.top + inset);
        dc.LineTo(rect.right - inset, rect.bottom - inset);
        dc.SelectObject(oldPen);

        if (rect.Width() > Pixel(54)) {
            const int saved = dc.SaveDC();
            dc.IntersectClipRect(rect.left + Pixel(3), rect.top, rect.right - Pixel(2),
                                 rect.bottom);
            dc.SetBkMode(TRANSPARENT);
            dc.SetTextColor(RGB(248, 245, 255));
            dc.TextOutW(rect.left + Pixel(5), rect.top + Pixel(1),
                        TransitionName(transition.type));
            dc.RestoreDC(saved);
        }
    }
}

void CTimelineView::DrawPlayhead(CDC& dc, const CRect& client)
{
    const double view = ViewDuration();
    if (view <= 0.0) {
        return;
    }

    const int x = TimeToX(m_playhead);

    CPen pen(PS_SOLID, 2, kPlayhead);
    CPen* oldPen = dc.SelectObject(&pen);
    dc.MoveTo(x, RulerHeight() - Pixel(2));
    dc.LineTo(x, client.bottom - Pixel(2));
    dc.SelectObject(oldPen);

    CBrush brush(kPlayhead);
    CBrush* oldBrush = dc.SelectObject(&brush);
    CPoint points[3];
    points[0] = CPoint(x - Pixel(5), RulerHeight() - Pixel(9));
    points[1] = CPoint(x + Pixel(5), RulerHeight() - Pixel(9));
    points[2] = CPoint(x, RulerHeight());
    dc.Polygon(points, 3);
    dc.SelectObject(oldBrush);
}

void CTimelineView::OnPaint()
{
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);

    dc.FillSolidRect(&client, kBackground);
    EnsureFont(dc);

    // Franja inferior reservada para la barra de desplazamiento horizontal.
    const int rowsBottom = TrackTop() + RowHeight() * kTrackCount;
    if (rowsBottom < client.bottom) {
        dc.FillSolidRect(client.left, rowsBottom, client.Width(), client.bottom - rowsBottom,
                         kScrollBackground);
    }

    CFont* oldFont = dc.SelectObject(&m_font);

    DrawRuler(dc, client);
    DrawTracks(dc, client);
    CFont* smallFont = dc.SelectObject(&m_fontSmall);
    DrawClips(dc);
    DrawTexts(dc);
    DrawTransitions(dc);
    dc.SelectObject(smallFont);
    DrawSnapGuide(dc, client);
    DrawPlayhead(dc, client);

    dc.SelectObject(oldFont);
}

BOOL CTimelineView::OnEraseBkgnd(CDC* pDC)
{
    CRect client;
    GetClientRect(&client);
    pDC->FillSolidRect(&client, kBackground);
    return TRUE;
}

void CTimelineView::BeginTrim(int index, int x, bool leftSide)
{
    if (m_clips == nullptr || index < 0 || index >= static_cast<int>(m_clips->size())) {
        return;
    }

    const ClipInfo& clip = (*m_clips)[static_cast<size_t>(index)];
    m_drag = leftSide ? DragMode::TrimIn : DragMode::TrimOut;
    m_dragIndex = index;
    m_dragStartX = x;
    m_dragStartStart = clip.start;
    m_dragStartIn = clip.inPoint;
    m_dragStartOut = clip.TrimEnd();
    m_dragMoved = false;
    m_dragSpan = ViewDuration();
    m_snapVisible = false;
    SetCapture();
}

void CTimelineView::BeginMove(int index, CPoint point)
{
    if (m_clips == nullptr || index < 0 || index >= static_cast<int>(m_clips->size())) {
        return;
    }

    const ClipInfo& clip = (*m_clips)[static_cast<size_t>(index)];
    m_drag = DragMode::Move;
    m_dragIndex = index;
    m_dragStartX = point.x;
    m_dragStartY = point.y;
    m_dragStartStart = clip.start;
    m_dragStartTrack = clip.EffectiveTrack();
    m_dragMoved = false;
    m_dragSpan = ViewDuration();
    m_snapVisible = false;
    SetCapture();
}

void CTimelineView::BeginTextTrim(int index, int x, bool leftSide)
{
    if (m_texts == nullptr || index < 0 || index >= static_cast<int>(m_texts->size())) {
        return;
    }

    const TextInfo& text = (*m_texts)[static_cast<size_t>(index)];
    m_drag = leftSide ? DragMode::TextTrimIn : DragMode::TextTrimOut;
    m_dragIndex = index;
    m_dragStartX = x;
    m_dragStartStart = text.start;
    m_dragStartDuration = text.EffectiveDuration();
    m_dragMoved = false;
    m_dragSpan = ViewDuration();
    m_snapVisible = false;
    SetCapture();
}

void CTimelineView::BeginTextMove(int index, CPoint point)
{
    if (m_texts == nullptr || index < 0 || index >= static_cast<int>(m_texts->size())) {
        return;
    }

    const TextInfo& text = (*m_texts)[static_cast<size_t>(index)];
    m_drag = DragMode::TextMove;
    m_dragIndex = index;
    m_dragStartX = point.x;
    m_dragStartY = point.y;
    m_dragStartStart = text.start;
    m_dragStartDuration = text.EffectiveDuration();
    m_dragStartTrack = text.EffectiveTrack();
    m_dragMoved = false;
    m_dragSpan = ViewDuration();
    m_snapVisible = false;
    SetCapture();
}

double CTimelineView::SnapToFrame(double seconds, double fps) const
{
    if (fps <= 0.0) {
        return seconds;
    }
    return std::floor(seconds * fps + 0.5) / fps;
}

double CTimelineView::SnapToNeighborEdge(double value, int ignoreIndex, double threshold,
                                          bool& snapped, double& guide,
                                          int ignoreTextIndex) const
{
    snapped = false;
    guide = 0.0;
    if (threshold <= 0.0) {
        return value;
    }

    double best = value;
    double bestGuide = 0.0;
    double bestDistance = threshold;

    const auto consider = [&](double candidate) {
        const double distance = std::fabs(value - candidate);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = candidate;
            bestGuide = candidate;
            snapped = true;
        }
    };

    // El inicio del proyecto tambien es un borde util de alineacion.
    consider(0.0);
    if (m_clips != nullptr) {
        for (int i = 0; i < static_cast<int>(m_clips->size()); ++i) {
            if (i == ignoreIndex) {
                continue;
            }
            const ClipInfo& other = (*m_clips)[static_cast<size_t>(i)];
            consider(other.start);
            consider(other.End());
        }
    }
    if (m_texts != nullptr) {
        for (int i = 0; i < static_cast<int>(m_texts->size()); ++i) {
            if (i == ignoreTextIndex) {
                continue;
            }
            const TextInfo& other = (*m_texts)[static_cast<size_t>(i)];
            consider(other.start);
            consider(other.End());
        }
    }

    if (snapped) {
        guide = bestGuide;
    }
    return snapped ? best : value;
}

void CTimelineView::DrawSnapGuide(CDC& dc, const CRect& client)
{
    if (!m_snapVisible || ViewDuration() <= 0.0) {
        return;
    }

    const int x = TimeToX(m_snapTime);
    if (x < TrackLeft() || x > client.right) {
        return;
    }

    CPen pen(PS_DASH, 1, Theme::kAccent);
    CPen* oldPen = dc.SelectObject(&pen);
    dc.MoveTo(x, RulerHeight() - Pixel(2));
    dc.LineTo(x, client.bottom - Pixel(2));
    dc.SelectObject(oldPen);
}

void CTimelineView::ApplyTrim(int x)
{
    if (m_clips == nullptr || m_dragIndex < 0 ||
        m_dragIndex >= static_cast<int>(m_clips->size())) {
        return;
    }

    ClipInfo& clip = (*m_clips)[static_cast<size_t>(m_dragIndex)];
    const int width = (std::max)(1, TrackWidth());
    const double secondsPerPixel = m_dragSpan / width;
    const double delta = (x - m_dragStartX) * secondsPerPixel;
    const double threshold = Pixel(9) * secondsPerPixel;
    const double fps = (m_sourceFps > 0.0) ? m_sourceFps : 30.0;
    const double speed = clip.EffectiveSpeed();
    const double minimum = 0.15;

    m_snapVisible = false;

    if (m_drag == DragMode::TrimIn) {
        double newStart = (std::max)(0.0, m_dragStartStart + delta);
        bool snapped = false;
        double guide = 0.0;
        const double aligned = SnapToNeighborEdge(newStart, m_dragIndex, threshold,
                                                  snapped, guide);
        if (snapped) {
            newStart = aligned;
            m_snapTime = guide;
            m_snapVisible = true;
        }
        else {
            newStart = SnapToFrame(newStart, 30.0);
        }

        const double maxStart =
            m_dragStartStart + (m_dragStartOut - m_dragStartIn - minimum) / speed;
        newStart = (std::max)(0.0, (std::min)(newStart, (std::max)(0.0, maxStart)));

        double newIn = m_dragStartIn + (newStart - m_dragStartStart) * speed;
        if (!snapped) {
            newIn = SnapToFrame(newIn, fps);
        }
        newIn = (std::max)(0.0, (std::min)(newIn, m_dragStartOut - minimum));
        clip.inPoint = newIn;
        clip.start = (std::max)(0.0, m_dragStartStart + (newIn - m_dragStartIn) / speed);
    }
    else if (m_drag == DragMode::TrimOut) {
        double newOut = m_dragStartOut + delta * speed;
        const double endTime = clip.start + (newOut - clip.inPoint) / speed;
        bool snapped = false;
        double guide = 0.0;
        const double aligned = SnapToNeighborEdge(endTime, m_dragIndex, threshold,
                                                  snapped, guide);
        if (snapped) {
            newOut = clip.inPoint + (aligned - clip.start) * speed;
            m_snapTime = guide;
            m_snapVisible = true;
        }
        else {
            newOut = SnapToFrame(newOut, fps);
        }
        newOut = (std::max)(m_dragStartIn + minimum, (std::min)(newOut, clip.duration));
        clip.outPoint = newOut;
    }

    Refresh();
    if (OnTrim) {
        OnTrim(false);
    }
}

void CTimelineView::ApplyMove(CPoint point)
{
    if (m_clips == nullptr || m_dragIndex < 0 ||
        m_dragIndex >= static_cast<int>(m_clips->size())) {
        return;
    }

    ClipInfo& clip = (*m_clips)[static_cast<size_t>(m_dragIndex)];
    const int width = (std::max)(1, TrackWidth());
    const double secondsPerPixel = m_dragSpan / width;
    const double delta = (point.x - m_dragStartX) * secondsPerPixel;
    const double threshold = Pixel(9) * secondsPerPixel;
    const double span = clip.OutputDuration();

    double newStart = (std::max)(0.0, m_dragStartStart + delta);
    m_snapVisible = false;

    // Se alinea el borde de entrada o el de salida del clip arrastrado con el
    // borde mas cercano de cualquier otro clip (video o audio) del timeline.
    bool snappedStart = false;
    bool snappedEnd = false;
    double guideStart = 0.0;
    double guideEnd = 0.0;
    const double alignedStart = SnapToNeighborEdge(newStart, m_dragIndex, threshold,
                                                   snappedStart, guideStart);
    const double alignedEnd = SnapToNeighborEdge(newStart + span, m_dragIndex, threshold,
                                                 snappedEnd, guideEnd);
    const double startShift = std::fabs(alignedStart - newStart);
    const double endShift = std::fabs((alignedEnd - span) - newStart);
    if (snappedStart && (!snappedEnd || startShift <= endShift)) {
        newStart = alignedStart;
        m_snapTime = guideStart;
        m_snapVisible = true;
    }
    else if (snappedEnd) {
        newStart = alignedEnd - span;
        m_snapTime = guideEnd;
        m_snapVisible = true;
    }
    else {
        newStart = SnapToFrame(newStart, 30.0);
    }

    clip.start = (std::max)(0.0, newStart);

    if (!clip.IsAudioOnly()) {
        const int track = TrackAtY(point.y);
        if (track >= 0 && track < kVideoTrackCount) {
            clip.track = track;
        }
    }

    Refresh();
    if (OnTrim) {
        OnTrim(false);
    }
}

void CTimelineView::ApplyTextTrim(int x)
{
    if (m_texts == nullptr || m_dragIndex < 0 ||
        m_dragIndex >= static_cast<int>(m_texts->size())) {
        return;
    }

    TextInfo& text = (*m_texts)[static_cast<size_t>(m_dragIndex)];
    const int width = (std::max)(1, TrackWidth());
    const double secondsPerPixel = m_dragSpan / width;
    const double delta = (x - m_dragStartX) * secondsPerPixel;
    const double threshold = Pixel(9) * secondsPerPixel;
    const double originalEnd = m_dragStartStart + m_dragStartDuration;

    m_snapVisible = false;

    if (m_drag == DragMode::TextTrimIn) {
        double newStart = (std::max)(0.0, m_dragStartStart + delta);
        bool snappedStart = false;
        bool snappedEnd = false;
        double guideStart = 0.0;
        double guideEnd = 0.0;
        const double alignedStart = SnapToNeighborEdge(newStart, m_dragIndex, threshold,
                                                       snappedStart, guideStart, m_dragIndex);
        const double alignedEnd = SnapToNeighborEdge(newStart + m_dragStartDuration,
                                                     m_dragIndex, threshold,
                                                     snappedEnd, guideEnd, m_dragIndex);
        const double startShift = std::fabs(alignedStart - newStart);
        const double endShift = std::fabs((alignedEnd - m_dragStartDuration) - newStart);
        if (snappedStart && (!snappedEnd || startShift <= endShift)) {
            newStart = alignedStart;
            m_snapTime = guideStart;
            m_snapVisible = true;
        }
        else if (snappedEnd) {
            newStart = alignedEnd - m_dragStartDuration;
            m_snapTime = guideEnd;
            m_snapVisible = true;
        }
        else {
            newStart = SnapToFrame(newStart, 30.0);
        }
        newStart = (std::max)(0.0, (std::min)(newStart, originalEnd - 0.1));
        double duration = originalEnd - newStart;
        if (duration > 60.0) {
            duration = 60.0;
            newStart = originalEnd - duration;
        }
        text.start = (std::max)(0.0, newStart);
        text.duration = duration;
    }
    else if (m_drag == DragMode::TextTrimOut) {
        double newEnd = originalEnd + delta;
        bool snapped = false;
        double guide = 0.0;
        const double aligned = SnapToNeighborEdge(newEnd, m_dragIndex, threshold,
                                                  snapped, guide, m_dragIndex);
        if (snapped) {
            newEnd = aligned;
            m_snapTime = guide;
            m_snapVisible = true;
        }
        else {
            newEnd = SnapToFrame(newEnd, 30.0);
        }
        const double minimumEnd = text.start + 0.1;
        const double maximumEnd = text.start + 60.0;
        newEnd = (std::max)(minimumEnd, (std::min)(newEnd, maximumEnd));
        text.duration = newEnd - text.start;
    }

    Refresh();
    if (OnTrim) {
        OnTrim(false);
    }
}

void CTimelineView::ApplyTextMove(CPoint point)
{
    if (m_texts == nullptr || m_dragIndex < 0 ||
        m_dragIndex >= static_cast<int>(m_texts->size())) {
        return;
    }

    TextInfo& text = (*m_texts)[static_cast<size_t>(m_dragIndex)];
    const int width = (std::max)(1, TrackWidth());
    const double secondsPerPixel = m_dragSpan / width;
    const double delta = (point.x - m_dragStartX) * secondsPerPixel;
    const double threshold = Pixel(9) * secondsPerPixel;
    const double span = m_dragStartDuration;

    double newStart = (std::max)(0.0, m_dragStartStart + delta);
    m_snapVisible = false;

    bool snappedStart = false;
    bool snappedEnd = false;
    double guideStart = 0.0;
    double guideEnd = 0.0;
    const double alignedStart = SnapToNeighborEdge(newStart, m_dragIndex, threshold,
                                                   snappedStart, guideStart, m_dragIndex);
    const double alignedEnd = SnapToNeighborEdge(newStart + span, m_dragIndex, threshold,
                                                 snappedEnd, guideEnd, m_dragIndex);
    const double startShift = std::fabs(alignedStart - newStart);
    const double endShift = std::fabs((alignedEnd - span) - newStart);
    if (snappedStart && (!snappedEnd || startShift <= endShift)) {
        newStart = alignedStart;
        m_snapTime = guideStart;
        m_snapVisible = true;
    }
    else if (snappedEnd) {
        newStart = alignedEnd - span;
        m_snapTime = guideEnd;
        m_snapVisible = true;
    }
    else {
        newStart = SnapToFrame(newStart, 30.0);
    }

    text.start = (std::max)(0.0, newStart);
    const int track = TrackAtY(point.y);
    if (track >= 0 && track < kVideoTrackCount) {
        text.track = track;
    }

    Refresh();
    if (OnTrim) {
        OnTrim(false);
    }
}

void CTimelineView::OnLButtonDown(UINT nFlags, CPoint point)
{
    CWnd::OnLButtonDown(nFlags, point);
    SetFocus();

    const bool hasClips = (m_clips != nullptr && !m_clips->empty());
    const bool hasTexts = (m_texts != nullptr && !m_texts->empty());
    if (!hasClips && !hasTexts) {
        return;
    }
    if (ViewDuration() <= 0.0) {
        return;
    }

    // Los textos se dibujan encima de los clips: primero se comprueba su
    // rectangulo para que se puedan seleccionar y arrastrar aunque coincidan.
    const int textHit = HitText(point.x, point.y);
    if (textHit >= 0) {
        if (textHit != m_textSelection) {
            m_textSelection = textHit;
            Refresh();
            if (OnTextSelectionChanged) {
                OnTextSelectionChanged(textHit);
            }
        }

        const CRect rect = TextRect(textHit);
        const int handle = Pixel(7);
        if (point.x - rect.left <= handle) {
            BeginTextTrim(textHit, point.x, true);
            return;
        }
        if (rect.right - point.x <= handle) {
            BeginTextTrim(textHit, point.x, false);
            return;
        }
        BeginTextMove(textHit, point);
        return;
    }

    const int hit = hasClips ? HitClip(point.x, point.y) : -1;
    if (hit >= 0) {
        if (hit != m_selection) {
            m_selection = hit;
            Refresh();
            if (OnSelectionChanged) {
                OnSelectionChanged();
            }
        }

        const CRect rect = ClipRect(hit);
        const int handle = Pixel(8);
        if (point.x - rect.left <= handle) {
            BeginTrim(hit, point.x, true);
            return;
        }
        if (rect.right - point.x <= handle) {
            BeginTrim(hit, point.x, false);
            return;
        }
        BeginMove(hit, point);
        return;
    }

    // Zona vacia o regla: mover el playhead y seleccionar lo que quede debajo.
    m_drag = DragMode::Seek;
    SetCapture();
    const double time = XToTime(point.x);
    m_playhead = time;
    SelectAtTime(time);
    Refresh();
    if (OnSeek) {
        OnSeek();
    }
}

void CTimelineView::OnMouseMove(UINT nFlags, CPoint point)
{
    CWnd::OnMouseMove(nFlags, point);

    if (m_drag == DragMode::None) {
        return;
    }

    if (m_drag == DragMode::Seek) {
        const double time = XToTime(point.x);
        m_playhead = time;
        SelectAtTime(time);
        Refresh();
        if (OnSeek) {
            OnSeek();
        }
        return;
    }

    const int dx = point.x - m_dragStartX;
    const int dy = point.y - m_dragStartY;
    if (!m_dragMoved && (std::abs(dx) >= 3 || std::abs(dy) >= Pixel(4))) {
        m_dragMoved = true;
    }

    if (!m_dragMoved) {
        return;
    }

    switch (m_drag) {
    case DragMode::Move:
        ApplyMove(point);
        break;
    case DragMode::TrimIn:
    case DragMode::TrimOut:
        ApplyTrim(point.x);
        break;
    case DragMode::TextMove:
        ApplyTextMove(point);
        break;
    case DragMode::TextTrimIn:
    case DragMode::TextTrimOut:
        ApplyTextTrim(point.x);
        break;
    default:
        break;
    }
}

void CTimelineView::OnLButtonUp(UINT nFlags, CPoint point)
{
    CWnd::OnLButtonUp(nFlags, point);

    const DragMode mode = m_drag;
    const int index = m_dragIndex;
    const bool moved = m_dragMoved;

    if (mode == DragMode::TrimIn || mode == DragMode::TrimOut || mode == DragMode::Move ||
        mode == DragMode::TextTrimIn || mode == DragMode::TextTrimOut ||
        mode == DragMode::TextMove) {
        if (moved) {
            if (OnTrim) {
                OnTrim(true);
            }
            UpdateViewDuration();
        }
        else {
            // Un clic sin arrastre solo mueve el playhead.
            const double time = XToTime(point.x);
            m_playhead = time;
            if ((mode == DragMode::Move || mode == DragMode::TextMove) && index >= 0) {
                if (OnSeek) {
                    OnSeek();
                }
            }
            Refresh();
        }
    }

    if (m_drag != DragMode::None) {
        m_drag = DragMode::None;
        m_dragIndex = -1;
        m_snapVisible = false;
        if (GetCapture() == this) {
            ReleaseCapture();
        }
    }
}

void CTimelineView::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    UpdateViewDuration();
    Refresh();
}

void CTimelineView::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    if (pScrollBar == nullptr || pScrollBar->GetSafeHwnd() != m_scroll.GetSafeHwnd()) {
        CWnd::OnHScroll(nSBCode, nPos, pScrollBar);
        return;
    }

    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_ALL;
    m_scroll.GetScrollInfo(&info);

    int position = info.nPos;
    switch (nSBCode) {
    case SB_LEFT:
        position = 0;
        break;
    case SB_RIGHT:
        position = 1000;
        break;
    case SB_LINELEFT:
        position -= 25;
        break;
    case SB_LINERIGHT:
        position += 25;
        break;
    case SB_PAGELEFT:
        position -= static_cast<int>(info.nPage);
        break;
    case SB_PAGERIGHT:
        position += static_cast<int>(info.nPage);
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        position = info.nTrackPos;
        break;
    case SB_ENDSCROLL:
        return;
    default:
        break;
    }

    position = (std::max)(0, (std::min)(position, 1000));
    m_viewStart = MaxViewStart() * position / 1000.0;
    ClampViewStart();
    UpdateScrollBar();
    Invalidate(FALSE);
}

BOOL CTimelineView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    if ((nFlags & MK_CONTROL) != 0) {
        if (OnZoomStep) {
            OnZoomStep(zDelta > 0 ? 1 : -1);
        }
        return TRUE;
    }

    const double maxStart = MaxViewStart();
    if (maxStart <= 0.0) {
        return CWnd::OnMouseWheel(nFlags, zDelta, pt);
    }

    const double step = ViewDuration() * 0.08;
    m_viewStart -= (zDelta > 0) ? step : -step;
    ClampViewStart();
    Refresh();
    return TRUE;
}

void CTimelineView::OnMouseHWheel(UINT nFlags, short zDelta, CPoint pt)
{
    const double maxStart = MaxViewStart();
    if (maxStart <= 0.0) {
        CWnd::OnMouseHWheel(nFlags, zDelta, pt);
        return;
    }

    const double step = ViewDuration() * 0.08;
    m_viewStart += (zDelta > 0) ? step : -step;
    ClampViewStart();
    Refresh();
}

BOOL CTimelineView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    CPoint point;
    ::GetCursorPos(&point);
    ScreenToClient(&point);

    const int textHit = HitText(point.x, point.y);
    if (textHit >= 0) {
        const CRect rect = TextRect(textHit);
        const int handle = Pixel(7);
        if (point.x - rect.left <= handle || rect.right - point.x <= handle) {
            ::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
        }
        else {
            ::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
        }
        return TRUE;
    }

    if (m_clips != nullptr) {
        const int hit = HitClip(point.x, point.y);
        if (hit >= 0) {
            const CRect rect = ClipRect(hit);
            const int handle = Pixel(8);
            if (point.x - rect.left <= handle || rect.right - point.x <= handle) {
                ::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
            }
            else {
                ::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
            }
            return TRUE;
        }
    }

    return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

void CTimelineView::OnCaptureChanged(CWnd* pWnd)
{
    if (m_drag != DragMode::None && GetCapture() != this) {
        // Si el arrastre se cancelo sin WM_LBUTTONUP, avisamos igual para que
        // el historial de deshacer cierre el cambio que ya se habia iniciado.
        if (m_dragMoved && (m_drag == DragMode::Move || m_drag == DragMode::TrimIn ||
                            m_drag == DragMode::TrimOut || m_drag == DragMode::TextMove ||
                            m_drag == DragMode::TextTrimIn ||
                            m_drag == DragMode::TextTrimOut) && OnTrim) {
            OnTrim(true);
        }
        m_drag = DragMode::None;
        m_dragIndex = -1;
        m_snapVisible = false;
    }
    CWnd::OnCaptureChanged(pWnd);
}
