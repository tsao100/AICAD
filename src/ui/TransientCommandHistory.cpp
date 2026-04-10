#include "TransientCommandHistory.h"
#include <QPainter>
#include <QApplication>

namespace aicad {
namespace ui {

TransientCommandHistory::TransientCommandHistory(QWidget* anchor)
    : QWidget(anchor->window(), Qt::Tool | Qt::FramelessWindowHint |
                                    Qt::WindowTransparentForInput)
    , m_anchor(anchor)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(4, 2, 4, 2);
    m_layout->setSpacing(1);

    m_fadeTimer = new QTimer(this);
    m_fadeTimer->setSingleShot(true);
    connect(m_fadeTimer, &QTimer::timeout, this, &TransientCommandHistory::beginFadeOut);
}

void TransientCommandHistory::addLine(const QString& text, bool isPrompt) {
    // 停止進行中的淡出
    if (m_fadeAnim) { m_fadeAnim->stop(); }
    setOpacity(1.0);

    m_lines.append(text);
    m_isPrompt.append(isPrompt);
    while (m_lines.size() > m_maxLines) {
        m_lines.removeFirst();
        m_isPrompt.removeFirst();
    }
    rebuildLabels();
    repositionAboveAnchor();
    show();

    // 重設淡出計時（2 秒後淡出）
    m_fadeTimer->start(2000);
}

void TransientCommandHistory::setMaxLines(int n) {
    m_maxLines = qBound(1, n, 3);
}

void TransientCommandHistory::beginFadeOut() {
    if (!m_fadeAnim) {
        m_fadeAnim = new QPropertyAnimation(this, "opacity");
        m_fadeAnim->setDuration(800);
        m_fadeAnim->setStartValue(1.0);
        m_fadeAnim->setEndValue(0.0);
        m_fadeAnim->setEasingCurve(QEasingCurve::InQuad);
        connect(m_fadeAnim, &QPropertyAnimation::finished,
                this, &QWidget::hide);
    }
    m_fadeAnim->start();
}

void TransientCommandHistory::setOpacity(qreal v) {
    m_opacity = v;
    setWindowOpacity(v);
    update();
}

void TransientCommandHistory::rebuildLabels() {
    QLayoutItem* item;
    while ((item = m_layout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    for (int i = 0; i < m_lines.size(); ++i) {
        auto* lbl = new QLabel(m_lines[i], this);
        QPalette pal = lbl->palette();
        pal.setColor(QPalette::WindowText,
                     m_isPrompt[i] ? Qt::gray : Qt::white);
        lbl->setPalette(pal);
        lbl->setWordWrap(false);
        m_layout->addWidget(lbl);
    }
    adjustSize();
}

void TransientCommandHistory::repositionAboveAnchor() {
    if (!m_anchor) return;
    QPoint anchorGlobal = m_anchor->mapToGlobal(QPoint(0, 0));
    // 底部對齊 anchor 上緣
    int x = anchorGlobal.x();
    int y = anchorGlobal.y()-60; // - height() - 2;
    move(x, y);
    resize(m_anchor->width(), height());
}

void TransientCommandHistory::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(30, 30, 30, 200));
    QWidget::paintEvent(event);
}

} // namespace ui
} // namespace aicad
