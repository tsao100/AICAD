/**
 * @file LispEditorWidget.cpp
 */
#include "LispEditorWidget.h"
#include "LispCodeEdit.h"
#include "scripting/LispEngine.h"
#include "scripting/LispLanguage.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace aicad {
namespace ui {

using namespace scripting;

namespace {

const char* kOrg = "AICAD";
const char* kApp = "AICAD";

QString sampleScript()
{
    return QString::fromUtf8(
        ";;; AICAD Lisp 編輯器\n"
        ";;;   F5            執行整個檔案\n"
        ";;;   Ctrl+Enter    執行選取範圍；沒有選取時，執行游標所在的頂層表達式\n"
        ";;;   Tab           重新縮排目前行 / 選取的行        Ctrl+/  註解切換\n"
        ";;;   Ctrl+Space    自動補全                        Ctrl+F  尋找\n"
        "\n"
        "(defun draw-box (x y w h)\n"
        "  \"在 (x, y) 畫一個 w × h 的矩形\"\n"
        "  (rectangle x y (+ x w) (+ y h)))\n"
        "\n"
        "(print-message \"Hello from Lisp\")\n"
        "(+ 1 2 3)\n");
}

QString prettifyError(const QString& err)
{
    if (err.contains(QLatin1String("END-OF-FILE")))
        return QString::fromUtf8("表達式不完整：括號或字串沒有關閉（END-OF-FILE）");
    if (err.contains(QLatin1String("Unmatched right parenthesis")))
        return QString::fromUtf8("多餘的右括號 )");
    QString e = err;
    e.replace(QRegularExpression(QStringLiteral("#<string-input stream[^>]*>")), QStringLiteral("<source>"));
    return e.left(600);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// 建構
// ═══════════════════════════════════════════════════════════════════════════

LispEditorWidget::LispEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    buildActions();
    buildUi();
    m_fontSize = QSettings(kOrg, kApp).value(QStringLiteral("LispEditor/fontSize"), 0).toDouble();

    m_outlineTimer = new QTimer(this);
    m_outlineTimer->setSingleShot(true);
    m_outlineTimer->setInterval(400);
    connect(m_outlineTimer, &QTimer::timeout, this, &LispEditorWidget::refreshOutline);

    updateEngineStatus();
}

LispEditorWidget::~LispEditorWidget() = default;

void LispEditorWidget::buildActions()
{
    auto mk = [this](const QString& text, const QKeySequence& ks, const QString& tip) {
        QAction* a = new QAction(text, this);
        a->setShortcut(ks);
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        a->setToolTip(tip + (ks.isEmpty() ? QString() : QStringLiteral("  (%1)").arg(ks.toString(QKeySequence::NativeText))));
        return a;
    };
    m_actNew     = mk(tr("新增"),     QKeySequence(QStringLiteral("Ctrl+N")),       tr("新增空白檔案"));
    m_actOpen    = mk(tr("開啟…"),    QKeySequence(QStringLiteral("Ctrl+O")),       tr("開啟 Lisp 檔案"));
    m_actSave    = mk(tr("儲存"),     QKeySequence(QStringLiteral("Ctrl+S")),       tr("儲存目前檔案"));
    m_actSaveAs  = mk(tr("另存…"),    QKeySequence(QStringLiteral("Ctrl+Shift+S")), tr("另存新檔"));
    m_actRunFile = mk(tr("▶ 執行檔案"), QKeySequence(Qt::Key_F5),                    tr("執行整個檔案的所有表達式"));
    m_actRunForm = mk(tr("▶ 執行選取/表達式"), QKeySequence(), tr("執行選取範圍；沒有選取時執行游標所在的頂層表達式（Ctrl+Enter）"));
    m_actFormat  = mk(tr("縮排"),     QKeySequence(QStringLiteral("Ctrl+I")),       tr("重新縮排選取的行（無選取 = 目前行）；Ctrl+Shift+I 整份檔案"));
    m_actComment = mk(tr("註解"),     QKeySequence(QStringLiteral("Ctrl+/")),       tr("切換註解"));
    m_actFind    = mk(tr("尋找"),     QKeySequence(QStringLiteral("Ctrl+F")),       tr("尋找文字"));
    m_actClear   = mk(tr("清除輸出"), QKeySequence(),                               tr("清除輸出面板"));

    connect(m_actNew,     &QAction::triggered, this, [this]() { newDocument(); });
    connect(m_actOpen,    &QAction::triggered, this, [this]() {
        QSettings s(kOrg, kApp);
        const QStringList files = QFileDialog::getOpenFileNames(
            this, tr("開啟 Lisp 檔案"), s.value(QStringLiteral("LispEditor/lastDir")).toString(),
            tr("Lisp 檔案 (*.lisp *.lsp *.cl);;所有檔案 (*)"));
        for (const QString& f : files) openFile(f);
    });
    connect(m_actSave,    &QAction::triggered, this, [this]() { saveCurrent(); });
    connect(m_actSaveAs,  &QAction::triggered, this, [this]() { saveCurrentAs(); });
    connect(m_actRunFile, &QAction::triggered, this, [this]() { runFile(); });
    connect(m_actRunForm, &QAction::triggered, this, [this]() { runSelectionOrForm(); });
    connect(m_actFormat,  &QAction::triggered, this, [this]() { if (auto* e = currentEditor()) e->reindentSelection(); });
    connect(m_actComment, &QAction::triggered, this, [this]() { if (auto* e = currentEditor()) e->toggleComment(); });
    connect(m_actFind,    &QAction::triggered, this, [this]() { showFindBar(); });
    connect(m_actClear,   &QAction::triggered, this, [this]() { if (m_output) m_output->clear(); });

    // Ctrl+Shift+I：整份重排（沒有放進工具列，只有快捷鍵）
    QAction* fmtAll = new QAction(this);
    fmtAll->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+I")));
    fmtAll->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(fmtAll, &QAction::triggered, this, [this]() { if (auto* e = currentEditor()) e->reindentAll(); });
    addAction(fmtAll);
}

void LispEditorWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── 工具列 ──────────────────────────────────────────────────────────
    auto* tb = new QToolBar(this);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tb->setMovable(false);
    tb->addAction(m_actNew);  tb->addAction(m_actOpen);
    tb->addAction(m_actSave); tb->addAction(m_actSaveAs);
    tb->addSeparator();
    tb->addAction(m_actRunFile); tb->addAction(m_actRunForm);
    tb->addSeparator();
    tb->addAction(m_actFormat); tb->addAction(m_actComment); tb->addAction(m_actFind);
    tb->addSeparator();
    tb->addAction(m_actClear);
    m_outline = new QComboBox(this);
    m_outline->setMinimumContentsLength(18);
    m_outline->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_outline->setToolTip(tr("跳到定義（defun / defvar / defmacro …）"));
    tb->addSeparator();
    tb->addWidget(m_outline);
    connect(m_outline, QOverload<int>::of(&QComboBox::activated), this, &LispEditorWidget::onOutlineActivated);
    root->addWidget(tb);

