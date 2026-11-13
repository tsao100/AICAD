/**
 * @file DrawingSheetManager.cpp
 * @brief 圖紙文件管理器實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "DrawingSheetManager.h"
#include "PlanProfileSettings.h"
#include "cad/Document.h"
#include "railway/RailwayAlignment.h"
#include "PlanProfileGenerator.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

namespace aicad {
namespace drawing {

DrawingSheetManager::DrawingSheetManager(QObject* parent)
    : QObject(parent)
{
    // 圖紙有任何修改 → 文件標記為已修改（只在建構時連一次；lambda 不可搭配 UniqueConnection）
    connect(this, &DrawingSheetManager::anySheetModified, this, [this]() {
        if (m_sourceDocument && !m_refreshing) m_sourceDocument->setModified(true);
    });

    // 同步到 Document（讓 Feature Browser 與存檔即時反映）；合併短時間內的多次變更
    m_syncTimer = new QTimer(this);
    m_syncTimer->setSingleShot(true);
    m_syncTimer->setInterval(150);
    connect(m_syncTimer, &QTimer::timeout, this, &DrawingSheetManager::syncToDocument);
    connect(this, &DrawingSheetManager::anySheetModified, m_syncTimer, QOverload<>::of(&QTimer::start));
    connect(this, &DrawingSheetManager::sheetAdded,   m_syncTimer, [this]() { m_syncTimer->start(); });
    connect(this, &DrawingSheetManager::sheetRemoved, m_syncTimer, [this]() { m_syncTimer->start(); });

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(500);
    connect(m_refreshTimer, &QTimer::timeout, this, &DrawingSheetManager::refreshBooks);
    qDebug() << "[DrawingSheetManager] Created";
}

DrawingSheetManager::~DrawingSheetManager()
{
    qDeleteAll(m_sheets);
    m_sheets.clear();
    qDebug() << "[DrawingSheetManager] Destroyed";
}

void DrawingSheetManager::setSourceDocument(cad::Document* doc)
{
    if (m_sourceDocument)
        disconnect(m_sourceDocument, nullptr, this, nullptr);

    m_sourceDocument = doc;

    if (m_sourceDocument) {
        // 存檔前把最新圖紙內容寫進 Document（隨 .aicad 一併儲存）
        connect(m_sourceDocument, &cad::Document::aboutToSave, this, [this]() {
            if (m_sourceDocument)
                m_sourceDocument->setDrawingSheetsData(toJson());
        });
        // 線形清單／內容／名稱有變更 → 重算相關圖冊
        connect(m_sourceDocument, &cad::Document::trackCenterLinesChanged,
                this, &DrawingSheetManager::onTclListChanged);
        for (railway::TrackCenterLine* t : m_sourceDocument->trackCenterLines()) {
            connect(t, &railway::TrackCenterLine::dataChanged, this,
                    &DrawingSheetManager::scheduleRefresh, Qt::UniqueConnection);
            connect(t, &railway::TrackCenterLine::nameChanged, this,
                    &DrawingSheetManager::scheduleRefresh, Qt::UniqueConnection);
        }
    }

    for (DrawingSheet* sheet : m_sheets)
        sheet->setSourceDocument(doc);
}

DrawingSheetManager* DrawingSheetManager::forDocument(cad::Document* doc)
{
    if (!doc) return nullptr;
    for (QObject* child : doc->children())
        if (auto* mgr = qobject_cast<DrawingSheetManager*>(child))
            return mgr;
    auto* mgr = new DrawingSheetManager(doc);
    mgr->setSourceDocument(doc);
    mgr->loadFromJson(doc->drawingSheetsData());
    if (!mgr->m_books.isEmpty()) mgr->scheduleRefresh();   // 載入後與線形比對，必要時重算
    return mgr;
}

DrawingBook* DrawingSheetManager::findBook(const QString& id)
{
    for (DrawingBook& b : m_books) if (b.id == id) return &b;
    return nullptr;
}

DrawingBook* DrawingSheetManager::addBook(const QString& name, const QJsonObject& settings, const QString& id)
{
    DrawingBook b;
    b.id = id.isEmpty() ? QUuid::createUuid().toString() : id;
    b.name = name;
    b.settings = settings;
    m_books.append(b);
    touch();
    return &m_books.last();
}

void DrawingSheetManager::removeBook(const QString& id, bool deleteSheets)
{
    for (int i = 0; i < m_books.size(); ++i) {
        if (m_books[i].id != id) continue;
        m_books.removeAt(i);
        QVector<QUuid> kill;
        for (DrawingSheet* s : m_sheets) {
            if (s->bookId() != id) continue;
            if (deleteSheets) kill.append(s->id()); else s->setBookId(QString());
        }
        for (const QUuid& u : kill) removeSheet(u);
        touch();
        return;
    }
}

void DrawingSheetManager::touch()
{
    if (m_sourceDocument && !m_refreshing) m_sourceDocument->setModified(true);
    if (m_syncTimer) m_syncTimer->start();
}

void DrawingSheetManager::syncToDocument()
{
    if (!m_sourceDocument) return;
    m_sourceDocument->setDrawingSheetsData(toJson());
    Q_EMIT m_sourceDocument->treeStructureChanged();
}

void DrawingSheetManager::onTclListChanged()
{
    if (!m_sourceDocument) return;
    for (railway::TrackCenterLine* t : m_sourceDocument->trackCenterLines()) {
        connect(t, &railway::TrackCenterLine::dataChanged, this,
                &DrawingSheetManager::scheduleRefresh, Qt::UniqueConnection);
        connect(t, &railway::TrackCenterLine::nameChanged, this,
                &DrawingSheetManager::scheduleRefresh, Qt::UniqueConnection);
    }
    scheduleRefresh();
}

void DrawingSheetManager::scheduleRefresh()
{
    if (m_books.isEmpty() || m_refreshing) return;
    m_refreshTimer->start();
}

void DrawingSheetManager::refreshBooks()
{
    if (!m_sourceDocument || m_books.isEmpty() || m_refreshing) return;
    m_refreshing = true;
    const QJsonArray before = toJson();
    const QUuid activeId = m_activeSheet ? m_activeSheet->id() : QUuid();

    QStringList ids;
    for (const DrawingBook& b : m_books) ids << b.id;
    for (const QString& id : ids) {
        DrawingBook* b = findBook(id);
        if (!b) continue;
        PlanProfileSettings s;
        s.fromJson(b->settings);
        QString err;
        if (!updatePlanProfileBook(this, m_sourceDocument, id, s, &err))
            qWarning() << "[DrawingSheetManager] refresh book" << b->name << "failed:" << err;
    }
    if (DrawingSheet* a = findSheet(activeId)) setActiveSheet(a);
    m_refreshing = false;

    if (toJson() != before) {
        m_sourceDocument->setModified(true);
        syncToDocument();
    }
}

DrawingSheet* DrawingSheetManager::newSheet(const QString& title)
{
    auto* sheet = new DrawingSheet(this);
    {
        // 採用最近一次在 Drawing Sheet／平縱面設定對話框中使用的圖紙規格
        SheetConfig cfg = m_defaultConfig;
        const DrawingPrefs dp = loadDrawingPrefs();
        if (dp.paperSize >= 0)     cfg.paperSize        = static_cast<PaperSize>(dp.paperSize);
        if (dp.orientation >= 0)   cfg.orientation      = static_cast<PaperOrientation>(dp.orientation);
        if (dp.unit >= 0)          cfg.unit             = static_cast<DrawingUnit>(dp.unit);
        if (dp.frameTemplate >= 0) cfg.frameTemplate    = static_cast<FrameTemplate>(dp.frameTemplate);
        if (dp.projection >= 0)    cfg.projectionMethod = static_cast<ProjectionMethod>(dp.projection);
        if (dp.defaultScale > 0)   cfg.defaultScale     = dp.defaultScale;
        if (dp.lwThin > 0)         cfg.lineWidthThin    = dp.lwThin;
        if (dp.lwMedium > 0)       cfg.lineWidthMedium  = dp.lwMedium;
        if (dp.lwThick > 0)        cfg.lineWidthThick   = dp.lwThick;
        sheet->applyConfig(cfg);
    }

    // 套用公司資訊
    if (!m_companyName.isEmpty()) {
        TitleBlock tb = sheet->titleBlock();
        tb.companyName = m_companyName;
        tb.companyLogo = m_companyLogo;
        if (!title.isEmpty())
            tb.drawingTitle = title;
        sheet->setTitleBlock(tb);
    } else if (!title.isEmpty()) {
        TitleBlock tb = sheet->titleBlock();
        tb.drawingTitle = title;
        sheet->setTitleBlock(tb);
    }

    if (m_sourceDocument)
        sheet->setSourceDocument(m_sourceDocument);

    connect(sheet, &DrawingSheet::modifiedChanged,
            this, [this](bool) { Q_EMIT anySheetModified(); });

    m_sheets.append(sheet);
    setActiveSheet(sheet);

    qDebug() << "[DrawingSheetManager] New sheet:" << title;
    Q_EMIT sheetAdded(sheet);
    return sheet;
}

QJsonArray DrawingSheetManager::toJson() const
{
    QJsonArray arr;
    for (const DrawingBook& b : m_books) {
        QJsonObject o;
        o["__book"] = true;
        o["id"] = b.id;
        o["name"] = b.name;
        o["settings"] = b.settings;
        arr.append(o);
    }
    for (DrawingSheet* s : m_sheets) {
        QJsonObject o = s->toJson();
        if (s == m_activeSheet)
            o["active"] = true;
        arr.append(o);
    }
    return arr;
}

void DrawingSheetManager::loadFromJson(const QJsonArray& sheets)
{
    qDeleteAll(m_sheets);
    m_sheets.clear();
    m_books.clear();
    m_activeSheet = nullptr;

    DrawingSheet* active = nullptr;
    for (const QJsonValue& v : sheets) {
        const QJsonObject o = v.toObject();
        if (o.value("__book").toBool()) {
            DrawingBook b;
            b.id = o.value("id").toString();
            b.name = o.value("name").toString();
            b.settings = o.value("settings").toObject();
            m_books.append(b);
            continue;
        }
        auto* sheet = new DrawingSheet(this);
        sheet->setSourceDocument(m_sourceDocument);   // fromJson 需要它還原來源特徵
        if (!sheet->fromJson(o)) {
            delete sheet;
            continue;
        }
        connect(sheet, &DrawingSheet::modifiedChanged,
                this, [this](bool) { Q_EMIT anySheetModified(); });
        m_sheets.append(sheet);
        if (o.value("active").toBool()) active = sheet;
        Q_EMIT sheetAdded(sheet);
    }
    if (!active && !m_sheets.isEmpty()) active = m_sheets.last();
    if (active) setActiveSheet(active);

    qDebug() << "[DrawingSheetManager] Loaded" << m_sheets.size() << "sheet(s) from document";
}

DrawingSheet* DrawingSheetManager::openSheet(const QString& fileName)
{
    // 避免重複載入
    for (DrawingSheet* s : m_sheets) {
        if (s->fileName() == fileName) {
            qDebug() << "[DrawingSheetManager] Already loaded:" << fileName;
            setActiveSheet(s);
            return s;
        }
    }

    auto* sheet = new DrawingSheet(this);
    if (!sheet->load(fileName)) {
        delete sheet;
        qWarning() << "[DrawingSheetManager] Failed to load sheet:" << fileName;
        return nullptr;
    }

    if (m_sourceDocument)
        sheet->setSourceDocument(m_sourceDocument);

    connect(sheet, &DrawingSheet::modifiedChanged,
            this, [this](bool) { Q_EMIT anySheetModified(); });

    m_sheets.append(sheet);
    setActiveSheet(sheet);

    Q_EMIT sheetAdded(sheet);
    return sheet;
}

void DrawingSheetManager::removeSheet(const QUuid& sheetId)
{
    for (int i = 0; i < m_sheets.size(); ++i) {
        if (m_sheets[i]->id() == sheetId) {
            DrawingSheet* sheet = m_sheets.takeAt(i);

            if (m_activeSheet == sheet) {
                m_activeSheet = m_sheets.isEmpty() ? nullptr : m_sheets.last();
                Q_EMIT activeSheetChanged(m_activeSheet);
            }

            Q_EMIT sheetRemoved(sheetId);
            sheet->deleteLater();
            return;
        }
    }
}

DrawingSheet* DrawingSheetManager::duplicateSheet(const QUuid& sheetId)
{
    DrawingSheet* src = findSheet(sheetId);
    if (!src) return nullptr;

    // 序列化再反序列化，產生新的實體
    // 這裡用暫存 JSON 方式複製
    const QString tempTitle = src->titleBlock().drawingTitle + " (Copy)";
    DrawingSheet* copy = newSheet(tempTitle);

    // 深複製：整張圖紙序列化後灌進新圖紙（設定、標題欄、視圖、各表格、說明），
    // 再換新的圖紙 id；視圖的來源特徵由 sourceFeatureId 還原（copy 已綁定同一 Document）。
    QJsonObject j = src->toJson();
    j["id"] = QUuid::createUuid().toString();
    j.remove("active");
    copy->fromJson(j);

    TitleBlock tb = copy->titleBlock();
    tb.drawingTitle = tempTitle;
    copy->setTitleBlock(tb);

    qDebug() << "[DrawingSheetManager] Duplicated sheet:" << sheetId;
    return copy;
}

bool DrawingSheetManager::setSheetOrder(const QVector<DrawingSheet*>& order)
{
    if (order.size() != m_sheets.size()) return false;
    for (DrawingSheet* s : order)
        if (!m_sheets.contains(s)) return false;
    if (order == m_sheets) return true;
    m_sheets = order;
    Q_EMIT anySheetModified();
    return true;
}

DrawingSheet* DrawingSheetManager::findSheet(const QUuid& id) const
{
    for (DrawingSheet* s : m_sheets)
        if (s->id() == id) return s;
    return nullptr;
}

void DrawingSheetManager::setActiveSheet(const QUuid& sheetId)
{
    DrawingSheet* sheet = findSheet(sheetId);
    setActiveSheet(sheet);
}

void DrawingSheetManager::setActiveSheet(DrawingSheet* sheet)
{
    if (m_activeSheet == sheet) return;
    m_activeSheet = sheet;
    qDebug() << "[DrawingSheetManager] Active sheet:"
             << (sheet ? sheet->titleBlock().drawingTitle : "null");
    Q_EMIT activeSheetChanged(sheet);
}

bool DrawingSheetManager::saveAll()
{
    bool ok = true;
    for (DrawingSheet* sheet : m_sheets) {
        if (sheet->isModified()) {
            if (!sheet->save())
                ok = false;
        }
    }
    return ok;
}

bool DrawingSheetManager::saveIndex(const QString& indexFilePath)
{
    QJsonObject root;
    root["version"] = "1.0";

    QJsonArray arr;
    for (const DrawingSheet* sheet : m_sheets) {
        QJsonObject entry;
        entry["id"]       = sheet->id().toString();
        entry["fileName"] = sheet->fileName();
        entry["title"]    = sheet->titleBlock().drawingTitle;
        arr.append(entry);
    }
    root["sheets"] = arr;

    QFile file(indexFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[DrawingSheetManager] Cannot write index:" << indexFilePath;
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    qDebug() << "[DrawingSheetManager] Index saved:" << indexFilePath;
    return true;
}

bool DrawingSheetManager::loadFromIndex(const QString& indexFilePath)
{
    QFile file(indexFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[DrawingSheetManager] Cannot read index:" << indexFilePath;
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isNull()) {
        qWarning() << "[DrawingSheetManager] Invalid index JSON";
        return false;
    }

    QDir baseDir = QFileInfo(indexFilePath).absoluteDir();

    for (const QJsonValue& v : doc.object()["sheets"].toArray()) {
        QJsonObject entry = v.toObject();
        QString relPath   = entry["fileName"].toString();
        QString absPath   = baseDir.absoluteFilePath(relPath);
        openSheet(absPath);
    }

    qDebug() << "[DrawingSheetManager] Loaded from index:" << m_sheets.size() << "sheets";
    return true;
}

void DrawingSheetManager::setDefaultConfig(const SheetConfig& config)
{
    m_defaultConfig = config;
}

void DrawingSheetManager::setCompanyInfo(const QString& name, const QString& logoPath)
{
    m_companyName = name;
    m_companyLogo = logoPath;
}

} // namespace drawing
} // namespace aicad
