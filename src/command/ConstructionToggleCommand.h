/**
 * @file ConstructionToggleCommand.h
 * @brief CONSTRUCTION — 將選取的草圖幾何（線／弧／圓…）在「建構
 *        （Construction）」與「一般（Normal）」之間來回切換（別名 CT）。
 *
 * 背景／需求：
 *   建構線／弧／圓除了「不參與輪廓（profile/contour）偵測」外，其他所有
 *   功能都必須與一般幾何完全相同——包含可被滑鼠選取、參與 OSnap、
 *   TRIM/EXTEND/FILLET/CHAMFER/MIRROR/ROTATE/MOVE/COPY/STRETCH/ERASE、
 *   以及 GDIM 標註／幾何約束。目前繪圖階段已可透過 SketchPanel「建構幾何」
 *   群組（建構線／中心線／建構圓）直接畫出建構幾何，但畫出來以後沒有任何
 *   方式能把「已存在的一般幾何」轉成建構幾何，或反向轉回——本命令補上
 *   這個缺口。
 *
 * 切換規則（二元切換，Centerline 視同「非 Normal」）：
 *   - 目前是 GeomRole::Normal      → 切換為 GeomRole::Construction
 *   - 目前是 GeomRole::Construction 或 GeomRole::Centerline
 *                                   → 切換為 GeomRole::Normal
 *
 * 兩種使用方式（比照 ERASE/MIRROR 等命令的模式 A／B）：
 *   - 模式 A：先在視圖中選取幾何，再輸入 CONSTRUCTION
 *             → ctx.args 已含選取的 UUID，直接切換，命令同步完成。
 *   - 模式 B：直接輸入 CONSTRUCTION，尚未選取任何幾何
 *             → 進入互動選取（SketchSelectionPicker::Mode::PickMultiple），
 *               點選一個或多個幾何、按 Enter 確認即切換，Esc 取消。
 *
 * 草圖平面參考幾何（X 軸／Y 軸／原點）為固定參考，不可被此命令選取/切換
 * （比照 ERASE/MIRROR 的 isFixedReferenceUuid()）。
 */
#pragma once

#include "Command.h"
#include "SketchSelectionPicker.h"

#include <QStringList>
#include <QVariant>

namespace aicad {
namespace cad { class Sketch; }

namespace command {

class ConstructionToggleCommand : public Command {
    Q_OBJECT
public:
    ConstructionToggleCommand();

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    cad::Sketch* activeSketch() const;

    void beginSelection(cad::Sketch* sketch);
    void onSelectionConfirmed(const QStringList& uuids);
    void onSelectionCancelled();

    /// 實際套用切換並印出結果訊息；不負責 cleanup()/complete()（由呼叫端決定）。
    void applyToggle(cad::Sketch* sketch, const QStringList& uuids);

    void cleanup();

    SketchSelectionPicker* m_picker = nullptr;
};

} // namespace command
} // namespace aicad
