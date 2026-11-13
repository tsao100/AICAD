/**
 * @file DrawingBookActions.cpp
 */
#include "command/DrawingBookActions.h"

#include "cad/Document.h"
#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "drawing/DrawingSheet.h"
#include "drawing/DrawingSheetManager.h"
#include "drawing/DrawingSheetWidget.h"   // 內含 DrawingSheetDialog
#include "drawing/PlanProfileDialog.h"
#include "drawing/PlanProfileGenerator.h"
#include "drawing/PlanProfileSettings.h"
#include "railway/RailwayAlignment.h"
#include "drawing/PlanProfilePainter.h"
#include "core/geometry/ProjectOrigin.h"
#include "ui/UIManager.h"
#include "view/CadView.h"
#include "command/CommandManager.h"

#include <BRepBuilderAPI_MakePolygon.hxx>
#include <AIS_Shape.hxx>
#include <AIS_TextLabel.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Quantity_Color.hxx>
#include <gp_Pnt.hxx>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QPointer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QElapsedTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>

#include <QWidget>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QJsonArray>
#include <QUuid>

namespace aicad {
namespace command {

using namespace drawing;

namespace {

void say(const QString& m)
{
    if (auto* clm = core::Application::instance()->commandLineManager())
        clm->printMessage(QStringLiteral("[DSH] ") + m);
}

void openSheetDialog(DrawingSheetManager* mgr, cad::Document* doc, DrawingSheet* sh, QWidget* parent)
{
    if (!sh) return;
    mgr->setActiveSheet(sh);
    const QJsonArray before = mgr->toJson();
    DrawingSheetDialog dlg(sh, parent);
    dlg.exec();
    if (mgr->toJson() != before) {
        doc->setModified(true);
        mgr->touch();
    }
}

QVector<DrawingSheet*> sheetsOfBook(DrawingSheetManager* mgr, const QString& bookId)
{
    QVector<DrawingSheet*> v;
    for (DrawingSheet* s : mgr->sheets()) if (s->bookId() == bookId) v.append(s);
    return v;
}

QHash<QString, Handle(AIS_Shape)>& depotShapes()
{
    static QHash<QString, Handle(AIS_Shape)> h;
    return h;
}

QHash<QString, Handle(AIS_TextLabel)>& depotLabels()
{
    static QHash<QString, Handle(AIS_TextLabel)> h;
    return h;
}

double depotZ(cad::Document* doc)
{
    for (railway::TrackCenterLine* t : doc->trackCenterLines()) {
        double a = 0, b = 0;
        if (t && t->vertical() && !t->vertical()->isEmpty() && planProfileChainageRange(t, a, b))
            return t->getElevationForCalc(a);
    }
    return 0.0;
}

Handle(AIS_Shape) buildDepotShape(cad::Document* doc, const DepotView& v)
{
    if (!(v.w > 0) || !(v.h > 0)) return {};
    core::geometry::ProjectOrigin::ensureDefault();
    const QPointF c = core::geometry::ProjectOrigin::instance().toLocal(v.e, v.n);
    const double th = planProfileDepotAngle(doc, v);
    const double ex = std::cos(th), ey = std::sin(th);
    const double z = depotZ(doc);
    auto corner = [&](double sx, double sy) {
        const double u = sx * v.w / 2, w = sy * v.h / 2;
        return gp_Pnt(c.x() + u * ex - w * ey, c.y() + u * ey + w * ex, z);
    };
    BRepBuilderAPI_MakePolygon poly;
    poly.Add(corner(-1, -1)); poly.Add(corner(1, -1)); poly.Add(corner(1, 1));
    poly.Add(corner(-1, 1)); poly.Add(corner(-1, -1));
    if (!poly.IsDone()) return {};
    Handle(AIS_Shape) shape = new AIS_Shape(poly.Shape());
    shape->SetColor(Quantity_Color(1.0, 0.55, 0.0, Quantity_TOC_RGB));
    shape->SetWidth(2.5);
    shape->SetDisplayMode(0);
    return shape;
}

view::CadView* currentCadView()
{
    auto* ui = core::Application::instance()->uiManager();
    return ui ? ui->cadView() : nullptr;
}

QString depotKey(const QString& bookId, int idx) { return QStringLiteral("%1#%2").arg(bookId).arg(idx); }


QString g_editBook;   // 正在 View 中編輯的圖冊：refreshDepotOverlays 不重畫它的視圖（由編輯器自己畫）

/** 把第 idx 個視圖移到序號 newPos（1 起算），其餘依原順序遞補，並重新編成 1..N。 */
void reorderDepotViews(QVector<DepotView>& views, int idx, int newPos)
{
    QVector<int> ord;
    for (int i = 0; i < views.size(); ++i) ord.append(i);
    std::stable_sort(ord.begin(), ord.end(), [&](int a, int b) { return views[a].order < views[b].order; });
    ord.removeOne(idx);
    ord.insert(std::max(0, std::min(newPos - 1, ord.size())), idx);
    for (int pos = 0; pos < ord.size(); ++pos) views[ord[pos]].order = pos + 1;
}

int depotPosition(const QVector<DepotView>& views, int idx)
{
    int pos = 1;
    for (int i = 0; i < views.size(); ++i)
        if (i != idx && (views[i].order < views[idx].order || (views[i].order == views[idx].order && i < idx))) ++pos;
    return pos;
}

/** 矩形中心的序號文字（螢幕固定字高）。序號＝依排序欄的順位，與機廠圖頁、Key Plan 一致。 */
Handle(AIS_TextLabel) buildDepotLabel(cad::Document* doc, const QVector<DepotView>& views, int i, bool current)
{
    if (i < 0 || i >= views.size()) return {};
    core::geometry::ProjectOrigin::ensureDefault();
    const QPointF c = core::geometry::ProjectOrigin::instance().toLocal(views[i].e, views[i].n);
    Handle(AIS_TextLabel) t = new AIS_TextLabel();
    t->SetText(QString::number(depotPosition(views, i)).toStdString().c_str());
    t->SetPosition(gp_Pnt(c.x(), c.y(), depotZ(doc)));
    t->SetColor(current ? Quantity_Color(1.0, 0.1, 0.1, Quantity_TOC_RGB) : Quantity_Color(0.0, 0.2, 0.9, Quantity_TOC_RGB));
    t->SetHeight(current ? 34.0 : 28.0);
    t->SetZLayer(Graphic3d_ZLayerId_Top);
    return t;
}

/**
 * 在 View 中以滑鼠編輯機廠矩形視圖。編輯期間顯示該圖冊的全部視圖（目前編輯者紅色、其餘橘色）。
 *  - 左鍵拖曳：依目前操作（移動／移動全部／旋轉／調整大小）；在別的矩形內按下會改為編輯那一個
 *  - 對齊：左鍵點一條線形
 *  - 「移動」操作下，游標靠近任一矩形的邊線會出現「＋」，左鍵點一下就在該邊新增緊鄰的矩形
 *  - 右鍵點一下開選單（切換操作、變更序號、詳細設定、結束）；ESC／Enter 結束
 * 以 event filter 攔截 CadView 的滑鼠事件，編輯期間左鍵不做一般選取。
 */
class DepotViewEditor : public QObject
{
public:
    enum Op { Move, MoveAll, Rotate, Resize, Align };

