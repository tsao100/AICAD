/**
 * @file AlignmentDrawElementDialog.cpp
 * @brief Implementation of the redesigned AlignmentDrawElementDialog — see
 *        the header for the overall design (three add-areas: Tangent /
 *        Curve / Transition-pair).
 */
#include "AlignmentDrawElementDialog.h"
#include "AlignmentDataTableDialog.h"   // azimuthToDMS()（共用格式化邏輯）

#include "railway/AlignmentQuickCalc.h"
#include "railway/RailwayAlignmentElement.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QGroupBox>
#include <QListWidget>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QtMath>
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

QString pointLabel(int idx, const AlignmentPoint& p)
{
    return QObject::tr("#%1  %2  里程=%3")
        .arg(idx)
        .arg(p.tsc.isEmpty() ? QStringLiteral("??") : p.tsc)
        .arg(p.chainage, 0, 'f', 3);
}

} // namespace

// ============================================================================
//  Construction
// ============================================================================

AlignmentDrawElementDialog::AlignmentDrawElementDialog(
    const QString& tclName, const QVector<AlignmentPoint>& existingPointsTM2, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("接續目前線形繪製線元 (ADC) — %1").arg(tclName));
    resize(860, 720);

    m_points = existingPointsTM2;
    m_existingCount = m_points.size();

    buildUi(tclName);
    refreshStartPointCombo(tclName);
}

