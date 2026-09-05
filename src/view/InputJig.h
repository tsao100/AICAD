/**
 * @file InputJig.h
 * @brief 距離／角度輸入 Jig 的「輸入」部分——兩個 QLineEdit，負責使用者
 *        鍵盤輸入覆寫值（游標、選取、退格、IME），只在使用者實際打字或
 *        Tab 切換聚焦到某欄位時才顯示成貼著錨點的小方框。
 *
 * 仿 SolidWorks / Fusion 360「Dynamic Input」：取點或 Grip 拖曳過程中，
 * 距離／角度數值即時顯示在滑鼠附近；使用者可直接輸入數字取代滑鼠位置，
 * Tab 鍵在兩欄位間切換並鎖定目前欄位的值，Enter 送出。
 *
 * 職責分工（2024 改版，見 InputJigOverlay.h 開頭的完整說明）：
 *   - 本類別（InputJig）：純輸入。畫面上的「即時顯示」（距離/角度數值、
 *     尺寸線、尺寸弧線）已經改由 InputJigOverlay 用 OCCT Prs3d_Presentation
 *     直接畫在 3D 場景中，完全不經過這裡的 Qt widget——本類別的兩個
 *     QFrame 現在只在對應欄位「實際擁有 focus」（使用者正在打字）時才
 *     顯示，其餘時間（單純移動滑鼠、即時追蹤數值）保持隱藏。這樣一來，
 *     滑鼠移動／點選（含 OSnap 吸附）幾乎不會再被任何 Qt widget 攔截，
 *     只有在使用者主動聚焦某個輸入框的短暫期間才會有一個小方框浮在畫面上
 *     （此時使用者的操作本來就是打字，不是點擊 3D 場景）。
 *   - InputJigOverlay：純顯示，見該類別開頭的完整說明。
 *
 * 「距離/角度 → 平面座標」的換算、兩個標籤各自應該放在螢幕上哪個位置
 * （線段中點／起點）、以及送出後的提交（POINT_ACQUIRED /
 * GripManager::commitDragAt）都由呼叫端（CadView）負責，維持單一職責。
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

class InputJig : public QObject {
    Q_OBJECT
public:
    explicit InputJig(QWidget* parent);

    void setAzimuthMode(bool azimuth);
    bool isAzimuthMode() const { return m_azimuth; }

    /**
     * @brief 更新即時追蹤值，並依「欄位是否正在被打字」決定對應的小方框
     *        要不要顯示（見類別說明）。呼叫端（CadView）另外把
     *        distanceEditText()/angleEditText() 讀出來餵給
     *        InputJigOverlay 做 3D 文字顯示。
     * @param distAnchor  距離小方框的錨點（螢幕座標，僅在該欄位有 focus
     *                    時才會實際顯示於此）。
     * @param angleAnchor 角度小方框的錨點（同上）。
     */
    void showLive(const QPoint& distAnchor, const QPoint& angleAnchor,
                  double distance, double angleDeg);

    void hideJig();

    /** InputJig 這整套「距離／角度輸入」session 是否啟用中——語意上是
     *  「session 是否作用中」，不是「Qt frame 目前有沒有畫出來」（frame
     *  現在只在使用者實際打字/Tab 聚焦時才顯示，其餘時間由
     *  InputJigOverlay 的 3D 文字顯示，但整個 session 仍然是作用中，
     *  ESC/右鍵取消、開始打字觸發 beginTypedInput() 都要繼續依此判斷）。 */
    bool isJigVisible() const { return m_active; }

    void resetLocks();

    bool isDistanceLocked() const { return m_distLocked; }
    bool isAngleLocked()    const { return m_angleLocked; }

    bool distanceValue(double& out) const;
    bool angleValue(double& out) const;

    /** 目前距離欄位的顯示文字——未鎖定時是 showLive() 帶入之 distance 的
     *  格式化字串，鎖定/編輯中則是使用者正在輸入的內容。供呼叫端餵給
     *  InputJigOverlay::showLive() 的 distText 參數，讓 3D 文字即時反映
     *  使用者輸入。 */
    QString distanceEditText() const;
    /** 同上，角度欄位。 */
    QString angleEditText() const;

    void beginTypedInput(const QString& firstChars);

Q_SIGNALS:
    void committed(double distance, double angleDeg);
    void cancelled();

    /** 使用者正在打字（textEdited）時發出，與滑鼠移動無關——呼叫端
     *  （CadView）用來即時刷新 InputJigOverlay 的 3D 文字，不必等下一次
     *  滑鼠移動事件才更新（否則使用者打字時 3D 場景會看起來沒反應）。 */
    void liveTextChanged();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QFrame* buildFrame(QWidget* parent, const QString& labelText,
                        QLineEdit** outEdit, QLabel** outLabel);
    void lockField(QLineEdit* edit, bool& lockedFlag);
    void focusField(QLineEdit* edit, bool selectAll);
    void emitCommit();
    /// 依 hasFocus() 決定兩個 frame 個別要不要顯示（見類別說明）。
    void updateFrameVisibility();

    QFrame*    m_distFrame  = nullptr;
    QFrame*    m_angleFrame = nullptr;
    QLineEdit* m_distEdit   = nullptr;
    QLineEdit* m_angleEdit  = nullptr;
    QLabel*    m_distLabel  = nullptr;
    QLabel*    m_angleLabel = nullptr;

    bool m_distLocked  = false;
    bool m_angleLocked = false;
    bool m_azimuth      = false;
    bool m_active        = false;  ///< session 是否作用中，見 isJigVisible()

    double m_liveDistance = 0.0;
    double m_liveAngle    = 0.0;
};

} // namespace view
} // namespace aicad
