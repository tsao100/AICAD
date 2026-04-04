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

namespace aicad {
namespace cad { class Document; }

namespace drawing {

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

private:
    cad::Document*        m_sourceDocument = nullptr;
    QVector<DrawingSheet*> m_sheets;
    DrawingSheet*          m_activeSheet   = nullptr;
    SheetConfig            m_defaultConfig;
    QString                m_companyName;
    QString                m_companyLogo;
};

} // namespace drawing
} // namespace aicad
