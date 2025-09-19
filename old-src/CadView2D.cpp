#include "CadView2D.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <cmath>
#include <QPrinter>
#include <QPrintDialog>
#include <QPdfWriter>

// --- ctor ---
CadView2D::CadView2D(QWidget *parent) : QWidget(parent), m_scale(1.0) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

// --- helpers ---
QPointF CadView2D::toScreen(const QPointF &world) const {
    return m_transform.map(world);
}
QPointF CadView2D::toWorld(const QPointF &screen) const {
    return m_transform.inverted().map(screen);
}
void CadView2D::setMode(Mode m) {
    m_mode = m;
    m_lineActive = false;
    m_arcStage = 0;
    update();
}

// --- paint ---
void CadView2D::paintEvent(QPaintEvent *ev) {
    Q_UNUSED(ev);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().color(QPalette::Base));
    drawGrid(&p);

    p.save();
    p.setTransform(m_transform, true);

    // example: crosshair origin
    p.setPen(QPen(QColor(200,40,40), 0));
    p.drawLine(QPointF(-1000,0), QPointF(1000,0));
    p.drawLine(QPointF(0,-1000), QPointF(0,1000));

    // draw all entities
    p.setPen(QPen(Qt::darkGreen, 0));
    for (const auto &ent : m_entities)
        ent->paint(p);

    // --- rubber band line ---
    if (m_mode == DrawLine && m_lineActive) {
        p.setPen(QPen(Qt::red, 0, Qt::DashLine));
        p.drawLine(m_lineStart, m_mouseWorld);
    }

    // preview line
    if (m_mode==DrawLine && m_lineActive) {
        p.setPen(QPen(Qt::red, 0, Qt::DashLine));
        p.drawLine(m_lineStart, m_mouseWorld);
    }

    // --- Arc preview ---
    if (m_mode == DrawArc && m_arcStage > 0) {
        p.setPen(QPen(Qt::blue, 0, Qt::DashLine));

        if (m_arcStage == 1) {
            // First click done, draw rubber line from start → mouse
            p.drawLine(m_arcStart, m_mouseWorld);
        } else if (m_arcStage == 2) {
            // Start + mid chosen, preview arc through 3 points
            ArcDef def;
            if (circleFrom3Points(m_arcStart, m_arcMid, m_mouseWorld, def)) {
                QRectF rect(def.center.x() - def.radius,
                            def.center.y() - def.radius,
                            2 * def.radius,
                            2 * def.radius);

                // QPainter angles: degrees*16, y-axis down → need negative sign
                int start16 = int(-def.startAngle * 180.0 / M_PI * 16);
                int sweep16 = int(-def.sweepAngle * 180.0 / M_PI * 16);

                p.drawArc(rect, start16, sweep16);
            } else {
                // fallback if points are nearly collinear
                p.drawLine(m_arcStart, m_mouseWorld);
            }
        }
    }

    // rectangle
    p.setPen(QPen(Qt::blue, 0));
    p.setBrush(QBrush(QColor(0,0,255,40)));
    p.drawRect(QRectF(50,50,200,120));

    p.restore();

    // HUD
    p.setPen(Qt::black);
    p.drawText(8, height()-8,
               QString("W: %1, %2").arg(m_mouseWorld.x(),0,'f',2).arg(m_mouseWorld.y(),0,'f',2));
}

void CadView2D::saveEntities(const QString &file) {
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly|QIODevice::Text)) return;
    QTextStream out(&f);
    for (const auto &ent : m_entities)
        ent->save(out);
}

void CadView2D::loadEntities(const QString &file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&f);    // <-- create normally
    m_entities.clear();
    QString type;
    while (!in.atEnd()) {
        in >> type;
        auto ent = loadEntity(in, type);
        if (ent) m_entities.push_back(std::move(ent));
    }
    update();
}

