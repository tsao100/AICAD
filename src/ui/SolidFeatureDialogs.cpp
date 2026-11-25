/**
 * @file SolidFeatureDialogs.cpp
 */
#include "SolidFeatureDialogs.h"

#include "cad/Document.h"
#include "cad/Sketch.h"
#include "cad/Revolve.h"
#include "cad/Sweep.h"
#include "cad/SketchLoft.h"
#include "cad/SolidModifier.h"
#include "cad/SolidBoolean.h"
#include "cad/SolidPatterns.h"
#include "cad/HoleFeature.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QStackedWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace aicad {
namespace ui {

using cad::SolidOp;

namespace {

QString sketchLabel(const cad::Sketch* s) { return s ? s->name() : QString(); }

/// 把草圖清單填進 combo（userData = Sketch*）
void fillSketchCombo(QComboBox* cb, const QList<cad::Sketch*>& list, const cad::Sketch* select)
{
    cb->clear();
    for (cad::Sketch* s : list)
        cb->addItem(sketchLabel(s), QVariant::fromValue(static_cast<QObject*>(s)));
    for (int i = 0; i < cb->count(); ++i)
        if (cb->itemData(i).value<QObject*>() == select) { cb->setCurrentIndex(i); break; }
}

cad::Sketch* comboSketch(const QComboBox* cb)
{
    return qobject_cast<cad::Sketch*>(cb->currentData().value<QObject*>());
}

/// 區域清單：第 0 項 = 全部；其餘依 describeRegions()
void fillRegionCombo(QComboBox* cb, cad::Sketch* sk, int current)
{
    cb->clear();
    cb->addItem(QObject::tr("全部區域"), -1);
    if (!sk) return;
    for (const auto& r : cad::solidops::describeRegions(sk)) {
        cb->addItem(QObject::tr("區域 %1（面積 %2，中心 %3, %4）")
                        .arg(r.index + 1).arg(r.area, 0, 'g', 6)
                        .arg(r.cx, 0, 'f', 2).arg(r.cy, 0, 'f', 2), r.index);
    }
    for (int i = 0; i < cb->count(); ++i)
        if (cb->itemData(i).toInt() == current) { cb->setCurrentIndex(i); break; }
}

QDialogButtonBox* addButtons(QDialog* dlg, QVBoxLayout* lay)
{
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    QObject::connect(bb, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    lay->addWidget(bb);
    return bb;
}

} // namespace

// ═════════════════════════ OpTargetBox ═════════════════════════

OpTargetBox::OpTargetBox(cad::Document* doc, const cad::Feature* self,
                         SolidOp op, cad::Feature* target, QWidget* parent)
    : QGroupBox(tr("運算方式"), parent)
{
    auto* form = new QFormLayout(this);

    m_opCombo = new QComboBox(this);
    for (SolidOp o : { SolidOp::NewBody, SolidOp::Fuse, SolidOp::Cut, SolidOp::Common })
        m_opCombo->addItem(cad::solidops::opLabel(o), static_cast<int>(o));
    for (int i = 0; i < m_opCombo->count(); ++i)
        if (m_opCombo->itemData(i).toInt() == static_cast<int>(op)) { m_opCombo->setCurrentIndex(i); break; }
    form->addRow(tr("方式："), m_opCombo);

    m_targetCombo = new QComboBox(this);
    const QList<cad::Feature*> cands = doc ? doc->solidFeatureCandidates(self, target) : QList<cad::Feature*>();
    for (cad::Feature* f : cands)
        m_targetCombo->addItem(f->name(), QVariant::fromValue(static_cast<QObject*>(f)));
    for (int i = 0; i < m_targetCombo->count(); ++i)
        if (m_targetCombo->itemData(i).value<QObject*>() == target) { m_targetCombo->setCurrentIndex(i); break; }
    form->addRow(tr("目標實體："), m_targetCombo);

    connect(m_opCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateEnabled(); });
    updateEnabled();
}

void OpTargetBox::updateEnabled()
{
    m_targetCombo->setEnabled(op() != SolidOp::NewBody && m_targetCombo->count() > 0);
}

SolidOp OpTargetBox::op() const
{
    return static_cast<SolidOp>(m_opCombo->currentData().toInt());
}

cad::Feature* OpTargetBox::target() const
{
    if (op() == SolidOp::NewBody) return nullptr;
    return qobject_cast<cad::Feature*>(m_targetCombo->currentData().value<QObject*>());
}

bool OpTargetBox::validate(QString* err) const
{
    if (op() != SolidOp::NewBody && !target()) {
        if (err) *err = tr("「%1」需要一個目標實體，但目前文件裡沒有可選的實體。")
                            .arg(cad::solidops::opLabel(op()));
        return false;
    }
    return true;
}

// ═════════════════════════ 旋轉 ═════════════════════════

namespace {

class RevolveDialog : public QDialog {
public:
    RevolveDialog(cad::Document* doc, cad::Revolve* existing, cad::Sketch* preset,
                  SolidOp defOp, QWidget* parent)
        : QDialog(parent), m_doc(doc), m_rev(existing)
    {
        setWindowTitle(existing ? tr("編輯旋轉 — %1").arg(existing->name()) : tr("旋轉"));
        resize(460, 420);
        auto* lay = new QVBoxLayout(this);
        auto* form = new QFormLayout;

        m_sketch = new QComboBox(this);
        const cad::Sketch* cur = existing ? existing->sketch() : preset;
        fillSketchCombo(m_sketch, doc->allSketches(), cur);
        m_sketch->setEnabled(!existing);           // 編輯時不換草圖
        form->addRow(tr("輪廓草圖："), m_sketch);

        m_region = new QComboBox(this);
        form->addRow(tr("輪廓區域："), m_region);

        m_axis = new QComboBox(this);
        form->addRow(tr("旋轉軸："), m_axis);

        m_angle = new QLineEdit(existing ? existing->angleExpression() : QStringLiteral("360"), this);
        m_angle->setToolTip(tr("度數或參數運算式；360 = 整圈"));
        form->addRow(tr("角度（°）："), m_angle);
        lay->addLayout(form);

        m_reverse = new QCheckBox(tr("反向"), this);
        m_sym = new QCheckBox(tr("對稱（輪廓平面兩側各一半）"), this);
        m_reverse->setChecked(existing && existing->isReversed());
        m_sym->setChecked(existing && existing->isSymmetric());
        lay->addWidget(m_reverse);
        lay->addWidget(m_sym);

        m_op = new OpTargetBox(doc, existing, existing ? existing->operation() : defOp,
                               existing ? existing->target() : nullptr, this);
        lay->addWidget(m_op);

        const QString hint = tr("提示：旋轉軸若選「草圖內的直線」，建議把那條線畫成建構線／中心線。");
        auto* lbl = new QLabel(hint, this);
        lbl->setWordWrap(true);
        lay->addWidget(lbl);
        addButtons(this, lay);

        connect(m_sketch, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refill(); });
        refill();
    }

