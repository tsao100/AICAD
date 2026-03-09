#include "CommandLineLayout.h"
#include <QPropertyAnimation>
#include <QDebug>

namespace aicad {
namespace ui {

CommandLineLayout::CommandLineLayout(QWidget* parentWidget, QObject* parent)
    : QObject(parent)
    , m_parentWidget(parentWidget)
    , m_mainLayout(nullptr)
    , m_layoutMode(LayoutMode::Compact)
    , m_historyLineCount(3)
{
    setupLayout();
}

CommandLineLayout::~CommandLineLayout() = default;

void CommandLineLayout::setupLayout() {
    m_mainLayout = new QVBoxLayout(m_parentWidget);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // 初始化區域狀態
    m_areaStates[CommandLineArea::Toolbar] =
        AreaState(true, DEFAULT_TOOLBAR_HEIGHT, 25, 50, false, true);

    m_areaStates[CommandLineArea::History] =
        AreaState(true, DEFAULT_HISTORY_HEIGHT, HISTORY_LINE_HEIGHT, 500, false, false);

    m_areaStates[CommandLineArea::Options] =
        AreaState(false, DEFAULT_OPTIONS_HEIGHT, 0, 80, false, false);

    m_areaStates[CommandLineArea::Input] =
        AreaState(true, DEFAULT_INPUT_HEIGHT, 25, 50, false, true);

    m_areaStates[CommandLineArea::StatusBar] =
        AreaState(false, DEFAULT_STATUSBAR_HEIGHT, 20, 30, false, true);
}

void CommandLineLayout::addArea(CommandLineArea area, QWidget* widget) {
    if (!widget) return;

    m_areas[area] = widget;

    // 根據區域順序添加到佈局
    int insertIndex = 0;

    if (area == CommandLineArea::Toolbar) {
        insertIndex = 0;
    } else if (area == CommandLineArea::History) {
        insertIndex = m_areas.contains(CommandLineArea::Toolbar) ? 1 : 0;
    } else if (area == CommandLineArea::Options) {
        insertIndex = m_mainLayout->count();
        if (m_areas.contains(CommandLineArea::Input)) insertIndex--;
        if (m_areas.contains(CommandLineArea::StatusBar)) insertIndex--;
    } else if (area == CommandLineArea::Input) {
        insertIndex = m_mainLayout->count();
        if (m_areas.contains(CommandLineArea::StatusBar)) insertIndex--;
    } else { // StatusBar
        insertIndex = m_mainLayout->count();
    }

    m_mainLayout->insertWidget(insertIndex, widget);
    applyAreaState(area);
}

void CommandLineLayout::removeArea(CommandLineArea area) {
    if (!m_areas.contains(area)) return;

    QWidget* widget = m_areas[area];
    m_mainLayout->removeWidget(widget);
    m_areas.remove(area);
}

QWidget* CommandLineLayout::getArea(CommandLineArea area) const {
    return m_areas.value(area, nullptr);
}

void CommandLineLayout::setAreaVisible(CommandLineArea area, bool visible, bool animated) {
    if (!m_areas.contains(area)) return;

    QWidget* widget = m_areas[area];
    AreaState& state = m_areaStates[area];

    if (state.visible == visible) return;

    state.visible = visible;

    if (animated && area == CommandLineArea::History) {
        int targetHeight = visible ? state.height : 0;
        animateAreaHeight(widget, widget->height(), targetHeight);
    } else {
        widget->setVisible(visible);
        if (visible && state.height > 0) {
            widget->setFixedHeight(state.height);
        }
    }

    updateLayout();
    emit areaVisibilityChanged(area, visible);
}

bool CommandLineLayout::isAreaVisible(CommandLineArea area) const {
    return m_areaStates.value(area).visible;
}

void CommandLineLayout::toggleArea(CommandLineArea area) {
    setAreaVisible(area, !isAreaVisible(area));
}

void CommandLineLayout::setAreaHeight(CommandLineArea area, int height) {
    if (!m_areas.contains(area)) return;

    AreaState& state = m_areaStates[area];
    height = qBound(state.minHeight, height, state.maxHeight);

    state.height = height;

    QWidget* widget = m_areas[area];
    if (state.visible && !state.collapsed) {
        widget->setFixedHeight(height);
    }

    emit areaSizeChanged(area, height);
}

int CommandLineLayout::getAreaHeight(CommandLineArea area) const {
    return m_areaStates.value(area).height;
}

void CommandLineLayout::setAreaMinHeight(CommandLineArea area, int minHeight) {
    m_areaStates[area].minHeight = minHeight;
}

void CommandLineLayout::setAreaMaxHeight(CommandLineArea area, int maxHeight) {
    m_areaStates[area].maxHeight = maxHeight;
}

void CommandLineLayout::setLayoutMode(LayoutMode mode) {
    if (m_layoutMode == mode) return;

    m_layoutMode = mode;
    applyLayoutMode();

    emit layoutModeChanged(mode);
}

void CommandLineLayout::applyLayoutMode() {
    switch (m_layoutMode) {
    case LayoutMode::Compact:
        setAreaVisible(CommandLineArea::Toolbar, false, false);
        setAreaVisible(CommandLineArea::History, false, false);
        setAreaVisible(CommandLineArea::Options, false, false);
        setAreaVisible(CommandLineArea::StatusBar, false, false);
        setHistoryLineCount(0);
        break;

    case LayoutMode::Standard:
        setAreaVisible(CommandLineArea::Toolbar, false, false);
        setAreaVisible(CommandLineArea::History, true, true);
        setAreaVisible(CommandLineArea::Options, false, false);
        setAreaVisible(CommandLineArea::StatusBar, false, false);
        setHistoryLineCount(3);
        break;

    case LayoutMode::Extended:
        setAreaVisible(CommandLineArea::Toolbar, true, false);
        setAreaVisible(CommandLineArea::History, true, true);
        setAreaVisible(CommandLineArea::Options, false, false);
        setAreaVisible(CommandLineArea::StatusBar, true, false);
        setHistoryLineCount(6);
        break;

    case LayoutMode::Full:
        setAreaVisible(CommandLineArea::Toolbar, true, false);
        setAreaVisible(CommandLineArea::History, true, true);
        setAreaVisible(CommandLineArea::Options, true, false);
        setAreaVisible(CommandLineArea::StatusBar, true, false);
        setHistoryLineCount(20);
        break;
    }
}

void CommandLineLayout::setHistoryLineCount(int lines) {
    m_historyLineCount = qBound(0, lines, 50);

    int height = m_historyLineCount * HISTORY_LINE_HEIGHT;
    setAreaHeight(CommandLineArea::History, height);

    emit historyLineCountChanged(m_historyLineCount);
}

void CommandLineLayout::expandHistory() {
    if (m_layoutMode == LayoutMode::Compact) {
        setLayoutMode(LayoutMode::Standard);
    } else if (m_layoutMode == LayoutMode::Standard) {
        setLayoutMode(LayoutMode::Extended);
    } else if (m_layoutMode == LayoutMode::Extended) {
        setLayoutMode(LayoutMode::Full);
    } else {
        setLayoutMode(LayoutMode::Compact);
    }
}

void CommandLineLayout::collapseHistory() {
    setLayoutMode(LayoutMode::Compact);
}

void CommandLineLayout::onHistoryExpandRequested() {
    expandHistory();
}

int CommandLineLayout::totalHeight() const {
    int total = 0;

    for (auto it = m_areaStates.constBegin(); it != m_areaStates.constEnd(); ++it) {
        const AreaState& state = it.value();
        if (state.visible && !state.collapsed) {
            total += state.height;
        }
    }

    return total;
}

void CommandLineLayout::saveState(QSettings* settings) {
    if (!settings) return;

    settings->beginGroup("CommandLineLayout");
    settings->setValue("layoutMode", static_cast<int>(m_layoutMode));
    settings->setValue("historyLineCount", m_historyLineCount);

    for (auto it = m_areaStates.constBegin(); it != m_areaStates.constEnd(); ++it) {
        QString areaName = QString::number(static_cast<int>(it.key()));
        settings->beginGroup(areaName);

        const AreaState& state = it.value();
        settings->setValue("visible", state.visible);
        settings->setValue("height", state.height);

        settings->endGroup();
    }

    settings->endGroup();
}

void CommandLineLayout::restoreState(QSettings* settings) {
    if (!settings) return;

    settings->beginGroup("CommandLineLayout");

    int mode = settings->value("layoutMode", static_cast<int>(LayoutMode::Compact)).toInt();
    m_layoutMode = static_cast<LayoutMode>(mode);

    m_historyLineCount = settings->value("historyLineCount", 3).toInt();

    for (auto it = m_areaStates.begin(); it != m_areaStates.end(); ++it) {
        QString areaName = QString::number(static_cast<int>(it.key()));
        settings->beginGroup(areaName);

        AreaState& state = it.value();
        state.visible = settings->value("visible", state.visible).toBool();
        state.height = settings->value("height", state.height).toInt();

        applyAreaState(it.key());

        settings->endGroup();
    }

    settings->endGroup();

    applyLayoutMode();
}

void CommandLineLayout::updateLayout() {
    m_mainLayout->update();
    m_parentWidget->updateGeometry();
}

void CommandLineLayout::applyAreaState(CommandLineArea area) {
    if (!m_areas.contains(area)) return;

    QWidget* widget = m_areas[area];
    const AreaState& state = m_areaStates[area];

    widget->setVisible(state.visible && !state.collapsed);

    if (state.locked || (state.visible && state.height > 0)) {
        widget->setFixedHeight(state.height);
    } else {
        widget->setMinimumHeight(state.minHeight);
        widget->setMaximumHeight(state.maxHeight);
    }
}

void CommandLineLayout::animateAreaHeight(QWidget* widget, int from, int to) {
    QPropertyAnimation* animation = new QPropertyAnimation(widget, "maximumHeight");
    animation->setDuration(200);
    animation->setStartValue(from);
    animation->setEndValue(to);
    animation->setEasingCurve(QEasingCurve::InOutQuad);

    if (to > 0) {
        widget->show();
    }

    connect(animation, &QPropertyAnimation::finished, [widget, to]() {
        if (to == 0) {
            widget->hide();
        } else {
            widget->setMaximumHeight(QWIDGETSIZE_MAX);
        }
    });

    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace ui
} // namespace aicad
