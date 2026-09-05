#include "DimExpressionDialog.h"
#include "../cad/Sketch.h"
#include "../core/ParameterStore.h"
#include "../view/CadView.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFont>
#include <QCloseEvent>
#include <QKeyEvent>

namespace aicad::ui {

DimExpressionDialog::DimExpressionDialog(cad::Sketch* sk,
                                         const QString& constraintUuid,
                                         const QString& autoParamName,
                                         const QString& currentExpr,
                                         view::CadView* cadView,
                                         QWidget* parent)
    : QDialog(parent)
    , m_sketch(sk)
    , m_constraintUuid(constraintUuid)
    , m_cadView(cadView)
{
    // ⚠️ 非模態（不呼叫 setModal(true)/exec()，外部用 show()）——見標頭檔
    // 說明：「插入參考」功能需要 CadView 在對話框開著時仍能收到滑鼠事件。
    setWindowTitle(tr("編輯尺寸運算式"));
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(320);

    auto* mainLayout = new QVBoxLayout(this);

    // ⚠️ 修改：參數名稱標籤改成放在運算式編輯框「旁邊」（同一行、水平排列），
    // 而不是像先前那樣單獨佔一行放在編輯框上方——編輯框本身顯示的是運算式
    // 本身（例如 "width*2+5"），參數名稱（例如 "d3"）只是這個運算式的名字，
    // 放在旁邊當作標籤比較符合「這是誰的運算式」的直覺，也比較省垂直空間。
    auto* exprRow = new QHBoxLayout();
    const QString displayName = autoParamName.isEmpty()
        ? constraintUuid.left(8) : autoParamName;
    m_titleLabel = new QLabel(tr("%1 =").arg(displayName), this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    exprRow->addWidget(m_titleLabel);

    m_exprEdit = new QLineEdit(currentExpr, this);
    m_exprEdit->selectAll();
    exprRow->addWidget(m_exprEdit, /*stretch=*/1);
    mainLayout->addLayout(exprRow);

    // ⚠️ 修改：「插入參考」不再需要按鈕手動開關——對話框一打開就自動
    // 啟用（見建構子尾端 setRefPickModeActive(true)），全程都能直接點選
    // 畫面上其他尺寸標註插入參數名稱，原本的 toggle 按鈕已移除。
    m_hintLabel = new QLabel(
        tr("可直接輸入數字或運算式（例如 d1*2+5），"
           "或直接點選草圖中其他尺寸標註插入其參數名稱。"),
        this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet("color: gray; font-size: 11px;");
    mainLayout->addWidget(m_hintLabel);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_cancelButton = new QPushButton(tr("取消"), this);
    m_okButton     = new QPushButton(tr("確定"), this);
    m_okButton->setDefault(true);
    btnRow->addWidget(m_cancelButton);
    btnRow->addWidget(m_okButton);
    mainLayout->addLayout(btnRow);

    connect(m_okButton, &QPushButton::clicked, this, &DimExpressionDialog::onAccept);
    connect(m_cancelButton, &QPushButton::clicked, this, &DimExpressionDialog::onReject);
    connect(m_exprEdit, &QLineEdit::returnPressed, this, &DimExpressionDialog::onAccept);

    if (m_cadView) {
        connect(m_cadView, &view::CadView::dimensionRefPicked,
                this, &DimExpressionDialog::onDimensionRefPicked);
        // 對話框一開啟就自動進入「插入參考」模式，不需要使用者另外按按鈕。
        m_cadView->setRefPickModeActive(true);
    }
}

DimExpressionDialog::~DimExpressionDialog()
{
    // 保險：對話框以任何方式關閉（含視窗管理員的 X 按鈕，不會經過
    // onAccept()/onReject()）都要確保「插入參考」模式被關掉，避免 CadView
    // 卡在攔截左鍵點擊的狀態、之後完全無法一般選取幾何。
    if (m_cadView) m_cadView->setRefPickModeActive(false);
}

void DimExpressionDialog::closeEvent(QCloseEvent* event)
{
    if (m_cadView) m_cadView->setRefPickModeActive(false);
    QDialog::closeEvent(event);
}

QString DimExpressionDialog::autoNameForConstraint(const QString& refConstraintUuid) const
{
    if (!m_sketch) return QString();
    cad::SketchConstraint* con = m_sketch->findConstraint(refConstraintUuid);
    if (!con || con->paramExpr.isEmpty()) return QString();

    auto* store = m_sketch->parameterStore();
    if (!store || !store->hasLocal(con->paramExpr)) return QString();

    return con->paramExpr;
}

void DimExpressionDialog::onDimensionRefPicked(const QString& refConstraintUuid)
{
    // 點到的是自己 → 忽略（在自己的運算式裡插入自己的名稱沒有意義，
    // 且會造成 ParameterStore 循環參考）。
    if (refConstraintUuid == m_constraintUuid) return;

    QString name = autoNameForConstraint(refConstraintUuid);
    if (name.isEmpty()) return;  // 該約束沒有自動命名參數（例如非 driving），忽略

    m_exprEdit->insert(name);
    m_exprEdit->setFocus();

    // ⚠️ 修改：「插入參考」現在全程自動啟用（不再是一次性 toggle），
    // 插入完成後不需要、也不應該關掉——讓使用者可以連續點選多個尺寸標註，
    // 插入多個參數名稱組成像 "d1+d2*2" 這樣的運算式，不用每插入一個就
    // 重新開啟一次。
}

void DimExpressionDialog::onAccept()
{
    const QString expr = m_exprEdit->text().trimmed();
    if (expr.isEmpty()) { onReject(); return; }
    Q_EMIT expressionAccepted(m_constraintUuid, expr);
    accept();
}

void DimExpressionDialog::onReject()
{
    reject();
}

} // namespace aicad::ui
