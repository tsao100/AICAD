/**
 * @file PlanProfileDialog.cpp
 */
#include "PlanProfileDialog.h"

#include "PlanProfileGenerator.h"
#include <QUuid>
#include "PlanProfilePainter.h"
#include "cad/Document.h"
#include "railway/RailwayAlignment.h"

#include <QTabWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <algorithm>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include "core/geometry/ProjectOrigin.h"
#include <QSignalBlocker>
#include <QSettings>
#include <QSet>
#include "DrawingSheetManager.h"
#include "DrawingSheet.h"

namespace aicad {
namespace drawing {

namespace {

QDoubleSpinBox* dsb(double lo, double hi, double step, int dec, const QString& suffix = {})
{
    auto* w = new QDoubleSpinBox;
    w->setRange(lo, hi);
    w->setSingleStep(step);
    w->setDecimals(dec);
    if (!suffix.isEmpty()) w->setSuffix(suffix);
    return w;
}

QWidget* page(QFormLayout*& form)
{
    auto* w = new QWidget;
    form = new QFormLayout(w);
    return w;
}

/** 欄位清單：依 ids 順序先列已勾選者，其餘（未勾選）接在後面。 */
void fillColPicker(QListWidget* list, int kind, const QStringList& ids)
{
    QSignalBlocker b(list);
    list->clear();
    const QVector<PlanProfileTableColumn> defs = planProfileTableColumnDefs(kind);
    auto add = [&](const PlanProfileTableColumn& d, bool on) {
        QString lab = d.label;
        lab.replace(QLatin1Char('\n'), QLatin1Char(' '));
        auto* it = new QListWidgetItem(lab);
        it->setData(Qt::UserRole, d.id);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        list->addItem(it);
    };
    for (const QString& id : ids)
        for (const auto& d : defs) if (d.id == id) add(d, true);
    for (const auto& d : defs) if (!ids.contains(d.id)) add(d, false);
}

QStringList colPickerIds(const QListWidget* list)
{
    QStringList ids;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked) ids << list->item(i)->data(Qt::UserRole).toString();
    return ids;
}

/** 清單＋▲▼＋「預設」按鈕的組合 widget */
QWidget* makeColPicker(QListWidget*& list, QPushButton*& reset, QWidget* owner)
{
    list = new QListWidget;
    list->setMinimumHeight(200);
    auto* up = new QPushButton(QStringLiteral("▲"));
    auto* down = new QPushButton(QStringLiteral("▼"));
    reset = new QPushButton(QStringLiteral("預設"));
    up->setMaximumWidth(36); down->setMaximumWidth(36);
    QListWidget* l = list;
    QObject::connect(up, &QPushButton::clicked, owner, [l]() {
        const int r = l->currentRow();
        if (r <= 0) return;
        QListWidgetItem* it = l->takeItem(r);
        l->insertItem(r - 1, it);
        l->setCurrentRow(r - 1);
    });
    QObject::connect(down, &QPushButton::clicked, owner, [l]() {
        const int r = l->currentRow();
        if (r < 0 || r + 1 >= l->count()) return;
        QListWidgetItem* it = l->takeItem(r);
        l->insertItem(r + 1, it);
        l->setCurrentRow(r + 1);
    });
    auto* box = new QWidget;
    auto* hl = new QHBoxLayout(box);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addWidget(list, 1);
    auto* vl = new QVBoxLayout;
    vl->addWidget(up); vl->addWidget(down); vl->addWidget(reset); vl->addStretch();
    hl->addLayout(vl);
    return box;
}

} // namespace

PlanProfileDialog::PlanProfileDialog(cad::Document* doc, const PlanProfileSettings& initial,
                                     QWidget* parent)
    : QDialog(parent), m_doc(doc)
{
    setWindowTitle(QStringLiteral("平縱面圖（Plan & Profile）設定"));
    resize(520, 520);

    auto* tabs = new QTabWidget;
    m_tabs = tabs;
    QFormLayout* f = nullptr;

    // ── 線形與範圍 ────────────────────────────────────────────────────────
    tabs->addTab(page(f), QStringLiteral("線形與分段"));
    m_tclList = new QListWidget;
    m_tclList->setMaximumHeight(130);
    if (m_doc)
        for (railway::TrackCenterLine* t : m_doc->trackCenterLines()) {
            if (!t) continue;
            auto* it = new QListWidgetItem(t->name().isEmpty() ? t->id() : t->name());
            it->setData(Qt::UserRole, t->id());
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(Qt::Unchecked);
            it->setData(Qt::UserRole + 1, false);
            it->setData(Qt::UserRole + 2, it->text());
            m_tclList->addItem(it);
        }
    m_tclUp = new QPushButton(QStringLiteral("▲"));
    m_tclDown = new QPushButton(QStringLiteral("▼"));
    m_tclUp->setMaximumWidth(36); m_tclDown->setMaximumWidth(36);
    m_tclSolo = new QPushButton(QStringLiteral("單獨"));
    m_tclSolo->setMaximumWidth(36);
    m_tclSolo->setToolTip(QStringLiteral("切換所選線形「單獨（不成串）」：不往後接續，也不被其他線形串進去"));
    connect(m_tclSolo, &QPushButton::clicked, this, [this]() {
        QListWidgetItem* it = m_tclList->currentItem();
        if (!it) return;
        it->setData(Qt::UserRole + 1, !it->data(Qt::UserRole + 1).toBool());
        applySoloText(it);
        updateInfo();
    });
    connect(m_tclUp, &QPushButton::clicked, this, [this]() {
        const int r = m_tclList->currentRow();
        if (r <= 0) return;
        QListWidgetItem* it = m_tclList->takeItem(r);
        m_tclList->insertItem(r - 1, it);
        m_tclList->setCurrentRow(r - 1);
        updateInfo();
    });
    connect(m_tclDown, &QPushButton::clicked, this, [this]() {
        const int r = m_tclList->currentRow();
        if (r < 0 || r + 1 >= m_tclList->count()) return;
        QListWidgetItem* it = m_tclList->takeItem(r);
        m_tclList->insertItem(r + 1, it);
        m_tclList->setCurrentRow(r + 1);
        updateInfo();
    });
    connect(m_tclList, &QListWidget::itemChanged, this, [this](QListWidgetItem*) { updateInfo(); });
    m_fullRange = new QCheckBox(QStringLiteral("使用整條線形"));
    m_startCh = dsb(-1e7, 1e7, 100, 2, " m");
    m_endCh   = dsb(-1e7, 1e7, 100, 2, " m");
    m_hScale  = dsb(1, 1e6, 100, 0);
    m_vScale  = dsb(1, 1e6, 10, 0);
    m_autoV   = new QCheckBox(QStringLiteral("垂直比例自動（不會比上方設定更誇張）"));
    m_segLen  = dsb(0, 1e7, 100, 1, " m");
    m_segLen->setSpecialValueText(QStringLiteral("自動（依圖幅寬度）"));
    m_overlap = dsb(0, 1e6, 10, 1, " m");
    m_follow = new QCheckBox(QStringLiteral("線形後面有接續線形時繼續產生（直到沒有後續）"));
    m_joinTol = dsb(0, 1000, 0.5, 2, " m");
    m_neighbors = new QCheckBox(QStringLiteral("平面圖框內的其他線形一併繪出"));
    m_genIndex = new QCheckBox(QStringLiteral("同時產生圖目錄（獨立圖紙）"));
    m_genCover = new QCheckBox(QStringLiteral("圖冊含封面"));
    m_genTables = new QCheckBox(QStringLiteral("同時產生平面／縱面線形資料表圖頁"));
    m_genSchema = new QCheckBox(QStringLiteral("同時產生簡圖（軌道示意、平面簡圖、縱面簡圖）"));
    m_joinTolZ = dsb(0, 1000, 0.1, 2, " m");
    m_bookName = new QLineEdit;
    f->addRow(QStringLiteral("圖冊名稱"), m_bookName);
    {
        auto* box = new QWidget;
        auto* hl = new QHBoxLayout(box);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(m_tclList, 1);
        auto* vl = new QVBoxLayout;
        vl->addWidget(m_tclUp); vl->addWidget(m_tclDown); vl->addWidget(m_tclSolo); vl->addStretch();
        hl->addLayout(vl);
        f->addRow(QStringLiteral("起始線形（勾選 1 條以上，依清單順序產生）"), box);
    }
    f->addRow(m_follow);
    f->addRow(QStringLiteral("接續判定：E,N 距離"), m_joinTol);
    f->addRow(QStringLiteral("接續判定：高程差 Z"), m_joinTolZ);
    f->addRow(m_neighbors);
    f->addRow(m_genIndex);
    f->addRow(m_genTables);
    f->addRow(m_genSchema);
    f->addRow(m_genCover);
    f->addRow(m_fullRange);
    f->addRow(QStringLiteral("起點里程（僅作用於起始線形，且不再串接）"), m_startCh);
    f->addRow(QStringLiteral("終點里程"), m_endCh);
    f->addRow(QStringLiteral("水平比例 1 :"), m_hScale);
    f->addRow(QStringLiteral("垂直比例 1 :"), m_vScale);
    f->addRow(m_autoV);
    f->addRow(QStringLiteral("每張長度"), m_segLen);
    f->addRow(QStringLiteral("相鄰重疊"), m_overlap);
    connect(m_fullRange, &QCheckBox::toggled, this, [this](bool on) {
        m_startCh->setEnabled(!on); m_endCh->setEnabled(!on);
    });

    // ── 版面 ──────────────────────────────────────────────────────────────
    tabs->addTab(page(f), QStringLiteral("版面"));
    m_paper = new QComboBox;
    m_paper->addItems({ "A4", "A3", "A2", "A1", "A0" });
    m_orient = new QComboBox;
    m_orient->addItems({ QStringLiteral("直向"), QStringLiteral("橫向") });
    m_planH   = dsb(20, 2000, 5, 1, " mm");
    m_profH   = dsb(30, 2000, 5, 1, " mm");
    m_rowH    = dsb(3, 30, 0.5, 1, " mm");
    m_labelW  = dsb(10, 80, 1, 1, " mm");
    m_textSize= dsb(1, 8, 0.1, 2, " mm");
    f->addRow(QStringLiteral("紙張"), m_paper);
    f->addRow(QStringLiteral("方向"), m_orient);
    f->addRow(QStringLiteral("平面圖帶高度"), m_planH);
    f->addRow(QStringLiteral("縱斷面帶高度"), m_profH);
    f->addRow(QStringLiteral("資料表列高"), m_rowH);
    f->addRow(QStringLiteral("左側標籤欄寬"), m_labelW);
    f->addRow(QStringLiteral("註記字高"), m_textSize);

    // ── 刻度與格式 ────────────────────────────────────────────────────────
    tabs->addTab(page(f), QStringLiteral("刻度與格式"));
    m_minor   = dsb(1, 1e5, 5, 1, " m");
    m_major   = dsb(1, 1e5, 10, 1, " m");
    m_elevStep= dsb(0, 1e4, 1, 2, " m");
    m_elevStep->setSpecialValueText(QStringLiteral("自動"));
    m_tblInt  = dsb(1, 1e5, 10, 1, " m");
    m_prefix  = new QLineEdit;
    m_km      = new QCheckBox(QStringLiteral("K1+234.00 格式（否則純數字）"));
    m_cont    = new QCheckBox(QStringLiteral("以連續里程標註"));
    m_elevDec = new QSpinBox; m_elevDec->setRange(0, 4);
    f->addRow(QStringLiteral("次刻度間距"), m_minor);
    f->addRow(QStringLiteral("主刻度間距（標註里程）"), m_major);
    f->addRow(QStringLiteral("高程格線間距"), m_elevStep);
    f->addRow(QStringLiteral("資料表取值間距"), m_tblInt);
    f->addRow(QStringLiteral("里程前綴"), m_prefix);
    f->addRow(m_km);
    f->addRow(m_cont);
    f->addRow(QStringLiteral("高程小數位數"), m_elevDec);

    // ── 內容 ──────────────────────────────────────────────────────────────
    tabs->addTab(page(f), QStringLiteral("內容"));
    m_coordGrid = dsb(0, 1e5, 50, 1, " m");
    m_coordGrid->setSpecialValueText(QStringLiteral("不畫"));
    m_north = new QCheckBox(QStringLiteral("指北針"));
    m_keyPts = new QCheckBox(QStringLiteral("平面圖：關鍵點（TS/SC/CS…）"));
    m_curveData = new QCheckBox(QStringLiteral("平面圖：曲線資料（R、Lc、超高）"));
    m_grid = new QCheckBox(QStringLiteral("縱斷面：格線"));
    m_pvi = new QCheckBox(QStringLiteral("縱斷面：豎曲線／VPI 標註"));
    m_gradeLbl = new QCheckBox(QStringLiteral("縱斷面：坡度與坡長"));
    m_rowElev = new QCheckBox(QStringLiteral("資料表列：高程"));
    m_rowGrade = new QCheckBox(QStringLiteral("資料表列：坡度"));
    m_rowCurve = new QCheckBox(QStringLiteral("資料表列：半徑／超高"));
    m_rowAz = new QCheckBox(QStringLiteral("資料表列：方位角"));
    f->addRow(QStringLiteral("TM2 座標格線間距"), m_coordGrid);
    for (QCheckBox* c : { m_north, m_keyPts, m_curveData, m_grid, m_pvi, m_gradeLbl,
                          m_rowElev, m_rowGrade, m_rowCurve, m_rowAz })
        f->addRow(c);

    // ── 資料表欄位 ────────────────────────────────────────────────────────
    tabs->addTab(page(f), QStringLiteral("資料表欄位"));
    f->addRow(new QLabel(QStringLiteral("線形資料表圖頁的欄位：勾選要顯示的欄位，用 ▲▼ 調整順序（屬於圖冊設定）。")));
    m_tblDec = new QSpinBox;
    m_tblDec->setRange(0, 8);
    m_tblDec->setSuffix(QStringLiteral(" 位"));
    m_tblAzDec = new QSpinBox;
    m_tblAzDec->setRange(0, 6);
    m_tblAzDec->setSuffix(QStringLiteral(" 位"));
    f->addRow(QStringLiteral("數值小數位數（座標、里程、長度、半徑、高程…，預設 5）"), m_tblDec);
    f->addRow(QStringLiteral("方位角「秒」的小數位數（格式 ddd°mm'ss.sss\"，預設 3）"), m_tblAzDec);
    m_radSigned = new QCheckBox(QStringLiteral("半徑帶正負號（右＋、左－）"));
    f->addRow(m_radSigned);
    f->addRow(QStringLiteral("平面線形資料表"), makeColPicker(m_hzList, m_hzReset, this));
    f->addRow(QStringLiteral("縱斷面線形資料表"), makeColPicker(m_vtList, m_vtReset, this));
    connect(m_hzReset, &QPushButton::clicked, this, [this]() { fillColPicker(m_hzList, 1, planProfileDefaultTableColumns(1)); });
    connect(m_vtReset, &QPushButton::clicked, this, [this]() { fillColPicker(m_vtList, 2, planProfileDefaultTableColumns(2)); });

    // ── 機廠視圖 ──────────────────────────────────────────────────────────
    m_depotTabIdx = tabs->addTab(page(f), QStringLiteral("機廠視圖"));
    m_genDepot = new QCheckBox(QStringLiteral("同時產生機廠平面圖（每個矩形視圖一張）"));
    f->addRow(m_genDepot);
    f->addRow(new QLabel(QStringLiteral(
        "每列一個矩形視圖：中心 E、N（TM2）、寬、高（m）、轉角（度，逆時針）。\n"
        "選了「對齊線形」時，視圖 X 軸對齊該線形在「對齊里程」處的直線方向，轉角再疊加其上。\n"
        "比例填 0 為自動配合圖紙；排序小者在前。")));
    m_depotHint = new QLabel;
    m_depotHint->setStyleSheet(QStringLiteral("color:#335;background:#eef;padding:4px;"));
    f->addRow(m_depotHint);
    connect(m_paper, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updateDepotHint(); });
    connect(m_orient, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updateDepotHint(); });
    m_depotTbl = new QTableWidget(0, 10);
    m_depotTbl->setHorizontalHeaderLabels({ QStringLiteral("名稱"), QStringLiteral("中心E"), QStringLiteral("中心N"),
        QStringLiteral("寬(m)"), QStringLiteral("高(m)"), QStringLiteral("轉角(°)"), QStringLiteral("對齊線形"),
        QStringLiteral("對齊里程"), QStringLiteral("排序"), QStringLiteral("比例 1:n") });
    m_depotTbl->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_depotTbl->horizontalHeader()->setStretchLastSection(true);
    m_depotTbl->setMinimumHeight(180);
    m_depotAdd = new QPushButton(QStringLiteral("新增視圖"));
    m_depotDel = new QPushButton(QStringLiteral("刪除所選"));
    {
        auto* hb = new QHBoxLayout;
        hb->addWidget(m_depotAdd);
        hb->addWidget(m_depotDel);
        hb->addStretch();
        auto* w = new QWidget;
        auto* vb = new QVBoxLayout(w);
        vb->setContentsMargins(0, 0, 0, 0);
        vb->addWidget(m_depotTbl);
        vb->addLayout(hb);
        f->addRow(w);
    }
    connect(m_depotAdd, &QPushButton::clicked, this, [this]() {
        DepotView v;
        v.name = QStringLiteral("機廠視圖 %1").arg(m_depotTbl->rowCount() + 1);
        v.order = m_depotTbl->rowCount() + 1;
        if (m_doc) {
            for (railway::TrackCenterLine* t : m_doc->trackCenterLines()) {
                double a = 0, b = 0;
                if (t && planProfileChainageRange(t, a, b)) {
                    core::geometry::ProjectOrigin::ensureDefault();
                    const QPointF g = core::geometry::ProjectOrigin::instance().toGlobal(t->getXYForCalc(a));
                    v.e = g.x(); v.n = g.y();
                    break;
                }
            }
        }
        addDepotRow(v);
    });
    connect(m_depotDel, &QPushButton::clicked, this, [this]() {
        const int r = m_depotTbl->currentRow();
        if (r >= 0) m_depotTbl->removeRow(r);
    });

    // ── 圖紙文字 ──────────────────────────────────────────────────────────
    tabs->addTab(page(f), QStringLiteral("圖紙文字"));
    m_titleTpl = new QLineEdit;
    m_numberTpl = new QLineEdit;
    m_replace = new QCheckBox(QStringLiteral("重新產生時取代此線形先前產生的平縱面圖紙"));
    f->addRow(QStringLiteral("圖名範本"), m_titleTpl);
    f->addRow(QStringLiteral("圖號範本"), m_numberTpl);
    m_remarkDec = new QSpinBox;
    m_remarkDec->setRange(0, 4);
    m_remarkDec->setSuffix(QStringLiteral(" 位"));
    f->addRow(QStringLiteral("圖目錄 Remark 里程小數位數（0＝整數，格式 0K+000~0K+200）"), m_remarkDec);
    f->addRow(new QLabel(QStringLiteral("可用變數：{name} 線形名、{n} 第幾張、{N} 總張數、{start} {end} 起訖里程")));
    f->addRow(m_replace);

    // ── 底部 ──────────────────────────────────────────────────────────────
    m_info = new QLabel;
    m_info->setWordWrap(true);

    auto* bLoad = new QPushButton(QStringLiteral("載入設定檔…"));
    auto* bSave = new QPushButton(QStringLiteral("儲存設定檔…"));
    auto* bDef  = new QPushButton(QStringLiteral("設為我的預設"));
    auto* bUser = new QPushButton(QStringLiteral("套用我的預設"));
    auto* bFac  = new QPushButton(QStringLiteral("還原出廠值"));
    auto* bGen  = new QPushButton(QStringLiteral("產生圖紙"));
    auto* bCan  = new QPushButton(QStringLiteral("取消"));
    bGen->setDefault(true);
    m_okBtn = bGen;

    auto* row1 = new QHBoxLayout;
    row1->addWidget(bLoad); row1->addWidget(bSave); row1->addWidget(bDef);
    row1->addWidget(bUser); row1->addWidget(bFac);
    auto* row2 = new QHBoxLayout;
    row2->addStretch(); row2->addWidget(bGen); row2->addWidget(bCan);

    auto* lay = new QVBoxLayout(this);
    lay->addWidget(tabs);
    lay->addWidget(m_info);
    lay->addLayout(row1);
    lay->addLayout(row2);

    connect(bLoad, &QPushButton::clicked, this, &PlanProfileDialog::onLoad);
    connect(bSave, &QPushButton::clicked, this, &PlanProfileDialog::onSave);
    connect(bDef,  &QPushButton::clicked, this, &PlanProfileDialog::onSaveDefault);
    connect(bUser, &QPushButton::clicked, this, &PlanProfileDialog::onLoadUserDefault);
    connect(bFac,  &QPushButton::clicked, this, &PlanProfileDialog::onResetFactory);
    connect(bGen,  &QPushButton::clicked, this, &QDialog::accept);
    connect(bCan,  &QPushButton::clicked, this, &QDialog::reject);

    apply(initial);

    // 採用前次的視窗大小／位置與分頁（QSettings）
    {
        QSettings qs("AICAD", "AICAD");
        const QByteArray g = qs.value("PlanProfileDialog/geometry").toByteArray();
        if (!g.isEmpty()) restoreGeometry(g);
        const int tab = qs.value("PlanProfileDialog/tab", 0).toInt();
        if (tab >= 0 && tab < m_tabs->count()) m_tabs->setCurrentIndex(tab);
    }

    // 任何變更都更新「將產生幾張」的提示
    for (QDoubleSpinBox* w : findChildren<QDoubleSpinBox*>())
        connect(w, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updateInfo(); });
    for (QCheckBox* w : findChildren<QCheckBox*>())
        connect(w, &QCheckBox::toggled, this, [this](bool) { updateInfo(); });
    for (QComboBox* w : findChildren<QComboBox*>())
        connect(w, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updateInfo(); });
    updateInfo();
}

