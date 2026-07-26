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
#include <QPainterPath>
#include <QPen>
#include <QPaintEvent>
#include <QVector>
#include <QRectF>

namespace aicad {
namespace view {

// ── 角度方向工具 ─────────────────────────────────────────────────────────
// 依 CadView 送來的 angleDeg 定義（azimuth: 正北=0°、順時針為正；
// 一般數學: +X=0°、逆時針為正），換算成「螢幕座標系」下的單位方向向量
// （螢幕 Y 軸向下）。與 CadView 內 committed 訊號 lambda 換算 du/dv 再轉螢幕
// 座標（螢幕 Y 相對世界 Y 反向）的推導完全一致，thetaDeg=0 時回傳的方向對應
// showLive() 收到的 angleAnchor 參考方向（azimuth: 正北／螢幕正上方；
// 數學: 平面 +X／螢幕正右方）。
static QPointF jigDirAt(double thetaDeg, bool azimuth)
{
    const double rad = thetaDeg * M_PI / 180.0;
    return azimuth ? QPointF(std::sin(rad), -std::cos(rad))
                    : QPointF(std::cos(rad), -std::sin(rad));
}

// ── JigLeaderLine：距離尺寸線／角度尺寸弧線 ──────────────────────────────
// 純視覺用的透明覆蓋 widget，不接收滑鼠/鍵盤事件；幾何範圍只包住所有繪製
// 元素的 bounding box（而非整個 CadView），畫完立刻依內容自動收放大小與位置。
//
//  距離模式：尺寸線與橡皮筋（p0→p1）平行、延伸線與橡皮筋垂直、距離值
//            （由呼叫端 labelCenter 決定位置）落在尺寸線中央。
//  角度模式：尺寸弧線與橡皮筋方向垂直（弧線的弦與 vertex→p1 方向垂直）、
//            延伸線（參考方向 0° 與目前橡皮筋方向）與橡皮筋平行地向外延伸、
//            角度值落在弧線角平分線上（由呼叫端 labelCenter 決定位置）。
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

    /// p0/p1 為橡皮筋的起點／終點，labelCenter 為距離數值方框中心，
    /// 三者皆為「父層（CadView）座標系」下的座標。
    void setDistanceDim(const QPointF& p0, const QPointF& p1, const QPointF& labelCenter)
    {
        m_mode        = Mode::Distance;
        m_p0          = p0;
        m_p1          = p1;
        m_labelCenter = labelCenter;
        relayout({ p0, p1, labelCenter });
    }

    /// vertex 為橡皮筋起點（角度頂點），angleDeg/azimuth 決定弧線掃過的
    /// 範圍與參考方向，labelCenter 為角度數值方框中心。
    void setAngleDim(const QPointF& vertex, double angleDeg, bool azimuth,
                      const QPointF& labelCenter)
    {
        m_mode        = Mode::Angle;
        m_p0          = vertex;
        m_angleDeg    = angleDeg;
        m_azimuth     = azimuth;
        m_labelCenter = labelCenter;

        const double radius = kArcRadius;
        QVector<QPointF> bounds{ vertex, labelCenter };
        // 弧線＋延伸線可能延伸到的最遠處，一併納入 bounding box。
        bounds << (vertex + jigDirAt(0.0, azimuth) * (radius + kExtLen));
        bounds << (vertex + jigDirAt(angleDeg, azimuth) * (radius + kExtLen));
        relayout(bounds);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        if (m_mode == Mode::Distance) {
            paintDistance(p);
        } else if (m_mode == Mode::Angle) {
            paintAngle(p);
        }
    }

private:
    enum class Mode { None, Distance, Angle };

    void relayout(const QVector<QPointF>& pts)
    {
        QRectF r(pts.first(), QSizeF(0, 0));
        for (const QPointF& pt : pts) r = r.united(QRectF(pt, QSizeF(0, 0)));
        const int pad = 12;   // 容納延伸線超出量與線寬
        r.adjust(-pad, -pad, pad, pad);
        if (r.width()  < 1) r.setWidth(1);
        if (r.height() < 1) r.setHeight(1);

        move(r.topLeft().toPoint());
        resize(r.size().toSize());
        m_origin = r.topLeft();
        update();
    }

