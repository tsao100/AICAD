/**
 * @file VAlignEditorDockWidget.cpp
 * @brief Vertical-alignment editor dock widget implementation.
 *        Includes VAlignPropertiesPanel.
 * @author AICAD Team
 * @date   2025-01
 */

#include "VAlignEditorDockWidget.h"
#include "VAlignProfileView.h"
#include "VAlignCommandBar.h"              // Step 13
#include "VAlignTheme.h"
#include "railway/RailwayAlignment.h"
#include "railway/AlignmentDocument.h"     // Step 16

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
#include <QComboBox>
#include <QDebug>
#include <cmath>

using namespace aicad::railway;

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
    // Theme is defined in VAlignTheme.h — alias it here for convenience
    using Theme = aicad::ui::Theme;

    explicit VAlignPropertiesPanel(QWidget* parent = nullptr);

    void applyTheme(const Theme& t);
    static Theme makeTheme(ColorScheme s);

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
    QLabel*      m_vipListHeader = nullptr;

    QWidget* m_editArea  = nullptr;   // hide when nothing selected
    QWidget* m_emptyHint = nullptr;   // show when nothing selected

    int m_currentIdx = -1;

    Theme m_theme;   // current active theme (for dynamic label recoloring)

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
    sep->setStyleSheet("background-color: palette(mid); border: none;");
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
    m_vipListHeader = makeLabel("VIP LIST", 7, QColor("#0c2438"));
    m_vipListHeader->setContentsMargins(10, 4, 0, 3);
    m_vipListHeader->setObjectName("vipListHeader");
    rootLayout->addWidget(m_vipListHeader);

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

// ─────────────────────────────────────────────────────────────────────────────
//  Theme factory — delegates to VAlignTheme.cpp (single source of truth)
// ─────────────────────────────────────────────────────────────────────────────

VAlignPropertiesPanel::Theme
VAlignPropertiesPanel::makeTheme(ColorScheme s)
{
    return aicad::ui::makeTheme(s);
}

// ── applyTheme: 把 Theme struct 轉成所有 setStyleSheet 呼叫 ─────────────────

