/**
 * @file AlignmentDataTableDialog.cpp
 * @brief 「線形資料表」對話框實作。
 *
 * 水平資料表：讀取 result()->rawPoints() 所產生的關鍵點序列（TS/SC/CS/ST/TT/CC…），
 *   直接顯示 tsc 點位、座標、里程、方位角、長度及曲線類型/半徑。
 *   可編輯欄位透過 HRowMeta 映射回對應的 EditableElement 索引。
 *
 * 縱斷面資料表：讀取 AlignmentDocument::vertical() 的 VipRecord 序列。
 *   若 VipRecord 為空（TCL 從 ALD 直接載入，尚未透過 AlignmentDocument 處理），
 *   則由 tcl->vertical()->points() 反推 PVI 資料。
 *
 * @author AICAD Team
 */

#include "AlignmentDataTableDialog.h"

#include "railway/AlignmentDocument.h"
#include "railway/RailwayAlignment.h"
#include "command/alignment/AlignmentEditCommand.h"
#include "core/geometry/ProjectOrigin.h"

#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QLocale>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QMenu>
#include <QAction>
#include <QtMath>
#include <cmath>

using namespace aicad::railway;

namespace aicad {
namespace ui {

// ============================================================================
//  Local constants and helpers
// ============================================================================
namespace {

// ── 緩和曲線類型 ─────────────────────────────────────────────────────────────
const QStringList kSpiralTypeNames = {
    QObject::tr("Clothoid (歐拉螺線)"),
    QObject::tr("HalfSine (半正弦)"),
    QObject::tr("Parabola (三次拋物線)"),
    QObject::tr("CubicJPN (日式三次拋物線)"),
    QObject::tr("CubicECI (CECI三次拋物線)")
};

// curveType 字串 → SpiralType
SpiralType curveTypeToEnum(const QString& s)
{
    if (s == QLatin1String("HALFSINE")) return SpiralType::HalfSine;
    if (s == QLatin1String("PARABOLA")) return SpiralType::Parabola;
    if (s == QLatin1String("CUBICJPN")) return SpiralType::CubicJPN;
    if (s == QLatin1String("CUBICECI")) return SpiralType::CubicECI;
    return SpiralType::Clothoid;
}

QString spiralTypeToDisplay(SpiralType t)
{
    switch (t) {
    case SpiralType::HalfSine: return QObject::tr("HalfSine");
    case SpiralType::Parabola: return QObject::tr("Parabola");
    case SpiralType::CubicJPN: return QObject::tr("CubicJPN");
    case SpiralType::CubicECI: return QObject::tr("CubicECI");
    default:                   return QObject::tr("Clothoid");
    }
}

QString curveTypeToDisplay(const QString& ct)
{
    if (ct == QLatin1String("ARC"))      return QObject::tr("圓弧 (ARC)");
    if (ct == QLatin1String("HALFSINE")) return QObject::tr("HalfSine");
    if (ct == QLatin1String("PARABOLA")) return QObject::tr("Parabola");
    if (ct == QLatin1String("CUBICJPN")) return QObject::tr("CubicJPN");
    if (ct == QLatin1String("CUBICECI")) return QObject::tr("CubicECI");
    if (ct == QLatin1String("SPIRAL"))   return QObject::tr("Clothoid");
    return QObject::tr("STRAIGHT");
}

/// 方位角（弧度，順時針由北）→ ddd°mm'ss.sss" 格式，不足位補零
/// 例：方位角 123.7524° → "123°45'08.640\""
QString azimuthToDMS(double rad)
{
    double deg = qRadiansToDegrees(rad);
    while (deg <    0.0) deg += 360.0;
    while (deg >= 360.0) deg -= 360.0;
    const int    d = static_cast<int>(deg);
    const double rem1 = (deg - d) * 60.0;
    const int    m = static_cast<int>(rem1);
    const double s = (rem1 - m) * 60.0;
    // 度：3 位補零；分：2 位補零；秒：ss.sss（共 6 字元）補零
    return QString(u8"%1\u00B0%2'%3\"")
        .arg(d,  3, 10, QChar('0'))
        .arg(m,  2, 10, QChar('0'))
        .arg(s,  6, 'f', 3, QChar('0'));
}

/// 唯讀儲存格（灰底）
QTableWidgetItem* roItem(const QString& text,
                         const QColor& bg = QColor(242, 242, 242))
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setBackground(QBrush(bg));
    return item;
}

/// 可編輯儲存格（淡黃底）
QTableWidgetItem* editItem(double v, int decimals = 5)
{
    auto* item = new QTableWidgetItem(QString::number(v, 'f', decimals));
    item->setBackground(QBrush(QColor(255, 255, 210)));
    return item;
}

} // namespace

// ============================================================================
//  Construction / setup
// ============================================================================

AlignmentDataTableDialog::AlignmentDataTableDialog(AlignmentDocument* doc,
                                                   TrackCenterLine*   tcl,
                                                   QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
    , m_tcl(tcl)
{
    setWindowTitle(tcl ? tr("線形資料表 — %1").arg(tcl->name())
                       : tr("線形資料表"));
    resize(1020, 580);
    buildUi();
    populateHorizontalTable();
    initVerticalVipsIfEmpty();
    populateVerticalTable();

    // ── 與內部資料同步 ─────────────────────────────────────────────────────
    // aDoc->vertical() 是 VAlignProfileView（繪圖編輯）與本資料表共用的唯一
    // VIP 資料來源；不論變更來自本表格自身編輯、VAlignProfileView 的拖曳/
    // 增刪 VIP，或 Undo/Redo，都會呼叫 solve() 並發出 changed()。這裡統一
    // 監聽該訊號以自動刷新表格，確保兩者不會顯示不同步的資料。
    // （m_populating 已保護 onVCellChanged 不會被 populate 觸發的
    //   itemChanged 訊號誤觸發，故此處重新整理是安全的。）
    if (m_doc) {
        connect(m_doc->vertical(), &railway::VerticalAlignmentEdit::changed,
                this, &AlignmentDataTableDialog::populateVerticalTable);
    }
}

void AlignmentDataTableDialog::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);
    mainLayout->addWidget(m_tabs);