    void accept() override
    {
        cad::Sketch* sk = comboSketch(m_sketch);
        if (!sk) { QMessageBox::warning(this, windowTitle(), tr("請先選擇輪廓草圖。")); return; }
        QString err;
        if (!m_op->validate(&err)) { QMessageBox::warning(this, windowTitle(), err); return; }

        QString lineUuid;
        cad::Revolve::AxisKind k = cad::Revolve::AxisKind::SketchX;
        const QVariant ad = m_axis->currentData();
        if (ad.toString() == QLatin1String("x")) k = cad::Revolve::AxisKind::SketchX;
        else if (ad.toString() == QLatin1String("y")) k = cad::Revolve::AxisKind::SketchY;
        else { k = cad::Revolve::AxisKind::SketchLine; lineUuid = ad.toString(); }

        const int region = m_region->currentData().toInt();
        cad::Revolve* rev = m_rev;
        const bool creating = (rev == nullptr);
        if (creating) { rev = new cad::Revolve(m_doc); rev->setSketch(sk); }

        if (!rev->setParameters(m_angle->text(), k, lineUuid, m_reverse->isChecked(),
                                m_sym->isChecked(), region, &err)) {
            if (creating) delete rev;
            QMessageBox::warning(this, windowTitle(), err);
            return;
        }
        rev->setOperationAndTarget(m_op->op(), m_op->target());
        if (rev->target()) rev->target()->setVisible(false);

        if (creating) {
            QList<cad::Feature*> consumed;
            if (rev->target()) consumed << rev->target();
            m_doc->registerSolidFeature(rev, QStringLiteral("Revolve"), { sk }, consumed);
        } else {
            m_doc->rebuildFeature(rev);
        }
        m_doc->setModified(true);
        QDialog::accept();
    }

private:
    void refill()
    {
        cad::Sketch* sk = comboSketch(m_sketch);
        fillRegionCombo(m_region, sk, m_rev ? m_rev->regionIndex() : -1);

        m_axis->clear();
        m_axis->addItem(tr("草圖 X 軸"), QStringLiteral("x"));
        m_axis->addItem(tr("草圖 Y 軸"), QStringLiteral("y"));
        int sel = (m_rev && m_rev->axisKind() == cad::Revolve::AxisKind::SketchX) ? 0 : 1;
        if (sk) {
            int n = 0;
            for (cad::SketchGeometry* g : sk->geometries()) {
                if (!g || g->type != cad::SketchGeometryType::Line) continue;
                auto* ln = static_cast<cad::SketchLine*>(g);
                ++n;
                m_axis->addItem(tr("直線 %1%2：(%3, %4) → (%5, %6)")
                                    .arg(n).arg(g->isConstruction() ? tr("［建構線］") : QString())
                                    .arg(ln->start.x(), 0, 'f', 2).arg(ln->start.y(), 0, 'f', 2)
                                    .arg(ln->end.x(), 0, 'f', 2).arg(ln->end.y(), 0, 'f', 2),
                                g->uuid);
                if (m_rev && m_rev->axisKind() == cad::Revolve::AxisKind::SketchLine
                    && m_rev->axisLineUuid() == g->uuid)
                    sel = m_axis->count() - 1;
            }
        }
        m_axis->setCurrentIndex(sel);
    }

    cad::Document* m_doc;
    cad::Revolve*  m_rev;
    QComboBox *m_sketch, *m_region, *m_axis;
    QLineEdit* m_angle;
    QCheckBox *m_reverse, *m_sym;
    OpTargetBox* m_op;
};

} // namespace

bool runRevolveDialog(cad::Document* doc, cad::Sketch* preset, SolidOp defaultOp, QWidget* parent)
{
    if (!doc) return false;
    if (doc->allSketches().isEmpty()) {
        QMessageBox::information(parent, QObject::tr("旋轉"), QObject::tr("文件裡還沒有草圖，請先畫一個輪廓。"));
        return false;
    }
    RevolveDialog dlg(doc, nullptr, preset, defaultOp, parent);
    return dlg.exec() == QDialog::Accepted;
}

// ═════════════════════════ 掃掠 ═════════════════════════

namespace {

class SweepDialog : public QDialog {
public:
    SweepDialog(cad::Document* doc, cad::Sweep* existing, cad::Sketch* preset,
                SolidOp defOp, QWidget* parent)
        : QDialog(parent), m_doc(doc), m_sw(existing)
    {
        setWindowTitle(existing ? tr("編輯掃掠 — %1").arg(existing->name()) : tr("掃掠"));
        resize(460, 360);
        auto* lay = new QVBoxLayout(this);
        auto* form = new QFormLayout;

        const QList<cad::Sketch*> all = doc->allSketches();
        m_profile = new QComboBox(this);
        fillSketchCombo(m_profile, all, existing ? existing->profileSketch() : preset);
        m_path = new QComboBox(this);
        // 路徑預設挑「不是輪廓」的第一張草圖
        const cad::Sketch* pathSel = existing ? existing->pathSketch() : nullptr;
        fillSketchCombo(m_path, all, pathSel);
        if (!existing) {
            for (int i = 0; i < m_path->count(); ++i)
                if (m_path->itemData(i).value<QObject*>() != comboSketch(m_profile)) { m_path->setCurrentIndex(i); break; }
        }
        m_profile->setEnabled(!existing);
        m_path->setEnabled(!existing);
        form->addRow(tr("輪廓草圖："), m_profile);
        form->addRow(tr("路徑草圖："), m_path);

        m_region = new QComboBox(this);
        form->addRow(tr("輪廓區域："), m_region);

        m_orient = new QComboBox(this);
        m_orient->addItem(tr("修正 Frenet（建議）"), 0);
        m_orient->addItem(tr("Frenet"), 1);
        m_orient->addItem(tr("固定方向"), 2);
        if (existing) m_orient->setCurrentIndex(static_cast<int>(existing->orientation()));
        form->addRow(tr("輪廓朝向："), m_orient);
        lay->addLayout(form);

        m_op = new OpTargetBox(doc, existing, existing ? existing->operation() : defOp,
                               existing ? existing->target() : nullptr, this);
        lay->addWidget(m_op);
        auto* lbl = new QLabel(tr("提示：路徑草圖內的線段會自動接成一條路徑；輪廓應與路徑起點垂直相交最理想。"), this);
        lbl->setWordWrap(true);
        lay->addWidget(lbl);
        addButtons(this, lay);

        connect(m_profile, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refill(); });
        refill();
    }

