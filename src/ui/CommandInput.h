// CommandInput.h
#ifndef COMMANDINPUT_H
#define COMMANDINPUT_H

#include <QLineEdit>
#include <QCompleter>
#include <QStringListModel>

namespace aicad {
namespace ui {

enum class InputMode {
    Command,
    Coordinate,
    Number,
    Option,
    String
};

class CommandInput : public QLineEdit {
    Q_OBJECT

public:
    explicit CommandInput(QWidget* parent = nullptr);
    ~CommandInput() override;

    // 自動完成
    void setAutoCompleteModel(QAbstractItemModel* model);
    void setAutoCompleteEnabled(bool enabled);
    void showAutoComplete();
    void hideAutoComplete();

    // 命令歷史
    void setCommandHistory(const QStringList& history);
    void addToHistory(const QString& command);
    void navigateHistory(int direction);
    QStringList commandHistory() const { return m_commandHistory; }

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
    void inputModeChanged(InputMode mode);
    void historyNavigated(const QString& command);
    void f2Pressed();
    void escapePressed();

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
    void updatePlaceholder();

    // ✅ 修改為返回 bool，表示是否已處理
    bool handleSpecialKeys(QKeyEvent* event);

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
