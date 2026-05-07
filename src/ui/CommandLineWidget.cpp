#include "CommandLineWidget.h"
#include "CommandInputEdit.h"
#include "TransientCommandHistory.h"
#include "CommandHistoryPopup.h"
#include "core/CommandLineManager.h"
#include <QApplication>
#include <QWindow>
#include <QCursor>
#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QScrollBar>
#include <QRegularExpression>

namespace aicad {
namespace ui {

CommandLineWidget::CommandLineWidget(QWidget* cadView, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
    , m_cadView(cadView)
{
    setAttribute(Qt::WA_StyledBackground);
    setAttribute(Qt::WA_NativeWindow);
    setMouseTracking(true);
    setMinimumHeight(SINGLE_ROW_HEIGHT);
    setMinimumWidth(600);

    setupGripper();
    setupCloseButton();
    setupCustomizeButton();
    setupComboAndInput();
    setupHistoryButton();
    setupOptionsBar();
    setupTransientHistory();
    setupResizeHandles();

    buildSingleRow();
    //buildMultiRow();
    //alignToCadView(); CadView 尚未就緒

    connect(m_inputEdit, &CommandInputEdit::optionChipClicked,
            this, &CommandLineWidget::optionSelected);

    using CLM = core::CommandLineManager;
    connect(CLM::instance(), &CLM::promptOptionsChanged,
            this, &CommandLineWidget::onPromptOptionsChanged);

    installMouseFilterOnChildren(this);

    if (m_cadView) {
        m_cadView->installEventFilter(this);
        // ↓ 新增：也監聽 CadView 所在的頂層視窗 (MainWindow)
        if (auto* win = m_cadView->window(); win && win != m_cadView)
            win->installEventFilter(this);
    }
}

CommandLineWidget::~CommandLineWidget() = default;

// ─────────────────────────────────────────
// 子 Widget 建立
// ─────────────────────────────────────────

void CommandLineWidget::setupGripper() {
    m_gripButton = new QToolButton(this);
    m_gripButton->setCursor(Qt::SizeAllCursor);
    m_gripButton->setFixedSize(16, SINGLE_ROW_HEIGHT);
    m_gripButton->setToolTip(tr("拖曳移動  |  雙擊恢復自動對齊"));

    // pressed → 啟動系統拖曳移動（QToolButton 吃掉 mousePress，
    // 必須在 signal 裡呼叫，不能靠 mousePressEvent 攔截）
    connect(m_gripButton, &QToolButton::pressed, this, [this]() {
        if (!m_floating)
            detachToCadView();          // 嵌入模式先切換為浮動
        if (auto* handle = windowHandle())
            handle->startSystemMove();    // this->windowHandle()，非 window()
    });

    // 雙擊恢復自動對齊（改用 QTimer 區分單/雙擊，或直接用 doubleClicked）
    // QToolButton 沒有 doubleClicked signal，改用 eventFilter 攔截
    m_gripButton->installEventFilter(this);
}

void CommandLineWidget::setupCloseButton() {
    m_closeButton = new QToolButton(this);
    m_closeButton->setToolTip(tr("關閉命令列 (Ctrl+9)"));
    m_closeButton->setText("✕");
    m_closeButton->setFixedSize(22, 22);
    connect(m_closeButton, &QToolButton::clicked,
            this, &CommandLineWidget::hide);
}

void CommandLineWidget::setupCustomizeButton() {
    m_customizeButton = new QToolButton(this);
    m_customizeButton->setToolTip(tr("自訂"));
    m_customizeButton->setText("▾");
    m_customizeButton->setFixedSize(22, 22);
    m_customizeButton->setPopupMode(QToolButton::InstantPopup);

    auto* menu = new QMenu(m_customizeButton);
    menu->addAction(tr("輸入設定..."),    this, [this]{ /* 開啟輸入設定對話框 */ });
    menu->addAction(tr("提示歷程行數"), this, [this]{ /* 設定 1~3 */ });
    menu->addAction(tr("輸入搜尋選項"), this, [this]{ /* 搜尋選項 */ });
    menu->addSeparator();
    menu->addAction(tr("透明度..."),     this, [this]{ /* 透明度滑桿 */ });
    menu->addAction(tr("選項..."),       this, [this]{ /* 主選項對話框 */ });
    menu->addAction(tr("恢復自動對齊"), this, &CommandLineWidget::resetAlignment);
    m_customizeButton->setMenu(menu);
}

void CommandLineWidget::setupComboAndInput() {
    m_inputRow    = new QWidget(this);
    m_inputRowLayout = new QHBoxLayout(m_inputRow);
    m_inputRowLayout->setContentsMargins(2, 0, 2, 0);
    m_inputRowLayout->setSpacing(2);

    // 最近指令下拉按鈕
    m_recentButton = new QToolButton(m_inputRow);
    m_recentButton->setToolTip(tr("最近使用的指令"));
    m_recentButton->setText("▼");
    m_recentButton->setFixedWidth(28);
    m_recentMenu = new QMenu(m_recentButton);
    m_recentButton->setMenu(m_recentMenu);
    m_recentButton->setPopupMode(QToolButton::InstantPopup);
    connect(m_recentMenu, &QMenu::triggered,
            this, &CommandLineWidget::onRecentMenuTriggered);

    // 命令輸入區
    m_inputEdit = new CommandInputEdit(m_inputRow);
    connect(m_inputEdit, &CommandInputEdit::commandSubmitted,
            this, &CommandLineWidget::onInputSubmit);

    m_inputRowLayout->addWidget(m_recentButton);
    m_inputRowLayout->addWidget(m_inputEdit, 1);
    // historyButton 在 setupHistoryButton() 加入
}

void CommandLineWidget::setupHistoryButton() {
    m_historyButton = new QToolButton(m_inputRow);
    m_historyButton->setToolTip(tr("指令歷程 (F2)"));
    // 三角形向上 icon
    m_historyButton->setText("▲");
    m_historyButton->setFixedSize(22, 22);
    connect(m_historyButton, &QToolButton::clicked,
            this, &CommandLineWidget::onHistoryButtonClicked);
    m_inputRowLayout->addWidget(m_historyButton);
}

void CommandLineWidget::setupOptionsBar() {
    m_optionsBar = new QWidget(this);
    auto* lay = new QHBoxLayout(m_optionsBar);
    m_optionsLayout = lay;
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(6);
    lay->addStretch();
    m_optionsBar->setVisible(false);
}

void CommandLineWidget::setupTransientHistory() {
    // m_cadView 是 anchor 的 parent，labels 掛在上面
    m_transientHistory = new TransientCommandHistory(this, m_cadView);
}

void CommandLineWidget::setupResizeHandles() {
    // 透過 mouseMoveEvent / resizeEvent 處理，不需要額外 widget
    setMouseTracking(true);
}

// ─────────────────────────────────────────
// 排版切換
// ─────────────────────────────────────────

void CommandLineWidget::buildSingleRow() {
    // 清除現有排版
    if (m_singleRowBar) m_singleRowBar->deleteLater();
    if (m_leftButtonCol) m_leftButtonCol->deleteLater();
    if (m_rightSplitter){
        m_historyView = nullptr;
        m_rightSplitter->deleteLater();
    }
    m_singleRowBar = m_leftButtonCol = nullptr;
    m_rightSplitter = nullptr;

    m_singleRowBar = new QWidget(this);
    m_singleRowLayout = new QHBoxLayout(m_singleRowBar);
    m_singleRowLayout->setContentsMargins(0, 0, 0, 0);
    m_singleRowLayout->setSpacing(2);

    m_singleRowLayout->addWidget(m_gripButton);
    m_singleRowLayout->addWidget(m_closeButton);
    m_singleRowLayout->addWidget(m_customizeButton);
    m_singleRowLayout->addWidget(m_inputRow, 1);   // inputRow 含 combo+input+▲

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_optionsBar);   // 選項列（命令進行時顯示）
    root->addWidget(m_singleRowBar);

    m_multiRowMode = false;
    setFixedHeight(SINGLE_ROW_HEIGHT +
                   (m_optionsBar->isVisible() ? m_optionsBar->sizeHint().height() : 0));
}

void CommandLineWidget::buildMultiRow() {
    if (m_singleRowBar) m_singleRowBar->deleteLater();
    m_singleRowBar = nullptr;

    // 左欄：Gripper(上)、Close(中)、Customize(下)，靠左由上而下
    m_leftButtonCol = new QWidget(this);
    m_leftColLayout = new QVBoxLayout(m_leftButtonCol);
    m_leftColLayout->setContentsMargins(2, 2, 2, 2);
    m_leftColLayout->setSpacing(2);
    m_leftColLayout->addWidget(m_gripButton);
    m_leftColLayout->addWidget(m_closeButton);
    m_leftColLayout->addWidget(m_customizeButton);
    m_leftColLayout->addStretch();
    m_leftButtonCol->setFixedWidth(28);

    // 右側：歷程(上) + 輸入列(下)，垂直 splitter
    m_historyView = new QTextEdit(this);
    m_historyView->setReadOnly(true);
    m_historyView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_historyView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    const QString color = QStringLiteral("#888888");

    for (int i = 0; i < m_fullHistory.size(); ++i)
        m_historyView->append(
            QStringLiteral("<span style='color:%1;'>%2</span>")
                .arg(color, (m_fullHistory[i]).toHtmlEscaped()));


    m_rightSplitter = new QSplitter(Qt::Vertical, this);
    m_rightSplitter->setHandleWidth(3);
    m_rightSplitter->setChildrenCollapsible(false);
    m_rightSplitter->addWidget(m_historyView);
    m_rightSplitter->addWidget(m_inputRow);
    m_rightSplitter->setStretchFactor(0, 1);
    m_rightSplitter->setStretchFactor(1, 0);

    // 底部固定：選項列（命令進行時）
    auto* rightCol = new QWidget(this);
    auto* rightLay = new QVBoxLayout(rightCol);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(0);
    rightLay->addWidget(m_rightSplitter, 1);
    rightLay->addWidget(m_optionsBar);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_leftButtonCol);
    root->addWidget(rightCol, 1);

