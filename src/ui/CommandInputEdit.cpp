#include "CommandInputEdit.h"
#include "core/CommandLineManager.h"
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

void CommandInputEdit::setPromptText(const QString& text) {
    m_promptText = text;
    if (m_options.isEmpty())
        rebuildDocument();
}

void CommandInputEdit::clearPromptOptions() {
    m_options.clear();
    m_promptPrefix.clear();
    m_promptText.clear();
    m_promptDoc->clear();
    m_docWidth = 0;
    setTextMargins(0, 0, 0, 0);
    update();
}

void CommandInputEdit::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const QString line = text().trimmed();

        // 只有在「系統閒置（非等待資料輸入）」時才判斷 Lisp 表達式，
        // 等待資料輸入（座標/數值/選項…）維持原本行為不受影響。
        const bool waitingForInput = core::CommandLineManager::instance()->isWaitingForInput();

        if (!waitingForInput) {
            // 尚未進入 Lisp 模式時，若本行以 '(' 開頭，視為 Lisp 表達式的開始。
            if (!m_lispMode && line.startsWith(QLatin1Char('('))) {
                m_lispMode = true;
            }

            if (m_lispMode) {
                const QString combined = m_lispBuffer.isEmpty()
                                              ? line
                                              : m_lispBuffer + QLatin1Char('\n') + line;

                if (!isLispParensBalanced(combined)) {
                    // 左右括弧尚未對應：代表換行，繼續讓使用者輸入 Lisp 表達式，
                    // 不送出、不清除歷程索引以外的狀態。
                    m_lispBuffer = combined;
                    clear();
                    m_historyIndex = -1;
                    updateLispContinuationHint();
                    return;
                }

                // 括弧已對應：視為完整的 Lisp 表達式，正常送出。
                m_lispBuffer.clear();
                m_lispMode = false;
                clear();
                m_historyIndex = -1;
                updateLispContinuationHint();
                emit commandSubmitted(combined);
                return;
            }
        }

        // 非 Lisp 表達式（或正在等待資料輸入）：維持原本行為。
        submitCurrentLine();
        return;
    }
    case Qt::Key_Space: {
        // 若正在輸入左右括弧尚未對應的 Lisp 表達式（跨行累積中），或本行
        // 本身已經以 '(' 開頭但尚未按下 Enter，空白鍵是 Lisp 語法所需的
        // 引數分隔符，不可被視為 Enter，維持一般字元輸入行為。
        if (m_lispMode || text().trimmed().startsWith(QLatin1Char('(')))
            break;

        const bool waitingForInput = core::CommandLineManager::instance()->isWaitingForInput();

        if (waitingForInput) {
            // 等待「選項」輸入時（例如窗選提示的 F/WP/CP 這類單一選項代碼）：
            // Space 視同 Enter，直接送出，不把空白字元插入文字框——這類輸入
            // 本來就不會用到空白字元，維持原本行為只會讓使用者以為沒反應。
            if (core::CommandLineManager::instance()->expectedInputType() == core::InputType::Option) {
                submitCurrentLine();
                return;
            }

            // 其餘等待資料輸入的型別（例如字串型別可能包含空白字元），維持
            // 原本行為：僅輸入為空時，空白鍵才等同 Enter（送出空字串套用
            // 預設值）；有內容時讓 QLineEdit 正常插入空白字元，避免破壞
            // 多字詞輸入。
            if (text().trimmed().isEmpty()) {
                emit commandSubmitted(QString());
                return;
            }
            break;
        }

        // 系統閒置：空白鍵一律等同 Enter——不論輸入框是否已有內容，
        // 例如輸入 "l" 後按空白鍵，效果等同輸入 "l" 後按 Enter。
        submitCurrentLine();
        return;
    }
    case Qt::Key_Up:
        historyUp();
        return;
    case Qt::Key_Down:
        historyDown();
        return;
    case Qt::Key_Escape:
        clear();
        m_historyIndex = -1;
        m_lispMode = false;
        m_lispBuffer.clear();
        updateLispContinuationHint();
        emit escapePressed();
        return;
    default:
        break;
    }
    QLineEdit::keyPressEvent(event);
}

// 送出目前輸入框內容，等同「按下 Enter」的最終效果：
// - 輸入框有內容 → 直接送出該內容作為指令/資料。
// - 輸入框為空且系統正在等待資料輸入 → 送出空字串（套用預設值）。
// - 輸入框為空且系統閒置 → 重複上一個指令。
// 供 Enter 與（非 Lisp、非等待資料輸入時的）空白鍵共用。
void CommandInputEdit::submitCurrentLine() {
    const QString t = text().trimmed();
    clear();
    m_historyIndex = -1;
    if (t.isEmpty()) {
        if (core::CommandLineManager::instance()->isWaitingForInput())
            emit commandSubmitted(QString());
        else
            repeatLastCommand();
    } else {
        emit commandSubmitted(t);
    }
}

// 判斷 Lisp 表達式的左右括弧是否已對應（忽略字串常量與行內註解 ";"）。
// 若最終深度 <= 0（含括弧過多的錯誤情況），一律視為「已對應」交給
// Lisp 引擎在送出後回報錯誤，不在命令列這一層卡住使用者輸入。
bool CommandInputEdit::isLispParensBalanced(const QString& text) {
    int  depth    = 0;
    bool inString = false;

    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);

        if (inString) {
            if (c == QLatin1Char('\\')) {
                ++i; // 跳過跳脫字元（例如 \" 或 \\）
                continue;
            }
            if (c == QLatin1Char('"'))
                inString = false;
            continue;
        }

        if (c == QLatin1Char(';')) {
            // Lisp 行內註解，跳到本行結尾（下一個 '\n'）
            const int nl = text.indexOf(QLatin1Char('\n'), i);
            if (nl < 0)
                break;
            i = nl;
            continue;
        }

        if (c == QLatin1Char('"')) {
            inString = true;
        } else if (c == QLatin1Char('(')) {
            ++depth;
        } else if (c == QLatin1Char(')')) {
            --depth;
        }
    }

    return depth <= 0;
}

void CommandInputEdit::updateLispContinuationHint() {
    setPlaceholderText(m_lispMode
                            ? tr("Lisp 括弧未對應，請繼續輸入...")
                            : tr("輸入指令或 LISP..."));
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
    // 組合 HTML
    QString html;
    if (!m_promptPrefix.isEmpty())
        html += QString("<span class='prefix'>%1 [</span>")
                    .arg(m_promptPrefix.toHtmlEscaped());

    if (m_options.isEmpty())
        html += QString("<span class='prefix'>%1</span>")
                    .arg(m_promptText.toHtmlEscaped());


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
    return;
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