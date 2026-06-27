// src/ui/SketchPanel.cpp
#include "SketchPanel.h"
#include "cad/ConstraintPickSession.h"   // full definition needed for connect() and method calls
#include "core/CommandLineManager.h"
#include <limits>
#include <QPair>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QSplitter>
#include <QScrollArea>
#include <QIcon>
#include <QHeaderView>
#include <QHash>
#include <QDebug>
#include <QInputDialog>
#include <QCheckBox>
#include "core/CommandLineManager.h"

using namespace aicad::cad;

namespace aicad::ui {

// ─────────────────────────────────────────────────────────────────────────────
// 小工具：建立統一樣式的工具按鈕
// ─────────────────────────────────────────────────────────────────────────────
static QToolButton* makeToolBtn(const QString& text,
                                const QString& tooltip,
                                QWidget* parent)
{
    auto* btn = new QToolButton(parent);
    btn->setText(text);
    btn->setToolTip(tooltip);
    btn->setMinimumWidth(72);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return btn;
}

// ─────────────────────────────────────────────────────────────────────────────
SketchPanel::SketchPanel(QWidget* parent)
    : QDockWidget(tr("草圖工具"), parent)
{
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setMinimumWidth(220);
    setupUI();
    clearSketch();   // 初始禁用
}

SketchPanel::~SketchPanel() = default;

// ─────────────────────────────────────────────────────────────────────────────
void SketchPanel::setupUI()
{
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* root   = new QWidget(scroll);
    auto* layout = new QVBoxLayout(root);
    layout->setSpacing(6);
    layout->setContentsMargins(6, 6, 6, 6);

    setupConstructionGroup(root, layout);
    setupConstraintGroup(root, layout);
    setupSolverGroup(root, layout);
    layout->addStretch();

    scroll->setWidget(root);
    setWidget(scroll);
}

// ─────────────────────────────────────────────────────────────────────────────
void SketchPanel::setupConstructionGroup(QWidget*, QVBoxLayout* layout)
{
    auto* grp   = new QGroupBox(tr("建構幾何"), this);
    auto* grid  = new QGridLayout(grp);
    grid->setSpacing(4);

    m_btnConstrLine   = makeToolBtn(tr("建構線"),   tr("加入建構線（虛線，不進入輪廓）"), grp);
    m_btnCenterline   = makeToolBtn(tr("中心線"),   tr("加入中心線（點划線，用作旋轉軸/對稱軸）"), grp);
    m_btnConstrCircle = makeToolBtn(tr("建構圓"),   tr("加入建構圓（僅供參考）"), grp);

    grid->addWidget(m_btnConstrLine,   0, 0);
    grid->addWidget(m_btnCenterline,   0, 1);
    grid->addWidget(m_btnConstrCircle, 1, 0);

    connect(m_btnConstrLine,   &QToolButton::clicked, this,
            &SketchPanel::requestAddConstructionLine);
    connect(m_btnCenterline,   &QToolButton::clicked, this,
            &SketchPanel::requestAddCenterline);
    connect(m_btnConstrCircle, &QToolButton::clicked, this,
            &SketchPanel::requestAddConstructionCircle);

    layout->addWidget(grp);
}

// ─────────────────────────────────────────────────────────────────────────────
void SketchPanel::setupConstraintGroup(QWidget*, QVBoxLayout* layout)
{
    auto* grp  = new QGroupBox(tr("幾何約束"), this);
    auto* vbox = new QVBoxLayout(grp);
    vbox->setSpacing(4);

    // ── 幾何型約束：4×3 格 ───────────────────────────────────────
    auto* geomGrid = new QGridLayout();
    geomGrid->setSpacing(3);

    struct BtnDef { QToolButton** ptr; QString text; QString tip; ConstraintType ct; };
    QVector<BtnDef> defs = {
        {&m_btnCoincident,    tr("重合"),    tr("Coincident：兩點重合"),             ConstraintType::Coincident},
        {&m_btnHorizontal,    tr("水平"),    tr("Horizontal：線段水平"),             ConstraintType::Horizontal},
        {&m_btnVertical,      tr("垂直"),    tr("Vertical：線段垂直"),               ConstraintType::Vertical},
        {&m_btnParallel,      tr("平行"),    tr("Parallel：兩線平行"),               ConstraintType::Parallel},
        {&m_btnPerpendicular, tr("垂直相交"), tr("Perpendicular：兩線互相垂直"),      ConstraintType::Perpendicular},
        {&m_btnTangent,       tr("相切"),    tr("Tangent：曲線相切"),                ConstraintType::Tangent},
        {&m_btnEqualLen,      tr("等長"),    tr("Equal Length：兩線段等長"),         ConstraintType::EqualLength},
        {&m_btnEqualRad,      tr("等半徑"),  tr("Equal Radius：兩圓/弧等半徑"),      ConstraintType::EqualRadius},
        {&m_btnConcentric,    tr("同心"),    tr("Concentric：兩圓/弧同心"),          ConstraintType::Concentric},
        {&m_btnFixed,         tr("固定"),    tr("Fixed：幾何元素位置固定"),           ConstraintType::Fixed},
        {&m_btnMidpoint,      tr("中點"),    tr("Midpoint：點在線段中點"),            ConstraintType::Midpoint},
        {&m_btnPointOnCurve,  tr("點在線"),  tr("PointOnCurve：點在曲線上"),         ConstraintType::PointOnCurve},
        {&m_btnCollinear,     tr("共線"),    tr("Collinear：三點共線或線段共線"),     ConstraintType::Collinear},
        {&m_btnSymmetric,     tr("對稱"),    tr("Symmetric：相對某軸對稱"),           ConstraintType::Symmetric},
    };

    // ✅ Task G: 幾何約束按鈕走命令列路徑（記錄歷史 + 觸發互動選取）
    // constraintType → 命令名稱對照表
    static const QHash<ConstraintType, QString> kCmdMap = {
        {ConstraintType::Coincident,    QStringLiteral("COINCIDENT")},
        {ConstraintType::Horizontal,    QStringLiteral("HORIZONTAL")},
        {ConstraintType::Vertical,      QStringLiteral("VERTICAL")},
        {ConstraintType::Parallel,      QStringLiteral("PARALLEL")},
        {ConstraintType::Perpendicular, QStringLiteral("PERPENDICULAR")},
        {ConstraintType::Tangent,       QStringLiteral("TANGENT")},
        {ConstraintType::EqualLength,   QStringLiteral("EQUALLEN")},
        {ConstraintType::EqualRadius,   QStringLiteral("EQUALRAD")},
        {ConstraintType::Concentric,    QStringLiteral("CONCENTRIC")},
        {ConstraintType::Fixed,         QStringLiteral("FIX")},
        {ConstraintType::Midpoint,      QStringLiteral("MIDPOINT")},
        {ConstraintType::PointOnCurve,  QStringLiteral("POINTONCURVE")},
        {ConstraintType::Collinear,     QStringLiteral("COLLINEAR")},
        {ConstraintType::Symmetric,     QStringLiteral("SYMMETRIC")},
    };

    for (int i = 0; i < defs.size(); ++i) {
        auto& d  = defs[i];
        *d.ptr   = makeToolBtn(d.text, d.tip, grp);
        geomGrid->addWidget(*d.ptr, i / 4, i % 4);
        ConstraintType ct = d.ct;
        connect(*d.ptr, &QToolButton::clicked, this, [this, ct] {
            QString cmd = kCmdMap.value(ct);
            if (!cmd.isEmpty()) {
                // 走命令列：保存歷史記錄，並觸發互動選取流程
                auto* cmdMgr = core::CommandLineManager::instance();
                if (cmdMgr) {
                    cmdMgr->executeCommand(cmd);
                    return;
                }
            }
            // Fallback（CommandLineManager 不可用時）：直接 emit（向後相容）
            Q_EMIT requestConstraint(ct);
        });
    }
    vbox->addLayout(geomGrid);

    // ── 尺寸型約束（帶數值輸入） ─────────────────────────────────
    auto* dimBox  = new QGroupBox(tr("尺寸約束"), grp);
    auto* dimGrid = new QGridLayout(dimBox);
    dimGrid->setSpacing(3);

    m_btnGeneralDim = makeToolBtn(
        tr("一般尺寸 (GDIM)"),
        tr("General Dimension：自動判斷尺寸類型\n"
           "• 線段→長度  • 圓→直徑  • 弧→半徑/弧長\n"
           "• 兩點→距離  • 兩線→夾角\n"
           "指令：GDIM 或 GD"),
        dimBox);

    // ── 數值 SpinBox ──────────────────────────────────────────────
    m_valueInput = new QDoubleSpinBox(dimBox);
    m_valueInput->setRange(0.001, 1e6);
    m_valueInput->setDecimals(3);
    m_valueInput->setValue(10.0);
    m_valueInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_valueInput->setToolTip(tr("尺寸數值（當表達式欄為空時採用此值）"));

    // ── 參數表達式輸入：輸入 "width" 或 "width*2" 等 ──────────────
    m_exprInput = new QLineEdit(dimBox);
    m_exprInput->setPlaceholderText(tr("參數名或算式，如 width、height*2"));
    m_exprInput->setToolTip(tr("留空則使用數值；填入參數名可與 ParameterPanel 聯動"));
    m_exprInput->setClearButtonEnabled(true);

    // ── Driving / 量測模式 ────────────────────────────────────────
    m_drivingCheck = new QCheckBox(tr("驅動模式（Driving）"), dimBox);
    m_drivingCheck->setChecked(true);
    m_drivingCheck->setToolTip(tr("勾選：約束驅動幾何（Driving）\n取消：量測模式（Reference）"));

    dimGrid->addWidget(new QLabel(tr("數值:"), dimBox),   0, 0);
    dimGrid->addWidget(m_valueInput,   0, 1, 1, 3);
    dimGrid->addWidget(new QLabel(tr("表達式:"), dimBox), 1, 0);
    dimGrid->addWidget(m_exprInput,    1, 1, 1, 3);
    dimGrid->addWidget(m_drivingCheck,  2, 0, 1, 4);
    dimGrid->addWidget(m_btnGeneralDim, 3, 0, 1, 4);

    connect(m_btnGeneralDim, &QToolButton::clicked, this, [this] {
        QString expr = m_exprInput   ? m_exprInput->text().trimmed() : QString();
        bool    drv  = m_drivingCheck ? m_drivingCheck->isChecked()  : true;
        QString cmd  = "GDIM";
        if (!expr.isEmpty()) cmd += " " + expr;
        if (!drv)            cmd += " -measured";
        core::CommandLineManager::instance()->executeCommand(cmd);
    });

    vbox->addWidget(dimBox);

    // ── 現有約束清單 ─────────────────────────────────────────────
    auto* listBox  = new QGroupBox(tr("現有約束"), grp);
    auto* listVbox = new QVBoxLayout(listBox);

    m_constraintTree = new QTreeWidget(listBox);
    m_constraintTree->setHeaderLabels({tr("類型"), tr("幾何")});
    m_constraintTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_constraintTree->setRootIsDecorated(false);
    m_constraintTree->setAlternatingRowColors(true);
    m_constraintTree->setMinimumHeight(100);
    m_constraintTree->header()->setStretchLastSection(true);
    m_constraintTree->header()->resizeSection(0, 90);

    m_constraintTree->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_btnRemove = new QPushButton(tr("刪除選取約束"), listBox);
    m_btnRemove->setEnabled(false);

    m_btnToggleOverlay = new QPushButton(tr("隱藏約束符號"), listBox);
    m_btnToggleOverlay->setCheckable(true);
    m_btnToggleOverlay->setToolTip(tr("切換 3D 視埠中約束符號的顯示"));

    connect(m_constraintTree, &QTreeWidget::itemClicked,
            this, &SketchPanel::onConstraintItemClicked);
    connect(m_constraintTree, &QTreeWidget::itemDoubleClicked,
            this, &SketchPanel::onConstraintItemDoubleClicked);
    connect(m_btnRemove, &QPushButton::clicked,
            this, &SketchPanel::onRemoveConstraintClicked);
    connect(m_btnToggleOverlay, &QPushButton::toggled,
            this, &SketchPanel::onToggleOverlay);

    auto* listBtnRow = new QHBoxLayout;
    listBtnRow->addWidget(m_btnRemove);
    listBtnRow->addWidget(m_btnToggleOverlay);

    listVbox->addWidget(m_constraintTree);
    listVbox->addLayout(listBtnRow);
    vbox->addWidget(listBox);

    layout->addWidget(grp);
}

// ─────────────────────────────────────────────────────────────────────────────
void SketchPanel::setupSolverGroup(QWidget*, QVBoxLayout* layout)
{
    auto* grp  = new QGroupBox(tr("約束求解器"), this);
    auto* vbox = new QVBoxLayout(grp);

    auto* infoGrid = new QGridLayout();
    infoGrid->addWidget(new QLabel(tr("自由度:"), grp), 0, 0);
    m_dofLabel = new QLabel(tr("—"), grp);
    m_dofLabel->setAlignment(Qt::AlignRight);
    infoGrid->addWidget(m_dofLabel, 0, 1);

    infoGrid->addWidget(new QLabel(tr("狀態:"), grp), 1, 0);
    m_statusLabel = new QLabel(tr("—"), grp);
    m_statusLabel->setAlignment(Qt::AlignRight);
    infoGrid->addWidget(m_statusLabel, 1, 1);

    infoGrid->addWidget(new QLabel(tr("殘差:"), grp), 2, 0);
    m_residualLabel = new QLabel(tr("—"), grp);
    m_residualLabel->setAlignment(Qt::AlignRight);
    infoGrid->addWidget(m_residualLabel, 2, 1);

    m_btnSolve = new QPushButton(tr("立即求解"), grp);
    m_btnSolve->setToolTip(tr("手動觸發約束求解（通常在加入約束後自動執行）"));

    connect(m_btnSolve, &QPushButton::clicked,
            this, &SketchPanel::onSolveClicked);

    vbox->addLayout(infoGrid);
    vbox->addWidget(m_btnSolve);
    layout->addWidget(grp);
}

// ─────────────────────────────────────────────────────────────────────────────
// setActiveSketch / clearSketch
// ─────────────────────────────────────────────────────────────────────────────
void SketchPanel::setActiveSketch(Sketch* sketch)
{
    // 斷開舊連接
    if (m_sketch) {
        m_sketch->disconnect(this);
    }

    m_sketch = sketch;

    // 啟用 UI
    setEnabled(sketch != nullptr);

    if (!sketch) {
        clearSketch();
        return;
    }

    // 連接 Sketch 信號
    connect(sketch, &Sketch::constraintAdded,
            this, &SketchPanel::onConstraintAdded);
    connect(sketch, &Sketch::constraintRemoved,
            this, &SketchPanel::onConstraintRemoved);
    connect(sketch, &Sketch::constraintSolved,
            this, &SketchPanel::onConstraintSolved);
    connect(sketch, &Sketch::geometryChanged,
            this, &SketchPanel::onGeometryChanged);

    // Phase 6：ConstraintPickSession → Sketch 自動施加約束
    if (auto* session = m_pickSession) {
        connect(session, &cad::ConstraintPickSession::constraintReady,
                this, &SketchPanel::onConstraintReadyFromSession,
                Qt::UniqueConnection);
        connect(session, &cad::ConstraintPickSession::sessionEnded,
                this, &SketchPanel::clearPickPrompt,
                Qt::UniqueConnection);
        connect(session, &cad::ConstraintPickSession::promptChanged,
                this, &SketchPanel::showPickPrompt,
                Qt::UniqueConnection);
    }

    refreshConstraintList();
    // 顯示初始 DOF
    int dof = sketch->degreesOfFreedom();
    updateDofLabel(dof, dof == 0 ? SolveStatus::FullyConstrained
                                 : SolveStatus::UnderConstrained);
}

void SketchPanel::clearSketch()
{
    if (m_sketch) m_sketch->disconnect(this);
    m_sketch = nullptr;
    m_pickingActive = false;
    setEnabled(false);
    if (m_constraintTree) m_constraintTree->clear();
    if (m_dofLabel)       m_dofLabel->setText(tr("—"));
    if (m_statusLabel)    m_statusLabel->setText(tr("—"));
    if (m_residualLabel)  m_residualLabel->setText(tr("—"));
    clearPickPrompt();
}

void SketchPanel::showPickPrompt(const QString& text)
{
    if (!m_statusLabel) return;
    m_pickingActive = true;
    // 橘色粗體提示 + ESC 取消說明
    m_statusLabel->setText(
        QString("<span style='color:#E07000;font-weight:bold;'>⊙ %1</span>"
                " <span style='color:#888;font-size:10px;'>（ESC 取消）</span>")
        .arg(text));
    // 不 disable 按鈕 — 改用 m_pickingActive flag 防止重複觸發
}

void SketchPanel::clearPickPrompt()
{
    if (!m_statusLabel) return;
    m_pickingActive = false;
    m_statusLabel->setText(tr("—"));
}

// ─────────────────────────────────────────────────────────────────────────────
// 槽函式
// ─────────────────────────────────────────────────────────────────────────────
void SketchPanel::onConstraintAdded(const QString&)
{
    refreshConstraintList();
    // ✅ 不重複計算 DOF，等待 onConstraintSolved 信號更新
    // （addConstraint 已觸發 solveConstraints → emit constraintSolved）
}

void SketchPanel::onConstraintRemoved(const QString&)
{
    refreshConstraintList();
    // ✅ 不重複計算 DOF，等待 onConstraintSolved 信號更新
    // （addConstraint 已觸發 solveConstraints → emit constraintSolved）
}

void SketchPanel::onConstraintSolved(SolveResult result)
{
    updateDofLabel(result.dof, result.status);
    m_residualLabel->setText(QString::number(result.residual, 'e', 3));
}

void SketchPanel::onGeometryChanged()
{
    // ✅ 幾何變動時重算 DOF 並顯示（此時尚未 solve，所以直接用 degreesOfFreedom()）
    if (!m_sketch) return;
    int dof = m_sketch->degreesOfFreedom();
    SolveStatus st = (dof == 0) ? SolveStatus::FullyConstrained
                     : (dof <  0) ? SolveStatus::OverConstrained
                                 : SolveStatus::UnderConstrained;
    updateDofLabel(dof, st);
}

void SketchPanel::onConstraintItemClicked(QTreeWidgetItem* item, int)
{
    m_btnRemove->setEnabled(item != nullptr);
}

void SketchPanel::onRemoveConstraintClicked()
{
    if (!m_sketch) return;
    auto* item = m_constraintTree->currentItem();
    if (!item) return;
    QString uuid = item->data(0, Qt::UserRole).toString();
    Q_EMIT requestRemoveConstraint(uuid);
}

void SketchPanel::onSolveClicked()
{
    Q_EMIT requestSolve();
}

// ─────────────────────────────────────────────────────────────────────────────
// 輔助
// ─────────────────────────────────────────────────────────────────────────────
void SketchPanel::refreshConstraintList()
{
    if (!m_constraintTree) return;
    m_constraintTree->clear();
    m_btnRemove->setEnabled(false);
    if (!m_sketch) return;

    m_constraintTree->setColumnCount(3);
    m_constraintTree->setHeaderLabels({tr("類型"), tr("幾何"), tr("值/表達式")});
    m_constraintTree->header()->resizeSection(0, 80);
    m_constraintTree->header()->resizeSection(1, 100);

    for (const auto& c : m_sketch->constraints()) {
        auto* item = new QTreeWidgetItem();
        item->setText(0, constraintTypeName(c.type));

        // 幾何 UUID（縮短顯示）
        QStringList refs;
        for (const auto& r : c.refs)
            refs << r.geomUuid.left(8);
        item->setText(1, refs.join(", "));

        // 尺寸約束：顯示 paramExpr（若有）或 value
        if (c.isDimensional()) {
            QString label = c.paramExpr.isEmpty()
                ? QString::number(c.value, 'f', 3)
                : QString("%1=%2").arg(c.paramExpr)
                                  .arg(c.value, 0, 'f', 3);
            // 量測模式用灰色斜體
            item->setText(2, label);
            if (!c.driving) {
                QFont f = item->font(2);
                f.setItalic(true);
                item->setFont(2, f);
                item->setForeground(2, QColor(0x888888));
            }
        }

        item->setData(0, Qt::UserRole, c.uuid);
        m_constraintTree->addTopLevelItem(item);
    }
}

void SketchPanel::updateDofLabel(int dof, SolveStatus status)
{
    if (!m_dofLabel) return;
    m_dofLabel->setText(QString::number(dof));

    QString statusText;
    QString color;
    switch (status) {
    case SolveStatus::FullyConstrained:
        statusText = tr("完全約束");   color = "#4CAF50"; break;
    case SolveStatus::UnderConstrained:
        statusText = tr("欠約束");     color = "#2196F3"; break;
    case SolveStatus::OverConstrained:
        statusText = tr("過度約束");   color = "#F44336"; break;
    case SolveStatus::Conflict:
        statusText = tr("約束衝突");   color = "#FF9800"; break;
    default:
        statusText = tr("求解錯誤");   color = "#9E9E9E"; break;
    }
    m_statusLabel->setText(statusText);
    m_statusLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(color));
}

