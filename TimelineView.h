#pragma once

#include <afxwin.h>

#include <functional>
#include <vector>

#include "VideoEditorTypes.h"

// Consulta de automatizacion: wParam = indice del clip; devuelve su inicio en
// milisegundos (o -1 si el indice no existe). Sin punteros, entre procesos.
constexpr UINT TV_GETCLIPSTART = WM_APP + 31;
// wParam = 0: duracion visible en ms; 1: inicio visible en ms.
constexpr UINT TV_GETVIEWVAL = WM_APP + 32;
// Consultas de texto: wParam = indice; devuelve ms (o -1 si no existe).
constexpr UINT TV_GETTEXTSTART = WM_APP + 33;
constexpr UINT TV_GETTEXTDURATION = WM_APP + 34;
constexpr UINT TV_GETTEXTTRACK = WM_APP + 35;

// Linea de tiempo con 4 filas: Capa 3, Capa 2, Capa 1 y Audio. Los clips se
// colocan libremente en el tiempo; se pueden arrastrar entre capas, recortar
// por los bordes y hacer zoom con la barra inferior o Ctrl + rueda.
class CTimelineView : public CWnd {
public:
    void CreateView(CWnd* parent, UINT id);
    void Attach(std::vector<ClipInfo>* clips);
    void AttachTransitions(std::vector<TransitionInfo>* transitions);
    void AttachTexts(std::vector<TextInfo>* texts);
    void Refresh();
    void SetPlayhead(double seconds, bool redraw = true);
    double Playhead() const { return m_playhead; }
    void SetSelection(int index, bool redraw = true);
    int Selection() const { return m_selection; }
    void SetTextSelection(int index, bool redraw = true);
    int TextSelection() const { return m_textSelection; }
    void SetSourceFps(double fps) { m_sourceFps = (fps > 0.0 ? fps : 30.0); }

    // Zoom horizontal: 1.0 muestra todo el proyecto, valores mayores acercan.
    void SetZoom(double zoom);
    double Zoom() const { return m_zoom; }

    std::function<void()> OnSeek;
    std::function<void()> OnSelectionChanged;
    std::function<void(int)> OnTextSelectionChanged;
    std::function<void(bool)> OnTrim;
    std::function<void(int)> OnZoomStep;

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnMouseHWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg LRESULT OnGetClipStart(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnGetViewVal(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnGetTextStart(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnGetTextDuration(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnGetTextTrack(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    enum class DragMode {
        None, Seek, TrimIn, TrimOut, Move,
        TextMove, TextTrimIn, TextTrimOut
    };

    double TotalDuration() const;
    double BaseDuration() const;
    double MaxViewStart() const;
    void ClampViewStart();
    void UpdateViewDuration();
    void UpdateScrollBar();
    double ViewDuration() const { return m_viewDuration; }
    int Pixel(int value) const;
    int LabelWidth() const;
    int TrackLeft() const;
    int TrackWidth() const;
    int RulerHeight() const;
    int TrackTop() const;
    int RowHeight() const;
    int TrackRow(int track) const;
    int TrackAtY(int y) const;
    CRect RowRect(int row) const;
    int TimeToX(double seconds) const;
    double XToTime(int x) const;
    CRect ClipRect(int index) const;
    CRect TextRect(int index) const;
    int HitClip(int x, int y) const;
    int HitText(int x, int y) const;
    void SelectAtTime(double time);
    void EnsureFont(CDC& dc);
    void DrawRuler(CDC& dc, const CRect& client);
    void DrawTracks(CDC& dc, const CRect& client);
    void DrawClips(CDC& dc);
    void DrawTexts(CDC& dc);
    void DrawTransitions(CDC& dc);
    void DrawPlayhead(CDC& dc, const CRect& client);
    void BeginTrim(int index, int x, bool leftSide);
    void BeginMove(int index, CPoint point);
    void BeginTextTrim(int index, int x, bool leftSide);
    void BeginTextMove(int index, CPoint point);
    void ApplyTrim(int x);
    void ApplyMove(CPoint point);
    void ApplyTextTrim(int x);
    void ApplyTextMove(CPoint point);
    double SnapToFrame(double seconds, double fps) const;
    double SnapToNeighborEdge(double value, int ignoreIndex, double threshold,
                              bool& snapped, double& guide,
                              int ignoreTextIndex = -1) const;
    void DrawSnapGuide(CDC& dc, const CRect& client);

    std::vector<ClipInfo>* m_clips = nullptr;
    std::vector<TransitionInfo>* m_transitions = nullptr;
    std::vector<TextInfo>* m_texts = nullptr;
    double m_playhead = 0.0;
    int m_selection = -1;
    int m_textSelection = -1;
    double m_sourceFps = 30.0;
    double m_viewDuration = 6.0;
    double m_baseDuration = 6.0;
    double m_viewStart = 0.0;
    double m_zoom = 1.0;
    CScrollBar m_scroll;

    DragMode m_drag = DragMode::None;
    int m_dragIndex = -1;
    int m_dragStartX = 0;
    int m_dragStartY = 0;
    double m_dragStartStart = 0.0;
    double m_dragStartDuration = 0.0;
    int m_dragStartTrack = 0;
    double m_dragStartIn = 0.0;
    double m_dragStartOut = 0.0;
    bool m_dragMoved = false;
    double m_dragSpan = 0.0;
    bool m_snapVisible = false;
    double m_snapTime = 0.0;

    CFont m_font;
    CFont m_fontSmall;
};