    void accept() override
    {
        cad::Sketch* prof = comboSketch(m_profile);
        cad::Sketch* path = comboSketch(m_path);
        QString err;
        if (!prof || !path) { QMessageBox::warning(this, windowTitle(), tr("請選擇輪廓草圖與路徑草圖。")); return; }
        if (prof == path) { QMessageBox::warning(this, windowTitle(), tr("輪廓與路徑不可是同一張草圖。")); return; }
        if (!m_op->validate(&err)) { QMessageBox::warning(this, windowTitle(), err); return; }

        cad::Sweep* sw = m_sw;
        const bool creating = (sw == nullptr);
        if (creating) {
            sw = new cad::Sweep(m_doc);
            sw->setProfileSketch(prof);
            sw->setPathSketch(path);
        }
        sw->setRegionIndex(m_region->currentData().toInt());
        sw->setOrientation(static_cast<cad::Sweep::Orientation>(m_orient->currentData().toInt()));
        sw->setOperationAndTarget(m_op->op(), m_op->target());
        if (sw->target()) sw->target()->setVisible(false);

        if (creating) {
            QList<cad::Feature*> consumed;
            if (sw->target()) consumed << sw->target();
            m_doc->registerSolidFeature(sw, QStringLiteral("Sweep"), { prof, path }, consumed);
        } else {
            m_doc->rebuildFeature(sw);
        }
        m_doc->setModified(true);
        QDialog::accept();
    }

private:
    void refill() { fillRegionCombo(m_region, comboSketch(m_profile), m_sw ? m_sw->regionIndex() : -1); }

    cad::Document* m_doc;
    cad::Sweep*    m_sw;
    QComboBox *m_profile, *m_path, *m_region, *m_orient;
    OpTargetBox* m_op;
};

} // namespace

bool runSweepDialog(cad::Document* doc, cad::Sketch* presetProfile, SolidOp defaultOp, QWidget* parent)
{
    if (!doc) return false;
    if (doc->allSketches().size() < 2) {
        QMessageBox::information(parent, QObject::tr("掃掠"),
                                 QObject::tr("掃掠需要兩張草圖：一張輪廓、一張路徑。"));
        return false;
    }
    SweepDialog dlg(doc, nullptr, presetProfile, defaultOp, parent);
    return dlg.exec() == QDialog::Accepted;
}

// ═════════════════════════ 放樣 ═════════════════════════

namespace {

class LoftDialog : public QDialog {
public:
    LoftDialog(cad::Document* doc, cad::SketchLoft* existing, SolidOp defOp, QWidget* parent)
        : QDialog(parent), m_doc(doc), m_loft(existing)
    {
        setWindowTitle(existing ? tr("編輯放樣 — %1").arg(existing->name()) : tr("放樣"));
        resize(480, 480);
        auto* lay = new QVBoxLayout(this);

        lay->addWidget(new QLabel(tr("勾選要放樣的斷面草圖，並用上移／下移調整順序（至少 2 個）："), this));
        m_list = new QListWidget(this);
        lay->addWidget(m_list, 1);

        // 已選的排在前面（維持既有順序），其餘附在後面未勾選
        QList<cad::Sketch*> ordered;
        if (existing) ordered = existing->sections();
        for (cad::Sketch* s : doc->allSketches())
            if (!ordered.contains(s)) ordered << s;
        const QList<cad::Sketch*> chosen = existing ? existing->sections() : QList<cad::Sketch*>();
        for (cad::Sketch* s : ordered) {
            auto* it = new QListWidgetItem(s->name(), m_list);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(chosen.contains(s) ? Qt::Checked : Qt::Unchecked);
            it->setData(Qt::UserRole, QVariant::fromValue(static_cast<QObject*>(s)));
        }

        auto* row = new QHBoxLayout;
        auto* up = new QPushButton(tr("上移"), this);
        auto* dn = new QPushButton(tr("下移"), this);
        row->addWidget(up); row->addWidget(dn); row->addStretch();
        lay->addLayout(row);
        connect(up, &QPushButton::clicked, this, [this] { moveCurrent(-1); });
        connect(dn, &QPushButton::clicked, this, [this] { moveCurrent(+1); });

        m_ruled = new QCheckBox(tr("直紋（斷面之間用直線連接，不做平滑）"), this);
        m_ruled->setChecked(existing && existing->isRuled());
        lay->addWidget(m_ruled);

        m_op = new OpTargetBox(doc, existing, existing ? existing->operation() : defOp,
                               existing ? existing->target() : nullptr, this);
        lay->addWidget(m_op);
        addButtons(this, lay);
    }

    void accept() override
    {
        QList<cad::Sketch*> secs;
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem* it = m_list->item(i);
            if (it->checkState() != Qt::Checked) continue;
            if (auto* s = qobject_cast<cad::Sketch*>(it->data(Qt::UserRole).value<QObject*>())) secs << s;
        }
        QString err;
        if (secs.size() < 2) { QMessageBox::warning(this, windowTitle(), tr("放樣至少需要 2 個斷面。")); return; }
        if (!m_op->validate(&err)) { QMessageBox::warning(this, windowTitle(), err); return; }

        cad::SketchLoft* lf = m_loft;
        const bool creating = (lf == nullptr);
        if (creating) lf = new cad::SketchLoft(m_doc);
        lf->setSections(secs);
        lf->setRuled(m_ruled->isChecked());
        lf->setOperationAndTarget(m_op->op(), m_op->target());
        if (lf->target()) lf->target()->setVisible(false);

        if (creating) {
            QList<cad::Sketch*> consumed = secs;
            QList<cad::Feature*> consumedF;
            if (lf->target()) consumedF << lf->target();
            m_doc->registerSolidFeature(lf, QStringLiteral("Loft"), consumed, consumedF);
        } else {
            m_doc->rebuildFeature(lf);
        }
        m_doc->setModified(true);
        QDialog::accept();
    }

private:
    void moveCurrent(int d)
    {
        const int r = m_list->currentRow();
        const int nr = r + d;
        if (r < 0 || nr < 0 || nr >= m_list->count()) return;
        QListWidgetItem* it = m_list->takeItem(r);
        m_list->insertItem(nr, it);
        m_list->setCurrentRow(nr);
    }

    cad::Document*   m_doc;
    cad::SketchLoft* m_loft;
    QListWidget*     m_list;
    QCheckBox*       m_ruled;
    OpTargetBox*     m_op;
};

} // namespace

bool runLoftDialog(cad::Document* doc, SolidOp defaultOp, QWidget* parent)
{
    if (!doc) return false;
    if (doc->allSketches().size() < 2) {
        QMessageBox::information(parent, QObject::tr("放樣"), QObject::tr("放樣至少需要 2 張斷面草圖。"));
        return false;
    }
    LoftDialog dlg(doc, nullptr, defaultOp, parent);
    return dlg.exec() == QDialog::Accepted;
}

