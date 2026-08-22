/**
 * @file ImportAldCommand.cpp
 * @brief Implementation of IMPORTALD — see ImportAldCommand.h.
 */
#include "command/alignment/ImportAldCommand.h"

#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/CommandLineManager.h"
#include "core/EventBus.h"
#include "cad/Document.h"
#include "railway/RailwayAlignment.h"
#include "railway/AldFileIO.h"
#include "core/geometry/ProjectOrigin.h"

#include <QFileDialog>
#include <QSettings>
#include <QFileInfo>
#include <QVariantMap>
#include <QDebug>

namespace aicad {
namespace command {

using core::Application;
using railway::AldFileIO;
using railway::TrackCenterLine;

namespace {

const char* kSettingsOrg   = "AICAD";
const char* kSettingsApp   = "AICAD";
// 與 ImportAlignmentCommand 共用同一個「上次 ALD 目錄」設定鍵：兩個命令
// 選檔的資料夾通常相同，沒有理由分開記憶。
const char* kKeyLastAldDir = "railway/lastAldDirectory";

/** 找出既有同名 TrackCenterLine（重新匯入時就地更新，而非產生重複線路）。 */
TrackCenterLine* findByName(cad::Document* doc, const QString& name)
{
    for (TrackCenterLine* tcl : doc->trackCenterLines()) {
        if (tcl && tcl->name() == name)
            return tcl;
    }
    return nullptr;
}

/** 依檔名推導 TrackCenterLine 名稱：優先比照 "...H.ALD" 慣例去掉結尾的 H。 */
QString deriveTrackName(const QString& fileName)
{
    if (fileName.endsWith(QStringLiteral("H.ALD"), Qt::CaseInsensitive)) {
        QString base = fileName;
        base.chop(5);   // 去掉 "H.ALD"（5 字元），與 AldFileIO::verticalFileNameFor 對稱
        return base;
    }
    return QFileInfo(fileName).completeBaseName();
}

} // namespace

// ============================================================================
//  ctor
// ============================================================================

ImportAldCommand::ImportAldCommand()
    : Command("importald", "Import Single ALD")
{}

QString ImportAldCommand::getUsage() const
{
    return "Usage: importald — 選取單一 .ALD 檔以匯入水平線形資料";
}

// ============================================================================
//  execute
// ============================================================================

CommandResult ImportAldCommand::execute(const CommandContext& /*context*/)
{
    Application* app = Application::instance();
    core::DocumentManager* docMgr = app->documentManager();
    core::CommandLineManager* clm = app->commandLineManager();
    core::EventBus* bus = app->eventBus();

    cad::Document* doc = docMgr->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    // ── 1. 選取單一 .ALD 檔（記憶上次目錄） ──────────────────────────────────
    QSettings settings(kSettingsOrg, kSettingsApp);
    const QString lastAldDir = settings.value(kKeyLastAldDir).toString();

    const QString aldPath = QFileDialog::getOpenFileName(
        nullptr,
        "Import ALD (Horizontal Alignment)",
        lastAldDir,
        "ALD Files (*.ald *.ALD);;All Files (*)"
    );
    if (aldPath.isEmpty())
        return CommandResult::Failure("Import cancelled");

    settings.setValue(kKeyLastAldDir, QFileInfo(aldPath).absolutePath());

    // ── 2. 讀取水平線形 ────────────────────────────────────────────────────
    QString hErr;
    QVector<railway::AlignmentPoint> hPts = AldFileIO::readHorizontalALD(aldPath, &hErr);
    if (hPts.size() < 2) {
        return CommandResult::Failure(
            hErr.isEmpty()
                ? QStringLiteral("ALD 有效關鍵點不足: %1").arg(aldPath)
                : QStringLiteral("ALD 讀取失敗: %1 — %2").arg(aldPath, hErr));
    }

    // ── TM2 → Local（比照 ImportAlignmentCommand，見 ProjectOrigin.h） ───────
    {
        using aicad::core::geometry::ProjectOrigin;
        ProjectOrigin::ensureDefault();
        auto& origin = ProjectOrigin::instance();
        for (railway::AlignmentPoint& pt : hPts) {
            const QPointF local = origin.toLocal(pt.easting, pt.northing);
            pt.easting  = local.x();
            pt.northing = local.y();
        }
    }

    // ── 3. 建立（或就地更新同名）TrackCenterLine ──────────────────────────────
    const QString fileName  = QFileInfo(aldPath).fileName();
    const QString trackName = deriveTrackName(fileName);

    TrackCenterLine* tcl = findByName(doc, trackName);
    const bool isUpdate = (tcl != nullptr);
    if (!tcl)
        tcl = doc->addTrackCenterLine(trackName);

    tcl->setAldHorizontalImport(hPts);
    tcl->loadHorizontal(hPts);

    doc->setModified(true);

    // ── 4. 開啟 Railway 資料夾彙總 3D Alignment 顯示 ─────────────────────────
    if (bus) {
        QVariantMap vdata;
        vdata["itemId"]  = QStringLiteral("__railway_folder__");
        vdata["visible"] = true;
        bus->publish("feature.visibility-changed", vdata);
    }

    const QString summary = QStringLiteral("匯入完成: %1（%2 個關鍵點）%3")
        .arg(trackName)
        .arg(hPts.size())
        .arg(isUpdate ? QStringLiteral("— 已更新既有線路") : QString());

    if (clm) clm->printMessage(QStringLiteral("[匯入 ALD] %1").arg(summary));
    return CommandResult::Success(summary);
}

// ─────────────────────────────────────────────────────────────────────────────
// Static registration — 指令 "importald" 觸發 ImportAldCommand
// ─────────────────────────────────────────────────────────────────────────────
REGISTER_COMMAND("importald", ImportAldCommand);

} // namespace command
} // namespace aicad
