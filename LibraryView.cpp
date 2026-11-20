#include "LibraryView.h"

#include <algorithm>
#include <cwchar>
#include <string>

#include "Theme.h"

namespace {

std::wstring ShortDuration(double seconds)
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int total = static_cast<int>(seconds + 0.5);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%d:%02d", total / 60, total % 60);
    return buffer;
}

} // namespace

BEGIN_MESSAGE_MAP(CLibraryView, CWnd)
    ON_WM_CREATE()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_MOUSEWHEEL()
    ON_MESSAGE(LM_GETITEMCOUNT, &CLibraryView::OnGetItemCount)
    ON_MESSAGE(LM_GETADDRECT, &CLibraryView::OnGetAddRect)
    ON_MESSAGE(LM_GETCLIPCOUNT, &CLibraryView::OnGetClipCount)
    ON_MESSAGE(LM_ADDITEM, &CLibraryView::OnAddItem)
    ON_MESSAGE(LM_SELECTITEM, &CLibraryView::OnSelectItem)
    ON_MESSAGE(LM_GETADDVAL, &CLibraryView::OnGetAddVal)
END_MESSAGE_MAP()

LRESULT CLibraryView::OnGetAddVal(WPARAM wParam, LPARAM)
{
    const int packed = static_cast<int>(wParam);
    const int index = packed / 4;
    const int component = packed % 4;
    if (index < 0 || index >= ItemCount()) {
        return -1;
    }
    const CRect rect = AddRect(index, GetMetrics());
    switch (component) {
    case 0: return rect.left;
    case 1: return rect.top;
    case 2: return rect.right;
    case 3: return rect.bottom;
    default: return -1;
    }
}

void CLibraryView::CreateView(CWnd* parent, UINT id)
{
    const LPCWSTR className = AfxRegisterWndClass(CS_DBLCLKS,
                                                  ::LoadCursor(nullptr, IDC_ARROW),
                                                  nullptr, nullptr);
    CreateEx(0, className, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
             CRect(0, 0, 10, 10), parent, id);
}

int CLibraryView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CWnd::OnCreate(lpCreateStruct) == -1) {
        return -1;
    }
    CClientDC dc(this);
    EnsureFonts(dc);
    return 0;
}

void CLibraryView::SetItems(const std::vector<MediaItem>* items)
{
    m_items = items;
    if (m_selection >= ItemCount()) {
        m_selection = ItemCount() - 1;
    }
    const Metrics m = GetMetrics();
    ClampScroll(m);
    Refresh();
}

void CLibraryView::SetSelection(int index)
{
    const int count = ItemCount();
    m_selection = (index >= 0 && index < count) ? index : -1;

    const Metrics m = GetMetrics();
    if (m_selection >= 0) {
        EnsureVisible(m_selection, m);
    }
    Refresh();
}

void CLibraryView::SetTimelineCount(int count)
{
    m_timelineCount = count;
    Refresh();
}

void CLibraryView::Refresh()
{
    if (GetSafeHwnd() != nullptr) {
        Invalidate(FALSE);
    }
}

void CLibraryView::EnsureFonts(CDC& dc)
{
    const int dpi = dc.GetDeviceCaps(LOGPIXELSY);
    const auto make = [dpi](CFont& font, int points, int weight) {
        if (font.GetSafeHandle() != nullptr) {
            return;
        }
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(points, dpi, 72);
        lf.lfWeight = weight;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, _countof(lf.lfFaceName), L"Segoe UI");
        font.CreateFontIndirect(&lf);
    };

    make(m_fontName, 9, FW_SEMIBOLD);
    make(m_fontInfo, 8, FW_NORMAL);
    make(m_fontAdd, 13, FW_SEMIBOLD);
}

