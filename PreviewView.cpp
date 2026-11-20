#include "PreviewView.h"

#include <cmath>
#include <cstdlib>
#include <string>

#include "Theme.h"

namespace {

constexpr COLORREF kBackground = Theme::kCanvas;
constexpr COLORREF kHintBackground = RGB(24, 26, 33);
constexpr COLORREF kHintText = RGB(160, 166, 180);

} // namespace

BEGIN_MESSAGE_MAP(CPreviewView, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_SETCURSOR()
    ON_WM_CAPTURECHANGED()
    ON_MESSAGE(PV_GETSELECTIONVAL, &CPreviewView::OnGetSelectionVal)
END_MESSAGE_MAP()

LRESULT CPreviewView::OnGetSelectionVal(WPARAM wParam, LPARAM)
{
    CRect rect;
    if (!SelectionCanvasRect(rect)) {
        return -1;
    }
    switch (static_cast<int>(wParam)) {
    case 0: return rect.left;
    case 1: return rect.top;
    case 2: return rect.right;
    case 3: return rect.bottom;
    default: return -1;
    }
}

void CPreviewView::CreateView(CWnd* parent, UINT id)
{
    // Sin brush de clase: el fondo se pinta completo en OnPaint (doble buffer).
    const LPCWSTR className = AfxRegisterWndClass(CS_DBLCLKS,
                                                  ::LoadCursor(nullptr, IDC_ARROW),
                                                  nullptr, nullptr);
    CreateEx(0, className, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, CRect(0, 0, 10, 10),
             parent, id);
}

void CPreviewView::Refresh()
{
    if (GetSafeHwnd() != nullptr) {
        Invalidate(FALSE);
    }
}

void CPreviewView::SetSnapGuides(bool vertical, double x, bool horizontal, double y)
{
    const bool changed = vertical != m_guideVertical || horizontal != m_guideHorizontal ||
                         (vertical && std::abs(x - m_guideX) > 0.5) ||
                         (horizontal && std::abs(y - m_guideY) > 0.5);
    m_guideVertical = vertical;
    m_guideHorizontal = horizontal;
    m_guideX = x;
    m_guideY = y;
    if (changed) {
        Refresh();
    }
}

BOOL CPreviewView::OnEraseBkgnd(CDC*)
{
    // Se evita el borrado del fondo para que no parpadee entre frames.
    return TRUE;
}

void CPreviewView::EnsureFonts(CDC& dc)
{
    if (m_fontEmpty.GetSafeHandle() == nullptr) {
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(10, dc.GetDeviceCaps(LOGPIXELSY), 72);
        lf.lfWeight = FW_NORMAL;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, _countof(lf.lfFaceName), L"Segoe UI");
        m_fontEmpty.CreateFontIndirect(&lf);
    }
    if (m_fontHint.GetSafeHandle() == nullptr) {
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(8, dc.GetDeviceCaps(LOGPIXELSY), 72);
        lf.lfWeight = FW_NORMAL;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, _countof(lf.lfFaceName), L"Segoe UI");
        m_fontHint.CreateFontIndirect(&lf);
    }
}

bool CPreviewView::CanvasRect(CRect& rect) const
{
    rect.SetRectEmpty();
    if (m_player == nullptr || !m_player->IsOpen() || m_player->Width() <= 0 ||
        m_player->Height() <= 0) {
        return false;
    }

    CRect client;
    GetClientRect(&client);
    if (client.Width() <= 0 || client.Height() <= 0) {
        return false;
    }

    const double aspect =
        static_cast<double>(m_player->Width()) / static_cast<double>(m_player->Height());
    int targetWidth = client.Width();
    int targetHeight = static_cast<int>(targetWidth / aspect + 0.5);
    if (targetHeight > client.Height()) {
        targetHeight = client.Height();
        targetWidth = static_cast<int>(targetHeight * aspect + 0.5);
    }
    if (targetWidth <= 0 || targetHeight <= 0) {
        return false;
    }

    const int left = client.left + (client.Width() - targetWidth) / 2;
    const int top = client.top + (client.Height() - targetHeight) / 2;
    rect.SetRect(left, top, left + targetWidth, top + targetHeight);
    return true;
}

