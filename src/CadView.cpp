#include "CadView.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QPrintDialog>
#include <cmath>

// ctor
CadView::CadView(QWidget *parent) : QOpenGLWidget(parent), m_scale(1.0) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void CadView::setViewMode(ViewMode m) {
    m_viewMode = m;

    if (m_viewMode == Mode2D) {
        // 相機正視 XY 平面
        QMatrix4x4 view;
        view.lookAt(
            QVector3D(0, 0, 10),   // 相機位置：Z 軸正向
            QVector3D(0, 0, 0),    // 注視點：原點
            QVector3D(0, 1, 0)     // Up：Y 軸
            );

        m_proj.setToIdentity();
        // 投影大小依視窗大小決定，確保單位比例正確
        float w = width() / 100.0f;   // 可調整 scale
        float h = height() / 100.0f;
        m_proj.ortho(-w, w, -h, h, 0.1f, 100.0f);

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(m_proj.constData());
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(view.constData());
    }
    else if (m_viewMode == Mode3D) {
        // 使用 TrackballCamera 控制
        m_proj.setToIdentity();
        m_proj.perspective(45.0f, float(width())/height(), 0.1f, 100.0f);
    }

    update();
}

// -------------------- Mode --------------------
void CadView::setMode(Mode m) {
    m_mode = m;
    m_lineActive = false;
    m_arcStage = 0;
    update();
}

// -------------------- Save / Load --------------------
void CadView::saveEntities(const QString &file) {
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly|QIODevice::Text)) return;
    QTextStream out(&f);
    for (const auto &ent : m_entities)
        ent->save(out);
}

void CadView::loadEntities(const QString &file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&f);
    m_entities.clear();
    QString type;
    while (!in.atEnd()) {
        in >> type;
        auto ent = loadEntity(in, type);
        if (ent) m_entities.push_back(std::move(ent));
    }
    update();
}

// -------------------- OpenGL lifecycle --------------------
void CadView::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(1.0f,1.0f,1.0f,1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LINE_SMOOTH);
}

void CadView::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    m_proj.setToIdentity();
    float aspect = float(w) / qMax(h, 1);
    if (m_ortho) {
        float s = 5.0f;
        m_proj.ortho(-s * aspect, s * aspect, -s, s, 0.01f, 1000.0f);
    } else {
        m_proj.perspective(60.0f, aspect, 0.01f, 1000.0f);
    }
    updateTransform();
}

void CadView::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_viewMode == Mode2D) {
        // Render 2D sketch using simple OpenGL lines
        paint2D();  // replace QPainter rendering
    } else if (m_viewMode == Mode3D) {
        paint3D();
    }
}

// -------------------- 2D Drawing --------------------
QPointF CadView::toScreen(const QPointF &world) const { return m_transform.map(world); }
QPointF CadView::toWorld(const QPointF &screen) const { return m_transform.inverted().map(screen); }

void CadView::updateTransform() {
    if (m_transform.isIdentity()) {
        m_transform.translate(width()/2.0, height()/2.0);
    }
}

void CadView::paint2D() {
    // --- Projection, Y 軸向上 ---
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width(), 0, height(), -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glPushMatrix();
    // --- Apply QTransform as 4x4 ---
    QTransform t = m_transform;
    GLdouble mat[16] = {
        t.m11(), t.m12(), 0, 0,
        t.m21(), t.m22(), 0, 0,
        0,       0,       1, 0,
        t.m31(), t.m32(), 0, 1
    };
    glMultMatrixd(mat);

    // ---- Rectangle (填充) ----
    glColor4f(0.0f, 0.0f, 1.0f, 0.16f); // 藍色，透明度約 40/255 ≈ 0.16
    glBegin(GL_QUADS);
    glVertex2f(50.0f, 50.0f);
    glVertex2f(250.0f, 50.0f);
    glVertex2f(250.0f, 170.0f);
    glVertex2f(50.0f, 170.0f);
    glEnd();

    // ---- Rectangle (邊框) ----
    glColor3f(0.0f, 0.0f, 1.0f); // 藍色線
    glBegin(GL_LINE_LOOP);
    glVertex2f(50.0f, 50.0f);
    glVertex2f(250.0f, 50.0f);
    glVertex2f(250.0f, 170.0f);
    glVertex2f(50.0f, 170.0f);
    glEnd();

    glColor3f(0.0f, 0.8f, 0.0f); // draw entities in green
    glBegin(GL_LINES);
    for (auto &ent : m_entities) {
        if (auto line = dynamic_cast<LineEntity*>(ent.get())) {
            glVertex2d(line->p1.x(), line->p1.y());
            glVertex2d(line->p2.x(), line->p2.y());
        } else if (auto arc = dynamic_cast<ArcEntity*>(ent.get())) {
            // approximate arc with segments
            int segments = 32;
            double start = arc->m_startAngle, sweep = arc->m_sweepAngle;
            for (int i=0;i<segments;i++) {
                double t0 = start + sweep*i/segments;
                double t1 = start + sweep*(i+1)/segments;
                glVertex2d(arc->m_center.x() + arc->m_radius*cos(t0),
                           arc->m_center.y() + arc->m_radius*sin(t0));
                glVertex2d(arc->m_center.x() + arc->m_radius*cos(t1),
                           arc->m_center.y() + arc->m_radius*sin(t1));
            }
        }
    }
    glEnd();
    glPopMatrix();
}

