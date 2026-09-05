#pragma once
#include "SketchConstraint.h"
#include <QHash>
#include <Eigen/Dense>   // 或自行用 QVector<double>
#include <gp_Dir.hxx>

namespace aicad::cad {

class Sketch;   // ✅ forward declaration
struct SketchGeometry;
enum class SketchGeometryType;

// ─────────────────────────────────────────────────────────────────────────────
// 變數向量佈局
// 每個幾何元素的 DOF 在扁平向量中的起始 index
// ─────────────────────────────────────────────────────────────────────────────
struct GeomVarLayout {
    int offset;   ///< 在變數向量的起始位置
    int dof;      ///< DOF 數量
    /// SketchLine 端點共用 SketchPoint 的 offset（非 -1 時覆蓋 Start/End）
    int startOffset = -1;  ///< Start handle 對應的 vars index（SketchPoint.x）
    int endOffset   = -1;  ///< End   handle 對應的 vars index（SketchPoint.x）

    /// Arc 專用：求解前（pack 當下）的原始簽名掃角 t1_orig - t0_orig（弧度）。
    /// 純粹作為分支判斷用的參考常數，不是求解變數——用來讓
    /// FixedArcLengthEquation 判斷這是優弧（>π）還是劣弧，並讓
    /// unpackVariables() 用「角度差」而非「絕對角度」重建 t0/t1，避免
    /// t0/t1 直接進入 Newton 求解變數（見下方 Arc 版面說明的修正註解）。
    double refSweep = 0.0;

    // 語意 accessor（依 GeomHandle 取 index）
    // Line:    offset+0=x1, +1=y1, +2=x2, +3=y2（或共用 SketchPoint DOF）
    // Circle:  offset+0=cx, +1=cy, +2=r
    // Arc:     offset+0=cx, +1=cy, +2=r（Start/End 一律透過 indexFor(Start/End)
    //          取得，指向共用的 SketchPoint DOF，或找不到對應點時的局部
    //          fallback 變數；t0/t1 兩個角度「不」在求解變數之列——早期版本
    //          把 t0/t1 也當成獨立求解變數並用 cx+r·cos(t) 這類三角函數方
    //          程式與 Start/End 綁定，會讓「弧度」與「座標/長度」這種量級
    //          差異懸殊的量混在同一個最小平方系統裡求解，Moore-Penrose 偽
    //          逆在殘差極小甚至為零時仍可能因為系統病態（ill-conditioned）
    //          而選出一個數值上很小、換算成座標卻很大的 Δt，造成「弧長沒
    //          改，起終點/圓心卻大幅跳動」的現象。改為只用 cx,cy,r 三個
    //          自然座標量作為 Arc 自身的獨立變數，t0/t1 只在求解「結束後」
    //          於 unpackVariables() 用角度差的方式重建，徹底避免這個問題。
    // Ellipse: offset+0=cx, +1=cy, +2=majorR, +3=minorR, +4=angle
    int indexFor(GeomHandle h) const;
};

// ─────────────────────────────────────────────────────────────────────────────
// 約束方程式介面（每個 SketchConstraint → 一或多條方程式）
// ─────────────────────────────────────────────────────────────────────────────
class ConstraintEquation {
public:
    virtual ~ConstraintEquation() = default;

    ConstraintEquation(const SketchConstraint& c,
                       const QHash<QString, GeomVarLayout>* layout)
        : m_constraint(c), m_layout(layout) {}

    /**
     * 方程式數量（此約束貢獻幾條 F(x)=0）
     */
    virtual int equationCount() const = 0;

    /**
     * 計算殘差向量 F(x)
     * @param vars  目前的全域變數向量
     * @param out   輸出殘差（大小必須 == equationCount()）
     */
    virtual void evaluate(const QVector<double>& vars, QVector<double>& out) const = 0;

    /**
     * 計算雅可比矩陣的對應行（稀疏，只填非零項）
     * @param vars      目前變數向量
     * @param row0      此約束的起始行號
     * @param jacobian  輸出（行 × 列，列數 = 全域 DOF 數）
     */
    virtual void jacobian(const QVector<double>& vars,
                          int row0,
                          QVector<QVector<double>>& jacobian) const = 0;

    const SketchConstraint& constraint() const { return m_constraint; }
    const QHash<QString, GeomVarLayout>& layout() const { return *m_layout; }

protected:
    // 取得參考幾何的某個 DOF 的向量 index
    int varIdx(int refIdx, GeomHandle handle) const;
    int varIdx(const GeomRef& ref) const;

private:
    const SketchConstraint& m_constraint;
    const QHash<QString, GeomVarLayout>* m_layout;
};

// ─────────────────────────────────────────────────────────────────────────────
// 具體方程式實作
// ─────────────────────────────────────────────────────────────────────────────

class CoincidentEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 2; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>&, int row0, QVector<QVector<double>>&) const override;
};

class HorizontalEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>&, int row0, QVector<QVector<double>>&) const override;
};

class VerticalEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>&, int row0, QVector<QVector<double>>&) const override;
};

class ParallelEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

class PerpendicularEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

class TangentEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

class ConcentricEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 2; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>&, int row0, QVector<QVector<double>>&) const override;
};

class FixedDistanceEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

class EqualLengthEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

class FixedRadiusEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>&, int row0, QVector<QVector<double>>&) const override;
};

class PointOnCurveEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

/// Midpoint：點 P 在線段 AB 的中點
/// F0(x) = px - (x1+x2)/2 = 0
/// F1(x) = py - (y1+y2)/2 = 0
/// refs[0] = 點(point)，refs[1] = 線段(line)
class MidpointEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 2; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

/// Symmetric：P0 與 P1 關於線段 axis 軸對稱
/// 條件：(a) P0P1 的中點在 axis 上；(b) P0P1 垂直於 axis
/// 產生 3 條方程式
/// refs[0] = 點 A，refs[1] = 點 B，refs[2] = 軸線(line, GeomHandle::Curve)
class SymmetricEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 3; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

/// Collinear：兩線段共線
/// 等價：Parallel + 線段 A 的起點在線段 B 上
/// 產生 2 條方程式：
/// F0 = cross(dA, dB) = 0  （平行）
/// F1 = cross(dB, B1→A1) = 0  （A1 在 B 的延伸線上）
/// refs[0] = 線段 A，refs[1] = 線段 B
class CollinearEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 2; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

class EqualRadiusEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

class FixedXEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>&, int row0, QVector<QVector<double>>&) const override;
};

class FixedYEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>&, int row0, QVector<QVector<double>>&) const override;
};

// ── General Dimension 新增方程式 ─────────────────────────────────────────────

/// F(x) = sqrt((x2-x1)²+(y2-y1)²) - value = 0  (線段長度)
class FixedLengthEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

/// F(x) = r - value/2 = 0  (直徑約束，refs[0]=圓/弧)
class FixedDiameterEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>&, int row0, QVector<QVector<double>>&) const override;
};

/// F(x) = (x2 - x1) - value = 0  (水平距離，refs[0/1]=兩點)
class FixedHorizDistEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>&, int row0, QVector<QVector<double>>&) const override;
};

/// F(x) = (y2 - y1) - value = 0  (垂直距離，refs[0/1]=兩點)
class FixedVertDistEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>&, int row0, QVector<QVector<double>>&) const override;
};

/// F(x) = r * sweep(Start,Center,End) - value = 0  (弧長，refs[0]=弧)
/// sweep 由 Start/End 相對圓心的夾角（cross/dot）決定，搭配
/// GeomVarLayout::refSweep 判斷優弧/劣弧分支（見該欄位註解）。
class FixedArcLengthEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

/// FixedAngleDim：兩線夾角約束（refs[0]=lineA, refs[1]=lineB）
/// F(x) = atan2(cross, dot) - value = 0
class FixedAngleDimEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;
};

/// F1(x) = px - value = 0 ; F2(x) = py - value2 = 0  (點座標，refs[0]=點)
class CoordinateDimEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 2; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>&, int row0, QVector<QVector<double>>&) const override;
};

/// Slope：單一線段斜度約束（refs[0] = 線，取 geomUuid，Start/End 由
/// GeomVarLayout::indexFor() 解析，不受 refs[0].handle 是 Curve 或
/// WholeGeom 影響）。
/// F(x) = (y2 - y1) - value * (x2 - x1) = 0
/// 用線性（非 atan2/sqrt）形式表達，避免除以零（垂直線）與角度不連續問題，
/// 且天然保留方向性正負號 —— value 的正負由線的 Start→End 方向決定
/// （工程慣例：沿 Start→End 方向上升為正、下降為負）。
class SlopeEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override { return 1; }
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>&, int row0, QVector<QVector<double>>&) const override;
};

// Fixed = 用多條 FixedX/Y 方程式固定整個幾何
class FixedEquation : public ConstraintEquation {
public:
    using ConstraintEquation::ConstraintEquation;
    int  equationCount() const override;  // 依幾何類型決定（2~5）
    void evaluate(const QVector<double>& vars, QVector<double>& out) const override;
    void jacobian(const QVector<double>& vars, int row0, QVector<QVector<double>>&) const override;

