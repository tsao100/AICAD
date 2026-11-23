/**
 * @file DefinePlaneDialog.h
 * @brief 定義／編輯平面：名稱 + 定義方式（10 種，見 docs/DefinedPlane_Methods.md）+ 該方式的參照／參數。
 */
#pragma once

#include "cad/DefinedPlane.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QStackedWidget;

namespace aicad {
namespace cad { class Document; }
namespace ui {

class DefinePlaneDialog : public QDialog {
    Q_OBJECT
public:
    /// existing = nullptr：新增；否則編輯該平面。defaultBase：新增時預選的基準（"XY"/"XZ"/"YZ" 或平面 id）
    DefinePlaneDialog(cad::Document* doc, cad::DefinedPlane* existing,
                      const QString& defaultBase, QWidget* parent = nullptr);
    ~DefinePlaneDialog() override;

    QString         planeName() const;
    cad::PlaneDef   definition() const;
    // 相容舊介面（偏移平面）
    QString baseRef()    const { return definition().baseRef; }
    QString offsetExpr() const { return definition().offsetExpr; }

protected:
    void accept() override;

private:
    struct Pages;                       // 各方式頁面的控制項（定義在 .cpp）
    void buildPages();
    void loadDefinition(const cad::PlaneDef& d);
    void updatePreview();
    QWidget* currentPage() const;

    cad::Document*     m_doc = nullptr;
    cad::DefinedPlane* m_existing = nullptr;
    QLineEdit*      m_name = nullptr;
    QComboBox*      m_method = nullptr;
    QStackedWidget* m_stack = nullptr;
    QCheckBox*      m_flip = nullptr;
    QLabel*         m_preview = nullptr;
    Pages*          m_p = nullptr;
};

} // namespace ui
} // namespace aicad
