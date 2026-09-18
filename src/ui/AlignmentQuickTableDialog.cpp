/**
 * @file AlignmentQuickTableDialog.cpp
 * @brief Implementation of AlignmentQuickTableDialog — see the header for the
 *        overall design and usage flow.
 */
#include "AlignmentQuickTableDialog.h"
#include "AlignmentDataTableDialog.h"   // azimuthToDMS()（共用格式化邏輯）
#include "HAlignTableWidget.h"

#include "railway/AlignmentQuickCalc.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QApplication>
#include <QRegularExpression>
#include <QtMath>
#include <cmath>

using namespace aicad::railway;

namespace aicad {
namespace ui {

namespace {

enum Col {
    kColTsc = 0,
    kColE,
    kColN,
    kColChainage,
    kColContChainage,
    kColAzimuth,
    kColLength,             ///< 以下欄位為「本點 → 下一點」之間的線元資訊（下移半格顯示）
    kColRadiusCurveType,
    kColCurveNo,
    kColCant,
    kColGaugeWidening,
    kColSpeedLimit,
    kColText1,
    kColText2,
    kColReal1,
    kColReal2,
    kColCount
};

constexpr int kFirstBetweenCol = kColLength;

/// 已知的緩和曲線類型 token（與 AlignmentPoint::curveType／spiralTypeName()
/// 慣例一致），供「曲線類型/半徑」欄輸入非數字文字時的合法性提示，以及
/// RadiusCurveTypeDelegate 下拉選單使用。
const QStringList kKnownSpiralTokens = {
    QStringLiteral("SPIRAL"), QStringLiteral("HALFSINE"), QStringLiteral("PARABOLA"),
    QStringLiteral("CUBICJPN"), QStringLiteral("CUBICECI"), QStringLiteral("SINUSOIDAL"),
    QStringLiteral("COSINE"), QStringLiteral("BLOSS"), QStringLiteral("LEMNISCATE"),
    QStringLiteral("WIENERBOGEN"), QStringLiteral("RADIOID"), QStringLiteral("ELASRADIOID"),
    QStringLiteral("NORWICHSTURM"), QStringLiteral("PSEUELLRADIOID"), QStringLiteral("LOGARITHMIC"),
    QStringLiteral("HYPERBOLIC"), QStringLiteral("POLYNOMIAL"), QStringLiteral("QUINTIC"),
    QStringLiteral("PHQUINTIC"), QStringLiteral("BIQUADRATIC"), QStringLiteral("SPLINE"),
    QStringLiteral("BLOSSEULERHYBRID")
};

/**
 * @brief 「曲線類型/半徑」合併欄的專用委派：圓弧列可直接輸入半徑數字；
 *        緩和曲線列可從下拉選單選取曲線類型 token（滿足需求 9：curve type
 *        要有選單可以選取），兩者共用同一欄位、同一個可編輯下拉方塊。
 *
 *        繼承 BetweenPointDelegate 取得「點位間」欄位既有的下移半列高
 *        編輯器定位（updateEditorGeometry()）與「不繪製、內容交給
 *        HAlignTableWidget::paintEvent() 處理」（paint()）——只覆寫編輯器
 *        本身的建立／資料存取，不影響顯示與版面配置慣例。
 */
class RadiusCurveTypeDelegate : public BetweenPointDelegate
{
public:
    explicit RadiusCurveTypeDelegate(QObject* parent = nullptr)
        : BetweenPointDelegate(parent)
    {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& /*option*/,
                          const QModelIndex& /*index*/) const override
    {
        auto* combo = new QComboBox(parent);
        combo->setEditable(true);   // 圓弧列仍可直接輸入任意半徑數字
        combo->addItem(QString());  // 空白：切線列或想清空時使用
        combo->addItems(kKnownSpiralTokens);

        // 從下拉選單「選取」一個項目時立即提交＋關閉編輯器，不等待失去
        // 焦點——否則使用者選完曲線類型後，若下一個動作是直接點擊
        // 「計算」／「確定」按鈕，尚未提交的選取值會被忽略，造成「設定過
        // 的曲線類型沒有存好」。QComboBox::activated 只在使用者「主動選取」
        // 時觸發（不含程式化 setCurrentIndex／setEditText），因此不會誤觸發。
        auto* self = const_cast<RadiusCurveTypeDelegate*>(this);
        connect(combo, QOverload<int>::of(&QComboBox::activated), combo,
                [self, combo](int) {
                    Q_EMIT self->commitData(combo);
                    Q_EMIT self->closeEditor(combo);
                });
        return combo;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        if (auto* combo = qobject_cast<QComboBox*>(editor))
            combo->setEditText(index.model()->data(index, Qt::EditRole).toString());
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model,
                       const QModelIndex& index) const override
    {
        if (auto* combo = qobject_cast<QComboBox*>(editor))
            model->setData(index, combo->currentText(), Qt::EditRole);
    }
};

QTableWidgetItem* makeItem(const QString& text, bool editable, bool computedStyle)
{
    auto* item = new QTableWidgetItem(text);
    if (!editable)
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setBackground(QBrush(computedStyle ? QColor(242, 242, 242)
                                              : QColor(255, 255, 210)));
    return item;
}

/// 「曲線類型/半徑」欄的顯示字串：以本點的「離開線元類型」（tsc 第 2
/// 字元）為準決定顯示內容，而不是先看 radius 是否非 0──因為某些來源
/// 資料（例如透過 EditableElement 鏈求解、再攤平回稠密點序列的結果）
/// 可能在緩和曲線列上也帶有非 0 的 radius（該 SCS 群組共用的圓弧半徑），
/// 若先判斷 radius 會誤把緩和曲線列顯示成半徑數字，而不是應該顯示的
/// SPIRAL／HALFSINE... 曲線類型。
///   tsc[1]=='C'（圓弧列）→ 半徑數字。
///   tsc[1]=='S'（緩和曲線列）→ curveType；若來源沒有明確存 curveType，
///                              預設顯示 "SPIRAL"（Clothoid）。
///   其餘（切線列，或 tsc 不完整時的保守預設）→ "STRAIGHT"。
QString radiusCurveTypeToText(const AlignmentPoint& p)
{
    const QChar depart = (p.tsc.size() >= 2) ? p.tsc[1] : QLatin1Char('T');
    if (depart == QLatin1Char('C')) {
        if (std::abs(p.radius) > 1e-9)
            return QString::number(p.radius, 'f', 4);
        return QString();   // 理論上不會發生：圓弧列應該要有半徑
    }
    if (depart == QLatin1Char('S'))
        return p.curveType.isEmpty() ? QStringLiteral("SPIRAL") : p.curveType;
    return QStringLiteral("STRAIGHT");
}

/**
 * @brief 解析起點方位角輸入：支援 "ddd-mm-ss.sss"（度-分-秒，與 ALD 檔案
 *        格式相同的既有慣例，見 AldFileIO::parseAzimuthDMS()）、
 *        azimuthToDMS() 的顯示格式 "ddd°mm'ss.sss""（計算後重新格式化顯示
 *        的樣子，必須能反向解析回來，否則使用者不改任何東西、只是再按一次
 *        「計算」就會失敗）、或原先即支援的十進位度數（例如 "125.5043"）；
 *        成功時輸出徑度（弧度）。度分秒格式的負號（若有）僅寫在最前面的
 *        度數上，例如 "-10-30-15"。
 */
bool parseAzimuthInput(const QString& text, double& outRadians)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return false;

