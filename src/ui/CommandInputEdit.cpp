#include "CommandInputEdit.h"
#include <QKeyEvent>
#include <QPainter>

namespace aicad {
namespace ui {

CommandInputEdit::CommandInputEdit(QWidget* parent)
    : QLineEdit(parent)
{
    setPlaceholderText(tr("輸入指令或 LISP..."));
    setMouseTracking(true);   // 讓 mouseMoveEvent 在不按鍵時也觸發

    m_promptDoc = new QTextDocument(this);
    m_promptDoc->setDefaultStyleSheet(
        "a { color:#7ec8e3; text-decoration:none; }"
        "a.hover { background:#3d5068; border-radius:3px; }"
        "span.prefix { color:#777777; }");
}

void CommandInputEdit::setHistory(const QStringList& history) {
    m_history = history;
    m_historyIndex = -1;
}

void CommandInputEdit::addToHistory(const QString& cmd) {
    if (cmd.isEmpty()) return;
    QString upper = cmd.trimmed().toUpper();
    m_history.removeAll(upper);
    m_history.prepend(upper);
    m_historyIndex = -1;
}

void CommandInputEdit:: setPromptOptions(const QString& prefix,
                                        const QList<command::InputParser::ParsedOption>& options) {
    m_promptPrefix = prefix;
    m_options      = options;
    rebuildDocument();
}

void CommandInputEdit::clearPromptOptions() {
    m_options.clear();
    m_promptPrefix.clear();
    m_promptDoc->clear();
    m_docWidth = 0;
    setTextMargins(0, 0, 0, 0);
    update();
}

void CommandInputEdit::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const QString t = text().trimmed();
        clear();
        m_historyIndex = -1;
        if (t.isEmpty())
            repeatLastCommand();
        else
            emit commandSubmitted(t);
        return;
    }
    case Qt::Key_Space:
        if (text().trimmed().isEmpty()) {
            repeatLastCommand();
            return;
        }
        break;
    case Qt::Key_Up:
        historyUp();
        return;
    case Qt::Key_Down:
        historyDown();
        return;
    case Qt::Key_Escape:
        clear();
        m_historyIndex = -1;
        emit escapePressed();
        return;
    default:
        break;
    }
    QLineEdit::keyPressEvent(event);
}

void CommandInputEdit::historyUp() {
    if (m_history.isEmpty()) return;
    if (m_historyIndex == -1)
        m_savedInput = text();
    m_historyIndex = qMin(m_historyIndex + 1, m_history.size() - 1);
    setText(m_history.at(m_historyIndex));
    selectAll();
}

void CommandInputEdit::historyDown() {
    if (m_historyIndex == -1) return;
    m_historyIndex--;
    if (m_historyIndex < 0) {
        setText(m_savedInput);
        m_historyIndex = -1;
    } else {
        setText(m_history.at(m_historyIndex));
    }
    selectAll();
}

void CommandInputEdit::repeatLastCommand() {
    if (!m_history.isEmpty())
        emit commandSubmitted(m_history.first());
}

void CommandInputEdit::rebuildDocument() {
    if (m_options.isEmpty()) { clearPromptOptions(); return; }

    // 組合 HTML
    QString html;
    if (!m_promptPrefix.isEmpty())
        html += QString("<span class='prefix'>%1 [</span>")
                    .arg(m_promptPrefix.toHtmlEscaped());

    for (const auto& po : m_options) {
        const bool isChinese = !po.label.isEmpty() &&
                               po.label[0].unicode() > 0x2E7F; // CJK range

        QString display;
        if (isChinese) {
            // 中文：label + (U) 格式，括號內快捷鍵用藍色
            display = QString("%1(<span style='color:#7ec8e3;font-weight:bold;'>%2</span>)")
                          .arg(po.label.toHtmlEscaped(),
                               po.shortcut.toHtmlEscaped());
        } else {
            // 英文：首字母藍色
            display = QString("<span style='color:#7ec8e3;font-weight:bold;'>%1</span>%2")
                          .arg(po.label[0].toUpper())
                          .arg(po.label.mid(1).toHtmlEscaped());
        }

        const bool hovered = (("opt:" + po.shortcut) == m_hoveredAnchor);
        html += QString("<a href='opt:%1' class='%2'"
                        " style='background:%3;"
                        " padding:1px 5px;"
                        " border-radius:3px;'>"
                        "%4</a> ")
                    .arg(po.shortcut.toHtmlEscaped(),
                         hovered ? "hover" : "",
                         hovered ? "#3d5068" : "#2d3a4a",
                         display);
    }

    if (!m_promptPrefix.isEmpty())
        html += QString("<span class='prefix'>]: </span>");

    m_promptDoc->setHtml(html);
    m_promptDoc->setTextWidth(-1);          // 先不限寬，取得自然寬
    m_docWidth = int(m_promptDoc->idealWidth()) + 8;
    updateLeftMargin();
    setPlaceholderText({});
    update();
}

void CommandInputEdit::updateLeftMargin() {
    setTextMargins(m_docWidth, 0, 0, 0);
}

QString CommandInputEdit::anchorAtPos(const QPoint& pos) const {
    // chips 繪製在左側 content rect 的 [0, m_docWidth) 區間
    const QRect cr = contentsRect();
    // doc 的繪製原點
    const QPointF docOrigin(cr.left() + 2,
                            cr.top() + (cr.height() - m_promptDoc->size().height()) / 2.0);
    const QPointF docPos = pos - docOrigin;
    return m_promptDoc->documentLayout()->anchorAt(docPos);
}

void CommandInputEdit::paintEvent(QPaintEvent* event) {
    QLineEdit::paintEvent(event);   // 原生邊框 + 游標 + 使用者輸入文字

    if (m_options.isEmpty() || m_docWidth == 0) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect cr = contentsRect();
    const qreal docH = m_promptDoc->size().height();
    const qreal y = cr.top() + (cr.height() - docH) / 2.0;

    p.save();
    p.translate(cr.left() + 2, y);
    m_promptDoc->setTextWidth(m_docWidth);
    m_promptDoc->drawContents(&p);
    p.restore();
}

void CommandInputEdit::mousePressEvent(QMouseEvent* e) {
    const QString anchor = anchorAtPos(e->pos());
    if (!anchor.isEmpty() && anchor.startsWith("opt:")) {
        emit optionChipClicked(anchor.mid(4));  // 去掉 "opt:" 前綴
        return;   // 不傳給 QLineEdit，避免游標跳位
    }
    QLineEdit::mousePressEvent(e);
}

void CommandInputEdit::mouseMoveEvent(QMouseEvent* e) {
    const QString anchor = anchorAtPos(e->pos());
    if (anchor != m_hoveredAnchor) {
        m_hoveredAnchor = anchor;
        setCursor((!anchor.isEmpty() && anchor.startsWith("opt:"))
                      ? Qt::PointingHandCursor
                      : Qt::IBeamCursor);
        rebuildDocument();   // 重繪 hover 色
    }
    QLineEdit::mouseMoveEvent(e);
}

void CommandInputEdit::leaveEvent(QEvent* e) {
    if (!m_hoveredAnchor.isEmpty()) {
        m_hoveredAnchor.clear();
        rebuildDocument();
    }
    QLineEdit::leaveEvent(e);
}

void CommandInputEdit::resizeEvent(QResizeEvent* e) {
    QLineEdit::resizeEvent(e);
    if (!m_options.isEmpty())
        updateLeftMargin();
}

} // namespace ui
} // namespace aicad
