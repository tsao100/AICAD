/**
 * @file SketchSelectionPicker.h
 * @brief Phase 0 共用建設：可重用的草圖互動選取器（見 AICAD_SketchEdit_
 *        Advanced_Commands_Plan.md §2.1）。
 *
 * 從 EraseCommand 的「模式 B：互動取幾何」邏輯抽出、泛化而成，供
 * MOVE/COPY/ROTATE/MIRROR/TRIM/EXTEND/FILLET/CHAMFER 等命令共用「輸入命令
 * 後、於視圖中逐一點選幾何、Enter 確認／Esc 取消」的互動生命週期，避免
 * 每個命令各自複製貼上一份幾乎相同的
 * GEOM_PICKED / STRING_INPUT / COMMAND_CANCELLED 訂閱邏輯。
 *
 * 與 EraseCommand 原邏輯的差異（刻意簡化）：
 *  - 本選取器只處理「幾何」選取，不像 EraseCommand::onGeomPicked() 那樣
 *    在幾何沒命中時 fallback 去偵測尺寸線/約束符號——MOVE/COPY/ROTATE/
 *    MIRROR/TRIM/EXTEND/FILLET/CHAMFER 這幾個命令操作對象本來就是幾何，
 *    選取到 SketchConstraint（標註/約束）沒有意義。
 *  - 草圖平面參考幾何（X 軸／Y 軸／原點，UUID 前綴 "sketch_xaxis:" 等）
 *    一律視為不可選取的固定參考，比照 EraseCommand 的 isFixedReferenceUuid()。
 *
 * CrossingWindow（框選，供 STRETCH 使用）於 Phase 0 尚未實作：目前若呼叫
 * begin() 時傳入 Mode::CrossingWindow，會直接印出警告並發出 cancelled()。
 * 待 Phase 3 確認/補齊 CadView 的窗選矩形機制後再啟用，避免在還沒有對應
 * 基礎設施的情況下呼叫不存在的 API。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace aicad {
namespace cad { class Sketch; }

namespace command {

class SketchSelectionPicker : public QObject {
    Q_OBJECT

public:
    enum class Mode {
        /// 可累積點選多個幾何（再次點擊已選取的物件會取消選取），
        /// 按 Enter（STRING_INPUT 空白輸入）確認。MOVE/COPY/ROTATE/MIRROR/
        /// STRETCH 的「選取要變換的物件」步驟使用本模式。
        PickMultiple,

        /// 點選一個幾何立即確認，不需要按 Enter。TRIM/EXTEND 逐次點選
        /// 「要裁切/延伸的那一段」、FILLET/CHAMFER 點選「第一個/第二個
        /// 物件」時使用本模式（每次呼叫 begin() 對應一次單一選取）。
        PickSingle,

        /// 框選（Crossing Window）。供 STRETCH 使用，Phase 0 尚未實作。
        CrossingWindow,
    };

    explicit SketchSelectionPicker(QObject* parent = nullptr);
    ~SketchSelectionPicker() override;

    /**
     * @brief 開始互動選取。
     * @param sketch 目前作用中的草圖（呼叫端負責確保非 nullptr 且為目前
     *               active sketch；本函式不重複檢查 Application::activeSketch()）。
     * @param mode   選取模式。
     * @param prompt 顯示在命令列的提示文字（PickMultiple 模式下，本選取器
     *               會在提示文字後自動附加「已選取 N 個」等即時狀態）。
     *
     * 呼叫端應在呼叫 begin() 之前，先將 CadView 切到 GetGeom 模式並呼叫
     * Command::setWaitingForInput()（比照 EraseCommand::execute() 模式 B），
     * 本函式不會重複做這兩件事，理由是：不同命令在「選取階段」與後續
     * 「取基準點/取角度」階段之間，Running 狀態是連續的（一個命令的一次
     * 執行只應該呼叫一次 setWaitingForInput()），交由呼叫端統一管理。
     */
    void begin(cad::Sketch* sketch, Mode mode, const QString& prompt);

    /// 目前已累積的選取（僅 PickMultiple 模式下有意義）。
    const QStringList& pending() const { return m_pending; }

    /// 呼叫端主動提前結束（例如切換到別的互動階段），會清除訂閱與高亮，
    /// 但不會發出 confirmed()／cancelled() 訊號。
    void abortSilently();

Q_SIGNALS:
    /// 使用者按 Enter（PickMultiple）或點選了一個幾何（PickSingle）確認。
    /// PickMultiple 模式下，即使 geomUuids 是空清單（使用者直接按 Enter
    /// 沒有點任何東西）也會發出本訊號——空清單對不同呼叫端意義不同
    /// （例如 TRIM/EXTEND 把「空選」當作「全選作為剪切邊/邊界邊」），
    /// 本選取器不預設立場，由呼叫端自行判斷 geomUuids.isEmpty()。
    void confirmed(const QStringList& geomUuids);

    /// 使用者按 Esc 取消（COMMAND_CANCELLED）。
    void cancelled();

private:
    void subscribeAll();
    void unsubscribeAll();
    void cleanup();

    void onGeomPicked(const QVariant& data);
    void onConfirm(const QVariant& data);
    void onCancelled(const QVariant& data);

    void setHighlight(const QString& uuid, bool on);
    void updatePrompt();

    cad::Sketch* m_sketch = nullptr;
    Mode         m_mode   = Mode::PickMultiple;
    QString      m_basePrompt;
    QStringList  m_pending;
    bool         m_active = false;
};

} // namespace command
} // namespace aicad
