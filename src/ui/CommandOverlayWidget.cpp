#include "CommandOverlayWidget.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include <QResizeEvent>
#include <QClipboard>
#include <QScrollBar>
#include <QApplication>
#include <QDebug>

namespace aicad {
namespace ui {

CommandOverlayWidget::CommandOverlayWidget(QWidget* parent)
    : QWidget(parent)
    , m_layoutManager(nullptr)
    , m_isPinned(false)
    , m_commandActive(false)
{
    setAutoFillBackground(true);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, QApplication::palette().color(QPalette::Window));
    setPalette(pal);

    setStyleSheet("background: palette(window);");

    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);

    resize(400, 30);
    setMinimumWidth(300);
    setMaximumWidth(800);

    setupUI();
    setupAreas();
    setupConnections();
    connectEventBus();

    // 預設為緊湊模式
    m_layoutManager->setLayoutMode(LayoutMode::Compact);

    // 事件過濾器
    if (parent) {
        parent->installEventFilter(this);
    }

    reposition();
    raise();
    show();
}

CommandOverlayWidget::~CommandOverlayWidget() = default;

void CommandOverlayWidget::setupUI() {
    // 主佈局由 CommandLineLayout 管理
    m_layoutManager = new CommandLineLayout(this, this);

    // ✅ 完全清除自定義樣式，使用系統預設
    setStyleSheet("");

    // ✅ 確保使用系統調色板
    setAutoFillBackground(false);  // 不自動填充背景

    // 設定整體樣式
    // setStyleSheet(R"(
    //     QWidget {
    //         background-color: rgba(30, 30, 30, 240);
    //         border-radius: 4px;
    //     }
    // )");
}

void CommandOverlayWidget::setupAreas() {
    // 1. 工具列
    m_toolbarWidget = new CommandLineToolbar(this);
    // 使用系統 palette
    m_toolbarWidget->setPalette(QApplication::palette());
    m_toolbarWidget->setAutoFillBackground(true);

    // ===== 美化樣式 =====
    m_toolbarWidget->setStyleSheet(R"(
        QToolBar {
            background: palette(window);
            border: 1px solid palette(mid);
            border-radius: 6px;
            padding: 4px;
        }

        QToolButton {
            padding: 4px;
        }

        QToolButton:hover {
            background: palette(light);
        }
    )");

    m_layoutManager->addArea(CommandLineArea::Toolbar, m_toolbarWidget);

    // 2. 歷史記錄
    m_historyWidget = new QTextEdit(this);
    m_historyWidget->setReadOnly(true);
    m_historyWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_historyWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // m_historyWidget->setStyleSheet(R"(
    //     QTextEdit {
    //         background-color: rgba(30, 30, 30, 200);
    //         color: white;
    //         border: none;
    //         font-family: "Consolas", "Courier New", monospace;
    //         font-size: 9pt;
    //         padding: 4px;
    //     }
    //     QScrollBar:vertical {
    //         background: #2D2D30;
    //         width: 12px;
    //         border-radius: 6px;
    //     }
    //     QScrollBar::handle:vertical {
    //         background: #686868;
    //         border-radius: 6px;
    //         min-height: 20px;
    //     }
    //     QScrollBar::handle:vertical:hover {
    //         background: #9E9E9E;
    //     }
    // )");

    // ✅ 改用 Qt 預設 + 等寬字體
    m_historyWidget->setStyleSheet(R"(
        QTextEdit {
            font-family: "Consolas", "Courier New", "Monaco", monospace;
            font-size: 9pt;
            padding: 4px;
        }
    )");

    m_layoutManager->addArea(CommandLineArea::History, m_historyWidget);

    // 3. 選項面板
    m_optionsWidget = new CommandLineOptionsPanel(this);
    m_layoutManager->addArea(CommandLineArea::Options, m_optionsWidget);

    // 4. 輸入框
    m_inputWidget = new CommandInput(this);
    m_layoutManager->addArea(CommandLineArea::Input, m_inputWidget);

    // 5. 狀態列
    // m_statusBarWidget = new CommandLineStatusBar(this);
    // m_layoutManager->addArea(CommandLineArea::StatusBar, m_statusBarWidget);
}