    DepotViewEditor(cad::Document* doc, view::CadView* view, const QString& bookId, int idx, QWidget* parent)
        : QObject(nullptr), m_doc(doc), m_view(view), m_book(bookId), m_idx(idx), m_parent(parent)
    {
        if (!load()) { m_dead = true; return; }
        g_editBook = m_book;
        markAllShown();
        m_view->installEventFilter(this);
        syncShapes();
        refreshDepotOverlays(m_doc);
        hint();
    }

    ~DepotViewEditor() override
    {
        if (m_view) {
            m_view->removeEventFilter(this);
            for (const Handle(AIS_Shape)& s : m_shapes) if (!s.IsNull()) m_view->removeOverlayAIS(s);
            for (const Handle(AIS_TextLabel)& s : m_labels) if (!s.IsNull()) m_view->removeOverlayAIS(s);
            if (!m_plus.IsNull()) m_view->removeOverlayAIS(m_plus);
        }
        g_editBook.clear();
        if (m_doc) refreshDepotOverlays(m_doc);
    }

    bool dead() const { return m_dead; }

    bool eventFilter(QObject* obj, QEvent* ev) override
    {
        if (obj != m_view || m_dead) return false;
        switch (ev->type()) {
        case QEvent::MouseButtonPress: {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton) { onPress(me->pos()); return true; }
            if (me->button() == Qt::RightButton) { m_rPos = me->pos(); m_rMoved = false; m_rDown = true; }
            return false;
        }
        case QEvent::MouseMove: {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (m_dragging) { onMove(me->pos(), false); return true; }
            if (m_rDown && (me->pos() - m_rPos).manhattanLength() > 4) m_rMoved = true;
            if (!m_rDown) onHover(me->pos(), false);
            return false;
        }
        case QEvent::MouseButtonRelease: {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton && m_dragging) { onMove(me->pos(), true); m_dragging = false; commit(); return true; }
            if (me->button() == Qt::RightButton) {
                const bool click = m_rDown && !m_rMoved;
                m_rDown = false;
                if (click) {
                    const QPoint gp = me->globalPos();
                    QTimer::singleShot(0, this, [this, gp]() { showMenu(gp); });
                }
            }
            return false;
        }
        case QEvent::KeyPress: {
            auto* ke = static_cast<QKeyEvent*>(ev);
            if (ke->key() == Qt::Key_Escape || ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                finish();
                return true;
            }
            return false;
        }
        default: return false;
        }
    }

private:
    struct Hover { bool valid = false; int view = -1, dir = 0; QPointF at; };

    DepotView& cur() { return m_views[m_idx]; }

    bool load()
    {
        DrawingSheetManager* mgr = DrawingSheetManager::forDocument(m_doc);
        DrawingBook* b = mgr ? mgr->findBook(m_book) : nullptr;
        if (!b) return false;
        PlanProfileSettings g;
        g.fromJson(b->settings);
        m_views = g.depotViews;
        return m_idx >= 0 && m_idx < m_views.size();
    }

    void markAllShown()
    {
        for (int i = 0; i < m_views.size(); ++i) m_doc->shownDepotViews().insert(depotKey(m_book, i));
    }

    void finish()
    {
        say(QStringLiteral("結束機廠視圖編輯。"));
        m_dead = true;
        deleteLater();
    }

