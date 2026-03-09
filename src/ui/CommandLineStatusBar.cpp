#include "CommandLineStatusBar.h"

namespace aicad {
namespace ui {

CommandLineStatusBar::CommandLineStatusBar(QWidget* parent)
    : QWidget(parent)
    , m_currentState(CommandLineState::Ready)
{
    setupUI();
}

void CommandLineStatusBar::setupUI() {
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(6, 2, 6, 2);
    m_layout->setSpacing(8);

    QLabel* lastCmdTitle = new QLabel("Last:", this);
    lastCmdTitle->setStyleSheet("color: #CCCCCC;");

    m_lastCommandLabel = new QLabel("-", this);
    m_lastCommandLabel->setStyleSheet("font-weight: bold; color: white;");

    QLabel* separator1 = new QLabel("|", this);
    separator1->setStyleSheet("color: #666666;");

    QLabel* modeTitle = new QLabel("Mode:", this);
    modeTitle->setStyleSheet("color: #CCCCCC;");

    m_inputModeLabel = new QLabel("Command", this);
    m_inputModeLabel->setStyleSheet("color: white;");

    QLabel* separator2 = new QLabel("|", this);
    separator2->setStyleSheet("color: #666666;");

    m_stateIndicator = new QLabel("●", this);
    m_stateTextLabel = new QLabel("Ready", this);
    m_stateTextLabel->setStyleSheet("color: white;");

    m_layout->addWidget(lastCmdTitle);
    m_layout->addWidget(m_lastCommandLabel);
    m_layout->addWidget(separator1);
    m_layout->addWidget(modeTitle);
    m_layout->addWidget(m_inputModeLabel);
    m_layout->addWidget(separator2);
    m_layout->addWidget(m_stateIndicator);
    m_layout->addWidget(m_stateTextLabel);
    m_layout->addStretch();

    setStyleSheet(R"(
        QWidget {
            background-color: #007ACC;
            font-size: 9pt;
        }
    )");

    setFixedHeight(22);
    updateStateIndicator();
}

void CommandLineStatusBar::setLastCommand(const QString& command) {
    m_lastCommandLabel->setText(command);
}

void CommandLineStatusBar::setInputMode(const QString& mode) {
    m_inputModeLabel->setText(mode);
}

void CommandLineStatusBar::setState(CommandLineState state) {
    m_currentState = state;
    updateStateIndicator();
}

void CommandLineStatusBar::setStateText(const QString& text) {
    m_stateTextLabel->setText(text);
}

void CommandLineStatusBar::updateStateIndicator() {
    QString color;
    QString text;

    switch (m_currentState) {
    case CommandLineState::Ready:
        color = "#00FF00";
        text = "Ready";
        break;
    case CommandLineState::Busy:
        color = "#FFFF00";
        text = "Busy";
        break;
    case CommandLineState::Error:
        color = "#FF0000";
        text = "Error";
        break;
    case CommandLineState::Warning:
        color = "#FFA500";
        text = "Warning";
        break;
    }

    m_stateIndicator->setStyleSheet(QString("color: %1; font-size: 12pt;").arg(color));
    m_stateTextLabel->setText(text);
}

} // namespace ui
} // namespace aicad
