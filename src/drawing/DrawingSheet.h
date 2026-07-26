/**
 * @file DrawingSheet.h
 * @brief 圖紙文件主類別 — 管理整張圖紙的設定、視圖、表格與標題欄
 * @author AICAD Team
 * @date 2025-01-08
 *
 * 架構概覽：
 *
 *   DrawingSheet          ← 一張圖紙（對應一個 .aicad_dwg 檔案）
 *     ├─ SheetConfig      ← 圖紙規格（紙張大小、比例、單位、圖框樣板）
 *     ├─ TitleBlock       ← 標題欄（公司、圖號、版次、日期、核准…）
 *     ├─ DrawingView[]    ← 各種視圖（前視、上視、等角、剖面…）
 *     ├─ PartsListTable   ← 零件表（BOM）
 *     ├─ DrawingIndex     ← 圖目錄
 *     ├─ RevisionTable    ← 進版說明（ECO / ECN）
 *     ├─ Legend           ← 圖例（符號說明）
 *     └─ NoteBlock[]      ← 說明文字區塊
 */

#pragma once

#include <QObject>
#include <QUuid>
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QSizeF>
#include <QPointF>
#include <QRectF>

// Forward declarations
namespace aicad {
namespace cad  { class Document; class Feature; }
}

namespace aicad {
namespace drawing {

// ─────────────────────────────────────────────────────────────────────────────
// 基本列舉
// ─────────────────────────────────────────────────────────────────────────────

/** 紙張尺寸 */
enum class PaperSize {
    A4,   // 210 × 297 mm
    A3,   // 297 × 420 mm
    A2,   // 420 × 594 mm
    A1,   // 594 × 841 mm
    A0,   // 841 × 1189 mm
    B4,
    B3,
    Letter,
    Tabloid,
    Custom
};

/** 紙張方向 */
enum class PaperOrientation {
    Portrait,
    Landscape
};

/** 長度單位 */
enum class DrawingUnit {
    Millimeter,
    Inch
};

/** 圖框樣板 */
enum class FrameTemplate {
    None,
    Simple,       // 只有外框
    Standard,     // 外框 + 標題欄
    ISO7200,      // ISO 7200 標準標題欄
    Custom        // 使用者自訂 SVG/模板
};

/** 視圖投影方式 */
enum class ProjectionMethod {
    FirstAngle,   // 第一角投影法（歐規）
    ThirdAngle    // 第三角投影法（美規/台規）
};

/** 視圖類型 */
enum class ViewType {
    Front,        // 前視圖
    Top,          // 上視圖
    Right,        // 右視圖
    Left,         // 左視圖
    Bottom,       // 下視圖
    Back,         // 後視圖
    Isometric,    // 等角視圖
    Section,      // 剖面視圖
    Detail,       // 局部放大圖
    Auxiliary,    // 輔助視圖
    Custom        // 自訂視角
};

/** 剖面方向 */
enum class SectionDirection {
    Horizontal,
    Vertical,
    Diagonal
};

// ─────────────────────────────────────────────────────────────────────────────
// 圖紙設定
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 圖紙基本規格設定
 *
 * 這是整張圖紙最頂層的配置，決定紙張大小、單位、預設比例、投影法等。
 */
struct SheetConfig {
    PaperSize        paperSize        = PaperSize::A3;
    PaperOrientation orientation      = PaperOrientation::Landscape;
    DrawingUnit      unit             = DrawingUnit::Millimeter;
    FrameTemplate    frameTemplate    = FrameTemplate::Standard;
    ProjectionMethod projectionMethod = ProjectionMethod::ThirdAngle;

    double           defaultScale     = 1.0;   // 1:1
    double           lineWidthThin    = 0.25;  // mm
    double           lineWidthMedium  = 0.5;
    double           lineWidthThick   = 0.7;

    QString          customTemplatePath;       // FrameTemplate::Custom 時使用