// ═════════════════════════ 布林 ═════════════════════════

namespace {

class BooleanDialog : public QDialog {
public:
    BooleanDialog(cad::Document* doc, cad::SolidBoolean* existing, QWidget* parent)
        : QDialog(parent), m_doc(doc), m_bool(existing)
    {
        setWindowTitle(existing ? tr("編輯布林運算 — %1").arg(existing->name()) : tr("布林運算"));
        resize(440, 240);
        auto* lay = new QVBoxLayout(this);
        auto* form = new QFormLayout;

        m_op = new QComboBox(this);
        m_op->addItem(tr("聯集（A ∪ B）"), static_cast<int>(SolidOp::Fuse));
        m_op->addItem(tr("差集（A − B，B 是被挖掉的工具）"), static_cast<int>(SolidOp::Cut));
        m_op->addItem(tr("交集（A ∩ B）"), static_cast<int>(SolidOp::Common));
        if (existing)
            for (int i = 0; i < m_op->count(); ++i)
                if (m_op->itemData(i).toInt() == static_cast<int>(existing->operation())) m_op->setCurrentIndex(i);
        form->addRow(tr("運算："), m_op);

        // 候選：未被消耗的實體；編輯時也保留目前的 A、B
        QList<cad::Feature*> cands = doc->solidFeatureCandidates(existing, existing ? existing->baseFeature() : nullptr);
        if (existing && existing->toolFeature() && !cands.contains(existing->toolFeature())) {
            // 依賴檢查：保留目前工具
            cands << existing->toolFeature();
        }
        m_a = new QComboBox(this);
        m_b = new QComboBox(this);
        for (cad::Feature* f : cands) {
            const QVariant v = QVariant::fromValue(static_cast<QObject*>(f));
            m_a->addItem(f->name(), v);
            m_b->addItem(f->name(), v);
        }
        if (existing) {
            for (int i = 0; i < m_a->count(); ++i) {
                if (m_a->itemData(i).value<QObject*>() == existing->baseFeature()) m_a->setCurrentIndex(i);
                if (m_b->itemData(i).value<QObject*>() == existing->toolFeature()) m_b->setCurrentIndex(i);
            }
        } else if (m_b->count() > 1) {
            m_b->setCurrentIndex(1);
        }
        form->addRow(tr("實體 A（基底）："), m_a);
        form->addRow(tr("實體 B（工具）："), m_b);
        lay->addLayout(form);
        addButtons(this, lay);
    }

    void accept() override
    {
        auto* a = qobject_cast<cad::Feature*>(m_a->currentData().value<QObject*>());
        auto* b = qobject_cast<cad::Feature*>(m_b->currentData().value<QObject*>());
        if (!a || !b) { QMessageBox::warning(this, windowTitle(), tr("請選擇兩個實體。")); return; }
        if (a == b)   { QMessageBox::warning(this, windowTitle(), tr("A 與 B 不可是同一個實體。")); return; }

        cad::SolidBoolean* bo = m_bool;
        const bool creating = (bo == nullptr);
        if (creating) bo = new cad::SolidBoolean(m_doc);
        bo->setOperation(static_cast<SolidOp>(m_op->currentData().toInt()));
        bo->setFeatures(a, b);

        if (creating) m_doc->registerSolidFeature(bo, QStringLiteral("Boolean"), {}, { a, b });
        else {
            m_doc->refreshFeatureDependencies(bo);
            m_doc->rebuildFeature(bo);
        }
        m_doc->setModified(true);
        QDialog::accept();
    }

private:
    cad::Document*     m_doc;
    cad::SolidBoolean* m_bool;
    QComboBox *m_op, *m_a, *m_b;
};

} // namespace

bool runBooleanDialog(cad::Document* doc, QWidget* parent)
{
    if (!doc) return false;
    if (doc->solidFeatureCandidates().size() < 2) {
        QMessageBox::information(parent, QObject::tr("布林運算"),
                                 QObject::tr("布林運算需要至少兩個獨立的實體。"));
        return false;
    }
    BooleanDialog dlg(doc, nullptr, parent);
    return dlg.exec() == QDialog::Accepted;
}


// ═════════════════════════ 陣列／鏡射／孔精靈 ═════════════════════════

namespace {

/// 三個數值欄位（X／Y／Z）
class Vec3Edit : public QWidget {
public:
    explicit Vec3Edit(QWidget* parent = nullptr) : QWidget(parent)
    {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(0, 0, 0, 0);
        for (QDoubleSpinBox*& sb : m_sb) {
            sb = new QDoubleSpinBox(this);
            sb->setRange(-1e7, 1e7);
            sb->setDecimals(4);
            h->addWidget(sb);
        }
    }
    void set(double x, double y, double z) { m_sb[0]->setValue(x); m_sb[1]->setValue(y); m_sb[2]->setValue(z); }
    gp_Vec vec() const { return gp_Vec(m_sb[0]->value(), m_sb[1]->value(), m_sb[2]->value()); }
    gp_Pnt pnt() const { return gp_Pnt(m_sb[0]->value(), m_sb[1]->value(), m_sb[2]->value()); }
private:
    QDoubleSpinBox* m_sb[3] = { nullptr, nullptr, nullptr };
};

/// 方向預設：X／Y／Z／自訂，選了預設就把向量欄位填好
QComboBox* makeAxisPresetCombo(Vec3Edit* edit, QWidget* parent)
{
    auto* cb = new QComboBox(parent);
    cb->addItem(QObject::tr("X 軸方向"), 0);
    cb->addItem(QObject::tr("Y 軸方向"), 1);
    cb->addItem(QObject::tr("Z 軸方向"), 2);
    cb->addItem(QObject::tr("自訂"), 3);
    QObject::connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), edit, [cb, edit](int) {
        switch (cb->currentData().toInt()) {
        case 0: edit->set(1, 0, 0); break;
        case 1: edit->set(0, 1, 0); break;
        case 2: edit->set(0, 0, 1); break;
        default: break;
        }
    });
    return cb;
}

/// 把向量對應回預設選項（找不到 = 自訂）
void selectAxisPreset(QComboBox* cb, Vec3Edit* edit, const gp_Vec& v)
{
    int idx = 3;
    if (v.Magnitude() > 1e-9) {
        const gp_Vec n = v.Normalized();
        if (n.Subtracted(gp_Vec(1, 0, 0)).Magnitude() < 1e-9) idx = 0;
        else if (n.Subtracted(gp_Vec(0, 1, 0)).Magnitude() < 1e-9) idx = 1;
        else if (n.Subtracted(gp_Vec(0, 0, 1)).Magnitude() < 1e-9) idx = 2;
    }
    cb->setCurrentIndex(idx);
    edit->set(v.X(), v.Y(), v.Z());      // 自訂時保留原值；預設時等於預設
}

