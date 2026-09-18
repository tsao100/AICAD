/**
 * @file AlignmentQuickTableCommand.cpp
 * @brief Implementation of ALIGNMENTQUICKTABLE — see the header for design.
 */
#include "command/alignment/AlignmentQuickTableCommand.h"

#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/CommandLineManager.h"
#include "core/EventBus.h"
#include "core/geometry/ProjectOrigin.h"
#include "cad/Document.h"
#include "railway/AlignmentDocument.h"
#include "railway/RailwayAlignment.h"
#include "ui/AlignmentQuickTableDialog.h"
#include "ui/UIManager.h"
#include "view/CadView.h"   // CadView : public QWidget — 供 static_cast(context.cadView) 使用

#include <QVariantMap>
#include <QDebug>

namespace aicad {
namespace command {

using core::Application;
using core::geometry::ProjectOrigin;
using railway::TrackCenterLine;

namespace {

/** 找出既有同名 TrackCenterLine（比照 ImportAldCommand：重複名稱就地更新，
 *  而非產生重複線路）。僅供「新增」模式（無 tclId）使用；「編輯既有」模式
 *  一律直接以 tclId 精確定位，不經過名稱比對。 */
TrackCenterLine* findByName(cad::Document* doc, const QString& name)
{
    if (name.isEmpty())
        return nullptr;
    for (TrackCenterLine* tcl : doc->trackCenterLines()) {
        if (tcl && tcl->name() == name)
            return tcl;
    }
    return nullptr;
}

/**
 * @brief 取得既有關鍵點序列（Local 座標）。
 *
 * 刻意直接讀 tcl->horizontal()->rawPoints()，不透過
 * ensureTclAlignmentDocument()+solve() 改讀「已求解的 EditableElement 鏈
 * 結果」──先前版本試過這個做法（原意是想讓 AQT／ADC 與「線形資料表」看到
 * 同一份、更完整的資料），但 seedFromRawPoints()→solve() 這條路徑本身是
 * 一套複雜的模式比對／重建邏輯（處理 SCS 群組、Egg 複合曲線、反向緩和
 * 曲線……），對於某些點序列可能會合併／省略中間關鍵點（例如「線形資料表」
 * 自己的顯示層就有一段「SS 交會點合併」的後處理），一旦把這樣重建過的
 * 序列當成 ADC 的「既有資料」再寫回 tcl->loadHorizontal()，等於把這個
 * 簡化／合併的版本永久取代掉原始資料，實際測試會看到某些緩和曲線消失。
 * 兩害相權，仍以「不遺失資料」為最高原則，退回直接讀 rawPoints()。
 *
 * 代價：若某條線路的線形元素只存在於尚未同步回 TCL 的 EditableElement
 * 鏈（例如剛用互動式指令加了一個 Fixed 元素、還沒觸發同步），這裡會讀到
 * 空／不完整的資料。這種情況目前請先透過「線形資料表」操作一次（會觸發
 * 同步）再使用 AQT／ADC。
 */
QVector<railway::AlignmentPoint> existingRawPoints(TrackCenterLine* tcl)
{
    return (tcl && tcl->horizontal()) ? tcl->horizontal()->rawPoints()
                                       : QVector<railway::AlignmentPoint>();
}

/// Local（OCCT）→ TM2（Global），就地轉換整個序列（供「編輯既有線形」模式
/// 把既有關鍵點（Local）轉成對話框顯示用的 TM2）。
void convertLocalToTM2(QVector<railway::AlignmentPoint>& pts)
{
    ProjectOrigin::ensureDefault();
    auto& origin = ProjectOrigin::instance();
    for (railway::AlignmentPoint& pt : pts) {
        const QPointF global = origin.toGlobal(pt.easting, pt.northing);
        pt.easting  = global.x();
        pt.northing = global.y();
    }
}

/// TM2（Global）→ Local（OCCT），就地轉換整個序列（比照 ImportAldCommand
/// 既定慣例）——寫入 TrackCenterLine 前的必要邊界轉換，見 ProjectOrigin.h。
void convertTM2ToLocal(QVector<railway::AlignmentPoint>& pts)
{
    ProjectOrigin::ensureDefault();
    auto& origin = ProjectOrigin::instance();
    for (railway::AlignmentPoint& pt : pts) {
        const QPointF local = origin.toLocal(pt.easting, pt.northing);
        pt.easting  = local.x();
        pt.northing = local.y();
    }
}

} // namespace

// ============================================================================
//  ctor
// ============================================================================

AlignmentQuickTableCommand::AlignmentQuickTableCommand()
    : Command("alignmentquicktable", "Quick Alignment Table")
{}

QString AlignmentQuickTableCommand::getUsage() const
{
    return "Usage: alignmentquicktable — 以部分關鍵點資料表格輸入，正算出完整"
           "平面線形並建立（或編輯既有的）TrackCenterLine";
}

// ============================================================================
//  execute
// ============================================================================

CommandResult AlignmentQuickTableCommand::execute(const CommandContext& context)
{
    Application* app = Application::instance();
    core::DocumentManager* docMgr = app->documentManager();
    core::CommandLineManager* clm = app->commandLineManager();

    cad::Document* doc = docMgr->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    // ── 0. 「編輯既有線形」模式偵測 ────────────────────────────────────────
    // 由 FeatureBrowser「以 AQT 編輯」選單動作經 UIManager 帶入
    // context.data["tclId"]（比照既有「線形資料表」tclId 慣例）；未帶
    // tclId 則為「新增」模式。
    const QString editTclId = context.data.value("tclId").toString();
    TrackCenterLine* editTarget = editTclId.isEmpty() ? nullptr
                                                       : doc->findTrackCenterLine(editTclId);
    if (!editTclId.isEmpty() && !editTarget)
        return CommandResult::Failure(
            QStringLiteral("找不到指定的軌道中心線 (id=%1)").arg(editTclId));

    // ── 1. 開啟對話框（模態），使用者輸入＋計算 ────────────────────────────
    QWidget* parentWidget = static_cast<QWidget*>(context.cadView);
    ui::AlignmentQuickTableDialog* dlg = nullptr;

    if (editTarget) {
        // 既有的關鍵點是 Local（OCCT）座標；對話框全程停留在 TM2，見
        // AlignmentQuickTableDialog.h 頭註「座標單位／座標系統」。
        QVector<railway::AlignmentPoint> existingTM2 = existingRawPoints(editTarget);
        convertLocalToTM2(existingTM2);
        dlg = new ui::AlignmentQuickTableDialog(existingTM2, editTarget->name(), parentWidget);
    } else {
        dlg = new ui::AlignmentQuickTableDialog(parentWidget);
    }

    const int result = dlg->exec();
    if (result != QDialog::Accepted) {
        dlg->deleteLater();
        return CommandResult::Failure("使用者取消");
    }

    QVector<railway::AlignmentPoint> pts = dlg->resultPoints();   // TM2 座標
    const QString requestedName = dlg->trackName();
    dlg->deleteLater();

    if (pts.size() < 2)
        return CommandResult::Failure("計算結果關鍵點不足");

    // ── 2. TM2 → Local（比照 ImportAldCommand，見 ProjectOrigin.h） ────────
    convertTM2ToLocal(pts);

    // ── 3. 建立（新增模式）或就地更新（編輯模式／新增模式撞名）TrackCenterLine ──
    TrackCenterLine* tcl = editTarget ? editTarget : findByName(doc, requestedName);
    const bool isUpdate = (tcl != nullptr);
    if (!tcl)
        tcl = doc->addTrackCenterLine(requestedName);

    tcl->setAldHorizontalImport(pts);   // 保留為權威幾何來源（比照 ALD 匯入）
    tcl->loadHorizontal(pts);

    doc->setModified(true);

    // 單一資料存取：raw points 已更新，任何快取中的 AlignmentDocument
    // （供「線形資料表」／grip 編輯使用）必須失效，避免與此處剛寫入的
    // 資料分岔（見 UIManager::invalidateTclAlignmentDocument() 的說明）。
    if (context.uiManager)
        context.uiManager->invalidateTclAlignmentDocument(tcl->id());

    // 注意：不在此自動切換／開啟「Railway 3D Alignment」資料夾顯示——
    // 若使用者原本就沒有顯示該資料夾，AQT 結束後也不強制打開（需求 13）。

    const QString summary = QStringLiteral("快速輸入完成: %1（%2 個關鍵點）%3")
        .arg(tcl->name())
        .arg(pts.size())
        .arg(isUpdate ? QStringLiteral("— 已更新既有線路") : QString());

    if (clm) clm->printMessage(QStringLiteral("[快速輸入線形] %1").arg(summary));
    return CommandResult::Success(summary);
}

// ─────────────────────────────────────────────────────────────────────────────
// Static registration — 指令 "alignmentquicktable" 觸發 AlignmentQuickTableCommand
// ─────────────────────────────────────────────────────────────────────────────
REGISTER_COMMAND("alignmentquicktable", AlignmentQuickTableCommand);

} // namespace command
} // namespace aicad
