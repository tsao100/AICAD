/**
 * @file Document.h
 * @brief CAD 文件管理類別 (完整版)
 * @author Ben
 * @date 2024-12-04
 */

#ifndef AICAD_CAD_DOCUMENT_H
#define AICAD_CAD_DOCUMENT_H

#include <QObject>
#include <QString>
#include <QVector>

namespace aicad {
namespace cad {

class Feature;
class Sketch;
class Extrude;
struct CustomPlane;

/**
 * @brief CAD 文件類別
 * 
 * Document 管理一個 CAD 文件中的所有特徵，提供建立、儲存、載入功能。
 * 
 * 使用範例:
 * @code
 * Document doc;
 * 
 * // 建立草圖
 * Sketch* sketch = doc.createSketch(CustomPlane::XY(), "Sketch1");
 * sketch->addPolyline(points);
 * 
 * // 建立擠出
 * Extrude* extrude = doc.createExtrude(sketch, 10.0, "Extrude1");
 * 
 * // 更新所有特徵
 * doc.updateAll();
 * 
 * // 儲存
 * doc.save("myproject.aicad");
 * @endcode
 */
class Document : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileNameChanged)
    Q_PROPERTY(bool modified READ isModified NOTIFY modifiedChanged)
    Q_PROPERTY(int featureCount READ featureCount NOTIFY featureCountChanged)
    
public:
    /**
     * @brief 建構子
     * @param parent 父物件
     */
    explicit Document(QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~Document() override;
    
    /**
     * @brief 建立新文件
     * 
     * 清除所有現有特徵並重置文件狀態
     */
    void newDocument();
    
    /**
     * @brief 儲存文件
     * @param fileName 檔案路徑
     * @return 成功回傳 true
     */
    bool save(const QString& fileName);
    
    /**
     * @brief 載入文件
     * @param fileName 檔案路徑
     * @return 成功回傳 true
     */
    bool load(const QString& fileName);
    
    /**
     * @brief 建立草圖特徵
     * @param plane 草圖平面
     * @param name 特徵名稱（可選）
     * @return 新建立的草圖指標
     */
    Sketch* createSketch(const CustomPlane& plane, const QString& name = QString());
    
    /**
     * @brief 建立擠出特徵
     * @param sketch 要擠出的草圖
     * @param height 擠出高度
     * @param name 特徵名稱（可選）
     * @return 新建立的擠出指標
     */
    Extrude* createExtrude(Sketch* sketch, double height, const QString& name = QString());
    
    /**
     * @brief 新增特徵到文件
     * @param feature 特徵指標（所有權轉移給 Document）
     */
    void addFeature(Feature* feature);
    
    /**
     * @brief 移除特徵
     * @param feature 要移除的特徵
     */
    void removeFeature(Feature* feature);
    
    /**
     * @brief 取得所有特徵
     * @return 特徵列表
     */
    QVector<Feature*> features() const;
    
    /**
     * @brief 根據 ID 尋找特徵
     * @param id 特徵 ID
     * @return 找到的特徵，否則為 nullptr
     */
    Feature* findFeature(int id) const;
    
    /**
     * @brief 根據名稱尋找特徵
     * @param name 特徵名稱
     * @return 找到的特徵，否則為 nullptr
     */
    Feature* findFeatureByName(const QString& name) const;
    
    /**
     * @brief 更新所有特徵
     * 
     * 依照依賴順序更新所有特徵
     */
    void updateAll();
    
    /**
     * @brief 取得特徵數量
     */
    int featureCount() const;
    
    /**
     * @brief 取得檔案名稱
     */
    QString fileName() const;
    
    /**
     * @brief 設定檔案名稱
     */
    void setFileName(const QString& fileName);
    
    /**
     * @brief 檢查是否已修改
     */
    bool isModified() const;
    
    /**
     * @brief 設定修改狀態
     */
    void setModified(bool modified);
    
    /**
     * @brief 清除所有特徵
     */
    void clear();
    
Q_SIGNALS:
    /**
     * @brief 特徵被新增時發出
     */
    void featureAdded(Feature* feature);
    
    /**
     * @brief 特徵被移除時發出
     */
    void featureRemoved(Feature* feature);
    
    /**
     * @brief 特徵被更新時發出
     */
    void featureUpdated(Feature* feature);
    
    /**
     * @brief 檔案名稱改變時發出
     */
    void fileNameChanged(const QString& fileName);
    
    /**
     * @brief 修改狀態改變時發出
     */
    void modifiedChanged(bool modified);
    
    /**
     * @brief 特徵數量改變時發出
     */
    void featureCountChanged(int count);
    
    /**
     * @brief 文件被清空時發出
     */
    void documentCleared();
    
private:
    /**
     * @brief 產生唯一的特徵 ID
     */
    int generateFeatureId();
    
    class Private;
    Private* d;
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_DOCUMENT_H