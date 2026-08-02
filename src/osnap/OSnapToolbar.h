/**
 * @file OSnapToolbar.h
 * @brief Object Snap 工具列 - 切換各 Snap 模式的 Qt UI 元件
 *
 * 功能：
 *  - 每個 SnapType 對應一個可切換按鈕（圖示 + tooltip）
 *  - 「全開/全關」快捷按鈕
 *  - 顯示目前鎖定的 snap 類型名稱
 *  - 雙向與 OSnapManager 綁定
 *
 * @author AICAD Team
 * @date 2025-01-08
 */

#pragma once

#include "OSnapTypes.h"

#include <QToolBar>
#include <QMap>
#include <QLabel>
#include <QAction>

namespace aicad {
namespace osnap {

class OSnapManager;

class OSnapToolbar : public QToolBar
{
    Q_OBJECT

public:
    explicit OSnapToolbar(OSnapManager* manager, QWidget* parent = nullptr);
    ~OSnapToolbar() override = default;

    /// 從 OSnapManager 的設定同步 UI 狀態
    void syncFromManager();

Q_SIGNALS:
    void snapTypeToggled(SnapType type, bool enabled);

private Q_SLOTS:
    void onActionToggled(bool checked);
    void onSnapLocked(const SnapCandidate& candidate);
    void onSnapCleared();
    void onSettingsChanged(const OSnapSettings& settings);
    void onEnableAllClicked();
    void onDisableAllClicked();

private:
    void setupUI();
    void addSnapButton(SnapType type, const QString& iconPath,
                       const QString& tooltip);

    OSnapManager*            m_manager = nullptr;
    QMap<SnapType, QAction*> m_actions;
    QLabel*                  m_statusLabel = nullptr;
};

} // namespace osnap
} // namespace aicad