void PlanProfileDialog::applySoloText(QListWidgetItem* it)
{
    if (!it) return;
    QSignalBlocker block(m_tclList);
    const QString base = it->data(Qt::UserRole + 2).toString();
    it->setText(it->data(Qt::UserRole + 1).toBool() ? base + QStringLiteral("  〔單獨〕") : base);
}

QStringList PlanProfileDialog::soloIds() const
{
    QStringList ids;
    for (int i = 0; i < m_tclList->count(); ++i) {
        const QListWidgetItem* it = m_tclList->item(i);
        if (it->data(Qt::UserRole + 1).toBool()) ids << it->data(Qt::UserRole).toString();
    }
    return ids;
}

QStringList PlanProfileDialog::checkedIds() const
{
    QStringList ids;
    for (int i = 0; i < m_tclList->count(); ++i) {
        const QListWidgetItem* it = m_tclList->item(i);
        if (it->checkState() == Qt::Checked) ids << it->data(Qt::UserRole).toString();
    }
    return ids;
}

void PlanProfileDialog::setCheckedIds(const QStringList& ids)
{
    QSignalBlocker block(m_tclList);
    // 已選的依 ids 順序排到最前面，其餘維持原順序
    QVector<QListWidgetItem*> sel, rest;
    while (m_tclList->count() > 0) {
        QListWidgetItem* it = m_tclList->takeItem(0);
        (ids.contains(it->data(Qt::UserRole).toString()) ? sel : rest).append(it);
    }
    std::sort(sel.begin(), sel.end(), [&](QListWidgetItem* a, QListWidgetItem* b) {
        return ids.indexOf(a->data(Qt::UserRole).toString()) < ids.indexOf(b->data(Qt::UserRole).toString());
    });
    for (QListWidgetItem* it : sel)  { it->setCheckState(Qt::Checked);   m_tclList->addItem(it); }
    for (QListWidgetItem* it : rest) { it->setCheckState(Qt::Unchecked); m_tclList->addItem(it); }
    if (sel.isEmpty() && m_tclList->count() > 0) m_tclList->item(0)->setCheckState(Qt::Checked);
}