    /** 取得紙張實際尺寸（mm）。Landscape 時寬高互換。 */
    QSizeF paperSizeMM() const;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& json);
};

// ─────────────────────────────────────────────────────────────────────────────
// 標題欄
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 標題欄資料
 *
 * 對應 ISO 7200 / CNS 的標題欄欄位。
 * 未來可擴充為支援自訂欄位（CustomField map）。
 */
struct TitleBlock {
    // ── 文件識別 ──────────────────────────────────────────────────────────
    QString drawingNumber;     // 圖號
    QString drawingTitle;      // 圖名
    QString sheetNumber;       // 頁次（例：1/3）
    QString revision;          // 版次（例：A、B、01）

    // ── 組織資訊 ──────────────────────────────────────────────────────────
    QString companyName;
    QString companyLogo;       // 公司 Logo 路徑（PNG/SVG）
    QString projectName;       // 專案名稱
    QString partNumber;        // 料號

    // ── 設計資訊 ──────────────────────────────────────────────────────────
    QString designedBy;
    QString checkedBy;
    QString approvedBy;
    QString designDate;
    QString approvalDate;

    // ── 技術規格 ──────────────────────────────────────────────────────────
    QString material;
    QString surfaceFinish;
    QString tolerance;         // 一般公差（例：±0.1）
    QString angularTolerance;  // 角度公差

    // ── 進版紀錄（最後一次，完整紀錄在 RevisionTable）──────────────────────
    QString lastRevisionDesc;
    QString lastRevisionDate;

    // ── 自訂欄位（key-value）──────────────────────────────────────────────
    QMap<QString, QString> customFields;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& json);
};

// ─────────────────────────────────────────────────────────────────────────────
// 視圖
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 剖面視圖參數
 */
struct SectionParams {
    SectionDirection direction = SectionDirection::Horizontal;
    QPointF          cutPlaneStart;   // 剖切線起點（圖紙座標，mm）
    QPointF          cutPlaneEnd;     // 剖切線終點
    QString          sectionLabel;    // 例："A-A"
    bool             showCutLine     = true;
    bool             showArrows      = true;
};

/**
 * @brief 局部放大視圖參數
 */
struct DetailParams {
    QPointF  center;         // 放大區域中心點
    double   radius  = 10.0; // 放大區域半徑（來源視圖座標，mm）
    double   scale   = 2.0;  // 局部放大比例
    QString  label;          // 例："B"
};

/**
 * @brief 一個圖面視圖
 *
 * DrawingView 記錄一個投影視圖的位置、比例與相關參數。
 * 實際的幾何生成（HLR/Hidden Line Removal）在 DrawingViewRenderer 中處理。
 */
class DrawingView : public QObject {
    Q_OBJECT

public:
    explicit DrawingView(QObject* parent = nullptr);
    ~DrawingView() override;

    // ── 識別 ──────────────────────────────────────────────────────────────
    QUuid    id()    const { return m_id; }
    QString  label() const { return m_label; }
    ViewType type()  const { return m_type; }

    // ── 版面配置 ──────────────────────────────────────────────────────────
    QPointF  position()  const { return m_position; }  // 視圖原點（mm）
    QSizeF   size()      const { return m_size; }      // 視圖邊界框（mm）
    double   scale()     const { return m_scale; }     // 顯示比例
    QRectF   boundingRect() const;

    // ── 關聯模型 ──────────────────────────────────────────────────────────
    cad::Feature* sourceFeature() const { return m_sourceFeature; }

    // ── 進階參數（依 type 使用） ──────────────────────────────────────────
    SectionParams sectionParams() const { return m_sectionParams; }
    DetailParams  detailParams()  const { return m_detailParams; }

    // ── 顯示選項 ──────────────────────────────────────────────────────────
    bool showHiddenLines()     const { return m_showHiddenLines; }
    bool showCenterLines()     const { return m_showCenterLines; }
    bool showDimensions()      const { return m_showDimensions; }
    bool showAnnotations()     const { return m_showAnnotations; }
    bool showScaleLabel()      const { return m_showScaleLabel; }
    bool showViewLabel()       const { return m_showViewLabel; }

