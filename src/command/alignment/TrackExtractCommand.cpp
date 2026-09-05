/**
 * @file TrackExtractCommand.cpp
 * @brief 見 TrackExtractCommand.h 檔頭說明。
 */
#include "command/alignment/TrackExtractCommand.h"
#include "command/CommandFactory.h"

#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "core/DocumentManager.h"
#include "core/EventBus.h"
#include "cad/Document.h"
#include "railway/AlignmentDocument.h"
#include "ui/UIManager.h"
#include "ui/TrackNameSequenceDialog.h"
#include "view/CadView.h"   // CadView : public QWidget — 供 TrackNameSequenceDialog 取用 parent

#include <QDebug>
#include <QDialog>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QSignalBlocker>
#include <algorithm>
#include <limits>

using namespace aicad::core;

namespace aicad {
namespace command {

namespace {

// ─────────────────────────────────────────────────────────────────────────
// 子範圍擷取輔助函式
// ─────────────────────────────────────────────────────────────────────────

/** 將一段 AlignmentPoint 的 chainage/contChainage 平移，使第一點的里程為 0。 */
QVector<railway::AlignmentPoint> rebaseChainage(QVector<railway::AlignmentPoint> pts)
{
    if (pts.isEmpty()) return pts;
    const double p0  = pts.first().chainage;
    const double cp0 = pts.first().contChainage;
    for (auto& pt : pts) {
        pt.chainage     -= p0;
        pt.contChainage -= cp0;
    }
    return pts;
}

/**
 * @brief 依里程範圍 [pStart, pEnd]（原始里程座標）從既有 VerticalAlignment
 *        切出子範圍；邊界若未剛好落在既有記錄上，以 getElevation()/getSlope()
 *        內插合成邊界點（見 TrackExtractCommand.h 檔頭「設計取捨」說明）。
 *
 * @param rebase true 時將結果里程平移至從 0 開始（供新 TCL 使用）；
 *               false 時保留原始里程（供原線頭段使用）。
 */
QVector<railway::VerticalAlignmentPoint> sliceVertical(
    const railway::VerticalAlignment* src,
    double pStart, double pEnd, bool rebase)
{
    QVector<railway::VerticalAlignmentPoint> out;
    if (!src || src->isEmpty() || pEnd <= pStart) return out;

    const double kEps = 1e-6;

    railway::VerticalAlignmentPoint startPt;
    startPt.chainage  = pStart;
    startPt.elevation = src->getElevation(pStart);
    startPt.grade     = src->getSlope(pStart);
    out.append(startPt);

    for (const auto& pt : src->points()) {
        if (pt.chainage > pStart + kEps && pt.chainage < pEnd - kEps)
            out.append(pt);
    }

    railway::VerticalAlignmentPoint endPt;
    endPt.chainage  = pEnd;
    endPt.elevation = src->getElevation(pEnd);
    endPt.grade     = src->getSlope(pEnd);
    out.append(endPt);

    if (rebase) {
        const double p0 = out.first().chainage;
        for (auto& pt : out) pt.chainage -= p0;
    }
    return out;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// Constructor / getUsage
// ─────────────────────────────────────────────────────────────────────────

TrackExtractCommand::TrackExtractCommand(QObject* parent)
    : Command("TRACKEXTRACT",
              "Split a track centerline at a picked TT (tangent-tangent) "
              "point, or type A/AUTO to split at every internal TT point "
              "(alias: TX)",
              parent)
{
}

QString TrackExtractCommand::getUsage() const
{
    return "Usage: TRACKEXTRACT — in Alignment edit mode, click near a TT "
           "point (an internal joint where two Tangents meet directly, "
           "excluding the line's own start/end). The click snaps to the "
           "nearest such TT point. The original line keeps everything up "
           "to and including that point; everything with greater chainage "
           "moves into a single new TrackCenterLine. Type A (or AUTO) "
           "instead of clicking to split at every internal TT point, "
           "lowest chainage first, producing one new TrackCenterLine per cut.";
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult TrackExtractCommand::execute(const CommandContext& context)
{
    auto* cmdMgr = CommandLineManager::instance();

    railway::AlignmentDocument* alignDoc = context.alignmentDoc;
    if (!alignDoc) {
        if (cmdMgr) cmdMgr->printError("尚未進入 Alignment 編輯模式。");
        return CommandResult::Failure("Not in alignment edit mode.");
    }

    m_doc = Application::instance()->documentManager()->currentDocument();
    if (!m_doc) {
        if (cmdMgr) cmdMgr->printError("No active document.");
        return CommandResult::Failure("No active document.");
    }

    // AlignmentDocument::activeTclId()/setActiveTclId() 目前在整個專案中從未
    // 被任何呼叫端設定過（永遠是空字串），不能拿來反查 TCL。真正維護
    // 「tclId ↔ AlignmentDocument*」對應關係的是 UIManager::tclAlignmentDocs()
    // （每條 TCL 各自一份 AlignmentDocument），故改以 context.alignmentDoc
    // 反查其所屬的 tclId。
    if (!context.uiManager) {
        if (cmdMgr) cmdMgr->printError("內部錯誤：CommandContext 缺少 UIManager。");
        return CommandResult::Failure("Missing UIManager in context.");
    }

    QString tclId;
    const auto& tclDocs = context.uiManager->tclAlignmentDocs();
    for (auto it = tclDocs.constBegin(); it != tclDocs.constEnd(); ++it) {
        if (it.value() == alignDoc) { tclId = it.key(); break; }
    }
    if (tclId.isEmpty()) {
        if (cmdMgr) cmdMgr->printError("找不到目前編輯中的 TrackCenterLine（AlignmentDocument 未對應到任何 TCL id）。");
        return CommandResult::Failure("Active AlignmentDocument has no associated TCL id.");
    }

    m_tcl = m_doc->findTrackCenterLine(tclId);
    if (!m_tcl) {
        if (cmdMgr) cmdMgr->printError("找不到目前編輯中的 TrackCenterLine。");
        return CommandResult::Failure("No active TrackCenterLine.");
    }

    m_alignDoc     = alignDoc;
    m_uiManager    = context.uiManager;
    m_parentWidget = static_cast<QWidget*>(context.cadView);   // CadView : public QWidget

    m_useNameSequence = false;   // 每次 execute() 重新開始，避免沿用上一輪殘留設定

    if (!m_tcl->horizontal() || m_tcl->horizontal()->count() < 3) {
        // 至少需要 3 個關鍵點：頭段（切點以前，>=2 點）與新線
        // （切點以後直到終點，>=2 點）才都能構成有效線形；且切點必須是
        // 「內部」TT（見 nearestInternalTTIndex），本來就排除第 0 與最後一筆。
        if (cmdMgr) cmdMgr->printWarning("⚠️  線形關鍵點數量不足，無法切分。");
        return CommandResult::Failure("Not enough keypoints to split.");
    }

    // 若整條線根本沒有內部 TT 接合點（例如只有一段 Tangent + 一個曲線），
    // 直接提示、不進入互動點選，避免使用者點了半天卻永遠吸附失敗。
    bool hasInternalTT = false;
    {
        const auto& pts = m_tcl->horizontal()->rawPoints();
        for (int i = 1; i < pts.size() - 1; ++i) {
            if (pts[i].tsc == "TT") { hasInternalTT = true; break; }
        }
    }
    if (!hasInternalTT) {
        if (cmdMgr) cmdMgr->printWarning(
            "⚠️  此線形沒有內部 TT（兩條 Tangent 相接）接合點，無法切分。");
        return CommandResult::Failure("No internal TT point to split at.");
    }

    m_isFinishing = false;

    EventBus* bus = Application::instance()->eventBus();

    QVariantMap viewSetup;
    viewSetup["mode"]           = "sketching";
    viewSetup["rubberBandMode"] = "none";
    bus->publish("command.request-view-setup", viewSetup);

    // TRACKEXTRACT 只需要「點一下取得座標」的 OSnap 點選能力
    // （POINT_ACQUIRED），不需要 grip 拖曳編輯，所以在切到 sketching 模式
    // 之後，另外多發一次 {"gripsEnabled": false} 的請求把 grips 關掉（見
    // UIManager.cpp 對 command.request-view-setup 新增的 gripsEnabled
    // 覆寫邏輯）。用獨立的第二次 publish() 確保無論兩個訂閱者
    // （ViewManager／UIManager）的執行順序為何，grips 最終一定會被關掉。
    {
        QVariantMap noGrips;
        noGrips["gripsEnabled"] = false;
        bus->publish("command.request-view-setup", noGrips);
    }

    bus->subscribe(Events::POINT_ACQUIRED, this,
        [this](const QVariant& data) {
            QVariantMap map = data.toMap();
            QPointF pt = map["point"].value<QPointF>();
            QMetaObject::invokeMethod(this, [this, pt]() {
                handlePointAcquired(pt);
            }, Qt::QueuedConnection);
        });

    bus->subscribe(Events::STRING_INPUT, this,
        [this](const QVariant& data) {
            const QString text = data.toString();
            QMetaObject::invokeMethod(this, [this, text]() {
                handleStringInput(text);
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
                 tr("TRACKEXTRACT — 點選切斷點（TT，兩條 Tangent 相接處），"
                    "或輸入 A 執行自動全部切分："));
    outputMessage("TRACKEXTRACT — Specify the TT split point "
                  "(snaps to nearest internal TT joint), or type A for Auto:");

    // 關鍵：沒有這行，鍵入的文字不會被路由到上面訂閱的 STRING_INPUT，
    // 而是被 CommandLineManager 當成「使用者輸入了一個新指令」直接執行
    // （例如輸入 A 會被解析成別名，執行成 ARC 指令，把 TRACKEXTRACT 打斷）。
    // 比照 RotateCommand::beginAngleStage()／ProfileArrayCommand 對
    // STRING_INPUT 的既有用法：呼叫 waitForInput() 才會讓命令列把接下來的
    // 文字輸入視為「回覆目前這個命令的提示」而不是新指令；且這個等待狀態
    // 是一次性的，每次要繼續等待文字輸入都必須重新呼叫（見下方兩處
    // "stay waiting" 分支）。呼叫這個不影響同時透過 POINT_ACQUIRED 接收
    // 滑鼠點選（ROTATE 的 WaitAngle 階段也是點選／打字兩種輸入同時有效）。
    if (cmdMgr) cmdMgr->waitForInput(core::InputType::String);

    return CommandResult::Success("Waiting for input");
}

// ─────────────────────────────────────────────────────────────────────────
// handlePointAcquired
// ─────────────────────────────────────────────────────────────────────────

int TrackExtractCommand::nearestInternalTTIndex(
    const QVector<railway::AlignmentPoint>& pts, const QPointF& worldPt) const
{
    int best = -1;
    double bestDist2 = std::numeric_limits<double>::max();
    // i 從 1 到 size-2：排除線形本身的起點（i=0）與終點（i=size-1），
    // 只考慮「內部」TT 接合點（見 TrackExtractCommand.h 檔頭說明）。
    for (int i = 1; i < pts.size() - 1; ++i) {
        if (pts[i].tsc != "TT") continue;
        const double dx = pts[i].easting  - worldPt.x();
        const double dy = pts[i].northing - worldPt.y();
        const double d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) { bestDist2 = d2; best = i; }
    }
    return best;
}

void TrackExtractCommand::handlePointAcquired(const QPointF& point)
{
    if (m_isFinishing || !m_tcl || !m_tcl->horizontal()) return;

    EventBus* bus = Application::instance()->eventBus();
    const QVector<railway::AlignmentPoint> pts = m_tcl->horizontal()->rawPoints();

    const int cutIdx = nearestInternalTTIndex(pts, point);
    if (cutIdx < 0) {
        // 理論上 execute() 已檢查過整條線至少有一個內部 TT，這裡不應該發生；
        // 仍加上防呆訊息並保持等待狀態，讓使用者可以重新點選或取消。
        outputMessage("Error: no internal TT joint found near that point "
                      "— try clicking closer to a TT junction.");
        bus->publish(Events::COMMAND_PROMPT,
                     tr("該處附近找不到內部 TT 接合點，請重新點選："));
        // 見 execute() 的說明：waitForInput() 是一次性的，繼續等待文字輸入
        // （例如使用者改打 A）也要重新呼叫，否則第一次點擊落空後，接下來
        // 打字就會又被當成新指令執行。
        if (auto* cmdMgr = CommandLineManager::instance())
            cmdMgr->waitForInput(core::InputType::String);
        return;
    }

    const bool ok = performExtractSplit(cutIdx);

    m_isFinishing = true;
    Q_EMIT finished(ok ? CommandResult::Success("TRACKEXTRACT completed")
                        : CommandResult::Failure("TRACKEXTRACT failed"));
}

void TrackExtractCommand::handleStringInput(const QString& text)
{
    if (m_isFinishing) return;

    const QString kw = text.trimmed().toUpper();
    if (kw != "A" && kw != "AUTO") {
        outputMessage(QString("Error: unrecognized option \"%1\" — click a "
                              "TT point, or type A for Auto.").arg(text));
        Application::instance()->eventBus()->publish(
            Events::COMMAND_PROMPT,
            tr("無法辨識的輸入「%1」— 請點選 TT 切點，或輸入 A 執行自動全部切分：")
                .arg(text));
        // waitForInput() 是一次性的（見 execute() 的說明），輸入無效後要
        // 重新呼叫才能繼續把後續文字路由到這裡，否則下一次打字又會被
        // CommandLineManager 當成新指令執行。
        if (auto* cmdMgr = CommandLineManager::instance())
            cmdMgr->waitForInput(core::InputType::String);
        return; // 保持等待狀態
    }

    // 輸入 A／AUTO 後：接下來全自動切分全部內部 TT，不會再有互動點選
    // （不需要 POINT_ACQUIRED），execute() 一開始為了「讓使用者在畫面上
    // 點選 TT 切點」而開啟的 sketching 疊加層也就不再需要了，切回 idle
    // 模式順便關掉 InputJig 等疊加層（grips 在 execute() 一開始就已經被
    // 明確關掉，見該處說明）。
    {
        QVariantMap idleSetup;
        idleSetup["mode"] = "idle";
        Application::instance()->eventBus()->publish("command.request-view-setup", idleSetup);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    // 輸入 A／AUTO 後先彈出命名規則對話框（見 TrackExtractCommand.h 檔頭
    // 「Auto 模式命名規則」說明），讓使用者設定字首／開始序號／字尾／
    // 遞增遞減，套用到全部切分結果段（含原線保留的頭段）。取消對話框視為
    // 取消整個 TX 指令，不執行任何切分。
    ui::TrackNameSequenceDialog nameDlg(m_parentWidget);
    const int dlgResult = nameDlg.exec();
    if (dlgResult != QDialog::Accepted) {
        outputMessage("TRACKEXTRACT cancelled (naming dialog dismissed).");
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Success("TRACKEXTRACT cancelled"));
        return;
    }
    m_useNameSequence  = true;
    m_nameSeqPrefix    = nameDlg.prefix();
    m_nameSeqStart     = nameDlg.startSeq();
    m_nameSeqSuffix    = nameDlg.suffix();
    m_nameSeqIncrement = nameDlg.isIncrement();

    const bool ok = performAutoSplitAll();

    m_isFinishing = true;
    Q_EMIT finished(ok ? CommandResult::Success("TRACKEXTRACT Auto completed")
                        : CommandResult::Failure("TRACKEXTRACT Auto failed"));
}

// ─────────────────────────────────────────────────────────────────────────
// performExtractSplit
// ─────────────────────────────────────────────────────────────────────────

void TrackExtractCommand::reseedAlignmentEdit(
    railway::AlignmentDocument* alignDoc,
    const QVector<railway::AlignmentPoint>& hPts,
    const QVector<railway::VerticalAlignmentPoint>& vPts)
{
    if (!alignDoc) return;

    // seedFromRawPoints()/seedFromDensePoints() 都會在鏈已有資料時直接
    // 回傳 false、拒絕覆蓋（見 AlignmentDocument.h 檔頭說明），所以要先
    // 清空既有元素／VIP，才能用新的關鍵點重建，讓存檔時 "alignmentEdit"
    // 區塊與 "horizontal.rawPoints" 保持一致。
    if (auto* hEdit = alignDoc->horizontal()) {
        while (!hEdit->elements().isEmpty())
            hEdit->removeElement(hEdit->elements().size() - 1);
        hEdit->seedFromRawPoints(hPts);   // 內部已呼叫 solve()
    }

    if (auto* vEdit = alignDoc->vertical()) {
        while (vEdit->vipCount() > 0)
            vEdit->removeVip(vEdit->vipCount() - 1);
        if (vPts.size() >= 2)
            vEdit->seedFromDensePoints(vPts);   // 內部已呼叫 solve()
    }
}

railway::TrackCenterLine* TrackExtractCommand::splitTclAt(
    railway::TrackCenterLine* tcl, railway::AlignmentDocument* tclAlignDoc, int cutIdx,
    const QString& newTclName)
{
    if (!tcl || !m_doc || !tcl->horizontal()) return nullptr;

    const QVector<railway::AlignmentPoint> pts = tcl->horizontal()->rawPoints();
    if (cutIdx < 1 || cutIdx > pts.size() - 2) return nullptr;

    const double pCut = pts[cutIdx].chainage;

    QVector<railway::AlignmentPoint> headPts, newPts;
    for (int i = 0; i <= cutIdx; ++i) headPts.append(pts[i]);
    for (int i = cutIdx; i < pts.size(); ++i) newPts.append(pts[i]);

    newPts = rebaseChainage(newPts);
    // headPts 維持原線里程（原線頭段不重新編號）。

    QVector<railway::VerticalAlignmentPoint> headVPts, newVPts;
    const railway::VerticalAlignment* vsrc = tcl->vertical();
    if (vsrc && !vsrc->isEmpty()) {
        const double vFirst = vsrc->points().first().chainage;
        const double vLast  = vsrc->points().last().chainage;
        headVPts = sliceVertical(vsrc, vFirst, pCut, /*rebase=*/false);
        newVPts  = sliceVertical(vsrc, pCut,   vLast, /*rebase=*/true);
    }

    // ── tcl → 頭段（烘焙點 + 對應的可編輯元素鏈皆同步更新） ────────────────
    tcl->loadHorizontal(headPts);
    if (headVPts.size() >= 2) tcl->loadVertical(headVPts);
    reseedAlignmentEdit(tclAlignDoc, headPts, headVPts);

    // ── 新建：切點之後（含）全部合併為一條新 TrackCenterLine ───────────────
    // newTclName 為空時交給 Document::addTrackCenterLine() 依既有規則自動
    // 命名（單點互動切分／未啟用 Auto 命名規則時走此路徑）。
    railway::TrackCenterLine* newTcl = m_doc->addTrackCenterLine(newTclName);
    if (newTcl) {
        newTcl->loadHorizontal(newPts);
        if (newVPts.size() >= 2) newTcl->loadVertical(newVPts);

        // 新 TCL 也要有自己的 AlignmentDocument 才能存檔時寫出一致的
        // "alignmentEdit" 區塊；沿用 UIManager 既有的「取得或建立」入口。
        if (m_uiManager) {
            railway::AlignmentDocument* newAlignDoc =
                m_uiManager->ensureTclAlignmentDocument(newTcl->id());
            reseedAlignmentEdit(newAlignDoc, newPts, newVPts);
        }
    }

    return newTcl;
}

bool TrackExtractCommand::performExtractSplit(int cutIdx)
{
    railway::TrackCenterLine* newTcl = splitTclAt(m_tcl, m_alignDoc, cutIdx);

    auto* cmdMgr = CommandLineManager::instance();
    if (cmdMgr) {
        if (newTcl) {
            // splitTclAt() 執行後 m_tcl 已原地縮成頭段，其 rawPoints() 的
            // 最後一筆正是切點本身，故此處的終點里程就是切點里程。
            const auto& headPts = m_tcl->horizontal()->rawPoints();
            const double pCut = headPts.isEmpty() ? 0.0 : headPts.last().chainage;
            cmdMgr->printSuccess(
                QString("✅ 已在 TT 關鍵點 #%1 切分（里程 %2 m）：原線保留頭段，"
                        "其後全部移至新中心線「%3」。")
                    .arg(cutIdx)
                    .arg(pCut, 0, 'f', 3)
                    .arg(newTcl->name()));
        } else {
            cmdMgr->printWarning("⚠️  新中心線建立失敗，請檢查 Document 狀態。");
        }
    }

    return newTcl != nullptr;
}

bool TrackExtractCommand::performAutoSplitAll()
{
    if (!m_tcl || !m_doc) return false;

    railway::TrackCenterLine*   current      = m_tcl;
    railway::AlignmentDocument* currentAlign = m_alignDoc;
    int segmentCount = 1;

    // 命名規則（見 TrackNameSequenceDialog）：套用到全部結果段，含原線保留
    // 的頭段本身——所以在切分迴圈開始之前就先把 m_tcl（頭段）重新命名為
    // 序列中的第一個名稱，之後每切一刀，新段依序取下一個名稱。
    int nameStepIdx = 0;
    if (m_useNameSequence && current) {
        current->setName(ui::TrackNameSequenceDialog::formatName(
            m_nameSeqPrefix, m_nameSeqStart, m_nameSeqSuffix,
            m_nameSeqIncrement, nameStepIdx));
        ++nameStepIdx;
    }

    // ── 效能／回應性 ─────────────────────────────────────────────────────
    // 原本每切一刀，Document::addTrackCenterLine() 都會立刻 emit
    // trackCenterLinesChanged()／treeStructureChanged()，分別觸發
    // FeatureBrowser 整個特徵樹「清空重建」與
    // UIManager::refreshAlignmentOSnapSources()「掃描全部 TCL 重建 OSnap
    // 來源」——這兩個重建單獨呼叫一次很快，但內部 TT 較多、逐一切成十幾段
    // 時，在同一個事件迴圈疊代裡連續觸發十幾次，總耗時累加起來容易造成
    // 明顯延遲。
    //
    // 修法兩部分：
    //   1. 迴圈期間用 QSignalBlocker 暫時封鎖 m_doc 的訊號，避免每次
    //      addTrackCenterLine() 都觸發一次全樹重建／全域 OSnap 重掃；
    //      迴圈結束後再手動補發一次，讓這兩個重建只做「這一次」。
    //      （findTrackCenterLine()／ensureTclAlignmentDocument() 都是直接
    //      查 m_doc 內部的 TCL 清單，不依賴訊號送達，封鎖訊號不影響迴圈
    //      內後續切分邏輯的正確性。）
    //   2. 每切一刀後主動呼叫 processEvents()，讓事件迴圈仍有機會抽出
    //      訊息（處理輸入、重繪視窗），避免作業系統誤判整個程式沒有回應。
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    {
        QSignalBlocker docBlocker(m_doc);

        // 每一輪都在「目前這段」剩餘的 rawPoints() 裡找里程最小（即第一個）
        // 的內部 TT 切一刀；切下去的後半段變成新的 current，繼續找下一個
        // 內部 TT，直到某一段已經沒有內部 TT 為止——等同於依里程由少往多，
        // 在所有內部 TT 處逐一切分。
        while (current && current->horizontal()) {
            const QVector<railway::AlignmentPoint> pts = current->horizontal()->rawPoints();
            int cutIdx = -1;
            for (int i = 1; i < pts.size() - 1; ++i) {
                if (pts[i].tsc == "TT") { cutIdx = i; break; }
            }
            if (cutIdx < 0) break; // 這段已無內部 TT，結束

            const QString newName = m_useNameSequence
                ? ui::TrackNameSequenceDialog::formatName(m_nameSeqPrefix, m_nameSeqStart,
                                                       m_nameSeqSuffix, m_nameSeqIncrement,
                                                       nameStepIdx)
                : QString();
            railway::TrackCenterLine* newTcl = splitTclAt(current, currentAlign, cutIdx, newName);
            if (!newTcl) break;

            if (m_useNameSequence) ++nameStepIdx;
            ++segmentCount;
            current      = newTcl;
            currentAlign = m_uiManager ? m_uiManager->ensureTclAlignmentDocument(newTcl->id())
                                        : nullptr;

            // 讓事件迴圈有機會抽出訊息，避免長串切分時系統誤判程式沒有回應。
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    } // QSignalBlocker 解除封鎖（離開作用域時自動還原）

    // 迴圈期間所有 trackCenterLinesChanged()/treeStructureChanged() 都被
    // 封鎖住了，這裡補發一次，讓 FeatureBrowser／OSnap 來源／3D 視圖依最終
    // 結果只重建一次，而不是每切一刀都重建一次。
    if (segmentCount > 1) {
        Q_EMIT m_doc->trackCenterLinesChanged();
        Q_EMIT m_doc->treeStructureChanged();
    }
    QGuiApplication::restoreOverrideCursor();

    auto* cmdMgr = CommandLineManager::instance();
    if (cmdMgr) {
        if (segmentCount > 1) {
            cmdMgr->printSuccess(
                QString("✅ Auto：已依所有內部 TT（依里程由少往多）切分為 %1 段線形。")
                    .arg(segmentCount));
        } else {
            cmdMgr->printWarning("⚠️  此線形沒有內部 TT 接合點，未執行任何切分。");
        }
    }

    return segmentCount > 1;
}

// ─────────────────────────────────────────────────────────────────────────
// handleCancelled / cleanup
// ─────────────────────────────────────────────────────────────────────────

void TrackExtractCommand::handleCancelled()
{
    qDebug() << "[TRACKEXTRACT] Cancelled";
    outputMessage("TRACKEXTRACT cancelled.");
    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("TRACKEXTRACT cancelled"));
}

void TrackExtractCommand::cleanup()
{
    EventBus* bus = Application::instance()->eventBus();
    bus->unsubscribeAll(this);

    QVariantMap rb;
    rb["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", rb);

    m_isFinishing = false;
    m_tcl       = nullptr;
    m_doc       = nullptr;
    m_alignDoc  = nullptr;
    m_uiManager = nullptr;
}

} // namespace command
} // namespace aicad

// ─────────────────────────────────────────────────────────────────────────
// Static registration
//
// registerCommandsFromMenu()（core/Application.cpp）以 menu.txt 中
// `command|trackextract|tx|0` 這行的第一欄（原樣、小寫）當作 cmdDef.id，
// 並用它呼叫 CommandFactory::create(cmdDef.id) —— 與使用者在命令列實際輸入
// 的文字（大小寫、別名）無關，該 lambda 在建立時就已經把 cmdDef.id 綁死。
// 因此這裡必須以「與 menu.txt 完全相同的原樣字串」("trackextract") 註冊，
// 大寫 "TRACKEXTRACT" 對 QMap 而言是不同的 key，會查無此鍵。
// ─────────────────────────────────────────────────────────────────────────
static void registerTrackExtractCommand()
{
    using namespace aicad::command;
    auto creator = []() -> Command* { return new TrackExtractCommand(); };
    CommandFactory::registerCreator("trackextract", creator);
    CommandFactory::registerCreator("TRACKEXTRACT", creator);
}
Q_CONSTRUCTOR_FUNCTION(registerTrackExtractCommand)