    m_multiRowMode = true;
    setMaximumHeight(QWIDGETSIZE_MAX);
}

bool CommandLineWidget::isMultiRowMode() const {
    return m_multiRowMode;
}

void CommandLineWidget::switchLayout(bool multiRow) {
    if (multiRow == m_multiRowMode) return;
    // 保留目前 widgets 的 parent 設為 this，重新建立排版
    m_gripButton->setParent(this);
    m_closeButton->setParent(this);
    m_customizeButton->setParent(this);
    m_inputRow->setParent(this);
    m_optionsBar->setParent(this);

    // 切換到單行時，historyView 即將消失
    if (!multiRow)
        m_historyView = nullptr;   // ← 新增

    // 刪除現有 layout
    delete layout();

    if (multiRow)
        buildMultiRow();
    else
        buildSingleRow();

    update();
}

// ─────────────────────────────────────────
// 事件
// ─────────────────────────────────────────

void CommandLineWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const int h = event->size().height();
    if (h > MULTI_ROW_THRESHOLD && !m_multiRowMode)
        switchLayout(true);
    else if (h <= MULTI_ROW_THRESHOLD && m_multiRowMode)
        switchLayout(false);

    if (!m_resizing && !m_userPositioned)
        alignToCadView();   // 只在非 resize、未手動定位時才對齊

