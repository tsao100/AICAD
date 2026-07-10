/**
 * @file ImportAlignmentCommand.cpp
 * @brief Implementation of IMPORTALIGNMENT — see ImportAlignmentCommand.h.
 */
#include "command/alignment/ImportAlignmentCommand.h"

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
#include <QDir>
#include <QVariantMap>
#include <QDebug>

namespace aicad {
namespace command {

using core::Application;
using railway::AldFileIO;
using railway::TrackCenterLine;

namespace {

const char* kSettingsOrg  = "AICAD";
const char* kSettingsApp  = "AICAD";
const char* kKeyLastPrjDir = "railway/lastPrjDirectory";
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

/** 檢查資料夾內是否存在該批 .prj 項目所列的 *H.ALD 檔案（至少一部分即視為候選成立）。 */
bool directoryLooksValid(const QDir& dir, const QVector<AldFileIO::PrjEntry>& entries)
{
    if (!dir.exists())
        return false;
    int found = 0;
    for (const AldFileIO::PrjEntry& e : entries) {
        if (dir.exists(e.hFileName))
            ++found;
    }
    // 只要能找到任何一筆列表中的檔案，就視為此資料夾可用（實際匯入時仍會逐筆
    // 檢查是否存在，找不到的個別項目會被略過並提示，不會讓整個匯入失敗）。
    return found > 0;
}

} // namespace

// ============================================================================
//  ctor
// ============================================================================

ImportAlignmentCommand::ImportAlignmentCommand()
    : Command("importalignment", "Import Railway Alignment (PRJ/ALD)")
{}

QString ImportAlignmentCommand::getUsage() const
{
    return "Usage: importalignment — 選取 .prj 檔以匯入水平/垂直線形資料";
}

// ============================================================================
//  execute
// ============================================================================

CommandResult ImportAlignmentCommand::execute(const CommandContext& /*context*/)
{
    Application* app = Application::instance();
    core::DocumentManager* docMgr = app->documentManager();
    core::CommandLineManager* clm = app->commandLineManager();
    core::EventBus* bus = app->eventBus();

    cad::Document* doc = docMgr->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    // ── 1. 選取 .prj 檔（記憶上次目錄） ──────────────────────────────────────
    QSettings settings(kSettingsOrg, kSettingsApp);
    const QString lastPrjDir = settings.value(kKeyLastPrjDir).toString();

    const QString prjPath = QFileDialog::getOpenFileName(
        nullptr,
        "Import PRJ (Track List)",
        lastPrjDir,
        "Project Files (*.prj);;All Files (*)"
    );
    if (prjPath.isEmpty())
        return CommandResult::Failure("Import cancelled");

    settings.setValue(kKeyLastPrjDir, QFileInfo(prjPath).absolutePath());

    // ── 2. 解析 .prj ─────────────────────────────────────────────────────────
    QString err;
    const QVector<AldFileIO::PrjEntry> entries = AldFileIO::readPrj(prjPath, &err);
    if (entries.isEmpty()) {
        return CommandResult::Failure(
            err.isEmpty() ? QStringLiteral("PRJ 檔內容為空: %1").arg(prjPath) : err);
    }

    // ── 3. 解析 ALD 檔案所在資料夾 ────────────────────────────────────────────
    //      優先序：與 .prj 相同目錄 → 上次記憶的 ALD 目錄 → 使用者手動選擇。
    QDir aldDir(QFileInfo(prjPath).absolutePath());
    bool resolved = directoryLooksValid(aldDir, entries);

    if (!resolved) {
        const QString rememberedAldDir = settings.value(kKeyLastAldDir).toString();
        if (!rememberedAldDir.isEmpty()) {
            QDir candidate(rememberedAldDir);
            if (directoryLooksValid(candidate, entries)) {
                aldDir = candidate;
                resolved = true;
            }
        }
    }

    if (!resolved) {
        const QString startDir = settings.value(kKeyLastAldDir, aldDir.absolutePath()).toString();
        const QString chosen = QFileDialog::getExistingDirectory(
            nullptr,
            QStringLiteral("選擇 ALD 檔案所在資料夾（內含 %1 等檔案）").arg(entries.first().hFileName),
            startDir
        );
        if (chosen.isEmpty())
            return CommandResult::Failure("Import cancelled: ALD directory not selected");
        aldDir = QDir(chosen);
        resolved = directoryLooksValid(aldDir, entries);
        if (!resolved) {
            return CommandResult::Failure(
                QStringLiteral("選取的資料夾內找不到 PRJ 所列的任何 ALD 檔案: %1").arg(chosen));
        }
    }

    settings.setValue(kKeyLastAldDir, aldDir.absolutePath());

    // ── 4. 逐筆匯入 ───────────────────────────────────────────────────────────
    int importedTracks   = 0;
    int withVertical      = 0;
    int skippedMissingH   = 0;
    int skippedTooFewPts  = 0;

    for (const AldFileIO::PrjEntry& entry : entries) {
        const QString hPath = aldDir.filePath(entry.hFileName);
        if (!QFileInfo::exists(hPath)) {
            ++skippedMissingH;
            if (clm) clm->printMessage(
                QStringLiteral("[匯入線形] 略過（找不到檔案）: %1").arg(entry.hFileName));
            continue;
        }

        QString hErr;
        QVector<railway::AlignmentPoint> hPts =
            AldFileIO::readHorizontalALD(hPath, &hErr);
        if (hPts.size() < 2) {
            ++skippedTooFewPts;
            if (clm) clm->printMessage(
                QStringLiteral("[匯入線形] 略過（有效關鍵點不足）: %1%2")
                    .arg(entry.hFileName)
                    .arg(hErr.isEmpty() ? QString() : QStringLiteral(" — %1").arg(hErr)));
            continue;
        }

        // ── TM2 → Local ──────────────────────────────────────────────────
        // ALD 檔內的 easting/northing 是原始 TM2 絕對座標；但整個應用程式的
        // 架構原則是「OCCT/CAD 內部只使用 Local 座標，TM2 大數值只在輸入框／
        // 顯示文字／檔案 I/O 三個邊界出現」（見 ProjectOrigin.h）。匯入流程本身
        // 就是這三個邊界之一，因此在存入 TrackCenterLine 之前先扣掉 TM2
        // origin（若專案尚未設定，套用預設值）換算成 Local，避免大量級 TM2
        // 數字被下游（3D 算圖、AlignmentDataTableDialog 等）誤當成 Local
        // 座標直接使用，造成 fitAll()/座標顯示異常。
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

        const QString trackName = entry.trackId.isEmpty() ? entry.hFileName : entry.trackId;

        TrackCenterLine* tcl = findByName(doc, trackName);
        if (!tcl)
            tcl = doc->addTrackCenterLine(trackName);

        tcl->loadHorizontal(hPts);
        ++importedTracks;

        // ── 依 *H.ALD 檔名推導對應 *V.ALD ───────────────────────────────────
        const QString vFileName = AldFileIO::verticalFileNameFor(entry.hFileName);
        if (!vFileName.isEmpty()) {
            const QString vPath = aldDir.filePath(vFileName);
            if (QFileInfo::exists(vPath)) {
                QString vErr;
                const QVector<railway::VerticalAlignmentPoint> vPts =
                    AldFileIO::readVerticalALD(vPath, &vErr);
                if (!vPts.isEmpty()) {
                    tcl->loadVertical(vPts);
                    ++withVertical;
                } else if (!vErr.isEmpty() && clm) {
                    clm->printMessage(
                        QStringLiteral("[匯入線形] %1 垂直線形讀取失敗: %2")
                            .arg(vFileName, vErr));
                }
            }
        }
    }

    doc->setModified(true);

    // ── 5. 開啟 Railway 資料夾彙總 3D Alignment 顯示 ───────────────────────────
    //      Railway3DAlignmentRenderer::showAll() 會以 doc->trackCenterLines()
    //      彙總顯示所有線路（不受個別 TCL 的 hAlignVisible 影響），因此匯入
    //      大量線路時無需逐一設定個別可見性即可立即看到匯入結果。
    if (importedTracks > 0 && bus) {
        QVariantMap vdata;
        vdata["itemId"]  = QStringLiteral("__railway_folder__");
        vdata["visible"] = true;
        bus->publish("feature.visibility-changed", vdata);
    }

    // ── 6. 組成結果訊息 ──────────────────────────────────────────────────────
    QString summary = QStringLiteral(
        "匯入完成: %1 條線路（其中 %2 條含垂直線形）")
            .arg(importedTracks).arg(withVertical);
    if (skippedMissingH > 0 || skippedTooFewPts > 0) {
        summary += QStringLiteral("，略過 %1 筆（檔案缺失 %2、資料不足 %3）")
            .arg(skippedMissingH + skippedTooFewPts)
            .arg(skippedMissingH)
            .arg(skippedTooFewPts);
    }

    if (importedTracks == 0)
        return CommandResult::Failure(summary);

    if (clm) clm->printMessage(QStringLiteral("[匯入線形] %1").arg(summary));
    return CommandResult::Success(summary);
}

// ─────────────────────────────────────────────────────────────────────────────
// Static registration — 指令 "importalignment" 觸發 ImportAlignmentCommand
// ─────────────────────────────────────────────────────────────────────────────
REGISTER_COMMAND("importalignment", ImportAlignmentCommand);

} // namespace command
} // namespace aicad
