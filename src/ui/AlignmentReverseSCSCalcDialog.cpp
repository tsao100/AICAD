#include "ui/AlignmentReverseSCSCalcDialog.h"

#include "railway/AlignmentDocument.h"
#include "railway/AlignmentSolver.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QSettings>
#include <QtMath>

using aicad::railway::AlignmentDocument;
using aicad::railway::HorizontalAlignmentEdit;
using aicad::railway::EditableElementType;
using aicad::railway::SpiralType;
using aicad::railway::AlignmentSolver;
using aicad::railway::SolvedReverseSCS;
using aicad::railway::ReverseSCSSpec;

namespace aicad {
namespace ui {

namespace {

// ── QSettings 落地位置／機碼（比照 ExportAlignmentCommand.cpp、
//    BasicCommands.cpp 既有慣例：("AICAD","AICAD")；Windows 上等同
//    VBA SaveSetting()/GetSetting() 落地在 HKEY_CURRENT_USER\Software\
//    AICAD\AICAD 底下）───────────────────────────────────────────────────
const char* kSettingsOrg = "AICAD";
const char* kSettingsApp = "AICAD";
const char* kKeyRadius1  = "railway/rscs/radius1";
const char* kKeyLength1  = "railway/rscs/length1";
const char* kKeyType1    = "railway/rscs/type1";
const char* kKeyRadius2  = "railway/rscs/radius2";
const char* kKeyLength2  = "railway/rscs/length2";
const char* kKeyType2    = "railway/rscs/type2";
const char* kKeyTypeM    = "railway/rscs/typeM";

/** 與 AlignmentSCSCommand.cpp / CompoundChainCalcDialog.cpp 一致的顯示名稱。 */
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

} // namespace

AlignmentReverseSCSCalcDialog::AlignmentReverseSCSCalcDialog(AlignmentDocument* doc, QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
{
    init();
}

AlignmentReverseSCSCalcDialog::AlignmentReverseSCSCalcDialog(AlignmentDocument* doc,
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

void AlignmentReverseSCSCalcDialog::addSpiralTypeItems(QComboBox* combo) const
{
    for (SpiralType t : { SpiralType::Clothoid, SpiralType::HalfSine, SpiralType::Parabola,
                          SpiralType::CubicJPN, SpiralType::CubicECI,
                          SpiralType::Sinusoidal, SpiralType::Cosine, SpiralType::Bloss,
                          SpiralType::Lemniscate, SpiralType::WienerBogen, SpiralType::Radioid,
                          SpiralType::ElasticRadioid, SpiralType::NorwichSturm, SpiralType::PseudoEllipticRadioid,
                          SpiralType::Logarithmic, SpiralType::Hyperbolic, SpiralType::Polynomial,
                          SpiralType::Quintic, SpiralType::PHQuintic, SpiralType::Biquadratic, SpiralType::Spline,
                          SpiralType::BlossEulerHybrid })
        combo->addItem(spiralTypeDisplayName(t), static_cast<int>(t));
}

void AlignmentReverseSCSCalcDialog::init()
{
    setWindowTitle(tr("Reverse SCS+SCS Trial Calculation (RSCS)"));
    resize(720, 560);

    auto* mainLayout = new QVBoxLayout(this);

    // ── 邊界切線選擇 ─────────────────────────────────────────────────────
    auto* tanForm = new QFormLayout();
    m_entryTangentCombo = new QComboBox(this);
    m_exitTangentCombo  = new QComboBox(this);
    tanForm->addRow(tr("Entry tangent:"), m_entryTangentCombo);
    tanForm->addRow(tr("Exit tangent:"),  m_exitTangentCombo);
    mainLayout->addLayout(tanForm);

    // ── Arc1 側（入螺旋 L1 → Arc1 R1）─────────────────────────────────────
    auto* arc1Form = new QFormLayout();
    m_radius1Spin = new QDoubleSpinBox(this);
    m_radius1Spin->setRange(0.001, 1.0e7);
    m_radius1Spin->setDecimals(3);
    m_radius1Spin->setValue(600.0);
    arc1Form->addRow(tr("R1 (Arc1 radius):"), m_radius1Spin);

    m_length1Spin = new QDoubleSpinBox(this);
    m_length1Spin->setRange(0.0, 1.0e6);
    m_length1Spin->setDecimals(3);
    m_length1Spin->setValue(100.0);
    arc1Form->addRow(tr("L1 (entry spiral length, 0=omit):"), m_length1Spin);

    m_type1Combo = new QComboBox(this);
    addSpiralTypeItems(m_type1Combo);
    arc1Form->addRow(tr("T1 (entry spiral type):"), m_type1Combo);
    mainLayout->addWidget(new QLabel(tr("<b>Entry side (Tangent \u2192 Spiral(L1) \u2192 Arc1(R1))</b>"), this));
    mainLayout->addLayout(arc1Form);

    // ── Arc2 側（Arc2 R2 → 出螺旋 L2）─────────────────────────────────────
    auto* arc2Form = new QFormLayout();
    m_radius2Spin = new QDoubleSpinBox(this);
    m_radius2Spin->setRange(0.001, 1.0e7);
    m_radius2Spin->setDecimals(3);
    m_radius2Spin->setValue(600.0);
    arc2Form->addRow(tr("R2 (Arc2 radius):"), m_radius2Spin);

    m_length2Spin = new QDoubleSpinBox(this);
    m_length2Spin->setRange(0.0, 1.0e6);
    m_length2Spin->setDecimals(3);
    m_length2Spin->setValue(100.0);
    arc2Form->addRow(tr("L2 (exit spiral length, 0=omit):"), m_length2Spin);

    m_type2Combo = new QComboBox(this);
    addSpiralTypeItems(m_type2Combo);
    arc2Form->addRow(tr("T2 (exit spiral type):"), m_type2Combo);
    mainLayout->addWidget(new QLabel(tr("<b>Exit side (Arc2(R2) \u2192 Spiral(L2) \u2192 Tangent)</b>"), this));
    mainLayout->addLayout(arc2Form);

    // ── 中間反向對（Lm1/Lm2，EqualLength，長度自動反解）───────────────────
    auto* midForm = new QFormLayout();
    m_typeMCombo = new QComboBox(this);
    addSpiralTypeItems(m_typeMCombo);
    midForm->addRow(tr("TM (reverse-pair spiral type, Lm1=Lm2 auto-solved):"), m_typeMCombo);
    mainLayout->addWidget(new QLabel(
        tr("<b>Reverse pair (Arc1 \u2194 Arc2, EqualLength \u2014 length auto-solved)</b>"), this));
    mainLayout->addLayout(midForm);

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

    connect(m_calcButton,  &QPushButton::clicked, this, &AlignmentReverseSCSCalcDialog::onCalculate);
    connect(m_applyButton, &QPushButton::clicked, this, &AlignmentReverseSCSCalcDialog::onApply);

    populateTangentCombos();
    loadLastSettings();   // 覆蓋上面硬編碼的 600.0/100.0/Clothoid 預設值
}

// ────────────────────────────────────────────────────────────────────────────
//  populateTangentCombos / lockTangentCombos
// ────────────────────────────────────────────────────────────────────────────

void AlignmentReverseSCSCalcDialog::populateTangentCombos()
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

void AlignmentReverseSCSCalcDialog::lockTangentCombos(int entryIdx, int exitIdx)
{
    const int entryPos = m_entryTangentCombo->findData(entryIdx);
    const int exitPos  = m_exitTangentCombo->findData(exitIdx);
    if (entryPos >= 0) m_entryTangentCombo->setCurrentIndex(entryPos);
    if (exitPos  >= 0) m_exitTangentCombo->setCurrentIndex(exitPos);

    // 鎖定：使用者已經在畫面上點選過切線，這裡只是唯讀顯示，避免與畫面
    // 上的高亮切線選取不一致（比照 CompoundChainCalcDialog::lockTangentCombos()）。
    m_entryTangentCombo->setEnabled(false);
    m_exitTangentCombo->setEnabled(false);
    m_tangentsLocked = true;
}

// ────────────────────────────────────────────────────────────────────────────
//  loadLastSettings / saveLastSettings  (GetSetting/SaveSetting 風格)
// ────────────────────────────────────────────────────────────────────────────

void AlignmentReverseSCSCalcDialog::loadLastSettings()
{
    QSettings settings(kSettingsOrg, kSettingsApp);

    // QSettings::value() 的第二參數是「找不到機碼時的預設值」，直接傳入
    // spin box/combo 目前已有的硬編碼值即可：第一次使用（尚無任何記錄）
    // 時整段等於no-op，行為與修改前完全一致。
    m_radius1Spin->setValue(settings.value(kKeyRadius1, m_radius1Spin->value()).toDouble());
    m_length1Spin->setValue(settings.value(kKeyLength1, m_length1Spin->value()).toDouble());
    m_radius2Spin->setValue(settings.value(kKeyRadius2, m_radius2Spin->value()).toDouble());
    m_length2Spin->setValue(settings.value(kKeyLength2, m_length2Spin->value()).toDouble());

    auto restoreTypeCombo = [&settings](QComboBox* combo, const char* key) {
        if (!settings.contains(key)) return;   // 找不到機碼 → 保留目前選項不動
        const int typeInt = settings.value(key).toInt();
        const int pos = combo->findData(typeInt);
        if (pos >= 0) combo->setCurrentIndex(pos);
    };
    restoreTypeCombo(m_type1Combo, kKeyType1);
    restoreTypeCombo(m_type2Combo, kKeyType2);
    restoreTypeCombo(m_typeMCombo, kKeyTypeM);
}

void AlignmentReverseSCSCalcDialog::saveLastSettings() const
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(kKeyRadius1, m_radius1Spin->value());
    settings.setValue(kKeyLength1, m_length1Spin->value());
    settings.setValue(kKeyType1,   m_type1Combo->currentData().toInt());
    settings.setValue(kKeyRadius2, m_radius2Spin->value());
    settings.setValue(kKeyLength2, m_length2Spin->value());
    settings.setValue(kKeyType2,   m_type2Combo->currentData().toInt());
    settings.setValue(kKeyTypeM,   m_typeMCombo->currentData().toInt());
}

// ────────────────────────────────────────────────────────────────────────────
//  readInputs
// ────────────────────────────────────────────────────────────────────────────

bool AlignmentReverseSCSCalcDialog::readInputs(double& r1, double& l1, SpiralType& t1,
                                                double& r2, double& l2, SpiralType& t2,
                                                SpiralType& tm, QString& errorOut) const
{
    r1 = m_radius1Spin->value();
    l1 = m_length1Spin->value();
    r2 = m_radius2Spin->value();
    l2 = m_length2Spin->value();

    if (r1 <= 0.0) { errorOut = tr("R1 must be > 0."); return false; }
    if (r2 <= 0.0) { errorOut = tr("R2 must be > 0."); return false; }
    if (l1 < 0.0)  { errorOut = tr("L1 must be >= 0."); return false; }
    if (l2 < 0.0)  { errorOut = tr("L2 must be >= 0."); return false; }

    t1 = static_cast<SpiralType>(m_type1Combo->currentData().toInt());
    t2 = static_cast<SpiralType>(m_type2Combo->currentData().toInt());
    tm = static_cast<SpiralType>(m_typeMCombo->currentData().toInt());
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
//  onCalculate
// ────────────────────────────────────────────────────────────────────────────

void AlignmentReverseSCSCalcDialog::onCalculate()
{
    m_lastCalcValid = false;
    m_applyButton->setEnabled(false);
    m_resultTable->setRowCount(0);

    // 不管等下算不算得出反向曲線，先把使用者目前輸入的 R1/L1/T1/R2/L2/T2/TM
    // 存起來——下次打開對話框才帶得回「上次試過的值」，而不是只記得上次
    // 「成功套用」的值（原本只在 onApply() 成功時存，等於試算失敗、使用者
    // 直接關掉對話框重來的那些輸入就白打了）。
    saveLastSettings();

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

    double r1, l1, r2, l2;
    SpiralType t1, t2, tm;
    QString err;
    if (!readInputs(r1, l1, t1, r2, l2, t2, tm, err)) {
        m_statusLabel->setText(err);
        return;
    }

    const auto& elems = m_doc->horizontal()->elements();
    if (tb < 0 || tb >= elems.size() || ta < 0 || ta >= elems.size()) {
        m_statusLabel->setText(tr("Invalid tangent selection."));
        return;
    }

    const SolvedReverseSCS rscs = AlignmentSolver::solveReverseSCS(
        r1, l1, t1, r2, l2, t2, tm, tm,
        elems[tb].startPI, elems[tb].endPI,
        elems[ta].startPI, elems[ta].endPI);

    if (!rscs.valid) {
        // rscs.failureReason 已經內嵌具體數據（兩弧圓心座標、圓心距離、
        // R1+R2、|R1-R2|、掃描上限），見 AlignmentSolver::solveReverseSCS()
        // 的組訊邏輯；這裡直接顯示，不用自己重算或用籠統說法帶過。
        m_statusLabel->setText(
            tr("Calculation failed.\n\n%1").arg(rscs.failureReason));

        // Arc1/Arc2 的入口/出口幾何不論成功失敗都有算出來（見
        // solveReverseSCS() 的欄位說明），把這幾個點列出來，讓使用者
        // 至少看得到兩弧實際落在哪裡、差多遠搭不起來，而不是只看到一段
        // 文字說明。
        struct FailRow { QString label; QPointF pt; double az; double radius; };
        QVector<FailRow> failRows;
        const double az1 = AlignmentSolver::azimuthOf(elems[tb].startPI, elems[tb].endPI);
        const double az2 = AlignmentSolver::azimuthOf(elems[ta].startPI, elems[ta].endPI);
        failRows.append({ tr("TS"),          elems[tb].endPI,   az1,           r1 });
        failRows.append({ tr("Arc1 start"),  rscs.arc1Pc,       rscs.azArc1Pc, r1 });
        failRows.append({ tr("Arc1 center"), rscs.arc1Center,   0.0,           r1 });
        failRows.append({ tr("Arc2 center"), rscs.arc2Center,   0.0,           r2 });
        failRows.append({ tr("Arc2 end"),    rscs.arc2Pt,       rscs.azArc2Pt, r2 });
        failRows.append({ tr("ST"),          elems[ta].startPI, az2,           r2 });

        m_resultTable->setRowCount(failRows.size());
        for (int i = 0; i < failRows.size(); ++i) {
            const FailRow& row = failRows[i];
            m_resultTable->setItem(i, 0, new QTableWidgetItem(row.label));
            m_resultTable->setItem(i, 1, new QTableWidgetItem(QString::number(row.pt.x(), 'f', 4)));
            m_resultTable->setItem(i, 2, new QTableWidgetItem(QString::number(row.pt.y(), 'f', 4)));
            m_resultTable->setItem(i, 3, new QTableWidgetItem(
                QString::number(qRadiansToDegrees(row.az), 'f', 4) + QStringLiteral("deg")));
            m_resultTable->setItem(i, 4, new QTableWidgetItem(QStringLiteral("\u2014")));
            m_resultTable->setItem(i, 5, new QTableWidgetItem(QString::number(row.radius, 'f', 3)));
        }
        return;
    }

    // ── 顯示節點序列：TS(tan1) \u2192 SC(Arc1) \u2192 CS(Lm1) \u2192 junction
    //    \u2192 SC(Arc2) \u2192 CS(tan2 side) \u2192 ST(tan2) ────────────────
    struct Row { QString label; QPointF pt; double az; double nextLen; double radius; };
    QVector<Row> rows;

    const double az1 = AlignmentSolver::azimuthOf(elems[tb].startPI, elems[tb].endPI);
    const double az2 = AlignmentSolver::azimuthOf(elems[ta].startPI, elems[ta].endPI);

    rows.append({ tr("TS"),  elems[tb].endPI,     az1,                    l1,        r1 });
    rows.append({ tr("SC"),  rscs.arc1Pc,          rscs.azArc1Pc,          rscs.arc1Len, r1 });
    rows.append({ tr("CS"),  rscs.arc1TrimPoint,   rscs.azArc1Trim,        rscs.Lm1,  r1 });
    rows.append({ tr("PRC"), rscs.junction,        rscs.junctionAzimuth,   rscs.Lm2,  r2 });
    rows.append({ tr("SC"),  rscs.arc2TrimPoint,   rscs.azArc2Trim,        rscs.arc2Len, r2 });
    rows.append({ tr("CS"),  rscs.arc2Pt,          rscs.azArc2Pt,          l2,        r2 });
    rows.append({ tr("ST"),  elems[ta].startPI,    az2,                    0.0,       0.0 });

    m_resultTable->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        m_resultTable->setItem(i, 0, new QTableWidgetItem(row.label));
        m_resultTable->setItem(i, 1, new QTableWidgetItem(QString::number(row.pt.x(), 'f', 4)));
        m_resultTable->setItem(i, 2, new QTableWidgetItem(QString::number(row.pt.y(), 'f', 4)));
        m_resultTable->setItem(i, 3, new QTableWidgetItem(
            QString::number(qRadiansToDegrees(row.az), 'f', 4) + QStringLiteral("\xC2\xB0")));
        m_resultTable->setItem(i, 4, new QTableWidgetItem(QString::number(row.nextLen, 'f', 3)));
        m_resultTable->setItem(i, 5, new QTableWidgetItem(QString::number(row.radius, 'f', 3)));
    }

    m_statusLabel->setText(
        tr("Calculation succeeded.  Lm1 = Lm2 = %1 m (EqualLength).  Review the result, then click Apply.")
            .arg(rscs.Lm1, 0, 'f', 3));
    m_lastCalcValid = true;
    m_applyButton->setEnabled(true);
}

// ────────────────────────────────────────────────────────────────────────────
//  onApply
// ────────────────────────────────────────────────────────────────────────────

void AlignmentReverseSCSCalcDialog::onApply()
{
    if (!m_lastCalcValid || !m_doc || !m_doc->horizontal()) return;

    const int tb = m_entryTangentCombo->currentData().toInt();
    const int ta = m_exitTangentCombo->currentData().toInt();

    double r1, l1, r2, l2;
    SpiralType t1, t2, tm;
    QString err;
    if (!readInputs(r1, l1, t1, r2, l2, t2, tm, err)) {
        QMessageBox::warning(this, tr("Apply failed"), err);
        return;
    }

    ReverseSCSSpec spec;
    spec.tangentIdxBefore = tb;
    spec.tangentIdxAfter  = ta;
    spec.radius1 = r1;
    spec.length1 = l1;
    spec.radius2 = r2;
    spec.length2 = l2;
    spec.type1  = t1;
    spec.type2  = t2;
    spec.typeM1 = tm;
    spec.typeM2 = tm;

    const int idx = m_doc->horizontal()->addReverseSCS(spec);
    if (idx < 0) {
        QMessageBox::warning(this, tr("Apply failed"),
                              tr("addReverseSCS() failed \u2014 check tangent selection and radii."));
        return;
    }

    m_doc->horizontal()->solve();
    saveLastSettings();   // 已在 onCalculate() 存過一次；這裡再存一次是為了
                          // 涵蓋「算完後又手動改了欄位、沒重按試算就直接
                          // 套用」的情況，確保存到的是實際套用的那組值。
    Q_EMIT curveApplied();
    accept();
}

} // namespace ui
} // namespace aicad