QString SketchPanel::constraintTypeName(ConstraintType t) const
{
    const QHash<ConstraintType, QString> names = {
         {ConstraintType::Coincident,    tr("重合")},
         {ConstraintType::Horizontal,    tr("水平")},
         {ConstraintType::Vertical,      tr("垂直")},
         {ConstraintType::Parallel,      tr("平行")},
         {ConstraintType::Perpendicular, tr("垂直相交")},
         {ConstraintType::Tangent,       tr("相切")},
         {ConstraintType::EqualLength,   tr("等長")},
         {ConstraintType::EqualRadius,   tr("等半徑")},
         {ConstraintType::Concentric,    tr("同心")},
         {ConstraintType::Fixed,         tr("固定")},
         {ConstraintType::FixedDistance, tr("距離")},
         {ConstraintType::FixedRadius,   tr("半徑")},
         {ConstraintType::FixedAngleDim, tr("角度")},
         {ConstraintType::Midpoint,      tr("中點")},
         {ConstraintType::PointOnCurve,  tr("點在曲線")},
         {ConstraintType::Collinear,     tr("共線")},
         {ConstraintType::Symmetric,     tr("對稱")},
         };
    return names.value(t, tr("未知"));
}

// ── Phase 6：Overlay 管理 ────────────────────────────────────────────────────

