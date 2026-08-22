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

/** 欄位順序：Segment(標籤) / Lk / Rk / 弧長 Dk（弧長=圓弧弧長，非直徑）。 */
enum InputColumn { ColLabel = 0, ColSpiralLen = 1, ColArcRadius = 2, ColArcLen = 3 };

/** 與 AlignmentSCSCommand.cpp / AlignmentAddSpiralCommand.cpp 一致的顯示名稱。 */
QString spiralTypeDisplayName(SpiralType t)
{
    switch (t) {
    case SpiralType::HalfSine:         return QStringLiteral("HalfSine");
    case SpiralType::Parabola:         return QStringLiteral("Parabola");
    case SpiralType::CubicJPN:         return QStringLiteral("CubicJPN");
    case SpiralType::CubicECI:         return QStringLiteral("CubicECI");
    case SpiralType::Sinusoidal:       return QStringLiteral("Sinusoidal");
    case SpiralType::Cosine:           return QStringLiteral("Cosine");
    case SpiralType::Bloss:            return QStringLiteral("Bloss");
    case SpiralType::Lemniscate:       return QStringLiteral("Lemniscate");
    case SpiralType::WienerBogen:      return QStringLiteral("WienerBogen");
    case SpiralType::Radioid:          return QStringLiteral("Radioid");
    case SpiralType::ElasticRadioid:   return QStringLiteral("ElasticRadioid");
    case SpiralType::NorwichSturm:     return QStringLiteral("NorwichSturm");
    case SpiralType::PseudoEllipticRadioid: return QStringLiteral("PseudoEllipticRadioid");
    case SpiralType::Logarithmic:      return QStringLiteral("Logarithmic");
    case SpiralType::Hyperbolic:       return QStringLiteral("Hyperbolic");
    case SpiralType::Polynomial:       return QStringLiteral("Polynomial");
    case SpiralType::Quintic:          return QStringLiteral("Quintic");
    case SpiralType::PHQuintic:        return QStringLiteral("PHQuintic");
    case SpiralType::Biquadratic:      return QStringLiteral("Biquadratic");
    case SpiralType::Spline:           return QStringLiteral("Spline");
    case SpiralType::BlossEulerHybrid: return QStringLiteral("BlossEulerHybrid");
    default:                           return QStringLiteral("Clothoid");
    }
}
}

