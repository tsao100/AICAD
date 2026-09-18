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
class QValidator;

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

    /** 使用者在尚未聚焦任何欄位時直接打字（見 CadView::keyPressEvent()
     *  的「Jig 可見時，直接打字＝輸入距離」）——預設塞進距離欄位並聚焦
     *  它，跟原本行為一致。
     *  @param angleField 為 true 時改塞進角度欄位並聚焦它。目前只有
     *         方位角模式下，使用者直接打 '-'（開始輸入「度-分-秒」格式
     *         的角度值）時才會用到——距離恆為正值，不會用到開頭的 '-'，
     *         所以方位角模式下把 '-' 一律視為「要開始輸入角度」的訊號，
     *         而不是機械地塞進距離欄位（見 CadView::keyPressEvent()）。
     */
    void beginTypedInput(const QString& firstChars, bool angleField = false);

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

    /// 方位角模式刻意「不設驗證器」（見 setAzimuthMode()）——之前這裡
    /// 曾經改用允許 '-' 分隔符的正規表示式驗證器，但使用者實測仍然打不
    /// 出 '-'；為了徹底排除任何驗證器本身的字元組成限制造成的疑慮，
    /// 方位角模式乾脆完全不設驗證器，讓使用者能輸入任何字元組成
    /// 「度-分-秒」格式（真正的格式/範圍檢查交給送出/鎖定時呼叫的
    /// parseAngleText()，見該函式）。以 m_angleEdit 為 parent，交給
    /// Qt 的 parent-child 機制自動釋放，這裡不用手動 delete。
    QValidator* m_angleValidatorDecimal = nullptr;

    bool m_distLocked  = false;
    bool m_angleLocked = false;
    /// 角度欄位是否被使用者「實際打字」修改過內容（由 textEdited 訊號
    /// 設定），跟 m_angleLocked 是兩件事：m_angleLocked 只代表「這個
    /// 欄位目前是否被凍結，不再隨滑鼠即時更新」，Tab／Enter 經過而完全
    /// 沒打字也會讓它變 true（見 lockField()）。這裡額外記錄「是否真的
    /// 打過字」，是因為一般模式下畫面顯示的文字已經折算成不大於 180°、
    /// 無方向性的角度值（見 InputJig.cpp 內 formatAngleForDisplay() 的
    /// 說明），如果使用者只是 Tab／Enter 經過、沒有實際修改內容，就不能
    /// 直接把這段「折算過」的顯示文字拿來反推座標，否則方向可能被錯誤
    /// 對摺到 X 軸另一側；這種情況要改用 m_liveAngle（真正、有方向性的
    /// 即時角度）——見 angleValue()／emitCommit()。
    bool m_angleTypedByUser = false;
    bool m_azimuth      = false;
    bool m_active        = false;  ///< session 是否作用中，見 isJigVisible()

    double m_liveDistance = 0.0;
    double m_liveAngle    = 0.0;
};

} // namespace view
} // namespace aicad
