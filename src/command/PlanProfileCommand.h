/**
 * @file PlanProfileCommand.h
 * @brief PLANPROFILE（alias: PPS）— 沿線形分段自動產生平縱面圖圖紙
 *
 *   PPS                開啟參數對話框（預設值＋可調整），確認後產生圖紙並開啟第一張
 *   PPS GEN [線形]     直接以「我的預設」（或作用中平縱面圖的設定）產生
 *   PPS LOAD <檔>      載入 *.aicad_ppcfg 設定檔後產生
 *   PPS SAVE <檔>      把目前（作用中平縱面圖／我的預設）的設定存成設定檔
 *   PPS INFO           列出線形、里程範圍、分段預覽
 *
 * 產生的每張圖紙以 DrawingSheet::planProfile() 保存設定與里程範圍，
 * 隨 .aicad 存檔；也可在 DSH 圖紙對話框中預覽、匯出 PDF／SVG。
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"

#include <QPointF>

namespace aicad {
namespace command {

class PlanProfileCommand : public Command
{
public:
    PlanProfileCommand();

    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

/**
 * DEPOTVIEW（alias: DV）— 機廠平面圖的矩形視圖：在 View 中以點選設定位置、轉角、大小，並可排序。
 *
 *   DV                 互動：①在一段直線上點兩點（決定轉角）②點矩形第一角③點對角 → 新增一個視圖
 *   DV LIST            列出視圖
 *   DV ADD 名稱 E N 寬 高 [轉角 排序 比例]   以 TM2 座標直接新增
 *   DV SET 序號 key=值 …   key：name e n w h rot order scale align（線形名／id，- 取消）ch
 *   DV DEL 序號        刪除視圖
 *   DV SIZE            列出常用比例下矩形視圖的最大尺寸建議值
 *   DV ORDER 序號 [新序號]  變更視圖序號
 *   DV EDIT 序號       在 View 中以滑鼠編輯（拖放／旋轉／調整大小／對齊／新增緊鄰矩形）
 *   DV GEN             依目前視圖重算圖冊
 */
class DepotViewCommand : public Command
{
    Q_OBJECT
public:
    DepotViewCommand();
    ~DepotViewCommand() override;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    void handlePoint(const QPointF& p);
    void handleCancelled();
    void cleanup() override;
    void prompt(const QString& msg);

    int     m_stage = 0;            ///< 0 轉角點 A、1 轉角點 B、2 第一角、3 對角
    QPointF m_a, m_b, m_c1;
    bool    m_done = false;
};

} // namespace command
} // namespace aicad
