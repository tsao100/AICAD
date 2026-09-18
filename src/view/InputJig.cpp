/**
 * @file InputJig.cpp
 */
#include "view/InputJig.h"

#include <cmath>
#include <QLineEdit>
#include <QLabel>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QEvent>
#include <QDoubleValidator>
#include <QWidget>

namespace aicad {
namespace view {

namespace {

/// 解析角度文字。方位角模式下額外支援「度-分-秒」格式
/// （ddd-mm-ss.sss，例如 "125-30-15.5" = 125°30'15.5"）——這是測量／
/// 土木製圖常用的角度表示慣例，跟純小數角度（例如 "125.504"）並存：
/// 文字裡只要含有 '-' 分隔符就當 DMS 解析，否則照舊當純小數角度解析，
/// 兩種輸入方式使用者都可以打。度／分／秒任一段可省略（"ddd-mm" 或單純
/// "ddd" 也接受，缺的部分視為 0），方便使用者不見得每次都想打到秒。
/// 非方位角模式（一般幾何夾角）沒有「度-分-秒」的製圖慣例，維持原本
/// 純小數解析，不套用 DMS。
bool parseAngleText(const QString& text, bool azimuthMode, double& outDeg)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return false;

    if (azimuthMode && trimmed.contains(QLatin1Char('-'))) {
        QString body = trimmed;
        bool negative = false;
        if (body.startsWith(QLatin1Char('-'))) {
            negative = true;
            body = body.mid(1);
        }
        const QStringList parts = body.split(QLatin1Char('-'), Qt::SkipEmptyParts);
        if (parts.isEmpty() || parts.size() > 3) return false;

        bool ok = false;
        const double deg = parts[0].toDouble(&ok);
        if (!ok) return false;
        double minPart = 0.0, secPart = 0.0;
        if (parts.size() >= 2) {
            minPart = parts[1].toDouble(&ok);
            if (!ok) return false;
        }
        if (parts.size() >= 3) {
            secPart = parts[2].toDouble(&ok);
            if (!ok) return false;
        }
        double total = deg + minPart / 60.0 + secPart / 3600.0;
        if (negative) total = -total;
        outDeg = total;
        return true;
    }

