#pragma once

#include <QObject>
#include <QString>

class QAction;
class MainWindow;

/**
 * @brief UI-5: Central place to control QAction state
 *
 * Phase-1:
 * - Depends only on MainWindow
 * - No command system required yet
 */
class UIStateController : public QObject
{
    Q_OBJECT
public:
    explicit UIStateController(MainWindow* mainWindow);

    // UI-level API (temporary)
    void setEnabled(const QString& commandId, bool enabled);
    void setChecked(const QString& commandId, bool checked);
    void setVisible(const QString& commandId, bool visible);

private:
    MainWindow* m_mainWindow = nullptr;
};
