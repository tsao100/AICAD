/**
 * @file AlignmentDrawChainCommand.cpp
 * @brief Implementation of ALIGNMENTDRAWCHAIN — see the header for design.
 */
#include "command/alignment/AlignmentDrawChainCommand.h"

#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/CommandLineManager.h"
#include "core/EventBus.h"
#include "cad/Document.h"
#include "railway/AlignmentDocument.h"
#include "railway/AlignmentQuickCalc.h"
#include "railway/RailwayAlignment.h"
#include "railway/RailwayAlignmentElement.h"
#include "ui/AlignmentDrawElementDialog.h"
#include "ui/UIManager.h"
#include "view/CadView.h"   // CadView : public QWidget — 供 static_cast(context.cadView) 使用

#include <QMetaObject>
#include <cmath>
#include <limits>

using namespace aicad::core;

namespace aicad {
namespace command {

using railway::TrackCenterLine;
using railway::AlignmentPoint;
using railway::ElementType;

namespace {

/**
 * @brief 把「先輸入資料」模式建立的相對關鍵點（原點=0,0，方位角基準=0，
 *        里程基準=0）套到真實的接續點（@p anchor）。只用於直線／曲線區
 *        （其幾何與起點曲率無關）；緩和曲線組改用
 *        AlignmentDrawChainCommand::buildTransitionGroupAt() 直接以真實
 *        座標／真實曲率計算，見標頭說明。
 *
 * 公式與 AlignmentElement::localToWorld()（見 RailwayAlignmentElement.cpp
 * 開頭註解）的世界座標公式一致：Wx=ox+X1·sin(az)+Y1·cos(az)，
 * Wy=oy+X1·cos(az)−Y1·sin(az)。@p rel 本身就是用同一套公式、以方位角基準
 * 0 算出來的整條鏈，因此在 az=0 時公式化簡為 Wx=ox+Y1、Wy=oy+X1，也就是
 * rel.easting 扮演 Y1、rel.northing 扮演 X1 的角色；套用到新的
 * anchor（新原點 ox,oy、新方位角基準 az）時只要代回同一條公式即可（az=0
 * 時可驗證會原樣還原 rel.easting/northing，確認公式與角色對應正確）。
 */
AlignmentPoint transformRecipePoint(const AlignmentPoint& rel, const AlignmentPoint& anchor)
{
    const double sinA = std::sin(anchor.azimuth);
    const double cosA = std::cos(anchor.azimuth);
    const double dx = rel.easting;    // 扮演公式裡的 Y1
    const double dy = rel.northing;   // 扮演公式裡的 X1

    AlignmentPoint out = rel;
    out.easting  = anchor.easting  + dy * sinA + dx * cosA;
    out.northing = anchor.northing + dy * cosA - dx * sinA;
    out.azimuth  = railway::AlignmentElement::normalise(anchor.azimuth + rel.azimuth);
    out.chainage     = anchor.chainage     + rel.chainage;
    out.contChainage = anchor.contChainage + rel.contChainage;
    return out;
}

/**
 * @brief 見 AlignmentDrawElementDialog.cpp 內同名函式的說明——把本對話框
 *        慣用的大寫 curveType token 轉成 railway::ElementType，供直接建構
 *        railway::EggTransitionElement 使用；各自保留一份（與
 *        HAlignTableWidget 已有先例一致）。
 */
ElementType eggFamilyFromToken(const QString& token)
{
    if      (token == QLatin1String("HALFSINE"))    return ElementType::HalfSine;
    else if (token == QLatin1String("PARABOLA"))    return ElementType::Parabola;
    else if (token == QLatin1String("CUBICJPN"))    return ElementType::CubicJPN;
    else if (token == QLatin1String("CUBICECI"))    return ElementType::CubicECI;
    else if (token == QLatin1String("SINUSOIDAL"))  return ElementType::Sinusoidal;
    else if (token == QLatin1String("COSINE"))      return ElementType::Cosine;
    else if (token == QLatin1String("BLOSS"))       return ElementType::Bloss;
    else if (token == QLatin1String("RADIOID"))     return ElementType::Radioid;
    else if (token == QLatin1String("LOGARITHMIC")) return ElementType::Logarithmic;
    else if (token == QLatin1String("HYPERBOLIC"))  return ElementType::Hyperbolic;
    else if (token == QLatin1String("POLYNOMIAL"))  return ElementType::Polynomial;
    else if (token == QLatin1String("QUINTIC"))     return ElementType::Quintic;
    else if (token == QLatin1String("BIQUADRATIC")) return ElementType::Biquadratic;
    else if (token == QLatin1String("SPLINE"))      return ElementType::Spline;
    else if (token == QLatin1String("BLOSSEULERHYBRID")) return ElementType::BlossEulerHybrid;
    return ElementType::Clothoid;
}

} // namespace

// ============================================================================
//  ctor / usage
// ============================================================================

AlignmentDrawChainCommand::AlignmentDrawChainCommand(QObject* parent)
    : Command("alignmentdrawchain", "Draw Alignment Chain", parent)
{}

QString AlignmentDrawChainCommand::getUsage() const
{
    return "Usage: alignmentdrawchain — 先於對話框設定一段線元，"
           "再連續點選既有線形的起點或終點套用、直接繪出，按右鍵結束";
}

// ============================================================================
//  execute
// ============================================================================

CommandResult AlignmentDrawChainCommand::execute(const CommandContext& context)
{
    Application* app = Application::instance();
    cad::Document* doc = app->documentManager()->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    // 抓取目前正在編輯的 alignment（若有），供後續 resolveExistingPoints()
    // 在對應 TCL 的 rawPoints() 是空的時候（例如只用過 FT/FC）退回使用。
    m_activeAlignmentDoc = context.alignmentDoc;

    const bool hasAnyTcl = !doc->trackCenterLines().isEmpty();
    const bool hasActiveElements =
        m_activeAlignmentDoc && m_activeAlignmentDoc->horizontal()
        && m_activeAlignmentDoc->horizontal()->result()
        && !m_activeAlignmentDoc->horizontal()->result()->isEmpty();
    if (!hasAnyTcl && !hasActiveElements) {
        return CommandResult::Failure(
            "文件內尚無任何軌道中心線可供接續；請先用 ALIGNMENTQUICKTABLE (AQT)、"
            "FT 或 FC 建立至少一個線元。");
    }

    m_parentWidget = static_cast<QWidget*>(context.cadView);
    m_isFinishing  = false;
    m_appliedCount = 0;

    // ── 先顯示對話框、輸入資料、按某區塊的「繪製」關閉對話框 ────────────────
    auto* dlg = new ui::AlignmentDrawElementDialog(m_parentWidget);
    const int dlgResult = dlg->exec();
    if (dlgResult != QDialog::Accepted) {
        dlg->deleteLater();
        return CommandResult::Failure("使用者取消");
    }

    m_isTransitionGroup = dlg->isTransitionGroup();
    if (m_isTransitionGroup) {
        // 需求 20：緩和曲線組只帶著原始參數走，實際幾何要等選到端點、
        // 讀出真實曲率後才算——見 buildTransitionGroupAt()。
        const auto p = dlg->transitionGroupParams();
        m_transFamily           = p.family;
        m_transSpiralInLength   = p.spiralInLength;
        m_transTargetIsInfinite = p.targetIsInfinite;
        m_transTargetRadius     = p.targetRadius;
        m_transCurveLength      = p.curveLength;
        m_transSpiralOutLength  = p.spiralOutLength;
        m_recipe.clear();
    } else {
        m_recipe = dlg->resultPoints();
        if (m_recipe.size() < 3) {   // [0]=佔位基準點 [1]=原點錨點 [2..]=新關鍵點
            dlg->deleteLater();
            return CommandResult::Failure("尚未加入任何線元");
        }
    }
    dlg->deleteLater();

    EventBus* bus = app->eventBus();

    if (auto* uiMgr = app->uiManager()) {
        if (auto* cadView = uiMgr->cadView())
            cadView->setMode(view::InteractionMode::Sketching);
    }

    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       const QVariantMap map = data.toMap();
                       const QPointF pt = map["point"].value<QPointF>();
                       QMetaObject::invokeMethod(this, [this, pt]() {
                           handlePointAcquired(pt);
                       }, Qt::QueuedConnection);
                   });