/// 來源實體下拉（userData = Feature*），並依需要預選
QComboBox* makeSourceCombo(cad::Document* doc, const cad::Feature* self, cad::Feature* current, QWidget* parent)
{
    auto* cb = new QComboBox(parent);
    for (cad::Feature* f : doc->solidFeatureCandidates(self, current))
        cb->addItem(f->name(), QVariant::fromValue(static_cast<QObject*>(f)));
    for (int i = 0; i < cb->count(); ++i)
        if (cb->itemData(i).value<QObject*>() == current) { cb->setCurrentIndex(i); break; }
    return cb;
}

cad::Feature* comboFeature(const QComboBox* cb)
{
    return qobject_cast<cad::Feature*>(cb->currentData().value<QObject*>());
}

/// 「特徵」模式要求來源是成形特徵且有運算目標
bool featureModeOk(const cad::Feature* src, QString* err)
{
    auto* sf = qobject_cast<const cad::ShapingFeature*>(src);
    if (!sf || sf->operation() == SolidOp::NewBody) {
        if (err) *err = QObject::tr("「特徵」模式的來源必須是挖除／聯集類的成形特徵（例如孔、擠出切除）。");
        return false;
    }
    return true;
}

// ───────────── 陣列 ─────────────
class PatternDialog : public QDialog {
public:
    PatternDialog(cad::Document* doc, cad::SolidPattern* existing, QWidget* parent)
        : QDialog(parent), m_doc(doc), m_pat(existing)
    {
        setWindowTitle(existing ? tr("編輯陣列 — %1").arg(existing->name()) : tr("陣列"));
        resize(520, 560);
        auto* lay = new QVBoxLayout(this);
        auto* top = new QFormLayout;

        m_source = makeSourceCombo(doc, existing, existing ? existing->sourceFeature() : nullptr, this);
        m_source->setEnabled(!existing);
        top->addRow(tr("來源："), m_source);

        m_mode = new QComboBox(this);
        m_mode->addItem(tr("整個實體（複製實體本身）"), 0);
        m_mode->addItem(tr("特徵（重複來源的挖除／加料，例如孔）"), 1);
        if (existing) m_mode->setCurrentIndex(existing->mode() == cad::SolidTransformCopy::Mode::Feature ? 1 : 0);
        top->addRow(tr("陣列對象："), m_mode);

        m_kind = new QComboBox(this);
        m_kind->addItem(tr("線性陣列"), 0);
        m_kind->addItem(tr("環狀陣列"), 1);
        top->addRow(tr("陣列方式："), m_kind);
        lay->addLayout(top);

        m_stack = new QStackedWidget(this);
        lay->addWidget(m_stack, 1);

        // ── 線性頁 ──
        auto* lin = new QWidget(this);
        auto* lf = new QFormLayout(lin);
        m_dir1 = new Vec3Edit(lin);
        m_dir1Preset = makeAxisPresetCombo(m_dir1, lin);
        lf->addRow(tr("方向 1："), m_dir1Preset);
        lf->addRow(tr("　向量："), m_dir1);
        m_count1 = new QSpinBox(lin); m_count1->setRange(1, 2000);
        m_spacing1 = new QDoubleSpinBox(lin); m_spacing1->setRange(-1e7, 1e7); m_spacing1->setDecimals(4);
        lf->addRow(tr("數量（含原件）："), m_count1);
        lf->addRow(tr("間距："), m_spacing1);
        m_useDir2 = new QCheckBox(tr("加入方向 2（矩形陣列）"), lin);
        lf->addRow(m_useDir2);
        m_dir2 = new Vec3Edit(lin);
        m_dir2Preset = makeAxisPresetCombo(m_dir2, lin);
        lf->addRow(tr("方向 2："), m_dir2Preset);
        lf->addRow(tr("　向量："), m_dir2);
        m_count2 = new QSpinBox(lin); m_count2->setRange(1, 2000);
        m_spacing2 = new QDoubleSpinBox(lin); m_spacing2->setRange(-1e7, 1e7); m_spacing2->setDecimals(4);
        lf->addRow(tr("數量（含原件）："), m_count2);
        lf->addRow(tr("間距："), m_spacing2);
        m_stack->addWidget(lin);

        // ── 環狀頁 ──
        auto* cir = new QWidget(this);
        auto* cf = new QFormLayout(cir);
        m_axisDir = new Vec3Edit(cir);
        m_axisPreset = makeAxisPresetCombo(m_axisDir, cir);
        m_axisOrigin = new Vec3Edit(cir);
        cf->addRow(tr("旋轉軸方向："), m_axisPreset);
        cf->addRow(tr("　向量："), m_axisDir);
        cf->addRow(tr("旋轉軸通過的點："), m_axisOrigin);
        m_count = new QSpinBox(cir); m_count->setRange(2, 2000);
        m_angle = new QDoubleSpinBox(cir); m_angle->setRange(-360.0, 360.0); m_angle->setDecimals(3);
        cf->addRow(tr("數量（含原件）："), m_count);
        cf->addRow(tr("總角度（°）："), m_angle);
        cf->addRow(new QLabel(tr("360° = 等分整圈；小於 360° = 第一個到最後一個剛好跨越該角度。"), cir));
        m_stack->addWidget(cir);

        auto* hint = new QLabel(tr("座標為世界座標。「特徵」模式會重複來源特徵的工具形狀，並套用與來源相同的運算（挖除／加料）。"), this);
        hint->setWordWrap(true);
        lay->addWidget(hint);
        addButtons(this, lay);

        // 初值
        cad::PatternParams p;
        if (existing) p = existing->params();
        else { p.count1 = 3; p.count = 6; }
        m_kind->setCurrentIndex(p.kind == cad::PatternParams::Kind::Circular ? 1 : 0);
        selectAxisPreset(m_dir1Preset, m_dir1, p.dir1);
        m_count1->setValue(p.count1); m_spacing1->setValue(p.spacing1);
        m_useDir2->setChecked(p.useDir2);
        selectAxisPreset(m_dir2Preset, m_dir2, p.dir2);
        m_count2->setValue(p.count2); m_spacing2->setValue(p.spacing2);
        selectAxisPreset(m_axisPreset, m_axisDir, p.axisDir);
        m_axisOrigin->set(p.axisOrigin.X(), p.axisOrigin.Y(), p.axisOrigin.Z());
        m_count->setValue(p.count); m_angle->setValue(p.totalAngle);

        connect(m_kind, QOverload<int>::of(&QComboBox::currentIndexChanged), m_stack, &QStackedWidget::setCurrentIndex);
        m_stack->setCurrentIndex(m_kind->currentIndex());
        auto upd = [this] {
            const bool on = m_useDir2->isChecked();
            m_dir2Preset->setEnabled(on); m_dir2->setEnabled(on);
            m_count2->setEnabled(on); m_spacing2->setEnabled(on);
        };
        connect(m_useDir2, &QCheckBox::toggled, this, upd);
        upd();
    }

