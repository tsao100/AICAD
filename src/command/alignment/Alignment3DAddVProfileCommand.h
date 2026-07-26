#pragma once

/**
 * @file Alignment3DAddVProfileCommand.h
 * @brief ALIGNMENT3DADDVPROFILE（alias: V3D）— 於「3D Alignment」彙總顯示中，
 *        為目前已選取的線路（可多選）加入僅含起訖點（無中間豎曲線）的簡易
 *        垂直線形；起訖高程由使用者在畫面上分別點擊兩條 3D Alignment 折線
 *        取樣而得，起訖里程與各自的平面線形一致。
 *
 * 前置條件
 * ────────
 *   1. Railway 資料夾「3D Alignment」彙總顯示必須已開啟（eyeOpen），
 *      即 Railway3DAlignmentRenderer 目前可見（否則沒有折線可選 / 可點）。
 *   2. 使用者已在該 3D 檢視中，於欲加入垂直線形的折線上完成選取（可多選，
 *      Shift 疊加選取，沿用 CadView 既有的 AIS 選取機制）。
 *
 * 互動流程
 * ────────
 *   execute()
 *     │  讀取目前選取（Railway3DAlignmentRenderer::selectedTcls()）
 *     │  為空 → Failure（請先選取）
 *     ▼
 *   PickStartRef  ── 提示「點擊某條 3D Alignment 折線以取得『起點』高程」
 *     │  POINT_ACQUIRED，且 geomUuid 對應到已註冊的 3D Alignment 折線
 *     │  → 取得該線路在點擊位置的里程 p，以 TrackCenterLine::getZ(p) 取高程
 *     ▼
 *   PickEndRef    ── 提示「點擊某條 3D Alignment 折線以取得『終點』高程」
 *     │  同上，取得第二個高程
 *     ▼
 *   commitAll()   ── 對每條目標線路：
 *                     - 起訖里程＝該線路自身水平線形頭尾里程（rawPoints）
 *                     - 若已有縱斷面資料（vertical 非空）則略過並警告，
 *                       避免覆蓋既有資料
 *                     - 否則 addVip(startCh, startEl, 0) / addVip(endCh, endEl, 0)
 *                       → solve() → 同步回 TrackCenterLine::loadVertical()
 *
 *   任一步驟皆可 POINT_CANCELLED（右鍵 / ESC）中止。
 *
 * 兩個點擊可以點在同一條或不同的 3D Alignment 折線上（可以是目標線路本身，
 * 也可以是任何其他已顯示的參考線路）——純粹依點擊位置在該線路上的最近里程
 * 取樣高程，不要求與目標線路有任何幾何關係。
 *
 * @see Railway3DAlignmentRenderer::selectedTcls()
 * @see AlignmentDocument::VerticalAlignmentEdit::addVip()
 */

#include "command/Command.h"
#include "command/CommandFactory.h"

#include <QPointF>
#include <QString>
#include <QList>

namespace aicad {

namespace railway {
class TrackCenterLine;
}

namespace command {

class Alignment3DAddVProfileCommand : public Command
{
    Q_OBJECT

public:
    explicit Alignment3DAddVProfileCommand(QObject* parent = nullptr);
    ~Alignment3DAddVProfileCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool          isInteractive() const override { return true; }
    QString       getUsage()      const override;

private:
    enum class Step {
        PickStartRef,   ///< 等待點擊取得起點高程
        PickEndRef      ///< 等待點擊取得終點高程
    };

    void handlePointAcquired(const QPointF& point, const QString& geomUuid);
    void handleCancelled();
    void commitAll();
    void cleanup() override;

    /**
     * @brief 從 geomUuid 反查 3D Alignment 折線所屬的 TrackCenterLine，
     *        並回傳該線路在點擊位置 (x,y) 的里程／高程。
     * @return true 表示成功命中一條 3D Alignment 折線。
     */
    bool sampleElevation(const QString& geomUuid, const QPointF& xy,
                         double& outChainage, double& outElevation) const;

    // ── 狀態 ─────────────────────────────────────────────────────────────────
    CommandContext m_ctx;
    Step           m_step        = Step::PickStartRef;
    bool           m_isFinishing = false;

    QList<railway::TrackCenterLine*> m_targets;  ///< 3D 檢視中已選取的目標線路

    double m_startElev = 0.0;
    double m_endElev   = 0.0;
};

} // namespace command
} // namespace aicad
