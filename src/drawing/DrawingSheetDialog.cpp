/**
 * @file DrawingSheetDialog.cpp
 * @brief 圖紙設定主對話框實作 — 8 個 Tab
 * @author AICAD Team
 * @date 2025-01-08
 *
 * Tab 總覽：
 *   0 – 預覽        DrawingSheetWidget（互動預覽）
 *   1 – 圖紙設定    紙張、方向、比例、投影法、圖框
 *   2 – 標題欄      TitleBlock 所有欄位
 *   3 – 視圖管理    新增/移除/屬性
 *   4 – 零件表      啟用、欄位、手動編輯
 *   5 – 進版說明    RevisionTable
 *   6 – 圖目錄      DrawingIndex
 *   7 – 圖例&說明   Legend + NoteBlock
 */

#include "DrawingSheetWidget.h"
#include "DrawingSheetManager.h"
#include "PlanProfileDialog.h"
#include <QFileInfo>
#include <QSignalBlocker>
#include <QUuid>   // DrawingSheetDialog 宣告在同一個 .h
#include "DrawingSheet.h"
#include "cad/Document.h"
#include "cad/Feature.h"
#include <functional>
#include <memory>

#include <QTabWidget>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QSettings>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QFrame>
#include <QScrollArea>
#include <QSizePolicy>
#include <QDateTime>
#include <QDebug>

namespace aicad {
namespace drawing {

// ─────────────────────────────────────────────────────────────────────────────
// 小工具函式
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/** 建立帶標題的分組框，回傳其內部佈局 */
QFormLayout* makeGroupForm(const QString& title, QWidget* parent, QLayout* addTo)
{
    auto* group = new QGroupBox(title, parent);
    auto* form  = new QFormLayout(group);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    addTo->addWidget(group);
    return form;
}

/** 建立水平分隔線 */
QFrame* makeSeparator(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

/** 建立標準的「新增 / 刪除」工具列回傳 pair<QToolBar*, {addAct, removeAct}> */
struct TableToolbar {
    QToolBar* bar;
    QAction* addAct;
    QAction* removeAct;
    QAction* moveUpAct;
    QAction* moveDownAct;
};

TableToolbar makeTableToolbar(QWidget* parent)
{
    auto* bar = new QToolBar(parent);
    bar->setIconSize(QSize(16, 16));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto* addAct    = bar->addAction("＋ Add");
    auto* removeAct = bar->addAction("－ Remove");
    bar->addSeparator();
    auto* upAct     = bar->addAction("▲");
    auto* downAct   = bar->addAction("▼");

    return { bar, addAct, removeAct, upAct, downAct };
}

} // anon namespace

// ─────────────────────────────────────────────────────────────────────────────
// 建構
// ─────────────────────────────────────────────────────────────────────────────

DrawingSheetDialog::DrawingSheetDialog(DrawingSheet* sheet, QWidget* parent)
    : QDialog(parent)
    , m_sheet(sheet)
    , m_mgr(sheet ? qobject_cast<DrawingSheetManager*>(sheet->parent()) : nullptr)
{
    Q_ASSERT(sheet);
    setWindowTitle(QString("Drawing Sheet — %1")
                       .arg(sheet->titleBlock().drawingTitle.isEmpty()
                                ? "Untitled" : sheet->titleBlock().drawingTitle));
    setMinimumSize(1100, 700);
    resize(1200, 780);

    setupUi();
    loadFromSheet();

    // 採用前次的視窗大小／位置與分頁（QSettings）
    {
        QSettings qs("AICAD", "AICAD");
        qs.beginGroup("DrawingSheetDialog");
        const QByteArray g = qs.value("geometry").toByteArray();
        if (!g.isEmpty()) restoreGeometry(g);
        const int tab = qs.value("tab", 0).toInt();
        if (m_tabs && tab >= 0 && tab < m_tabs->count()) m_tabs->setCurrentIndex(tab);
        qs.endGroup();
    }

    qDebug() << "[DrawingSheetDialog] Created";
}

DrawingSheetDialog::~DrawingSheetDialog()
{
    // 儲存對話框設定（下次開啟採用）
    {
        QSettings qs("AICAD", "AICAD");
        qs.beginGroup("DrawingSheetDialog");
        qs.setValue("geometry", saveGeometry());
        if (m_tabs) qs.setValue("tab", m_tabs->currentIndex());
        if (auto* a = findChild<QAction*>("gridAct"))  qs.setValue("grid", a->isChecked());
        if (auto* a = findChild<QAction*>("rulerAct")) qs.setValue("rulers", a->isChecked());
        qs.endGroup();
    }
    qDebug() << "[DrawingSheetDialog] Destroyed";
}

// ─────────────────────────────────────────────────────────────────────────────
// UI 建構
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    // ── Tab 容器 ──────────────────────────────────────────────────────────
    m_tabs = new QTabWidget(this);
    m_tabs->setTabPosition(QTabWidget::North);
    mainLayout->addWidget(m_tabs, 1);

    setupPreviewTab();
    setupSheetConfigTab();
    setupTitleBlockTab();
    setupViewsTab();
    setupPartsListTab();
    setupRevisionTab();
    setupDrawingIndexTab();
    setupLegendTab();

    // ── 底部按鈕列 ────────────────────────────────────────────────────────
    auto* btnLayout = new QHBoxLayout();

    auto* exportPdfBtn = new QPushButton("Export PDF…", this);
    auto* exportSvgBtn = new QPushButton("Export SVG…", this);
    btnLayout->addWidget(exportPdfBtn);
    btnLayout->addWidget(exportSvgBtn);
    btnLayout->addStretch();

    auto* btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel,
        this);
    btnLayout->addWidget(btnBox);
    mainLayout->addLayout(btnLayout);

    // 連接信號
    connect(btnBox, &QDialogButtonBox::accepted, this, &DrawingSheetDialog::onOk);
    connect(btnBox, &QDialogButtonBox::rejected, this, &DrawingSheetDialog::onCancel);
    connect(btnBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &DrawingSheetDialog::onApply);
    connect(exportPdfBtn, &QPushButton::clicked, this, &DrawingSheetDialog::onExportPdf);
    connect(exportSvgBtn, &QPushButton::clicked, this, &DrawingSheetDialog::onExportSvg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab 0 — 預覽
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetDialog::refreshPageCombo()
{
    if (!m_pageCombo) return;
    QSignalBlocker block(m_pageCombo);
    m_pageCombo->clear();
    if (!m_mgr) {
        m_pageCombo->addItem(m_sheet ? m_sheet->titleBlock().drawingTitle : QString());
        m_pageCombo->setEnabled(false);
        return;
    }
    m_pageCombo->setEnabled(true);
    int cur = 0, n = 0;
    for (DrawingSheet* s : m_mgr->sheets()) {
        const TitleBlock& tb = s->titleBlock();
        m_pageCombo->addItem(QString("%1. %2  %3  %4")
                                 .arg(n + 1).arg(tb.drawingNumber, tb.drawingTitle, tb.sheetNumber));
        if (s == m_sheet) cur = n;
        ++n;
    }
    m_pageCombo->setCurrentIndex(cur);
}

void DrawingSheetDialog::switchToSheet(DrawingSheet* s)
{
    if (!s || s == m_sheet) return;
    applyToSheet();                      // 先把目前圖紙的編輯寫回
    m_sheet = s;
    if (m_mgr) m_mgr->setActiveSheet(s);
    if (m_preview) m_preview->setSheet(s);
    loadFromSheet();
    if (m_preview) m_preview->fitToWindow();
    setWindowTitle(QString("Drawing Sheet — %1")
                       .arg(s->titleBlock().drawingTitle.isEmpty() ? "Untitled"
                                                                   : s->titleBlock().drawingTitle));
    refreshPageCombo();
}

void DrawingSheetDialog::setupPreviewTab()
{
    auto* tab    = new QWidget();
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);

    // 預覽控制列
    auto* ctrlBar = new QToolBar(tab);
    ctrlBar->setIconSize(QSize(14, 14));

    auto* fitAct    = ctrlBar->addAction("Fit");
    auto* zoomInAct = ctrlBar->addAction("Zoom +");
    auto* zoomOutAct= ctrlBar->addAction("Zoom -");
    ctrlBar->addSeparator();
    QSettings uiqs("AICAD", "AICAD");
    auto* gridAct   = ctrlBar->addAction("Grid");
    gridAct->setObjectName("gridAct");
    gridAct->setCheckable(true);
    gridAct->setChecked(uiqs.value("DrawingSheetDialog/grid", true).toBool());
    auto* rulerAct  = ctrlBar->addAction("Rulers");
    rulerAct->setObjectName("rulerAct");
    rulerAct->setCheckable(true);
    rulerAct->setChecked(uiqs.value("DrawingSheetDialog/rulers", true).toBool());

    // ── 視圖歷史：返回前一視圖／下一視圖 ─────────────────────────────────────
    ctrlBar->addSeparator();
    auto* backAct = ctrlBar->addAction(QStringLiteral("⟲ Back"));
    backAct->setToolTip(QStringLiteral("返回前一視圖（縮放／平移／圖紙）"));
    backAct->setEnabled(false);
    auto* fwdAct = ctrlBar->addAction(QStringLiteral("Next View ⟳"));
    fwdAct->setToolTip(QStringLiteral("下一視圖"));
    fwdAct->setEnabled(false);
    ctrlBar->addSeparator();
    ctrlBar->addAction("Rebuild All", [this]() {
        if (m_preview) m_preview->triggerRenderAll();
    });
    ctrlBar->addAction("Auto Layout", [this]() {
        if (!m_sheet || !m_preview) return;
        m_sheet->autoLayout();                   // 依投影法排列正投影視圖，其餘放右側
        m_preview->update();
    });
    ctrlBar->addAction("Auto-Fit All", [this]() {
        if (!m_sheet || !m_preview) return;
        for (DrawingView* v : m_sheet->views())
            v->setAutoFit(true);                 // 同時觸發重新渲染 → 重新 Fit
        m_preview->triggerRenderAll();
    });

    // ── 換頁：◀ 頁次下拉 ▶ 圖目錄… ────────────────────────────────────────
    ctrlBar->addSeparator();
    auto* prevAct = ctrlBar->addAction(QStringLiteral("◀ Prev Page"));
    m_pageCombo = new QComboBox(ctrlBar);
    m_pageCombo->setMinimumWidth(260);
    m_pageCombo->setToolTip(QStringLiteral("切換到其他圖紙"));
    ctrlBar->addWidget(m_pageCombo);
    auto* nextAct = ctrlBar->addAction(QStringLiteral("Next Page ▶"));
    auto* indexAct = ctrlBar->addAction(QStringLiteral("Index…"));
    indexAct->setToolTip(QStringLiteral("調整圖的順序並重建圖目錄"));
    refreshPageCombo();

    auto step = [this](int d) {
        if (!m_mgr) return;
        const auto& all = m_mgr->sheets();
        const int i = all.indexOf(m_sheet);
        const int t = i + d;
        if (i < 0 || t < 0 || t >= all.size()) return;
        switchToSheet(all[t]);
    };
    connect(prevAct, &QAction::triggered, this, [step]() { step(-1); });
    connect(nextAct, &QAction::triggered, this, [step]() { step(1); });
    connect(m_pageCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int idx) {
        if (!m_mgr || idx < 0 || idx >= m_mgr->sheets().size()) return;
        switchToSheet(m_mgr->sheets()[idx]);
    });
    connect(indexAct, &QAction::triggered, this, [this]() {
        if (!m_mgr || !m_sheet || !m_sheet->sourceDocument()) return;
        applyToSheet();
        const QUuid keep = m_sheet->id();
        if (!runPlanProfileIndexEditor(m_mgr, m_sheet->sourceDocument(), this, m_sheet->bookId())) return;
        DrawingSheet* t = m_mgr->findSheet(keep);
        if (!t && !m_mgr->sheets().isEmpty()) t = m_mgr->sheets().first();
        if (t) {
            m_sheet = t;
            m_preview->setSheet(t);
            loadFromSheet();
            m_preview->fitToWindow();
            setWindowTitle(QString("Drawing Sheet — %1").arg(t->titleBlock().drawingTitle));
        }
        refreshPageCombo();
    });