void AlignmentDrawElementDialog::buildUi(const QString& /*tclName*/)
{
    auto* mainLayout = new QVBoxLayout(this);

    // ── 接續點 ────────────────────────────────────────────────────────────
    auto* pickForm = new QFormLayout();
    m_startPointCombo = new QComboBox(this);
    connect(m_startPointCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AlignmentDrawElementDialog::onStartPointChanged);
    pickForm->addRow(tr("接續點（既有關鍵點）："), m_startPointCombo);
    mainLayout->addLayout(pickForm);

    m_startInfoLabel = new QLabel(this);
    m_startInfoLabel->setWordWrap(true);
    mainLayout->addWidget(m_startInfoLabel);

    auto* areasLayout = new QHBoxLayout();

    // ── 直線區 ────────────────────────────────────────────────────────────
    m_tangentGroup = new QGroupBox(tr("直線區"), this);
    auto* ft = new QFormLayout(m_tangentGroup);
    m_tangentLengthEdit = new QLineEdit(m_tangentGroup);
    m_tangentLengthEdit->setValidator(new QDoubleValidator(0.0001, 1.0e9, 6, m_tangentLengthEdit));
    ft->addRow(tr("長度（TextBox7）："), m_tangentLengthEdit);
    auto* addTangentBtn = new QPushButton(tr("加入直線"), m_tangentGroup);
    connect(addTangentBtn, &QPushButton::clicked, this, &AlignmentDrawElementDialog::onAddTangent);
    ft->addRow(addTangentBtn);
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
    auto* addCurveBtn = new QPushButton(tr("加入曲線"), m_curveGroup);
    connect(addCurveBtn, &QPushButton::clicked, this, &AlignmentDrawElementDialog::onAddCurve);
    fc->addRow(addCurveBtn);
    areasLayout->addWidget(m_curveGroup);

    mainLayout->addLayout(areasLayout);

    // ── 緩和曲線區（二段）────────────────────────────────────────────────
    m_transGroup = new QGroupBox(tr("緩和曲線區（二段）"), this);
    auto* fs = new QFormLayout(m_transGroup);
    m_transSpiralCombo = new QComboBox(m_transGroup);
    m_transSpiralCombo->addItems(kSpiralTokens);
    fs->addRow(tr("緩和曲線類型（ComboBox1）："), m_transSpiralCombo);
    m_transInLengthEdit = new QLineEdit(m_transGroup);
    m_transInLengthEdit->setValidator(new QDoubleValidator(0.0001, 1.0e9, 6, m_transInLengthEdit));
    fs->addRow(tr("入緩和曲線長度（TextBox1）："), m_transInLengthEdit);
    m_transCurveRadiusEdit = new QLineEdit(m_transGroup);
    m_transCurveRadiusEdit->setValidator(new QDoubleValidator(-1.0e9, 1.0e9, 6, m_transCurveRadiusEdit));
    fs->addRow(tr("圓弧半徑（TextBox2，留白＝無限大＝直線）："), m_transCurveRadiusEdit);
    m_transCurveLengthEdit = new QLineEdit(m_transGroup);
    m_transCurveLengthEdit->setValidator(new QDoubleValidator(0.0, 1.0e9, 6, m_transCurveLengthEdit));
    m_transCurveLengthEdit->setText(QStringLiteral("0"));
    fs->addRow(tr("圓弧長度（0＝無中間圓弧）："), m_transCurveLengthEdit);
    m_transOutLengthEdit = new QLineEdit(m_transGroup);
    m_transOutLengthEdit->setValidator(new QDoubleValidator(0.0, 1.0e9, 6, m_transOutLengthEdit));
    m_transOutLengthEdit->setText(QStringLiteral("0"));
    fs->addRow(tr("出緩和曲線長度（0＝不接回切線）："), m_transOutLengthEdit);
    m_transHint = new QLabel(m_transGroup);
    m_transHint->setWordWrap(true);
    fs->addRow(QString(), m_transHint);
    auto* addTransBtn = new QPushButton(tr("加入緩和曲線組"), m_transGroup);
    connect(addTransBtn, &QPushButton::clicked,
            this, &AlignmentDrawElementDialog::onAddTransitionGroup);
    fs->addRow(addTransBtn);
    mainLayout->addWidget(m_transGroup);

    // ── 已加入線元列表／復原 ─────────────────────────────────────────────
    auto* listBar = new QHBoxLayout();
    listBar->addWidget(new QLabel(tr("已加入線元："), this));
    listBar->addStretch(1);
    m_undoButton = new QPushButton(tr("復原上一段"), this);
    connect(m_undoButton, &QPushButton::clicked, this, &AlignmentDrawElementDialog::onUndoLast);
    listBar->addWidget(m_undoButton);
    mainLayout->addLayout(listBar);

    m_segmentList = new QListWidget(this);
    m_segmentList->setMaximumHeight(120);
    mainLayout->addWidget(m_segmentList);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    auto* buttons = new QDialogButtonBox(this);
    m_finishButton = buttons->addButton(tr("完成"), QDialogButtonBox::AcceptRole);
    auto* cancelBtn = buttons->addButton(tr("取消"), QDialogButtonBox::RejectRole);
    connect(m_finishButton, &QPushButton::clicked, this, &AlignmentDrawElementDialog::onFinish);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

// ============================================================================
//  Start point selection
// ============================================================================

void AlignmentDrawElementDialog::refreshStartPointCombo(const QString& /*tclName*/)
{
    m_startPointCombo->blockSignals(true);
    m_startPointCombo->clear();
    for (int i = 0; i < m_existingCount; ++i)
        m_startPointCombo->addItem(pointLabel(i, m_points[i]), i);
    if (m_existingCount > 0)
        m_startPointCombo->setCurrentIndex(m_existingCount - 1);   // 預設接續最後一點
    m_startPointCombo->blockSignals(false);

    onStartPointChanged(m_startPointCombo->currentIndex());
}

void AlignmentDrawElementDialog::onStartPointChanged(int /*index*/)
{
    const int ptIdx = m_startPointCombo->currentData().toInt();
    if (ptIdx < 0 || ptIdx >= m_existingCount)
        return;

    // 重新從既有資料截斷（丟棄任何本次對話框已加入、但使用者又改選接續點
    // 的線元），並把截斷點的收尾字元暫定為 'T'，直到使用者實際加入新線元
    // 才會被覆寫——避免殘留原本「接到下一個既有點」的舊 tsc/length/radius。
    m_points.resize(ptIdx + 1);
    QString tsc = m_points.last().tsc;
    if (tsc.size() < 2) tsc = QStringLiteral("TT");
    tsc[1] = QLatin1Char('T');
    m_points.last().tsc = tsc;
    m_points.last().length = 0.0;
    m_points.last().radius = 0.0;
    m_points.last().curveType.clear();

    m_radiusHistory.clear();
    for (int i = 0; i <= ptIdx; ++i)
        m_radiusHistory.push_back(curvatureAtPoint(m_points, i));

    m_checkpoints.clear();

    refreshCurrentStateLabel();
    refreshSegmentList();
    refreshTransitionGroupAvailability();
}

// ============================================================================
//  State helpers
// ============================================================================

double AlignmentDrawElementDialog::currentCurvature() const
{
    return m_radiusHistory.isEmpty() ? 0.0 : m_radiusHistory.last();
}

void AlignmentDrawElementDialog::refreshCurrentStateLabel()
{
    if (m_points.isEmpty()) {
        m_startInfoLabel->clear();
        return;
    }
    const AlignmentPoint& cur = m_points.last();
    const double curvature = currentCurvature();
    const QString curvatureText = (std::abs(curvature) < 1e-9)
        ? tr("切線（曲率 0）")
        : tr("圓弧上，半徑 = %1").arg(curvature, 0, 'f', 3);

    m_startInfoLabel->setText(
        tr("目前端點： E=%1  N=%2  里程=%3  方位角=%4\n目前狀態： %5")
            .arg(cur.easting, 0, 'f', 4)
            .arg(cur.northing, 0, 'f', 4)
            .arg(cur.chainage, 0, 'f', 4)
            .arg(azimuthToDMS(cur.azimuth))
            .arg(curvatureText));
}

void AlignmentDrawElementDialog::refreshSegmentList()
{
    m_segmentList->clear();
    const int baseIdx = m_startPointCombo->currentData().toInt();
    for (int i = baseIdx; i + 1 < m_points.size(); ++i) {
        const AlignmentPoint& a = m_points[i];
        const AlignmentPoint& b = m_points[i + 1];
        const QChar depart = (a.tsc.size() >= 2) ? a.tsc[1] : QLatin1Char('?');
        QString typeStr, extra;
        if (depart == QLatin1Char('T')) {
            typeStr = tr("直線");
        } else if (depart == QLatin1Char('C')) {
            typeStr = tr("圓弧");
            extra = tr("  R=%1").arg(a.radius, 0, 'f', 3);
        } else if (depart == QLatin1Char('S')) {
            typeStr = tr("緩和曲線");
            extra = tr("  類型=%1").arg(a.curveType.isEmpty() ? QStringLiteral("SPIRAL") : a.curveType);
        }
        m_segmentList->addItem(
            tr("#%1  %2  長度=%3%4  → E=%5, N=%6")
                .arg(i + 1 - baseIdx)
                .arg(typeStr)
                .arg(a.length, 0, 'f', 3)
                .arg(extra)
                .arg(b.easting, 0, 'f', 3)
                .arg(b.northing, 0, 'f', 3));
    }
    m_segmentList->scrollToBottom();
    m_undoButton->setEnabled(!m_checkpoints.isEmpty());
}

void AlignmentDrawElementDialog::refreshTransitionGroupAvailability()
{
    // 需求：起點在圓弧上時緩和曲線區也要能用——不再停用整個區塊，只更新
    // 提示文字說明「圓弧半徑」欄目前該怎麼填。
    const bool atZero = std::abs(currentCurvature()) < 1e-9;
    m_transHint->setText(
        atZero ? tr("目前在切線上：圓弧半徑請填入實際半徑（不可留白）。")
               : tr("目前在圓弧上（半徑 %1）：圓弧半徑請留白（代表銜接回切線）；"
                    "不支援轉去另一個不同的有限半徑（複合曲線請改用 "
                    "ALIGNMENTQUICKTABLE）。")
                     .arg(currentCurvature(), 0, 'f', 3));
}

// ============================================================================
//  Add: Tangent
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

    m_checkpoints.push_back(m_points.size());

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
    m_radiusHistory.push_back(0.0);

    return QString();
}

