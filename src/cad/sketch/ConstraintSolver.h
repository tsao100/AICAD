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

    // 語意 accessor（依 GeomHandle 取 index）
    // Line:    offset+0=x1, +1=y1, +2=x2, +3=y2
    // Circle:  offset+0=cx, +1=cy, +2=r
    // Arc:     offset+0=cx, +1=cy, +2=r, +3=startAngle, +4=endAngle
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
    static bool solveLinearLS(const QVector<QVector<double>>& J,
                              const QVector<double>& F,
                              QVector<double>& dx);
};

} // namespace aicad::cad
