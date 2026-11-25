/**
 * @file AssemblySolver.h
 * @brief 零件組合的「配合（Mate）」求解器 —— 純數學，不依賴 Qt／OCCT，只用 Eigen。
 *
 * 模型：每個零件是一個剛體，位姿 = 旋轉 R + 平移 t（外加均勻縮放 scale，由零件單位換算而來）：
 *        世界座標 x_w = R · (scale · x_local) + t
 * 配合（Mate）= 兩個「幾何參照」（點／軸線／平面，各自屬於某個零件或「世界」）之間的關係：
 *
 *   類型            可用的參照組合                                 殘差（= 0 即滿足）
 *   ─────────────  ─────────────────────────────────────────  ────────────────────────────
 *   Coincident 重合  點-點、點-軸線、點-平面                      兩點差／點到線的垂直向量／點到面的距離
 *   Collinear  共線  軸線-軸線                                    方向（同向／反向／不限）＋ 點在線上
 *   Coplanar   共面  平面-平面、軸線-平面（軸線落在平面內）       法向量（同向／反向／不限）＋ 點在面上
 *   Parallel   平行  軸線-軸線、平面-平面、軸線-平面              方向（同向／反向／不限）／ 軸線 ⟂ 法向量
 *   Perpendicular 垂直 軸線-軸線、平面-平面、軸線-平面            方向內積 = 0 ／ 軸線 ∥ 法向量
 *   Distance   距離  六種組合（點-點、點-軸、點-面、軸-軸、軸-面、面-面）
 *   Angle      夾角  軸線-軸線、平面-平面（兩法向量夾角）、軸線-平面（軸線與平面的夾角）
 *
 * 與座標軸、座標平面的關係 = 其中一個參照屬於世界（body = -1），例如：零件的某面與世界 XY 平面共面／距離 30、
 * 零件的圓柱軸線與世界 X 軸共線／平行／夾角 15°。
 *
 * 求解：把所有「未固定」零件的位姿當作未知數（每個零件 6 個自由度，增量參數化，轉動以零件包圍盒中心為軸心，
 * 避免零件原點離幾何很遠時病態），以 Levenberg–Marquardt（數值 Jacobian）最小化全部殘差的平方和。
 * 欠約束時阻尼項使步伐在零空間方向為最小範數 → 零件只做「滿足配合所需的最小移動」。
 * 求解失敗（衝突／過度約束）時，由呼叫端決定是否還原原位姿；每個配合都有各自的殘差，可標示出不滿足者。
 * 另外以 Jacobian 的 SVD 估計剩餘自由度（總數與每個零件）。
 *
 * 單位：殘差內部以 lengthScale 無因次化（長度／lengthScale、角度為弧度），容許誤差 tol 是無因次的。
 */
#pragma once

#include <Eigen/Dense>
#include <string>
#include <vector>

