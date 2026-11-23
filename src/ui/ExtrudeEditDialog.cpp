/**
 * @file ExtrudeEditDialog.cpp
 */
#include "ExtrudeEditDialog.h"

#include "cad/Extrude.h"
#include "cad/Document.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QVBoxLayout>

namespace aicad {
namespace ui {

ExtrudeEditDialog::ExtrudeEditDialog(cad::Extrude* extrude, QWidget* parent)
    : QDialog(parent), m_extrude(extrude)
{
    setWindowTitle(tr("編輯擠出 — %1").arg(extrude ? extrude->name() : QString()));
    resize(460, 360);

    auto* lay = new QVBoxLayout(this);

    // ── 方向／模式 ──
    auto* modeBox = new QGroupBox(tr("方向"), this);
    auto* ml = new QVBoxLayout(modeBox);
    m_rbForward = new QRadioButton(tr("單向 — 正向（沿草圖法線）"), modeBox);
    m_rbReverse = new QRadioButton(tr("單向 — 反向（沿草圖法線的反方向）"), modeBox);
    m_rbSym     = new QRadioButton(tr("兩向平分（總長，草圖平面兩側各半）"), modeBox);
    m_rbTwo     = new QRadioButton(tr("兩向各自給值（第一側＝正向、第二側＝反向）"), modeBox);
    ml->addWidget(m_rbForward);
    ml->addWidget(m_rbReverse);
    ml->addWidget(m_rbSym);
    ml->addWidget(m_rbTwo);
    lay->addWidget(modeBox);

    // ── 值 ──
    auto* valBox = new QGroupBox(tr("值"), this);
    auto* vl = new QVBoxLayout(valBox);

    m_singleRow = new QWidget(valBox);
    auto* sf = new QFormLayout(m_singleRow);
    sf->setContentsMargins(0, 0, 0, 0);
    m_single = new QLineEdit(m_singleRow);
    m_single->setToolTip(tr("數字或參數運算式（例如 H、d1*2）"));
    m_singleLabel = new QLabel(tr("高度："), m_singleRow);
    sf->addRow(m_singleLabel, m_single);
    vl->addWidget(m_singleRow);

    m_twoRow = new QWidget(valBox);
    auto* tf = new QFormLayout(m_twoRow);
    tf->setContentsMargins(0, 0, 0, 0);
    m_side1 = new QLineEdit(m_twoRow);
    m_side2 = new QLineEdit(m_twoRow);
    m_side1->setToolTip(tr("沿草圖法線正向的長度（≥ 0）"));
    m_side2->setToolTip(tr("沿草圖法線反方向的長度（≥ 0）"));
    tf->addRow(tr("第一側（正向）："), m_side1);
    tf->addRow(tr("第二側（反向）："), m_side2);
    vl->addWidget(m_twoRow);
    lay->addWidget(valBox);

    m_preview = new QLabel(this);
    m_preview->setWordWrap(true);
    lay->addWidget(m_preview);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    // ── 由目前的擠出推回模式與各欄位的值 ──
    const QString h = extrude->heightExpression();
    const QString b = extrude->backExpression();
    const bool hasBack = !extrude->isSymmetric() && extrude->backLength() > 1e-9;
    if (extrude->isSymmetric()) {
        m_rbSym->setChecked(true);
        m_single->setText(h);
    } else if (hasBack) {
        m_rbTwo->setChecked(true);
        // 內部 reversed 是「整體翻轉」：reversed 時 height 走 -n、back 走 +n
        if (extrude->isReversed()) { m_side1->setText(b); m_side2->setText(h); }
        else                       { m_side1->setText(h); m_side2->setText(b); }
        m_single->setText(h);
    } else if (extrude->isReversed()) {
        m_rbReverse->setChecked(true);
        m_single->setText(h);
    } else {
        m_rbForward->setChecked(true);
        m_single->setText(h);
    }
    if (m_side1->text().isEmpty()) m_side1->setText(h);
    if (m_side2->text().isEmpty()) m_side2->setText(QStringLiteral("0"));

    for (QRadioButton* rb : { m_rbForward, m_rbReverse, m_rbSym, m_rbTwo })
        connect(rb, &QRadioButton::toggled, this, [this]() { updateUi(); });
    for (QLineEdit* le : { m_single, m_side1, m_side2 })
        connect(le, &QLineEdit::textChanged, this, [this]() { updateUi(); });
    updateUi();
}

ExtrudeEditDialog::Mode ExtrudeEditDialog::currentMode() const
{
    if (m_rbTwo->isChecked())     return TwoSided;
    if (m_rbSym->isChecked())     return SymmetricTotal;
    if (m_rbReverse->isChecked()) return OneWayReverse;
    return OneWayForward;
}

bool ExtrudeEditDialog::evalExpr(const QString& expr, double& out) const
{
    const QString e = expr.trimmed();
    if (e.isEmpty()) { out = 0.0; return true; }
    bool lit = false;
    const double v = e.toDouble(&lit);
    if (lit) { out = v; return true; }
    if (!m_extrude || !m_extrude->document()) return false;
    const auto r = m_extrude->document()->parameterStore()->evaluate(e);
    if (!r.first) return false;
    out = r.second;
    return true;
}

void ExtrudeEditDialog::updateUi()
{
    const Mode m = currentMode();
    m_singleRow->setVisible(m != TwoSided);
    m_twoRow->setVisible(m == TwoSided);
    m_singleLabel->setText(m == SymmetricTotal ? tr("總長：") : tr("長度："));

    QString txt;
    if (m == TwoSided) {
        double a = 0, b = 0;
        if (!evalExpr(m_side1->text(), a) || !evalExpr(m_side2->text(), b)) {
            m_preview->setText(tr("<span style='color:#c00'>運算式無法求值</span>"));
            return;
        }
        txt = tr("總長 %1：第一側（正向）%2、第二側（反向）%3").arg(a + b).arg(a).arg(b);
    } else {
        double v = 0;
        if (!evalExpr(m_single->text(), v)) {
            m_preview->setText(tr("<span style='color:#c00'>運算式無法求值</span>"));
            return;
        }
        const double len = qAbs(v);
        if (m == SymmetricTotal) txt = tr("總長 %1：自草圖平面向兩側各 %2").arg(len).arg(len / 2.0);
        else if (m == OneWayReverse) txt = tr("總長 %1：沿反方向").arg(len);
        else txt = tr("總長 %1：沿正向").arg(len);
    }
    m_preview->setText(txt);
}

void ExtrudeEditDialog::accept()
{
    QString err;
    bool ok = false;
    switch (currentMode()) {
    case OneWayForward:
        ok = m_extrude->setParameters(m_single->text(), QStringLiteral("0"), false, false, &err);
        break;
    case OneWayReverse:
        ok = m_extrude->setParameters(m_single->text(), QStringLiteral("0"), true, false, &err);
        break;
    case SymmetricTotal:
        ok = m_extrude->setParameters(m_single->text(), QStringLiteral("0"), false, true, &err);
        break;
    case TwoSided: {
        double a = 0, b = 0;
        if (!evalExpr(m_side1->text(), a) || !evalExpr(m_side2->text(), b)) {
            err = tr("運算式無法求值");
        } else if (a < 0 || b < 0) {
            err = tr("兩側的值都不可為負");
        } else if (a < 1e-9 && b < 1e-9) {
            err = tr("兩側的值不可同時為 0");
        } else if (a >= 1e-9) {
            // 第一側（正向）有值：height = 第一側、反向附加長度 = 第二側
            ok = m_extrude->setParameters(m_side1->text(), m_side2->text(), false, false, &err);
        } else {
            // 只有第二側有值 = 單向反向
            ok = m_extrude->setParameters(m_side2->text(), QStringLiteral("0"), true, false, &err);
        }
        break;
    }
    }
    if (!ok) {
        QMessageBox::warning(this, tr("編輯擠出"), err);
        return;
    }
    if (m_extrude->document()) m_extrude->document()->setModified(true);
    QDialog::accept();
}

} // namespace ui
} // namespace aicad