    // ── 尋找列 ──────────────────────────────────────────────────────────
    m_findBar = new QWidget(this);
    auto* fl = new QHBoxLayout(m_findBar);
    fl->setContentsMargins(6, 2, 6, 2);
    fl->addWidget(new QLabel(tr("尋找:"), m_findBar));
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->installEventFilter(this);
    fl->addWidget(m_findEdit, 1);
    m_findCase = new QCheckBox(tr("區分大小寫"), m_findBar);
    fl->addWidget(m_findCase);
    auto* prev = new QPushButton(tr("上一個"), m_findBar);
    auto* next = new QPushButton(tr("下一個"), m_findBar);
    auto* close = new QPushButton(tr("✕"), m_findBar);
    close->setFixedWidth(28);
    fl->addWidget(prev); fl->addWidget(next); fl->addWidget(close);
    connect(m_findEdit, &QLineEdit::returnPressed, this, &LispEditorWidget::findNext);
    connect(next,  &QPushButton::clicked, this, &LispEditorWidget::findNext);
    connect(prev,  &QPushButton::clicked, this, &LispEditorWidget::findPrevious);
    connect(close, &QPushButton::clicked, this, [this]() {
        m_findBar->hide();
        if (auto* e = currentEditor()) e->setFocus();
    });
    m_findBar->hide();
    root->addWidget(m_findBar);

    // ── 分頁 + 下方輸出 ─────────────────────────────────────────────────
    m_splitter = new QSplitter(Qt::Vertical, this);

