/**
 * @file DefinePlaneDialog.cpp
 */
#include "DefinePlaneDialog.h"

#include "cad/DefinedPlane.h"
#include "cad/Document.h"
#include "cad/Sketch.h"
#include "railway/RailwayAlignment.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <functional>

namespace aicad {
namespace ui {

using cad::PlaneDef;
using cad::PlaneDirRef;
using cad::PlaneLineRef;
using cad::PlaneMethod;
using cad::PlanePointRef;

// ============================================================================
//  小型編輯元件（無 Q_OBJECT：變更通知用 std::function）
// ============================================================================
namespace {

enum GeomMask { MLine = 1, MArc = 2, MCircle = 4, MPoly = 8, MSpline = 16, MPoint = 32, MEllipse = 64 };
constexpr int kAnyGeom   = MLine | MArc | MCircle | MPoly | MSpline | MPoint | MEllipse;
constexpr int kCurveGeom = MLine | MArc | MPoly | MSpline;          // 有端點切線的幾何

int maskOf(cad::SketchGeometryType t)
{
    switch (t) {
    case cad::SketchGeometryType::Line:     return MLine;
    case cad::SketchGeometryType::Arc:      return MArc;
    case cad::SketchGeometryType::Circle:   return MCircle;
    case cad::SketchGeometryType::Polyline: return MPoly;
    case cad::SketchGeometryType::Spline:   return MSpline;
    case cad::SketchGeometryType::Point:    return MPoint;
    case cad::SketchGeometryType::Ellipse:  return MEllipse;
    }
    return 0;
}

QComboBox* makeTclCombo(cad::Document* doc, QWidget* parent)
{
    auto* cb = new QComboBox(parent);
    if (doc)
        for (railway::TrackCenterLine* t : doc->trackCenterLines())
            cb->addItem(t->name().isEmpty() ? t->id() : t->name(), t->id());
    return cb;
}

/// 草圖 + 幾何 (+ 取哪一點) 選取器
class GeomPicker : public QWidget {
public:
    GeomPicker(cad::Document* doc, int mask, const QStringList& whichLabels, const QList<int>& whichValues, QWidget* parent)
        : QWidget(parent), m_doc(doc), m_mask(mask)
    {
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        m_sketch = new QComboBox(this);
        m_geom   = new QComboBox(this);
        lay->addWidget(m_sketch, 1);
        lay->addWidget(m_geom, 1);
        if (!whichLabels.isEmpty()) {
            m_which = new QComboBox(this);
            for (int i = 0; i < whichLabels.size(); ++i) m_which->addItem(whichLabels[i], whichValues.value(i));
            lay->addWidget(m_which);
        }
        if (doc)
            for (cad::Feature* f : doc->features())
                if (auto* sk = qobject_cast<cad::Sketch*>(f)) m_sketch->addItem(sk->name(), sk->id());
        QObject::connect(m_sketch, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            fillGeoms();
            if (changed) changed();
        });
        QObject::connect(m_geom, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { if (changed) changed(); });
        if (m_which)
            QObject::connect(m_which, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { if (changed) changed(); });
        fillGeoms();
    }

    QString sketchId() const { return m_sketch->currentData().toString(); }
    QString geomUuid() const { return m_geom->currentData().toString(); }
    int     which()    const { return m_which ? m_which->currentData().toInt() : 0; }

    void set(const QString& sketchId, const QString& geomUuid, int which)
    {
        int si = m_sketch->findData(sketchId);
        if (si >= 0) m_sketch->setCurrentIndex(si);
        fillGeoms();
        int gi = m_geom->findData(geomUuid);
        if (gi >= 0) m_geom->setCurrentIndex(gi);
        if (m_which) { int wi = m_which->findData(which); if (wi >= 0) m_which->setCurrentIndex(wi); }
    }

    bool hasChoices() const { return m_geom->count() > 0; }

