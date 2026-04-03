/**
 * @file VAlignEditorDockWidget.cpp
 * @brief Vertical-alignment editor dock widget implementation.
 *        Includes VAlignPropertiesPanel.
 * @author AICAD Team
 * @date   2025-01
 */

#include "VAlignEditorDockWidget.h"
#include "VAlignProfileView.h"
#include "railway/RailwayAlignment.h"

#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QScrollArea>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFrame>
#include <QFont>
#include <QFontDatabase>
#include <QDebug>
#include <cmath>

namespace aicad {
namespace ui {

// ─────────────────────────────────────────────────────────────────────────────
//  VAlignPropertiesPanel
//  A self-contained widget embedded in the right side of the dock.
// ─────────────────────────────────────────────────────────────────────────────

class VAlignPropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit VAlignPropertiesPanel(QWidget* parent = nullptr);

    /** Show data for VIP at index i in the vips list. -1 = clear. */
    void showVip(int index, const QVector<Vip>& vips,
                 const QVector<VcData>& vcs,
                 const QVector<double>& grades,
                 const QVector<HElem>& hElems);

    void clearSelection();

    /** Populate the VIP list widget. */
    void refreshVipList(const QVector<Vip>& vips, int selIdx);

Q_SIGNALS:
    void formApplied(double ch, double el, double lvc);
    void vipDeleteRequested(int index);
    void vipSelectedFromList(int index);

private Q_SLOTS:
    void onApply();
    void onDelete();

private:
    void setupUI();

