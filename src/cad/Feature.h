/**
 * @file Feature.h
 * @brief CAD 特徵基礎類別
 * @author AICAD Team
 * @date 2025-01-08
 */

#ifndef AICAD_CAD_FEATURE_H
#define AICAD_CAD_FEATURE_H

#include <QObject>
#include <QString>
#include <QUuid>
#include <QJsonObject>
#include <QList>
#include <TopoDS_Shape.hxx>
#include <TDF_Label.hxx>

namespace aicad {
namespace cad {

class Document;

/**
 * @brief 特徵類型列舉
 */
enum class FeatureType {
    Base,           ///< 基礎特徵（抽象）
    Sketch,         ///< 草圖特徵
    Extrude,        ///< 擠出特徵
    Revolve,        ///< 旋轉特徵
    Fillet,         ///< 圓角特徵
    Chamfer,        ///< 倒角特徵
    Boolean,        ///< 布林運算
    Pattern         ///< 陣列特徵
};

/**
 * @brief 特徵基礎類別（抽象）
 *
 * 所有 CAD 特徵的基礎類別，提供：
 * - 唯一識別 ID
 * - 名稱管理
 * - 幾何形狀
 * - OCAF 標籤綁定
 * - 父子關係
 * - 可見性控制
 * - 錯誤狀態
 *
 * 使用範例:
 * @code
 * class MyFeature : public Feature {
 * public:
 *     MyFeature(Document* doc) : Feature(doc) {}
 *
 *     FeatureType type() const override {
 *         return FeatureType::Custom;
 *     }
 *
 *     bool rebuild() override {
 *         // 重建幾何
 *         m_shape = ...; // 建立 TopoDS_Shape
 *         return true;
 *     }
 * };
 * @endcode
 */
class Feature : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibilityChanged)
    Q_PROPERTY(bool suppressed READ isSuppressed WRITE setSuppressed NOTIFY suppressedChanged)

public:
    /**
     * @brief 建構子
     * @param parent 父文件
     */
    explicit Feature(Document* parent = nullptr);

    /**
     * @brief 解構子
     */
    virtual ~Feature();

    /**
     * @brief 取得特徵類型（子類別必須實作）
     */
    virtual FeatureType type() const = 0;

    /**
     * @brief 取得特徵類型的字串表示
     */
    virtual QString typeString() const;

    /**
     * @brief 重建特徵幾何（子類別必須實作）
     * @return 成功回傳 true
     */
    virtual bool rebuild() = 0;

    // 基本屬性
    QString id() const { return m_id.toString(); }
    QString name() const { return m_name; }
    void setName(const QString& name);

    // 幾何形狀
    TopoDS_Shape shape() const { return m_shape; }
    void setShape(const TopoDS_Shape& shape);
    bool hasValidShape() const { return !m_shape.IsNull(); }

    // OCAF 綁定
    TDF_Label label() const { return m_label; }
    void setLabel(const TDF_Label& label) { m_label = label; }

    // 文件關聯
    Document* document() const { return m_document; }

    // 可見性控制
    bool isVisible() const { return m_visible; }
    void setVisible(bool visible);

    // 抑制狀態（不參與計算）
    bool isSuppressed() const { return m_suppressed; }
    void setSuppressed(bool suppressed);

    // 錯誤狀態
    bool hasError() const { return m_hasError; }
    QString errorMessage() const { return m_errorMessage; }
    void setError(const QString& message);
    void clearError();

    // 父子關係
    Feature* parent() const { return m_parent; }
    void setParent(Feature* parent);

    QList<Feature*> children() const { return m_children; }
    void addChild(Feature* child);
    void removeChild(Feature* child);

    /**
     * @brief 取得特徵在文件中的索引
     */
    int index() const;

    /**
     * @brief 序列化為 JSON
     */
    virtual QJsonObject toJson() const;

    /**
     * @brief 從 JSON 反序列化
     */
    virtual bool fromJson(const QJsonObject& json);

Q_SIGNALS:
    /**
     * @brief 名稱改變時發出
     */
    void nameChanged(const QString& name);

    /**
     * @brief 幾何形狀改變時發出
     */
    void shapeChanged();

    /**
     * @brief 可見性改變時發出
     */
    void visibilityChanged(bool visible);

    /**
     * @brief 抑制狀態改變時發出
     */
    void suppressedChanged(bool suppressed);

    /**
     * @brief 錯誤狀態改變時發出
     */
    void errorChanged(bool hasError, const QString& message);

    /**
     * @brief 特徵需要重建時發出
     */
    void rebuildRequested();

protected:
    /**
     * @brief 設定錯誤狀態（給子類別使用）
     */
    void setErrorState(bool hasError, const QString& message = QString());

private:
    QUuid m_id;                    ///< 唯一識別碼
    QString m_name;                ///< 特徵名稱
    TopoDS_Shape m_shape;          ///< 幾何形狀
    TDF_Label m_label;             ///< OCAF 標籤
    Document* m_document;          ///< 所屬文件

    bool m_visible;                ///< 可見性
    bool m_suppressed;             ///< 抑制狀態

    bool m_hasError;               ///< 錯誤狀態
    QString m_errorMessage;        ///< 錯誤訊息

    Feature* m_parent;             ///< 父特徵
    QList<Feature*> m_children;    ///< 子特徵列表

    Q_DISABLE_COPY(Feature)
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_FEATURE_H
