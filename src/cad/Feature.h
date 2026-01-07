/**
 * @file Feature.h
 * @brief 特徵基礎類別，所有 CAD 特徵的父類別
 * @author Ben
 * @date 2025-01-06
 */

#ifndef AICAD_CAD_FEATURE_H
#define AICAD_CAD_FEATURE_H

#include <QObject>
#include <QString>
#include <QVariant>

// OCCT includes
#include <TDF_Label.hxx>
#include <TopoDS_Shape.hxx>

namespace aicad {
namespace cad {

class Document;

/**
 * @brief 特徵類型枚舉
 */
enum class FeatureType {
    Unknown,
    Sketch,
    Extrude,
    Revolve,
    Fillet,
    Chamfer
};

/**
 * @brief 特徵基礎類別
 * 
 * Feature 是所有 CAD 特徵的基礎類別:
 * - 提供通用特徵介面
 * - 管理特徵屬性
 * - 處理特徵重建
 * - 整合 OCCT 標籤系統
 * 
 * 使用範例:
 * @code
 * Feature* feature = doc->findFeature(123);
 * qDebug() << feature->name() << feature->typeName();
 * feature->setVisible(false);
 * feature->rebuild();
 * @endcode
 */
class Feature : public QObject {
    Q_OBJECT
    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(FeatureType type READ type CONSTANT)
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(bool valid READ isValid NOTIFY validChanged)
    
public:
    /**
     * @brief 建構子
     * @param doc 父文件
     * @param label OCCT 標籤
     */
    explicit Feature(Document* doc, TDF_Label label);
    
    /**
     * @brief 解構子
     */
    ~Feature() override;
    
    /**
     * @brief 取得特徵 ID
     */
    int id() const;
    
    /**
     * @brief 取得特徵名稱
     */
    QString name() const;
    
    /**
     * @brief 設定特徵名稱
     */
    void setName(const QString& name);
    
    /**
     * @brief 取得特徵類型
     */
    virtual FeatureType type() const = 0;
    
    /**
     * @brief 取得特徵類型名稱
     */
    QString typeName() const;
    
    /**
     * @brief 檢查特徵是否可見
     */
    bool isVisible() const;
    
    /**
     * @brief 設定特徵可見性
     */
    void setVisible(bool visible);
    
    /**
     * @brief 檢查特徵是否有效
     */
    bool isValid() const;
    
    /**
     * @brief 取得父文件
     */
    Document* document() const;
    
    /**
     * @brief 取得 OCCT 標籤
     */
    TDF_Label label() const;
    
    /**
     * @brief 取得特徵形狀
     */
    TopoDS_Shape shape() const;
    
    /**
     * @brief 重建特徵
     * @return 成功回傳 true
     */
    virtual bool rebuild() = 0;
    
    /**
     * @brief 取得特徵屬性
     * @param name 屬性名稱
     * @return 屬性值
     */
    QVariant property(const QString& name) const;
    
    /**
     * @brief 設定特徵屬性
     * @param name 屬性名稱
     * @param value 屬性值
     */
    void setProperty(const QString& name, const QVariant& value);
    
    /**
     * @brief 取得所有屬性名稱
     */
    QStringList propertyNames() const;
    
Q_SIGNALS:
    /**
     * @brief 名稱改變時發出
     */
    void nameChanged(const QString& name);
    
    /**
     * @brief 可見性改變時發出
     */
    void visibleChanged(bool visible);
    
    /**
     * @brief 有效性改變時發出
     */
    void validChanged(bool valid);
    
    /**
     * @brief 特徵被修改時發出
     */
    void modified();
    
protected:
    /**
     * @brief 設定特徵形狀
     */
    void setShape(const TopoDS_Shape& shape);
    
    /**
     * @brief 標記特徵為無效
     */
    void setValid(bool valid);
    
    /**
     * @brief 通知特徵已修改
     */
    void notifyModified();
    
private:
    class Private;
    Private* d;
};

/**
 * @brief 將特徵類型轉換為字串
 */
QString featureTypeToString(FeatureType type);

/**
 * @brief 將字串轉換為特徵類型
 */
FeatureType stringToFeatureType(const QString& str);

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_FEATURE_H