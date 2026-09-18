/**
 * @file AlignmentQuickCalc.cpp
 * @brief Implementation of computeQuickAlignmentTable() — see AlignmentQuickCalc.h.
 */
#include "AlignmentQuickCalc.h"
#include "RailwayAlignmentElement.h"

#include <QtMath>
#include <cmath>

namespace aicad {
namespace railway {

namespace {

// ============================================================================
//  makeTransitionElementForFamily  (file-local helper)
//
//  curveType 字串 → 具體 TransitionElement 子類別，token 慣例與
//  AlignmentSolver.cpp 的 spiralTypeName()／RailwayAlignmentElement.cpp
//  AlignmentElementFactory::createSpiral() 內的 makeTransition lambda 完全
//  一致（本專案「小型對照表各檔案各自持有一份」的既有慣例，見
//  createSpiral() 附近註解），未知 token 一律退回 ClothoidElement（預設
//  "SPIRAL"）。回傳的元素尚未設定 placement/length/radius/reversed，由呼叫
//  端視情境個別設定。
// ============================================================================
std::unique_ptr<TransitionElement> makeTransitionElementForFamily(const QString& ct)
{
    if      (ct == QLatin1String("HALFSINE"))         return std::make_unique<HalfSineElement>();
    else if (ct == QLatin1String("PARABOLA"))         return std::make_unique<ParabolaElement>();
    else if (ct == QLatin1String("CUBICJPN"))         return std::make_unique<CubicJPNElement>();
    else if (ct == QLatin1String("CUBICECI"))         return std::make_unique<CubicECIElement>();
    else if (ct == QLatin1String("SINUSOIDAL"))       return std::make_unique<SinusoidalElement>();
    else if (ct == QLatin1String("COSINE"))           return std::make_unique<CosineElement>();
    else if (ct == QLatin1String("BLOSS"))            return std::make_unique<BlossElement>();
    else if (ct == QLatin1String("LEMNISCATE"))       return std::make_unique<LemniscateElement>();
    else if (ct == QLatin1String("WIENERBOGEN"))      return std::make_unique<WienerBogenElement>();
    else if (ct == QLatin1String("RADIOID"))          return std::make_unique<RadioidElement>();
    else if (ct == QLatin1String("ELASRADIOID"))      return std::make_unique<ElasticRadioidElement>();
    else if (ct == QLatin1String("NORWICHSTURM"))     return std::make_unique<NorwichSturmElement>();
    else if (ct == QLatin1String("PSEUELLRADIOID"))   return std::make_unique<PseudoEllipticRadioidElement>();
    else if (ct == QLatin1String("LOGARITHMIC"))      return std::make_unique<LogarithmicElement>();
    else if (ct == QLatin1String("HYPERBOLIC"))       return std::make_unique<HyperbolicElement>();
    else if (ct == QLatin1String("POLYNOMIAL"))       return std::make_unique<PolynomialElement>();
    else if (ct == QLatin1String("QUINTIC"))          return std::make_unique<QuinticElement>();
    else if (ct == QLatin1String("PHQUINTIC"))        return std::make_unique<PHQuinticElement>();
    else if (ct == QLatin1String("BIQUADRATIC"))      return std::make_unique<BiquadraticElement>();
    else if (ct == QLatin1String("SPLINE"))           return std::make_unique<SplineElement>();
    else if (ct == QLatin1String("BLOSSEULERHYBRID")) return std::make_unique<BlossEulerHybridElement>();
    return std::make_unique<ClothoidElement>();   // 預設 "SPIRAL" / 未知 token
}

/** curveType → ElementType，供 EggTransitionElement 的 spiralFamily 使用。 */
ElementType curveTypeToElementType(const QString& ct)
{
    if      (ct == QLatin1String("HALFSINE"))         return ElementType::HalfSine;
    else if (ct == QLatin1String("PARABOLA"))         return ElementType::Parabola;
    else if (ct == QLatin1String("CUBICJPN"))         return ElementType::CubicJPN;
    else if (ct == QLatin1String("CUBICECI"))         return ElementType::CubicECI;
    else if (ct == QLatin1String("SINUSOIDAL"))       return ElementType::Sinusoidal;
    else if (ct == QLatin1String("COSINE"))           return ElementType::Cosine;
    else if (ct == QLatin1String("BLOSS"))            return ElementType::Bloss;
    else if (ct == QLatin1String("LEMNISCATE"))       return ElementType::Lemniscate;
    else if (ct == QLatin1String("WIENERBOGEN"))      return ElementType::WienerBogen;
    else if (ct == QLatin1String("RADIOID"))          return ElementType::Radioid;
    else if (ct == QLatin1String("ELASRADIOID"))      return ElementType::ElasticRadioid;
    else if (ct == QLatin1String("NORWICHSTURM"))     return ElementType::NorwichSturm;
    else if (ct == QLatin1String("PSEUELLRADIOID"))   return ElementType::PseudoEllipticRadioid;
    else if (ct == QLatin1String("LOGARITHMIC"))      return ElementType::Logarithmic;
    else if (ct == QLatin1String("HYPERBOLIC"))       return ElementType::Hyperbolic;
    else if (ct == QLatin1String("POLYNOMIAL"))       return ElementType::Polynomial;
    else if (ct == QLatin1String("QUINTIC"))          return ElementType::Quintic;
    else if (ct == QLatin1String("PHQUINTIC"))        return ElementType::PHQuintic;
    else if (ct == QLatin1String("BIQUADRATIC"))      return ElementType::Biquadratic;
    else if (ct == QLatin1String("SPLINE"))           return ElementType::Spline;
    else if (ct == QLatin1String("BLOSSEULERHYBRID")) return ElementType::BlossEulerHybrid;
    return ElementType::Clothoid;
}

bool isValidRadius(double r)
{
    return std::isfinite(r) && std::abs(r) > 1e-3;
}

/// asPatternChar：'S' 視同 'T'（比照 AlignmentElementFactory::createSpiral()
/// 對 SS 交會點的處理——曲率歸零的虛擬切線點，與真正的直線在型別判斷上等價）。
QChar asPatternChar(QChar c) { return (c == QLatin1Char('S')) ? QChar('T') : c; }

/**
 * @brief "CS"（反算）分支：由已知的圓弧側端點（cur）反算出緩和曲線遠端
 *        （切線側端點，next）的座標與方位角。
 *
 * 對應 VBA else 分支：
 *   OALFA = sa(0, LE, R1, LE, caldms(amz), SType)
 *   y = sy(0, 0, 0, ElementLength, -R1, LE, 0, 90, SType)
 *   x = sx(0, 0, 0, ElementLength, -R1, LE, 0, 90, SType)
 *   next.X = tx(X1, Y1, OALFA, 0, x, -y)
 *   next.Y = ty(X1, Y1, OALFA, 0, x, -y)
 *   next.azimuth = OALFA
 *
 * 本移植版改用 TransitionElement 本身的 localFrame()（帶入 reversed=true，
 * 讓各家族的內部符號翻轉——見 ClothoidElement::localFrame() 等——自動套
 * 用），取代 VBA 手刻的 90° 參考角三角函數技巧，可套用到全部曲線族：
 *
 *   設 lf = elem(reversed=true, radius=R1, length=LE).localFrame(LE)
 *   （L=LE 即完整長度，對應「反向元素」查詢起點 cur 所在的 L，
 *    見 AlignmentElement::resolvePW() 對 m_reversed 的處理）。
 *
 *   worldAzimuth 關係（見 AlignmentElement::worldAzimuth()）：
 *     ALFA1 = normalise(placeAz + π + lf.theta)
 *     ⇒ placeAz = normalise(ALFA1 − π − lf.theta)
 *     ⇒ 遠端（next）真實方位角 azNext = normalise(placeAz + π)
 *                                    = normalise(ALFA1 − lf.theta)
 *
 *   worldXY 關係（w=0 時 X1=lf.x, Y1=lf.y，見 AlignmentElement::
 *   localToWorld()）：
 *     cur.E = next.E + X1·sin(placeAz) + Y1·cos(placeAz)
 *     cur.N = next.N + X1·cos(placeAz) − Y1·sin(placeAz)
 *     ⇒ next.E = cur.E − [X1·sin(placeAz) + Y1·cos(placeAz)]
 *     ⇒ next.N = cur.N − [X1·cos(placeAz) − Y1·sin(placeAz)]
 */
void computeReversedSpiralEndpointImpl(const AlignmentPoint& cur,
                                        double R1, double LE, const QString& curveType,
                                        double& outE, double& outN, double& outAz)
{
    auto elem = makeTransitionElementForFamily(curveType);
    elem->setLength(LE);
    elem->setRadius(R1);
    elem->setReversed(true);

    const LocalFrame lf = elem->localFrame(LE);
    const double X1 = lf.x;
    const double Y1 = lf.y;

    const double placeAz = AlignmentElement::normalise(cur.azimuth - M_PI - lf.theta);
    outAz = AlignmentElement::normalise(placeAz + M_PI);

    outE = cur.easting  - (X1 * std::sin(placeAz) + Y1 * std::cos(placeAz));
    outN = cur.northing - (X1 * std::cos(placeAz) - Y1 * std::sin(placeAz));
}

/// 正向緩和曲線（曲率 0 → targetRadius）之遠端座標／方位角，直接以
/// TransitionElement 的一般（非反向）placement 正算，對應 VBA "TS"/"SS" 分支。
void computeForwardSpiralEndpointImpl(const AlignmentPoint& start, double targetRadius,
                                       double length, const QString& curveType,
                                       double& outE, double& outN, double& outAz)
{
    auto elem = makeTransitionElementForFamily(curveType);
    elem->setPlacement({start.chainage, start.easting, start.northing, start.azimuth});
    elem->setLength(length);
    elem->setRadius(targetRadius);
    elem->setReversed(false);
    const QPointF xy = elem->worldXY(start.chainage + length);
    outE  = xy.x();
    outN  = xy.y();
    outAz = elem->worldAzimuth(start.chainage + length);
}

} // namespace

void computeForwardSpiralEndpoint(const AlignmentPoint& start, double targetRadius,
                                   double length, const QString& curveType,
                                   double& outE, double& outN, double& outAz)
{
    computeForwardSpiralEndpointImpl(start, targetRadius, length, curveType, outE, outN, outAz);
}

void computeReversedSpiralEndpoint(const AlignmentPoint& start, double R1,
                                    double length, const QString& curveType,
                                    double& outE, double& outN, double& outAz)
{
    computeReversedSpiralEndpointImpl(start, R1, length, curveType, outE, outN, outAz);
}

// ============================================================================
//  computeQuickAlignmentTable
// ============================================================================

bool computeQuickAlignmentTable(QVector<AlignmentPoint>& pts, QString* errorOut)
{
    auto fail = [&](int rowOneBased, const QString& msg) -> bool {
        if (errorOut) {
            *errorOut = QObject::tr("第 %1 列資料錯誤：%2").arg(rowOneBased).arg(msg);
        }
        return false;
    };

    if (pts.size() < 2)
        return fail(1, QObject::tr("至少需要 2 列（起點 + 至少一個線元）"));

    for (int i = 0; i < pts.size() - 1; ++i) {
        AlignmentPoint& cur  = pts[i];
        AlignmentPoint& next = pts[i + 1];

        if (cur.tsc.size() < 2)
            return fail(i + 1, QObject::tr("點位代碼「%1」不是合法的 2 字元 TSC 代碼").arg(cur.tsc));

        const double len = cur.length;
        if (!(len > 0.0))
            return fail(i + 1, QObject::tr("長度必須大於 0"));

        const QChar arrive = cur.tsc[0];
        const QChar depart = cur.tsc[1];

        double outE = 0.0, outN = 0.0, outAz = 0.0;

        if (depart == QLatin1Char('T')) {
            // ── "TT"/"CT"/"ST"：切線 ────────────────────────────────────────
            TangentElement elem;
            elem.setPlacement({cur.chainage, cur.easting, cur.northing, cur.azimuth});
            elem.setLength(len);
            const QPointF xy = elem.worldXY(cur.chainage + len);
            outE  = xy.x();
            outN  = xy.y();
            outAz = elem.worldAzimuth(cur.chainage + len);

        } else if (depart == QLatin1Char('C')) {
            // ── "TC"/"CC"/"SC"：圓弧，半徑取本列 radius 欄 ───────────────────
            if (!isValidRadius(cur.radius))
                return fail(i + 1, QObject::tr("圓弧半徑無效：%1").arg(cur.radius));
            CircularArcElement elem(cur.radius);
            elem.setPlacement({cur.chainage, cur.easting, cur.northing, cur.azimuth});
            elem.setLength(len);
            const QPointF xy = elem.worldXY(cur.chainage + len);
            outE  = xy.x();
            outN  = xy.y();
            outAz = elem.worldAzimuth(cur.chainage + len);

        } else if (depart == QLatin1Char('S')) {
            if (asPatternChar(arrive) == QLatin1Char('C')) {
                // ── "CS" ──────────────────────────────────────────────────
                if (i == 0)
                    return fail(i + 1, QObject::tr("CS 列之前必須有圓弧列以取得入弧半徑"));
                const AlignmentPoint& prevArc = pts[i - 1];
                if (prevArc.tsc.size() < 2 || asPatternChar(prevArc.tsc[1]) != QLatin1Char('C'))
                    return fail(i + 1, QObject::tr("CS 列之前一列必須是圓弧列（TC/CC/SC）"));
                const double R1 = prevArc.radius;
                if (!isValidRadius(R1))
                    return fail(i + 1, QObject::tr("入弧半徑無效：%1").arg(R1));

                const bool nextIsSC =
                    (next.tsc.size() >= 2 && next.tsc.left(2) == QLatin1String("SC"));

                if (nextIsSC) {
                    // 複合曲線（圓→緩和→圓，Egg）：R2 取下一列（SC）radius 欄，
                    // placement 直接錨定在本點（cur），無需反算。
                    const double R2 = next.radius;
                    if (!isValidRadius(R2))
                        return fail(i + 2, QObject::tr("出弧半徑無效：%1").arg(R2));
                    const ElementType family = curveTypeToElementType(cur.curveType);
                    EggTransitionElement elem(R1, R2, len, family);
                    elem.setPlacement({cur.chainage, cur.easting, cur.northing, cur.azimuth});
                    elem.setLength(len);
                    const QPointF xy = elem.worldXY(cur.chainage + len);
                    outE  = xy.x();
                    outN  = xy.y();
                    outAz = elem.worldAzimuth(cur.chainage + len);
                } else {
                    // 反算緩和曲線（圓→緩和→切線）：見 computeReversedSpiralEndpointImpl()。
                    computeReversedSpiralEndpointImpl(cur, R1, len, cur.curveType, outE, outN, outAz);
                }

            } else {
                // ── "TS"/"SS"：正向緩和曲線，目標半徑取「下一列」radius 欄 ───
                const double targetR = next.radius;
                if (!isValidRadius(targetR))
                    return fail(i + 2, QObject::tr("緩和曲線目標半徑無效：%1").arg(targetR));
                computeForwardSpiralEndpointImpl(cur, targetR, len, cur.curveType, outE, outN, outAz);
            }

        } else {
            return fail(i + 1, QObject::tr("無法辨識的點位代碼「%1」").arg(cur.tsc));
        }

        next.easting      = outE;
        next.northing     = outN;
        next.chainage      = cur.chainage     + len;
        next.contChainage  = cur.contChainage + len;
        next.azimuth       = outAz;
    }

    return true;
}

// ============================================================================
//  curvatureAtPoint
// ============================================================================

double curvatureAtPoint(const QVector<AlignmentPoint>& pts, int idx)
{
    if (idx <= 0 || idx >= pts.size())
        return 0.0;   // 文件起點（或索引異常）一律假設位於曲率 0

    const QString& tsc = pts[idx].tsc;
    const QChar arrive = tsc.size() >= 1 ? tsc[0] : QLatin1Char('T');

    if (arrive == QLatin1Char('C')) {
        // 圓弧列的半徑存在「起始該圓弧」的列，也就是 idx-1。
        return (idx - 1 >= 0) ? pts[idx - 1].radius : 0.0;
    }
    // T／S 抵達：曲率就是本點自身的 radius 欄（正向緩和曲線落在圓弧上時，
    // 目標半徑依 AQT／VBA 慣例存在「下一列」，也就是 idx 自己）；
    // 切線抵達或反向緩和曲線落回切線時，此欄自然是 0。
    return pts[idx].radius;
}

// ============================================================================
//  reverseAlignmentPoints
// ============================================================================

QVector<AlignmentPoint> reverseAlignmentPoints(const QVector<AlignmentPoint>& pts)
{
    const int n = pts.size();
    if (n < 2)
        return pts;

    QVector<AlignmentPoint> rev(n);
    const double endChainage = pts.last().chainage;
    const double endCont     = pts.last().contChainage;

    // 第一輪：座標／方位角／里程反轉（每一點獨立計算，與線元型別無關）；
    // length/radius/curveType/tsc 先清空，第二輪再依「反轉後的線元」補上。
    for (int k = 0; k < n; ++k) {
        const int i = n - 1 - k;
        AlignmentPoint p = pts[i];   // 拷貝其餘輔助欄位（cant/text1/real1…）
        p.azimuth       = AlignmentElement::normalise(pts[i].azimuth + M_PI);
        p.chainage      = endChainage - pts[i].chainage;
        p.contChainage  = endCont     - pts[i].contChainage;
        p.length = 0.0;
        p.radius = 0.0;
        p.curveType.clear();
        p.tsc = QStringLiteral("TT");
        rev[k] = p;
    }

    // 第二輪：rev[k] → rev[k+1] 對應原始線元 p[i] → p[i+1]（i = n-2-k），
    // 反過來走；型別不變，圓弧半徑正負號反轉，緩和曲線類型不變。
    for (int k = 0; k + 1 < n; ++k) {
        const int i = n - 2 - k;
        const QChar depart = (pts[i].tsc.size() >= 2) ? pts[i].tsc[1] : QLatin1Char('T');

        QString tsc = rev[k].tsc;
        tsc[1] = depart;
        rev[k].tsc = tsc;
        rev[k].length = pts[i].length;

        if (depart == QLatin1Char('C')) {
            rev[k].radius = -pts[i].radius;   // 方向反過來走，左右彎互換
        } else if (depart == QLatin1Char('S')) {
            rev[k].curveType = pts[i].curveType.isEmpty() ? QStringLiteral("SPIRAL")
                                                            : pts[i].curveType;
            // radius 恆為 0（緩和曲線起始列的既有慣例），抵達曲率改由下一輪
            // 透過 rev[k+1].radius 表示。
        }
    }

    // 第三輪：補上抵達字元（tsc[0] = 前一列的 tsc[1]），以及緩和曲線抵達點
    // 的曲率（rev[k].radius，供 curvatureAtPoint() 之後接續使用時的 'S'
    // 抵達分支讀取）＝原始對應點曲率（在原始方向下）取負號。
    for (int k = 1; k < n; ++k) {
        QString tsc = rev[k].tsc;
        tsc[0] = rev[k - 1].tsc[1];
        rev[k].tsc = tsc;

        if (tsc[0] == QLatin1Char('S')) {
            const int i = n - 1 - k;   // rev[k] 對應原始 p[i]
            rev[k].radius = -curvatureAtPoint(pts, i);
        }
    }

    return rev;
}

} // namespace railway
} // namespace aicad
