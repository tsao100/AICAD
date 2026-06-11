#pragma once
#include "SketchConstraint.h"
#include <QList>
#include <QString>

namespace aicad::cad {

class Sketch;

/**
 * @brief GDIM 型別推斷器
 */
class GeneralDimClassifier {
public:

    /// 選單類型（需要彈出選項讓使用者選擇）
    enum class MenuKey {
        None,
        LineType,     ///< 整條線：線長(L) / 量距第二點(D)
        CircleType,   ///< 整個圓：直徑(D) / 半徑(R)
        ArcType,      ///< 整條弧：半徑(R) / 弧長(L)
        PointCoord,   ///< 點/圓心/弧圓心：FixedX(X) / FixedY(Y) / CoordinateDim(C) / 量距(D)
    };

    struct Result {
        ConstraintType type       = ConstraintType::FixedDistance;
        DistanceMode   distMode   = DistanceMode::PointToPoint;
        bool           needMore   = false;   ///< true = 需要再選第二個幾何
        bool           needMenu   = false;   ///< true = 需要彈出選單讓使用者選類型
        MenuKey        menuKey    = MenuKey::None;
        QString        nextPrompt;
        bool           valid      = false;
    };

    static Result classify(const QList<GeomRef>& refs, const Sketch* sketch,
                           bool allowHorizVert = false);

    static QString initialPrompt() {
        return "GDIM 選取幾何元素（線段 / 圓 / 弧 / 點）";
    }

    static bool isPointLike(const GeomRef& r, const Sketch* sketch);

private:
    static Result classifySingle(const GeomRef& r, const Sketch* sketch);
    static Result classifyPair  (const GeomRef& a, const GeomRef& b,
                                  const Sketch* sketch);
};

} // namespace aicad::cad