    layout->addWidget(ctrlBar);

    // 預覽 Widget
    m_preview = new DrawingSheetWidget(tab);
    m_preview->setSheet(m_sheet);
    layout->addWidget(m_preview, 1);

    // 連接工具列
    connect(fitAct,     &QAction::triggered, m_preview, &DrawingSheetWidget::fitToWindow);
    connect(zoomInAct,  &QAction::triggered, m_preview, &DrawingSheetWidget::zoomIn);
    connect(zoomOutAct, &QAction::triggered, m_preview, &DrawingSheetWidget::zoomOut);
    connect(gridAct,    &QAction::toggled,   m_preview, &DrawingSheetWidget::setGridVisible);
    connect(rulerAct,   &QAction::toggled,   m_preview, &DrawingSheetWidget::setRulersVisible);
    m_preview->setGridVisible(gridAct->isChecked());
    m_preview->setRulersVisible(rulerAct->isChecked());

    // 視圖歷史
    connect(backAct, &QAction::triggered, m_preview, &DrawingSheetWidget::viewBack);
    connect(fwdAct,  &QAction::triggered, m_preview, &DrawingSheetWidget::viewForward);
    connect(m_preview, &DrawingSheetWidget::historyChanged, this, [this, backAct, fwdAct]() {
        backAct->setEnabled(m_preview->canViewBack());
        fwdAct->setEnabled(m_preview->canViewForward());
    });
    connect(m_preview, &DrawingSheetWidget::historySheetRequested, this, [this](const QUuid& id) {
        if (!m_mgr) return;
        if (DrawingSheet* s = m_mgr->findSheet(id)) switchToSheet(s);
    });

    // 雙擊圖目錄列 → 切換到該圖頁（以圖號比對，優先同圖冊）
    connect(m_preview, &DrawingSheetWidget::indexEntryActivated, this,
            [this](const QString& drawingNumber, const QString& sheetNumber) {
        if (!m_mgr || !m_sheet) return;
        DrawingSheet* target = nullptr;
        for (DrawingSheet* s : m_mgr->sheets()) {
            if (s == m_sheet) continue;
            const bool match = !drawingNumber.isEmpty()
                                   ? s->titleBlock().drawingNumber == drawingNumber
                                   : (!sheetNumber.isEmpty() && s->titleBlock().sheetNumber == sheetNumber);
            if (!match) continue;
            if (s->bookId() == m_sheet->bookId()) { target = s; break; }
            if (!target) target = s;
        }
        if (target) switchToSheet(target);
    });

    // 視圖雙擊 → 跳到視圖管理 Tab
    connect(m_preview, &DrawingSheetWidget::viewDoubleClicked,
            this, [this](DrawingView*) {
                m_tabs->setCurrentIndex(3);   // Views tab
            });

    m_tabs->addTab(tab, "🔍 Preview");
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab 1 — 圖紙設定
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetDialog::setupSheetConfigTab()
{
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    auto* tab    = new QWidget();
    auto* layout = new QVBoxLayout(tab);
    layout->setAlignment(Qt::AlignTop);

    // ── 紙張規格 ──────────────────────────────────────────────────────────
    auto* specForm = makeGroupForm("Paper Specification", tab, layout);

    m_paperSizeCombo = new QComboBox(tab);
    for (const char* s : {"A4 (210×297)", "A3 (297×420)", "A2 (420×594)",
                          "A1 (594×841)", "A0 (841×1189)", "B4", "B3",
                          "Letter", "Tabloid", "Custom"})
        m_paperSizeCombo->addItem(s);
    specForm->addRow("Paper Size:", m_paperSizeCombo);

    m_orientationCombo = new QComboBox(tab);
    m_orientationCombo->addItems({"Portrait", "Landscape"});
    specForm->addRow("Orientation:", m_orientationCombo);

    m_unitCombo = new QComboBox(tab);
    m_unitCombo->addItems({"Millimeter (mm)", "Inch (in)"});
    specForm->addRow("Unit:", m_unitCombo);

    // ── 圖框設定 ──────────────────────────────────────────────────────────
    auto* frameForm = makeGroupForm("Frame & Template", tab, layout);

    m_frameTemplateCombo = new QComboBox(tab);
    m_frameTemplateCombo->addItems({"None", "Simple", "Standard", "ISO 7200", "Custom…"});
    frameForm->addRow("Frame Template:", m_frameTemplateCombo);

    m_projectionCombo = new QComboBox(tab);
    m_projectionCombo->addItems({"First Angle (European)",
                                 "Third Angle (US / Taiwan)"});
    frameForm->addRow("Projection Method:", m_projectionCombo);

    // ── 比例設定 ──────────────────────────────────────────────────────────
    auto* scaleForm = makeGroupForm("Scale & Line Widths", tab, layout);

    m_defaultScaleSpin = new QDoubleSpinBox(tab);
    m_defaultScaleSpin->setRange(0.01, 100.0);
    m_defaultScaleSpin->setSingleStep(0.5);
    m_defaultScaleSpin->setDecimals(2);
    m_defaultScaleSpin->setSuffix("  (1 : N or N : 1)");
    scaleForm->addRow("Default Scale:", m_defaultScaleSpin);

    auto* lwThinSpin   = new QDoubleSpinBox(tab);
    auto* lwMediumSpin = new QDoubleSpinBox(tab);
    auto* lwThickSpin  = new QDoubleSpinBox(tab);
    for (auto* s : {lwThinSpin, lwMediumSpin, lwThickSpin}) {
        s->setRange(0.05, 2.0); s->setSingleStep(0.05);
        s->setDecimals(2);      s->setSuffix(" mm");
    }
    // 儲存指標供 applyToSheet() 使用
    lwThinSpin->setObjectName("lwThin");
    lwMediumSpin->setObjectName("lwMedium");
    lwThickSpin->setObjectName("lwThick");

    scaleForm->addRow("Thin Line Width:",   lwThinSpin);
    scaleForm->addRow("Medium Line Width:", lwMediumSpin);
    scaleForm->addRow("Thick Line Width:",  lwThickSpin);

    // 即時預覽
    auto applyAndRefresh = [this]() { onSheetConfigEdited(); };
    connect(m_paperSizeCombo,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, applyAndRefresh);
    connect(m_orientationCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, applyAndRefresh);
    connect(m_frameTemplateCombo,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, applyAndRefresh);
    connect(m_projectionCombo,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, applyAndRefresh);
    connect(m_unitCombo,         QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, applyAndRefresh);
    connect(m_defaultScaleSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, applyAndRefresh);
    for (auto* s : {lwThinSpin, lwMediumSpin, lwThickSpin})
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyAndRefresh);