    // ── 水平線形 ──────────────────────────────────────────────────────────────
    m_hTable = new QTableWidget(this);
    m_hTable->setColumnCount(7);
    m_hTable->setHorizontalHeaderLabels({
        tr("點位"), tr("Easting (m)"), tr("Northing (m)"),
        tr("里程 (m)"), tr("方位角"), tr("長度 (m)"), tr("曲線類型 / 半徑 (m)")
    });
    m_hTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_hTable->horizontalHeader()->setStretchLastSection(true);
    m_hTable->verticalHeader()->setVisible(false);
    m_hTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_hTable->setEditTriggers(QAbstractItemView::DoubleClicked |
                              QAbstractItemView::EditKeyPressed);
    m_hTable->setAlternatingRowColors(true);
    connect(m_hTable, &QTableWidget::itemChanged,
            this, &AlignmentDataTableDialog::onHCellChanged);
    connect(m_hTable, &QTableWidget::cellDoubleClicked,
            this, &AlignmentDataTableDialog::onHCellDoubleClicked);
    m_tabs->addTab(m_hTable, tr("平面線形"));

    // ── 縱斷面線形 ───────────────────────────────────────────────────────────
    m_vTable = new QTableWidget(this);
    m_vTable->setColumnCount(8);
    m_vTable->setHorizontalHeaderLabels({
        tr("點位"), tr("里程 (m)"), tr("高程 (m)"),
        tr("坡度%"), tr("K值"), tr("PVI高程 (m)"), tr("Lvc (m)"), tr("Mo (m)")
    });
    m_vTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_vTable->horizontalHeader()->setStretchLastSection(true);
    m_vTable->verticalHeader()->setVisible(false);
    m_vTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_vTable->setEditTriggers(QAbstractItemView::DoubleClicked |
                              QAbstractItemView::EditKeyPressed);
    m_vTable->setAlternatingRowColors(true);
    connect(m_vTable, &QTableWidget::itemChanged,
            this, &AlignmentDataTableDialog::onVCellChanged);
    m_tabs->addTab(m_vTable, tr("縱斷面線形"));