void SketchPanel::enterSketchMode(
        cad::Sketch* sketch,
        const Handle(AIS_InteractiveContext)& ctx,
        const gp_Trsf& toWorld)
{
    exitOverlayMode();
    m_overlay = std::make_unique<cad::ConstraintOverlayManager>(ctx, this);
    m_overlay->attachMaster(sketch, toWorld);
    connect(m_overlay.get(),
            &cad::ConstraintOverlayManager::dimensionConstraintClicked,
            this, &SketchPanel::onDimensionClicked);
}

void SketchPanel::enterInstanceMode(
        cad::SketchInstance* inst,
        const Handle(AIS_InteractiveContext)& ctx,
        const gp_Trsf& toWorld)
{
    exitOverlayMode();
    m_overlay = std::make_unique<cad::ConstraintOverlayManager>(ctx, this);
    m_overlay->attachInstance(inst, toWorld);
    connect(m_overlay.get(),
            &cad::ConstraintOverlayManager::dimensionConstraintClicked,
            this, &SketchPanel::onDimensionClicked);
}

void SketchPanel::exitOverlayMode() {
    if (m_overlay) {
        m_overlay->detach();
        m_overlay.reset();
    }
}

void SketchPanel::onDimensionClicked(
        const QString& uuid,
        cad::ConstraintOverlayManager::Mode mode,
        const QString& instanceId)
{
    Q_EMIT dimensionConstraintClicked(uuid, mode, instanceId);

    // 同步填回尺寸輸入區（方便使用者快速修改）
    if (!m_sketch) return;
    for (const auto& c : m_sketch->constraints()) {
        if (c.uuid != uuid) continue;
        if (!c.paramExpr.isEmpty() && m_exprInput)
            m_exprInput->setText(c.paramExpr);
        else if (m_valueInput)
            m_valueInput->setValue(c.value);
        if (m_drivingCheck)
            m_drivingCheck->setChecked(c.driving);
        break;
    }
    qDebug() << "[SketchPanel] Dim clicked:" << uuid;
}