    void hint()
    {
        static const char* kOp[5] = { "移動（左鍵拖放；邊線上的＋可新增緊鄰矩形）", "移動全部矩形（左鍵拖放）",
                                      "旋轉（左鍵拖曳）", "調整大小（左鍵拖曳最近的邊）", "對齊（左鍵點一條線形）" };
        say(QStringLiteral("編輯「%1」：目前操作＝%2；右鍵點一下開選單；ESC 結束。")
                .arg(cur().name, QString::fromUtf8(kOp[m_op])));
    }

    QPointF localCentre(const DepotView& v) const
    {
        core::geometry::ProjectOrigin::ensureDefault();
        return core::geometry::ProjectOrigin::instance().toLocal(v.e, v.n);
    }
    void setLocalCentre(DepotView& v, const QPointF& c) const
    {
        const QPointF g = core::geometry::ProjectOrigin::instance().toGlobal(c);
        v.e = g.x(); v.n = g.y();
    }

    double worldPerPixel(const QPoint& pos) const
    {
        const QPointF a = m_view->screenToPlaneD(pos), b = m_view->screenToPlaneD(pos + QPoint(1, 0));
        return std::hypot(b.x() - a.x(), b.y() - a.y());
    }

    /** P（模型座標）在視圖 v 座標系中的 (u,v)。 */
    QPointF toFrame(const DepotView& v, const QPointF& P) const
    {
        const QPointF c = localCentre(v);
        const double th = planProfileDepotAngle(m_doc, v);
        const double ex = std::cos(th), ey = std::sin(th);
        const double dx = P.x() - c.x(), dy = P.y() - c.y();
        return QPointF(dx * ex + dy * ey, -dx * ey + dy * ex);
    }
    QPointF fromFrame(const DepotView& v, const QPointF& uv) const
    {
        const QPointF c = localCentre(v);
        const double th = planProfileDepotAngle(m_doc, v);
        const double ex = std::cos(th), ey = std::sin(th);
        return QPointF(c.x() + uv.x() * ex - uv.y() * ey, c.y() + uv.x() * ey + uv.y() * ex);
    }

    bool contains(int i, const QPointF& P) const
    {
        const QPointF q = toFrame(m_views[i], P);
        return std::fabs(q.x()) <= m_views[i].w / 2 && std::fabs(q.y()) <= m_views[i].h / 2;
    }
    int hitView(const QPointF& P) const
    {
        for (int i = 0; i < m_views.size(); ++i) if (contains(i, P)) return i;
        return -1;
    }

    Handle(AIS_Shape) makeShape(int i) const
    {
        Handle(AIS_Shape) s = buildDepotShape(m_doc, m_views[i]);
        if (!s.IsNull()) {
            if (i == m_idx) { s->SetColor(Quantity_Color(1.0, 0.1, 0.1, Quantity_TOC_RGB)); s->SetWidth(3.5); }
        }
        return s;
    }

    void setLabel(int i)
    {
        while (m_labels.size() <= i) m_labels.append(Handle(AIS_TextLabel)());
        if (!m_labels[i].IsNull()) m_view->removeOverlayAIS(m_labels[i]);
        m_labels[i] = buildDepotLabel(m_doc, m_views, i, i == m_idx);
        if (!m_labels[i].IsNull()) m_view->addOverlayAIS(m_labels[i]);
    }

    void setShape(int i, const Handle(AIS_Shape)& s)
    {
        setLabel(i);
        if (i < m_shapes.size() && !m_shapes[i].IsNull()) m_view->removeOverlayAIS(m_shapes[i]);
        while (m_shapes.size() <= i) m_shapes.append(Handle(AIS_Shape)());
        m_shapes[i] = s;
        if (!s.IsNull()) m_view->addOverlayAIS(s, {0});
    }

    /** 全部重畫（初始、新增、換編輯對象、序號變更後）。 */
    void syncShapes()
    {
        for (int i = m_views.size(); i < m_shapes.size(); ++i)
            if (!m_shapes[i].IsNull()) m_view->removeOverlayAIS(m_shapes[i]);
        for (int i = m_views.size(); i < m_labels.size(); ++i)
            if (!m_labels[i].IsNull()) m_view->removeOverlayAIS(m_labels[i]);
        m_shapes.resize(m_views.size());
        m_labels.resize(m_views.size());
        for (int i = 0; i < m_views.size(); ++i) setShape(i, makeShape(i));
    }

    /** 拖曳中的即時更新：moveAll 重畫全部，否則只重畫目前這個。 */
    void liveShapes(bool all, bool force)
    {
        if (!force && m_throttle.isValid() && m_throttle.elapsed() < 30) return;
        m_throttle.start();
        if (all) { for (int i = 0; i < m_views.size(); ++i) setShape(i, makeShape(i)); }
        else setShape(m_idx, makeShape(m_idx));
    }

    void commit()
    {
        syncShapes();
        DrawingSheetManager* mgr = DrawingSheetManager::forDocument(m_doc);
        QString err;
        if (!updatePlanProfileDepotViews(mgr, m_doc, m_book, m_views, &err)) say(err);
        cad::Document* doc = m_doc;
        QTimer::singleShot(0, doc, [doc]() { Q_EMIT doc->treeStructureChanged(); });
    }

    // ── 邊線 hover 的「＋」──────────────────────────────────────────────
    void clearHover()
    {
        if (!m_plus.IsNull()) { m_view->removeOverlayAIS(m_plus); m_plus.Nullify(); }
        m_hover = Hover();
    }

