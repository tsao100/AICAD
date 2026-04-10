#include "CommandInputEdit.h"
#include <QKeyEvent>

namespace aicad {
namespace ui {

CommandInputEdit::CommandInputEdit(QWidget* parent)
    : QLineEdit(parent)
{
    setPlaceholderText(tr("輸入指令或 LISP..."));
}

void CommandInputEdit::setHistory(const QStringList& history) {
    m_history = history;
    m_historyIndex = -1;
}

void CommandInputEdit::addToHistory(const QString& cmd) {
    if (cmd.isEmpty()) return;
    m_history.removeAll(cmd);
    m_history.prepend(cmd);
    m_historyIndex = -1;
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

} // namespace ui
} // namespace aicad