CLibraryView::Metrics CLibraryView::GetMetrics()
{
    CClientDC dc(this);
    const int dpi = dc.GetDeviceCaps(LOGPIXELSY);
    const auto px = [dpi](int value) { return MulDiv(value, dpi, 96); };

    CRect client;
    GetClientRect(&client);

    Metrics m;
    m.clientW = client.Width();
    m.clientH = client.Height();
    m.itemH = px(82);
    m.margin = px(7);
    m.thumbW = px(104);
    m.thumbH = px(58);
    m.addSize = px(24);
    m.textLeft = m.margin + m.thumbW + px(8);
    m.textRight = (std::max)(m.textLeft + px(10), m.clientW - m.margin - m.addSize - px(8));
    m.radius = px(8);
    m.gap = px(3);
    m.line1H = px(18);
    m.line2Top = px(23);
    return m;
}

int CLibraryView::ItemCount() const
{
    return m_items == nullptr ? 0 : static_cast<int>(m_items->size());
}

CRect CLibraryView::ItemRect(int index, const Metrics& m) const
{
    const int top = index * m.itemH - m_scroll;
    return CRect(m.margin, top, m.clientW - m.margin, top + m.itemH - m.gap);
}

CRect CLibraryView::AddRect(int index, const Metrics& m) const
{
    const CRect item = ItemRect(index, m);
    const int size = m.addSize;
    const int right = item.right - m.margin;
    const int top = item.top + m.margin;
    return CRect(right - size, top, right, top + size);
}

CRect CLibraryView::ThumbRect(int index, const Metrics& m) const
{
    const CRect item = ItemRect(index, m);
    const int top = item.top + (item.Height() - m.thumbH) / 2;
    return CRect(item.left + m.margin, top, item.left + m.margin + m.thumbW, top + m.thumbH);
}

void CLibraryView::ClampScroll(const Metrics& m)
{
    const int content = ItemCount() * m.itemH;
    const int maximum = (std::max)(0, content - m.clientH);
    m_scroll = (std::max)(0, (std::min)(m_scroll, maximum));
}

void CLibraryView::EnsureVisible(int index, const Metrics& m)
{
    if (index < 0 || index >= ItemCount()) {
        return;
    }
    const int top = index * m.itemH;
    const int bottom = top + m.itemH;
    if (top < m_scroll) {
        m_scroll = top;
    }
    else if (bottom > m_scroll + m.clientH) {
        m_scroll = bottom - m.clientH;
    }
    ClampScroll(m);
}

int CLibraryView::ItemAtPoint(CPoint point, const Metrics& m) const
{
    if (point.x < 0 || point.x >= m.clientW || point.y < 0 || point.y >= m.clientH) {
        return -1;
    }
    const int index = (point.y + m_scroll) / m.itemH;
    return (index >= 0 && index < ItemCount()) ? index : -1;
}

bool CLibraryView::IsInAddRect(CPoint point, int index, const Metrics& m) const
{
    return index >= 0 && AddRect(index, m).PtInRect(point) != FALSE;
}

void CLibraryView::NotifyParent(UINT message, int index)
{
    CWnd* parent = GetParent();
    if (parent != nullptr && parent->GetSafeHwnd() != nullptr) {
        parent->SendMessage(message, static_cast<WPARAM>(index), 0);
    }
}