    Hover findHover(const QPoint& pos) const
    {
        Hover h;
        const QPointF P = m_view->screenToPlaneD(pos);
        const double tol = worldPerPixel(pos) * 10.0;
        double best = tol;
        for (int i = 0; i < m_views.size(); ++i) {
            const DepotView& v = m_views[i];
            const QPointF q = toFrame(v, P);
            const double hw = v.w / 2, hh = v.h / 2;
            const double cu = std::max(-hw, std::min(hw, q.x())), cv = std::max(-hh, std::min(hh, q.y()));
            const double d[4] = { std::hypot(q.x() - hw, q.y() - cv), std::hypot(q.x() + hw, q.y() - cv),
                                  std::hypot(q.x() - cu, q.y() - hh), std::hypot(q.x() - cu, q.y() + hh) };
            const QPointF at[4] = { QPointF(hw, cv), QPointF(-hw, cv), QPointF(cu, hh), QPointF(cu, -hh) };
            for (int k = 0; k < 4; ++k) {
                if (d[k] < best) {
                    best = d[k];
                    h.valid = true; h.view = i; h.dir = k; h.at = fromFrame(v, at[k]);
                }
            }
        }
        return h;
    }

    void showPlus(const Hover& h, const QPoint& pos)
    {
        const double s = worldPerPixel(pos) * 8.0;
        const double z = depotZ(m_doc);
        BRepBuilderAPI_MakePolygon poly;
        poly.Add(gp_Pnt(h.at.x() - s, h.at.y(), z)); poly.Add(gp_Pnt(h.at.x() + s, h.at.y(), z));
        poly.Add(gp_Pnt(h.at.x(), h.at.y(), z));
        poly.Add(gp_Pnt(h.at.x(), h.at.y() + s, z)); poly.Add(gp_Pnt(h.at.x(), h.at.y() - s, z));
        if (!poly.IsDone()) return;
        if (!m_plus.IsNull()) m_view->removeOverlayAIS(m_plus);
        m_plus = new AIS_Shape(poly.Shape());
        m_plus->SetColor(Quantity_Color(0.0, 0.7, 0.1, Quantity_TOC_RGB));
        m_plus->SetWidth(4.0);
        m_view->addOverlayAIS(m_plus, {0});
    }

    void onHover(const QPoint& pos, bool force)
    {
        if (m_op != Move) { if (m_hover.valid) clearHover(); return; }
        if (!force && m_hoverClock.isValid() && m_hoverClock.elapsed() < 40) return;
        m_hoverClock.start();
        const Hover h = findHover(pos);
        if (!h.valid) { if (m_hover.valid) clearHover(); return; }
        const double wpp = worldPerPixel(pos);
        const bool same = m_hover.valid && m_hover.view == h.view && m_hover.dir == h.dir &&
                          std::hypot(h.at.x() - m_hover.at.x(), h.at.y() - m_hover.at.y()) < wpp * 2.0;
        if (same) return;
        m_hover = h;
        showPlus(h, pos);
    }

    /** 在第 src 個視圖的 dir 邊（0右 1左 2上 3下，依該視圖座標軸）新增緊鄰矩形，並改為編輯新矩形。 */
    void addAdjacent(int src, int dir)
    {
        const DepotView base = m_views[src];
        double su = 0, sv = 0;
        if (dir == 0) su = base.w; else if (dir == 1) su = -base.w;
        else if (dir == 2) sv = base.h; else sv = -base.h;
        DepotView nv = base;
        setLocalCentre(nv, fromFrame(base, QPointF(su, sv)));
        static const char* kDir[4] = { "右", "左", "上", "下" };
        nv.name = QStringLiteral("%1 %2").arg(base.name, QString::fromUtf8(kDir[dir]));
        nv.order = base.order + 1;
        for (DepotView& v : m_views) if (v.order > base.order) ++v.order;
        m_views.append(nv);
        m_idx = m_views.size() - 1;
        markAllShown();
        say(QStringLiteral("已在「%1」的%2側新增緊鄰矩形「%3」，目前編輯它。").arg(base.name, QString::fromUtf8(kDir[dir]), nv.name));
        commit();
    }

    // ── 滑鼠操作 ────────────────────────────────────────────────────────
    void onPress(const QPoint& pos)
    {
        if (m_op == Move) {
            const Hover h = findHover(pos);
            if (h.valid) { clearHover(); addAdjacent(h.view, h.dir); return; }
        }
        m_p0 = m_view->screenToPlaneD(pos);
        if (m_op == Align) { pickAlign(pos); return; }
        if (m_op != MoveAll && !contains(m_idx, m_p0)) {
            const int h = hitView(m_p0);
            if (h >= 0) { m_idx = h; syncShapes(); hint(); }
        }
        if (!m_plus.IsNull()) clearHover();
        m_views0 = m_views;
        if (m_op == Rotate) {
            const QPointF c = localCentre(m_views0[m_idx]);
            m_a0 = std::atan2(m_p0.y() - c.y(), m_p0.x() - c.x());
        }
        if (m_op == Resize) {
            const QPointF q = toFrame(m_views0[m_idx], m_p0);
            const DepotView& v = m_views0[m_idx];
            const double d[4] = { std::fabs(q.x() - v.w / 2), std::fabs(q.x() + v.w / 2),
                                  std::fabs(q.y() - v.h / 2), std::fabs(q.y() + v.h / 2) };
            m_edge = int(std::min_element(d, d + 4) - d);
        }
        m_dragging = true;
    }

