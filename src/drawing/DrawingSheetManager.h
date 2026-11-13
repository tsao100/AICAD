/**
 * @file DrawingSheetManager.h
 * @brief 圖紙文件管理器 — 管理一個 CAD 文件下的多張圖紙
 * @author AICAD Team
 * @date 2025-01-08
 *
 * 一個 CAD Document 可以對應多張 DrawingSheet（例如：
 *   - 總組圖（Assembly）
 *   - 零件圖 1、零件圖 2…
 *   - 焊接圖
 * ）
 * DrawingSheetManager 負責建立、切換、序列化這些圖紙。
 */

#pragma once

#include "DrawingSheet.h"
#include <QObject>
#include <QVector>
#include <QUuid>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

namespace aicad {
namespace cad { class Document; }

namespace drawing {

/** 圖冊（Book）：一組圖紙（封面、圖目錄、圖）與其全域設定 */
struct DrawingBook {
    QString     id;
    QString     name;
    QJsonObject settings;   ///< 全域設定（PlanProfileSettings::toJson）
};

/**
 * @brief 圖紙管理器
 *
 * 建議掛在 Document 上（或以獨立物件持有），作為圖紙的統一入口。
 *
 * 典型使用流程：
 * @code
 *   // 建立
 *   auto* mgr = new DrawingSheetManager(document);
 *   mgr->setSourceDocument(document);
 *
 *   // 新增圖紙
 *   DrawingSheet* sheet = mgr->newSheet("Assembly Drawing");
 *   sheet->config().paperSize = PaperSize::A2;
 *   sheet->addView(ViewType::Front, {30, 50});
 *
 *   // 儲存所有圖紙
 *   mgr->saveAll("/path/to/project/");
 * @endcode
 */
class DrawingSheetManager : public QObject {
    Q_OBJECT

public:
    explicit DrawingSheetManager(QObject* parent = nullptr);
    ~DrawingSheetManager() override;

    /** 取得（必要時建立）文件的管理器；掛在 Document 之下。 */
    static DrawingSheetManager* forDocument(cad::Document* doc);

    // ── 圖冊 ──────────────────────────────────────────────────────────────
    const QVector<DrawingBook>& books() const { return m_books; }
    DrawingBook* findBook(const QString& id);
    DrawingBook* addBook(const QString& name, const QJsonObject& settings, const QString& id = {});
    /** 刪除圖冊；deleteSheets=false 時圖紙保留（脫離圖冊）。 */
    void removeBook(const QString& id, bool deleteSheets);

    /** 通知內容已變更：同步到 Document（Feature Browser 與存檔用）並標記文件已修改。 */
    void touch();
    /** 線形有變更 → 稍後重算所有圖冊 */
    void scheduleRefresh();
    /** 立即重算所有圖冊（以圖冊的全域設定與各圖的局部設定）。 */
    void refreshBooks();

    // ── 關聯文件 ──────────────────────────────────────────────────────────
    void setSourceDocument(cad::Document* doc);
    cad::Document* sourceDocument() const { return m_sourceDocument; }

    // ── 圖紙管理 ──────────────────────────────────────────────────────────

    /** 新增一張空白圖紙 */
    DrawingSheet* newSheet(const QString& title = {});

    /** 開啟已存在的圖紙檔案並加入管理 */
    DrawingSheet* openSheet(const QString& fileName);

    /** 移除圖紙（從記憶體移除，不刪除檔案） */
    void removeSheet(const QUuid& sheetId);

    /** 複製圖紙（產生新的 UUID 與 fileName） */
    DrawingSheet* duplicateSheet(const QUuid& sheetId);

    const QVector<DrawingSheet*>& sheets() const { return m_sheets; }
    DrawingSheet* findSheet(const QUuid& id) const;

    /** 重新排列圖紙順序（order 必須恰為目前所有圖紙的一個排列）。 */
    bool setSheetOrder(const QVector<DrawingSheet*>& order);
    DrawingSheet* activeSheet() const { return m_activeSheet; }

    // ── 啟用圖紙 ──────────────────────────────────────────────────────────
    void setActiveSheet(const QUuid& sheetId);
    void setActiveSheet(DrawingSheet* sheet);

    // ── 儲存 / 載入 ───────────────────────────────────────────────────────

    /** 儲存所有圖紙（各自儲存至自身 fileName） */
    bool saveAll();

    /**
     * 儲存管理器索引（記錄哪些圖紙屬於本文件）
     * 檔案為 .aicad_dwg_index 的 JSON。
     */
    bool saveIndex(const QString& indexFilePath);

    /** 從索引檔案重新載入所有圖紙 */
    bool loadFromIndex(const QString& indexFilePath);

    // ── 內嵌於 .aicad ─────────────────────────────────────────────────────

    /**
     * 序列化所有圖紙（JSON 陣列）。DrawingSheetManager 在文件 aboutToSave() 時
     * 會自動呼叫並寫入 Document::setDrawingSheetsData()，隨 .aicad 一併存檔。
     */
    QJsonArray toJson() const;

    /** 從 JSON 陣列重建圖紙（先 setSourceDocument()，才能還原視圖的來源特徵）。 */
    void loadFromJson(const QJsonArray& sheets);

    // ── 預設值 ────────────────────────────────────────────────────────────

    /** 設定新圖紙的預設配置（之後 newSheet() 都套用） */
    void setDefaultConfig(const SheetConfig& config);
    const SheetConfig& defaultConfig() const { return m_defaultConfig; }

    /** 設定公司資訊（套用到所有新圖紙的標題欄） */
    void setCompanyInfo(const QString& name, const QString& logoPath = {});

Q_SIGNALS:
    void sheetAdded(DrawingSheet* sheet);
    void sheetRemoved(const QUuid& sheetId);
    void activeSheetChanged(DrawingSheet* sheet);
    void anySheetModified();

private Q_SLOTS:
    void onTclListChanged();
    void syncToDocument();

private:
    QVector<DrawingBook>  m_books;
    QTimer*               m_syncTimer = nullptr;
    QTimer*               m_refreshTimer = nullptr;
    bool                  m_refreshing = false;
    cad::Document*        m_sourceDocument = nullptr;
    QVector<DrawingSheet*> m_sheets;
    DrawingSheet*          m_activeSheet   = nullptr;
    SheetConfig            m_defaultConfig;
    QString                m_companyName;
    QString                m_companyLogo;
};

} // namespace drawing
} // namespace aicad