    bus->subscribe(Events::POINT_CANCELLED, this,
                   [this](const QVariant&) {
                       QMetaObject::invokeMethod(this, [this]() {
                           handleCancelled();
                       }, Qt::QueuedConnection);
                   });

    setState(CommandState::Running);
    bus->publish(Events::COMMAND_PROMPT,
                 tr("ADC — 點選要接續的既有線形（可連續點選套用於多個端點；"
                    "按右鍵結束）："));
    outputMessage("ADC — 線元已設定完成，請點選既有線形靠近起點或終點的"
                  "位置以套用；可連續點選多次，按右鍵結束。");
    return CommandResult::Success("Waiting for input");
}

// ============================================================================
//  handlePointAcquired / handleCancelled
// ============================================================================

void AlignmentDrawChainCommand::handlePointAcquired(const QPointF& point)
{
    QString tclId;
    bool isStart = false;
    if (!findNearestEndpoint(point, tclId, isStart)) {
        outputMessage(tr("找不到任何含關鍵點的線形，請重新點選，或按右鍵結束。"));
        return;
    }
    proceedWithEndpoint(tclId, isStart);
}

void AlignmentDrawChainCommand::handleCancelled()
{
    cleanup();
    if (m_appliedCount > 0) {
        Q_EMIT finished(CommandResult::Success(
            QStringLiteral("ADC 完成，共套用 %1 次").arg(m_appliedCount)));
    } else {
        Q_EMIT finished(CommandResult::Failure("Cancelled"));
    }
}

