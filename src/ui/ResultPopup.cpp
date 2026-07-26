//src/ui/ResultPopup.cpp

#include "ResultPopup.h"
#include "core/Application.h"
#include "core/EventBus.h"

using namespace aicad::core;

namespace aicad::ui {

ResultPopup::ResultPopup(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating);

    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);

    setStyleSheet(R"(
        background: rgba(30,30,30,220);
        color: white;
        padding: 8px 14px;
        border-radius: 6px;
    )");

    connect(&m_timer, &QTimer::timeout, this, &QWidget::hide);
    m_timer.setSingleShot(true);

    auto bus = Application::instance()->eventBus();

    bus->subscribe(Events::COMMAND_EXECUTED, this,
        [this](const QVariant& v) {
            showMessage(v.toString(), false);
        });

    bus->subscribe(Events::COMMAND_FAILED, this,
        [this](const QVariant& v) {
            showMessage(v.toString(), true);
        });
}

void ResultPopup::showMessage(const QString& msg, bool error)
{
    m_label->setText(msg);

    if (error)
        setStyleSheet("background:#802020;color:white;padding:8px;");
    else
        setStyleSheet("background:#204080;color:white;padding:8px;");

    adjustSize();
    show();
    raise();

    m_timer.start(3000);
}

}