void PlanProfileDialog::updateDepotHint()
{
    if (!m_depotHint || !m_paper || !m_orient) return;
    PlanProfileSettings tmp;
    tmp.paperSize = m_paper->currentIndex();
    tmp.orientation = m_orient->currentIndex();
    m_depotHint->setText(planProfileDepotSizeHint(tmp));
}

void PlanProfileDialog::showDepotTab(int row)
{
    if (m_tabs) m_tabs->setCurrentIndex(m_depotTabIdx);
    if (m_depotTbl && row >= 0 && row < m_depotTbl->rowCount()) m_depotTbl->selectRow(row);
}

void PlanProfileDialog::setApplyMode(bool on)
{
    if (m_okBtn) m_okBtn->setText(on ? QStringLiteral("套用並更新圖紙") : QStringLiteral("產生圖紙"));
}

void PlanProfileDialog::accept()
{
    // 儲存對話框設定（下次開啟採用）：視窗、分頁、最後一次的參數（全域設定；局部設定不覆寫）
    QSettings qs("AICAD", "AICAD");
    qs.setValue("PlanProfileDialog/geometry", saveGeometry());
    if (m_tabs) qs.setValue("PlanProfileDialog/tab", m_tabs->currentIndex());
    if (!m_local) settings().saveAsDefault();
    QDialog::accept();
}

