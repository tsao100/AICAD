/**
 * @file UIManager.h
 * @brief UI 管理器，管理所有使用者介面元件
 * @author James
 * @date 2025-01-07
 */

#ifndef AICAD_UI_UIMANAGER_H
#define AICAD_UI_UIMANAGER_H

#include <QObject>
#include <QString>
#include <QMainWindow>
#include <QUndoStack>
#include "osnap/OSnapToolbar.h"
#include "cad/sketch/SketchRegion.h"
#include "cad/SketchInstance.h"
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>

namespace aicad {

// 前向宣告
namespace cad {
    class Document;
    class Feature;
    class Sketch;
    struct SketchGeometry;
    class ConstraintPickSession;   // Phase 7
    enum class ConstraintType;
    struct SketchRegion;
}

namespace view {
    class CadView;
    class Railway3DAlignmentRenderer;
}

namespace core {
    class MenuParser;
    class CommandLineManager;  // ✅ 新增
}

namespace command {
    class CommandAlias;  // ✅ 新增
}

namespace railway {
    class AlignmentDocument;
}

namespace ui {

class MainWindow;
class FeatureBrowser;
class PropertyPanel;
class ToolManager;
class CommandLineWidget;
class TransientCommandHistory;
class AutoCompleteModel;     // ✅ 新增
class VAlignEditorDockWidget; // Step 16

TopoDS_Face buildFaceFromRegion(
    const cad::Sketch* sketch,
    const cad::SketchRegion& region);

void highlightRegionFace(const TopoDS_Face& face,
                         const Handle(AIS_InteractiveContext)& context);

TopoDS_Edge makeEdgeFromGeometry(const aicad::cad::Sketch* sketch,
                                 const aicad::cad::SketchGeometry* g);

/**
 * @brief UI 管理器
 * 
 * UIManager 管理應用程式的所有 UI 元件：
 * - 主視窗
 * - 特徵瀏覽器
 * - 屬性面板
 * - 工具列管理
 * 
 * 使用範例：
 * @code
 * UIManager* uiMgr = app->uiManager();
 * uiMgr->showMainWindow();
 * uiMgr->updateFeatureTree();
 * @endcode
 */
class UIManager : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     * @param parent 父物件
     */
    explicit UIManager(QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~UIManager() override;
    
    /**
     * @brief 初始化 UI 系統
     * @return 成功回傳 true
     */
    bool initialize(core::MenuParser* menuParser);
    void initGripSystem();

    /**
     * @brief 顯示主視窗
     */
    void showMainWindow();
    
    /**
     * @brief 取得主視窗
     * @return 主視窗指標
     */
    MainWindow* mainWindow() const;
    
    /**
     * @brief 取得特徵瀏覽器
     * @return 特徵瀏覽器指標
     */
    FeatureBrowser* featureBrowser() const;
    
    /**
     * @brief 取得屬性面板
     * @return 屬性面板指標
     */
    PropertyPanel* propertyPanel() const;
    
    /**
     * @brief 取得工具管理器
     * @return 工具管理器指標
     */
    ToolManager* toolManager() const;
    
    /**
     * @brief 更新特徵樹
     */
    void updateFeatureTree();
    
    /**
     * @brief 設定狀態列訊息
     * @param message 訊息內容
     * @param timeout 顯示時間（毫秒），0 表示永久顯示
     */
    void setStatusMessage(const QString& message, int timeout = 0);
    view::CadView* cadView() const;

    // Phase 7：供 DimConstraintCommand 取得 pickSession
    cad::ConstraintPickSession* constraintPickSession() const;

    /**
     * @brief 啟動幾何約束互動選取模式
     *
     * 供 GeomConstraintCommand 呼叫：
     *  1. 呼叫 session->begin(sketch, type, 0)
     *  2. 切換 CadView 到 GetGeom 模式
     *  3. 設定 status bar 提示
     *
     * constraintReady signal 已在初始化時連接到 SketchPanel::onConstraintReadyFromSession。
     */
    void beginGeomConstraintPick(cad::Sketch* sketch,
                                 cad::ConstraintType type,
                                 int requiredCount);

    void setupMenusFromParser();
    void setupToolbarsFromParser();
    void executeCommand(const QString& commandId);
    void setupDefaultUI();
    void onNewDocument();
    void onOpenDocument();
    void onSaveDocument();

    void onViewReady();

    /**
     * @brief 取得命令列覆蓋層
     * @return  CommandLineWidget 指標
     */
    CommandLineWidget* commandLine() const;

    /**
     * @brief 取得命令列管理器
     * @return CommandLineManager 指標
     */
    core::CommandLineManager* commandLineManager() const;

    /**
     * @brief 取得命令別名管理器
     * @return CommandAlias 指標
     */
    command::CommandAlias* commandAlias() const;

    /**
     * @brief 在命令列顯示訊息
     * @param message 訊息內容
     * @param color 文字顏色 (預設白色)
     */
    void showCommandMessage(const QString& message, const QString& color = "white");

    /**
     * @brief 在命令列顯示錯誤
     * @param error 錯誤訊息
     */
    void showCommandError(const QString& error);

    /**
     * @brief 在命令列顯示警告
     * @param warning 警告訊息
     */
    void showCommandWarning(const QString& warning);

    cad::Sketch* currentActiveSketch();

    QUndoStack* undoStack() const;

    /// Railway alignment document (owned by UIManager)
    railway::AlignmentDocument* alignmentDocument() const;

    /// All per-TCL AlignmentDocument instances (key = tclId)
    const QHash<QString, railway::AlignmentDocument*>& tclAlignmentDocs() const;

    /**
     * @brief 取得（若不存在則建立）指定 TCL 的 AlignmentDocument，並視需要
     *        從其既有的 raw/dense 資料反推一次元素鏈／VIP 清單（與
     *        showAlignmentDataTableRequested / railway.valign-visibility-changed
     *        兩處既有邏輯相同，抽出供命令層等其他呼叫者共用，避免各自維護
     *        一份可能產生分歧結果的重建邏輯）。
     * @return 找不到對應 TrackCenterLine 時回傳 nullptr。
     */
    railway::AlignmentDocument* ensureTclAlignmentDocument(const QString& tclId);

    /// Railway 資料夾「3D Alignment」彙總顯示（可能為 nullptr，直到第一次
    /// eyeOpen 觸發建立）。
    view::Railway3DAlignmentRenderer* railway3DRenderer() const;

    /// Step 16: Vertical-alignment dock widget
    ui::VAlignEditorDockWidget* vAlignDockWidget() const;

Q_SIGNALS:
    /**
     * @brief UI 初始化完成時發出
     */
    void initialized();
    
    /**
     * @brief 主視窗關閉時發出
     */
    void mainWindowClosed();

public slots:
    void onSketchEditStarted(cad::Sketch* sketch);
    void onSketchEditEnded();

    /** Phase 3/7：在 ParameterPanel 中顯示 SketchInstance 覆寫參數 */
    void showInstanceInParameterPanel(cad::SketchInstance* instance);

    
private:

    void initializeReferenceGeometry();
    void onDocumentCreated();
    void onCurrentDocumentChanged(cad::Document* doc);

    void setupCommandLine();
    void connectCommandLineEvents();

    /**
     * @brief 統一設定「目前作用中」的 AlignmentDocument（d->alignmentDoc），
     *        並同步把它接到 CadView 的 OSnapManager／OSnapDetector，讓
     *        Alignment edit（H-alignment grip 拖曳）時，OSnap 也能吃到
     *        目前可視 alignment 的鎖點（TS/SC/CS/ST／IP 等）。
     *
     *        OSnapDetector 原本就有完整的 Alignment snap 偵測邏輯
     *        （setAlignmentDocument()／detect() 內的 alignment 分支），但
     *        該 setter 從未被任何呼叫端呼叫過，永遠是 nullptr，導致這段
     *        偵測邏輯形同虛設。所有原本直接寫 `d->alignmentDoc = ...` 的
     *        地方都應改呼叫這個方法，避免遺漏。
     */
    void setActiveAlignmentDoc(railway::AlignmentDocument* doc);

    /**
     * @brief 重新掃描目前文件的所有 TrackCenterLine，把每一條的
     *        tcl->horizontal() 推給 OSnap（見 setActiveAlignmentDoc() 內的
     *        說明）。
     *
     *        setActiveAlignmentDoc() 只會在使用者實際做了某些操作時被呼叫
     *        （開啟垂直斷面 dock、切換水平/垂直可視性、進入編輯…），如果
     *        使用者開檔後，alignment 一開始就顯示著、卻完全沒有觸發上述任何
     *        一個分支就直接下 FC/AS 等取點指令，OSnap 的 alignment 來源清單
     *        會維持在初始的空清單，導致「畫面上明明看得到、卻怎麼樣都吸不
     *        到」。這個方法在文件載入／切換時（onCurrentDocumentChanged()）
     *        主動呼叫一次，確保不依賴使用者是否恰好走過那幾個特定入口。
     */
    void refreshAlignmentOSnapSources();

    osnap::OSnapToolbar* m_snapToolbar = nullptr;
    cad::Sketch* m_currentActiveSketch = nullptr;

    void setupSketchPanel();
    void applyConstraintToSketch(cad::Sketch* sketch,
                                cad::ConstraintType type,
                                const QStringList& selected,
                                double value);

    void showFeatureProperties(cad::Feature* feature);

    /**
     * @brief GDIM v2 Phase 4：顯示標註（SketchAnnotation）屬性 Mini Toolbar
     *
     * 於 PropertyPanel 顯示 Prefix/Suffix/Tolerance/Precision/
     * Basic Dimension/Inspection Dimension 欄位，並讓
     * PropertyPanel::propertyChanged 的寫回目標指向這個 (sketch, uuid)。
     * 這些屬性只影響顯示（不影響幾何求解），寫回時走
     * Sketch::addAnnotation() → ConstraintOverlayManager 的輕量刷新路徑
     * （見 Sketch::annotationAdded 訊號），不觸發完整 rebuildAll()/重新求解。
     */
    void showAnnotationProperties(cad::Sketch* sketch, const QString& annotationUuid);

    class Private;
    Private* d;
};

} // namespace ui
} // namespace aicad

#endif // AICAD_UI_UIMANAGER_H