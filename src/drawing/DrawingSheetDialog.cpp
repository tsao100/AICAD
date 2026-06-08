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

#include "DrawingSheetWidget.h"   // DrawingSheetDialog 宣告在同一個 .h
#include "DrawingSheet.h"

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
{
    Q_ASSERT(sheet);
    setWindowTitle(QString("Drawing Sheet — %1")
                       .arg(sheet->titleBlock().drawingTitle.isEmpty()
                                ? "Untitled" : sheet->titleBlock().drawingTitle));
    setMinimumSize(1100, 700);
    resize(1200, 780);

    setupUi();
    loadFromSheet();

    qDebug() << "[DrawingSheetDialog] Created";
}

DrawingSheetDialog::~DrawingSheetDialog()
{
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
    auto* gridAct   = ctrlBar->addAction("Grid");
    gridAct->setCheckable(true);
    gridAct->setChecked(true);
    auto* rulerAct  = ctrlBar->addAction("Rulers");
    rulerAct->setCheckable(true);
    rulerAct->setChecked(true);
    ctrlBar->addSeparator();
    ctrlBar->addAction("Rebuild All", [this]() {
        if (m_preview) m_preview->triggerRenderAll();
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
    viewScaleSpin->setRange(0.01, 100.0);
    viewScaleSpin->setSingleStep(0.5);
    viewScaleSpin->setDecimals(2);
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

    propLayout->addStretch();
    rightScroll->setWidget(propWidget);
    layout->addWidget(rightScroll, 1);

    // ── 清單選取 → 填入屬性 ──────────────────────────────────────────────
    connect(m_viewList, &QListWidget::currentRowChanged,
            [this, propWidget, viewLabelEdit, viewScaleSpin,
             viewPosXSpin, viewPosYSpin,
             showHiddenChk, showCenterChk, showDimChk,
             showAnnotChk, showScaleLblChk, showViewLblChk](int row) {
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
            });

    // 屬性變更 → 更新視圖
    auto updateSelectedView = [this, viewLabelEdit, viewScaleSpin,
                               viewPosXSpin, viewPosYSpin,
                               showHiddenChk, showCenterChk]() {
        int row = m_viewList->currentRow();
        if (!m_sheet || row < 0 || row >= m_sheet->views().size()) return;
        DrawingView* v = m_sheet->views().at(row);
        v->setLabel(viewLabelEdit->text());
        v->setScale(viewScaleSpin->value());
        v->setPosition(QPointF(viewPosXSpin->value(), viewPosYSpin->value()));
        v->setShowHiddenLines(showHiddenChk->isChecked());
        v->setShowCenterLines(showCenterChk->isChecked());
        if (m_preview) m_preview->update();
    };

    connect(viewLabelEdit, &QLineEdit::textChanged,     updateSelectedView);
    connect(viewScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            updateSelectedView);
    connect(viewPosXSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            updateSelectedView);
    connect(viewPosYSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            updateSelectedView);
    connect(showHiddenChk, &QCheckBox::toggled, updateSelectedView);
    connect(showCenterChk, &QCheckBox::toggled, updateSelectedView);

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

void DrawingSheetDialog::onExportPdf()
{
    QString path = QFileDialog::getSaveFileName(this, "Export PDF",
                                                m_sheet->fileName().replace(".aicad_dwg", ".pdf"),
                                                "PDF Files (*.pdf)");
    if (path.isEmpty()) return;

    applyToSheet();
    if (!m_sheet->exportPdf(path)) {
        QMessageBox::warning(this, "Export Failed",
                             "Failed to export PDF.\n(Not yet fully implemented)");
    } else {
        QMessageBox::information(this, "Export Complete",
                                 "PDF exported to:\n" + path);
    }
}

void DrawingSheetDialog::onExportSvg()
{
    QString path = QFileDialog::getSaveFileName(this, "Export SVG",
                                                m_sheet->fileName().replace(".aicad_dwg", ".svg"),
                                                "SVG Files (*.svg)");
    if (path.isEmpty()) return;

    applyToSheet();
    if (!m_sheet->exportSvg(path)) {
        QMessageBox::warning(this, "Export Failed",
                             "Failed to export SVG.\n(Not yet fully implemented)");
    } else {
        QMessageBox::information(this, "Export Complete",
                                 "SVG exported to:\n" + path);
    }
}

// 即時預覽：圖紙設定變更
void DrawingSheetDialog::onSheetConfigEdited()
{
    if (!m_sheet) return;

    // 即時套用圖紙規格（紙張大小、方向），讓預覽立即反映
    SheetConfig cfg = m_sheet->config();
    cfg.paperSize        = (PaperSize)       m_paperSizeCombo->currentIndex();
    cfg.orientation      = (PaperOrientation)m_orientationCombo->currentIndex();
    cfg.frameTemplate    = (FrameTemplate)   m_frameTemplateCombo->currentIndex();
    cfg.projectionMethod = (ProjectionMethod)m_projectionCombo->currentIndex();
    cfg.defaultScale     = m_defaultScaleSpin->value();
    m_sheet->applyConfig(cfg);

    if (m_preview) {
        m_preview->fitToWindow();
        m_preview->update();
    }
}

// 即時預覽：標題欄欄位變更
void DrawingSheetDialog::onTitleBlockEdited()
{
    if (!m_sheet || !m_preview) return;

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
