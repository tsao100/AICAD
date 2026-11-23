/**
 * @file KeymapView.cpp
 */
#include "KeymapView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

namespace aicad {
namespace ui {

// ============================================================================
//  KeymapCanvas：實際繪圖與滑鼠互動
// ============================================================================
class KeymapCanvas : public QWidget {
    Q_OBJECT
public:
    explicit KeymapCanvas(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(140);
        setMouseTracking(false);
        setCursor(Qt::CrossCursor);
        setToolTip(tr("點選或拖曳：讓 View 平移到該點"));
    }

    void setScene(const QVector<QPolygonF>& lines,
                  const QVector<QPolygonF>& sketchLines,
                  const QVector<QRectF>& boxes)
    {
        m_lines       = lines;
        m_sketchLines = sketchLines;
        m_boxes       = boxes;

        QRectF b;
        bool first = true;
        auto add = [&](const QRectF& r) {
            if (!r.isValid() && r.width() == 0 && r.height() == 0 && !first) { /* 點也算 */ }
            b = first ? r : b.united(r);
            first = false;
        };
        for (const QPolygonF& pl : m_lines)
            if (!pl.isEmpty()) add(pl.boundingRect());
        for (const QPolygonF& pl : m_sketchLines)
            if (!pl.isEmpty()) add(pl.boundingRect());
        for (const QRectF& r : m_boxes)
            add(r);
        m_bounds = b;
        m_hasData = !first;
        update();
    }

    void setViewport(const QPolygonF& vp)
    {
        if (vp == m_viewport) return;
        m_viewport = vp;
        update();
    }

Q_SIGNALS:
    void panRequested(const QPointF& worldXY);

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor(30, 33, 38));

        if (!m_hasData) {
            p.setPen(QColor(150, 150, 150));
            p.drawText(rect(), Qt::AlignCenter, tr("（無資料）"));
            return;
        }

        const Mapping m = mapping();

        // 其他物件的包圍框
        p.setPen(QPen(QColor(120, 130, 145), 1));
        p.setBrush(Qt::NoBrush);
        for (const QRectF& r : m_boxes) {
            const QPointF a = toWidget(QPointF(r.left(),  r.top()),    m);
            const QPointF c = toWidget(QPointF(r.right(), r.bottom()), m);
            p.drawRect(QRectF(a, c).normalized());
        }

        // 草圖幾何（細線；水平／垂直的線寬度為 0 的 boundingRect 也能正確納入範圍）
        p.setPen(QPen(QColor(110, 190, 255), 1));
        for (const QPolygonF& pl : m_sketchLines) {
            if (pl.size() < 2) continue;
            QPolygonF w;
            w.reserve(pl.size());
            for (const QPointF& pt : pl) w << toWidget(pt, m);
            p.drawPolyline(w);
        }

        // 線路折線
        p.setPen(QPen(QColor(230, 90, 210), 1.5));
        for (const QPolygonF& pl : m_lines) {
            if (pl.size() < 2) continue;
            QPolygonF w;
            w.reserve(pl.size());
            for (const QPointF& pt : pl) w << toWidget(pt, m);
            p.drawPolyline(w);
        }

        // 目前 View 可視範圍
        if (m_viewport.size() >= 3) {
            QPolygonF w;
            for (const QPointF& pt : m_viewport) w << toWidget(pt, m);
            p.setPen(QPen(QColor(255, 210, 0), 1.5));
            p.setBrush(QColor(255, 210, 0, 40));
            p.drawPolygon(w);
        }
    }

    void mousePressEvent(QMouseEvent* e) override   { handle(e); }
    void mouseMoveEvent(QMouseEvent* e) override    { if (e->buttons() & Qt::LeftButton) handle(e); }

private:
    struct Mapping { double scale; double ox; double oy; };

    Mapping mapping() const
    {
        const double margin = 10.0;
        const double bw = std::max(m_bounds.width(),  1e-6);
        const double bh = std::max(m_bounds.height(), 1e-6);
        const double sx = (width()  - 2 * margin) / bw;
        const double sy = (height() - 2 * margin) / bh;
        const double s  = std::max(1e-9, std::min(sx, sy));
        // 置中；世界 Y 向上、畫面 Y 向下
        const double ox = (width()  - bw * s) / 2.0 - m_bounds.left() * s;
        const double oy = (height() + bh * s) / 2.0 + m_bounds.top()  * s;
        return {s, ox, oy};
    }

    static QPointF toWidget(const QPointF& w, const Mapping& m)
    {
        return QPointF(w.x() * m.scale + m.ox, m.oy - w.y() * m.scale);
    }

    QPointF toWorld(const QPointF& pt) const
    {
        const Mapping m = mapping();
        return QPointF((pt.x() - m.ox) / m.scale, (m.oy - pt.y()) / m.scale);
    }

    void handle(QMouseEvent* e)
    {
        if (!m_hasData) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPointF pos = e->position();
#else
        const QPointF pos = e->localPos();
#endif
        Q_EMIT panRequested(toWorld(pos));
    }

    QVector<QPolygonF> m_lines;
    QVector<QPolygonF> m_sketchLines;
    QVector<QRectF>    m_boxes;
    QPolygonF          m_viewport;
    QRectF             m_bounds;
    bool               m_hasData = false;
};

// ============================================================================
//  KeymapView
// ============================================================================
KeymapView::KeymapView(QWidget* parent)
    : QDockWidget(tr("Keymap View"), parent)
{
    setObjectName(QStringLiteral("KeymapViewDock"));
    auto* box = new QWidget(this);
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    m_filterBox = new QComboBox(box);
    m_filterBox->addItem(tr("全部"),              int(All));
    m_filterBox->addItem(tr("Railway + Loft"),   int(RailwayAndLoft));
    m_filterBox->addItem(tr("僅 Sketch"),         int(SketchOnly));
    {
        QSettings st("AICAD", "AICAD");
        const int saved = st.value("Keymap/filter", int(All)).toInt();
        const int idx = m_filterBox->findData(saved);
        m_filterBox->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    lay->addWidget(m_filterBox);

    m_canvas = new KeymapCanvas(box);
    lay->addWidget(m_canvas, 1);
    setWidget(box);

    connect(m_canvas, &KeymapCanvas::panRequested, this, &KeymapView::panRequested);
    connect(m_filterBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        QSettings("AICAD", "AICAD").setValue("Keymap/filter", int(filter()));
        Q_EMIT filterChanged();
    });
}

KeymapView::Filter KeymapView::filter() const
{
    return static_cast<Filter>(m_filterBox->currentData().toInt());
}

void KeymapView::setScene(const QVector<QPolygonF>& lines,
                          const QVector<QPolygonF>& sketchLines,
                          const QVector<QRectF>& boxes)
{
    m_canvas->setScene(lines, sketchLines, boxes);
}

void KeymapView::setViewport(const QPolygonF& viewport)
{
    m_canvas->setViewport(viewport);
}

} // namespace ui
} // namespace aicad

#include "KeymapView.moc"
