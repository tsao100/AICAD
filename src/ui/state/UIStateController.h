// ui/state/UIStateController.h
#pragma once

#include <QObject>
#include <QString>

class MenuBuilder;
class QAction;
class QToolBar;

class UIStateController : public QObject
{
    Q_OBJECT
public:
    explicit UIStateController(MenuBuilder* builder,
                               QObject* parent = nullptr);

    // Command-driven
    void setCommandEnabled(const QString& commandId, bool enabled);
    void setCommandChecked(const QString& commandId, bool checked);

    // Toolbar-driven
    void setToolbarVisible(const QString& toolbarName, bool visible);
    void setToolbarEnabled(const QString& toolbarName, bool enabled);

private:
    MenuBuilder* m_builder;
};