void CadView::drawGrid() {
    double spacing = 10.0 / m_scale; // grid spacing (world units)

    // convert screen rect corners into world coords
    QPointF topLeft     = toWorld(QPointF(0, 0));
    QPointF bottomRight = toWorld(QPointF(width(), height()));

    QRectF worldRect(topLeft, bottomRight);

    double startX = std::floor(worldRect.left() / spacing) * spacing;
    double endX   = std::ceil(worldRect.right() / spacing) * spacing;
    double startY = std::floor(worldRect.top() / spacing) * spacing;
    double endY   = std::ceil(worldRect.bottom() / spacing) * spacing;

    glColor3f(0.9f, 0.9f, 0.9f);
    glBegin(GL_LINES);

    for (double x = startX; x <= endX; x += spacing) {
        glVertex2d(x, worldRect.top());
        glVertex2d(x, worldRect.bottom());
    }
    for (double y = startY; y <= endY; y += spacing) {
        glVertex2d(worldRect.left(), y);
        glVertex2d(worldRect.right(), y);
    }

    glEnd();
}

// -------------------- 3D Drawing --------------------
void CadView::paint3D() {
    QMatrix4x4 view=m_camera.viewMatrix();
    drawAxis(view);
    drawCube(view);
}

void CadView::drawAxis(const QMatrix4x4 &view) {
    QMatrix4x4 mvp=m_proj*view;
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(mvp.constData());
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor3f(1,0,0); glVertex3f(0,0,0); glVertex3f(1,0,0);
    glColor3f(0,1,0); glVertex3f(0,0,0); glVertex3f(0,1,0);
    glColor3f(0,0,1); glVertex3f(0,0,0); glVertex3f(0,0,1);
    glEnd();
}

void CadView::drawCube(const QMatrix4x4 &view) {
    QMatrix4x4 model; model.setToIdentity(); model.scale(1.0f);
    QMatrix4x4 mvp=m_proj*view*model;
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(mvp.constData());
    glBegin(GL_QUADS);
    glColor3f(0.8f,0.2f,0.2f);
    glVertex3f(1,1,-1); glVertex3f(-1,1,-1); glVertex3f(-1,1,1); glVertex3f(1,1,1);
    glColor3f(0.2f,0.8f,0.2f);
    glVertex3f(1,-1,1); glVertex3f(-1,-1,1); glVertex3f(-1,-1,-1); glVertex3f(1,-1,-1);
    glColor3f(0.2f,0.2f,0.8f);
    glVertex3f(1,1,1); glVertex3f(-1,1,1); glVertex3f(-1,-1,1); glVertex3f(1,-1,1);
    glColor3f(0.8f,0.8f,0.2f);
    glVertex3f(1,-1,-1); glVertex3f(-1,-1,-1); glVertex3f(-1,1,-1); glVertex3f(1,1,-1);
    glColor3f(0.8f,0.2f,0.8f);
    glVertex3f(-1,1,1); glVertex3f(-1,1,-1); glVertex3f(-1,-1,-1); glVertex3f(-1,-1,1);
    glColor3f(0.2f,0.8f,0.8f);
    glVertex3f(1,1,-1); glVertex3f(1,1,1); glVertex3f(1,-1,1); glVertex3f(1,-1,-1);
    glEnd();
}