    // 純小數角度（方位角模式下文字沒有 '-'，或非方位角模式）。
    bool ok = false;
    const double v = trimmed.toDouble(&ok);
    if (!ok) return false;
    outDeg = v;
    return true;
}

/// 方位角模式下，把小數角度格式化成「度-分-秒」文字（ddd-mm-ss.sss），
/// 對應 parseAngleText() 支援的輸入格式，讓使用者在還沒開始打字時看到
/// 的預設顯示值跟輸入時預期打的格式一致（否則畫面上顯示 "125.50"，
/// 卻要求使用者打 "125-30-15" 這種格式，會很不直觀）。
///
/// 一般模式：角度值是「與 X 軸的夾角」，不管往哪個方向轉（順時針／
/// 逆時針）都是正值，且不大於 180°——跟方位角是有方向性的 0°~360°
/// 方位不同，一般模式單純描述線段跟 X 軸的夾角大小，沒有正負號的
/// 概念。deg 傳進來時是 CadView 算出的方向性角度（可能是 0°~360°的
/// 任意值），這裡先折算成 (-180°,180°] 的「較短路徑」夾角，再取絕對值
/// 得到 [0°,180°]。
///
/// ⚠️ 這一步只影響「顯示的文字」，不影響底層真正拿去算端點座標用的角度
/// 值——那個仍然是方向性的 m_liveAngle（見 angleValue()／emitCommit()
/// 對「使用者是否實際打字」的判斷，避免不小心把這段折算過的顯示文字
/// 誤當成使用者明確要的覆寫角度，導致方向被錯誤對摺到 X 軸另一側）。
QString formatAngleForDisplay(double deg, bool azimuthMode)
{
    if (!azimuthMode) {
        double sweep = std::fmod(deg, 360.0);
        if (sweep > 180.0) sweep -= 360.0;
        else if (sweep <= -180.0) sweep += 360.0;
        return QString::number(std::abs(sweep), 'f', 2);
    }

    const bool negative = deg < 0.0;
    double a = std::abs(deg);
    int d = static_cast<int>(a);
    double remMin = (a - d) * 60.0;
    int m = static_cast<int>(remMin);
    double s = (remMin - m) * 60.0;
    // 四捨五入到毫秒等級可能進位到 60，逐級進位避免顯示「xx-60-...」。
    if (s >= 59.9995) { s = 0.0; ++m; }
    if (m >= 60) { m = 0; ++d; }

    return QString("%1%2-%3-%4")
        .arg(negative ? QStringLiteral("-") : QString())
        .arg(d, 3, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 6, 'f', 3, QLatin1Char('0'));
}

} // namespace

static const char* kFrameStyle =
    "QFrame#InputJigFrame {"
    "  background-color: rgba(35, 35, 38, 225);"
    "  border: 1px solid rgba(150, 150, 150, 200);"
    "  border-radius: 4px;"
    "}"
    "QLabel { color: #d8d8d8; font-size: 11px; }"
    "QLineEdit {"
    "  background-color: rgba(255,255,255,235);"
    "  color: #202020;"
    "  border: 1px solid #888;"
    "  border-radius: 2px;"
    "  padding: 1px 3px;"
    "  font-size: 11px;"
    "}"
    "QLineEdit:focus { border: 1px solid #3d8ef7; }";

InputJig::InputJig(QWidget* parent)
    : QObject(parent)
{
    m_distFrame  = buildFrame(parent, tr("L:"),  &m_distEdit,  &m_distLabel);
    m_angleFrame = buildFrame(parent, tr("\u2220:"), &m_angleEdit, &m_angleLabel);

    m_distEdit->setValidator(new QDoubleValidator(-1.0e12, 1.0e12, 6, m_distEdit));

    // 角度欄位：一般模式沿用純小數驗證器；方位角模式刻意不設驗證器
    // （見 setAzimuthMode()），讓使用者能不受限制地打出「度-分-秒」
    // 格式。
    m_angleValidatorDecimal = new QDoubleValidator(-360000.0, 360000.0, 6, m_angleEdit);
    m_angleEdit->setValidator(m_angleValidatorDecimal);  // 預設非方位角模式

    m_distEdit->installEventFilter(this);
    m_angleEdit->installEventFilter(this);

    connect(m_distEdit, &QLineEdit::textEdited, this, [this](const QString&) {
        m_distLocked = true;
        Q_EMIT liveTextChanged();
    });
    connect(m_angleEdit, &QLineEdit::textEdited, this, [this](const QString&) {
        m_angleLocked      = true;
        m_angleTypedByUser = true;
        Q_EMIT liveTextChanged();
    });
    // focus 改變時（Tab 切換、beginTypedInput 聚焦、或使用者點掉焦點）
    // 立即更新兩個 frame 的顯示狀態，不必等下一次 showLive()。
    connect(m_distEdit,  &QLineEdit::editingFinished, this, [this]() { updateFrameVisibility(); });
    connect(m_angleEdit, &QLineEdit::editingFinished, this, [this]() { updateFrameVisibility(); });

    m_distFrame->hide();
    m_angleFrame->hide();
}

QFrame* InputJig::buildFrame(QWidget* parent, const QString& labelText,
                              QLineEdit** outEdit, QLabel** outLabel)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName("InputJigFrame");
    frame->setAttribute(Qt::WA_ShowWithoutActivating, true);
    frame->setFocusPolicy(Qt::NoFocus);
    frame->setStyleSheet(kFrameStyle);

    // 這個 frame 只在對應欄位實際有 focus（使用者正在打字）時才會顯示
    // （見 updateFrameVisibility()），但仍保留滑鼠事件穿透：即使顯示的
    // 短暫期間，也不應該讓它意外攔截到本來要給 3D 場景的點擊——效果與
    // JigLeaderLine（已移除，改由 InputJigOverlay 取代）原本套用的做法
    // 一致。
    frame->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto* label = new QLabel(labelText, frame);
    auto* edit  = new QLineEdit(frame);
    edit->setFixedWidth(64);
    label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    edit->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(5, 3, 5, 3);
    layout->setSpacing(3);
    layout->addWidget(label);
    layout->addWidget(edit);
    frame->setLayout(layout);
    frame->adjustSize();

