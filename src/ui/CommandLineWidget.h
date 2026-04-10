#ifndef COMMANDLINEWIDGET_H
#define COMMANDLINEWIDGET_H

#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QTextEdit>
#include <QComboBox>
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

    // 設定
    int historyTransientLines() const { return m_transientLines; }
    void setHistoryTransientLines(int n);

    CommandInputEdit* inputEdit();
    TransientCommandHistory* transientHistory() const;

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
    void onComboActivated(int index);

    // 右邊緣/上邊緣拖曳
    void updateResizeCursor(const QPoint& pos);
    void startResize(const QPoint& pos);
    void doResize(const QPoint& pos);
    void endResize();

    // 對齊 CadView
    void alignToCadView();

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

    QComboBox*       m_recentCombo    = nullptr;
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

    // 狀態
    bool m_multiRowMode   = false;
    int  m_transientLines = 3;       // 可設 1~3

    // 拖曳 resize
    enum ResizeEdge { None, Right, Top };
    ResizeEdge m_resizeEdge = None;
    QPoint     m_resizeStart;
    QSize      m_resizeStartSize;
    int        m_resizeStartX = 0;   // 右邊緣拖曳：記錄 widget 的 x

    static constexpr int SINGLE_ROW_HEIGHT = 32;
    static constexpr int MULTI_ROW_THRESHOLD = 50;
    static constexpr int RESIZE_MARGIN = 6;
};

} // namespace ui
} // namespace aicad

#endif // COMMANDLINEWIDGET_H
