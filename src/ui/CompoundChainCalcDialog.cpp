#include "ui/CompoundChainCalcDialog.h"

#include "railway/AlignmentDocument.h"
#include "railway/AlignmentSolver.h"
#include "core/geometry/ProjectOrigin.h"
#include "ui/AlignmentDataTableDialog.h"   // azimuthToDMS()

#include <QComboBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QtMath>

using aicad::railway::AlignmentDocument;
using aicad::railway::HorizontalAlignmentEdit;
using aicad::railway::EditableElementType;
using aicad::railway::SpiralType;
using aicad::railway::AlignmentSolver;
using aicad::railway::SolvedCompoundChain;

namespace aicad {
namespace ui {

namespace {
constexpr int kMinArcs = 2;
constexpr int kMaxArcs = 12;   ///< 表格 UI 的合理上限，非 solver 限制

// ── QSettings 持久化：比照 VBA GetSetting/SaveSetting 的用法，記錄「上次
//    執行後的選取及輸入資料」，下次開啟對話框時帶入（見標頭檔 done() 上方
//    的說明、loadSettings()/saveSettings()）。沿用既有 ImportAlignmentCommand
//    / BasicCommands 已在用的 "AICAD"/"AICAD" org/app 名稱，同一個 .ini
//    （或平台對應的設定儲存位置）底下用獨立的 group 隔開，不會互相污染。
constexpr const char* kSettingsOrg   = "AICAD";
constexpr const char* kSettingsApp   = "AICAD";
constexpr const char* kSettingsGroup = "CompoundChainCalcDialog";
constexpr const char* kKeySpiralType = "spiralType";
constexpr const char* kKeyArcCount   = "arcCount";
constexpr const char* kKeyEntryIdx   = "entryTangentIdx";
constexpr const char* kKeyExitIdx    = "exitTangentIdx";
constexpr const char* kKeyLens       = "lens";      ///< 各段緩和曲線長度 Lk，N+1 個
constexpr const char* kKeyRadii      = "radii";     ///< 各段圓弧半徑 Rk，N 個
constexpr const char* kKeyArcLens    = "arcLens";   ///< 各段圓弧弧長 Dk，N 個（最後一段是 "Auto"，還原時略過）

/** 欄位順序：Segment(標籤) / Lk / Rk / 弧長 Dk（弧長=圓弧弧長，非直徑）。 */
enum InputColumn { ColLabel = 0, ColSpiralLen = 1, ColArcRadius = 2, ColArcLen = 3 };

/** 與 AlignmentSCSCommand.cpp / AlignmentAddSpiralCommand.cpp 一致的顯示名稱。 */
QString spiralTypeDisplayName(SpiralType t)
{
    switch (t) {
    case SpiralType::HalfSine:         return QStringLiteral("HalfSine");
    case SpiralType::Parabola:         return QStringLiteral("Parabola");
    case SpiralType::CubicJPN:         return QStringLiteral("CubicJPN");
    case SpiralType::CubicECI:         return QStringLiteral("CubicECI");
    case SpiralType::Sinusoidal:       return QStringLiteral("Sinusoidal");
    case SpiralType::Cosine:           return QStringLiteral("Cosine");
    case SpiralType::Bloss:            return QStringLiteral("Bloss");
    case SpiralType::Lemniscate:       return QStringLiteral("Lemniscate");
    case SpiralType::WienerBogen:      return QStringLiteral("WienerBogen");
    case SpiralType::Radioid:          return QStringLiteral("Radioid");
    case SpiralType::ElasticRadioid:   return QStringLiteral("ElasticRadioid");
    case SpiralType::NorwichSturm:     return QStringLiteral("NorwichSturm");
    case SpiralType::PseudoEllipticRadioid: return QStringLiteral("PseudoEllipticRadioid");
    case SpiralType::Logarithmic:      return QStringLiteral("Logarithmic");
    case SpiralType::Hyperbolic:       return QStringLiteral("Hyperbolic");
    case SpiralType::Polynomial:       return QStringLiteral("Polynomial");
    case SpiralType::Quintic:          return QStringLiteral("Quintic");
    case SpiralType::PHQuintic:        return QStringLiteral("PHQuintic");
    case SpiralType::Biquadratic:      return QStringLiteral("Biquadratic");
    case SpiralType::Spline:           return QStringLiteral("Spline");
    case SpiralType::BlossEulerHybrid: return QStringLiteral("BlossEulerHybrid");
    default:                           return QStringLiteral("Clothoid");
    }
}
}

CompoundChainCalcDialog::CompoundChainCalcDialog(AlignmentDocument* doc, QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
{
    init();
    // 未鎖定切線的情況：init() 當下 m_tangentsLocked 還是 false，這裡呼叫
    // loadSettings() 才能正確還原上次的入/出切線選取（見 loadSettings()
    // 內的 m_tangentsLocked 判斷、標頭檔 done() 上方的說明）。
    loadSettings();
}

CompoundChainCalcDialog::CompoundChainCalcDialog(AlignmentDocument* doc,
                                                  int presetEntryIdx, int presetExitIdx,
                                                  QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
{
    init();
    lockTangentCombos(presetEntryIdx, presetExitIdx);
    // ⚠️ 順序很重要：必須在 lockTangentCombos() 之後才呼叫 loadSettings()，
    // 讓 m_tangentsLocked 在 loadSettings() 內判斷時已經是 true，這樣才會
    // 正確跳過「還原切線選取」——切線已由呼叫端（AlignmentSCSChainCommand）
    // 依畫面點選鎖定，不該被上次的記錄覆蓋掉。若順序顛倒，loadSettings()
    // 執行當下看到的 m_tangentsLocked 會還是預設的 false，就會誤把上次
    // 記錄的切線 index 寫回下拉選單（即使隨後馬上被 lockTangentCombos()
    // 蓋掉、視覺上看不出異狀，但仍是邏輯錯誤，一旦重排呼叫順序就會露餡）。
    loadSettings();
}

// ────────────────────────────────────────────────────────────────────────────
//  init — 兩個建構子共用的 UI 建構
// ────────────────────────────────────────────────────────────────────────────

void CompoundChainCalcDialog::init()
{
    setWindowTitle(tr("Compound Chain Trial Calculation (S0 C0 S1 C1 ... Sn)"));
    resize(760, 600);

    auto* mainLayout = new QVBoxLayout(this);

    // ── 邊界切線選擇／螺線形式 ────────────────────────────────────────────
    auto* form = new QFormLayout();
    m_entryTangentCombo = new QComboBox(this);
    m_exitTangentCombo  = new QComboBox(this);
    form->addRow(tr("Entry tangent:"), m_entryTangentCombo);
    form->addRow(tr("Exit tangent:"),  m_exitTangentCombo);

    m_spiralTypeCombo = new QComboBox(this);
    for (SpiralType t : { SpiralType::Clothoid, SpiralType::HalfSine, SpiralType::Parabola,
                          SpiralType::CubicJPN, SpiralType::CubicECI,
                          SpiralType::Sinusoidal, SpiralType::Cosine, SpiralType::Bloss,
                          SpiralType::Lemniscate, SpiralType::WienerBogen, SpiralType::Radioid,
                          SpiralType::ElasticRadioid, SpiralType::NorwichSturm, SpiralType::PseudoEllipticRadioid,
                          SpiralType::Logarithmic, SpiralType::Hyperbolic, SpiralType::Polynomial,
                          SpiralType::Quintic, SpiralType::PHQuintic, SpiralType::Biquadratic, SpiralType::Spline,
                          SpiralType::BlossEulerHybrid })
        m_spiralTypeCombo->addItem(spiralTypeDisplayName(t), static_cast<int>(t));
    form->addRow(tr("Spiral form (all segments):"), m_spiralTypeCombo);

    m_arcCountSpin = new QSpinBox(this);
    m_arcCountSpin->setRange(kMinArcs, kMaxArcs);
    m_arcCountSpin->setValue(kMinArcs);
    form->addRow(tr("Arc count N (>=2):"), m_arcCountSpin);
    mainLayout->addLayout(form);

    // ── 輸入表格：N+1 列，欄位 [Lk][Rk][弧長 Dk]（最後一列 Rk/Dk 留空；───
    //    每段圓弧的最後一段 Dk 欄鎖定顯示 Auto，見 rebuildInputTable()） ──
    m_inputTable = new QTableWidget(this);
    m_inputTable->setColumnCount(4);
    m_inputTable->setHorizontalHeaderLabels(
        { tr("Segment"), tr("Spiral length Lk (0=omit)"), tr("Arc radius Rk"), tr("Arc length Dk") });
    m_inputTable->horizontalHeader()->setStretchLastSection(true);
    mainLayout->addWidget(new QLabel(tr("Chain parameters:"), this));
    mainLayout->addWidget(m_inputTable);

    // ── 按鈕列 ───────────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    m_calcButton  = new QPushButton(tr("Calculate"), this);
    m_applyButton = new QPushButton(tr("Apply"), this);
    m_applyButton->setEnabled(false);
    btnRow->addWidget(m_calcButton);
    btnRow->addWidget(m_applyButton);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    // ── 狀態列 ───────────────────────────────────────────────────────────
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    // ── 試算結果表格 ─────────────────────────────────────────────────────
    m_resultTable = new QTableWidget(this);
    m_resultTable->setColumnCount(6);
    m_resultTable->setHorizontalHeaderLabels(
        { tr("Point"), tr("Easting"), tr("Northing"), tr("Azimuth"), tr("Next seg. length"), tr("Radius") });
    m_resultTable->horizontalHeader()->setStretchLastSection(true);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(new QLabel(tr("Trial result (node sequence):"), this));
    mainLayout->addWidget(m_resultTable);

    connect(m_arcCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CompoundChainCalcDialog::onArcCountChanged);
    connect(m_calcButton,  &QPushButton::clicked, this, &CompoundChainCalcDialog::onCalculate);
    connect(m_applyButton, &QPushButton::clicked, this, &CompoundChainCalcDialog::onApply);

    populateTangentCombos();
    rebuildInputTable();
}

// ────────────────────────────────────────────────────────────────────────────
//  populateTangentCombos / lockTangentCombos
// ────────────────────────────────────────────────────────────────────────────

void CompoundChainCalcDialog::populateTangentCombos()
{
    m_entryTangentCombo->clear();
    m_exitTangentCombo->clear();
    if (!m_doc || !m_doc->horizontal()) return;

    const auto& elems = m_doc->horizontal()->elements();
    for (int i = 0; i < elems.size(); ++i) {
        if (elems[i].type != EditableElementType::Tangent) continue;
        const QString label = tr("Tangent #%1").arg(i);
        m_entryTangentCombo->addItem(label, i);
        m_exitTangentCombo->addItem(label, i);
    }
    if (m_exitTangentCombo->count() > 1)
        m_exitTangentCombo->setCurrentIndex(1);
}

void CompoundChainCalcDialog::lockTangentCombos(int entryIdx, int exitIdx)
{
    const int entryPos = m_entryTangentCombo->findData(entryIdx);
    const int exitPos  = m_exitTangentCombo->findData(exitIdx);
    if (entryPos >= 0) m_entryTangentCombo->setCurrentIndex(entryPos);
    if (exitPos  >= 0) m_exitTangentCombo->setCurrentIndex(exitPos);

    // 鎖定：使用者已經在畫面上點選過切線，這裡只是唯讀顯示，避免與畫面
    // 上的高亮切線選取不一致。
    m_entryTangentCombo->setEnabled(false);
    m_exitTangentCombo->setEnabled(false);
    m_tangentsLocked = true;
}

// ────────────────────────────────────────────────────────────────────────────
//  onArcCountChanged / rebuildInputTable
// ────────────────────────────────────────────────────────────────────────────

void CompoundChainCalcDialog::onArcCountChanged(int)
{
    rebuildInputTable();
    m_lastCalcValid = false;
    m_applyButton->setEnabled(false);
}

void CompoundChainCalcDialog::rebuildInputTable()
{
    const int n = m_arcCountSpin->value();

    // 「—」是 row==n（末端切線，沒有圓弧/緩和曲線）那一列的固定佔位符號，
    // 不是使用者輸入的資料，不該被寫進快取。
    static const QString kNotApplicable = QStringLiteral("\u2014");

    // ── 快取目前表格內容（見標頭檔 m_cachedLens 等成員的說明）──────────
    // 不管這次 N 是變大還是變小，重建表格前都先把「目前還存在的列」依
    // 列索引寫回快取——快取本身只增不減，縮小 N 時，即將被砍掉的那些列
    // 的值不會真的消失，而是留在快取裡；N 之後再放大、同一個列索引重新
    // 出現時，下面會優先從快取取值，而不是每次都填回 0／留白。
    const int oldRowCount = m_inputTable->rowCount();
    if (m_cachedLens.size() < oldRowCount)
        m_cachedLens.resize(oldRowCount);
    if (m_cachedRadii.size() < oldRowCount)
        m_cachedRadii.resize(oldRowCount);
    if (m_cachedArcLens.size() < oldRowCount)
        m_cachedArcLens.resize(oldRowCount);
    for (int row = 0; row < oldRowCount; ++row) {
        if (auto* item = m_inputTable->item(row, ColSpiralLen)) {
            const QString t = item->text();
            if (!t.isEmpty() && t != kNotApplicable) m_cachedLens[row] = t;
        }
        if (auto* item = m_inputTable->item(row, ColArcRadius)) {
            const QString t = item->text();
            if (!t.isEmpty() && t != kNotApplicable) m_cachedRadii[row] = t;
        }
        if (auto* item = m_inputTable->item(row, ColArcLen)) {
            const QString t = item->text();
            // "Auto" 是鎖定列的顯示文字，不是使用者輸入值，不寫入快取
            // ——否則這一列如果之後又變回「非自動」列，會被誤填成字面
            // 上的 "Auto" 字串。
            if (!t.isEmpty() && t != kNotApplicable && t != tr("Auto"))
                m_cachedArcLens[row] = t;
        }
    }

    m_inputTable->setRowCount(n + 1);
    if (m_cachedLens.size() < n + 1)
        m_cachedLens.resize(n + 1);
    if (m_cachedRadii.size() < n)
        m_cachedRadii.resize(n);
    if (m_cachedArcLens.size() < n)
        m_cachedArcLens.resize(n);

    // 最後一段圓弧（arc index n-1，對應 row n-1）弧長由 solver 自動算出，
    // 使用者不需輸入（見標頭檔說明）。
    const int autoArcRow = n - 1;

    for (int row = 0; row <= n; ++row) {
        const QString label = (row == 0) ? tr("S0 (entry)")
            : (row == n) ? tr("S%1 (exit)").arg(row)
            : tr("S%1 (interior)").arg(row);
        m_inputTable->setItem(row, ColLabel, new QTableWidgetItem(label));
        m_inputTable->item(row, ColLabel)->setFlags(Qt::ItemIsEnabled);

        const QString lenText = !m_cachedLens[row].isEmpty() ? m_cachedLens[row]
                                                               : QStringLiteral("0");
        auto* lenItem = new QTableWidgetItem(lenText);
        m_inputTable->setItem(row, ColSpiralLen, lenItem);

        if (row < n) {
            const QString radText = !m_cachedRadii[row].isEmpty() ? m_cachedRadii[row]
                                                                    : QStringLiteral("0");
            auto* radItem = new QTableWidgetItem(radText);
            m_inputTable->setItem(row, ColArcRadius, radItem);

            if (row == autoArcRow) {
                // 這一列此刻是「自動算出」的那一段，不管快取裡同一列
                // 索引原本存的是不是輸入值（可能是因為 N 變小、原本可
                // 編輯的一列現在移到了自動列的位置），都強制顯示 Auto、
                // 鎖定不可編輯——快取本身不受影響（上面的擷取邏輯本來
                // 就不會把 "Auto" 這個顯示文字寫進快取），N 之後放大、
                // 這一列不再是自動列時，原本的數值仍然找得回來。
                auto* autoItem = new QTableWidgetItem(tr("Auto"));
                autoItem->setFlags(Qt::ItemIsEnabled);
                m_inputTable->setItem(row, ColArcLen, autoItem);
            } else {
                auto* lenArcItem = new QTableWidgetItem(m_cachedArcLens[row]);
                m_inputTable->setItem(row, ColArcLen, lenArcItem);
            }
        } else {
            auto* naItem = new QTableWidgetItem(kNotApplicable);
            naItem->setFlags(Qt::ItemIsEnabled);
            m_inputTable->setItem(row, ColArcRadius, naItem);

            auto* naItem2 = new QTableWidgetItem(kNotApplicable);
            naItem2->setFlags(Qt::ItemIsEnabled);
            m_inputTable->setItem(row, ColArcLen, naItem2);
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  readInputs
// ────────────────────────────────────────────────────────────────────────────

bool CompoundChainCalcDialog::readInputs(QVector<double>& outRadii, QVector<double>& outLens,
                                          QVector<double>& outArcAngles) const
{
    const int n = m_arcCountSpin->value();
    outLens.resize(n + 1);
    outRadii.resize(n);
    outArcAngles.fill(0.0, n);   // 0 = 交給 solver 自動算出（預設全部；下面依輸入覆寫前 n-1 段）

    const int autoArcRow = n - 1;   // 最後一段圓弧：弧長固定 Auto，不讀取此欄

    for (int row = 0; row <= n; ++row) {
        bool ok = false;
        const double len = m_inputTable->item(row, ColSpiralLen)->text().toDouble(&ok);
        if (!ok || len < 0.0) {
            m_statusLabel->setText(tr("Invalid spiral length at row %1 (must be >= 0).").arg(row));
            return false;
        }
        outLens[row] = len;

        if (row < n) {
            bool okR = false;
            const double r = m_inputTable->item(row, ColArcRadius)->text().toDouble(&okR);
            if (!okR || r <= 0.0) {
                m_statusLabel->setText(tr("Invalid arc radius at row %1 (must be > 0).").arg(row));
                return false;
            }
            outRadii[row] = r;

            // 最後一段圓弧（autoArcRow）弧長不讀取，維持 outArcAngles[row]==0.0
            // （由 solveCompoundChain() 用剩餘轉角自動算出）。
            if (row != autoArcRow) {
                bool okD = false;
                const double d = m_inputTable->item(row, ColArcLen)->text().toDouble(&okD);
                if (!okD || d <= 0.0) {
                    m_statusLabel->setText(
                        tr("Invalid arc length at row %1 (must be > 0; only the LAST arc's"
                           " length is automatic).").arg(row));
                    return false;
                }
                // 弧長 → 弧心角絕對值：angle = length / radius（見
                // AlignmentDocument.h CompoundChainSpec::ArcSeg::centralAngle）。
                outArcAngles[row] = d / r;
            }
        }
    }
    return true;
}

SpiralType CompoundChainCalcDialog::selectedSpiralType() const
{
    return static_cast<SpiralType>(m_spiralTypeCombo->currentData().toInt());
}

// ────────────────────────────────────────────────────────────────────────────
//  onCalculate
// ────────────────────────────────────────────────────────────────────────────

void CompoundChainCalcDialog::onCalculate()
{
    m_lastCalcValid = false;
    m_applyButton->setEnabled(false);
    m_resultTable->setRowCount(0);

    if (!m_doc || !m_doc->horizontal()) {
        m_statusLabel->setText(tr("No AlignmentDocument."));
        return;
    }

    // 確保 ProjectOrigin 已設定（若尚未設定，套用預設 TM2 origin），這樣
    // 下方組節點座標時 toGlobal() 才能得到合理量級的 TM2 座標，而不會因
    // origin 未設定而退化成恆等轉換、把 Local 座標直接當 TM2 顯示（見
    // ProjectOrigin.h、AlignmentDataTableDialog::populateHorizontalTable()
    // 同樣的作法）。
    core::geometry::ProjectOrigin::ensureDefault();

    if (m_entryTangentCombo->currentIndex() < 0 || m_exitTangentCombo->currentIndex() < 0) {
        m_statusLabel->setText(tr("Please select both entry and exit tangents."));
        return;
    }
    const int tb = m_entryTangentCombo->currentData().toInt();
    const int ta = m_exitTangentCombo->currentData().toInt();
    if (tb == ta) {
        m_statusLabel->setText(tr("Entry and exit tangent must be different."));
        return;
    }

    QVector<double> radii, lens, arcAngles;
    if (!readInputs(radii, lens, arcAngles)) return;   // 訊息已由 readInputs 設定

    const auto& elems = m_doc->horizontal()->elements();
    if (tb < 0 || tb >= elems.size() || ta < 0 || ta >= elems.size()) {
        m_statusLabel->setText(tr("Invalid tangent selection."));
        return;
    }

    const int n = m_arcCountSpin->value();
    QVector<SpiralType> types(n + 1, selectedSpiralType());

    // arcAngles：前 n-1 段已由使用者輸入的弧長換算為弧心角（釘死值）；最後
    // 一段固定 0.0 = 交給 solveCompoundChain() 用剩餘轉角自動算出（見
    // readInputs() 說明；unknown 保持預設 None，不啟用反解機制）。
    const SolvedCompoundChain chain = AlignmentSolver::solveCompoundChain(
        radii, lens, types,
        elems[tb].startPI, elems[tb].endPI,
        elems[ta].startPI, elems[ta].endPI,
        arcAngles);

    if (!chain.valid) {
        m_statusLabel->setText(tr("Calculation failed — spirals may overlap or the turning"
                                   " angle is inconsistent with the given radii/lengths."
                                   " Try shorter spirals or check tangent selection."));
        return;
    }

    // ── 顯示節點序列 ─────────────────────────────────────────────────────
    // node.pt 是 Local（CAD 內部）座標，不是 TM2——TM2 大數值只該出現在
    // 顯示文字這個邊界（見 ProjectOrigin.h 架構原則），必須透過
    // ProjectOrigin::toGlobal() 加上 projectOrigin 換回真正的 TM2
    // Easting/Northing 才能顯示；上面 onCalculate() 開頭已呼叫
    // ensureDefault() 確保 origin 一定有合理值可用。方位角一律用
    // azimuthToDMS()（定義在 AlignmentDataTableDialog.cpp、宣告於其
    // 標頭檔）格式化成 ddd°mm'ss.sss"，跟線形資料表對齊，不要各自
    // 手動組字串（原本這裡是自己拼 "度.dddd°"，跟系統其他地方的方位角
    // 顯示格式不一致）。
    m_resultTable->setRowCount(chain.nodes.size());
    for (int i = 0; i < chain.nodes.size(); ++i) {
        const auto& node = chain.nodes[i];
        const QString ptLabel = (i == 0) ? tr("TS")
            : (i == chain.nodes.size() - 1) ? tr("ST")
            : (node.isArcStart ? tr("SC%1").arg(i) : tr("CS%1").arg(i));

        const QPointF tm2 = core::geometry::ProjectOrigin::instance().toGlobal(node.pt);

        m_resultTable->setItem(i, 0, new QTableWidgetItem(ptLabel));
        m_resultTable->setItem(i, 1, new QTableWidgetItem(QString::number(tm2.x(), 'f', 4)));
        m_resultTable->setItem(i, 2, new QTableWidgetItem(QString::number(tm2.y(), 'f', 4)));
        m_resultTable->setItem(i, 3, new QTableWidgetItem(azimuthToDMS(node.az)));
        m_resultTable->setItem(i, 4, new QTableWidgetItem(QString::number(node.segLength, 'f', 3)));
        m_resultTable->setItem(i, 5, new QTableWidgetItem(QString::number(node.radius, 'f', 3)));
    }

    m_statusLabel->setText(tr("Calculation succeeded (%1 nodes). Review the result, then click Apply.")
                               .arg(chain.nodes.size()));
    m_lastCalcValid = true;
    m_applyButton->setEnabled(true);
}

// ────────────────────────────────────────────────────────────────────────────
//  onApply
// ────────────────────────────────────────────────────────────────────────────

void CompoundChainCalcDialog::onApply()
{
    if (!m_lastCalcValid || !m_doc || !m_doc->horizontal()) return;

    const int tb = m_entryTangentCombo->currentData().toInt();
    const int ta = m_exitTangentCombo->currentData().toInt();
    const int n  = m_arcCountSpin->value();

    QVector<double> radii, lens, arcAngles;
    if (!readInputs(radii, lens, arcAngles)) {
        QMessageBox::warning(this, tr("Apply failed"), m_statusLabel->text());
        return;
    }

    const SpiralType spiralType = selectedSpiralType();

    HorizontalAlignmentEdit::CompoundChainSpec spec;
    spec.arcs.resize(n);
    spec.spirals.resize(n + 1);
    for (int k = 0; k < n; ++k) {
        spec.arcs[k].radius = radii[k];
        // arcAngles[k] > 0 表示使用者已輸入該段弧長（釘死角度）；最後一段
        // 保持 0.0，交由 addCompoundChain()/solveCompoundChain() 依剩餘轉角
        // 自動算出（見 AlignmentDocument.h CompoundChainSpec::ArcSeg::
        // centralAngle 的說明：0 = 自動平分「剩餘」轉角——此處只留最後一段
        // 自動，等同把全部轉角分給那一段，而非在多段間平分）。
        spec.arcs[k].centralAngle = arcAngles[k];
    }
    for (int k = 0; k <= n; ++k) {
        spec.spirals[k].length = lens[k];
        spec.spirals[k].type   = spiralType;
    }

    const int idx = m_doc->horizontal()->addCompoundChain(tb, ta, spec);
    if (idx < 0) {
        QMessageBox::warning(this, tr("Apply failed"),
                              tr("addCompoundChain() failed — check tangent selection."));
        return;
    }

    m_doc->horizontal()->solve();
    Q_EMIT chainApplied();
    accept();
}

// ────────────────────────────────────────────────────────────────────────────
//  done / loadSettings / saveSettings — 持久化「上次執行後的選取及輸入
//  資料」，比照 VBA GetSetting/SaveSetting 的用法（見標頭檔說明）。
// ────────────────────────────────────────────────────────────────────────────

void CompoundChainCalcDialog::done(int result)
{
    // accept()（按「套用」成功）、reject()（按取消／Esc／右上角 X）最終都
    // 會流經 QDialog::done()，在這裡存一次設定即可涵蓋所有關閉對話框的
    // 途徑——不論這次有沒有成功套用，都值得記住使用者這次輸入的內容，
    // 下次開啟時可以接著改，而不是每次都要重新輸入一遍。
    saveSettings();
    QDialog::done(result);
}

void CompoundChainCalcDialog::loadSettings()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.beginGroup(QLatin1String(kSettingsGroup));

    if (settings.contains(QLatin1String(kKeySpiralType))) {
        const int t = settings.value(QLatin1String(kKeySpiralType)).toInt();
        const int pos = m_spiralTypeCombo->findData(t);
        if (pos >= 0) m_spiralTypeCombo->setCurrentIndex(pos);
    }

    if (settings.contains(QLatin1String(kKeyArcCount))) {
        const int n = settings.value(QLatin1String(kKeyArcCount)).toInt();
        // setValue() 若真的改變了數值會同步觸發 onArcCountChanged() →
        // rebuildInputTable()，下面才能安全地依還原後的 N 逐列寫回輸入
        // 表格；若剛好跟目前值（spinbox 預設 kMinArcs）相同則不會觸發，
        // 但這種情況下 init() 最後呼叫的 rebuildInputTable() 本來就已經
        // 用同樣的 N 建好表格，效果一樣。
        if (n >= m_arcCountSpin->minimum() && n <= m_arcCountSpin->maximum())
            m_arcCountSpin->setValue(n);
    }

    // 入/出切線：只有在下拉選單「本來就可以自由選擇」時才還原——鎖定的
    // 情況下（由 AlignmentSCSChainCommand 建構）切線已由呼叫端依畫面點選
    // 決定，見標頭檔 done() 上方與兩個建構子內的說明。
    if (!m_tangentsLocked) {
        if (settings.contains(QLatin1String(kKeyEntryIdx))) {
            const int pos = m_entryTangentCombo->findData(
                settings.value(QLatin1String(kKeyEntryIdx)).toInt());
            if (pos >= 0) m_entryTangentCombo->setCurrentIndex(pos);
        }
        if (settings.contains(QLatin1String(kKeyExitIdx))) {
            const int pos = m_exitTangentCombo->findData(
                settings.value(QLatin1String(kKeyExitIdx)).toInt());
            if (pos >= 0) m_exitTangentCombo->setCurrentIndex(pos);
        }
    }

    // 輸入表格：此時 m_inputTable 已經因為上面 arcCount 的還原而是 N+1 列
    // （或本來就是同樣的 N），可以安全地逐列寫回。列數若和儲存當下不同
    // （例如換了文件、上次的 N 超出目前 kMaxArcs 而沒被套用），下面的
    // `row < list.size()` 邊界檢查會讓多出來的儲存值單純被忽略，不會
    // 存取越界或寫壞表格。
    const int n = m_arcCountSpin->value();
    const int autoArcRow = n - 1;   // 最後一段圓弧：弧長鎖定顯示 Auto，不覆寫

    const QStringList lens    = settings.value(QLatin1String(kKeyLens)).toStringList();
    const QStringList radii   = settings.value(QLatin1String(kKeyRadii)).toStringList();
    const QStringList arcLens = settings.value(QLatin1String(kKeyArcLens)).toStringList();

    for (int row = 0; row <= n && row < lens.size(); ++row) {
        if (auto* item = m_inputTable->item(row, ColSpiralLen))
            item->setText(lens[row]);
    }
    for (int row = 0; row < n && row < radii.size(); ++row) {
        if (auto* item = m_inputTable->item(row, ColArcRadius))
            item->setText(radii[row]);
    }
    for (int row = 0; row < n && row < arcLens.size(); ++row) {
        if (row == autoArcRow) continue;   // Auto 列本來就鎖定唯讀，不覆寫
        if (auto* item = m_inputTable->item(row, ColArcLen))
            item->setText(arcLens[row]);
    }

    settings.endGroup();
}

void CompoundChainCalcDialog::saveSettings() const
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.beginGroup(QLatin1String(kSettingsGroup));

    settings.setValue(QLatin1String(kKeySpiralType), static_cast<int>(selectedSpiralType()));

    const int n = m_arcCountSpin->value();
    settings.setValue(QLatin1String(kKeyArcCount), n);

    // 鎖定切線的情況不記錄切線選取，理由同 loadSettings()：切線是呼叫端
    // 依畫面點選決定的，不該被下次開啟時的「上次記錄」覆蓋。
    if (!m_tangentsLocked) {
        if (m_entryTangentCombo->currentIndex() >= 0)
            settings.setValue(QLatin1String(kKeyEntryIdx), m_entryTangentCombo->currentData().toInt());
        if (m_exitTangentCombo->currentIndex() >= 0)
            settings.setValue(QLatin1String(kKeyExitIdx), m_exitTangentCombo->currentData().toInt());
    }

    // 輸入表格：Lk 共 N+1 列；Rk／Dk 共 N 列（Dk 最後一列是鎖定顯示的
    // "Auto"，原樣存回沒有副作用——loadSettings() 還原時會主動跳過那一
    // 列，不會把字串 "Auto" 誤寫進儲存格當成使用者輸入值）。
    QStringList lens, radii, arcLens;
    for (int row = 0; row <= n; ++row) {
        auto* lenItem = m_inputTable->item(row, ColSpiralLen);
        lens << (lenItem ? lenItem->text() : QString());
        if (row < n) {
            auto* radItem = m_inputTable->item(row, ColArcRadius);
            auto* dItem   = m_inputTable->item(row, ColArcLen);
            radii   << (radItem ? radItem->text() : QString());
            arcLens << (dItem   ? dItem->text()   : QString());
        }
    }
    settings.setValue(QLatin1String(kKeyLens),    lens);
    settings.setValue(QLatin1String(kKeyRadii),   radii);
    settings.setValue(QLatin1String(kKeyArcLens), arcLens);

    settings.endGroup();
}

} // namespace ui
} // namespace aicad