void AlignmentDrawElementDialog::onAddTangent()
{
    bool ok = false;
    const double len = m_tangentLengthEdit->text().toDouble(&ok);
    const QString err = ok ? appendTangentSegment(len) : tr("長度必須是大於 0 的數字。");
    if (!err.isEmpty()) { m_statusLabel->setText(err); return; }

    m_tangentLengthEdit->clear();
    m_statusLabel->setText(tr("已加入直線段。"));
    refreshCurrentStateLabel();
    refreshSegmentList();
    refreshTransitionGroupAvailability();
}

// ============================================================================
//  Add: Curve
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

    m_checkpoints.push_back(m_points.size());

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
    m_radiusHistory.push_back(radius);

    return QString();
}

void AlignmentDrawElementDialog::onAddCurve()
{
    bool ok1 = false, ok2 = false;
    const double radius = m_curveRadiusEdit->text().toDouble(&ok1);
    const double len    = m_curveLengthEdit->text().toDouble(&ok2);
    const QString err = (ok1 && ok2) ? appendCurveSegment(radius, len)
                                      : tr("半徑與長度必須是數字。");
    if (!err.isEmpty()) { m_statusLabel->setText(err); return; }

    m_statusLabel->setText(tr("已加入曲線段。"));
    refreshCurrentStateLabel();
    refreshSegmentList();
    refreshTransitionGroupAvailability();
}

