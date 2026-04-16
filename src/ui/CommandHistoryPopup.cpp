#include "CommandHistoryPopup.h"
#include <QVBoxLayout>
#include <QScrollBar>

namespace aicad {
namespace ui {

CommandHistoryPopup::CommandHistoryPopup(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    buildToolbar();
    lay->addWidget(m_toolbar);

    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    // ReadOnly 模式下 Qt 預設允許鍵盤選取與 Ctrl+C，滑鼠選取亦可
    // 額外設定讓選取更明顯
    m_textEdit->setTextInteractionFlags(
        Qt::TextSelectableByMouse |
        Qt::TextSelectableByKeyboard);
    m_textEdit->setStyleSheet(
        "QTextEdit { background:#1e1e1e; color:#e0e0e0; "
        "font-family:monospace; font-size:12px; "
        "selection-background-color:#264f78; }");
    lay->addWidget(m_textEdit);
    setMinimumHeight(0);

    hide();
}

void CommandHistoryPopup::buildToolbar() {
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(14, 14));
    m_toolbar->setStyleSheet(
        "QToolBar { background:#2d2d2d; border-bottom:1px solid #444; "
        "spacing:2px; padding:1px; }");

    auto* actCopySelected = m_toolbar->addAction(tr("複製選取"));
    auto* actCopyAll      = m_toolbar->addAction(tr("複製全部"));

    m_toolbar->addSeparator();

    auto* actClear = m_toolbar->addAction(tr("清除"));

    connect(actCopySelected, &QAction::triggered, this, &CommandHistoryPopup::copySelected);
    connect(actCopyAll,      &QAction::triggered, this, &CommandHistoryPopup::copyAll);
    connect(actClear,        &QAction::triggered, this, &CommandHistoryPopup::clearContent);
}

void CommandHistoryPopup::appendLine(const QString& text, bool isPrompt) {
    const QString color = isPrompt ? QStringLiteral("#888888")
                                   : QStringLiteral("#e0e0e0");
    m_textEdit->append(
        QStringLiteral("<span style='color:%1;'>%2</span>")
            .arg(color, text.toHtmlEscaped()));

    // 自動捲到最新一行
    auto* sb = m_textEdit->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void CommandHistoryPopup::clearContent() {
    m_textEdit->clear();
}

void CommandHistoryPopup::copySelected() {
    // QTextEdit::copy() 已處理無選取時不動作
    m_textEdit->copy();
}

void CommandHistoryPopup::copyAll() {
    m_textEdit->selectAll();
    m_textEdit->copy();
    // 複製後取消選取（視覺還原）
    QTextCursor c = m_textEdit->textCursor();
    c.clearSelection();
    m_textEdit->setTextCursor(c);
}

void CommandHistoryPopup::toggle(QWidget* anchor) {
    if (isVisible()) {
        slideOut();
    } else {
        slideIn(anchor);
    }
}

void CommandHistoryPopup::slideIn(QWidget* anchor) {
    m_anchor = anchor;
    reposition();

    if (!m_anim) {
        m_anim = new QPropertyAnimation(this, "slideHeight");
        m_anim->setDuration(180);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
    }

    // ← 關鍵：先斷開 slideOut 留下的 finished→hide 連接
    disconnect(m_anim, &QPropertyAnimation::finished,
               this,   &QWidget::hide);

    m_anim->stop();
    m_anim->setStartValue(m_slideH);
    m_anim->setEndValue(m_targetH);
    show();
    raise();
    m_anim->start();
}

void CommandHistoryPopup::slideOut() {
    if (!m_anim) return;

    m_anim->stop();
    m_anim->setStartValue(m_slideH);
    m_anim->setEndValue(0);

    // UniqueConnection 確保不重複，但 slideIn 前會 disconnect 掉
    connect(m_anim, &QPropertyAnimation::finished,
            this,   &QWidget::hide,
            Qt::UniqueConnection);

    m_anim->start();
}

void CommandHistoryPopup::setSlideHeight(int h) {
    m_slideH = h;       // ← 新增：記錄邏輯高度
    if (!m_anchor) return;

    const QPoint ag = m_anchor->mapToGlobal(QPoint(0, 0));

    // 保持 widget 實際大小不變，位置固定在 anchor 上方 m_targetH 處
    // ← 改：不再 setGeometry；只做 move + 確保 size 正確
    if (width() != m_anchor->width() || height() != m_targetH)
        resize(m_anchor->width(), m_targetH);
    move(ag.x(), ag.y() - m_targetH);

    // 用 mask 控制可見區域（底部 h 像素），完全繞開最小高度限制
    if (h <= 0) {
        setMask(QRegion());                                     // 全遮（看不見）
    } else if (h >= m_targetH) {
        clearMask();                                            // 全顯
    } else {
        setMask(QRegion(0, m_targetH - h, m_anchor->width(), h)); // 底部 h px
    }
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