    static const QRegularExpression dashDmsRe(
        QStringLiteral(R"(^(-?\d+)-(\d+)-(\d+(?:\.\d+)?)$)"));
    static const QRegularExpression symbolDmsRe(
        QString::fromUtf8(u8"^(-?\\d+)\u00B0(\\d+)'(\\d+(?:\\.\\d+)?)\"?$"));

    QRegularExpressionMatch m = dashDmsRe.match(t);
    if (!m.hasMatch())
        m = symbolDmsRe.match(t);

    if (m.hasMatch()) {
        const double deg = m.captured(1).toDouble();
        const double minute = m.captured(2).toDouble();
        const double sec = m.captured(3).toDouble();
        const double sign = (deg < 0.0) ? -1.0 : 1.0;
        const double totalDeg = sign * (std::abs(deg) + minute / 60.0 + sec / 3600.0);
        outRadians = qDegreesToRadians(totalDeg);
        return true;
    }

    bool ok = false;
    const double deg = t.toDouble(&ok);
    if (!ok)
        return false;
    outRadians = qDegreesToRadians(deg);
    return true;
}

} // namespace

// ============================================================================
//  Construction / setup
// ============================================================================

AlignmentQuickTableDialog::AlignmentQuickTableDialog(QWidget* parent)
    : QDialog(parent)
{
    buildUi(/*editMode=*/false);

    // 預設兩列：起點（可完整輸入）＋一個切線列，方便使用者直接上手。
    appendRow(QStringLiteral("TT"), /*isFirst=*/true);
    appendRow(QStringLiteral("TT"), /*isFirst=*/false);
}

AlignmentQuickTableDialog::AlignmentQuickTableDialog(
    const QVector<AlignmentPoint>& existingPointsTM2, const QString& existingName,
    QWidget* parent)
    : QDialog(parent)
{
    buildUi(/*editMode=*/true);
    m_nameEdit->setText(existingName);
    m_nameEdit->setEnabled(false);   // 更名請另用重新命名功能，避免與 tclId 對應關係混淆
    setWindowTitle(tr("編輯線形資料表 (AQT) — %1").arg(existingName));

    if (existingPointsTM2.size() >= 2) {
        loadFromPoints(existingPointsTM2);
    } else {
        appendRow(QStringLiteral("TT"), /*isFirst=*/true);
        appendRow(QStringLiteral("TT"), /*isFirst=*/false);
    }
}

void AlignmentQuickTableDialog::buildUi(bool editMode)
{
    setWindowTitle(tr("快速輸入線形資料表 (AQT)"));
    resize(1560, 620);
    setMinimumWidth(1400);

    auto* mainLayout = new QVBoxLayout(this);

    // ── 名稱 ──────────────────────────────────────────────────────────────
    auto* form = new QFormLayout();
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("新線路名稱（留空則自動命名）"));
    form->addRow(tr("線路名稱："), m_nameEdit);
    mainLayout->addLayout(form);