// ============================================================================
//  Add: Transition group（緩和曲線，二段）
// ============================================================================

QString AlignmentDrawElementDialog::appendTransitionGroup(const QString& curveType,
                                                            double spiralInLength,
                                                            double curveRadius,
                                                            double curveLength,
                                                            double spiralOutLength)
{
    const double curvatureIn = currentCurvature();
    const bool startsAtZero = std::abs(curvatureIn) < 1e-9;
    const bool targetIsInfinite = !isValidRadius(curveRadius);   // 留白／0 ＝ 無限大＝直線

    if (!(spiralInLength > 0.0))
        return tr("入緩和曲線長度必須是大於 0 的數字。");
    if (curveLength < 0.0 || spiralOutLength < 0.0)
        return tr("圓弧長度／出緩和曲線長度不可為負數。");

    if (startsAtZero) {
        if (targetIsInfinite) {
            return tr("目前在切線上，圓弧半徑不可留白（留白代表直線，切線接直線"
                       "沒有意義，請改用直線區）。");
        }
    } else {
        if (!targetIsInfinite) {
            if (std::abs(curveRadius - curvatureIn) < 1e-6) {
                return tr("圓弧半徑與目前狀態相同，不需要緩和曲線，請改用曲線區延伸。");
            }
            return tr("目前在圓弧上時，圓弧半徑只能留白（代表銜接回切線）；"
                       "轉去另一個不同的有限半徑（複合曲線）請改用 ALIGNMENTQUICKTABLE。");
        }
    }

    const int lastIdx = m_points.size() - 1;
    const AlignmentPoint start = m_points[lastIdx];
    const QString family = curveType.isEmpty() ? QStringLiteral("SPIRAL") : curveType;
    const double midCurvature = targetIsInfinite ? 0.0 : curveRadius;

    // ── 1. 入緩和曲線：curvatureIn → midCurvature ───────────────────────
    // curvatureIn==0 時為正向（0→midCurvature，此時 midCurvature 必為
    // 實際半徑）；否則為反向（curvatureIn→0，此時 midCurvature 必為 0，
    // 前面的驗證已排除其餘組合）。
    double e1 = 0.0, n1 = 0.0, az1 = 0.0;
    if (startsAtZero)
        computeForwardSpiralEndpoint(start, midCurvature, spiralInLength, family, e1, n1, az1);
    else
        computeReversedSpiralEndpoint(start, curvatureIn, spiralInLength, family, e1, n1, az1);

    AlignmentPoint afterIn;
    afterIn.tsc = QStringLiteral("ST");
    afterIn.easting  = e1;
    afterIn.northing = n1;
    afterIn.azimuth  = az1;
    afterIn.chainage     = start.chainage     + spiralInLength;
    afterIn.contChainage = start.contChainage + spiralInLength;
    afterIn.radius = midCurvature;   // 供 'S' 抵達分支的 curvatureAtPoint() 讀取

    QVector<AlignmentPoint> toAppend;
    QVector<double> radiiForEach;

    AlignmentPoint curEnd = afterIn;
    double curEndCurvature = midCurvature;

    if (curveLength > 0.0) {
        if (!targetIsInfinite) {
            // ── 2a.（可選）中間圓弧，維持同一半徑 midCurvature ──────────
            CircularArcElement arcElem(midCurvature);
            arcElem.setPlacement({afterIn.chainage, afterIn.easting, afterIn.northing, afterIn.azimuth});
            arcElem.setLength(curveLength);
            const QPointF axy = arcElem.worldXY(afterIn.chainage + curveLength);
            const double aaz = arcElem.worldAzimuth(afterIn.chainage + curveLength);

            afterIn.tsc = QStringLiteral("SC");   // 緩和曲線抵達、同時是圓弧起始列
            afterIn.length = curveLength;

            AlignmentPoint afterMid;
            afterMid.tsc = QStringLiteral("CT");
            afterMid.easting  = axy.x();
            afterMid.northing = axy.y();
            afterMid.azimuth  = aaz;
            afterMid.chainage     = afterIn.chainage     + curveLength;
            afterMid.contChainage = afterIn.contChainage + curveLength;

            toAppend.push_back(afterIn);
            radiiForEach.push_back(midCurvature);
            curEnd = afterMid;
            curEndCurvature = midCurvature;
        } else {
            // ── 2b.（可選）中間直線段（midCurvature==0）─────────────────
            TangentElement tanElem;
            tanElem.setPlacement({afterIn.chainage, afterIn.easting, afterIn.northing, afterIn.azimuth});
            tanElem.setLength(curveLength);
            const QPointF txy = tanElem.worldXY(afterIn.chainage + curveLength);
            const double taz = tanElem.worldAzimuth(afterIn.chainage + curveLength);

            afterIn.tsc = QStringLiteral("ST");   // 緩和曲線抵達、同時是直線起始列
            afterIn.length = curveLength;

            AlignmentPoint afterMid;
            afterMid.tsc = QStringLiteral("TT");
            afterMid.easting  = txy.x();
            afterMid.northing = txy.y();
            afterMid.azimuth  = taz;
            afterMid.chainage     = afterIn.chainage     + curveLength;
            afterMid.contChainage = afterIn.contChainage + curveLength;

            toAppend.push_back(afterIn);
            radiiForEach.push_back(0.0);
            curEnd = afterMid;
            curEndCurvature = 0.0;
        }
    }

    if (spiralOutLength > 0.0) {
        // ── 3. 出緩和曲線：curEndCurvature → curvatureIn（回到本次操作
        //    開始前的曲率，邏輯完全對稱）───────────────────────────────
        double e2 = 0.0, n2 = 0.0, az2 = 0.0;
        if (std::abs(curEndCurvature) < 1e-9)
            computeForwardSpiralEndpoint(curEnd, curvatureIn, spiralOutLength, family, e2, n2, az2);
        else
            computeReversedSpiralEndpoint(curEnd, curEndCurvature, spiralOutLength, family, e2, n2, az2);

        curEnd.tsc = QStringLiteral("?S");   // 第一字元由下方串接迴圈統一修正
        curEnd.length = spiralOutLength;
        curEnd.curveType = family;
        // 注意：curEnd.radius 刻意不清成 0——若中間沒有圓弧／直線段
        // （curveLength==0），curEnd 就是 afterIn 本身，其 radius
        // （=midCurvature）同時也是 curvatureAtPoint() 對「S 抵達」分支的
        // 判讀依據，代表「抵達本點時的曲率」，清掉會讓之後任何從這點接續
        // 的操作誤判曲率狀態。

        AlignmentPoint afterOut;
        afterOut.tsc = QStringLiteral("ST");
        afterOut.easting  = e2;
        afterOut.northing = n2;
        afterOut.azimuth  = az2;
        afterOut.chainage     = curEnd.chainage     + spiralOutLength;
        afterOut.contChainage = curEnd.contChainage + spiralOutLength;

        toAppend.push_back(curEnd);
        radiiForEach.push_back(curEndCurvature);
        curEnd = afterOut;
        curEndCurvature = curvatureIn;   // 回到本次操作開始前的曲率
    }

    toAppend.push_back(curEnd);
    radiiForEach.push_back(curEndCurvature);

    // ── 寫回：先設定「起點列」（m_points[lastIdx]）的 tsc[1]/length/curveType ──
    m_checkpoints.push_back(m_points.size());
    {
        QString tsc = m_points[lastIdx].tsc;
        if (tsc.size() < 2) tsc = QStringLiteral("TT");
        tsc[1] = QLatin1Char('S');
        m_points[lastIdx].tsc = tsc;
        m_points[lastIdx].length = spiralInLength;
        m_points[lastIdx].radius = 0.0;   // 緩和曲線起始列半徑恆為 0（既有慣例）
        m_points[lastIdx].curveType = family;
    }

    // 逐一修正每個新點的抵達字元（tsc[0] = 前一列的 tsc[1]），最後一點強制
    // 收尾字元 'T'。
    for (int i = 0; i < toAppend.size(); ++i) {
        const QChar prevDepart = (i == 0) ? m_points[lastIdx].tsc[1] : toAppend[i - 1].tsc[1];
        QString tsc = toAppend[i].tsc;
        if (tsc.size() < 2) tsc = QStringLiteral("TT");
        tsc[0] = prevDepart;
        if (i == toAppend.size() - 1)
            tsc[1] = QLatin1Char('T');
        toAppend[i].tsc = tsc;

        m_points.push_back(toAppend[i]);
        m_radiusHistory.push_back(radiiForEach[i]);
    }

    return QString();
}