// ============================================================================
//  resolveExistingPoints
// ============================================================================

QVector<AlignmentPoint>
AlignmentDrawChainCommand::resolveExistingPoints(TrackCenterLine* tcl) const
{
    if (!tcl || !tcl->horizontal())
        return {};

    const QVector<AlignmentPoint>& raw = tcl->horizontal()->rawPoints();
    if (!raw.isEmpty())
        return raw;

    // 權威的 tcl->horizontal() 尚無資料（例如僅用 FT／FC 加入固定切線／
    // 圓弧——兩者都只寫入 context.alignmentDoc，從不呼叫
    // tcl->loadHorizontal()，見標頭說明）時，退回讀取目前作用中
    // AlignmentDocument 的求解結果，但僅限於它確實是這條 TCL 對應的那一份
    // （透過 UIManager::tclAlignmentDocs() 比對，避免誤用到別條線的資料）。
    if (!m_activeAlignmentDoc)
        return {};

    auto* uiMgr = Application::instance()->uiManager();
    if (!uiMgr)
        return {};
    const auto& map = uiMgr->tclAlignmentDocs();
    if (map.value(tcl->id(), nullptr) != m_activeAlignmentDoc)
        return {};

    const railway::HorizontalAlignment* ha = m_activeAlignmentDoc->horizontal()->result();
    return (ha && !ha->isEmpty()) ? ha->rawPoints() : QVector<AlignmentPoint>();
}

// ============================================================================
//  findNearestEndpoint
// ============================================================================

