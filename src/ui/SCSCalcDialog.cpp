#include "ui/SCSCalcDialog.h"

#include "railway/AlignmentDocument.h"
#include "railway/AlignmentSolver.h"
#include "core/geometry/ProjectOrigin.h"

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
#include <QPointF>
#include <QSettings>
#include <QVector>
#include <QtMath>

using aicad::railway::AlignmentDocument;
using aicad::railway::HorizontalAlignmentEdit;
using aicad::railway::EditableElementType;
using aicad::railway::SpiralType;
using aicad::railway::AlignmentSolver;
using aicad::railway::SolvedSCS;

namespace aicad {
namespace ui {

namespace {

/** 與 CompoundChainCalcDialog.cpp / AlignmentSCSCommand.cpp 一致的顯示名稱。 */
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

void fillSpiralTypeCombo(QComboBox* combo)
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

/// 方位角（弧度，順時針由北）→ ddd°mm'ss.sss" 格式，不足位補零。
/// 與 AlignmentDataTableDialog.cpp 的同名函式（匿名 namespace，未匯出）
/// 保持一致的顯示格式；例：方位角 123.7524° → "123°45'08.640\""。
QString azimuthToDMS(double rad)
{
    double deg = qRadiansToDegrees(rad);
    while (deg <    0.0) deg += 360.0;
    while (deg >= 360.0) deg -= 360.0;
    const int    d = static_cast<int>(deg);
    const double rem1 = (deg - d) * 60.0;
    const int    m = static_cast<int>(rem1);
    const double s = (rem1 - m) * 60.0;
    return QString(u8"%1\u00B0%2'%3\"")
        .arg(d,  3, 10, QChar('0'))
        .arg(m,  2, 10, QChar('0'))
        .arg(s,  6, 'f', 3, QChar('0'));
}

/**
 * @brief 跨次開啟對話框的參數記憶 — 透過 QSettings 持久化到磁碟（如
 *        Windows 登錄檔 HKCU\Software\AICAD\AICAD，或 Linux 上的
 *        ~/.config/AICAD/AICAD.conf），效果等同 VBA 的
 *        GetSetting()/SaveSetting()：重開 AICAD 之後仍會回填上次的值，
 *        而且與任何 .aicad 檔案無關（不隨檔案存檔/載入、不同檔案間共用
 *        同一份記憶）。
 *
 * 只有使用者按「套用」成功（onApply() 內 addSCS() 成功）時才寫入，
 * 尚未完成或被取消的試算輸入不會覆蓋既有設定。entryIdx/exitIdx 只有在
 * 「未鎖定」建構子（使用者自行從下拉選單挑選切線）情境下才會被拿來
 * 回填；由 AlignmentSCSCommand 開啟的鎖定版對話框一律以當次點選的切線
 * 為準，不受此設定影響。
 */
struct LastSCSParams {
    bool       valid   = false;
    double     radius  = 500.0;
    double     l1      = 100.0;
    double     l2      = 100.0;
    SpiralType type1   = SpiralType::Clothoid;
    SpiralType type2   = SpiralType::Clothoid;
    int        entryIdx = -1;   ///< 上次套用時的入切線 element index（僅供未鎖定建構子回填）
    int        exitIdx  = -1;   ///< 上次套用時的出切線 element index（僅供未鎖定建構子回填）
};

const char* kSettingsOrg   = "AICAD";
const char* kSettingsApp   = "AICAD";
const char* kSettingsGroup = "SCSCalcDialog";

/// 讀取上次「套用」成功時的參數；找不到已存設定時回傳 valid=false，
/// 其餘欄位維持內建預設值（R=500, L1=L2=100, Clothoid, 無切線記憶）。
LastSCSParams loadLastParams()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.beginGroup(kSettingsGroup);
    LastSCSParams p;
    p.valid = settings.value(QStringLiteral("valid"), false).toBool();
    if (p.valid) {
        p.radius   = settings.value(QStringLiteral("radius"),   p.radius).toDouble();
        p.l1       = settings.value(QStringLiteral("l1"),       p.l1).toDouble();
        p.l2       = settings.value(QStringLiteral("l2"),       p.l2).toDouble();
        p.type1    = static_cast<SpiralType>(
            settings.value(QStringLiteral("type1"), static_cast<int>(p.type1)).toInt());
        p.type2    = static_cast<SpiralType>(
            settings.value(QStringLiteral("type2"), static_cast<int>(p.type2)).toInt());
        p.entryIdx = settings.value(QStringLiteral("entryIdx"), p.entryIdx).toInt();
        p.exitIdx  = settings.value(QStringLiteral("exitIdx"),  p.exitIdx).toInt();
    }
    settings.endGroup();
    return p;
}

/// 寫入本次「套用」成功的參數，供下次（包含重開 AICAD 之後）開啟本
/// 對話框時回填。
void saveLastParams(const LastSCSParams& p)
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.beginGroup(kSettingsGroup);
    settings.setValue(QStringLiteral("valid"),    true);
    settings.setValue(QStringLiteral("radius"),   p.radius);
    settings.setValue(QStringLiteral("l1"),       p.l1);
    settings.setValue(QStringLiteral("l2"),       p.l2);
    settings.setValue(QStringLiteral("type1"),    static_cast<int>(p.type1));
    settings.setValue(QStringLiteral("type2"),    static_cast<int>(p.type2));
    settings.setValue(QStringLiteral("entryIdx"), p.entryIdx);
    settings.setValue(QStringLiteral("exitIdx"),  p.exitIdx);
    settings.endGroup();
}

} // namespace

