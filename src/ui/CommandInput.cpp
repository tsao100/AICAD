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

    m_completer->popup()->setStyleSheet(R"(
        QListView::item {
            padding: 4px;
            min-height: 24px;
        }
    )");

    setCompleter(m_completer);

    connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated),
            this, &CommandInput::onCompleterActivated);
}

void CommandInput::setupStyle() {
    setStyleSheet(R"(
        QLineEdit {
            font-family: "Consolas", "Courier New", "Monaco", monospace;
            font-size: 10pt;
            padding: 4px 8px;
        }
        QLineEdit:focus {
            border: 2px solid palette(highlight);
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
    // ✅ 修正：先嘗試處理特殊鍵，如果處理了就返回，否則傳遞給基類
    if (handleSpecialKeys(event)) {
        // 特殊鍵已處理，不傳遞給基類
        event->accept();
        return;
    }

    // ✅ 普通按鍵：傳遞給 QLineEdit 處理（允許文字輸入）
    QLineEdit::keyPressEvent(event);
}

bool CommandInput::handleSpecialKeys(QKeyEvent* event) {
    // ✅ 返回 true 表示已處理，false 表示未處理

    switch (event->key()) {
    case Qt::Key_Up:
        if (!m_completer->popup()->isVisible()) {
            navigateHistory(-1);
            return true;  // ✅ 已處理
        }
        break;

    case Qt::Key_Down:
        if (!m_completer->popup()->isVisible()) {
            navigateHistory(+1);
            return true;  // ✅ 已處理
        }
        break;

    case Qt::Key_F2:
        emit f2Pressed();
        return true;  // ✅ 已處理
        break;

    case Qt::Key_Tab:
        if (m_completer && m_completer->popup()->isVisible()) {
            // 接受當前補全項
            if (m_completer->currentCompletion().isEmpty()) {
                m_completer->setCurrentRow(0);
            }
            setText(m_completer->currentCompletion());
            return true;  // ✅ 已處理
        }
        break;

    case Qt::Key_Escape:
        clear();
        hideAutoComplete();
        emit escapePressed();
        emit commandCancelled();
        return true;  // ✅ 已處理
        break;

    case Qt::Key_Space:
        // Space 重複上一個命令（在空輸入時）
        if (text().isEmpty() && event->modifiers() == Qt::NoModifier) {
            if (!m_commandHistory.isEmpty()) {
                setText(m_commandHistory.first());
                onReturnPressed();
                return true;  // ✅ 已處理
            }
        }
        // ✅ 如果不是空輸入，讓 Space 正常輸入空格
        break;

    default:
        // 不是特殊鍵，返回 false
        break;
    }

    return false;  // ✅ 未處理，讓基類處理
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
