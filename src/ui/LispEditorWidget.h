/**
 * @file LispEditorWidget.h
 * @brief Lisp 編輯器主面板：多分頁編輯、開啟 / 儲存、執行檔案 / 選取 / 游標所在表達式、
 *        輸出面板、REPL 輸入列、尋找、定義大綱、工作階段還原
 */
#ifndef AICAD_UI_LISPEDITORWIDGET_H
#define AICAD_UI_LISPEDITORWIDGET_H

#include <QWidget>
#include <QStringList>

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSplitter;
class QTabWidget;
class QTimer;
class QCheckBox;

namespace aicad {
namespace scripting { class LispEngine; }
namespace ui {

class LispCodeEdit;

class LispEditorWidget : public QWidget {
    Q_OBJECT
public:
    enum class MessageKind { Info, Result, Error, Dim, Output };

    explicit LispEditorWidget(QWidget* parent = nullptr);
    ~LispEditorWidget() override;

    /// 設定要使用的 Lisp 引擎；可為 nullptr（編輯器仍可編輯，只是不能執行）
    void setEngine(scripting::LispEngine* engine);
    scripting::LispEngine* engine() const { return m_engine; }

    // ── 文件 ──────────────────────────────────────────────────────────
    LispCodeEdit* newDocument(const QString& content = QString());
    bool openFile(const QString& path);         ///< 已開啟則切換過去
    bool saveCurrent();
    bool saveCurrentAs();
    bool saveAll();
    bool closeTab(int index);                   ///< 有未存檔變更時會詢問
    bool maybeSaveAll();                        ///< 全部分頁；使用者取消回傳 false
    LispCodeEdit* currentEditor() const;
    int  documentCount() const;

    // ── 執行 ──────────────────────────────────────────────────────────
    void runFile();
    void runSelectionOrForm();
    /// 在輸出面板顯示一段訊息（print-message 等外部來源也用這個）
    void appendMessage(const QString& text, MessageKind kind = MessageKind::Info);
    /// 求值一段程式碼並顯示結果（REPL 與測試使用）；回傳是否成功
    bool evaluateAndReport(const QString& code, const QString& label,
                           LispCodeEdit* originEditor = nullptr, int originOffset = 0);

    // ── 工作階段 ──────────────────────────────────────────────────────
    void saveSession();
    void restoreSession();

Q_SIGNALS:
    /// 腳本執行完畢（可能已修改了 Document；外部可據此更新視圖）
    void scriptFinished(bool ok);

private Q_SLOTS:
    void onTabCloseRequested(int index);
    void onCurrentTabChanged(int index);
    void onDocumentModificationChanged(bool modified);
    void refreshOutline();
    void onOutlineActivated(int comboIndex);
    void replSubmit();
    void findNext();
    void findPrevious();

private:
    void buildUi();
    void buildActions();
    void connectEditor(LispCodeEdit* e);
    void updateTabTitle(LispCodeEdit* e);
    void updateEngineStatus();
    void refreshEngineCompletions();
    void showFindBar();
    bool doFind(bool backward);
    bool saveEditor(LispCodeEdit* e, bool forceDialog);
    QString nextUntitledName();
    bool eventFilter(QObject* obj, QEvent* ev) override;
    QPlainTextEdit* outputPane() const { return m_output; }

    scripting::LispEngine* m_engine = nullptr;

    QTabWidget*     m_tabs      = nullptr;
    QPlainTextEdit* m_output    = nullptr;
    QLineEdit*      m_repl      = nullptr;
    QLabel*         m_replPrompt= nullptr;
    QLabel*         m_hint      = nullptr;
    QLabel*         m_status    = nullptr;
    QLabel*         m_engineLbl = nullptr;
    QComboBox*      m_outline   = nullptr;
    QWidget*        m_findBar   = nullptr;
    QLineEdit*      m_findEdit  = nullptr;
    QCheckBox*      m_findCase  = nullptr;
    QSplitter*      m_splitter  = nullptr;
    QTimer*         m_outlineTimer = nullptr;

    QAction *m_actNew, *m_actOpen, *m_actSave, *m_actSaveAs, *m_actRunFile, *m_actRunForm,
            *m_actFormat, *m_actComment, *m_actFind, *m_actClear;

    QStringList m_replHistory;
    int         m_replHistoryPos = 0;
    QString     m_replPending;      ///< 尚未完成（括號沒關）的多行輸入
    QStringList m_extraWordsCache;
    qreal       m_fontSize = 0;     ///< 使用者縮放後的編輯區字型大小（0 = 預設）
    int         m_untitledCounter = 0;
    bool        m_busy = false;
};

} // namespace ui
} // namespace aicad

#endif