    void onMove(const QPoint& pos, bool final)
    {
        const QPointF P = m_view->screenToPlaneD(pos);
        const double dx = P.x() - m_p0.x(), dy = P.y() - m_p0.y();
        const DepotView& v0 = m_views0[m_idx];
        if (m_op == MoveAll) {
            for (int i = 0; i < m_views.size(); ++i) {
                const QPointF c = localCentre(m_views0[i]);
                setLocalCentre(m_views[i], QPointF(c.x() + dx, c.y() + dy));
            }
        } else if (m_op == Move) {
            const QPointF c = localCentre(v0);
            setLocalCentre(cur(), QPointF(c.x() + dx, c.y() + dy));
            snapToNeighbours(pos);
        } else if (m_op == Rotate) {
            const QPointF c = localCentre(v0);
            const double a = std::atan2(P.y() - c.y(), P.x() - c.x());
            double r = v0.rotDeg + qRadiansToDegrees(a - m_a0);
            while (r > 180) r -= 360;
            while (r <= -180) r += 360;
            cur().rotDeg = r;
        } else if (m_op == Resize) {
            const double th = planProfileDepotAngle(m_doc, v0);
            const double ex = std::cos(th), ey = std::sin(th);
            const double du = dx * ex + dy * ey, dv = -dx * ey + dy * ex;
            DepotView& c = cur();
            c.w = v0.w; c.h = v0.h;
            double su = 0, sv = 0;
            if (m_edge == 0)      { c.w = std::max(1.0, v0.w + du); su = (c.w - v0.w) / 2; }
            else if (m_edge == 1) { c.w = std::max(1.0, v0.w - du); su = -(c.w - v0.w) / 2; }
            else if (m_edge == 2) { c.h = std::max(1.0, v0.h + dv); sv = (c.h - v0.h) / 2; }
            else                  { c.h = std::max(1.0, v0.h - dv); sv = -(c.h - v0.h) / 2; }
            const QPointF c0 = localCentre(v0);
            setLocalCentre(c, QPointF(c0.x() + su * ex - sv * ey, c0.y() + su * ey + sv * ex));
        }
        liveShapes(m_op == MoveAll, final);
    }

    /** 移動時若某邊離同向矩形的邊很近（10 像素內），就貼齊：緊靠（邊線重合）並可再與該邊端點／中心對齊。 */
    void snapToNeighbours(const QPoint& pos)
    {
        const double tol = worldPerPixel(pos) * 10.0;
        DepotView& c = cur();
        const double th = planProfileDepotAngle(m_doc, c);
        for (int j = 0; j < m_views.size(); ++j) {
            if (j == m_idx) continue;
            const DepotView& o = m_views[j];
            const double d = std::remainder(th - planProfileDepotAngle(m_doc, o), 3.14159265358979323846);
            if (std::fabs(d) > 0.003) continue;                       // 只貼齊方向相同者
            const QPointF q = toFrame(o, localCentre(c));
            const double hwo = o.w / 2, hho = o.h / 2, hwm = c.w / 2, hhm = c.h / 2;
            double nu = q.x(), nv = q.y();
            auto pick = [&](double val, std::initializer_list<double> targets, double& out) {
                double best = tol;
                bool found = false;
                for (double t : targets)
                    if (std::fabs(val - t) < best) { best = std::fabs(val - t); out = t; found = true; }
                return found;
            };
            bool su = false, sv = false;
            if (std::fabs(q.y()) < hho + hhm + tol) su = pick(q.x(), { hwo + hwm, -(hwo + hwm) }, nu);
            if (std::fabs(q.x()) < hwo + hwm + tol) sv = pick(q.y(), { hho + hhm, -(hho + hhm) }, nv);
            if (!su && !sv) continue;
            if (sv && !su) pick(q.x(), { hwo - hwm, -(hwo - hwm), 0.0 }, nu);     // 上下緊靠：左右邊／中心對齊
            if (su && !sv) pick(q.y(), { hho - hhm, -(hho - hhm), 0.0 }, nv);     // 左右緊靠：上下邊／中心對齊
            setLocalCentre(c, fromFrame(o, QPointF(nu, nv)));
            break;
        }
    }

    void pickAlign(const QPoint& pos)
    {
        const double tol = std::max(0.5, worldPerPixel(pos) * 12.0);
        QString bestId;
        double bestCh = 0, bestD = tol;
        for (railway::TrackCenterLine* t : m_doc->trackCenterLines()) {
            double a = 0, b = 0;
            if (!t || !planProfileChainageRange(t, a, b)) continue;
            const double step = std::max(2.0, tol);
            double cBest = a, dBest = 1e18;
            for (double x = a;; x += step) {
                const double ch = std::min(x, b);
                const QPointF p = t->getXYForCalc(ch);
                const double d = std::hypot(p.x() - m_p0.x(), p.y() - m_p0.y());
                if (d < dBest) { dBest = d; cBest = ch; }
                if (ch >= b) break;
            }
            if (dBest > tol + step) continue;
            for (double x = std::max(a, cBest - step); x <= std::min(b, cBest + step); x += 0.1) {
                const QPointF p = t->getXYForCalc(x);
                const double d = std::hypot(p.x() - m_p0.x(), p.y() - m_p0.y());
                if (d < dBest) { dBest = d; cBest = x; }
            }
            if (dBest < bestD) { bestD = dBest; bestId = t->id(); bestCh = cBest; }
        }
        if (bestId.isEmpty()) { say(QStringLiteral("附近沒有線形，請點在線形上。")); return; }
        cur().alignTcl = bestId;
        cur().alignCh = bestCh;
        cur().rotDeg = 0.0;
        const railway::TrackCenterLine* t = m_doc->findTrackCenterLine(bestId);
        say(QStringLiteral("已對齊線形 %1 @ %2。").arg(t ? t->name() : bestId).arg(bestCh, 0, 'f', 2));
        commit();
    }