    void accept() override
    {
        cad::Feature* src = comboFeature(m_source);
        if (!src) { QMessageBox::warning(this, windowTitle(), tr("請選擇來源。")); return; }
        const auto mode = m_mode->currentData().toInt() == 1 ? cad::SolidTransformCopy::Mode::Feature
                                                              : cad::SolidTransformCopy::Mode::Body;
        QString err;
        if (mode == cad::SolidTransformCopy::Mode::Feature && !featureModeOk(src, &err)) {
            QMessageBox::warning(this, windowTitle(), err);
            return;
        }

        cad::PatternParams p;
        p.kind = m_kind->currentIndex() == 1 ? cad::PatternParams::Kind::Circular : cad::PatternParams::Kind::Linear;
        p.dir1 = m_dir1->vec(); p.count1 = m_count1->value(); p.spacing1 = m_spacing1->value();
        p.useDir2 = m_useDir2->isChecked();
        p.dir2 = m_dir2->vec(); p.count2 = m_count2->value(); p.spacing2 = m_spacing2->value();
        p.axisOrigin = m_axisOrigin->pnt(); p.axisDir = m_axisDir->vec();
        p.count = m_count->value(); p.totalAngle = m_angle->value();

        cad::SolidPattern* pat = m_pat;
        const bool creating = (pat == nullptr);
        if (creating) pat = new cad::SolidPattern(m_doc);
        pat->setParams(p);
        pat->setMode(mode);

        if (creating) {
            pat->setSourceFeature(src);
            m_doc->registerSolidFeature(pat, QStringLiteral("Pattern"), {}, { src });
        } else {
            m_doc->rebuildFeature(pat);
        }
        m_doc->setModified(true);
        QDialog::accept();
    }

private:
    cad::Document* m_doc;
    cad::SolidPattern* m_pat;
    QComboBox *m_source, *m_mode, *m_kind, *m_dir1Preset, *m_dir2Preset, *m_axisPreset;
    QStackedWidget* m_stack;
    Vec3Edit *m_dir1, *m_dir2, *m_axisDir, *m_axisOrigin;
    QSpinBox *m_count1, *m_count2, *m_count;
    QDoubleSpinBox *m_spacing1, *m_spacing2, *m_angle;
    QCheckBox* m_useDir2;
};

// ───────────── 鏡射 ─────────────
class MirrorDialog : public QDialog {
public:
    MirrorDialog(cad::Document* doc, cad::SolidMirror* existing, QWidget* parent)
        : QDialog(parent), m_doc(doc), m_mir(existing)
    {
        setWindowTitle(existing ? tr("編輯鏡射 — %1").arg(existing->name()) : tr("鏡射"));
        resize(500, 360);
        auto* lay = new QVBoxLayout(this);
        auto* form = new QFormLayout;

        m_source = makeSourceCombo(doc, existing, existing ? existing->sourceFeature() : nullptr, this);
        m_source->setEnabled(!existing);
        form->addRow(tr("來源："), m_source);

        m_mode = new QComboBox(this);
        m_mode->addItem(tr("整個實體（鏡射實體本身）"), 0);
        m_mode->addItem(tr("特徵（鏡射來源的挖除／加料，例如孔）"), 1);
        if (existing) m_mode->setCurrentIndex(existing->mode() == cad::SolidTransformCopy::Mode::Feature ? 1 : 0);
        form->addRow(tr("鏡射對象："), m_mode);

        m_normal = new Vec3Edit(this);
        m_preset = new QComboBox(this);
        m_preset->addItem(tr("YZ 平面（法向 X）"), 0);
        m_preset->addItem(tr("XZ 平面（法向 Y）"), 1);
        m_preset->addItem(tr("XY 平面（法向 Z）"), 2);
        m_preset->addItem(tr("自訂"), 3);
        connect(m_preset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            switch (m_preset->currentData().toInt()) {
            case 0: m_normal->set(1, 0, 0); break;
            case 1: m_normal->set(0, 1, 0); break;
            case 2: m_normal->set(0, 0, 1); break;
            default: break;
            }
        });
        m_point = new Vec3Edit(this);
        form->addRow(tr("鏡射平面："), m_preset);
        form->addRow(tr("　法向量："), m_normal);
        form->addRow(tr("　平面通過的點："), m_point);

        m_keep = new QCheckBox(tr("保留原件（取消 = 只留鏡射後的實體；僅「整個實體」模式有效）"), this);
        m_keep->setChecked(true);
        lay->addLayout(form);
        lay->addWidget(m_keep);
        auto* hint = new QLabel(tr("座標為世界座標。想以某個面當鏡面，請把「通過的點」設在該面上、法向量設成該面法向。"), this);
        hint->setWordWrap(true);
        lay->addWidget(hint);
        addButtons(this, lay);

        gp_Vec n(1, 0, 0); gp_Pnt pt(0, 0, 0);
        if (existing) { n = existing->planeNormal(); pt = existing->planePoint(); }
        selectAxisPreset(m_preset, m_normal, n);          // 預設項與 selectAxisPreset 的 0/1/2 = X/Y/Z 一致
        m_point->set(pt.X(), pt.Y(), pt.Z());
        if (existing) m_keep->setChecked(existing->isKeepOriginal());
    }

    void accept() override
    {
        cad::Feature* src = comboFeature(m_source);
        if (!src) { QMessageBox::warning(this, windowTitle(), tr("請選擇來源。")); return; }
        const auto mode = m_mode->currentData().toInt() == 1 ? cad::SolidTransformCopy::Mode::Feature
                                                              : cad::SolidTransformCopy::Mode::Body;
        QString err;
        if (mode == cad::SolidTransformCopy::Mode::Feature && !featureModeOk(src, &err)) {
            QMessageBox::warning(this, windowTitle(), err);
            return;
        }
        if (m_normal->vec().Magnitude() < 1e-9) {
            QMessageBox::warning(this, windowTitle(), tr("法向量不可為零向量。"));
            return;
        }

        cad::SolidMirror* mir = m_mir;
        const bool creating = (mir == nullptr);
        if (creating) mir = new cad::SolidMirror(m_doc);
        mir->setPlane(m_point->pnt(), m_normal->vec());
        mir->setMode(mode);
        mir->setKeepOriginal(m_keep->isChecked());

        if (creating) {
            mir->setSourceFeature(src);
            m_doc->registerSolidFeature(mir, QStringLiteral("Mirror"), {}, { src });
        } else {
            m_doc->rebuildFeature(mir);
        }
        m_doc->setModified(true);
        QDialog::accept();
    }