    std::function<void()> changed;

private:
    void fillGeoms()
    {
        const QString prev = m_geom->currentData().toString();
        QSignalBlocker b(m_geom);
        m_geom->clear();
        auto* sk = m_doc ? qobject_cast<cad::Sketch*>(m_doc->findFeature(sketchId())) : nullptr;
        if (sk) {
            const auto& geoms = sk->geometriesRef();
            for (int i = 0; i < geoms.size(); ++i) {
                const cad::SketchGeometry* g = geoms[i];
                if (!g || !(maskOf(g->type) & m_mask)) continue;
                m_geom->addItem(cad::DefinedPlane::geometryLabel(sk, i), g->uuid);
            }
        }
        const int pi = m_geom->findData(prev);
        if (pi >= 0) m_geom->setCurrentIndex(pi);
    }

    cad::Document* m_doc;
    int        m_mask;
    QComboBox* m_sketch = nullptr;
    QComboBox* m_geom   = nullptr;
    QComboBox* m_which  = nullptr;
};

const QStringList kPointWhichLabels = { QStringLiteral("起點"), QStringLiteral("終點"), QStringLiteral("中點"), QStringLiteral("圓心") };
const QList<int>  kPointWhichValues = { cad::WhichStart, cad::WhichEnd, cad::WhichMid, cad::WhichCenter };
const QStringList kEndWhichLabels   = { QStringLiteral("起點"), QStringLiteral("終點") };
const QList<int>  kEndWhichValues   = { cad::WhichStart, cad::WhichEnd };

/// 點的編輯：座標／草圖幾何的點／線形里程
class PointEditor : public QWidget {
public:
    PointEditor(cad::Document* doc, QWidget* parent) : QWidget(parent)
    {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        m_kind = new QComboBox(this);
        m_kind->addItem(QStringLiteral("座標 XYZ"), int(PlanePointRef::Coord));
        m_kind->addItem(QStringLiteral("草圖幾何的點（端點／中點／圓心）"), int(PlanePointRef::GeomPoint));
        m_kind->addItem(QStringLiteral("線形里程"), int(PlanePointRef::Alignment));
        lay->addWidget(m_kind);

        m_stack = new QStackedWidget(this);
        lay->addWidget(m_stack);

        auto* c = new QWidget(m_stack);                       // 座標
        auto* cl = new QHBoxLayout(c); cl->setContentsMargins(0, 0, 0, 0);
        for (QLineEdit** e : { &m_x, &m_y, &m_z }) { *e = new QLineEdit(QStringLiteral("0"), c); cl->addWidget(*e); }
        m_stack->addWidget(c);

        m_geom = new GeomPicker(doc, kAnyGeom, kPointWhichLabels, kPointWhichValues, m_stack);   // 草圖幾何
        m_stack->addWidget(m_geom);

        auto* a = new QWidget(m_stack);                       // 線形
        auto* al = new QHBoxLayout(a); al->setContentsMargins(0, 0, 0, 0);
        m_tcl = makeTclCombo(doc, a);
        m_chain = new QLineEdit(QStringLiteral("0"), a);
        m_chain->setToolTip(QStringLiteral("里程 [m]"));
        al->addWidget(m_tcl, 1); al->addWidget(m_chain);
        m_stack->addWidget(a);

        auto notify = [this]() { if (changed) changed(); };
        QObject::connect(m_kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, notify](int i) {
            m_stack->setCurrentIndex(i); notify();
        });
        for (QLineEdit* e : { m_x, m_y, m_z, m_chain }) QObject::connect(e, &QLineEdit::textChanged, this, [notify]() { notify(); });
        QObject::connect(m_tcl, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
        m_geom->changed = notify;
    }

    PlanePointRef value() const
    {
        PlanePointRef r;
        r.kind = static_cast<PlanePointRef::Kind>(m_kind->currentData().toInt());
        r.x = m_x->text().toDouble(); r.y = m_y->text().toDouble(); r.z = m_z->text().toDouble();
        r.sketchId = m_geom->sketchId(); r.geomUuid = m_geom->geomUuid(); r.which = m_geom->which();
        r.tclId = m_tcl->currentData().toString(); r.chainage = m_chain->text().toDouble();
        return r;
    }
    void setValue(const PlanePointRef& r)
    {
        const int ki = m_kind->findData(int(r.kind));
        if (ki >= 0) m_kind->setCurrentIndex(ki);
        m_stack->setCurrentIndex(m_kind->currentIndex());
        m_x->setText(QString::number(r.x, 'g', 12)); m_y->setText(QString::number(r.y, 'g', 12)); m_z->setText(QString::number(r.z, 'g', 12));
        m_geom->set(r.sketchId, r.geomUuid, r.which);
        const int ti = m_tcl->findData(r.tclId);
        if (ti >= 0) m_tcl->setCurrentIndex(ti);
        m_chain->setText(QString::number(r.chainage, 'g', 12));
    }

