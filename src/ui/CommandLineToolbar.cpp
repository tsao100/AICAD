#include "CommandLineToolbar.h"
#include <QLabel>

namespace aicad {
namespace ui {

CommandLineToolbar::CommandLineToolbar(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void CommandLineToolbar::setupUI() {
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 2, 4, 2);
    m_layout->setSpacing(4);

    createButtons();

    m_layout->addSpacing(8);

    createOptions();

    m_layout->addStretch();

    // setStyleSheet(R"(
    //     QWidget {
    //         background-color: rgba(45, 45, 48, 200);
    //     }
    //     QToolButton {
    //         background: transparent;
    //         border: none;
    //         padding: 4px;
    //         color: white;
    //     }
    //     QToolButton:hover {
    //         background: rgba(255, 255, 255, 30);
    //         border-radius: 3px;
    //     }
    //     QToolButton:pressed {
    //         background: rgba(255, 255, 255, 50);
    //     }
    //     QToolButton:checked {
    //         background: rgba(0, 122, 204, 128);
    //         border-radius: 3px;
    //     }
    //     QCheckBox {
    //         color: white;
    //         spacing: 4px;
    //     }
    //     QCheckBox::indicator {
    //         width: 14px;
    //         height: 14px;
    //     }
    // )");

    // ✅ 完全清除自定義樣式，使用系統預設
    setStyleSheet(R"(
        QToolButton {
            border: none;
            padding: 4px;
        }
        QToolButton:hover {
            background: palette(light);
        }
        QToolButton:pressed {
            background: palette(mid);
        }
        QToolButton:checked {
            background: palette(highlight);
            color: palette(highlighted-text);
        }
    )");

    setFixedHeight(30);
}

void CommandLineToolbar::createButtons() {
    // Pin
    m_pinButton = new QToolButton(this);
    m_pinButton->setText("📌");
    m_pinButton->setToolTip("Pin/Unpin command line");
    m_pinButton->setCheckable(true);
    connect(m_pinButton, &QToolButton::toggled,
            this, &CommandLineToolbar::pinToggled);
    m_layout->addWidget(m_pinButton);

    // Copy
    m_copyButton = new QToolButton(this);
    m_copyButton->setText("📋");
    m_copyButton->setToolTip("Copy history (Ctrl+C)");
    connect(m_copyButton, &QToolButton::clicked,
            this, &CommandLineToolbar::copyRequested);
    m_layout->addWidget(m_copyButton);

    // Search
    m_searchButton = new QToolButton(this);
    m_searchButton->setText("🔍");
    m_searchButton->setToolTip("Search history");
    connect(m_searchButton, &QToolButton::clicked,
            this, &CommandLineToolbar::searchRequested);
    m_layout->addWidget(m_searchButton);

    // Settings
    m_settingsButton = new QToolButton(this);
    m_settingsButton->setText("⚙️");
    m_settingsButton->setToolTip("Command line settings");
    connect(m_settingsButton, &QToolButton::clicked,
            this, &CommandLineToolbar::settingsRequested);
    m_layout->addWidget(m_settingsButton);

    // Clear
    m_clearButton = new QToolButton(this);
    m_clearButton->setText("🗑️");
    m_clearButton->setToolTip("Clear history");
    connect(m_clearButton, &QToolButton::clicked,
            this, &CommandLineToolbar::clearRequested);
    m_layout->addWidget(m_clearButton);

    // Expand/Collapse
    m_expandButton = new QToolButton(this);
    m_expandButton->setText("↕️");
    m_expandButton->setToolTip("Expand/Collapse (F2)");
    m_expandButton->setCheckable(true);
    connect(m_expandButton, &QToolButton::toggled,
            this, &CommandLineToolbar::expandToggled);
    m_layout->addWidget(m_expandButton);
}

void CommandLineToolbar::createOptions() {
    // AutoComplete
    m_autoCompleteCheck = new QCheckBox("AutoComplete", this);
    m_autoCompleteCheck->setChecked(true);
    connect(m_autoCompleteCheck, &QCheckBox::toggled,
            this, &CommandLineToolbar::autoCompleteToggled);
    m_layout->addWidget(m_autoCompleteCheck);

    m_layout->addSpacing(8);

    // Echo
    m_echoCheck = new QCheckBox("Echo", this);
    m_echoCheck->setChecked(true);
    connect(m_echoCheck, &QCheckBox::toggled,
            this, &CommandLineToolbar::echoToggled);
    m_layout->addWidget(m_echoCheck);
}

} // namespace ui
} // namespace aicad
