/**
 * @file AlignmentDrawElementDialog.cpp
 * @brief Implementation of AlignmentDrawElementDialog — see the header for
 *        the overall design (three independent draw-areas: Tangent / Curve /
 *        Transition-pair; each area's own "繪製" button immediately closes
 *        the dialog with that one segment).
 */
#include "AlignmentDrawElementDialog.h"

#include "railway/RailwayAlignmentElement.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QGroupBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QSettings>
#include <cmath>

using namespace aicad::railway;

namespace aicad {
namespace ui {

namespace {

const QStringList kSpiralTokens = {
    QStringLiteral("SPIRAL"), QStringLiteral("HALFSINE"), QStringLiteral("PARABOLA"),
    QStringLiteral("CUBICJPN"), QStringLiteral("CUBICECI"), QStringLiteral("SINUSOIDAL"),
    QStringLiteral("COSINE"), QStringLiteral("BLOSS"), QStringLiteral("LEMNISCATE"),
    QStringLiteral("WIENERBOGEN"), QStringLiteral("RADIOID"), QStringLiteral("ELASRADIOID"),
    QStringLiteral("NORWICHSTURM"), QStringLiteral("PSEUELLRADIOID"), QStringLiteral("LOGARITHMIC"),
    QStringLiteral("HYPERBOLIC"), QStringLiteral("POLYNOMIAL"), QStringLiteral("QUINTIC"),
    QStringLiteral("PHQUINTIC"), QStringLiteral("BIQUADRATIC"), QStringLiteral("SPLINE"),
    QStringLiteral("BLOSSEULERHYBRID")
};

bool isValidRadius(double r) { return std::isfinite(r) && std::abs(r) > 1e-3; }

constexpr auto kSettingsGroup = "AlignmentDrawElementDialog";

} // namespace

// ============================================================================
//  Construction
// ============================================================================

AlignmentDrawElementDialog::AlignmentDrawElementDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("繪製線元 (ADC) — 設定後套用到既有線形端點"));
    resize(760, 420);

    buildUi();
    loadSettings();   // 需求 18：載入上次關閉前存的欄位值
    setupAnchor();
}

void AlignmentDrawElementDialog::done(int result)
{
    // 需求 18：涵蓋三個「繪製」按鈕（accept）與「取消」／Esc／叉關閉
    // （reject）所有結束路徑，關閉前統一存一次。
    saveSettings();
    QDialog::done(result);
}

void AlignmentDrawElementDialog::loadSettings()
{
    QSettings settings(QStringLiteral("AICAD"), QStringLiteral("AICAD"));
    settings.beginGroup(QLatin1String(kSettingsGroup));

    m_tangentLengthEdit->setText(settings.value(QStringLiteral("tangentLength")).toString());

    m_curveRadiusEdit->setText(settings.value(QStringLiteral("curveRadius")).toString());
    m_curveLengthEdit->setText(settings.value(QStringLiteral("curveLength")).toString());

    const QString spiralType = settings.value(QStringLiteral("transSpiralType")).toString();
    if (!spiralType.isEmpty()) {
        const int idx = m_transSpiralCombo->findText(spiralType);
        if (idx >= 0) m_transSpiralCombo->setCurrentIndex(idx);
    }
    m_transInLengthEdit->setText(settings.value(QStringLiteral("transInLength")).toString());
    m_transCurveRadiusEdit->setText(settings.value(QStringLiteral("transCurveRadius")).toString());
    // 圓弧長度／出緩和曲線長度預設 "0"（見 buildUi()），只有存過值才覆寫，
    // 避免把 buildUi() 給的預設 "0" 洗成空字串。
    if (settings.contains(QStringLiteral("transCurveLength")))
        m_transCurveLengthEdit->setText(settings.value(QStringLiteral("transCurveLength")).toString());
    if (settings.contains(QStringLiteral("transOutLength")))
        m_transOutLengthEdit->setText(settings.value(QStringLiteral("transOutLength")).toString());

    settings.endGroup();
}