    std::function<void()> changed;

private:
    QComboBox* m_kind; QStackedWidget* m_stack;
    QLineEdit *m_x, *m_y, *m_z, *m_chain;
    GeomPicker* m_geom; QComboBox* m_tcl;
};

/// 方向的編輯：向量／草圖幾何端點切線／基準軸／線形切線
class DirEditor : public QWidget {
public:
    DirEditor(cad::Document* doc, QWidget* parent) : QWidget(parent)
    {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        m_kind = new QComboBox(this);
        m_kind->addItem(QStringLiteral("向量 XYZ"), int(PlaneDirRef::Vector));
        m_kind->addItem(QStringLiteral("草圖幾何端點的切線"), int(PlaneDirRef::GeomTangent));
        m_kind->addItem(QStringLiteral("基準軸 X／Y／Z"), int(PlaneDirRef::Axis));
        m_kind->addItem(QStringLiteral("線形切線（里程）"), int(PlaneDirRef::AlignmentTangent));
        lay->addWidget(m_kind);

        m_stack = new QStackedWidget(this);
        lay->addWidget(m_stack);

        auto* v = new QWidget(m_stack);
        auto* vl = new QHBoxLayout(v); vl->setContentsMargins(0, 0, 0, 0);
        m_x = new QLineEdit(QStringLiteral("0"), v); m_y = new QLineEdit(QStringLiteral("0"), v); m_z = new QLineEdit(QStringLiteral("1"), v);
        vl->addWidget(m_x); vl->addWidget(m_y); vl->addWidget(m_z);
        m_stack->addWidget(v);

        m_geom = new GeomPicker(doc, kCurveGeom, kEndWhichLabels, kEndWhichValues, m_stack);
        m_stack->addWidget(m_geom);

        m_axis = new QComboBox(m_stack);
        m_axis->addItem(QStringLiteral("X 軸"), 0); m_axis->addItem(QStringLiteral("Y 軸"), 1); m_axis->addItem(QStringLiteral("Z 軸"), 2);
        m_axis->setCurrentIndex(2);
        m_stack->addWidget(m_axis);

        auto* a = new QWidget(m_stack);
        auto* al = new QHBoxLayout(a); al->setContentsMargins(0, 0, 0, 0);
        m_tcl = makeTclCombo(doc, a);
        m_chain = new QLineEdit(QStringLiteral("0"), a);
        al->addWidget(m_tcl, 1); al->addWidget(m_chain);
        m_stack->addWidget(a);

        m_rev = new QCheckBox(QStringLiteral("反向"), this);
        lay->addWidget(m_rev);

        auto notify = [this]() { if (changed) changed(); };
        QObject::connect(m_kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, notify](int i) {
            m_stack->setCurrentIndex(i); notify();
        });
        for (QLineEdit* e : { m_x, m_y, m_z, m_chain }) QObject::connect(e, &QLineEdit::textChanged, this, [notify]() { notify(); });
        QObject::connect(m_axis, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
        QObject::connect(m_tcl,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
        QObject::connect(m_rev, &QCheckBox::toggled, this, [notify](bool) { notify(); });
        m_geom->changed = notify;
    }

    PlaneDirRef value() const
    {
        PlaneDirRef r;
        r.kind = static_cast<PlaneDirRef::Kind>(m_kind->currentData().toInt());
        r.x = m_x->text().toDouble(); r.y = m_y->text().toDouble(); r.z = m_z->text().toDouble();
        r.sketchId = m_geom->sketchId(); r.geomUuid = m_geom->geomUuid(); r.which = m_geom->which();
        r.axis = m_axis->currentData().toInt();
        r.tclId = m_tcl->currentData().toString(); r.chainage = m_chain->text().toDouble();
        r.reverse = m_rev->isChecked();
        return r;
    }
    void setValue(const PlaneDirRef& r)
    {
        const int ki = m_kind->findData(int(r.kind));
        if (ki >= 0) m_kind->setCurrentIndex(ki);
        m_stack->setCurrentIndex(m_kind->currentIndex());
        m_x->setText(QString::number(r.x, 'g', 12)); m_y->setText(QString::number(r.y, 'g', 12)); m_z->setText(QString::number(r.z, 'g', 12));
        m_geom->set(r.sketchId, r.geomUuid, r.which);
        const int ai = m_axis->findData(r.axis);
        if (ai >= 0) m_axis->setCurrentIndex(ai);
        const int ti = m_tcl->findData(r.tclId);
        if (ti >= 0) m_tcl->setCurrentIndex(ti);
        m_chain->setText(QString::number(r.chainage, 'g', 12));
        m_rev->setChecked(r.reverse);
    }

    std::function<void()> changed;

private:
    QComboBox* m_kind; QStackedWidget* m_stack;
    QLineEdit *m_x, *m_y, *m_z, *m_chain;
    GeomPicker* m_geom; QComboBox *m_axis, *m_tcl; QCheckBox* m_rev;
};

/// 線的編輯：草圖直線／兩點
class LineEditor : public QWidget {
public:
    LineEditor(cad::Document* doc, QWidget* parent) : QWidget(parent)
    {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        m_kind = new QComboBox(this);
        m_kind->addItem(QStringLiteral("草圖中的直線"), int(PlaneLineRef::SketchLine));
        m_kind->addItem(QStringLiteral("兩點連線（A → B）"), int(PlaneLineRef::TwoPoints));
        lay->addWidget(m_kind);

        m_stack = new QStackedWidget(this);
        lay->addWidget(m_stack);

        m_geom = new GeomPicker(doc, MLine, {}, {}, m_stack);
        m_stack->addWidget(m_geom);

        auto* two = new QWidget(m_stack);
        auto* tl = new QVBoxLayout(two); tl->setContentsMargins(0, 0, 0, 0);
        tl->addWidget(new QLabel(QStringLiteral("點 A："), two));
        m_a = new PointEditor(doc, two); tl->addWidget(m_a);
        tl->addWidget(new QLabel(QStringLiteral("點 B："), two));
        m_b = new PointEditor(doc, two); tl->addWidget(m_b);
        m_stack->addWidget(two);

        auto notify = [this]() { if (changed) changed(); };
        QObject::connect(m_kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, notify](int i) {
            m_stack->setCurrentIndex(i); notify();
        });
        m_geom->changed = notify; m_a->changed = notify; m_b->changed = notify;
    }

