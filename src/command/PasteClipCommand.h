/**
 * @file PasteClipCommand.h
 * @brief PASTECLIP — 把剪貼簿裡「從 AutoCAD 複製來的圖」貼進目前草圖
 *        （別名 PC；草圖編輯模式下 Ctrl+V 也會觸發）。
 *
 * 來源
 *   AutoCAD 的 Ctrl+C 放在剪貼簿的 EMF 圖片。VirtualBox 共用剪貼簿不傳 EMF，所以在
 *   Windows 端先執行 tools/AICAD-CopyEmf.ps1，把 EMF 轉成 "AICAD-EMF:<base64>" 文字
 *   放回剪貼簿（文字能跨過去），本命令在 Ubuntu 端解碼。EMF 只是預覽圖，座標是螢幕
 *   像素，沒有原始座標，所以比例要靠「比例尺標記」（見下）或手動輸入倍率。
 *
 * 背景（EMF）
 *   AutoCAD 的 Ctrl+C 會同時往剪貼簿放好幾種格式：AutoCAD 私有二進位、
 *   Enhanced Metafile (EMF，向量圖片)、點陣圖…。私有格式無法解讀，但 EMF
 *   裡是一條條「線／折線／弧」的繪圖指令，可以還原成草圖幾何。
 *   解析工作在 core/geometry/EmfVectorParser（純 C++，有獨立測試），本命令只負責
 *   「取剪貼簿 → 解析 → 指定插入點 → 建立草圖幾何 → 輸入倍率校正」。
 *
 * ⚠️ 重要限制（EMF 是圖片，不是 CAD 資料）
 *   - 只有「形狀」可信；絕對尺寸取決於 AutoCAD 複製當下的螢幕縮放，所以貼上後
 *     要用倍率校正（見下）。圖元不帶圖層／顏色／線型；文字（EMF 內是字型輪廓）、
 *     點陣圖、HATCH 填色都不會進來，貼上完成時會列出略過的數量。
 *   - AutoCAD 把圓／弧輸出成多段折線；解析器會把「夠密、且全部落在同一個圓上」
 *     的整條折線還原成真正的 Circle/Arc（容差見 EmfVectorParser.h）。
 *     無法還原的曲線（樣條、橢圓弧）維持折線。
 *
 * 自動比例（比例尺標記）
 *   在 AutoCAD 圖形的左下角畫一個「L」：一條水平線＋一條垂直線，等長（預設 1 單位），
 *   共用左下角的角點，且兩臂遠端不接其他圖形。PASTECLIP 找到後會
 *     - 用「標記實際長度 ÷ 標記在剪貼簿圖片中的長度」自動算出倍率並套用
 *     - 把標記的兩條線移除（不會貼進草圖）
 *     - 以標記角點作為插入基準點（點的插入點 = 角點位置，圖形相對位置即真實座標差）
 *   標記長度預設 1；不是 1 時，在「指定插入點」提示下輸入 L（Length）再輸入長度，
 *   比照 CHAMFER 的 [Distance(D)]。長度存在 QSettings("AICAD","AICAD") 群組
 *   "PasteClipCommand"，下次執行沿用。
 *   ⚠️ 精度 ≈ ±1 像素 ÷ 標記像素長度。標記太短（< 約 100 像素）時會提示；請把標記
 *   畫得夠長（例如圖寬的 1/10 以上），並用 L 選項告知實際長度。
 *   有標記：點完插入點倍率自動套用，命令隨即結束（不需再按 Esc）。
 *   找不到標記：1:1 貼上後進入手動輸入倍率階段（倍數／W／H，Esc 結束）。
 *
 * 互動流程
 *   PASTECLIP
 *     → 指定插入點（圖形範圍的左下角對齊此點）
 *     → 以 1:1（EMF 的毫米數值當草圖單位）建立幾何
 *     → 輸入倍率重新縮放：  0.001      絕對倍率（相對於剛貼上的原始大小）
 *                            W 120.5    讓整體寬度 = 120.5
 *                            H 80       讓整體高度 = 80
 *                          可重複輸入；Esc 結束（保留目前結果）。
 *   PASTECLIP <file.emf>   改從 EMF 檔讀取（非 Windows 平台測試用）。
 *
 * Undo：沿用 UIManager 的草圖快照機制，整個 PASTECLIP（含縮放）合併成一筆。
 */
#pragma once

#include "Command.h"
#include "../core/geometry/EmfVectorParser.h"

#include <AIS_Shape.hxx>
#include <Standard_Handle.hxx>

#include <QMetaObject>
#include <QStringList>
#include <QVariant>
#include <QVector2D>

namespace aicad {
namespace cad { class Sketch; }

namespace command {

class PasteClipCommand : public Command {
    Q_OBJECT
public:
    PasteClipCommand();

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    enum class State { Idle, WaitInsertPoint, WaitMarkerLen, WaitScale };

    cad::Sketch* activeSketch() const;

    /// 取得 EMF 位元組（檔案、剪貼簿文字 AICAD-EMF:<base64>、或 Windows 原生剪貼簿）。
    /// 失敗時已把原因印到命令列。
    bool loadEmf(const QStringList& args, std::vector<unsigned char>& bytes);

    void onPointAcquired(const QVariant& payload);
    void onNumberInput(const QVariant& payload);
    void onOptionSelected(const QVariant& payload);
    void onCancelled(const QVariant&);

    void insertAt(const QVector2D& insertPt);
    /// 「指定插入點或 [Length(L)]」提示；Option 輸入與滑鼠點取並存（比照 CHAMFER 的 [Distance(D)]）
    void beginInsertStage();
    /// [Length(L)]：輸入標記長度（寫入 QSettings，下次執行沿用）
    void beginMarkerLengthStage();

    // ── 插入點預覽（幽靈圖形跟隨游標）─────────────────────────────────
    // 指定插入點時，把即將貼上的圖形以 AIS_Shape 暫時顯示、隨游標移動；完全不碰草圖
    // 資料（不建立任何 Geometry／Constraint），所以不影響 undo 快照，取消時也沒有殘留。
    // 游標來源沿用 MOVE/COPY 的機制：RubberBand 設成非 None 模式（不加任何點，所以它
    // 自己不畫東西），CadView 就會在 GetPoint 模式下持續餵游標並發出 updated()。
    void showInsertPreview();
    void hideInsertPreview();
    void updateInsertPreview();
    double initialFactor() const;                 ///< 初始倍率：有標記 = 標記長度 ÷ 圖片中標記長度，否則 1
    core::emf::Pt anchorPoint() const;            ///< 對齊插入點的基準：標記角點，或圖形左下角

    static double loadMarkerLength();            ///< QSettings("AICAD","AICAD") / PasteClipCommand / markerLength，預設 1
    static void   saveMarkerLength(double len);
    void applyAbsoluteFactor(double factor);
    void printSummary() const;

    void subscribeAll();
    void unsubscribeAll();
    void cleanup();

    State                   m_state = State::Idle;
    core::emf::Result       m_result;
    QVector2D               m_insertPt;
    QStringList             m_created;
    double                  m_factor = 1.0;   ///< 目前相對於「剛貼上」大小的累積倍率
    core::emf::ScaleMarker  m_marker;
    Handle(AIS_Shape)       m_ghost;
    QMetaObject::Connection m_ghostConn;
    double                  m_markerLen = 1.0;   ///< 標記的實際長度（CAD 單位）
};

} // namespace command
} // namespace aicad