    // ── 提示文字 ─────────────────────────────────────────────────────────────
    auto* hint = new QLabel(
        tr("提示：雙擊黃色儲存格可編輯（灰色欄位為計算結果，唯讀）。修改後立即套用，可使用 Ctrl+Z 復原。"),
        this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked,
            this, &QDialog::accept);
    mainLayout->addWidget(buttons);
}

// ============================================================================
//  Horizontal table
// ============================================================================

void AlignmentDataTableDialog::populateHorizontalTable()
{
    if (!m_doc) return;
    m_populating = true;
    m_hTable->setRowCount(0);
    m_hMeta.clear();

    // 優先用 m_doc 的 solver result；若尚未 solve，fallback 到 TCL 已載入的 rawPoints
    const HorizontalAlignment* ha = m_doc->horizontal()->result();
    const QVector<AlignmentPoint>* rawSrc = nullptr;
    QVector<AlignmentPoint> fallback;
    bool usingSolverResult = false;
    if (ha && !ha->isEmpty()) {
        rawSrc = &ha->rawPoints();
        usingSolverResult = true;
    } else if (m_tcl && !m_tcl->horizontal()->isEmpty()) {
        fallback = m_tcl->horizontal()->rawPoints();
        rawSrc   = &fallback;
        usingSolverResult = false;  // ALD 直接載入：座標已是 TM2
    }
    if (!rawSrc || rawSrc->isEmpty()) {
        m_populating = false;
        return;
    }

    // ── 去除重複計量的切線段起始點 ──────────────────────────────────────────
    // 根因：SCS 群組發出 ST 時已將 exit tangent T(4) 的長度計入里程，
    //       但 T(4) 未被 handledBySCS 標記，仍在已前進的里程再次發出 TT。
    //       結果：ST 與 TT 的 easting/northing「相同」（都在 stPoint），
    //             但里程「不同」（ST=X, TT=X+L4）。
    // 判斷邏輯：
    //   若本點「出發為切線」(tsc[1]=='T')，且緊鄰的前一個保留點「出發也為切線」，
    //   且兩點的實體位置（easting/northing）相同（距離 < 1mm）→ 同一切線段重複 → 略過本點。
    QVector<AlignmentPoint> filteredPts;
    filteredPts.reserve(rawSrc->size());
    for (const AlignmentPoint& p : *rawSrc) {
        if (!filteredPts.isEmpty()
            && p.tsc.size() == 2 && p.tsc[1] == QLatin1Char('T')
            && filteredPts.last().tsc.size() == 2
            && filteredPts.last().tsc[1] == QLatin1Char('T')) {
            const double dx = p.easting  - filteredPts.last().easting;
            const double dy = p.northing - filteredPts.last().northing;
            if (dx*dx + dy*dy < 1.0e-6) {   // 實體距離 < 1 mm
                continue;   // 同一切線段起始點重複 → 略過
            }
        }
        filteredPts.append(p);
    }
    const QVector<AlignmentPoint>* pts = &filteredPts;

    // ── 建立 HRowMeta 映射：掃描 EditableElements，與 rawPoints tsc 序列對齊 ─
    // 計數：第幾個 SpiralIn / CircularArc / SpiralOut 尚未被映射
    const auto& elems = m_doc->horizontal()->elements();
    int spiralInCount  = 0;
    int spiralOutCount = 0;
    int arcCount       = 0;

    auto nthElemIdx = [&](EditableElementType t, int nth) -> int {
        int cnt = 0;
        for (int i = 0; i < elems.size(); ++i) {
            if (elems[i].type == t) {
                if (cnt == nth) return i;
                ++cnt;
            }
        }
        return -1;
    };

    m_hTable->setRowCount(pts->size());
    m_hMeta.resize(pts->size());

    for (int row = 0; row < pts->size(); ++row) {
        const AlignmentPoint& p = (*pts)[row];
        HRowMeta meta;

        // ── 點位（tsc）──────────────────────────────────────────────────────
        // 粗體標示 TS/SC/CS/ST 等特殊點
        auto* tscItem = roItem(p.tsc, Qt::transparent);
        tscItem->setFlags(tscItem->flags() & ~Qt::ItemIsEditable);
        if (p.tsc != QLatin1String("TT")) {
            QFont f = tscItem->font();
            f.setBold(true);
            tscItem->setFont(f);
        }
        m_hTable->setItem(row, 0, tscItem);

        // ── TM2 座標（LOCAL → TM2 via ProjectOrigin）────────────────────────
        // solver result 為 Local 座標；ALD 直接載入時座標已是 TM2（identity 轉換）。
        // ProjectOrigin::toGlobal() 在未設定時為恆等轉換，安全呼叫。
        using aicad::core::geometry::ProjectOrigin;
        const QPointF tm2 = usingSolverResult
                                ? ProjectOrigin::instance().toGlobal(p.easting, p.northing)
                                : QPointF(p.easting, p.northing);

        m_hTable->setItem(row, 1, roItem(QString::number(tm2.x(), 'f', 5)));
        m_hTable->setItem(row, 2, roItem(QString::number(tm2.y(), 'f', 5)));
        m_hTable->setItem(row, 3, roItem(QString::number(p.chainage, 'f', 5)));
        m_hTable->setItem(row, 4, roItem(azimuthToDMS(p.azimuth)));

        // ── 長度欄：TS / CS 可編輯（緩和曲線長度）──────────────────────────
        if (p.tsc == QLatin1String("TS")) {
            meta.elemIdx  = nthElemIdx(EditableElementType::SpiralIn, spiralInCount);
            meta.editLen  = true;
            meta.editType = true;
            ++spiralInCount;
            m_hTable->setItem(row, 5, editItem(p.length));
        } else if (p.tsc == QLatin1String("CS")) {
            // CS 點的出緩和曲線類型來源：
            //   SCS 群組：由 SpiralIn.spiralType2 決定（solver 讀取 SpiralIn 元素）
            //   CA  群組：由 SpiralOut.spiralType2 決定（獨立 SpiralOut 元素）
            // 判斷方式：若 spiralOutCount < spiralInCount，代表此 CS 屬於已配對的 SCS。
            if (spiralOutCount < spiralInCount) {
                // SCS 群組：spiralOutCount 個 SCS 的出螺旋 → 對應第 spiralOutCount 個 SpiralIn
                meta.elemIdx = nthElemIdx(EditableElementType::SpiralIn, spiralOutCount);
            } else {
                // CA 群組：SpiralOut 元素在 m_elems 中的順序 = spiralOutCount
                // （前 spiralInCount 個 SpiralOut 屬於 SCS；後面的屬於 CA）
                meta.elemIdx = nthElemIdx(EditableElementType::SpiralOut, spiralOutCount);
            }
            meta.editLen  = true;
            meta.editType = true;
            ++spiralOutCount;
            m_hTable->setItem(row, 5, editItem(p.length));
        } else {
            m_hTable->setItem(row, 5, roItem(p.length > 1e-9
                                                 ? QString::number(p.length, 'f', 5) : QStringLiteral("—")));
        }

        // ── 曲線類型 / 半徑欄：SC / CC / TC 可編輯半徑；TS / CS 唯讀（類型）──
        if (p.tsc == QLatin1String("SC") ||
            p.tsc == QLatin1String("CC") ||
            p.tsc == QLatin1String("TC")) {
            const int eidx = nthElemIdx(EditableElementType::CircularArc, arcCount);
            ++arcCount;
            meta.elemIdx = eidx;
            meta.editRad = true;
            // 儲存格顯示半徑數值（黃底可編輯）
            const double absR = std::abs(p.radius);
            m_hTable->setItem(row, 6, editItem(absR > 1e-9 ? absR : 0.0, 5));
        } else if (p.tsc == QLatin1String("TS") || p.tsc == QLatin1String("CS")) {
            // 緩和曲線類型：唯讀文字（雙擊觸發選單）
            m_hTable->setItem(row, 6, roItem(curveTypeToDisplay(p.curveType)));
        } else {
            m_hTable->setItem(row, 6, roItem(curveTypeToDisplay(p.curveType)));
        }

        m_hMeta[row] = meta;
    }

    // 調整列寬
    m_hTable->resizeColumnsToContents();
    m_populating = false;
}

