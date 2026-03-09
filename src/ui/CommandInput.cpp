#include "CommandInput.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include <QKeyEvent>
#include <QAbstractItemView>
#include <QDebug>

namespace aicad {
namespace ui {

CommandInput::CommandInput(QWidget* parent)
    : QLineEdit(parent)
    , m_completer(nullptr)
    , m_completerModel(nullptr)
    , m_historyIndex(-1)
    , m_inputMode(InputMode::Command)
    , m_autoCompleteEnabled(true)
{
    setupAutoComplete();
    setupStyle();
    updatePlaceholder();

    connect(this, &QLineEdit::returnPressed,
            this, &CommandInput::onReturnPressed);

    connect(this, &QLineEdit::textChanged,
            this, &CommandInput::onTextChanged);
}

CommandInput::~CommandInput() = default;

void CommandInput::setupAutoComplete() {
    m_completerModel = new QStringListModel(this);

    m_completer = new QCompleter(m_completerModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setMaxVisibleItems(10);
    m_completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);

    // 自訂樣式
    m_completer->popup()->setStyleSheet(R"(
        QListView {
            background-color: #2D2D30;
            color: white;
            border: 1px solid #007ACC;
            selection-background-color: #007ACC;
            outline: none;
        }
        QListView::item {
            padding: 4px;
            min-height: 24px;
        }
        QListView::item:hover {
            background-color: #3F3F46;
        }
    )");

    setCompleter(m_completer);

    connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated),
            this, &CommandInput::onCompleterActivated);
}

void CommandInput::setupStyle() {
    setStyleSheet(R"(
        QLineEdit {
            background-color: #2D2D30;
            color: #00FF00;
            font-family: "Consolas", "Courier New", monospace;
            font-size: 10pt;
            border: 1px solid #007ACC;
            padding: 4px 8px;
            selection-background-color: #264F78;
        }
        QLineEdit:focus {
            border: 2px solid #007ACC;
        }
    )");

    setMinimumHeight(30);
}

void CommandInput::setAutoCompleteModel(QAbstractItemModel* model) {
    if (m_completer) {
        m_completer->setModel(model);
    }
}

void CommandInput::setAutoCompleteEnabled(bool enabled) {
    m_autoCompleteEnabled = enabled;

    if (m_completer) {
        m_completer->setCompletionMode(enabled ?
                                           QCompleter::PopupCompletion : QCompleter::InlineCompletion);
    }
}

void CommandInput::showAutoComplete() {
    if (m_completer && m_autoCompleteEnabled) {
        m_completer->complete();
    }
}

void CommandInput::hideAutoComplete() {
    if (m_completer) {
        m_completer->popup()->hide();
    }
}

void CommandInput::setCommandHistory(const QStringList& history) {
    m_commandHistory = history;
    m_historyIndex = -1;
}

void CommandInput::addToHistory(const QString& command) {
    if (command.isEmpty()) return;

    // 移除重複項
    m_commandHistory.removeAll(command);

    // 添加到最前面
    m_commandHistory.prepend(command);

    // 限制歷史記錄數量
    if (m_commandHistory.size() > MAX_HISTORY) {
        m_commandHistory.removeLast();
    }

    m_historyIndex = -1;
}

void CommandInput::navigateHistory(int direction) {
    if (m_commandHistory.isEmpty()) return;

    m_historyIndex += direction;

    // 邊界檢查
    if (m_historyIndex < 0) {
        m_historyIndex = -1;
        clear();
        return;
    }

    if (m_historyIndex >= m_commandHistory.size()) {
        m_historyIndex = m_commandHistory.size() - 1;
    }

    QString cmd = m_commandHistory[m_historyIndex];
    setText(cmd);
    emit historyNavigated(cmd);
}

void CommandInput::setInputMode(InputMode mode) {
    if (m_inputMode == mode) return;

    m_inputMode = mode;
    updatePlaceholder();

    emit inputModeChanged(mode);
}

