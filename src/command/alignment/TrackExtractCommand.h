/**
 * @file TrackExtractCommand.h
 * @brief TRACKEXTRACT（別名 TX）— 在既有 TrackCenterLine 上點選一個 TT
 *        （兩條 Tangent 直接相接）切點，將該點之後（含）的所有線元移到一條
 *        新的 TrackCenterLine，原線只保留頭段。也支援輸入 A/AUTO 自動依所有
 *        內部 TT 依里程切分成多段。
 *
 * 互動流程：點選一次即完成單點切分（比照 AlignmentFixCurveCommand 的
 * EventBus 訂閱模式）；或於提示時輸入 A（或 AUTO）改為自動全部切分模式，
 * 不需要點選。
 *
 * 設計取捨（已與使用者確認，見對話紀錄，並依實測回饋修正）：
 *   - 只能切在 TT 點（rawPoints() 中 tsc=="TT" 且非線形起訖端點，也就是兩條
 *     Tangent 直接相接的內部接合點）；點選只用來「吸附」到最近的 TT 候選點，
 *     不支援切在其他關鍵點（TS/SC/CS/ST 等曲線邊界）或任意位置，因為切在曲線
 *     邊界／曲線中段容易破壞緩和曲線的內部幾何連續性，而 TT 點兩側本就是各自
 *     獨立、方向可能不同的直線，天然適合作為分界。
 *   - 操作在「已求解」的關鍵點層級（TrackCenterLine::horizontal()->rawPoints()）
 *     進行，不觸碰 AlignmentDocument 的 EditableElement 編輯鏈本身的相依索引，
 *     因此完全不受 Floating / SCS / 複合曲線 / 反向緩和曲線等元素間索引相依
 *     關係影響。
 *   - 切分結果：原線保留切點以前（含）的部分；切點（含）之後、里程較大的
 *     所有點合併為「一條」新 TrackCenterLine（名稱交給 Document 自動編號）。
 *   - 縱斷面（Vertical）若存在，依相同里程邊界切分；邊界若未剛好落在既有
 *     VerticalAlignmentPoint 上，以 getElevation()/getSlope() 於邊界內插一個
 *     切線型關鍵點（grade＝該點坡度，lvc=0）作為近似 —— 如切點恰好落在豎曲線
 *     （拋物線）範圍內，切分後的縱斷面在此近似下會與原豎曲線略有落差。
 *   - 除了更新兩條線「已求解」的 rawPoints()，也會同步重建雙方 AlignmentDocument
 *     的 EditableElement 編輯鏈（seedFromRawPoints），避免存檔後 alignmentEdit
 *     區塊仍是切分前的舊資料、重新開檔又「復原」的問題。切分出的新線形若內部
 *     還有其他 TT，seedFromRawPoints() 一律保留為獨立的 Fixed Tangent 邊界，
 *     即使前後兩段方位角完全一致（真正共線）也不合併（見 AlignmentDocument.cpp
 *     的修正），不會被自動接成一段 Tangent。
 *   - Auto 模式：從目前線形開始，反覆找「里程最小的內部 TT」切一刀、把後半段
 *     搬到新線，再對這條新線重複同樣的動作，直到某一段已經沒有內部 TT 為止；
 *     等同於依里程順序在所有內部 TT 處逐一切分成 N+1 段。
 *   - Auto 模式命名規則：輸入 A／AUTO 後，先跳出 TrackNameSequenceDialog
 *     （見該檔案說明）讓使用者設定「字首（1 碼）＋序號（2 碼，補零）＋
 *     字尾（1 碼）」的命名規則（例如 "B01U"），以及序號逐段遞增或遞減。
 *     取消對話框則整個 TX 指令視為取消，不執行任何切分。命名套用到全部
 *     N+1 段結果（依里程由小到大，含原線保留的頭段本身），而非只命名新
 *     切出來的那幾段——因為這個慣例底下，整條原始線形本來就代表一系列
 *     連號的實體軌道區段。
 */
#pragma once

#include "command/Command.h"
#include "command/CommandTypes.h"
#include "railway/RailwayAlignment.h"

#include <QPointF>
#include <QString>
#include <QVector>