bool CPreviewView::CanvasPoint(CPoint point, double& x, double& y, bool& inside) const
{
    x = 0.0;
    y = 0.0;
    inside = false;

    CRect rect;
    if (!CanvasRect(rect)) {
        return false;
    }

    inside = rect.PtInRect(point) != FALSE;
    x = static_cast<double>(point.x - rect.left) * m_player->Width() / rect.Width();
    y = static_cast<double>(point.y - rect.top) * m_player->Height() / rect.Height();
    return true;
}

bool CPreviewView::SelectionCanvasRect(CRect& rect) const
{
    rect.SetRectEmpty();
    if (!GetSelectionRect || m_player == nullptr || !m_player->IsOpen()) {
        return false;
    }

    CRect canvas;
    if (!CanvasRect(canvas) || canvas.Width() <= 0 || canvas.Height() <= 0) {
        return false;
    }

    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    if (!GetSelectionRect(left, top, right, bottom)) {
        return false;
    }

    const double scaleX = static_cast<double>(canvas.Width()) / m_player->Width();
    const double scaleY = static_cast<double>(canvas.Height()) / m_player->Height();
    rect.SetRect(canvas.left + static_cast<int>(left * scaleX + 0.5),
                 canvas.top + static_cast<int>(top * scaleY + 0.5),
                 canvas.left + static_cast<int>(right * scaleX + 0.5),
                 canvas.top + static_cast<int>(bottom * scaleY + 0.5));
    return !rect.IsRectEmpty();
}

void CPreviewView::DrawSelection(CDC& dc, const CRect& canvas)
{
    CRect rect;
    if (!SelectionCanvasRect(rect)) {
        return;
    }

    CPen pen(PS_SOLID, 2, Theme::kDanger);
    CPen* oldPen = dc.SelectObject(&pen);
    CBrush* oldBrush = static_cast<CBrush*>(dc.SelectStockObject(NULL_BRUSH));
    dc.Rectangle(rect);

    const int midX = (rect.left + rect.right) / 2;
    const int midY = (rect.top + rect.bottom) / 2;
    const int size = (std::max)(6, MulDiv(9, dc.GetDeviceCaps(LOGPIXELSY), 96));
    const int edgeSize = (std::max)(5, size - 2);
    const CPoint handles[8] = {
        rect.TopLeft(),
        CPoint(rect.right, rect.top),
        CPoint(rect.left, rect.bottom),
        rect.BottomRight(),
        CPoint(rect.left, midY),   // izquierda
        CPoint(midX, rect.top),    // arriba
        CPoint(rect.right, midY),  // derecha
        CPoint(midX, rect.bottom)  // abajo
    };

    CBrush handleBrush(Theme::kPanel);
    CBrush* previousBrush = dc.SelectObject(&handleBrush);
    for (int i = 0; i < 8; ++i) {
        const int s = i < 4 ? size : edgeSize;
        const CPoint& handle = handles[i];
        CRect box(handle.x - s / 2, handle.y - s / 2,
                  handle.x - s / 2 + s, handle.y - s / 2 + s);
        dc.Rectangle(box);
    }
    dc.SelectObject(previousBrush);
    dc.SelectObject(oldBrush);
    dc.SelectObject(oldPen);
}

void CPreviewView::DrawSnapGuides(CDC& dc, const CRect& canvas)
{
    if (m_player == nullptr || m_player->Width() <= 0 || m_player->Height() <= 0 ||
        canvas.Width() <= 0 || canvas.Height() <= 0) {
        return;
    }
    if (!m_guideVertical && !m_guideHorizontal) {
        return;
    }

    CPen pen(PS_DASH, 1, Theme::kAccent);
    CPen* oldPen = dc.SelectObject(&pen);
    if (m_guideVertical) {
        const int x = canvas.left + static_cast<int>(
            m_guideX * canvas.Width() / m_player->Width() + 0.5);
        dc.MoveTo(x, canvas.top);
        dc.LineTo(x, canvas.bottom);
    }
    if (m_guideHorizontal) {
        const int y = canvas.top + static_cast<int>(
            m_guideY * canvas.Height() / m_player->Height() + 0.5);
        dc.MoveTo(canvas.left, y);
        dc.LineTo(canvas.right, y);
    }
    dc.SelectObject(oldPen);
}

