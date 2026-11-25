/**
 * @file AssemblyPart.cpp
 */
#include "AssemblyPart.h"
#include "AssemblyGeom.h"
#include "ComponentInstall.h"
#include "Document.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QTimer>
#include <cmath>
#include <memory>

#include <BRepBuilderAPI_Transform.hxx>
#include <Standard_Failure.hxx>
#include <gp_Mat.hxx>
#include <gp_Pnt.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Vec.hxx>

namespace aicad {
namespace cad {

namespace {
constexpr double kDeg = 3.14159265358979323846 / 180.0;
}

AssemblyPart::AssemblyPart(Document* parent) : Feature(parent)
{
    setName(QStringLiteral("Part"));
    setGroupRaw(Group::Assembly);                 // 插入的零件預設放進 Assembly 根
}

void AssemblyPart::setSource(const QString& sourcePath, const TopoDS_Shape& localShape, const QStringList& featureNames)
{
    m_sourcePath   = sourcePath;
    m_local        = localShape;
    m_featureNames = featureNames;
    m_sourceMTime  = QFileInfo(sourcePath).exists() ? QFileInfo(sourcePath).lastModified() : QDateTime();
}

gp_Trsf AssemblyPart::fullTrsf() const
{
    gp_Trsf s;
    s.SetScale(gp_Pnt(0, 0, 0), m_unitScale);
    return m_pose * s;                                  // 先縮放、再剛體位姿
}

gp_Trsf AssemblyPart::makePose(double x, double y, double z, double rxDeg, double ryDeg, double rzDeg)
{
    gp_Quaternion q;
    q.SetEulerAngles(gp_Extrinsic_XYZ, rxDeg * kDeg, ryDeg * kDeg, rzDeg * kDeg);
    gp_Trsf t;
    t.SetRotation(q);
    t.SetTranslationPart(gp_Vec(x, y, z));
    return t;
}

void AssemblyPart::decomposePose(const gp_Trsf& t, double& x, double& y, double& z,
                                 double& rxDeg, double& ryDeg, double& rzDeg)
{
    x = t.Value(1, 4); y = t.Value(2, 4); z = t.Value(3, 4);
    const gp_Quaternion q = t.GetRotation();
    double a = 0, b = 0, c = 0;
    q.GetEulerAngles(gp_Extrinsic_XYZ, a, b, c);
    rxDeg = a / kDeg; ryDeg = b / kDeg; rzDeg = c / kDeg;
}

void AssemblyPart::poseToRT(const gp_Trsf& t, double R[9], double T[3])
{
    const double s = std::abs(t.ScaleFactor()) > 1e-300 ? t.ScaleFactor() : 1.0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) R[i * 3 + j] = t.Value(i + 1, j + 1) / s;
        T[i] = t.Value(i + 1, 4);
    }
}

gp_Trsf AssemblyPart::poseFromRT(const double R[9], const double T[3])
{
    // 經四元數正規化，避免數值漂移造成非正交矩陣
    gp_Mat m(R[0], R[1], R[2], R[3], R[4], R[5], R[6], R[7], R[8]);
    gp_Quaternion q;
    q.SetMatrix(m);
    gp_Trsf t;
    t.SetRotation(q);
    t.SetTranslationPart(gp_Vec(T[0], T[1], T[2]));
    return t;
}

