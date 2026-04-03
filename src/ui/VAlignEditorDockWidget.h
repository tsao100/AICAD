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

namespace aicad {
namespace railway {
class TrackCenterLine;
struct VerticalAlignmentPoint;
}
namespace ui {

class VAlignProfileView;
class VAlignPropertiesPanel;

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

    // ── Widgets ────────────────────────────────────────────────────────────
    QToolBar*             m_toolbar     = nullptr;
    QSplitter*            m_splitter    = nullptr;
    VAlignProfileView*    m_profileView = nullptr;
    VAlignPropertiesPanel* m_propsPanel = nullptr;
    QLabel*               m_cursorLabel = nullptr;

    // ── Actions ────────────────────────────────────────────────────────────
    QAction* m_actSelect  = nullptr;
    QAction* m_actAddVip  = nullptr;
    QAction* m_actDelVip  = nullptr;
    QAction* m_actGrid    = nullptr;
    QAction* m_actGrade   = nullptr;
    QAction* m_actVC      = nullptr;
    QAction* m_actKval    = nullptr;

    // ── Data ───────────────────────────────────────────────────────────────
    QPointer<railway::TrackCenterLine> m_tcl;
};

} // namespace ui
} // namespace aicad