void PlanProfileDialog::setLocalMode(bool local)
{
    if (local && m_okBtn) m_okBtn->setText(QStringLiteral("套用並更新此圖"));
    setWindowTitle(local ? QStringLiteral("此圖設定（局部）— 只影響這一張；未改的欄位仍跟隨圖冊設定")
                         : QStringLiteral("平縱面圖冊設定（全域）"));
    const QList<QWidget*> fixed = { m_tclList, m_tclUp, m_tclDown, m_tclSolo, m_follow, m_joinTol, m_joinTolZ, m_fullRange,
        m_startCh, m_endCh, m_hScale, m_segLen, m_overlap, m_paper, m_orient, m_labelW,
        m_bookName, m_genIndex, m_genTables, m_genSchema, m_genDepot, m_depotTbl, m_depotAdd, m_depotDel, m_genCover, m_replace, m_hzList, m_vtList, m_hzReset, m_vtReset, m_tblDec, m_tblAzDec, m_radSigned };
    for (QWidget* w : fixed) if (w) w->setEnabled(!local);
    if (!local) { m_startCh->setEnabled(!m_fullRange->isChecked()); m_endCh->setEnabled(!m_fullRange->isChecked()); }
    else if (m_info) m_info->setText(QStringLiteral("灰色欄位（線形、分段、紙張、圖冊）屬於圖冊層級，請用「圖冊設定（全域）」修改。"));
    m_local = local;
}

