#include "CommandHistoryPopup.h"
#include <QVBoxLayout>
#include <QScrollBar>

namespace aicad {
namespace ui {

CommandHistoryPopup::CommandHistoryPopup(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);

    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    lay->addWidget(m_textEdit);

    hide();
}

void CommandHistoryPopup::slideIn(QWidget* anchor) {
    m_anchor = anchor;
    reposition();
    setSlideHeight(0);
    show();
    raise();

    if (!m_anim) {
        m_anim = new QPropertyAnimation(this, "slideHeight");
        m_anim->setDuration(180);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
    }
    m_anim->setStartValue(0);
    m_anim->setEndValue(m_targetH);
    m_anim->start();
}

void CommandHistoryPopup::slideOut() {
    if (!m_anim) return;
    m_anim->setStartValue(height());
    m_anim->setEndValue(0);
    connect(m_anim, &QPropertyAnimation::finished, this, &QWidget::hide,
            Qt::UniqueConnection);
    m_anim->start();
}

void CommandHistoryPopup::setSlideHeight(int h) {
    if (!m_anchor) return;
    QPoint anchorGlobal = m_anchor->mapToGlobal(QPoint(0, 0));
    setGeometry(anchorGlobal.x(),
                anchorGlobal.y() - h,
                m_anchor->width(), h);
}

void CommandHistoryPopup::reposition() {
    if (!m_anchor) return;
    QPoint ag = m_anchor->mapToGlobal(QPoint(0,0));
    move(ag.x(), ag.y() - m_targetH);
    resize(m_anchor->width(), m_targetH);
}

void CommandHistoryPopup::setContent(const QString& html) {
    m_textEdit->setHtml(html);
    auto* sb = m_textEdit->verticalScrollBar();
    sb->setValue(sb->maximum());
}

} // namespace ui
} // namespace aicad