    m_tabs = new QTabWidget(m_splitter);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &LispEditorWidget::onTabCloseRequested);
    connect(m_tabs, &QTabWidget::currentChanged,    this, &LispEditorWidget::onCurrentTabChanged);

    auto* bottom = new QWidget(m_splitter);
    auto* bl = new QVBoxLayout(bottom);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);

    m_output = new QPlainTextEdit(bottom);
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(5000);
    m_output->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_output->setFont(font());
    QFont mono = m_output->font(); mono.setStyleHint(QFont::Monospace); mono.setFamily(QStringLiteral("Consolas"));
    m_output->setFont(mono);
    m_output->setPlaceholderText(tr("輸出（print、format t、錯誤訊息、執行結果）"));
    bl->addWidget(m_output, 1);

    auto* replRow = new QWidget(bottom);
    auto* rl = new QHBoxLayout(replRow);
    rl->setContentsMargins(6, 2, 6, 2);
    m_replPrompt = new QLabel(QStringLiteral("lisp>"), replRow);
    m_replPrompt->setFont(mono);
    m_repl = new QLineEdit(replRow);
    m_repl->setFont(mono);
    m_repl->setPlaceholderText(tr("在這裡輸入 Lisp 表達式，Enter 執行（括號未關閉時會繼續等待下一行）"));
    m_repl->installEventFilter(this);
    rl->addWidget(m_replPrompt);
    rl->addWidget(m_repl, 1);
    connect(m_repl, &QLineEdit::returnPressed, this, &LispEditorWidget::replSubmit);
    bl->addWidget(replRow);

    m_splitter->addWidget(m_tabs);
    m_splitter->addWidget(bottom);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 1);
    root->addWidget(m_splitter, 1);

    // ── 狀態列 ──────────────────────────────────────────────────────────
    auto* sbar = new QWidget(this);
    auto* sl = new QHBoxLayout(sbar);
    sl->setContentsMargins(8, 2, 8, 2);
    m_engineLbl = new QLabel(sbar);
    m_hint = new QLabel(sbar);
    m_hint->setTextFormat(Qt::RichText);
    m_hint->setTextInteractionFlags(Qt::NoTextInteraction);
    m_status = new QLabel(QStringLiteral("1:1"), sbar);
    sl->addWidget(m_engineLbl);
    sl->addWidget(m_hint, 1);
    sl->addWidget(m_status);
    root->addWidget(sbar);
}

// ═══════════════════════════════════════════════════════════════════════════
// 引擎
// ═══════════════════════════════════════════════════════════════════════════

void LispEditorWidget::setEngine(LispEngine* engine)
{
    m_engine = engine;
    updateEngineStatus();
    refreshEngineCompletions();
}

void LispEditorWidget::updateEngineStatus()
{
    const bool ok = m_engine && m_engine->isInitialized();
    m_engineLbl->setText(ok ? tr("● ECL 就緒") : tr("○ Lisp 引擎未啟用（可編輯，不能執行）"));
    m_engineLbl->setStyleSheet(ok ? QStringLiteral("color:#2e9d4f;") : QStringLiteral("color:#c27a00;"));
    m_actRunFile->setEnabled(ok);
    m_actRunForm->setEnabled(ok);
    m_repl->setEnabled(ok);
}

void LispEditorWidget::refreshEngineCompletions()
{
    if (!m_engine || !m_engine->isInitialized()) return;
    // 取得 CL-USER 中目前已定義（含 AICAD 綁定與使用者 defun）的函式名稱，供自動補全。
    // 以單一字串回傳：list 會被 *print-length* 截斷，字串不會。
    const LispEvalResult r = m_engine->evalDetailed(QStringLiteral(
        "(let (names) (do-symbols (s :cl-user) (when (and (fboundp s) (eq (symbol-package s) (find-package :cl-user)))"
        " (push (string-downcase (symbol-name s)) names)))"
        " (format nil \"~{~A~^ ~}\" names))"));
    if (!r.ok) return;
    QString v = r.value;
    if (v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')) && v.size() >= 2) v = v.mid(1, v.size() - 2);
    QStringList words;
    for (const QString& w : v.simplified().split(QLatin1Char(' ')))      // simplified()：不需要 SkipEmptyParts（Qt5/6 寫法不同）
        if (!w.isEmpty() && !w.startsWith(QLatin1Char('%'))) words << w;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto* e = qobject_cast<LispCodeEdit*>(m_tabs->widget(i))) e->setExtraCompletions(words);
    m_extraWordsCache = words;
}

