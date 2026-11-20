#pragma once

#include <afxwin.h>

#include <vector>

#include "VideoEditorTypes.h"

// Mensajes de consulta para automatizacion de UI (y para el propio control).
constexpr UINT LM_GETITEMCOUNT = WM_APP + 10;
constexpr UINT LM_GETADDRECT = WM_APP + 11;
constexpr UINT LM_GETCLIPCOUNT = WM_APP + 12;
constexpr UINT LM_ADDITEM = WM_APP + 13;
constexpr UINT LM_SELECTITEM = WM_APP + 14;
// Devuelve una coordenada del boton + sin usar punteros entre procesos.
// wParam = indice * 4 + componente (0 izquierda, 1 arriba, 2 derecha, 3 abajo).
constexpr UINT LM_GETADDVAL = WM_APP + 15;

// Notificaciones enviadas al padre (CMainFrame).
constexpr UINT LN_SELCHANGED = WM_APP + 20;
constexpr UINT LN_ADD = WM_APP + 21;
constexpr UINT LN_DOUBLECLICK = WM_APP + 22;

// Galeria de la pestana "Clips": una miniatura por archivo importado y un
// boton "+" para copiarlo a una capa del timeline.
class CLibraryView : public CWnd {
public:
    void CreateView(CWnd* parent, UINT id);
    void SetItems(const std::vector<MediaItem>* items);
    void SetSelection(int index);
    int Selection() const { return m_selection; }
    void SetTimelineCount(int count);
    void Refresh();

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave();
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg LRESULT OnGetItemCount(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnGetAddRect(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnGetClipCount(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnAddItem(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnSelectItem(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnGetAddVal(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    struct Metrics {
        int itemH = 0;
        int margin = 0;
        int thumbW = 0;
        int thumbH = 0;
        int addSize = 0;
        int textLeft = 0;
        int textRight = 0;
        int radius = 0;
        int gap = 0;
        int line1H = 0;
        int line2Top = 0;
        int clientW = 0;
        int clientH = 0;
    };

    void EnsureFonts(CDC& dc);
    Metrics GetMetrics();
    int ItemCount() const;
    CRect ItemRect(int index, const Metrics& m) const;
    CRect AddRect(int index, const Metrics& m) const;
    CRect ThumbRect(int index, const Metrics& m) const;
    int ItemAtPoint(CPoint point, const Metrics& m) const;
    bool IsInAddRect(CPoint point, int index, const Metrics& m) const;
    void ClampScroll(const Metrics& m);
    void EnsureVisible(int index, const Metrics& m);
    void NotifyParent(UINT message, int index);
    void DrawThumbnail(CDC& dc, const MediaItem& item, const CRect& rect);

    const std::vector<MediaItem>* m_items = nullptr;
    int m_selection = -1;
    int m_hover = -1;
    int m_hoverAdd = -1;
    int m_pressedAdd = -1;
    int m_timelineCount = 0;
    int m_scroll = 0;
    bool m_trackingMouse = false;
    CFont m_fontName;
    CFont m_fontInfo;
    CFont m_fontAdd;
};
