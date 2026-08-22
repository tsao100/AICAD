/**
 * @file EventBus.h
 * @brief 事件總線系統，用於模組間解耦通訊
 * @author Jack
 * @date 2024-12-04
 */

#ifndef AICAD_CORE_EVENTBUS_H
#define AICAD_CORE_EVENTBUS_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <functional>

namespace aicad {
namespace core {

/**
 * @brief 事件總線，實現發布-訂閱模式
 *
 * EventBus 允許模組之間進行鬆耦合的通訊:
 * - 模組可以訂閱感興趣的事件
 * - 模組可以發布事件通知其他訂閱者
 * - 不需要知道訂閱者的具體類型
 *
 * 使用範例:
 * @code
 * EventBus* bus = app->eventBus();
 *
 * // 訂閱事件
 * bus->subscribe(Events::FEATURE_CREATED, this,
 *     [](const QVariant& data) {
 *         qDebug() << "Feature created:" << data.toString();
 *     });
 *
 * // 發布事件
 * bus->publish(Events::FEATURE_CREATED, featureName);
 * @endcode
 */
class EventBus : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 建構子
     * @param parent 父物件
     */
    explicit EventBus(QObject* parent = nullptr);

    /**
     * @brief 解構子
     */
    ~EventBus() override;

    /**
     * @brief 訂閱事件
     * @param eventName 事件名稱
     * @param receiver 接收者物件 (用於自動取消訂閱)
     * @param callback 回呼函式
     *
     * @note 當 receiver 物件被銷毀時，會自動取消訂閱
     */
    void subscribe(const QString& eventName,
                   QObject* receiver,
                   std::function<void(const QVariant&)> callback);

    /**
     * @brief 發布事件
     * @param eventName 事件名稱
     * @param data 事件資料 (可選)
     *
     * 所有訂閱此事件的回呼函式都會被呼叫
     */
    void publish(const QString& eventName, const QVariant& data = QVariant());

    /**
     * @brief 取消訂閱
     * @param eventName 事件名稱
     * @param receiver 接收者物件
     *
     * 取消指定接收者對某事件的所有訂閱
     */
    void unsubscribe(const QString& eventName, QObject* receiver);

    /**
     * @brief 取消接收者的所有訂閱
     * @param receiver 接收者物件
     */
    void unsubscribeAll(QObject* receiver);

    /**
     * @brief 取得訂閱者數量
     * @param eventName 事件名稱
     * @return 訂閱者數量
     */
    int subscriberCount(const QString& eventName) const;

