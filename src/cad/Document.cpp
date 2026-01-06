#include "Document.h"
#include "cad/features/Feature.h"

#include <QDebug>

namespace aicad::cad {

Document::Document(QObject* parent)
    : QObject(parent)
{
}

// ===== core::DocumentManager needs =====

QString Document::fileName() const
{
    return m_fileName;
}

void Document::setFileName(const QString& name)
{
    m_fileName = name;
}

bool Document::isModified() const
{
    return m_modified;
}

void Document::setModified(bool modified)
{
    m_modified = modified;
}

bool Document::save(const QString& filePath)
{
    qDebug() << "[CadDocument] Saving to:" << filePath;
    m_modified = false;
    return true;
}

bool Document::load(const QString& filePath)
{
    qDebug() << "[CadDocument] Loading from:" << filePath;
    m_modified = false;
    return true;
}

// ===== CAD Engine =====

void Document::addFeature(const std::shared_ptr<Feature>& feature)
{
    m_features.append(feature);
    recompute();
    setModified(true);
}

void Document::recompute()
{
    TopoDS_Shape current;

    for (const auto& feature : m_features)
    {
        current = feature->build(current);
    }

    m_result = current;
}

const TopoDS_Shape& Document::shape() const
{
    return m_result;
}

} // namespace aicad::cad