    // ── Setters ───────────────────────────────────────────────────────────
    void setLabel(const QString& label);
    void setType(ViewType type);
    void setPosition(const QPointF& pos);
    void setSize(const QSizeF& size);
    void setScale(double scale);
    void setSourceFeature(cad::Feature* feature);
    void setSectionParams(const SectionParams& params);
    void setDetailParams(const DetailParams& params);
    void setShowHiddenLines(bool show);
    void setShowCenterLines(bool show);
    void setShowDimensions(bool show);
    void setShowAnnotations(bool show);

    // ── 序列化 ────────────────────────────────────────────────────────────
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& json);

Q_SIGNALS:
    void labelChanged(const QString& label);
    void positionChanged(const QPointF& pos);
    void scaleChanged(double scale);
    void paramsChanged();
    void rebuildRequested();

private:
    QUuid         m_id;
    QString       m_label;
    ViewType      m_type        = ViewType::Front;
    QPointF       m_position;
    QSizeF        m_size;
    double        m_scale       = 1.0;

    cad::Feature* m_sourceFeature = nullptr;

    SectionParams m_sectionParams;
    DetailParams  m_detailParams;

    bool m_showHiddenLines  = true;
    bool m_showCenterLines  = true;
    bool m_showDimensions   = true;
    bool m_showAnnotations  = true;
    bool m_showScaleLabel   = true;
    bool m_showViewLabel    = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// 零件表（BOM）
// ─────────────────────────────────────────────────────────────────────────────

/** 零件表一行 */
struct PartsListRow {
    int     itemNo;
    QString partNumber;
    QString description;
    QString material;
    int     quantity;
    QString unit;          // 個、組、m…
    QString revision;
    QString remark;
    QMap<QString, QString> customColumns;
};

/** 零件表設定 */
struct PartsListConfig {
    QPointF position;                           // 表格左上角（mm）
    double  rowHeight       = 8.0;             // 行高（mm）
    double  headerHeight    = 10.0;
    bool    autoFromDocument = true;            // 自動從 Document 讀取
    QStringList visibleColumns = {             // 顯示的欄位（有序）
        "itemNo", "partNumber", "description",
        "material", "quantity", "unit", "remark"
    };
};

/**
 * @brief 零件表（BOM）管理
 */
class PartsListTable : public QObject {
    Q_OBJECT

public:
    explicit PartsListTable(QObject* parent = nullptr);

    const PartsListConfig& config() const { return m_config; }
    void setConfig(const PartsListConfig& config);

    const QVector<PartsListRow>& rows() const { return m_rows; }
    void setRows(const QVector<PartsListRow>& rows);

    /** 從 Document 自動填充零件表 */
    void populateFromDocument(cad::Document* document);

    void addRow(const PartsListRow& row);
    void removeRow(int index);
    void moveRow(int from, int to);

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& json);

Q_SIGNALS:
    void rowsChanged();
    void configChanged();

private:
    PartsListConfig      m_config;
    QVector<PartsListRow> m_rows;
};

// ─────────────────────────────────────────────────────────────────────────────
// 進版說明表
// ─────────────────────────────────────────────────────────────────────────────

/** 一筆進版紀錄 */
struct RevisionEntry {
    QString revision;        // 版次代號（A, B, 01…）
    QString date;
    QString description;     // 變更說明
    QString changedBy;
    QString approvedBy;
    QString zone;            // 受影響區域（可選）
};

/**
 * @brief 進版說明表
 */
class RevisionTable : public QObject {
    Q_OBJECT

public:
    explicit RevisionTable(QObject* parent = nullptr);

    QPointF position() const { return m_position; }
    void setPosition(const QPointF& pos);

    const QVector<RevisionEntry>& entries() const { return m_entries; }
    void addEntry(const RevisionEntry& entry);
    void removeEntry(int index);

    /** 最新版次（供標題欄使用） */
    QString latestRevision() const;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& json);

Q_SIGNALS:
    void entriesChanged();

private:
    QPointF               m_position;
    QVector<RevisionEntry> m_entries;
};