void PlanProfileDialog::apply(const PlanProfileSettings& s)
{
    setCheckedIds(s.startIds());
    m_follow->setChecked(s.followChain);
    for (int i = 0; i < m_tclList->count(); ++i) {
        QListWidgetItem* it = m_tclList->item(i);
        it->setData(Qt::UserRole + 1, s.noChainIds.contains(it->data(Qt::UserRole).toString()));
        applySoloText(it);
    }
    m_joinTol->setValue(s.joinTol);
    m_joinTolZ->setValue(s.joinTolZ);
    m_bookName->setText(s.bookName);
    m_genCover->setChecked(s.generateCover);
    m_neighbors->setChecked(s.showNeighbors);
    m_genIndex->setChecked(s.generateIndex);
    m_genTables->setChecked(s.generateDataTables);
    m_genSchema->setChecked(s.generateSchematic);
    m_genDepot->setChecked(s.generateDepot);
    m_depotTbl->setRowCount(0);
    for (const DepotView& v : s.depotViews) addDepotRow(v);
    updateDepotHint();
    m_tblDec->setValue(s.tableDecimals);
    m_radSigned->setChecked(s.radiusSigned);
    m_remarkDec->setValue(s.remarkDecimals);
    m_tblAzDec->setValue(s.tableAzDecimals);
    fillColPicker(m_hzList, 1, planProfileEffectiveTableColumns(s, 1));
    fillColPicker(m_vtList, 2, planProfileEffectiveTableColumns(s, 2));
    m_fullRange->setChecked(s.useFullRange);
    m_startCh->setValue(s.startCh);
    m_endCh->setValue(s.endCh);
    m_startCh->setEnabled(!s.useFullRange);
    m_endCh->setEnabled(!s.useFullRange);
    m_hScale->setValue(s.hScale);
    m_vScale->setValue(s.vScale);
    m_autoV->setChecked(s.autoVScale);
    m_segLen->setValue(s.segmentLength);
    m_overlap->setValue(s.overlap);

    m_paper->setCurrentIndex(std::max(0, std::min(4, s.paperSize)));
    m_orient->setCurrentIndex(s.orientation ? 1 : 0);
    m_planH->setValue(s.planBandH);
    m_profH->setValue(s.profileBandH);
    m_rowH->setValue(s.tableRowH);
    m_labelW->setValue(s.leftLabelW);
    m_textSize->setValue(s.textSize);

    m_minor->setValue(s.minorTick);
    m_major->setValue(s.majorTick);
    m_elevStep->setValue(s.elevGridStep);
    m_tblInt->setValue(s.tableInterval);
    m_prefix->setText(s.chainagePrefix);
    m_km->setChecked(s.kmFormat);
    m_cont->setChecked(s.useContChainage);
    m_elevDec->setValue(s.elevDecimals);

    m_coordGrid->setValue(s.coordGrid);
    m_north->setChecked(s.showNorthArrow);
    m_keyPts->setChecked(s.showKeyPoints);
    m_curveData->setChecked(s.showCurveData);
    m_grid->setChecked(s.showGridLines);
    m_pvi->setChecked(s.showPVI);
    m_gradeLbl->setChecked(s.showGradeLabels);
    m_rowElev->setChecked(s.rowElevation);
    m_rowGrade->setChecked(s.rowGrade);
    m_rowCurve->setChecked(s.rowCurve);
    m_rowAz->setChecked(s.rowAzimuth);

    m_titleTpl->setText(s.titleTemplate);
    m_numberTpl->setText(s.numberTemplate);
    m_replace->setChecked(s.replaceExisting);
}