    PlaneLineRef value() const
    {
        PlaneLineRef r;
        r.kind = static_cast<PlaneLineRef::Kind>(m_kind->currentData().toInt());
        r.sketchId = m_geom->sketchId(); r.geomUuid = m_geom->geomUuid();
        r.a = m_a->value(); r.b = m_b->value();
        return r;
    }
    void setValue(const PlaneLineRef& r)
    {
        const int ki = m_kind->findData(int(r.kind));
        if (ki >= 0) m_kind->setCurrentIndex(ki);
        m_stack->setCurrentIndex(m_kind->currentIndex());
        m_geom->set(r.sketchId, r.geomUuid, 0);
        m_a->setValue(r.a); m_b->setValue(r.b);
    }

    std::function<void()> changed;

private:
    QComboBox* m_kind; QStackedWidget* m_stack;
    GeomPicker* m_geom; PointEditor *m_a, *m_b;
};

QComboBox* makeBaseCombo(cad::Document* doc, const cad::DefinedPlane* existing, QWidget* parent)
{
    auto* cb = new QComboBox(parent);
    cb->addItem(QStringLiteral("XY 平面"), QStringLiteral("XY"));
    cb->addItem(QStringLiteral("XZ 平面"), QStringLiteral("XZ"));
    cb->addItem(QStringLiteral("YZ 平面"), QStringLiteral("YZ"));
    if (doc)
        for (cad::Feature* f : doc->features())
            if (auto* dp = qobject_cast<cad::DefinedPlane*>(f))
                if (dp != existing) cb->addItem(dp->name(), dp->id());
    return cb;
}

QWidget* wrapForm(const std::function<void(QFormLayout*, QWidget*)>& fill, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* f = new QFormLayout(w);
    f->setContentsMargins(0, 0, 0, 0);
    fill(f, w);
    return w;
}

} // namespace

// ============================================================================
//  各頁控制項
// ============================================================================
struct DefinePlaneDialog::Pages {
    // 0 偏移
    QComboBox* offBase = nullptr; QLineEdit* offExpr = nullptr;
    // 1 線端點＋切線
    GeomPicker* etGeom = nullptr;
    // 2 一點＋法向量
    PointEditor* pnPoint = nullptr; DirEditor* pnDir = nullptr;
    // 3 三點
    PointEditor *tp1 = nullptr, *tp2 = nullptr, *tp3 = nullptr;
    // 4 一點＋一線
    PointEditor* plPoint = nullptr; LineEditor* plLine = nullptr;
    // 5 兩線
    LineEditor *tlA = nullptr, *tlB = nullptr;
    // 6 繞軸旋轉
    LineEditor* anAxis = nullptr; QComboBox* anBase = nullptr; QLineEdit* anAngle = nullptr;
    // 7 中間／等分
    QComboBox *mdA = nullptr, *mdB = nullptr;
    // 8 過一點平行
    QComboBox* ptBase = nullptr; PointEditor* ptPoint = nullptr;
    // 9 線形里程橫斷面
    QComboBox* alTcl = nullptr; QLineEdit* alChain = nullptr;
};

DefinePlaneDialog::DefinePlaneDialog(cad::Document* doc, cad::DefinedPlane* existing,
                                     const QString& defaultBase, QWidget* parent)
    : QDialog(parent), m_doc(doc), m_existing(existing), m_p(new Pages)
{
    setWindowTitle(existing ? tr("編輯平面 — %1").arg(existing->name()) : tr("定義平面"));
    resize(560, 560);

    auto* lay = new QVBoxLayout(this);
    auto* hint = new QLabel(
        tr("平面 = 原點 + 法向量 + 面內 X 軸。選一種定義方式，再指定它的參照／參數。\n"
           "參照的草圖或線形改變時，平面會自動更新（已建立的草圖不會跟著移動）。"), this);
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* form = new QFormLayout;
    m_name = new QLineEdit(this);
    form->addRow(tr("名稱："), m_name);
    m_method = new QComboBox(this);
    const QStringList labels = {
        tr("從既有平面偏移"), tr("取線端點，切線當法向量"), tr("一點 + 法向量方向"), tr("三點"),
        tr("過一點 + 一線"), tr("兩線（共面）"), tr("繞軸線旋轉角度"), tr("兩平面的中間／等分平面"),
        tr("過一點且平行於平面"), tr("線形里程橫斷面（垂直於線形）") };
    for (int i = 0; i < labels.size(); ++i) m_method->addItem(labels[i], i);
    form->addRow(tr("定義方式："), m_method);
    lay->addLayout(form);

    m_stack = new QStackedWidget(this);
    lay->addWidget(m_stack, 1);
    buildPages();

    m_flip = new QCheckBox(tr("反向（翻轉法向量）"), this);
    lay->addWidget(m_flip);

    m_preview = new QLabel(this);
    m_preview->setWordWrap(true);
    lay->addWidget(m_preview);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    connect(m_method, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_stack->setCurrentIndex(i);
        updatePreview();
    });
    connect(m_flip, &QCheckBox::toggled, this, [this](bool) { updatePreview(); });