bool AssemblyPart::rebuild()
{
    if (m_local.IsNull()) { setError(QStringLiteral("沒有零件實體")); return false; }
    try {
        BRepBuilderAPI_Transform xf(m_local, fullTrsf(), Standard_True);     // copy → 序號與本地形狀一致
        if (!xf.IsDone()) { setError(QStringLiteral("零件放置失敗")); return false; }
        setShape(xf.Shape());
        clearError();
        return true;
    } catch (const Standard_Failure& ex) {
        setError(QStringLiteral("零件放置失敗：%1").arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

// ── 同步來源檔（與 ComponentInstall 同一套機制）──────────────────────────────────
bool AssemblyPart::sourceIsNewer() const
{
    const QFileInfo fi(m_sourcePath);
    return fi.exists() && m_sourceMTime.isValid() && fi.lastModified() > m_sourceMTime;
}

bool AssemblyPart::syncFromSource(QString* error)
{
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    if (!QFileInfo::exists(m_sourcePath)) return fail(QStringLiteral("來源檔不存在：%1").arg(m_sourcePath));

    auto tmp = std::make_unique<Document>();
    if (!tmp->load(m_sourcePath)) return fail(QStringLiteral("無法讀取來源檔：%1").arg(m_sourcePath));

    TopoDS_Shape comp, vd;
    QStringList cn, vn;
    if (!ComponentInstall::extractFromDocument(tmp.get(), comp, cn, vd, vn, error)) return false;

    m_local = comp;
    m_featureNames = cn;
    m_sourceMTime = QFileInfo(m_sourcePath).lastModified();
    return true;
}

void AssemblyPart::setAutoSync(bool on)
{
    m_autoSync = on;
    attachSourceWatcher();
}

bool AssemblyPart::attachSourceWatcher()
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
                    qDebug() << "[AssemblyPart]" << name() << "來源檔已更新，自動同步";
                    Q_EMIT sourceSynced();
                } else {
                    qWarning() << "[AssemblyPart]" << name() << "自動同步失敗：" << e;
                }
            });
        });
    }
    const QStringList watched = m_watcher->files();
    if (!watched.isEmpty()) m_watcher->removePaths(watched);
    if (m_autoSync && !m_sourcePath.isEmpty() && QFileInfo::exists(m_sourcePath))
        m_watcher->addPath(m_sourcePath);

    if (m_autoSync && sourceIsNewer()) {                 // 讀檔當下來源檔就比內嵌副本新
        QString e;
        if (syncFromSource(&e)) return true;
        qWarning() << "[AssemblyPart]" << name() << "載入時同步失敗：" << e;
    }
    return false;
}

// ── 序列化 ──────────────────────────────────────────────────────────────────
QJsonObject AssemblyPart::toJson() const
{
    QJsonObject obj = Feature::toJson();
    obj["type"]         = typeString();
    obj["sourcePath"]   = m_sourcePath;
    obj["featureNames"] = QJsonArray::fromStringList(m_featureNames);
    obj["sourceMTime"]  = m_sourceMTime.isValid() ? m_sourceMTime.toString(Qt::ISODateWithMs) : QString();
    obj["autoSync"]     = m_autoSync;
    obj["unitScale"]    = m_unitScale;
    obj["fixed"]        = m_fixed;
    double R[9], T[3];
    poseToRT(m_pose, R, T);
    QJsonArray ra, ta;
    for (int i = 0; i < 9; ++i) ra.append(R[i]);
    for (int i = 0; i < 3; ++i) ta.append(T[i]);
    obj["poseR"] = ra;
    obj["poseT"] = ta;
    obj["brep"]  = assemblygeom::shapeToBase64(m_local);      // 內嵌：文件不依賴來源檔
    return obj;
}

bool AssemblyPart::fromJson(const QJsonObject& json)
{
    Feature::fromJson(json);
    m_sourcePath = json["sourcePath"].toString();
    m_featureNames.clear();
    for (const QJsonValue& v : json["featureNames"].toArray()) m_featureNames << v.toString();
    m_sourceMTime = QDateTime::fromString(json["sourceMTime"].toString(), Qt::ISODateWithMs);
    m_autoSync    = json["autoSync"].toBool(true);
    m_unitScale   = json["unitScale"].toDouble(1.0);
    if (m_unitScale <= 0.0) m_unitScale = 1.0;
    m_fixed       = json["fixed"].toBool(false);

    const QJsonArray ra = json["poseR"].toArray(), ta = json["poseT"].toArray();
    double R[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, T[3] = {0, 0, 0};
    if (ra.size() == 9) for (int i = 0; i < 9; ++i) R[i] = ra[i].toDouble();
    if (ta.size() == 3) for (int i = 0; i < 3; ++i) T[i] = ta[i].toDouble();
    m_pose = poseFromRT(R, T);

    m_local = assemblygeom::shapeFromBase64(json["brep"].toString());
    return true;
}

} // namespace cad
} // namespace aicad
