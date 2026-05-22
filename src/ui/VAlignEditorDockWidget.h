/**
 * @file VAlignEditorDockWidget.h
 * @brief Dock widget that hosts the vertical-alignment profile view and
 *        its associated properties panel.
 *
 * Layout
 * ──────
 *   ┌─────────────────────────────────────────────────────┐
 *   │  Toolbar: Select | Add VIP | Del VIP | toggles       │
 *   ├────────────────────────────────────┬────────────────┤
 *   │                                    │                │
 *   │   VAlignProfileView                │  PropertiesPanel│
 *   │   (profile + H-align strip)        │  (editable VIP │
 *   │                                    │   fields +     │
 *   │                                    │   computed info)│
 *   └────────────────────────────────────┴────────────────┘
 *
 * @author AICAD Team
 * @date   2025-01
 */
#pragma once

#include <QDockWidget>
#include <QPointer>

// Forward declarations
class QToolBar;
class QAction;
class QActionGroup;
class QSplitter;
class QLabel;

// Railway / alignment
namespace aicad { namespace railway { class AlignmentDocument; } }

namespace aicad {
namespace railway {
class TrackCenterLine;
struct VerticalAlignmentPoint;
}
namespace ui {

class VAlignProfileView;
class VAlignPropertiesPanel;
class VAlignCommandBar;

// ─────────────────────────────────────────────────────────────────────────────

class VAlignEditorDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit VAlignEditorDockWidget(QWidget* parent = nullptr);
    ~VAlignEditorDockWidget() override;

    /** Bind to a live TrackCenterLine.  Ownership stays with the caller. */
    void setTrackCenterLine(railway::TrackCenterLine* tcl);
    railway::TrackCenterLine* trackCenterLine() const;

    /**
     * @brief Step 16：連接 AlignmentDocument（水平＋縱斷面聯動）。
     *
     * 連接後，每當水平 alignment 求解完成（changed()），
     * 自動更新 VAlignProfileView 的水平元素條帶與總里程。
     */
    void setAlignmentDocument(railway::AlignmentDocument* doc);

    /** 取得底部命令列（供命令層驅動提示與輸入）。 */
    VAlignCommandBar* commandBar() const { return m_commandBar; }

Q_SIGNALS:
    /** Emitted whenever the user modifies the vertical alignment. */
    void alignmentChanged();

private Q_SLOTS:
    void onToolChanged(QAction* action);
    void onViewToggled();
    void onVipSelected(int index);
    void onFormApplied(double ch, double el, double lvc);
    void onVipDeleted(int index);
    void onVipAdded(double ch, double el);
    void onCursorMoved(double ch, double el);

private:
    void setupToolbar();
    void setupContent();
    void updateCursorLabel(double ch, double el);
    void refreshPanel(int selIdx);

    // ── Widgets ────────────────────────────────────────────────────────────
    QToolBar*             m_toolbar     = nullptr;
    QSplitter*            m_splitter    = nullptr;
    VAlignProfileView*    m_profileView = nullptr;
    VAlignPropertiesPanel* m_propsPanel = nullptr;
    QLabel*               m_cursorLabel = nullptr;
    VAlignCommandBar*     m_commandBar  = nullptr;   ///< Step 13

    // ── Actions ────────────────────────────────────────────────────────────
    QAction* m_actSelect  = nullptr;
    QAction* m_actAddVip  = nullptr;
    QAction* m_actDelVip  = nullptr;
    QAction* m_actGrid    = nullptr;
    QAction* m_actGrade   = nullptr;
    QAction* m_actVC      = nullptr;
    QAction* m_actKval    = nullptr;

    // ── Data ───────────────────────────────────────────────────────────────
    QPointer<railway::TrackCenterLine>   m_tcl;
    QPointer<railway::AlignmentDocument> m_alignDoc;  ///< Step 16
};

} // namespace ui
} // namespace aicad