SCSCalcDialog::SCSCalcDialog(AlignmentDocument* doc, QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
{
    init();
}

SCSCalcDialog::SCSCalcDialog(AlignmentDocument* doc,
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

void SCSCalcDialog::init()
{
    setWindowTitle(tr("SCS Trial Calculation (TS-SC-CS-ST)"));
    resize(680, 560);

    const LastSCSParams last = loadLastParams();   // QSettings 回填（重開 AICAD 仍保留）

    auto* mainLayout = new QVBoxLayout(this);

    // ── 邊界切線選擇／R／L1／T1／L2／T2 ────────────────────────────────────
    auto* form = new QFormLayout();
    m_entryTangentCombo = new QComboBox(this);
    m_exitTangentCombo  = new QComboBox(this);
    form->addRow(tr("Entry tangent:"), m_entryTangentCombo);
    form->addRow(tr("Exit tangent:"),  m_exitTangentCombo);

    m_radiusSpin = new QDoubleSpinBox(this);
    m_radiusSpin->setRange(0.001, 1.0e7);
    m_radiusSpin->setDecimals(3);
    m_radiusSpin->setValue(last.radius);
    form->addRow(tr("Arc radius R:"), m_radiusSpin);

    m_l1Spin = new QDoubleSpinBox(this);
    m_l1Spin->setRange(0.0, 1.0e6);
    m_l1Spin->setDecimals(3);
    m_l1Spin->setValue(last.l1);
    form->addRow(tr("Entry spiral length L1 (0=omit):"), m_l1Spin);

    m_type1Combo = new QComboBox(this);
    fillSpiralTypeCombo(m_type1Combo);
    {
        const int pos = m_type1Combo->findData(static_cast<int>(last.type1));
        if (pos >= 0) m_type1Combo->setCurrentIndex(pos);
    }
    form->addRow(tr("Entry spiral form T1:"), m_type1Combo);

    m_l2Spin = new QDoubleSpinBox(this);
    m_l2Spin->setRange(0.0, 1.0e6);
    m_l2Spin->setDecimals(3);
    m_l2Spin->setValue(last.l2);
    form->addRow(tr("Exit spiral length L2 (0=omit):"), m_l2Spin);

    m_type2Combo = new QComboBox(this);
    fillSpiralTypeCombo(m_type2Combo);
    {
        const int pos = m_type2Combo->findData(static_cast<int>(last.type2));
        if (pos >= 0) m_type2Combo->setCurrentIndex(pos);
    }
    form->addRow(tr("Exit spiral form T2:"), m_type2Combo);

    mainLayout->addLayout(form);

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
        { tr("Point"), tr("Easting (TM2)"), tr("Northing (TM2)"), tr("Azimuth"), tr("Next seg. length"), tr("Radius") });
    m_resultTable->horizontalHeader()->setStretchLastSection(true);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(new QLabel(tr("Trial result (node sequence):"), this));
    mainLayout->addWidget(m_resultTable);

    connect(m_calcButton,  &QPushButton::clicked, this, &SCSCalcDialog::onCalculate);
    connect(m_applyButton, &QPushButton::clicked, this, &SCSCalcDialog::onApply);

    // 任一參數變更後，先前的試算結果視為失效，需重新按「試算」。
    auto invalidate = [this]() {
        m_lastCalcValid = false;
        m_applyButton->setEnabled(false);
    };
    connect(m_radiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, invalidate);
    connect(m_l1Spin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, invalidate);
    connect(m_l2Spin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, invalidate);
    connect(m_type1Combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, invalidate);
    connect(m_type2Combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, invalidate);

    populateTangentCombos();
}

// ────────────────────────────────────────────────────────────────────────────
//  populateTangentCombos / lockTangentCombos
// ────────────────────────────────────────────────────────────────────────────

void SCSCalcDialog::populateTangentCombos()
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

    // 未鎖定（未預選）情況下，優先回填上次「套用」成功時的入/出切線；
    // 若該 index 在目前線形中已不存在（例如線形被重新編輯過），則退回
    // 舊有的預設行為（entry=第一項、exit=第二項）。鎖定版建構子稍後會
    // 呼叫 lockTangentCombos() 覆蓋此處的選取，故不影響 AlignmentSCSCommand
    // 的既有流程。
    const LastSCSParams last = loadLastParams();
    const int lastEntryPos = m_entryTangentCombo->findData(last.entryIdx);
    const int lastExitPos  = m_exitTangentCombo->findData(last.exitIdx);
    if (last.valid && lastEntryPos >= 0 && lastExitPos >= 0 && lastEntryPos != lastExitPos) {
        m_entryTangentCombo->setCurrentIndex(lastEntryPos);
        m_exitTangentCombo->setCurrentIndex(lastExitPos);
    } else if (m_exitTangentCombo->count() > 1) {
        m_exitTangentCombo->setCurrentIndex(1);
    }
}