void PlanProfileDialog::addDepotRow(const DepotView& v)
{
    const int r = m_depotTbl->rowCount();
    m_depotTbl->insertRow(r);
    auto num = [](double x, int dec) { return QString::number(x, 'f', dec); };
    m_depotTbl->setItem(r, 0, new QTableWidgetItem(v.name));
    m_depotTbl->setItem(r, 1, new QTableWidgetItem(num(v.e, 3)));
    m_depotTbl->setItem(r, 2, new QTableWidgetItem(num(v.n, 3)));
    m_depotTbl->setItem(r, 3, new QTableWidgetItem(num(v.w, 1)));
    m_depotTbl->setItem(r, 4, new QTableWidgetItem(num(v.h, 1)));
    m_depotTbl->setItem(r, 5, new QTableWidgetItem(num(v.rotDeg, 3)));
    auto* cb = new QComboBox;
    cb->addItem(QStringLiteral("（不對齊）"), QString());
    if (m_doc)
        for (railway::TrackCenterLine* t : m_doc->trackCenterLines())
            if (t) cb->addItem(t->name(), t->id());
    const int ix = cb->findData(v.alignTcl);
    cb->setCurrentIndex(ix >= 0 ? ix : 0);
    m_depotTbl->setCellWidget(r, 6, cb);
    m_depotTbl->setItem(r, 7, new QTableWidgetItem(num(v.alignCh, 3)));
    m_depotTbl->setItem(r, 8, new QTableWidgetItem(QString::number(v.order)));
    m_depotTbl->setItem(r, 9, new QTableWidgetItem(num(v.scale, 0)));
}

