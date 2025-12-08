/**
 * @file DocumentManager.h
 * @brief 文件管理器，管理所有開啟的 CAD 文件
 * @author Jack
 * @date 2024-12-04
 */

#ifndef AICAD_CORE_DOCUMENTMANAGER_H
#define AICAD_CORE_DOCUMENTMANAGER_H

#include <QObject>
#include <QString>
#include <QVector>

namespace aicad {

// 前向宣告 (避免循環依賴)
namespace cad {
    class Document;
}

namespace core {

/**
 * @brief 文件管理器
 * 
 * DocumentManager 管理應用程式中所有開啟的文件:
 * - 建立/開啟/關閉文件
 * - 追蹤當前活動文件
 * - 管理文件生命週期
 * 
 * 使用範例:
 * @code
 * DocumentManager* mgr = app->documentManager();
 * 
 * // 建立新文件
 * cad::Document* doc = mgr->createDocument("MyProject");
 * 
 * // 取得當前文件
 * cad::Document* current = mgr->currentDocument();
 * @endcode
 */
class DocumentManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(int documentCount READ documentCount NOTIFY documentCountChanged)
    
public:
    /**
     * @brief 建構子
     * @param parent 父物件
     */
    explicit DocumentManager(QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~DocumentManager() override;
    
    /**
     * @brief 建立新文件
     * @param name 文件名稱 (可選，自動產生唯一名稱)
     * @return 新建立的文件指標
     * 
     * @note 文件所有權歸 DocumentManager，不要手動刪除
     */
    cad::Document* createDocument(const QString& name = QString());
    
    /**
     * @brief 開啟文件
     * @param filePath 檔案路徑
     * @return 開啟的文件指標，失敗則為 nullptr
     */
    cad::Document* openDocument(const QString& filePath);
    
    /**
     * @brief 關閉文件
     * @param document 要關閉的文件
     * @param force 是否強制關閉 (不檢查未儲存變更)
     * @return 成功回傳 true
     */
    bool closeDocument(cad::Document* document, bool force = false);
    
    /**
     * @brief 關閉所有文件
     * @param force 是否強制關閉
     * @return 成功回傳 true
     */
    bool closeAll(bool force = false);
    
    /**
     * @brief 取得所有文件
     */
    QVector<cad::Document*> documents() const;
    
    /**
     * @brief 取得文件數量
     */
    int documentCount() const;
    
    /**
     * @brief 設定當前活動文件
     * @param document 要設為活動的文件
     */
    void setCurrentDocument(cad::Document* document);
    
    /**
     * @brief 取得當前活動文件
     * @return 當前文件指標，無文件時為 nullptr
     */
    cad::Document* currentDocument() const;
    
    /**
     * @brief 根據名稱尋找文件
     * @param name 文件名稱
     * @return 找到的文件指標，否則為 nullptr
     */
    cad::Document* findDocument(const QString& name) const;
    
    /**
     * @brief 檢查是否有未儲存的變更
     * @return 有任何文件被修改則回傳 true
     */
    bool hasUnsavedChanges() const;
    
Q_SIGNALS:
    /**
     * @brief 文件被建立時發出
     * @param name 文件名稱
     */
    void documentCreated(const QString& name);
    
    /**
     * @brief 文件被開啟時發出
     * @param document 文件指標
     */
    void documentOpened(cad::Document* document);
    
    /**
     * @brief 文件被關閉時發出
     * @param name 文件名稱
     */
    void documentClosed(const QString& name);
    
    /**
     * @brief 當前文件改變時發出
     * @param document 新的當前文件
     */
    void currentDocumentChanged(cad::Document* document);
    
    /**
     * @brief 文件數量改變時發出
     * @param count 新的文件數量
     */
    void documentCountChanged(int count);
    
private:
    /**
     * @brief 產生唯一的文件名稱
     */
    QString generateUniqueName();
    
    class Private;
    Private* d;
};

} // namespace core
} // namespace aicad

#endif // AICAD_CORE_DOCUMENTMANAGER_H