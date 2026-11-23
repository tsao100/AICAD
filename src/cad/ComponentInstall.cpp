/**
 * @file ComponentInstall.cpp
 */
#include "ComponentInstall.h"
#include "AlignedProfileArray.h"
#include "CantLineUtil.h"
#include "SketchInstance.h"
#include "Sketch.h"
#include "Document.h"
#include "DefinedPlane.h"
#include "TrackSlabPlan.h"
#include "sketch/SketchConstraint.h"

#include <QByteArray>
#include <QDebug>
#include <QFileInfo>
#include <QMetaType>
#include <QJsonArray>
#include <QRegularExpression>
#include <QJsonObject>
#include <QTimer>
#include <QVector2D>
#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

namespace aicad {
namespace cad {

namespace {

// ── BRep ⇄ base64（存檔用；組件資料內嵌在文件中，不依賴來源 *.aicad 還在）────────────
QString shapeToBase64(const TopoDS_Shape& s)
{
    if (s.IsNull()) return QString();
    std::ostringstream os;
    BRepTools::Write(s, os);
    const std::string str = os.str();
    return QString::fromLatin1(QByteArray(str.data(), int(str.size())).toBase64());
}

TopoDS_Shape shapeFromBase64(const QString& b64)
{
    TopoDS_Shape s;
    if (b64.isEmpty()) return s;
    const QByteArray raw = QByteArray::fromBase64(b64.toLatin1());
    std::istringstream is(std::string(raw.constData(), size_t(raw.size())));
    BRep_Builder bb;
    BRepTools::Read(s, is, bb);
    return s;
}

Bnd_Box boxOf(const TopoDS_Shape& s)
{
    Bnd_Box b;
    if (!s.IsNull()) BRepBndLib::Add(s, b);
    return b;
}

} // namespace

ComponentInstall::ComponentInstall(Document* parent)
    : Feature(parent)
{
    setName("Component");
}

void ComponentInstall::setSource(const QString& sourcePath,
                                 const TopoDS_Shape& component, const QStringList& componentNames,
                                 const TopoDS_Shape& space,     const QStringList& spaceNames,
                                 const QStringList& componentIds, const QStringList& spaceIds)
{
    m_sourcePath     = sourcePath;
    m_component      = component;
    m_componentNames = componentNames;
    m_componentIds   = componentIds;
    m_space          = space;
    m_spaceNames     = spaceNames;
    m_spaceIds       = spaceIds;
    m_sourceMTime    = QFileInfo(sourcePath).lastModified();
}

Sketch* ComponentInstall::frameSketch() const { return m_masterSketch.data(); }

void ComponentInstall::resolveReferences(Document* doc)
{
    if (!doc || m_masterSketchId.isEmpty()) return;
    m_masterSketch = qobject_cast<Sketch*>(doc->findFeature(m_masterSketchId));
    if (!m_masterSketch)
        qWarning() << "[ComponentInstall]" << name() << "找不到座標線所在的主草圖:" << m_masterSketchId;
}

double ComponentInstall::spaceHalfLength() const
{
    const Bnd_Box b = boxOf(m_space);
    if (b.IsVoid()) return 0.0;
    Standard_Real x0, y0, z0, x1, y1, z1;
    b.Get(x0, y0, z0, x1, y1, z1);
    return std::max(std::abs(y0), std::abs(y1)) * m_unitScale;   // 沿線方向 = 組件 +Y；mm → m
}

// ─────────────────────────────────────────────────────────────────────────────
// 定位
// ─────────────────────────────────────────────────────────────────────────────
bool ComponentInstall::trsfInArray(const AlignedProfileArray* arr, const Placement& pl,
                                   gp_Trsf& out, QString* err) const
{
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    if (!arr) return fail(QStringLiteral("沒有斷面陣列"));
    if (!pl.tclId.isEmpty() && arr->trackCenterLineId() != pl.tclId)
        return fail(QStringLiteral("陣列不在指定的線路上"));

    // 以「該安裝里程精確的 cant／H」解算斷面（探針測站），而不是在相鄰測站之間內插：
    // 組件座標線與軌道面直線都是草圖幾何，用安裝里程當下的超高求解才會落在正確的軌道面上。
    QString perr;
    std::unique_ptr<SketchInstance> probe(arr->createProbeAt(pl.chainage, &perr));
    if (!probe) return fail(perr);
    Sketch* master = probe->masterSketch();
    if (!master) return fail(QStringLiteral("探針測站沒有斷面草圖"));

    QVector2D O, D;          // 斷面平面內：組件原點與 +X 方向（單位：m，已乘 scale）
    if (hasFrame()) {
        // 已建立組件座標線 → 位置與轉角由草圖約束決定（隨 cant 變化）
        if (frameSketch() && master != frameSketch())
            return fail(QStringLiteral("此陣列的斷面草圖不是組件座標線所在的草圖"));
        QVector2D a, b;
        if (!probe->solvedLineEnds(m_frameXLine, a, b))
            return fail(QStringLiteral("找不到組件座標線（草圖中的線可能已被刪除）"));
        O = a;
        D = b - a;
    } else {
        // 沒有座標線 → 預設貼在軌道面（cant）直線的起點，X 軸 ∥ cant 直線
        const QSet<QString> names = cantDependentNames(master->parameterStore());
        QString e;
        const CantLineUV c = findCantLineUV(probe.get(), names, &e);
        if (!c.ok) return fail(e);
        O = c.a;
        D = c.b - c.a;
    }
    if (D.length() < 1e-9f) return fail(QStringLiteral("組件 X 軸方向長度為 0"));
    D.normalize();

    // 該里程精確的放置座標系（含曲線與縱坡）
    const AlignedProfileArray::StationFrame f = arr->frameAt(pl.chainage);
    if (!f.ok) return fail(QStringLiteral("里程 %1 超出線形範圍").arg(pl.chainage, 0, 'f', 3));

    const gp_Vec X(f.xx, f.xy, f.xz), Y(f.yx, f.yy, f.yz), Z(f.zx, f.zy, f.zz);
    gp_Vec dirW = X * double(D.x()) + Y * double(D.y());       // 軌道面橫向（世界）
    if (dirW.Magnitude() < 1e-12) return fail(QStringLiteral("組件橫向退化"));
    dirW.Normalize();

    gp_Vec origin(f.ox, f.oy, f.oz);
    origin += X * double(O.x()) + Y * double(O.y());           // 軌道面上的組件原點（m）
    origin += dirW * pl.lateral;                               // 沿組件 +X 的額外偏移（m）

    // 組件座標系：+X = 軌道面橫向（貼合 cant 直線、+ 朝右）；
    //            +Y = 草圖 -Z（= 沿線、里程增加方向 T）；
    //            +Z = X × Y（垂直軌道面朝上）。
    gp_Vec Xc = dirW;
    gp_Vec Yc = Z.Normalized().Reversed();             // 草圖 Z = -T → 取 -Z = +T
    if (pl.flip180) { Xc.Reverse(); Yc.Reverse(); }    // 繞軌道面法線轉 180°（Z 不變）
    gp_Vec Zc = Xc.Crossed(Yc);
    Zc.Normalize();

    try {
        gp_Trsf rigid;
        rigid.SetValues(Xc.X(), Yc.X(), Zc.X(), origin.X(),
                        Xc.Y(), Yc.Y(), Zc.Y(), origin.Y(),
                        Xc.Z(), Yc.Z(), Zc.Z(), origin.Z());
        // 組件（*.aicad）與草圖預設單位是 mm，世界座標是 m：先縮小（預設 1/1000）再放到安裝位置
        gp_Trsf scale;
        scale.SetScale(gp_Pnt(0.0, 0.0, 0.0), m_unitScale);
        rigid.Multiply(scale);                         // rigid ∘ scale：先縮放、再旋轉平移
        out = rigid;
    } catch (const Standard_Failure& ex) {
        return fail(QStringLiteral("組件座標系不是正交座標系：%1").arg(ex.GetMessageString()));
    }
    return true;
}

bool ComponentInstall::sketchPreview(TopoDS_Shape& compOut, TopoDS_Shape& voidOut, QString* error) const
{
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    Sketch* m = frameSketch();
    if (!m || !hasFrame()) return fail(QStringLiteral("尚未建立組件座標線"));

    auto* xl = dynamic_cast<SketchLine*>(m->findGeometry(m_frameXLine));
    if (!xl) return fail(QStringLiteral("草圖中找不到組件 X 軸線"));
    SketchPoint* ps = m->point(xl->startUuid);
    SketchPoint* pe = m->point(xl->endUuid);
    Plane* pln = m->plane();
    if (!ps || !pe || !pln) return fail(QStringLiteral("草圖平面或座標線端點不存在"));

    QVector2D D = pe->pos - ps->pos;
    if (D.length() < 1e-9f) return fail(QStringLiteral("組件 X 軸線長度為 0"));
    D.normalize();

    const QVector3D ex = pln->xAxis(), ey = pln->yAxis(), n = pln->normal(), org = pln->origin();
    auto vec = [](const QVector3D& v) { return gp_Vec(v.x(), v.y(), v.z()); };

    gp_Vec Xc = vec(ex) * double(D.x()) + vec(ey) * double(D.y());   // 組件 +X（草圖平面內，沿座標線）
    Xc.Normalize();
    gp_Vec Yc = vec(n).Reversed();                                   // 組件 +Y = 草圖 -Z
    Yc.Normalize();
    gp_Vec Zc = Xc.Crossed(Yc);
    Zc.Normalize();
    const gp_Vec origin = vec(org) + vec(ex) * double(ps->pos.x()) + vec(ey) * double(ps->pos.y());

    try {
        gp_Trsf t;      // 草圖與組件同為草圖單位（mm）：不縮放
        t.SetValues(Xc.X(), Yc.X(), Zc.X(), origin.X(),
                    Xc.Y(), Yc.Y(), Zc.Y(), origin.Y(),
                    Xc.Z(), Yc.Z(), Zc.Z(), origin.Z());
        compOut = TopoDS_Shape();
        voidOut = TopoDS_Shape();
        if (!m_component.IsNull()) {
            BRepBuilderAPI_Transform xf(m_component, t, Standard_True);
            if (xf.IsDone()) compOut = xf.Shape();
        }
        if (!m_space.IsNull()) {
            BRepBuilderAPI_Transform xf(m_space, t, Standard_True);
            if (xf.IsDone()) voidOut = xf.Shape();
        }
    } catch (const Standard_Failure& ex2) {
        return fail(QStringLiteral("預覽變換失敗：%1").arg(ex2.GetMessageString()));
    }
    return !compOut.IsNull();
}

// ─────────────────────────────────────────────────────────────────────────────
// 組件座標線（建立在主草圖）
// ─────────────────────────────────────────────────────────────────────────────
bool ComponentInstall::createFrameInSketch(Sketch* master, QString* error, QString* note)
{
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    if (!master) return fail(QStringLiteral("沒有指定斷面草圖"));

    // 重新建立：先移除舊的座標線（連同參照它們的約束）
    if (hasFrame() && frameSketch()) {
        if (!m_frameZLine.isEmpty()) frameSketch()->removeGeometry(m_frameZLine);
        if (!m_frameXLine.isEmpty()) frameSketch()->removeGeometry(m_frameXLine);
        m_frameXLine.clear(); m_frameZLine.clear();
    }

    // 預設把組件座標線貼在 cant 直線上：原點 = cant 直線起點、X 軸 ∥ cant 直線
    QString e;
    SketchLine* cant = locateCantLine(master, cantDependentNames(master->parameterStore()), &e);

    QVector2D P0(0.0f, 0.0f);
    QVector2D dir(1.0f, 0.0f);
    if (cant) {
        SketchPoint* ps = master->point(cant->startUuid);
        SketchPoint* pe = master->point(cant->endUuid);
        if (!ps || !pe) return fail(QStringLiteral("cant 直線端點不存在"));
        P0  = ps->pos;
        dir = pe->pos - ps->pos;
        if (dir.length() < 1e-9f) return fail(QStringLiteral("cant 直線長度為 0"));
        dir.normalize();
    } else if (note) {
        // 這張草圖沒有受 cant 約束的直線：座標線建在草圖原點、沿 +X，不加與 cant 直線的約束，
        // 請使用者自行用草圖約束（與草圖的軸線／原點／建構線）決定位置與轉角。
        *note = QStringLiteral("草圖「%1」沒有受 cant 約束的直線：座標線建在草圖原點，請自行加約束").arg(master->name());
    }
    const QVector2D perp(-dir.y(), dir.x());          // +90°（組件 +Z：垂直軌道面朝上；組件 +Y 是垂直草圖平面的沿線方向，無法畫在 2D 草圖中）
    constexpr float kLen = 100.0f;                    // 座標線顯示長度（草圖單位）；不影響定位

    // 座標線：Centerline 角色（不進入輪廓／Loft；也不會被誤認為 cant 直線）
    const QString xUuid = master->addLineGeom(P0, P0 + dir * kLen, QString(), QString(),
                                              GeomRole::Centerline, /*emitSignals=*/false);
    auto* xl = dynamic_cast<SketchLine*>(master->findGeometry(xUuid));
    if (!xl) return fail(QStringLiteral("建立組件 X 軸線失敗"));
    const QString zUuid = master->addLineGeom(P0, P0 + perp * kLen, xl->startUuid, QString(),
                                              GeomRole::Centerline, /*emitSignals=*/false);   // 共用原點
    if (zUuid.isEmpty()) return fail(QStringLiteral("建立組件 Z 軸線失敗"));

    // 預設約束（使用者之後可在草圖中刪除／改成別的）
    if (cant) {
        master->addConstraint(SketchConstraint::makeCoincident(GeomRef(xUuid, GeomHandle::Start),
                                                               GeomRef(cant->uuid, GeomHandle::Start)), false);
        master->addConstraint(SketchConstraint::makeParallel(xUuid, cant->uuid), false);
    }
    master->addConstraint(SketchConstraint::makePerpendicular(xUuid, zUuid), false);
    master->solveConstraints();

    m_masterSketch   = master;
    m_masterSketchId = master->id();
    m_frameXLine     = xUuid;
    m_frameZLine     = zUuid;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 與來源 *.aicad 同步
// ─────────────────────────────────────────────────────────────────────────────
bool ComponentInstall::sourceIsNewer() const
{
    const QFileInfo fi(m_sourcePath);
    return fi.exists() && m_sourceMTime.isValid() && fi.lastModified() > m_sourceMTime;
}

bool ComponentInstall::isVoidName(const QString& featureName)
{
    // void / void1 / void_2 / void 3 / VOID-4（不分大小寫）
    static const QRegularExpression re(QStringLiteral("^void([\\s_\\-]?\\d+)?$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(featureName.trimmed()).hasMatch();
}

bool ComponentInstall::extractFromDocument(const Document* src,
                                           TopoDS_Shape& component, QStringList& componentNames,
                                           TopoDS_Shape& voidShape, QStringList& voidNames,
                                           QString* error)
{
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    if (!src) return fail(QStringLiteral("沒有文件"));

    // 被其他特徵依賴（消耗）的中間實體不單獨計入（例如被倒角吃掉的擠出），避免重複
    QSet<QString> consumed;
    for (Feature* f : src->features())
        if (f) for (const QString& id : f->featureDependencies()) consumed.insert(id);

    BRep_Builder bb;
    TopoDS_Compound comp, voids;
    bb.MakeCompound(comp);
    bb.MakeCompound(voids);
    componentNames.clear();
    voidNames.clear();

    for (Feature* f : src->features()) {
        if (!f || f->shape().IsNull()) continue;
        if (qobject_cast<SketchInstance*>(f) || qobject_cast<TrackSlabPlan*>(f)
            || qobject_cast<ComponentInstall*>(f) || qobject_cast<DefinedPlane*>(f)) continue;
        if (consumed.contains(f->id())) continue;

        if (isVoidName(f->name())) { bb.Add(voids, f->shape()); voidNames << f->name(); }
        else                       { bb.Add(comp,  f->shape()); componentNames << f->name(); }
    }
    if (componentNames.isEmpty())
        return fail(QStringLiteral("此檔案中沒有 3D 組件實體（除了名稱為 void 的特徵之外，需要至少一個擠出／放樣等 3D 特徵）"));

    component = comp;
    voidShape = voidNames.isEmpty() ? TopoDS_Shape() : TopoDS_Shape(voids);
    return true;
}

bool ComponentInstall::syncFromSource(QString* error)
{
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    if (!QFileInfo::exists(m_sourcePath)) return fail(QStringLiteral("來源檔不存在：%1").arg(m_sourcePath));

    auto tmp = std::make_unique<Document>();
    if (!tmp->load(m_sourcePath)) return fail(QStringLiteral("無法讀取來源檔：%1").arg(m_sourcePath));

    TopoDS_Shape comp, vd;
    QStringList cn, vn;
    if (!extractFromDocument(tmp.get(), comp, cn, vd, vn, error)) return false;

    m_component = comp;  m_componentNames = cn;
    m_space     = vd;    m_spaceNames     = vn;
    m_sourceMTime = QFileInfo(m_sourcePath).lastModified();
    return true;
}

void ComponentInstall::setAutoSync(bool on)
{
    m_autoSync = on;
    attachSourceWatcher();
}

bool ComponentInstall::attachSourceWatcher()
{
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
            // 許多程式存檔是「刪除再建立」→ 監看會失效：稍後重新加入；並合併短時間內的多次通知
            QTimer::singleShot(800, this, [this, path]() {
                if (m_watcher && QFileInfo::exists(path) && !m_watcher->files().contains(path))
                    m_watcher->addPath(path);
                if (!m_autoSync || !sourceIsNewer()) return;
                QString e;
                if (syncFromSource(&e)) {
                    qDebug() << "[ComponentInstall]" << name() << "來源檔已更新，自動同步";
                    if (Document* doc = document()) doc->applyComponentChange(this);
                } else {
                    qWarning() << "[ComponentInstall]" << name() << "自動同步失敗：" << e;
                }
            });
        });
    }

    const QStringList watched = m_watcher->files();
    if (!watched.isEmpty()) m_watcher->removePaths(watched);
    if (m_autoSync && !m_sourcePath.isEmpty() && QFileInfo::exists(m_sourcePath))
        m_watcher->addPath(m_sourcePath);

    // 讀檔當下來源檔就比內嵌副本新 → 同步一次
    if (m_autoSync && sourceIsNewer()) {
        QString e;
        if (syncFromSource(&e)) return true;
        qWarning() << "[ComponentInstall]" << name() << "載入時同步失敗：" << e;
    }
    return false;
}

bool ComponentInstall::computeTrsf(const Placement& pl, gp_Trsf& out, QString* err) const
{
    const Document* doc = document();
    if (!doc) { if (err) *err = QStringLiteral("沒有文件"); return false; }

    QString lastErr = QStringLiteral("文件中沒有斷面陣列");
    for (Feature* f : doc->features()) {
        auto* arr = qobject_cast<AlignedProfileArray*>(f);
        if (!arr) continue;
        QString e;
        if (trsfInArray(arr, pl, out, &e)) return true;
        lastErr = e;
    }
    if (err) *err = lastErr;
    return false;
}

QVector<TopoDS_Shape> ComponentInstall::placedSpaces(const AlignedProfileArray* arr,
                                                     double pMin, double pMax) const
{
    QVector<TopoDS_Shape> out;
    if (m_space.IsNull() || !arr) return out;

    const double margin = spaceHalfLength();
    for (const Placement& pl : m_placements) {
        if (!pl.tclId.isEmpty() && arr->trackCenterLineId() != pl.tclId) continue;
        if (pl.chainage < pMin - margin || pl.chainage > pMax + margin) continue;   // 與此範圍無關

        gp_Trsf t;
        QString e;
        if (!trsfInArray(arr, pl, t, &e)) {
            // 該里程不在這個陣列的測站內（可能屬於相鄰分段／別的陣列）：略過，不算錯誤
            continue;
        }
        try {
            // void 是多個 3D 特徵的合成：各實體分開當作布林的工具（同一個 compound 內互相重疊會讓布林失敗）
            QVector<TopoDS_Shape> parts;
            for (TopoDS_Iterator it(m_space); it.More(); it.Next()) parts.append(it.Value());
            if (parts.isEmpty()) parts.append(m_space);
            for (const TopoDS_Shape& part : std::as_const(parts)) {
                BRepBuilderAPI_Transform xf(part, t, Standard_True);
                if (xf.IsDone()) out.append(xf.Shape());
            }
        } catch (const Standard_Failure&) {}
    }
    return out;
}

TopoDS_Shape ComponentInstall::cutSpaces(Document* doc, const TopoDS_Shape& solid,
                                         const AlignedProfileArray* arr, double pMin, double pMax,
                                         QString* warn)
{
    if (!doc || solid.IsNull() || !arr) return solid;

    // 與 solid 包圍盒有交集的所需空間才參與（省下無謂的布林運算）
    const Bnd_Box solidBox = boxOf(solid);
    TopTools_ListOfShape tools;
    for (Feature* f : doc->features()) {
        auto* ci = qobject_cast<ComponentInstall*>(f);
        if (!ci) continue;
        for (const TopoDS_Shape& sp : ci->placedSpaces(arr, pMin, pMax)) {
            const Bnd_Box b = boxOf(sp);
            if (b.IsVoid() || solidBox.IsOut(b)) continue;
            tools.Append(sp);
        }
    }
    if (tools.IsEmpty()) return solid;

    try {
        TopTools_ListOfShape args;
        args.Append(solid);

        BRepAlgoAPI_Cut cut;
        cut.SetArguments(args);
        cut.SetTools(tools);
        cut.SetRunParallel(Standard_True);
        cut.Build();
        if (!cut.IsDone() || cut.HasErrors() || cut.Shape().IsNull()) {
            if (warn) *warn = QStringLiteral("所需空間的布林扣除失敗，道版維持未開孔");
            return solid;
        }
        return cut.Shape();
    } catch (const Standard_Failure& ex) {
        if (warn) *warn = QStringLiteral("所需空間的布林扣除發生 OCC 例外：%1").arg(ex.GetMessageString());
        return solid;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 與斷面陣列連動、重建（放置後的組件，供顯示）
// ─────────────────────────────────────────────────────────────────────────────
void ComponentInstall::requestRebuild()
{
    if (m_rebuildPending) return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_rebuildPending = false;
        if (Document* doc = document()) doc->rebuildFeature(this);
        else rebuild();
    });
}

void ComponentInstall::refreshSources()
{
    for (const QMetaObject::Connection& c : std::as_const(m_conns)) disconnect(c);
    m_conns.clear();

    QStringList ids;
    if (const Document* doc = document()) {
        for (Feature* f : doc->features()) {
            auto* arr = qobject_cast<AlignedProfileArray*>(f);
            if (!arr) continue;
            ids << arr->id();
            // 陣列（測站／cant）變了 → 重算放置後的組件
            m_conns << connect(arr, &Feature::shapeChanged, this, [this]() { requestRebuild(); });
        }
    }
    ids.sort();
    const QString sig = ids.join(QLatin1Char('|'));
    if (sig != m_arraySig) {
        m_arraySig = sig;
        requestRebuild();
    }
}

bool ComponentInstall::rebuild()
{
    // 組件定義與安裝位置是分開管理的（「組件安裝位置」功能）：尚未指定位置不算錯誤，只是不顯示
    if (m_component.IsNull())   { setError("沒有組件實體"); return false; }
    if (m_placements.isEmpty()) { setShape(TopoDS_Shape()); clearError(); return true; }

    try {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        int ok = 0;
        QString firstError;

        for (const Placement& pl : m_placements) {
            gp_Trsf t;
            QString e;
            if (!computeTrsf(pl, t, &e)) {
                if (firstError.isEmpty()) firstError = QString("里程 %1：%2").arg(pl.chainage, 0, 'f', 3).arg(e);
                continue;
            }
            BRepBuilderAPI_Transform xf(m_component, t, Standard_True);
            if (!xf.IsDone()) {
                if (firstError.isEmpty()) firstError = QString("里程 %1：組件變換失敗").arg(pl.chainage, 0, 'f', 3);
                continue;
            }
            builder.Add(compound, xf.Shape());
            ++ok;
        }

        if (ok == 0) { setError(firstError.isEmpty() ? QStringLiteral("沒有可定位的安裝里程") : firstError); return false; }

        setShape(compound);
        if (firstError.isEmpty()) clearError();
        else setError(QString("部分位置失敗：%1").arg(firstError));
        return true;

    } catch (const Standard_Failure& ex) {
        setError(QString("OCC error during component install: %1")
                     .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 序列化
// ─────────────────────────────────────────────────────────────────────────────
QJsonObject ComponentInstall::toJson() const
{
    QJsonObject obj = Feature::toJson();
    obj["type"]            = typeString();
    obj["sourcePath"]      = m_sourcePath;
    obj["componentNames"]  = QJsonArray::fromStringList(m_componentNames);
    obj["spaceNames"]      = QJsonArray::fromStringList(m_spaceNames);
    obj["componentIds"]    = QJsonArray::fromStringList(m_componentIds);
    obj["spaceIds"]        = QJsonArray::fromStringList(m_spaceIds);
    obj["sourceMTime"]     = m_sourceMTime.isValid() ? m_sourceMTime.toString(Qt::ISODateWithMs) : QString();
    obj["autoSync"]        = m_autoSync;
    obj["unitScale"]       = m_unitScale;
    obj["masterSketchId"]  = m_masterSketchId;
    obj["frameXLine"]      = m_frameXLine;
    obj["frameZLine"]      = m_frameZLine;
    obj["componentBRep"]   = shapeToBase64(m_component);   // 內嵌：文件不依賴來源檔
    obj["spaceBRep"]       = shapeToBase64(m_space);

    QJsonArray arr;
    for (const Placement& p : m_placements) {
        QJsonObject o;
        o["tclId"]    = p.tclId;
        o["chainage"] = p.chainage;
        o["lateral"]  = p.lateral;
        o["flip180"]  = p.flip180;
        arr.append(o);
    }
    obj["placements"] = arr;
    return obj;
}

bool ComponentInstall::fromJson(const QJsonObject& json)
{
    Feature::fromJson(json);
    m_sourcePath = json["sourcePath"].toString();

    m_componentNames.clear();
    for (const QJsonValue& v : json["componentNames"].toArray()) m_componentNames << v.toString();
    m_spaceNames.clear();
    for (const QJsonValue& v : json["spaceNames"].toArray()) m_spaceNames << v.toString();

    m_componentIds.clear();
    for (const QJsonValue& v : json["componentIds"].toArray()) m_componentIds << v.toString();
    m_spaceIds.clear();
    for (const QJsonValue& v : json["spaceIds"].toArray()) m_spaceIds << v.toString();
    m_sourceMTime    = QDateTime::fromString(json["sourceMTime"].toString(), Qt::ISODateWithMs);
    m_autoSync       = json["autoSync"].toBool(true);
    m_unitScale      = json["unitScale"].toDouble(0.001);     // 舊檔沒有此欄位：預設 mm → m
    m_masterSketchId = json["masterSketchId"].toString();
    m_frameXLine     = json["frameXLine"].toString();
    m_frameZLine     = json.contains("frameZLine") ? json["frameZLine"].toString()
                                                   : json["frameYLine"].toString();   // 舊檔欄位名

    m_component = shapeFromBase64(json["componentBRep"].toString());
    m_space     = shapeFromBase64(json["spaceBRep"].toString());

    m_placements.clear();
    for (const QJsonValue& v : json["placements"].toArray()) {
        const QJsonObject o = v.toObject();
        Placement p;
        p.tclId    = o["tclId"].toString();
        p.chainage = o["chainage"].toDouble();
        p.lateral  = o["lateral"].toDouble();
        p.flip180  = o["flip180"].toBool();
        m_placements.append(p);
    }
    return true;
}

} // namespace cad
} // namespace aicad