namespace aicad {
namespace cad     { class Document; }
namespace ui      { class UIManager; }
namespace railway { class AlignmentDocument; }

namespace command {

class TrackExtractCommand : public Command
{
    Q_OBJECT

public:
    explicit TrackExtractCommand(QObject* parent = nullptr);

    CommandResult execute(const CommandContext& context) override;
    QString       getUsage() const override;

private:
    void handlePointAcquired(const QPointF& point);
    void handleStringInput(const QString& text);
    void handleCancelled();
    void cleanup() override;

    /**
     * @brief 回傳離 worldPt 最近的「內部 TT」關鍵點索引（Euclidean，比對
     *        easting/northing）。內部 TT ＝ tsc=="TT" 且 0 < index < 最後一筆
     *        （排除線形本身的起訖端點）。找不到任何內部 TT 候選點時回傳 -1。
     */
    int nearestInternalTTIndex(const QVector<railway::AlignmentPoint>& pts,
                                const QPointF& worldPt) const;

    /** 單點切分：cutIdx 為 rawPoints() 中的切點索引（原線保留 [0,cutIdx]）。 */
    bool performExtractSplit(int cutIdx);

    /**
     * @brief Auto 模式：依里程由少往多，在目前線形（及每次切分後產生的新線）
     *        找到的第一個內部 TT 逐段切分，直到不再有內部 TT 為止。
     *        若 m_useNameSequence 為 true，切分過程中會依 m_nameSeq* 設定
     *        的規則依序為每一段（含原線保留的頭段）命名（見標頭檔說明）。
     */
    bool performAutoSplitAll();

    /**
     * @brief 單一切分動作的共用核心：在 tcl（其目前編輯中的 AlignmentDocument
     *        為 tclAlignDoc）的 rawPoints() 索引 cutIdx 處切分——tcl 原地保留
     *        [0,cutIdx]，[cutIdx,終點] 的內容搬到一條新建的 TrackCenterLine
     *        （並同步建立/重建雙方的 AlignmentDocument 編輯鏈）。供
     *        performExtractSplit()（單點）與 performAutoSplitAll()（逐段）
     *        共用，避免兩份幾乎一樣的切分邏輯各自維護一次。
     *
     * @param newTclName 新建立的 TrackCenterLine 名稱；空字串時交給
     *                    Document::addTrackCenterLine() 依既有規則自動命名
     *                    （單點互動切分／未啟用命名規則的 Auto 皆用此預設）。
     * @return 新建立的 TrackCenterLine；輸入不合法或建立失敗時回傳 nullptr。
     */
    railway::TrackCenterLine* splitTclAt(railway::TrackCenterLine* tcl,
                                          railway::AlignmentDocument* tclAlignDoc,
                                          int cutIdx,
                                          const QString& newTclName = QString());

    /**
     * @brief 清空並重建 alignDoc 的 EditableElement／VIP 編輯鏈，使其與傳入的
     *        「已求解」關鍵點一致（seedFromRawPoints()/seedFromDensePoints()
     *        兩者皆要求目前鏈為空才會建立，故先呼叫 removeElement()/removeVip()
     *        清空）。alignDoc 為 nullptr 時直接略過。
     */
    void reseedAlignmentEdit(railway::AlignmentDocument* alignDoc,
                              const QVector<railway::AlignmentPoint>& hPts,
                              const QVector<railway::VerticalAlignmentPoint>& vPts);

    railway::TrackCenterLine*   m_tcl       = nullptr;
    cad::Document*               m_doc       = nullptr;
    railway::AlignmentDocument* m_alignDoc  = nullptr;  ///< 原線目前編輯中的 AlignmentDocument
    ui::UIManager*               m_uiManager = nullptr;
    QWidget*                     m_parentWidget = nullptr;  ///< TrackNameSequenceDialog 的父視窗（來自 CommandContext::cadView）

    bool m_isFinishing = false;

    // ── Auto 模式命名規則（由 TrackNameSequenceDialog 設定，見標頭檔說明） ──
    bool    m_useNameSequence   = false;
    QString m_nameSeqPrefix;
    int     m_nameSeqStart      = 1;
    QString m_nameSeqSuffix;
    bool    m_nameSeqIncrement  = true;
};

} // namespace command
} // namespace aicad