private:
    cad::Document* m_doc;
    cad::SolidMirror* m_mir;
    QComboBox *m_source, *m_mode, *m_preset;
    Vec3Edit *m_normal, *m_point;
    QCheckBox* m_keep;
};

// ───────────── 孔精靈 ─────────────
struct MetricSpec { const char* name; double clearance, tap, cbDia, cbDepth, csDia; };
// 常用公制螺絲的參考值（中等配合通孔、攻牙底孔、內六角螺栓沉孔、90° 錐孔）；請依實際圖面／標準確認
const MetricSpec kMetric[] = {
    { "M3",  3.4,  2.5,  6.5,  3.4,  6.3 },
    { "M4",  4.5,  3.3,  8.0,  4.4,  8.4 },
    { "M5",  5.5,  4.2,  9.5,  5.4, 10.4 },
    { "M6",  6.6,  5.0, 11.0,  6.5, 12.6 },
    { "M8",  9.0,  6.8, 15.0,  8.6, 17.3 },
    { "M10", 11.0, 8.5, 18.0, 10.8, 20.0 },
    { "M12", 13.5, 10.2, 20.0, 12.8, 24.0 },
};

class HoleDialog : public QDialog {
public:
    HoleDialog(cad::Document* doc, cad::HoleFeature* existing, cad::Sketch* preset, QWidget* parent)
        : QDialog(parent), m_doc(doc), m_hole(existing)
    {
        setWindowTitle(existing ? tr("編輯孔 — %1").arg(existing->name()) : tr("孔精靈"));
        resize(500, 560);
        auto* lay = new QVBoxLayout(this);
        auto* form = new QFormLayout;

        m_sketch = new QComboBox(this);
        fillSketchCombo(m_sketch, doc->allSketches(), existing ? existing->sketch() : preset);
        m_sketch->setEnabled(!existing);
        form->addRow(tr("孔位草圖："), m_sketch);

        m_target = new QComboBox(this);
        cad::Feature* curT = existing ? existing->target() : nullptr;
        for (cad::Feature* f : doc->solidFeatureCandidates(existing, curT))
            m_target->addItem(f->name(), QVariant::fromValue(static_cast<QObject*>(f)));
        for (int i = 0; i < m_target->count(); ++i)
            if (m_target->itemData(i).value<QObject*>() == curT) { m_target->setCurrentIndex(i); break; }
        form->addRow(tr("目標實體："), m_target);

        m_type = new QComboBox(this);
        m_type->addItem(tr("簡單孔"), 0);
        m_type->addItem(tr("沉頭孔（內六角螺栓用）"), 1);
        m_type->addItem(tr("錐孔（flat head 螺絲用）"), 2);
        form->addRow(tr("孔型式："), m_type);

        m_size = new QComboBox(this);
        m_size->addItem(tr("自訂"), -1);
        for (int i = 0; i < int(sizeof(kMetric) / sizeof(kMetric[0])); ++i)
            m_size->addItem(QString::fromLatin1(kMetric[i].name), i);
        m_fit = new QComboBox(this);
        m_fit->addItem(tr("通孔（中等配合）"), 0);
        m_fit->addItem(tr("螺紋底孔"), 1);
        form->addRow(tr("公制規格："), m_size);
        form->addRow(tr("孔徑依據："), m_fit);

        m_dia = dspin(0.01, 1e5);
        form->addRow(tr("孔徑："), m_dia);
        m_through = new QCheckBox(tr("貫穿"), this);
        form->addRow(QString(), m_through);
        m_depth = dspin(0.01, 1e6);
        form->addRow(tr("孔深（從表面起算）："), m_depth);
        m_cbDia = dspin(0.01, 1e5);   form->addRow(tr("沉孔直徑："), m_cbDia);
        m_cbDepth = dspin(0.01, 1e5); form->addRow(tr("沉孔深度："), m_cbDepth);
        m_csDia = dspin(0.01, 1e5);   form->addRow(tr("錐孔表面直徑："), m_csDia);
        m_csAngle = dspin(1.0, 179.0); form->addRow(tr("錐角（°）："), m_csAngle);
        m_flip = new QCheckBox(tr("反向（程式已自動朝向實體內部，方向不對時才勾）"), this);
        form->addRow(QString(), m_flip);
        lay->addLayout(form);

        auto* hint = new QLabel(tr("孔位 = 草圖中的「點」與「圓的圓心」。先在要打孔的面上開一張草圖並放置點。\n"
                                   "規格表為常用參考值，請依實際圖面確認；選了規格後仍可手動修改各欄位。"), this);
        hint->setWordWrap(true);
        lay->addWidget(hint);
        addButtons(this, lay);

        cad::HoleFeature::Params p;
        if (existing) p = existing->params();
        m_type->setCurrentIndex(static_cast<int>(p.type));
        m_dia->setValue(p.diameter);
        m_through->setChecked(p.throughAll);
        m_depth->setValue(p.depth);
        m_cbDia->setValue(p.cboreDiameter); m_cbDepth->setValue(p.cboreDepth);
        m_csDia->setValue(p.csinkDiameter); m_csAngle->setValue(p.csinkAngle);
        m_flip->setChecked(p.flip);

        connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { applySpec(); updateEnabled(); });
        connect(m_size, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { applySpec(); });
        connect(m_fit,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { applySpec(); });
        connect(m_through, &QCheckBox::toggled, this, [this] { updateEnabled(); });
        updateEnabled();
    }

    void accept() override
    {
        cad::Sketch* sk = comboSketch(m_sketch);
        cad::Feature* tg = comboFeature(m_target);
        if (!sk) { QMessageBox::warning(this, windowTitle(), tr("請選擇孔位草圖。")); return; }
        if (!tg) { QMessageBox::warning(this, windowTitle(), tr("請選擇目標實體。")); return; }

        cad::HoleFeature::Params p;
        p.type = static_cast<cad::HoleFeature::HoleType>(m_type->currentData().toInt());
        p.diameter = m_dia->value();
        p.throughAll = m_through->isChecked();
        p.depth = m_depth->value();
        p.cboreDiameter = m_cbDia->value(); p.cboreDepth = m_cbDepth->value();
        p.csinkDiameter = m_csDia->value(); p.csinkAngle = m_csAngle->value();
        p.flip = m_flip->isChecked();
        if (m_size->currentData().toInt() >= 0)
            p.spec = QStringLiteral("%1 %2").arg(m_size->currentText(), m_fit->currentText());

        QString err;
        if (!cad::HoleFeature::validate(p, &err)) { QMessageBox::warning(this, windowTitle(), err); return; }

        cad::HoleFeature* h = m_hole;
        const bool creating = (h == nullptr);
        if (creating) { h = new cad::HoleFeature(m_doc); h->setSketch(sk); }
        h->setParams(p);
        h->setOperationAndTarget(cad::SolidOp::Cut, tg);
        tg->setVisible(false);

        if (creating) m_doc->registerSolidFeature(h, QStringLiteral("Hole"), { sk }, { tg });
        else          m_doc->rebuildFeature(h);
        m_doc->setModified(true);
        QDialog::accept();
    }