    void changeOrder()
    {
        const int n = m_views.size();
        bool ok = false;
        const int np = QInputDialog::getInt(m_parent, QStringLiteral("變更視圖序號"),
            QStringLiteral("「%1」的新序號（1～%2）：").arg(cur().name).arg(n), depotPosition(m_views, m_idx), 1, n, 1, &ok);
        if (!ok) return;
        reorderDepotViews(m_views, m_idx, np);
        say(QStringLiteral("「%1」的序號改為 %2。").arg(cur().name).arg(np));
        commit();
    }

    void showMenu(const QPoint& gp)
    {
        if (m_dead) return;
        clearHover();
        QMenu menu;
        auto op = [&](const QString& text, Op o) {
            QAction* a = menu.addAction(text);
            a->setCheckable(true);
            a->setChecked(m_op == o);
            connect(a, &QAction::triggered, this, [this, o]() { m_op = o; hint(); });
        };
        op(QStringLiteral("移動（拖放；邊線 ＋ 新增緊鄰矩形）"), Move);
        op(QStringLiteral("移動全部矩形視圖（拖放）"), MoveAll);
        op(QStringLiteral("旋轉（拖曳）"), Rotate);
        op(QStringLiteral("調整大小（拖曳邊）"), Resize);
        op(QStringLiteral("對齊線形（點選一條線）"), Align);
        menu.addSeparator();
        QAction* ord = menu.addAction(QStringLiteral("變更序號…"));
        connect(ord, &QAction::triggered, this, [this]() { changeOrder(); });
        QAction* det = menu.addAction(QStringLiteral("詳細設定…"));
        connect(det, &QAction::triggered, this, [this]() {
            runDrawingBookAction(m_doc, m_parent, QStringLiteral("depotEdit"),
                                 QStringLiteral("dview_%1_%2").arg(m_book).arg(m_idx));
            if (!load()) finish(); else { markAllShown(); syncShapes(); }
        });
        QAction* done = menu.addAction(QStringLiteral("結束編輯"));
        connect(done, &QAction::triggered, this, [this]() { finish(); });
        menu.exec(gp);
    }