    void paintDistance(QPainter& p)
    {
        const QPointF p0 = m_p0 - m_origin;
        const QPointF p1 = m_p1 - m_origin;
        const QPointF labelCenter = m_labelCenter - m_origin;

        const double len = std::hypot(p1.x() - p0.x(), p1.y() - p0.y());
        if (len < 1.0) return;
        const QPointF dirV(  (p1.x() - p0.x()) / len, (p1.y() - p0.y()) / len);
        const QPointF n   ( -dirV.y(), dirV.x() );   // 垂直於橡皮筋

        // 尺寸線偏移量 = labelCenter 相對線段中點在法線方向上的投影，
        // 讓尺寸線精確通過標籤中心（標籤位置由 showLive() 決定，兩者一致）。
        const QPointF mid((p0.x() + p1.x()) / 2.0, (p0.y() + p1.y()) / 2.0);
        const QPointF toLabel = labelCenter - mid;
        double offsetPx = toLabel.x() * n.x() + toLabel.y() * n.y();
        if (std::abs(offsetPx) < 6.0) offsetPx = (offsetPx < 0.0) ? -6.0 : 6.0;

        const QPointF foot0 = p0 + n * offsetPx;
        const QPointF foot1 = p1 + n * offsetPx;

        QPen extPen(QColor(190, 190, 190, 200));
        extPen.setWidthF(1.0);
        p.setPen(extPen);
        // 延伸線：與橡皮筋垂直，從端點附近的小空隙延伸到尺寸線外側一小段。
        const double gap = 3.0, overshoot = 3.0;
        const double sign = (offsetPx >= 0.0) ? 1.0 : -1.0;
        p.drawLine(p0 + n * (gap * sign), foot0 + n * (overshoot * sign));
        p.drawLine(p1 + n * (gap * sign), foot1 + n * (overshoot * sign));

        // 尺寸線：與橡皮筋平行。
        QPen dimPen(QColor(220, 220, 220, 230));
        dimPen.setWidthF(1.3);
        p.setPen(dimPen);
        p.drawLine(foot0, foot1);

        // 端點刻度（土木製圖慣用的 45° 斜刻，而非箭頭）。
        const double tick = 4.0;
        const QPointF t(dirV.x() + n.x(), dirV.y() + n.y());
        const double tLen = std::hypot(t.x(), t.y());
        if (tLen > 1e-6) {
            const QPointF tu(t.x() / tLen, t.y() / tLen);
            p.drawLine(foot0 - tu * tick, foot0 + tu * tick);
            p.drawLine(foot1 - tu * tick, foot1 + tu * tick);
        }
    }

    void paintAngle(QPainter& p)
    {
        const QPointF vertex = m_p0 - m_origin;
        const double  radius = kArcRadius;

        QPen extPen(QColor(190, 190, 190, 200));
        extPen.setWidthF(1.0);
        p.setPen(extPen);
        // 延伸線：與橡皮筋平行——一條沿參考方向（0°）、一條沿目前橡皮筋方向，
        // 兩條都從頂點向外延伸到超過弧線半徑的距離。
        const QPointF refDir = jigDirAt(0.0, m_azimuth);
        const QPointF curDir = jigDirAt(m_angleDeg, m_azimuth);
        p.drawLine(vertex, vertex + refDir * (radius + kExtLen));
        p.drawLine(vertex, vertex + curDir * (radius + kExtLen));

        // 弧線：與橡皮筋方向垂直（弧上每一點皆與頂點等距，弧線在 curDir
        // 處的切線方向即垂直於橡皮筋），以折線逼近半徑 radius 的圓弧。
        QPainterPath path;
        const int steps = qMax(4, static_cast<int>(std::abs(m_angleDeg) / 6.0) + 4);
        for (int i = 0; i <= steps; ++i) {
            const double t = m_angleDeg * (static_cast<double>(i) / steps);
            const QPointF pt = vertex + jigDirAt(t, m_azimuth) * radius;
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        QPen dimPen(QColor(220, 220, 220, 230));
        dimPen.setWidthF(1.3);
        p.setPen(dimPen);
        p.drawPath(path);

        // 尺寸弧線端點的小刻度，與距離刻度風格一致。
        const double tick = 4.0;
        auto drawTick = [&](const QPointF& dir) {
            const QPointF pt = vertex + dir * radius;
            const QPointF perp(-dir.y(), dir.x());
            p.drawLine(pt - perp * tick, pt + perp * tick);
        };
        drawTick(refDir);
        drawTick(curDir);
    }

    static constexpr double kArcRadius = 26.0;
    static constexpr double kExtLen    = 14.0;

    Mode    m_mode = Mode::None;
    QPointF m_p0;              // 距離: 起點／角度: 頂點
    QPointF m_p1;               // 距離: 終點（角度模式未使用）
    QPointF m_labelCenter;
    double  m_angleDeg = 0.0;
    bool    m_azimuth  = false;
    QPointF m_origin;           // widget 左上角在父層座標系的位置
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

    // ── 角度標籤：放在尺寸弧線的角平分線方向上，讓數值落在弧線中央 ─────────
    // （jigDirAt(angleDeg/2, azimuth) 正是參考方向 0° 與目前橡皮筋方向兩者
    //  之間的角平分方向，radius 需與 JigLeaderLine::kArcRadius 視覺上一致，
    //  再往外多留一點文字間距。）
    static constexpr double kArcRadiusForLabel = 26.0;   // 與 JigLeaderLine::kArcRadius 對應
    const QPointF bisector = jigDirAt(angleDeg / 2.0, m_azimuth);
    QPoint anglePos = angleAnchor
        + QPoint(static_cast<int>(bisector.x() * (kArcRadiusForLabel + 16.0)),
                 static_cast<int>(bisector.y() * (kArcRadiusForLabel + 16.0)));

    moveFrameCentered(m_distFrame,  distPos);
    moveFrameCentered(m_angleFrame, anglePos);

    // ── 距離尺寸線／角度尺寸弧線：依橡皮筋兩端點與標籤位置畫出正式尺寸標註 ──
    const QPointF p0 = angleAnchor;
    const QPointF p1 = angleAnchor + lineDir;
    m_distLeader->setDistanceDim(
        p0, p1,
        QPointF(m_distFrame->x() + m_distFrame->width() / 2.0,
                m_distFrame->y() + m_distFrame->height() / 2.0));
    m_angleLeader->setAngleDim(
        p0, angleDeg, m_azimuth,
        QPointF(m_angleFrame->x() + m_angleFrame->width() / 2.0,
                m_angleFrame->y() + m_angleFrame->height() / 2.0));

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
