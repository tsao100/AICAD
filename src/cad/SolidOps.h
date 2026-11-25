/**
 * @file SolidOps.h
 * @brief 3D 塑形／挖除功能共用的幾何工具（旋轉、掃掠、放樣、圓角、抽殼、布林…共用）。
 *
 * 內容：
 *   • SolidOp            ── 運算類型：新實體／聯集／挖除／交集
 *   • profileFaces()     ── 由草圖取出輪廓面（沿用 Extrude 的取面規則，但區域索引會存檔）
 *   • pathWireFromSketch ── 由草圖把所有邊接成一條路徑（掃掠用）
 *   • applyOp()          ── 對目標實體做布林運算
 *   • finalizeSolid()    ── 校正實體方向、確認結果真的含有實體
 *   • FaceSignature／matchFace／matchEdge ── 以幾何簽章在重建後的新 shape 中找回「同一個」面／邊
 */
#pragma once

#include "ChamferSolid.h"      // EdgeSignature

#include <QString>
#include <QList>
#include <QJsonObject>

#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>

namespace aicad {
namespace cad {

class Sketch;
class Document;

/**
 * @brief 成形特徵與「目標實體」之間的運算方式
 */
enum class SolidOp {
    NewBody = 0,   ///< 產生獨立的新實體（不使用目標）
    Fuse    = 1,   ///< 聯集：把新形狀加到目標實體上
    Cut     = 2,   ///< 挖除：從目標實體扣掉新形狀
    Common  = 3    ///< 交集：只保留與目標實體重疊的部分
};

/**
 * @brief 一個面的定址資訊（抽殼選面用；定址方式與 EdgeSignature 相同——幾何簽章配對）
 */
struct FaceSignature {
    gp_Pnt center;                       ///< 面的質心（世界座標）
    double area   = 0.0;                 ///< 面積
    gp_Dir normal = gp_Dir(0, 0, 1);     ///< 平面的外法向（planar 時有效）
    bool   planar = false;
};

namespace solidops {

// ── 運算類型 ──────────────────────────────────────────────────────────
QString opToString(SolidOp op);
SolidOp opFromString(const QString& s);
QString opLabel(SolidOp op);             ///< 中文名稱（對話框／屬性面板顯示）

// ── 運算式 ────────────────────────────────────────────────────────────
/// 數字或參數運算式求值；空字串或無法求值回傳 false
bool evalExpression(Document* doc, const QString& expr, double* out);

// ── 草圖 → 輪廓 ───────────────────────────────────────────────────────
struct RegionInfo {
    int    index = 0;
    double area  = 0.0;
    double cx = 0.0, cy = 0.0;           ///< 質心（草圖座標）
};

/// 草圖內各封閉區域的摘要（與 profileFaces() 的 regionIndex 同一順序）
QList<RegionInfo> describeRegions(Sketch* sketch);

/**
 * @param regionIndex  >= 0：只取該區域；否則（-1）取全部區域
 * 草圖沒有可偵測的區域時，退回「把所有邊接成封閉線圈」。
 */
QList<TopoDS_Face> profileFaces(Sketch* sketch, int regionIndex, QString* err);

/// 放樣用：取草圖最大的一個輪廓面的外環
bool sectionWireFromSketch(Sketch* sketch, TopoDS_Wire& out, QString* err);

/// 掃掠用：把草圖所有邊接成一條（最長的）路徑線，可開放亦可封閉
bool pathWireFromSketch(Sketch* sketch, TopoDS_Wire& out, QString* err);

// ── 形狀處理 ──────────────────────────────────────────────────────────
bool containsSolid(const TopoDS_Shape& s);

/**
 * @brief 取出 s 內所有實體、校正方向（體積為負者翻轉），多個時包成 compound。
 *        s 只有殼（Shell）時嘗試縫成實體。沒有任何實體回傳 Null 並填 err。
 */
TopoDS_Shape finalizeSolid(const TopoDS_Shape& s, QString* err);

/// 布林運算。NewBody 直接回傳 tool。成功時會合併共平面的面。
TopoDS_Shape applyOp(const TopoDS_Shape& target, const TopoDS_Shape& tool,
                     SolidOp op, QString* err);

/// 一個目標、多個工具（工具之間可以互相重疊）。Common 會先把工具聯集。成功時會合併共面。
TopoDS_Shape applyOpMulti(const TopoDS_Shape& target, const QList<TopoDS_Shape>& tools,
                          SolidOp op, QString* err);

/// 把多個形狀聯集成一個（只有一個時原樣回傳）；不做面合併
TopoDS_Shape fuseAll(const QList<TopoDS_Shape>& shapes, QString* err);

/**
 * @brief 拔模擠出（錐度）：沿 dir 擠出 face，輪廓隨擠出距離等距內縮／外擴。
 * @param angleDeg  正 = 越擠越小（向內收），負 = 越擠越大
 * 目前只支援沒有內孔的輪廓（單一外環）。失敗回傳 Null 並填 err。
 */
TopoDS_Shape taperedPrism(const TopoDS_Face& face, const gp_Vec& dir,
                          double angleDeg, QString* err);

// ── 簽章 ──────────────────────────────────────────────────────────────
FaceSignature makeFaceSignature(const TopoDS_Face& face);
TopoDS_Face   matchFace(const TopoDS_Shape& shape, const FaceSignature& sig);
TopoDS_Edge   matchEdge(const TopoDS_Shape& shape, const EdgeSignature& sig);

QJsonObject   toJson(const FaceSignature& sig);
FaceSignature faceSignatureFromJson(const QJsonObject& o);
QJsonObject   toJson(const EdgeSignature& sig);
EdgeSignature edgeSignatureFromJson(const QJsonObject& o);

} // namespace solidops
} // namespace cad
} // namespace aicad
