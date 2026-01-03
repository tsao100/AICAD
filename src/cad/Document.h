#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <memory>

#include <TopoDS_Shape.hxx>

namespace aicad::cad {

class Feature;

/**
 * @brief CAD Document
 *        Real implementation used by core::DocumentManager
 */
class Document : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Constructor
     * @param parent QObject parent (usually DocumentManager)
     */
    explicit Document(QObject* parent = nullptr);

    // ===== Interface required by core::DocumentManager =====
    QString fileName() const;
    void setFileName(const QString& name);

    bool isModified() const;
    void setModified(bool modified);

    bool save(const QString& filePath);
    bool load(const QString& filePath);

    // ===== CAD Engine API =====
    void addFeature(const std::shared_ptr<Feature>& feature);
    void recompute();

    const TopoDS_Shape& shape() const;

private:
    QString m_fileName;
    bool m_modified = false;

    QList<std::shared_ptr<Feature>> m_features;
    TopoDS_Shape m_result;
};

} // namespace aicad::cad