    if (m_transientHistory)
        m_transientHistory->updatePosition();
}

bool CommandLineWidget::eventFilter(QObject* obj, QEvent* event) {

    // ── 子 Widget hover：轉換座標後更新 resize 游標 ──
    if (event->type() == QEvent::MouseMove) {
        if (auto* cw = qobject_cast<QWidget*>(obj);
            cw && cw != this && isAncestorOf(cw))
        {
            auto* me = static_cast<QMouseEvent*>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPoint localPos = mapFromGlobal(me->globalPosition().toPoint());
#else
            const QPoint localPos = mapFromGlobal(me->globalPos());
#endif
            if (!m_resizing)
                updateResizeCursor(localPos);
        }
    }

    // gripper 雙擊 → 恢復自動對齊
    if (obj == m_gripButton && event->type() == QEvent::MouseButtonDblClick) {
        resetAlignment();
        return true;   // 吃掉，不觸發 pressed
    }

    // cadView resize/move 跟隨（原有邏輯）
    if (obj == m_cadView) {
        switch (event->type()) {

            // CadView 首次顯示：延一個 event loop 再對齊
            // 確保 mapToGlobal 已有正確的 window 座標
        case QEvent::Show:
        case QEvent::WindowActivate:
            break;

            // CadView resize/move：只有初次對齊完成後才跟隨
        case QEvent::Resize:
        case QEvent::Move:
            if (m_initialAlignDone)
                alignToCadView();
            break;

        default:
            break;
        }
    }
    // ↓ 新增：當 MainWindow 本身移動/縮放時，CadView 不發 QEvent::Move，
    //   必須獨立監聽 top-level window
    if (m_cadView && obj == m_cadView->window() && obj != m_cadView) {
        if ((event->type() == QEvent::Move ||
             event->type() == QEvent::Resize) && m_initialAlignDone) {
            alignToCadView();
        }
    }
    return QWidget::eventFilter(obj, event);
}

void CommandLineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // gripper 的 pressed signal 已處理拖曳，這裡不再需要判斷 gripButton
        // 只處理 resize 邊緣
        updateResizeCursor(event->pos());
        if (m_resizeEdge != None) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            startResize(event->globalPosition().toPoint());
#else
            startResize(event->globalPos());
#endif
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void CommandLineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_resizing) {
        if (event->buttons() & Qt::LeftButton) {
            // 正在 resize，持續更新
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            doResize(event->globalPosition().toPoint());
#else
            doResize(event->globalPos());
#endif
            event->accept();
            return;
        } else {
            // 按鍵在 widget 外被放開（Alt-Tab 等），強制結束 resize
            endResize();
        }
    }

    // 非 resize 狀態（含 hover）：始終更新游標與 m_resizeEdge
    updateResizeCursor(event->pos());
    QWidget::mouseMoveEvent(event);
}

void CommandLineWidget::mouseReleaseEvent(QMouseEvent* event) {
    endResize();
    QWidget::mouseReleaseEvent(event);
}

void CommandLineWidget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    if (!m_floating) return;

    QPainter p(this);
    // 固定畫外框
    p.setPen(QPen(QColor(80, 80, 80), 1));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    // resize 中高亮對應邊緣
    if (m_resizeEdge != None) {
        p.setPen(QPen(QColor(100, 180, 255), 2));
        switch (m_resizeEdge) {
        case Right:
            p.drawLine(width()-1, 0, width()-1, height());
            break;
        case Top:
            p.drawLine(0, 0, width(), 0);
            break;
        case TopRight:
            p.drawLine(width()-1, 0, width()-1, height());
            p.drawLine(0, 0, width(), 0);
            break;
        default: break;
        }
    }
}

void CommandLineWidget::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    if (!m_aligning) {
        m_userPositioned = true;
        if (m_floating)
            checkSnapToEdge();   // 嵌入模式座標系不同，不做 snap
    }
    if (m_transientHistory)
        m_transientHistory->updatePosition();
}

void CommandLineWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!m_initialAlignDone) {
        m_initialAlignDone = true;
        // 延到下一個 event loop，確保 WM 完成視窗定位
        QTimer::singleShot(0, this, &CommandLineWidget::alignToCadView);
    }
}