void CommandInput::setPromptText(const QString& prompt) {
    if (m_promptText == prompt) return;

    m_promptText = prompt;
    updatePlaceholder();
}

void CommandInput::updatePlaceholder() {
    QString placeholder;

    if (!m_promptText.isEmpty()) {
        placeholder = m_promptText;
    } else {
        switch (m_inputMode) {
        case InputMode::Command:
            placeholder = "Enter command...";
            break;
        case InputMode::Coordinate:
            placeholder = "Specify point (X,Y) or @X,Y or @dist<angle:";
            break;
        case InputMode::Number:
            placeholder = "Enter number:";
            break;
        case InputMode::Option:
            placeholder = "Select option:";
            break;
        case InputMode::String:
            placeholder = "Enter text:";
            break;
        }
    }

    setPlaceholderText(placeholder);
}

void CommandInput::keyPressEvent(QKeyEvent* event) {
    // 處理特殊鍵
    handleSpecialKeys(event);

    // 如果已處理，不傳遞給基類
    if (event->isAccepted()) {
        return;
    }

    QLineEdit::keyPressEvent(event);
}

void CommandInput::handleSpecialKeys(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Up:
        if (!m_completer->popup()->isVisible()) {
            navigateHistory(-1);
            event->accept();
        }
        break;

    case Qt::Key_Down:
        if (!m_completer->popup()->isVisible()) {
            navigateHistory(+1);
            event->accept();
        }
        break;

    case Qt::Key_F2:
        emit f2Pressed();
        event->accept();
        break;

    case Qt::Key_Tab:
        if (m_completer && m_completer->popup()->isVisible()) {
            // 接受當前補全項
            if (m_completer->currentCompletion().isEmpty()) {
                m_completer->setCurrentRow(0);
            }
            setText(m_completer->currentCompletion());
            event->accept();
        }
        break;

    case Qt::Key_Escape:
        clear();
        hideAutoComplete();
        emit escapePressed();
        emit commandCancelled();
        event->accept();
        break;

    case Qt::Key_Space:
        // Space 重複上一個命令（在空輸入時）
        if (text().isEmpty() && event->modifiers() == Qt::NoModifier) {
            if (!m_commandHistory.isEmpty()) {
                setText(m_commandHistory.first());
                onReturnPressed();
                event->accept();
            }
        }
        break;

    default:
        // 不處理，傳遞給基類
        break;
    }
}

void CommandInput::focusInEvent(QFocusEvent* event) {
    QLineEdit::focusInEvent(event);

    // 聚焦時顯示自動完成
    if (m_autoCompleteEnabled && !text().isEmpty()) {
        showAutoComplete();
    }
}

void CommandInput::focusOutEvent(QFocusEvent* event) {
    QLineEdit::focusOutEvent(event);
    hideAutoComplete();
}

void CommandInput::onReturnPressed() {
    QString cmd = text().trimmed();

    if (cmd.isEmpty()) {
        return;
    }

    // 添加到歷史記錄
    addToHistory(cmd);

    // 發送事件
    auto* bus = core::Application::instance()->eventBus();

    bus->publish(core::Events::COMMAND_LOG,
                 QVariant::fromValue("Command: " + cmd));

    bus->publish(core::Events::COMMAND_EXECUTE_REQUEST, cmd);

    emit commandEntered(cmd);

    clear();
    hideAutoComplete();
}

void CommandInput::onTextChanged(const QString& text) {
    if (text.isEmpty()) {
        hideAutoComplete();
        return;
    }

    // 觸發自動完成
    if (m_autoCompleteEnabled && m_inputMode == InputMode::Command) {
        emit autoCompleteRequested(text);

        // 如果有匹配項，顯示
        if (m_completer && m_completer->completionCount() > 0) {
            showAutoComplete();
        }
    }
}

void CommandInput::onCompleterActivated(const QString& text) {
    setText(text);
    // 自動執行命令（可選）
    // onReturnPressed();
}

} // namespace ui
} // namespace aicad