int CPreviewView::HitTestZone(CPoint point) const
{
    CRect rect;
    if (!SelectionCanvasRect(rect)) {
        return 0;
    }

    CClientDC dc(const_cast<CPreviewView*>(this));
    const int tolerance = (std::max)(8, MulDiv(10, dc.GetDeviceCaps(LOGPIXELSY), 96));

    const CPoint corners[4] = {
        rect.TopLeft(),
        CPoint(rect.right, rect.top),
        CPoint(rect.left, rect.bottom),
        rect.BottomRight()
    };
    for (int i = 0; i < 4; ++i) {
        if (std::abs(point.x - corners[i].x) <= tolerance &&
            std::abs(point.y - corners[i].y) <= tolerance) {
            return i + 1;
        }
    }

    const int midX = (rect.left + rect.right) / 2;
    const int midY = (rect.top + rect.bottom) / 2;
    const CPoint mids[4] = {
        CPoint(rect.left, midY),
        CPoint(midX, rect.top),
        CPoint(rect.right, midY),
        CPoint(midX, rect.bottom)
    };
    for (int i = 0; i < 4; ++i) {
        if (std::abs(point.x - mids[i].x) <= tolerance &&
            std::abs(point.y - mids[i].y) <= tolerance) {
            return i + 5;
        }
    }
    return 0;
}

