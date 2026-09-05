#include "ui/AddSpiralCalcDialog.h"

#include "railway/AlignmentDocument.h"
#include "railway/AlignmentSolver.h"

#include <QComboBox>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QSettings>
#include <QtMath>
#include <cmath>
#include <limits>

using aicad::railway::AlignmentDocument;
using aicad::railway::HorizontalAlignmentEdit;
using aicad::railway::SpiralType;
using aicad::railway::AlignmentSolver;
using aicad::railway::SolvedLC;
using aicad::railway::SolvedCA;
using aicad::railway::SolvedACA;

namespace aicad {
namespace ui {

namespace {

/** 與 AlignmentAddSpiralCommand::spiralTypeName() / CompoundChainCalcDialog
 *  的 spiralTypeDisplayName() 一致的顯示名稱（三處各自獨立維護同一份對照
 *  表，屬於刻意的小範圍重複——見 AlignmentAddSpiralCommand.cpp 開頭的
 *  「窄範圍修正」慣例，避免為此新增跨模組共用標頭的額外耦合）。 */
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

/** spiralTypeDisplayName() 的反查（用於還原上次記錄的選取，見
 *  AddSpiralCalcDialog::restoreSettings()）。未知/空字串一律回傳
 *  SpiralType::Clothoid（下拉選單的預設第一項），與 QSettings 找不到
 *  對應鍵值時「維持預設」的行為一致，不需要另外判斷「有沒有存過」。 */
SpiralType spiralTypeFromDisplayName(const QString& name)
{
    if (name == "HalfSine")              return SpiralType::HalfSine;
    if (name == "Parabola")              return SpiralType::Parabola;
    if (name == "CubicJPN")              return SpiralType::CubicJPN;
    if (name == "CubicECI")              return SpiralType::CubicECI;
    if (name == "Sinusoidal")            return SpiralType::Sinusoidal;
    if (name == "Cosine")                return SpiralType::Cosine;
    if (name == "Bloss")                 return SpiralType::Bloss;
    if (name == "Lemniscate")            return SpiralType::Lemniscate;
    if (name == "WienerBogen")           return SpiralType::WienerBogen;
    if (name == "Radioid")               return SpiralType::Radioid;
    if (name == "ElasticRadioid")        return SpiralType::ElasticRadioid;
    if (name == "NorwichSturm")          return SpiralType::NorwichSturm;
    if (name == "PseudoEllipticRadioid") return SpiralType::PseudoEllipticRadioid;
    if (name == "Logarithmic")           return SpiralType::Logarithmic;
    if (name == "Hyperbolic")            return SpiralType::Hyperbolic;
    if (name == "Polynomial")            return SpiralType::Polynomial;
    if (name == "Quintic")               return SpiralType::Quintic;
    if (name == "PHQuintic")             return SpiralType::PHQuintic;
    if (name == "Biquadratic")           return SpiralType::Biquadratic;
    if (name == "Spline")                return SpiralType::Spline;
    if (name == "BlossEulerHybrid")      return SpiralType::BlossEulerHybrid;
    return SpiralType::Clothoid;
}

// ────────────────────────────────────────────────────────────────────────────
//  持久化選取（比照 VBA GetSetting/SaveSetting；與專案既有慣例一致，見
//  ImportAlignmentCommand.cpp／ExportAlignmentCommand.cpp／BasicCommands.cpp
//  的 QSettings("AICAD", "AICAD") 用法）。三個群組方向（LC/CA/ACA）各自
//  記一把 key，因為使用者慣用的螺旋線類型常隨情境（例如 CC 轉場多半偏好
//  Bloss/Radioid 這類凹凸曲率斜坡，LC/CA 直線銜接則常用 Clothoid）而不同。
// ────────────────────────────────────────────────────────────────────────────

const char* kSettingsOrg = "AICAD";
const char* kSettingsApp = "AICAD";

QString spiralTypeSettingsKey(AddSpiralCalcDialog::GroupMode mode)
{
    switch (mode) {
    case AddSpiralCalcDialog::GroupMode::LC:  return QStringLiteral("AddSpiralCalcDialog/spiralType_LC");
    case AddSpiralCalcDialog::GroupMode::CA:  return QStringLiteral("AddSpiralCalcDialog/spiralType_CA");
    case AddSpiralCalcDialog::GroupMode::ACA: return QStringLiteral("AddSpiralCalcDialog/spiralType_ACA");
    }
    return QStringLiteral("AddSpiralCalcDialog/spiralType_LC"); // 不會發生，保底
}

// ────────────────────────────────────────────────────────────────────────────
//  arcAzimuthAtPC / arcAzimuthAtPT — 與 AlignmentAddSpiralCommand.cpp 完全
//  相同的實作（同名 static 檔案內部函式，刻意複製而非共用標頭，見該檔開頭
//  的說明：必須同時用圓弧起訖兩個真實已知點的半徑向量外積，才能無歧異地
//  判斷這段圓弧的實際轉向，不管圓弧本身順逆時針恆成立）。
// ────────────────────────────────────────────────────────────────────────────

double arcAzimuthAtPC(QPointF pc, QPointF pt, QPointF center, double* outSign = nullptr)
{
    const QPointF r1 = pc - center;
    const QPointF r2 = pt - center;
    const double  crossVal = r1.x() * r2.y() - r1.y() * r2.x();
    const double  sign     = (crossVal >= 0.0) ? 1.0 : -1.0;
    if (outSign) *outSign = sign;
    return std::atan2(sign * (-r1.y()), sign * r1.x());
}

double arcAzimuthAtPT(QPointF pc, QPointF pt, QPointF center)
{
    double sign = 1.0;
    const double azPC = arcAzimuthAtPC(pc, pt, center, &sign);
    const QPointF r1 = pc - center;
    const QPointF r2 = pt - center;
    const double crossVal = r1.x() * r2.y() - r1.y() * r2.x();
    const double dotVal   = r1.x() * r2.x() + r1.y() * r2.y();
    const double sweep    = std::abs(std::atan2(crossVal, dotVal));
    return azPC + sign * sweep;
}

/** 塞入一列 Key/Value 到試算結果表格。 */
void addRow(QTableWidget* table, const QString& key, const QString& value)
{
    const int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(key));
    table->setItem(row, 1, new QTableWidgetItem(value));
}

QString fmtPt(const QPointF& p)
{
    return QString("(%1, %2)").arg(p.x(), 0, 'f', 3).arg(p.y(), 0, 'f', 3);
}

QString fmtDeg(double rad)
{
    return QString::number(qRadiansToDegrees(rad), 'f', 4) + QStringLiteral("\xC2\xB0");
}

} // anonymous namespace

// ────────────────────────────────────────────────────────────────────────────
//  Constructor / init
// ────────────────────────────────────────────────────────────────────────────

AddSpiralCalcDialog::AddSpiralCalcDialog(AlignmentDocument* doc, GroupMode mode,
                                         int tangentIdx, int arcIdx, int arc2Idx,
                                         QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
    , m_mode(mode)
    , m_tangentIdx(tangentIdx)
    , m_arcIdx(arcIdx)
    , m_arc2Idx(arc2Idx)
{
    init();
}

QString AddSpiralCalcDialog::modeLabelText() const
{
    switch (m_mode) {
    case GroupMode::LC:
        return tr("LC (Line \xE2\x86\x92 Clothoid \xE2\x86\x92 Arc)   Tangent #%1  \xE2\x86\x92  Arc #%2")
            .arg(m_tangentIdx).arg(m_arcIdx);
    case GroupMode::CA:
        return tr("CA (Arc \xE2\x86\x92 Clothoid \xE2\x86\x92 Line)   Arc #%1  \xE2\x86\x92  Tangent #%2")
            .arg(m_arcIdx).arg(m_tangentIdx);
    case GroupMode::ACA:
        return tr("ACA (Arc\xE2\x82\x81 \xE2\x86\x92 Clothoid \xE2\x86\x92 Arc\xE2\x82\x82)   Arc #%1  \xE2\x86\x92  Arc #%2")
            .arg(m_arcIdx).arg(m_arc2Idx);
    }
    return QString();
}

void AddSpiralCalcDialog::init()
{
    setWindowTitle(tr("Add Spiral Trial Calculation (LC / CA / ACA)"));
    resize(560, 480);

    auto* mainLayout = new QVBoxLayout(this);

    // ── 已選元素（唯讀）／螺旋線類型 ─────────────────────────────────────
    auto* form = new QFormLayout();
    m_modeLabel = new QLabel(modeLabelText(), this);
    form->addRow(tr("Selection:"), m_modeLabel);

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
    form->addRow(tr("Spiral type:"), m_spiralTypeCombo);
    mainLayout->addLayout(form);

    // 帶入上次（同一個群組方向）記錄的選取，取代每次都固定回到 Clothoid
    // （見標頭檔 restoreSettings() 的說明）。放在 connect() 訊號之前，
    // 才不會為了這次還原多觸發一次自動重算。
    restoreSettings();

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
    m_statusLabel->setObjectName(QStringLiteral("statusLabel")); // 供自動化測試 findChild() 精準定位
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    // ── 試算結果（Key/Value） ───────────────────────────────────────────
    m_previewTable = new QTableWidget(this);
    m_previewTable->setColumnCount(2);
    m_previewTable->setHorizontalHeaderLabels({ tr("Item"), tr("Value") });
    m_previewTable->horizontalHeader()->setStretchLastSection(true);
    m_previewTable->verticalHeader()->setVisible(false);
    m_previewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(new QLabel(tr("Trial result:"), this));
    mainLayout->addWidget(m_previewTable);

    connect(m_spiralTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                // 切換螺旋線類型時立即重新試算（而不是只標記「已過期」等使用者
                // 再按一次 Calculate）：避免使用者誤以為「換了類型、按了
                // Calculate，卻沒有依新類型重算」——因為畫面上一路看到的都是
                // 已經用新類型算出的最新結果，沒有「舊結果殘留、要再點一次
                // 才會更新」的中間狀態。Calculate 按鈕仍保留，行為完全相同
                // （呼叫同一個 onCalculate()），可用來在不改變類型的情況下
                // 手動重算（例如外部資料變動後）。
                onCalculate();
            });
    connect(m_calcButton,  &QPushButton::clicked, this, &AddSpiralCalcDialog::onCalculate);
    connect(m_applyButton, &QPushButton::clicked, this, &AddSpiralCalcDialog::onApply);

    // 開啟時即先試算一次（帶入 restoreSettings() 還原的選取，或預設的
    // Clothoid）；上面的 addItem() 迴圈與 restoreSettings() 的
    // setCurrentIndex() 都在 connect() 之前執行，不會觸發自動重算，
    // 所以仍需在這裡手動呼叫一次。
    onCalculate();
}

SpiralType AddSpiralCalcDialog::selectedSpiralType() const
{
    return static_cast<SpiralType>(m_spiralTypeCombo->currentData().toInt());
}

// ────────────────────────────────────────────────────────────────────────────
//  restoreSettings / saveSettings / done — 記憶上次選取（見標頭檔說明）
// ────────────────────────────────────────────────────────────────────────────

void AddSpiralCalcDialog::restoreSettings()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    const QString savedName = settings.value(spiralTypeSettingsKey(m_mode)).toString();
    if (savedName.isEmpty())
        return; // 從未存過（例如第一次使用）→ 維持下拉選單預設的 Clothoid

