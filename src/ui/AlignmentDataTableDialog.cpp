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
#include <QPen>
#include <QFont>
#include <QMenu>
#include <QAction>
#include <QtMath>
#include <QPainter>
#include <QStyledItemDelegate>
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
    // 線形起訖點的邊界建構線，其虛擬（檔案資料範圍外）延伸另一側是圓弧
    // （seedFromRawPoints() 規則 2）：AlignmentSolver::solve() 以
    // "VIRTUAL_ARC:<radius>" 編碼半徑，這裡解析出來顯示，讓工程師看得出
    // 線形資料其實是在圓弧中途截斷，而非乾淨地在切線上結束。
    if (ct.startsWith(QLatin1String("VIRTUAL_ARC:")))
        return QObject::tr("建構弧 R=%1").arg(ct.mid(12));
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

/// 可編輯文字儲存格（淡黃底），供 Text1/Text2/CircularCurveNo 使用
QTableWidgetItem* editTextItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setBackground(QBrush(QColor(255, 255, 210)));
    return item;
}

/// 「不適用」儲存格（末列的點位間欄位；無背景色、唯讀、空白）
QTableWidgetItem* naItem()
{
    auto* item = new QTableWidgetItem(QString());
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setBackground(QBrush(QColor(250, 250, 250)));
    return item;
}

// ── 平面線形資料表欄位配置（共 16 欄）───────────────────────────────────────
enum HCol {
    kColTsc = 0,
    kColEasting,
    kColNorthing,
    kColChainage,
    kColContChainage,
    kColAzimuth,
    kColLength,             ///< 以下欄位為「本點 → 下一點」之間的線元資訊
    kColRadiusCurveType,
    kColCircularCurveNo,
    kColCant,
    kColGaugeWidenning,
    kColSpeedLimit,
    kColText1,
    kColText2,
    kColReal1,
    kColReal2,
    kHColCount
};

/// 第一個「點位間」欄位（length）；此欄以後的資料表現整體下移半列高，
/// 表示其歸屬於本點與下一點之間，而非單一點位本身。
constexpr int kFirstBetweenCol = kColLength;

/**
 * @brief 支援「點位間」欄位下移半格繪製的水平線形表格。
 *
 * Qt 的標準逐格繪製流程（QAbstractItemView）會將每個 delegate 的繪製動作
 * 裁切（clip）在該格「原始（未位移）」的矩形範圍內，即使 delegate 本身把
 * QStyleOptionViewItem::rect 位移半列高，超出原格範圍的下半部仍會被外層
 * 裁切掉——這正是「文字只顯示上半部」的成因。
 *
 * 因此「點位間」欄位（kColLength..kColReal2）改為：
 *   1. BetweenPointDelegate 完全不繪製內容（僅保留可編輯性／選取狀態邏輯），
 *      避免在錯誤（未位移）位置留下殘影。
 *   2. 本類別的 paintEvent() 在基底繪製完成後，直接以 viewport() 的
 *      QPainter 手動畫出這些欄位的背景與文字，並整體下移半列高——因為是
 *      在 paintEvent 中直接畫、不經過 QAbstractItemView 的逐格裁切機制，
 *      文字可以完整跨越列邊界顯示，不會被裁掉下半部。
 *   3. 格線同樣手動繪製：一般欄位（點位屬性）對齊正常列邊界；點位間欄位
 *      對齊下移半列高的位置，恰好銜接相鄰列，形成「錯位半格」的視覺效果；
 *      最後一列無下一點，故不繪製、也不延伸至該列。
 */
class HAlignTableWidget : public QTableWidget
{
public:
    explicit HAlignTableWidget(QWidget* parent = nullptr) : QTableWidget(parent) {}

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QTableWidget::paintEvent(event);

        const int rows = rowCount();
        const int cols = columnCount();
        if (rows == 0 || cols == 0)
            return;

        QPainter painter(viewport());

