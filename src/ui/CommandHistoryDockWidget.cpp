//src/ui/CommandDockWidget.cpp

#include "CommandHistoryDockWidget.h"
#include "core/Application.h"
#include "core/EventBus.h"

using namespace aicad::core;

namespace aicad::ui {

CommandHistoryDockWidget::CommandHistoryDockWidget(QWidget* parent)
    : QDockWidget("Command History", parent)
{
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFloating(true);
    resize(600, 300);

    m_text = new QTextEdit(this);
    m_text->setReadOnly(true);
    setWidget(m_text);

    Application::instance()->eventBus()->subscribe(
        Events::COMMAND_LOG, this,
        [this](const QVariant& v) {
            QString msg = v.value<QString>();
            m_text->append(msg);
        }
    );
}

}