// ═══════════════════════════════════════════════════════════════════════════
// 輸出
// ═══════════════════════════════════════════════════════════════════════════

void LispEditorWidget::appendMessage(const QString& textIn, MessageKind kind)
{
    QString text = textIn;
    while (text.endsWith(QLatin1Char('\n'))) text.chop(1);
    if (text.isEmpty() && kind != MessageKind::Output) return;

    const bool dark = m_output->palette().color(QPalette::Base).lightness() < 128;
    QTextCharFormat fmt;
    switch (kind) {
    case MessageKind::Info:   break;
    case MessageKind::Output: break;
    case MessageKind::Result: fmt.setForeground(QColor(dark ? "#6cb6ff" : "#0b5ed7")); break;
    case MessageKind::Error:  fmt.setForeground(QColor(dark ? "#ff7b7b" : "#c62828")); break;
    case MessageKind::Dim:    fmt.setForeground(QColor(dark ? "#8b929c" : "#7a828c")); break;
    }

    QTextCursor c(m_output->document());
    c.movePosition(QTextCursor::End);
    if (!m_output->document()->isEmpty()) c.insertBlock();
    c.insertText(text, fmt);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

// ═══════════════════════════════════════════════════════════════════════════
// 文件 / 分頁
// ═══════════════════════════════════════════════════════════════════════════

QString LispEditorWidget::nextUntitledName()
{
    return tr("未命名-%1.lisp").arg(++m_untitledCounter);
}

LispCodeEdit* LispEditorWidget::currentEditor() const
{
    return qobject_cast<LispCodeEdit*>(m_tabs->currentWidget());
}

int LispEditorWidget::documentCount() const { return m_tabs->count(); }

LispCodeEdit* LispEditorWidget::newDocument(const QString& content)
{
    auto* e = new LispCodeEdit(m_tabs);
    e->untitledName = nextUntitledName();
    e->setPlainText(content);
    e->document()->setModified(false);
    e->setExtraCompletions(m_extraWordsCache);
    if (m_fontSize > 0) e->setEditorFontSize(m_fontSize);
    connectEditor(e);
    const int idx = m_tabs->addTab(e, e->untitledName);
    m_tabs->setCurrentIndex(idx);
    updateTabTitle(e);
    e->setFocus();
    return e;
}

void LispEditorWidget::connectEditor(LispCodeEdit* e)
{
    e->installEventFilter(this);
    // Ctrl+滾輪縮放：所有分頁同步，並記住大小（下次開啟沿用）
    connect(e, &LispCodeEdit::fontSizeChanged, this, [this](qreal pt) {
        m_fontSize = pt;
        for (int i = 0; i < m_tabs->count(); ++i)
            if (auto* o = qobject_cast<LispCodeEdit*>(m_tabs->widget(i))) o->setEditorFontSize(pt);
        QSettings s(kOrg, kApp);
        s.setValue(QStringLiteral("LispEditor/fontSize"), pt);
    });
    connect(e, &LispCodeEdit::runFormRequested,  this, &LispEditorWidget::runSelectionOrForm);
    connect(e, &LispCodeEdit::callHintChanged,   this, [this, e](const QString& html) {
        if (e == currentEditor()) m_hint->setText(html);
    });
    connect(e, &LispCodeEdit::cursorInfoChanged, this, [this, e](int line, int col) {
        if (e == currentEditor()) m_status->setText(QStringLiteral("%1:%2").arg(line).arg(col));
    });
    connect(e->document(), &QTextDocument::modificationChanged,
            this, &LispEditorWidget::onDocumentModificationChanged);
    connect(e->document(), &QTextDocument::contentsChanged, this, [this]() { m_outlineTimer->start(); });
}

void LispEditorWidget::updateTabTitle(LispCodeEdit* e)
{
    const int idx = m_tabs->indexOf(e);
    if (idx < 0) return;
    QString name = e->filePath.isEmpty() ? e->untitledName : QFileInfo(e->filePath).fileName();
    if (e->document()->isModified()) name += QStringLiteral(" *");
    m_tabs->setTabText(idx, name);
    m_tabs->setTabToolTip(idx, e->filePath.isEmpty() ? e->untitledName : e->filePath);
}

void LispEditorWidget::onDocumentModificationChanged(bool)
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto* e = qobject_cast<LispCodeEdit*>(m_tabs->widget(i))) updateTabTitle(e);
}

