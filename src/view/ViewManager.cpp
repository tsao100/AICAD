#include "ViewManager.h"
#include "CadView.h"

ViewManager::ViewManager(QObject* parent)
    : QObject(parent)
{
}

void ViewManager::setActiveView(CadView* view)
{
    if (m_activeView == view)
        return;

    m_activeView = view;
    emit activeViewChanged(m_activeView);
}

CadView* ViewManager::activeView() const
{
    return m_activeView;
}
