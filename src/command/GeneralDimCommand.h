#pragma once
#include "Command.h"
#include "cad/sketch/SketchConstraint.h"
#include "cad/sketch/GeneralDimClassifier.h"
#include "view/CadView.h"    // DimPreviewInfo
#include <QList>
#include <QString>
#include <QVector2D>

namespace aicad::command {

/**
 * @brief GDIM — General Dimension 命令
 *
 * GDIM 昇級規劃 v3（無選單版，見《GDIM_滑鼠動作組合清單_無選單版.md》）：
 *
 * 取代 v2 Phase 2 的候選陣列引擎（WaitCandidate / classifyAll() /
 * Tab-Space 循環 / 快捷字母）。核心原則：**型別不是被選出來的，是被
 * 「量」出來的**——遊標當下位置本身就是分類函式的輸入，點擊只是把
 * 當下已經在預覽的型別鎖定下來。
 *
 * 狀態機由 Idle → WaitSecond → WaitCandidate → WaitDimPlace → WaitValue
 * 簡化為 Idle → Anchored → WaitDimPlace → WaitValue：
 *   - Idle     : 尚未鎖定任何幾何，hover 依 GeneralDimClassifier::inferSingle()
 *                即時預覽單幾何型別。
 *   - Anchored : 已鎖定第一個幾何（起點）。持續依滑鼠位置在「單幾何型別」
 *                （沒有可配對的第二幾何時，inferSingle()）與「雙幾何型別」
 *                （滑鼠落在可配對的第二幾何上時，inferPair()）之間即時切換
 *                預覽。取代舊的 WaitSecond + WaitCandidate 兩個狀態。
 *   - WaitDimPlace : 僅雙幾何型別會進入此階段（第二次點擊落在可配對的第二
 *                幾何上）。移動滑鼠決定偏移位置，第三次點擊確認。
 *   - WaitValue: Dynamic Input Jig 輸入數值／公式（沿用既有邏輯不變）。
 *
 * 單幾何型別＝2 次點擊（選起點、選型別兼定位，第 2 次點擊落在空白處或無法
 * 配對的位置時直接鎖定）；雙幾何型別＝3 次點擊（選起點、選第二點兼定型、
 * 定位）。不再有任何離散選單、候選陣列、Tab/Space 循環或字母快捷鍵。
 */
class GeneralDimCommand : public Command {
    Q_OBJECT
public:
    GeneralDimCommand();
    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }

    /// 右鍵／Esc 在 Idle 以外的任何階段都只退一步（見 onCancelled()/backTo*()），
    /// 不應讓 CommandManager 把整個指令收掉。只有 Idle 狀態才真正允許
    /// CommandManager::cancelCurrentCommand() 執行完整的 cleanup()/deleteLater()；
    /// 其餘狀態一律回傳 false，COMMAND_CANCELLED 事件仍會發布（onCancelled() 走
    /// 內部的「退一步」邏輯），但指令物件本身不會被摧毀。
    bool canCancel() const override;

private:
    enum class State {
        Idle,           ///< 等待第一個幾何（起點）
        Anchored,       ///< 已鎖定起點，持續依滑鼠位置即時切換單/雙幾何型別預覽
        WaitDimPlace,   ///< （僅雙幾何型別）PlaceDimLine 模式，等待點擊確認偏移
        WaitValue,      ///< 等待使用者輸入數值/表達式
    };

    State                m_state         = State::Idle;
    QList<cad::GeomRef>  m_refs;         ///< 目前鎖定/預覽用的量測 refs
    cad::GeomRef         m_anchorRef;    ///< Anchored 狀態鎖定的起點（原始 ref，不受
                                          ///< pairedRefs 正規化影響，供退回 Anchored 時還原）
    cad::ConstraintType  m_type          = cad::ConstraintType::FixedDistance;
    cad::DistanceMode    m_distMode      = cad::DistanceMode::PointToPoint;
    double               m_measuredValue = 0.0;
    double               m_measuredValue2= 0.0;  // CoordinateDim Y
    double               m_pendingValue  = 0.0;
    QString              m_pendingExpr;
    bool                 m_driving       = true;
    double               m_dimOffsetX    = 0.0;
    double               m_dimOffsetY    = 0.0;
    QVector2D            m_dimAnchor2D;           ///< beginPlaceDimLine 時的錨點（草圖座標）
    bool                 m_hasPending    = false; ///< execute() 帶入了預設值
    /// true：m_pendingValue 來自使用者直接輸入（literal number 或 expression）；
    /// false：m_pendingValue 是採用 m_measuredValue（已經是弧度，無需再轉換）。
    /// 僅在 m_type 為 FixedAngleDim/FixedAngle 時有意義：使用者輸入視為「度」，
    /// commitDimension() 會在此時轉換為弧度存入 SketchConstraint::value。
    bool                 m_pendingIsRawUserInput = false;

