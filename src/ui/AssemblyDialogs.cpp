/**
 * @file AssemblyDialogs.cpp
 */
#include "AssemblyDialogs.h"

#include "cad/AssemblyPart.h"
#include "cad/AssemblyUtil.h"
#include "cad/Document.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace aicad {
namespace ui {

using cad::AssemblyMate;
using cad::AssemblyPart;
using cad::MateRefData;
using cad::RefSource;
using namespace cad::asmsolve;

namespace {

QDoubleSpinBox* makeSpin(double lo, double hi, int decimals, double value, const QString& suffix = QString())
{
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setDecimals(decimals);
    s->setValue(value);
    s->setKeyboardTracking(false);
    if (!suffix.isEmpty()) s->setSuffix(suffix);
    return s;
}

QString kindText(RefKind k)
{
    return k == RefKind::Point ? QObject::tr("點") : k == RefKind::Axis ? QObject::tr("軸線") : QObject::tr("平面");
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// AssemblyPartDialog
// ─────────────────────────────────────────────────────────────────────────────
AssemblyPartDialog::AssemblyPartDialog(const AssemblyPartParams& init, bool insertMode, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(insertMode ? tr("插入零件") : tr("編輯零件"));
    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    m_name = new QLineEdit(init.name);
    form->addRow(tr("名稱"), m_name);

    m_scale = makeSpin(1e-6, 1e6, 6, init.scale);
    auto* presets = new QComboBox;
    presets->addItem(tr("自訂…"), 0.0);
    presets->addItem(tr("1　（零件與本文件同單位）"), 1.0);
    presets->addItem(tr("0.001　（mm → m）"), 0.001);
    presets->addItem(tr("1000　（m → mm）"), 1000.0);
    presets->addItem(tr("0.0254　（inch → m）"), 0.0254);
    presets->addItem(tr("25.4　（inch → mm）"), 25.4);
    for (int i = 1; i < presets->count(); ++i)
        if (std::abs(presets->itemData(i).toDouble() - init.scale) < 1e-12) presets->setCurrentIndex(i);
    connect(presets, QOverload<int>::of(&QComboBox::activated), this, [this, presets](int i) {
        const double v = presets->itemData(i).toDouble();
        if (v > 0.0) m_scale->setValue(v);
    });
    auto* scaleRow = new QHBoxLayout;
    scaleRow->addWidget(m_scale);
    scaleRow->addWidget(presets, 1);
    form->addRow(tr("單位縮放"), scaleRow);

    auto* pos = new QHBoxLayout;
    m_x = makeSpin(-1e9, 1e9, 4, init.x, QStringLiteral("  X"));
    m_y = makeSpin(-1e9, 1e9, 4, init.y, QStringLiteral("  Y"));
    m_z = makeSpin(-1e9, 1e9, 4, init.z, QStringLiteral("  Z"));
    pos->addWidget(m_x); pos->addWidget(m_y); pos->addWidget(m_z);
    form->addRow(tr("位置（組合單位）"), pos);

    auto* rot = new QHBoxLayout;
    m_rx = makeSpin(-3600, 3600, 4, init.rx, QStringLiteral("° X"));
    m_ry = makeSpin(-3600, 3600, 4, init.ry, QStringLiteral("° Y"));
    m_rz = makeSpin(-3600, 3600, 4, init.rz, QStringLiteral("° Z"));
    rot->addWidget(m_rx); rot->addWidget(m_ry); rot->addWidget(m_rz);
    form->addRow(tr("轉角（先 X、再 Y、最後 Z）"), rot);

    m_fixed = new QCheckBox(tr("固定（接地）：配合時不移動此零件"));
    m_fixed->setChecked(init.fixed);
    form->addRow(QString(), m_fixed);
    m_autoSync = new QCheckBox(tr("來源檔存檔時自動同步"));
    m_autoSync->setChecked(init.autoSync);
    form->addRow(QString(), m_autoSync);
    lay->addLayout(form);

    auto* hint = new QLabel(tr("位置與轉角是「初始位姿」。加上配合後，求解器會讓此零件做最小的移動來滿足配合。"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

AssemblyPartParams AssemblyPartDialog::params() const
{
    AssemblyPartParams p;
    p.name = m_name->text().trimmed();
    p.scale = m_scale->value();
    p.x = m_x->value(); p.y = m_y->value(); p.z = m_z->value();
    p.rx = m_rx->value(); p.ry = m_ry->value(); p.rz = m_rz->value();
    p.fixed = m_fixed->isChecked();
    p.autoSync = m_autoSync->isChecked();
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// MateDialog
// ─────────────────────────────────────────────────────────────────────────────
MateDialog::MateDialog(cad::Document* doc, const AssemblyMate::Data* initial, const QString& presetPartA,
                       const QString& defaultName, PickFn pick, QWidget* parent)
    : QDialog(parent), m_doc(doc), m_pick(std::move(pick))
{
    setWindowTitle(initial ? tr("編輯配合") : tr("新增配合"));
    setMinimumWidth(560);
    m_building = true;

    auto* lay = new QVBoxLayout(this);

    // 類型
    auto* top = new QFormLayout;
    m_type = new QComboBox;
    const MateType types[] = { MateType::Coincident, MateType::Collinear, MateType::Coplanar, MateType::Parallel,
                               MateType::Perpendicular, MateType::Distance, MateType::Angle };
    const char* tips[] = {
        "點與點、點與軸線、點與平面重合", "兩條軸線在同一直線上（同軸）", "兩平面在同一平面，或軸線落在平面內",
        "軸線／平面彼此平行", "軸線／平面彼此垂直", "點、軸線、平面之間保持指定距離", "軸線／平面之間保持指定夾角" };
    for (int i = 0; i < 7; ++i) {
        m_type->addItem(AssemblyMate::typeLabel(types[i]), static_cast<int>(types[i]));
        m_type->setItemData(i, tr(tips[i]), Qt::ToolTipRole);
    }
    top->addRow(tr("配合類型"), m_type);
    m_name = new QLineEdit(defaultName);
    top->addRow(tr("名稱"), m_name);
    lay->addLayout(top);

    // 兩個參照
    for (int i = 0; i < 2; ++i) {
        auto* box = new QGroupBox(i == 0 ? tr("參照 1") : tr("參照 2"));
        buildSlot(i, box);
        lay->addWidget(box);
    }

    // 取向與數值
    auto* opt = new QFormLayout;
    m_orient = new QComboBox;
    m_orient->addItem(tr("同向（法向量／軸向相同，例如兩面朝同一邊）"), static_cast<int>(Orient::Aligned));
    m_orient->addItem(tr("反向（相反，例如兩面面對面貼合）"), static_cast<int>(Orient::Opposed));
    m_orient->addItem(tr("不限（只要求平行，不管正反）"), static_cast<int>(Orient::Any));
    opt->addRow(tr("取向"), m_orient);

    auto* valRow = new QHBoxLayout;
    m_value = new QLineEdit(QStringLiteral("0"));
    m_value->setPlaceholderText(tr("數字或參數運算式"));
    m_valueUnit = new QLabel;
    auto* measure = new QPushButton(tr("取目前值"));
    valRow->addWidget(m_value, 1);
    valRow->addWidget(m_valueUnit);
    valRow->addWidget(measure);
    opt->addRow(tr("數值"), valRow);
    lay->addLayout(opt);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(m_buttons);

    // 初始值
    if (initial) {
        m_type->setCurrentIndex(std::max(0, m_type->findData(static_cast<int>(initial->type))));
        for (int i = 0; i < 2; ++i) {
            const MateRefData& r = i == 0 ? initial->a : initial->b;
            const int pi = m_slot[i].part->findData(r.ownerId);
            if (pi >= 0) m_slot[i].part->setCurrentIndex(pi);
            populateRefs(i, &r, &r);
        }
        m_orient->setCurrentIndex(std::max(0, m_orient->findData(static_cast<int>(initial->orient))));
        m_value->setText(initial->valueExpr);
        m_valueTouched = m_orientTouched = true;
    } else {
        if (!presetPartA.isEmpty()) {
            const int pi = m_slot[0].part->findData(presetPartA);
            if (pi >= 0) m_slot[0].part->setCurrentIndex(pi);
        }
        // 參照 2 預設選「世界」以外的另一個零件（如果有）
        if (m_slot[1].part->count() > 2) {
            for (int k = 1; k < m_slot[1].part->count(); ++k)
                if (m_slot[1].part->itemData(k).toString() != m_slot[0].part->currentData().toString()) {
                    m_slot[1].part->setCurrentIndex(k);
                    break;
                }
        }
        populateRefs(0, nullptr, nullptr);
        populateRefs(1, nullptr, nullptr);
        m_orient->setCurrentIndex(m_orient->findData(static_cast<int>(Orient::Opposed)));
    }

    // 連線
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { autoFill(); refreshState(); });
    for (int i = 0; i < 2; ++i) {
        connect(m_slot[i].part, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i](int) {
            if (m_building) return;
            populateRefs(i, nullptr, nullptr);
            autoFill();
            refreshState();
        });
        connect(m_slot[i].ref, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            if (m_building) return;
            autoFill();
            refreshState();
        });
        connect(m_slot[i].pick, &QPushButton::clicked, this, [this, i]() { onPick(i); });
    }
    connect(m_orient, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        m_orientTouched = true;
        if (!m_valueTouched) autoFill();
        refreshState();
    });
    connect(m_value, &QLineEdit::textEdited, this, [this](const QString&) { m_valueTouched = true; refreshState(); });
    connect(measure, &QPushButton::clicked, this, [this]() { m_valueTouched = false; autoFill(); refreshState(); });

    m_building = false;
    autoFill();
    refreshState();
}

void MateDialog::buildSlot(int i, QGroupBox* box)
{
    Slot& s = m_slot[i];
    auto* g = new QFormLayout(box);

    s.part = new QComboBox;
    s.part->addItem(tr("世界（座標軸／座標平面）"), QString());
    if (m_doc)
        for (AssemblyPart* p : m_doc->assemblyParts()) s.part->addItem(p->name(), p->id());
    g->addRow(tr("所屬"), s.part);

    auto* row = new QHBoxLayout;
    s.ref = new QComboBox;
    s.ref->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    s.ref->setMinimumContentsLength(28);
    s.pick = new QPushButton(tr("在 3D 視圖點選…"));
    s.pick->setEnabled(static_cast<bool>(m_pick));
    s.pick->setToolTip(tr("暫時隱藏對話框，直接點選零件上的面、邊或頂點"));
    row->addWidget(s.ref, 1);
    row->addWidget(s.pick);
    g->addRow(tr("幾何"), row);

    s.info = new QLabel;
    g->addRow(QString(), s.info);
}

void MateDialog::populateRefs(int i, const MateRefData* select, const MateRefData* extra)
{
    Slot& s = m_slot[i];
    const QString owner = s.part->currentData().toString();
    const bool wasBuilding = m_building;
    m_building = true;

    s.cands.clear();
    for (const MateRefData& d : cad::assemblygeom::allDatumRefs(owner)) {
        MateRefData r = d;
        r.label = tr("基準 · %1").arg(d.label);
        s.cands.append(r);
    }
    if (!owner.isEmpty() && m_doc) {
        if (const auto* part = qobject_cast<AssemblyPart*>(m_doc->findFeature(owner)))
            s.cands += cad::assemblygeom::enumerate(part->localShape(), 300);
        for (MateRefData& r : s.cands) r.ownerId = owner;
    }
    if (extra) {                                    // 編輯既有配合／剛選到的 3D 幾何：放在最前面
        bool dup = false;
        for (const MateRefData& r : s.cands)
            if (r.source == extra->source && r.topoIndex == extra->topoIndex && r.datum == extra->datum
                && extra->source != RefSource::Datum) { dup = true; break; }
        if (!dup && extra->source != RefSource::Datum) {
            MateRefData e = *extra;
            e.label = tr("（目前）%1").arg(extra->label);
            s.cands.prepend(e);
        }
    }

    s.ref->clear();
    int sel = 0;
    for (int k = 0; k < s.cands.size(); ++k) {
        s.ref->addItem(s.cands[k].label);
        if (select && s.cands[k].source == select->source && s.cands[k].datum == select->datum
            && (select->source == RefSource::Datum || s.cands[k].topoIndex == select->topoIndex)
            && (select->source != RefSource::Datum || s.cands[k].datum == select->datum)) sel = k;
        if (select && extra && select->source != RefSource::Datum && s.cands[k].label.startsWith(tr("（目前）"))) sel = k;
    }
    s.ref->setCurrentIndex(sel);
    m_building = wasBuilding;
}

MateRefData MateDialog::currentRef(int i) const
{
    const Slot& s = m_slot[i];
    const int k = s.ref->currentIndex();
    MateRefData r = (k >= 0 && k < s.cands.size()) ? s.cands[k] : MateRefData();
    r.ownerId = s.part->currentData().toString();
    // 去掉顯示用前綴，保留乾淨的標籤
    if (r.label.startsWith(tr("（目前）"))) r.label = r.label.mid(tr("（目前）").size());
    return r;
}

void MateDialog::onPick(int i)
{
    if (!m_pick) return;
    MateRefData r;
    QString err;
    hide();
    const bool ok = m_pick(r, &err);
    show();
    raise();
    activateWindow();
    if (!ok) {
        if (!err.isEmpty()) QMessageBox::information(this, tr("點選幾何"), err);
        return;
    }
    m_building = true;
    const int pi = m_slot[i].part->findData(r.ownerId);
    if (pi >= 0) m_slot[i].part->setCurrentIndex(pi);
    populateRefs(i, &r, &r);
    m_building = false;
    autoFill();
    refreshState();
}

AssemblyMate::Data MateDialog::data() const
{
    AssemblyMate::Data d;
    d.type = static_cast<MateType>(m_type->currentData().toInt());
    d.a = currentRef(0);
    d.b = currentRef(1);
    d.orient = static_cast<Orient>(m_orient->currentData().toInt());
    d.valueExpr = m_value->text().trimmed().isEmpty() ? QStringLiteral("0") : m_value->text().trimmed();
    return d;
}

QString MateDialog::mateName() const { return m_name->text().trimmed(); }

void MateDialog::autoFill()
{
    if (m_building || !m_doc) return;
    AssemblyMate::Data d = data();
    if (!mateUsesOrient(d.type, d.a.kind, d.b.kind) && !mateUsesValue(d.type, d.a.kind, d.b.kind)) return;

    double v = 0.0;
    Orient sugg = Orient::Any;
    cad::assemblyutil::measure(m_doc, d, v, sugg);          // 即使沒有數值也會回報建議取向

    if (!m_orientTouched && mateUsesOrient(d.type, d.a.kind, d.b.kind) && sugg != Orient::Any) {
        const int k = m_orient->findData(static_cast<int>(sugg));
        if (k >= 0) { const QSignalBlocker b(m_orient); m_orient->setCurrentIndex(k); }
        d.orient = sugg;
    }
    if (!m_valueTouched && mateUsesValue(d.type, d.a.kind, d.b.kind)) {
        double v2 = 0.0; Orient o2 = Orient::Any;
        if (cad::assemblyutil::measure(m_doc, d, v2, o2)) {
            const QSignalBlocker b(m_value);
            m_value->setText(QString::number(v2, 'g', 10));
        }
    }
}

void MateDialog::refreshState()
{
    if (m_building) return;
    const AssemblyMate::Data d = data();

    for (int i = 0; i < 2; ++i) {
        const MateRefData r = i == 0 ? d.a : d.b;
        const QString dir = r.kind == RefKind::Point ? QString()
            : tr("　方向 (%1, %2, %3)").arg(r.d[0], 0, 'f', 3).arg(r.d[1], 0, 'f', 3).arg(r.d[2], 0, 'f', 3);
        m_slot[i].info->setText(tr("%1%2").arg(kindText(r.kind), dir));
    }

    const bool usesOrient = mateUsesOrient(d.type, d.a.kind, d.b.kind);
    const bool usesValue  = mateUsesValue(d.type, d.a.kind, d.b.kind);
    m_orient->setEnabled(usesOrient);
    m_value->setEnabled(usesValue);
    m_valueUnit->setText(!usesValue ? QString()
                         : d.type == MateType::Angle ? tr("度") : tr("長度（組合單位）"));

    QString msg;
    bool ok = true;
    if (!isValidPair(d.type, d.a.kind, d.b.kind)) {
        msg = tr("「%1」不能用在 %2 與 %3。").arg(AssemblyMate::typeLabel(d.type), kindText(d.a.kind), kindText(d.b.kind));
        ok = false;
    } else if (d.a.ownerId == d.b.ownerId) {
        msg = d.a.isWorld() ? tr("兩個參照都是世界，沒有可移動的零件。") : tr("兩個參照屬於同一個零件，配合不會有作用。");
        ok = false;
    } else if (usesValue) {
        double v = 0.0;
        if (!AssemblyMate::evalExpr(m_doc, d.valueExpr, v)) { msg = tr("數值運算式無法求值。"); ok = false; }
        else if (d.type == MateType::Angle && (v < -180.0 || v > 180.0)) { msg = tr("夾角需在 -180° ～ 180° 之間。"); ok = false; }
    }
    if (ok) {
        if (d.type == MateType::Distance && usesValue)
            msg = tr("距離為有號值：點／軸線到平面、平面到平面沿「平面（參照 1）的法向量」量測；負值表示在法向量的另一側。");
        else if (d.type == MateType::Angle && usesValue)
            msg = tr("夾角：軸線／平面法向量之間的夾角（軸線與平面為軸線與平面的夾角）。選「反向」會以反向量計算。");
        else if (usesOrient)
            msg = tr("面的法向量一律朝實體外側：兩面面對面貼合請選「反向」，同方向對齊請選「同向」。");
    }
    m_status->setStyleSheet(ok ? QString() : QStringLiteral("color:#c0392b;"));
    m_status->setText(msg);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
}

} // namespace ui
} // namespace aicad