CompoundChainCalcDialog::CompoundChainCalcDialog(AlignmentDocument* doc, QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
{
    init();
}

CompoundChainCalcDialog::CompoundChainCalcDialog(AlignmentDocument* doc,
                                                  int presetEntryIdx, int presetExitIdx,
                                                  QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
{
    init();
    lockTangentCombos(presetEntryIdx, presetExitIdx);
}

// ────────────────────────────────────────────────────────────────────────────
//  init — 兩個建構子共用的 UI 建構
// ────────────────────────────────────────────────────────────────────────────

void CompoundChainCalcDialog::init()
{
    setWindowTitle(tr("Compound Chain Trial Calculation (S0 C0 S1 C1 ... Sn)"));
    resize(760, 600);

    auto* mainLayout = new QVBoxLayout(this);

    // ── 邊界切線選擇／螺線形式 ────────────────────────────────────────────
    auto* form = new QFormLayout();
    m_entryTangentCombo = new QComboBox(this);
    m_exitTangentCombo  = new QComboBox(this);
    form->addRow(tr("Entry tangent:"), m_entryTangentCombo);
    form->addRow(tr("Exit tangent:"),  m_exitTangentCombo);

    m_spiralTypeCombo = new QComboBox(this);
    for (SpiralType t : { SpiralType::Clothoid, SpiralType::HalfSine, SpiralType::Parabola,
                          SpiralType::CubicJPN, SpiralType::CubicECI,
                          SpiralType::Sinusoidal, SpiralType::Cosine, SpiralType::Bloss,
                          SpiralType::Lemniscate, SpiralType::WienerBogen, SpiralType::Radioid,
                          SpiralType::ElasticRadioid, SpiralType::NorwichSturm, SpiralType::PseudoEllipticRadioid,
                          SpiralType::Logarithmic, SpiralType::Hyperbolic, SpiralType::Polynomial,
                          SpiralType::Quintic, SpiralType::PHQuintic, SpiralType::Biquadratic, SpiralType::Spline,
                          SpiralType::BlossEulerHybrid })
        m_spiralTypeCombo->addItem(spiralTypeDisplayName(t), static_cast<int>(t));
    form->addRow(tr("Spiral form (all segments):"), m_spiralTypeCombo);

    m_arcCountSpin = new QSpinBox(this);
    m_arcCountSpin->setRange(kMinArcs, kMaxArcs);
    m_arcCountSpin->setValue(kMinArcs);
    form->addRow(tr("Arc count N (>=2):"), m_arcCountSpin);
    mainLayout->addLayout(form);

    // ── 輸入表格：N+1 列，欄位 [Lk][Rk][弧長 Dk]（最後一列 Rk/Dk 留空；───
    //    每段圓弧的最後一段 Dk 欄鎖定顯示 Auto，見 rebuildInputTable()） ──
    m_inputTable = new QTableWidget(this);
    m_inputTable->setColumnCount(4);
    m_inputTable->setHorizontalHeaderLabels(
        { tr("Segment"), tr("Spiral length Lk (0=omit)"), tr("Arc radius Rk"), tr("Arc length Dk") });
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
//  populateTangentCombos / lockTangentCombos
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

void CompoundChainCalcDialog::lockTangentCombos(int entryIdx, int exitIdx)
{
    const int entryPos = m_entryTangentCombo->findData(entryIdx);
    const int exitPos  = m_exitTangentCombo->findData(exitIdx);
    if (entryPos >= 0) m_entryTangentCombo->setCurrentIndex(entryPos);
    if (exitPos  >= 0) m_exitTangentCombo->setCurrentIndex(exitPos);

    // 鎖定：使用者已經在畫面上點選過切線，這裡只是唯讀顯示，避免與畫面
    // 上的高亮切線選取不一致。
    m_entryTangentCombo->setEnabled(false);
    m_exitTangentCombo->setEnabled(false);
    m_tangentsLocked = true;
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

    // 最後一段圓弧（arc index n-1，對應 row n-1）弧長由 solver 自動算出，
    // 使用者不需輸入（見標頭檔說明）。
    const int autoArcRow = n - 1;

    for (int row = 0; row <= n; ++row) {
        const QString label = (row == 0) ? tr("S0 (entry)")
            : (row == n) ? tr("S%1 (exit)").arg(row)
            : tr("S%1 (interior)").arg(row);
        m_inputTable->setItem(row, ColLabel, new QTableWidgetItem(label));
        m_inputTable->item(row, ColLabel)->setFlags(Qt::ItemIsEnabled);

        auto* lenItem = new QTableWidgetItem(QStringLiteral("0"));
        m_inputTable->setItem(row, ColSpiralLen, lenItem);

        if (row < n) {
            auto* radItem = new QTableWidgetItem(QStringLiteral("500"));
            m_inputTable->setItem(row, ColArcRadius, radItem);

            if (row == autoArcRow) {
                auto* autoItem = new QTableWidgetItem(tr("Auto"));
                autoItem->setFlags(Qt::ItemIsEnabled);
                m_inputTable->setItem(row, ColArcLen, autoItem);
            } else {
                auto* lenArcItem = new QTableWidgetItem(QStringLiteral(""));
                m_inputTable->setItem(row, ColArcLen, lenArcItem);
            }
        } else {
            auto* naItem = new QTableWidgetItem(QStringLiteral("—"));
            naItem->setFlags(Qt::ItemIsEnabled);
            m_inputTable->setItem(row, ColArcRadius, naItem);

            auto* naItem2 = new QTableWidgetItem(QStringLiteral("—"));
            naItem2->setFlags(Qt::ItemIsEnabled);
            m_inputTable->setItem(row, ColArcLen, naItem2);
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  readInputs
// ────────────────────────────────────────────────────────────────────────────

bool CompoundChainCalcDialog::readInputs(QVector<double>& outRadii, QVector<double>& outLens,
                                          QVector<double>& outArcAngles) const
{
    const int n = m_arcCountSpin->value();
    outLens.resize(n + 1);
    outRadii.resize(n);
    outArcAngles.fill(0.0, n);   // 0 = 交給 solver 自動算出（預設全部；下面依輸入覆寫前 n-1 段）

    const int autoArcRow = n - 1;   // 最後一段圓弧：弧長固定 Auto，不讀取此欄

    for (int row = 0; row <= n; ++row) {
        bool ok = false;
        const double len = m_inputTable->item(row, ColSpiralLen)->text().toDouble(&ok);
        if (!ok || len < 0.0) {
            m_statusLabel->setText(tr("Invalid spiral length at row %1 (must be >= 0).").arg(row));
            return false;
        }
        outLens[row] = len;

        if (row < n) {
            bool okR = false;
            const double r = m_inputTable->item(row, ColArcRadius)->text().toDouble(&okR);
            if (!okR || r <= 0.0) {
                m_statusLabel->setText(tr("Invalid arc radius at row %1 (must be > 0).").arg(row));
                return false;
            }
            outRadii[row] = r;

            // 最後一段圓弧（autoArcRow）弧長不讀取，維持 outArcAngles[row]==0.0
            // （由 solveCompoundChain() 用剩餘轉角自動算出）。
            if (row != autoArcRow) {
                bool okD = false;
                const double d = m_inputTable->item(row, ColArcLen)->text().toDouble(&okD);
                if (!okD || d <= 0.0) {
                    m_statusLabel->setText(
                        tr("Invalid arc length at row %1 (must be > 0; only the LAST arc's"
                           " length is automatic).").arg(row));
                    return false;
                }
                // 弧長 → 弧心角絕對值：angle = length / radius（見
                // AlignmentDocument.h CompoundChainSpec::ArcSeg::centralAngle）。
                outArcAngles[row] = d / r;
            }
        }
    }
    return true;
}

SpiralType CompoundChainCalcDialog::selectedSpiralType() const
{
    return static_cast<SpiralType>(m_spiralTypeCombo->currentData().toInt());
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

    QVector<double> radii, lens, arcAngles;
    if (!readInputs(radii, lens, arcAngles)) return;   // 訊息已由 readInputs 設定

    const auto& elems = m_doc->horizontal()->elements();
    if (tb < 0 || tb >= elems.size() || ta < 0 || ta >= elems.size()) {
        m_statusLabel->setText(tr("Invalid tangent selection."));
        return;
    }

    const int n = m_arcCountSpin->value();
    QVector<SpiralType> types(n + 1, selectedSpiralType());

    // arcAngles：前 n-1 段已由使用者輸入的弧長換算為弧心角（釘死值）；最後
    // 一段固定 0.0 = 交給 solveCompoundChain() 用剩餘轉角自動算出（見
    // readInputs() 說明；unknown 保持預設 None，不啟用反解機制）。
    const SolvedCompoundChain chain = AlignmentSolver::solveCompoundChain(
        radii, lens, types,
        elems[tb].startPI, elems[tb].endPI,
        elems[ta].startPI, elems[ta].endPI,
        arcAngles);

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

    QVector<double> radii, lens, arcAngles;
    if (!readInputs(radii, lens, arcAngles)) {
        QMessageBox::warning(this, tr("Apply failed"), m_statusLabel->text());
        return;
    }

    const SpiralType spiralType = selectedSpiralType();

    HorizontalAlignmentEdit::CompoundChainSpec spec;
    spec.arcs.resize(n);
    spec.spirals.resize(n + 1);
    for (int k = 0; k < n; ++k) {
        spec.arcs[k].radius = radii[k];
        // arcAngles[k] > 0 表示使用者已輸入該段弧長（釘死角度）；最後一段
        // 保持 0.0，交由 addCompoundChain()/solveCompoundChain() 依剩餘轉角
        // 自動算出（見 AlignmentDocument.h CompoundChainSpec::ArcSeg::
        // centralAngle 的說明：0 = 自動平分「剩餘」轉角——此處只留最後一段
        // 自動，等同把全部轉角分給那一段，而非在多段間平分）。
        spec.arcs[k].centralAngle = arcAngles[k];
    }
    for (int k = 0; k <= n; ++k) {
        spec.spirals[k].length = lens[k];
        spec.spirals[k].type   = spiralType;
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
