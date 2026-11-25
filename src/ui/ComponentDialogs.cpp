/**
 * @file ComponentDialogs.cpp
 */
#include "ComponentDialogs.h"

#include "cad/Document.h"
#include "railway/RailwayAlignment.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace aicad {
namespace ui {

// ============================================================================
//  ComponentRoleDialog
// ============================================================================
namespace {
// 名稱含這些字 → 預設角色為「所需空間」
bool looksLikeSpace(const QString& name)
{
    const QString n = name.toLower();
    static const QStringList kw = { "space", "keepout", "keep-out", "clear", "envelope", "zone",
                                    "空間", "淨空", "限界", "淨距" };
    for (const QString& k : kw) if (n.contains(k)) return true;
    return false;
}
} // namespace

ComponentRoleDialog::ComponentRoleDialog(const QVector<Candidate>& candidates, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("插入 3D 組件 — 指定實體角色"));
    resize(560, 360);

    auto* lay = new QVBoxLayout(this);
    auto* hint = new QLabel(
        tr("請指定來源檔中每個實體的角色：\n"
           "  • 組件：安裝後顯示的 3D 組件本體\n"
           "  • 所需空間：組件需要的空間，道版 Loft 會在此處開孔\n"
           "  • 略過：不使用\n"
           "組件座標系：原點在軌道面中心；+X 與 cant 直線貼合（朝右手邊鋼軌）；\n"
           "+Y 與草圖 -Z 貼合（= 沿線、里程增加方向）；+Z 垂直軌道面朝上。"), this);
    hint->setWordWrap(true);
    lay->addWidget(hint);