        // ── 手動繪製「點位間」欄位的背景 + 文字（下移半列高）───────────────
        // 直接在此處繪製，不透過 delegate/裁切機制，文字才能完整顯示。
        for (int row = 0; row < rows; ++row) {
            for (int c = kFirstBetweenCol; c < cols; ++c) {
                QTableWidgetItem* it = item(row, c);
                if (!it) continue;

                QRect r = visualItemRect(it);
                r.translate(0, r.height() / 2);

                painter.fillRect(r, it->background());
                if (!it->text().isEmpty()) {
                    painter.setPen(Qt::black);
                    painter.setFont(it->font());
                    painter.drawText(r.adjusted(4, 0, -4, 0),
                                      Qt::AlignLeft | Qt::AlignVCenter, it->text());
                }
            }
        }

        // ── 格線 ─────────────────────────────────────────────────────────
        painter.setPen(QPen(QColor(215, 215, 215)));

        const int leftX      = columnViewportPosition(0);
        const int betweenX   = columnViewportPosition(qMin(kFirstBetweenCol, cols - 1));
        const int rightX     = columnViewportPosition(cols - 1) + columnWidth(cols - 1);

        // ── 垂直欄分隔線（貫穿全表高度）──────────────────────────────────
        int x = leftX;
        for (int c = 0; c < cols; ++c) {
            painter.drawLine(x, rowViewportPosition(0), x, rowViewportPosition(rows - 1) + rowHeight(rows - 1));
            x += columnWidth(c);
        }
        painter.drawLine(x, rowViewportPosition(0), x, rowViewportPosition(rows - 1) + rowHeight(rows - 1));

        // ── 一般欄位（點位本身屬性）：正常列邊界 ─────────────────────────
        for (int row = 0; row <= rows; ++row) {
            const int y = (row < rows) ? rowViewportPosition(row)
                                        : rowViewportPosition(rows - 1) + rowHeight(rows - 1);
            painter.drawLine(leftX, y, betweenX, y);
        }

        // ── 點位間欄位：下移半列高的格線（首尾各少畫半格，最後一列不延伸）──
        for (int row = 0; row < rows; ++row) {
            const int y = rowViewportPosition(row) + rowHeight(row) / 2;
            painter.drawLine(betweenX, y, rightX, y);
        }
    }
};

/**
 * @brief 「點位間」欄位的委派：不繪製任何內容（實際顯示由
 *        HAlignTableWidget::paintEvent() 手動處理，見上方類別註解），
 *        但雙擊編輯時把編輯器位置一併下移半列高，與視覺顯示對齊。
 */