void AlignmentDrawElementDialog::saveSettings() const
{
    QSettings settings(QStringLiteral("AICAD"), QStringLiteral("AICAD"));
    settings.beginGroup(QLatin1String(kSettingsGroup));

    settings.setValue(QStringLiteral("tangentLength"), m_tangentLengthEdit->text());

    settings.setValue(QStringLiteral("curveRadius"), m_curveRadiusEdit->text());
    settings.setValue(QStringLiteral("curveLength"), m_curveLengthEdit->text());

    settings.setValue(QStringLiteral("transSpiralType"), m_transSpiralCombo->currentText());
    settings.setValue(QStringLiteral("transInLength"), m_transInLengthEdit->text());
    settings.setValue(QStringLiteral("transCurveRadius"), m_transCurveRadiusEdit->text());
    settings.setValue(QStringLiteral("transCurveLength"), m_transCurveLengthEdit->text());
    settings.setValue(QStringLiteral("transOutLength"), m_transOutLengthEdit->text());

    settings.endGroup();
}

void AlignmentDrawElementDialog::setupAnchor()
{
    // 需求 20：直線／曲線兩區塊的幾何與起點曲率無關，仍然用「原點錨點」
    // （E=0,N=0,az=0,chainage=0）在對話框內立即算好，呼叫端只需剛體變換
    // 套用（見標頭說明）。[0] 只是佔位用的基準點，內容與 [1] 完全相同、
    // 也不會被寫進最終結果（AlignmentDrawChainCommand 只取 m_recipe[1..]）。
    AlignmentPoint base;
    base.tsc = QStringLiteral("TT");
    base.easting = 0.0; base.northing = 0.0; base.azimuth = 0.0;
    base.chainage = 0.0; base.contChainage = 0.0; base.radius = 0.0;

    m_points.clear();
    m_points.push_back(base);
    m_points.push_back(base);
}

void AlignmentDrawElementDialog::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    auto* areasLayout = new QHBoxLayout();

    // ── 直線區 ────────────────────────────────────────────────────────────
    m_tangentGroup = new QGroupBox(tr("直線區"), this);
    auto* ft = new QFormLayout(m_tangentGroup);
    m_tangentLengthEdit = new QLineEdit(m_tangentGroup);
    m_tangentLengthEdit->setValidator(new QDoubleValidator(0.0001, 1.0e9, 6, m_tangentLengthEdit));
    ft->addRow(tr("長度（TextBox7）："), m_tangentLengthEdit);
    auto* drawTangentBtn = new QPushButton(tr("繪製直線"), m_tangentGroup);
    connect(drawTangentBtn, &QPushButton::clicked, this, &AlignmentDrawElementDialog::onDrawTangent);
    ft->addRow(drawTangentBtn);
    areasLayout->addWidget(m_tangentGroup);

    // ── 曲線區 ────────────────────────────────────────────────────────────
    m_curveGroup = new QGroupBox(tr("曲線區"), this);
    auto* fc = new QFormLayout(m_curveGroup);
    m_curveRadiusEdit = new QLineEdit(m_curveGroup);
    m_curveRadiusEdit->setValidator(new QDoubleValidator(-1.0e9, 1.0e9, 6, m_curveRadiusEdit));
    fc->addRow(tr("半徑（TextBox6）："), m_curveRadiusEdit);
    m_curveLengthEdit = new QLineEdit(m_curveGroup);
    m_curveLengthEdit->setValidator(new QDoubleValidator(0.0001, 1.0e9, 6, m_curveLengthEdit));
    fc->addRow(tr("長度（TextBox5）："), m_curveLengthEdit);
    auto* drawCurveBtn = new QPushButton(tr("繪製曲線"), m_curveGroup);
    connect(drawCurveBtn, &QPushButton::clicked, this, &AlignmentDrawElementDialog::onDrawCurve);
    fc->addRow(drawCurveBtn);
    areasLayout->addWidget(m_curveGroup);

    mainLayout->addLayout(areasLayout);

    // ── 緩和曲線區（二段）────────────────────────────────────────────────
    // 需求 20：不再收集「起點曲率」——圓弧半徑欄的意義（切線接圓弧的正向、
    // 或圓弧接切線／另一圓弧的反向／蛋形）要等使用者選取真正的接續點、
    // 知道該點「以選取為準」的實際曲率後，才由 AlignmentDrawChainCommand
    // 判斷、計算（見標頭檔說明）。
    m_transGroup = new QGroupBox(tr("緩和曲線區（二段；起點曲率將依所選端點自動判斷）"), this);
    auto* fs = new QFormLayout(m_transGroup);
    m_transSpiralCombo = new QComboBox(m_transGroup);
    m_transSpiralCombo->addItems(kSpiralTokens);
    fs->addRow(tr("緩和曲線類型（ComboBox1）："), m_transSpiralCombo);
    m_transInLengthEdit = new QLineEdit(m_transGroup);
    m_transInLengthEdit->setValidator(new QDoubleValidator(0.0001, 1.0e9, 6, m_transInLengthEdit));
    fs->addRow(tr("入緩和曲線長度（TextBox1）："), m_transInLengthEdit);
    m_transCurveRadiusEdit = new QLineEdit(m_transGroup);
    m_transCurveRadiusEdit->setValidator(new QDoubleValidator(-1.0e9, 1.0e9, 6, m_transCurveRadiusEdit));
    fs->addRow(tr("圓弧半徑（TextBox2，留白＝無限大／切線）："), m_transCurveRadiusEdit);
    m_transCurveLengthEdit = new QLineEdit(m_transGroup);
    m_transCurveLengthEdit->setValidator(new QDoubleValidator(0.0, 1.0e9, 6, m_transCurveLengthEdit));
    m_transCurveLengthEdit->setText(QStringLiteral("0"));
    fs->addRow(tr("圓弧長度（0＝無中間圓弧／直線段）："), m_transCurveLengthEdit);
    m_transOutLengthEdit = new QLineEdit(m_transGroup);
    m_transOutLengthEdit->setValidator(new QDoubleValidator(0.0, 1.0e9, 6, m_transOutLengthEdit));
    m_transOutLengthEdit->setText(QStringLiteral("0"));
    fs->addRow(tr("出緩和曲線長度（0＝不再接回）："), m_transOutLengthEdit);
    auto* drawTransBtn = new QPushButton(tr("繪製緩和曲線組"), m_transGroup);
    connect(drawTransBtn, &QPushButton::clicked,
            this, &AlignmentDrawElementDialog::onDrawTransitionGroup);
    fs->addRow(drawTransBtn);
    mainLayout->addWidget(m_transGroup);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    // 三個區域各自的「繪製」按鈕已經各自負責關閉對話框。這裡只留「取消」。
    auto* buttons = new QDialogButtonBox(this);
    auto* cancelBtn = buttons->addButton(tr("取消"), QDialogButtonBox::RejectRole);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