namespace aicad {
namespace cad {
namespace asmsolve {

using Vec3 = Eigen::Vector3d;
using Mat3 = Eigen::Matrix3d;

enum class RefKind { Point = 0, Axis = 1, Plane = 2 };

enum class MateType {
    Coincident = 0,      ///< 重合
    Collinear,           ///< 共線（同軸）
    Coplanar,            ///< 共面
    Parallel,            ///< 平行
    Perpendicular,       ///< 垂直
    Distance,            ///< 距離
    Angle                ///< 夾角
};

/// 方向的取向：同向（法向量／軸向相同）、反向（相反，例如兩個面對面貼合）、不限（只要求平行，不管正反）
enum class Orient { Any = 0, Aligned = 1, Opposed = 2 };

/// 剛體
struct Body {
    Mat3   R = Mat3::Identity();   ///< 旋轉（本地 → 世界）
    Vec3   t = Vec3::Zero();       ///< 平移（本地原點在世界的位置）
    double scale = 1.0;            ///< 本地座標 → 組合座標的均勻縮放（零件單位換算）
    bool   fixed = false;          ///< true = 固定在世界（接地），不參與求解
    Vec3   center = Vec3::Zero();  ///< 本地座標中的幾何中心（轉動軸心）；只影響條件數，不影響解的正確性
};

/// 幾何參照（點／軸線／平面）。座標為「所屬零件的本地座標」；body = -1 表示世界座標。
struct Ref {
    int     body = -1;
    RefKind kind = RefKind::Point;
    Vec3    p = Vec3::Zero();      ///< 點本身／軸線上一點／平面上一點
    Vec3    d = Vec3::UnitZ();     ///< 軸線方向／平面法向量（單位向量；點不使用）
};

struct Mate {
    MateType type   = MateType::Coincident;
    Ref      a, b;                 ///< 參照 1、參照 2
    Orient   orient = Orient::Any; ///< 取向（只對有方向的配合有意義）
    double   value  = 0.0;         ///< Distance：長度（組合單位）；Angle：弧度
    bool     enabled = true;       ///< false = 抑制，不參與求解
};

struct Problem {
    std::vector<Body> bodies;
    std::vector<Mate> mates;
    double lengthScale = 1.0;      ///< 特徵長度（例如全部零件包圍盒對角線）；殘差以此無因次化
};

struct Options {
    int    maxIter    = 200;
    double tol        = 1e-9;      ///< 收斂：全部殘差的無因次無窮範數
    double okTol      = 1e-6;      ///< 判定「單一配合已滿足」的容許殘差
    bool   multiStart = true;      ///< 主要求解失敗時，嘗試幾組不同的初始姿態
    bool   computeDof = true;
};

struct Result {
    bool   converged = false;      ///< 所有啟用的配合都滿足（殘差 < okTol）
    int    iterations = 0;
    double maxResidual = 0.0;      ///< 無因次
    std::vector<double> mateResidual;   ///< 每個配合的殘差（無因次；抑制者為 0）
    std::vector<bool>   mateOk;         ///< 每個配合是否滿足（抑制者為 true）
    int    dof = 0;                ///< 剩餘自由度總數（只計未固定零件）
    std::vector<int>    bodyDof;       ///< 每個零件的剩餘自由度（固定者為 0）
    bool   usedRestart = false;
    std::string message;
};

// ── 參照組合是否合法／配合是否有方向或數值 ─────────────────────────────────────
bool isValidPair(MateType type, RefKind a, RefKind b);
bool mateUsesValue(MateType type, RefKind a, RefKind b);    ///< Distance、Angle（Angle 的「值」為角度）
bool mateUsesOrient(MateType type, RefKind a, RefKind b);   ///< 是否有「同向／反向」可選

/// 世界座標下的幾何
struct WorldGeom { Vec3 p; Vec3 d; };
WorldGeom worldGeom(const Problem& pr, const Ref& r);

/// 目前幾何下，Distance／Angle 的量測值（距離為組合單位、可為負的有號距離；角度為弧度）。
/// 供對話框預填「目前值」，避免新增配合時零件被無謂地移動。不適用時回傳 false。
bool measureValue(const Problem& pr, MateType type, const Ref& a, const Ref& b, Orient orient, double& out);

/// 依目前幾何，建議最接近的取向（兩方向內積 >= 0 → Aligned，否則 Opposed）；沒有方向的組合回傳 Any。
Orient closestOrient(const Problem& pr, MateType type, const Ref& a, const Ref& b);

/// 一個配合的殘差（有因次；長度為組合單位、角度為弧度、方向殘差無因次）。
/// 參照順序以 RefKind 小者在前（點 < 軸線 < 平面）正規化；同種類保持原順序。
void mateResiduals(const Problem& pr, const Mate& m, std::vector<double>& out);

/// 求解：就地更新 pr.bodies 中「未固定」零件的 R、t。converged = false 時 bodies 為「殘差最小」的那一組
/// （呼叫端可選擇還原）。
Result solve(Problem& pr, const Options& opt = Options());

/// 旋轉向量 → 旋轉矩陣（Rodrigues）
Mat3 expSO3(const Vec3& w);

} // namespace asmsolve
} // namespace cad
} // namespace aicad