void VAlignPropertiesPanel::applyTheme(const Theme& t)
{
    m_theme = t;   // store for dynamic recolouring in showVip
    // ── header labels ──────────────────────────────────────────────────────────
    if (m_headerTitle) m_headerTitle->setStyleSheet(
        QString("color: %1;").arg(t.textTitle.name()));
    if (m_headerSub)   m_headerSub->setStyleSheet(
        QString("color: %1;").arg(t.textSub.name()));

    // ── section headers (grade, vc, h-align) ──────────────────────────────────
    for (QLabel* lbl : {m_lblVcSection, m_lblHSection}) {
        if (lbl) lbl->setStyleSheet(QString("color: %1;").arg(t.textSub.name()));
    }

    // ── empty hint ─────────────────────────────────────────────────────────────
    // Find hintIcon and hintText inside m_emptyHint
    if (m_emptyHint) {
        const auto children = m_emptyHint->findChildren<QLabel*>();
        if (children.size() >= 2) {
            children[0]->setStyleSheet(QString("color: rgba(%1,%2,%3,%4);")
                .arg(t.hintIcon.red()).arg(t.hintIcon.green())
                .arg(t.hintIcon.blue()).arg(t.hintIcon.alpha()));
            children[1]->setStyleSheet(QString("color: %1;").arg(t.hintText.name()));
        }
    }

    // ── main stylesheet ────────────────────────────────────────────────────────
    setStyleSheet(QString(R"(
        VAlignPropertiesPanel {
            background-color: %1;
            color: %2;
        }
        QWidget#panelHeader {
            background-color: %3;
            border-bottom: 1px solid %4;
        }
        QScrollArea { background: %1; border: none; }
        QScrollArea > QWidget { background: %1; }

        QDoubleSpinBox {
            background-color: %5;
            border: 1px solid %6;
            border-radius: 2px;
            color: %7;
            padding: 2px 5px;
            selection-background-color: %8;
        }
        QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
            width: 14px;
            background-color: %3;
            border-left: 1px solid %6;
        }

        QPushButton#btnApply {
            background-color: %9;
            border: 1px solid %10;
            border-radius: 2px;
            color: %11;
            padding: 4px 0;
            font-family: 'Courier New';
            font-size: 8pt;
            letter-spacing: 1px;
            text-transform: uppercase;
        }
        QPushButton#btnApply:hover { background-color: %3; border-color: %10; }

        QPushButton#btnDelete {
            background-color: %12;
            border: 1px solid %13;
            border-radius: 2px;
            color: %14;
            padding: 4px 0;
            font-family: 'Courier New';
            font-size: 8pt;
            letter-spacing: 1px;
        }
        QPushButton#btnDelete:hover { background-color: %3; border-color: %13; }
        QPushButton#btnDelete:disabled { opacity: 0.35; }

        QListWidget {
            background-color: %1;
            border: none;
            border-top: 1px solid %4;
            color: %15;
            font-family: 'Courier New';
            font-size: 8pt;
            outline: none;
        }
        QListWidget::item {
            padding: 3px 10px;
            border-bottom: 1px solid %4;
        }
        QListWidget::item:selected {
            background-color: %16;
            color: %17;
        }
        QListWidget::item:hover { background-color: %18; }
        QScrollBar:vertical { width: 6px; background: %3; }
        QScrollBar::handle:vertical { background: %19; border-radius: 3px; min-height: 20px; }
    )")
    .arg(t.bg.name())           // %1  main bg
    .arg(t.textMain.name())     // %2  main text
    .arg(t.bgPanel.name())      // %3  panel bg
    .arg(t.border.name())       // %4  border
    .arg(t.bgInput.name())      // %5  input bg
    .arg(t.borderInput.name())  // %6  input border
    .arg(t.textInput.name())    // %7  input text
    .arg(t.bgListSel.name())    // %8  selection bg (spinbox)
    .arg(t.bgApply.name())      // %9  apply bg
    .arg(t.borderApply.name())  // %10 apply border
    .arg(t.textApply.name())    // %11 apply text
    .arg(t.bgDelete.name())     // %12 delete bg
    .arg(t.borderDelete.name()) // %13 delete border
    .arg(t.textDelete.name())   // %14 delete text
    .arg(t.textListNorm.name()) // %15 list normal text
    .arg(t.bgListSel.name())    // %16 list selected bg
    .arg(t.textListSel.name())  // %17 list selected text
    .arg(t.bgListHov.name())    // %18 list hover bg
    .arg(t.bgScrollHandle.name()) // %19 scrollbar handle
    );
    // ── VIP list section header ────────────────────────────────────────────────
    if (m_vipListHeader) {
        m_vipListHeader->setStyleSheet(
            QString("background:%1; color:%2; padding: 4px 10px;")
            .arg(t.bgPanel.name()).arg(t.textSub.name()));
    }
}

void VAlignPropertiesPanel::applyDarkStyle()
{
    applyTheme(makeTheme(ColorScheme::A_OriginalDark));
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
        const QString col = g > 0 ? m_theme.accentGradePos.name()
                          : g < 0 ? m_theme.accentGradeNeg.name()
                                  : m_theme.accentGradeZero.name();
        m_lblGradeIn->setStyleSheet(
            QString("color: %1; font-family:'Courier New'; font-size:10pt; font-weight:bold;").arg(col));
    } else {
        m_lblGradeIn->setText("—");
    }
    if (index < grades.size()) {
        double g = grades[index];
        m_lblGradeOut->setText(gradeStr(g));
        const QString col = g > 0 ? m_theme.accentGradePos.name()
                          : g < 0 ? m_theme.accentGradeNeg.name()
                                  : m_theme.accentGradeZero.name();
        m_lblGradeOut->setStyleSheet(
            QString("color: %1; font-family:'Courier New'; font-size:10pt; font-weight:bold;").arg(col));
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
            QString col = el.type == HElemType::Tangent  ? m_theme.accentHTangent.name()
                          : el.type == HElemType::Circular ? m_theme.accentHCircular.name()
                                                           : m_theme.accentHSpiral.name();
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

    // ── 主題切換 Combo ────────────────────────────────────────────────────────
    m_schemeCombo = new QComboBox;
    m_schemeCombo->setFont(QFont("Courier New", 8));
    m_schemeCombo->setFixedWidth(148);
    m_schemeCombo->setToolTip("Color scheme");
    m_schemeCombo->addItem("A — Original Dark",   int(ColorScheme::A_OriginalDark));
    m_schemeCombo->addItem("B — Slate Dark",       int(ColorScheme::B_SlateDark));
    m_schemeCombo->addItem("C — Forest Dark",      int(ColorScheme::C_ForestDark));
    m_schemeCombo->addItem("D — Charcoal Amber",   int(ColorScheme::D_CharcoalAmber));
    m_schemeCombo->addItem("E — Light Steel",      int(ColorScheme::E_LightSteel));
    m_schemeCombo->addItem("F — Warm Ivory",       int(ColorScheme::F_WarmIvory));
    m_schemeCombo->addItem("G — Mint White",       int(ColorScheme::G_MintWhite));
    m_schemeCombo->addItem("H — Paper White",      int(ColorScheme::H_PaperWhite));
    m_toolbar->addWidget(m_schemeCombo);

    connect(m_schemeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                setColorScheme(static_cast<ColorScheme>(
                    m_schemeCombo->itemData(idx).toInt()));
            });

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

    // ── Step 13：底部命令列 ────────────────────────────────────────────────
    m_commandBar = new VAlignCommandBar;
    mainLayout->addWidget(m_commandBar);

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

    // Apply the initial (default) colour scheme so all sub-widgets are in sync.
    setColorScheme(m_currentScheme);
}