    // ── 說明 ──────────────────────────────────────────────────────────────
    auto* hint = new QLabel(
        editMode
            ? tr("已載入既有線形的關鍵點資料（TM2 座標）。可直接修改任一列的點位代碼"
                 "（TSC）、長度、半徑或緩和曲線類型（同一欄，數字＝半徑／文字＝類型，"
                 "也可從下拉選單選取）、輔助欄位，或用「新增列」「刪除末列」調整點數。"
                 "起點方位角可輸入十進位度數或 ddd-mm-ss.sss（度-分-秒）格式。按「計算」"
                 "正算補齊起點以外各列的座標／里程／方位角，確認無誤後按「確定」寫回"
                 "這條線路。")
            : tr("僅需輸入第一列（起點）的完整座標（TM2）／里程／方位角，以及各列的點位"
                 "代碼（TSC，例如 TT/TC/CC/SC/TS/SS/CS/CT/ST）、長度、半徑或緩和曲線類型"
                 "（同一欄：可解析為數字者視為圓弧半徑，否則視為緩和曲線類型 token，也可"
                 "從下拉選單選取）。起點方位角可輸入十進位度數（例如 125.5043）或"
                 "ddd-mm-ss.sss 度-分-秒格式（例如 125-30-15.5）。按「計算」正算補齊其餘"
                 "座標／里程／方位角，確認無誤後按「確定」建立新的軌道中心線。"),
        this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    // ── 表格 ──────────────────────────────────────────────────────────────
    m_table = new HAlignTableWidget(this);
    m_table->setColumnCount(kColCount);
    m_table->setHorizontalHeaderLabels({
        tr("點位代碼"), tr("東座標 E (TM2)"), tr("北座標 N (TM2)"),
        tr("里程"), tr("連續里程"), tr("方位角(起點:十進位度或ddd-mm-ss.sss)"),
        tr("長度"), tr("曲線類型/半徑"),
        tr("圓曲線編號"), tr("超高(mm)"), tr("軌距加寬(mm)"), tr("速限(km/h)"),
        tr("備註一"), tr("備註二"), tr("數值一"), tr("數值二")
    });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked |
                              QAbstractItemView::EditKeyPressed);
    m_table->setAlternatingRowColors(true);
    m_table->setFirstBetweenColumn(kFirstBetweenCol);

    if (auto* hdrItem = m_table->horizontalHeaderItem(kColRadiusCurveType)) {
        hdrItem->setToolTip(
            tr("圓弧列：輸入帶號半徑數字。\n緩和曲線列：輸入曲線類型，例如：\n%1")
                .arg(kKnownSpiralTokens.join(QStringLiteral(", "))));
    }