// ============================================================================
//  Tangent
// ============================================================================

QString AlignmentDrawElementDialog::appendTangentSegment(double length)
{
    if (!(length > 0.0))
        return tr("長度必須是大於 0 的數字。");

    const int lastIdx = m_points.size() - 1;
    const AlignmentPoint from = m_points[lastIdx];

    TangentElement elem;
    elem.setPlacement({from.chainage, from.easting, from.northing, from.azimuth});
    elem.setLength(length);
    const QPointF xy = elem.worldXY(from.chainage + length);
    const double az = elem.worldAzimuth(from.chainage + length);

    m_points[lastIdx].length = length;
    m_points[lastIdx].radius = 0.0;
    m_points[lastIdx].curveType.clear();
    QString tsc = m_points[lastIdx].tsc;
    if (tsc.size() < 2) tsc = QStringLiteral("TT");
    tsc[1] = QLatin1Char('T');
    m_points[lastIdx].tsc = tsc;

    AlignmentPoint next;
    next.tsc = QStringLiteral("TT");
    next.easting = xy.x();
    next.northing = xy.y();
    next.chainage = from.chainage + length;
    next.contChainage = from.contChainage + length;
    next.azimuth = az;
    m_points.push_back(next);

    return QString();
}

void AlignmentDrawElementDialog::onDrawTangent()
{
    bool ok = false;
    const double len = m_tangentLengthEdit->text().toDouble(&ok);
    const QString err = ok ? appendTangentSegment(len) : tr("長度必須是大於 0 的數字。");
    if (!err.isEmpty()) { m_statusLabel->setText(err); return; }

    m_isTransitionGroup = false;
    // 按下就直接關閉對話框（accept），呼叫端接著進入
    // 「選物件→直接繪製結果」的連續流程。
    accept();
}