    QFont monoFont() const {
        QFont f("Courier New", 9);
        return f;
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    QLabel* makeLabel(const QString& text, int ptSize = 7,
                      const QColor& col = QColor("#0e2438")) const;

    QFrame* makeSeparator() const;

    static QString gradeStr(double g) {
        return (g >= 0 ? "+" : "") + QString::number(g * 100.0, 'f', 3) + " %";
    }

    // ── Widgets ───────────────────────────────────────────────────────────────
    QLabel*       m_headerTitle  = nullptr;
    QLabel*       m_headerSub    = nullptr;

    // Editable fields
    QDoubleSpinBox* m_sbCh  = nullptr;
    QDoubleSpinBox* m_sbEl  = nullptr;
    QDoubleSpinBox* m_sbLvc = nullptr;
    QPushButton*    m_btnApply  = nullptr;
    QPushButton*    m_btnDelete = nullptr;

    // Computed display rows
    QLabel* m_lblGradeIn   = nullptr;
    QLabel* m_lblGradeOut  = nullptr;
    QLabel* m_lblVcSection = nullptr;    // "Vertical Curve" section heading
    QLabel* m_lblVcType    = nullptr;
    QLabel* m_lblVcStart   = nullptr;
    QLabel* m_lblVcEnd     = nullptr;
    QLabel* m_lblVcLen     = nullptr;
    QLabel* m_lblVcK       = nullptr;
    QLabel* m_lblVcDg      = nullptr;
    QLabel* m_lblHSection  = nullptr;   // "H-Align at VIP" section heading
    QLabel* m_lblHType     = nullptr;
    QLabel* m_lblHRadius   = nullptr;

    // VIP list
    QListWidget* m_vipList = nullptr;

    QWidget* m_editArea  = nullptr;   // hide when nothing selected
    QWidget* m_emptyHint = nullptr;   // show when nothing selected

    int m_currentIdx = -1;

    // Styling helpers
    void styleValueLabel(QLabel* lbl, const QColor& col) const;
    void applyDarkStyle();
};

// ─────────────────────────────────────────────────────────────────────────────

VAlignPropertiesPanel::VAlignPropertiesPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    applyDarkStyle();
    clearSelection();
}

QLabel* VAlignPropertiesPanel::makeLabel(const QString& text, int ptSize,
                                         const QColor& col) const
{
    auto* lbl = new QLabel(text);
    QFont f("Courier New", ptSize);
    lbl->setFont(f);
    lbl->setStyleSheet(QString("color: %1;").arg(col.name()));
    return lbl;
}

QFrame* VAlignPropertiesPanel::makeSeparator() const
{
    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    sep->setStyleSheet("background-color: #0b1a2c; border: none;");
    return sep;
}

void VAlignPropertiesPanel::styleValueLabel(QLabel* lbl, const QColor& col) const
{
    QFont f("Courier New", 10);
    f.setBold(true);
    lbl->setFont(f);
    lbl->setStyleSheet(QString("color: %1;").arg(col.name()));
}

void VAlignPropertiesPanel::setupUI()
{
    setMinimumWidth(188);
    setMaximumWidth(240);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Header ────────────────────────────────────────────────────────────────
    auto* headerWidget = new QWidget;
    headerWidget->setObjectName("panelHeader");
    auto* headerLayout = new QVBoxLayout(headerWidget);
    headerLayout->setContentsMargins(10, 7, 10, 6);
    headerLayout->setSpacing(1);

    m_headerTitle = makeLabel("PROPERTIES", 7, QColor("#0e3458"));
    QFont ht = m_headerTitle->font();
    ht.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    m_headerTitle->setFont(ht);
    headerLayout->addWidget(m_headerTitle);

    m_headerSub = makeLabel("No selection", 7, QColor("#0b1c30"));
    headerLayout->addWidget(m_headerSub);
    rootLayout->addWidget(headerWidget);

    // ── Separator ─────────────────────────────────────────────────────────────
    rootLayout->addWidget(makeSeparator());

    // ── Scroll area for editable + computed content ───────────────────────────
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // ── Edit area (shown when VIP selected) ───────────────────────────────────
    m_editArea = new QWidget;
    auto* editLayout = new QVBoxLayout(m_editArea);
    editLayout->setContentsMargins(10, 8, 10, 4);
    editLayout->setSpacing(0);

    // Helper: add a labelled spinbox row
    auto addSpinRow = [&](const QString& labelTxt, QDoubleSpinBox*& sb,
                          double lo, double hi, double step, int dec,
                          const QString& suffix) {
        editLayout->addWidget(makeLabel(labelTxt.toUpper(), 7, QColor("#0d2438")));
        sb = new QDoubleSpinBox;
        sb->setRange(lo, hi);
        sb->setSingleStep(step);
        sb->setDecimals(dec);
        sb->setSuffix(suffix);
        sb->setFont(QFont("Courier New", 10));
        editLayout->addWidget(sb);
        editLayout->addSpacing(7);
    };
    addSpinRow("Chainage",  m_sbCh,   0.0, 99999.9, 1.0,  1, " m");
    addSpinRow("Elevation", m_sbEl,  -999.0,  9999.0, 0.001, 3, " m");
    addSpinRow("VC Length", m_sbLvc,    0.0,  9999.0, 1.0,   0, " m");

    // Apply / Delete buttons
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);
    m_btnApply = new QPushButton("Apply");
    m_btnApply->setObjectName("btnApply");
    m_btnDelete = new QPushButton("Delete VIP");
    m_btnDelete->setObjectName("btnDelete");
    btnRow->addWidget(m_btnApply);
    btnRow->addWidget(m_btnDelete);
    editLayout->addLayout(btnRow);
    editLayout->addSpacing(2);
    editLayout->addWidget(makeSeparator());
    editLayout->addSpacing(6);

    // Helper: add a computed row (label on left, value on right)
    auto addRow = [&](const QString& labelTxt, QLabel*& valLbl,
                      const QColor& valCol = QColor("#2a6888")) {
        auto* row = new QHBoxLayout;
        row->setSpacing(0);
        row->addWidget(makeLabel(labelTxt, 7, QColor("#1a3048")));
        row->addStretch();
        valLbl = makeLabel("—", 10, valCol);
        valLbl->setFont(QFont("Courier New", 10));
        QFont vf = valLbl->font(); vf.setBold(true); valLbl->setFont(vf);
        row->addWidget(valLbl);
        editLayout->addLayout(row);
    };

    editLayout->addWidget(makeLabel("GRADES", 7, QColor("#0d2438")));
    addRow("Grade In",  m_lblGradeIn,  QColor("#38c858"));
    addRow("Grade Out", m_lblGradeOut, QColor("#38c858"));

    editLayout->addSpacing(6);
    editLayout->addWidget(makeSeparator());
    editLayout->addSpacing(4);

