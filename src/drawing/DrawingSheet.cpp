/**
 * @file DrawingSheet.cpp
 * @brief 圖紙文件主類別實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "DrawingSheet.h"
#include "cad/Document.h"
#include "cad/Feature.h"
#include "DrawingViewRenderer.h"

#include <QHash>
#include <QPdfWriter>
#include <QPageLayout>
#include <QPageSize>
#ifdef AICAD_HAVE_QTSVG
#include <QSvgGenerator>
#endif
#include <QPainter>
#include <QFileInfo>
#include <QDir>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>

#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDebug>

namespace aicad {
namespace drawing {

// ─────────────────────────────────────────────────────────────────────────────
// SheetConfig
// ─────────────────────────────────────────────────────────────────────────────

QSizeF SheetConfig::paperSizeMM() const
{
    // 標準尺寸（Portrait 方向）
    static const QMap<int, QSizeF> sizes = {
                                            { (int)PaperSize::A4,      { 210,  297 } },
                                            { (int)PaperSize::A3,      { 297,  420 } },
                                            { (int)PaperSize::A2,      { 420,  594 } },
                                            { (int)PaperSize::A1,      { 594,  841 } },
                                            { (int)PaperSize::A0,      { 841, 1189 } },
                                            { (int)PaperSize::B4,      { 250,  353 } },
                                            { (int)PaperSize::B3,      { 353,  500 } },
                                            { (int)PaperSize::Letter,  { 216,  279 } },
                                            { (int)PaperSize::Tabloid, { 279,  432 } },
                                            };

    QSizeF sz = sizes.value((int)paperSize, { 297, 420 });

    if (orientation == PaperOrientation::Landscape) {
        sz = sz.transposed();   // 寬高互換
    }
    return sz;
}

QJsonObject SheetConfig::toJson() const
{
    QJsonObject j;
    j["paperSize"]        = (int)paperSize;
    j["orientation"]      = (int)orientation;
    j["unit"]             = (int)unit;
    j["frameTemplate"]    = (int)frameTemplate;
    j["projectionMethod"] = (int)projectionMethod;
    j["defaultScale"]     = defaultScale;
    j["lineWidthThin"]    = lineWidthThin;
    j["lineWidthMedium"]  = lineWidthMedium;
    j["lineWidthThick"]   = lineWidthThick;
    if (!customTemplatePath.isEmpty())
        j["customTemplatePath"] = customTemplatePath;
    return j;
}

bool SheetConfig::fromJson(const QJsonObject& j)
{
    paperSize        = (PaperSize)       j.value("paperSize").toInt((int)PaperSize::A3);
    orientation      = (PaperOrientation)j.value("orientation").toInt((int)PaperOrientation::Landscape);
    unit             = (DrawingUnit)     j.value("unit").toInt((int)DrawingUnit::Millimeter);
    frameTemplate    = (FrameTemplate)   j.value("frameTemplate").toInt((int)FrameTemplate::Standard);
    projectionMethod = (ProjectionMethod)j.value("projectionMethod").toInt((int)ProjectionMethod::ThirdAngle);
    defaultScale     = j.value("defaultScale").toDouble(1.0);
    lineWidthThin    = j.value("lineWidthThin").toDouble(0.25);
    lineWidthMedium  = j.value("lineWidthMedium").toDouble(0.5);
    lineWidthThick   = j.value("lineWidthThick").toDouble(0.7);
    customTemplatePath = j.value("customTemplatePath").toString();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TitleBlock
// ─────────────────────────────────────────────────────────────────────────────

QJsonObject TitleBlock::toJson() const
{
    QJsonObject j;
    j["drawingNumber"]   = drawingNumber;
    j["drawingTitle"]    = drawingTitle;
    j["sheetNumber"]     = sheetNumber;
    j["revision"]        = revision;
    j["companyName"]     = companyName;
    j["companyLogo"]     = companyLogo;
    j["projectName"]     = projectName;
    j["partNumber"]      = partNumber;
    j["designedBy"]      = designedBy;
    j["checkedBy"]       = checkedBy;
    j["approvedBy"]      = approvedBy;
    j["designDate"]      = designDate;
    j["approvalDate"]    = approvalDate;
    j["material"]        = material;
    j["surfaceFinish"]   = surfaceFinish;
    j["tolerance"]       = tolerance;
    j["angularTolerance"]= angularTolerance;
    j["lastRevisionDesc"]= lastRevisionDesc;
    j["lastRevisionDate"]= lastRevisionDate;

    if (!customFields.isEmpty()) {
        QJsonObject cf;
        for (auto it = customFields.cbegin(); it != customFields.cend(); ++it)
            cf[it.key()] = it.value();
        j["customFields"] = cf;
    }
    return j;
}

bool TitleBlock::fromJson(const QJsonObject& j)
{
    drawingNumber    = j["drawingNumber"].toString();
    drawingTitle     = j["drawingTitle"].toString();
    sheetNumber      = j["sheetNumber"].toString();
    revision         = j["revision"].toString();
    companyName      = j["companyName"].toString();
    companyLogo      = j["companyLogo"].toString();
    projectName      = j["projectName"].toString();
    partNumber       = j["partNumber"].toString();
    designedBy       = j["designedBy"].toString();
    checkedBy        = j["checkedBy"].toString();
    approvedBy       = j["approvedBy"].toString();
    designDate       = j["designDate"].toString();
    approvalDate     = j["approvalDate"].toString();
    material         = j["material"].toString();
    surfaceFinish    = j["surfaceFinish"].toString();
    tolerance        = j["tolerance"].toString();
    angularTolerance = j["angularTolerance"].toString();
    lastRevisionDesc = j["lastRevisionDesc"].toString();
    lastRevisionDate = j["lastRevisionDate"].toString();

    if (j.contains("customFields")) {
        QJsonObject cf = j["customFields"].toObject();
        for (auto it = cf.begin(); it != cf.end(); ++it)
            customFields[it.key()] = it.value().toString();
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawingView
// ─────────────────────────────────────────────────────────────────────────────

DrawingView::DrawingView(QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_label("View")
    , m_size(100, 100)
{
}

DrawingView::~DrawingView() = default;

QRectF DrawingView::boundingRect() const
{
    // position 為視圖中心（與 DrawingSheetPainter::paintView 一致）
    return QRectF(m_position - QPointF(m_size.width() / 2, m_size.height() / 2), m_size);
}

void DrawingView::setLabel(const QString& label)
{
    if (m_label == label) return;
    m_label = label;
    Q_EMIT labelChanged(label);
    Q_EMIT paramsChanged();
}

void DrawingView::setType(ViewType type)
{
    if (m_type == type) return;
    m_type = type;
    Q_EMIT paramsChanged();
    Q_EMIT rebuildRequested();
}

void DrawingView::setPosition(const QPointF& pos)
{
    if (m_position == pos) return;
    m_position = pos;
    Q_EMIT positionChanged(pos);
}

void DrawingView::setSize(const QSizeF& size)
{
    if (m_size == size) return;
    m_size = size;
    Q_EMIT paramsChanged();
}

void DrawingView::setScale(double scale)
{
    if (qFuzzyCompare(m_scale, scale)) return;
    m_scale = scale;
    Q_EMIT scaleChanged(scale);
    Q_EMIT rebuildRequested();
}

void DrawingView::setAutoFit(bool on)
{
    if (m_autoFit == on) return;
    m_autoFit = on;
    Q_EMIT paramsChanged();
    if (on) Q_EMIT rebuildRequested();   // 重新渲染 → 重新 Fit
}

void DrawingView::applyFitScale(double scale)
{
    if (scale <= 0.0 || qFuzzyCompare(m_scale, scale)) return;
    m_scale = scale;
    Q_EMIT scaleChanged(scale);
}

void DrawingView::setSourceFeature(cad::Feature* feature)
{
    if (m_sourceFeature == feature) return;
    m_sourceFeature = feature;
    Q_EMIT paramsChanged();
    Q_EMIT rebuildRequested();
}

void DrawingView::setSectionParams(const SectionParams& params)
{
    m_sectionParams = params;
    Q_EMIT paramsChanged();
    Q_EMIT rebuildRequested();
}

void DrawingView::setDetailParams(const DetailParams& params)
{
    m_detailParams = params;
    Q_EMIT paramsChanged();
    Q_EMIT rebuildRequested();
}

void DrawingView::setShowHiddenLines(bool show)
{
    if (m_showHiddenLines == show) return;
    m_showHiddenLines = show;
    Q_EMIT rebuildRequested();
}

void DrawingView::setShowCenterLines(bool show)
{
    if (m_showCenterLines == show) return;
    m_showCenterLines = show;
    Q_EMIT rebuildRequested();
}

void DrawingView::setShowDimensions(bool show)
{
    if (m_showDimensions == show) return;
    m_showDimensions = show;
    Q_EMIT paramsChanged();
}

void DrawingView::setShowAnnotations(bool show)
{
    if (m_showAnnotations == show) return;
    m_showAnnotations = show;
    Q_EMIT paramsChanged();
}

void DrawingView::setShowScaleLabel(bool show)
{
    if (m_showScaleLabel == show) return;
    m_showScaleLabel = show;
    Q_EMIT paramsChanged();
}

void DrawingView::setShowViewLabel(bool show)
{
    if (m_showViewLabel == show) return;
    m_showViewLabel = show;
    Q_EMIT paramsChanged();
}

QJsonObject DrawingView::toJson() const
{
    QJsonObject j;
    j["id"]    = m_id.toString();
    j["label"] = m_label;
    j["type"]  = (int)m_type;
    j["posX"]  = m_position.x();
    j["posY"]  = m_position.y();
    j["sizeW"] = m_size.width();
    j["sizeH"] = m_size.height();
    j["scale"] = m_scale;
    j["autoFit"] = m_autoFit;

    if (m_sourceFeature)
        j["sourceFeatureId"] = m_sourceFeature->id();

    j["showHiddenLines"]  = m_showHiddenLines;
    j["showCenterLines"]  = m_showCenterLines;
    j["showDimensions"]   = m_showDimensions;
    j["showAnnotations"]  = m_showAnnotations;
    j["showScaleLabel"]   = m_showScaleLabel;
    j["showViewLabel"]    = m_showViewLabel;

    // Section params
    if (m_type == ViewType::Section) {
        QJsonObject sp;
        sp["direction"]  = (int)m_sectionParams.direction;
        sp["startX"]     = m_sectionParams.cutPlaneStart.x();
        sp["startY"]     = m_sectionParams.cutPlaneStart.y();
        sp["endX"]       = m_sectionParams.cutPlaneEnd.x();
        sp["endY"]       = m_sectionParams.cutPlaneEnd.y();
        sp["label"]      = m_sectionParams.sectionLabel;
        sp["offset"]     = m_sectionParams.offset;
        sp["showCutLine"]= m_sectionParams.showCutLine;
        sp["showArrows"] = m_sectionParams.showArrows;
        j["sectionParams"] = sp;
    }

    // Detail params
    if (m_type == ViewType::Detail) {
        QJsonObject dp;
        dp["centerX"] = m_detailParams.center.x();
        dp["centerY"] = m_detailParams.center.y();
        dp["radius"]  = m_detailParams.radius;
        dp["scale"]   = m_detailParams.scale;
        dp["label"]   = m_detailParams.label;
        dp["sourceType"] = (int)m_detailParams.sourceType;
        j["detailParams"] = dp;
    }

    return j;
}

bool DrawingView::fromJson(const QJsonObject& j)
{
    m_id    = QUuid::fromString(j["id"].toString());
    m_label = j["label"].toString();
    m_type  = (ViewType)j["type"].toInt();
    m_position = { j["posX"].toDouble(), j["posY"].toDouble() };
    m_size     = { j["sizeW"].toDouble(100), j["sizeH"].toDouble(100) };
    m_scale    = j["scale"].toDouble(1.0);
    m_autoFit  = j.value("autoFit").toBool(true);
    m_pendingSourceFeatureId = j.value("sourceFeatureId").toString();

    m_showHiddenLines = j.value("showHiddenLines").toBool(true);
    m_showCenterLines = j.value("showCenterLines").toBool(true);
    m_showDimensions  = j.value("showDimensions").toBool(true);
    m_showAnnotations = j.value("showAnnotations").toBool(true);
    m_showScaleLabel  = j.value("showScaleLabel").toBool(true);
    m_showViewLabel   = j.value("showViewLabel").toBool(true);

    if (j.contains("sectionParams")) {
        QJsonObject sp = j["sectionParams"].toObject();
        m_sectionParams.direction       = (SectionDirection)sp["direction"].toInt();
        m_sectionParams.cutPlaneStart   = { sp["startX"].toDouble(), sp["startY"].toDouble() };
        m_sectionParams.cutPlaneEnd     = { sp["endX"].toDouble(),   sp["endY"].toDouble() };
        m_sectionParams.sectionLabel    = sp["label"].toString();
        m_sectionParams.offset          = sp.value("offset").toDouble(0.0);
        m_sectionParams.showCutLine     = sp.value("showCutLine").toBool(true);
        m_sectionParams.showArrows      = sp.value("showArrows").toBool(true);
    }

    if (j.contains("detailParams")) {
        QJsonObject dp = j["detailParams"].toObject();
        m_detailParams.center = { dp["centerX"].toDouble(), dp["centerY"].toDouble() };
        m_detailParams.radius = dp["radius"].toDouble(10.0);
        m_detailParams.scale  = dp["scale"].toDouble(2.0);
        m_detailParams.label  = dp["label"].toString();
        m_detailParams.sourceType = (ViewType)dp.value("sourceType").toInt((int)ViewType::Front);
    }

    // 注意：sourceFeatureId 的 Feature* 解析由 DrawingSheet 在全部載入後處理
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// PartsListTable
// ─────────────────────────────────────────────────────────────────────────────

PartsListTable::PartsListTable(QObject* parent)
    : QObject(parent)
{
}

void PartsListTable::setConfig(const PartsListConfig& config)
{
    m_config = config;
    Q_EMIT configChanged();
}

void PartsListTable::setRows(const QVector<PartsListRow>& rows)
{
    m_rows = rows;
    Q_EMIT rowsChanged();
}

void PartsListTable::addRow(const PartsListRow& row)
{
    m_rows.append(row);
    Q_EMIT rowsChanged();
}

void PartsListTable::removeRow(int index)
{
    if (index < 0 || index >= m_rows.size()) return;
    m_rows.removeAt(index);
    Q_EMIT rowsChanged();
}

void PartsListTable::moveRow(int from, int to)
{
    if (from < 0 || from >= m_rows.size()) return;
    if (to   < 0 || to   >= m_rows.size()) return;
    m_rows.move(from, to);
    Q_EMIT rowsChanged();
}

void PartsListTable::populateFromDocument(cad::Document* document)
{
    if (!document) return;

    // 只收錄「含實體（Solid）」的特徵；草圖、參考平面、純線框等不列入。
    // 零件編號沿用特徵名稱；說明用特徵類型。已存在的手動欄位（材質／備註）
    // 以料號比對保留，避免重新產生時被清空。
    QHash<QString, PartsListRow> old;
    for (const PartsListRow& r : m_rows) old.insert(r.partNumber, r);

    QVector<PartsListRow> rows;
    int no = 1;
    for (cad::Feature* f : document->features()) {
        if (!f || f->isSuppressed()) continue;
        const TopoDS_Shape shape = f->shape();
        if (shape.IsNull()) continue;
        TopExp_Explorer ex(shape, TopAbs_SOLID);
        if (!ex.More()) continue;

        PartsListRow row;
        row.itemNo      = no++;
        row.partNumber  = f->name();
        row.description = f->typeString();
        row.quantity    = 1;
        row.unit        = QStringLiteral("個");
        if (old.contains(row.partNumber)) {
            const PartsListRow& o = old[row.partNumber];
            row.material = o.material;
            row.revision = o.revision;
            row.remark   = o.remark;
            row.customColumns = o.customColumns;
        }
        rows.append(row);
    }
    setRows(rows);
    qDebug() << "[PartsListTable] populateFromDocument:" << rows.size() << "row(s)";
}

QJsonObject PartsListTable::toJson() const
{
    QJsonObject j;

    // Config
    QJsonObject cfg;
    cfg["posX"]         = m_config.position.x();
    cfg["posY"]         = m_config.position.y();
    cfg["rowHeight"]    = m_config.rowHeight;
    cfg["headerHeight"] = m_config.headerHeight;
    cfg["autoFromDocument"] = m_config.autoFromDocument;
    QJsonArray cols;
    for (const QString& c : m_config.visibleColumns)
        cols.append(c);
    cfg["visibleColumns"] = cols;
    j["config"] = cfg;

    // Rows
    QJsonArray rows;
    for (const PartsListRow& row : m_rows) {
        QJsonObject r;
        r["itemNo"]      = row.itemNo;
        r["partNumber"]  = row.partNumber;
        r["description"] = row.description;
        r["material"]    = row.material;
        r["quantity"]    = row.quantity;
        r["unit"]        = row.unit;
        r["revision"]    = row.revision;
        r["remark"]      = row.remark;
        rows.append(r);
    }
    j["rows"] = rows;

    return j;
}

bool PartsListTable::fromJson(const QJsonObject& j)
{
    if (j.contains("config")) {
        QJsonObject cfg = j["config"].toObject();
        m_config.position         = { cfg["posX"].toDouble(), cfg["posY"].toDouble() };
        m_config.rowHeight        = cfg.value("rowHeight").toDouble(8.0);
        m_config.headerHeight     = cfg.value("headerHeight").toDouble(10.0);
        m_config.autoFromDocument = cfg.value("autoFromDocument").toBool(true);
        if (cfg.contains("visibleColumns")) {
            m_config.visibleColumns.clear();
            for (const QJsonValue& v : cfg["visibleColumns"].toArray())
                m_config.visibleColumns.append(v.toString());
        }
    }

    if (j.contains("rows")) {
        m_rows.clear();
        for (const QJsonValue& v : j["rows"].toArray()) {
            QJsonObject r = v.toObject();
            PartsListRow row;
            row.itemNo      = r["itemNo"].toInt();
            row.partNumber  = r["partNumber"].toString();
            row.description = r["description"].toString();
            row.material    = r["material"].toString();
            row.quantity    = r["quantity"].toInt();
            row.unit        = r["unit"].toString();
            row.revision    = r["revision"].toString();
            row.remark      = r["remark"].toString();
            m_rows.append(row);
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RevisionTable
// ─────────────────────────────────────────────────────────────────────────────

RevisionTable::RevisionTable(QObject* parent)
    : QObject(parent)
{
}

void RevisionTable::setPosition(const QPointF& pos)
{
    m_position = pos;
    Q_EMIT entriesChanged();
}

void RevisionTable::addEntry(const RevisionEntry& entry)
{
    m_entries.append(entry);
    Q_EMIT entriesChanged();
}

void RevisionTable::removeEntry(int index)
{
    if (index < 0 || index >= m_entries.size()) return;
    m_entries.removeAt(index);
    Q_EMIT entriesChanged();
}

QString RevisionTable::latestRevision() const
{
    if (m_entries.isEmpty()) return {};
    return m_entries.last().revision;
}

QJsonObject RevisionTable::toJson() const
{
    QJsonObject j;
    j["posX"] = m_position.x();
    j["posY"] = m_position.y();

    QJsonArray arr;
    for (const RevisionEntry& e : m_entries) {
        QJsonObject je;
        je["revision"]   = e.revision;
        je["date"]       = e.date;
        je["description"]= e.description;
        je["changedBy"]  = e.changedBy;
        je["approvedBy"] = e.approvedBy;
        je["zone"]       = e.zone;
        arr.append(je);
    }
    j["entries"] = arr;
    return j;
}

bool RevisionTable::fromJson(const QJsonObject& j)
{
    m_position = { j["posX"].toDouble(), j["posY"].toDouble() };
    m_entries.clear();
    for (const QJsonValue& v : j["entries"].toArray()) {
        QJsonObject je = v.toObject();
        RevisionEntry e;
        e.revision    = je["revision"].toString();
        e.date        = je["date"].toString();
        e.description = je["description"].toString();
        e.changedBy   = je["changedBy"].toString();
        e.approvedBy  = je["approvedBy"].toString();
        e.zone        = je["zone"].toString();
        m_entries.append(e);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawingIndex
// ─────────────────────────────────────────────────────────────────────────────

DrawingIndex::DrawingIndex(QObject* parent)
    : QObject(parent)
{
}

void DrawingIndex::setPosition(const QPointF& pos) { m_position = pos; }
void DrawingIndex::setTitle(const QString& title)  { m_title = title; }

void DrawingIndex::addEntry(const DrawingIndexEntry& entry)
{
    m_entries.append(entry);
    Q_EMIT entriesChanged();
}

void DrawingIndex::removeEntry(int index)
{
    if (index < 0 || index >= m_entries.size()) return;
    m_entries.removeAt(index);
    Q_EMIT entriesChanged();
}

QJsonObject DrawingIndex::toJson() const
{
    QJsonObject j;
    j["posX"]  = m_position.x();
    j["posY"]  = m_position.y();
    j["title"] = m_title;
    QJsonArray arr;
    for (const DrawingIndexEntry& e : m_entries) {
        QJsonObject je;
        je["sheetNumber"]  = e.sheetNumber;
        je["drawingNumber"]= e.drawingNumber;
        je["title"]        = e.title;
        je["revision"]     = e.revision;
        je["date"]         = e.date;
        je["remark"]       = e.remark;
        arr.append(je);
    }
    j["entries"] = arr;
    return j;
}

bool DrawingIndex::fromJson(const QJsonObject& j)
{
    m_position = { j["posX"].toDouble(), j["posY"].toDouble() };
    m_title    = j.value("title").toString("Drawing Index");
    m_entries.clear();
    for (const QJsonValue& v : j["entries"].toArray()) {
        QJsonObject je = v.toObject();
        DrawingIndexEntry e;
        e.sheetNumber   = je["sheetNumber"].toString();
        e.drawingNumber = je["drawingNumber"].toString();
        e.title         = je["title"].toString();
        e.revision      = je["revision"].toString();
        e.date          = je["date"].toString();
        e.remark        = je["remark"].toString();
        m_entries.append(e);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Legend
// ─────────────────────────────────────────────────────────────────────────────

Legend::Legend(QObject* parent)
    : QObject(parent)
{
}

void Legend::setPosition(const QPointF& pos) { m_position = pos; }
void Legend::setTitle(const QString& title)  { m_title = title; }

void Legend::addEntry(const LegendEntry& entry)
{
    m_entries.append(entry);
    Q_EMIT entriesChanged();
}

void Legend::removeEntry(int index)
{
    if (index < 0 || index >= m_entries.size()) return;
    m_entries.removeAt(index);
    Q_EMIT entriesChanged();
}

QJsonObject Legend::toJson() const
{
    QJsonObject j;
    j["posX"]  = m_position.x();
    j["posY"]  = m_position.y();
    j["title"] = m_title;
    QJsonArray arr;
    for (const LegendEntry& e : m_entries) {
        QJsonObject je;
        je["symbol"]      = e.symbol;
        je["description"] = e.description;
        arr.append(je);
    }
    j["entries"] = arr;
    return j;
}

bool Legend::fromJson(const QJsonObject& j)
{
    m_position = { j["posX"].toDouble(), j["posY"].toDouble() };
    m_title    = j.value("title").toString("Legend");
    m_entries.clear();
    for (const QJsonValue& v : j["entries"].toArray()) {
        QJsonObject je = v.toObject();
        LegendEntry e;
        e.symbol      = je["symbol"].toString();
        e.description = je["description"].toString();
        m_entries.append(e);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// NoteBlock
// ─────────────────────────────────────────────────────────────────────────────

QJsonObject NoteBlock::toJson() const
{
    QJsonObject j;
    j["id"]       = id.toString();
    j["title"]    = title;
    j["content"]  = content;
    j["posX"]     = position.x();
    j["posY"]     = position.y();
    j["sizeW"]    = size.width();
    j["sizeH"]    = size.height();
    j["fontSize"] = fontSize;
    j["hasBorder"]= hasBorder;
    return j;
}

bool NoteBlock::fromJson(const QJsonObject& j)
{
    id        = QUuid::fromString(j["id"].toString());
    title     = j["title"].toString();
    content   = j["content"].toString();
    position  = { j["posX"].toDouble(), j["posY"].toDouble() };
    size      = { j["sizeW"].toDouble(60), j["sizeH"].toDouble(40) };
    fontSize  = j.value("fontSize").toDouble(3.5);
    hasBorder = j.value("hasBorder").toBool(true);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawingSheet
// ─────────────────────────────────────────────────────────────────────────────

DrawingSheet::DrawingSheet(QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_partsList(new PartsListTable(this))
    , m_drawingIndex(new DrawingIndex(this))
    , m_revisionTable(new RevisionTable(this))
    , m_legend(new Legend(this))
{
    qDebug() << "[DrawingSheet]" << m_id.toString() << "created";
}

DrawingSheet::~DrawingSheet()
{
    qDeleteAll(m_views);
    m_views.clear();
    qDebug() << "[DrawingSheet]" << m_id.toString() << "destroyed";
}

void DrawingSheet::setModified(bool modified)
{
    if (m_modified == modified) return;
    m_modified = modified;
    Q_EMIT modifiedChanged(modified);
}

void DrawingSheet::setFileName(const QString& fileName)
{
    if (m_fileName == fileName) return;
    m_fileName = fileName;
    Q_EMIT fileNameChanged(fileName);
}

void DrawingSheet::setSourceDocument(cad::Document* doc)
{
    if (m_sourceDocument == doc) return;

    if (m_sourceDocument) {
        disconnect(m_sourceDocument, nullptr, this, nullptr);
    }

    m_sourceDocument = doc;

    if (m_sourceDocument) {
        // 當 CAD 文件有變動時，通知所有視圖重建
        connect(m_sourceDocument, &cad::Document::treeStructureChanged,
                this, &DrawingSheet::rebuildAllViews);

        // Feature 即將被刪除：清掉視圖對它的原始指標，避免懸空指標
        connect(m_sourceDocument, &cad::Document::featureAboutToBeRemoved,
                this, [this](cad::Feature* f) {
                    for (DrawingView* v : m_views)
                        if (v->sourceFeature() == f)
                            v->setSourceFeatureSilently(nullptr);
                });

        // 先 load() 再綁定文件的情況（DSH OPEN）：補解析 sourceFeatureId
        resolveViewSources();
    }
}

void DrawingSheet::applyConfig(const SheetConfig& config)
{
    m_config = config;
    setModified(true);
    Q_EMIT configChanged();
}

void DrawingSheet::setTitleBlock(const TitleBlock& tb)
{
    m_titleBlock = tb;
    setModified(true);
    Q_EMIT titleBlockChanged();
}

DrawingView* DrawingSheet::addView(ViewType type,
                                   const QPointF& position,
                                   double scale)
{
    auto* view = new DrawingView(this);
    view->setType(type);
    view->setPosition(position);
    view->setScale(scale > 0.0 ? scale : m_config.defaultScale);

    // 設定預設標籤
    static const QMap<ViewType, QString> defaultLabels = {
                                                          { ViewType::Front,     "FRONT VIEW" },
                                                          { ViewType::Top,       "TOP VIEW"   },
                                                          { ViewType::Right,     "RIGHT VIEW" },
                                                          { ViewType::Left,      "LEFT VIEW"  },
                                                          { ViewType::Bottom,    "BOTTOM VIEW"},
                                                          { ViewType::Back,      "REAR VIEW"  },
                                                          { ViewType::Isometric, "ISO VIEW"   },
                                                          { ViewType::Section,   "SECTION"    },
                                                          { ViewType::Detail,    "DETAIL"     },
                                                          { ViewType::Auxiliary, "AUX VIEW"   },
                                                          };
    view->setLabel(defaultLabels.value(type, "VIEW"));

    // 若有關聯的 CAD 文件，設定模型來源
    if (m_sourceDocument) {
        // 預設來源：文件中「最後一個」含有效形體的特徵（通常為最終實體）。
        // 使用者之後可在視圖屬性中改選其他特徵。
        cad::Feature* src = nullptr;
        const QList<cad::Feature*> feats = m_sourceDocument->features();
        for (int i = feats.size() - 1; i >= 0; --i) {
            if (feats[i] && !feats[i]->shape().IsNull()) { src = feats[i]; break; }
        }
        if (src)
            view->setSourceFeature(src);
        else
            qDebug() << "[DrawingSheet] addView: no feature with a valid shape in document";
    }

    // 監聽視圖重建請求
    connect(view, &DrawingView::rebuildRequested,
            this, [this, view]() {
                // TODO：觸發 HLR 重新計算
                Q_UNUSED(view)
            });

    m_views.append(view);
    setModified(true);

    qDebug() << "[DrawingSheet] View added:" << view->label()
             << "at" << position << "scale" << view->scale();

    Q_EMIT viewAdded(view);
    return view;
}

void DrawingSheet::removeView(const QUuid& viewId)
{
    for (int i = 0; i < m_views.size(); ++i) {
        if (m_views[i]->id() == viewId) {
            DrawingView* view = m_views.takeAt(i);
            Q_EMIT viewRemoved(viewId);
            view->deleteLater();
            setModified(true);
            return;
        }
    }
    qWarning() << "[DrawingSheet] removeView: id not found" << viewId;
}

DrawingView* DrawingSheet::findView(const QUuid& viewId) const
{
    for (DrawingView* v : m_views) {
        if (v->id() == viewId) return v;
    }
    return nullptr;
}

void DrawingSheet::enablePartsList(bool enable)
{
    if (m_partsListEnabled == enable) return;
    m_partsListEnabled = enable;
    if (enable && m_sourceDocument && m_partsList->config().autoFromDocument)
        m_partsList->populateFromDocument(m_sourceDocument);
    setModified(true);
    Q_EMIT partsListChanged();
}

void DrawingSheet::enableDrawingIndex(bool enable)
{
    if (m_drawingIndexEnabled == enable) return;
    m_drawingIndexEnabled = enable;
    setModified(true);
}

void DrawingSheet::enableRevisionTable(bool enable)
{
    if (m_revisionTableEnabled == enable) return;
    m_revisionTableEnabled = enable;
    setModified(true);
    Q_EMIT revisionTableChanged();
}

void DrawingSheet::enableLegend(bool enable)
{
    if (m_legendEnabled == enable) return;
    m_legendEnabled = enable;
    setModified(true);
}

NoteBlock* DrawingSheet::addNoteBlock(const QString& title)
{
    NoteBlock nb;
    nb.id    = QUuid::createUuid();
    nb.title = title;
    nb.position = { 20, 20 };
    nb.size     = { 80, 50 };
    m_noteBlocks.append(nb);
    setModified(true);
    return &m_noteBlocks.last();
}

void DrawingSheet::removeNoteBlock(const QUuid& id)
{
    for (int i = 0; i < m_noteBlocks.size(); ++i) {
        if (m_noteBlocks[i].id == id) {
            m_noteBlocks.removeAt(i);
            setModified(true);
            return;
        }
    }
}

// ── 檔案 I/O ──────────────────────────────────────────────────────────────────

QJsonObject DrawingSheet::toJson() const
{
    QJsonObject root;
    root["version"]  = "1.0";
    root["id"]       = m_id.toString();
    root["config"]   = m_config.toJson();
    root["titleBlock"] = m_titleBlock.toJson();

    // Views
    QJsonArray viewsArr;
    for (DrawingView* v : m_views)
        viewsArr.append(v->toJson());
    root["views"] = viewsArr;

    // Optional tables
    root["partsListEnabled"]    = m_partsListEnabled;
    root["drawingIndexEnabled"] = m_drawingIndexEnabled;
    root["revisionTableEnabled"]= m_revisionTableEnabled;
    root["legendEnabled"]       = m_legendEnabled;

    if (m_partsListEnabled)
        root["partsList"]    = m_partsList->toJson();
    if (m_drawingIndexEnabled)
        root["drawingIndex"] = m_drawingIndex->toJson();
    if (m_revisionTableEnabled)
        root["revisionTable"]= m_revisionTable->toJson();
    if (m_legendEnabled)
        root["legend"]       = m_legend->toJson();

    // Note blocks
    QJsonArray notesArr;
    for (const NoteBlock& nb : m_noteBlocks)
        notesArr.append(nb.toJson());
    root["noteBlocks"] = notesArr;

    if (!m_bookId.isEmpty()) root["bookId"] = m_bookId;
    if (m_isCover)           root["isCover"] = true;
    if (m_planProfile.enabled || m_planProfile.isIndex || m_planProfile.tableKind > 0)
        root["planProfile"] = m_planProfile.toJson();

    return root;
}

bool DrawingSheet::fromJson(const QJsonObject& root)
{
    m_id = QUuid::fromString(root["id"].toString());

    if (root.contains("config"))
        m_config.fromJson(root["config"].toObject());

    if (root.contains("titleBlock"))
        m_titleBlock.fromJson(root["titleBlock"].toObject());

    // Views
    qDeleteAll(m_views);
    m_views.clear();
    for (const QJsonValue& v : root["views"].toArray()) {
        auto* view = new DrawingView(this);
        view->fromJson(v.toObject());
        m_views.append(view);
    }

    // Optional tables
    m_partsListEnabled     = root.value("partsListEnabled").toBool();
    m_drawingIndexEnabled  = root.value("drawingIndexEnabled").toBool();
    m_revisionTableEnabled = root.value("revisionTableEnabled").toBool();
    m_legendEnabled        = root.value("legendEnabled").toBool();

    if (root.contains("partsList"))
        m_partsList->fromJson(root["partsList"].toObject());
    if (root.contains("drawingIndex"))
        m_drawingIndex->fromJson(root["drawingIndex"].toObject());
    if (root.contains("revisionTable"))
        m_revisionTable->fromJson(root["revisionTable"].toObject());
    if (root.contains("legend"))
        m_legend->fromJson(root["legend"].toObject());

    // Note blocks
    m_noteBlocks.clear();
    for (const QJsonValue& v : root["noteBlocks"].toArray()) {
        NoteBlock nb;
        nb.fromJson(v.toObject());
        m_noteBlocks.append(nb);
    }

    m_bookId  = root["bookId"].toString();
    m_isCover = root["isCover"].toBool(false);

    // 平縱面圖層
    m_planProfile = PlanProfileLayer();
    if (root.contains("planProfile"))
        m_planProfile.fromJson(root["planProfile"].toObject());

    // 解析各視圖的來源特徵（以 sourceFeatureId 對應 Document 內的 Feature）
    resolveViewSources();

    return true;
}

void DrawingSheet::resolveViewSources()
{
    if (!m_sourceDocument) return;
    for (DrawingView* v : m_views) {
        const QString fid = v->takePendingSourceFeatureId();
        if (fid.isEmpty()) continue;
        cad::Feature* f = m_sourceDocument->findFeature(fid);
        if (f)
            v->setSourceFeatureSilently(f);
        else
            qWarning() << "[DrawingSheet] view" << v->label()
                       << "source feature not found:" << fid;
    }
}

bool DrawingSheet::save(const QString& fileName)
{
    const QString savePath = fileName.isEmpty() ? m_fileName : fileName;
    if (savePath.isEmpty()) {
        qWarning() << "[DrawingSheet] save: no file name";
        return false;
    }

    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[DrawingSheet] Cannot open file for writing:" << savePath;
        return false;
    }
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    file.close();

    setFileName(savePath);
    setModified(false);

    qDebug() << "[DrawingSheet] Saved:" << savePath;
    return true;
}

bool DrawingSheet::load(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[DrawingSheet] Cannot open file for reading:" << fileName;
        return false;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (doc.isNull()) {
        qWarning() << "[DrawingSheet] JSON parse error:" << err.errorString();
        return false;
    }

    if (!fromJson(doc.object()))
        return false;

    setFileName(fileName);
    setModified(false);

    qDebug() << "[DrawingSheet] Loaded:" << fileName
             << "| views:" << m_views.size();
    return true;
}

// 匯出時 DrawingSheetPainter 的字型以「1pt = 96dpi 下的 1.33 單位」換算，
// 與螢幕預覽一致，因此 PDF/SVG 一律以 96 dpi 為座標單位（向量輸出不受解析度影響）。
static constexpr int    kExportDpi = 96;
static constexpr double kPxPerMM   = kExportDpi / 25.4;

static QString defaultExportPath(const DrawingSheet& s, const QString& ext)
{
    if (!s.fileName().isEmpty()) {
        QFileInfo fi(s.fileName());
        return fi.absolutePath() + "/" + fi.completeBaseName() + ext;
    }
    QString base = s.titleBlock().drawingNumber.isEmpty()
                       ? QStringLiteral("drawing") : s.titleBlock().drawingNumber;
    return QDir::currentPath() + "/" + base + ext;
}

bool DrawingSheet::exportPdf(const QString& pdfPath)
{
    const QString path = pdfPath.isEmpty() ? defaultExportPath(*this, ".pdf") : pdfPath;

    // 1. 同步渲染所有視圖（含自動 Fit，與預覽一致）
    const QMap<QUuid, ViewRenderResult> results = DrawingViewRenderer::renderSheetSync(*this);

    // 2. PDF writer：頁面 = 圖紙實際尺寸，無邊界
    const QSizeF mm = m_config.paperSizeMM();
    QPdfWriter writer(path);
    writer.setResolution(kExportDpi);
    writer.setTitle(m_titleBlock.drawingTitle);
    writer.setCreator(QStringLiteral("AICAD"));
    const bool landscape = mm.width() > mm.height();
    QPageSize pageSize(QSizeF(qMin(mm.width(), mm.height()), qMax(mm.width(), mm.height())),
                       QPageSize::Millimeter, QString(), QPageSize::ExactMatch);
    writer.setPageLayout(QPageLayout(pageSize,
                                     landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                                     QMarginsF(0, 0, 0, 0)));

    QPainter painter(&writer);
    if (!painter.isActive()) {
        qWarning() << "[DrawingSheet] exportPdf: cannot open" << path;
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(kPxPerMM, kPxPerMM);

    DrawingSheetPainter().paintSheet(painter, *this, results);
    painter.end();

    qDebug() << "[DrawingSheet] PDF exported:" << path << "views rendered:" << results.size();
    return true;
}

bool DrawingSheet::exportPdfAll(const QVector<DrawingSheet*>& sheets, const QString& pdfPath)
{
    if (sheets.isEmpty() || pdfPath.isEmpty()) return false;

    auto layoutFor = [](const QSizeF& mm) {
        const bool landscape = mm.width() > mm.height();
        QPageSize ps(QSizeF(qMin(mm.width(), mm.height()), qMax(mm.width(), mm.height())),
                     QPageSize::Millimeter, QString(), QPageSize::ExactMatch);
        return QPageLayout(ps, landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                           QMarginsF(0, 0, 0, 0));
    };

    QPdfWriter writer(pdfPath);
    writer.setResolution(kExportDpi);
    writer.setCreator(QStringLiteral("AICAD"));
    writer.setPageLayout(layoutFor(sheets.first()->config().paperSizeMM()));

    QPainter painter(&writer);
    if (!painter.isActive()) {
        qWarning() << "[DrawingSheet] exportPdfAll: cannot open" << pdfPath;
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    for (int i = 0; i < sheets.size(); ++i) {
        DrawingSheet* sh = sheets[i];
        if (i > 0) {
            writer.setPageLayout(layoutFor(sh->config().paperSizeMM()));
            writer.newPage();
        }
        const QMap<QUuid, ViewRenderResult> results = DrawingViewRenderer::renderSheetSync(*sh);
        painter.save();
        painter.scale(kPxPerMM, kPxPerMM);
        DrawingSheetPainter().paintSheet(painter, *sh, results);
        painter.restore();
    }
    painter.end();
    qDebug() << "[DrawingSheet] multi-page PDF exported:" << pdfPath << sheets.size() << "pages";
    return true;
}

bool DrawingSheet::exportSvg(const QString& svgPath)
{
#ifndef AICAD_HAVE_QTSVG
    Q_UNUSED(svgPath)
    qWarning() << "[DrawingSheet] exportSvg: built without QtSvg "
                  "(install libqt5svg5-dev and re-run qmake to enable)";
    return false;
#else
    const QString path = svgPath.isEmpty() ? defaultExportPath(*this, ".svg") : svgPath;

    const QMap<QUuid, ViewRenderResult> results = DrawingViewRenderer::renderSheetSync(*this);

    const QSizeF mm = m_config.paperSizeMM();
    const QSize px(qRound(mm.width() * kPxPerMM), qRound(mm.height() * kPxPerMM));

    QSvgGenerator gen;
    gen.setFileName(path);
    gen.setResolution(kExportDpi);
    gen.setSize(px);
    gen.setViewBox(QRect(QPoint(0, 0), px));
    gen.setTitle(m_titleBlock.drawingTitle);
    gen.setDescription(QStringLiteral("AICAD drawing sheet"));

    QPainter painter(&gen);
    if (!painter.isActive()) {
        qWarning() << "[DrawingSheet] exportSvg: cannot open" << path;
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(kPxPerMM, kPxPerMM);

    DrawingSheetPainter().paintSheet(painter, *this, results);
    painter.end();

    qDebug() << "[DrawingSheet] SVG exported:" << path << "views rendered:" << results.size();
    return true;
#endif
}

void DrawingSheet::autoLayout()
{
    // 可用繪圖區：內框（距紙邊 10mm）內縮 5mm；右側為直式標題欄（寬 60mm），
    // 因此整個右側預留 60mm 以避免與標題欄重疊。
    const QSizeF paper = m_config.paperSizeMM();
    constexpr double kMargin = 15.0, kTitleW = 60.0, kGap = 25.0;   // kGap：視圖間距（含標籤）
    const QRectF area(kMargin, kMargin,
                      paper.width()  - 2 * kMargin - kTitleW,
                      paper.height() - 2 * kMargin);
    if (m_views.isEmpty() || area.width() <= 0 || area.height() <= 0) return;

    const bool firstAngle = (m_config.projectionMethod == ProjectionMethod::FirstAngle);

    auto find = [this](ViewType t) -> DrawingView* {
        for (DrawingView* v : m_views) if (v->type() == t) return v;
        return nullptr;
    };

    DrawingView* front = find(ViewType::Front);
    // 沒有前視圖時以第一個視圖當作基準
    DrawingView* base = front ? front : m_views.first();

    // ── 1. 正投影群組（相對 base 中心，座標待平移）────────────────────────
    QHash<DrawingView*, QPointF> pos;
    pos[base] = QPointF(0, 0);
    const double bw = base->size().width(), bh = base->size().height();

    auto place = [&](DrawingView* v, ViewType t, double dx, double dy) {
        Q_UNUSED(t)
        if (!v || v == base || pos.contains(v)) return;
        pos[v] = QPointF(dx, dy);
    };

    DrawingView* top    = find(ViewType::Top);
    DrawingView* bottom = find(ViewType::Bottom);
    DrawingView* right  = find(ViewType::Right);
    DrawingView* left   = find(ViewType::Left);
    DrawingView* back   = find(ViewType::Back);

    // 第三角：上視圖在前視圖上方、右視圖在右方；第一角：上下、左右互換
    const double sTop = firstAngle ? +1 : -1;     // Y 向下為正
    const double sRgt = firstAngle ? -1 : +1;
    if (top)    place(top,    ViewType::Top,    0, sTop * (bh / 2 + kGap + top->size().height() / 2));
    if (bottom) place(bottom, ViewType::Bottom, 0, -sTop * (bh / 2 + kGap + bottom->size().height() / 2));
    if (right)  place(right,  ViewType::Right,  sRgt * (bw / 2 + kGap + right->size().width() / 2), 0);
    if (left)   place(left,   ViewType::Left,   -sRgt * (bw / 2 + kGap + left->size().width() / 2), 0);
    if (back) {
        // 後視圖放在最外側的水平鄰居旁
        DrawingView* outer = (sRgt > 0) ? (left ? left : base) : (right ? right : base);
        const double dir = (sRgt > 0) ? -1 : +1;
        const double ox = pos.value(outer).x();
        const double ow = outer->size().width();
        place(back, ViewType::Back, ox + dir * (ow / 2 + kGap + back->size().width() / 2), 0);
    }

    // ── 2. 其餘視圖（等角、剖面、局部放大…）放右側欄位 ─────────────────────
    QVector<DrawingView*> extras;
    for (DrawingView* v : m_views)
        if (!pos.contains(v)) extras.append(v);

    double extrasW = 0;
    for (DrawingView* v : extras) extrasW = qMax(extrasW, v->size().width());

    // 正投影群組外框
    QRectF group;
    for (auto it = pos.cbegin(); it != pos.cend(); ++it) {
        const QSizeF s = it.key()->size();
        group = group.united(QRectF(it.value() - QPointF(s.width() / 2, s.height() / 2), s));
    }

    // 正投影群組置中於「扣掉右側欄」後的區域
    const double groupAreaW = area.width() - (extras.isEmpty() ? 0 : extrasW + kGap);
    const QPointF shift(area.left() + groupAreaW / 2 - group.center().x(),
                        area.top() + area.height() / 2 - group.center().y());
    for (auto it = pos.cbegin(); it != pos.cend(); ++it)
        it.key()->setPosition(it.value() + shift);

    // 右側欄由上往下堆疊；放不下則往左換欄
    double colRight = area.right();
    double y = area.top();
    double colW = 0;
    for (DrawingView* v : extras) {
        const QSizeF s = v->size();
        if (y + s.height() > area.bottom() && y > area.top()) {
            colRight -= colW + kGap;
            y = area.top();
            colW = 0;
        }
        v->setPosition(QPointF(colRight - s.width() / 2, y + s.height() / 2));
        y += s.height() + kGap;
        colW = qMax(colW, s.width());
    }

    setModified(true);
    Q_EMIT viewsLayoutChanged();
    qDebug() << "[DrawingSheet] autoLayout done:" << m_views.size() << "views,"
             << (firstAngle ? "first" : "third") << "angle";
}

bool DrawingSheet::hasOverlap() const
{
    for (int i = 0; i < m_views.size(); ++i) {
        for (int j = i + 1; j < m_views.size(); ++j) {
            if (m_views[i]->boundingRect().intersects(m_views[j]->boundingRect()))
                return true;
        }
    }
    return false;
}

} // namespace drawing
} // namespace aicad