    // length（含）以後的欄位代表「點位間」的線元資訊，整體下移半列高顯示，
    // 呼應 HAlignTableWidget::paintEvent() 手動繪製的下移格線。
    auto* betweenDelegate = new BetweenPointDelegate(m_table);
    for (int c = kFirstBetweenCol; c < kColCount; ++c)
        m_table->setItemDelegateForColumn(c, betweenDelegate);
    // 「曲線類型/半徑」欄額外套用可下拉選取緩和曲線類型的專用委派（見
    // RadiusCurveTypeDelegate），取代一般的 BetweenPointDelegate。
    m_table->setItemDelegateForColumn(kColRadiusCurveType,
                                       new RadiusCurveTypeDelegate(m_table));

    connect(m_table, &QTableWidget::itemChanged,
            this, &AlignmentQuickTableDialog::onCellChanged);
    mainLayout->addWidget(m_table);

    // ── 列操作／計算 ─────────────────────────────────────────────────────
    auto* rowBar = new QHBoxLayout();
    auto* addBtn  = new QPushButton(tr("新增列"), this);
    auto* delBtn  = new QPushButton(tr("刪除末列"), this);
    auto* calcBtn = new QPushButton(tr("計算"), this);
    connect(addBtn,  &QPushButton::clicked, this, &AlignmentQuickTableDialog::onAddRow);
    connect(delBtn,  &QPushButton::clicked, this, &AlignmentQuickTableDialog::onRemoveRow);
    connect(calcBtn, &QPushButton::clicked, this, &AlignmentQuickTableDialog::onCalculate);
    rowBar->addWidget(addBtn);
    rowBar->addWidget(delBtn);
    rowBar->addWidget(calcBtn);
    rowBar->addStretch(1);
    rowBar->addWidget(new QLabel(tr("（表格支援 Ctrl+C/X/V 複製剪下貼上、Delete 清除）"), this));
    mainLayout->addLayout(rowBar);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    // ── 確定／取消 ───────────────────────────────────────────────────────
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setEnabled(false);   // 需先按「計算」成功一次才能確定
    connect(buttons, &QDialogButtonBox::accepted, this, &AlignmentQuickTableDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

// ============================================================================
//  Row management
// ============================================================================

void AlignmentQuickTableDialog::appendRow(const QString& tsc, bool isFirst)
{
    m_populating = true;

    const int row = m_table->rowCount();
    m_table->insertRow(row);

    m_table->setItem(row, kColTsc, makeItem(tsc, true, false));

    // 起點列可完整輸入座標（TM2）／里程／方位角；其餘列由「計算」填入，先顯示 0。
    m_table->setItem(row, kColE,            makeItem(QStringLiteral("0"), isFirst, !isFirst));
    m_table->setItem(row, kColN,            makeItem(QStringLiteral("0"), isFirst, !isFirst));
    m_table->setItem(row, kColChainage,     makeItem(QStringLiteral("0"), isFirst, !isFirst));
    m_table->setItem(row, kColContChainage, makeItem(QStringLiteral("0"), isFirst, !isFirst));
    m_table->setItem(row, kColAzimuth,      makeItem(QStringLiteral("0"), isFirst, !isFirst));

    m_table->setItem(row, kColLength,           makeItem(QString(), true, false));
    m_table->setItem(row, kColRadiusCurveType,  makeItem(QString(), true, false));
    m_table->setItem(row, kColCurveNo,          makeItem(QString(), true, false));
    m_table->setItem(row, kColCant,             makeItem(QStringLiteral("0"), true, false));
    m_table->setItem(row, kColGaugeWidening,    makeItem(QStringLiteral("0"), true, false));
    m_table->setItem(row, kColSpeedLimit,       makeItem(QStringLiteral("0"), true, false));
    m_table->setItem(row, kColText1,            makeItem(QString(), true, false));
    m_table->setItem(row, kColText2,            makeItem(QString(), true, false));
    m_table->setItem(row, kColReal1,            makeItem(QStringLiteral("0"), true, false));
    m_table->setItem(row, kColReal2,            makeItem(QStringLiteral("0"), true, false));

    m_populating = false;
}

void AlignmentQuickTableDialog::loadFromPoints(const QVector<AlignmentPoint>& pts)
{
    m_populating = true;
    for (int row = 0; row < pts.size(); ++row) {
        const AlignmentPoint& p = pts[row];
        const bool isFirst = (row == 0);
        appendRow(p.tsc, isFirst);
        m_populating = true;   // appendRow() 內部結尾會清 false，這裡重新鎖住到函式結束

        m_table->item(row, kColE)->setText(QString::number(p.easting, 'f', 4));
        m_table->item(row, kColN)->setText(QString::number(p.northing, 'f', 4));
        m_table->item(row, kColChainage)->setText(QString::number(p.chainage, 'f', 4));
        m_table->item(row, kColContChainage)->setText(QString::number(p.contChainage, 'f', 4));
        m_table->item(row, kColAzimuth)->setText(azimuthToDMS(p.azimuth));

        m_table->item(row, kColLength)->setText(
            p.length > 0.0 ? QString::number(p.length, 'f', 4) : QString());
        m_table->item(row, kColRadiusCurveType)->setText(radiusCurveTypeToText(p));
        m_table->item(row, kColCurveNo)->setText(p.circularCurveNo);
        m_table->item(row, kColCant)->setText(QString::number(p.cant, 'f', 3));
        m_table->item(row, kColGaugeWidening)->setText(QString::number(p.gaugeWidening, 'f', 3));
        m_table->item(row, kColSpeedLimit)->setText(QString::number(p.speedLimit, 'f', 3));
        m_table->item(row, kColText1)->setText(p.text1);
        m_table->item(row, kColText2)->setText(p.text2);
        m_table->item(row, kColReal1)->setText(QString::number(p.real1, 'f', 4));
        m_table->item(row, kColReal2)->setText(QString::number(p.real2, 'f', 4));
    }
    m_populating = false;

    // 既有資料本身就是一組合法的完整關鍵點序列，直接視為「已計算完成」，
    // 讓使用者不改任何東西也能直接按「確定」寫回（等同於原樣重新載入）。
    m_computed    = pts;
    m_resultValid = true;
    m_okButton->setEnabled(true);
    m_statusLabel->setText(tr("已載入既有線形，共 %1 個關鍵點。修改後請重新按「計算」。")
                                .arg(pts.size()));
}

void AlignmentQuickTableDialog::onAddRow()
{
    appendRow(QStringLiteral("TT"), false);
    m_resultValid = false;
    m_okButton->setEnabled(false);
    m_statusLabel->clear();
}

void AlignmentQuickTableDialog::onRemoveRow()
{
    if (m_table->rowCount() <= 2) {
        m_statusLabel->setText(tr("至少需保留 2 列（起點 + 至少一個線元）。"));
        return;
    }
    m_table->removeRow(m_table->rowCount() - 1);
    m_resultValid = false;
    m_okButton->setEnabled(false);
    m_statusLabel->clear();
}

void AlignmentQuickTableDialog::onCellChanged(QTableWidgetItem* /*item*/)
{
    if (m_populating)
        return;
    m_resultValid = false;
    if (m_okButton) m_okButton->setEnabled(false);
}

// ============================================================================
//  Calculate
// ============================================================================

QVector<AlignmentPoint> AlignmentQuickTableDialog::collectInputPoints(QString* errorOut) const
{
    QVector<AlignmentPoint> pts;
    const int n = m_table->rowCount();
    if (n < 2) {
        if (errorOut) *errorOut = tr("至少需要 2 列（起點 + 至少一個線元）。");
        return pts;
    }

    for (int row = 0; row < n; ++row) {
        AlignmentPoint pt;
        pt.tsc = m_table->item(row, kColTsc)->text().trimmed().toUpper();

        if (row == 0) {
            bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
            pt.easting      = m_table->item(row, kColE)->text().toDouble(&ok1);
            pt.northing     = m_table->item(row, kColN)->text().toDouble(&ok2);
            pt.chainage     = m_table->item(row, kColChainage)->text().toDouble(&ok3);
            pt.contChainage = m_table->item(row, kColContChainage)->text().toDouble(&ok4);
            const bool ok5 = parseAzimuthInput(m_table->item(row, kColAzimuth)->text(), pt.azimuth);
            if (!(ok1 && ok2 && ok3 && ok4 && ok5)) {
                if (errorOut) {
                    *errorOut = tr("起點列（第 1 列）的座標／里程必須是數字，方位角須為"
                                   "十進位度數或 ddd-mm-ss.sss 格式。");
                }
                return {};
            }
        }

        pt.length = m_table->item(row, kColLength)->text().toDouble();

        // 「曲線類型/半徑」同一欄：可解析為非 0 數字者視為半徑；"STRAIGHT"
        // （見 radiusCurveTypeToText() 的顯示慣例）或空白視為切線（不設定
        // 半徑／曲線類型）；其餘文字視為緩和曲線類型 token（大小寫不拘）。
        const QString rc = m_table->item(row, kColRadiusCurveType)->text().trimmed();
        bool rcIsNumber = false;
        const double rcNum = rc.toDouble(&rcIsNumber);
        if (rcIsNumber) {
            pt.radius = rcNum;
            pt.curveType.clear();
        } else if (rc.isEmpty() || rc.compare(QStringLiteral("STRAIGHT"), Qt::CaseInsensitive) == 0) {
            pt.radius = 0.0;
            pt.curveType.clear();
        } else {
            pt.radius = 0.0;
            pt.curveType = rc.toUpper();
        }

        pt.circularCurveNo = m_table->item(row, kColCurveNo)->text();
        pt.cant            = m_table->item(row, kColCant)->text().toDouble();
        pt.gaugeWidening   = m_table->item(row, kColGaugeWidening)->text().toDouble();
        pt.speedLimit      = m_table->item(row, kColSpeedLimit)->text().toDouble();
        pt.text1           = m_table->item(row, kColText1)->text();
        pt.text2           = m_table->item(row, kColText2)->text();
        pt.real1           = m_table->item(row, kColReal1)->text().toDouble();
        pt.real2           = m_table->item(row, kColReal2)->text().toDouble();

        pts.push_back(pt);
    }
    return pts;
}

void AlignmentQuickTableDialog::onCalculate()
{
    // 若目前有儲存格編輯器仍處於開啟狀態（例如剛從下拉選單選了曲線類型，
    // 或正在輸入數字時游標尚未離開），強制先讓它失去焦點以觸發提交，避免
    // 讀到尚未寫入 model 的舊值——這是「設定過的曲線類型/半徑沒有存好」
    // 的成因之一（另一成因已在 RadiusCurveTypeDelegate::createEditor() 用
    // activated 訊號立即提交解決；這裡是額外的防禦性保險，涵蓋其餘欄位
    // 及直接輸入文字後未按 Enter 就點擊「計算」的情形）。
    if (QWidget* fw = QApplication::focusWidget()) {
        if (m_table->isAncestorOf(fw))
            fw->clearFocus();
    }

    QString err;
    QVector<AlignmentPoint> pts = collectInputPoints(&err);
    if (pts.isEmpty()) {
        m_statusLabel->setText(err.isEmpty() ? tr("請至少輸入 2 列資料。") : err);
        m_resultValid = false;
        m_okButton->setEnabled(false);
        return;
    }

    QString calcErr;
    if (!computeQuickAlignmentTable(pts, &calcErr)) {
        m_statusLabel->setText(calcErr);
        m_resultValid = false;
        m_okButton->setEnabled(false);
        return;
    }

    m_computed    = pts;
    m_resultValid = true;
    m_okButton->setEnabled(true);
    m_statusLabel->setText(tr("計算完成，共 %1 個關鍵點。可按「確定」寫回。").arg(pts.size()));
    showComputedResults(pts);
}

void AlignmentQuickTableDialog::showComputedResults(const QVector<AlignmentPoint>& pts)
{
    m_populating = true;
    // 起點列（row 0）的方位角輸入格式較自由（十進位度數或
    // ddd-mm-ss.sss，見 parseAzimuthInput()）；計算成功後統一重新格式化
    // 為與其餘列一致的度分秒顯示，方便使用者確認實際解析結果（需求 16）。
    if (!pts.isEmpty() && m_table->rowCount() > 0)
        m_table->item(0, kColAzimuth)->setText(azimuthToDMS(pts[0].azimuth));
    for (int row = 1; row < pts.size() && row < m_table->rowCount(); ++row) {
        const AlignmentPoint& p = pts[row];
        m_table->item(row, kColE)->setText(QString::number(p.easting, 'f', 4));
        m_table->item(row, kColN)->setText(QString::number(p.northing, 'f', 4));
        m_table->item(row, kColChainage)->setText(QString::number(p.chainage, 'f', 4));
        m_table->item(row, kColContChainage)->setText(QString::number(p.contChainage, 'f', 4));
        m_table->item(row, kColAzimuth)->setText(azimuthToDMS(p.azimuth));
    }
    m_populating = false;
}

// ============================================================================
//  Accept / query
// ============================================================================

QString AlignmentQuickTableDialog::trackName() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

void AlignmentQuickTableDialog::onAccept()
{
    if (!m_resultValid || m_computed.size() < 2) {
        m_statusLabel->setText(tr("請先按「計算」成功一次，且計算後不可再修改輸入欄位。"));
        return;
    }
    accept();
}

} // namespace ui
} // namespace aicad