    m_table = new QTableWidget(candidates.size(), 3, this);
    m_table->setHorizontalHeaderLabels({ tr("名稱"), tr("類型"), tr("角色") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    for (int r = 0; r < candidates.size(); ++r) {
        auto* n = new QTableWidgetItem(candidates[r].name);
        n->setFlags(n->flags() & ~Qt::ItemIsEditable);
        auto* t = new QTableWidgetItem(candidates[r].typeName);
        t->setFlags(t->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(r, 0, n);
        m_table->setItem(r, 1, t);

        auto* cb = new QComboBox(m_table);
        cb->addItems({ tr("組件"), tr("所需空間"), tr("略過") });
        cb->setCurrentIndex(looksLikeSpace(candidates[r].name) ? RoleSpace : RoleComponent);
        m_table->setCellWidget(r, 2, cb);
    }
    m_table->resizeColumnsToContents();
    lay->addWidget(m_table, 1);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

QVector<ComponentRoleDialog::Role> ComponentRoleDialog::roles() const
{
    QVector<Role> out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto* cb = qobject_cast<QComboBox*>(m_table->cellWidget(r, 2));
        out.append(cb ? static_cast<Role>(cb->currentIndex()) : RoleIgnore);
    }
    return out;
}

// ============================================================================
//  ComponentPlacementDialog
// ============================================================================
namespace {
enum PCol { PColTcl = 0, PColChainage, PColLateral, PColFlip, PColInstall, PColVoid, PColCount };

/// 置中的勾選框儲存格
QWidget* makeCheckCell(QTableWidget* table, bool checked)
{
    auto* chk = new QCheckBox(table);
    chk->setChecked(checked);
    auto* holder = new QWidget(table);
    auto* hl = new QHBoxLayout(holder);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setAlignment(Qt::AlignCenter);
    hl->addWidget(chk);
    return holder;
}

bool checkCellValue(QTableWidget* table, int row, int col, bool fallback)
{
    if (auto* h = table->cellWidget(row, col))
        if (auto* chk = h->findChild<QCheckBox*>()) return chk->isChecked();
    return fallback;
}
}

ComponentPlacementDialog::ComponentPlacementDialog(
        cad::Document* doc, const QVector<cad::ComponentInstall::Placement>& initial, QWidget* parent)
    : QDialog(parent), m_doc(doc)
{
    setWindowTitle(tr("3D 組件安裝位置"));
    resize(560, 380);

    auto* lay = new QVBoxLayout(this);
    auto* hint = new QLabel(
        tr("每一列是一個安裝位置。組件會依該里程的超高（cant）定位到軌道面上，"
           "道版 Loft 在「所需空間」處開孔。\n"
           "里程單位 m（與斷面陣列的里程相同）；橫向偏移沿軌道面，+ 為右手邊。"), this);
    hint->setWordWrap(true);
    lay->addWidget(hint);

    m_table = new QTableWidget(0, PColCount, this);
    m_table->setHorizontalHeaderLabels({ tr("線路"), tr("里程 [m]"), tr("橫向偏移 [m]"), tr("旋轉 180°"), tr("安裝組件\n(Part+Assembly)"), tr("開孔\n(Void)") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    lay->addWidget(m_table, 1);

    auto* row = new QHBoxLayout;
    auto* bAdd = new QPushButton(tr("新增位置"), this);
    auto* bDel = new QPushButton(tr("刪除選取列"), this);
    row->addWidget(bAdd);
    row->addWidget(bDel);
    row->addStretch(1);
    lay->addLayout(row);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    lay->addWidget(bb);

    connect(bAdd, &QPushButton::clicked, this, [this]() {
        cad::ComponentInstall::Placement p;
        // 新列沿用上一列的線路與橫向偏移，里程留給使用者填
        const int last = m_table->rowCount() - 1;
        if (last >= 0) {
            if (auto* cb = qobject_cast<QComboBox*>(m_table->cellWidget(last, PColTcl)))
                p.tclId = cb->currentData().toString();
            if (auto* it = m_table->item(last, PColLateral)) p.lateral = it->text().toDouble();
        }
        addRow(p);
    });
    connect(bDel, &QPushButton::clicked, this, [this]() {
        const auto sel = m_table->selectionModel()->selectedRows();
        QList<int> rows;
        for (const QModelIndex& i : sel) rows << i.row();
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int r : rows) m_table->removeRow(r);
    });
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    for (const auto& p : initial) addRow(p);
    if (initial.isEmpty()) addRow(cad::ComponentInstall::Placement());
}

void ComponentPlacementDialog::addRow(const cad::ComponentInstall::Placement& p)
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);

    auto* cb = new QComboBox(m_table);
    if (m_doc) {
        for (railway::TrackCenterLine* tcl : m_doc->trackCenterLines())
            cb->addItem(tcl->name().isEmpty() ? tcl->id() : tcl->name(), tcl->id());
    }
    int idx = cb->findData(p.tclId);
    if (idx < 0 && cb->count() > 0) idx = 0;
    if (idx >= 0) cb->setCurrentIndex(idx);
    m_table->setCellWidget(r, PColTcl, cb);

    m_table->setItem(r, PColChainage, new QTableWidgetItem(QString::number(p.chainage, 'f', 3)));
    m_table->setItem(r, PColLateral,  new QTableWidgetItem(QString::number(p.lateral,  'f', 3)));

    auto* chk = new QCheckBox(m_table);
    chk->setChecked(p.flip180);
    auto* holder = new QWidget(m_table);
    auto* hl = new QHBoxLayout(holder);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setAlignment(Qt::AlignCenter);
    hl->addWidget(chk);
    m_table->setCellWidget(r, PColFlip, holder);
    m_table->setCellWidget(r, PColInstall, makeCheckCell(m_table, p.installAssembly));
    m_table->setCellWidget(r, PColVoid,    makeCheckCell(m_table, p.cutVoid));
}

void ComponentPlacementDialog::accept()
{
    QVector<cad::ComponentInstall::Placement> out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        cad::ComponentInstall::Placement p;
        if (auto* cb = qobject_cast<QComboBox*>(m_table->cellWidget(r, PColTcl)))
            p.tclId = cb->currentData().toString();

        bool ok1 = false, ok2 = false;
        p.chainage = m_table->item(r, PColChainage) ? m_table->item(r, PColChainage)->text().trimmed().toDouble(&ok1) : 0.0;
        p.lateral  = m_table->item(r, PColLateral)  ? m_table->item(r, PColLateral)->text().trimmed().toDouble(&ok2)  : 0.0;
        if (!ok1 || !ok2) {
            QMessageBox::warning(this, tr("安裝位置"), tr("第 %1 列的里程或橫向偏移不是數字").arg(r + 1));
            return;
        }
        if (auto* holder = m_table->cellWidget(r, PColFlip))
            if (auto* chk = holder->findChild<QCheckBox*>()) p.flip180 = chk->isChecked();
        p.installAssembly = checkCellValue(m_table, r, PColInstall, true);
        p.cutVoid         = checkCellValue(m_table, r, PColVoid, true);
        out.append(p);
    }
    if (out.isEmpty()) {
        QMessageBox::warning(this, tr("安裝位置"), tr("至少需要一個安裝位置"));
        return;
    }
    m_result = out;
    QDialog::accept();
}

// ============================================================================
//  ComponentLayoutDialog
// ============================================================================
namespace {
enum LCol { LColComponent = 0, LColTcl, LColChainage, LColLateral, LColFlip, LColInstall, LColVoid, LColCount };
}

ComponentLayoutDialog::ComponentLayoutDialog(cad::Document* doc, QWidget* parent)
    : QDialog(parent), m_doc(doc)
{
    setWindowTitle(tr("組件安裝位置"));
    resize(680, 420);

    auto* lay = new QVBoxLayout(this);
    auto* hint = new QLabel(
        tr("加入或編輯各式組件沿線形的安裝位置。每一列：選擇組件、線路、里程、橫向偏移。\n"
           "組件會依該里程的超高（依組件與斷面草圖的約束關係）定位。\n「安裝組件」＝在此里程放置來源檔 Part＋Assembly 的實體；「開孔」＝用來源檔 Void 對道版 Loft 開孔。兩者可分開使用。"), this);
    hint->setWordWrap(true);
    lay->addWidget(hint);

    m_table = new QTableWidget(0, LColCount, this);
    m_table->setHorizontalHeaderLabels({ tr("組件"), tr("線路"), tr("里程 [m]"), tr("橫向偏移 [m]"), tr("旋轉 180°"), tr("安裝組件\n(Part+Assembly)"), tr("開孔\n(Void)") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    lay->addWidget(m_table, 1);

    auto* row = new QHBoxLayout;
    auto* bAdd = new QPushButton(tr("新增位置"), this);
    auto* bDup = new QPushButton(tr("複製選取列"), this);
    auto* bDel = new QPushButton(tr("刪除選取列"), this);
    row->addWidget(bAdd); row->addWidget(bDup); row->addWidget(bDel);
    row->addStretch(1);
    lay->addLayout(row);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    lay->addWidget(bb);

    // 現有的所有安裝位置
    if (doc) {
        for (cad::Feature* f : doc->features())
            if (auto* ci = qobject_cast<cad::ComponentInstall*>(f))
                for (const auto& p : ci->placements()) addRow(ci->id(), p);
    }

    auto rowComponent = [this](int r) -> QString {
        auto* cb = qobject_cast<QComboBox*>(m_table->cellWidget(r, LColComponent));
        return cb ? cb->currentData().toString() : QString();
    };
    auto rowPlacement = [this](int r) {
        cad::ComponentInstall::Placement p;
        if (auto* cb = qobject_cast<QComboBox*>(m_table->cellWidget(r, LColTcl))) p.tclId = cb->currentData().toString();
        if (auto* it = m_table->item(r, LColChainage)) p.chainage = it->text().toDouble();
        if (auto* it = m_table->item(r, LColLateral))  p.lateral  = it->text().toDouble();
        if (auto* h = m_table->cellWidget(r, LColFlip))
            if (auto* chk = h->findChild<QCheckBox*>()) p.flip180 = chk->isChecked();
        p.installAssembly = checkCellValue(m_table, r, LColInstall, true);
        p.cutVoid         = checkCellValue(m_table, r, LColVoid, true);
        return p;
    };

    connect(bAdd, &QPushButton::clicked, this, [this, rowComponent, rowPlacement]() {
        const int last = m_table->rowCount() - 1;
        QString comp;
        cad::ComponentInstall::Placement p;
        if (last >= 0) { comp = rowComponent(last); p = rowPlacement(last); }
        addRow(comp, p);
    });
    connect(bDup, &QPushButton::clicked, this, [this, rowComponent, rowPlacement]() {
        const auto sel = m_table->selectionModel()->selectedRows();
        QList<int> rows;
        for (const QModelIndex& i : sel) rows << i.row();
        std::sort(rows.begin(), rows.end());
        for (int r : rows) addRow(rowComponent(r), rowPlacement(r));
    });
    connect(bDel, &QPushButton::clicked, this, [this]() {
        const auto sel = m_table->selectionModel()->selectedRows();
        QList<int> rows;
        for (const QModelIndex& i : sel) rows << i.row();
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int r : rows) m_table->removeRow(r);
    });
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (m_table->rowCount() == 0) addRow(QString(), cad::ComponentInstall::Placement());
}

void ComponentLayoutDialog::addRow(const QString& componentId, const cad::ComponentInstall::Placement& p)
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);

