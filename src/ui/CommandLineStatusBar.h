#ifndef COMMANDLINESTATUSBAR_H
#define COMMANDLINESTATUSBAR_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>

namespace aicad {
namespace ui {

enum class CommandLineState {
    Ready,
    Busy,
    Error,
    Warning
};

class CommandLineStatusBar : public QWidget {
    Q_OBJECT

public:
    explicit CommandLineStatusBar(QWidget* parent = nullptr);

    void setLastCommand(const QString& command);
    void setInputMode(const QString& mode);
    void setState(CommandLineState state);
    void setStateText(const QString& text);

private:
    void setupUI();
    void updateStateIndicator();

    QHBoxLayout* m_layout;
    QLabel* m_lastCommandLabel;
    QLabel* m_inputModeLabel;
    QLabel* m_stateIndicator;
    QLabel* m_stateTextLabel;

    CommandLineState m_currentState;
};

} // namespace ui
} // namespace aicad

#endif
