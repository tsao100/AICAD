#ifndef COMMANDINPUT_H
#define COMMANDINPUT_H

#include <QLineEdit>
#include <QCompleter>
#include <QStringListModel>
#include <QValidator>

namespace aicad {
namespace ui {

class CommandInput : public QLineEdit {
    Q_OBJECT

public:
    enum class InputMode {
        Command,       // 命令模式
        Coordinate,    // 座標輸入
        Number,        // 數值輸入
        Option,        // 選項輸入
        String         // 字串輸入
    };

    explicit CommandInput(QWidget* parent = nullptr);
    ~CommandInput() override;

    // 自動完成
    void setAutoCompleteModel(QAbstractItemModel* model);
    void setAutoCompleteEnabled(bool enabled);
    bool isAutoCompleteEnabled() const { return m_autoCompleteEnabled; }

    void showAutoComplete();
    void hideAutoComplete();

    // 歷史記錄
    void setCommandHistory(const QStringList& history);
    QStringList commandHistory() const { return m_commandHistory; }
    void addToHistory(const QString& command);
    void navigateHistory(int direction);

    // 輸入模式
    void setInputMode(InputMode mode);
    InputMode inputMode() const { return m_inputMode; }

    // 提示文字
    void setPromptText(const QString& prompt);
    QString promptText() const { return m_promptText; }

signals:
    void commandEntered(const QString& command);
    void commandCancelled();
    void autoCompleteRequested(const QString& prefix);
    void historyNavigated(const QString& command);
    void inputModeChanged(InputMode mode);
    void escapePressed();
    void f2Pressed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private slots:
    void onReturnPressed();
    void onTextChanged(const QString& text);
    void onCompleterActivated(const QString& text);

private:
    void setupAutoComplete();
    void setupStyle();
    void handleSpecialKeys(QKeyEvent* event);
    void updatePlaceholder();

    QCompleter* m_completer;
    QStringListModel* m_completerModel;

    QStringList m_commandHistory;
    int m_historyIndex;

    InputMode m_inputMode;
    QString m_promptText;
    bool m_autoCompleteEnabled;

    static constexpr int MAX_HISTORY = 100;
};

} // namespace ui
} // namespace aicad

#endif
