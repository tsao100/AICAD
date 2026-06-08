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

    // 不論 anchor 是否為 cadView 的 child（浮動/嵌入皆適用）
    const QPoint anchorGlobal = m_anchor->mapToGlobal(QPoint(0, 0));
    const QPoint anchorInCad  = m_cadView->mapFromGlobal(anchorGlobal);

    const int anchorTop = anchorInCad.y();
    const int anchorX   = anchorInCad.x();
    const int n         = m_lines.size();

    for (int i = 0; i < m_labels.size(); ++i) {
        if (i < n) {
            const int row = n - 1 - i;   // row=0 最靠近 anchor
            const int y   = anchorTop - GAP_BELOW - (LINE_H + GAP_BELOW) * (row + 1);

            // 依該行文字的實際寬度計算 label 寬，加左右 padding
            const QFontMetrics fm(m_labels[i]->font());
            const int textW = fm.horizontalAdvance(m_lines[i]);
            const int labelW = qMin(textW + SIDE_PAD * 2,
                                    m_anchor->minimumWidth());  // 不小於 anchor 最小寬

            m_labels[i]->setGeometry(anchorX, y, labelW, LINE_H);

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

    if (!m_fadeAnim) {
        // QPropertyAnimation 不能 animate QObject 本身（無 Q_PROPERTY）
        // 改用 QVariantAnimation 直接操作 effects
        auto* anim = new QVariantAnimation(this);
        anim->setDuration(800);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        anim->setEasingCurve(QEasingCurve::InQuad);
        connect(anim, &QVariantAnimation::valueChanged,
                this, [this](const QVariant& v) {
                    const qreal op = v.toReal();
                    for (auto* e : m_effects)
                        e->setOpacity(op);
                });
        connect(anim, &QVariantAnimation::finished,
                this, [this]() {
                    for (auto* lbl : m_labels)
                        lbl->hide();
                    m_lines.clear();
                    m_isPrompt.clear();
                });
        m_fadeAnim = anim;   // m_fadeAnim 型別改為 QVariantAnimation*（見下方 .h 修改）
    }

    m_fadeAnim->setStartValue(1.0);
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->start();
}
} // namespace ui
} // namespace aicad
