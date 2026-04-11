#include "TransientCommandHistory.h"
#include <QPainter>

namespace aicad {
namespace ui {

TransientCommandHistory::TransientCommandHistory(QWidget* anchor)
    : QWidget(nullptr,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowTransparentForInput)
    , m_anchor(anchor)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    // 先呼叫 setMaxLines 建立初始 label 陣列
    setMaxLines(m_maxLines);

    m_fadeTimer = new QTimer(this);
    m_fadeTimer->setSingleShot(true);
    connect(m_fadeTimer, &QTimer::timeout,
            this, &TransientCommandHistory::beginFadeOut);
}

void TransientCommandHistory::setMaxLines(int n) {
    n = qMax(1, n);
    if (n == m_maxLines && !m_labels.isEmpty()) return;

    m_maxLines = n;

    // 刪除多餘的
    while (m_labels.size() > n) {
        delete m_labels.takeLast();
    }

    // 補足不夠的
    while (m_labels.size() < n) {
        auto* lbl = new QLabel(this);
        lbl->setContentsMargins(0, 0, 0, 0);
        lbl->hide();
        m_labels.append(lbl);
    }

    // 裁剪已記錄的行數，避免超出新上限
    while (m_lines.size() > m_maxLines) {
        m_lines.removeFirst();
        m_isPrompt.removeFirst();
    }

    // 若已顯示中，立刻重排
    if (isVisible())
        rebuildLabels();
}

void TransientCommandHistory::addLine(const QString& text, bool isPrompt) {
    if (m_fadeAnim) m_fadeAnim->stop();
    setOpacity(1.0);

    m_lines.append(text);
    m_isPrompt.append(isPrompt);
    while (m_lines.size() > m_maxLines) {
        m_lines.removeFirst();
        m_isPrompt.removeFirst();
    }

    // 先 rebuildLabels 確定高度，再 reposition
    rebuildLabels();
    repositionAboveAnchor();
    show();
    m_fadeTimer->start(2000);
}

void TransientCommandHistory::beginFadeOut() {
    if (!m_fadeAnim) {
        m_fadeAnim = new QPropertyAnimation(this, "opacity");
        m_fadeAnim->setDuration(800);
        m_fadeAnim->setEasingCurve(QEasingCurve::InQuad);
        connect(m_fadeAnim, &QPropertyAnimation::finished,
                this, &QWidget::hide);
    }
    m_fadeAnim->setStartValue(1.0);
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->start();
}

void TransientCommandHistory::setOpacity(qreal v) {
    m_opacity = v;
    setWindowOpacity(v);
}

void TransientCommandHistory::repositionAboveAnchor() {
    if (!m_anchor) return;
    const QPoint ag = m_anchor->mapToGlobal(QPoint(0, 0));
    const int    w  = m_anchor->width();

    resize(w, height());

    const int lineH = fontMetrics().height() + LINE_PADDING;
    for (int i = 0; i < m_lines.size(); ++i)
        m_labels[i]->setGeometry(6, i * lineH, w - 12, lineH);

    move(ag.x(), ag.y() - height() - 2);
}

void TransientCommandHistory::rebuildLabels() {
    const int lineH = fontMetrics().height() + LINE_PADDING;
    const int n     = m_lines.size();
    const int w     = width() > 0 ? width() : 300;

    resize(w, n * lineH);

    for (int i = 0; i < m_labels.size(); ++i) {
        if (i < n) {
            m_labels[i]->setText(m_lines[i]);

            QPalette pal = m_labels[i]->palette();
            pal.setColor(QPalette::WindowText,
                         m_isPrompt[i] ? QColor(0xaa, 0xaa, 0xaa) : Qt::white);
            m_labels[i]->setPalette(pal);

            m_labels[i]->setGeometry(6, i * lineH, w - 12, lineH);
            m_labels[i]->show();
        } else {
            m_labels[i]->hide();
        }
    }
}

void TransientCommandHistory::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 30, 210));
    //QLabel::paintEvent(event);
}

} // namespace ui
} // namespace aicad
