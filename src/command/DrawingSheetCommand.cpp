/**
 * @file DrawingSheetCommand.cpp
 * @brief Implementation of DRAWINGSHEET — see the header for usage.
 */
#include "command/DrawingSheetCommand.h"

#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/CommandLineManager.h"
#include "cad/Document.h"
#include "cad/Feature.h"
#include "drawing/DrawingSheet.h"
#include "drawing/DrawingSheetManager.h"
#include "drawing/DrawingSheetWidget.h"   // 內含 DrawingSheetDialog 宣告
#include "view/CadView.h"

#include <QWidget>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Shape.hxx>

namespace aicad {
namespace command {

using core::Application;

namespace {

/** 每個 Document 一個管理器（掛在 Document 下，文件銷毀時一併釋放）。 */
drawing::DrawingSheetManager* managerFor(cad::Document* doc)
{
    for (QObject* child : doc->children()) {
        if (auto* mgr = qobject_cast<drawing::DrawingSheetManager*>(child))
            return mgr;
    }
    auto* mgr = new drawing::DrawingSheetManager(doc);
    mgr->setSourceDocument(doc);
    // 還原隨 .aicad 載入的圖紙（Document::drawingSheetsData，見 Document::load）
    mgr->loadFromJson(doc->drawingSheetsData());
    return mgr;
}

/** 新增圖紙並放入四個預設視圖（A3 橫向 420x297mm，位置為圖紙座標 mm）。 */
drawing::DrawingSheet* createSheetWithDefaultViews(drawing::DrawingSheetManager* mgr,
                                                   const QString& title)
{
    drawing::DrawingSheet* sheet = mgr->newSheet(title);
    sheet->addView(drawing::ViewType::Front,     QPointF(110, 190));
    sheet->addView(drawing::ViewType::Top,       QPointF(110,  80));
    sheet->addView(drawing::ViewType::Right,     QPointF(260, 190));
    sheet->addView(drawing::ViewType::Isometric, QPointF(330,  80));
    return sheet;
}

QString shapeTypeName(const TopoDS_Shape& s)
{
    if (s.IsNull()) return QStringLiteral("NULL");
    switch (s.ShapeType()) {
    case TopAbs_COMPOUND:  return QStringLiteral("Compound");
    case TopAbs_COMPSOLID: return QStringLiteral("CompSolid");
    case TopAbs_SOLID:     return QStringLiteral("Solid");
    case TopAbs_SHELL:     return QStringLiteral("Shell");
    case TopAbs_FACE:      return QStringLiteral("Face");
    case TopAbs_WIRE:      return QStringLiteral("Wire");
    case TopAbs_EDGE:      return QStringLiteral("Edge");
    case TopAbs_VERTEX:    return QStringLiteral("Vertex");
    default:               return QStringLiteral("Shape");
    }
}

void printInfo(core::CommandLineManager* clm, cad::Document* doc,
               drawing::DrawingSheetManager* mgr)
{
    auto out = [clm](const QString& s) {
        if (clm) clm->printMessage(QStringLiteral("[DSH] ") + s);
        qDebug().noquote() << "[DSH]" << s;
    };

    out(QStringLiteral("Document features: %1").arg(doc->featureCount()));
    for (cad::Feature* f : doc->features()) {
        if (!f) continue;
        out(QStringLiteral("  - %1 | type=%2 | shape=%3")
                .arg(f->name(), f->typeString(), shapeTypeName(f->shape())));
    }

    out(QStringLiteral("Drawing sheets: %1").arg(mgr->sheets().size()));
    for (drawing::DrawingSheet* sh : mgr->sheets()) {
        out(QStringLiteral("  sheet '%1' views=%2%3")
                .arg(sh->titleBlock().drawingTitle)
                .arg(sh->views().size())
                .arg(sh == mgr->activeSheet() ? QStringLiteral(" [active]") : QString()));
        for (drawing::DrawingView* v : sh->views()) {
            out(QStringLiteral("    view '%1' scale=%2%3 source=%4")
                    .arg(v->label())
                    .arg(v->scale())
                    .arg(v->autoFit() ? QStringLiteral(" (auto-fit)") : QString())
                    .arg(v->sourceFeature() ? v->sourceFeature()->name()
                                            : QStringLiteral("(none — will NOT render)")));
        }
    }
}

} // namespace

// ============================================================================

DrawingSheetCommand::DrawingSheetCommand()
    : Command("drawingsheet", "Drawing Sheet")
{}

QString DrawingSheetCommand::getUsage() const
{
    return "Usage: drawingsheet [子命令] — 無子命令＝開啟作用中圖紙對話框（沒有圖紙時自動建立 "
           "A3 並放入 Front/Top/Right/Iso 視圖）。子命令：\n"
           "  NEW                新增一張圖紙並開啟\n"
           "  LIST               列出所有圖紙\n"
           "  USE <n>            切換作用中圖紙（n 為 LIST 的編號）\n"
           "  DUP                複製作用中圖紙（深複製）\n"
           "  DELETE [n]         刪除圖紙（預設作用中）\n"
           "  LAYOUT             作用中圖紙自動排版\n"
           "  SAVE [路徑]        另存作用中圖紙為獨立檔 (.aicad_dwg)\n"
           "  OPEN <路徑>        開啟獨立圖紙檔\n"
           "  EXPORT PDF|SVG [路徑]  匯出作用中圖紙\n"
           "  INFO               輸出診斷資訊\n"
           "（圖紙會隨文件存檔時一併寫入 .aicad）";
}

CommandResult DrawingSheetCommand::execute(const CommandContext& context)
{
    Application* app = Application::instance();
    core::CommandLineManager* clm = app->commandLineManager();

    cad::Document* doc = app->documentManager()->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    drawing::DrawingSheetManager* mgr = managerFor(doc);

    const QString sub = context.args.isEmpty() ? QString()
                                               : context.args.first().trimmed().toUpper();

    if (sub == "INFO") {
        printInfo(clm, doc, mgr);
        return CommandResult::Success("DSH INFO done");
    }

    // ── 不開對話框的子命令 ──────────────────────────────────────────────────
    const QStringList rest = context.args.mid(1);
    const QString restText = rest.join(QLatin1Char(' ')).trimmed();
    auto say = [clm](const QString& s) { if (clm) clm->printMessage(QStringLiteral("[DSH] ") + s); };
    auto needActive = [&]() -> drawing::DrawingSheet* { return mgr->activeSheet(); };
    auto pickIndex = [&](bool& ok) -> int {
        ok = false;
        if (rest.isEmpty()) { ok = true; return mgr->sheets().indexOf(mgr->activeSheet()); }
        bool numOk = false;
        const int n = rest.first().toInt(&numOk);
        if (numOk && n >= 1 && n <= mgr->sheets().size()) { ok = true; return n - 1; }
        return -1;
    };

    if (sub == "LIST") {
        if (mgr->sheets().isEmpty()) { say(QStringLiteral("尚無圖紙（輸入 DSH 建立）")); return CommandResult::Success(); }
        int i = 1;
        for (drawing::DrawingSheet* s : mgr->sheets()) {
            say(QStringLiteral("%1. %2  views=%3%4%5")
                    .arg(i++).arg(s->titleBlock().drawingTitle).arg(s->views().size())
                    .arg(s == mgr->activeSheet() ? QStringLiteral("  [active]") : QString())
                    .arg(s->fileName().isEmpty() ? QString() : QStringLiteral("  file=") + s->fileName()));
        }
        return CommandResult::Success();
    }
    if (sub == "USE") {
        bool ok; const int idx = pickIndex(ok);
        if (!ok || idx < 0) return CommandResult::Failure("用法：DSH USE <編號>（編號見 DSH LIST）");
        mgr->setActiveSheet(mgr->sheets().at(idx));
        say(QStringLiteral("作用中圖紙：%1").arg(mgr->activeSheet()->titleBlock().drawingTitle));
        return CommandResult::Success();
    }
    if (sub == "DUP") {
        drawing::DrawingSheet* a = needActive();
        if (!a) return CommandResult::Failure("尚無圖紙");
        drawing::DrawingSheet* c = mgr->duplicateSheet(a->id());
        doc->setModified(true);
        say(QStringLiteral("已複製為：%1").arg(c ? c->titleBlock().drawingTitle : QString()));
        return CommandResult::Success();
    }
    if (sub == "DELETE") {
        bool ok; const int idx = pickIndex(ok);
        if (!ok || idx < 0) return CommandResult::Failure("尚無圖紙或編號無效（見 DSH LIST）");
        const QString t = mgr->sheets().at(idx)->titleBlock().drawingTitle;
        mgr->removeSheet(mgr->sheets().at(idx)->id());
        doc->setModified(true);
        say(QStringLiteral("已刪除圖紙：%1").arg(t));
        return CommandResult::Success();
    }
    if (sub == "LAYOUT") {
        drawing::DrawingSheet* a = needActive();
        if (!a) return CommandResult::Failure("尚無圖紙");
        a->autoLayout();
        doc->setModified(true);
        say(QStringLiteral("已自動排版"));
        return CommandResult::Success();
    }
    if (sub == "SAVE") {
        drawing::DrawingSheet* a = needActive();
        if (!a) return CommandResult::Failure("尚無圖紙");
        QString path = restText;
        if (path.isEmpty()) path = a->fileName();
        if (path.isEmpty()) {
            const QFileInfo fi(doc->fileName());
            const QString dir  = fi.absolutePath().isEmpty() ? QDir::currentPath() : fi.absolutePath();
            const QString base = fi.completeBaseName().isEmpty() ? QStringLiteral("drawing") : fi.completeBaseName();
            path = QStringLiteral("%1/%2_sheet%3.aicad_dwg").arg(dir, base)
                       .arg(mgr->sheets().indexOf(a) + 1);
        }
        if (!a->save(path)) return CommandResult::Failure("圖紙檔儲存失敗：" + path);
        say(QStringLiteral("已儲存：%1").arg(path));
        return CommandResult::Success();
    }
    if (sub == "OPEN") {
        if (restText.isEmpty()) return CommandResult::Failure("用法：DSH OPEN <路徑>");
        drawing::DrawingSheet* s = mgr->openSheet(restText);
        if (!s) return CommandResult::Failure("無法開啟圖紙檔：" + restText);
        doc->setModified(true);
        say(QStringLiteral("已載入圖紙：%1（輸入 DSH 開啟）").arg(s->titleBlock().drawingTitle));
        return CommandResult::Success();
    }
    if (sub == "EXPORT") {
        drawing::DrawingSheet* a = needActive();
        if (!a) return CommandResult::Failure("尚無圖紙");
        const QString fmt = rest.value(0).toUpper();
        const QString path = rest.mid(1).join(QLatin1Char(' ')).trimmed();
        bool ok = false;
        if (fmt == "PDF")      ok = a->exportPdf(path);
        else if (fmt == "SVG") ok = a->exportSvg(path);
        else return CommandResult::Failure("用法：DSH EXPORT PDF|SVG [路徑]");
        if (!ok) return CommandResult::Failure("匯出失敗");
        say(QStringLiteral("已匯出 %1").arg(fmt));
        return CommandResult::Success();
    }

    drawing::DrawingSheet* sheet = nullptr;
    if (sub == "NEW" || !mgr->activeSheet()) {
        const QString title = QStringLiteral("Sheet %1").arg(mgr->sheets().size() + 1);
        sheet = createSheetWithDefaultViews(mgr, title);
    } else {
        sheet = mgr->activeSheet();
    }

    // 圖紙建立時文件可能還沒有實體；之後才建立實體再開 DSH 時，補綁定尚無來源的視圖。
    {
        cad::Feature* src = nullptr;
        const QList<cad::Feature*> feats = doc->features();
        for (int i = feats.size() - 1; i >= 0; --i)
            if (feats[i] && !feats[i]->shape().IsNull()) { src = feats[i]; break; }
        if (src)
            for (drawing::DrawingView* v : sheet->views())
                if (!v->sourceFeature())
                    v->setSourceFeature(src);
    }

    // 與 AlignmentQuickTableCommand 相同：以 CadView 作為對話框的 parent widget
    QWidget* parentWidget = static_cast<QWidget*>(context.cadView);

    bool anySolid = false;
    for (cad::Feature* f : doc->features())
        if (f && !f->shape().IsNull()) { anySolid = true; break; }
    if (!anySolid && clm)
        clm->printMessage(QStringLiteral("[DSH] 注意：文件中沒有任何含形體(shape)的特徵 — "
                                         "圖紙框線與標題欄可預覽，但視圖內容會是空白。"
                                         "請先建立 Extrude 等實體；或輸入 DSH INFO 檢查。"));

    const QJsonArray before = mgr->toJson();

    drawing::DrawingSheetDialog dlg(sheet, parentWidget);
    dlg.exec();

    // 對話框的編輯（視圖位置／比例／標題欄…）是即時套用的，且不一定會觸發
    // sheet 的 modified 信號；以內容比對判斷，有差異就標記文件已修改，
    // 使用者按 Save 時會連同圖紙一併寫入 .aicad。
    if (mgr->toJson() != before) {
        doc->setModified(true);
        if (clm) clm->printMessage(QStringLiteral("[DSH] 圖紙已變更 — 請存檔（Save）以保存圖紙。"));
    }

    return CommandResult::Success(QStringLiteral("Drawing sheet '%1' closed")
                                      .arg(sheet->titleBlock().drawingTitle));
}

REGISTER_COMMAND("drawingsheet", DrawingSheetCommand);

} // namespace command
} // namespace aicad