bool AlignmentDrawChainCommand::findNearestEndpoint(const QPointF& worldPos, QString& outTclId,
                                                     bool& outIsStart) const
{
    cad::Document* doc = Application::instance()->documentManager()->currentDocument();
    if (!doc)
        return false;

    double bestDist2 = std::numeric_limits<double>::max();
    QString bestTclId;
    bool bestIsStart = false;
    bool found = false;

    for (TrackCenterLine* tcl : doc->trackCenterLines()) {
        if (!tcl)
            continue;
        const QVector<AlignmentPoint> pts = resolveExistingPoints(tcl);
        if (pts.isEmpty())
            continue;

        const AlignmentPoint& startPt = pts.first();
        const AlignmentPoint& endPt   = pts.last();

        const double dxs = startPt.easting - worldPos.x();
        const double dys = startPt.northing - worldPos.y();
        const double d2s = dxs * dxs + dys * dys;
        if (d2s < bestDist2) {
            bestDist2 = d2s;
            bestTclId = tcl->id();
            bestIsStart = true;
            found = true;
        }

        const double dxe = endPt.easting - worldPos.x();
        const double dye = endPt.northing - worldPos.y();
        const double d2e = dxe * dxe + dye * dye;
        if (d2e < bestDist2) {
            bestDist2 = d2e;
            bestTclId = tcl->id();
            bestIsStart = false;
            found = true;
        }
    }

    if (!found)
        return false;
    outTclId = bestTclId;
    outIsStart = bestIsStart;
    return true;
}

// ============================================================================
//  buildTransitionGroupAt — 需求 20：以真實端點／真實曲率直接計算
// ============================================================================