void CadView2D::updateTransform() {
    // if you want center the origin in center:
    // keep current transform; ensure valid
    if (m_transform.isIdentity()) {
        // center origin in widget center
        m_transform.translate(width()/2.0, height()/2.0);
    }
}

void CadView2D::drawGrid(QPainter *p) {
    // draw grid in world coordinates by mapping a range of world points
    // choose grid spacing relative to current scale
    // We'll compute approximate spacing in world units so lines appear nicely spaced.
    p->save();
    // find world rect
    QRectF worldRect = QRectF(toWorld(QPointF(0,0)), toWorld(QPointF(width(), height()))).normalized();
    double pixelsPerUnit = m_scale; // approximation: because transform may include translation only
    if (pixelsPerUnit <= 0) pixelsPerUnit = 1.0;
    // choose spacing: powers of 10 * {1,2,5}
    double targetPixels = 80; // approx spacing in pixels
    double worldSpacing = targetPixels / pixelsPerUnit;
    double base = std::pow(10.0, std::floor(std::log10(worldSpacing)));
    double multiples[] = {1,2,5};
    double spacing = base * multiples[0];
    for (double m : multiples) {
        double s = base*m;
        if (s >= worldSpacing) { spacing = s; break; }
    }
    // draw light grid lines
    QPen gridPen(QColor(220,220,220)); gridPen.setCosmetic(true);
    p->setPen(gridPen);
    // vertical lines
    int startX = std::floor(worldRect.left() / spacing) - 1;
    int endX = std::ceil(worldRect.right() / spacing) + 1;
    for (int i=startX;i<=endX;i++) {
        double x = i*spacing;
        QPointF a = toScreen(QPointF(x, worldRect.top()));
        QPointF b = toScreen(QPointF(x, worldRect.bottom()));
        p->drawLine(QPointF(a.x(), a.y()), QPointF(b.x(), b.y()));
    }
    // horizontal
    int startY = std::floor(worldRect.top() / spacing) - 1;
    int endY = std::ceil(worldRect.bottom() / spacing) + 1;
    for (int i=startY;i<=endY;i++) {
        double y = i*spacing;
        QPointF a = toScreen(QPointF(worldRect.left(), y));
        QPointF b = toScreen(QPointF(worldRect.right(), y));
        p->drawLine(QPointF(a.x(), a.y()), QPointF(b.x(), b.y()));
    }
    p->restore();
}

void CadView2D::resizeEvent(QResizeEvent *ev)  {
    Q_UNUSED(ev);
    updateTransform();
}