    auto* comp = new QComboBox(m_table);
    if (m_doc) {
        for (cad::Feature* f : m_doc->features())
            if (auto* ci = qobject_cast<cad::ComponentInstall*>(f)) comp->addItem(ci->name(), ci->id());
    }
    int ci = comp->findData(componentId);
    if (ci < 0 && comp->count() > 0) ci = 0;
    if (ci >= 0) comp->setCurrentIndex(ci);
    m_table->setCellWidget(r, LColComponent, comp);

    auto* tcl = new QComboBox(m_table);
    if (m_doc)
        for (railway::TrackCenterLine* t : m_doc->trackCenterLines())
            tcl->addItem(t->name().isEmpty() ? t->id() : t->name(), t->id());
    int ti = tcl->findData(p.tclId);
    if (ti < 0 && tcl->count() > 0) ti = 0;
    if (ti >= 0) tcl->setCurrentIndex(ti);
    m_table->setCellWidget(r, LColTcl, tcl);

    m_table->setItem(r, LColChainage, new QTableWidgetItem(QString::number(p.chainage, 'f', 3)));
    m_table->setItem(r, LColLateral,  new QTableWidgetItem(QString::number(p.lateral,  'f', 3)));

    auto* chk = new QCheckBox(m_table);
    chk->setChecked(p.flip180);
    auto* holder = new QWidget(m_table);
    auto* hl = new QHBoxLayout(holder);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setAlignment(Qt::AlignCenter);
    hl->addWidget(chk);
    m_table->setCellWidget(r, LColFlip, holder);
    m_table->setCellWidget(r, LColInstall, makeCheckCell(m_table, p.installAssembly));
    m_table->setCellWidget(r, LColVoid,    makeCheckCell(m_table, p.cutVoid));
}

