/**
 * @file ExtrudeEditDialog.h
 * @brief 編輯擠出（Extrude）的值與方向。
 *
 * 四種模式：
 *   1. 單向（正向）：沿草圖法線
 *   2. 單向（反向）：沿草圖法線的反方向
 *   3. 兩向平分：給「總長」，以草圖平面為中心兩側各半
 *   4. 兩向各自給值：第一側（正向）、第二側（反向）各自一個值
 * 所有值皆可用數字或參數運算式。
 */
#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QRadioButton;
class QWidget;

namespace aicad {
namespace cad { class Extrude; }
namespace ui {

class ExtrudeEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExtrudeEditDialog(cad::Extrude* extrude, QWidget* parent = nullptr);

protected:
    void accept() override;

private:
    enum Mode { OneWayForward = 0, OneWayReverse, SymmetricTotal, TwoSided };

    Mode currentMode() const;
    void updateUi();
    bool evalExpr(const QString& expr, double& out) const;

    cad::Extrude* m_extrude = nullptr;

    QRadioButton* m_rbForward = nullptr;
    QRadioButton* m_rbReverse = nullptr;
    QRadioButton* m_rbSym     = nullptr;
    QRadioButton* m_rbTwo     = nullptr;

    QWidget*   m_singleRow = nullptr;
    QLabel*    m_singleLabel = nullptr;
    QLineEdit* m_single = nullptr;

    QWidget*   m_twoRow = nullptr;
    QLineEdit* m_side1 = nullptr;
    QLineEdit* m_side2 = nullptr;

    QLabel*    m_preview = nullptr;
};

} // namespace ui
} // namespace aicad
