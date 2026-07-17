#include "ui/CompoundChainCalcDialog.h"

#include "railway/AlignmentDocument.h"
#include "railway/AlignmentSolver.h"

#include <QComboBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QDoubleSpinBox>
#include <QtMath>

using aicad::railway::AlignmentDocument;
using aicad::railway::HorizontalAlignmentEdit;
using aicad::railway::EditableElementType;
using aicad::railway::SpiralType;
using aicad::railway::AlignmentSolver;
using aicad::railway::SolvedCompoundChain;

namespace aicad {
namespace ui {

namespace {
constexpr int kMinArcs = 2;
constexpr int kMaxArcs = 12;   ///< 表格 UI 的合理上限，非 solver 限制
}

CompoundChainCalcDialog::CompoundChainCalcDialog(AlignmentDocument* doc, QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
{
    setWindowTitle(tr("Compound Chain Trial Calculation (S0 C0 S1 C1 ... Sn)"));
    resize(720, 560);

    auto* mainLayout = new QVBoxLayout(this);

    // ── 邊界切線選擇 ─────────────────────────────────────────────────────
    auto* form = new QFormLayout();
    m_entryTangentCombo = new QComboBox(this);
    m_exitTangentCombo  = new QComboBox(this);
    form->addRow(tr("Entry tangent:"), m_entryTangentCombo);
    form->addRow(tr("Exit tangent:"),  m_exitTangentCombo);

    m_arcCountSpin = new QSpinBox(this);
    m_arcCountSpin->setRange(kMinArcs, kMaxArcs);
    m_arcCountSpin->setValue(kMinArcs);
    form->addRow(tr("Arc count N (>=2):"), m_arcCountSpin);
    mainLayout->addLayout(form);

    // ── 輸入表格：N+1 列，欄位 [Lk][Rk]（最後一列 Rk 留空） ─────────────────
    m_inputTable = new QTableWidget(this);
    m_inputTable->setColumnCount(3);
    m_inputTable->setHorizontalHeaderLabels({ tr("Segment"), tr("Spiral length Lk (0=omit)"), tr("Arc radius Rk") });
    m_inputTable->horizontalHeader()->setStretchLastSection(true);
    mainLayout->addWidget(new QLabel(tr("Chain parameters:"), this));
    mainLayout->addWidget(m_inputTable);

    // ── 按鈕列 ───────────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    m_calcButton  = new QPushButton(tr("Calculate"), this);
    m_applyButton = new QPushButton(tr("Apply"), this);
    m_applyButton->setEnabled(false);
    btnRow->addWidget(m_calcButton);
    btnRow->addWidget(m_applyButton);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    // ── 狀態列 ───────────────────────────────────────────────────────────
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    // ── 試算結果表格 ─────────────────────────────────────────────────────
    m_resultTable = new QTableWidget(this);
    m_resultTable->setColumnCount(6);
    m_resultTable->setHorizontalHeaderLabels(
        { tr("Point"), tr("Easting"), tr("Northing"), tr("Azimuth"), tr("Next seg. length"), tr("Radius") });
    m_resultTable->horizontalHeader()->setStretchLastSection(true);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(new QLabel(tr("Trial result (node sequence):"), this));
    mainLayout->addWidget(m_resultTable);

    connect(m_arcCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CompoundChainCalcDialog::onArcCountChanged);
    connect(m_calcButton,  &QPushButton::clicked, this, &CompoundChainCalcDialog::onCalculate);
    connect(m_applyButton, &QPushButton::clicked, this, &CompoundChainCalcDialog::onApply);

    populateTangentCombos();
    rebuildInputTable();
}

// ────────────────────────────────────────────────────────────────────────────
//  populateTangentCombos
// ────────────────────────────────────────────────────────────────────────────

void CompoundChainCalcDialog::populateTangentCombos()
{
    m_entryTangentCombo->clear();
    m_exitTangentCombo->clear();
    if (!m_doc || !m_doc->horizontal()) return;

    const auto& elems = m_doc->horizontal()->elements();
    for (int i = 0; i < elems.size(); ++i) {
        if (elems[i].type != EditableElementType::Tangent) continue;
        const QString label = tr("Tangent #%1").arg(i);
        m_entryTangentCombo->addItem(label, i);
        m_exitTangentCombo->addItem(label, i);
    }
    if (m_exitTangentCombo->count() > 1)
        m_exitTangentCombo->setCurrentIndex(1);
}

// ────────────────────────────────────────────────────────────────────────────
//  onArcCountChanged / rebuildInputTable
// ────────────────────────────────────────────────────────────────────────────

void CompoundChainCalcDialog::onArcCountChanged(int)
{
    rebuildInputTable();
    m_lastCalcValid = false;
    m_applyButton->setEnabled(false);
}

void CompoundChainCalcDialog::rebuildInputTable()
{
    const int n = m_arcCountSpin->value();
    m_inputTable->setRowCount(n + 1);

    for (int row = 0; row <= n; ++row) {
        const QString label = (row == 0) ? tr("S0 (entry)")
            : (row == n) ? tr("S%1 (exit)").arg(row)
            : tr("S%1 (interior)").arg(row);
        m_inputTable->setItem(row, 0, new QTableWidgetItem(label));
        m_inputTable->item(row, 0)->setFlags(Qt::ItemIsEnabled);

        auto* lenItem = new QTableWidgetItem(QStringLiteral("0"));
        m_inputTable->setItem(row, 1, lenItem);

        if (row < n) {
            auto* radItem = new QTableWidgetItem(QStringLiteral("500"));
            m_inputTable->setItem(row, 2, radItem);
        } else {
            auto* naItem = new QTableWidgetItem(QStringLiteral("—"));
            naItem->setFlags(Qt::ItemIsEnabled);
            m_inputTable->setItem(row, 2, naItem);
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  readInputs
// ────────────────────────────────────────────────────────────────────────────

bool CompoundChainCalcDialog::readInputs(QVector<double>& outRadii, QVector<double>& outLens) const
{
    const int n = m_arcCountSpin->value();
    outLens.resize(n + 1);
    outRadii.resize(n);

    for (int row = 0; row <= n; ++row) {
        bool ok = false;
        const double len = m_inputTable->item(row, 1)->text().toDouble(&ok);
        if (!ok || len < 0.0) {
            m_statusLabel->setText(tr("Invalid spiral length at row %1 (must be >= 0).").arg(row));
            return false;
        }
        outLens[row] = len;

        if (row < n) {
            bool okR = false;
            const double r = m_inputTable->item(row, 2)->text().toDouble(&okR);
            if (!okR || r <= 0.0) {
                m_statusLabel->setText(tr("Invalid arc radius at row %1 (must be > 0).").arg(row));
                return false;
            }
            outRadii[row] = r;
        }
    }
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
//  onCalculate
// ────────────────────────────────────────────────────────────────────────────

void CompoundChainCalcDialog::onCalculate()
{
    m_lastCalcValid = false;
    m_applyButton->setEnabled(false);
    m_resultTable->setRowCount(0);

    if (!m_doc || !m_doc->horizontal()) {
        m_statusLabel->setText(tr("No AlignmentDocument."));
        return;
    }
    if (m_entryTangentCombo->currentIndex() < 0 || m_exitTangentCombo->currentIndex() < 0) {
        m_statusLabel->setText(tr("Please select both entry and exit tangents."));
        return;
    }
    const int tb = m_entryTangentCombo->currentData().toInt();
    const int ta = m_exitTangentCombo->currentData().toInt();
    if (tb == ta) {
        m_statusLabel->setText(tr("Entry and exit tangent must be different."));
        return;
    }

    QVector<double> radii, lens;
    if (!readInputs(radii, lens)) return;   // 訊息已由 readInputs 設定

    const auto& elems = m_doc->horizontal()->elements();
    if (tb < 0 || tb >= elems.size() || ta < 0 || ta >= elems.size()) {
        m_statusLabel->setText(tr("Invalid tangent selection."));
        return;
    }

    const int n = m_arcCountSpin->value();
    QVector<SpiralType> types(n + 1, SpiralType::Clothoid);

    const SolvedCompoundChain chain = AlignmentSolver::solveCompoundChain(
        radii, lens, types,
        elems[tb].startPI, elems[tb].endPI,
        elems[ta].startPI, elems[ta].endPI);

    if (!chain.valid) {
        m_statusLabel->setText(tr("Calculation failed — spirals may overlap or the turning"
                                   " angle is inconsistent with the given radii/lengths."
                                   " Try shorter spirals or check tangent selection."));
        return;
    }

    // ── 顯示節點序列 ─────────────────────────────────────────────────────
    m_resultTable->setRowCount(chain.nodes.size());
    for (int i = 0; i < chain.nodes.size(); ++i) {
        const auto& node = chain.nodes[i];
        const QString ptLabel = (i == 0) ? tr("TS")
            : (i == chain.nodes.size() - 1) ? tr("ST")
            : (node.isArcStart ? tr("SC%1").arg(i) : tr("CS%1").arg(i));

        m_resultTable->setItem(i, 0, new QTableWidgetItem(ptLabel));
        m_resultTable->setItem(i, 1, new QTableWidgetItem(QString::number(node.pt.x(), 'f', 4)));
        m_resultTable->setItem(i, 2, new QTableWidgetItem(QString::number(node.pt.y(), 'f', 4)));
        m_resultTable->setItem(i, 3, new QTableWidgetItem(QString::number(qRadiansToDegrees(node.az), 'f', 4) + QStringLiteral("\xC2\xB0")));
        m_resultTable->setItem(i, 4, new QTableWidgetItem(QString::number(node.segLength, 'f', 3)));
        m_resultTable->setItem(i, 5, new QTableWidgetItem(QString::number(node.radius, 'f', 3)));
    }

    m_statusLabel->setText(tr("Calculation succeeded (%1 nodes). Review the result, then click Apply.")
                               .arg(chain.nodes.size()));
    m_lastCalcValid = true;
    m_applyButton->setEnabled(true);
}

// ────────────────────────────────────────────────────────────────────────────
//  onApply
// ────────────────────────────────────────────────────────────────────────────

void CompoundChainCalcDialog::onApply()
{
    if (!m_lastCalcValid || !m_doc || !m_doc->horizontal()) return;

    const int tb = m_entryTangentCombo->currentData().toInt();
    const int ta = m_exitTangentCombo->currentData().toInt();
    const int n  = m_arcCountSpin->value();

    QVector<double> radii, lens;
    if (!readInputs(radii, lens)) {
        QMessageBox::warning(this, tr("Apply failed"), m_statusLabel->text());
        return;
    }

    HorizontalAlignmentEdit::CompoundChainSpec spec;
    spec.arcs.resize(n);
    spec.spirals.resize(n + 1);
    for (int k = 0; k < n; ++k)
        spec.arcs[k].radius = radii[k];
    for (int k = 0; k <= n; ++k) {
        spec.spirals[k].length = lens[k];
        spec.spirals[k].type   = SpiralType::Clothoid;
    }

    const int idx = m_doc->horizontal()->addCompoundChain(tb, ta, spec);
    if (idx < 0) {
        QMessageBox::warning(this, tr("Apply failed"),
                              tr("addCompoundChain() failed — check tangent selection."));
        return;
    }

    m_doc->horizontal()->solve();
    Q_EMIT chainApplied();
    accept();
}

} // namespace ui
} // namespace aicad
