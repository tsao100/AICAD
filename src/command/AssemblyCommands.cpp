/**
 * @file AssemblyCommands.cpp
 */
#include "command/AssemblyCommands.h"

#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "core/DocumentManager.h"
#include "cad/AssemblyMate.h"
#include "cad/AssemblyPart.h"
#include "cad/AssemblyUtil.h"
#include "cad/ComponentInstall.h"
#include "cad/Document.h"
#include "ui/AssemblyDialogs.h"
#include "view/CadView.h"

#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <memory>

namespace aicad {
namespace command {

using core::Application;

namespace {

cad::Document* currentDoc()
{
    return Application::instance()->documentManager()->currentDocument();
}

void say(const QString& msg)
{
    if (auto* clm = Application::instance()->commandLineManager()) clm->printMessage(msg);
}

QWidget* topWindow() { return QApplication::activeWindow(); }

// ── 3D 視圖點選一個零件上的面／邊／頂點 ─────────────────────────────────────────
bool pickReferenceFromView(view::CadView* view, cad::Document* doc, cad::MateRefData& out, QString* err)
{
    if (!view || !doc) { if (err) *err = QObject::tr("沒有可用的 3D 視圖"); return false; }
    if (doc->assemblyParts().isEmpty()) { if (err) *err = QObject::tr("文件中還沒有零件"); return false; }

    const view::InteractionMode prevMode = view->mode();
    view->beginSubShapePicking(true, true, true, QStringLiteral("AssemblyPart"));

    // 提示小視窗（非強制關閉；取消按鈕結束點選）
    QWidget tip(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint);
    tip.setWindowTitle(QObject::tr("點選幾何"));
    auto* lay = new QVBoxLayout(&tip);
    auto* label = new QLabel(QObject::tr("請在 3D 視圖點選零件上的面、邊或頂點\n（平面→平面、圓柱面／圓邊／直線邊→軸線、頂點→點）"));
    auto* cancel = new QPushButton(QObject::tr("取消"));
    lay->addWidget(label);
    lay->addWidget(cancel);
    tip.show();

    QEventLoop loop;
    bool got = false;
    QString lastErr;
    QObject::connect(cancel, &QPushButton::clicked, &loop, &QEventLoop::quit);
    QObject::connect(view, &view::CadView::edgePicked, &loop, [&]() {
        const QVector<view::PickedSubShapeRef> picked = view->pickedSubShapes();
        if (picked.isEmpty()) return;                          // 再點一次同一個 = 取消選取，繼續等待
        const view::PickedSubShapeRef& p = picked.last();
        cad::MateRefData r;
        QString e;
        if (cad::assemblyutil::refFromPickedShape(doc, p.featureId, p.shape, r, &e)) {
            out = r;
            got = true;
            loop.quit();
        } else {
            label->setText(QObject::tr("無法使用這個幾何：%1\n請再點選其他面、邊或頂點").arg(e));
            lastErr = e;
            view->endEdgePicking();                             // 清掉選取後重新開放
            view->beginSubShapePicking(true, true, true, QStringLiteral("AssemblyPart"));
        }
    });
    QObject::connect(view, &view::CadView::destroyed, &loop, &QEventLoop::quit);

    loop.exec();

    view->endEdgePicking();
    view->setMode(prevMode);
    tip.close();
    if (!got && err) *err = QString();                          // 使用者取消：不顯示訊息
    return got;
}

ui::MateDialog::PickFn makePickFn(view::CadView* view, cad::Document* doc)
{
    return [view, doc](cad::MateRefData& out, QString* err) { return pickReferenceFromView(view, doc, out, err); };
}

// ── 流程：插入零件 ───────────────────────────────────────────────────────────
CommandResult runInsertPart(bool asVoid = false)
{
    cad::Document* doc = currentDoc();
    if (!doc) return CommandResult::Failure("No active document");

    QSettings settings("AICAD", "AICAD");
    const QString path = QFileDialog::getOpenFileName(
        topWindow(), QObject::tr("插入零件 (*.aipart／*.aiasm)"), settings.value("Assembly/lastDir").toString(),
        cad::Document::insertPartFilter());
    if (path.isEmpty()) return CommandResult::Failure("Insert cancelled");
    settings.setValue("Assembly/lastDir", QFileInfo(path).absolutePath());

    // *.aicad 不存形狀，必須載入並重建；暫時 Document 離開此函式即銷毀（形狀 Handle 由零件持有）
    auto tmp = std::make_unique<cad::Document>();
    if (!tmp->load(path)) return CommandResult::Failure(QString("無法讀取檔案：%1").arg(path));

    TopoDS_Shape comp, voidShape;
    QStringList compNames, voidNames;
    QString exErr;
    if (!cad::ComponentInstall::extractFromDocument(tmp.get(), comp, compNames, voidShape, voidNames, &exErr))
        return CommandResult::Failure(exErr);

    ui::AssemblyPartParams init;
    init.name  = QFileInfo(path).completeBaseName();
    init.fixed = doc->assemblyParts().isEmpty();               // 第一個零件預設固定（接地）
    if (asVoid) init.name += QStringLiteral("_void");
    ui::AssemblyPartDialog dlg(init, true, topWindow());
    if (dlg.exec() != QDialog::Accepted) return CommandResult::Failure("Insert cancelled");
    const ui::AssemblyPartParams p = dlg.params();

    cad::AssemblyPart* part = doc->createAssemblyPart(p.name.isEmpty() ? init.name : p.name);
    part->setSource(path, comp, compNames);
    part->setUnitScale(p.scale);
    part->setFixed(p.fixed);
    part->setGroupRaw(asVoid ? cad::FeatureGroup::Void : cad::FeatureGroup::Assembly);   // 放進 Void（開孔形體）或 Assembly 根
    part->setAutoSync(p.autoSync);
    part->setPose(cad::AssemblyPart::makePose(p.x, p.y, p.z, p.rx, p.ry, p.rz));
    doc->addAssemblyPart(part);
    part->attachSourceWatcher();

    QString report;
    doc->solveAssembly(&report);
    const QString summary = QString("已插入零件「%1」到 %6（%2 個 3D 特徵：%3%4）。%5")
                                .arg(part->name()).arg(compNames.size()).arg(compNames.join(QStringLiteral("、")))
                                .arg(voidNames.isEmpty() ? QString() : QStringLiteral("；void 不納入零件"))
                                .arg(p.fixed ? QStringLiteral("此零件為固定。") : QStringLiteral("用 MATE 新增配合來組合零件。"))
                                .arg(asVoid ? QStringLiteral("Void") : QStringLiteral("Assembly"));
    say(QStringLiteral("[INSERTPART] ") + summary);
    return CommandResult::Success(summary);
}

// ── 流程：新增／編輯配合 ──────────────────────────────────────────────────────
QString defaultMateName(cad::Document* doc)
{
    int n = 1;
    QString nm;
    do { nm = QStringLiteral("Mate %1").arg(n++); } while (doc->findFeatureByName(nm));
    return nm;
}

CommandResult runMate(const CommandContext& ctx, const QString& presetPart, cad::AssemblyMate* editing)
{
    cad::Document* doc = currentDoc();
    if (!doc) return CommandResult::Failure("No active document");
    if (doc->assemblyParts().isEmpty())
        return CommandResult::Failure("文件中還沒有零件：請先用 INSERTPART 插入零件");

    const cad::AssemblyMate::Data initial = editing ? editing->data() : cad::AssemblyMate::Data();
    ui::MateDialog dlg(doc, editing ? &initial : nullptr, presetPart,
                       editing ? editing->name() : defaultMateName(doc),
                       makePickFn(ctx.cadView, doc), topWindow());
    if (dlg.exec() != QDialog::Accepted) return CommandResult::Failure("Mate cancelled");

    const cad::AssemblyMate::Data d = dlg.data();
    QString err;
    QString report;

    if (editing) {
        const cad::AssemblyMate::Data before = editing->data();
        if (!editing->setData(d, &err)) {
            QMessageBox::warning(topWindow(), QObject::tr("配合"), err);
            return CommandResult::Failure(err);
        }
        const QString nm = dlg.mateName();
        if (!nm.isEmpty() && nm != editing->name()) editing->setName(nm);
        doc->setModified(true);
        const bool ok = doc->solveAssembly(&report);
        if (!ok && editing->hasError()) {
            // 新設定無法滿足：還原成舊設定（舊設定本來滿足），避免組合卡在衝突狀態
            editing->setData(before, nullptr);
            doc->solveAssembly(nullptr);
            const QString msg = QObject::tr("新的設定無法與其他配合同時滿足，已還原。\n%1").arg(report);
            QMessageBox::warning(topWindow(), QObject::tr("配合"), msg);
            return CommandResult::Failure(msg);
        }
        say(QStringLiteral("[MATE] 已更新配合「%1」。%2").arg(editing->name(), report));
        return CommandResult::Success(report);
    }

    cad::AssemblyMate* mate = doc->createAssemblyMate(dlg.mateName());
    if (!mate->setData(d, &err)) {
        delete mate;
        QMessageBox::warning(topWindow(), QObject::tr("配合"), err);
        return CommandResult::Failure(err);
    }
    doc->addAssemblyMate(mate);
    const bool ok = doc->solveAssembly(&report);
    if (!ok && mate->hasError()) {
        // 衝突：移除這個新配合，零件本來就沒被移動
        const QString msg = QObject::tr("這個配合無法與現有配合同時滿足（衝突或過度約束），未加入。\n%1").arg(report);
        doc->removeFeature(mate);
        doc->solveAssembly(nullptr);
        QMessageBox::warning(topWindow(), QObject::tr("配合"), msg);
        return CommandResult::Failure(msg);
    }
    const QString summary = QStringLiteral("已新增配合「%1」：%2。%3").arg(mate->name(), mate->description(), report);
    say(QStringLiteral("[MATE] ") + summary);
    return CommandResult::Success(summary);
}

// ── 流程：編輯零件 ───────────────────────────────────────────────────────────
CommandResult runEditPart(cad::AssemblyPart* part)
{
    cad::Document* doc = currentDoc();
    if (!doc || !part) return CommandResult::Failure("No part");

    ui::AssemblyPartParams init;
    init.name = part->name();
    init.scale = part->unitScale();
    init.fixed = part->isFixed();
    init.autoSync = part->autoSync();
    cad::AssemblyPart::decomposePose(part->pose(), init.x, init.y, init.z, init.rx, init.ry, init.rz);

    ui::AssemblyPartDialog dlg(init, false, topWindow());
    if (dlg.exec() != QDialog::Accepted) return CommandResult::Failure("Edit cancelled");
    const ui::AssemblyPartParams p = dlg.params();

    if (!p.name.isEmpty() && p.name != part->name()) {
        if (doc->findFeatureByName(p.name)) {
            QMessageBox::warning(topWindow(), QObject::tr("編輯零件"), QObject::tr("名稱「%1」已被使用").arg(p.name));
        } else {
            part->setName(p.name);
        }
    }
    part->setUnitScale(p.scale);
    part->setFixed(p.fixed);
    if (p.autoSync != part->autoSync()) part->setAutoSync(p.autoSync);
    part->setPose(cad::AssemblyPart::makePose(p.x, p.y, p.z, p.rx, p.ry, p.rz));
    doc->applyAssemblyPartChange(part);

    QString report;
    doc->solveAssembly(&report);
    say(QStringLiteral("[ASM] 已更新零件「%1」。%2").arg(part->name(), report));
    return CommandResult::Success(report);
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// INSERTPART
// ═════════════════════════════════════════════════════════════════════════════
InsertPartCommand::InsertPartCommand() : Command("insertpart", "Insert Assembly Part") {}

QString InsertPartCommand::getUsage() const
{
    return "Usage: insertpart — 插入 *.aipart／*.aiasm 作為組合中的零件（單位縮放、初始位置／轉角、固定）";
}

CommandResult InsertPartCommand::execute(const CommandContext& /*context*/) { return runInsertPart(); }

// ═════════════════════════════════════════════════════════════════════════════
// MATE
// ═════════════════════════════════════════════════════════════════════════════
MateCommand::MateCommand() : Command("mate", "Add Assembly Mate") {}

QString MateCommand::getUsage() const
{
    return "Usage: mate — 新增配合：重合、共線、共面、平行、垂直、距離、夾角；"
           "參照可為零件的基準／面／邊／頂點，或世界座標軸／座標平面（可在 3D 視圖點選）";
}

CommandResult MateCommand::execute(const CommandContext& context)
{
    return runMate(context, context.data.value("partId").toString(), nullptr);
}

// ═════════════════════════════════════════════════════════════════════════════
// SOLVEASM
// ═════════════════════════════════════════════════════════════════════════════
SolveAssemblyCommand::SolveAssemblyCommand() : Command("solveasm", "Solve Assembly") {}

QString SolveAssemblyCommand::getUsage() const { return "Usage: solveasm — 重新求解整個組合"; }

CommandResult SolveAssemblyCommand::execute(const CommandContext& /*context*/)
{
    cad::Document* doc = currentDoc();
    if (!doc) return CommandResult::Failure("No active document");
    QString report;
    const bool ok = doc->solveAssembly(&report);
    say(QStringLiteral("[SOLVEASM] ") + report);
    return ok ? CommandResult::Success(report) : CommandResult::Failure(report);
}

// ═════════════════════════════════════════════════════════════════════════════
// ASMACTION（Feature Browser 右鍵選單）
// ═════════════════════════════════════════════════════════════════════════════
AssemblyActionCommand::AssemblyActionCommand() : Command("asmaction", "Assembly Action") {}

QString AssemblyActionCommand::getUsage() const
{
    return "Usage: asmaction — 內部使用（Feature Browser 的零件組合選單動作）";
}

CommandResult AssemblyActionCommand::execute(const CommandContext& context)
{
    cad::Document* doc = currentDoc();
    if (!doc) return CommandResult::Failure("No active document");
    const QString action = context.data.value("action").toString();
    const QString id = context.data.value("itemId").toString();
    auto* part = qobject_cast<cad::AssemblyPart*>(doc->findFeature(id));
    auto* mate = qobject_cast<cad::AssemblyMate*>(doc->findFeature(id));

    if (action == QLatin1String("insertPart"))     return runInsertPart(false);
    if (action == QLatin1String("insertPartVoid")) return runInsertPart(true);
    if ((action == QLatin1String("moveToPart") || action == QLatin1String("moveToAssembly") ||
         action == QLatin1String("moveToVoid")) && doc->findFeature(id)) {
        const cad::FeatureGroup g = action == QLatin1String("moveToPart") ? cad::FeatureGroup::Part
                                  : action == QLatin1String("moveToVoid") ? cad::FeatureGroup::Void
                                                                          : cad::FeatureGroup::Assembly;
        doc->setFeatureGroup(doc->findFeature(id), g);
        say(QStringLiteral("[ASM] 「%1」已移到 %2。").arg(doc->findFeature(id)->name(), cad::Document::groupLabel(g)));
        return CommandResult::Success("moved");
    }
    if (action == QLatin1String("setActivePart") || action == QLatin1String("setActiveAssembly") ||
        action == QLatin1String("setActiveVoid")) {
        const cad::FeatureGroup g = action == QLatin1String("setActivePart") ? cad::FeatureGroup::Part
                                  : action == QLatin1String("setActiveVoid") ? cad::FeatureGroup::Void
                                                                             : cad::FeatureGroup::Assembly;
        doc->setActiveGroup(g);
        say(QStringLiteral("[ASM] 新增的特徵（擠出等）之後會放進 %1。").arg(cad::Document::groupLabel(g)));
        return CommandResult::Success("active group");
    }
    if (action == QLatin1String("addMate"))        return runMate(context, QString(), nullptr);
    if (action == QLatin1String("addMateForPart")) return runMate(context, id, nullptr);
    if (action == QLatin1String("solve")) {
        QString report;
        const bool ok = doc->solveAssembly(&report);
        say(QStringLiteral("[ASM] ") + report);
        return ok ? CommandResult::Success(report) : CommandResult::Failure(report);
    }
    if (action == QLatin1String("editPart") && part) return runEditPart(part);
    if (action == QLatin1String("editMate") && mate) return runMate(context, QString(), mate);

    if (action == QLatin1String("toggleFixed") && part) {
        part->setFixed(!part->isFixed());
        doc->applyAssemblyPartChange(part);
        QString report;
        doc->solveAssembly(&report);
        say(QStringLiteral("[ASM] 「%1」已設為%2。%3").arg(part->name(), part->isFixed() ? "固定" : "浮動", report));
        return CommandResult::Success(report);
    }
    if (action == QLatin1String("syncPart") && part) {
        QString err;
        if (!part->syncFromSource(&err)) {
            QMessageBox::warning(topWindow(), QObject::tr("同步來源檔"), err);
            return CommandResult::Failure(err);
        }
        Q_EMIT part->sourceSynced();               // Document：重新取得配合幾何、重新放置、求解
        say(QStringLiteral("[ASM] 已重新載入零件「%1」。%2").arg(part->name(), doc->assemblyReport()));
        return CommandResult::Success("synced");
    }
    if (action == QLatin1String("openSource") && part) {
        if (!QFileInfo::exists(part->sourcePath()))
            return CommandResult::Failure(QString("來源檔不存在：%1").arg(part->sourcePath()));
        Application::instance()->documentManager()->openDocument(part->sourcePath());
        return CommandResult::Success("opened");
    }
    if (action == QLatin1String("toggleSuppress") && mate) {
        mate->setSuppressed(!mate->isSuppressed());
        doc->setModified(true);
        QString report;
        doc->solveAssembly(&report);
        say(QStringLiteral("[ASM] 配合「%1」已%2。%3").arg(mate->name(), mate->isSuppressed() ? "抑制" : "啟用", report));
        return CommandResult::Success(report);
    }
    return CommandResult::Failure(QString("Unknown assembly action: %1").arg(action));
}

REGISTER_COMMAND("insertpart", InsertPartCommand);
REGISTER_COMMAND("mate", MateCommand);
REGISTER_COMMAND("solveasm", SolveAssemblyCommand);
REGISTER_COMMAND("asmaction", AssemblyActionCommand);

} // namespace command
} // namespace aicad