// ─────────────────────────────────────────────────────────────────────────────
// 圖目錄
// ─────────────────────────────────────────────────────────────────────────────

/** 圖目錄中的一個項目 */
struct DrawingIndexEntry {
    QString sheetNumber;
    QString drawingNumber;
    QString title;
    QString revision;
    QString date;
    QString remark;
};

/**
 * @brief 圖目錄（Drawing Index / Sheet List）
 *
 * 通常用於多張圖紙組成的圖包首頁。
 */
class DrawingIndex : public QObject {
    Q_OBJECT

public:
    explicit DrawingIndex(QObject* parent = nullptr);

    QPointF position() const { return m_position; }
    QString title()    const { return m_title; }
    void setPosition(const QPointF& pos);
    void setTitle(const QString& title);

    const QVector<DrawingIndexEntry>& entries() const { return m_entries; }
    void addEntry(const DrawingIndexEntry& entry);
    void removeEntry(int index);

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& json);

Q_SIGNALS:
    void entriesChanged();

private:
    QPointF m_position;
    QString m_title = "Drawing Index";
    QVector<DrawingIndexEntry> m_entries;
};

// ─────────────────────────────────────────────────────────────────────────────
// 圖例
// ─────────────────────────────────────────────────────────────────────────────

/** 圖例中的一個符號說明 */
struct LegendEntry {
    QString symbol;       // 符號（Unicode 字符或圖示路徑）
    QString description;
};

/**
 * @brief 圖例（Legend）
 */
class Legend : public QObject {
    Q_OBJECT

public:
    explicit Legend(QObject* parent = nullptr);

    QPointF position() const { return m_position; }
    QString title()    const { return m_title; }
    void setPosition(const QPointF& pos);
    void setTitle(const QString& title);

    const QVector<LegendEntry>& entries() const { return m_entries; }
    void addEntry(const LegendEntry& entry);
    void removeEntry(int index);

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& json);

Q_SIGNALS:
    void entriesChanged();

private:
    QPointF m_position;
    QString m_title = "Legend";
    QVector<LegendEntry> m_entries;
};

// ─────────────────────────────────────────────────────────────────────────────
// 說明文字區塊
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 自由文字說明區塊（General Notes / Specifications）
 */
struct NoteBlock {
    QUuid   id;
    QString title;      // 例："General Notes"
    QString content;    // 多行文字
    QPointF position;
    QSizeF  size;
    double  fontSize   = 3.5;   // mm
    bool    hasBorder  = true;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& json);
};

// ─────────────────────────────────────────────────────────────────────────────
// DrawingSheet — 主類別
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 圖紙文件主類別
 *
 * 一個 DrawingSheet 代表一張完整的工程圖紙。
 * 支援序列化（JSON）、與 CAD Document 關聯、以及預覽輸出。
 *
 * 使用方式：
 * @code
 *   auto* sheet = new DrawingSheet(this);
 *   sheet->setSourceDocument(myDocument);
 *   sheet->config().paperSize = PaperSize::A2;
 *   sheet->titleBlock().drawingTitle = "Base Plate";
 *   sheet->addView(ViewType::Front, {20, 40}, 1.0);
 *   sheet->save("base_plate.aicad_dwg");
 * @endcode
 */
class DrawingSheet : public QObject {
    Q_OBJECT

public:
    explicit DrawingSheet(QObject* parent = nullptr);
    ~DrawingSheet() override;

    // ── 識別 ──────────────────────────────────────────────────────────────
    QUuid   id()       const { return m_id; }
    QString fileName() const { return m_fileName; }
    bool    isModified() const { return m_modified; }

    // ── 關聯文件 ──────────────────────────────────────────────────────────
    cad::Document* sourceDocument() const { return m_sourceDocument; }
    void setSourceDocument(cad::Document* doc);

    // ── 圖紙設定 ──────────────────────────────────────────────────────────
    SheetConfig&       config()     { return m_config; }
    const SheetConfig& config() const { return m_config; }
    void applyConfig(const SheetConfig& config);