// ── 約束清單雙擊 → inline 編輯尺寸約束 ─────────────────────────────────────
void SketchPanel::onConstraintItemDoubleClicked(QTreeWidgetItem* item, int /*col*/)
{
    if (!m_sketch || !item) return;
    QString uuid = item->data(0, Qt::UserRole).toString();

    // 找到對應約束
    for (const auto& c : m_sketch->constraints()) {
        if (c.uuid != uuid || !c.isDimensional()) continue;

        // 彈出 inline 編輯對話框
        bool ok = false;
        QString current = c.paramExpr.isEmpty()
                        ? QString::number(c.value, 'f', 3)
                        : c.paramExpr;

        QString newExpr = QInputDialog::getText(
            this,
            tr("編輯尺寸約束"),
            tr("數值或參數表達式（如 50、width、height*2）："),
            QLineEdit::Normal,
            current,
            &ok);

        if (!ok || newExpr.trimmed().isEmpty()) return;

        newExpr = newExpr.trimmed();

        // 嘗試解析純數字
        bool isNum = false;
        double numVal = newExpr.toDouble(&isNum);

        Q_EMIT requestEditConstraint(uuid, newExpr, isNum ? numVal : c.value, isNum);
        break;
    }
}

// ── Overlay 顯示/隱藏切換 ────────────────────────────────────────────────────
void SketchPanel::onToggleOverlay()
{
    bool hidden = m_btnToggleOverlay->isChecked();
    m_btnToggleOverlay->setText(hidden ? tr("顯示約束符號") : tr("隱藏約束符號"));
    if (m_overlay)
        m_overlay->setVisible(!hidden);
}