void CPreviewView::OnPaint()
{
    CPaintDC dc(this);

    CRect client;
    GetClientRect(&client);
    if (client.Width() <= 0 || client.Height() <= 0) {
        return;
    }

    // Dibuja todo en un buffer y lo copia de una sola vez a la pantalla.
    CDC buffer;
    if (!buffer.CreateCompatibleDC(&dc)) {
        return;
    }

    CBitmap bitmap;
    if (!bitmap.CreateCompatibleBitmap(&dc, client.Width(), client.Height())) {
        return;
    }

    CBitmap* previousBitmap = buffer.SelectObject(&bitmap);
    buffer.FillSolidRect(&client, kBackground);
    EnsureFonts(buffer);

    const bool hasVideo = m_player != nullptr && m_player->IsOpen() &&
                          m_player->Width() > 0 && !m_player->Pixels().empty();

    CRect canvas;
    const bool hasCanvas = hasVideo && CanvasRect(canvas);

    if (hasCanvas) {
        const int frameWidth = m_player->Width();
        const int frameHeight = m_player->Height();

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = frameWidth;
        info.bmiHeader.biHeight = -frameHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        const int previousMode = buffer.SetStretchBltMode(HALFTONE);
        SetBrushOrgEx(buffer.GetSafeHdc(), 0, 0, nullptr);

        StretchDIBits(buffer.GetSafeHdc(), canvas.left, canvas.top, canvas.Width(),
                      canvas.Height(), 0, 0, frameWidth, frameHeight,
                      m_player->Pixels().data(), &info, DIB_RGB_COLORS, SRCCOPY);

        buffer.SetStretchBltMode(previousMode);

        // Marco sutil del lienzo.
        buffer.FillSolidRect(canvas.left, canvas.top, canvas.Width(), 1, Theme::kBorder);
        buffer.FillSolidRect(canvas.left, canvas.bottom - 1, canvas.Width(), 1, Theme::kBorder);
        buffer.FillSolidRect(canvas.left, canvas.top, 1, canvas.Height(), Theme::kBorder);
        buffer.FillSolidRect(canvas.right - 1, canvas.top, 1, canvas.Height(), Theme::kBorder);

        // Guias de alineacion y contorno rojo del clip seleccionado con
        // tiradores en esquinas y puntos medios de los bordes.
        DrawSnapGuides(buffer, canvas);
        DrawSelection(buffer, canvas);

        // Pista de arrastre.
        const std::wstring hint = L"Arrastra para mover - esquinas y bordes para ajustar";
        CFont* previousFont = buffer.SelectObject(&m_fontHint);
        const CSize hintSize = buffer.GetTextExtent(hint.c_str());

        CRect pill(canvas.left + MulDiv(10, buffer.GetDeviceCaps(LOGPIXELSY), 96),
                   canvas.bottom - hintSize.cy - MulDiv(20, buffer.GetDeviceCaps(LOGPIXELSY), 96),
                   canvas.left + hintSize.cx + MulDiv(28, buffer.GetDeviceCaps(LOGPIXELSY), 96),
                   canvas.bottom - MulDiv(8, buffer.GetDeviceCaps(LOGPIXELSY), 96));
        if (pill.right < canvas.right) {
            CBrush pillBrush(kHintBackground);
            CPen pillPen(PS_SOLID, 1, Theme::kBorderSoft);
            CBrush* oldBrush = buffer.SelectObject(&pillBrush);
            CPen* oldPen = buffer.SelectObject(&pillPen);
            buffer.RoundRect(pill, CPoint(MulDiv(9, buffer.GetDeviceCaps(LOGPIXELSY), 96),
                                          MulDiv(9, buffer.GetDeviceCaps(LOGPIXELSY), 96)));
            buffer.SelectObject(oldPen);
            buffer.SelectObject(oldBrush);

            buffer.SetBkMode(TRANSPARENT);
            buffer.SetTextColor(kHintText);
            buffer.DrawText(hint.c_str(), pill, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        buffer.SelectObject(previousFont);
    }
    else if (!hasVideo) {
        CFont* previousFont = buffer.SelectObject(&m_fontEmpty);
        buffer.SetBkMode(TRANSPARENT);
        buffer.SetTextColor(Theme::kTextFaint);
        buffer.DrawText(L"Agrega videos para ver la vista previa",
                        CRect(client.left, client.top, client.right, client.bottom),
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        buffer.SelectObject(previousFont);
    }

    dc.BitBlt(0, 0, client.Width(), client.Height(), &buffer, 0, 0, SRCCOPY);
    buffer.SelectObject(previousBitmap);
}

void CPreviewView::OnLButtonDown(UINT nFlags, CPoint point)
{
    CWnd::OnLButtonDown(nFlags, point);

    double x = 0.0;
    double y = 0.0;
    bool inside = false;
    if (!CanvasPoint(point, x, y, inside) || !inside || !OnDragBegin) {
        return;
    }

    const int zone = HitTestZone(point);
    if (!OnDragBegin(x, y, zone)) {
        return;
    }

    m_dragging = true;
    m_dragZone = zone;
    SetCapture();
}

void CPreviewView::OnMouseMove(UINT nFlags, CPoint point)
{
    CWnd::OnMouseMove(nFlags, point);

    if (!m_dragging || !OnDragMove) {
        return;
    }

    double x = 0.0;
    double y = 0.0;
    bool inside = false;
    if (CanvasPoint(point, x, y, inside)) {
        OnDragMove(x, y);
    }
}

void CPreviewView::OnLButtonUp(UINT nFlags, CPoint point)
{
    CWnd::OnLButtonUp(nFlags, point);

    if (!m_dragging) {
        return;
    }

    m_dragging = false;
    m_dragZone = 0;
    if (GetCapture() == this) {
        ReleaseCapture();
    }
    if (OnDragEnd) {
        OnDragEnd();
    }
}

BOOL CPreviewView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    if (nHitTest == HTCLIENT) {
        CPoint point;
        ::GetCursorPos(&point);
        ScreenToClient(&point);

        double x = 0.0;
        double y = 0.0;
        bool inside = false;
        if (CanvasPoint(point, x, y, inside) && inside) {
            const int zone = HitTestZone(point);
            LPCWSTR cursor = IDC_SIZEALL;
            if (zone == 1 || zone == 4) {
                cursor = IDC_SIZENWSE;
            }
            else if (zone == 2 || zone == 3) {
                cursor = IDC_SIZENESW;
            }
            else if (zone == 5 || zone == 7) {
                cursor = IDC_SIZEWE;
            }
            else if (zone == 6 || zone == 8) {
                cursor = IDC_SIZENS;
            }
            ::SetCursor(::LoadCursor(nullptr, cursor));
            return TRUE;
        }
    }

    return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

void CPreviewView::OnCaptureChanged(CWnd* pWnd)
{
    if (m_dragging && GetCapture() != this) {
        m_dragging = false;
        m_dragZone = 0;
        if (OnDragEnd) {
            OnDragEnd();
        }
    }
    CWnd::OnCaptureChanged(pWnd);
}