bool AlignmentDrawChainCommand::buildTransitionGroupAt(
    const AlignmentPoint& start, double curvatureIn,
    QVector<AlignmentPoint>& outAppended, QString& outError) const
{
    const bool startsAtZero = std::abs(curvatureIn) < 1e-9;
    const bool targetIsInfinite = m_transTargetIsInfinite;
    const double curveRadius = m_transTargetRadius;

    if (startsAtZero && targetIsInfinite) {
        outError = tr("所選端點目前在切線上，圓弧半徑不可留白（留白代表直線，"
                       "切線接直線沒有意義，請改用直線區）。");
        return false;
    }
    if (!startsAtZero && !targetIsInfinite && std::abs(curveRadius - curvatureIn) < 1e-6) {
        outError = tr("圓弧半徑與所選端點目前的曲率相同，不需要緩和曲線，"
                       "請改用曲線區延伸。");
        return false;
    }

    const QString family = m_transFamily.isEmpty() ? QStringLiteral("SPIRAL") : m_transFamily;
    const double midCurvature = targetIsInfinite ? 0.0 : curveRadius;
    // 起點在圓弧上、且目標也是另一個（不同的）有限半徑——兩端都不是切線，
    // 是真正的「蛋形」複合曲線，不能用只認識「某一端＝切線」的
    // computeForwardSpiralEndpoint()／computeReversedSpiralEndpoint()。改用
    // railway::EggTransitionElement（與 AlignmentElementFactory::
    // createSpiral() 的 Egg(CC) 分支、AS 指令的 ACA 模式共用同一套已驗證
    // 過的正確公式）直接算出遠端座標／方位角。
    const bool isEggCase = (!startsAtZero && !targetIsInfinite);
    const double spiralInLength  = m_transSpiralInLength;
    const double curveLength     = m_transCurveLength;
    const double spiralOutLength = m_transSpiralOutLength;

    // ── 1. 入緩和曲線：curvatureIn → midCurvature ───────────────────────
    double e1 = 0.0, n1 = 0.0, az1 = 0.0;
    if (startsAtZero) {
        railway::computeForwardSpiralEndpoint(start, midCurvature, spiralInLength, family,
                                               e1, n1, az1);
    } else if (isEggCase) {
        railway::EggTransitionElement egg(curvatureIn, midCurvature, spiralInLength,
                                           eggFamilyFromToken(family));
        egg.setPlacement({start.chainage, start.easting, start.northing, start.azimuth});
        egg.setLength(spiralInLength);
        const QPointF xy = egg.worldXY(start.chainage + spiralInLength);
        e1  = xy.x();
        n1  = xy.y();
        az1 = egg.worldAzimuth(start.chainage + spiralInLength);
    } else {
        railway::computeReversedSpiralEndpoint(start, curvatureIn, spiralInLength, family,
                                                e1, n1, az1);
    }

    AlignmentPoint afterIn;
    afterIn.tsc = QStringLiteral("ST");
    afterIn.easting  = e1;
    afterIn.northing = n1;
    afterIn.azimuth  = az1;
    afterIn.chainage     = start.chainage     + spiralInLength;
    afterIn.contChainage = start.contChainage + spiralInLength;
    afterIn.radius = midCurvature;   // 供 'S' 抵達分支的 curvatureAtPoint() 讀取

    QVector<AlignmentPoint> toAppend;

    AlignmentPoint curEnd = afterIn;
    double curEndCurvature = midCurvature;

    if (curveLength > 0.0) {
        if (!targetIsInfinite) {
            // ── 2a.（可選）中間圓弧，維持同一半徑 midCurvature ──────────
            railway::CircularArcElement arcElem(midCurvature);
            arcElem.setPlacement({afterIn.chainage, afterIn.easting, afterIn.northing, afterIn.azimuth});
            arcElem.setLength(curveLength);
            const QPointF axy = arcElem.worldXY(afterIn.chainage + curveLength);
            const double aaz = arcElem.worldAzimuth(afterIn.chainage + curveLength);

            afterIn.tsc = QStringLiteral("SC");   // 緩和曲線抵達、同時是圓弧起始列
            afterIn.length = curveLength;

            AlignmentPoint afterMid;
            afterMid.tsc = QStringLiteral("CT");
            afterMid.easting  = axy.x();
            afterMid.northing = axy.y();
            afterMid.azimuth  = aaz;
            afterMid.chainage     = afterIn.chainage     + curveLength;
            afterMid.contChainage = afterIn.contChainage + curveLength;

            toAppend.push_back(afterIn);
            curEnd = afterMid;
            curEndCurvature = midCurvature;
        } else {
            // ── 2b.（可選）中間直線段（midCurvature==0）─────────────────
            railway::TangentElement tanElem;
            tanElem.setPlacement({afterIn.chainage, afterIn.easting, afterIn.northing, afterIn.azimuth});
            tanElem.setLength(curveLength);
            const QPointF txy = tanElem.worldXY(afterIn.chainage + curveLength);
            const double taz = tanElem.worldAzimuth(afterIn.chainage + curveLength);

            afterIn.tsc = QStringLiteral("ST");   // 緩和曲線抵達、同時是直線起始列
            afterIn.length = curveLength;

            AlignmentPoint afterMid;
            afterMid.tsc = QStringLiteral("TT");
            afterMid.easting  = txy.x();
            afterMid.northing = txy.y();
            afterMid.azimuth  = taz;
            afterMid.chainage     = afterIn.chainage     + curveLength;
            afterMid.contChainage = afterIn.contChainage + curveLength;

            toAppend.push_back(afterIn);
            curEnd = afterMid;
            curEndCurvature = 0.0;
        }
    }

    if (spiralOutLength > 0.0) {
        // ── 3. 出緩和曲線：curEndCurvature → curvatureIn（回到起點曲率，
        //    邏輯與入緩和曲線對稱，同樣要處理「兩端都非 0」的蛋形情形）──
        double e2 = 0.0, n2 = 0.0, az2 = 0.0;
        if (std::abs(curEndCurvature) < 1e-9) {
            railway::computeForwardSpiralEndpoint(curEnd, curvatureIn, spiralOutLength, family,
                                                   e2, n2, az2);
        } else if (std::abs(curvatureIn) < 1e-9) {
            railway::computeReversedSpiralEndpoint(curEnd, curEndCurvature, spiralOutLength,
                                                    family, e2, n2, az2);
        } else if (std::abs(curvatureIn - curEndCurvature) < 1e-6) {
            outError = tr("出緩和曲線的兩端曲率相同，不需要緩和曲線。");
            return false;
        } else {
            railway::EggTransitionElement egg(curEndCurvature, curvatureIn, spiralOutLength,
                                               eggFamilyFromToken(family));
            egg.setPlacement({curEnd.chainage, curEnd.easting, curEnd.northing, curEnd.azimuth});
            egg.setLength(spiralOutLength);
            const QPointF xy = egg.worldXY(curEnd.chainage + spiralOutLength);
            e2  = xy.x();
            n2  = xy.y();
            az2 = egg.worldAzimuth(curEnd.chainage + spiralOutLength);
        }

        curEnd.tsc = QStringLiteral("?S");   // 第一字元由下方串接迴圈統一修正
        curEnd.length = spiralOutLength;
        curEnd.curveType = family;
        // 注意：curEnd.radius 刻意不清成 0——若中間沒有圓弧／直線段
        // （curveLength==0），curEnd 就是 afterIn 本身，其 radius
        // （=midCurvature）同時也是 curvatureAtPoint() 對「S 抵達」分支的
        // 判讀依據，代表「抵達本點時的曲率」，清掉會讓之後任何從這點接續
        // 的操作誤判曲率狀態。

        AlignmentPoint afterOut;
        afterOut.tsc = QStringLiteral("ST");
        afterOut.easting  = e2;
        afterOut.northing = n2;
        afterOut.azimuth  = az2;
        afterOut.chainage     = curEnd.chainage     + spiralOutLength;
        afterOut.contChainage = curEnd.contChainage + spiralOutLength;

        toAppend.push_back(curEnd);
        curEnd = afterOut;
        curEndCurvature = curvatureIn;   // 回到起點曲率
    }

    toAppend.push_back(curEnd);

    // ── 起點列（真實端點的複本）：更新 tsc[1]/length/radius/curveType ──────
    AlignmentPoint startRow = start;
    {
        QString tsc = startRow.tsc;
        if (tsc.size() < 2) tsc = QStringLiteral("TT");
        tsc[1] = QLatin1Char('S');
        startRow.tsc = tsc;
        startRow.length = spiralInLength;
        startRow.radius = 0.0;   // 緩和曲線起始列半徑恆為 0（既有慣例）
        startRow.curveType = family;
    }

    outAppended.clear();
    outAppended.push_back(startRow);

    // 逐一修正每個新點的抵達字元（tsc[0] = 前一列的 tsc[1]），最後一點強制
    // 收尾字元 'T'。
    for (int i = 0; i < toAppend.size(); ++i) {
        const QChar prevDepart = outAppended.last().tsc.size() >= 2
                                      ? QChar(outAppended.last().tsc[1])
                                      : QChar(QLatin1Char('T'));
        QString tsc = toAppend[i].tsc;
        if (tsc.size() < 2) tsc = QStringLiteral("TT");
        tsc[0] = prevDepart;
        if (i == toAppend.size() - 1)
            tsc[1] = QLatin1Char('T');
        toAppend[i].tsc = tsc;

        outAppended.push_back(toAppend[i]);
    }

    return true;
}