// ── Public API ────────────────────────────────────────────────────────────────

void VAlignEditorDockWidget::setColorScheme(ColorScheme scheme)
{
    if (m_currentScheme == scheme) return;
    m_currentScheme = scheme;

    // Sync combo without re-triggering the signal
    if (m_schemeCombo) {
        const QSignalBlocker blocker(m_schemeCombo);
        for (int i = 0; i < m_schemeCombo->count(); ++i) {
            if (m_schemeCombo->itemData(i).toInt() == int(scheme)) {
                m_schemeCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    // Build theme
    const Theme t = aicad::ui::makeTheme(scheme);

    // ── Apply to PropertiesPanel ───────────────────────────────────────────────
    if (m_propsPanel)
        m_propsPanel->applyTheme(t);

    // ── Apply to ProfileView ──────────────────────────────────────────────────
    if (m_profileView)
        m_profileView->setColorScheme(scheme);

    // ── Apply to Toolbar ──────────────────────────────────────────────────────
    if (m_toolbar) {
        m_toolbar->setStyleSheet(QString(R"(
            QToolBar {
                background-color: %1;
                border-bottom: 1px solid %2;
                spacing: 2px;
                padding: 3px 6px;
            }
            QToolButton {
                background: transparent;
                border: 1px solid %3;
                border-radius: 3px;
                color: %4;
                padding: 4px 8px;
                font-family: 'Courier New';
                font-size: 8pt;
                min-width: 36px;
            }
            QToolButton:checked  { background-color: %5; border-color: %6; color: %7; }
            QToolButton:hover    { background-color: %8; border-color: %3; color: %4; }
            QToolButton:pressed  { background-color: %5; }
            QToolBar::separator  { background: %9; width: 1px; margin: 3px 4px; }
            QComboBox {
                background: %10;
                border: 1px solid %3;
                border-radius: 3px;
                color: %4;
                padding: 2px 4px;
                font-family: 'Courier New';
                font-size: 8pt;
            }
            QComboBox::drop-down { border: none; width: 14px; }
            QComboBox QAbstractItemView {
                background: %10;
                color: %11;
                border: 1px solid %3;
                selection-background-color: %12;
            }
        )")
        .arg(t.tbBg.name())                         // %1  toolbar bg
        .arg(t.tbBorder.name())                     // %2  toolbar border
        .arg(t.tbBtnBorder.name())                  // %3  button border
        .arg(t.tbBtnText.name())                    // %4  button text
        .arg(t.tbBtnCheckedBg.name())               // %5  checked bg
        .arg(t.tbBtnCheckedBorder.name())           // %6  checked border
        .arg(t.tbBtnCheckedText.name())             // %7  checked text
        .arg(t.tbBg.lighter(t.dark ? 115 : 95).name()) // %8 hover bg
        .arg(t.tbSeparator.name())                  // %9  separator
        .arg(t.bgPanel.name())                      // %10 combo bg
        .arg(t.textMain.name())                     // %11 combo dropdown text
        .arg(t.bgListSel.name())                    // %12 combo selection bg
        );
        // cursor label text
        if (m_cursorLabel)
            m_cursorLabel->setStyleSheet(
                QString("color: %1;").arg(t.textCursor.name()));
    }

    // ── Apply to Splitter ─────────────────────────────────────────────────────
    if (m_splitter)
        m_splitter->setStyleSheet(
            QString("QSplitter::handle { background-color: %1; }")
            .arg(t.splitterHandle.name()));
}

void VAlignEditorDockWidget::setTrackCenterLine(railway::TrackCenterLine* tcl)
{
    m_tcl = tcl;
    if (!tcl) return;

    // ── Load VIPs ─────────────────────────────────────────────────────────────
    // Prefer editor-VIP format (lossless, one record per PVI) over the raw
    // VerticalAlignment m_pts, which may contain ALD-style triplet records.
    QVector<Vip> vips;
    int id = 1;

    if (tcl->hasEditorVips()) {
        // Restore exactly what the editor last saved
        for (const QJsonValue& val : tcl->editorVips()) {
            QJsonObject o = val.toObject();
            Vip v;
            v.id  = id++;
            v.ch  = o["ch"].toDouble();
            v.el  = o["el"].toDouble();
            v.lvc = o["lvc"].toDouble();
            vips.append(v);
        }
    } else {
        // First time — build VIPs from the raw VerticalAlignment points.
        // Collapse ALD triplets: skip records that are VC sub-entries
        // (identified by having lvc==0 and same chainage range as next triplet).
        // Simplest safe approach: take only records where lvc > 0 (PVI records)
        // OR records not sandwiched between a lvc>0 neighbour.
        const auto& vPts = tcl->vertical()->points();
        for (int i = 0; i < vPts.size(); ++i) {
            const auto& vpt = vPts[i];
            // Skip sub-records of a triplet: a record is a sub-record if
            // a neighbour has lvc > 0 and this record has lvc == 0.
            bool isTripletSub = false;
            if (vpt.lvc < 1e-6) {
                if (i > 0 && vPts[i-1].lvc > 1e-6) isTripletSub = true;
                if (i + 1 < vPts.size() && vPts[i+1].lvc > 1e-6) isTripletSub = true;
            }
            if (isTripletSub) continue;

            Vip v;
            v.id  = id++;
            v.ch  = vpt.chainage;
            v.el  = vpt.elevation;
            v.lvc = vpt.lvc;
            vips.append(v);
        }
        // If still empty (e.g. brand-new TCL), create two flat endpoints
        if (vips.isEmpty()) {
            Vip v0; v0.id = 1; v0.ch = 0.0;   v0.el = 0.0; vips.append(v0);
            Vip v1; v1.id = 2; v1.ch = 1000.0; v1.el = 0.0; vips.append(v1);
        }
    }

    m_profileView->setVips(vips);

    // ── Load horizontal elements for PLAN DEV strip ───────────────────────────
    // Prefer live AlignmentDocument result (has solved rawPoints) over TCL's
    // stored rawPoints (which may be empty if never synced from an edit session).
    const QVector<AlignmentPoint>* rawPtsPtr = nullptr;
    QVector<AlignmentPoint> alignDocPts;

    if (m_alignDoc && m_alignDoc->horizontal()->result()
        && !m_alignDoc->horizontal()->result()->isEmpty()) {
        alignDocPts = m_alignDoc->horizontal()->result()->rawPoints();
        rawPtsPtr = &alignDocPts;
    } else {
        rawPtsPtr = &tcl->horizontal()->rawPoints();
    }

    const QVector<AlignmentPoint>& rawPts = *rawPtsPtr;
    QVector<HElem> hElems;
    for (int i = 0; i + 1 < rawPts.size(); ++i) {
        const auto& pt = rawPts[i];
        if (pt.tsc.size() < 2) continue;
        HElem el;
        el.ch0 = pt.chainage;
        el.ch1 = rawPts[i+1].chainage;
        QChar t = pt.tsc[1];
        if      (t == 'T') el.type = HElemType::Tangent;
        else if (t == 'C') { el.type = HElemType::Circular; el.radius = pt.radius; }  // keep sign: + = right, - = left
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
            double ds    = (el.ch1 - el.ch0) / n;
            double kappa = (el.type == HElemType::Circular) ? 1.0 / el.radius : 0.0;
            az  += kappa * ds;
            dev += std::sin(az) * ds;
            trace.append({ el.ch0 + k * ds, dev });
        }
    }
    m_profileView->setPlanTrace(trace);
}

railway::TrackCenterLine* VAlignEditorDockWidget::trackCenterLine() const
{
    return m_tcl;
}

// ── Step 16：水平縱斷面聯動 ─────────────────────────────────────────────────
//
//  當 HorizontalAlignmentEdit::changed() 觸發時：
//   1. 從 solver 結果取出各元素的 ch 區間與類型
//   2. 更新 VAlignProfileView 的水平元素條帶
//   3. 更新總里程（chainageEnd）
//
void VAlignEditorDockWidget::setAlignmentDocument(railway::AlignmentDocument* doc)
{
    if (m_alignDoc == doc) return;

    // 斷開舊連接
    if (m_alignDoc)
        disconnect(m_alignDoc->horizontal(), nullptr, this, nullptr);

    m_alignDoc = doc;
    if (!doc) return;

    // Lambda：從 HorizontalAlignment 重建水平元素條帶
    auto rebuildHStrip = [this]() {
        if (!m_alignDoc) return;
        const HorizontalAlignment* ha = m_alignDoc->horizontal()->result();
        if (!ha || ha->isEmpty()) return;

        const QVector<AlignmentPoint>& rawPts = ha->rawPoints();
        QVector<HElem> hElems;
        for (int i = 0; i + 1 < rawPts.size(); ++i) {
            const AlignmentPoint& pt = rawPts[i];
            HElem el;
            el.ch0 = pt.chainage;
            el.ch1 = rawPts[i + 1].chainage;
            if (pt.tsc.size() >= 2) {
                QChar t = pt.tsc[1];
                if      (t == 'T') el.type = HElemType::Tangent;
                else if (t == 'C') { el.type = HElemType::Circular; el.radius = pt.radius; }  // keep sign: + = right, - = left
                else               el.type = HElemType::Spiral;
            }
            el.label = pt.curveType;
            hElems.append(el);
        }
        m_profileView->setHElements(hElems);

        // 更新總里程
        if (!rawPts.isEmpty())
            m_profileView->setChainageEnd(rawPts.last().chainage);

        // 重建 plan trace（PLAN DEV. 曲線）
        QVector<PlanPoint> trace;
        const double step = 2.0;
        double az = 0.0, dev = 0.0;
        trace.append({ 0.0, 0.0 });
        for (const HElem& el : hElems) {
            int n = std::max(1, int(std::ceil(el.ch1 - el.ch0) / step));
            for (int k = 1; k <= n; ++k) {
                double ds    = (el.ch1 - el.ch0) / n;
                double kappa = (el.type == HElemType::Circular) ? 1.0 / el.radius : 0.0;
                az  += kappa * ds;
                dev += std::sin(az) * ds;
                trace.append({ el.ch0 + k * ds, dev });
            }
        }
        m_profileView->setPlanTrace(trace);
    };

    connect(m_alignDoc->horizontal(), &railway::HorizontalAlignmentEdit::changed,
            this, rebuildHStrip);

    // 初始同步一次（若已有求解結果）
    rebuildHStrip();
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

void VAlignEditorDockWidget::writeBackToTcl()
{
    if (!m_tcl) return;

    const QVector<Vip> vips = m_profileView->vips();

    // Serialise current Vips to JSON and store in TCL
    QJsonArray arr;
    for (const Vip& v : vips) {
        QJsonObject o;
        o["ch"]  = v.ch;
        o["el"]  = v.el;
        o["lvc"] = v.lvc;
        arr.append(o);
    }

    // setEditorVips also rebuilds m_v for runtime queries
    m_tcl->setEditorVips(arr);
    qDebug() << "[VAlignEditor] writeBackToTcl:" << arr.size() << "VIPs";
}

} // namespace ui
} // namespace aicad

// ── MOC is required for VAlignPropertiesPanel which is defined in .cpp ────────
#include "VAlignEditorDockWidget.moc"