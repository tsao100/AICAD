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
 * 先選取幾何，由 GeneralDimClassifier::classifyAll() 一次列出所有候選型別，
 * 再互動式設定尺寸線位置與數值。
 *
 * GDIM 昇級規劃 v2 — Phase 2：
 * 狀態機由 Idle → WaitSecond → WaitMenu → WaitArcType → WaitDimPlace →
 * WaitValue 簡化為 Idle → WaitSecond → WaitCandidate → WaitDimPlace →
 * WaitValue。WaitMenu/WaitArcType（字母選單）與 MenuKey/MenuOption 機制
 * 整套移除，統一併入 WaitCandidate：進入即列出 classifyAll() 全部結果、
 * 預覽第一個、TAB/SPACE 循環（見 CadView::keyPressEvent 發布的
 * Events::CANDIDATE_CYCLE）、Enter 或輸入快捷字母確認。
 */
class GeneralDimCommand : public Command {
    Q_OBJECT
public:
    GeneralDimCommand();
    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }

private:
    enum class State {
        Idle,           ///< 等待第一個幾何
        WaitSecond,     ///< 等待第二個幾何（點狀 / 選了「量距第二點」候選後）
        WaitCandidate,  ///< 等待使用者從候選陣列中確認一個標註型別（Phase 2）
        WaitDimPlace,   ///< PlaceDimLine 模式，等待點擊確認偏移
        WaitValue,      ///< 等待使用者輸入數值/表達式
    };

    State                m_state         = State::Idle;
    QList<cad::GeomRef>  m_refs;
    cad::GeomRef         m_originalLineRef;  ///< FixedLength 切換 H/V 時保存的原始單 ref
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

    // ── GDIM v2 Phase 2：多候選引擎 ───────────────────────────────────────
    QList<cad::GeneralDimClassifier::Candidate> m_candidates;
    int                  m_candidateIndex = 0;   ///< 目前高亮候選的索引
    /// 目前高亮/已確認候選是否要用補角（180°－夾角），見
    /// GeneralDimClassifier::Candidate::useSupplementAngle
    bool                 m_useSupplementAngle = false;

    /// 單一幾何選取後，若能立即判斷唯一預設候選（見 onGeomPicked），
    /// 會先顯示預覽並排一個短暫延遲的自動確認（QTimer::singleShot），
    /// 讓使用者仍有機會在延遲時間內點第二個幾何改成配對。這個計數器
    /// 用來讓「點了第二個幾何」或「cleanup()」能讓舊的延遲確認失效
    /// （比對時 token 不符就直接不執行，不需要真的管理 QTimer 物件）。
    int                  m_pendingConfirmToken = 0;

    void subscribeGeomPicked   ();
    void subscribeGeomHover    ();   ///< 新增：GetGeom 模式 hover 預覽
    void subscribeStringInput  ();
    void subscribeDimConfirmed ();
    void subscribePreview      ();
    void subscribeCancelled    ();
    void subscribeCandidateCycle();  ///< Phase 2：Tab/Space 候選循環
    void unsubscribeGeomPicked ();
    void unsubscribeAll        ();

    void onGeomPicked   (const QVariant& payload);
    void onGeomHover    (const QVariant& payload);   ///< 新增
    void onStringInput  (const QVariant& payload);
    void onDimConfirmed (const QVariant& payload);
    void onCancelled    (const QVariant&);
    void onCandidateCycle(const QVariant& payload);  ///< Phase 2

    void transitionToWaitSecond   ();
    void transitionToWaitCandidate(const QList<cad::GeneralDimClassifier::Candidate>& candidates);
    void transitionToWaitDimPlace ();
    void transitionToWaitValue    ();
    void commitDimension          ();
    void cleanup                  ();

    /// 依 m_candidateIndex 把候選內容套用到 m_type/m_distMode，並刷新命令列提示 + 預覽
    void applyHighlightedCandidate();

    /// 確認第 index 個候選（Enter/快捷字母/滑鼠再次點擊/單一候選自動確認 皆走此路徑）。
    /// needsSecondPick 則進 WaitSecond；否則直接進 WaitDimPlace。
    void confirmCandidate(int index);

    /// 依目前 m_refs + 可選的 extraRef 組出預覽，推送到 CadView
    void updateDimPreview(const cad::GeomRef* extraRef = nullptr);
    void clearDimPreview ();

    double        measureCurrentValue () const;  ///< 從幾何量測現有尺寸
    double        measureCurrentValue2() const;  ///< CoordinateDim Y 分量
    QVector2D     refMidpoint2D       () const;  ///< 用於 beginPlaceDimLine 錨點
    cad::Sketch*  activeSketch        () const;
    QString       constraintTypeName  () const;
};

} // namespace aicad::command
