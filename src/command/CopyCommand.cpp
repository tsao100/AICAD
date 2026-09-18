/**
 * @file CopyCommand.cpp
 * @brief 見 CopyCommand.h 檔頭說明。
 */
#include "CopyCommand.h"

#include "../core/CommandLineManager.h"
#include "../cad/Sketch.h"

namespace aicad {
namespace command {

using namespace cad;

CopyCommand::CopyCommand()
    : SketchTransformCommandBase("COPY", "Copy and move selected sketch geometry (alias: CO)")
{
}

QString CopyCommand::getUsage() const
{
    return "Usage: COPY — select geometry first then run COPY, "
           "or run COPY then click objects (window/crossing/fence supported) "
           "and press Enter or right-click, "
           "then specify base point and second point.";
}

QString CopyCommand::selectPrompt() const
{
    return "[COPY] Select objects, then press Enter or right-click:";
}

QString CopyCommand::basePointPrompt() const
{
    return "[COPY] Specify base point:";
}

QString CopyCommand::secondPointPrompt() const
{
    return "[COPY] Specify second point (displacement target):";
}

void CopyCommand::commit(cad::Sketch* sketch, const QStringList& selection,
                         const cad::transform::Transform2D& xf)
{
    const QStringList created = transform::cloneAndTransform(sketch, selection, xf);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        if (!created.isEmpty())
            cmdMgr->printSuccess(QString("✅ Copied %1 object(s).").arg(created.size()));
        else
            cmdMgr->printWarning("⚠️  Nothing was copied.");
    }
}

QStringList CopyCommand::armLivePreviewTargets(cad::Sketch* sketch)
{
    // 以零位移建立一份「疊在原物件正上方」的預覽用複製品（連同可複製的
    // 約束，見 CopyCommand.h／cloneAndTransform() 的說明）。之後每一幀
    // 只搬動這份複製品，原選取範圍完全不受影響。
    return transform::cloneAndTransform(sketch, selection(),
        transform::Transform2D::translation(QVector2D()));
}

void CopyCommand::teardownLivePreviewTargets(cad::Sketch* sketch, const QStringList& targets)
{
    // 預覽用複製品只是暫時的視覺回饋：確認時 commit() 會重新做一次正式
    // 複製，取消時整個操作作廢——兩種情況都要把這份複製品整個刪掉，
    // 避免留下重複或殘留的幾何。removeGeometry() 會一併清掉複製品身上
    // （含複製約束時新增出來的）約束/標註。
    if (!sketch || targets.isEmpty()) return;
    for (const QString& uuid : targets)
        sketch->removeGeometry(uuid);
    // ⚠️ 效能修正：removeGeometry() 自己在每次呼叫結尾就已經 emit 一次
    // rebuildRequested()（見 Sketch::removeGeometry()），迴圈跑完後這裡
    // 又再手動 emit 一次，是這 N+1 次裡最後那個多出來的重複；先移除，
    // 若要徹底解決「刪 N 個幾何觸發 N 次完整 rebuild」，需要另外提供
    // 批次刪除的 API（一次迴圈只 emit 一次），這裡先不動 removeGeometry()
    // 本身的行為以免影響其他呼叫端。
}

} // namespace command
} // namespace aicad