// ─────────────────────────────────────────────────────────────────────────────
// Phase 6：ConstraintPickSession 回呼 — 自動施加約束
// ─────────────────────────────────────────────────────────────────────────────

void SketchPanel::onConstraintReadyFromSession(
    const QList<cad::GeomRef>& refs,
    double value,
    const QString& paramExpr,
    bool driving,
    cad::ConstraintType type)
{
    if (!m_sketch) return;

    auto* cmdMgr = core::CommandLineManager::instance();
    using CT = cad::ConstraintType;
    using GH = cad::GeomHandle;

    // ── 草圖平面參考幾何 UUID 正規化 ────────────────────────────────────────
    // CadView 顯示的軸 AIS 使用虛擬 UUID（"sketch_xaxis:<id>"）。
    // 在套用約束前，將其轉換成 Sketch 內真實的幾何 UUID。
    auto resolveAxisUuid = [this](const QString& u) -> QString {
        QString skId = m_sketch->id();
        if (u == "sketch_xaxis:"  + skId) return m_sketch->xAxisGeomUuid();
        if (u == "sketch_yaxis:"  + skId) return m_sketch->yAxisGeomUuid();
        if (u == "sketch_origin:" + skId) return m_sketch->originPointUuid();
        return u;
    };
    // 正規化 refs 副本（保留 handle，只替換 geomUuid）
    QList<cad::GeomRef> resolvedRefs;
    for (const auto& r : refs)
        resolvedRefs.append(cad::GeomRef(resolveAxisUuid(r.geomUuid), r.handle));

    // ── 幾何約束：直接呼叫語意化的 Sketch 方法 ────────────────────────────
    auto uuid0 = resolvedRefs.size() > 0 ? resolvedRefs[0].geomUuid : QString();
    auto uuid1 = resolvedRefs.size() > 1 ? resolvedRefs[1].geomUuid : QString();
    auto uuid2 = resolvedRefs.size() > 2 ? resolvedRefs[2].geomUuid : QString();

    QString constraintUuid;

    switch (type) {
    // ── 單幾何 ──────────────────────────────────────────────────────────────
    case CT::Horizontal:
        constraintUuid = m_sketch->constrainHorizontal(uuid0);
        break;
    case CT::Vertical:
        constraintUuid = m_sketch->constrainVertical(uuid0);
        break;
    case CT::Fixed:
        constraintUuid = m_sketch->constrainFixed(uuid0);
        break;

    // ── 雙幾何 ──────────────────────────────────────────────────────────────
    case CT::Parallel:
        constraintUuid = m_sketch->constrainParallel(uuid0, uuid1);
        break;
    case CT::Perpendicular:
        constraintUuid = m_sketch->constrainPerpendicular(uuid0, uuid1);
        break;
    case CT::Tangent:
        constraintUuid = m_sketch->constrainTangent(uuid0, uuid1);
        break;
    case CT::EqualLength:
        constraintUuid = m_sketch->constrainEqualLength(uuid0, uuid1);
        break;
    case CT::EqualRadius:
        constraintUuid = m_sketch->constrainEqualRadius(uuid0, uuid1);
        break;
    case CT::Concentric:
        constraintUuid = m_sketch->constrainConcentric(uuid0, uuid1);
        break;
    case CT::Collinear:
        constraintUuid = m_sketch->constrainCollinear(uuid0, uuid1);
        break;
    case CT::PointOnCurve:
        constraintUuid = m_sketch->constrainPointOnCurve(refs[0], uuid1);
        break;
    case CT::Midpoint:
        constraintUuid = m_sketch->constrainMidpoint(refs[0], uuid1);
        break;

    // ── Coincident：需要智慧端點配對 ────────────────────────────────────────
    case CT::Coincident: {
        // 若 refs 已帶有明確的 handle（OSnap 鎖定端點），直接用
        // 注意：用 resolvedRefs（已正規化軸/原點 UUID），避免 constrainCoincident
        // 收到虛擬 UUID "sketch_origin:<id>" 而導致 solver 找不到對應幾何。
        bool aHasHandle = (resolvedRefs[0].handle != GH::WholeGeom);
        bool bHasHandle = resolvedRefs.size() > 1 && (resolvedRefs[1].handle != GH::WholeGeom);

        qDebug() << "[Coincident] uuid0=" << uuid0 << "handle0=" << (int)resolvedRefs[0].handle
                 << "uuid1=" << uuid1 << "handle1=" << (resolvedRefs.size()>1 ? (int)resolvedRefs[1].handle : -1);

        if (aHasHandle && bHasHandle) {
            constraintUuid = m_sketch->constrainCoincident(resolvedRefs[0], resolvedRefs[1]);
        } else {
            // WholeGeom：找最近端點對
            auto* gA = m_sketch->findGeometry(uuid0);
            auto* gB = m_sketch->findGeometry(uuid1);
            if (!gA || !gB) {
                qDebug() << "[Coincident] gA=" << (void*)gA << "gB=" << (void*)gB
                         << "geomCount=" << m_sketch->geometryCount();
                if (cmdMgr) cmdMgr->printError("COI: 找不到對應幾何元素");
                return;
            }
            // 收集各自端點
            auto endpoints = [](const cad::SketchGeometry* g)
                -> QVector<QPair<QVector2D, GH>> {
                QVector<QPair<QVector2D, GH>> pts;
                if (g->type == cad::SketchGeometryType::Point) {
                    if (!g->points.isEmpty())
                        pts.append({g->points[0], GH::WholeGeom});
                } else if (!g->points.isEmpty()) {
                    pts.append({g->points.front(), GH::Start});
                    if (g->points.size() > 1)
                        pts.append({g->points.back(), GH::End});
                    if (g->type == cad::SketchGeometryType::Circle ||
                        g->type == cad::SketchGeometryType::Arc)
                        pts.append({g->points[0], GH::Center});
                }
                return pts;
            };
            auto ptsA = endpoints(gA);
            auto ptsB = endpoints(gB);
            GH hA = GH::Start, hB = GH::Start;
            float best = std::numeric_limits<float>::max();
            for (auto& [pa, ha] : ptsA) {
                for (auto& [pb, hb] : ptsB) {
                    float d = (pa - pb).lengthSquared();
                    if (d < best) { best = d; hA = ha; hB = hb; }
                }
            }
            constraintUuid = m_sketch->constrainCoincident(
                cad::GeomRef(uuid0, hA), cad::GeomRef(uuid1, hB));
        }
        break;
    }

    // ── Symmetric：三個幾何（點A, 點B, 軸線）────────────────────────────────
    case CT::Symmetric:
        constraintUuid = m_sketch->constrainSymmetric(refs[0], refs[1], uuid2);
        break;

    // ── 尺寸約束：走舊路徑 ──────────────────────────────────────────────────
    default: {
        cad::SketchConstraint c;
        c.type      = type;
        c.refs      = refs;
        c.value     = value;
        c.paramExpr = paramExpr;
        c.driving   = driving;

        if (m_pickSession) {
            c.dimLineOffsetX = m_pickSession->dimLineOffsetX();
            c.dimLineOffsetY = m_pickSession->dimLineOffsetY();

            if (type == CT::FixedDistance) {
                cad::DistanceMode dm = m_pickSession->resolveDistanceMode();
                if (dm == cad::DistanceMode::Invalid) {
                    if (cmdMgr)
                        cmdMgr->printError(
                            "DIST: 兩條不平行的線無法計算線線距，"
                            "請選點-點、點-線或平行線-線。");
                    clearPickPrompt();
                    return;
                }
                c.distMode = dm;
            }
        }
        int dofBefore = m_sketch->degreesOfFreedom();
        m_sketch->addConstraint(c);
        cad::SolveResult result = m_sketch->solveConstraints();
        int dofAfter = m_sketch->degreesOfFreedom();
        if (cmdMgr) {
            cmdMgr->printSuccess(
                QString("✅ 約束已施加。DOF: %1 → %2").arg(dofBefore).arg(dofAfter));
            command::reportSolveResult(result, cmdMgr);
        }
        clearPickPrompt();
        return;
    }
    } // switch

    // ── 幾何約束共用後處理 ───────────────────────────────────────────────────
    if (constraintUuid.isEmpty()) {
        if (cmdMgr)
            cmdMgr->printError(
                QString("約束加入失敗（幾何不相容或已存在相同約束）"));
        clearPickPrompt();
        return;
    }

    cad::SolveResult result = m_sketch->solveConstraints();
    int dof = m_sketch->degreesOfFreedom();
    if (cmdMgr) {
        cmdMgr->printSuccess(
            QString("✅ 約束已施加。剩餘 DOF: %1").arg(dof));
        command::reportSolveResult(result, cmdMgr);
    }
    clearPickPrompt();
}

} // namespace aicad::ui