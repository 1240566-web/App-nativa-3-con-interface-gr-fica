#pragma once

#include <afxwin.h>
#include <afxcmn.h>
#include <afxdlgs.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "PreviewPlayer.h"
#include "PreviewAudio.h"
#include "PreviewView.h"
#include "LibraryView.h"
#include "TimelineView.h"
#include "VideoEditorTypes.h"

// Pestanas dibujadas a mano: el control estandar no respeta el tema oscuro
// en Windows 10/11, asi que pintamos la tira completa y cada lengueta.
class CDarkTabCtrl : public CTabCtrl {
public:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave();
    DECLARE_MESSAGE_MAP()

private:
    // Alto de la tira de lenguetas; solo esa franja cambia al pasar el cursor.
    int StripBottom(int dpi) const;
    void InvalidateStrip();

    int m_hover = -1;
};

// Casilla de verificacion con pintura propia. Conserva BS_AUTOCHECKBOX para
// que el control siga alternando su estado y enviando BN_CLICKED.
class CDarkCheckBox : public CButton {
public:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave();
    DECLARE_MESSAGE_MAP()

private:
    bool m_hover = false;
};

constexpr UINT WM_EXPORT_PROGRESS = WM_APP + 1;
constexpr UINT WM_EXPORT_FINISHED = WM_APP + 2;

enum : UINT {
    IDC_APP_TITLE = 1000,
    IDC_TOTAL_LABEL,
    IDC_CLIPS_LABEL,
    IDC_CLIP_LIST,
    IDC_BTN_ADD,
    IDC_BTN_REMOVE,
    IDC_BTN_UP,
    IDC_BTN_DOWN,
    IDC_BTN_SPLIT,
    IDC_FILTER_LABEL,
    IDC_CHK_NEGATE,
    IDC_CHK_FLIP_H,
    IDC_CHK_FLIP_V,
    IDC_SPEED_LABEL,
    IDC_SPEED_COMBO,
    IDC_RES_LABEL,
    IDC_RES_COMBO,
    IDC_BTN_EXPORT,
    IDC_BTN_CANCEL,
    IDC_STATUS,
    IDC_PROGRESS,
    IDC_PREVIEW,
    IDC_BTN_PREV,
    IDC_BTN_PLAY,
    IDC_BTN_NEXT,
    IDC_TIME_LABEL,
    IDC_TIMELINE,
    IDC_TRACK_LABEL,
    IDC_TRACK_COMBO,
    IDC_SCALE_LABEL,
    IDC_SCALE_SLIDER,
    IDC_POSX_LABEL,
    IDC_POSX_SLIDER,
    IDC_POSY_LABEL,
    IDC_POSY_SLIDER,
    IDC_BTN_CENTER,
    IDC_VOLUME_LABEL,
    IDC_VOLUME_SLIDER,
    IDC_SCALE_VALUE,
    IDC_POSX_VALUE,
    IDC_POSY_VALUE,
    IDC_VOLUME_VALUE,
    IDC_TABS,
    IDC_TRANS_LABEL,
    IDC_TRANS_FROM_LABEL,
    IDC_TRANS_FROM_COMBO,
    IDC_TRANS_TO_LABEL,
    IDC_TRANS_TO_COMBO,
    IDC_TRANS_TYPE_LABEL,
    IDC_TRANS_TYPE_COMBO,
    IDC_TRANS_DUR_LABEL,
    IDC_TRANS_DUR_SLIDER,
    IDC_TRANS_DUR_VALUE,
    IDC_BTN_TRANS_ADD,
    IDC_BTN_TRANS_REMOVE,
    IDC_TRANS_LIST,
    IDC_TRANS_HINT,
    IDC_EXPORT_LABEL,
    IDC_EXPORT_HINT,
    IDC_TIMELINE_ZOOM_LABEL,
    IDC_TIMELINE_ZOOM_SLIDER,
    IDC_TIMELINE_ZOOM_VALUE,
    IDC_TRANSFORM_HINT,
    // Al final para no desplazar los ID existentes (los tests los usan).
    IDC_BTN_UNDO,
    IDC_BTN_REDO,
    // Pestana "Textos".
    IDC_TXT_LABEL,
    IDC_TXT_LIST,
    IDC_TXT_ADD,
    IDC_TXT_REMOVE,
    IDC_TXT_CONTENT_LABEL,
    IDC_TXT_CONTENT,
    IDC_TXT_SIZE_LABEL,
    IDC_TXT_SIZE,
    IDC_TXT_BOLD,
    IDC_TXT_ITALIC,
    IDC_TXT_UNDERLINE,
    IDC_TXT_ALIGN_LABEL,
    IDC_TXT_ALIGN_COMBO,
    IDC_TXT_HINT,
    IDC_TXT_TRACK_LABEL,
    IDC_TXT_TRACK_COMBO,
    IDC_TXT_DUR_LABEL,
    IDC_TXT_DUR_SLIDER,
    IDC_TXT_DUR_VALUE,
    // Transiciones: los ID antiguos (1044..1047) se conservan pero ya no se crean.
    IDC_TRANS_DIRECTION_TABS,
    IDC_TRANS_SELECTED_LABEL
};