void SCSCalcDialog::lockTangentCombos(int entryIdx, int exitIdx)
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
//  selectedType1 / selectedType2
// ────────────────────────────────────────────────────────────────────────────

SpiralType SCSCalcDialog::selectedType1() const
{
    return static_cast<SpiralType>(m_type1Combo->currentData().toInt());
}

SpiralType SCSCalcDialog::selectedType2() const
{
    return static_cast<SpiralType>(m_type2Combo->currentData().toInt());
}

// ────────────────────────────────────────────────────────────────────────────
//  onCalculate
// ────────────────────────────────────────────────────────────────────────────

void SCSCalcDialog::onCalculate()
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

    const auto& elems = m_doc->horizontal()->elements();
    if (tb < 0 || tb >= elems.size() || ta < 0 || ta >= elems.size()) {
        m_statusLabel->setText(tr("Invalid tangent selection."));
        return;
    }

    // 確保 ProjectOrigin 已設定（若尚未設定，套用預設 TM2 origin），這樣
    // 下方 toGlobal() 轉換才能得到合理量級的 TM2 座標（見 ProjectOrigin.h
    // / AlignmentDataTableDialog.cpp populateHorizontalTable() 的同一作法）。
    aicad::core::geometry::ProjectOrigin::ensureDefault();

    const double radius = m_radiusSpin->value();
    const double l1 = m_l1Spin->value();
    const double l2 = m_l2Spin->value();
    if (radius <= 0.0) {
        m_statusLabel->setText(tr("Arc radius must be > 0."));
        return;
    }

    const SolvedSCS chain = AlignmentSolver::solveSCS(
        radius, l1, l2, selectedType1(), selectedType2(),
        elems[tb].startPI, elems[tb].endPI,
        elems[ta].startPI, elems[ta].endPI);

    if (!chain.valid) {
        m_statusLabel->setText(tr("Calculation failed — spirals may overlap or the turning"
                                   " angle is inconsistent with the given radius/lengths."
                                   " Try a shorter spiral or check tangent selection."));
        return;
    }

    // ── 顯示節點序列：TS → SC → CS → ST ─────────────────────────────────
    struct Row { QString label; QPointF pt; double az; double nextLen; double radius; };
    const QVector<Row> rows = {
        { tr("TS"), chain.tsPoint, chain.azTS, chain.Ls1,    0.0 },
        { tr("SC"), chain.scPoint, chain.azSC, chain.arcLen, chain.R },
        { tr("CS"), chain.csPoint, chain.azCS, chain.Ls2,    0.0 },
        { tr("ST"), chain.stPoint, chain.azST, 0.0,          0.0 },
    };

    m_resultTable->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        // Local(CAD) → Global(TM2) 顯示；架構原則見 ProjectOrigin.h：OCCT/
        // solver 內部只用 Local 座標，TM2 大數值只在顯示文字這個邊界出現。
        const QPointF tm2 = aicad::core::geometry::ProjectOrigin::instance()
                                 .toGlobal(row.pt.x(), row.pt.y());
        m_resultTable->setItem(i, 0, new QTableWidgetItem(row.label));
        m_resultTable->setItem(i, 1, new QTableWidgetItem(QString::number(tm2.x(), 'f', 4)));
        m_resultTable->setItem(i, 2, new QTableWidgetItem(QString::number(tm2.y(), 'f', 4)));
        m_resultTable->setItem(i, 3, new QTableWidgetItem(azimuthToDMS(row.az)));
        m_resultTable->setItem(i, 4, new QTableWidgetItem(QString::number(row.nextLen, 'f', 3)));
        m_resultTable->setItem(i, 5, new QTableWidgetItem(QString::number(row.radius, 'f', 3)));
    }

    const char* curveTag =
        (l1 < 1e-9 && l2 < 1e-9) ? "AFC" :
            (l1 < 1e-9)               ? "CS"  :
            (l2 < 1e-9)               ? "SC"  : "SCS";

    m_statusLabel->setText(tr("Calculation succeeded (%1, delta=%2\xC2\xB0). Review the result, then click Apply.")
                               .arg(curveTag)
                               .arg(qRadiansToDegrees(chain.delta), 0, 'f', 4));
    m_lastCalcValid = true;
    m_applyButton->setEnabled(true);
}

// ────────────────────────────────────────────────────────────────────────────
//  onApply
// ────────────────────────────────────────────────────────────────────────────

void SCSCalcDialog::onApply()
{
    if (!m_lastCalcValid || !m_doc || !m_doc->horizontal()) return;

    const int tb = m_entryTangentCombo->currentData().toInt();
    const int ta = m_exitTangentCombo->currentData().toInt();

    const double radius = m_radiusSpin->value();
    const double l1 = m_l1Spin->value();
    const double l2 = m_l2Spin->value();

    const int idx = m_doc->horizontal()->addSCS(
        tb, ta, radius, l1, l2, selectedType1(), selectedType2());
    if (idx < 0) {
        QMessageBox::warning(this, tr("Apply failed"),
                              tr("addSCS() failed — check tangent selection."));
        return;
    }

    m_doc->horizontal()->solve();

    // 記住這次成功套用的參數，供下次開啟對話框回填（QSettings 持久化，
    // 重開 AICAD 仍保留 — 見 loadLastParams()/saveLastParams()）。
    saveLastParams({ true, radius, l1, l2, selectedType1(), selectedType2(), tb, ta });

    Q_EMIT scsApplied();
    accept();
}

} // namespace ui
} // namespace aicad