void AlignmentDataTableDialog::onHCellDoubleClicked(int row, int col)
{
    // 僅攔截「曲線類型」欄（col 6）的緩和曲線行 → 彈出類型選擇選單。
    // 其他可編輯欄（長度 col5、半徑 col6 的弧線行）由 Qt DoubleClicked 觸發器直接處理。
    if (col != 6) return;
    if (!m_doc || row < 0 || row >= m_hMeta.size()) return;

    const HRowMeta& meta = m_hMeta[row];
    if (!meta.editType) return;  // 非緩和曲線行

    const auto& elems = m_doc->horizontal()->elements();
    if (meta.elemIdx < 0 || meta.elemIdx >= elems.size()) return;

    const auto& e = elems[meta.elemIdx];
    // 從表格第 0 欄取得 tsc 字串（col 0 = 點位）
    const QString tsc = m_hTable->item(row, 0) ? m_hTable->item(row, 0)->text() : QString();
    // SCS 群組 CS：meta.elemIdx 指向 SpiralIn，讀 spiralType2（exit type）
    // CA  群組 CS：meta.elemIdx 指向 SpiralOut，讀 spiralType2（exit type）
    // TS 行：meta.elemIdx 指向 SpiralIn，讀 spiralType1（entry type）
    SpiralType curType = SpiralType::Clothoid;
    if (tsc == QLatin1String("TS")) {
        curType = e.spiralType1;
    } else if (tsc == QLatin1String("CS")) {
        curType = e.spiralType2;
    } else {
        return;
    }

    const bool isExit = (tsc == QLatin1String("CS"));
    auto* menu = new QMenu(this);
    for (int i = 0; i < kSpiralTypeNames.size(); ++i) {
        auto* act = menu->addAction(kSpiralTypeNames[i]);
        act->setCheckable(true);
        act->setChecked(i == static_cast<int>(curType));
        const int newTypeIdx = i;
        connect(act, &QAction::triggered, this, [this, meta, newTypeIdx, isExit] {
            if (!m_doc) return;
            const QJsonObject before = m_doc->toJson();
            m_doc->horizontal()->setSpiralType(
                meta.elemIdx, static_cast<SpiralType>(newTypeIdx), isExit);
            m_doc->horizontal()->solve();
            command::AlignmentEditCommand::push(
                m_doc, before, m_doc->toJson(), tr("編輯緩和曲線類型"));
            populateHorizontalTable();
            Q_EMIT dataCommitted();
        });
    }
    const QPoint cellCenter = m_hTable->viewport()->mapToGlobal(
        m_hTable->visualItemRect(m_hTable->item(row, col)).center());
    menu->exec(cellCenter);
    menu->deleteLater();
}