void ComponentLayoutDialog::accept()
{
    QHash<QString, QVector<cad::ComponentInstall::Placement>> out;

    // 每個組件至少要有一筆記錄（沒有任何列的組件 = 清空其安裝位置）
    if (m_doc)
        for (cad::Feature* f : m_doc->features())
            if (auto* ci = qobject_cast<cad::ComponentInstall*>(f)) out[ci->id()] = {};

    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto* cc = qobject_cast<QComboBox*>(m_table->cellWidget(r, LColComponent));
        const QString comp = cc ? cc->currentData().toString() : QString();
        if (comp.isEmpty()) {
            QMessageBox::warning(this, tr("組件安裝位置"), tr("第 %1 列沒有選擇組件").arg(r + 1));
            return;
        }
        cad::ComponentInstall::Placement p;
        if (auto* cb = qobject_cast<QComboBox*>(m_table->cellWidget(r, LColTcl))) p.tclId = cb->currentData().toString();

        bool ok1 = false, ok2 = false;
        p.chainage = m_table->item(r, LColChainage) ? m_table->item(r, LColChainage)->text().trimmed().toDouble(&ok1) : 0.0;
        p.lateral  = m_table->item(r, LColLateral)  ? m_table->item(r, LColLateral)->text().trimmed().toDouble(&ok2)  : 0.0;
        if (!ok1 || !ok2) {
            QMessageBox::warning(this, tr("組件安裝位置"), tr("第 %1 列的里程或橫向偏移不是數字").arg(r + 1));
            return;
        }
        if (auto* holder = m_table->cellWidget(r, LColFlip))
            if (auto* chk = holder->findChild<QCheckBox*>()) p.flip180 = chk->isChecked();
        p.installAssembly = checkCellValue(m_table, r, LColInstall, true);
        p.cutVoid         = checkCellValue(m_table, r, LColVoid, true);
        out[comp].append(p);
    }
    m_result = out;
    QDialog::accept();
}

} // namespace ui
} // namespace aicad
