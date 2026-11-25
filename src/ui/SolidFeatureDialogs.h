/**
 * @file SolidFeatureDialogs.h
 * @brief 3D 塑形／挖除功能的對話框：旋轉、掃掠、放樣、布林，以及圓角／抽殼的編輯。
 *
 * 每個 run*Dialog() 都是「建立」流程：彈出對話框 → 確認後建立特徵並登記進文件。
 * editSolidFeature() 是「編輯」流程（特徵樹右鍵）：依特徵種類彈出對應對話框、改參數並重建。
 * 運算方式（新實體／聯集／挖除／交集）與目標實體由 OpTargetBox 統一處理。
 */
#pragma once

#include "cad/SolidOps.h"

#include <QGroupBox>
#include <QList>

class QComboBox;

namespace aicad {
namespace cad { class Document; class Feature; class Sketch; }
namespace ui {

/// 「運算方式 + 目標實體」群組方塊
class OpTargetBox : public QGroupBox {
    Q_OBJECT
public:
    /// self：正在編輯的特徵（建立時為 nullptr）；target：目前的目標
    OpTargetBox(cad::Document* doc, const cad::Feature* self,
                cad::SolidOp op, cad::Feature* target, QWidget* parent = nullptr);

    cad::SolidOp   op() const;
    cad::Feature*  target() const;

    /// 運算方式需要目標卻沒選到時回傳 false 並填入訊息
    bool validate(QString* err) const;

private:
    void updateEnabled();

    QComboBox* m_opCombo = nullptr;
    QComboBox* m_targetCombo = nullptr;
};

// ── 建立流程（回傳 true = 已建立特徵）──
bool runRevolveDialog(cad::Document* doc, cad::Sketch* preset, cad::SolidOp defaultOp, QWidget* parent);
bool runSweepDialog(cad::Document* doc, cad::Sketch* presetProfile, cad::SolidOp defaultOp, QWidget* parent);
bool runLoftDialog(cad::Document* doc, cad::SolidOp defaultOp, QWidget* parent);
bool runBooleanDialog(cad::Document* doc, QWidget* parent);

// ── 圓角／抽殼：數值輸入（選邊／選面由指令負責）──
// ── 陣列／鏡射／孔精靈 ──
bool runPatternDialog(cad::Document* doc, QWidget* parent);
bool runMirrorDialog(cad::Document* doc, QWidget* parent);
bool runHoleDialog(cad::Document* doc, cad::Sketch* presetSketch, QWidget* parent);

bool askFilletRadius(double* radius, QWidget* parent, double initial = 1.0);
bool askShellThickness(double* thickness, QWidget* parent, double initial = 1.0);

// ── 編輯流程 ──
/// 依特徵種類編輯（Revolve／Sweep／SketchLoft／Fillet／Shell／Boolean／Pattern／Mirror／Hole）。回傳 true = 已修改
bool editSolidFeature(cad::Feature* feature, QWidget* parent);

} // namespace ui
} // namespace aicad