    /// 目前預覽/已鎖定的型別是否要用補角（180°－夾角）。相交兩線的夾角型別
    /// 在 WaitDimPlace 階段依滑鼠落在角平分線的哪一側動態切換（無選單版
    /// 第 B 組第 16 項）。
    bool                 m_useSupplementAngle = false;

    /// 目前這筆標註是從「單幾何流程」（Anchored 第 2 次點擊同時定型定位，
    /// 直接跳進 WaitValue）還是「雙幾何流程」（經過 WaitDimPlace 第 3 次
    /// 點擊定位）進來的。用於 Esc/右鍵在 WaitValue 階段退回正確的上一步
    /// （無選單版第 D 組第 27 項）。
    bool                 m_wasSingleGeomFlow = false;

    void subscribeGeomPicked   ();
    void subscribeGeomHover    ();
    void subscribeStringInput  ();
    void subscribeDimConfirmed ();
    void subscribePreview      ();
    void subscribeCancelled    ();
    void unsubscribeGeomPicked ();
    void unsubscribeAll        ();

    void onGeomPicked   (const QVariant& payload);
    void onGeomHover    (const QVariant& payload);
    void onStringInput  (const QVariant& payload);
    void onDimConfirmed (const QVariant& payload);
    void onCancelled    (const QVariant&);

    /// Anchored 狀態下，第 2 次點擊落在可配對的第二幾何上：鎖定雙幾何型別，
    /// 進入 WaitDimPlace（第 3 次點擊定位）。
    void lockPairGeom(const cad::GeomRef& anchor, const cad::GeomRef& second,
                       const QVector2D& mousePt);

    /// Anchored 狀態下，第 2 次點擊落在空白處或無法配對的位置：直接依滑鼠
    /// 位置鎖定單幾何型別，同時用這次點擊的位置算出 offset，直接進 WaitValue
    /// （不經過 WaitDimPlace，因為型別與位置已經在同一次點擊裡一起決定）。
    void lockSingleGeom(const cad::GeomRef& anchor, const QVector2D& mousePt);

    void transitionToWaitDimPlace ();
    void transitionToWaitValue    ();
    void commitDimension          ();
    void cleanup                  ();

    /// Esc/右鍵狀態退回（無選單版第 D 組）
    void backToIdle               ();  ///< 25：Anchored → Idle
    void backToAnchoredFromDimPlace(); ///< 26：WaitDimPlace → Anchored
    void backToAnchoredFromValue  ();  ///< 27a：WaitValue（單幾何流程）→ Anchored
    void backToDimPlaceFromValue  ();  ///< 27b：WaitValue（雙幾何流程）→ WaitDimPlace

    /// 將 refs/type/mode 直接推送到 CadView 做預覽（不影響 m_refs/m_type/m_distMode，
    /// 供 Idle hover 這類「尚未鎖定任何狀態」的場合使用內部暫存計算）。
    void pushPreview(const QList<cad::GeomRef>& refs, cad::ConstraintType type,
                      cad::DistanceMode mode);
    void clearDimPreview();

    double        measureCurrentValue () const;  ///< 從幾何量測現有尺寸
    double        measureCurrentValue2() const;  ///< CoordinateDim Y 分量
    QVector2D     refMidpoint2D       () const;  ///< 用於 beginPlaceDimLine 錨點
    cad::Sketch*  activeSketch        () const;
    QString       constraintTypeName  () const;
};

} // namespace aicad::command
