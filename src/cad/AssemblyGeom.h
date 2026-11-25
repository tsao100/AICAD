/**
 * @file AssemblyGeom.h
 * @brief 組合（Assembly）用的幾何參照：從 OCCT 的面／邊／頂點取出「點／軸線／平面」，以及基準（原點、軸、座標平面）。
 *
 * 參照以「零件本地（原生）座標」凍結保存（點 p、方向 d），求解器只用這份幾何，因此不依賴拓撲是否仍存在；
 * 同時記下面／邊／頂點在零件形狀中的序號（TopExp::MapShapes 的索引），來源檔同步後可依序號重新取得最新幾何。
 *
 * 對應規則：
 *   平面            → Plane（點 = 平面上一點、d = 面的「外向」法向量，反向面已翻轉）
 *   圓柱／圓錐／環面 → Axis（軸線）；球面 → Point（球心）
 *   直線邊          → Axis；圓／圓弧邊 → Axis（圓心、沿圓的法線）；其他曲線不支援
 *   頂點            → Point
 * 基準：原點（Point）、X／Y／Z 軸（Axis）、XY／YZ／XZ 平面（Plane；法向量 +Z、+X、-Y，與 PlaneManager 的標準平面一致）。
 */
#pragma once

#include "AssemblySolver.h"

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <TopoDS_Shape.hxx>

namespace aicad {
namespace cad {

/// 參照來源的種類
enum class RefSource { Datum = 0, Face = 1, Edge = 2, Vertex = 3 };

struct MateRefData {
    QString ownerId;                    ///< AssemblyPart 的 id；空字串 = 世界
    asmsolve::RefKind kind = asmsolve::RefKind::Point;
    RefSource source = RefSource::Datum;
    int     topoIndex = -1;             ///< Face／Edge／Vertex：在零件本地形狀中的序號（1 起算）；-1 = 無
    QString datum;                      ///< Datum：origin／x／y／z／xy／yz／xz
    double  p[3] = {0, 0, 0};           ///< 零件本地（原生）座標；世界參照則為世界座標
    double  d[3] = {0, 0, 1};
    QString label;                      ///< 顯示用，例如「平面 #12」「圓柱軸 #3（R=10）」

    bool isWorld() const { return ownerId.isEmpty(); }
    QJsonObject toJson() const;
    static MateRefData fromJson(const QJsonObject& o);
};

namespace assemblygeom {

/// 基準（owner 為空 = 世界基準）。datum：origin／x／y／z／xy／yz／xz
MateRefData datumRef(const QString& datum, const QString& ownerId);
QVector<MateRefData> allDatumRefs(const QString& ownerId);       ///< 7 個基準
QString datumLabel(const QString& datum);

/// 由一個子形（Face／Edge／Vertex，座標即其目前所在座標）取出參照；p、d 為該座標下的值。
bool fromSubShape(const TopoDS_Shape& sub, MateRefData& out, QString* error = nullptr);

/// 列舉形狀中可用的參照（Face／Edge／Vertex），座標為形狀本身的座標；每種最多 maxPerKind 個。
QVector<MateRefData> enumerate(const TopoDS_Shape& shape, int maxPerKind = 300);

/// 依 topoIndex 從（更新後的）形狀重新取得幾何。種類不同或索引超出 → false（保留原幾何）。
bool refresh(const TopoDS_Shape& shape, MateRefData& ref);

/// 子形在形狀中的序號（1 起算）；找不到 -1。type = TopAbs_FACE／EDGE／VERTEX
int indexOf(const TopoDS_Shape& shape, const TopoDS_Shape& sub);

/// 包圍盒中心與對角線長
bool boundsOf(const TopoDS_Shape& shape, double center[3], double* diagonal);

QString shapeToBase64(const TopoDS_Shape& s);
TopoDS_Shape shapeFromBase64(const QString& b64);

} // namespace assemblygeom
} // namespace cad
} // namespace aicad
