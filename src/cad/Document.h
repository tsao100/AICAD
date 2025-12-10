/**
 * @file Document.h (樁檔案)
 * @brief Document 樁實作 (供 Felicia 編譯使用，等待 Ben 完成)
 * @author Felicia (臨時)
 * @date 2024-12-04
 */

#ifndef AICAD_CAD_DOCUMENT_H
#define AICAD_CAD_DOCUMENT_H

#include <QObject>
#include <QString>
#include <QVector>

// 前向宣告
class TDF_Label;
class TopoDS_Shape;

namespace aicad {
namespace cad {

struct CustomPlane;

/**
 * @brief 文件類別樁實作
 * 
 * TODO(Ben): 這是臨時樁實作，等待 Ben 完成 CAD Engine
 */
class Document : public QObject {
    Q_OBJECT
    
public:
    explicit Document(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~Document() {}
    
    // 樁方法
    QString fileName() const { return "Untitled"; }
    bool isModified() const { return false; }
    bool save(const QString& /*path*/) { return true; }
    bool load(const QString& /*path*/) { return true; }
    
    // TODO: 等待 Ben 實作
    // QVector<TDF_Label> getFeatures() const;
    // CustomPlane getSketchPlane(TDF_Label label) const;
    // void addFeature(...);
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_DOCUMENT_H