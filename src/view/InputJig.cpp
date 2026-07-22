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
#include <QPainter>
#include <QPen>
#include <QPaintEvent>

namespace aicad {
namespace view {

// ── JigLeaderLine：距離/角度標籤與其錨點（線段中點/起點）之間的點線 ──────────
// 純視覺用的透明覆蓋 widget，不接收滑鼠/鍵盤事件；幾何範圍只包住兩點的
// bounding box（而非整個 CadView），畫完立刻依內容自動收放大小與位置。
class JigLeaderLine : public QWidget {
public:
    explicit JigLeaderLine(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFocusPolicy(Qt::NoFocus);
    }

    /// a／b 為「父層（CadView）座標系」下的兩個端點。
    void setLine(const QPoint& a, const QPoint& b)
    {
        QRect r = QRect(a, b).normalized();
        const int pad = 3;   // 避免線寬/端點被裁到
        r.adjust(-pad, -pad, pad, pad);
        if (r.width()  < 1) r.setWidth(1);
        if (r.height() < 1) r.setHeight(1);

        move(r.topLeft());
        resize(r.size());
        m_localA = a - r.topLeft();
        m_localB = b - r.topLeft();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor(220, 220, 220, 230));
        pen.setStyle(Qt::DotLine);
        pen.setWidthF(1.2);
        p.setPen(pen);
        p.drawLine(m_localA, m_localB);
    }

private:
    QPoint m_localA;
    QPoint m_localB;
};

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

    m_distLeader  = new JigLeaderLine(parent);
    m_angleLeader = new JigLeaderLine(parent);

    m_distEdit->setValidator(new QDoubleValidator(-1.0e12, 1.0e12, 6, m_distEdit));
    m_angleEdit->setValidator(new QDoubleValidator(-360000.0, 360000.0, 6, m_angleEdit));

    m_distEdit->installEventFilter(this);
    m_angleEdit->installEventFilter(this);

    connect(m_distEdit, &QLineEdit::textEdited, this, [this](const QString&) {
        m_distLocked = true;
    });
    connect(m_angleEdit, &QLineEdit::textEdited, this, [this](const QString&) {
        m_angleLocked = true;
    });

    m_distFrame->hide();
    m_angleFrame->hide();
    m_distLeader->hide();
    m_angleLeader->hide();
}

QFrame* InputJig::buildFrame(QWidget* parent, const QString& labelText,
                              QLineEdit** outEdit, QLabel** outLabel)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName("InputJigFrame");
    frame->setAttribute(Qt::WA_ShowWithoutActivating, true);
    frame->setFocusPolicy(Qt::NoFocus);
    frame->setStyleSheet(kFrameStyle);

    auto* label = new QLabel(labelText, frame);
    auto* edit  = new QLineEdit(frame);
    edit->setFixedWidth(64);

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

void InputJig::showLive(const QPoint& distAnchor, const QPoint& angleAnchor,
                         const QPointF& lineDir, double distance, double angleDeg)
{
    m_liveDistance = distance;
    m_liveAngle    = angleDeg;

    // 只更新未鎖定、且目前沒有正在被使用者編輯（focus 中）的欄位，
    // 避免覆寫使用者正在輸入的文字。
    if (!m_distLocked && !m_distEdit->hasFocus())
        m_distEdit->setText(QString::number(distance, 'f', 3));
    if (!m_angleLocked && !m_angleEdit->hasFocus())
        m_angleEdit->setText(QString::number(angleDeg, 'f', 2));

    // ── 偏移：距離標籤沿線段的法線方向往外推開，避免壓在橡皮筋線上 ──────
    QPoint distPos = distAnchor;
    const double len = std::hypot(lineDir.x(), lineDir.y());
    if (len > 1.0) {
        // 法線（垂直於線段方向），螢幕座標系 Y 向下，選一側固定即可，
        // 兩個數值不會互相重疊就足夠可讀。
        const double nx = -lineDir.y() / len;
        const double ny =  lineDir.x() / len;
        const int offsetPx = 14;
        distPos += QPoint(static_cast<int>(nx * offsetPx), static_cast<int>(ny * offsetPx));
    } else {
        distPos += QPoint(0, -14);
    }

    // 角度標籤放在頂點附近，往線段反方向（起點外側）偏移一點，避免蓋住頂點本身
    QPoint anglePos = angleAnchor;
    if (len > 1.0) {
        const double ux = -lineDir.x() / len;
        const double uy = -lineDir.y() / len;
        const int offsetPx = 22;
        anglePos += QPoint(static_cast<int>(ux * offsetPx), static_cast<int>(uy * offsetPx));
    } else {
        anglePos += QPoint(0, 14);
    }

    moveFrameCentered(m_distFrame,  distPos);
    moveFrameCentered(m_angleFrame, anglePos);

    // ── 尺寸線（點線）：從真正的錨點（線段中點/起點）連到標籤方框中心 ──────
    m_distLeader->setLine(
        distAnchor,
        QPoint(m_distFrame->x() + m_distFrame->width() / 2,
               m_distFrame->y() + m_distFrame->height() / 2));
    m_angleLeader->setLine(
        angleAnchor,
        QPoint(m_angleFrame->x() + m_angleFrame->width() / 2,
               m_angleFrame->y() + m_angleFrame->height() / 2));

    if (!m_distLeader->isVisible())  m_distLeader->show();
    if (!m_angleLeader->isVisible()) m_angleLeader->show();
    if (!m_distFrame->isVisible())  m_distFrame->show();
    if (!m_angleFrame->isVisible()) m_angleFrame->show();

    // z-order：點線在下、標籤方框在上，避免點線壓過文字。
    m_distLeader->raise();
    m_angleLeader->raise();
    m_distFrame->raise();
    m_angleFrame->raise();
}

void InputJig::hideJig()
{
    m_distFrame->hide();
    m_angleFrame->hide();
    m_distLeader->hide();
    m_angleLeader->hide();
    resetLocks();
}

bool InputJig::isJigVisible() const
{
    return m_distFrame->isVisible() || m_angleFrame->isVisible();
}

void InputJig::resetLocks()
{
    m_distLocked  = false;
    m_angleLocked = false;
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
    bool ok = false;
    double v = m_angleEdit->text().toDouble(&ok);
    if (!ok) return false;
    out = v;
    return true;
}

void InputJig::lockField(QLineEdit* edit, bool& lockedFlag)
{
    if (edit->text().trimmed().isEmpty()) return;
    bool ok = false;
    edit->text().toDouble(&ok);
    if (ok) lockedFlag = true;
}

void InputJig::focusField(QLineEdit* edit, bool selectAll)
{
    edit->setFocus(Qt::OtherFocusReason);
    if (selectAll) edit->selectAll();
}

void InputJig::beginTypedInput(const QString& firstChars)
{
    if (!isJigVisible()) return;
    m_distEdit->clear();
    m_distEdit->insert(firstChars);
    m_distLocked = true;
    focusField(m_distEdit, false);
}

void InputJig::emitCommit()
{
    // 若使用者正在編輯中（尚未按 Tab），把目前欄位內容視為鎖定值一併採用。
    lockField(m_distEdit,  m_distLocked);
    lockField(m_angleEdit, m_angleLocked);

    bool ok = false;
    double distance = m_distLocked ? m_distEdit->text().toDouble(&ok) : m_liveDistance;
    if (m_distLocked && !ok) distance = m_liveDistance;

    ok = false;
    double angle = m_angleLocked ? m_angleEdit->text().toDouble(&ok) : m_liveAngle;
    if (m_angleLocked && !ok) angle = m_liveAngle;

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