QVector<DepotView> PlanProfileDialog::depotViews() const
{
    QVector<DepotView> out;
    auto txt = [this](int r, int c) {
        const QTableWidgetItem* it = m_depotTbl->item(r, c);
        return it ? it->text() : QString();
    };
    for (int r = 0; r < m_depotTbl->rowCount(); ++r) {
        DepotView v;
        v.name = txt(r, 0);
        v.e = txt(r, 1).toDouble(); v.n = txt(r, 2).toDouble();
        v.w = txt(r, 3).toDouble(); v.h = txt(r, 4).toDouble();
        v.rotDeg = txt(r, 5).toDouble();
        if (auto* cb = qobject_cast<QComboBox*>(m_depotTbl->cellWidget(r, 6))) v.alignTcl = cb->currentData().toString();
        v.alignCh = txt(r, 7).toDouble();
        v.order = txt(r, 8).toInt();
        v.scale = txt(r, 9).toDouble();
        out.append(v);
    }
    return out;
}

PlanProfileSettings PlanProfileDialog::settings() const
{
    PlanProfileSettings s;
    s.tclIds = checkedIds();
    s.tclId = s.tclIds.isEmpty() ? QString() : s.tclIds.first();
    s.followChain = m_follow->isChecked();
    s.noChainIds = soloIds();
    s.joinTol = m_joinTol->value();
    s.joinTolZ = m_joinTolZ->value();
    s.bookName = m_bookName->text();
    s.generateCover = m_genCover->isChecked();
    s.showNeighbors = m_neighbors->isChecked();
    s.generateIndex = m_genIndex->isChecked();
    s.generateDataTables = m_genTables->isChecked();
    s.generateSchematic = m_genSchema->isChecked();
    s.generateDepot = m_genDepot->isChecked();
    s.depotViews = depotViews();
    s.tableDecimals = m_tblDec->value();
    s.radiusSigned = m_radSigned->isChecked();
    s.remarkDecimals = m_remarkDec->value();
    s.tableAzDecimals = m_tblAzDec->value();
    s.hzCols = colPickerIds(m_hzList);
    s.vtCols = colPickerIds(m_vtList);
    s.useFullRange = m_fullRange->isChecked();
    s.startCh = m_startCh->value();
    s.endCh = m_endCh->value();
    s.hScale = m_hScale->value();
    s.vScale = m_vScale->value();
    s.autoVScale = m_autoV->isChecked();
    s.segmentLength = m_segLen->value();
    s.overlap = m_overlap->value();

    s.paperSize = m_paper->currentIndex();
    s.orientation = m_orient->currentIndex();
    s.planBandH = m_planH->value();
    s.profileBandH = m_profH->value();
    s.tableRowH = m_rowH->value();
    s.leftLabelW = m_labelW->value();
    s.textSize = m_textSize->value();

    s.minorTick = m_minor->value();
    s.majorTick = m_major->value();
    s.elevGridStep = m_elevStep->value();
    s.tableInterval = m_tblInt->value();
    s.chainagePrefix = m_prefix->text();
    s.kmFormat = m_km->isChecked();
    s.useContChainage = m_cont->isChecked();
    s.elevDecimals = m_elevDec->value();

    s.coordGrid = m_coordGrid->value();
    s.showNorthArrow = m_north->isChecked();
    s.showKeyPoints = m_keyPts->isChecked();
    s.showCurveData = m_curveData->isChecked();
    s.showGridLines = m_grid->isChecked();
    s.showPVI = m_pvi->isChecked();
    s.showGradeLabels = m_gradeLbl->isChecked();
    s.rowElevation = m_rowElev->isChecked();
    s.rowGrade = m_rowGrade->isChecked();
    s.rowCurve = m_rowCurve->isChecked();
    s.rowAzimuth = m_rowAz->isChecked();

    s.titleTemplate = m_titleTpl->text();
    s.numberTemplate = m_numberTpl->text();
    s.replaceExisting = m_replace->isChecked();
    return s;
}

void PlanProfileDialog::updateInfo()
{
    if (!m_info || m_local) return;
    const PlanProfileSettings s = settings();
    QString err;
    const auto segs = planProfileSegments(m_doc, s, &err);
    if (segs.isEmpty()) {
        m_info->setText(QStringLiteral("<span style='color:#c00'>無法產生：%1</span>").arg(err));
        return;
    }
    QStringList lines;
    for (const QVector<PlanProfileSegment>& sheetPieces : segs)
        for (const PlanProfileSegment& g : sheetPieces)
            if (!lines.contains(g.tclId)) lines << g.tclId;
    QStringList names;
    for (const QString& id : lines) {
        const railway::TrackCenterLine* t = m_doc ? m_doc->findTrackCenterLine(id) : nullptr;
        names << (t ? t->name() : id);
    }
    m_info->setText(QStringLiteral("將產生 %1 張平縱面圖（每張約 %2 m），涵蓋線形：%3")
        .arg(segs.size())
        .arg(planProfilePiecesLength(segs.first()), 0, 'f', 0)
        .arg(names.join(QStringLiteral(" → "))));
}

