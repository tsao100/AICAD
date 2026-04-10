#include "CommandLineWidget.h"
#include "CommandInputEdit.h"
#include "TransientCommandHistory.h"
#include "CommandHistoryPopup.h"
#include <QApplication>
#include <QWindow>
#include <QCursor>
#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QScrollBar>

namespace aicad {
namespace ui {

CommandLineWidget::CommandLineWidget(QWidget* cadView, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
    , m_cadView(cadView)
{
    setAttribute(Qt::WA_StyledBackground);
    setMouseTracking(true);
    setMinimumHeight(SINGLE_ROW_HEIGHT);
    setMinimumWidth(300);

    setupGripper();
    setupCloseButton();
    setupCustomizeButton();
    setupComboAndInput();
    setupHistoryButton();
    setupOptionsBar();
    setupTransientHistory();
    setupResizeHandles();

    buildSingleRow();
    alignToCadView();

    if (m_cadView)
        m_cadView->installEventFilter(this);
}

CommandLineWidget::~CommandLineWidget() = default;

// ─────────────────────────────────────────
// 子 Widget 建立
// ─────────────────────────────────────────

void CommandLineWidget::setupGripper() {
    m_gripButton = new QToolButton(this);
    m_gripButton->setToolTip(tr("移動命令列"));
    m_gripButton->setCursor(Qt::SizeAllCursor);
    m_gripButton->setFixedSize(16, SINGLE_ROW_HEIGHT);
    // 繪製點陣 gripper 圖示
    m_gripButton->setIcon(QIcon());   // 在 paintEvent 中直接繪製，或用自訂 icon

    // 讓 gripper 可拖曳整個視窗
    connect(m_gripButton, &QToolButton::pressed, this, [this]() {
        // 使用視窗移動慣例
        if (window()) {
            // Qt 沒有直接提供拖曳移動，用 mousePressEvent 攔截
        }
    });
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
    m_customizeButton->setMenu(menu);
}

void CommandLineWidget::setupComboAndInput() {
    m_inputRow    = new QWidget(this);
    m_inputRowLayout = new QHBoxLayout(m_inputRow);
    m_inputRowLayout->setContentsMargins(2, 0, 2, 0);
    m_inputRowLayout->setSpacing(2);

    // 最近使用指令 combobox（可編輯，與輸入區共用行）
    m_recentCombo = new QComboBox(m_inputRow);
    m_recentCombo->setEditable(false);
    m_recentCombo->setInsertPolicy(QComboBox::NoInsert);
    m_recentCombo->setToolTip(tr("最近使用的指令"));
    m_recentCombo->setFixedWidth(120);
    connect(m_recentCombo, QOverload<int>::of(&QComboBox::activated),
            this, &CommandLineWidget::onComboActivated);

    // 命令輸入區
    m_inputEdit = new CommandInputEdit(m_inputRow);
    connect(m_inputEdit, &CommandInputEdit::commandSubmitted,
            this, &CommandLineWidget::onInputSubmit);

    m_inputRowLayout->addWidget(m_recentCombo);
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
    // TransientCommandHistory 是獨立的頂層視窗（浮動在命令列上方）
    m_transientHistory = new TransientCommandHistory(this);
    m_transientHistory->hide();
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
    if (m_rightSplitter) m_rightSplitter->deleteLater();
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
    alignToCadView();
}

bool CommandLineWidget::eventFilter(QObject* obj, QEvent* event) {
    // CadView resize -> 重新對齊
    if (obj == m_cadView &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move))
    {
        alignToCadView();
    }
    return QWidget::eventFilter(obj, event);
}

void CommandLineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // 判斷是否在 gripper 上 → 移動視窗
        if (m_gripButton->geometry().contains(event->pos())) {
            // 使用 startSystemMove (Qt 5.15+) 或手動拖曳
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            window()->windowHandle()->startSystemMove();
#else
            m_resizeStart = event->globalPos() - pos();
#endif
            return;
        }
        // 判斷是否在右/上邊緣
        updateResizeCursor(event->pos());
        if (m_resizeEdge != None) {
            startResize(event->globalPos());
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void CommandLineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_resizeEdge != None && (event->buttons() & Qt::LeftButton)) {
        doResize(event->globalPos());
        return;
    }
    updateResizeCursor(event->pos());
    QWidget::mouseMoveEvent(event);
}

void CommandLineWidget::mouseReleaseEvent(QMouseEvent* event) {
    endResize();
    QWidget::mouseReleaseEvent(event);
}

void CommandLineWidget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    // 可在此繪製邊框
}

// ─────────────────────────────────────────
// Resize 邊緣拖曳（中心不動，左右同步）
// ─────────────────────────────────────────

void CommandLineWidget::updateResizeCursor(const QPoint& pos) {
    const bool onRight = pos.x() >= width() - RESIZE_MARGIN;
    const bool onTop   = pos.y() <= RESIZE_MARGIN;

    if (onRight && onTop)
        setCursor(Qt::SizeBDiagCursor);
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
    m_resizeStartX    = x();
}

