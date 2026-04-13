#ifndef COMMANDLINEWIDGET_H
#define COMMANDLINEWIDGET_H

#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QTextEdit>
#include <QToolButton>
#include <QMenu>
#include <QPropertyAnimation>
#include <QTimer>
#include <QSettings>

namespace aicad {
namespace ui {

class CommandInputEdit;
class TransientCommandHistory;
class CommandHistoryPopup;

// 命令列主 Widget（可 dock / float）
class CommandLineWidget : public QWidget {
    Q_OBJECT

public:
    explicit CommandLineWidget(QWidget* cadView, QWidget* parent = nullptr);
    ~CommandLineWidget() override;

    // 命令進行中：顯示提示選項按鈕
    void setCommandOptions(const QStringList& options);
    void clearCommandOptions();

    // 歷程追加
    void appendHistory(const QString& text, bool isPrompt = false);

    // 儲存/還原
    void saveState(QSettings* settings);
    void restoreState(QSettings* settings);

    // Ctrl+9 切換顯示
    void toggleVisible();

    CommandInputEdit* inputEdit();
    TransientCommandHistory* transientHistory() const;

    // 對齊 CadView
    void alignToCadView();

    // 程式化提交命令（等同使用者手動輸入並按 Enter）
    // 工具列、FeatureBrowser context menu 等非 UI 輸入均應呼叫此方法
    void submitCommand(const QString& cmd);
    // 讓 UIManager 在 alias resolve 後以正確名稱修正歷程
    void recordResolvedCommand(const QString& resolved);

signals:
    void commandSubmitted(const QString& cmd);
    void optionSelected(const QString& key);
    void historyPopupRequested();   // 等同 F2

public slots:
    void onHistoryButtonClicked();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void buildSingleRow();          // 建立單行排版
    void buildMultiRow();           // 建立多行排版
    void switchLayout(bool multiRow);
    bool isMultiRowMode() const;

    void setupGripper();
    void setupCloseButton();
    void setupCustomizeButton();
    void setupComboAndInput();
    void setupHistoryButton();
    void setupOptionsBar();
    void setupTransientHistory();
    void setupResizeHandles();

    void onCustomizeMenu();
    void onInputSubmit(const QString& text);
    void onRecentMenuTriggered(QAction* action);

    // 右邊緣/上邊緣拖曳
    void updateResizeCursor(const QPoint& pos);
    void startResize(const QPoint& pos);
    void doResize(const QPoint& pos);
    void doResizeFloating(const QPoint& delta);
    void doResizeEmbedded(const QPoint& delta);
    void endResize();

    void checkSnapToEdge();
    void attachToCadView();
    void detachToCadView();
    void installMouseFilterOnChildren(QWidget* parent);

    static constexpr int SNAP_THRESHOLD = 30;   // px，可依需求調整

    // --- 子 Widget ---
    QWidget*    m_cadView         = nullptr;

    // 單行容器
    QWidget*    m_singleRowBar    = nullptr;
    QHBoxLayout* m_singleRowLayout= nullptr;

    // 多行左欄按鍵
    QWidget*    m_leftButtonCol   = nullptr;
    QVBoxLayout* m_leftColLayout  = nullptr;

    // 右側垂直分割（歷程上、輸入下）
    QSplitter*  m_rightSplitter   = nullptr;
    QTextEdit*  m_historyView     = nullptr;

    // 命令輸入區 + ComboBox + 歷程按鈕（共用列）
    QWidget*    m_inputRow        = nullptr;
    QHBoxLayout* m_inputRowLayout = nullptr;

    QToolButton* m_recentButton   = nullptr;
    QMenu*       m_recentMenu     = nullptr;
    QStringList  m_recentCommands;
    CommandInputEdit* m_inputEdit     = nullptr;
    QToolButton*     m_historyButton  = nullptr;

    // 工具列按鍵（三個共用實體，單/多行都用同一個 widget ptr）
    QToolButton* m_gripButton     = nullptr;
    QToolButton* m_closeButton    = nullptr;
    QToolButton* m_customizeButton= nullptr;

    // 指令選項列（命令進行時）
    QWidget*    m_optionsBar      = nullptr;
    QHBoxLayout* m_optionsLayout  = nullptr;

    // Transient history（命令進行中浮動在上）
    TransientCommandHistory* m_transientHistory = nullptr;

    // 指令歷程彈出視窗
    CommandHistoryPopup* m_historyPopup = nullptr;
    QStringList  m_fullHistory;

    // 狀態
    bool m_multiRowMode   = false;
    int  m_transientLines = 3;       // 可設 1~3

    // 拖曳 resize
    enum ResizeEdge { None, Right, Top, TopRight };
    ResizeEdge m_resizeEdge = None;
    QPoint     m_resizeStart;
    QSize      m_resizeStartSize;
    int        m_resizeStartX = 0;   // 右邊緣拖曳：記錄 widget 的 x
    int        m_resizeStartY = 0;   // ← 新增
    // 拖曳/resize 後設 true，停止自動對齊
    bool m_userPositioned = false;
    // alignToCadView 呼叫 move() 期間設 true，避免 moveEvent 誤判
    bool m_aligning       = false;
    // 新增：是否正在 resize 中（給 paintEvent 畫 highlight 用）
    bool m_resizing = false;
    bool m_floating = false;

    // 雙擊 gripper 可重置回自動對齊（選用）
    bool m_initialAlignDone = false;
    void resetAlignment();


    static constexpr int SINGLE_ROW_HEIGHT = 32;
    static constexpr int MULTI_ROW_THRESHOLD = 50;
    static constexpr int RESIZE_MARGIN = 6;
};

} // namespace ui
} // namespace aicad

#endif // COMMANDLINEWIDGET_H