    m_lblVcSection = makeLabel("VERTICAL CURVE", 7, QColor("#0d2438"));
    editLayout->addWidget(m_lblVcSection);
    addRow("Type",     m_lblVcType,  QColor("#00c8b8"));
    addRow("VC Start", m_lblVcStart, QColor("#3a6088"));
    addRow("VC End",   m_lblVcEnd,   QColor("#3a6088"));
    addRow("Length",   m_lblVcLen,   QColor("#3a6088"));
    addRow("K value",  m_lblVcK,     QColor("#7830c8"));
    addRow("Δ grade",  m_lblVcDg,    QColor("#485898"));

    editLayout->addSpacing(6);
    editLayout->addWidget(makeSeparator());
    editLayout->addSpacing(4);

    m_lblHSection = makeLabel("H-ALIGN AT VIP", 7, QColor("#0d2438"));
    editLayout->addWidget(m_lblHSection);
    addRow("Type",   m_lblHType,   QColor("#1c60c8"));
    addRow("Radius", m_lblHRadius, QColor("#1c60c8"));

    editLayout->addStretch();

    // ── Empty hint (shown when nothing selected) ───────────────────────────────
    m_emptyHint = new QWidget;
    auto* hintLayout = new QVBoxLayout(m_emptyHint);
    hintLayout->setAlignment(Qt::AlignCenter);
    auto* hintIcon = makeLabel("◇", 22, QColor(255,255,255));
    hintIcon->setAlignment(Qt::AlignCenter);
    hintIcon->setStyleSheet("color: rgba(255,255,255,0.06);");
    hintLayout->addWidget(hintIcon);
    auto* hintTxt = makeLabel(
        "Click a VIP to\nselect & edit\n\n"
        "Toolbar:\nadd / delete VIPs\ndrag to reposition",
        8, QColor("#0b1e2e"));
    hintTxt->setAlignment(Qt::AlignCenter);
    hintLayout->addWidget(hintTxt);

    // Combine in scroll
    auto* scrollContent = new QWidget;
    auto* scrollLayout  = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->setSpacing(0);
    scrollLayout->addWidget(m_editArea);
    scrollLayout->addWidget(m_emptyHint);
    scroll->setWidget(scrollContent);

    rootLayout->addWidget(scroll, 1);

    // ── Separator ─────────────────────────────────────────────────────────────
    rootLayout->addWidget(makeSeparator());

    // ── VIP list ──────────────────────────────────────────────────────────────
    auto* listHeader = makeLabel("VIP LIST", 7, QColor("#0c2438"));
    listHeader->setContentsMargins(10, 4, 0, 3);
    listHeader->setStyleSheet("background:#04070e; color:#0c2438; padding: 4px 10px;");
    rootLayout->addWidget(listHeader);

    m_vipList = new QListWidget;
    m_vipList->setMaximumHeight(148);
    m_vipList->setFont(QFont("Courier New", 8));
    rootLayout->addWidget(m_vipList);

    // ── Status bar ────────────────────────────────────────────────────────────
    rootLayout->addWidget(makeSeparator());

    // ── Connect signals ───────────────────────────────────────────────────────
    connect(m_btnApply,  &QPushButton::clicked, this, &VAlignPropertiesPanel::onApply);
    connect(m_btnDelete, &QPushButton::clicked, this, &VAlignPropertiesPanel::onDelete);
    connect(m_vipList, &QListWidget::currentRowChanged,
            this, &VAlignPropertiesPanel::vipSelectedFromList);
}