    const SpiralType saved = spiralTypeFromDisplayName(savedName);
    const int idx = m_spiralTypeCombo->findData(static_cast<int>(saved));
    if (idx >= 0)
        m_spiralTypeCombo->setCurrentIndex(idx);
    // idx < 0：存檔內容對不上目前的下拉選單內容（理論上不會發生，因為
    // spiralTypeFromDisplayName() 找不到對應名稱時已經回傳 Clothoid，而
    // Clothoid 一定在選單中；防禦性寫法，維持預設選取即可）。
}

void AddSpiralCalcDialog::saveSettings() const
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(spiralTypeSettingsKey(m_mode),
                      spiralTypeDisplayName(selectedSpiralType()));
}

void AddSpiralCalcDialog::done(int result)
{
    // 不論是「套用」成功（Accepted）、按「取消」還是直接關閉視窗
    // （Rejected），都記住目前的選取，供下次開啟這個群組方向時帶入
    // ——語意上對應 VBA 表單關閉時呼叫 SaveSetting，而不是只有「執行
    // 成功」才存檔。
    saveSettings();
    QDialog::done(result);
}

// ────────────────────────────────────────────────────────────────────────────
//  onCalculate — 呼叫 solveLC/solveCA/solveACA，不修改 document。
// ────────────────────────────────────────────────────────────────────────────