    void setSnapshot(const QVector<double>& snap, int offset, int dof) {
        m_snap = snap; m_offset = offset; m_dof = dof;
    }
private:
    QVector<double> m_snap;
    int m_offset = 0;
    int m_dof = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// 求解器主體：Newton-Raphson 疊代
// ─────────────────────────────────────────────────────────────────────────────
class ConstraintSolver {
public:
    struct Config {
        int    maxIterations = 100;
        double tolerance     = 1e-8;   ///< ||F(x)|| < tolerance → 收斂
        double damping       = 1.0;    ///< 阻尼因子（0 < d ≤ 1）
        Config() = default;
    };

    explicit ConstraintSolver();
    explicit ConstraintSolver(Config cfg);
    /**
     * 主入口：給定目前幾何狀態 + 約束，求解後回寫幾何
     *
     * @param geometries  草圖的幾何元素（會被修改）
     * @param constraints 要滿足的約束列表
     * @return 求解結果（含 DOF、殘差、收斂狀態）
     */
    SolveResult solve(QList<SketchGeometry*>& geometries,
                      const QList<SketchConstraint>& constraints,
                      const gp_Dir& planeNormal = gp_Dir(0, 0, 1));

    /**
     * 純 DOF 計數（不求解，用於 UI 提示）
     */
    static int computeDOF(const QList<SketchGeometry*>& geometries,
                          const QList<SketchConstraint>& constraints);

private:
    Config m_config;

    // ── 內部步驟 ─────────────────────────────────────────────────────
    /**
     * 將幾何元素的參數打包成扁平的 double 向量
     */
    void packVariables(const QList<SketchGeometry*>& geoms,
                       QHash<QString, GeomVarLayout>& layout,
                       QVector<double>& vars) const;

    /**
     * 將求解後的變數向量回寫到幾何元素
     */
    void unpackVariables(const QVector<double>& vars,
                         const QHash<QString, GeomVarLayout>& layout,
                         QList<SketchGeometry*>& geoms,
                         const gp_Dir& planeNormal) const;

    /**
     * 將 SketchConstraint 轉換為具體的方程式物件
     */
    QList<ConstraintEquation*> buildEquations(
        const QList<SketchConstraint>& constraints,
        const QHash<QString, GeomVarLayout>& layout,
        const QVector<double>& vars) const;

    /**
     * Newton-Raphson 一次疊代：
     *   Δx = -J⁺ · F(x)  （J⁺ 為 Moore-Penrose 虛逆）
     *   x  ← x + damping * Δx
     *
     * 使用 QR 分解（以免 J 奇異）
     */
    bool newtonStep(QVector<double>& vars,
                    const QList<ConstraintEquation*>& eqs,
                    double& residualNorm) const;

    // QR 分解（自實作，不引入 Eigen 依賴）
    // 解最小二乘 J·Δx = -F
    //
    // ── 修正：對過小的奇異值做平滑阻尼（regularize），避免病態系統把極小
    // 殘差放大成很大的修正量 ───────────────────────────────────────────────
    // 內部實作用 Eigen::BDCSVD 的奇異值分解解最小平方問題。Eigen 預設的
    // 奇異值門檻只有 max(rows,cols)*epsilon（約 1e-13～1e-14 這個量級），
    // 對「幾何上真的接近奇異」但不是「數值上剛好等於 0」的系統（例如封閉
    // 輪廓：線段-圓弧-線段-圓弧…首尾相接的鏈狀 loop，其約束方程式之間天
    // 生就存在幾乎線性相依的方向）幾乎不會發揮作用——一旦某個方向的奇異
    // 值很小但非 0（ill-conditioned 而非真奇異），偽逆會用 1/σ 放大該方向
    // 的修正量，殘差稍微不是 0 就會被放大成很大的位移，牽動整個鏈（甚至
    // 完全無關的其他幾何）一起跳動。
    //
    // 這裡改用 Levenberg-Marquardt 風格的平滑阻尼（Tikhonov regularization：
    // σ/(σ²+λ) 取代 1/σ），而不是「非 0 即 1」的硬性截斷門檻（曾經試過，
    // 副作用是連正常、良態的小修正方向也可能被整個歸零，導致原本正確的
    // 圓角弧在求解後反而跟丟）。良態方向（σ 遠大於 √λ）幾乎不受影響
    // （σ/(σ²+λ)≈1/σ），只有真正接近奇異（σ→0）的方向修正量會平滑地趨近
    // 0，而不是被 1/σ 硬放大，兩種極端情況都不會出現。
    static bool solveLinearLS(const QVector<QVector<double>>& J,
                              const QVector<double>& F,
                              QVector<double>& dx);
};

} // namespace aicad::cad