void LispEditorWidget::onCurrentTabChanged(int)
{
    m_hint->clear();
    refreshOutline();
    if (auto* e = currentEditor()) {
        const QTextCursor c = e->textCursor();
        m_status->setText(QStringLiteral("%1:%2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
    }
}

void LispEditorWidget::onTabCloseRequested(int index) { closeTab(index); }

bool LispEditorWidget::closeTab(int index)
{
    auto* e = qobject_cast<LispCodeEdit*>(m_tabs->widget(index));
    if (!e) return true;
    if (e->document()->isModified()) {
        m_tabs->setCurrentIndex(index);
        const QString name = e->filePath.isEmpty() ? e->untitledName : QFileInfo(e->filePath).fileName();
        const auto ans = QMessageBox::question(this, tr("Lisp 編輯器"),
                                               tr("「%1」尚未儲存，要儲存嗎？").arg(name),
                                               QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                               QMessageBox::Save);
        if (ans == QMessageBox::Cancel) return false;
        if (ans == QMessageBox::Save && !saveEditor(e, false)) return false;
    }
    m_tabs->removeTab(index);
    e->deleteLater();
    if (m_tabs->count() == 0) newDocument();
    return true;
}

bool LispEditorWidget::maybeSaveAll()
{
    for (int i = m_tabs->count() - 1; i >= 0; --i) {
        auto* e = qobject_cast<LispCodeEdit*>(m_tabs->widget(i));
        if (e && e->document()->isModified() && !closeTab(i)) return false;
    }
    return true;
}

bool LispEditorWidget::openFile(const QString& pathIn)
{
    const QFileInfo fi(pathIn);
    const QString path = fi.absoluteFilePath();

    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* e = qobject_cast<LispCodeEdit*>(m_tabs->widget(i));
        if (e && !e->filePath.isEmpty() && QFileInfo(e->filePath) == fi) {
            m_tabs->setCurrentIndex(i);
            return true;
        }
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Lisp 編輯器"), tr("無法開啟檔案：\n%1").arg(path));
        return false;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    // UTF-8 優先；若不是合法 UTF-8（舊的 Big5 / CP950 腳本）退回系統編碼
    QString text = QString::fromUtf8(bytes);
    if (text.contains(QChar::ReplacementCharacter) && !bytes.contains("\xEF\xBF\xBD"))
        text = QString::fromLocal8Bit(bytes);
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);
    const bool crlf = text.contains(QStringLiteral("\r\n"));
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    // 若目前只有一個沒動過的空白新檔，直接取代它
    LispCodeEdit* replace = nullptr;
    if (m_tabs->count() == 1) {
        auto* only = qobject_cast<LispCodeEdit*>(m_tabs->widget(0));
        if (only && only->filePath.isEmpty() && !only->document()->isModified() && only->document()->isEmpty())
            replace = only;
    }

    LispCodeEdit* e = newDocument(text);
    e->filePath = path;
    e->crlf = crlf;
    e->document()->setModified(false);
    updateTabTitle(e);

    if (replace) {
        m_tabs->removeTab(m_tabs->indexOf(replace));
        replace->deleteLater();
    }
    QSettings s(kOrg, kApp);
    s.setValue(QStringLiteral("LispEditor/lastDir"), fi.absolutePath());
    refreshOutline();
    return true;
}

bool LispEditorWidget::saveEditor(LispCodeEdit* e, bool forceDialog)
{
    QString path = e->filePath;
    if (path.isEmpty() || forceDialog) {
        QSettings s(kOrg, kApp);
        const QString dir = path.isEmpty() ? s.value(QStringLiteral("LispEditor/lastDir")).toString()
                                           : QFileInfo(path).absolutePath();
        const QString suggested = QDir(dir).filePath(path.isEmpty() ? e->untitledName : QFileInfo(path).fileName());
        path = QFileDialog::getSaveFileName(this, tr("儲存 Lisp 檔案"), suggested,
                                            tr("Lisp 檔案 (*.lisp *.lsp *.cl);;所有檔案 (*)"));
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".lisp");
        s.setValue(QStringLiteral("LispEditor/lastDir"), QFileInfo(path).absolutePath());
    }

    QString text = e->toPlainText();
    if (e->crlf) text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));

    QSaveFile f(path);                                   // 先寫暫存檔再取代，失敗不會毀掉原檔
    if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0 || !f.commit()) {
        QMessageBox::warning(this, tr("Lisp 編輯器"),
                             tr("儲存失敗：\n%1\n%2").arg(path, f.errorString()));
        return false;
    }
    e->filePath = path;
    e->document()->setModified(false);
    updateTabTitle(e);
    return true;
}