    // ── 標題欄 ────────────────────────────────────────────────────────────
    TitleBlock&        titleBlock()       { return m_titleBlock; }
    const TitleBlock&  titleBlock() const { return m_titleBlock; }
    void setTitleBlock(const TitleBlock& tb);

    // ── 視圖管理 ──────────────────────────────────────────────────────────
    DrawingView* addView(ViewType type,
                         const QPointF& position,
                         double scale = 0.0);    // 0 = 使用預設比例
    void         removeView(const QUuid& viewId);
    DrawingView* findView(const QUuid& viewId) const;
    const QVector<DrawingView*>& views() const { return m_views; }

    // ── 零件表 ────────────────────────────────────────────────────────────
    PartsListTable* partsList() const { return m_partsList; }
    void enablePartsList(bool enable);
    bool isPartsListEnabled() const { return m_partsListEnabled; }

    // ── 圖目錄 ────────────────────────────────────────────────────────────
    DrawingIndex* drawingIndex() const { return m_drawingIndex; }
    void enableDrawingIndex(bool enable);
    bool isDrawingIndexEnabled() const { return m_drawingIndexEnabled; }

    // ── 進版表 ────────────────────────────────────────────────────────────
    RevisionTable* revisionTable() const { return m_revisionTable; }
    void enableRevisionTable(bool enable);
    bool isRevisionTableEnabled() const { return m_revisionTableEnabled; }

    // ── 圖例 ──────────────────────────────────────────────────────────────
    Legend* legend() const { return m_legend; }
    void enableLegend(bool enable);
    bool isLegendEnabled() const { return m_legendEnabled; }

    // ── 說明文字區塊 ──────────────────────────────────────────────────────
    NoteBlock* addNoteBlock(const QString& title = "Notes");
    void       removeNoteBlock(const QUuid& id);
    const QVector<NoteBlock>& noteBlocks() const { return m_noteBlocks; }

    // ── 檔案 I/O ──────────────────────────────────────────────────────────
    bool save(const QString& fileName = {});
    bool load(const QString& fileName);

    // ── 輸出 ──────────────────────────────────────────────────────────────
    /** 匯出為 PDF（路徑為空則使用同目錄同名） */
    bool exportPdf(const QString& pdfPath = {});

    /** 匯出為 SVG（用於預覽） */
    bool exportSvg(const QString& svgPath);

    /** 產生縮圖（QPixmap，用於檔案管理員預覽） */
    // QPixmap generateThumbnail(const QSize& size = {256, 181});

    // ── 版面計算 ──────────────────────────────────────────────────────────
    /** 計算所有視圖的建議排版位置（自動配置） */
    void autoLayout();

    /** 檢查視圖是否有重疊 */
    bool hasOverlap() const;

Q_SIGNALS:
    void fileNameChanged(const QString& fileName);
    void modifiedChanged(bool modified);
    void configChanged();
    void titleBlockChanged();
    void viewAdded(DrawingView* view);
    void viewRemoved(const QUuid& viewId);
    void viewsLayoutChanged();
    void partsListChanged();
    void revisionTableChanged();
    void rebuildAllViews();

private:
    void setModified(bool modified);
    void setFileName(const QString& fileName);

    // ── 成員 ──────────────────────────────────────────────────────────────
    QUuid          m_id;
    QString        m_fileName;
    bool           m_modified = false;

    cad::Document* m_sourceDocument = nullptr;

    SheetConfig    m_config;
    TitleBlock     m_titleBlock;

    QVector<DrawingView*> m_views;

    PartsListTable* m_partsList        = nullptr;
    bool            m_partsListEnabled = false;

    DrawingIndex*   m_drawingIndex        = nullptr;
    bool            m_drawingIndexEnabled = false;

    RevisionTable*  m_revisionTable        = nullptr;
    bool            m_revisionTableEnabled = false;

    Legend*         m_legend        = nullptr;
    bool            m_legendEnabled = false;

    QVector<NoteBlock> m_noteBlocks;
};

} // namespace drawing
} // namespace aicad