void CommandOverlayWidget::setupConnections() {
    // 輸入框信號
    connect(m_inputWidget, &CommandInput::commandEntered,
            this, &CommandOverlayWidget::onCommandEntered);

    connect(m_inputWidget, &CommandInput::f2Pressed,
            this, &CommandOverlayWidget::onF2Pressed);

    connect(m_inputWidget, &CommandInput::escapePressed,
            this, &CommandOverlayWidget::onEscapePressed);

    // 工具列信號
    connect(m_toolbarWidget, &CommandLineToolbar::pinToggled,
            this, &CommandOverlayWidget::onPinToggled);

    connect(m_toolbarWidget, &CommandLineToolbar::copyRequested,
            this, &CommandOverlayWidget::onCopyRequested);

    connect(m_toolbarWidget, &CommandLineToolbar::clearRequested,
            this, &CommandOverlayWidget::onClearRequested);

    connect(m_toolbarWidget, &CommandLineToolbar::expandToggled,
            this, &CommandOverlayWidget::onExpandToggled);

    // 選項面板信號
    connect(m_optionsWidget, &CommandLineOptionsPanel::optionSelected,
            this, &CommandOverlayWidget::onOptionSelected);

    // 佈局管理器信號
    connect(m_layoutManager, &CommandLineLayout::layoutModeChanged,
            this, [this](LayoutMode mode) {
                updateSize();
                emit layoutModeChanged(mode);
            });
}

void CommandOverlayWidget::connectEventBus() {
    auto* bus = core::Application::instance()->eventBus();

    // 命令提示
    bus->subscribe(core::Events::COMMAND_PROMPT, this,
                   [this](const QVariant& v) {
                       QString prompt = v.toString();
                       setPrompt(prompt);
                   });

    // 命令執行完成
    bus->subscribe(core::Events::COMMAND_EXECUTED, this,
                   [this](const QVariant& v) {
                       QString result = v.toString();
                       if (!result.isEmpty()) {
                           appendHistory(result, "#00FF00");
                       }
                       m_commandActive = false;
                       clearPrompt();
                   });

    // 命令失敗
    bus->subscribe(core::Events::COMMAND_FAILED, this,
                   [this](const QVariant& v) {
                       QString error = v.toString();
                       appendHistory("Error: " + error, "#FF0000");
                       m_commandActive = false;
                       setState(CommandLineState::Error);
                   });

    // 命令取消
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
                   [this](const QVariant&) {
                       appendHistory("Command cancelled", "#FFAA00");
                       m_commandActive = false;
                       clearPrompt();
                   });
}

void CommandOverlayWidget::setLayoutMode(LayoutMode mode) {
    m_layoutManager->setLayoutMode(mode);
}

LayoutMode CommandOverlayWidget::layoutMode() const {
    return m_layoutManager->layoutMode();
}

void CommandOverlayWidget::appendHistory(const QString& text, const QString& color) {
    if (text.isEmpty()) return;

    // ✅ 如果 color 為預設值，使用系統文字顏色
    QString actualColor = color;
    if (color == "white") {
        // 使用調色板的文字顏色
        actualColor = palette().color(QPalette::Text).name();
    }

    QString html = QString("<span style='color:%1'>%2</span>")
                       .arg(color)
                       .arg(text.toHtmlEscaped());

    m_historyWidget->append(html);

    // 自動滾動到底部
    m_historyWidget->verticalScrollBar()->setValue(
        m_historyWidget->verticalScrollBar()->maximum()
    );
}

void CommandOverlayWidget::clearHistory() {
    m_historyWidget->clear();
}

void CommandOverlayWidget::showOptions(const QList<CommandOption>& options) {
    m_optionsWidget->setOptions(options);

    if (!options.isEmpty()) {
        m_layoutManager->setAreaVisible(CommandLineArea::Options, true);
    }
}