    /**
     * @brief 清除所有訂閱
     */
    void clear();

Q_SIGNALS:
    /**
     * @brief 事件被發布時發出 (用於除錯)
     * @param eventName 事件名稱
     */
    void eventPublished(const QString& eventName);

private:
    class Private;
    Private* d;
};

/**
 * @brief 標準事件名稱
 */
namespace Events {
// 文件事件
constexpr const char* DOCUMENT_CREATED = "document.created";
constexpr const char* DOCUMENT_OPENED = "document.opened";
constexpr const char* DOCUMENT_CLOSED = "document.closed";
constexpr const char* DOCUMENT_SAVED = "document.saved";
constexpr const char* DOCUMENT_MODIFIED = "document.modified";

// 特徵事件
constexpr const char* FEATURE_CREATED = "feature.created";
constexpr const char* FEATURE_UPDATED = "feature.updated";
constexpr const char* FEATURE_DELETED = "feature.deleted";
constexpr const char* FEATURE_SELECTED = "feature.selected";

// 選取事件
constexpr const char* SELECTION_CHANGED = "selection.changed";

// 視圖事件
constexpr const char* VIEW_CHANGED = "view.changed";
constexpr const char* VIEW_REFRESHED = "view.refreshed";
constexpr const char* VIEW_READY    = "view.ready";

// 命令事件
constexpr const char* COMMAND_STARTED = "command.started";
constexpr const char* COMMAND_EXECUTE_REQUEST = "command.execute.request";
constexpr const char* COMMAND_EXECUTED = "command.executed";
constexpr const char* COMMAND_FAILED = "command.failed";
constexpr const char* COMMAND_FINISHED = "command.finished";
constexpr const char* COMMAND_CANCELLED = "command.cancelled";
constexpr const char* COMMAND_PROMPT = "command.prompt";
constexpr const char* COMMAND_LOG = "command.log";

// ✅ Add interaction events
constexpr const char* POINT_ACQUIRED = "interaction.point_acquired";
constexpr const char* POINT_CANCELLED = "interaction.point_cancelled";
constexpr const char* RUBBER_BAND_UPDATE = "interaction.rubber_update";

// 命令列相關
constexpr const char* COMMAND_ERROR = "command.error";
constexpr const char* COMMAND_WARNING = "command.warning";

// 輸入相關
constexpr const char* COORDINATE_INPUT = "input.coordinate";
constexpr const char* NUMBER_INPUT = "input.number";
constexpr const char* OPTION_SELECTED = "input.option";
constexpr const char* STRING_INPUT = "input.string";
constexpr const char* YESNO_INPUT = "input.yesno";

// 選項相關
constexpr const char* OPTIONS_AVAILABLE = "options.available";
constexpr const char* OPTIONS_CLEARED = "options.cleared";

// 歷史記錄
constexpr const char* HISTORY_UPDATED = "history.updated";

// 草圖生命週期
constexpr const char* SKETCH_ENTERED = "sketch.entered";
constexpr const char* SKETCH_EXITED  = "sketch.exited";

// 草圖幾何選取（與 selection.featureSelected 分離）
constexpr const char* SKETCH_GEOM_SELECTED = "sketch.geometrySelected";
constexpr const char* SKETCH_GEOM_CLEARED  = "sketch.geometryCleared";

// ── Phase 6：約束覆蓋 & SketchInstance 事件 ─────────────────────────────
constexpr const char* CONSTRAINT_SYMBOL_TOGGLE      = "constraint.symbol.toggle";
constexpr const char* CONSTRAINT_DIM_CLICKED        = "constraint.dim.clicked";
constexpr const char* CONSTRAINT_DIM_EDIT_DONE      = "constraint.dim.edit.done";
constexpr const char* SKETCH_INSTANCE_PARAM_CHANGED = "sketchinstance.param.changed";
constexpr const char* SKETCH_INSTANCE_CREATED       = "sketchinstance.created";

// ── General Dimension 互動事件 ──────────────────────────────────────────────
constexpr const char* GEOM_PICKED        = "input.geom_picked";
constexpr const char* GEOM_HOVER         = "input.geom_hover";        ///< GetGeom／GetPoint 模式 mouseMoveEvent hover（geomUuid / handle / point）
constexpr const char* DIM_LINE_CONFIRMED = "input.dim_line_confirmed";
constexpr const char* DIM_LINE_PREVIEW   = "input.dim_line_preview";  ///< 拖曳預覽（非 pickSession 路徑）

// ── GDIM v2 Phase 2：多候選引擎（取代字母選單）─────────────────────────────
/// payload: QVariantMap { "direction": int }  +1 = 下一個候選（Tab/Space）、
/// -1 = 上一個候選（Shift+Tab）。由 CadView::keyPressEvent 在 GetGeom /
/// WaitCandidate 情境下按 Tab/Space 時發布，取代舊的「輸入字母選單」機制。
/// 確認目前高亮候選沿用既有 STRING_INPUT（Enter 空白輸入 = 採用預設/當前
/// 高亮候選），不另外新增事件。
constexpr const char* CANDIDATE_CYCLE     = "input.candidate_cycle";

// ── GDIM v2 Phase 4：Mini Toolbar ──────────────────────────────────────────
/// payload: QString（annotationUuid）。點選一個有對應 SketchAnnotation 的
/// 尺寸線時發布，供 PropertyPanel（Mini Toolbar）顯示 Prefix/Suffix/
/// Tolerance/Precision/Basic/Inspection 欄位。
constexpr const char* ANNOTATION_SELECTED = "input.annotation_selected";

// ── ProjectOrigin / TM2 雙座標系統 ─────────────────────────────────────────
/// ProjectOrigin::setOrigin() 或 clear() 呼叫後發布。
/// payload: QVariantMap { "isSet": bool, "originE": double, "originN": double,
///                        "originZ": double, "epsgCode": QString }
constexpr const char* PROJECT_ORIGIN_CHANGED = "project.origin.changed";
}

} // namespace core
} // namespace aicad

#endif // AICAD_CORE_EVENTBUS_H