void AlignmentDrawElementDialog::onAddTransitionGroup()
{
    bool ok1 = false, ok3 = false, ok4 = false;
    const double spiralInLen  = m_transInLengthEdit->text().toDouble(&ok1);
    const double curveLen     = m_transCurveLengthEdit->text().toDouble(&ok3);
    const double spiralOutLen = m_transOutLengthEdit->text().toDouble(&ok4);

    // 半徑欄留白＝無限大（直線）；非空白時才需要成功解析為數字。
    const QString radiusText = m_transCurveRadiusEdit->text().trimmed();
    bool ok2 = true;
    double curveRadius = 0.0;
    if (!radiusText.isEmpty())
        curveRadius = radiusText.toDouble(&ok2);

    if (!(ok1 && ok2 && ok3 && ok4)) {
        m_statusLabel->setText(tr("緩和曲線組欄位必須是數字（圓弧半徑可留白）。"));
        return;
    }

    const QString err = appendTransitionGroup(m_transSpiralCombo->currentText(),
                                               spiralInLen, curveRadius, curveLen, spiralOutLen);
    if (!err.isEmpty()) { m_statusLabel->setText(err); return; }

    m_transInLengthEdit->clear();
    m_transCurveRadiusEdit->clear();
    m_transCurveLengthEdit->setText(QStringLiteral("0"));
    m_transOutLengthEdit->setText(QStringLiteral("0"));
    m_statusLabel->setText(tr("已加入緩和曲線組。"));
    refreshCurrentStateLabel();
    refreshSegmentList();
    refreshTransitionGroupAvailability();
}

// ============================================================================
//  Undo / Finish
// ============================================================================

void AlignmentDrawElementDialog::onUndoLast()
{
    if (m_checkpoints.isEmpty())
        return;

    const int checkpoint = m_checkpoints.takeLast();
    m_points.resize(checkpoint);
    m_radiusHistory.resize(checkpoint);

    m_statusLabel->setText(tr("已復原最後一次加入的線元。"));
    refreshCurrentStateLabel();
    refreshSegmentList();
    refreshTransitionGroupAvailability();
}

void AlignmentDrawElementDialog::onFinish()
{
    if (m_checkpoints.isEmpty()) {
        m_statusLabel->setText(tr("請至少加入一段線元。"));
        return;
    }
    // 收尾：最後一列的 tsc 第二字元本無意義，統一補上 'T' 保持資料格式一致
    // （新增區的邏輯已經這麼做了，這裡是保險）。
    QString tsc = m_points.last().tsc;
    if (tsc.size() < 2) tsc = QStringLiteral("TT");
    tsc[1] = QLatin1Char('T');
    m_points.last().tsc = tsc;

    accept();
}

} // namespace ui
} // namespace aicad
