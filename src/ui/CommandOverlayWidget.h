#ifndef COMMANDOVERLAYWIDGET_H
#define COMMANDOVERLAYWIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include "CommandInput.h"
#include "CommandLineLayout.h"
#include "CommandLineToolbar.h"
#include "CommandLineOptionsPanel.h"
#include "CommandLineStatusBar.h"

namespace aicad {
namespace ui {

class CommandOverlayWidget : public QWidget {
    Q_OBJECT

public:
    explicit CommandOverlayWidget(QWidget* parent = nullptr);
    ~CommandOverlayWidget() override;

    // 佈局模式
    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const;

    // 區域訪問
    CommandInput* inputWidget() const { return m_inputWidget; }
    QTextEdit* historyWidget() const { return m_historyWidget; }
    CommandLineToolbar* toolbar() const { return m_toolbarWidget; }
    CommandLineOptionsPanel* optionsPanel() const { return m_optionsWidget; }
    CommandLineStatusBar* statusBar() const { return m_statusBarWidget; }

    // 歷史記錄
    void appendHistory(const QString& text, const QString& color = "white");
    void clearHistory();

    // 選項
    void showOptions(const QList<CommandOption>& options);
    void clearOptions();

    // 提示
    void setPrompt(const QString& prompt);
    void clearPrompt();

    // 狀態
    void setLastCommand(const QString& cmd);
    void setInputMode(const QString& mode);
    void setState(CommandLineState state);

    // 大小調整
    void setHistoryLineCount(int lines);
    int historyLineCount() const;

signals:
    void layoutModeChanged(LayoutMode mode);
    void commandEntered(const QString& command);
    void optionSelected(const QString& option);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onCommandEntered(const QString& command);
    void onF2Pressed();
    void onEscapePressed();
    void onOptionSelected(const QString& key);

    // 工具列槽
    void onPinToggled(bool pinned);
    void onCopyRequested();
    void onClearRequested();
    void onExpandToggled(bool expanded);

private:
    void setupUI();
    void setupAreas();
    void setupConnections();
    void connectEventBus();

    void reposition();
    void updateSize();

    // 區域組件
    CommandLineToolbar* m_toolbarWidget;
    QTextEdit* m_historyWidget;
    CommandLineOptionsPanel* m_optionsWidget;
    CommandInput* m_inputWidget;
    CommandLineStatusBar* m_statusBarWidget;

    // 佈局管理
    CommandLineLayout* m_layoutManager;

    // 狀態
    bool m_isPinned;
    bool m_commandActive;
    QString m_activePrompt;
};

} // namespace ui
} // namespace aicad

#endif
