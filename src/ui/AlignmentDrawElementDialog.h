/**
 * @file AlignmentDrawElementDialog.h
 * @brief 「接續目前線形，逐段加入直線／曲線／緩和曲線組」對話框 —
 *        ALIGNMENTDRAWCHAIN（alias: ADC）用。
 *
 * 介面分成三個獨立區塊（比照使用者提供的 Excel VBA 表單：ComboBox1=緩和
 * 曲線類型、TextBox1=長度、TextBox2=端點半徑，三個 CommandButton 分別對應
 * 「加緩和曲線」「續接下一圓弧」「續接下一直線」），使用者可反覆點選任一
 * 區塊的「加入」按鈕，逐段從目前端點往下延伸；每加入一段，「目前端點」與
 * 已加入線元列表即時更新，可「復原上一段」；全部加完後按「完成」結束。
 *
 *   1. 【直線區】長度 → 加入一段切線。
 *   2. 【曲線區】半徑＋長度 → 加入一段圓弧（半徑帶號，正負號慣例與既有
 *      指令一致）。曲率不要求與目前狀態連續（允許複合曲線）。
 *   3. 【緩和曲線區（二段）】緩和曲線類型＋入緩和曲線長度＋圓弧半徑
 *      （留白＝無限大＝直線）＋圓弧長度（可為 0）＋出緩和曲線長度
 *      （可為 0）→ 一次加入 1～3 個新關鍵點，銜接「目前曲率」與「圓弧
 *      半徑欄代表的曲率」：
 *        - 目前在切線（曲率 0）：圓弧半徑欄必須填實際半徑（不可留白，
 *          留白代表直線，切線接直線沒有意義，請改用直線區）——正向緩和
 *          曲線 0→R，可選中間圓弧（維持 R），可選出緩和曲線 R→0（回到
 *          切線）。
 *        - 目前在圓弧上（曲率 R0）：圓弧半徑欄只能留白（代表銜接回
 *          切線）——反向緩和曲線 R0→0，可選中間直線段，可選出緩和曲線
 *          0→R0（回到原本的 R0，例如在既有曲線中間插入一小段直線再
 *          接回原曲線）。不支援轉去另一個「不同、非 0」的半徑（複合曲線
 *          Egg），這類需求請改用 ALIGNMENTQUICKTABLE。
 *      無論從哪一端開始，「出緩和曲線」一律回到「這次操作開始前」的曲率，
 *      邏輯完全對稱。
 *
 * ADC 的編輯對象固定為「目前作用中的 Alignment」（由
 * AlignmentDrawChainCommand 透過點選既有線形起點／終點決定，見該類別
 * 說明），本對話框開啟時已知目前作用中線路的名稱與其完整關鍵點序列
 * （TM2 座標）。使用者可從既有關鍵點清單中選擇要「接續」的點位（預設為
 * 最後一點）；若不是最後一點，代表該點之後原有的關鍵點將被取代。
 *
 * 座標系統：與 AlignmentQuickTableDialog 一致——全程在 TM2（Global）座標下
 * 顯示與計算，TM2 ↔ Local 的轉換由呼叫端（AlignmentDrawChainCommand）在
 * 跨越 TrackCenterLine／OCCT 邊界前後各做一次。
 *
 * @see railway::computeForwardSpiralEndpoint(), railway::computeReversedSpiralEndpoint()
 * @see railway::curvatureAtPoint()
 * @see AlignmentDrawChainCommand
 * @author AICAD Team
 */
#pragma once

#include <QDialog>
#include <QVector>

#include "railway/RailwayAlignment.h"   // AlignmentPoint

class QComboBox;
class QLineEdit;
class QLabel;
class QGroupBox;
class QPushButton;
class QListWidget;

namespace aicad {
namespace ui {

class AlignmentDrawElementDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param tclName            目前作用中線路的名稱，僅供顯示。
     * @param existingPointsTM2  該線路目前完整的關鍵點序列（TM2 座標）。
     */
    AlignmentDrawElementDialog(const QString& tclName,
                                const QVector<railway::AlignmentPoint>& existingPointsTM2,
                                QWidget* parent = nullptr);
    ~AlignmentDrawElementDialog() override = default;

    /**
     * @brief 按「完成」關閉（Accepted）後：完整的新關鍵點序列（TM2 座標），
     *        = 選取點（含）之前的既有關鍵點 + 逐段加入的新關鍵點，已可
     *        直接交給 tcl->loadHorizontal()（轉回 Local 之後）。
     */
    const QVector<railway::AlignmentPoint>& resultPoints() const { return m_points; }

private Q_SLOTS:
    void onStartPointChanged(int index);
    void onAddTangent();
    void onAddCurve();
    void onAddTransitionGroup();
    void onUndoLast();
    void onFinish();

private:
    void buildUi(const QString& tclName);
    void refreshStartPointCombo(const QString& tclName);
    void refreshCurrentStateLabel();
    void refreshSegmentList();
    /** 更新「緩和曲線區」的提示文字（依目前曲率狀態說明圓弧半徑欄的
     *  填法）；不再停用整個區塊——需求：起點在圓弧上時也要能使用。 */
    void refreshTransitionGroupAvailability();

    /** 目前端點（m_points 最後一點）所在曲率；0 = 位於切線。 */
    double currentCurvature() const;

    /**
     * @brief 把「一段」新線元附加到 m_points 尾端：mutates m_points.last()
     *        的 tsc[1]／length／radius／curveType，再 push_back 正算出的
     *        新端點（暫定收尾字元 'T'，若後續再接線元會被覆寫）。
     * @return 失敗時回傳錯誤訊息（非空）；成功回傳空字串。
     */
    QString appendTangentSegment(double length);
    QString appendCurveSegment(double radius, double length);

    /**
     * @brief 加入緩和曲線組（1～3 個新關鍵點），見標頭「緩和曲線區」說明。
     * @param curveRadius 圓弧半徑；傳入 0（對應 UI 上的「留白」）代表
     *                    無限大（直線）。
     * @return 失敗時回傳錯誤訊息（非空）；成功回傳空字串。
     */
    QString appendTransitionGroup(const QString& curveType, double spiralInLength,
                                   double curveRadius, double curveLength,
                                   double spiralOutLength);

    QVector<railway::AlignmentPoint> m_points;       ///< 既有關鍵點 + 已加入的新關鍵點
    QVector<double> m_radiusHistory;                 ///< 與 m_points 對齊：各點所在曲率
    QVector<int> m_checkpoints;                      ///< 每次「加入」前的 m_points.size()，供復原

    int m_existingCount = 0;   ///< 建構時既有關鍵點數量（起點列表只列這些點）

    QComboBox*   m_startPointCombo = nullptr;
    QLabel*      m_startInfoLabel  = nullptr;

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
    QLabel*      m_transHint            = nullptr;

    QListWidget* m_segmentList   = nullptr;
    QLabel*      m_statusLabel   = nullptr;
    QPushButton* m_undoButton    = nullptr;
    QPushButton* m_finishButton  = nullptr;
};

} // namespace ui
} // namespace aicad
