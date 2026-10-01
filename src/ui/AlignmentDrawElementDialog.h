/**
 * @file AlignmentDrawElementDialog.h
 * @brief 「設計一段線元（直線／曲線／緩和曲線組），套用到既有線形端點」
 *        對話框 — ALIGNMENTDRAWCHAIN（alias: ADC）用。
 *
 * 介面分成三個獨立區塊（直線／曲線／緩和曲線組），每個區塊各自有一個
 * 「繪製」按鈕——填好該區塊的欄位、按下該區塊的「繪製」，就直接把
 * 「這一段」線元的定義關閉本對話框並回傳（accept()），不會停留在對話框
 * 內等待再加下一段、也沒有另外的「完成」按鈕。呼叫端
 * （AlignmentDrawChainCommand）收到後才進入「選物件→直接繪製結果」的
 * 連續流程（見該類別說明）。
 *
 *   1. 【直線區】長度 → 一段切線。
 *   2. 【曲線區】半徑＋長度 → 一段圓弧（半徑帶號，正負號慣例與既有指令
 *      一致）。曲率不要求與目前狀態連續（允許複合曲線）。
 *   3. 【緩和曲線區（二段）】緩和曲線類型＋入緩和曲線長度＋圓弧半徑
 *      （留白＝無限大＝直線）＋圓弧長度（可為 0）＋出緩和曲線長度
 *      （可為 0）。
 *
 * 需求 20：緩和曲線組不再要求使用者預先宣告「起點曲率」——這個值本質上
 * 就是「選取哪個既有端點」決定的（曲線的半徑與左右彎都是既有幾何的一部
 * 分，不該由使用者重新用文字打一次，打錯了緩和曲線的形狀就真的是錯的：
 * 這和直線／曲線不同，緩和曲線的形狀無法靠剛體變換／事後修正）。因此
 * 本對話框不再收集「起點半徑」，緩和曲線組按「繪製」時只驗證欄位本身的
 * 基本合理性（長度需為正、半徑格式須可解析），並不在此計算實際幾何——
 * 而是把參數（transitionGroupParams()）交給呼叫端，等使用者選取了真正
 * 要接續的端點、知道該點「以選取為準」的真實曲率後，才由
 * AlignmentDrawChainCommand 直接算出實際座標（見該類別
 * buildTransitionGroupAt() 的說明）。直線／曲線兩區塊不受影響：它們的
 * 幾何本來就與起點曲率無關，仍然在對話框內立即算好（resultPoints()），
 * 呼叫端只需剛體變換套用。
 *
 * 緩和曲線組銜接規則（curvatureIn＝選取端點的實際曲率）：
 *   - curvatureIn＝0（切線）：圓弧半徑欄必須填實際半徑（不可留白，留白
 *     代表直線，切線接直線沒有意義，請改用直線區）——正向緩和曲線
 *     0→R，可選中間圓弧（維持 R），可選出緩和曲線 R→0（回到切線）。
 *   - curvatureIn≠0（圓弧上）：圓弧半徑欄留白代表銜接回切線——反向緩和
 *     曲線 curvatureIn→0，可選中間直線段，可選出緩和曲線 0→curvatureIn
 *     （回到原本的曲率）；圓弧半徑欄填入另一個不同的有限半徑 R1，則視為
 *     蛋形（Egg）複合曲線 curvatureIn→R1（改用
 *     railway::EggTransitionElement 求解，與 AlignmentElementFactory::
 *     createSpiral() 的 Egg(CC) 分支、AS 指令的 ACA 模式共用同一套公式）
 *     ——可選中間圓弧（維持 R1）、可選出緩和曲線接回 curvatureIn（或再接
 *     到另一個半徑，同樣以蛋形處理）。
 *
 * 直線／曲線兩區塊 resultPoints() 回傳的序列第一點是佔位用的基準點（不
 * 代表真實位置），真正的「相對錨點」是第二點（座標固定為原點）；呼叫端
 * 套用時應以此第二點為基準做剛體變換，其後各點依序平移／旋轉即可。
 *
 * @see railway::computeForwardSpiralEndpoint(), railway::computeReversedSpiralEndpoint()
 * @see railway::EggTransitionElement
 * @see AlignmentDrawChainCommand
 * @author AICAD Team
 */
#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "railway/RailwayAlignment.h"   // AlignmentPoint

class QComboBox;
class QLineEdit;
class QLabel;
class QGroupBox;
class QPushButton;

