/**
 * @file OSnapToolbar.cpp
 * @brief Object Snap 工具列實作
 */

#include "OSnapToolbar.h"
#include "OSnapManager.h"

#include <QAction>
#include <QLabel>
#include <QFont>
#include <QDebug>

namespace aicad {
namespace osnap {

OSnapToolbar::OSnapToolbar(OSnapManager* manager, QWidget* parent)
    : QToolBar("Object Snap", parent)
    , m_manager(manager)
{
    setObjectName("OSnapToolbar");
    setWindowTitle("Object Snap");
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setIconSize(QSize(20, 20));

    setupUI();

    if (m_manager) {
        connect(m_manager, &OSnapManager::snapLocked,
                this, &OSnapToolbar::onSnapLocked);
        connect(m_manager, &OSnapManager::snapCleared,
                this, &OSnapToolbar::onSnapCleared);
        connect(m_manager, &OSnapManager::settingsChanged,
                this, &OSnapToolbar::onSettingsChanged);

        syncFromManager();
    }
}

void OSnapToolbar::setupUI() {
    // ── 全開/全關 按鈕 ──────────────────────────────────────────────────────────
    QAction* enableAll  = addAction("All On");
    enableAll->setToolTip("Enable all snap types (F3)");
    enableAll->setShortcut(QKeySequence(Qt::Key_F3));
    enableAll->setIcon(QIcon(":/icons/osnap/OSNAP.ico"));
    connect(enableAll, &QAction::triggered,
            this, &OSnapToolbar::onEnableAllClicked);

    QAction* disableAll = addAction("All Off");
    disableAll->setToolTip("Disable all snap types (Shift+F3)");
    disableAll->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    disableAll->setIcon(QIcon(":/icons/osnap/OSNNON.ico"));
    connect(disableAll, &QAction::triggered,
            this, &OSnapToolbar::onDisableAllClicked);

    addSeparator();

    // ── 各 Snap 類型按鈕 ────────────────────────────────────────────────────────
    // 圖示路徑：使用 Qt resource system（應在 qrc 中定義）
    // 若圖示不存在，Qt 會顯示空白按鈕但功能正常
    addSnapButton(SnapType::Endpoint,      ":/icons/osnap/OSNEND.ico",
                  "Endpoint (ENDpoint)\nSnap to line endpoints and edge endpoints");
    addSnapButton(SnapType::Midpoint,      ":/icons/osnap/OSNMID.ico",
                  "Midpoint (MIDpoint)\nSnap to midpoint of edges");
    addSnapButton(SnapType::Center,        ":/icons/osnap/OSNCEN.ico",
                  "Center (CENter)\nSnap to center of circles, arcs, ellipses");
    addSnapButton(SnapType::Quadrant,      ":/icons/osnap/OSNQUA.ico",
                  "Quadrant (QUAdrant)\nSnap to 0°/90°/180°/270° of circles");
    addSnapButton(SnapType::Intersection,  ":/icons/osnap/OSNINT.ico",
                  "Intersection (INTersection)\nSnap to intersection of edges");
    addSnapButton(SnapType::Perpendicular, ":/icons/osnap/OSNPER.ico",
                  "Perpendicular (PERpendicular)\nSnap to perpendicular foot");
    addSnapButton(SnapType::Tangent,       ":/icons/osnap/OSNTAN.ico",
                  "Tangent (TANgent)\nSnap to tangent point on circles");
    addSnapButton(SnapType::Nearest,       ":/icons/osnap/OSNNEA.ico",
                  "Nearest (NEArest)\nSnap to nearest point on edge");
    addSnapButton(SnapType::Node,          ":/icons/osnap/OSNNOD.ico",
                  "Node (NODe)\nSnap to sketch geometry nodes");
    addSnapButton(SnapType::Extension,     ":/icons/osnap/OSNEXT.ico",
                  "Extension (EXTension)\nSnap on extension of edge");
    addSnapButton(SnapType::Parallel,     ":/icons/osnap/OSNPAR.ico",
                  "Parallel (PARallel)\nSnap to point along parallel direction");
    addSeparator();

    addSnapButton(SnapType::Grid,          ":/icons/osnap/OSNGRID.ico",
                  "Grid (GRID)\nSnap to grid points");

    addSeparator();

    // ── 狀態標籤（顯示目前鎖定的 snap 類型）──────────────────────────────────────
    m_statusLabel = new QLabel("", this);
    m_statusLabel->setFixedWidth(90);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    QFont f = m_statusLabel->font();
    f.setBold(true);
    f.setPointSize(8);
    m_statusLabel->setFont(f);
    m_statusLabel->setStyleSheet(
        "color: #FFD700; background: rgba(0,0,0,0); padding: 2px 4px;");
    addWidget(m_statusLabel);
}

void OSnapToolbar::addSnapButton(SnapType type,
                                 const QString& iconPath,
                                 const QString& tooltip)
{
    QAction* action = new QAction(this);
    QIcon icon(iconPath);
    if (icon.isNull() || icon.availableSizes().isEmpty()) {
        // ✅ Fallback：無圖示時顯示縮寫文字
        action->setText(snapTypeName(type).left(3));
    } else {
        action->setIcon(icon);
    }
    action->setToolTip(tooltip);
    action->setCheckable(true);
    action->setData(static_cast<int>(type));

    // 根據預設 settings 設定初始狀態
    if (m_manager) {
        action->setChecked(m_manager->isSnapTypeEnabled(type));
    }

    connect(action, &QAction::toggled,
            this, &OSnapToolbar::onActionToggled);

    addAction(action);
    m_actions[type] = action;
}

void OSnapToolbar::syncFromManager() {
    if (!m_manager) return;

    for (auto it = m_actions.begin(); it != m_actions.end(); ++it) {
        bool enabled = m_manager->isSnapTypeEnabled(it.key());
        QSignalBlocker blocker(it.value());  // 避免觸發 toggled 信號
        it.value()->setChecked(enabled);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Slots
// ──────────────────────────────────────────────────────────────────────────────
void OSnapToolbar::onActionToggled(bool checked) {
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action || !m_manager) return;

    SnapType type = static_cast<SnapType>(action->data().toInt());
    m_manager->setSnapTypeEnabled(type, checked);

    qDebug() << "[OSnapToolbar]" << snapTypeName(type)
             << (checked ? "on" : "off");
    Q_EMIT snapTypeToggled(type, checked);
}

void OSnapToolbar::onSnapLocked(const SnapCandidate& candidate) {
    if (m_statusLabel) {
        QString name = snapTypeName(candidate.type);
        m_statusLabel->setText(name);

        // 顏色對應 snap 類型
        Quantity_Color qc(snapColor(candidate.type));
        int r = static_cast<int>(qc.Red()   * 255);
        int g = static_cast<int>(qc.Green() * 255);
        int b = static_cast<int>(qc.Blue()  * 255);
        m_statusLabel->setStyleSheet(
            QString("color: rgb(%1,%2,%3); background: rgba(0,0,0,0); "
                    "padding: 2px 4px; font-weight: bold;")
                .arg(r).arg(g).arg(b));
    }
}

void OSnapToolbar::onSnapCleared() {
    if (m_statusLabel) {
        m_statusLabel->setText("");
        m_statusLabel->setStyleSheet(
            "color: #808080; background: rgba(0,0,0,0); padding: 2px 4px;");
    }
}

void OSnapToolbar::onSettingsChanged(const OSnapSettings& settings) {
    for (auto it = m_actions.begin(); it != m_actions.end(); ++it) {
        bool enabled = settings.enabledTypes.testFlag(it.key());
        QSignalBlocker blocker(it.value());
        it.value()->setChecked(enabled);
    }
}

void OSnapToolbar::onEnableAllClicked() {
    if (!m_manager) return;
    m_manager->setSettings([this]() {
        OSnapSettings s = m_manager->settings();
        s.enabledTypes = SnapType::All;
        return s;
    }());
    qDebug() << "[OSnapToolbar] All snap types enabled";
}

void OSnapToolbar::onDisableAllClicked() {
    if (!m_manager) return;
    m_manager->setSettings([this]() {
        OSnapSettings s = m_manager->settings();
        s.enabledTypes = SnapType::None;
        return s;
    }());
    qDebug() << "[OSnapToolbar] All snap types disabled";
}

} // namespace osnap
} // namespace aicad
