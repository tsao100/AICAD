/**
 * @file AlignmentDrawChainCommand.h
 * @brief ALIGNMENTDRAWCHAIN（alias: ADC）— 先設定一段「相對」線元，再點選
 *        一或多個既有線形的起點／終點，把該線元套用上去、直接繪出。
 *
 * 互動流程：
 *   1. 執行指令即先開啟 AlignmentDrawElementDialog（三個獨立區塊：直線／
 *      曲線／緩和曲線組，各自有自己的「繪製」按鈕）。按下某區塊的
 *      「繪製」立即關閉對話框，回傳這「一段」線元的定義。
 *      - 直線／曲線：對話框已經算好一組以原點為基準的「相對」關鍵點
 *        （resultPoints()），因為這兩種線元的幾何與起點曲率無關。
 *      - 緩和曲線組：對話框只回傳原始參數（isTransitionGroup()／
 *        transitionGroupParams()），不算幾何——因為緩和曲線的形狀本質上
 *        依賴起點曲率，這個值「以選取為準」（見下），無法像直線／曲線
 *        那樣先算好、再靠剛體變換套用；必須等知道使用者選了哪個端點、
 *        讀出該點真實的曲率之後才能算（見 buildTransitionGroupAt()）。
 *   2. 之後進入連續選取模式：每點選畫面上一點，命令在文件內所有
 *      TrackCenterLine 的「起點」／「終點」中找出距離最近的一個（若某條
 *      線的 rawPoints() 是空的——例如只用 FT/FC 建立、尚未同步回
 *      TrackCenterLine 的情況——退回讀取目前作用中的 AlignmentDocument
 *      〔context.alignmentDoc〕的求解結果），以此判定要接續哪一條線路、
 *      從哪一端接續，並讀出該端點目前的真實曲率（curvatureAtPoint()）。
 *   3. 直線／曲線：把 resultPoints() 以剛體變換（旋轉＋平移）套到真實
 *      端點上。緩和曲線組：直接以該端點的真實曲率為起點曲率，呼叫
 *      buildTransitionGroupAt() 就地算出實際幾何（不需要剛體變換，一開始
 *      就是用真實座標算的）。兩種情形都直接寫回該 TrackCenterLine 並刷新
 *      顯示——不再另外彈出逐段對話框，也不需要「使用者事先宣告的曲率」
 *      是否正確的驗證（沒有任何東西需要驗證：曲率就是讀出來的，不會錯）。
 *   4. 可重複步驟 2～3，把同一段線元套用到多個不同的端點；按右鍵
 *      （POINT_CANCELLED）結束整個指令。
 *
 * 若判定為「起點」：使用 railway::reverseAlignmentPoints() 把整條線形
 * 反轉方向，讓「原本的起點」變成反轉後陣列的「終點」，套用同一套「從
 * 終點接續」的計算邏輯，最後再反轉回來，寫回時就等於是從原本的起點
 * 往外延伸——不需要另外實作一套「反向接續」的計算路徑。
 *
 * 與 ALIGNMENTQUICKTABLE（AQT，批次表格輸入／編輯）互補：兩者共用同一套
 * railway::AlignmentQuickCalc 正算函式與 AlignmentPoint／TSC 資料模型。
 *
 * @see railway::reverseAlignmentPoints(), railway::curvatureAtPoint()
 * @see railway::EggTransitionElement
 * @see aicad::ui::AlignmentDrawElementDialog
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "railway/RailwayAlignment.h"

#include <QPointF>
#include <QString>
#include <QVector>

class QWidget;

namespace aicad {

namespace railway {
class AlignmentDocument;
}

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

    /** 取得某條 TrackCenterLine 目前「可用」的關鍵點序列：優先讀
     *  tcl->horizontal()->rawPoints()；若是空的且這條線正好是目前作用中
     *  的 AlignmentDocument（m_activeAlignmentDoc）對應的那一份，退回讀取
     *  該 AlignmentDocument 的求解結果（見標頭說明，涵蓋僅用 FT/FC 建立、
     *  尚未同步回 TCL 的情況）。都沒有資料時回傳空陣列。 */
    QVector<railway::AlignmentPoint> resolveExistingPoints(railway::TrackCenterLine* tcl) const;

    /** 找出最靠近 @p worldPos 的 TrackCenterLine 起點／終點（見
     *  resolveExistingPoints()）；找不到（文件內沒有任何含關鍵點的線路）
     *  時回傳 false。 */
    bool findNearestEndpoint(const QPointF& worldPos, QString& outTclId, bool& outIsStart) const;

    /** 把這一段線元（m_recipe 或緩和曲線組參數）套到指定端點：直接寫回
     *  並刷新顯示。 */
    void proceedWithEndpoint(const QString& tclId, bool extendFromStart);

    /**
     * @brief 需求 20：在真實端點 @p start（真實曲率 @p curvatureIn，
     *        以 curvatureAtPoint() 讀出，不需要、也不應該由使用者預先
     *        宣告）算出緩和曲線組的實際幾何，append 到 @p outAppended。
     *        邏輯與舊版 AlignmentDrawElementDialog::appendTransitionGroup()
     *        相同（原地移植——不再需要對話框內的相對座標與事後的剛體
     *        變換，因為一開始就是用真實座標、真實曲率算的），只是輸入
     *        的「起點」與「起點曲率」改成參數，而非對話框內部狀態。
     * @return 失敗（例如起點在切線上卻把圓弧半徑留白）時回傳 false 並填
     *         @p outError；成功回傳 true。
     */
    bool buildTransitionGroupAt(const railway::AlignmentPoint& start, double curvatureIn,
                                 QVector<railway::AlignmentPoint>& outAppended,
                                 QString& outError) const;

    QWidget* m_parentWidget = nullptr;
    bool     m_isFinishing  = false;

    railway::AlignmentDocument* m_activeAlignmentDoc = nullptr;  ///< = context.alignmentDoc

    /** 直線／曲線區用：對話框算好的相對關鍵點序列（見標頭說明）；
     *  [0]=佔位基準點、[1]=原點錨點、[2..]=正算出的新關鍵點。
     *  isTransitionGroup()==true 時不使用，改用 m_trans* 系列成員。 */
    QVector<railway::AlignmentPoint> m_recipe;

    bool    m_isTransitionGroup   = false;
    QString m_transFamily;
    double  m_transSpiralInLength = 0.0;
    bool    m_transTargetIsInfinite = true;
    double  m_transTargetRadius   = 0.0;
    double  m_transCurveLength    = 0.0;
    double  m_transSpiralOutLength = 0.0;

    int m_appliedCount = 0;  ///< 本次指令執行期間，recipe 成功套用的次數
};

} // namespace command
} // namespace aicad