void AlignmentDataTableDialog::onHCellChanged(QTableWidgetItem* item)
{
    if (m_populating || !item || !m_doc) return;
    const int row = item->row();
    const int col = item->column();
    if (row < 0 || row >= m_hMeta.size()) return;

    const HRowMeta& meta = m_hMeta[row];
    if (meta.elemIdx < 0) return;

    bool ok = false;
    const double value = item->text().toDouble(&ok);
    if (!ok || value <= 0.0) {
        populateHorizontalTable();
        return;
    }

    const QJsonObject before = m_doc->toJson();
    QString actionText;

    if (col == 5 && meta.editLen) {
        m_doc->horizontal()->setLength(meta.elemIdx, value);
        actionText = tr("編輯緩和曲線長度");
    } else if (col == 6 && meta.editRad) {
        m_doc->horizontal()->setRadius(meta.elemIdx, value);
        actionText = tr("編輯圓曲線半徑");
    } else {
        return;
    }

    m_doc->horizontal()->solve();
    command::AlignmentEditCommand::push(m_doc, before, m_doc->toJson(), actionText);
    populateHorizontalTable();
    Q_EMIT dataCommitted();
}

// ============================================================================
//  Vertical table — VIP initialization from raw points
// ============================================================================

void AlignmentDataTableDialog::initVerticalVipsIfEmpty()
{
    if (!m_doc || !m_tcl) return;
    // 反推邏輯已統一至 VerticalAlignmentEdit::seedFromDensePoints，
    // 與 VAlignEditorDockWidget 共用同一份實作，避免兩處各自維護、
    // 可能產生不同結果的還原邏輯。
    m_doc->vertical()->seedFromDensePoints(m_tcl->vertical()->points());
}