void CLibraryView::DrawThumbnail(CDC& dc, const MediaItem& item, const CRect& rect)
{
    if (!item.HasThumbnail()) {
        dc.FillSolidRect(&rect, Theme::kBackground);
        CFont* previous = dc.SelectObject(&m_fontInfo);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(Theme::kTextFaint);
        CRect box = rect;
        dc.DrawText(item.IsAudioOnly() ? L"Audio" : L"Sin miniatura", -1,
                    &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        dc.SelectObject(previous);
        return;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = item.thumbnailWidth;
    info.bmiHeader.biHeight = -item.thumbnailHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    const int previousMode = dc.SetStretchBltMode(HALFTONE);
    SetBrushOrgEx(dc.GetSafeHdc(), 0, 0, nullptr);
    StretchDIBits(dc.GetSafeHdc(), rect.left, rect.top, rect.Width(), rect.Height(),
                  0, 0, item.thumbnailWidth, item.thumbnailHeight,
                  item.thumbnail.data(), &info, DIB_RGB_COLORS, SRCCOPY);
    dc.SetStretchBltMode(previousMode);
}

void CLibraryView::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    const Metrics m = GetMetrics();
    ClampScroll(m);
    Refresh();
}

BOOL CLibraryView::OnEraseBkgnd(CDC*)
{
    return TRUE;
}

void CLibraryView::OnPaint()
{
    CPaintDC dc(this);

    CRect client;
    GetClientRect(&client);
    if (client.Width() <= 0 || client.Height() <= 0) {
        return;
    }

    CDC buffer;
    CBitmap bitmap;
    if (!buffer.CreateCompatibleDC(&dc) ||
        !bitmap.CreateCompatibleBitmap(&dc, client.Width(), client.Height())) {
        return;
    }
    CBitmap* previousBitmap = buffer.SelectObject(&bitmap);
    buffer.FillSolidRect(&client, Theme::kPanel);
    EnsureFonts(buffer);

    const Metrics m = GetMetrics();
    ClampScroll(m);

    if (ItemCount() == 0) {
        buffer.SetBkMode(TRANSPARENT);
        buffer.SetTextColor(Theme::kTextFaint);
        CFont* previous = buffer.SelectObject(&m_fontInfo);
        buffer.DrawText(L"Biblioteca vacia. Pulsa \"+ Agregar videos\".",
                        client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        buffer.SelectObject(previous);
    }

    for (int i = 0; i < ItemCount(); ++i) {
        const CRect item = ItemRect(i, m);
        if (item.bottom < 0 || item.top > client.Height()) {
            continue;
        }

        const bool selected = (i == m_selection);
        const bool hovered = (i == m_hover);
        const MediaItem& media = (*m_items)[static_cast<size_t>(i)];

        CBrush fill(selected ? Theme::kPanelHover : (hovered ? Theme::kPanelAlt : Theme::kPanel));
        CPen border(PS_SOLID, 1, selected ? Theme::kAccent : Theme::kBorderSoft);
        CBrush* oldBrush = buffer.SelectObject(&fill);
        CPen* oldPen = buffer.SelectObject(&border);
        buffer.RoundRect(item, CPoint(m.radius, m.radius));
        buffer.SelectObject(oldPen);
        buffer.SelectObject(oldBrush);

        buffer.FillSolidRect(item.left + 1, item.bottom - 1, item.Width() - 2, 1,
                             Theme::kBorderSoft);

        DrawThumbnail(buffer, media, ThumbRect(i, m));

        CRect text(item.left + m.textLeft, item.top + m.margin,
                   m.textRight, item.top + m.margin + m.line1H);
        buffer.SetBkMode(TRANSPARENT);
        buffer.SetTextColor(selected ? Theme::kText : Theme::kText);
        CFont* previous = buffer.SelectObject(&m_fontName);
        buffer.DrawText(media.fileName.c_str(), -1, &text,
                        DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_SINGLELINE);
        buffer.SelectObject(previous);

        std::wstring info = ShortDuration(media.duration);
        if (media.IsAudioOnly()) {
            info += L"  •  Audio";
        }
        else {
            wchar_t resolution[48]{};
            swprintf_s(resolution, L"  •  %dx%d", media.width, media.height);
            info += resolution;
        }
        CRect infoRect(item.left + m.textLeft, item.top + m.margin + m.line2Top,
                       m.textRight, item.bottom - m.margin);
        previous = buffer.SelectObject(&m_fontInfo);
        buffer.SetTextColor(Theme::kTextDim);
        buffer.DrawText(info.c_str(), -1, &infoRect,
                        DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_SINGLELINE);
        buffer.SelectObject(previous);

        const CRect add = AddRect(i, m);
        const bool addHot = (m_hoverAdd == i || m_pressedAdd == i);
        CBrush addFill(addHot ? Theme::kAccent : Theme::kPanelAlt);
        CPen addPen(PS_SOLID, 1, addHot ? Theme::kAccentHover : Theme::kBorder);
        oldBrush = buffer.SelectObject(&addFill);
        oldPen = buffer.SelectObject(&addPen);
        buffer.RoundRect(add, CPoint(m.radius / 2, m.radius / 2));
        buffer.SelectObject(oldPen);
        buffer.SelectObject(oldBrush);

        previous = buffer.SelectObject(&m_fontAdd);
        buffer.SetTextColor(addHot ? RGB(255, 255, 255) : Theme::kAccent);
        CRect addBox = add;
        buffer.DrawText(L"+", -1, &addBox,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        buffer.SelectObject(previous);
    }

    dc.BitBlt(0, 0, client.Width(), client.Height(), &buffer, 0, 0, SRCCOPY);
    buffer.SelectObject(previousBitmap);
}

void CLibraryView::OnLButtonDown(UINT nFlags, CPoint point)
{
    CWnd::OnLButtonDown(nFlags, point);

    const Metrics m = GetMetrics();
    const int index = ItemAtPoint(point, m);
    if (index < 0) {
        return;
    }

    if (IsInAddRect(point, index, m)) {
        m_pressedAdd = index;
        Refresh();
        NotifyParent(LN_ADD, index);
        return;
    }

    SetSelection(index);
    NotifyParent(LN_SELCHANGED, index);
}

void CLibraryView::OnLButtonUp(UINT nFlags, CPoint point)
{
    CWnd::OnLButtonUp(nFlags, point);
    if (m_pressedAdd >= 0) {
        m_pressedAdd = -1;
        Refresh();
    }
}

void CLibraryView::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    CWnd::OnLButtonDblClk(nFlags, point);
    const Metrics m = GetMetrics();
    const int index = ItemAtPoint(point, m);
    if (index >= 0 && !IsInAddRect(point, index, m)) {
        NotifyParent(LN_DOUBLECLICK, index);
    }
}

void CLibraryView::OnMouseMove(UINT nFlags, CPoint point)
{
    CWnd::OnMouseMove(nFlags, point);

    if (!m_trackingMouse) {
        TRACKMOUSEEVENT event{};
        event.cbSize = sizeof(event);
        event.dwFlags = TME_LEAVE;
        event.hwndTrack = GetSafeHwnd();
        if (::TrackMouseEvent(&event)) {
            m_trackingMouse = true;
        }
    }

    const Metrics m = GetMetrics();
    const int hover = ItemAtPoint(point, m);
    const int hoverAdd = IsInAddRect(point, hover, m) ? hover : -1;
    if (hover != m_hover || hoverAdd != m_hoverAdd) {
        m_hover = hover;
        m_hoverAdd = hoverAdd;
        Refresh();
    }
}

void CLibraryView::OnMouseLeave()
{
    m_trackingMouse = false;
    if (m_hover != -1 || m_hoverAdd != -1) {
        m_hover = -1;
        m_hoverAdd = -1;
        Refresh();
    }
    CWnd::OnMouseLeave();
}

BOOL CLibraryView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    const Metrics m = GetMetrics();
    m_scroll -= static_cast<int>(zDelta / 120) * m.itemH / 2;
    ClampScroll(m);
    Refresh();
    return TRUE;
}

LRESULT CLibraryView::OnGetItemCount(WPARAM, LPARAM)
{
    return ItemCount();
}

LRESULT CLibraryView::OnGetAddRect(WPARAM wParam, LPARAM lParam)
{
    const int index = static_cast<int>(wParam);
    auto* rect = reinterpret_cast<RECT*>(lParam);
    if (rect == nullptr || index < 0 || index >= ItemCount()) {
        return FALSE;
    }
    const CRect value = AddRect(index, GetMetrics());
    *rect = value;
    return TRUE;
}

LRESULT CLibraryView::OnGetClipCount(WPARAM, LPARAM)
{
    return m_timelineCount;
}

LRESULT CLibraryView::OnAddItem(WPARAM wParam, LPARAM)
{
    NotifyParent(LN_ADD, static_cast<int>(wParam));
    return 0;
}

LRESULT CLibraryView::OnSelectItem(WPARAM wParam, LPARAM)
{
    const int index = static_cast<int>(wParam);
    SetSelection(index);
    NotifyParent(LN_SELCHANGED, index);
    return 0;
}