class CMainFrame : public CFrameWnd {
public:
    CMainFrame();
    ~CMainFrame() override;

    BOOL PreCreateWindow(CREATESTRUCT& cs) override;
    BOOL PreTranslateMessage(MSG* pMsg) override;

    // Agrega archivos recibidos por linea de comandos (abrir con...).
    void AddInitialFiles(const std::vector<std::wstring>& paths);

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
    afx_msg void OnDropFiles(HDROP hDropInfo);
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnClose();
    afx_msg void OnDestroy();

    afx_msg void OnAddFiles();
    afx_msg void OnRemoveClip();
    afx_msg void OnMoveUp();
    afx_msg void OnMoveDown();
    afx_msg void OnSplit();
    afx_msg void OnFilterChanged();
    afx_msg void OnSpeedChanged();
    afx_msg void OnTrackChanged();
    afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg void OnCenterClip();
    afx_msg void OnPrevFrame();
    afx_msg void OnNextFrame();
    afx_msg void OnPlayPause();
    afx_msg void OnExport();
    afx_msg void OnCancelExport();
    afx_msg void OnAddTransition();
    afx_msg void OnRemoveTransition();
    afx_msg void OnTransDirectionChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnUndo();
    afx_msg void OnRedo();

    afx_msg void OnTextAdd();
    afx_msg void OnTextRemove();
    afx_msg void OnTextListSelChange();
    afx_msg void OnTextContentChanged();
    afx_msg void OnTextEditFocus();
    afx_msg void OnTextSizeChanged();
    afx_msg void OnTextStyleChanged();
    afx_msg void OnTextAlignChanged();
    afx_msg void OnTextTrackChanged();

    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
    void DrawHistoryArrow(CDC& dc, const CRect& rect, bool undo, bool disabled);
    afx_msg LRESULT OnLibrarySelectionChanged(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnLibraryAdd(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnLibraryDoubleClick(WPARAM wParam, LPARAM lParam);
    afx_msg void OnTabChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnTabsCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);

    afx_msg LRESULT OnExportProgress(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnExportFinished(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    struct ExportContext {
        std::wstring ffmpegPath;
        std::wstring arguments;
        std::wstring outputPath;
        std::wstring log;
        double totalDuration = 0.0;
        int exitCode = -1;
        std::atomic<bool> cancel{false};
        std::atomic<HANDLE> process{nullptr};
    };

    // Estado completo del proyecto antes de una accion editable. Con esto
    // deshacer/rehacer funcionan igual para clips, transiciones y ajustes.
    struct ProjectSnapshot {
        std::vector<ClipInfo> clips;
        std::vector<TransitionInfo> transitions;
        std::vector<TextInfo> texts;
        int selection = -1;
        int textSelection = -1;
        double playhead = 0.0;
    };

    ProjectSnapshot CaptureSnapshot() const;
    void CommitUndo(ProjectSnapshot before);
    void PushUndo();
    void RestoreSnapshot(const ProjectSnapshot& snapshot, const wchar_t* action);
    void UpdateUndoButtons();

    void CreateControls();
    void CreateFonts();
    void ApplyFonts();
    void LayoutControls();
    void LayoutTabPages(const CRect& page);
    void ShowTabPage(int index);
    void UpdateTransitionTarget();
    void UpdateTransitionList(int selectIndex = -1);
    int TransitionNeighborClip(int clipIndex, bool outgoing) const;
    void ApplyTransitionPlacements(int anchorClip = -1);
    void RemapTransitionsAfterInsert(int index);
    void RemapTransitionsAfterRemove(int index);
    void DropTransitionsForClip(int index);
    bool AddTransitionFromControls(std::wstring* error);
    void RestoreTransitionGap(int transitionIndex);
    void InvalidateTransitionState();

    void UpdateTextList(int selectIndex = -1);
    void SyncTextControls();
    void RefreshTextPreview();
    TextInfo* SelectedText();
    const TextInfo* SelectedText() const;

    void UpdateLibrary(int selectIndex = -1);
    void UpdateTotalLabel();
    void UpdateTimeLabel();
    void UpdatePlayButton();
    void SetLibrarySelection(int index);
    void SetListSelection(int index);
    void SyncClipControls();
    void UpdateSliderLabels();
    void ApplyTransformToPreview();
    void ApplyTimelineZoom(int position);
    int TopVideoClipAtCanvasPoint(double x, double y) const;
    bool GetSelectionCanvasRect(double& left, double& top, double& right, double& bottom) const;
    bool PreviewDragBegin(double x, double y, int zone);
    void PreviewDragMove(double x, double y);
    void PreviewDragEnd();

    void AddPaths(const std::vector<std::wstring>& paths);
    void AddMediaToTimeline(int libraryIndex);
    static void GenerateThumbnail(const MediaItem& item, std::vector<unsigned char>& pixels,
                                  int& width, int& height);
    void SetExporting(bool exporting);
    void SetStatus(const std::wstring& text, bool error = false);

    std::wstring FindFfmpeg() const;
    std::wstring BuildOutputResolution(int& width, int& height) const;
    ExportSettings CurrentSettings() const;

    bool RebuildPreview(double time, bool resetTime);
    void ClearTextOverlays();
    void SeekTo(double time);
    void StepFrame(int direction);
    void SyncSelectionToTime(double time);
    void TogglePlayback();
    void StopPlayback();
    int ClipIndexAt(double outputTime) const;
    double ClipStartTime(int index) const;
    double TrackEndTime(int track) const;
    bool IsTransformEditFocused() const;

    void StartExport();
    void JoinExportThread();

    CStatic m_title;
    CStatic m_total;
    CDarkTabCtrl m_tabs;
    CStatic m_clipsLabel;
    CLibraryView m_libraryView;
    CButton m_btnAdd;
    CButton m_btnRemove;
    CButton m_btnUp;
    CButton m_btnDown;
    CButton m_btnSplit;
    CButton m_btnUndo;
    CButton m_btnRedo;
    CStatic m_filterLabel;
    CDarkCheckBox m_chkNegate;
    CDarkCheckBox m_chkFlipH;
    CDarkCheckBox m_chkFlipV;
    CStatic m_speedLabel;
    CComboBox m_speedCombo;
    CStatic m_trackLabel;
    CComboBox m_trackCombo;
    CStatic m_scaleLabel;
    CStatic m_scaleValue;
    CStatic m_volumeLabel;
    CSliderCtrl m_volumeSlider;
    CStatic m_volumeValue;
    CStatic m_transformHint;
    CButton m_btnCenter;
    CStatic m_zoomLabel;
    CSliderCtrl m_zoomSlider;
    CStatic m_zoomValue;
    CStatic m_resLabel;
    CComboBox m_resCombo;
    CButton m_btnExport;
    CButton m_btnCancel;
    CStatic m_exportLabel;
    CStatic m_exportHint;
    CStatic m_transLabel;
    CDarkTabCtrl m_transDirectionTabs;
    CStatic m_transSelectedLabel;
    CStatic m_transTypeLabel;
    CComboBox m_transTypeCombo;
    CStatic m_transDurLabel;
    CSliderCtrl m_transDurSlider;
    CStatic m_transDurValue;
    CButton m_btnTransAdd;
    CButton m_btnTransRemove;
    CStatic m_transHint;
    CListCtrl m_transList;

    CStatic m_txtLabel;
    CListBox m_txtList;
    CButton m_txtAdd;
    CButton m_txtRemove;
    CStatic m_txtContentLabel;
    CEdit m_txtContent;
    CStatic m_txtSizeLabel;
    CEdit m_txtSize;
    CDarkCheckBox m_txtBold;
    CDarkCheckBox m_txtItalic;
    CDarkCheckBox m_txtUnderline;
    CStatic m_txtAlignLabel;
    CComboBox m_txtAlignCombo;
    CStatic m_txtTrackLabel;
    CComboBox m_txtTrackCombo;
    CStatic m_txtDurLabel;
    CSliderCtrl m_txtDurSlider;
    CStatic m_txtDurValue;
    CStatic m_txtHint;

    CStatic m_status;
    CProgressCtrl m_progress;
    CPreviewView m_previewView;
    CButton m_btnPrev;
    CButton m_btnPlay;
    CButton m_btnNext;
    CStatic m_timeLabel;
    CTimelineView m_timeline;

    CFont m_fontUi;
    CFont m_fontTitle;
    CFont m_fontSection;
    CFont m_fontBold;
    CFont m_fontMono;
    CBrush m_background;
    CBrush m_panelBrush;
    COLORREF m_statusColor = RGB(150, 156, 170);

    std::vector<MediaItem> m_library;
    std::vector<ClipInfo> m_clips;
    std::vector<TransitionInfo> m_transitions;
    std::vector<TextInfo> m_texts;
    std::vector<double> m_speedValues;
    int m_nextMediaId = 1;
    int m_librarySelection = -1;
    int m_selection = -1;
    int m_textSelection = -1;
    int m_tabIndex = 0;
    bool m_syncingSelection = false;
    bool m_syncingText = false;
    bool m_textEditUndoPushed = false;
    bool m_textDurDragging = false;
    bool m_textDurUndoPushed = false;
    bool m_syncingClipSettings = false;
    bool m_syncingZoom = false;

    // Deshacer/rehacer. Las acciones continuas (arrastres) guardan una sola
    // instantanea por gesto, no una por mensaje de movimiento.
    std::vector<ProjectSnapshot> m_undoStack;
    std::vector<ProjectSnapshot> m_redoStack;
    static constexpr size_t kMaxUndoSteps = 50;
    bool m_timelineDragCaptured = false;
    bool m_previewDragUndoPushed = false;
    bool m_volumeDragging = false;
    bool m_volumeUndoPushed = false;

    bool m_previewDragging = false;
    int m_dragClip = -1;
    int m_dragZone = 0;
    double m_dragStartCanvasX = 0.0;
    double m_dragStartCanvasY = 0.0;
    double m_dragStartPosX = 0.0;
    double m_dragStartPosY = 0.0;
    double m_dragStartScale = 1.0;
    double m_dragStartScaleX = 1.0;
    double m_dragStartScaleY = 1.0;
    double m_resizeAnchorX = 0.0;
    double m_resizeAnchorY = 0.0;
    double m_resizeStartW = 0.0;
    double m_resizeStartH = 0.0;
    double m_resizeStartCenterX = 0.0;
    double m_resizeStartCenterY = 0.0;

    // Arrastre de la caja de texto en la vista previa.
    bool m_textDragging = false;
    int m_dragText = -1;
    int m_dragTextZone = 0;
    double m_textDragStartX = 0.0;
    double m_textDragStartY = 0.0;
    double m_textDragBoxX = 0.0;
    double m_textDragBoxY = 0.0;
    double m_textDragBoxW = 0.0;
    double m_textDragBoxH = 0.0;

    // Imán al mover: alineacion por eje (0 borde inicial, 1 final, 2 centro).
    bool m_snapXActive = false;
    bool m_snapYActive = false;
    int m_snapXKind = -1;
    int m_snapYKind = -1;
    double m_snapXValue = 0.0;
    double m_snapYValue = 0.0;

    bool m_playing = false;
    ULONGLONG m_playStartTick = 0;
    double m_playStartTime = 0.0;

    PreviewPlayer m_preview;
    PreviewAudio m_audio;

    bool m_exporting = false;
    bool m_closing = false;
    std::wstring m_textOverlayPath;
    std::vector<std::wstring> m_textOverlayPaths;
    std::shared_ptr<ExportContext> m_exportContext;
    std::thread m_exportThread;
};
