#include "TransientCommandHistory.h"
#include <QPainter>
#include <QFontMetrics>

namespace aicad {
namespace ui {

TransientCommandHistory::TransientCommandHistory(QWidget* anchor, QWidget* cadView)
    : QObject(anchor)   // anchor 銷毀時自動清理
    , m_anchor(anchor)
    , m_cadView(cadView)
{
    setMaxLines(m_maxLines);   // 建立初始 labels

    m_fadeTimer = new QTimer(this);
    m_fadeTimer->setSingleShot(true);
    connect(m_fadeTimer, &QTimer::timeout,
            this, &TransientCommandHistory::beginFadeOut);
}

TransientCommandHistory::~TransientCommandHistory() {
    for (auto* lbl : m_labels)
        delete lbl;
}

void TransientCommandHistory::setMaxLines(int n) {
    n = qMax(1, n);
    if (n == m_maxLines && !m_labels.isEmpty()) { m_maxLines = n; return; }
    m_maxLines = n;

    // 刪多餘
    while (m_labels.size() > n) {
        delete m_labels.takeLast();
        delete m_effects.takeLast();
    }

    // 補不足（parent = cadView）
    while (m_labels.size() < n) {
        auto* lbl = new QLabel(m_cadView);
        lbl->setContentsMargins(SIDE_PAD, 0, SIDE_PAD, 0);
        lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        lbl->setAttribute(Qt::WA_TransparentForMouseEvents);

        // 半透明背景用 stylesheet
        lbl->setStyleSheet(
            "QLabel { background: rgba(30,30,30,210); color: white; }");
        lbl->hide();

        auto* effect = new QGraphicsOpacityEffect(lbl);
        effect->setOpacity(1.0);
        lbl->setGraphicsEffect(effect);

        m_labels.append(lbl);
        m_effects.append(effect);
    }

    // 裁剪超出行數的資料
    while (m_lines.size() > m_maxLines) {
        m_lines.removeFirst();
        m_isPrompt.removeFirst();
    }
}

void TransientCommandHistory::addLine(const QString& text, bool isPrompt) {
    if (m_fadeAnim) m_fadeAnim->stop();

    // 全部 effect 恢復不透明
    for (auto* e : m_effects)
        e->setOpacity(1.0);

    m_lines.append(text);
    m_isPrompt.append(isPrompt);
    while (m_lines.size() > m_maxLines) {
        m_lines.removeFirst();
        m_isPrompt.removeFirst();
    }

    repositionLabels();
    m_fadeTimer->start(2000);
}

void TransientCommandHistory::repositionLabels() {
    if (!m_anchor || !m_cadView) return;

    // anchor 在 cadView 中的本地座標
    const QPoint anchorPos = m_anchor->mapTo(m_cadView, QPoint(0, 0));
    const int    anchorTop = anchorPos.y();
    const int    anchorX   = anchorPos.x();
    const int    anchorW   = m_anchor->width();

    const int n = m_lines.size();

    for (int i = 0; i < m_labels.size(); ++i) {
        if (i < n) {
            // 最新一行在最下方（緊貼 anchor 上緣），往上排
            const int row = n - 1 - i;   // row=0 最靠近 anchor
            const int y   = anchorTop - GAP_BELOW - LINE_H * (row + 1);

            m_labels[i]->setGeometry(anchorX, y, anchorW, LINE_H);

            // 文字顏色
            const QString color = m_isPrompt[i] ? "#aaaaaa" : "#eeeeee";
            m_labels[i]->setStyleSheet(
                QString("QLabel { background: rgba(30,30,30,210); color: %1; }")
                    .arg(color));
            m_labels[i]->setText(m_lines[i]);
            m_labels[i]->raise();
            m_labels[i]->show();
        } else {
            m_labels[i]->hide();
        }
    }
}

void TransientCommandHistory::updatePosition() {
    if (m_lines.isEmpty()) return;
    repositionLabels();
}

void TransientCommandHistory::beginFadeOut() {
    if (m_lines.isEmpty()) return;

    // 用第一個 effect 做主動畫，其餘跟進
    if (!m_fadeAnim) {
        m_fadeAnim = new QPropertyAnimation(this);
        m_fadeAnim->setDuration(800);
        m_fadeAnim->setEasingCurve(QEasingCurve::InQuad);
        connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
            for (auto* lbl : m_labels)
                lbl->hide();
            m_lines.clear();
            m_isPrompt.clear();
        });
        connect(m_fadeAnim, &QPropertyAnimation::valueChanged,
                this, [this](const QVariant& v) {
                    const qreal op = v.toReal();
                    for (auto* e : m_effects)
                        e->setOpacity(op);
                });
    }

    m_fadeAnim->setStartValue(1.0);
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->start();
}

void TransientCommandHistory::rebuildLabels() {
    //    const int lineH = fontMetrics().height() + LINE_PADDING;
        const int lineH = 10 + LINE_PADDING;
    const int n     = m_lines.size();
    //    const int w     = width() > 0 ? width() : 300;
        const int w     = 300;

//    resize(w, n * lineH);

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
} // namespace ui
} // namespace aicad
