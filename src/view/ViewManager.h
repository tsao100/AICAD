#pragma once

#include <QObject>

class CadView;

/**
 * @brief ViewManager
 *
 * 負責管理目前啟用的 View（CadView）
 * 第一階段僅支援單一 View
 * 後續可擴充為多視窗 / 多分割 View
 */
class ViewManager : public QObject
{
    Q_OBJECT

public:
    explicit ViewManager(QObject* parent = nullptr);

    /// 設定目前啟用的 View
    void setActiveView(CadView* view);

    /// 取得目前啟用的 View
    CadView* activeView() const;

Q_SIGNALS:
    /// 當 active view 改變時通知（預留給未來 UI / Command 使用）
    void activeViewChanged(CadView* view);

private:
    CadView* m_activeView = nullptr; // non-owning
};