// -------------------- Input --------------------
void CadView::mousePressEvent(QMouseEvent *ev) {
    if (m_mode==Sketch2D||m_mode==DrawLine||m_mode==DrawArc) {
        if (ev->button()==Qt::MiddleButton) {
            m_panning=true; m_panStart=ev->pos(); setCursor(Qt::ClosedHandCursor);
        } else if (ev->button()==Qt::LeftButton) {
            if (m_mode==DrawLine) {
                QPointF click=toWorld(ev->pos());
                if (!m_lineActive) { m_lineStart=click; m_lineActive=true; m_polylineMode=true; }
                else { auto line=std::make_unique<LineEntity>(m_lineStart,click);
                    m_entities.push_back(std::move(line));
                    m_lineStart=click; m_lineActive=true; m_polylineMode=true; }
                update(); return;
            }
            if (m_mode==DrawArc) {
                QPointF click=toWorld(ev->pos());
                if (m_arcStage==0) { m_arcStart=click; m_arcStage=1; }
                else if (m_arcStage==1) { m_arcMid=click; m_arcStage=2; }
                else if (m_arcStage==2) { m_arcEnd=click;
                    auto arc=std::make_unique<ArcEntity>(m_arcStart,m_arcMid,m_arcEnd);
                    m_entities.push_back(std::move(arc));
                    m_arcStage=0; m_mode=Normal; }
                update(); return;
            }
        }
        if (m_mode==DrawLine && ev->button()==Qt::RightButton) {
            m_lineActive=false; m_polylineMode=false; m_mode=Normal; update(); return;
        }
    } else {
        m_lastPos=ev->pos();
        if (ev->button()==Qt::LeftButton) m_orbit=true;
        if (ev->button()==Qt::MiddleButton) m_pan=true;
    }
}

void CadView::mouseMoveEvent(QMouseEvent *ev) {
    QPoint pos=ev->pos();
    m_mouseWorld=toWorld(pos);
    if (m_mode==DrawArc && m_arcStage>0) {
        glColor3f(0.0f,0.0f,1.0f); // blue
        glBegin(GL_LINES);
        ArcDef def;
        if (circleFrom3Points(m_arcStart, m_arcMid, m_mouseWorld, def)) {
            int segments=32;
            for(int i=0;i<segments;i++){
                double t0 = def.startAngle + def.sweepAngle*i/segments;
                double t1 = def.startAngle + def.sweepAngle*(i+1)/segments;
                glVertex2d(def.center.x()+def.radius*cos(t0),
                           def.center.y()+def.radius*sin(t0));
                glVertex2d(def.center.x()+def.radius*cos(t1),
                           def.center.y()+def.radius*sin(t1));
            }
        } else {
            // fallback: draw line
            glVertex2d(m_arcStart.x(), m_arcStart.y());
            glVertex2d(m_mouseWorld.x(), m_mouseWorld.y());
        }
        glEnd();
    }
}

void CadView::mouseReleaseEvent(QMouseEvent *ev) {
    if (m_mode==Sketch2D||m_mode==DrawLine||m_mode==DrawArc) {
        if (ev->button()==Qt::MiddleButton) { m_panning=false; setCursor(Qt::ArrowCursor); }
    } else {
        m_orbit=m_pan=false;
    }
}

void CadView::wheelEvent(QWheelEvent *ev) {
    if (m_mode==Sketch2D||m_mode==DrawLine||m_mode==DrawArc) {
        QPointF cursor=ev->position();
        QPointF worldBefore=toWorld(cursor);
        double zoom=std::pow(1.0015, ev->angleDelta().y());
        m_transform.translate(cursor.x(),cursor.y());
        m_transform.scale(zoom,zoom);
        m_transform.translate(-cursor.x(),-cursor.y());
        QPointF worldAfter=toWorld(cursor);
        QPointF delta=worldAfter-worldBefore;
        m_transform.translate(delta.x()*m_scale,delta.y()*m_scale);
        update();
    } else {
        m_camera.zoomBy(ev->angleDelta().y()); update();
    }
}

void CadView::keyPressEvent(QKeyEvent *ev) {
    if (m_mode==DrawLine) {
        if (ev->key()==Qt::Key_Escape) { m_lineActive=false; m_polylineMode=false; update(); return; }
        if (ev->key()==Qt::Key_Return||ev->key()==Qt::Key_Enter) {
            m_lineActive=false; m_polylineMode=false; m_mode=Normal; update(); return; }
    }
    if (m_mode==DrawArc && ev->key()==Qt::Key_Escape) { m_arcStage=0; update(); return; }
    QOpenGLWidget::keyPressEvent(ev);
}

// -------------------- Printing --------------------
void CadView::printView() {
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageOrientation(QPageLayout::Landscape);
    QPrintDialog dlg(&printer,this);
    if (dlg.exec()==QDialog::Accepted) {
        QPainter painter(&printer);
        render(&painter);
    }
}

void CadView::exportPdf(const QString &file) {
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