// ─────────────────────────────────────────
// Resize 邊緣拖曳（中心不動，左右同步）
// ─────────────────────────────────────────

void CommandLineWidget::updateResizeCursor(const QPoint& pos) {
    const bool onRight = pos.x() >= width() - RESIZE_MARGIN;
    const bool onTop   = pos.y() <= RESIZE_MARGIN;

    if (onRight && onTop)
        setCursor(Qt::SizeBDiagCursor), m_resizeEdge = TopRight;
    else if (onRight)
        setCursor(Qt::SizeHorCursor), m_resizeEdge = Right;
    else if (onTop)
        setCursor(Qt::SizeVerCursor), m_resizeEdge = Top;
    else
        setCursor(Qt::ArrowCursor), m_resizeEdge = None;
}

void CommandLineWidget::startResize(const QPoint& globalPos) {
    m_resizeStart     = globalPos;
    m_resizeStartSize = size();
    m_resizeStartX    = x();           // Top edge 拖曳需要 x 不變
    m_resizeStartY    = y();
    m_resizing        = true;
    if (m_floating)
        m_userPositioned  = true;
    grabMouse();                        // ← 確保滑鼠移出 widget 時仍收到事件
}

void CommandLineWidget::doResize(const QPoint& globalPos) {
    const QPoint delta = globalPos - m_resizeStart;

    if (m_floating)
        doResizeFloating(delta);
    else
        doResizeEmbedded(delta);
}

void CommandLineWidget::doResizeFloating(const QPoint& delta) {
    const int bottom = m_resizeStartY + m_resizeStartSize.height(); // 底部固定
    int newX = m_resizeStartX;
    int newY = m_resizeStartY;
    int newW = m_resizeStartSize.width();
    int newH = m_resizeStartSize.height() - delta.y();

    switch (m_resizeEdge) {
    case Right:
        newW = qMax(minimumWidth(), newW + delta.x());
        break;
    case Top:
        newH = qMax(SINGLE_ROW_HEIGHT, newH - delta.y());
        newY = bottom - newH;   // 底部不動，上緣往上
        setFixedHeight(newH);
        break;
    case TopRight:
        newW = qMax(minimumWidth(), newW + delta.x());
        newH = qMax(SINGLE_ROW_HEIGHT, newH - delta.y());
        newY = bottom - newH;
        setFixedHeight(newH);
        break;
    default:
        return;
    }

    setGeometry(newX, newY, newW, newH);
}

void CommandLineWidget::doResizeEmbedded(const QPoint& delta) {
    const int centerX = m_resizeStartX + m_resizeStartSize.width() / 2;
    const int bottom  = m_resizeStartY + m_resizeStartSize.height();
    int newW = m_resizeStartSize.width();
    int newH = m_resizeStartSize.height();

    switch (m_resizeEdge) {
    case Right:
        newW = qMax(minimumWidth(), newW + delta.x() * 2);
        break;
    case Top:
        newH = qMax(SINGLE_ROW_HEIGHT, newH - delta.y());
        setFixedHeight(newH);
        break;
    case TopRight:
        newW = qMax(minimumWidth(), newW + delta.x() * 2);
        newH = qMax(SINGLE_ROW_HEIGHT, newH - delta.y());
        setFixedHeight(newH);
        break;
    default:
        return;
    }

    const int newX = centerX - newW / 2;
    const int newY = bottom  - newH;    // 底部固定，上緣往上
    move(newX, newY);
    resize(newW, newH);
}

void CommandLineWidget::endResize() {
    if (m_resizing) {
        releaseMouse();             // ← 對應 grabMouse()
        m_resizing = false;
    }
    m_resizeEdge = None;
    setCursor(Qt::ArrowCursor);
}

// ─────────────────────────────────────────
// 對齊 CadView
// ─────────────────────────────────────────