void VAlignPropertiesPanel::applyDarkStyle()
{
    setStyleSheet(R"(
        VAlignPropertiesPanel {
            background-color: #050810;
            color: #2a5070;
        }
        QWidget#panelHeader {
            background-color: #04070e;
            border-bottom: 1px solid #0b1a2c;
        }
        QScrollArea { background: #050810; border: none; }
        QScrollArea > QWidget { background: #050810; }

        QDoubleSpinBox {
            background-color: #020509;
            border: 1px solid #0b1c2e;
            border-radius: 2px;
            color: #1470b8;
            padding: 2px 5px;
            selection-background-color: #143a60;
        }
        QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
            width: 14px;
            background-color: #06101e;
            border-left: 1px solid #0b1c2e;
        }

        QPushButton#btnApply {
            background-color: #061428;
            border: 1px solid #144268;
            border-radius: 2px;
            color: #1878c0;
            padding: 4px 0;
            font-family: 'Courier New';
            font-size: 8pt;
            letter-spacing: 1px;
            text-transform: uppercase;
        }
        QPushButton#btnApply:hover { background-color: #0a1e38; border-color: #1e60a0; }

        QPushButton#btnDelete {
            background-color: #140810;
            border: 1px solid #320e1c;
            border-radius: 2px;
            color: #b03050;
            padding: 4px 0;
            font-family: 'Courier New';
            font-size: 8pt;
            letter-spacing: 1px;
        }
        QPushButton#btnDelete:hover { background-color: #1c0a14; border-color: #482030; }
        QPushButton#btnDelete:disabled { opacity: 0.3; }

        QListWidget {
            background-color: #040710;
            border: none;
            border-top: 1px solid #0b1a2c;
            color: #123450;
            font-family: 'Courier New';
            font-size: 8pt;
            outline: none;
        }
        QListWidget::item {
            padding: 3px 10px;
            border-bottom: 1px solid #07101e;
        }
        QListWidget::item:selected {
            background-color: #081628;
            color: #1868b0;
        }
        QListWidget::item:hover { background-color: #060e1c; }
        QScrollBar:vertical { width: 6px; background: #04070e; }
        QScrollBar::handle:vertical { background: #0e2438; border-radius: 3px; min-height: 20px; }
    )");
}

void VAlignPropertiesPanel::clearSelection()
{
    m_currentIdx = -1;
    m_headerSub->setText("No selection");
    m_editArea->hide();
    m_emptyHint->show();
    m_btnDelete->setEnabled(false);
}

void VAlignPropertiesPanel::showVip(int index, const QVector<Vip>& vips,
                                    const QVector<VcData>& vcs,
                                    const QVector<double>& grades,
                                    const QVector<HElem>& hElems)
{
    if (index < 0 || index >= vips.size()) { clearSelection(); return; }

    m_currentIdx = index;
    const Vip& v = vips[index];

    m_headerSub->setText(QString("VIP %1 / %2").arg(index + 1).arg(vips.size()));
    m_editArea->show();
    m_emptyHint->hide();

    // Populate spin boxes (block signals to avoid recursive updates)
    m_sbCh->blockSignals(true);   m_sbCh->setValue(v.ch);   m_sbCh->blockSignals(false);
    m_sbEl->blockSignals(true);   m_sbEl->setValue(v.el);   m_sbEl->blockSignals(false);
    m_sbLvc->blockSignals(true);  m_sbLvc->setValue(v.lvc); m_sbLvc->blockSignals(false);

    // Delete button: only disabled if this is the first or last VIP
    // (but we always allow deletion if vips.size() > 2)
    m_btnDelete->setEnabled(vips.size() > 2);

    // ── Grades ────────────────────────────────────────────────────────────────
    if (index > 0 && index - 1 < grades.size()) {
        double g = grades[index - 1];
        m_lblGradeIn->setText(gradeStr(g));
        m_lblGradeIn->setStyleSheet(
            QString("color: %1; font-family:'Courier New'; font-size:10pt; font-weight:bold;")
                .arg(g > 0 ? "#38c858" : g < 0 ? "#d84040" : "#4488a0"));
    } else {
        m_lblGradeIn->setText("—");
    }
    if (index < grades.size()) {
        double g = grades[index];
        m_lblGradeOut->setText(gradeStr(g));
        m_lblGradeOut->setStyleSheet(
            QString("color: %1; font-family:'Courier New'; font-size:10pt; font-weight:bold;")
                .arg(g > 0 ? "#38c858" : g < 0 ? "#d84040" : "#4488a0"));
    } else {
        m_lblGradeOut->setText("—");
    }

    // ── Vertical curve ─────────────────────────────────────────────────────────
    const VcData& vc = vcs[index];
    const bool hasVC = vc.L > 0.0;

    m_lblVcSection->setVisible(hasVC);
    m_lblVcType->setVisible(hasVC);
    m_lblVcStart->setVisible(hasVC);
    m_lblVcEnd->setVisible(hasVC);
    m_lblVcLen->setVisible(hasVC);
    m_lblVcK->setVisible(hasVC);
    m_lblVcDg->setVisible(hasVC);

    if (hasVC) {
        m_lblVcType->setText(vc.isSag ? "SAG" : "CREST");
        m_lblVcStart->setText(QString("%1 m").arg(vc.csStart, 0, 'f', 2));
        m_lblVcEnd->setText  (QString("%1 m").arg(vc.csEnd,   0, 'f', 2));
        m_lblVcLen->setText  (QString("%1 m").arg(vc.L,       0, 'f', 1));
        m_lblVcK->setText    (std::isinf(vc.K) ? "∞" : QString::number(vc.K, 'f', 1));
        m_lblVcDg->setText   (gradeStr(vc.s2 - vc.s1));
    }

    // ── Horizontal alignment context ───────────────────────────────────────────
    bool foundH = false;
    for (const HElem& el : hElems) {
        if (v.ch >= el.ch0 && v.ch <= el.ch1) {
            QString typeStr = el.type == HElemType::Tangent  ? "Tangent"
                              : el.type == HElemType::Circular ? "Circular"
                                                               :                                  "Spiral";
            QString col = el.type == HElemType::Tangent  ? "#1c60c8"
                          : el.type == HElemType::Circular ? "#00a8cc"
                                                           :                                  "#6828d8";
            m_lblHType->setText(typeStr);
            m_lblHType->setStyleSheet(
                QString("color:%1; font-family:'Courier New'; font-size:10pt; font-weight:bold;").arg(col));
            if (el.radius > 0.0) {
                m_lblHRadius->setText(QString("%1 m").arg(el.radius, 0, 'f', 0));
                m_lblHRadius->setStyleSheet(
                    QString("color:%1; font-family:'Courier New'; font-size:10pt; font-weight:bold;").arg(col));
                m_lblHRadius->setVisible(true);
            } else {
                m_lblHRadius->setVisible(false);
            }
            m_lblHSection->setVisible(true);
            m_lblHType->setVisible(true);
            foundH = true;
            break;
        }
    }
    if (!foundH) {
        m_lblHSection->setVisible(false);
        m_lblHType->setVisible(false);
        m_lblHRadius->setVisible(false);
    }
}

void VAlignPropertiesPanel::refreshVipList(const QVector<Vip>& vips, int selIdx)
{
    m_vipList->blockSignals(true);
    m_vipList->clear();
    for (int i = 0; i < vips.size(); ++i) {
        const Vip& v = vips[i];
        auto* item = new QListWidgetItem(
            QString("VIP %1   %2 · %3")
                .arg(i + 1)
                .arg(v.ch, 0, 'f', 0)
                .arg(v.el, 0, 'f', 2)
            );
        m_vipList->addItem(item);
    }
    m_vipList->setCurrentRow(selIdx);
    m_vipList->blockSignals(false);
}

void VAlignPropertiesPanel::onApply()
{
    Q_EMIT formApplied(m_sbCh->value(), m_sbEl->value(), m_sbLvc->value());
}

void VAlignPropertiesPanel::onDelete()
{
    if (m_currentIdx >= 0)
        Q_EMIT vipDeleteRequested(m_currentIdx);
}

// ─────────────────────────────────────────────────────────────────────────────
//  VAlignEditorDockWidget
// ─────────────────────────────────────────────────────────────────────────────

VAlignEditorDockWidget::VAlignEditorDockWidget(QWidget* parent)
    : QDockWidget("Vertical Alignment Editor", parent)
{
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setupToolbar();
    setupContent();
}

VAlignEditorDockWidget::~VAlignEditorDockWidget() = default;

// ── Toolbar ──────────────────────────────────────────────────────────────────

void VAlignEditorDockWidget::setupToolbar()
{
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setMovable(false);
    m_toolbar->setStyleSheet(R"(
        QToolBar {
            background-color: #04070f;
            border-bottom: 1px solid #0c1c2e;
            spacing: 2px;
            padding: 3px 6px;
        }
        QToolButton {
            background: transparent;
            border: 1px solid #0a1828;
            border-radius: 3px;
            color: #1c3c58;
            padding: 4px 8px;
            font-family: 'Courier New';
            font-size: 8pt;
            min-width: 36px;
        }
        QToolButton:checked  { background-color: rgba(20,100,180,.35); border-color: #1a6ab0; color: #48b8ff; }
        QToolButton:hover    { background-color: rgba(20,60,120,.2);   border-color: #183860; color: #2870a0; }
        QToolButton:pressed  { background-color: rgba(10,50,100,.4);   }
        QToolBar::separator  { background: #0e2034; width: 1px; margin: 3px 4px; }
    )");

    // ── Tool group (exclusive) ────────────────────────────────────────────────
    auto* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    m_actSelect = toolGroup->addAction("↖ Select");
    m_actSelect->setCheckable(true);
    m_actSelect->setChecked(true);
    m_actSelect->setToolTip("Select and drag VIPs");

    m_actAddVip = toolGroup->addAction("＋ Add VIP");
    m_actAddVip->setCheckable(true);
    m_actAddVip->setToolTip("Click in profile to insert a new VIP");

    m_actDelVip = toolGroup->addAction("✕ Delete");
    m_actDelVip->setCheckable(true);
    m_actDelVip->setToolTip("Click a VIP to delete it");

    m_toolbar->addActions(toolGroup->actions());
    m_toolbar->addSeparator();

    // ── View toggles ──────────────────────────────────────────────────────────
    m_actGrid = m_toolbar->addAction("⊞ Grid");
    m_actGrid->setCheckable(true);
    m_actGrid->setChecked(true);
    m_actGrid->setToolTip("Show/hide grid");

    m_actGrade = m_toolbar->addAction("G% Grade");
    m_actGrade->setCheckable(true);
    m_actGrade->setChecked(true);
    m_actGrade->setToolTip("Show/hide grade labels");

    m_actVC = m_toolbar->addAction("∩ VC");
    m_actVC->setCheckable(true);
    m_actVC->setChecked(true);
    m_actVC->setToolTip("Show/hide vertical curves");

    m_actKval = m_toolbar->addAction("K val");
    m_actKval->setCheckable(true);
    m_actKval->setChecked(true);
    m_actKval->setToolTip("Show/hide K-value labels");

    m_toolbar->addSeparator();

    // Cursor readout label (right side)
    m_cursorLabel = new QLabel("  —");
    m_cursorLabel->setFont(QFont("Courier New", 8));
    m_cursorLabel->setStyleSheet("color: #105888;");
    m_cursorLabel->setMinimumWidth(220);

    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
    m_toolbar->addWidget(m_cursorLabel);

    // Connect tool group
    connect(toolGroup, &QActionGroup::triggered,
            this,      &VAlignEditorDockWidget::onToolChanged);
    // Connect view toggles
    for (QAction* act : {m_actGrid, m_actGrade, m_actVC, m_actKval})
        connect(act, &QAction::toggled,
                this, &VAlignEditorDockWidget::onViewToggled);
}

// ── Content (profile + properties) ───────────────────────────────────────────

void VAlignEditorDockWidget::setupContent()
{
    auto* container = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_toolbar);

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setHandleWidth(4);
    m_splitter->setStyleSheet(
        "QSplitter::handle { background-color: #0b1a2c; }"
        );

    // Profile view
    m_profileView = new VAlignProfileView;
    m_splitter->addWidget(m_profileView);

    // Properties panel
    m_propsPanel = new VAlignPropertiesPanel;
    m_splitter->addWidget(m_propsPanel);

    // Initial split ratio ~76 / 24
    m_splitter->setStretchFactor(0, 76);
    m_splitter->setStretchFactor(1, 24);

    mainLayout->addWidget(m_splitter, 1);
    setWidget(container);

    // ── Connect profile view signals ──────────────────────────────────────────
    connect(m_profileView, &VAlignProfileView::vipSelected,
            this, &VAlignEditorDockWidget::onVipSelected);

    connect(m_profileView, &VAlignProfileView::vipMoved,
            this, [this](int idx, double, double) { onVipSelected(idx); });

    connect(m_profileView, &VAlignProfileView::vipListChanged,
            this, [this] {
                refreshPanel(m_profileView->selectedVip());
                Q_EMIT alignmentChanged();
            });

    connect(m_profileView, &VAlignProfileView::vipAdded,
            this, &VAlignEditorDockWidget::onVipAdded);

    connect(m_profileView, &VAlignProfileView::vipDeleteRequested,
            this, &VAlignEditorDockWidget::onVipDeleted);

    connect(m_profileView, &VAlignProfileView::cursorMoved,
            this, &VAlignEditorDockWidget::onCursorMoved);

    // ── Connect properties panel signals ──────────────────────────────────────
    connect(m_propsPanel, &VAlignPropertiesPanel::formApplied,
            this,         &VAlignEditorDockWidget::onFormApplied);

    connect(m_propsPanel, &VAlignPropertiesPanel::vipDeleteRequested,
            this,         &VAlignEditorDockWidget::onVipDeleted);

    connect(m_propsPanel, &VAlignPropertiesPanel::vipSelectedFromList,
            this,         &VAlignEditorDockWidget::onVipSelected);
}

// ── Public API ────────────────────────────────────────────────────────────────

void VAlignEditorDockWidget::setTrackCenterLine(railway::TrackCenterLine* tcl)
{
    m_tcl = tcl;
    if (!tcl) return;

    // Load VIPs from the vertical alignment
    const auto& vPts = tcl->vertical()->points();
    QVector<Vip> vips;
    int id = 1;
    for (const auto& vpt : vPts) {
        Vip v;
        v.id  = id++;
        v.ch  = vpt.chainage;
        v.el  = vpt.elevation;
        v.lvc = vpt.lvc;
        vips.append(v);
    }
    m_profileView->setVips(vips);

    // Load horizontal elements
    const auto& rawPts = tcl->horizontal()->rawPoints();
    QVector<HElem> hElems;
    for (int i = 0; i + 1 < rawPts.size(); ++i) {
        const auto& pt = rawPts[i];
        if (pt.tsc.size() < 2) continue;
        HElem el;
        el.ch0 = pt.chainage;
        el.ch1 = rawPts[i+1].chainage;
        QChar t = pt.tsc[1];
        if      (t == 'T') el.type = HElemType::Tangent;
        else if (t == 'C') { el.type = HElemType::Circular; el.radius = std::abs(pt.radius); }
        else                el.type = HElemType::Spiral;
        el.label = pt.curveType;
        hElems.append(el);
    }
    m_profileView->setHElements(hElems);
    m_profileView->setChainageEnd(rawPts.isEmpty() ? 850.0 : rawPts.last().chainage);

    // Build plan trace
    QVector<PlanPoint> trace;
    const double step = 2.0;
    double az = 0.0, dev = 0.0;
    trace.append({ 0.0, 0.0 });
    for (const HElem& el : hElems) {
        int n = std::max(1, int(std::ceil(el.ch1 - el.ch0) / step));
        for (int k = 1; k <= n; ++k) {
            double ds = (el.ch1 - el.ch0) / n;
            double s  = k * ds;
            double kappa = 0.0;
            if (el.type == HElemType::Circular) kappa = 1.0 / el.radius;
            az  += kappa * ds;
            dev += std::sin(az) * ds;
            trace.append({ el.ch0 + s, dev });
        }
    }
    m_profileView->setPlanTrace(trace);
}

railway::TrackCenterLine* VAlignEditorDockWidget::trackCenterLine() const
{
    return m_tcl;
}

// ── Private helpers ───────────────────────────────────────────────────────────

void VAlignEditorDockWidget::refreshPanel(int selIdx)
{
    const auto& vips   = m_profileView->vips();
    const auto  gs     = [&] {
        QVector<double> g;
        for (int i = 0; i < vips.size() - 1; ++i) {
            double dch = vips[i+1].ch - vips[i].ch;
            g.append(dch > 1e-9 ? (vips[i+1].el - vips[i].el) / dch : 0.0);
        }
        return g;
    }();

    // Build VcData vector for panel
    QVector<VcData> vcs(vips.size());
    for (int i = 1; i < vips.size() - 1; ++i) {
        const Vip& v = vips[i];
        if (v.lvc <= 0.0) continue;
        VcData vc;
        vc.vipIdx  = i;
        vc.s1      = gs[i-1];
        vc.s2      = gs[i];
        vc.L       = v.lvc;
        vc.csStart = v.ch - vc.L / 2.0;
        vc.csEnd   = v.ch + vc.L / 2.0;
        vc.elStart = v.el - vc.s1 * (vc.L / 2.0);
        double ds  = std::abs(vc.s2 - vc.s1);
        vc.K       = ds > 1e-10 ? vc.L / (ds * 100.0) : std::numeric_limits<double>::infinity();
        vc.isSag   = vc.s2 > vc.s1;
        vcs[i]     = vc;
    }

    m_propsPanel->showVip(selIdx, vips, vcs, gs, m_profileView->hElements());
    m_propsPanel->refreshVipList(vips, selIdx);
}

// ── Slot implementations ──────────────────────────────────────────────────────

void VAlignEditorDockWidget::onToolChanged(QAction* act)
{
    VAlignProfileView::Tool t = VAlignProfileView::Tool::Select;
    if (act == m_actAddVip) t = VAlignProfileView::Tool::AddVip;
    if (act == m_actDelVip) t = VAlignProfileView::Tool::DeleteVip;
    m_profileView->setTool(t);
}

void VAlignEditorDockWidget::onViewToggled()
{
    m_profileView->setShowGrid (m_actGrid->isChecked());
    m_profileView->setShowGrade(m_actGrade->isChecked());
    m_profileView->setShowVC   (m_actVC->isChecked());
    m_profileView->setShowKVal (m_actKval->isChecked());
}

void VAlignEditorDockWidget::onVipSelected(int index)
{
    m_profileView->setSelectedVip(index);
    refreshPanel(index);
}

void VAlignEditorDockWidget::onFormApplied(double ch, double el, double lvc)
{
    int idx = m_profileView->selectedVip();
    if (idx < 0) return;

    QVector<Vip> vips = m_profileView->vips();
    vips[idx].ch  = ch;
    vips[idx].el  = el;
    vips[idx].lvc = lvc;
    std::sort(vips.begin(), vips.end(),
              [](const Vip& a, const Vip& b){ return a.ch < b.ch; });

    m_profileView->setVips(vips);
    // Find new index of edited VIP
    int id = vips[idx].id;
    for (int i = 0; i < vips.size(); ++i)
        if (vips[i].id == id) { m_profileView->setSelectedVip(i); break; }

    refreshPanel(m_profileView->selectedVip());
    Q_EMIT alignmentChanged();
}

void VAlignEditorDockWidget::onVipDeleted(int index)
{
    QVector<Vip> vips = m_profileView->vips();
    if (vips.size() <= 2) return;
    vips.remove(index);
    m_profileView->setVips(vips);
    m_profileView->setSelectedVip(-1);
    m_propsPanel->clearSelection();
    m_propsPanel->refreshVipList(vips, -1);
    Q_EMIT alignmentChanged();
}

void VAlignEditorDockWidget::onVipAdded(double ch, double el)
{
    static int nextId = 200;
    QVector<Vip> vips = m_profileView->vips();
    Vip v;
    v.id  = ++nextId;
    v.ch  = std::round(ch * 10.0) / 10.0;
    v.el  = std::round(el * 1000.0) / 1000.0;
    v.lvc = 0.0;
    vips.append(v);
    std::sort(vips.begin(), vips.end(),
              [](const Vip& a, const Vip& b){ return a.ch < b.ch; });

    m_profileView->setVips(vips);

    // Select the newly added VIP
    for (int i = 0; i < vips.size(); ++i)
        if (vips[i].id == v.id) { m_profileView->setSelectedVip(i); break; }

    refreshPanel(m_profileView->selectedVip());
    Q_EMIT alignmentChanged();
}

void VAlignEditorDockWidget::onCursorMoved(double ch, double el)
{
    m_cursorLabel->setText(
        QString("  CH %1 m   │   EL %2 m")
            .arg(ch, 8, 'f', 1)
            .arg(el, 8, 'f', 3)
        );
}

} // namespace ui
} // namespace aicad

// ── MOC is required for VAlignPropertiesPanel which is defined in .cpp ────────
#include "VAlignEditorDockWidget.moc"
