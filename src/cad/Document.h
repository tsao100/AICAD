/**
 * @file Document.h
 * @brief CAD 文件類別，管理特徵樹和幾何數據
 * @author Ben
 * @date 2025-01-06
 */

#ifndef AICAD_CAD_DOCUMENT_H
#define AICAD_CAD_DOCUMENT_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QSharedPointer>

// OCCT includes
#include <TDocStd_Document.hxx>
#include <TDF_Label.hxx>
#include <TopoDS_Shape.hxx>

namespace aicad {
namespace cad {

// Forward declarations
class Feature;
class Sketch;

/**
 * @brief CAD 文件類別
 * 
 * Document 管理單一 CAD 設計的所有數據:
 * - 特徵樹層級結構
 * - OCCT 文件整合
 * - 特徵創建和管理
 * - 文件儲存/載入
 * 
 * 使用範例:
 * @code
 * Document* doc = new Document();
 * Sketch* sketch = doc->createSketch("XY");
 * Extrude* extrude = doc->createExtrude(sketch, 10.0);
 * doc->save("mydesign.cad");
 * @endcode
 */
class Document : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY fileNameChanged)
    Q_PROPERTY(bool modified READ isModified WRITE setModified NOTIFY modifiedChanged)
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
     * @brief 建立新的草圖
     * @param planeName 平面名稱 ("XY", "XZ", "YZ")
     * @param name 特徵名稱 (可選)
     * @return 草圖指標
     */
    Sketch* createSketch(const QString& planeName, const QString& name = QString());
    
    /**
     * @brief 建立擠出特徵
     * @param sketch 基準草圖
     * @param distance 擠出距離
     * @param name 特徵名稱 (可選)
     * @return 擠出特徵指標
     */
    Feature* createExtrude(Sketch* sketch, double distance, const QString& name = QString());
    
    /**
     * @brief 取得所有特徵
     */
    QVector<Feature*> features() const;
    
    /**
     * @brief 取得特徵數量
     */
    int featureCount() const;
    
    /**
     * @brief 根據 ID 尋找特徵
     * @param id 特徵 ID
     * @return 特徵指標，找不到則為 nullptr
     */
    Feature* findFeature(int id) const;
    
    /**
     * @brief 根據名稱尋找特徵
     * @param name 特徵名稱
     * @return 特徵指標，找不到則為 nullptr
     */
    Feature* findFeatureByName(const QString& name) const;
    
    /**
     * @brief 刪除特徵
     * @param feature 要刪除的特徵
     * @return 成功回傳 true
     */
    bool deleteFeature(Feature* feature);
    
    /**
     * @brief 儲存文件
     * @param filePath 檔案路徑
     * @return 成功回傳 true
     */
    bool save(const QString& filePath);
    
    /**
     * @brief 載入文件
     * @param filePath 檔案路徑
     * @return 成功回傳 true
     */
    bool load(const QString& filePath);
    
    /**
     * @brief 取得檔案名稱
     */
    QString fileName() const;
    
    /**
     * @brief 設定檔案名稱
     */
    void setFileName(const QString& name);
    
    /**
     * @brief 檢查是否已修改
     */
    bool isModified() const;
    
    /**
     * @brief 設定修改狀態
     */
    void setModified(bool modified);
    
    /**
     * @brief 取得 OCCT 文件
     */
    Handle(TDocStd_Document) occtDocument() const;
    
    /**
     * @brief 重建所有特徵
     */
    void rebuild();
    
Q_SIGNALS:
    /**
     * @brief 特徵被建立時發出
     * @param feature 新特徵
     */
    void featureCreated(Feature* feature);
    
    /**
     * @brief 特徵被刪除時發出
     * @param featureId 特徵 ID
     */
    void featureDeleted(int featureId);
    
    /**
     * @brief 特徵被修改時發出
     * @param feature 被修改的特徵
     */
    void featureModified(Feature* feature);
    
    /**
     * @brief 檔案名稱改變時發出
     */
    void fileNameChanged(const QString& name);
    
    /**
     * @brief 修改狀態改變時發出
     */
    void modifiedChanged(bool modified);
    
    /**
     * @brief 特徵數量改變時發出
     */
    void featureCountChanged(int count);
    
    /**
     * @brief 重建開始時發出
     */
    void rebuildStarted();
    
    /**
     * @brief 重建完成時發出
     */
    void rebuildCompleted();
    
private:
    /**
     * @brief 產生唯一的特徵 ID
     */
    int generateFeatureId();
    
    /**
     * @brief 產生預設特徵名稱
     */
    QString generateFeatureName(const QString& type);
    
    class Private;
    Private* d;
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_DOCUMENT_H