    *outEdit  = edit;
    *outLabel = label;
    return frame;
}

void InputJig::setAzimuthMode(bool azimuth)
{
    m_azimuth = azimuth;
    m_angleLabel->setText(azimuth ? tr("Az:") : tr("\u2220:"));
    m_angleFrame->adjustSize();
    // ⚠️ 需求：角度值輸入框請不要抑制 '-' 號——先前方位角模式改用一個
    // 只允許數字/小數點/減號的正規表示式驗證器，但實測使用者仍然打不
    // 出 '-'；為了徹底排除「驗證器本身的字元限制」這個疑慮，方位角模式
    // 乾脆完全不設驗證器（nullptr），任何字元都能打，格式/範圍檢查全部
    // 交給送出/鎖定時呼叫的 parseAngleText() 處理（見該函式）。
    m_angleEdit->setValidator(azimuth ? nullptr : m_angleValidatorDecimal);
}

// 把 anchor 附近的 frame 移到「以 anchor 為中心」的位置，並確保不超出父層邊界。
static void moveFrameCentered(QFrame* frame, const QPoint& anchor)
{
    QWidget* parent = frame->parentWidget();
    QPoint pos = anchor - QPoint(frame->width() / 2, frame->height() / 2);
    if (parent) {
        const QRect bounds = parent->rect();
        if (pos.x() < bounds.left())               pos.setX(bounds.left());
        if (pos.y() < bounds.top())                 pos.setY(bounds.top());
        if (pos.x() + frame->width()  > bounds.right())  pos.setX(bounds.right()  - frame->width());
        if (pos.y() + frame->height() > bounds.bottom()) pos.setY(bounds.bottom() - frame->height());
    }
    frame->move(pos);
}

void InputJig::updateFrameVisibility()
{
    // 只在「實際擁有 focus」（使用者正在打字）時才顯示——見類別說明；
    // 其餘時間（單純追蹤／已鎖定但已 Tab 離開）保持隱藏，畫面上的顯示
    // 完全交給 InputJigOverlay 的 3D 文字。
    m_distFrame->setVisible(m_active && m_distEdit->hasFocus());
    m_angleFrame->setVisible(m_active && m_angleEdit->hasFocus());
    if (m_distFrame->isVisible())  m_distFrame->raise();
    if (m_angleFrame->isVisible()) m_angleFrame->raise();
}

void InputJig::showLive(const QPoint& distAnchor, const QPoint& angleAnchor,
                        double distance, double angleDeg)
{
    m_active       = true;
    m_liveDistance = distance;
    m_liveAngle    = angleDeg;

    // 只更新未鎖定、且目前沒有正在被使用者編輯（focus 中）的欄位，
    // 避免覆寫使用者正在輸入的文字。
    if (!m_distLocked && !m_distEdit->hasFocus())
        m_distEdit->setText(QString::number(distance, 'f', 3));
    if (!m_angleLocked && !m_angleEdit->hasFocus())
        m_angleEdit->setText(formatAngleForDisplay(angleDeg, m_azimuth));

    moveFrameCentered(m_distFrame,  distAnchor);
    moveFrameCentered(m_angleFrame, angleAnchor);

    updateFrameVisibility();
}

void InputJig::hideJig()
{
    m_active = false;
    m_distFrame->hide();
    m_angleFrame->hide();
    resetLocks();
}

void InputJig::resetLocks()
{
    m_distLocked  = false;
    m_angleLocked = false;
    m_angleTypedByUser = false;
    m_distEdit->clear();
    m_angleEdit->clear();
}

bool InputJig::distanceValue(double& out) const
{
    if (!m_distLocked) return false;
    bool ok = false;
    double v = m_distEdit->text().toDouble(&ok);
    if (!ok) return false;
    out = v;
    return true;
}