    QPointer<cad::Document> m_doc;
    QPointer<view::CadView> m_view;
    QString m_book;
    int m_idx = 0;
    QWidget* m_parent = nullptr;
    QVector<DepotView> m_views, m_views0;
    QVector<Handle(AIS_Shape)> m_shapes;
    QVector<Handle(AIS_TextLabel)> m_labels;
    Handle(AIS_Shape) m_plus;
    Hover m_hover;
    Op m_op = Move;
    bool m_dragging = false, m_dead = false, m_rDown = false, m_rMoved = false;
    QPoint m_rPos;
    QPointF m_p0;
    double m_a0 = 0;
    int m_edge = 0;
    QElapsedTimer m_throttle, m_hoverClock;
};

QPointer<DepotViewEditor> g_editor;

void runDepotAction(DrawingSheetManager* mgr, cad::Document* doc, QWidget* parent,
                    const QString& action, const QString& itemId)
{
    QString bookId;
    int idx = -1;
    if (itemId.startsWith(QLatin1String("depotfolder_"))) {
        bookId = itemId.mid(12);
    } else if (itemId.startsWith(QLatin1String("dview_"))) {
        const QString r = itemId.mid(6);
        const int p = r.lastIndexOf(QLatin1Char('_'));
        bookId = r.left(p);
        idx = r.mid(p + 1).toInt();
    }
    DrawingBook* book = bookId.isEmpty() ? nullptr : mgr->findBook(bookId);
    if (!book) return;
    PlanProfileSettings g;
    g.fromJson(book->settings);
    const int n = g.depotViews.size();
    QSet<QString>& shown = doc->shownDepotViews();

    auto deferTreeRefresh = [doc]() {
        QTimer::singleShot(0, doc, [doc]() { Q_EMIT doc->treeStructureChanged(); });
    };

    if (action == QLatin1String("depotShow") || action == QLatin1String("depotHide")) {
        if (idx < 0 || idx >= n) return;
        if (action == QLatin1String("depotShow")) shown.insert(depotKey(bookId, idx));
        else shown.remove(depotKey(bookId, idx));
    } else if (action == QLatin1String("depotShowAll") || action == QLatin1String("depotHideAll")) {
        for (int i = 0; i < n; ++i) {
            if (action == QLatin1String("depotShowAll")) shown.insert(depotKey(bookId, i));
            else shown.remove(depotKey(bookId, i));
        }
    } else if (action == QLatin1String("depotOrder")) {
        if (idx < 0 || idx >= n) return;
        bool ok = false;
        const int np = QInputDialog::getInt(parent, QStringLiteral("變更視圖序號"),
            QStringLiteral("「%1」的新序號（1～%2）：").arg(g.depotViews[idx].name).arg(n),
            depotPosition(g.depotViews, idx), 1, n, 1, &ok);
        if (!ok) return;
        QVector<DepotView> views = g.depotViews;
        reorderDepotViews(views, idx, np);
        QString err;
        if (!updatePlanProfileDepotViews(mgr, doc, bookId, views, &err)) { QMessageBox::warning(parent, QStringLiteral("機廠矩形視圖"), err); return; }
    } else if (action == QLatin1String("depotEditView")) {
        if (idx < 0 || idx >= n) return;
        view::CadView* cv = currentCadView();
        if (!cv) return;
        if (g_editor) { delete g_editor.data(); }
        g_editor = new DepotViewEditor(doc, cv, bookId, idx, parent);
        if (g_editor->dead()) { delete g_editor.data(); }
        return;
    } else if (action == QLatin1String("depotAdd")) {
        const auto sheets = sheetsOfBook(mgr, bookId);
        if (!sheets.isEmpty()) mgr->setActiveSheet(sheets.first());
        if (auto* cmdMgr = core::Application::instance()->commandManager())
            cmdMgr->executeCommand("depotview", QStringList());
        return;
    } else if (action == QLatin1String("depotEdit")) {
        PlanProfileDialog dlg(doc, g, parent);
        dlg.setLocalMode(false);
        dlg.setApplyMode(true);
        dlg.showDepotTab(idx);
        if (dlg.exec() != QDialog::Accepted) return;
        PlanProfileSettings ng = dlg.settings();
        ng.replaceExisting = true;
        QString err;
        if (!updatePlanProfileBook(mgr, doc, bookId, ng, &err)) {
            QMessageBox::warning(parent, QStringLiteral("機廠矩形視圖"), err.isEmpty() ? QStringLiteral("重算失敗") : err);
            return;
        }
        say(QStringLiteral("機廠視圖已更新並重算圖冊。"));
    } else if (action == QLatin1String("depotDelete")) {
        if (idx < 0 || idx >= n) return;
        if (QMessageBox::question(parent, QStringLiteral("刪除機廠視圖"),
                QStringLiteral("刪除視圖「%1」及其圖紙？").arg(g.depotViews[idx].name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        for (int i = 0; i < n; ++i) shown.remove(depotKey(bookId, i));   // 序號會位移，顯示狀態重置
        g.depotViews.removeAt(idx);
        g.replaceExisting = true;
        QString err;
        if (!updatePlanProfileBook(mgr, doc, bookId, g, &err)) {
            QMessageBox::warning(parent, QStringLiteral("機廠矩形視圖"), err.isEmpty() ? QStringLiteral("重算失敗") : err);
            return;
        }
    }
    refreshDepotOverlays(doc);
    deferTreeRefresh();
}

} // namespace

void refreshDepotOverlays(cad::Document* doc)
{
    view::CadView* view = currentCadView();
    if (!doc || !view) return;
    for (auto it = depotShapes().begin(); it != depotShapes().end(); ++it)
        view->removeOverlayAIS(it.value());
    depotShapes().clear();
    for (auto it = depotLabels().begin(); it != depotLabels().end(); ++it)
        view->removeOverlayAIS(it.value());
    depotLabels().clear();

    DrawingSheetManager* mgr = DrawingSheetManager::forDocument(doc);
    QSet<QString> valid;
    for (const QString& key : doc->shownDepotViews()) {
        const int p = key.lastIndexOf(QLatin1Char('#'));
        if (p <= 0) continue;
        DrawingBook* b = mgr->findBook(key.left(p));
        if (!b) continue;
        if (key.left(p) == g_editBook) { valid.insert(key); continue; }
        PlanProfileSettings g;
        g.fromJson(b->settings);
        const int idx = key.mid(p + 1).toInt();
        if (idx < 0 || idx >= g.depotViews.size()) continue;
        Handle(AIS_Shape) shape = buildDepotShape(doc, g.depotViews[idx]);
        if (shape.IsNull()) continue;
        view->addOverlayAIS(shape, {0});
        depotShapes().insert(key, shape);
        {
            Handle(AIS_TextLabel) lb = buildDepotLabel(doc, g.depotViews, idx, false);
            if (!lb.IsNull()) { view->addOverlayAIS(lb); depotLabels().insert(key, lb); }
        }
        valid.insert(key);
    }
    doc->shownDepotViews() = valid;
    view->refreshView();
}

void runDrawingBookAction(cad::Document* doc, QWidget* parent,
                          const QString& action, const QString& itemId)
{
    if (!doc) return;
    DrawingSheetManager* mgr = DrawingSheetManager::forDocument(doc);

    if (action.startsWith(QLatin1String("depot"))) { runDepotAction(mgr, doc, parent, action, itemId); return; }

    QString bookId;
    DrawingSheet* sheet = nullptr;
    if (itemId.startsWith(QLatin1String("book_")))
        bookId = itemId.mid(5);
    else if (itemId.startsWith(QLatin1String("dsheet_"))) {
        sheet = mgr->findSheet(QUuid(itemId.mid(7)));
        if (sheet) bookId = sheet->bookId();
    }
    DrawingBook* book = bookId.isEmpty() ? nullptr : mgr->findBook(bookId);

    auto warn = [&](const QString& m) { QMessageBox::warning(parent, QStringLiteral("Drawing Sheets"), m); };

    // ── 新增圖冊 ──────────────────────────────────────────────────────────
    if (action == QLatin1String("newbook")) {
        if (doc->trackCenterLines().isEmpty()) { warn(QStringLiteral("文件中沒有線形（TrackCenterLine）。")); return; }
        PlanProfileSettings s = PlanProfileSettings::loadDefault();
        s.tclId = doc->trackCenterLines().first()->id();
        s.tclIds = QStringList(s.tclId);
        s.replaceExisting = false;     // 新增：同名時自動加編號
        PlanProfileDialog dlg(doc, s, parent);
        if (dlg.exec() != QDialog::Accepted) return;
        QString err;
        const int n = generatePlanProfileSheets(mgr, doc, dlg.settings(), &err);
        if (n <= 0) { warn(err.isEmpty() ? QStringLiteral("產生失敗") : err); return; }
        say(QStringLiteral("已產生圖冊，共 %1 張平縱面圖。").arg(n));
        return;
    }

    // ── 開啟 ──────────────────────────────────────────────────────────────
    if (action == QLatin1String("open")) {
        if (!sheet && book) {
            const auto v = sheetsOfBook(mgr, bookId);
            if (!v.isEmpty()) sheet = v.first();
        }
        openSheetDialog(mgr, doc, sheet, parent);
        return;
    }

    // ── 圖冊層級 ──────────────────────────────────────────────────────────
    if (action == QLatin1String("bookSettings") || action == QLatin1String("regen") ||
        action == QLatin1String("index") || action == QLatin1String("rename") ||
        action == QLatin1String("deleteBook") || action == QLatin1String("exportPdf")) {
        if (!book) return;

        PlanProfileSettings g;
        g.fromJson(book->settings);

        if (action == QLatin1String("bookSettings")) {
            PlanProfileDialog dlg(doc, g, parent);
            dlg.setLocalMode(false);
            dlg.setApplyMode(true);
            if (dlg.exec() != QDialog::Accepted) return;
            QString err;
            PlanProfileSettings ng = dlg.settings();
            ng.replaceExisting = true;
            if (!updatePlanProfileBook(mgr, doc, bookId, ng, &err))
                warn(err.isEmpty() ? QStringLiteral("重算失敗") : err);
            else say(QStringLiteral("圖冊設定已套用並重算全部圖。"));
        } else if (action == QLatin1String("regen")) {
            QString err;
            if (!updatePlanProfileBook(mgr, doc, bookId, g, &err)) warn(err);
            else say(QStringLiteral("已重新計算圖冊「%1」。").arg(book->name));
        } else if (action == QLatin1String("index")) {
            runPlanProfileIndexEditor(mgr, doc, parent, bookId);
        } else if (action == QLatin1String("rename")) {
            bool ok = false;
            const QString name = QInputDialog::getText(parent, QStringLiteral("重新命名圖冊"),
                                                       QStringLiteral("圖冊名稱："), QLineEdit::Normal,
                                                       book->name, &ok).trimmed();
            if (ok && !name.isEmpty() && name != book->name) {
                g.bookName = name;
                QString err;
                if (!updatePlanProfileBook(mgr, doc, bookId, g, &err)) warn(err);
            }
        } else if (action == QLatin1String("deleteBook")) {
            if (QMessageBox::question(parent, QStringLiteral("刪除圖冊"),
                    QStringLiteral("刪除圖冊「%1」及其全部圖紙（封面、圖目錄、圖）？").arg(book->name),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
                mgr->removeBook(bookId, true);
        } else if (action == QLatin1String("exportPdf")) {
            const QString path = QFileDialog::getSaveFileName(parent, QStringLiteral("匯出圖冊 PDF"),
                                     book->name + QStringLiteral(".pdf"), QStringLiteral("PDF (*.pdf)"));
            if (path.isEmpty()) return;
            if (!DrawingSheet::exportPdfAll(sheetsOfBook(mgr, bookId), path)) warn(QStringLiteral("匯出失敗"));
            else say(QStringLiteral("已匯出 %1").arg(path));
        }
        return;
    }

    // ── 單張圖 ────────────────────────────────────────────────────────────
    if (!sheet) return;

    if (action == QLatin1String("sheetSettings")) {
        if (!sheet->planProfile().enabled) { warn(QStringLiteral("這張圖不是平縱面圖，沒有平縱面圖設定。")); return; }
        PlanProfileDialog dlg(doc, sheet->planProfile().settings, parent);
        dlg.setLocalMode(true);
        if (dlg.exec() != QDialog::Accepted) return;
        applyPlanProfileSheetSettings(mgr, doc, sheet, dlg.settings());
        say(QStringLiteral("此圖的局部設定已套用。"));
    } else if (action == QLatin1String("clearOverride")) {
        clearPlanProfileSheetOverrides(mgr, doc, sheet);
        say(QStringLiteral("已清除局部設定。"));
    } else if (action == QLatin1String("regenSheet")) {
        if (book) {
            PlanProfileSettings g;
            g.fromJson(book->settings);
            QString err;
            if (!updatePlanProfileBook(mgr, doc, bookId, g, &err)) warn(err);
        }
    } else if (action == QLatin1String("deleteSheet")) {
        if (QMessageBox::question(parent, QStringLiteral("刪除圖紙"),
                QStringLiteral("刪除這張圖紙？"), QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) return;
        const QUuid id = sheet->id();
        mgr->removeSheet(id);
        if (book) {
            renumberPlanProfileSheets(mgr, doc, bookId);
            rebuildPlanProfileIndex(mgr, doc, bookId);
            normalizeSheetOrder(mgr);
        }
        mgr->touch();
    }
}

} // namespace command
} // namespace aicad