bool LispEditorWidget::saveCurrent()   { auto* e = currentEditor(); return e && saveEditor(e, false); }
bool LispEditorWidget::saveCurrentAs() { auto* e = currentEditor(); return e && saveEditor(e, true); }

bool LispEditorWidget::saveAll()
{
    bool ok = true;
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* e = qobject_cast<LispCodeEdit*>(m_tabs->widget(i));
        if (e && e->document()->isModified()) ok = saveEditor(e, false) && ok;
    }
    return ok;
}

// ═══════════════════════════════════════════════════════════════════════════
// 執行
// ═══════════════════════════════════════════════════════════════════════════

bool LispEditorWidget::evaluateAndReport(const QString& code, const QString& label,
                                         LispCodeEdit* origin, int offset)
{
    if (!m_engine || !m_engine->isInitialized()) {
        appendMessage(tr("✖ Lisp 引擎未啟用（ECL 沒有編譯進來，或初始化失敗）"), MessageKind::Error);
        return false;
    }
    if (m_busy) return false;

    if (origin) origin->clearErrorRange();
    m_busy = true;
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    appendMessage(QStringLiteral("▶ ") + label, MessageKind::Dim);

    const LispEvalResult r = m_engine->evalDetailed(code);

    QGuiApplication::restoreOverrideCursor();
    m_busy = false;

    if (!r.output.isEmpty()) appendMessage(r.output, MessageKind::Output);

    if (r.ok) {
        if (r.formsEvaluated > 0) appendMessage(QStringLiteral("=> ") + r.value, MessageKind::Result);
        else                      appendMessage(tr("（沒有可執行的表達式）"), MessageKind::Dim);
        if (origin && offset >= 0 && r.formsEvaluated > 0) origin->flashRange(offset, offset + code.size());
    } else {
        QString where;
        if (origin && r.errorStart >= 0) {
            const int abs = offset + r.errorStart;
            const QTextBlock b = origin->document()->findBlock(abs);
            where = tr("  （第 %1 行，第 %2 欄）").arg(b.blockNumber() + 1).arg(abs - b.position() + 1);
            origin->setErrorRange(abs, offset + r.errorEnd);
            origin->goToPosition(abs);
        }
        appendMessage(QStringLiteral("✖ ") + prettifyError(r.error) + where, MessageKind::Error);
        if (r.formsEvaluated > 0)
            appendMessage(tr("（錯誤發生前已成功執行 %1 個表達式，它們的效果不會自動復原）").arg(r.formsEvaluated),
                          MessageKind::Dim);
    }

    refreshEngineCompletions();
    Q_EMIT scriptFinished(r.ok);
    return r.ok;
}

void LispEditorWidget::runFile()
{
    LispCodeEdit* e = currentEditor();
    if (!e) return;
    const QString label = e->filePath.isEmpty() ? e->untitledName : QFileInfo(e->filePath).fileName();
    evaluateAndReport(e->wholeText(), label, e, 0);
}

void LispEditorWidget::runSelectionOrForm()
{
    LispCodeEdit* e = currentEditor();
    if (!e) return;
    QString code; int offset = 0;
    if (!e->selectionOrFormToRun(&code, &offset)) {
        appendMessage(tr("游標位置沒有可執行的表達式"), MessageKind::Dim);
        return;
    }
    QString label = code.simplified();
    if (label.size() > 70) label = label.left(70) + QStringLiteral("…");
    evaluateAndReport(code, label, e, offset);
}

// ── REPL ──────────────────────────────────────────────────────────────────