private:
    QDoubleSpinBox* dspin(double lo, double hi)
    {
        auto* s = new QDoubleSpinBox(this);
        s->setRange(lo, hi);
        s->setDecimals(3);
        return s;
    }

    void applySpec()
    {
        const int i = m_size->currentData().toInt();
        if (i < 0) return;                                  // 自訂：不動欄位
        const MetricSpec& m = kMetric[i];
        m_dia->setValue(m_fit->currentData().toInt() == 1 ? m.tap : m.clearance);
        m_cbDia->setValue(m.cbDia);
        m_cbDepth->setValue(m.cbDepth);
        m_csDia->setValue(m.csDia);
        m_csAngle->setValue(90.0);
    }

    void updateEnabled()
    {
        const int t = m_type->currentData().toInt();
        m_depth->setEnabled(!m_through->isChecked());
        m_cbDia->setEnabled(t == 1);   m_cbDepth->setEnabled(t == 1);
        m_csDia->setEnabled(t == 2);   m_csAngle->setEnabled(t == 2);
    }

    cad::Document* m_doc;
    cad::HoleFeature* m_hole;
    QComboBox *m_sketch, *m_target, *m_type, *m_size, *m_fit;
    QDoubleSpinBox *m_dia, *m_depth, *m_cbDia, *m_cbDepth, *m_csDia, *m_csAngle;
    QCheckBox *m_through, *m_flip;
};

} // namespace

bool runPatternDialog(cad::Document* doc, QWidget* parent)
{
    if (!doc) return false;
    if (doc->solidFeatureCandidates().isEmpty()) {
        QMessageBox::information(parent, QObject::tr("陣列"), QObject::tr("文件裡還沒有可以陣列的實體。"));
        return false;
    }
    PatternDialog dlg(doc, nullptr, parent);
    return dlg.exec() == QDialog::Accepted;
}

bool runMirrorDialog(cad::Document* doc, QWidget* parent)
{
    if (!doc) return false;
    if (doc->solidFeatureCandidates().isEmpty()) {
        QMessageBox::information(parent, QObject::tr("鏡射"), QObject::tr("文件裡還沒有可以鏡射的實體。"));
        return false;
    }
    MirrorDialog dlg(doc, nullptr, parent);
    return dlg.exec() == QDialog::Accepted;
}

bool runHoleDialog(cad::Document* doc, cad::Sketch* presetSketch, QWidget* parent)
{
    if (!doc) return false;
    if (doc->allSketches().isEmpty()) {
        QMessageBox::information(parent, QObject::tr("孔精靈"),
                                 QObject::tr("請先在要打孔的面上建立一張草圖，並放置孔位的點。"));
        return false;
    }
    if (doc->solidFeatureCandidates().isEmpty()) {
        QMessageBox::information(parent, QObject::tr("孔精靈"), QObject::tr("文件裡還沒有可以打孔的實體。"));
        return false;
    }
    HoleDialog dlg(doc, nullptr, presetSketch, parent);
    return dlg.exec() == QDialog::Accepted;
}

// ═════════════════════════ 圓角／抽殼數值 ═════════════════════════

bool askFilletRadius(double* radius, QWidget* parent, double initial)
{
    bool ok = false;
    const double v = QInputDialog::getDouble(parent, QObject::tr("3D 圓角"), QObject::tr("圓角半徑："),
                                             initial, 1e-6, 1e9, 4, &ok);
    if (ok && radius) *radius = v;
    return ok;
}

bool askShellThickness(double* thickness, QWidget* parent, double initial)
{
    bool ok = false;
    const double v = QInputDialog::getDouble(parent, QObject::tr("抽殼"),
                         QObject::tr("壁厚（正 = 向內抽殼，負 = 向外加厚）："),
                         initial, -1e9, 1e9, 4, &ok);
    if (ok && qAbs(v) < 1e-9) {
        QMessageBox::warning(parent, QObject::tr("抽殼"), QObject::tr("壁厚不可為 0。"));
        return false;
    }
    if (ok && thickness) *thickness = v;
    return ok;
}

// ═════════════════════════ 編輯分派 ═════════════════════════

bool editSolidFeature(cad::Feature* f, QWidget* parent)
{
    if (!f || !f->document()) return false;
    cad::Document* doc = f->document();

    if (auto* r = qobject_cast<cad::Revolve*>(f)) {
        RevolveDialog dlg(doc, r, nullptr, r->operation(), parent);
        return dlg.exec() == QDialog::Accepted;
    }
    if (auto* s = qobject_cast<cad::Sweep*>(f)) {
        SweepDialog dlg(doc, s, nullptr, s->operation(), parent);
        return dlg.exec() == QDialog::Accepted;
    }
    if (auto* l = qobject_cast<cad::SketchLoft*>(f)) {
        LoftDialog dlg(doc, l, l->operation(), parent);
        return dlg.exec() == QDialog::Accepted;
    }
    if (auto* b = qobject_cast<cad::SolidBoolean*>(f)) {
        BooleanDialog dlg(doc, b, parent);
        return dlg.exec() == QDialog::Accepted;
    }
    if (auto* pt = qobject_cast<cad::SolidPattern*>(f)) {
        PatternDialog dlg(doc, pt, parent);
        return dlg.exec() == QDialog::Accepted;
    }
    if (auto* mr = qobject_cast<cad::SolidMirror*>(f)) {
        MirrorDialog dlg(doc, mr, parent);
        return dlg.exec() == QDialog::Accepted;
    }
    if (auto* hl = qobject_cast<cad::HoleFeature*>(f)) {
        HoleDialog dlg(doc, hl, nullptr, parent);
        return dlg.exec() == QDialog::Accepted;
    }
    if (auto* fl = qobject_cast<cad::FilletSolid*>(f)) {
        double r = fl->radius();
        if (!askFilletRadius(&r, parent, r)) return false;
        fl->setRadius(r);
        doc->rebuildFeature(fl);
        doc->setModified(true);
        return true;
    }
    if (auto* sh = qobject_cast<cad::ShellSolid*>(f)) {
        double t = sh->thickness();
        if (!askShellThickness(&t, parent, t)) return false;
        sh->setThickness(t);
        doc->rebuildFeature(sh);
        doc->setModified(true);
        return true;
    }
    return false;
}

} // namespace ui
} // namespace aicad
