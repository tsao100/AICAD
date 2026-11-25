/**
 * @file ComponentInsertCommand.cpp
 */
#include "command/ComponentInsertCommand.h"

#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/CommandLineManager.h"
#include "cad/Document.h"
#include "cad/ComponentInstall.h"
#include "cad/TrackSlabPlan.h"
#include "cad/SketchInstance.h"
#include "cad/AlignedProfileArray.h"
#include "cad/Sketch.h"
#include "ui/ComponentDialogs.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QSet>
#include <QSettings>
#include <QDebug>
#include <memory>

#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>

namespace aicad {
namespace command {

using core::Application;

ComponentInsertCommand::ComponentInsertCommand()
    : Command("insertcomp", "Insert 3D Component")
{}

QString ComponentInsertCommand::getUsage() const
{
    return "Usage: insertcomp — 插入 *.aicad 作為 3D 組件，指定安裝里程；道版 Loft 於所需空間處開孔";
}

CommandResult ComponentInsertCommand::execute(const CommandContext& /*context*/)
{
    Application* app = Application::instance();
    core::DocumentManager* docMgr = app->documentManager();
    core::CommandLineManager* clm = app->commandLineManager();

    cad::Document* doc = docMgr->currentDocument();
    if (!doc) return CommandResult::Failure("No active document");

    QWidget* parentWidget = QApplication::activeWindow();

    // ── 1. 選取 *.aicad ──────────────────────────────────────────────────────
    QSettings settings("AICAD", "AICAD");
    const QString lastDir = settings.value("Component/lastDir").toString();
    const QString path = QFileDialog::getOpenFileName(
        parentWidget, QObject::tr("插入 3D 組件 (*.aiasm／*.aipart)"), lastDir,
        cad::Document::insertComponentFilter());
    if (path.isEmpty()) return CommandResult::Failure("Insert cancelled");
    settings.setValue("Component/lastDir", QFileInfo(path).absolutePath());

    // ── 2. 以暫時的 Document 載入，列出可用的實體 ───────────────────────────
    //    （*.aicad 不存形狀，必須載入並重建才有實體；Document 會訂閱 EventBus 但用 QPointer，
    //     離開此函式即銷毀，不影響目前文件。）
    auto tmp = std::make_unique<cad::Document>();
    if (!tmp->load(path))
        return CommandResult::Failure(QString("無法讀取檔案：%1").arg(path));

    // 組件檔內「所有 3D 特徵的組合結果」：Void 根底下的特徵合成「void」（讓道版 Loft 開孔，不一定要有），
    // Assembly 根底下的造型特徵與插入零件合成「組件實體」（顯示在安裝位置）。不再需要逐一指定角色。
    TopoDS_Shape comp, space;
    QStringList compNames, spaceNames;
    QString exErr;
    if (!cad::ComponentInstall::extractFromDocument(tmp.get(), comp, compNames, space, spaceNames, &exErr))
        return CommandResult::Failure(exErr);

    // ── 3. 與哪一個斷面草圖建立約束關係（預設 = 目前選取的 Sketch，也可改選）──────────
    QVector<cad::Sketch*> sketches;
    for (cad::Feature* f : doc->features())
        if (auto* sk = qobject_cast<cad::Sketch*>(f)) sketches.append(sk);

    cad::Sketch* constrainSketch = nullptr;
    if (!sketches.isEmpty()) {
        // 預設：作用中草圖 → Feature Browser 目前選取的 Sketch → 第一個斷面陣列的主草圖
        cad::Sketch* def = app->activeSketch();
        if (!def) {
            const QString selId = app->property("selectedFeatureId").toString();
            def = qobject_cast<cad::Sketch*>(doc->findFeature(selId));
        }
        if (!def)
            for (cad::Feature* f : doc->features())
                if (auto* arr = qobject_cast<cad::AlignedProfileArray*>(f))
                    if (arr->masterSketch()) { def = arr->masterSketch(); break; }

        QStringList names;
        int defIdx = 0;
        for (int i = 0; i < sketches.size(); ++i) {
            names << sketches[i]->name();
            if (sketches[i] == def) defIdx = i;
        }
        names << QObject::tr("（不建立約束）");

        bool ok = false;
        const QString pick = QInputDialog::getItem(
            parentWidget, QObject::tr("組件與草圖的約束關係"),
            QObject::tr("要將組件的原點／軸線與哪一個 Sketch 建立約束關係？\n"
                        "（預設為目前選取的 Sketch；座標線會建立在該草圖，預設貼合 cant 直線，\n"
                        "之後可在草圖中加約束，尺寸可用 cant 參數）"),
            names, defIdx, false, &ok);
        if (!ok) return CommandResult::Failure("Insert cancelled");
        const int idx = names.indexOf(pick);
        if (idx >= 0 && idx < sketches.size()) constrainSketch = sketches[idx];
    }

    // ── 4. 建立 ComponentInstall（形狀的 Handle 由 ComponentInstall 持有，tmp 銷毀後仍有效）─
    cad::ComponentInstall* ci = doc->createComponentInstall(QFileInfo(path).completeBaseName());
    ci->setSource(path, comp, compNames, space, spaceNames);

    QString frameNote;
    if (constrainSketch) {
        QString fe;
        if (!ci->createFrameInSketch(constrainSketch, &fe, &frameNote))
            qWarning() << "[INSERTCOMP] 建立組件座標線失敗：" << fe;
    }

    doc->addComponentInstall(ci);
    doc->setModified(true);
    ci->attachSourceWatcher();       // 與來源檔連動（來源檔被存檔 → 自動同步並重新開孔）

    QString summary = QString("已插入 3D 組件「%1」（組件實體 %2 個特徵：%3；void %4）。單位 mm → m（縮小 1000 倍）。")
                          .arg(ci->name()).arg(compNames.size()).arg(compNames.join(QStringLiteral("、")))
                          .arg(spaceNames.isEmpty() ? QStringLiteral("無") : spaceNames.join(QStringLiteral("、")));
    if (constrainSketch) summary += QString("座標線建立於草圖「%1」。").arg(constrainSketch->name());
    summary += QStringLiteral("請在 Feature Browser「組件安裝位置」資料夾（右鍵或雙擊）設定沿線形的安裝位置。");
    if (!frameNote.isEmpty()) summary += QStringLiteral("（") + frameNote + QStringLiteral("）");
    if (clm) clm->printMessage(QStringLiteral("[INSERTCOMP] ") + summary);
    return CommandResult::Success(summary);
}

REGISTER_COMMAND("insertcomp", ComponentInsertCommand);

} // namespace command
} // namespace aicad