void LispEditorWidget::replSubmit()
{
    const QString line = m_repl->text();
    if (line.trimmed().isEmpty() && m_replPending.isEmpty()) return;

    const QString combined = m_replPending.isEmpty() ? line : m_replPending + QLatin1Char('\n') + line;
    const int st = LispLexer::lex(combined, 0, nullptr);
    const bool incomplete = LispLexState::parenDepth(st) > 0 || LispLexState::mode(st) != LispLexState::Normal;

    m_repl->clear();
    if (incomplete) {                                   // 括號沒關：等下一行
        m_replPending = combined;
        m_replPrompt->setText(QStringLiteral("  …>"));
        return;
    }
    m_replPending.clear();
    m_replPrompt->setText(QStringLiteral("lisp>"));

    if (m_replHistory.isEmpty() || m_replHistory.last() != combined) m_replHistory << combined;
    m_replHistoryPos = m_replHistory.size();

    evaluateAndReport(combined, combined.simplified());
}

// ── 尋找 ──────────────────────────────────────────────────────────────────

void LispEditorWidget::showFindBar()
{
    m_findBar->show();
    if (auto* e = currentEditor()) {
        const QString sel = e->textCursor().selectedText();
        if (!sel.isEmpty() && !sel.contains(QChar(0x2029))) m_findEdit->setText(sel);
    }
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

bool LispEditorWidget::doFind(bool backward)
{
    LispCodeEdit* e = currentEditor();
    if (!e || m_findEdit->text().isEmpty()) return false;
    QTextDocument::FindFlags flags;
    if (backward)                 flags |= QTextDocument::FindBackward;
    if (m_findCase->isChecked())  flags |= QTextDocument::FindCaseSensitively;
    if (e->find(m_findEdit->text(), flags)) return true;

    QTextCursor c = e->textCursor();                    // 到頭 / 到尾：繞回另一端再找一次
    c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    e->setTextCursor(c);
    return e->find(m_findEdit->text(), flags);
}

void LispEditorWidget::findNext()
{
    const bool ok = doFind(false);
    m_findEdit->setStyleSheet(ok || m_findEdit->text().isEmpty() ? QString() : QStringLiteral("background:#ffd6d6;color:#000;"));
}

void LispEditorWidget::findPrevious()
{
    const bool ok = doFind(true);
    m_findEdit->setStyleSheet(ok || m_findEdit->text().isEmpty() ? QString() : QStringLiteral("background:#ffd6d6;color:#000;"));
}

// ── 大綱 ──────────────────────────────────────────────────────────────────

void LispEditorWidget::refreshOutline()
{
    m_outline->blockSignals(true);
    m_outline->clear();
    LispCodeEdit* e = currentEditor();
    const QVector<LispDefinition> defs = e ? e->index().definitions() : QVector<LispDefinition>();
    m_outline->addItem(tr("定義 (%1)").arg(defs.size()), -1);
    for (const LispDefinition& d : defs)
        m_outline->addItem(QStringLiteral("%1  %2    :%3").arg(d.kind, d.name).arg(d.line + 1), d.pos);
    m_outline->blockSignals(false);
}

void LispEditorWidget::onOutlineActivated(int comboIndex)
{
    const int pos = m_outline->itemData(comboIndex).toInt();
    if (pos < 0) return;
    if (auto* e = currentEditor()) { e->goToPosition(pos); e->setFocus(); }
    m_outline->setCurrentIndex(0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 事件過濾：REPL 歷史、尋找列 Esc、以及「編輯器有焦點時，本面板的快捷鍵優先於主視窗」
// ═══════════════════════════════════════════════════════════════════════════

bool LispEditorWidget::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_repl && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Up && !m_replHistory.isEmpty()) {
            m_replHistoryPos = qMax(0, m_replHistoryPos - 1);
            m_repl->setText(m_replHistory.at(m_replHistoryPos));
            return true;
        }
        if (ke->key() == Qt::Key_Down && !m_replHistory.isEmpty()) {
            m_replHistoryPos = qMin(m_replHistory.size(), m_replHistoryPos + 1);
            m_repl->setText(m_replHistoryPos < m_replHistory.size() ? m_replHistory.at(m_replHistoryPos) : QString());
            return true;
        }
        if (ke->key() == Qt::Key_Escape && !m_replPending.isEmpty()) {
            m_replPending.clear();
            m_replPrompt->setText(QStringLiteral("lisp>"));
            m_repl->clear();
            return true;
        }
        return QWidget::eventFilter(obj, ev);
    }

    if (obj == m_findEdit && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Escape) {
            m_findBar->hide();
            if (auto* e = currentEditor()) e->setFocus();
            return true;
        }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && (ke->modifiers() & Qt::ShiftModifier)) {
            findPrevious();
            return true;
        }
        return QWidget::eventFilter(obj, ev);
    }

    // 主視窗也有 Ctrl+N / Ctrl+O / Ctrl+S（CAD 文件的新增 / 開啟 / 儲存）。
    // 游標在程式碼編輯區時，這些鍵應該作用在 Lisp 檔案：在 ShortcutOverride 階段接下來，
    // 之後的 KeyPress 再觸發對應的 QAction。
    if (qobject_cast<LispCodeEdit*>(obj) &&
        (ev->type() == QEvent::ShortcutOverride || ev->type() == QEvent::KeyPress)) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        const int key = ke->key();
        if (key != Qt::Key_Control && key != Qt::Key_Shift && key != Qt::Key_Alt && key != Qt::Key_Meta) {
            const QKeySequence seq(key | int(ke->modifiers() & ~Qt::KeypadModifier));
            const QList<QAction*> acts = { m_actNew, m_actOpen, m_actSave, m_actSaveAs, m_actRunFile,
                                           m_actFormat, m_actComment, m_actFind };
            for (QAction* a : acts) {
                if (!a->isEnabled() || a->shortcut().isEmpty()) continue;
                if (a->shortcut().matches(seq) == QKeySequence::ExactMatch) {
                    if (ev->type() == QEvent::ShortcutOverride) { ev->accept(); return true; }
                    a->trigger();
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(obj, ev);
}

// ═══════════════════════════════════════════════════════════════════════════
// 工作階段：關閉程式再開啟時，還原開著的檔案與「尚未存檔的編輯內容」
// ═══════════════════════════════════════════════════════════════════════════

void LispEditorWidget::saveSession()
{
    QSettings s(kOrg, kApp);
    s.remove(QStringLiteral("LispEditor/docs"));
    s.beginWriteArray(QStringLiteral("LispEditor/docs"));
    int n = 0;
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* e = qobject_cast<LispCodeEdit*>(m_tabs->widget(i));
        if (!e) continue;
        const bool modified = e->document()->isModified();
        if (e->filePath.isEmpty() && e->document()->isEmpty()) continue;       // 空白新檔不用存
        s.setArrayIndex(n++);
        s.setValue(QStringLiteral("path"), e->filePath);
        s.setValue(QStringLiteral("name"), e->untitledName);
        s.setValue(QStringLiteral("crlf"), e->crlf);
        s.setValue(QStringLiteral("modified"), modified);
        s.setValue(QStringLiteral("text"), (modified || e->filePath.isEmpty()) ? e->toPlainText() : QString());
    }
    s.endArray();
    s.setValue(QStringLiteral("LispEditor/current"), m_tabs->currentIndex());
    s.setValue(QStringLiteral("LispEditor/splitter"), m_splitter->saveState());
}

void LispEditorWidget::restoreSession()
{
    QSettings s(kOrg, kApp);
    const int n = s.beginReadArray(QStringLiteral("LispEditor/docs"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        const QString path = s.value(QStringLiteral("path")).toString();
        const bool modified = s.value(QStringLiteral("modified")).toBool();
        if (!modified && !path.isEmpty()) {              // 沒改過的檔案：直接從磁碟重新開啟
            if (QFileInfo::exists(path)) openFile(path);
            continue;
        }
        LispCodeEdit* e = newDocument(s.value(QStringLiteral("text")).toString());
        const QString nm = s.value(QStringLiteral("name")).toString();
        if (!nm.isEmpty()) e->untitledName = nm;
        e->filePath = path;
        e->crlf = s.value(QStringLiteral("crlf")).toBool();
        e->document()->setModified(true);                // 還原回來的內容尚未存到檔案
        updateTabTitle(e);
    }
    s.endArray();

    if (m_tabs->count() == 0) newDocument(sampleScript());
    else if (m_tabs->count() > 1) {
        // 還原後若第一個是 newDocument 留下的空白頁已被取代；否則維持現狀
    }
    const int cur = s.value(QStringLiteral("LispEditor/current"), 0).toInt();
    if (cur >= 0 && cur < m_tabs->count()) m_tabs->setCurrentIndex(cur);
    const QByteArray sp = s.value(QStringLiteral("LispEditor/splitter")).toByteArray();
    if (!sp.isEmpty()) m_splitter->restoreState(sp);
    refreshOutline();
}

} // namespace ui
} // namespace aicad