void CommandLineWidget::alignToCadView() {
    if (!m_cadView)        return;
    //if (!m_userPositioned)  return;   // ← 使用者已手動定位，不覆蓋

    QPoint cadTL = m_cadView->mapToGlobal(QPoint(0, 0));
    int targetX  = cadTL.x() + (m_cadView->width()  - width())  / 2;
    int targetY  = cadTL.y() +  m_cadView->height() - height() - 4;

    m_aligning = true;       // ← 標記「程式主動對齊」
    move(targetX, targetY);
    m_aligning = false;
}

void CommandLineWidget::resetAlignment() {
    m_userPositioned = false;
    m_floating = false;
    alignToCadView();
}

void CommandLineWidget::checkSnapToEdge() {
    if (!m_cadView) return;

    const QPoint cadTL     = m_cadView->mapToGlobal(QPoint(0, 0));
    const int    cadTop    = cadTL.y();
    const int    cadBottom = cadTL.y() + m_cadView->height();

    const int myTop    = y();
    const int myBottom = y() + height();

    const bool nearTop    = qAbs(myTop    - cadTop)    <= SNAP_THRESHOLD;
    const bool nearBottom = qAbs(myBottom - cadBottom) <= SNAP_THRESHOLD;

    if (nearTop || nearBottom)
        resetAlignment();   // m_userPositioned=false → alignToCadView()
}

void CommandLineWidget::attachToCadView() {
    if (!m_cadView) return;

    // 清除上次的 overlay layout（若有）
    if (auto* old = m_cadView->layout()) {
        old->removeWidget(this);
        delete old;
    }

    setWindowFlags(Qt::Widget);
    setParent(m_cadView);

    // 用 layout 取得正確的初始置中位置
    auto* lay = new QVBoxLayout(m_cadView);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addStretch();
    lay->addWidget(this, 0, Qt::AlignBottom | Qt::AlignHCenter);
    m_cadView->setLayout(lay);

    m_floating = false;
    show();
}

void CommandLineWidget::detachToCadView() {
    if (m_floating) return;

    const QPoint globalPos = mapToGlobal(QPoint(0, 0));

    // 先從 cadView layout 移除，避免 layout 仍持有指針
    if (m_cadView && m_cadView->layout()) {
        m_cadView->layout()->removeWidget(this);
        delete m_cadView->layout();
        m_cadView->setLayout(nullptr);
    }

    setParent(nullptr);
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_NativeWindow);
    move(globalPos);
    show();

    m_floating = true;
}

// CommandLineWidget.cpp

// ── 工具函式：遞迴安裝 ──────────────────────
void CommandLineWidget::installMouseFilterOnChildren(QWidget* w) {
    for (QObject* child : w->children()) {
        if (auto* cw = qobject_cast<QWidget*>(child)) {
            cw->setMouseTracking(true);
            cw->installEventFilter(this);
            installMouseFilterOnChildren(cw);   // 遞迴
        }
    }
}

// ─────────────────────────────────────────
// 指令 I/O
// ─────────────────────────────────────────

void CommandLineWidget::onInputSubmit(const QString& text) {
    if (text.isEmpty()) {
        // 重覆上一個指令
        if (!m_recentCommands.isEmpty())
            emit commandSubmitted(m_recentCommands.first());
        return;
    }

    appendHistory(text, false);
    emit commandSubmitted(text);
}

void CommandLineWidget::onRecentMenuTriggered(QAction* action) {
    m_inputEdit->setText(action->text());
    m_inputEdit->setFocus();
}

void CommandLineWidget::submitCommand(const QString& cmd) {
    if (cmd.isEmpty()) return;
    // 直接走 onInputSubmit，行為與使用者手打完全相同：
    // 更新 m_recentCommands、m_recentMenu、inputEdit 歷程、appendHistory、emit commandSubmitted
    onInputSubmit(cmd);
}