    if (existing) {
        m_name->setText(existing->name());
        loadDefinition(existing->definition());
    } else {
        PlaneDef d;
        d.method = PlaneMethod::Offset;
        d.baseRef = defaultBase.isEmpty() ? QStringLiteral("XY") : defaultBase;
        d.offsetExpr = QStringLiteral("-90");
        loadDefinition(d);
        int n = 1;
        if (doc)
            for (cad::Feature* f : doc->features())
                if (qobject_cast<cad::DefinedPlane*>(f)) ++n;
        m_name->setText(tr("Plane %1").arg(n));
    }
    updatePreview();
}

DefinePlaneDialog::~DefinePlaneDialog() { delete m_p; }

void DefinePlaneDialog::buildPages()
{
    auto notify = [this]() { updatePreview(); };
    Pages& P = *m_p;

    // 0 偏移
    m_stack->addWidget(wrapForm([&](QFormLayout* f, QWidget* w) {
        P.offBase = makeBaseCombo(m_doc, m_existing, w);
        P.offExpr = new QLineEdit(QStringLiteral("0"), w);
        P.offExpr->setToolTip(tr("數字或參數運算式；沿基準平面的法向量（+/-）。例：XY（法向量 +Z）、-90 → z = -90"));
        f->addRow(tr("基準平面："), P.offBase);
        f->addRow(tr("偏移量（沿法向量）："), P.offExpr);
        connect(P.offBase, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
        connect(P.offExpr, &QLineEdit::textChanged, this, [notify]() { notify(); });
    }, m_stack));

    // 1 線端點＋切線
    m_stack->addWidget(wrapForm([&](QFormLayout* f, QWidget* w) {
        P.etGeom = new GeomPicker(m_doc, kCurveGeom, kEndWhichLabels, kEndWhichValues, w);
        P.etGeom->changed = notify;
        f->addRow(tr("草圖／幾何／端點："), P.etGeom);
        f->addRow(new QLabel(tr("平面通過該端點，法向量 = 該端點的切線（線：起→終；圓弧：沿弧行進方向；多段線／樣條：端部線段方向）。"), w));
    }, m_stack));

    // 2 一點＋法向量
    m_stack->addWidget(wrapForm([&](QFormLayout* f, QWidget* w) {
        P.pnPoint = new PointEditor(m_doc, w); P.pnPoint->changed = notify;
        P.pnDir   = new DirEditor(m_doc, w);   P.pnDir->changed = notify;
        f->addRow(tr("通過的點："), P.pnPoint);
        f->addRow(tr("法向量方向："), P.pnDir);
    }, m_stack));

    // 3 三點
    m_stack->addWidget(wrapForm([&](QFormLayout* f, QWidget* w) {
        P.tp1 = new PointEditor(m_doc, w); P.tp2 = new PointEditor(m_doc, w); P.tp3 = new PointEditor(m_doc, w);
        P.tp1->changed = notify; P.tp2->changed = notify; P.tp3->changed = notify;
        f->addRow(tr("點 1（原點）："), P.tp1);
        f->addRow(tr("點 2（X 軸方向）："), P.tp2);
        f->addRow(tr("點 3："), P.tp3);
    }, m_stack));

    // 4 一點＋一線
    m_stack->addWidget(wrapForm([&](QFormLayout* f, QWidget* w) {
        P.plPoint = new PointEditor(m_doc, w); P.plLine = new LineEditor(m_doc, w);
        P.plPoint->changed = notify; P.plLine->changed = notify;
        f->addRow(tr("點："), P.plPoint);
        f->addRow(tr("線："), P.plLine);
    }, m_stack));

    // 5 兩線
    m_stack->addWidget(wrapForm([&](QFormLayout* f, QWidget* w) {
        P.tlA = new LineEditor(m_doc, w); P.tlB = new LineEditor(m_doc, w);
        P.tlA->changed = notify; P.tlB->changed = notify;
        f->addRow(tr("線 1："), P.tlA);
        f->addRow(tr("線 2："), P.tlB);
    }, m_stack));

    // 6 繞軸旋轉
    m_stack->addWidget(wrapForm([&](QFormLayout* f, QWidget* w) {
        P.anAxis = new LineEditor(m_doc, w); P.anAxis->changed = notify;
        P.anBase = makeBaseCombo(m_doc, m_existing, w);
        P.anAngle = new QLineEdit(QStringLiteral("90"), w);
        P.anAngle->setToolTip(tr("角度（度），可用參數運算式"));
        f->addRow(tr("旋轉軸（線）："), P.anAxis);
        f->addRow(tr("參考平面："), P.anBase);
        f->addRow(tr("角度 [度]："), P.anAngle);
        connect(P.anBase, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
        connect(P.anAngle, &QLineEdit::textChanged, this, [notify]() { notify(); });
    }, m_stack));

    // 7 中間／等分
    m_stack->addWidget(wrapForm([&](QFormLayout* f, QWidget* w) {
        P.mdA = makeBaseCombo(m_doc, m_existing, w); P.mdB = makeBaseCombo(m_doc, m_existing, w);
        P.mdB->setCurrentIndex(1);
        f->addRow(tr("平面 A："), P.mdA);
        f->addRow(tr("平面 B："), P.mdB);
        f->addRow(new QLabel(tr("兩平面平行 → 中間平面；不平行 → 等分平面（勾「反向」改取另一條等分平面）。"), w));
        connect(P.mdA, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
        connect(P.mdB, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
    }, m_stack));

    // 8 過一點平行
    m_stack->addWidget(wrapForm([&](QFormLayout* f, QWidget* w) {
        P.ptBase = makeBaseCombo(m_doc, m_existing, w);
        P.ptPoint = new PointEditor(m_doc, w); P.ptPoint->changed = notify;
        f->addRow(tr("平行於："), P.ptBase);
        f->addRow(tr("通過的點："), P.ptPoint);
        connect(P.ptBase, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
    }, m_stack));

    // 9 線形里程橫斷面
    m_stack->addWidget(wrapForm([&](QFormLayout* f, QWidget* w) {
        P.alTcl = makeTclCombo(m_doc, w);
        P.alChain = new QLineEdit(QStringLiteral("0"), w);
        f->addRow(tr("線路："), P.alTcl);
        f->addRow(tr("里程 [m]："), P.alChain);
        f->addRow(new QLabel(tr("平面通過線形該里程的 3D 點，法向量 = 線形的 3D 切線（垂直於線形的橫斷面）。"), w));
        connect(P.alTcl, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [notify](int) { notify(); });
        connect(P.alChain, &QLineEdit::textChanged, this, [notify]() { notify(); });
    }, m_stack));
}

QString DefinePlaneDialog::planeName() const { return m_name->text().trimmed(); }

PlaneDef DefinePlaneDialog::definition() const
{
    const Pages& P = *m_p;
    PlaneDef d;
    d.method = static_cast<PlaneMethod>(m_method->currentData().toInt());
    d.flip = m_flip->isChecked();

    switch (d.method) {
    case PlaneMethod::Offset:
        d.baseRef = P.offBase->currentData().toString();
        d.offsetExpr = P.offExpr->text().trimmed().isEmpty() ? QStringLiteral("0") : P.offExpr->text().trimmed();
        break;
    case PlaneMethod::EndpointTangent:
        d.p1.kind = PlanePointRef::GeomPoint;
        d.p1.sketchId = P.etGeom->sketchId(); d.p1.geomUuid = P.etGeom->geomUuid(); d.p1.which = P.etGeom->which();
        d.dir1.kind = PlaneDirRef::GeomTangent;
        d.dir1.sketchId = d.p1.sketchId; d.dir1.geomUuid = d.p1.geomUuid; d.dir1.which = d.p1.which;
        break;
    case PlaneMethod::PointNormal:
        d.p1 = P.pnPoint->value(); d.dir1 = P.pnDir->value();
        break;
    case PlaneMethod::ThreePoints:
        d.p1 = P.tp1->value(); d.p2 = P.tp2->value(); d.p3 = P.tp3->value();
        break;
    case PlaneMethod::PointLine:
        d.p1 = P.plPoint->value(); d.line1 = P.plLine->value();
        break;
    case PlaneMethod::TwoLines:
        d.line1 = P.tlA->value(); d.line2 = P.tlB->value();
        break;
    case PlaneMethod::AngleFromPlane:
        d.line1 = P.anAxis->value(); d.baseRef = P.anBase->currentData().toString(); d.angleExpr = P.anAngle->text().trimmed();
        break;
    case PlaneMethod::MidPlane:
        d.baseRef = P.mdA->currentData().toString(); d.baseRef2 = P.mdB->currentData().toString();
        break;
    case PlaneMethod::ParallelThroughPoint:
        d.baseRef = P.ptBase->currentData().toString(); d.p1 = P.ptPoint->value();
        break;
    case PlaneMethod::AlignmentSection:
        d.p1.kind = PlanePointRef::Alignment;
        d.p1.tclId = P.alTcl->currentData().toString(); d.p1.chainage = P.alChain->text().toDouble();
        d.dir1.kind = PlaneDirRef::AlignmentTangent;
        d.dir1.tclId = d.p1.tclId; d.dir1.chainage = d.p1.chainage;
        break;
    }
    return d;
}

void DefinePlaneDialog::loadDefinition(const PlaneDef& d)
{
    Pages& P = *m_p;
    const int mi = m_method->findData(int(d.method));
    if (mi >= 0) m_method->setCurrentIndex(mi);
    m_stack->setCurrentIndex(m_method->currentIndex());
    m_flip->setChecked(d.flip);

    auto selBase = [](QComboBox* cb, const QString& ref) { const int i = cb->findData(ref); if (i >= 0) cb->setCurrentIndex(i); };

    selBase(P.offBase, d.baseRef); P.offExpr->setText(d.offsetExpr);
    P.etGeom->set(d.p1.sketchId, d.p1.geomUuid, d.p1.which);
    P.pnPoint->setValue(d.p1); P.pnDir->setValue(d.dir1);
    P.tp1->setValue(d.p1); P.tp2->setValue(d.p2); P.tp3->setValue(d.p3);
    P.plPoint->setValue(d.p1); P.plLine->setValue(d.line1);
    P.tlA->setValue(d.line1); P.tlB->setValue(d.line2);
    P.anAxis->setValue(d.line1); selBase(P.anBase, d.baseRef); P.anAngle->setText(d.angleExpr);
    selBase(P.mdA, d.baseRef); selBase(P.mdB, d.baseRef2);
    selBase(P.ptBase, d.baseRef); P.ptPoint->setValue(d.p1);
    {
        const int ti = P.alTcl->findData(d.p1.tclId);
        if (ti >= 0) P.alTcl->setCurrentIndex(ti);
        P.alChain->setText(QString::number(d.p1.chainage, 'g', 12));
    }
}

void DefinePlaneDialog::updatePreview()
{
    if (!m_preview || !m_p || !m_p->offBase) return;      // 建構中
    cad::DefinedPlane::Frame f;
    QString err;
    if (!cad::DefinedPlane::evaluate(m_doc, definition(), f, m_existing, &err)) {
        m_preview->setText(tr("<span style='color:#c00'>%1</span>").arg(err));
        return;
    }
    m_preview->setText(tr("法向量 (%1, %2, %3)；通過 (%4, %5, %6)；X 軸 (%7, %8, %9)")
                           .arg(f.normal.x(), 0, 'f', 3).arg(f.normal.y(), 0, 'f', 3).arg(f.normal.z(), 0, 'f', 3)
                           .arg(f.origin.x(), 0, 'f', 3).arg(f.origin.y(), 0, 'f', 3).arg(f.origin.z(), 0, 'f', 3)
                           .arg(f.xAxis.x(),  0, 'f', 3).arg(f.xAxis.y(),  0, 'f', 3).arg(f.xAxis.z(),  0, 'f', 3));
}

void DefinePlaneDialog::accept()
{
    if (planeName().isEmpty()) {
        QMessageBox::warning(this, tr("定義平面"), tr("請輸入平面名稱"));
        return;
    }
    if (m_doc)
        for (cad::Feature* f : m_doc->features())
            if (f && f != m_existing && f->name() == planeName()) {
                QMessageBox::warning(this, tr("定義平面"), tr("已有名為「%1」的項目，請改用其他名稱").arg(planeName()));
                return;
            }
    cad::DefinedPlane::Frame f;
    QString err;
    if (!cad::DefinedPlane::evaluate(m_doc, definition(), f, m_existing, &err)) {
        QMessageBox::warning(this, tr("定義平面"), err);
        return;
    }
    QDialog::accept();
}

} // namespace ui
} // namespace aicad