class BetweenPointDelegate : public QStyledItemDelegate
{
public:
    explicit BetweenPointDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter* /*painter*/, const QStyleOptionViewItem& /*option*/,
               const QModelIndex& /*index*/) const override
    {
        // 刻意不繪製任何東西：Qt 會將本函式的繪製結果裁切在「原始（未位移）」
        // 格子範圍內，若在此處畫下移後的內容，超出原格範圍的部分會被裁掉。
        // 實際可見內容改由 HAlignTableWidget::paintEvent() 直接在
        // viewport() 上繪製，不受此裁切限制。
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        opt.rect.translate(0, opt.rect.height() / 2);
        QStyledItemDelegate::updateEditorGeometry(editor, opt, index);
    }
};

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
    resize(1560, 620);
    setMinimumWidth(1400);
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
    m_hTable = new HAlignTableWidget(this);
    m_hTable->setColumnCount(kHColCount);
    m_hTable->setHorizontalHeaderLabels({
        tr("點位"), tr("東座標 (m)"), tr("北座標 (m)"),
        tr("里程 (m)"), tr("連續里程 (m)"), tr("方位角"),
        tr("長度 (m)"), tr("曲線類型/半徑"), tr("圓曲線編號"),
        tr("超高 (mm)"), tr("軌距加寬 (mm)"), tr("速限 (km/h)"),
        tr("備註一"), tr("備註二"), tr("數值一"), tr("數值二")
    });
    m_hTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_hTable->horizontalHeader()->setStretchLastSection(true);
    m_hTable->verticalHeader()->setVisible(false);
    m_hTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_hTable->setEditTriggers(QAbstractItemView::DoubleClicked |
                              QAbstractItemView::EditKeyPressed);
    m_hTable->setAlternatingRowColors(true);
    m_hTable->setShowGrid(false);  // 格線改由 HAlignTableWidget::paintEvent 手動繪製

    // length（含）以後的欄位代表「點位間」的線元資訊，整體下移半列高顯示，
    // 呼應 HAlignTableWidget 手動繪製的下移格線。
    auto* betweenDelegate = new BetweenPointDelegate(m_hTable);
    for (int c = kFirstBetweenCol; c < kHColCount; ++c)
        m_hTable->setItemDelegateForColumn(c, betweenDelegate);

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
        tr("提示：雙擊黃色儲存格可編輯（灰色欄位為計算結果，唯讀）。"
           "第一列的「里程」／「連續里程」可編輯起始里程；"
           "「圓曲線編號」／「超高」／「軌距加寬」／「速限」／"
           "「備註一」／「備註二」／「數值一」／「數值二」各列皆可編輯（末列除外）。"
           "修改後立即套用，可使用 Ctrl+Z 復原。"),
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

    // 確保 ProjectOrigin 已設定（若尚未設定，套用預設 TM2 origin），
    // 這樣 toGlobal() 轉換才能得到合理量級的 TM2 座標，而不會因 origin 未
    // 設定而退化成恆等轉換（顯示出遠小於 TM2 量級的數字）。
    aicad::core::geometry::ProjectOrigin::ensureDefault();

    // 優先用 m_doc 的 solver result；若尚未 solve，fallback 到 TCL 已載入的 rawPoints
    const HorizontalAlignment* ha = m_doc->horizontal()->result();
    const QVector<AlignmentPoint>* rawSrc = nullptr;
    QVector<AlignmentPoint> fallback;
    bool usingSolverResult = false;
    if (ha && !ha->isEmpty()) {
        rawSrc = &ha->rawPoints();
        usingSolverResult = true;
    } else if (m_tcl && !m_tcl->horizontal()->isEmpty()) {
        // ALD 直接匯入、尚未 solve 的 fallback：ImportAlignmentCommand 在匯入
        // 邊界已把 easting/northing 從 TM2 轉成 Local（扣掉 TM2 origin，未
        // 設定時套用預設值），所以這裡 rawPoints() 已經和 solver 分支一樣是
        // Local 座標，不需要（也不應該）再轉換一次。
        fallback = m_tcl->horizontal()->rawPoints();
        rawSrc   = &fallback;
        usingSolverResult = false;
    }
    if (!rawSrc || rawSrc->isEmpty()) {
        m_hAux.clear();
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

        // ── SS 交會點合併：ST → TS 間殘留短切線（< 1e-6 m）視為不存在，
        //    合併為單一「SS」列 ─────────────────────────────────────────
        // 背景：SS 交會點（兩段緩和曲線直接相接、中間無圓弧）在內部以
        //   ST（前一群組出緩和曲線終點）+ TS（後一群組入緩和曲線起點）
        //   兩個各自獨立求解的點表示，理論上應重合。AlignmentSolver 的
        //   solveSSJunction() 已把兩者距離收斂到遠低於量測精度；但只要
        //   還是兩個 AlignmentPoint，資料表上就會多出一列看起來像「一小
        //   段直線」的 ST/TS，容易誤導。純顯示層面：這條 Tangent 長度
        //   （= ST 點的 p.length）小於 1e-6 m 時，兩列合併為一列 tsc=
        //   "SS"，沿用 TS 點（後一群組入緩和曲線）本身的座標／里程／
        //   長度／類型資料——因為 TS 點才帶著「下一段緩和曲線」的完整
        //   資訊，且座標已與 ST 點幾乎重合，直接沿用不影響精度。
        if (!filteredPts.isEmpty()
            && p.tsc == QLatin1String("TS")
            && filteredPts.last().tsc == QLatin1String("ST")
            && filteredPts.last().length < 1.0e-6) {
            AlignmentPoint merged = p;
            merged.tsc = QStringLiteral("SS");
            filteredPts.removeLast();
            filteredPts.append(merged);
            continue;
        }

        filteredPts.append(p);
    }
    const QVector<AlignmentPoint>* pts = &filteredPts;

    // ── 輔助欄位快取 ─────────────────────────────────────────────────────────
    // CircularCurveNo/Cant/GaugeWidenning/SpeedLimit/Text1/Text2/Real1/Real2
    // 並非幾何求解的一部分：每次 solve() 都會以預設值（0 / 空字串）重新產生
    // AlignmentPoint 序列。若列數與快取相符，保留使用者先前輸入；否則（結構
    // 性變動，例如新增/刪除元素）改由目前來源資料重新播種。
    if (m_hAux.size() != pts->size())
        m_hAux = *pts;

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

    // ── 判斷某個 SpiralIn 是否為「單弧 SCS 群組」的起點 ─────────────────────
    // （SpiralIn → CircularArc → SpiralOut，恰好 3 個元素、共用同一組
    //  tangentIdxBefore/After）。用來與 addCompoundChain() 產生的複合鏈結
    // （N≥2 弧，元素數 2N+1 > 3）區分——後者每個 SpiralOut 元素自身就攜帶
    // 正確的螺旋類型（spiralType1，見 addCompoundChain()），不像單弧 SCS
    // 那樣把出螺旋類型另外存在配對的 SpiralIn.spiralType2 上。若不區分，
    // 複合鏈結中段 CS 列的「曲線類型」雙擊編輯會錯誤指向該群組自己的入
    // 螺旋元素，而非中段緩和曲線本身。
    auto isSingleArcSCSHead = [&](int spiralInIdx) -> bool {
        if (spiralInIdx < 0 || spiralInIdx + 2 >= elems.size()) return false;
        if (elems[spiralInIdx].type   != EditableElementType::SpiralIn)    return false;
        if (elems[spiralInIdx + 1].type != EditableElementType::CircularArc) return false;
        if (elems[spiralInIdx + 2].type != EditableElementType::SpiralOut)  return false;
        const int tb = elems[spiralInIdx].tangentIdxBefore;
        const int ta = elems[spiralInIdx].tangentIdxAfter;
        // 若第 4 個元素仍延續同一組邊界的 (CircularArc, SpiralOut) 配對，
        // 代表這其實是複合鏈結的一部分，不是單弧 SCS。
        if (spiralInIdx + 4 < elems.size()
            && elems[spiralInIdx + 3].type == EditableElementType::CircularArc
            && elems[spiralInIdx + 3].tangentIdxBefore == tb
            && elems[spiralInIdx + 3].tangentIdxAfter  == ta
            && elems[spiralInIdx + 4].type == EditableElementType::SpiralOut
            && elems[spiralInIdx + 4].tangentIdxBefore == tb
            && elems[spiralInIdx + 4].tangentIdxAfter  == ta) {
            return false;
        }
        return true;
    };

    m_hTable->setRowCount(pts->size());
    m_hMeta.resize(pts->size());

    const int lastRow = pts->size() - 1;

    for (int row = 0; row < pts->size(); ++row) {
        const AlignmentPoint& p = (*pts)[row];
        HRowMeta meta;
        meta.isLastPoint = (row == lastRow);

        // ── 點位（tsc）──────────────────────────────────────────────────────
        // 粗體標示 TS/SC/CS/ST 等特殊點
        auto* tscItem = roItem(p.tsc, Qt::transparent);
        tscItem->setFlags(tscItem->flags() & ~Qt::ItemIsEditable);
        if (p.tsc != QLatin1String("TT")) {
            QFont f = tscItem->font();
            f.setBold(true);
            tscItem->setFont(f);
        }
        m_hTable->setItem(row, kColTsc, tscItem);

        // ── TM2 座標 ─────────────────────────────────────────────────────────
        // 架構原則（見 ProjectOrigin.h）：OCCT/CAD 內部只使用 Local 座標，
        // TM2 大數值只在輸入框／顯示文字／檔案 I/O 這三個邊界出現。
        // 不論資料來源是 solver 結果，還是 ALD 直接匯入的 fallback（匯入時
        // ImportAlignmentCommand 已在檔案 I/O 邊界把 TM2 換算成 Local），
        // 到這裡 p.easting/p.northing 一律是 Local 座標，統一透過
        // ProjectOrigin::toGlobal() 轉回真正的 TM2 顯示；若專案尚未設定
        // origin，populateHorizontalTable() 開頭的 ProjectOrigin::ensureDefault()
        // 已套用預設值，因此一定能得到合理量級的 TM2 座標。
        using aicad::core::geometry::ProjectOrigin;
        const QPointF tm2 = ProjectOrigin::instance().toGlobal(p.easting, p.northing);

        m_hTable->setItem(row, kColEasting,  roItem(QString::number(tm2.x(), 'f', 5)));
        m_hTable->setItem(row, kColNorthing, roItem(QString::number(tm2.y(), 'f', 5)));

        // ── Chainage / ContinuousChainage：僅第一列可編輯（起始里程）────────
        // 不論資料來源是否已透過 solver 求解皆可編輯：已求解時透過
        // HorizontalAlignmentEdit 的起始里程偏移量套用；尚未求解（例如 ALD
        // 直接匯入、尚未進行任何幾何編輯）則直接平移 TCL 的原始點位。
        if (row == 0) {
            auto* chItem = editItem(p.chainage);
            m_hTable->setItem(row, kColChainage, chItem);
            meta.editStartChainage = true;

            auto* contItem = editItem(p.contChainage);
            m_hTable->setItem(row, kColContChainage, contItem);
            meta.editStartContChainage = true;

            meta.startChainageUsesSolver = usingSolverResult;
        } else {
            m_hTable->setItem(row, kColChainage,     roItem(QString::number(p.chainage, 'f', 5)));
            m_hTable->setItem(row, kColContChainage, roItem(QString::number(p.contChainage, 'f', 5)));
        }

        m_hTable->setItem(row, kColAzimuth, roItem(azimuthToDMS(p.azimuth)));

        // ── length（含）以後：點位間（本點 → 下一點）的線元資訊 ─────────────
        // 最後一列之後沒有下一點，故不顯示這些欄位。
        if (meta.isLastPoint) {
            for (int c = kFirstBetweenCol; c < kHColCount; ++c)
                m_hTable->setItem(row, c, naItem());
            m_hMeta[row] = meta;
            continue;
        }

        // ── 長度欄：TS / CS 可編輯（緩和曲線長度）；SS 為 ST/TS 合併列，──
        //    比照 TS 處理（沿用其後一段緩和曲線的長度／SpiralIn 計數，
        //    見上方 filteredPts 合併邏輯的說明）。
        if (p.tsc == QLatin1String("TS") || p.tsc == QLatin1String("SS")) {
            meta.elemIdx  = nthElemIdx(EditableElementType::SpiralIn, spiralInCount);
            meta.lenElemIdx = meta.elemIdx;   // SpiralIn 自身持有 L1
            meta.editLen  = true;
            meta.editType = true;
            ++spiralInCount;
            m_hTable->setItem(row, kColLength, editItem(p.length));
        } else if (p.tsc == QLatin1String("CS")) {
            // CS 點的出緩和曲線類型來源：
            //   單弧 SCS 群組：由 SpiralIn.spiralType2 決定（solver 讀取 SpiralIn 元素）
            //   CA 群組／複合鏈結中段或末段：由 SpiralOut.spiralType1 自身決定
            //     （複合鏈結每個 SpiralOut 元素在 addCompoundChain() 建立時
            //      就已攜帶自己的類型，見 AlignmentDocument.cpp）
            // 判斷方式：spiralOutCount < spiralInCount 且對應的 SpiralIn 經
            // isSingleArcSCSHead() 確認「確實是單弧 SCS 群組」才走 SCS 分支；
            // 否則（含複合鏈結）一律走 CA 分支，直接讀 SpiralOut 自身欄位。
            const int candidateSpiralInIdx = nthElemIdx(EditableElementType::SpiralIn, spiralOutCount);
            const bool isPairedSingleArcSCS =
                (spiralOutCount < spiralInCount) && isSingleArcSCSHead(candidateSpiralInIdx);
            if (isPairedSingleArcSCS) {
                // 單弧 SCS 群組：spiralOutCount 個 SCS 的出螺旋 → 對應第 spiralOutCount 個 SpiralIn
                meta.elemIdx = candidateSpiralInIdx;
                // 但 L2（出螺旋長度）實際存在獨立的 SpiralOut 元素上，不能用
                // SpiralIn 的索引寫入，否則會覆蓋掉 SpiralIn 自己的 L1。
                meta.lenElemIdx = nthElemIdx(EditableElementType::SpiralOut, spiralOutCount);
            } else {
                // CA 群組／複合鏈結：SpiralOut 元素在 m_elems 中的順序 = spiralOutCount
                meta.elemIdx = nthElemIdx(EditableElementType::SpiralOut, spiralOutCount);
                meta.lenElemIdx = meta.elemIdx;
            }
            meta.editLen  = true;
            meta.editType = true;
            ++spiralOutCount;
            m_hTable->setItem(row, kColLength, editItem(p.length));
        } else {
            m_hTable->setItem(row, kColLength, roItem(p.length > 1e-9
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
            m_hTable->setItem(row, kColRadiusCurveType, editItem(absR > 1e-9 ? absR : 0.0, 5));
        } else {
            // 直線（STRAIGHT）或緩和曲線類型：唯讀文字（TS/CS 雙擊可觸發選單）
            m_hTable->setItem(row, kColRadiusCurveType, roItem(curveTypeToDisplay(p.curveType)));
        }

        // ── 輔助欄位（所有非末列皆可編輯，值來自 m_hAux 快取）───────────────
        const AlignmentPoint& aux = m_hAux[row];
        m_hTable->setItem(row, kColCircularCurveNo, editTextItem(aux.circularCurveNo));
        m_hTable->setItem(row, kColCant,             editItem(aux.cant, 3));
        m_hTable->setItem(row, kColGaugeWidenning,   editItem(aux.gaugeWidening, 3));
        m_hTable->setItem(row, kColSpeedLimit,       editItem(aux.speedLimit, 1));
        m_hTable->setItem(row, kColText1,            editTextItem(aux.text1));
        m_hTable->setItem(row, kColText2,            editTextItem(aux.text2));
        m_hTable->setItem(row, kColReal1,            editItem(aux.real1, 5));
        m_hTable->setItem(row, kColReal2,            editItem(aux.real2, 5));

        m_hMeta[row] = meta;
    }

    // 調整列寬
    m_hTable->resizeColumnsToContents();

    // 將輔助欄位快取回寫至 TCL（見 pushAuxToTcl() 注解）。
    // 注意：m_hAux 只用來保存使用者輸入的「輔助欄位」；幾何相關欄位（尤其
    // chainage）一律以 *pts 目前最新的值為準——若幾何剛被編輯（長度/半徑/
    // 起始里程），m_hAux 內快取的里程可能已經過時，若直接拿去比對會在
    // pushAuxToTcl() 的里程匹配邏輯中錯位。這裡以「列序」（m_hAux 與 *pts
    // 一一對應）合併出正確的里程 + 最新輔助欄位後再回寫。
    QVector<AlignmentPoint> merged = *pts;
    for (int i = 0; i < merged.size() && i < m_hAux.size(); ++i) {
        merged[i].circularCurveNo = m_hAux[i].circularCurveNo;
        merged[i].cant            = m_hAux[i].cant;
        merged[i].gaugeWidening   = m_hAux[i].gaugeWidening;
        merged[i].speedLimit      = m_hAux[i].speedLimit;
        merged[i].text1           = m_hAux[i].text1;
        merged[i].text2           = m_hAux[i].text2;
        merged[i].real1           = m_hAux[i].real1;
        merged[i].real2           = m_hAux[i].real2;
    }
    pushAuxToTcl(merged);

    m_populating = false;
}

void AlignmentDataTableDialog::onHCellDoubleClicked(int row, int col)
{
    // 僅攔截「RadiusCurveType」欄的緩和曲線行 → 彈出類型選擇選單。
    // 其他可編輯欄（length、半徑）由 Qt DoubleClicked 觸發器直接處理。
    if (col != kColRadiusCurveType) return;
    if (!m_doc || row < 0 || row >= m_hMeta.size()) return;

    const HRowMeta& meta = m_hMeta[row];
    if (!meta.editType) return;  // 非緩和曲線行

    const auto& elems = m_doc->horizontal()->elements();
    if (meta.elemIdx < 0 || meta.elemIdx >= elems.size()) return;

    const auto& e = elems[meta.elemIdx];
    // 從表格第 0 欄取得 tsc 字串（col 0 = 點位）
    const QString tsc = m_hTable->item(row, kColTsc) ? m_hTable->item(row, kColTsc)->text() : QString();
    // SCS 群組 CS：meta.elemIdx 指向 SpiralIn，讀 spiralType2（exit type）
    // CA  群組 CS：meta.elemIdx 指向 SpiralOut，讀 spiralType2（exit type）
    // TS 行：meta.elemIdx 指向 SpiralIn，讀 spiralType1（entry type）
    // SS 行：ST/TS 合併列（短切線 < 1e-6 m），比照 TS 處理
    SpiralType curType = SpiralType::Clothoid;
    if (tsc == QLatin1String("TS") || tsc == QLatin1String("SS")) {
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

    // ── 輔助欄位（CircularCurveNo/Cant/GaugeWidenning/SpeedLimit/Text1/Text2/
    //    Real1/Real2）：所有非末列皆可編輯，不需要 EditableElement，也不觸發
    //    幾何 solve()／Undo，直接寫回快取並同步至 TCL。────────────────────────
    if (!meta.isLastPoint && col >= kColCircularCurveNo && col < kHColCount) {
        if (row >= m_hAux.size()) return;
        AlignmentPoint& aux = m_hAux[row];

        if (col == kColCircularCurveNo) {
            aux.circularCurveNo = item->text();
        } else if (col == kColText1) {
            aux.text1 = item->text();
        } else if (col == kColText2) {
            aux.text2 = item->text();
        } else {
            bool ok = false;
            const double value = item->text().toDouble(&ok);
            if (!ok) { populateHorizontalTable(); return; }
            if      (col == kColCant)           aux.cant           = value;
            else if (col == kColGaugeWidenning)  aux.gaugeWidening  = value;
            else if (col == kColSpeedLimit)      aux.speedLimit     = value;
            else if (col == kColReal1)           aux.real1          = value;
            else if (col == kColReal2)           aux.real2          = value;
            else return;
        }

        // 輔助欄位不影響幾何，直接同步至 TCL（不進 Undo 堆疊，也不需 re-solve）。
        pushAuxToTcl(m_hAux);
        Q_EMIT dataCommitted();
        return;
    }

    // ── 起始里程（僅第一列）───────────────────────────────────────────────
    if (row == 0 && (col == kColChainage || col == kColContChainage) &&
        (meta.editStartChainage || meta.editStartContChainage)) {
        bool ok = false;
        const double value = item->text().toDouble(&ok);
        if (!ok) { populateHorizontalTable(); return; }

        if (meta.startChainageUsesSolver) {
            // ── 已透過 solver 求解：以 HorizontalAlignmentEdit 的起始里程
            //    偏移量套用（純顯示/輸出用途，見 AlignmentDocument.h 注解）。
            const QJsonObject before = m_doc->toJson();
            QString actionText;
            if (col == kColChainage) {
                m_doc->horizontal()->setStartChainage(value);
                actionText = tr("編輯起始里程");
            } else {
                m_doc->horizontal()->setStartContinuousChainage(value);
                actionText = tr("編輯起始連續里程");
            }
            m_doc->horizontal()->solve();
            command::AlignmentEditCommand::push(m_doc, before, m_doc->toJson(), actionText);
        } else if (m_tcl) {
            // ── 尚未求解（例如 ALD 直接匯入、尚未進行任何幾何編輯）：
            //    沒有 EditableElement/solver 可用，直接平移 TCL 的原始點位。
            QVector<AlignmentPoint> raw = m_tcl->horizontal()->rawPoints();
            if (!raw.isEmpty()) {
                if (col == kColChainage) {
                    const double delta = value - raw.first().chainage;
                    for (AlignmentPoint& pt : raw) pt.chainage += delta;
                } else {
                    const double delta = value - raw.first().contChainage;
                    for (AlignmentPoint& pt : raw) pt.contChainage += delta;
                }
                m_tcl->loadHorizontal(raw);
            }
        }

        populateHorizontalTable();
        Q_EMIT dataCommitted();
        return;
    }

    // ── 幾何欄位（長度／半徑）：需對應 EditableElement ───────────────────────
    if (meta.elemIdx < 0) return;

    bool ok = false;
    const double value = item->text().toDouble(&ok);
    if (!ok || value <= 0.0) {
        populateHorizontalTable();
        return;
    }

    const QJsonObject before = m_doc->toJson();
    QString actionText;

    if (col == kColLength && meta.editLen) {
        const int lenIdx = (meta.lenElemIdx >= 0) ? meta.lenElemIdx : meta.elemIdx;
        m_doc->horizontal()->setLength(lenIdx, value);
        actionText = tr("編輯緩和曲線長度");
    } else if (col == kColRadiusCurveType && meta.editRad) {
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
//  Auxiliary field sync — dialog cache → TCL persisted horizontal alignment
// ============================================================================

void AlignmentDataTableDialog::pushAuxToTcl(const QVector<AlignmentPoint>& displayPts)
{
    if (!m_tcl) return;

    // 取得 TCL 目前實際持有的水平線形原始點清單（尚未經過本對話框的重複點
    // 過濾），依里程比對寫回輔助欄位，確保存檔／匯出讀到使用者最新編輯值。
    //
    // 背景：UIManager 在 HorizontalAlignmentEdit::changed()（即每次 solve()）
    // 時會用 solver 結果整批覆寫 m_tcl->horizontal()（見 UIManager.cpp），
    // 因此每次幾何編輯後 tcl 上的輔助欄位都會被重置為預設值；本函式在
    // populateHorizontalTable() 結尾固定呼叫一次，把 m_hAux 快取的使用者
    // 輸入「補回去」，避免與 UIManager 的同步時序互相覆蓋。
    QVector<AlignmentPoint> tclPts = m_tcl->horizontal()->rawPoints();
    if (tclPts.isEmpty() || displayPts.size() > tclPts.size())
        return;  // 尚無對應的 TCL 資料（例如僅在 solver 端編輯過、尚未提交）

    bool anyChanged = false;
    for (int i = 0; i < displayPts.size(); ++i) {
        const AlignmentPoint& src = displayPts[i];
        int bestIdx = -1;
        double bestDelta = 1.0;  // 里程容許誤差 [m]
        for (int j = 0; j < tclPts.size(); ++j) {
            const double d = std::abs(tclPts[j].chainage - src.chainage);
            if (d < bestDelta) { bestDelta = d; bestIdx = j; }
        }
        if (bestIdx < 0) continue;

        AlignmentPoint& dst = tclPts[bestIdx];
        if (dst.circularCurveNo != src.circularCurveNo ||
            dst.cant            != src.cant            ||
            dst.gaugeWidening    != src.gaugeWidening    ||
            dst.speedLimit       != src.speedLimit       ||
            dst.text1            != src.text1            ||
            dst.text2            != src.text2            ||
            dst.real1            != src.real1            ||
            dst.real2            != src.real2) {
            dst.circularCurveNo = src.circularCurveNo;
            dst.cant            = src.cant;
            dst.gaugeWidening   = src.gaugeWidening;
            dst.speedLimit      = src.speedLimit;
            dst.text1           = src.text1;
            dst.text2           = src.text2;
            dst.real1           = src.real1;
            dst.real2           = src.real2;
            anyChanged = true;
        }
    }

    if (anyChanged)
        m_tcl->loadHorizontal(tclPts);
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
