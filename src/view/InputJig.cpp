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
    m_angleEdit->setValidator(new QDoubleValidator(-360000.0, 360000.0, 6, m_angleEdit));

    m_distEdit->installEventFilter(this);
    m_angleEdit->installEventFilter(this);

    connect(m_distEdit, &QLineEdit::textEdited, this, [this](const QString&) {
        m_distLocked = true;
        Q_EMIT liveTextChanged();
    });
    connect(m_angleEdit, &QLineEdit::textEdited, this, [this](const QString&) {
        m_angleLocked = true;
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
        m_angleEdit->setText(QString::number(angleDeg, 'f', 2));

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

QString InputJig::distanceEditText() const { return m_distEdit->text(); }
QString InputJig::angleEditText()    const { return m_angleEdit->text(); }

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
    updateFrameVisibility();
}

void InputJig::beginTypedInput(const QString& firstChars)
{
    if (!isJigVisible()) return;
    m_distEdit->clear();
    m_distEdit->insert(firstChars);
    m_distLocked = true;
    focusField(m_distEdit, false);
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