void AddSpiralCalcDialog::onCalculate()
{
    m_lastCalcValid = false;
    m_applyButton->setEnabled(false);
    m_previewTable->setRowCount(0);

    if (!m_doc || !m_doc->horizontal()) {
        m_statusLabel->setText(tr("No AlignmentDocument."));
        return;
    }
    const auto& elems = m_doc->horizontal()->elements();
    const int n = elems.size();
    const SpiralType spiralType = selectedSpiralType();

    if (m_mode == GroupMode::LC) {
        if (m_tangentIdx < 0 || m_arcIdx < 0 || m_tangentIdx >= n || m_arcIdx >= n) {
            m_statusLabel->setText(tr("Invalid element selection."));
            return;
        }
        const auto& tanElem = elems[m_tangentIdx];
        const auto& arcElem = elems[m_arcIdx];
        const double R = std::abs(arcElem.radius);
        const double azArcEnd = arcAzimuthAtPT(arcElem.startPI, arcElem.endPI, arcElem.arcCenter);

        const SolvedLC lc = AlignmentSolver::solveLC(
            arcElem.arcCenter, R,
            arcElem.endPI, azArcEnd,
            tanElem.startPI, tanElem.endPI,
            spiralType);

        // 「Spiral type」永遠是第一列，不論試算成功或失敗都會顯示——避免
        // 使用者誤以為換了類型卻沒有真的用新類型重算（見 onCalculate()
        // 開頭的 setRowCount(0)：舊結果一定會先清空，所以失敗時表格只會
        // 留下這一列，不會殘留前一次「不同類型」的舊數字）。
        addRow(m_previewTable, tr("Spiral type"), spiralTypeDisplayName(spiralType));

        if (lc.valid) {
            addRow(m_previewTable, tr("Ls (solved)"), QString::number(lc.Ls, 'f', 3) + " m");
            addRow(m_previewTable, tr("TS (spiral start)"), fmtPt(lc.tsPoint));
            addRow(m_previewTable, tr("SC (= new arc PC)"), fmtPt(lc.scPoint));
            addRow(m_previewTable, tr("Azimuth at TS"), fmtDeg(lc.azTS));
            addRow(m_previewTable, tr("Azimuth at SC"), fmtDeg(lc.azSC));
            addRow(m_previewTable, tr("Trimmed arc length"), QString::number(lc.arcLen, 'f', 3) + " m");
            m_statusLabel->setText(tr("Calculation succeeded. Review the result, then click Apply."));
            m_lastCalcValid = true;
            m_applyButton->setEnabled(true);
        } else {
            const double tanAz = std::atan2(tanElem.endPI.x() - tanElem.startPI.x(),
                                             tanElem.endPI.y() - tanElem.startPI.y());
            const double sA = std::sin(tanAz), cA = std::cos(tanAz);
            const double D = std::abs((arcElem.arcCenter.x() - tanElem.startPI.x()) * cA
                                     - (arcElem.arcCenter.y() - tanElem.startPI.y()) * sA);
            if (D < R - 1e-6) {
                addRow(m_previewTable, tr("Result"), tr("No solution (D < R)"));
                m_statusLabel->setText(
                    tr("No solution for %1 — the arc centre's perpendicular distance to"
                       " the tangent (D = %2 m) is less than the arc radius (R = %3 m)."
                       " This tangent/arc pair cannot be joined by ANY transition curve"
                       " family, not just %1 — the geometry itself has no solution.")
                        .arg(spiralTypeDisplayName(spiralType)).arg(D, 0, 'f', 3).arg(R, 0, 'f', 3));
            } else {
                addRow(m_previewTable, tr("Result"), tr("No solution for this type"));
                m_statusLabel->setText(
                    tr("No solution found for %1 with the current geometry (the search"
                       " for Ls found no valid length in range). This is NOT a fallback"
                       " to Clothoid — %1 was genuinely tried and its curvature-ramp"
                       " shape cannot bridge this particular tangent/arc pair. Try a"
                       " different spiral type, or Clothoid.")
                        .arg(spiralTypeDisplayName(spiralType)));
            }
        }

    } else if (m_mode == GroupMode::CA) {
        if (m_arcIdx < 0 || m_tangentIdx < 0 || m_arcIdx >= n || m_tangentIdx >= n) {
            m_statusLabel->setText(tr("Invalid element selection."));
            return;
        }
        const auto& arcElem = elems[m_arcIdx];
        const auto& tanElem = elems[m_tangentIdx];
        const double R = std::abs(arcElem.radius);
        const double azArcStart = arcAzimuthAtPC(arcElem.startPI, arcElem.endPI, arcElem.arcCenter);

        const SolvedCA ca = AlignmentSolver::solveCA(
            arcElem.arcCenter, R,
            arcElem.startPI, azArcStart,
            tanElem.startPI, tanElem.endPI,
            spiralType);

        addRow(m_previewTable, tr("Spiral type"), spiralTypeDisplayName(spiralType));

        if (ca.valid) {
            addRow(m_previewTable, tr("Ls (solved)"), QString::number(ca.Ls, 'f', 3) + " m");
            addRow(m_previewTable, tr("CS (= new arc PT)"), fmtPt(ca.csPoint));
            addRow(m_previewTable, tr("ST (spiral end)"), fmtPt(ca.stPoint));
            addRow(m_previewTable, tr("Azimuth at CS"), fmtDeg(ca.azCS));
            addRow(m_previewTable, tr("Azimuth at ST"), fmtDeg(ca.azST));
            addRow(m_previewTable, tr("Trimmed arc length"), QString::number(ca.arcLen, 'f', 3) + " m");
            m_statusLabel->setText(tr("Calculation succeeded. Review the result, then click Apply."));
            m_lastCalcValid = true;
            m_applyButton->setEnabled(true);
        } else {
            const double tanAz = std::atan2(tanElem.endPI.x() - tanElem.startPI.x(),
                                             tanElem.endPI.y() - tanElem.startPI.y());
            const double sA = std::sin(tanAz), cA = std::cos(tanAz);
            const double D = std::abs((arcElem.arcCenter.x() - tanElem.startPI.x()) * cA
                                     - (arcElem.arcCenter.y() - tanElem.startPI.y()) * sA);
            if (D < R - 1e-6) {
                addRow(m_previewTable, tr("Result"), tr("No solution (D < R)"));
                m_statusLabel->setText(
                    tr("No solution for %1 — the arc centre's perpendicular distance to"
                       " the tangent (D = %2 m) is less than the arc radius (R = %3 m)."
                       " This arc/tangent pair cannot be joined by ANY transition curve"
                       " family, not just %1 — the geometry itself has no solution.")
                        .arg(spiralTypeDisplayName(spiralType)).arg(D, 0, 'f', 3).arg(R, 0, 'f', 3));
            } else {
                addRow(m_previewTable, tr("Result"), tr("No solution for this type"));
                m_statusLabel->setText(
                    tr("No solution found for %1 with the current geometry (the search"
                       " for Ls found no valid length in range). This is NOT a fallback"
                       " to Clothoid — %1 was genuinely tried and its curvature-ramp"
                       " shape cannot bridge this particular arc/tangent pair. Try a"
                       " different spiral type, or Clothoid.")
                        .arg(spiralTypeDisplayName(spiralType)));
            }
        }

    } else { // ACA
        if (m_arcIdx < 0 || m_arc2Idx < 0 || m_arcIdx >= n || m_arc2Idx >= n) {
            m_statusLabel->setText(tr("Invalid element selection."));
            return;
        }
        const auto& arc1Elem = elems[m_arcIdx];
        const auto& arc2Elem = elems[m_arc2Idx];
        const double R1 = std::abs(arc1Elem.radius);
        const double R2 = std::abs(arc2Elem.radius);
        const double azArc1Start = arcAzimuthAtPC(arc1Elem.startPI, arc1Elem.endPI, arc1Elem.arcCenter);
        const double azArc2End   = arcAzimuthAtPT(arc2Elem.startPI, arc2Elem.endPI, arc2Elem.arcCenter);

        const SolvedACA aca = AlignmentSolver::solveACA(
            arc1Elem.arcCenter, R1,
            arc1Elem.startPI,   azArc1Start,
            arc2Elem.arcCenter, R2,
            arc2Elem.endPI,     azArc2End,
            spiralType);

        addRow(m_previewTable, tr("Spiral type"), spiralTypeDisplayName(spiralType));

        // ACA 的等效螺線族由 EggTransitionElement 內部實作（見
        // AlignmentSolver::solveACA()）；並非全部 22 種類型都能當「等效
        // 螺線」使用（見 EggTransitionElement::isFamilyEggCompatible()）。
        // 不支援的類型會在 solveACA() 內部靜默退回 Clothoid——這裡把這件事
        // 明確攤開來讓使用者看到，而不是讓表格顯示「你選的類型：X」卻其實
        // 是用 Clothoid 算出來的數字。
        const bool fellBackToClothoid = aca.actualSpiralType != spiralType;
        if (fellBackToClothoid) {
            addRow(m_previewTable, tr("Actually used"),
                   tr("Clothoid (fallback — see note below)"));
        }

        if (aca.valid) {
            const double Req = (R1 * R2) / std::abs(R1 - R2);
            addRow(m_previewTable, tr("Ls (solved)"), QString::number(aca.Ls, 'f', 3) + " m");
            addRow(m_previewTable, tr("Req (equivalent radius)"), QString::number(Req, 'f', 1) + " m");
            addRow(m_previewTable, tr("SC\xE2\x82\x81 (= new Arc\xE2\x82\x81 PT)"), fmtPt(aca.sc1Point));
            addRow(m_previewTable, tr("SC\xE2\x82\x82 (= new Arc\xE2\x82\x82 PC)"), fmtPt(aca.sc2Point));
            addRow(m_previewTable, tr("Trimmed Arc\xE2\x82\x81 length"), QString::number(aca.arc1Len, 'f', 3) + " m");
            addRow(m_previewTable, tr("Trimmed Arc\xE2\x82\x82 length"), QString::number(aca.arc2Len, 'f', 3) + " m");
            if (fellBackToClothoid) {
                m_statusLabel->setText(
                    tr("%1 is not currently supported as the equivalent-spiral shape"
                       " for an ACA (Arc" "\xE2\x86\x92" "Clothoid" "\xE2\x86\x92" "Arc) transition"
                       " \xE2\x80\x94 %1's curvature is not a pure function of chainage"
                       " fraction (or its shape parameter needs Ls/R jointly solved),"
                       " which the Egg-transition construction requires. The result"
                       " above was computed with Clothoid instead. Supported ACA"
                       " families: Clothoid, HalfSine, Parabola, CubicJPN, CubicECI,"
                       " Sinusoidal, Cosine, Bloss, Radioid, Logarithmic, Hyperbolic,"
                       " Polynomial, Quintic, Biquadratic, Spline, BlossEulerHybrid.")
                        .arg(spiralTypeDisplayName(spiralType)));
                m_applyButton->setEnabled(false); // 見下方按鈕文字說明
                m_lastCalcValid = false;
            } else {
                m_statusLabel->setText(tr("Calculation succeeded. Review the result, then click Apply."));
                m_lastCalcValid = true;
                m_applyButton->setEnabled(true);
            }
        } else {
            const double centreDist = std::hypot(arc2Elem.arcCenter.x() - arc1Elem.arcCenter.x(),
                                                  arc2Elem.arcCenter.y() - arc1Elem.arcCenter.y());
            const double minReach = std::abs(R1 - R2);
            if (centreDist < minReach - 1e-6) {
                addRow(m_previewTable, tr("Result"), tr("No solution (centres too close)"));
                m_statusLabel->setText(
                    tr("No solution for %1 — the distance between the two arc centres"
                       " (%2 m) is less than |R\xE2\x82\x81 \xE2\x88\x92 R\xE2\x82\x82| (%3 m)."
                       " NO transition curve family, not just %1, can bridge these two"
                       " arcs — the geometry itself has no solution.")
                        .arg(spiralTypeDisplayName(spiralType))
                        .arg(centreDist, 0, 'f', 3).arg(minReach, 0, 'f', 3));
            } else {
                addRow(m_previewTable, tr("Result"), tr("No solution for this type"));
                m_statusLabel->setText(
                    tr("No solution found for %1 with the current geometry (the search"
                       " for Ls found no valid length in range). This is NOT a fallback"
                       " to Clothoid — %1 was genuinely tried and its curvature-ramp"
                       " shape cannot bridge these two arcs. Check that R\xE2\x82\x81 != R\xE2\x82\x82,"
                       " or try a different spiral type.")
                        .arg(spiralTypeDisplayName(spiralType)));
            }
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  onApply — addLC/addCA/addACA + solve()
// ────────────────────────────────────────────────────────────────────────────

void AddSpiralCalcDialog::onApply()
{
    if (!m_lastCalcValid || !m_doc || !m_doc->horizontal()) return;

    // 保險：套用前用目前選擇的螺旋類型重新試算一次，避免使用者在按下
    // 「試算」之後、按「套用」之前又切換了下拉選單卻沒有觸發重算（正常
    // UI 流程下 currentIndexChanged 已經會直接呼叫 onCalculate() 重算，
    // 這裡是防禦性檢查，成本很低）。
    onCalculate();
    if (!m_lastCalcValid) {
        QMessageBox::warning(this, tr("Apply failed"), m_statusLabel->text());
        return;
    }

    const SpiralType spiralType = selectedSpiralType();
    HorizontalAlignmentEdit* edit = m_doc->horizontal();

    int idx = -1;
    QString modeStr;
    if (m_mode == GroupMode::LC) {
        idx     = edit->addLC(m_tangentIdx, m_arcIdx, spiralType);
        modeStr = QStringLiteral("LC");
    } else if (m_mode == GroupMode::CA) {
        idx     = edit->addCA(m_arcIdx, m_tangentIdx, spiralType);
        modeStr = QStringLiteral("CA");
    } else {
        idx     = edit->addACA(m_arcIdx, m_arc2Idx, spiralType);
        modeStr = QStringLiteral("ACA");
    }

    if (idx < 0) {
        QMessageBox::warning(this, tr("Apply failed"),
            tr("%1: add%1() failed — check that the geometry is compatible.\n"
               "  ACA: R1 != R2 required; arcs must be geometrically reachable.")
                .arg(modeStr));
        return;
    }

    edit->solve();
    m_resultIdx = idx;
    Q_EMIT spiralApplied();
    accept();
}

} // namespace ui
} // namespace aicad