void CadView2D::mousePressEvent(QMouseEvent *ev)  {
    if (ev->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = ev->pos();
        setCursor(Qt::ClosedHandCursor);
    } else if (ev->button() == Qt::LeftButton) {
        m_rubberActive = true;
        m_rubberStart = ev->pos();
        m_rubberEnd = ev->pos();
    }
    if (m_mode == DrawLine && ev->button() == Qt::LeftButton) {
        QPointF clickPoint = toWorld(ev->pos());

        if (!m_lineActive) {
            // first segment
            m_lineStart = clickPoint;
            m_lineActive = true;
            m_polylineMode = true;
        } else {
            // add new segment
            auto line = std::make_unique<LineEntity>(m_lineStart, clickPoint);
            m_entities.push_back(std::move(line));

            // continue polyline
            m_lineStart = clickPoint;
            m_lineActive = true;
            m_polylineMode = true;
        }
        update();
        return;
    }

    if (m_mode == DrawLine && ev->button() == Qt::RightButton) {
        // finish polyline on RMB
        m_lineActive = false;
        m_polylineMode = false;
        m_mode = Normal; // or stay in DrawLine if you prefer
        update();
        return;
    }

    if (m_mode == DrawArc && ev->button() == Qt::LeftButton) {
        QPointF clickPoint = toWorld(ev->pos());

        if (m_arcStage == 0) {
            m_arcStart = clickPoint;
            m_arcStage = 1;
        } else if (m_arcStage == 1) {
            m_arcMid = clickPoint;
            m_arcStage = 2;
        } else if (m_arcStage == 2) {
            m_arcEnd = clickPoint;
            auto arc = std::make_unique<ArcEntity>(m_arcStart, m_arcMid, m_arcEnd);
            m_entities.push_back(std::move(arc));

            // reset arc state
            m_arcStage = 0;
            m_mode = Normal;  // finish arc command
        }

        update();
        return;
    }
}
void CadView2D::mouseMoveEvent(QMouseEvent *ev)  {
    QPoint pos = ev->pos();
    m_mouseWorld = toWorld(pos);
    if (m_panning) {
        QPoint delta = pos - m_panStart;
        m_panStart = pos;
        m_transform.translate(delta.x(), delta.y());
        update();
    } else if (m_rubberActive) {
        m_rubberEnd = pos;
        update();
    } else {
        update();
    }

    if (m_mode == DrawArc && m_arcStage > 0) {
        m_mouseWorld = toWorld(ev->pos());
        update(); // redraw preview
    }

}
void CadView2D::mouseReleaseEvent(QMouseEvent *ev)  {
    if (ev->button() == Qt::MiddleButton) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
    } else if (ev->button() == Qt::LeftButton) {
        m_rubberActive = false;
        QRect r(m_rubberStart, m_rubberEnd);
        // convert rect to world and ideally select objects
        QRectF worldRect = QRectF(toWorld(r.topLeft()), toWorld(r.bottomRight())).normalized();
        qDebug() << "Rubber selection in world:" << worldRect;
        update();
    }
}
void CadView2D::wheelEvent(QWheelEvent *ev)  {
    // zoom around cursor (world point under mouse stays fixed)
    QPointF cursorPos = ev->position();
    QPointF worldBefore = toWorld(cursorPos);

    double zoomFactor = std::pow(1.0015, ev->angleDelta().y());
    m_transform.translate(cursorPos.x(), cursorPos.y());
    m_transform.scale(zoomFactor, zoomFactor);
    m_transform.translate(-cursorPos.x(), -cursorPos.y());

    QPointF worldAfter = toWorld(cursorPos);
    QPointF deltaWorld = worldAfter - worldBefore;
    m_transform.translate(deltaWorld.x() * m_scale, deltaWorld.y() * m_scale);

    update();
}

void CadView2D::keyPressEvent(QKeyEvent *ev) {
    if (m_mode == DrawLine) {
        if (ev->key() == Qt::Key_Escape) {
            // cancel current sequence
            m_lineActive = false;
            m_polylineMode = false;
            update();
            return;
        }
        if (ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Enter) {
            // finish polyline on Enter
            m_lineActive = false;
            m_polylineMode = false;
            m_mode = Normal;
            update();
            return;
        }
    }

    if (m_mode == DrawArc && ev->key() == Qt::Key_Escape) {
        // cancel in-progress arc
        m_arcStage = 0;
        update();
        return;
    }

    // fallback: let base class handle unprocessed keys
    QWidget::keyPressEvent(ev); // fallback
}

void CadView2D::printView() {
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageOrientation(QPageLayout::Landscape);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() == QDialog::Accepted) {
        QPainter painter(&printer);
        render(&painter); // render widget to printer
    }
}

void CadView2D::exportPdf(const QString &file) {
    QPdfWriter pdf(file);
    pdf.setPageOrientation(QPageLayout::Landscape);
    pdf.setPageSize(QPageSize(QPageSize::A4));

    QPainter painter(&pdf);
    QRect pageRect = pdf.pageLayout().paintRectPixels(pdf.resolution());
    QRectF srcRect = rect();

    qreal sx = (qreal)pageRect.width()  / srcRect.width();
    qreal sy = (qreal)pageRect.height() / srcRect.height();
    qreal s = qMin(sx, sy);

    painter.translate(pageRect.center());
    painter.scale(s, s);
    painter.translate(-srcRect.center());
    render(&painter); // render widget to PDF
}
