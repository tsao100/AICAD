/**
 * @file ExportAlignmentCommand.cpp
 * @brief Implementation of EXPORTALIGNMENT — see ExportAlignmentCommand.h.
 */
#include "command/alignment/ExportAlignmentCommand.h"

#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/CommandLineManager.h"
#include "cad/Document.h"
#include "railway/RailwayAlignment.h"
#include "railway/AldFileIO.h"
#include "core/geometry/ProjectOrigin.h"

#include <QFileDialog>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

namespace aicad {
namespace command {

using core::Application;
using railway::AldFileIO;
using railway::TrackCenterLine;

namespace {

// 沿用與 ImportAlignmentCommand 相同的設定鍵值，讓匯入/匯出共用同一組
// 「上次使用目錄」記憶，避免使用者每次都要重新選擇資料夾。
const char* kSettingsOrg   = "AICAD";
const char* kSettingsApp   = "AICAD";
const char* kKeyLastPrjDir = "railway/lastPrjDirectory";
const char* kKeyLastAldDir = "railway/lastAldDirectory";

} // namespace

// ============================================================================
//  ctor
// ============================================================================

ExportAlignmentCommand::ExportAlignmentCommand()
    : Command("exportalignment", "Export/Update Railway Alignment (PRJ/ALD)")
{}

QString ExportAlignmentCommand::getUsage() const
{
    return "Usage: exportalignment — 選取（或建立）.prj 檔以匯出/更新水平/垂直線形資料";
}

// ============================================================================
//  execute
// ============================================================================

CommandResult ExportAlignmentCommand::execute(const CommandContext& /*context*/)
{
    Application* app = Application::instance();
    core::DocumentManager* docMgr = app->documentManager();
    core::CommandLineManager* clm = app->commandLineManager();

    cad::Document* doc = docMgr->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    const QList<TrackCenterLine*>& tcls = doc->trackCenterLines();
    if (tcls.isEmpty())
        return CommandResult::Failure("目前文件沒有任何 TrackCenterLine 可供匯出");

    // ── 1. 選取（或建立）.prj 檔（記憶上次目錄） ─────────────────────────────
    QSettings settings(kSettingsOrg, kSettingsApp);
    const QString lastPrjDir = settings.value(kKeyLastPrjDir).toString();

    const QString prjPath = QFileDialog::getSaveFileName(
        nullptr,
        "Export/Update PRJ (Track List)",
        lastPrjDir,
        "Project Files (*.prj);;All Files (*)",
        nullptr,
        QFileDialog::DontConfirmOverwrite  // 逐檔覆寫由本命令自行處理，不需二次確認
    );
    if (prjPath.isEmpty())
        return CommandResult::Failure("Export cancelled");

    const QDir aldDir(QFileInfo(prjPath).absolutePath());
    settings.setValue(kKeyLastPrjDir, aldDir.absolutePath());
    settings.setValue(kKeyLastAldDir, aldDir.absolutePath());

    // ── 2. 逐條線路寫出 *H.ALD / *V.ALD ───────────────────────────────────────
    QVector<AldFileIO::PrjEntry> entries;
    entries.reserve(tcls.size());

    int exportedTracks  = 0;
    int withVertical    = 0;
    int failedTracks    = 0;

    for (TrackCenterLine* tcl : tcls) {
        if (!tcl)
            continue;

        const QString trackName = tcl->name().isEmpty() ? tcl->id() : tcl->name();
        const QString hFileName = trackName + QStringLiteral("H.ALD");

        AldFileIO::PrjEntry entry;
        entry.hFileName = hFileName;
        entry.trackId   = trackName;
        entries.append(entry);

        const QVector<railway::AlignmentPoint>& hPtsLocal = tcl->horizontal()->rawPoints();
        if (hPtsLocal.isEmpty()) {
            ++failedTracks;
            if (clm) clm->printMessage(
                QStringLiteral("[匯出線形] 略過（無水平線形資料）: %1").arg(trackName));
            continue;
        }

        // ── Local → TM2 ─────────────────────────────────────────────────────
        // TrackCenterLine::horizontal() 內部儲存的是 Local 座標（見
        // ImportAlignmentCommand 的匯入邊界轉換），而 *.ALD 檔案格式要求的是
        // 原始 TM2 絕對座標——這裡是「檔案 I/O」邊界，需轉回 Global 才能寫出
        // 正確的 ALD 內容。
        QVector<railway::AlignmentPoint> hPts = hPtsLocal;
        {
            using aicad::core::geometry::ProjectOrigin;
            ProjectOrigin::ensureDefault();
            auto& origin = ProjectOrigin::instance();
            for (railway::AlignmentPoint& pt : hPts) {
                const QPointF global = origin.toGlobal(pt.easting, pt.northing);
                pt.easting  = global.x();
                pt.northing = global.y();
            }
        }

        const QString hPath = aldDir.filePath(hFileName);
        QString hErr;
        if (!AldFileIO::writeHorizontalALD(hPath, hPts, &hErr)) {
            ++failedTracks;
            if (clm) clm->printMessage(
                QStringLiteral("[匯出線形] %1 水平線形寫出失敗: %2").arg(hFileName, hErr));
            continue;
        }
        ++exportedTracks;

        // ── 依 *H.ALD 檔名推導對應 *V.ALD ───────────────────────────────────
        const QVector<railway::VerticalAlignmentPoint>& vPts = tcl->vertical()->points();
        if (!vPts.isEmpty()) {
            const QString vFileName = AldFileIO::verticalFileNameFor(hFileName);
            if (!vFileName.isEmpty()) {
                const QString vPath = aldDir.filePath(vFileName);
                QString vErr;
                if (AldFileIO::writeVerticalALD(vPath, vPts, &vErr)) {
                    ++withVertical;
                } else if (clm) {
                    clm->printMessage(
                        QStringLiteral("[匯出線形] %1 垂直線形寫出失敗: %2")
                            .arg(vFileName, vErr));
                }
            }
        }
    }

    // ── 3. 寫出 .prj（僅列出成功寫出水平線形的線路） ──────────────────────────
    QVector<AldFileIO::PrjEntry> writtenEntries;
    writtenEntries.reserve(entries.size());
    {
        int idx = 0;
        for (TrackCenterLine* tcl : tcls) {
            if (!tcl)
                continue;
            if (idx < entries.size() && !tcl->horizontal()->rawPoints().isEmpty())
                writtenEntries.append(entries.at(idx));
            ++idx;
        }
    }

    QString prjErr;
    if (!AldFileIO::writePrj(prjPath, writtenEntries, &prjErr)) {
        return CommandResult::Failure(
            prjErr.isEmpty() ? QStringLiteral("寫出 PRJ 檔失敗: %1").arg(prjPath) : prjErr);
    }

    doc->setModified(true);

    // ── 4. 組成結果訊息 ────────────────────────────────────────────────────
    QString summary = QStringLiteral(
        "匯出完成: %1 條線路（其中 %2 條含垂直線形）")
            .arg(exportedTracks).arg(withVertical);
    if (failedTracks > 0) {
        summary += QStringLiteral("，略過 %1 筆（無水平線形資料或寫出失敗）")
            .arg(failedTracks);
    }

    if (exportedTracks == 0)
        return CommandResult::Failure(summary);

    if (clm) clm->printMessage(QStringLiteral("[匯出線形] %1").arg(summary));
    return CommandResult::Success(summary);
}

// ─────────────────────────────────────────────────────────────────────────────
// Static registration — 指令 "exportalignment" 觸發 ExportAlignmentCommand
// ─────────────────────────────────────────────────────────────────────────────
REGISTER_COMMAND("exportalignment", ExportAlignmentCommand);

} // namespace command
} // namespace aicad