// ============================================================================
//  Vertical table
// ============================================================================

void AlignmentDataTableDialog::populateVerticalTable()
{
    if (!m_doc) return;
    m_populating = true;
    m_vTable->setRowCount(0);

    // ── 資料來源：solver result 優先，fallback 到 TCL ─────────────────────
    const VerticalAlignment* va = m_doc->vertical()->result();
    const QVector<VerticalAlignmentPoint>* vSrc = nullptr;
    QVector<VerticalAlignmentPoint> fallback;
    if (va && !va->isEmpty()) {
        vSrc = &va->points();
    } else if (m_tcl && !m_tcl->vertical()->isEmpty()) {
        fallback = m_tcl->vertical()->points();
        vSrc     = &fallback;
    }
    if (!vSrc || vSrc->size() < 2) {
        m_populating = false;
        return;
    }

    // ── 豎曲線辨識門檻 ─────────────────────────────────────────────────
    // solver 以 tiny=1e-6 模擬純折點；真實豎曲線長度至少數十公分。
    // threshold = 1mm 足以區分兩者。
    constexpr double kMinLvc = 0.001;

    // ── 建立顯示列結構 ────────────────────────────────────────────────────
    // Solver 輸出結構（中間 VIP 各出 2 筆）：
    //   pts[0]          = 起點
    //   pts[2k-1]       = entry (BVC / tiny_before)  lvc = 0
    //   pts[2k]         = exit  (EVC / tiny_after)   lvc = L
    //   pts[2*(n-2)+1]  = 終點
    //
    // 顯示：起點 → (BVC, PVI_synth, EVC) / 折點 → ... → 終點
    // PVI 列: ch = (BVC.ch + EVC.ch)/2 = vip.ch, el = EVC.pviElevation = vip.el
    // 可編輯: PVI 列的里程、高程、Lvc（對應 vipIdx = k，k = 1..(n-2)）

    struct VRow {
        QString label;
        double  ch, el, grade, k, pviEl, lvc, mo;
        int     vipIdx;   ///< -1 = 唯讀；>=1 = 對應 m_vips[vipIdx]，PVI 列可編輯
    };
    QVector<VRow> rows;

    // 起點
    {
        const auto& p0 = vSrc->first();
        rows.append({"起點", p0.chainage, p0.elevation, p0.grade,
                     p0.kValue, 0.0, 0.0, 0.0, 0});
    }

    // 中間點：以步長 2 掃描 (entry, exit) 配對
    int vipIdx = 1;   // 對應 m_vips 中的第幾個中間 VIP（0=起點，n-1=終點）
    for (int i = 1; i + 1 <= vSrc->size() - 1; i += 2) {
        const VerticalAlignmentPoint& entry = (*vSrc)[i];
        const VerticalAlignmentPoint& exit  = (*vSrc)[i + 1];

        if (exit.lvc > kMinLvc) {
            // ── 真實豎曲線：BVC → PVI（合成） → EVC ─────────────────────
            const double pviCh = (entry.chainage + exit.chainage) / 2.0;

            rows.append({"BVC", entry.chainage, entry.elevation,
                         entry.grade, exit.kValue, exit.pviElevation, exit.lvc, exit.mo, -1});

            rows.append({"PVI", pviCh,
                         exit.pviElevation + exit.mo * (entry.grade < exit.grade ? 1.0 : -1.0),
                         (entry.grade + exit.grade)/2,  // 坡度
                         exit.kValue, exit.pviElevation, exit.lvc,
                         exit.mo * (entry.grade < exit.grade ? 1.0 : -1.0), vipIdx});

            rows.append({"EVC", exit.chainage, exit.elevation,
                         exit.grade, exit.kValue, exit.pviElevation, exit.lvc, exit.mo, -1});
        } else {
            // ── 純折點（tiny VC）：單列 ──────────────────────────────────
            rows.append({"折點", exit.chainage, exit.elevation,
                         exit.grade, exit.kValue, exit.pviElevation, 0.0, 0.0, vipIdx});
        }
        ++vipIdx;
    }

    // 終點
    {
        const auto& pN = vSrc->last();
        rows.append({"終點", pN.chainage, pN.elevation, pN.grade,
                     pN.kValue, 0.0, 0.0, 0.0, vipIdx});
    }

    // ── 填入表格 ────────────────────────────────────────────────────────
    m_vTable->setRowCount(rows.size());

    // 欄位索引：0=點位 1=里程 2=高程 3=坡度% 4=K值 5=PVI高程 6=Lvc 7=Mo
    for (int row = 0; row < rows.size(); ++row) {
        const VRow& r = rows[row];
        const bool isBEG   = (r.label == "起點");
        const bool isEND   = (r.label == "終點");
        const bool isPVI    = (r.label == QLatin1String("PVI"));
        const bool isFold   = (r.label == "折點");
        const bool editable = (isBEG || isEND || isPVI || isFold);

        // ── 點位（唯讀，粗體特殊點）
        auto* labelItem = roItem(r.label);
        if (isPVI || r.label == QLatin1String("起點") || r.label == QLatin1String("終點")) {
            QFont f = labelItem->font(); f.setBold(true); labelItem->setFont(f);
        }
        m_vTable->setItem(row, 0, labelItem);

        // ── 里程（PVI/折點可編輯）
        auto* chItem = editable ? editItem(r.ch) : roItem(QString::number(r.ch, 'f', 5));
        if (editable) chItem->setData(Qt::UserRole, r.vipIdx);  // 存 vipIdx 供 onVCellChanged
        m_vTable->setItem(row, 1, chItem);

        // ── 高程
        auto* elItem = (isBEG || isEND) ? editItem(r.el) : roItem(QString::number(r.el, 'f', 5));
        if (isBEG || isEND) elItem->setData(Qt::UserRole, r.vipIdx);  // 存 vipIdx 供 onVCellChanged
        m_vTable->setItem(row, 2, elItem);

        // ── 坡度%（BVC/折點/起點/終點 顯示坡度；PVI/EVC 坡度無意義留空）
        m_vTable->setItem(row, 3,
                          roItem(QString::number(r.grade, 'f', 5)));

        // ── K值（BVC/折點/起點/終點 顯示K值；PVI/EVC K值無意義留空）
        if (isPVI) {
            m_vTable->setItem(row, 4,
                              roItem(QString::number(r.k, 'f', 5)));
        } else {
            m_vTable->setItem(row, 4, roItem(QStringLiteral("—")));
        }

        if (isPVI) {
            // ──（PVI/折點可編輯）
            auto* pvielItem = editable ? editItem(r.pviEl) : roItem(QString::number(r.pviEl, 'f', 5));
            if (editable) pvielItem->setData(Qt::UserRole, r.vipIdx);
            m_vTable->setItem(row, 5, pvielItem);

            // ── Lvc（PVI/折點可編輯；折點顯示 0）
            if (editable && r.lvc > kMinLvc) {
                auto* lvcItem = editItem(r.lvc);
                lvcItem->setData(Qt::UserRole, r.vipIdx);
                m_vTable->setItem(row, 6, lvcItem);
            } else if (editable && isFold) {
                auto* lvcItem = editItem(0.0);
                lvcItem->setData(Qt::UserRole, r.vipIdx);
                m_vTable->setItem(row, 6, lvcItem);
            } else {
                m_vTable->setItem(row, 6,
                                  roItem(r.lvc > kMinLvc ? QString::number(r.lvc, 'f', 5)
                                                         : QStringLiteral("—")));
            }

            // ── Mo（唯讀）
            m_vTable->setItem(row, 7,
                              roItem(QString::number(r.mo, 'f', 5)));
        }
    }

    m_vTable->resizeColumnsToContents();
    m_populating = false;
}

