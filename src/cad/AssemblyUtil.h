/**
 * @file AssemblyUtil.h
 * @brief 組合功能的小工具：把文件中的零件位姿轉成求解器的 Body、把參照轉成求解器的 Ref、
 *        量測目前值（供對話框預填）、以及把 3D 視圖選到的子形轉成「零件本地座標」的參照。
 */
#pragma once

#include "AssemblyGeom.h"
#include "AssemblyMate.h"
#include "AssemblySolver.h"

#include <QHash>
#include <QString>
#include <TopoDS_Shape.hxx>

namespace aicad {
namespace cad {

class Document;

namespace assemblyutil {

/// 由文件中全部零件組出求解問題的 bodies 與 lengthScale；idx = 零件 id → bodies 索引
void buildBodies(const Document* doc, asmsolve::Problem& pr, QHash<QString, int>& idx);

/// 參照 → 求解器的 Ref（世界 → body = -1；找不到零件 → body = -2，視為無效）
asmsolve::Ref toSolverRef(const MateRefData& r, const QHash<QString, int>& idx);

/// 以目前零件位姿量測：距離（組合單位，可為負的有號值）或夾角（度）；並建議最接近的取向。
/// 參照不完整／配合沒有數值 → value 不變（回傳 false）。
bool measure(const Document* doc, const AssemblyMate::Data& d, double& value, asmsolve::Orient& suggested);

/// 一個配合在 3D 視圖的標示資料（世界座標）：兩個參照各自的點／軸線／平面
struct MateMarker {
    enum State { Ok = 0, Error = 1, Suppressed = 2 };
    QString mateId;
    asmsolve::RefKind kind[2] = { asmsolve::RefKind::Point, asmsolve::RefKind::Point };
    double p[2][3] = { {0, 0, 0}, {0, 0, 0} };
    double d[2][3] = { {0, 0, 1}, {0, 0, 1} };
    State  state = Ok;
};

/// 依零件目前位姿，建立全部配合的標示資料；sizeOut = 建議的標示大小（約為最大零件尺寸的 3%）
QVector<MateMarker> buildMarkers(const Document* doc, double* sizeOut = nullptr);

/// 3D 視圖選到的（放置後的）子形 → 該零件本地座標下的凍結參照（含面／邊／頂點序號）
bool refFromPickedShape(const Document* doc, const QString& partId, const TopoDS_Shape& placedSub,
                        MateRefData& out, QString* error = nullptr);

} // namespace assemblyutil
} // namespace cad
} // namespace aicad