    layout->addStretch();
    scroll->setWidget(tab);
    m_tabs->addTab(scroll, "📄 Sheet");
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab 2 — 標題欄
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetDialog::setupTitleBlockTab()
{
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    auto* tab    = new QWidget();
    auto* layout = new QVBoxLayout(tab);
    layout->setAlignment(Qt::AlignTop);

    auto* docForm = makeGroupForm("Document Identification", tab, layout);
    m_drawingNumberEdit = new QLineEdit(tab);
    m_drawingTitleEdit  = new QLineEdit(tab);
    m_sheetNumberEdit   = new QLineEdit(tab);
    m_revisionEdit      = new QLineEdit(tab);
    docForm->addRow("Drawing Number:", m_drawingNumberEdit);
    docForm->addRow("Drawing Title:",  m_drawingTitleEdit);
    docForm->addRow("Sheet Number:",   m_sheetNumberEdit);
    docForm->addRow("Revision:",       m_revisionEdit);

    auto* orgForm = makeGroupForm("Organization", tab, layout);
    m_companyNameEdit  = new QLineEdit(tab);
    m_projectNameEdit  = new QLineEdit(tab);
    m_partNumberEdit   = new QLineEdit(tab);
    auto* logoLayout   = new QHBoxLayout();
    auto* logoEdit     = new QLineEdit(tab);
    logoEdit->setObjectName("companyLogo");
    auto* logoBrowse   = new QPushButton("Browse…", tab);
    logoLayout->addWidget(logoEdit);
    logoLayout->addWidget(logoBrowse);
    connect(logoBrowse, &QPushButton::clicked, [this, logoEdit]() {
        QString path = QFileDialog::getOpenFileName(this, "Select Logo",
                                                    {}, "Images (*.png *.svg *.jpg)");
        if (!path.isEmpty()) logoEdit->setText(path);
    });
    orgForm->addRow("Company Name:", m_companyNameEdit);
    orgForm->addRow("Company Logo:", logoLayout);
    orgForm->addRow("Project Name:", m_projectNameEdit);
    orgForm->addRow("Part Number:",  m_partNumberEdit);

    auto* signForm = makeGroupForm("Signatures & Dates", tab, layout);
    m_designedByEdit  = new QLineEdit(tab);
    m_checkedByEdit   = new QLineEdit(tab);
    m_approvedByEdit  = new QLineEdit(tab);

    auto* designDateEdit   = new QLineEdit(tab);
    auto* approvalDateEdit = new QLineEdit(tab);
    designDateEdit->setObjectName("designDate");
    approvalDateEdit->setObjectName("approvalDate");

    auto* todayBtn = new QPushButton("Today", tab);
    auto* hdate = new QHBoxLayout();
    hdate->addWidget(designDateEdit);
    hdate->addWidget(todayBtn);
    connect(todayBtn, &QPushButton::clicked, [designDateEdit]() {
        designDateEdit->setText(QDate::currentDate().toString("yyyy-MM-dd"));
    });

    signForm->addRow("Designed By:",  m_designedByEdit);
    signForm->addRow("Checked By:",   m_checkedByEdit);
    signForm->addRow("Approved By:",  m_approvedByEdit);
    signForm->addRow("Design Date:",  hdate);
    signForm->addRow("Approval Date:",approvalDateEdit);

    auto* specForm = makeGroupForm("Technical Specifications", tab, layout);
    m_materialEdit  = new QLineEdit(tab);
    m_toleranceEdit = new QLineEdit(tab);
    auto* angTolEdit   = new QLineEdit(tab);
    auto* surfEdit     = new QLineEdit(tab);
    angTolEdit->setObjectName("angularTolerance");
    surfEdit->setObjectName("surfaceFinish");
    specForm->addRow("Material:",          m_materialEdit);
    specForm->addRow("General Tolerance:", m_toleranceEdit);
    specForm->addRow("Angular Tolerance:", angTolEdit);
    specForm->addRow("Surface Finish:",    surfEdit);

    // 即時預覽：標題欄欄位改變 → 更新預覽
    for (auto* edit : tab->findChildren<QLineEdit*>())
        connect(edit, &QLineEdit::textChanged, this, &DrawingSheetDialog::onTitleBlockEdited);

    layout->addStretch();
    scroll->setWidget(tab);
    m_tabs->addTab(scroll, "🏷  Title Block");
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab 3 — 視圖管理
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetDialog::setupViewsTab()
{
    auto* tab    = new QWidget();
    auto* layout = new QHBoxLayout(tab);

    // 左側：視圖清單
    auto* leftLayout = new QVBoxLayout();

    auto tb = makeTableToolbar(tab);
    leftLayout->addWidget(tb.bar);

    m_viewList = new QListWidget(tab);
    m_viewList->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(m_viewList, 1);

    // 按鈕
    auto* addViewCombo = new QComboBox(tab);
    for (const char* n : {"Front", "Top", "Right", "Left", "Bottom",
                          "Back", "Isometric", "Section", "Detail"})
        addViewCombo->addItem(n);
    leftLayout->addWidget(addViewCombo);

    layout->addLayout(leftLayout, 0);
    layout->addWidget(makeSeparator(tab));

    // 右側：選取的視圖屬性
    auto* rightScroll = new QScrollArea(tab);
    rightScroll->setWidgetResizable(true);
    auto* propWidget = new QWidget();
    auto* propLayout = new QVBoxLayout(propWidget);
    propLayout->setAlignment(Qt::AlignTop);

    auto* basicForm = makeGroupForm("View Properties", propWidget, propLayout);

    auto* viewLabelEdit = new QLineEdit(propWidget);
    viewLabelEdit->setObjectName("viewLabel");
    basicForm->addRow("Label:", viewLabelEdit);

    auto* viewScaleSpin = new QDoubleSpinBox(propWidget);
    viewScaleSpin->setObjectName("viewScale");
    viewScaleSpin->setRange(0.000001, 1000.0);   // 大型模型可能需要 1:5000 以上
    viewScaleSpin->setSingleStep(0.0001);
    viewScaleSpin->setDecimals(6);
    viewScaleSpin->setToolTip("手動改比例會關閉該視圖的自動 Fit；"
                              "預覽工具列的「Auto-Fit All」可重新啟用。");
    basicForm->addRow("Scale:", viewScaleSpin);

    auto* viewPosXSpin = new QDoubleSpinBox(propWidget);
    auto* viewPosYSpin = new QDoubleSpinBox(propWidget);
    viewPosXSpin->setObjectName("viewPosX");
    viewPosYSpin->setObjectName("viewPosY");
    for (auto* s : {viewPosXSpin, viewPosYSpin}) {
        s->setRange(-2000, 2000);
        s->setDecimals(1);
        s->setSuffix(" mm");
    }
    auto* posLayout = new QHBoxLayout();
    posLayout->addWidget(new QLabel("X:", propWidget));
    posLayout->addWidget(viewPosXSpin);
    posLayout->addWidget(new QLabel("Y:", propWidget));
    posLayout->addWidget(viewPosYSpin);
    basicForm->addRow("Position:", posLayout);

    auto* optionsForm = makeGroupForm("Display Options", propWidget, propLayout);

    auto* showHiddenChk  = new QCheckBox("Hidden Lines",   propWidget);
    auto* showCenterChk  = new QCheckBox("Center Lines",   propWidget);
    auto* showDimChk     = new QCheckBox("Dimensions",     propWidget);
    auto* showAnnotChk   = new QCheckBox("Annotations",    propWidget);
    auto* showScaleLblChk= new QCheckBox("Scale Label",    propWidget);
    auto* showViewLblChk = new QCheckBox("View Label",     propWidget);
    showHiddenChk->setObjectName("showHidden");
    showCenterChk->setObjectName("showCenter");
    showDimChk->setObjectName("showDim");
    showAnnotChk->setObjectName("showAnnot");
    showScaleLblChk->setObjectName("showScaleLbl");
    showViewLblChk->setObjectName("showViewLbl");

    for (auto* chk : {showHiddenChk, showCenterChk, showDimChk,
                      showAnnotChk, showScaleLblChk, showViewLblChk})
        optionsForm->addRow(chk);

    // ── 版面 / 來源 / 剖面 / 局部放大 參數 ───────────────────────────────
    struct ViewExtras {
        QCheckBox* autoFit; QDoubleSpinBox *boxW, *boxH; QComboBox* source;
        QGroupBox* secGroup; QComboBox* secDir; QDoubleSpinBox* secOffset;
        QLineEdit* secLabel; QCheckBox *secCut, *secArrows;
        QGroupBox* detGroup; QComboBox* detSrc; QDoubleSpinBox *detX, *detY, *detR;
        QLineEdit* detLabel;
    };
    auto ex = std::make_shared<ViewExtras>();

    {
        auto* g = new QGroupBox("Layout && Source", propWidget);
        auto* f = new QFormLayout(g);
        ex->autoFit = new QCheckBox("Auto-fit scale to box", g);
        ex->autoFit->setToolTip("內容自動縮放到配置框的 90%（標準比例 1/2/5×10ⁿ）");
        f->addRow(ex->autoFit);
        ex->boxW = new QDoubleSpinBox(g); ex->boxH = new QDoubleSpinBox(g);
        for (auto* s : {ex->boxW, ex->boxH}) { s->setRange(20, 2000); s->setDecimals(1); s->setSuffix(" mm"); }
        auto* bl = new QHBoxLayout();
        bl->addWidget(new QLabel("W:", g)); bl->addWidget(ex->boxW);
        bl->addWidget(new QLabel("H:", g)); bl->addWidget(ex->boxH);
        f->addRow("Box size:", bl);
        ex->source = new QComboBox(g);
        f->addRow("Source feature:", ex->source);
        propLayout->addWidget(g);
    }
    {
        ex->secGroup = new QGroupBox("Section Parameters", propWidget);
        auto* f = new QFormLayout(ex->secGroup);
        ex->secDir = new QComboBox(ex->secGroup);
        ex->secDir->addItems({"Horizontal (cut plane Y = offset)", "Vertical (cut plane X = offset)",
                              "Diagonal (not drawn on host)"});
        f->addRow("Direction:", ex->secDir);
        ex->secOffset = new QDoubleSpinBox(ex->secGroup);
        ex->secOffset->setRange(-1e9, 1e9); ex->secOffset->setDecimals(3);
        ex->secOffset->setToolTip("切割平面沿法向的位置（模型單位）。從法向正側往負側看。");
        f->addRow("Offset:", ex->secOffset);
        ex->secLabel = new QLineEdit(ex->secGroup);
        ex->secLabel->setPlaceholderText("A");
        f->addRow("Label:", ex->secLabel);
        ex->secCut    = new QCheckBox("Show cut line on host view", ex->secGroup);
        ex->secArrows = new QCheckBox("Show arrows", ex->secGroup);
        f->addRow(ex->secCut); f->addRow(ex->secArrows);
        propLayout->addWidget(ex->secGroup);
    }
    {
        ex->detGroup = new QGroupBox("Detail Parameters", propWidget);
        auto* f = new QFormLayout(ex->detGroup);
        ex->detSrc = new QComboBox(ex->detGroup);
        ex->detSrc->addItems({"Front", "Top", "Right", "Left", "Bottom", "Back", "Isometric"});
        f->addRow("Magnify view of:", ex->detSrc);
        ex->detX = new QDoubleSpinBox(ex->detGroup); ex->detY = new QDoubleSpinBox(ex->detGroup);
        ex->detR = new QDoubleSpinBox(ex->detGroup);
        for (auto* s : {ex->detX, ex->detY}) { s->setRange(-1e9, 1e9); s->setDecimals(3); }
        ex->detR->setRange(0.001, 1e9); ex->detR->setDecimals(3);
        auto* cl = new QHBoxLayout();
        cl->addWidget(new QLabel("X:", ex->detGroup)); cl->addWidget(ex->detX);
        cl->addWidget(new QLabel("Y:", ex->detGroup)); cl->addWidget(ex->detY);
        f->addRow("Center (model):", cl);
        f->addRow("Radius (model):", ex->detR);
        ex->detLabel = new QLineEdit(ex->detGroup);
        ex->detLabel->setPlaceholderText("B");
        f->addRow("Label:", ex->detLabel);
        propLayout->addWidget(ex->detGroup);
    }
    ex->secGroup->setVisible(false);
    ex->detGroup->setVisible(false);

    // 目前選取的視圖
    auto currentView = [this]() -> DrawingView* {
        const int row = m_viewList ? m_viewList->currentRow() : -1;
        if (!m_sheet || row < 0 || row >= m_sheet->views().size()) return nullptr;
        return m_sheet->views().at(row);
    };

    // 視圖 → 控制項
    auto refreshExtras = [this, ex](DrawingView* v) {
        const QSignalBlocker b1(ex->autoFit), b2(ex->boxW), b3(ex->boxH), b4(ex->source),
            b5(ex->secDir), b6(ex->secOffset), b7(ex->secLabel), b8(ex->secCut), b9(ex->secArrows),
            b10(ex->detSrc), b11(ex->detX), b12(ex->detY), b13(ex->detR), b14(ex->detLabel);

        ex->autoFit->setChecked(v->autoFit());
        ex->boxW->setValue(v->size().width());
        ex->boxH->setValue(v->size().height());

        ex->source->clear();
        cad::Document* doc = m_sheet ? m_sheet->sourceDocument() : nullptr;
        ex->source->setEnabled(doc != nullptr);
        int cur = -1;
        if (doc) {
            for (cad::Feature* f : doc->features()) {
                if (!f || f->shape().IsNull()) continue;
                ex->source->addItem(QString("%1 (%2)").arg(f->name(), f->typeString()), f->id());
                if (f == v->sourceFeature()) cur = ex->source->count() - 1;
            }
        }
        if (v->sourceFeature() && cur < 0) {   // 來源特徵目前沒有形體：仍列出，避免被誤清
            ex->source->addItem(v->sourceFeature()->name(), v->sourceFeature()->id());
            cur = ex->source->count() - 1;
        }
        ex->source->setCurrentIndex(cur);

        const bool isSec = (v->type() == ViewType::Section);
        const bool isDet = (v->type() == ViewType::Detail);
        ex->secGroup->setVisible(isSec);
        ex->detGroup->setVisible(isDet);
        if (isSec) {
            const SectionParams p = v->sectionParams();
            ex->secDir->setCurrentIndex((int)p.direction);
            ex->secOffset->setValue(p.offset);
            ex->secLabel->setText(p.sectionLabel);
            ex->secCut->setChecked(p.showCutLine);
            ex->secArrows->setChecked(p.showArrows);
        }
        if (isDet) {
            const DetailParams p = v->detailParams();
            static const ViewType order[] = {ViewType::Front, ViewType::Top, ViewType::Right,
                ViewType::Left, ViewType::Bottom, ViewType::Back, ViewType::Isometric};
            int idx = 0;
            for (int i = 0; i < 7; ++i) if (order[i] == p.sourceType) idx = i;
            ex->detSrc->setCurrentIndex(idx);
            ex->detX->setValue(p.center.x());
            ex->detY->setValue(p.center.y());
            ex->detR->setValue(p.radius);
            ex->detLabel->setText(p.label);
        }
    };

    // 控制項 → 視圖（只在有差異時才動，避免無謂的重新渲染）
    auto applyExtras = [this, ex, currentView]() {
        DrawingView* v = currentView();
        if (!v) return;
        bool refit = false;

        if (v->autoFit() != ex->autoFit->isChecked())
            v->setAutoFit(ex->autoFit->isChecked());   // 開啟時自帶 rebuildRequested

        const QSizeF box(ex->boxW->value(), ex->boxH->value());
        if (box != v->size()) { v->setSize(box); refit = true; }

        cad::Document* doc = m_sheet ? m_sheet->sourceDocument() : nullptr;
        const QString fid = ex->source->currentData().toString();
        if (doc && !fid.isEmpty()) {
            cad::Feature* f = doc->findFeature(fid);
            if (f && f != v->sourceFeature()) v->setSourceFeature(f);   // 自帶 rebuildRequested
        }

        if (v->type() == ViewType::Section) {
            SectionParams p = v->sectionParams();
            SectionParams n = p;
            n.direction   = (SectionDirection)ex->secDir->currentIndex();
            n.offset      = ex->secOffset->value();
            n.sectionLabel= ex->secLabel->text();
            n.showCutLine = ex->secCut->isChecked();
            n.showArrows  = ex->secArrows->isChecked();
            if (n.direction != p.direction || n.offset != p.offset ||
                n.sectionLabel != p.sectionLabel || n.showCutLine != p.showCutLine ||
                n.showArrows != p.showArrows)
                v->setSectionParams(n);
        }
        if (v->type() == ViewType::Detail) {
            static const ViewType order[] = {ViewType::Front, ViewType::Top, ViewType::Right,
                ViewType::Left, ViewType::Bottom, ViewType::Back, ViewType::Isometric};
            DetailParams p = v->detailParams();
            DetailParams n = p;
            n.sourceType = order[qBound(0, ex->detSrc->currentIndex(), 6)];
            n.center     = QPointF(ex->detX->value(), ex->detY->value());
            n.radius     = ex->detR->value();
            n.label      = ex->detLabel->text();
            if (n.sourceType != p.sourceType || n.center != p.center ||
                n.radius != p.radius || n.label != p.label)
                v->setDetailParams(n);
        }

        if (refit && m_preview) m_preview->triggerRender(v);
        if (m_preview) m_preview->update();
    };

    connect(ex->autoFit,   &QCheckBox::toggled, this, applyExtras);
    connect(ex->boxW,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyExtras);
    connect(ex->boxH,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyExtras);
    connect(ex->source,    QOverload<int>::of(&QComboBox::activated), this, applyExtras);
    connect(ex->secDir,    QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyExtras);
    connect(ex->secOffset, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyExtras);
    connect(ex->secLabel,  &QLineEdit::editingFinished, this, applyExtras);
    connect(ex->secCut,    &QCheckBox::toggled, this, applyExtras);
    connect(ex->secArrows, &QCheckBox::toggled, this, applyExtras);
    connect(ex->detSrc,    QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyExtras);
    connect(ex->detX,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyExtras);
    connect(ex->detY,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyExtras);
    connect(ex->detR,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyExtras);
    connect(ex->detLabel,  &QLineEdit::editingFinished, this, applyExtras);

    propLayout->addStretch();
    rightScroll->setWidget(propWidget);
    layout->addWidget(rightScroll, 1);

    // ── 清單選取 → 填入屬性 ──────────────────────────────────────────────
    connect(m_viewList, &QListWidget::currentRowChanged,
            [this, propWidget, viewLabelEdit, viewScaleSpin,
             viewPosXSpin, viewPosYSpin,
             showHiddenChk, showCenterChk, showDimChk,
             showAnnotChk, showScaleLblChk, showViewLblChk, refreshExtras](int row) {
                if (!m_sheet || row < 0 || row >= m_sheet->views().size()) return;
                DrawingView* v = m_sheet->views().at(row);

                // 暫時斷開信號避免循環
                QSignalBlocker bL(viewLabelEdit), bSc(viewScaleSpin),
                    bX(viewPosXSpin),  bY(viewPosYSpin);

                viewLabelEdit->setText(v->label());
                viewScaleSpin->setValue(v->scale());
                viewPosXSpin->setValue(v->position().x());
                viewPosYSpin->setValue(v->position().y());
                showHiddenChk->setChecked(v->showHiddenLines());
                showCenterChk->setChecked(v->showCenterLines());
                showDimChk->setChecked(v->showDimensions());
                showAnnotChk->setChecked(v->showAnnotations());
                showScaleLblChk->setChecked(v->showScaleLabel());
                showViewLblChk->setChecked(v->showViewLabel());
                refreshExtras(v);
            });

    // 屬性變更 → 更新視圖
    auto updateSelectedView = [this, viewLabelEdit, viewScaleSpin,
                               viewPosXSpin, viewPosYSpin,
                               showHiddenChk, showCenterChk, showDimChk,
                               showAnnotChk, showScaleLblChk, showViewLblChk]() {
        int row = m_viewList->currentRow();
        if (!m_sheet || row < 0 || row >= m_sheet->views().size()) return;
        DrawingView* v = m_sheet->views().at(row);
        v->setLabel(viewLabelEdit->text());
        v->setPosition(QPointF(viewPosXSpin->value(), viewPosYSpin->value()));
        v->setShowHiddenLines(showHiddenChk->isChecked());
        v->setShowCenterLines(showCenterChk->isChecked());
        v->setShowDimensions(showDimChk->isChecked());
        v->setShowAnnotations(showAnnotChk->isChecked());
        v->setShowScaleLabel(showScaleLblChk->isChecked());
        v->setShowViewLabel(showViewLblChk->isChecked());
        if (m_preview) m_preview->update();
    };

    connect(viewLabelEdit, &QLineEdit::textChanged,     updateSelectedView);
    // 比例欄位：只有使用者實際編輯它時才視為手動指定（否則自動 Fit 算出的新比例
    // 會被舊的欄位值蓋回去）→ 關閉該視圖的自動 Fit
    connect(viewScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, viewScaleSpin](double val) {
                const int row = m_viewList->currentRow();
                if (!m_sheet || row < 0 || row >= m_sheet->views().size()) return;
                DrawingView* v = m_sheet->views().at(row);
                if (qAbs(v->scale() - val) <= 1e-9) return;
                v->setAutoFit(false);
                v->setScale(val);
            });
    // 渲染完成後（自動 Fit 可能改了比例）→ 同步目前選取視圖的比例欄位
    if (m_preview) {
        connect(m_preview, &DrawingSheetWidget::renderingComplete, this,
                [this, viewScaleSpin]() {
                    const int row = m_viewList->currentRow();
                    if (!m_sheet || row < 0 || row >= m_sheet->views().size()) return;
                    const QSignalBlocker b(viewScaleSpin);
                    viewScaleSpin->setValue(m_sheet->views().at(row)->scale());
                });
    }
    connect(viewPosXSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            updateSelectedView);
    connect(viewPosYSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            updateSelectedView);
    connect(showHiddenChk, &QCheckBox::toggled, updateSelectedView);
    connect(showCenterChk, &QCheckBox::toggled, updateSelectedView);
    connect(showDimChk,     &QCheckBox::toggled, updateSelectedView);
    connect(showAnnotChk,   &QCheckBox::toggled, updateSelectedView);
    connect(showScaleLblChk,&QCheckBox::toggled, updateSelectedView);
    connect(showViewLblChk, &QCheckBox::toggled, updateSelectedView);

    // ── 新增視圖 ──────────────────────────────────────────────────────────
    connect(tb.addAct, &QAction::triggered, [this, addViewCombo]() {
        if (!m_sheet) return;
        static const QMap<int, ViewType> typeMap = {
            {0, ViewType::Front},  {1, ViewType::Top},    {2, ViewType::Right},
            {3, ViewType::Left},   {4, ViewType::Bottom}, {5, ViewType::Back},
            {6, ViewType::Isometric}, {7, ViewType::Section}, {8, ViewType::Detail}
        };
        ViewType type = typeMap.value(addViewCombo->currentIndex(), ViewType::Front);
        QSizeF sz = m_sheet->config().paperSizeMM();
        m_sheet->addView(type, QPointF(sz.width() / 2 - 40, sz.height() / 2 - 30));

        // 更新清單
        m_viewList->clear();
        for (DrawingView* v : m_sheet->views())
            m_viewList->addItem(QString("[%1] %2").arg(
                                                      (int)v->type()).arg(v->label()));
        m_viewList->setCurrentRow(m_viewList->count() - 1);
    });

    // ── 刪除視圖 ──────────────────────────────────────────────────────────
    connect(tb.removeAct, &QAction::triggered, [this]() {
        int row = m_viewList->currentRow();
        if (!m_sheet || row < 0 || row >= m_sheet->views().size()) return;
        DrawingView* v = m_sheet->views().at(row);
        m_sheet->removeView(v->id());
        m_viewList->takeItem(row);
    });

    m_addViewBtn    = nullptr;  // 已用 toolbar action 取代
    m_removeViewBtn = nullptr;
    m_viewPropsBtn  = nullptr;

    m_tabs->addTab(tab, "📐 Views");
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab 4 — 零件表
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetDialog::setupPartsListTab()
{
    auto* tab    = new QWidget();
    auto* layout = new QVBoxLayout(tab);

    // 啟用開關 + 自動同步選項
    auto* hdr = new QHBoxLayout();
    m_partsListCheck = new QCheckBox("Enable Parts List (BOM)", tab);
    auto* autoChk    = new QCheckBox("Auto-populate from Document", tab);
    autoChk->setObjectName("partsListAuto");
    autoChk->setChecked(true);
    hdr->addWidget(m_partsListCheck);
    hdr->addWidget(autoChk);
    auto* populateBtn = new QPushButton("Populate from Document", tab);
    populateBtn->setToolTip("重新掃描文件中含實體的特徵產生零件表（保留同料號的材質／備註）");
    hdr->addWidget(populateBtn);
    hdr->addStretch();
    layout->addLayout(hdr);

    layout->addWidget(makeSeparator(tab));

    // 工具列
    auto tb = makeTableToolbar(tab);
    layout->addWidget(tb.bar);

    // 表格
    m_partsListTable = new QTableWidget(0, 8, tab);
    m_partsListTable->setHorizontalHeaderLabels({
        "No.", "Part Number", "Description", "Material",
        "Qty", "Unit", "Rev", "Remark"
    });
    m_partsListTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_partsListTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_partsListTable->setAlternatingRowColors(true);
    layout->addWidget(m_partsListTable, 1);

    // 由文件自動產生 BOM → 填入表格
    connect(populateBtn, &QPushButton::clicked, this, [this]() {
        if (!m_sheet || !m_sheet->sourceDocument() || !m_sheet->partsList()) return;
        // 先把表格上目前的手動內容寫回，populate 才能依料號保留材質／備註
        applyToSheet();
        m_sheet->partsList()->populateFromDocument(m_sheet->sourceDocument());
        const auto& rows = m_sheet->partsList()->rows();
        m_partsListTable->setRowCount(0);
        for (const PartsListRow& r : rows) {
            const int row = m_partsListTable->rowCount();
            m_partsListTable->insertRow(row);
            const QStringList cells = { QString::number(r.itemNo), r.partNumber, r.description,
                                        r.material, QString::number(r.quantity), r.unit,
                                        r.revision, r.remark };
            for (int c = 0; c < cells.size(); ++c)
                m_partsListTable->setItem(row, c, new QTableWidgetItem(cells[c]));
        }
        if (!m_partsListCheck->isChecked()) m_partsListCheck->setChecked(true);
        if (m_preview) m_preview->update();
    });

    // 啟用/停用表格
    connect(m_partsListCheck, &QCheckBox::toggled,
            m_partsListTable, &QWidget::setEnabled);
    connect(m_partsListCheck, &QCheckBox::toggled, tb.bar, &QWidget::setEnabled);

    // 新增行
    connect(tb.addAct, &QAction::triggered, [this]() {
        int row = m_partsListTable->rowCount();
        m_partsListTable->insertRow(row);
        m_partsListTable->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        m_partsListTable->setItem(row, 4, new QTableWidgetItem("1"));
        m_partsListTable->setItem(row, 5, new QTableWidgetItem("EA"));
        m_partsListTable->scrollToBottom();
        m_partsListTable->editItem(m_partsListTable->item(row, 1));
    });

    // 刪除行
    connect(tb.removeAct, &QAction::triggered, [this]() {
        auto sel = m_partsListTable->selectedItems();
        if (sel.isEmpty()) return;
        QSet<int> rows;
        for (auto* item : sel) rows.insert(item->row());
        QList<int> sortedRows(rows.begin(), rows.end());
        std::sort(sortedRows.rbegin(), sortedRows.rend());
        for (int r : sortedRows)
            m_partsListTable->removeRow(r);
    });

    // 移動行
    connect(tb.moveUpAct, &QAction::triggered, [this]() {
        int row = m_partsListTable->currentRow();
        if (row <= 0) return;
        for (int col = 0; col < m_partsListTable->columnCount(); ++col) {
            QTableWidgetItem* a = m_partsListTable->takeItem(row, col);
            QTableWidgetItem* b = m_partsListTable->takeItem(row - 1, col);
            m_partsListTable->setItem(row, col, b);
            m_partsListTable->setItem(row - 1, col, a);
        }
        m_partsListTable->setCurrentCell(row - 1, m_partsListTable->currentColumn());
    });
    connect(tb.moveDownAct, &QAction::triggered, [this]() {
        int row = m_partsListTable->currentRow();
        if (row >= m_partsListTable->rowCount() - 1) return;
        for (int col = 0; col < m_partsListTable->columnCount(); ++col) {
            QTableWidgetItem* a = m_partsListTable->takeItem(row, col);
            QTableWidgetItem* b = m_partsListTable->takeItem(row + 1, col);
            m_partsListTable->setItem(row, col, b);
            m_partsListTable->setItem(row + 1, col, a);
        }
        m_partsListTable->setCurrentCell(row + 1, m_partsListTable->currentColumn());
    });

    m_tabs->addTab(tab, "📋 Parts List");
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab 5 — 進版說明
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetDialog::setupRevisionTab()
{
    auto* tab    = new QWidget();
    auto* layout = new QVBoxLayout(tab);

    auto* hdr = new QHBoxLayout();
    m_revisionCheck = new QCheckBox("Enable Revision Table", tab);
    hdr->addWidget(m_revisionCheck);
    hdr->addStretch();
    layout->addLayout(hdr);
    layout->addWidget(makeSeparator(tab));

    auto tb = makeTableToolbar(tab);
    layout->addWidget(tb.bar);

    m_revisionTable = new QTableWidget(0, 6, tab);
    m_revisionTable->setHorizontalHeaderLabels({
        "Rev", "Date", "Description", "Changed By", "Approved By", "Zone"
    });
    m_revisionTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_revisionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_revisionTable->setAlternatingRowColors(true);
    layout->addWidget(m_revisionTable, 1);

    connect(m_revisionCheck, &QCheckBox::toggled,
            m_revisionTable, &QWidget::setEnabled);
    connect(m_revisionCheck, &QCheckBox::toggled, tb.bar, &QWidget::setEnabled);

    // 新增進版紀錄
    connect(tb.addAct, &QAction::triggered, [this]() {
        int row = m_revisionTable->rowCount();
        m_revisionTable->insertRow(row);

        // 自動填入下一個版次 (A, B, C … AA, AB …)
        QString nextRev;
        if (row == 0) {
            nextRev = "A";
        } else {
            QTableWidgetItem* prevItem = m_revisionTable->item(row - 1, 0);
            if (prevItem && !prevItem->text().isEmpty()) {
                QString prev = prevItem->text();
                // 簡單遞增：A→B…Z→AA
                int val = 0;
                for (QChar c : prev)
                    val = val * 26 + (c.toUpper().toLatin1() - 'A' + 1);
                ++val;
                nextRev.clear();
                while (val > 0) {
                    nextRev.prepend(QChar('A' + (val - 1) % 26));
                    val = (val - 1) / 26;
                }
            } else {
                nextRev = QString::number(row + 1).rightJustified(2, '0');
            }
        }

        m_revisionTable->setItem(row, 0, new QTableWidgetItem(nextRev));
        m_revisionTable->setItem(row, 1, new QTableWidgetItem(
                                             QDate::currentDate().toString("yyyy-MM-dd")));
        m_revisionTable->scrollToBottom();
        m_revisionTable->editItem(m_revisionTable->item(row, 2));
    });

    // 刪除
    connect(tb.removeAct, &QAction::triggered, [this]() {
        int row = m_revisionTable->currentRow();
        if (row >= 0) m_revisionTable->removeRow(row);
    });

    m_addRevBtn    = nullptr;
    m_removeRevBtn = nullptr;

    m_tabs->addTab(tab, "🔄 Revisions");
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab 6 — 圖目錄
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetDialog::setupDrawingIndexTab()
{
    auto* tab    = new QWidget();
    auto* layout = new QVBoxLayout(tab);

    auto* hdr = new QHBoxLayout();
    auto* enableChk = new QCheckBox("Enable Drawing Index", tab);
    enableChk->setObjectName("drawingIndexCheck");
    auto* titleEdit = new QLineEdit("DRAWING INDEX", tab);
    titleEdit->setObjectName("drawingIndexTitle");
    hdr->addWidget(enableChk);
    hdr->addWidget(new QLabel("Title:", tab));
    hdr->addWidget(titleEdit);
    hdr->addStretch();
    layout->addLayout(hdr);
    layout->addWidget(makeSeparator(tab));

    auto tb = makeTableToolbar(tab);
    layout->addWidget(tb.bar);

    auto* indexTable = new QTableWidget(0, 6, tab);
    indexTable->setObjectName("drawingIndexTable");
    indexTable->setHorizontalHeaderLabels({
        "Sheet", "DWG No.", "Title", "Rev", "Date", "Remark"
    });
    indexTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    indexTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    indexTable->setAlternatingRowColors(true);
    layout->addWidget(indexTable, 1);

    connect(enableChk, &QCheckBox::toggled, indexTable,  &QWidget::setEnabled);
    connect(enableChk, &QCheckBox::toggled, tb.bar,      &QWidget::setEnabled);

    connect(tb.addAct, &QAction::triggered, [indexTable]() {
        int row = indexTable->rowCount();
        indexTable->insertRow(row);
        indexTable->setItem(row, 0, new QTableWidgetItem(
                                        QString::number(row + 1).rightJustified(2, '0')));
        indexTable->setItem(row, 4, new QTableWidgetItem(
                                        QDate::currentDate().toString("yyyy-MM-dd")));
        indexTable->scrollToBottom();
        indexTable->editItem(indexTable->item(row, 1));
    });

    connect(tb.removeAct, &QAction::triggered, [indexTable]() {
        int row = indexTable->currentRow();
        if (row >= 0) indexTable->removeRow(row);
    });

    m_tabs->addTab(tab, "📑 Index");
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab 7 — 圖例 & 說明
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetDialog::setupLegendTab()
{
    auto* tab    = new QWidget();
    auto* layout = new QHBoxLayout(tab);

    // ── 左：圖例 ──────────────────────────────────────────────────────────
    auto* legendGroup  = new QGroupBox("Legend", tab);
    auto* legendLayout = new QVBoxLayout(legendGroup);

    auto* legEnableChk = new QCheckBox("Enable Legend", legendGroup);
    legendLayout->addWidget(legEnableChk);

    auto legTb = makeTableToolbar(legendGroup);
    legendLayout->addWidget(legTb.bar);

    auto* legendTable = new QTableWidget(0, 2, legendGroup);
    legendTable->setObjectName("legendTable");
    legendTable->setHorizontalHeaderLabels({"Symbol", "Description"});
    legendTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    legendTable->setColumnWidth(0, 60);
    legendTable->setAlternatingRowColors(true);
    legendLayout->addWidget(legendTable, 1);

    connect(legEnableChk, &QCheckBox::toggled, legendTable, &QWidget::setEnabled);

    connect(legTb.addAct, &QAction::triggered, [legendTable]() {
        int row = legendTable->rowCount();
        legendTable->insertRow(row);
        legendTable->editItem(legendTable->item(row, 0));
    });
    connect(legTb.removeAct, &QAction::triggered, [legendTable]() {
        int row = legendTable->currentRow();
        if (row >= 0) legendTable->removeRow(row);
    });

    layout->addWidget(legendGroup, 1);

    // ── 右：說明文字區塊 ──────────────────────────────────────────────────
    auto* notesGroup  = new QGroupBox("Notes / Specifications", tab);
    auto* notesLayout = new QVBoxLayout(notesGroup);

    // 現有說明清單
    auto* notesList = new QListWidget(notesGroup);
    notesList->setObjectName("notesList");
    notesLayout->addWidget(notesList, 0);

    // 新增 / 刪除按鈕
    auto* noteBtnLayout = new QHBoxLayout();
    m_addRevBtn    = new QPushButton("＋ Add Note Block", notesGroup);
    m_removeRevBtn = new QPushButton("－ Remove",        notesGroup);
    noteBtnLayout->addWidget(m_addRevBtn);
    noteBtnLayout->addWidget(m_removeRevBtn);
    noteBtnLayout->addStretch();
    notesLayout->addLayout(noteBtnLayout);

    // 內容編輯區
    auto* noteEditFrame = new QGroupBox("Content", notesGroup);
    auto* noteEditLayout = new QFormLayout(noteEditFrame);

    auto* noteTitleEdit   = new QLineEdit(notesGroup);
    auto* noteContentEdit = new QTextEdit(notesGroup);
    noteContentEdit->setMaximumHeight(120);
    noteTitleEdit->setObjectName("noteTitleEdit");
    noteContentEdit->setObjectName("noteContentEdit");

    noteEditLayout->addRow("Title:",   noteTitleEdit);
    noteEditLayout->addRow("Content:", noteContentEdit);
    notesLayout->addWidget(noteEditFrame, 1);

    // 清單選取 → 填入編輯區
    connect(notesList, &QListWidget::currentRowChanged,
            [this, notesList, noteTitleEdit, noteContentEdit](int row) {
                if (!m_sheet || row < 0 || row >= m_sheet->noteBlocks().size()) return;
                const NoteBlock& nb = m_sheet->noteBlocks().at(row);
                noteTitleEdit->setText(nb.title);
                noteContentEdit->setPlainText(nb.content);
            });

    // 新增說明區塊
    connect(m_addRevBtn, &QPushButton::clicked, [this, notesList]() {
        if (!m_sheet) return;
        NoteBlock* nb = m_sheet->addNoteBlock("Notes");
        notesList->addItem(nb->title);
        notesList->setCurrentRow(notesList->count() - 1);
    });

    // 刪除說明區塊
    connect(m_removeRevBtn, &QPushButton::clicked,
            [this, notesList]() {
                int row = notesList->currentRow();
                if (!m_sheet || row < 0 || row >= m_sheet->noteBlocks().size()) return;
                QUuid id = m_sheet->noteBlocks().at(row).id;
                m_sheet->removeNoteBlock(id);
                notesList->takeItem(row);
            });

    layout->addWidget(notesGroup, 1);

    m_tabs->addTab(tab, "📝 Legend & Notes");
}

// ─────────────────────────────────────────────────────────────────────────────
// 資料同步
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetDialog::loadFromSheet()
{
    if (!m_sheet) return;

    // 載入時逐一 setCurrentIndex／setValue 會觸發 onSheetConfigEdited：它會讀「全部」控制項
    // （尚未載入者還是預設值：Portrait、None…）寫回圖紙，把 Orientation／FrameTemplate 等蓋掉。
    struct LoadGuard {
        bool& f;
        explicit LoadGuard(bool& flag) : f(flag) { f = true; }
        ~LoadGuard() { f = false; }
    } loadGuard(m_loading);

    // ── Tab 1：圖紙設定 ───────────────────────────────────────────────────
    const SheetConfig& cfg = m_sheet->config();
    m_paperSizeCombo->setCurrentIndex((int)cfg.paperSize);
    m_orientationCombo->setCurrentIndex((int)cfg.orientation);
    m_unitCombo->setCurrentIndex((int)cfg.unit);
    m_frameTemplateCombo->setCurrentIndex((int)cfg.frameTemplate);
    m_projectionCombo->setCurrentIndex((int)cfg.projectionMethod);
    m_defaultScaleSpin->setValue(cfg.defaultScale);

    // 線寬
    if (auto* s = findChild<QDoubleSpinBox*>("lwThin"))   s->setValue(cfg.lineWidthThin);
    if (auto* s = findChild<QDoubleSpinBox*>("lwMedium")) s->setValue(cfg.lineWidthMedium);
    if (auto* s = findChild<QDoubleSpinBox*>("lwThick"))  s->setValue(cfg.lineWidthThick);

    // ── Tab 2：標題欄 ─────────────────────────────────────────────────────
    const TitleBlock& tb = m_sheet->titleBlock();
    m_drawingNumberEdit->setText(tb.drawingNumber);
    m_drawingTitleEdit->setText(tb.drawingTitle);
    m_sheetNumberEdit->setText(tb.sheetNumber);
    m_revisionEdit->setText(tb.revision);
    m_companyNameEdit->setText(tb.companyName);
    m_projectNameEdit->setText(tb.projectName);
    m_partNumberEdit->setText(tb.partNumber);
    m_designedByEdit->setText(tb.designedBy);
    m_checkedByEdit->setText(tb.checkedBy);
    m_approvedByEdit->setText(tb.approvedBy);
    m_materialEdit->setText(tb.material);
    m_toleranceEdit->setText(tb.tolerance);

    if (auto* e = findChild<QLineEdit*>("companyLogo"))      e->setText(tb.companyLogo);
    if (auto* e = findChild<QLineEdit*>("designDate"))       e->setText(tb.designDate);
    if (auto* e = findChild<QLineEdit*>("approvalDate"))     e->setText(tb.approvalDate);
    if (auto* e = findChild<QLineEdit*>("angularTolerance")) e->setText(tb.angularTolerance);
    if (auto* e = findChild<QLineEdit*>("surfaceFinish"))    e->setText(tb.surfaceFinish);

    // ── Tab 3：視圖清單 ───────────────────────────────────────────────────
    m_viewList->clear();
    for (DrawingView* v : m_sheet->views())
        m_viewList->addItem(QString("[%1] %2")
                                .arg(v->type() == ViewType::Front    ? "FR" :
                                         v->type() == ViewType::Top      ? "TP" :
                                         v->type() == ViewType::Right    ? "RT" :
                                         v->type() == ViewType::Isometric? "ISO": "??")
                                .arg(v->label()));

    // ── Tab 4：零件表 ─────────────────────────────────────────────────────
    m_partsListCheck->setChecked(m_sheet->isPartsListEnabled());
    m_partsListTable->setRowCount(0);
    if (m_sheet->partsList()) {
        for (const PartsListRow& row : m_sheet->partsList()->rows()) {
            int r = m_partsListTable->rowCount();
            m_partsListTable->insertRow(r);
            m_partsListTable->setItem(r, 0, new QTableWidgetItem(QString::number(row.itemNo)));
            m_partsListTable->setItem(r, 1, new QTableWidgetItem(row.partNumber));
            m_partsListTable->setItem(r, 2, new QTableWidgetItem(row.description));
            m_partsListTable->setItem(r, 3, new QTableWidgetItem(row.material));
            m_partsListTable->setItem(r, 4, new QTableWidgetItem(QString::number(row.quantity)));
            m_partsListTable->setItem(r, 5, new QTableWidgetItem(row.unit));
            m_partsListTable->setItem(r, 6, new QTableWidgetItem(row.revision));
            m_partsListTable->setItem(r, 7, new QTableWidgetItem(row.remark));
        }
    }

    // ── Tab 5：進版表 ─────────────────────────────────────────────────────
    m_revisionCheck->setChecked(m_sheet->isRevisionTableEnabled());
    m_revisionTable->setRowCount(0);
    if (m_sheet->revisionTable()) {
        for (const RevisionEntry& e : m_sheet->revisionTable()->entries()) {
            int r = m_revisionTable->rowCount();
            m_revisionTable->insertRow(r);
            m_revisionTable->setItem(r, 0, new QTableWidgetItem(e.revision));
            m_revisionTable->setItem(r, 1, new QTableWidgetItem(e.date));
            m_revisionTable->setItem(r, 2, new QTableWidgetItem(e.description));
            m_revisionTable->setItem(r, 3, new QTableWidgetItem(e.changedBy));
            m_revisionTable->setItem(r, 4, new QTableWidgetItem(e.approvedBy));
            m_revisionTable->setItem(r, 5, new QTableWidgetItem(e.zone));
        }
    }

    // ── Tab 6：圖目錄 ─────────────────────────────────────────────────────
    if (auto* chk = findChild<QCheckBox*>("drawingIndexCheck"))
        chk->setChecked(m_sheet->isDrawingIndexEnabled());
    if (auto* tbl = findChild<QTableWidget*>("drawingIndexTable")) {
        tbl->setRowCount(0);
        if (m_sheet->drawingIndex()) {
            for (const DrawingIndexEntry& e : m_sheet->drawingIndex()->entries()) {
                int r = tbl->rowCount();
                tbl->insertRow(r);
                tbl->setItem(r, 0, new QTableWidgetItem(e.sheetNumber));
                tbl->setItem(r, 1, new QTableWidgetItem(e.drawingNumber));
                tbl->setItem(r, 2, new QTableWidgetItem(e.title));
                tbl->setItem(r, 3, new QTableWidgetItem(e.revision));
                tbl->setItem(r, 4, new QTableWidgetItem(e.date));
                tbl->setItem(r, 5, new QTableWidgetItem(e.remark));
            }
        }
    }

    // ── Tab 7：圖例 ───────────────────────────────────────────────────────
    if (auto* tbl = findChild<QTableWidget*>("legendTable")) {
        tbl->setRowCount(0);
        if (m_sheet->legend()) {
            for (const LegendEntry& e : m_sheet->legend()->entries()) {
                int r = tbl->rowCount();
                tbl->insertRow(r);
                tbl->setItem(r, 0, new QTableWidgetItem(e.symbol));
                tbl->setItem(r, 1, new QTableWidgetItem(e.description));
            }
        }
    }
    if (auto* lst = findChild<QListWidget*>("notesList")) {
        lst->clear();
        for (const NoteBlock& nb : m_sheet->noteBlocks())
            lst->addItem(nb.title);
    }
}

void DrawingSheetDialog::applyToSheet()
{
    if (!m_sheet) return;

    // ── 圖紙設定 ──────────────────────────────────────────────────────────
    SheetConfig cfg;
    cfg.paperSize        = (PaperSize)       m_paperSizeCombo->currentIndex();
    cfg.orientation      = (PaperOrientation)m_orientationCombo->currentIndex();
    cfg.unit             = (DrawingUnit)     m_unitCombo->currentIndex();
    cfg.frameTemplate    = (FrameTemplate)   m_frameTemplateCombo->currentIndex();
    cfg.projectionMethod = (ProjectionMethod)m_projectionCombo->currentIndex();
    cfg.defaultScale     = m_defaultScaleSpin->value();
    if (auto* s = findChild<QDoubleSpinBox*>("lwThin"))   cfg.lineWidthThin   = s->value();
    if (auto* s = findChild<QDoubleSpinBox*>("lwMedium")) cfg.lineWidthMedium = s->value();
    if (auto* s = findChild<QDoubleSpinBox*>("lwThick"))  cfg.lineWidthThick  = s->value();
    m_sheet->applyConfig(cfg);
    saveSheetPrefs(cfg);

    // ── 標題欄 ────────────────────────────────────────────────────────────
    TitleBlock tb;
    tb.drawingNumber    = m_drawingNumberEdit->text();
    tb.drawingTitle     = m_drawingTitleEdit->text();
    tb.sheetNumber      = m_sheetNumberEdit->text();
    tb.revision         = m_revisionEdit->text();
    tb.companyName      = m_companyNameEdit->text();
    tb.projectName      = m_projectNameEdit->text();
    tb.partNumber       = m_partNumberEdit->text();
    tb.designedBy       = m_designedByEdit->text();
    tb.checkedBy        = m_checkedByEdit->text();
    tb.approvedBy       = m_approvedByEdit->text();
    tb.material         = m_materialEdit->text();
    tb.tolerance        = m_toleranceEdit->text();
    if (auto* e = findChild<QLineEdit*>("companyLogo"))      tb.companyLogo      = e->text();
    if (auto* e = findChild<QLineEdit*>("designDate"))       tb.designDate       = e->text();
    if (auto* e = findChild<QLineEdit*>("approvalDate"))     tb.approvalDate     = e->text();
    if (auto* e = findChild<QLineEdit*>("angularTolerance")) tb.angularTolerance = e->text();
    if (auto* e = findChild<QLineEdit*>("surfaceFinish"))    tb.surfaceFinish    = e->text();
    m_sheet->setTitleBlock(tb);

    // ── 零件表 ────────────────────────────────────────────────────────────
    m_sheet->enablePartsList(m_partsListCheck->isChecked());
    if (m_sheet->partsList()) {
        QVector<PartsListRow> rows;
        for (int r = 0; r < m_partsListTable->rowCount(); ++r) {
            PartsListRow row;
            auto cellText = [&](int col) -> QString {
                auto* item = m_partsListTable->item(r, col);
                return item ? item->text() : QString();
            };
            row.itemNo      = cellText(0).toInt();
            row.partNumber  = cellText(1);
            row.description = cellText(2);
            row.material    = cellText(3);
            row.quantity    = cellText(4).toInt();
            row.unit        = cellText(5);
            row.revision    = cellText(6);
            row.remark      = cellText(7);
            rows.append(row);
        }
        m_sheet->partsList()->setRows(rows);
    }

    // ── 進版表 ────────────────────────────────────────────────────────────
    m_sheet->enableRevisionTable(m_revisionCheck->isChecked());
    if (m_sheet->revisionTable()) {
        // 清空後重新加入
        while (!m_sheet->revisionTable()->entries().isEmpty())
            m_sheet->revisionTable()->removeEntry(0);

        for (int r = 0; r < m_revisionTable->rowCount(); ++r) {
            RevisionEntry e;
            auto cellText = [&](int col) -> QString {
                auto* item = m_revisionTable->item(r, col);
                return item ? item->text() : QString();
            };
            e.revision    = cellText(0);
            e.date        = cellText(1);
            e.description = cellText(2);
            e.changedBy   = cellText(3);
            e.approvedBy  = cellText(4);
            e.zone        = cellText(5);
            m_sheet->revisionTable()->addEntry(e);
        }
    }

    // ── 圖目錄 ────────────────────────────────────────────────────────────
    if (auto* chk = findChild<QCheckBox*>("drawingIndexCheck"))
        m_sheet->enableDrawingIndex(chk->isChecked());

    if (m_sheet->drawingIndex()) {
        if (auto* titleE = findChild<QLineEdit*>("drawingIndexTitle"))
            m_sheet->drawingIndex()->setTitle(titleE->text());

        if (auto* tbl = findChild<QTableWidget*>("drawingIndexTable")) {
            while (!m_sheet->drawingIndex()->entries().isEmpty())
                m_sheet->drawingIndex()->removeEntry(0);
            for (int r = 0; r < tbl->rowCount(); ++r) {
                DrawingIndexEntry e;
                auto c = [&](int col) -> QString {
                    auto* item = tbl->item(r, col);
                    return item ? item->text() : QString();
                };
                e.sheetNumber   = c(0);
                e.drawingNumber = c(1);
                e.title         = c(2);
                e.revision      = c(3);
                e.date          = c(4);
                e.remark        = c(5);
                m_sheet->drawingIndex()->addEntry(e);
            }
        }
    }

    // ── 圖例 ──────────────────────────────────────────────────────────────
    if (auto* tbl = findChild<QTableWidget*>("legendTable")) {
        if (m_sheet->legend()) {
            while (!m_sheet->legend()->entries().isEmpty())
                m_sheet->legend()->removeEntry(0);
            for (int r = 0; r < tbl->rowCount(); ++r) {
                LegendEntry e;
                auto* sym  = tbl->item(r, 0);
                auto* desc = tbl->item(r, 1);
                e.symbol      = sym  ? sym->text()  : QString();
                e.description = desc ? desc->text() : QString();
                m_sheet->legend()->addEntry(e);
            }
        }
    }

    if (m_preview) m_preview->update();
    qDebug() << "[DrawingSheetDialog] Applied to sheet";
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetDialog::onApply()
{
    applyToSheet();
}

void DrawingSheetDialog::onOk()
{
    applyToSheet();
    accept();
}

void DrawingSheetDialog::onCancel()
{
    reject();
}

/** 圖紙規格存入 QSettings（與平縱面圖冊設定對話框同步；新建圖紙也採用） */
void DrawingSheetDialog::saveSheetPrefs(const SheetConfig& cfg)
{
    DrawingPrefs p;
    p.paperSize     = static_cast<int>(cfg.paperSize);
    p.orientation   = static_cast<int>(cfg.orientation);
    p.unit          = static_cast<int>(cfg.unit);
    p.frameTemplate = static_cast<int>(cfg.frameTemplate);
    p.projection    = static_cast<int>(cfg.projectionMethod);
    p.defaultScale  = cfg.defaultScale;
    p.lwThin        = cfg.lineWidthThin;
    p.lwMedium      = cfg.lineWidthMedium;
    p.lwThick       = cfg.lineWidthThick;
    saveDrawingPrefs(p);
}

namespace {
/** 有多張圖紙時詢問：全部／僅目前。回傳 1＝全部、0＝僅目前、-1＝取消。 */
int askExportScope(QWidget* parent, int sheetCount)
{
    if (sheetCount <= 1) return 0;
    QMessageBox box(parent);
    box.setWindowTitle("Export");
    box.setText(QString("文件中共有 %1 張圖紙。要輸出哪些？").arg(sheetCount));
    QPushButton* all = box.addButton(QString("全部圖面（%1 張）").arg(sheetCount), QMessageBox::AcceptRole);
    QPushButton* cur = box.addButton("僅目前圖面", QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(all);
    box.exec();
    if (box.clickedButton() == all) return 1;
    if (box.clickedButton() == cur) return 0;
    return -1;
}
} // namespace

void DrawingSheetDialog::onExportPdf()
{
    applyToSheet();
    const int scope = askExportScope(this, m_mgr ? m_mgr->sheets().size() : 1);
    if (scope < 0) return;

    QString defName = scope == 1 ? QString("drawings.pdf")
                                 : m_sheet->fileName().replace(".aicad_dwg", ".pdf");
    QString path = QFileDialog::getSaveFileName(this, "Export PDF", defName, "PDF Files (*.pdf)");
    if (path.isEmpty()) return;

    const bool ok = scope == 1 ? DrawingSheet::exportPdfAll(m_mgr->sheets(), path)
                               : m_sheet->exportPdf(path);
    if (!ok) {
        QMessageBox::warning(this, "Export Failed",
                             "Failed to export PDF.\nSee the application log for details.");
    } else {
        QMessageBox::information(this, "Export Complete",
                                 scope == 1 ? QString("已輸出全部 %1 張圖面（多頁 PDF）：\n%2")
                                                  .arg(m_mgr->sheets().size()).arg(path)
                                            : "PDF exported to:\n" + path);
    }
}

void DrawingSheetDialog::onExportSvg()
{
    applyToSheet();
    const int scope = askExportScope(this, m_mgr ? m_mgr->sheets().size() : 1);
    if (scope < 0) return;

    QString defName = scope == 1 ? QString("drawings.svg")
                                 : m_sheet->fileName().replace(".aicad_dwg", ".svg");
    QString path = QFileDialog::getSaveFileName(this, "Export SVG", defName, "SVG Files (*.svg)");
    if (path.isEmpty()) return;

    bool ok = true;
    QString msg;
    if (scope == 1) {
        // SVG 一檔一頁：依圖紙順序輸出 <name>_01.svg、<name>_02.svg …
        const QFileInfo fi(path);
        const QString base = fi.absolutePath() + "/" + fi.completeBaseName();
        int n = 0;
        for (DrawingSheet* s : m_mgr->sheets()) {
            const QString p = QString("%1_%2.svg").arg(base).arg(++n, 2, 10, QLatin1Char('0'));
            if (!s->exportSvg(p)) { ok = false; break; }
        }
        msg = QString("已輸出 %1 個 SVG：\n%2_01.svg …").arg(n).arg(base);
    } else {
        ok = m_sheet->exportSvg(path);
        msg = "SVG exported to:\n" + path;
    }
    if (!ok) {
        QMessageBox::warning(this, "Export Failed",
                             "Failed to export SVG.\nSVG export needs the QtSvg module (Ubuntu: sudo apt install libqt5svg5-dev, then re-run qmake).");
    } else {
        QMessageBox::information(this, "Export Complete", msg);
    }
}

// 即時預覽：圖紙設定變更
void DrawingSheetDialog::onSheetConfigEdited()
{
    if (!m_sheet || m_loading) return;

    // 即時套用圖紙規格（紙張大小、方向），讓預覽立即反映
    SheetConfig cfg = m_sheet->config();
    const QSizeF oldPaper = cfg.paperSizeMM();
    cfg.paperSize        = (PaperSize)       m_paperSizeCombo->currentIndex();
    cfg.orientation      = (PaperOrientation)m_orientationCombo->currentIndex();
    cfg.frameTemplate    = (FrameTemplate)   m_frameTemplateCombo->currentIndex();
    cfg.projectionMethod = (ProjectionMethod)m_projectionCombo->currentIndex();
    cfg.defaultScale     = m_defaultScaleSpin->value();
    cfg.unit             = (DrawingUnit)m_unitCombo->currentIndex();
    if (auto* s = findChild<QDoubleSpinBox*>("lwThin"))   cfg.lineWidthThin   = s->value();
    if (auto* s = findChild<QDoubleSpinBox*>("lwMedium")) cfg.lineWidthMedium = s->value();
    if (auto* s = findChild<QDoubleSpinBox*>("lwThick"))  cfg.lineWidthThick  = s->value();
    m_sheet->applyConfig(cfg);
    saveSheetPrefs(cfg);

    if (m_preview) {
        if (m_sheet->config().paperSizeMM() != oldPaper)   // 只有紙張大小／方向變了才重新 Fit 視窗
            m_preview->fitToWindow();
        m_preview->update();
    }
}

// 即時預覽：標題欄欄位變更
void DrawingSheetDialog::onTitleBlockEdited()
{
    if (!m_sheet || !m_preview || m_loading) return;

    // 僅更新標題欄（不觸發重渲染）
    TitleBlock tb = m_sheet->titleBlock();
    tb.drawingTitle  = m_drawingTitleEdit->text();
    tb.drawingNumber = m_drawingNumberEdit->text();
    tb.revision      = m_revisionEdit->text();
    tb.companyName   = m_companyNameEdit->text();
    tb.designedBy    = m_designedByEdit->text();
    m_sheet->setTitleBlock(tb);

    m_preview->update();
}

} // namespace drawing
} // namespace aicad