// ============================================================================
//  proceedWithEndpoint
// ============================================================================

void AlignmentDrawChainCommand::proceedWithEndpoint(const QString& tclId, bool extendFromStart)
{
    Application* app = Application::instance();
    cad::Document* doc = app->documentManager()->currentDocument();
    TrackCenterLine* tcl = doc ? doc->findTrackCenterLine(tclId) : nullptr;
    if (!tcl) {
        outputMessage(tr("找不到指定的軌道中心線，請重新點選。"));
        return;
    }

    QVector<AlignmentPoint> existing = resolveExistingPoints(tcl);
    if (existing.isEmpty()) {
        outputMessage(tr("這條線路沒有任何關鍵點資料，請重新點選。"));
        return;
    }

    // 若判定為從「起點」端接續，先整條反轉方向，讓原本的起點變成反轉後
    // 陣列的「終點」，即可直接沿用「從終點接續」的計算邏輯，不需要另外
    // 實作一套反向接續路徑——見 railway::reverseAlignmentPoints() 的說明。
    QVector<AlignmentPoint> workingLocal =
        extendFromStart ? railway::reverseAlignmentPoints(existing) : existing;

    const AlignmentPoint realAnchor = workingLocal.last();

    QVector<AlignmentPoint> finalLocal = workingLocal;
    finalLocal.removeLast();   // 被新算出的接續點（更新過收尾字元）取代

    if (m_isTransitionGroup) {
        // 需求 20：起點曲率「以選取為準」——直接讀出所選端點目前的真實
        // 曲率，不需要（也不可能事先要求）使用者自己宣告，見標頭說明。
        const double realCurvature =
            railway::curvatureAtPoint(workingLocal, workingLocal.size() - 1);
        QVector<AlignmentPoint> appended;
        QString err;
        if (!buildTransitionGroupAt(realAnchor, realCurvature, appended, err)) {
            outputMessage(tr("緩和曲線組套用失敗：%1").arg(err));
            return;
        }
        finalLocal += appended;
    } else {
        // ── 剛體變換：把 m_recipe（原點=0,0，方位角基準=0）套到真實接續點 ──
        QVector<AlignmentPoint> transformed;
        transformed.reserve(m_recipe.size() - 1);
        for (int i = 1; i < m_recipe.size(); ++i)
            transformed.push_back(transformRecipePoint(m_recipe[i], realAnchor));
        finalLocal += transformed;
    }

    // 若前面反轉過，這裡要轉回真正的方向再寫回。
    if (extendFromStart)
        finalLocal = railway::reverseAlignmentPoints(finalLocal);

    if (finalLocal.size() < 2) {
        outputMessage(tr("結果關鍵點不足，已略過本次套用。"));
        return;
    }

    tcl->setAldHorizontalImport(finalLocal);
    tcl->loadHorizontal(finalLocal);
    doc->setModified(true);

    // 單一資料存取：raw points 已更新，讓快取的 AlignmentDocument（供
    // 「線形資料表」／grip 編輯使用）失效，避免與此處剛寫入的資料分岔。
    if (auto* uiMgr = Application::instance()->uiManager()) {
        uiMgr->invalidateTclAlignmentDocument(tclId);
        // 主動確保這條線「立刻」畫出來，不依賴使用者剛好又做了某件會
        // 觸發 renderer 建立／刷新的事（例如打開線形資料表）——見
        // UIManager::ensureTclDisplayed() 的完整說明。
        uiMgr->ensureTclDisplayed(tclId);
    }
    if (m_activeAlignmentDoc) {
        // 剛剛寫回的這條 TCL 若正好是目前作用中的那一份，它已被
        // invalidateTclAlignmentDocument() 刪除，不能再繼續使用。
        auto* uiMgr = Application::instance()->uiManager();
        if (!uiMgr || !uiMgr->tclAlignmentDocs().values().contains(m_activeAlignmentDoc))
            m_activeAlignmentDoc = nullptr;
    }

    ++m_appliedCount;

    const QString summary = QStringLiteral("接續繪製完成: %1（%2端接續，共 %3 個關鍵點）")
                                 .arg(tcl->name())
                                 .arg(extendFromStart ? tr("起點") : tr("終點"))
                                 .arg(finalLocal.size());
    outputMessage(QStringLiteral("[接續繪製線形] %1").arg(summary));

    Application::instance()->eventBus()->publish(
        Events::COMMAND_PROMPT,
        tr("ADC — 已套用（共 %1 次）。可繼續點選其他端點，或按右鍵結束：")
            .arg(m_appliedCount));
}

// ============================================================================
//  cleanup
// ============================================================================

void AlignmentDrawChainCommand::cleanup()
{
    EventBus* bus = Application::instance()->eventBus();
    bus->unsubscribeAll(this);

    if (auto* uiMgr = Application::instance()->uiManager()) {
        if (auto* cadView = uiMgr->cadView())
            cadView->setMode(view::InteractionMode::Sketching);
    }

    m_parentWidget = nullptr;
    m_isFinishing  = false;
    m_activeAlignmentDoc = nullptr;
    m_recipe.clear();
    m_isTransitionGroup = false;
    m_transFamily.clear();
    m_transSpiralInLength = 0.0;
    m_transTargetIsInfinite = true;
    m_transTargetRadius = 0.0;
    m_transCurveLength = 0.0;
    m_transSpiralOutLength = 0.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Static registration — 指令 "alignmentdrawchain" 觸發 AlignmentDrawChainCommand
// ─────────────────────────────────────────────────────────────────────────────
REGISTER_COMMAND("alignmentdrawchain", AlignmentDrawChainCommand);

} // namespace command
} // namespace aicad
