#pragma once

#include <afxwin.h>

#include <functional>

#include "PreviewPlayer.h"

// Consulta de automatizacion: devuelve una coordenada del contorno de seleccion
// en pixeles de cliente. wParam = 0 izquierda, 1 arriba, 2 derecha, 3 abajo.
// Se usan valores sueltos (sin punteros) para poder consultarlo entre procesos.
constexpr UINT PV_GETSELECTIONVAL = WM_APP + 30;

// Panel que dibuja el frame actual de la vista previa, ajustado al area. El
// video se puede arrastrar para moverlo; sus esquinas cambian el tamano
// conservando la proporcion y los puntos medios de los bordes lo estiran
// solo en ancho o solo en alto.
class CPreviewView : public CWnd {
public:
    void CreateView(CWnd* parent, UINT id);
    void SetPlayer(PreviewPlayer* player) { m_player = player; }
    void Refresh();

    // Guias de alineacion mientras se mueve el clip. Coordenadas de lienzo
    // (0..ancho, 0..alto); pasar false para ocultarlas.
    void SetSnapGuides(bool vertical, double x, bool horizontal, double y);

    // Coordenadas del lienzo en pixeles (0..ancho, 0..alto). OnDragBegin
    // devuelve true si comienza un arrastre. zone: 0 = mover, 1..4 = esquinas,
    // 5 = izquierda, 6 = arriba, 7 = derecha, 8 = abajo (puntos medios).
    std::function<bool(double, double, int)> OnDragBegin;
    std::function<void(double, double)> OnDragMove;
    std::function<void()> OnDragEnd;

    // Devuelve el rectangulo del clip seleccionado en coordenadas de lienzo.
    std::function<bool(double&, double&, double&, double&)> GetSelectionRect;

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    afx_msg LRESULT OnGetSelectionVal(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    bool CanvasRect(CRect& rect) const;
    bool CanvasPoint(CPoint point, double& x, double& y, bool& inside) const;
    bool SelectionCanvasRect(CRect& rect) const;
    void DrawSelection(CDC& dc, const CRect& canvas);
    void DrawSnapGuides(CDC& dc, const CRect& canvas);
    int HitTestZone(CPoint point) const;
    void EnsureFonts(CDC& dc);

    PreviewPlayer* m_player = nullptr;
    bool m_dragging = false;
    int m_dragZone = 0;
    bool m_guideVertical = false;
    bool m_guideHorizontal = false;
    double m_guideX = 0.0;
    double m_guideY = 0.0;
    CFont m_fontHint;
    CFont m_fontEmpty;
};