void CommandOverlayWidget::clearOptions() {
    m_optionsWidget->clearOptions();
    m_layoutManager->setAreaVisible(CommandLineArea::Options, false);
}

void CommandOverlayWidget::setPrompt(const QString& prompt) {
    if (prompt.isEmpty()) return;

    m_activePrompt = prompt;
    m_commandActive = true;

    // 在歷史記錄中顯示提示
    appendHistory(prompt, "#00AAFF");

    // 更新輸入框提示
    m_inputWidget->setPromptText(prompt);

    // 如果是緊湊模式，自動展開到標準模式
    if (layoutMode() == LayoutMode::Compact) {
        setLayoutMode(LayoutMode::Standard);
    }
}

void CommandOverlayWidget::clearPrompt() {
    m_activePrompt.clear();
    m_inputWidget->setPromptText("");
}

void CommandOverlayWidget::setLastCommand(const QString& cmd) {
    m_statusBarWidget->setLastCommand(cmd);
}

void CommandOverlayWidget::setInputMode(const QString& mode) {
    m_statusBarWidget->setInputMode(mode);
}

void CommandOverlayWidget::setState(CommandLineState state) {
    m_statusBarWidget->setState(state);
}

void CommandOverlayWidget::setHistoryLineCount(int lines) {
    m_layoutManager->setHistoryLineCount(lines);
    updateSize();
}

int CommandOverlayWidget::historyLineCount() const {
    return m_layoutManager->historyLineCount();
}

void CommandOverlayWidget::onCommandEntered(const QString& command) {
    // 在歷史記錄中顯示命令
    appendHistory("Command: " + command, "#FFFFFF");

    // 更新狀態列
    setLastCommand(command);
    setState(CommandLineState::Busy);

    emit commandEntered(command);
}

void CommandOverlayWidget::onF2Pressed() {
    m_layoutManager->expandHistory();
}

void CommandOverlayWidget::onEscapePressed() {
    clearOptions();
    clearPrompt();

    if (layoutMode() != LayoutMode::Compact) {
        setLayoutMode(LayoutMode::Compact);
    }
}

void CommandOverlayWidget::onOptionSelected(const QString& key) {
    // 將選項作為命令輸入
    m_inputWidget->setText(key);
    m_inputWidget->setFocus();

    emit optionSelected(key);
}

void CommandOverlayWidget::onPinToggled(bool pinned) {
    m_isPinned = pinned;

    if (pinned) {
        // 釘選模式：固定位置
        setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    } else {
        // 非釘選模式：跟隨父視窗
        setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
    }

    show();
}

void CommandOverlayWidget::onCopyRequested() {
    QString text = m_historyWidget->toPlainText();
    QApplication::clipboard()->setText(text);

    appendHistory("History copied to clipboard", "#00FF00");
}

void CommandOverlayWidget::onClearRequested() {
    clearHistory();
    appendHistory("History cleared", "#FFAA00");
}

void CommandOverlayWidget::onExpandToggled(bool expanded) {
    if (expanded) {
        if (layoutMode() == LayoutMode::Compact) {
            setLayoutMode(LayoutMode::Standard);
        }
    } else {
        setLayoutMode(LayoutMode::Compact);
    }
}

bool CommandOverlayWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        if (!m_isPinned) {
            reposition();
        }
    }

    return QWidget::eventFilter(obj, event);
}

void CommandOverlayWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    if (!m_isPinned) {
        reposition();
    }
}

void CommandOverlayWidget::reposition() {
    if (!parentWidget()) return;

    QWidget* view = parentWidget();
    const int marginBottom = 8;

    int totalHeight = m_layoutManager->totalHeight();

    int x = (view->width() - width()) / 2;
    int y = view->height() - totalHeight - marginBottom;

    move(x, y);
}

void CommandOverlayWidget::updateSize() {
    int totalHeight = m_layoutManager->totalHeight();
    setFixedHeight(totalHeight);

    reposition();
}

} // namespace ui
} // namespace aicad
