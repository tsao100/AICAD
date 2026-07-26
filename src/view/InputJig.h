/**
 * @file InputJig.h
 * @brief 距離／角度輸入 Jig（跟隨橡皮筋 / Grip 拖曳，標示在各自的尺寸線位置上）
 *
 * 仿 SolidWorks / Fusion 360「Dynamic Input」：取點或 Grip 拖曳過程中，
 * 距離數值標示在橡皮筋線段中點附近（如同該線段自己的尺寸線），角度數值
 * 標示在起點（頂點）附近（如同角度標註）。使用者可直接輸入數字取代滑鼠
 * 位置，Tab 鍵在兩欄位間切換並鎖定目前欄位的值，Enter 送出。
 *
 * 本類別純粹負責 UI 顯示與數值輸入/鎖定邏輯；「距離/角度 → 平面座標」的
 * 換算、兩個標籤各自應該放在螢幕上哪個位置（線段中點／起點）、以及送出
 * 後的提交（POINT_ACQUIRED / GripManager::commitDragAt）都由呼叫端
 * （CadView）負責，維持單一職責。
 */
#pragma once

#include <QObject>
#include <QPoint>
#include <QPointF>

class QLineEdit;
class QLabel;
class QFrame;
class QWidget;
class QEvent;

namespace aicad {
namespace view {

class JigLeaderLine;   // 內部用：畫距離/角度標籤與其錨點之間的點線（尺寸線）

class InputJig : public QObject {
    Q_OBJECT
public:
    explicit InputJig(QWidget* parent);

    void setAzimuthMode(bool azimuth);
    bool isAzimuthMode() const { return m_azimuth; }

    void showLive(const QPoint& distAnchor, const QPoint& angleAnchor,
                  const QPointF& lineDir, double distance, double angleDeg);

    void hideJig();
    bool isJigVisible() const;

    void resetLocks();

    bool isDistanceLocked() const { return m_distLocked; }
    bool isAngleLocked()    const { return m_angleLocked; }

    bool distanceValue(double& out) const;
    bool angleValue(double& out) const;

    void beginTypedInput(const QString& firstChars);

Q_SIGNALS:
    void committed(double distance, double angleDeg);
    void cancelled();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QFrame* buildFrame(QWidget* parent, const QString& labelText,
                        QLineEdit** outEdit, QLabel** outLabel);
    void lockField(QLineEdit* edit, bool& lockedFlag);
    void focusField(QLineEdit* edit, bool selectAll);
    void emitCommit();

    QFrame*    m_distFrame  = nullptr;
    QFrame*    m_angleFrame = nullptr;
    QLineEdit* m_distEdit   = nullptr;
    QLineEdit* m_angleEdit  = nullptr;
    QLabel*    m_distLabel  = nullptr;
    QLabel*    m_angleLabel = nullptr;

    // 距離/角度標籤各自的尺寸線（點線）：從線段中點/起點連到標籤方框
    JigLeaderLine* m_distLeader  = nullptr;
    JigLeaderLine* m_angleLeader = nullptr;

    bool m_distLocked  = false;
    bool m_angleLocked = false;
    bool m_azimuth      = false;

    double m_liveDistance = 0.0;
    double m_liveAngle    = 0.0;
};

} // namespace view
} // namespace aicad