void AlignmentDataTableDialog::onVCellChanged(QTableWidgetItem* item)
{
    if (m_populating || !item || !m_doc) return;
    const int row = item->row();
    const int col = item->column();

    // 取得 vipIdx（存於 UserRole）
    const QVariant role = item->data(Qt::UserRole);
    if (!role.isValid()) return;
    const int vipIdx = role.toInt();
    // 有效範圍為 [0, vipCount()-1]：起點(0)、終點(vipCount()-1) 只能編輯
    // 里程／高程（欄1/2），中間 PVI 才能額外編輯 PVI 高程／Lvc（欄5/6）；
    // 原本 [1, vipCount()-2] 的範圍會把起點與終點排除在外，導致這兩列的
    // 編輯被直接忽略、既未套用也未還原顯示。
    if (vipIdx < 0 || vipIdx >= m_doc->vertical()->vipCount()) return;

    bool ok = false;
    const double value = item->text().toDouble(&ok);
    if (!ok) { populateVerticalTable(); return; }

    const QJsonObject before = m_doc->toJson();
    auto* ve = m_doc->vertical();
    QString actionText;

    if (col == 1) {                              // 里程
        ve->moveVip(vipIdx, value, ve->vipElevation(vipIdx));
        actionText = tr("編輯 PVI 里程");
    } else if (col == 2) {                       // 高程
        ve->moveVip(vipIdx, ve->vipChainage(vipIdx), value);
        actionText = tr("編輯 PVI 高程BEG/END");
    } else if (col == 5) {                       // 高程
        ve->moveVip(vipIdx, ve->vipChainage(vipIdx), value);
        actionText = tr("編輯 PVI 高程");
    } else if (col == 6) {                       // Lvc
        ve->setLvc(vipIdx, value);
        actionText = tr("編輯豎曲線長度 Lvc");
    } else {
        return;
    }

    ve->solve();
    command::AlignmentEditCommand::push(m_doc, before, m_doc->toJson(), actionText);
    populateVerticalTable();
    Q_EMIT dataCommitted();
}

} // namespace ui
} // namespace aicad