namespace aicad {
namespace ui {

class AlignmentDrawElementDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AlignmentDrawElementDialog(QWidget* parent = nullptr);
    ~AlignmentDrawElementDialog() override = default;

    /**
     * @brief 直線／曲線區適用：這一段線元的相對關鍵點序列——[0]=佔位基準
     *        點、[1]=原點錨點、[2..]=正算出的新關鍵點，供呼叫端做剛體
     *        變換後套用。緩和曲線組（isTransitionGroup()==true）時固定
     *        回傳只含 [基準點, 原點錨點] 的 2 點陣列，實際幾何請改用
     *        transitionGroupParams()（見上方檔案說明）。
     */
    const QVector<railway::AlignmentPoint>& resultPoints() const { return m_points; }

    /** 這次「繪製」是否為緩和曲線組。 */
    bool isTransitionGroup() const { return m_isTransitionGroup; }

    /** 緩和曲線組的原始輸入參數（需求 20：不含起點曲率——由呼叫端在選取
     *  端點後代入真實曲率，見 AlignmentDrawChainCommand::
     *  buildTransitionGroupAt()）。只在 isTransitionGroup()==true 時有效。 */
    struct TransitionGroupParams {
        QString family;                  ///< 緩和曲線類型 token（kSpiralTokens 之一）
        double  spiralInLength   = 0.0;
        bool    targetIsInfinite = true; ///< 圓弧半徑留白＝目標曲率 0（切線）
        double  targetRadius     = 0.0;  ///< targetIsInfinite 為 false 時才有意義（有號）
        double  curveLength      = 0.0;  ///< 中間圓弧／直線段長度，可為 0
        double  spiralOutLength  = 0.0;  ///< 出緩和曲線長度，可為 0
    };
    TransitionGroupParams transitionGroupParams() const { return m_transParams; }

protected:
    /** 需求 18：QDialog 的通用結束入口，涵蓋三個「繪製」按鈕（accept）與
     *  「取消」／Esc／叉關閉（reject）所有路徑——在這裡統一儲存目前欄位值
     *  到 QSettings，之後不論用哪個管道關閉都會存到，不用在每個按鈕處理
     *  常式裡各自呼叫一次。 */
    void done(int result) override;

private Q_SLOTS:
    void onDrawTangent();
    void onDrawCurve();
    void onDrawTransitionGroup();

private:
    void buildUi();
    /** 把 m_points 初始化成 [佔位基準點, 原點錨點]（皆固定在原點，見
     *  上方檔案說明——不再需要使用者宣告起點曲率）。 */
    void setupAnchor();

    /** 需求 18：對話框打開後，把上次關閉前存的各欄位值（QSettings）載入
     *  回控制項；找不到值（第一次使用）時保留 buildUi() 給的預設值。 */
    void loadSettings();
    /** 需求 18：對話框關閉前（見 done()），把目前各欄位值存進 QSettings，
     *  供下次打開時 loadSettings() 還原。 */
    void saveSettings() const;

    /**
     * @brief 把「一段」新線元附加到 m_points 尾端：mutates m_points.last()
     *        的 tsc[1]／length／radius／curveType，再 push_back 正算出的
     *        新端點（收尾字元固定 'T'）。
     * @return 失敗時回傳錯誤訊息（非空）；成功回傳空字串。
     */
    QString appendTangentSegment(double length);
    QString appendCurveSegment(double radius, double length);

    QVector<railway::AlignmentPoint> m_points;   ///< [0]=佔位基準點, [1..]=直線/曲線區用

    bool                   m_isTransitionGroup = false;
    TransitionGroupParams  m_transParams;

    // 直線區
    QGroupBox*   m_tangentGroup      = nullptr;
    QLineEdit*   m_tangentLengthEdit = nullptr;

    // 曲線區
    QGroupBox*   m_curveGroup        = nullptr;
    QLineEdit*   m_curveRadiusEdit   = nullptr;
    QLineEdit*   m_curveLengthEdit   = nullptr;

    // 緩和曲線區（二段）
    QGroupBox*   m_transGroup           = nullptr;
    QComboBox*   m_transSpiralCombo     = nullptr;
    QLineEdit*   m_transInLengthEdit    = nullptr;
    QLineEdit*   m_transCurveRadiusEdit = nullptr;
    QLineEdit*   m_transCurveLengthEdit = nullptr;
    QLineEdit*   m_transOutLengthEdit   = nullptr;

    QLabel*      m_statusLabel   = nullptr;
};

} // namespace ui
} // namespace aicad
