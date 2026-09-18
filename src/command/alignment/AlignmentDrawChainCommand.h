/**
 * @file AlignmentDrawChainCommand.h
 * @brief ALIGNMENTDRAWCHAIN（alias: ADC）— 點選既有線形的起點或終點，接續
 *        繪製一段或二段新線元。
 *
 * 互動流程（需求 20 重新設計）：
 *   1. 使用者點選畫面上一點；命令在文件內所有 TrackCenterLine 的
 *      「起點」與「終點」（rawPoints() 的第一個／最後一個關鍵點）中找出
 *      距離最近的一個，以此判定：(a) 要接續哪一條線路、(b) 要從它的起點
 *      端還是終點端接續。
 *   2. 若判定為「終點」：直接以既有邏輯（讀取關鍵點、開對話框、串接新
 *      線元）處理。
 *   3. 若判定為「起點」：使用 railway::reverseAlignmentPoints() 把整條
 *      線形反轉方向，讓「原本的起點」變成反轉後陣列的「終點」，套用同一套
 *      「從終點接續」的邏輯計算新線元，最後再反轉回來，寫回時就等於是從
 *      原本的起點往外延伸。這樣不需要另外實作一套「反向接續」的計算路徑。
 *   4. 開啟 AlignmentDrawElementDialog（模態），使用者設定一或二段新線元。
 *   5. 對話框「確定」→ 取得完整新關鍵點序列 → （若步驟 1 判定為起點，先
 *      反轉回來）→ 轉回 Local 座標 → 寫回該 TrackCenterLine。
 *
 * 與 ALIGNMENTQUICKTABLE（AQT，批次表格輸入／編輯）互補：兩者共用同一套
 * railway::AlignmentQuickCalc 正算函式與 AlignmentPoint／TSC 資料模型。
 *
 * @see railway::reverseAlignmentPoints(), railway::curvatureAtPoint()
 * @see aicad::ui::AlignmentDrawElementDialog
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "railway/RailwayAlignment.h"

#include <QPointF>
#include <QVector>

class QWidget;

namespace aicad {
namespace command {

class AlignmentDrawChainCommand : public Command
{
    Q_OBJECT

public:
    explicit AlignmentDrawChainCommand(QObject* parent = nullptr);
    ~AlignmentDrawChainCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool          isInteractive() const override { return true; }
    QString       getUsage()      const override;
    void          cleanup()       override;

private:
    void handlePointAcquired(const QPointF& point);
    void handleCancelled();

    /** 找出最靠近 @p worldPos 的 TrackCenterLine 起點／終點；找不到（文件
     *  內沒有任何含關鍵點的線路）時回傳 false。 */
    bool findNearestEndpoint(const QPointF& worldPos, QString& outTclId, bool& outIsStart) const;

    /** 開啟 AlignmentDrawElementDialog 並處理其結果（含起點端的反轉/反轉回來）。 */
    void proceedWithEndpoint(const QString& tclId, bool extendFromStart);

    QWidget* m_parentWidget = nullptr;
    bool     m_isFinishing  = false;
};

} // namespace command
} // namespace aicad