void PlanProfileDialog::onLoad()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("載入平縱面圖設定檔"),
        QString(), QStringLiteral("平縱面圖設定 (*.aicad_ppcfg);;JSON (*.json);;All (*)"));
    if (path.isEmpty()) return;
    PlanProfileSettings s = settings();      // 設定檔沒寫的欄位保留目前值
    QString err;
    if (!s.loadFromFile(path, &err)) { QMessageBox::warning(this, windowTitle(), err); return; }
    {   // 檔內線形在本文件找不到時，沿用目前勾選
        QStringList ok;
        for (const QString& id : s.startIds())
            if (m_doc && m_doc->findTrackCenterLine(id)) ok << id;
        s.tclIds = ok.isEmpty() ? checkedIds() : ok;
        s.tclId = s.tclIds.isEmpty() ? QString() : s.tclIds.first();
    }
    apply(s);
    updateInfo();
}

void PlanProfileDialog::onSave()
{
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("儲存平縱面圖設定檔"),
        QStringLiteral("planprofile.aicad_ppcfg"), QStringLiteral("平縱面圖設定 (*.aicad_ppcfg)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".aicad_ppcfg"), Qt::CaseInsensitive)) path += QStringLiteral(".aicad_ppcfg");
    QString err;
    if (!settings().saveToFile(path, &err)) QMessageBox::warning(this, windowTitle(), err);
}

void PlanProfileDialog::onSaveDefault()
{
    settings().saveAsDefault();
    QMessageBox::information(this, windowTitle(), QStringLiteral("已存為你的預設值（下次開啟自動套用）。"));
}

void PlanProfileDialog::onResetFactory()
{
    PlanProfileSettings s;
    s.tclIds = checkedIds();
    s.tclId = s.tclIds.isEmpty() ? QString() : s.tclIds.first();
    apply(s);
    updateInfo();
}

void PlanProfileDialog::onLoadUserDefault()
{
    PlanProfileSettings s = PlanProfileSettings::loadDefault();
    s.tclIds = checkedIds();
    s.tclId = s.tclIds.isEmpty() ? QString() : s.tclIds.first();
    apply(s);
    updateInfo();
}

bool runPlanProfileIndexEditor(DrawingSheetManager* mgr, cad::Document* doc, QWidget* parent,
                               const QString& bookId)
{
    if (!mgr || !doc) return false;

    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("圖目錄 — 調整圖的順序"));
    dlg.resize(560, 460);
    auto* list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::SingleSelection);

    QVector<DrawingSheet*> others;      // 本圖冊內、封面與圖目錄以外的圖紙（原順序）
    for (DrawingSheet* sh : mgr->sheets())
        if (sh->bookId() == bookId && !sh->isCover() && !sh->planProfile().isIndex) others.append(sh);

    for (DrawingSheet* sh : others) {
        auto* it = new QListWidgetItem(QStringLiteral("%1   %2   %3")
            .arg(sh->titleBlock().sheetNumber, -6)
            .arg(sh->titleBlock().drawingNumber, -14)
            .arg(sh->titleBlock().drawingTitle));
        it->setData(Qt::UserRole, sh->id().toString());
        list->addItem(it);
    }
    if (list->count() > 0) list->setCurrentRow(0);

    auto* up = new QPushButton(QStringLiteral("▲ 上移"));
    auto* down = new QPushButton(QStringLiteral("▼ 下移"));
    auto* ok = new QPushButton(QStringLiteral("套用並重建圖目錄"));
    auto* cancel = new QPushButton(QStringLiteral("取消"));
    auto* side = new QVBoxLayout;
    side->addWidget(up); side->addWidget(down); side->addStretch();
    auto* mid = new QHBoxLayout;
    mid->addWidget(list, 1); mid->addLayout(side);
    auto* bot = new QHBoxLayout;
    bot->addStretch(); bot->addWidget(ok); bot->addWidget(cancel);
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QStringLiteral("調整後的順序即為圖目錄、圖號與頁次的順序（平縱面圖會依序重新編號）。")));
    lay->addLayout(mid, 1);
    lay->addLayout(bot);

    auto move = [list](int d) {
        const int r = list->currentRow(), t = r + d;
        if (r < 0 || t < 0 || t >= list->count()) return;
        QListWidgetItem* it = list->takeItem(r);
        list->insertItem(t, it);
        list->setCurrentRow(t);
    };
    QObject::connect(up, &QPushButton::clicked, &dlg, [move]() { move(-1); });
    QObject::connect(down, &QPushButton::clicked, &dlg, [move]() { move(1); });
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return false;

    // 新順序：先把本圖冊的圖依清單重排，再整理（封面→圖目錄→其餘）
    QVector<DrawingSheet*> newOrder;
    for (int i = 0; i < list->count(); ++i)
        if (DrawingSheet* sh = mgr->findSheet(QUuid(list->item(i)->data(Qt::UserRole).toString())))
            newOrder.append(sh);
    QVector<DrawingSheet*> order;
    bool placed = false;
    for (DrawingSheet* sh : mgr->sheets()) {
        if (others.contains(sh)) {
            if (!placed) { order += newOrder; placed = true; }
        } else {
            order.append(sh);
        }
    }
    mgr->setSheetOrder(order);
    renumberPlanProfileSheets(mgr, doc, bookId);
    rebuildPlanProfileIndex(mgr, doc, bookId);
    normalizeSheetOrder(mgr);
    mgr->touch();
    return true;
}

} // namespace drawing
} // namespace aicad
