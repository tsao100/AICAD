/**
 * @file Feature.h
 * @brief CAD 特徵基礎類別
 * @author Ben
 * @date 2024-12-04
 */

#ifndef AICAD_CAD_FEATURES_FEATURE_H
#define AICAD_CAD_FEATURES_FEATURE_H

#include <QObject>
#include <QString>
#include <QVector>
#include <TopoDS_Shape.hxx>

namespace aicad {
namespace cad {

/**
 * @brief 特徵類型列舉
 */
enum class FeatureType {
    Unknown,
    Sketch,
    Extrude,
    Revolve,
    Fillet,
    Chamfer,
    Boolean
};

/**
 * @brief 所有 CAD 特徵的基礎類別
 * 
 * Feature 代表一個 CAD 建模操作，例如草圖、擠出、倒角等。
 * 每個特徵都可以計算出一個 3D 形狀 (TopoDS_Shape)。
 * 
 * 特徵之間可以有依賴關係，形成特徵樹。
 * 
 * 使用範例:
 * @code
 * Sketch* sketch = new Sketch(CustomPlane::XY());
 * sketch->addPolyline(points);
 * 
 * Extrude* extrude = new Extrude(sketch, 10.0);
 * TopoDS_Shape shape = extrude->compute();
 * @endcode
 */
class Feature : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibilityChanged)
    Q_PROPERTY(bool valid READ isValid NOTIFY validityChanged)
    
public:
    /**
     * @brief 建構子
     * @param parent 父物件
     */
    explicit Feature(QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    virtual ~Feature();
    
    /**
     * @brief 取得特徵類型
     * @return 特徵類型
     */
    virtual FeatureType type() const = 0;
    
    /**
     * @brief 計算特徵的幾何形狀
     * @return 計算結果，失敗則回傳空形狀
     * 
     * 此方法應該在子類別中實作具體的計算邏輯。
     * 計算結果會快取在 m_shape 中。
     */
    virtual TopoDS_Shape compute() = 0;
    
    /**
     * @brief 更新特徵 (重新計算)
     * 
     * 呼叫 compute() 並更新快取的形狀。
     * 若計算失敗，會發出 computeFailed 信號。
     */
    void update();
    
    /**
     * @brief 取得此特徵依賴的其他特徵
     * @return 依賴的特徵列表
     * 
     * 例如：擠出特徵依賴於草圖特徵
     */
    virtual QVector<Feature*> dependencies() const;
    
    /**
     * @brief 取得依賴此特徵的其他特徵
     * @return 依賴者列表
     */
    QVector<Feature*> dependents() const;
    
    /**
     * @brief 新增依賴者
     * @param dependent 依賴此特徵的其他特徵
     */
    void addDependent(Feature* dependent);
    
    /**
     * @brief 移除依賴者
     * @param dependent 要移除的依賴者
     */
    void removeDependent(Feature* dependent);
    
    // === 屬性存取 ===
    
    /**
     * @brief 取得特徵 ID
     */
    int id() const;
    
    /**
     * @brief 設定特徵 ID
     */
    void setId(int id);
    
    /**
     * @brief 取得特徵名稱
     */
    QString name() const;
    
    /**
     * @brief 設定特徵名稱
     */
    void setName(const QString& name);
    
    /**
     * @brief 檢查是否可見
     */
    bool isVisible() const;
    
    /**
     * @brief 設定可見性
     */
    void setVisible(bool visible);
    
    /**
     * @brief 檢查特徵是否有效
     * @return 若形狀有效則回傳 true
     */
    bool isValid() const;
    
    /**
     * @brief 取得計算的形狀
     * @return OCCT 形狀
     */
    TopoDS_Shape shape() const;
    
    /**
     * @brief 檢查是否需要更新
     * @return 若需要重新計算則回傳 true
     */
    bool needsUpdate() const;
    
Q_SIGNALS:
    /**
     * @brief 名稱改變時發出
     */
    void nameChanged(const QString& name);
    
    /**
     * @brief 可見性改變時發出
     */
    void visibilityChanged(bool visible);
    
    /**
     * @brief 特徵更新後發出
     */
    void updated();
    
    /**
     * @brief 計算失敗時發出
     */
    void computeFailed(const QString& error);
    
    /**
     * @brief 有效性改變時發出
     */
    void validityChanged(bool valid);
    
protected:
    /**
     * @brief 設定計算結果
     * @param shape 計算出的形狀
     */
    void setShape(const TopoDS_Shape& shape);
    
    /**
     * @brief 標記為無效 (需要重新計算)
     */
    void invalidate();
    
    /**
     * @brief 檢查是否已標記為無效
     */
    bool isInvalidated() const;
    
private:
    class Private;
    Private* d;
};

/**
 * @brief 特徵類型轉字串
 */
QString featureTypeToString(FeatureType type);

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_FEATURES_FEATURE_H