// ============================================================================
//  Curve
// ============================================================================

QString AlignmentDrawElementDialog::appendCurveSegment(double radius, double length)
{
    if (!(length > 0.0))
        return tr("長度必須是大於 0 的數字。");
    if (!isValidRadius(radius))
        return tr("半徑必須是非 0 的數字。");

    const int lastIdx = m_points.size() - 1;
    const AlignmentPoint from = m_points[lastIdx];

    CircularArcElement elem(radius);
    elem.setPlacement({from.chainage, from.easting, from.northing, from.azimuth});
    elem.setLength(length);
    const QPointF xy = elem.worldXY(from.chainage + length);
    const double az = elem.worldAzimuth(from.chainage + length);

    m_points[lastIdx].length = length;
    m_points[lastIdx].radius = radius;
    m_points[lastIdx].curveType.clear();
    QString tsc = m_points[lastIdx].tsc;
    if (tsc.size() < 2) tsc = QStringLiteral("TT");
    tsc[1] = QLatin1Char('C');
    m_points[lastIdx].tsc = tsc;

    AlignmentPoint next;
    next.tsc = QStringLiteral("CT");
    next.easting = xy.x();
    next.northing = xy.y();
    next.chainage = from.chainage + length;
    next.contChainage = from.contChainage + length;
    next.azimuth = az;
    m_points.push_back(next);

    return QString();
}

void AlignmentDrawElementDialog::onDrawCurve()
{
    bool ok1 = false, ok2 = false;
    const double radius = m_curveRadiusEdit->text().toDouble(&ok1);
    const double len    = m_curveLengthEdit->text().toDouble(&ok2);
    const QString err = (ok1 && ok2) ? appendCurveSegment(radius, len)
                                      : tr("半徑與長度必須是數字。");
    if (!err.isEmpty()) { m_statusLabel->setText(err); return; }

    m_isTransitionGroup = false;
    accept();
}

// ============================================================================
//  Transition group（緩和曲線，二段）—— 需求 20：只驗證欄位本身的基本
//  合理性，不在此計算幾何；實際幾何延後到 AlignmentDrawChainCommand 知道
//  真實曲率（選取的端點）後才算，見標頭檔與該類別的說明。
// ============================================================================

void AlignmentDrawElementDialog::onDrawTransitionGroup()
{
    bool ok1 = false, ok3 = false, ok4 = false;
    const double spiralInLen  = m_transInLengthEdit->text().toDouble(&ok1);
    const double curveLen     = m_transCurveLengthEdit->text().toDouble(&ok3);
    const double spiralOutLen = m_transOutLengthEdit->text().toDouble(&ok4);

    // 半徑欄留白＝無限大（切線）；非空白時才需要成功解析為數字。
    const QString radiusText = m_transCurveRadiusEdit->text().trimmed();
    bool ok2 = true;
    double targetRadius = 0.0;
    if (!radiusText.isEmpty())
        targetRadius = radiusText.toDouble(&ok2);

    if (!(ok1 && ok2 && ok3 && ok4)) {
        m_statusLabel->setText(tr("緩和曲線組欄位必須是數字（圓弧半徑可留白）。"));
        return;
    }
    if (!(spiralInLen > 0.0)) {
        m_statusLabel->setText(tr("入緩和曲線長度必須是大於 0 的數字。"));
        return;
    }
    if (curveLen < 0.0 || spiralOutLen < 0.0) {
        m_statusLabel->setText(tr("圓弧長度／出緩和曲線長度不可為負數。"));
        return;
    }

    const bool targetIsInfinite = radiusText.isEmpty() || !isValidRadius(targetRadius);

    m_transParams.family           = m_transSpiralCombo->currentText();
    m_transParams.spiralInLength   = spiralInLen;
    m_transParams.targetIsInfinite = targetIsInfinite;
    m_transParams.targetRadius     = targetIsInfinite ? 0.0 : targetRadius;
    m_transParams.curveLength      = curveLen;
    m_transParams.spiralOutLength  = spiralOutLen;
    m_isTransitionGroup = true;

    accept();
}

} // namespace ui
} // namespace aicad
