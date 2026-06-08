/**
 * @file DrawingSheetManager.cpp
 * @brief 圖紙文件管理器實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "DrawingSheetManager.h"
#include "cad/Document.h"

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
    m_sourceDocument = doc;
    for (DrawingSheet* sheet : m_sheets)
        sheet->setSourceDocument(doc);
}

DrawingSheet* DrawingSheetManager::newSheet(const QString& title)
{
    auto* sheet = new DrawingSheet(this);
    sheet->applyConfig(m_defaultConfig);

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
    QString tempTitle = src->titleBlock().drawingTitle + " (Copy)";
    DrawingSheet* copy = newSheet(tempTitle);
    copy->applyConfig(src->config());

    TitleBlock tb = src->titleBlock();
    tb.drawingTitle = tempTitle;
    copy->setTitleBlock(tb);

    // TODO：深複製視圖、表格等

    qDebug() << "[DrawingSheetManager] Duplicated sheet:" << sheetId;
    return copy;
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
