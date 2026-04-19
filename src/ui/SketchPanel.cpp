// src/ui/SketchPanel.cpp
#include "SketchPanel.h"
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

void SketchPanel::setupSelectGroup(QWidget*, QVBoxLayout* layout)
{
    auto* grp  = new QGroupBox(tr("模式"), this);
    auto* hbox = new QHBoxLayout(grp);
    hbox->setSpacing(4);

    m_btnSelect = makeToolBtn(tr("▶ 選取"), tr("切換為選取模式，點選幾何元素"), grp);
    m_btnSelect->setCheckable(true);
    m_btnSelect->setChecked(false);

    hbox->addWidget(m_btnSelect);
    hbox->addStretch();

    connect(m_btnSelect, &QToolButton::clicked, this, [this] {
        // 取消所有繪圖按鈕 checked
        for (auto* b : {m_btnConstrLine, m_btnCenterline, m_btnConstrCircle})
            if (b) b->setChecked(false);
        m_btnSelect->setChecked(true);
        Q_EMIT requestSelectMode();
    });

    layout->addWidget(grp);
}

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

    setupSelectGroup(root, layout);
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

    auto uncheckSelect = [this] {
        if (m_btnSelect) m_btnSelect->setChecked(false);
        Q_EMIT requestDrawMode();
    };
    connect(m_btnConstrLine,   &QToolButton::clicked, this, uncheckSelect);
    connect(m_btnCenterline,   &QToolButton::clicked, this, uncheckSelect);
    connect(m_btnConstrCircle, &QToolButton::clicked, this, uncheckSelect);

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
                            {&m_btnCoincident,    tr("重合"),    tr("Coincident：兩點重合"),              ConstraintType::Coincident},
                            {&m_btnHorizontal,    tr("水平"),    tr("Horizontal：線段水平"),              ConstraintType::Horizontal},
                            {&m_btnVertical,      tr("垂直"),    tr("Vertical：線段垂直"),                ConstraintType::Vertical},
                            {&m_btnParallel,      tr("平行"),    tr("Parallel：兩線平行"),                ConstraintType::Parallel},
                            {&m_btnPerpendicular, tr("垂直相交"), tr("Perpendicular：兩線互相垂直"),       ConstraintType::Perpendicular},
                            {&m_btnTangent,       tr("相切"),    tr("Tangent：曲線相切"),                 ConstraintType::Tangent},
                            {&m_btnEqualLen,      tr("等長"),    tr("Equal Length：兩線段等長"),          ConstraintType::EqualLength},
                            {&m_btnEqualRad,      tr("等半徑"),  tr("Equal Radius：兩圓/弧等半徑"),       ConstraintType::EqualRadius},
                            {&m_btnConcentric,    tr("同心"),    tr("Concentric：兩圓/弧同心"),           ConstraintType::Concentric},
                            {&m_btnFixed,         tr("固定"),    tr("Fixed：幾何元素位置固定"),            ConstraintType::Fixed},
                            };

    for (int i = 0; i < defs.size(); ++i) {
        auto& d  = defs[i];
        *d.ptr   = makeToolBtn(d.text, d.tip, grp);
        geomGrid->addWidget(*d.ptr, i / 3, i % 3);
        ConstraintType ct = d.ct;
        connect(*d.ptr, &QToolButton::clicked, this,
                [this, ct]{ Q_EMIT requestConstraint(ct); });
    }
    vbox->addLayout(geomGrid);

    // ── 尺寸型約束（帶數值輸入） ─────────────────────────────────
    auto* dimBox  = new QGroupBox(tr("尺寸約束"), grp);
    auto* dimGrid = new QGridLayout(dimBox);
    dimGrid->setSpacing(3);

    m_btnDistance = makeToolBtn(tr("距離"), tr("Fixed Distance：設定兩點距離"), dimBox);
    m_btnRadius   = makeToolBtn(tr("半徑"), tr("Fixed Radius：設定圓/弧半徑"),  dimBox);
    m_btnAngle    = makeToolBtn(tr("角度"), tr("Fixed Angle：設定線段角度"),     dimBox);

    m_valueInput  = new QDoubleSpinBox(dimBox);
    m_valueInput->setRange(0.001, 1e6);
    m_valueInput->setDecimals(3);
    m_valueInput->setValue(10.0);
    m_valueInput->setPrefix(tr("值: "));
    m_valueInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    dimGrid->addWidget(new QLabel(tr("數值:"), dimBox), 0, 0);
    dimGrid->addWidget(m_valueInput,   0, 1, 1, 2);
    dimGrid->addWidget(m_btnDistance,  1, 0);
    dimGrid->addWidget(m_btnRadius,    1, 1);
    dimGrid->addWidget(m_btnAngle,     1, 2);

    auto emitDim = [this](ConstraintType ct) {
        Q_EMIT requestConstraintWithValue(ct, m_valueInput->value());
    };
    connect(m_btnDistance, &QToolButton::clicked, this,
            [this, emitDim]{ emitDim(ConstraintType::FixedDistance); });
    connect(m_btnRadius,   &QToolButton::clicked, this,
            [this, emitDim]{ emitDim(ConstraintType::FixedRadius); });
    connect(m_btnAngle,    &QToolButton::clicked, this,
            [this, emitDim]{ emitDim(ConstraintType::FixedAngleDim); });

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

    m_btnRemove = new QPushButton(tr("刪除選取約束"), listBox);
    m_btnRemove->setEnabled(false);

    connect(m_constraintTree, &QTreeWidget::itemClicked,
            this, &SketchPanel::onConstraintItemClicked);
    connect(m_btnRemove, &QPushButton::clicked,
            this, &SketchPanel::onRemoveConstraintClicked);

    listVbox->addWidget(m_constraintTree);
    listVbox->addWidget(m_btnRemove);
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
    setEnabled(false);
    if (m_constraintTree) m_constraintTree->clear();
    if (m_dofLabel)       m_dofLabel->setText(tr("—"));
    if (m_statusLabel)    m_statusLabel->setText(tr("—"));
    if (m_residualLabel)  m_residualLabel->setText(tr("—"));
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

    for (const auto& c : m_sketch->constraints()) {
        auto* item = new QTreeWidgetItem();
        item->setText(0, constraintTypeName(c.type));
        // 顯示參與的幾何 UUID（縮短顯示）
        QStringList refs;
        for (const auto& r : c.refs)
            refs << r.geomUuid.left(8);
        item->setText(1, refs.join(", "));
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

} // namespace aicad::ui
