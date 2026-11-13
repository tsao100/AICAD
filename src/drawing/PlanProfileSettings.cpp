/**
 * @file PlanProfileSettings.cpp
 */
#include "PlanProfileSettings.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>
#include <QFile>
#include <QSettings>
#include <QDebug>
#include <cmath>

namespace aicad {
namespace drawing {

QJsonObject DepotView::toJson() const
{
    QJsonObject o;
    o["name"] = name; o["e"] = e; o["n"] = n; o["w"] = w; o["h"] = h;
    o["rotDeg"] = rotDeg; o["alignTcl"] = alignTcl; o["alignCh"] = alignCh;
    o["order"] = order; o["scale"] = scale;
    return o;
}

DepotView DepotView::fromJson(const QJsonObject& o)
{
    DepotView v;
    v.name = o.value("name").toString();
    v.e = o.value("e").toDouble(); v.n = o.value("n").toDouble();
    v.w = o.value("w").toDouble(150.0); v.h = o.value("h").toDouble(120.0);
    v.rotDeg = o.value("rotDeg").toDouble();
    v.alignTcl = o.value("alignTcl").toString();
    v.alignCh = o.value("alignCh").toDouble();
    v.order = o.value("order").toInt(1);
    v.scale = o.value("scale").toDouble();
    return v;
}


// ─────────────────────────────────────────────────────────────────────────────
// PlanProfileSettings JSON
// ─────────────────────────────────────────────────────────────────────────────

QJsonObject PlanProfileSettings::toJson() const
{
    QJsonObject j;
    j["tclId"] = tclId;
    j["useFullRange"] = useFullRange;
    j["startCh"] = startCh;
    j["endCh"] = endCh;
    QJsonArray ids;
    for (const QString& id : tclIds) ids.append(id);
    j["tclIds"] = ids;
    j["noChainIds"] = QJsonArray::fromStringList(noChainIds);
    j["followChain"] = followChain;
    j["joinTol"] = joinTol;
    j["joinTolZ"] = joinTolZ;
    j["bookName"] = bookName;
    j["generateCover"] = generateCover;
    j["showNeighbors"] = showNeighbors;
    j["generateIndex"] = generateIndex;
    j["generateDataTables"] = generateDataTables;
    j["generateSchematic"] = generateSchematic;
    j["generateDepot"] = generateDepot;
    {
        QJsonArray da;
        for (const DepotView& v : depotViews) da.append(v.toJson());
        j["depotViews"] = da;
    }
    j["tableDecimals"] = tableDecimals;
    j["tableAzDecimals"] = tableAzDecimals;
    j["radiusSigned"] = radiusSigned;
    j["remarkDecimals"] = remarkDecimals;
    j["hzCols"] = QJsonArray::fromStringList(hzCols);
    j["vtCols"] = QJsonArray::fromStringList(vtCols);

    j["hScale"] = hScale;
    j["vScale"] = vScale;
    j["autoVScale"] = autoVScale;
    j["segmentLength"] = segmentLength;
    j["overlap"] = overlap;

    j["paperSize"] = paperSize;
    j["orientation"] = orientation;
    j["planBandH"] = planBandH;
    j["profileBandH"] = profileBandH;
    j["tableRowH"] = tableRowH;
    j["leftLabelW"] = leftLabelW;
    j["textSize"] = textSize;

    j["minorTick"] = minorTick;
    j["majorTick"] = majorTick;
    j["elevGridStep"] = elevGridStep;
    j["tableInterval"] = tableInterval;
    j["chainagePrefix"] = chainagePrefix;
    j["kmFormat"] = kmFormat;
    j["useContChainage"] = useContChainage;
    j["elevDecimals"] = elevDecimals;

    j["coordGrid"] = coordGrid;
    j["showNorthArrow"] = showNorthArrow;
    j["showKeyPoints"] = showKeyPoints;
    j["showCurveData"] = showCurveData;

    j["showGridLines"] = showGridLines;
    j["showPVI"] = showPVI;
    j["showGradeLabels"] = showGradeLabels;

    j["rowElevation"] = rowElevation;
    j["rowGrade"] = rowGrade;
    j["rowCurve"] = rowCurve;
    j["rowAzimuth"] = rowAzimuth;

    j["titleTemplate"] = titleTemplate;
    j["numberTemplate"] = numberTemplate;
    j["replaceExisting"] = replaceExisting;
    return j;
}

bool PlanProfileSettings::fromJson(const QJsonObject& j)
{
    // 缺欄位一律保留目前（預設）值，讓舊檔／手寫設定檔都能載入
    auto d = [&](const char* k, double def) { return j.contains(k) ? j[k].toDouble(def) : def; };
    auto b = [&](const char* k, bool def)   { return j.contains(k) ? j[k].toBool(def)   : def; };
    auto i = [&](const char* k, int def)    { return j.contains(k) ? j[k].toInt(def)    : def; };
    auto s = [&](const char* k, const QString& def) { return j.contains(k) ? j[k].toString(def) : def; };

    tclId = s("tclId", tclId);
    useFullRange = b("useFullRange", useFullRange);
    startCh = d("startCh", startCh);
    endCh = d("endCh", endCh);
    if (j.contains("tclIds")) {
        tclIds.clear();
        for (const QJsonValue& v : j.value("tclIds").toArray()) tclIds << v.toString();
    } else if (j.contains("tclId2") || (j.contains("tclId") && tclIds.isEmpty())) {   // 舊檔相容
        tclIds.clear();
        if (!tclId.isEmpty()) tclIds << tclId;
        const QString t2 = j.value("tclId2").toString();
        if (!t2.isEmpty() && t2 != tclId) tclIds << t2;
    }
    noChainIds.clear();
    for (const QJsonValue& v : j.value("noChainIds").toArray()) noChainIds << v.toString();
    followChain = b("followChain", followChain);
    joinTol = d("joinTol", joinTol);
    joinTolZ = d("joinTolZ", joinTolZ);
    bookName = s("bookName", bookName);
    generateCover = b("generateCover", generateCover);
    showNeighbors = b("showNeighbors", showNeighbors);
    generateIndex = b("generateIndex", generateIndex);
    generateDataTables = b("generateDataTables", generateDataTables);
    generateSchematic = b("generateSchematic", generateSchematic);
    generateDepot = b("generateDepot", generateDepot);
    if (j.contains("depotViews")) {
        depotViews.clear();
        for (const QJsonValue& v : j.value("depotViews").toArray()) depotViews.append(DepotView::fromJson(v.toObject()));
    }
    tableDecimals = std::clamp(j.value("tableDecimals").toInt(tableDecimals), 0, 8);
    tableAzDecimals = std::clamp(j.value("tableAzDecimals").toInt(tableAzDecimals), 0, 6);
    radiusSigned = b("radiusSigned", radiusSigned);
    remarkDecimals = std::clamp(j.value("remarkDecimals").toInt(remarkDecimals), 0, 4);
    if (j.contains("hzCols")) { hzCols.clear(); for (const QJsonValue& v : j.value("hzCols").toArray()) hzCols << v.toString(); }
    if (j.contains("vtCols")) { vtCols.clear(); for (const QJsonValue& v : j.value("vtCols").toArray()) vtCols << v.toString(); }

    hScale = d("hScale", hScale);
    vScale = d("vScale", vScale);
    autoVScale = b("autoVScale", autoVScale);
    segmentLength = d("segmentLength", segmentLength);
    overlap = d("overlap", overlap);

    paperSize = i("paperSize", paperSize);
    orientation = i("orientation", orientation);
    planBandH = d("planBandH", planBandH);
    profileBandH = d("profileBandH", profileBandH);
    tableRowH = d("tableRowH", tableRowH);
    leftLabelW = d("leftLabelW", leftLabelW);
    textSize = d("textSize", textSize);

    minorTick = d("minorTick", minorTick);
    majorTick = d("majorTick", majorTick);
    elevGridStep = d("elevGridStep", elevGridStep);
    tableInterval = d("tableInterval", tableInterval);
    chainagePrefix = s("chainagePrefix", chainagePrefix);
    kmFormat = b("kmFormat", kmFormat);
    useContChainage = b("useContChainage", useContChainage);
    elevDecimals = i("elevDecimals", elevDecimals);

    coordGrid = d("coordGrid", coordGrid);
    showNorthArrow = b("showNorthArrow", showNorthArrow);
    showKeyPoints = b("showKeyPoints", showKeyPoints);
    showCurveData = b("showCurveData", showCurveData);

    showGridLines = b("showGridLines", showGridLines);
    showPVI = b("showPVI", showPVI);
    showGradeLabels = b("showGradeLabels", showGradeLabels);

    rowElevation = b("rowElevation", rowElevation);
    rowGrade = b("rowGrade", rowGrade);
    rowCurve = b("rowCurve", rowCurve);
    rowAzimuth = b("rowAzimuth", rowAzimuth);

    titleTemplate = s("titleTemplate", titleTemplate);
    numberTemplate = s("numberTemplate", numberTemplate);
    replaceExisting = b("replaceExisting", replaceExisting);

    // 防呆：避免 0／負值造成除以零或無窮迴圈
    if (!(hScale > 0)) hScale = 1000.0;
    if (!(vScale > 0)) vScale = 100.0;
    if (!(minorTick > 0)) minorTick = 20.0;
    if (!(majorTick > 0)) majorTick = 100.0;
    if (!(tableInterval > 0)) tableInterval = 50.0;
    if (!(textSize > 0)) textSize = 2.5;
    if (overlap < 0) overlap = 0;
    if (!(joinTol >= 0)) joinTol = 2.0;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 設定檔
// ─────────────────────────────────────────────────────────────────────────────

bool PlanProfileSettings::saveToFile(const QString& path, QString* error) const
{
    QJsonObject root;
    root["type"] = QStringLiteral("AICAD.PlanProfileSettings");
    root["version"] = 1;
    root["settings"] = toJson();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("無法寫入：%1").arg(path);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool PlanProfileSettings::loadFromFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("無法讀取：%1").arg(path);
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject()) {
        if (error) *error = QStringLiteral("JSON 解析失敗：%1").arg(pe.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value("type").toString() != QLatin1String("AICAD.PlanProfileSettings")) {
        if (error) *error = QStringLiteral("不是平縱面圖設定檔（type 不符）");
        return false;
    }
    return fromJson(root.value("settings").toObject());
}

// ─────────────────────────────────────────────────────────────────────────────
// 使用者預設值（QSettings convention：org/app = "AICAD"/"AICAD"）
// ─────────────────────────────────────────────────────────────────────────────

void PlanProfileSettings::saveAsDefault() const
{
    PlanProfileSettings copy = *this;
    copy.tclId.clear();            // 線形與範圍屬於專案內容，不當作使用者預設
    copy.tclIds.clear();
    copy.useFullRange = true;
    copy.startCh = copy.endCh = 0.0;

    QSettings qs("AICAD", "AICAD");
    qs.beginGroup("PlanProfile");
    qs.setValue("defaults",
                QString::fromUtf8(QJsonDocument(copy.toJson()).toJson(QJsonDocument::Compact)));
    qs.endGroup();

    // 紙張與方向與 Drawing Sheet 對話框共用
    DrawingPrefs dp;
    dp.paperSize = paperSize;
    dp.orientation = orientation;
    saveDrawingPrefs(dp);
}

DrawingPrefs loadDrawingPrefs()
{
    DrawingPrefs p;
    QSettings qs("AICAD", "AICAD");
    qs.beginGroup("Drawing");
    p.paperSize     = qs.value("paperSize", -1).toInt();
    p.orientation   = qs.value("orientation", -1).toInt();
    p.unit          = qs.value("unit", -1).toInt();
    p.frameTemplate = qs.value("frameTemplate", -1).toInt();
    p.projection    = qs.value("projection", -1).toInt();
    p.defaultScale  = qs.value("defaultScale", -1.0).toDouble();
    p.lwThin        = qs.value("lwThin", -1.0).toDouble();
    p.lwMedium      = qs.value("lwMedium", -1.0).toDouble();
    p.lwThick       = qs.value("lwThick", -1.0).toDouble();
    qs.endGroup();
    return p;
}

void saveDrawingPrefs(const DrawingPrefs& p)
{
    QSettings qs("AICAD", "AICAD");
    qs.beginGroup("Drawing");
    if (p.paperSize >= 0)     qs.setValue("paperSize", p.paperSize);
    if (p.orientation >= 0)   qs.setValue("orientation", p.orientation);
    if (p.unit >= 0)          qs.setValue("unit", p.unit);
    if (p.frameTemplate >= 0) qs.setValue("frameTemplate", p.frameTemplate);
    if (p.projection >= 0)    qs.setValue("projection", p.projection);
    if (p.defaultScale >= 0)  qs.setValue("defaultScale", p.defaultScale);
    if (p.lwThin >= 0)        qs.setValue("lwThin", p.lwThin);
    if (p.lwMedium >= 0)      qs.setValue("lwMedium", p.lwMedium);
    if (p.lwThick >= 0)       qs.setValue("lwThick", p.lwThick);
    qs.endGroup();
}

PlanProfileSettings PlanProfileSettings::loadDefault()
{
    PlanProfileSettings s;
    QSettings qs("AICAD", "AICAD");
    qs.beginGroup("PlanProfile");
    const QString text = qs.value("defaults").toString();
    qs.endGroup();
    if (!text.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isObject()) s.fromJson(doc.object());
    }
    // 紙張與方向：採用與 Drawing Sheet 對話框同步的最近一次值
    const DrawingPrefs dp = loadDrawingPrefs();
    if (dp.paperSize >= 0 && dp.paperSize <= 4)   s.paperSize = dp.paperSize;
    if (dp.orientation >= 0 && dp.orientation <= 1) s.orientation = dp.orientation;
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// 線形資料表欄位
// ─────────────────────────────────────────────────────────────────────────────

QVector<PlanProfileTableColumn> planProfileTableColumnDefs(int kind)
{
    auto C = [](const char* id, const QString& label, double w, bool center, bool between = false) {
        PlanProfileTableColumn c; c.id = QLatin1String(id); c.label = label; c.width = w; c.center = center;
        c.between = between;
        return c;
    };
    QVector<PlanProfileTableColumn> v;
    if (kind == 1) {
        v << C("pt",    QStringLiteral("點位\nPoint"), 8, true)
          << C("name",  QStringLiteral("點名\nName"), 9, true)
          << C("chain", QStringLiteral("里程\nChainage"), 16, false)
          << C("cont",  QStringLiteral("連續里程\nCont. Ch."), 14, false)
          << C("e",     QStringLiteral("E (m)\nTM2"), 15, false)
          << C("n",     QStringLiteral("N (m)\nTM2"), 15, false)
          << C("az",    QStringLiteral("方位角\nAzimuth"), 16, false)
          << C("len",   QStringLiteral("長度\nLength (m)"), 10, false, true)
          << C("rc",    QStringLiteral("半徑／曲線型式\nRadius / Type"), 15, false, true)
          << C("rad",   QStringLiteral("半徑\nRadius (m)"), 11, false, true)
          << C("curve", QStringLiteral("曲線型式\nCurve Type"), 13, true, true)
          << C("ccno",  QStringLiteral("曲線編號\nCurve No."), 9, true, true)
          << C("cant",  QStringLiteral("超高\nCant (mm)"), 8, false, true)
          << C("gauge", QStringLiteral("軌距加寬\nGauge (mm)"), 8, false, true)
          << C("speed", QStringLiteral("速限\nSpeed"), 7, false, true)
          << C("text1", QStringLiteral("備註一\nText1"), 12, true, true)
          << C("text2", QStringLiteral("備註二\nText2"), 12, true, true)
          << C("real1", QStringLiteral("數值一\nReal1"), 9, false, true)
          << C("real2", QStringLiteral("數值二\nReal2"), 9, false, true);
    } else {
        v << C("pt",    QStringLiteral("點位\nPoint"), 9, true)
          << C("chain", QStringLiteral("里程\nChainage"), 18, false)
          << C("elev",  QStringLiteral("高程\nElev. (m)"), 15, false)
          << C("grade", QStringLiteral("坡度\nGrade (%)"), 20, false)
          << C("pvi",   QStringLiteral("PVI 高程\nPVI Elev. (m)"), 15, false)
          << C("lvc",   QStringLiteral("Lvc (m)"), 10, false)
          << C("k",     QStringLiteral("K 值\nK"), 10, false)
          << C("dist",  QStringLiteral("間距\nDist. (m)"), 11, false, true)
          << C("mo",    QStringLiteral("Mo (m)"), 10, false);
    }
    return v;
}

QStringList planProfileDefaultTableColumns(int kind)
{
    if (kind == 1)
        return { "pt", "chain", "e", "n", "az", "len", "rc", "cant", "speed" };
    return { "pt", "chain", "elev", "grade", "pvi", "lvc", "k", "mo", "dist" };
}

QStringList planProfileEffectiveTableColumns(const PlanProfileSettings& s, int kind)
{
    const QVector<PlanProfileTableColumn> defs = planProfileTableColumnDefs(kind);
    QStringList out;
    for (const QString& id : (kind == 1 ? s.hzCols : s.vtCols)) {
        bool known = false;
        for (const auto& d : defs) if (d.id == id) known = true;
        if (known && !out.contains(id)) out << id;
    }
    return out.isEmpty() ? planProfileDefaultTableColumns(kind) : out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layer
// ─────────────────────────────────────────────────────────────────────────────

QJsonObject PlanProfileLayer::toJson() const
{
    QJsonObject j;
    j["enabled"] = enabled;
    j["isIndex"] = isIndex;
    j["tableKind"] = tableKind;
    j["tableTclId"] = tableTclId;
    j["tableTclIds"] = QJsonArray::fromStringList(tableTclIds);
    j["tablePage"] = tablePage;
    j["tablePages"] = tablePages;
    {
        QJsonArray ba;
        for (const SchemaBand& b : schemaBands) {
            QJsonObject o;
            o["name"] = b.name;
            o["tclIds"] = QJsonArray::fromStringList(b.tclIds);
            ba.append(o);
        }
        j["schemaBands"] = ba;
        QJsonArray la;
        for (const SchemaLink& k : schemaLinks) {
            QJsonObject o;
            o["name"] = k.name; o["kind"] = k.kind;
            o["aTcl"] = k.aTcl; o["aCh"] = k.aCh;
            o["bTcl"] = k.bTcl; o["bCh"] = k.bCh;
            o["side"] = k.side; o["dir"] = k.dir;
            la.append(o);
        }
        j["schemaLinks"] = la;
    }
    j["settings"] = settings.toJson();
    j["startCh"] = startCh;
    j["endCh"] = endCh;
    j["index"] = index;
    j["count"] = count;
    j["groupId"] = groupId;
    j["prevNo"] = prevNo;
    j["nextNo"] = nextNo;
    j["depotView"] = depotView.toJson();
    QJsonArray pa;
    for (const PlanProfileSegment& g : pieces) {
        QJsonObject o;
        o["tclId"] = g.tclId; o["start"] = g.start; o["end"] = g.end;
        pa.append(o);
    }
    j["pieces"] = pa;
    j["overrides"] = overrides;
    return j;
}

bool PlanProfileLayer::fromJson(const QJsonObject& j)
{
    enabled = j.value("enabled").toBool(true);
    isIndex = j.value("isIndex").toBool(false);
    tableKind  = j.value("tableKind").toInt(0);
    tableTclId = j.value("tableTclId").toString();
    tableTclIds.clear();
    for (const QJsonValue& v : j.value("tableTclIds").toArray()) tableTclIds << v.toString();
    tablePage  = j.value("tablePage").toInt(1);
    tablePages = j.value("tablePages").toInt(1);
    schemaBands.clear();
    for (const QJsonValue& v : j.value("schemaBands").toArray()) {
        const QJsonObject o = v.toObject();
        SchemaBand b;
        b.name = o.value("name").toString();
        for (const QJsonValue& t : o.value("tclIds").toArray()) b.tclIds << t.toString();
        schemaBands.append(b);
    }
    depotView = DepotView::fromJson(j.value("depotView").toObject());
    schemaLinks.clear();
    for (const QJsonValue& v : j.value("schemaLinks").toArray()) {
        const QJsonObject o = v.toObject();
        SchemaLink k;
        k.name = o.value("name").toString();
        k.kind = o.value("kind").toInt(2);
        k.aTcl = o.value("aTcl").toString(); k.aCh = o.value("aCh").toDouble();
        k.bTcl = o.value("bTcl").toString(); k.bCh = o.value("bCh").toDouble();
        k.side = o.value("side").toInt(1); k.dir = o.value("dir").toInt(1);
        schemaLinks.append(k);
    }
    settings = PlanProfileSettings();
    settings.fromJson(j.value("settings").toObject());
    startCh = j.value("startCh").toDouble();
    endCh   = j.value("endCh").toDouble();
    index   = j.value("index").toInt(1);
    count   = j.value("count").toInt(1);
    groupId = j.value("groupId").toString();
    prevNo = j.value("prevNo").toString();
    nextNo = j.value("nextNo").toString();
    overrides = j.value("overrides").toObject();
    pieces.clear();
    for (const QJsonValue& v : j.value("pieces").toArray()) {
        const QJsonObject o = v.toObject();
        PlanProfileSegment g;
        g.tclId = o.value("tclId").toString();
        g.start = o.value("start").toDouble();
        g.end   = o.value("end").toDouble();
        pieces.append(g);
    }
    if (pieces.isEmpty() && !isIndex && tableKind == 0 && !settings.tclId.isEmpty()) {   // 舊檔相容：單一線形
        PlanProfileSegment g; g.tclId = settings.tclId; g.start = startCh; g.end = endCh;
        pieces.append(g);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 版面
// ─────────────────────────────────────────────────────────────────────────────

PlanProfileLayout computePlanProfileLayout(const QSizeF& paper, const PlanProfileSettings& s,
                                           double planHeightRequest)
{
    PlanProfileLayout L;

    // 直式標題欄在右側（寬 60，距紙邊 10；見 DrawingSheetPainter::paintTitleBlock），
    // 圖表區域避開它。
    constexpr double kInner = 10.0, kTitleW = 60.0;
    const double top    = kInner + 5.0;
    const double bottom = paper.height() - kInner - 5.0;
    const double right  = paper.width() - kInner - kTitleW - 5.0;
    L.area = QRectF(kInner + 5.0, top, right - (kInner + 5.0), bottom - top);

    L.plotLeft  = L.area.left() + s.leftLabelW;
    L.plotWidth = L.area.right() - 6.0 - L.plotLeft;   // 右側留 6mm 給最後一個標註
    constexpr double kDataPad = 10.0;                   // 框內左右各留的邊界（放接圖線與標示）
    L.dataLeft  = L.plotLeft + kDataPad;
    L.dataWidth = L.plotWidth - 2 * kDataPad;

    // 資料表列數：里程列 + 啟用的列
    // 資料表文字為垂直書寫，列高依該列最長文字決定（至少 tableRowH）
    L.rowH = s.tableRowH;
    auto rowHeight = [&](int kind) {
        const int chars = kind == 0 ? 17 : kind == 1 ? s.elevDecimals + 5 : kind == 2 ? 7
                        : kind == 3 ? 12 : 10;
        return qMax(s.tableRowH, chars * s.textSize * 0.62 + 3.0);
    };
    L.rowKinds << 0;
    if (s.rowElevation) L.rowKinds << 1;
    if (s.rowGrade)     L.rowKinds << 2;
    if (s.rowCurve)     L.rowKinds << 3;
    if (s.rowAzimuth)   L.rowKinds << 4;
    double tableH = 0;
    for (int k : L.rowKinds) { L.rowHs << rowHeight(k); tableH += L.rowHs.last(); }
    L.rowCount = L.rowKinds.size();
    constexpr double kGap = 9.0;          // 框與下一區之間（放「框下方」的標示文字）
    double planH = qMax(s.planBandH, planHeightRequest);
    double profH = s.profileBandH;

    // 總高度超過可用區域時，先壓縮縱斷面（最小 30mm），再壓縮平面圖（最小 25mm）
    double overflow = planH + kGap + profH + kGap + tableH - L.area.height();
    if (overflow > 0) { const double cut = qMin(overflow, qMax(0.0, profH - 30.0)); profH -= cut; overflow -= cut; }
    if (overflow > 0) { const double cut = qMin(overflow, qMax(0.0, planH - 25.0)); planH -= cut; }

    L.planRect    = QRectF(L.plotLeft, L.area.top(), L.plotWidth, planH);
    L.profileRect = QRectF(L.plotLeft, L.planRect.bottom() + kGap, L.plotWidth, profH);
    L.tableRect   = QRectF(L.area.left(), L.profileRect.bottom() + kGap,
                           L.area.width(), tableH);
    return L;
}

double planProfileSegmentLength(const QSizeF& paper, const PlanProfileSettings& s)
{
    if (s.segmentLength > 0.0) return s.segmentLength;
    const PlanProfileLayout L = computePlanProfileLayout(paper, s);
    const double lenM = L.dataWidth * s.hScale / 1000.0;          // 繪圖寬度可涵蓋的實長
    const double step = qMax(s.majorTick, 1.0);
    return qMax(step, std::floor(lenM / step) * step);            // 取主刻度整數倍
}

} // namespace drawing
} // namespace aicad
