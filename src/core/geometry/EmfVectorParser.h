/**
 * @file EmfVectorParser.h
 * @brief 把 Windows Enhanced Metafile (EMF) 位元組內容解析成「向量圖元」
 *        （折線／圓／圓弧／橢圓），供 PASTECLIP 把 AutoCAD 複製到剪貼簿的
 *        圖形貼進 AICAD 草圖。
 *
 * 設計重點
 *   - 純 C++17 標準函式庫，不依賴 Qt / OCCT / Windows API，所以可以在任何
 *     平台上用獨立的單元測試驗證（見 test/emf_parser_test.cpp）。
 *   - 輸入是 EMF 檔案原始位元組（Windows 上可用 GetEnhMetaFileBits() 從剪貼簿
 *     取得），輸出座標一律是「毫米、Y 軸向上」，原點是 EMF 裝置座標原點。
 *     ⚠️ EMF 是「圖片」，不帶 CAD 單位——AutoCAD 複製出來的比例取決於複製當下的
 *     螢幕縮放，所以座標只有「形狀」是可信的，絕對尺寸需要使用者再縮放校正
 *     （PASTECLIP 在貼上後提供輸入倍率重新縮放）。
 *   - AutoCAD 通常把圓／圓弧輸出成多段折線，因此提供 recoverArcs：
 *     整條折線若夠密、且所有頂點落在同一個圓上，就還原成真正的 Circle/Arc。
 */
#ifndef AICAD_CORE_GEOMETRY_EMFVECTORPARSER_H
#define AICAD_CORE_GEOMETRY_EMFVECTORPARSER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aicad {
namespace core {
namespace emf {

struct Pt {
    double x = 0.0;
    double y = 0.0;
};

enum class PrimKind { Polyline, Circle, Arc, Ellipse };

struct Primitive {
    PrimKind kind = PrimKind::Polyline;

    /// Polyline：各頂點；Arc：恰好 3 點 {起點, 弧上中點, 終點}
    std::vector<Pt> pts;
    bool   closed = false;     ///< Polyline 是否封閉（最後一點不重複第一點）

    Pt     center;             ///< Circle / Ellipse 圓心
    double r        = 0.0;     ///< Circle 半徑；Ellipse 長軸半徑
    double r2       = 0.0;     ///< Ellipse 短軸半徑
    double angleRad = 0.0;     ///< Ellipse 長軸方向（弧度，逆時針自 +X）
};

struct Options {
    bool   recoverArcs      = true;   ///< 把密集折線還原成圓／弧
    bool   simplify         = true;   ///< Douglas-Peucker 去除共線多餘頂點
    int    bezierSegments   = 16;     ///< 每段三次貝茲曲線攤平成幾段
    int    minCircleVerts   = 16;     ///< 折線至少幾個頂點才考慮還原成圓（避免把正八邊形誤判成圓）
    int    minArcVerts      = 5;      ///< 開放折線至少幾個頂點才考慮還原成弧
    double maxStepDeg       = 30.0;   ///< 還原圓弧時，相鄰頂點張角上限
};

struct Result {
    bool        ok = false;
    std::string error;

    std::vector<Primitive> prims;

    // 範圍（毫米）；ok 時有效
    double minX = 0, minY = 0, maxX = 0, maxY = 0;

    /// 一個裝置單位（像素）折合多少毫米；用作容差基準（座標量化誤差）
    double quantumMm = 0.0;

    // ── 統計／診斷（讓使用者知道哪些東西沒進來）──────────────────────
    int recordCount        = 0;
    int textRecords        = 0;   ///< 文字：EMF 內是字型輪廓，無法還原成文字，已略過
    int bitmapRecords      = 0;   ///< 點陣圖：已略過
    int filledSkipped      = 0;   ///< 只有填色（無筆）的多邊形，例如 HATCH／SOLID，已略過
    int recoveredCircles   = 0;
    int recoveredArcs      = 0;
};

/**
 * 比例尺標記：使用者在 AutoCAD 圖形的左下角畫一個「L」——一條水平線＋一條垂直線，
 * 長度相同（例如 1 單位），共用左下角的角點。EMF 沒有 CAD 單位，用這個已知長度
 * 反推 毫米(EMF) → CAD 單位 的倍率。
 *
 * 判定條件（全部成立才算，避免把一般幾何誤認成標記）：
 *   1. 兩條各為獨立的 2 點開放線（或一條 3 點 L 形折線），一條水平、一條垂直，
 *      都從角點向「右」與向「上」延伸
 *   2. 兩臂長度相等（容差：2 個像素 + 2%）
 *   3. 兩臂「遠端」都沒有接到其他圖形（矩形的角、倒角等會接到其他線，因此被排除）
 *   4. 角點靠近整張圖範圍的左下角（5% 內）
 */
struct ScaleMarker {
    bool   found     = false;
    Pt     corner;            ///< 角點（毫米）
    double lengthMm  = 0.0;   ///< 兩臂平均長度（EMF 毫米）
    double pixels    = 0.0;   ///< 約等於幾個像素；越少則比例精度越差（±1 像素 / pixels）
};

/// 偵測比例尺標記；找到時把標記的線從 r.prims 移除並重算範圍。
ScaleMarker extractScaleMarker(Result& r);

/// 解析 EMF 位元組。失敗時 ok=false 並填 error。
Result parse(const unsigned char* data, std::size_t size,
             const Options& opt = Options());

} // namespace emf
} // namespace core
} // namespace aicad

#endif // AICAD_CORE_GEOMETRY_EMFVECTORPARSER_H