bool InputJig::angleValue(double& out) const
{
    if (!m_angleLocked) return false;
    if (!m_angleTypedByUser) {
        // 使用者只是 Tab／Enter 經過角度欄位，沒有實際打字修改內容——
        // 畫面上顯示的可能是已經折算成「不大於 180°、無方向性」的文字
        // （一般模式，見 formatAngleForDisplay()），不能直接拿來反推
        // 座標，否則方向會被錯誤地對摺到 X 軸另一側。這種情況直接採用
        // 滑鼠當下真正指向的方向（m_liveAngle，未折算、有方向性），
        // 效果等同於「維持滑鼠目前指的方向」，符合直覺。
        out = m_liveAngle;
        return true;
    }
    return parseAngleText(m_angleEdit->text(), m_azimuth, out);
}

QString InputJig::distanceEditText() const { return m_distEdit->text(); }
QString InputJig::angleEditText()    const { return m_angleEdit->text(); }

void InputJig::lockField(QLineEdit* edit, bool& lockedFlag)
{
    if (edit->text().trimmed().isEmpty()) return;
    bool ok = false;
    if (edit == m_angleEdit) {
        double dummy = 0.0;
        ok = parseAngleText(edit->text(), m_azimuth, dummy);
    } else {
        edit->text().toDouble(&ok);
    }
    if (ok) lockedFlag = true;
}

void InputJig::focusField(QLineEdit* edit, bool selectAll)
{
    edit->setFocus(Qt::OtherFocusReason);
    if (selectAll) edit->selectAll();
    updateFrameVisibility();
}

void InputJig::beginTypedInput(const QString& firstChars, bool angleField)
{
    if (!isJigVisible()) return;
    QLineEdit* edit    = angleField ? m_angleEdit   : m_distEdit;
    bool&      locked  = angleField ? m_angleLocked : m_distLocked;
    edit->clear();
    edit->insert(firstChars);
    locked = true;
    // 這是使用者真正打的字（跟 textEdited 訊號同義），角度欄位要一併
    // 標記「已由使用者打字」，emitCommit()/angleValue() 才會採用這裡
    // 打進去的文字，而不是誤用 m_liveAngle 蓋掉使用者剛打的內容。
    if (angleField) m_angleTypedByUser = true;
    focusField(edit, false);
    Q_EMIT liveTextChanged();
}

void InputJig::emitCommit()
{
    // 若使用者正在編輯中（尚未按 Tab），把目前欄位內容視為鎖定值一併採用。
    lockField(m_distEdit,  m_distLocked);
    lockField(m_angleEdit, m_angleLocked);

    bool ok = false;
    double distance = m_distLocked ? m_distEdit->text().toDouble(&ok) : m_liveDistance;
    if (m_distLocked && !ok) distance = m_liveDistance;

    double angle = m_liveAngle;
    if (m_angleLocked && m_angleTypedByUser) {
        // 只有使用者「實際打字」修改過角度欄位時，才採用文字內容解析出
        // 來的值；否則（單純 Tab／Enter 經過）保留 m_liveAngle（真正、
        // 有方向性的即時角度），理由同 angleValue()。
        double parsed = 0.0;
        if (parseAngleText(m_angleEdit->text(), m_azimuth, parsed))
            angle = parsed;
    }

    Q_EMIT committed(distance, angle);
    resetLocks();
}

bool InputJig::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() != QEvent::KeyPress) return QObject::eventFilter(obj, event);

    auto* ke = static_cast<QKeyEvent*>(event);
    auto* edit = qobject_cast<QLineEdit*>(obj);
    if (!edit) return QObject::eventFilter(obj, event);

    switch (ke->key()) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab: {
        if (edit == m_distEdit) {
            lockField(m_distEdit, m_distLocked);
            focusField(m_angleEdit, true);
        } else {
            lockField(m_angleEdit, m_angleLocked);
            focusField(m_distEdit, true);
        }
        return true;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emitCommit();
        return true;
    case Qt::Key_Escape:
        hideJig();
        Q_EMIT cancelled();
        return true;
    default:
        break;
    }
    return QObject::eventFilter(obj, event);
}

} // namespace view
} // namespace aicad