void CommandLineWidget::recordResolvedCommand(const QString& resolved) {
    if (resolved.isEmpty()) return;

    QString upper = resolved.trimmed().toUpper();
    // ↑/↓ 鍵歷程：記錄 resolved 名稱
    m_inputEdit->addToHistory(upper);

    // 最近命令清單：去重後以 resolved 置頂
    m_recentCommands.removeAll(upper);
    m_recentCommands.prepend(upper);
    if (m_recentCommands.size() > 10)
        m_recentCommands.removeLast();

    // 重建下拉選單
    m_recentMenu->clear();
    for (const QString& cmd : m_recentCommands)
        m_recentMenu->addAction(cmd);
}

void CommandLineWidget::setLastPrompt(const QString& prompt) {
    m_lastPrompt = prompt;
}

void CommandLineWidget::onHistoryButtonClicked() {
    // 從命令輸入區上緣滑出歷程視窗（等同 F2）
    if (!m_historyPopup) {
        m_historyPopup = new CommandHistoryPopup(this);
        // 把已有歷程填入
        for (int i = 0; i < m_fullHistory.size(); ++i)
            m_historyPopup->appendLine(m_fullHistory[i]);
    }

    m_historyPopup->toggle(m_inputRow);   // ← toggle 取代原來只呼叫一次 slideIn
    emit historyPopupRequested();
}

void CommandLineWidget::onPromptOptionsChanged(const QList<command::InputParser::ParsedOption>& options) {
    setCommandOptions(options);
    if (!options.isEmpty())
        m_inputEdit->setFocus();   // 確保鍵盤輸入仍有效
}

void CommandLineWidget::appendHistory(const QString& text, bool isPrompt) {
    if (isPrompt)
        m_lastPrompt = text;

    m_fullHistory.append(text);

    if (m_historyView) {
        if (isPrompt)
            m_historyView->append("<span style='color:gray;'>" +
                                  text.toHtmlEscaped() + "</span>");
        else
            m_historyView->append(text.toHtmlEscaped());
        m_historyView->verticalScrollBar()->setValue(
            m_historyView->verticalScrollBar()->maximum());
    }

    // 同步寫入 popup（若已建立）
    if (m_historyPopup)
        m_historyPopup->appendLine(text, isPrompt);

    if (!m_multiRowMode)
        m_transientHistory->addLine(text, isPrompt);
}

void CommandLineWidget::setCommandOptions(const QList<command::InputParser::ParsedOption>& options) {
    if (options.isEmpty()) {
        m_inputEdit->clearPromptOptions();
        return;
    }

    // ★ 修正：截取 "[" 前的文字作為 prefix（m_lastPrompt 由 appendHistory 之前已設）
    QString prefix = m_lastPrompt;
    const int bracketIdx = prefix.indexOf('[');
    if (bracketIdx != -1)
        prefix = prefix.left(bracketIdx).trimmed();

    m_inputEdit->setPromptOptions(prefix, options);
    m_inputEdit->setFocus();
    m_optionsBar->setVisible(false);   // 舊 bar 隱藏
}

void CommandLineWidget::clearCommandOptions() {
    setCommandOptions({});
}

void CommandLineWidget::toggleVisible() {
    setVisible(!isVisible());
}

CommandInputEdit* CommandLineWidget::inputEdit(){
    return m_inputEdit;}

TransientCommandHistory* CommandLineWidget::transientHistory() const {
    return m_transientHistory; }

void CommandLineWidget::saveState(QSettings* settings) {
    settings->beginGroup("CommandLineWidget");
    settings->setValue("geometry",       saveGeometry());
    settings->setValue("multiRow",       m_multiRowMode);
    settings->setValue("recentCommands", m_recentCommands);  // ← 取代 combo
    settings->endGroup();
}

void CommandLineWidget::restoreState(QSettings* settings) {
    settings->beginGroup("CommandLineWidget");
    restoreGeometry(settings->value("geometry").toByteArray());
    m_recentCommands = settings->value("recentCommands").toStringList();  // ← 取代
    // 還原 menu
    m_recentMenu->clear();
    for (const QString& cmd : m_recentCommands)
        m_recentMenu->addAction(cmd);
    settings->endGroup();
}

} // namespace ui
} // namespace aicad
