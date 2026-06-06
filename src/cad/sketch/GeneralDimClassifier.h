#pragma once
#include "SketchConstraint.h"
#include <QList>
#include <QString>

namespace aicad::cad {

class Sketch;

/**
 * @brief GDIM 型別推斷器
 *
 * 依目前已選取的 GeomRef 列表，推斷應施加的 ConstraintType 及相關參數。
 */
class GeneralDimClassifier {
public:
    struct Result {
        ConstraintType type       = ConstraintType::FixedDistance;
        DistanceMode   distMode   = DistanceMode::PointToPoint;
        bool           needMore   = false;   ///< true = 需要再選一個幾何
        QString        nextPrompt;           ///< needMore=true 時顯示的提示
        bool           valid      = false;
    };

    /**
     * 依目前已選取的 refs 推斷類型。
     * refs.size()==0 → 回傳 needMore=true
     * refs.size()==1 → classifySingle
     * refs.size()==2 → classifyPair
     */
    /// allowHorizVert: true 時才允許自動選 FixedHorizDist/FixedVertDist（需按 Shift）
    static Result classify(const QList<GeomRef>& refs, const Sketch* sketch,
                           bool allowHorizVert = false);

    /// 初始提示文字（命令啟動時顯示）
    static QString initialPrompt() {
        return "GDIM 選取幾何元素（線段 / 圓 / 弧 / 點）";
    }

    /// 判斷 ref 是否為點狀（Point、Line.Start/End、Arc.Start/End/Center）
    static bool isPointLike(const GeomRef& r, const Sketch* sketch);

private:
    static Result classifySingle(const GeomRef& r, const Sketch* sketch);
    static Result classifyPair  (const GeomRef& a, const GeomRef& b,
                                  const Sketch* sketch,
                                  bool allowHorizVert = false);
};

} // namespace aicad::cad