void CommandLineWidget::doResize(const QPoint& globalPos) {
    const QPoint delta = globalPos - m_resizeStart;

    if (m_resizeEdge == Right) {
        // 右邊緣拖曳：中心不動，左右同步加寬
        int newWidth = m_resizeStartSize.width() + delta.x() * 2;
        newWidth = qMax(minimumWidth(), newWidth);
        int newX = m_resizeStartX - (newWidth - m_resizeStartSize.width()) / 2;
        setGeometry(newX, y(), newWidth, height());
    } else if (m_resizeEdge == Top) {
        // 上邊緣拖曳：往上拉高
        int newHeight = m_resizeStartSize.height() - delta.y();
        newHeight = qMax(SINGLE_ROW_HEIGHT, newHeight);
        int newY = y() + (m_resizeStartSize.height() - newHeight);
        setGeometry(x(), newY, width(), newHeight);
    }
}

void CommandLineWidget::endResize() {
    m_resizeEdge = None;
    setCursor(Qt::ArrowCursor);
}

// ─────────────────────────────────────────
// 對齊 CadView
// ─────────────────────────────────────────

void CommandLineWidget::alignToCadView() {
    if (!m_cadView) return;
    // 命令列底部對齊 CadView 底部，水平置中

    QPoint cadTopLeft = m_cadView->mapToGlobal(QPoint(0,0));

    int cadW = m_cadView->width();
    int cadH = m_cadView->height();

    int cadCenterX = cadTopLeft.x() + cadW / 2;
    int cadBottomY = cadTopLeft.y() + cadH;

    int myW = width();
    int myH = height();

    int targetX = cadCenterX + myW / 2;
    int targetY = cadBottomY + myH + 100;

    //QWidget* p = parentWidget();
    QPoint local =  m_cadView->mapFromGlobal(QPoint(targetX, targetY));

    move(local);
}

// ─────────────────────────────────────────
// 指令 I/O
// ─────────────────────────────────────────

void CommandLineWidget::onInputSubmit(const QString& text) {
    if (text.isEmpty()) {
        // 空白鍵/Enter 重覆上一個指令
        if (m_recentCombo->count() > 0)
            emit commandSubmitted(m_recentCombo->itemText(0));
        return;
    }
    // 加入最近指令 combobox
    int idx = m_recentCombo->findText(text);
    if (idx != -1) m_recentCombo->removeItem(idx);
    m_recentCombo->insertItem(0, text);
    m_recentCombo->setCurrentIndex(0);

    appendHistory(text, false);
    emit commandSubmitted(text);
}

void CommandLineWidget::onComboActivated(int index) {
    const QString cmd = m_recentCombo->itemText(index);
    if (!cmd.isEmpty())
        m_inputEdit->setText(cmd);
    m_inputEdit->setFocus();
}

void CommandLineWidget::onHistoryButtonClicked() {
    // 從命令輸入區上緣滑出歷程視窗（等同 F2）
    if (!m_historyPopup) {
        m_historyPopup = new CommandHistoryPopup(this);
        m_historyPopup->slideIn(m_inputRow);
    }
    emit historyPopupRequested();
}

void CommandLineWidget::appendHistory(const QString& text, bool isPrompt) {
    if (m_historyView) {
        if (isPrompt)
            m_historyView->append("<span style='color:gray;'>" +
                                  text.toHtmlEscaped() + "</span>");
        else
            m_historyView->append(text.toHtmlEscaped());
        m_historyView->verticalScrollBar()->setValue(
            m_historyView->verticalScrollBar()->maximum());
    }
    m_transientHistory->addLine(text, isPrompt);
}

void CommandLineWidget::setCommandOptions(const QStringList& options) {
    // 清除舊按鈕
    QLayoutItem* item;
    while ((item = m_optionsLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    for (const QString& opt : options) {
        auto* btn = new QToolButton(m_optionsBar);
        btn->setText(opt);
        connect(btn, &QToolButton::clicked, this, [this, opt](){
            emit optionSelected(opt);
        });
        m_optionsLayout->addWidget(btn);
    }
    m_optionsLayout->addStretch();
    m_optionsBar->setVisible(!options.isEmpty());
}

void CommandLineWidget::clearCommandOptions() {
    setCommandOptions({});
}

void CommandLineWidget::toggleVisible() {
    setVisible(!isVisible());
}

void CommandLineWidget::setHistoryTransientLines(int n) {
    m_transientLines = qBound(1, n, 3);
    m_transientHistory->setMaxLines(m_transientLines);
}

CommandInputEdit* CommandLineWidget::inputEdit(){
    return m_inputEdit;}

TransientCommandHistory* CommandLineWidget::transientHistory() const {
    return m_transientHistory; }

void CommandLineWidget::saveState(QSettings* settings) {
    settings->beginGroup("CommandLineWidget");
    settings->setValue("geometry", saveGeometry());
    settings->setValue("multiRow", m_multiRowMode);
    settings->setValue("transientLines", m_transientLines);
    settings->endGroup();
}

void CommandLineWidget::restoreState(QSettings* settings) {
    settings->beginGroup("CommandLineWidget");
    restoreGeometry(settings->value("geometry").toByteArray());
    m_transientLines = settings->value("transientLines", 3).toInt();
    settings->endGroup();
}

} // namespace ui
} // namespace aicad
