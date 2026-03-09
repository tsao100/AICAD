#include "CommandLineOptionsPanel.h"
#include <QEvent>
#include <QVariant>
#include <QStyle>

namespace aicad {
namespace ui {

CommandLineOptionsPanel::CommandLineOptionsPanel(QWidget* parent)
    : QWidget(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(6);
    m_layout->addStretch();

    setStyleSheet(R"(
        QWidget {
            background-color: rgba(30, 30, 30, 200);
        }
        QPushButton {
            background-color: #3F3F46;
            color: white;
            border: 1px solid #007ACC;
            border-radius: 3px;
            padding: 6px 14px;
            font-size: 9pt;
            min-width: 60px;
        }
        QPushButton:hover {
            background-color: #007ACC;
        }
        QPushButton:pressed {
            background-color: #005A9E;
        }
        QPushButton[isDefault="true"] {
            border: 2px solid #00FF00;
            font-weight: bold;
        }
    )");

    setVisible(false);
    setFixedHeight(40);
}

void CommandLineOptionsPanel::setOptions(const QList<CommandOption>& options) {
    clearButtons();

    for (const CommandOption& option : options) {
        createOptionButton(option);
    }

    setVisible(!options.isEmpty());
}

void CommandLineOptionsPanel::clearOptions() {
    clearButtons();
    setVisible(false);
}

void CommandLineOptionsPanel::createOptionButton(const CommandOption& option) {
    QPushButton* btn = new QPushButton(this);

    QString btnText;
    if (!option.shortcut.isEmpty()) {
        btnText = QString("[%1] %2").arg(option.shortcut).arg(option.label);
    } else {
        btnText = option.label;
    }

    btn->setText(btnText);
    btn->setToolTip(option.description.isEmpty() ? option.label : option.description);

    if (option.isDefault) {
        btn->setProperty("isDefault", true);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
        m_currentDefault = option.key;
    }

    connect(btn, &QPushButton::clicked, this, [this, option]() {
        emit optionSelected(option.key);
    });

    btn->installEventFilter(this);

    m_buttons[option.key] = btn;
    m_layout->insertWidget(m_layout->count() - 1, btn);
}

void CommandLineOptionsPanel::clearButtons() {
    for (QPushButton* btn : m_buttons) {
        m_layout->removeWidget(btn);
        btn->deleteLater();
    }
    m_buttons.clear();
    m_currentDefault.clear();
}

void CommandLineOptionsPanel::highlightOption(const QString& key) {
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
        it.value()->setProperty("isDefault", false);
        it.value()->style()->unpolish(it.value());
        it.value()->style()->polish(it.value());
    }

    if (m_buttons.contains(key)) {
        m_buttons[key]->setProperty("isDefault", true);
        m_buttons[key]->style()->unpolish(m_buttons[key]);
        m_buttons[key]->style()->polish(m_buttons[key]);
        m_currentDefault = key;
    }
}

void CommandLineOptionsPanel::setDefaultOption(const QString& key) {
    highlightOption(key);
}

bool CommandLineOptionsPanel::eventFilter(QObject* obj, QEvent* event) {
    QPushButton* btn = qobject_cast<QPushButton*>(obj);
    if (!btn) return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::Enter) {
        QString key = m_buttons.key(btn);
        QString desc = btn->toolTip();
        emit optionHovered(key, desc);
    }

    return QWidget::eventFilter(obj, event);
}

} // namespace ui
} // namespace aicad
