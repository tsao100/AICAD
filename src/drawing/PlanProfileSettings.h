/**
 * @file PlanProfileSettings.h
 * @brief 平縱面圖（Plan & Profile）設定、版面計算與設定檔存取
 *
 * 一張平縱面圖圖紙 = DrawingSheet + PlanProfileLayer。Layer 內含一份
 * PlanProfileSettings（產生當下的參數快照）與該張負責的里程範圍，
 * 由 PlanProfilePainter 直接以軌道中心線（TrackCenterLine）資料繪製，
 * 因此預覽、PDF/SVG 匯出與 .aicad 存檔都共用同一條路徑。
 *
 * 設定的存取方式：
 *   1. 隨每張圖紙存進 .aicad（DrawingSheet::toJson 的 "planProfile"）。
 *   2. 獨立設定檔 *.aicad_ppcfg（saveToFile / loadFromFile），可跨專案共用。
 *   3. 使用者預設值（QSettings "AICAD"/"AICAD" 群組 "PlanProfile"）。
 *
 * 此標頭刻意不含 OCCT／DrawingSheet.h，避免循環引用（紙張規格以 int 儲存，
 * 對應 DrawingSheet.h 的 PaperSize／PaperOrientation 列舉順序）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QRectF>
#include <QSizeF>
#include <QVector>

namespace aicad {
namespace drawing {

/** 一張圖紙裡的一段線形（某條線形的一個里程區間） */
struct PlanProfileSegment {
    QString tclId;
    double  start = 0.0;
    double  end   = 0.0;
};

/** 機廠平面圖的一個矩形視圖（一個視圖一張圖）。 */
struct DepotView {
    QString name;
    double  e = 0.0, n = 0.0;     ///< 矩形中心（TM2 E、N）
    double  w = 150.0, h = 120.0; ///< 矩形大小 [m]（A3 橫式 1:500 約可容納 159×125）（沿視圖 X、Y 軸）
    double  rotDeg = 0.0;         ///< 轉角 [度]，逆時針；有對齊線形時疊加在對齊方向上
    QString alignTcl;             ///< 對齊的線形 id（空＝不對齊；視圖 X 軸對齊該線形在 alignCh 處的切線）
    double  alignCh = 0.0;        ///< 對齊位置的里程
    int     order = 1;            ///< 排序（小者在前）
    double  scale = 0.0;          ///< 比例分母（0＝自動配合圖紙）
    QJsonObject toJson() const;
    static DepotView fromJson(const QJsonObject& o);
};

struct PlanProfileSettings {
    // ── 來源線形 ──────────────────────────────────────────────────────────
    QString tclId;                       ///< TrackCenterLine id
    bool    useFullRange   = true;       ///< true＝整條線形；false＝使用 startCh/endCh
    double  startCh        = 0.0;        ///< [m]
    double  endCh          = 0.0;        ///< [m]
    QStringList tclIds;                  ///< 起始線形（1 條以上；依序各自沿後續線形產生）
    QStringList noChainIds;              ///< 單獨處理（不成串）的線形：不往後接續，也不被其他線形串進去
    bool    followChain    = true;       ///< 線形終點接著另一條線形時，繼續產生（直到沒有後續）
    double  joinTol        = 2.0;        ///< 判定「接在後面」的 E,N 端點距離容許值 [m]
    double  joinTolZ       = 1.0;        ///< 判定「接在後面」的高程差容許值 [m]（任一線形無縱斷面則不檢查）
    QString bookName       = QStringLiteral("平縱面圖冊");   ///< 圖冊名稱
    bool    generateCover  = true;       ///< 圖冊含封面
    bool    showNeighbors  = true;       ///< 平面圖框內其他線形一併繪出
    bool    generateIndex  = true;       ///< 同時產生圖目錄（獨立圖紙）
    bool    generateDataTables = true;   ///< 同時產生平面／縱面線形資料表圖頁
    bool    generateSchematic  = true;   ///< 同時產生簡圖（軌道示意／平面簡圖／縱面簡圖）
    bool    generateDepot      = true;   ///< 同時產生機廠平面圖（每個矩形視圖一張）
    QVector<DepotView> depotViews;       ///< 機廠矩形視圖清單
    int     tableDecimals  = 5;          ///< 線形資料表數值的小數位數
    int     tableAzDecimals = 3;
    bool    radiusSigned   = true;       ///< 資料表半徑是否帶正負號（右＋左－）
    int     remarkDecimals = 0;          ///< 圖目錄 Remark 里程範圍的小數位數         ///< 線形資料表方位角「秒」的小數位數
    QStringList hzCols;                  ///< 平面資料表欄位（id，依序；空＝預設）
    QStringList vtCols;                  ///< 縱面資料表欄位（id，依序；空＝預設）

    // ── 比例與分段 ────────────────────────────────────────────────────────
    double  hScale         = 1000.0;     ///< 水平比例分母（1:N）；平面圖與縱斷面共用
    double  vScale         = 100.0;      ///< 垂直比例分母（1:N）
    bool    autoVScale     = true;       ///< true：垂直比例由內容自動挑（不會比 vScale 更誇張）
    double  segmentLength  = 0.0;        ///< 每張圖紙長度 [m]；0＝依繪圖寬度自動
    double  overlap        = 0.0;        ///< 相鄰圖紙重疊長度 [m]

    // ── 版面（mm）────────────────────────────────────────────────────────
    int     paperSize      = 1;          ///< PaperSize 列舉（1＝A3）
    int     orientation    = 1;          ///< PaperOrientation（1＝Landscape）
    double  planBandH      = 70.0;       ///< 平面圖帶高度
    double  profileBandH   = 85.0;       ///< 縱斷面帶高度
    double  tableRowH      = 7.0;        ///< 資料表每列高度
    double  leftLabelW     = 28.0;       ///< 左側標籤欄寬度
    double  textSize       = 2.5;        ///< 註記字高 [mm]

    // ── 刻度與里程格式 ────────────────────────────────────────────────────
    double  minorTick      = 20.0;       ///< 次刻度間距 [m]
    double  majorTick      = 100.0;      ///< 主刻度間距 [m]（標註里程）
    double  elevGridStep   = 0.0;        ///< 高程格線間距 [m]；0＝自動
    double  tableInterval  = 50.0;       ///< 資料表高程取值間距 [m]
    QString chainagePrefix = QStringLiteral("K");  ///< 里程前綴；格式 K{km}+{m}
    bool    kmFormat       = true;       ///< true：K1+234.00；false：純數字 1234.00
    bool    useContChainage= false;      ///< 以連續里程（contChainage）標註
    int     elevDecimals   = 2;          ///< 高程小數位數

    // ── 平面圖 ────────────────────────────────────────────────────────────
    double  coordGrid      = 100.0;      ///< TM2 座標格線間距 [m]；0＝不畫
    bool    showNorthArrow = true;
    bool    showKeyPoints  = true;       ///< 關鍵點（TS/SC/CS/ST…）標記
    bool    showCurveData  = true;       ///< 曲線資料（R、Lc）

    // ── 縱斷面 ────────────────────────────────────────────────────────────
    bool    showGridLines  = true;
    bool    showPVI        = true;       ///< PVI／豎曲線起訖點／K 值標註
    bool    showGradeLabels= true;       ///< 坡度與坡長標註

    // ── 資料表列 ──────────────────────────────────────────────────────────
    bool    rowElevation   = true;
    bool    rowGrade       = true;
    bool    rowCurve       = true;       ///< 半徑／超高
    bool    rowAzimuth     = true;

    // ── 圖紙文字（可用 {name} {n} {N} {start} {end}）──────────────────────
    QString titleTemplate  = QStringLiteral("平縱面圖 — {name}");
    QString numberTemplate = QStringLiteral("PP-{n}");
    bool    replaceExisting= true;       ///< 重新產生時先移除同一條線形之前產生的圖紙

    /** 起始線形清單（tclIds 為空時退回 tclId） */
    QStringList startIds() const {
        if (!tclIds.isEmpty()) return tclIds;
        return tclId.isEmpty() ? QStringList() : QStringList(tclId);
    }

    QJsonObject toJson() const;
    bool        fromJson(const QJsonObject& j);

    // ── 設定檔 *.aicad_ppcfg ───────────────────────────────────────────────
    bool saveToFile(const QString& path, QString* error = nullptr) const;
    bool loadFromFile(const QString& path, QString* error = nullptr);

    // ── 使用者預設值（QSettings）────────────────────────────────────────────
    void saveAsDefault() const;                 ///< tclId 與範圍不存（屬於專案內容）
    static PlanProfileSettings loadDefault();
};

/**
 * 圖紙共用偏好（QSettings "Drawing/…"）：Drawing Sheet 對話框與平縱面圖冊設定對話框共用，
 * 任一邊改了，另一邊下次開啟（以及新建圖紙）就採用。欄位 <0 表示尚未儲存。
 */
struct DrawingPrefs {
    int    paperSize = -1, orientation = -1, unit = -1, frameTemplate = -1, projection = -1;
    double defaultScale = -1, lwThin = -1, lwMedium = -1, lwThick = -1;
};
DrawingPrefs loadDrawingPrefs();
void saveDrawingPrefs(const DrawingPrefs& p);     ///< 只寫入有效（>=0）的欄位

/** 線形資料表的可選欄位定義（kind：1＝平面、2＝縱面） */
struct PlanProfileTableColumn {
    QString id;        ///< 欄位代號（存檔用）
    QString label;     ///< 顯示名稱（可含 \n 換行）
    double  width = 10;///< 相對寬度
    bool    center = false;  ///< true＝置中，false＝靠右（數值）
    bool    between = false; ///< true＝點間資料（長度等，下移半格）；false＝跟著點
};
QVector<PlanProfileTableColumn> planProfileTableColumnDefs(int kind);
QStringList planProfileDefaultTableColumns(int kind);
/** 實際採用的欄位：設定值去除未知 id；空則用預設 */
QStringList planProfileEffectiveTableColumns(const PlanProfileSettings& s, int kind);

/** 簡圖：一個線形串（band）在本頁的線形項（依序）。 */
struct SchemaBand {
    QString     name;      ///< 線形串名稱（起始線形名）
    QStringList tclIds;    ///< 本頁此串所含的線形（1～2 項）
};

/** 簡圖：渡線（兩端接到兩個線形串）或支線（一端接到線形串）。 */
struct SchemaLink {
    QString name;
    int     kind = 2;          ///< 1＝渡線、2＝支線
    QString aTcl; double aCh = 0.0;   ///< 接到主線的位置（線形 id、該線形里程）
    QString bTcl; double bCh = 0.0;   ///< 渡線的另一端
    int     side = 1;          ///< 支線方向：+1＝行進方向左側（圖上方）、-1＝右側（圖下方）
    int     dir  = 1;          ///< 支線沿主線前進方向 +1／反向 -1（決定斜線偏向）
};

/** 平縱面圖層：掛在 DrawingSheet 上。 */
struct PlanProfileLayer {
    bool                enabled = false;
    bool                isIndex = false;   ///< true＝圖目錄圖紙（由 PPS 自動產生）
    int                 tableKind = 0;     ///< 線形資料表圖紙：0＝否 1＝平面 2＝縱面
    QString             tableTclId;        ///< （舊版）資料表所屬線形；新版改用 tableTclIds
    QStringList         tableTclIds;       ///< 資料表涵蓋的線形（依序接續列出，占滿一頁再接下一頁）
    int                 tablePage  = 1;    ///< 資料表第幾頁
    int                 tablePages = 1;    ///< 資料表共幾頁
    DepotView           depotView;         ///< tableKind==4（機廠平面圖）：本頁的矩形視圖
    QVector<SchemaBand> schemaBands;       ///< tableKind==3（簡圖）：本頁各線形串的線形項
    QVector<SchemaLink> schemaLinks;       ///< tableKind==3：本頁範圍內的渡線與支線
    PlanProfileSettings settings;       ///< 產生當下的設定快照
    double              startCh = 0.0;  ///< 本張負責的里程範圍 [m]
    double              endCh   = 0.0;
    int                 index   = 1;    ///< 第幾張（1 起算）
    int                 count   = 1;    ///< 共幾張
    QString             prevNo;         ///< 左側接圖：相鄰圖紙的圖號（空＝無）
    QString             nextNo;         ///< 右側接圖
    QString             groupId;        ///< 同一次產生的圖紙共用 id（＝圖冊 id）
    QVector<PlanProfileSegment> pieces; ///< 本張涵蓋的線形片段（依序；跨線形時有多段）
    QJsonObject         overrides;      ///< 局部設定：與圖冊（全域）設定不同的欄位

    QJsonObject toJson() const;
    bool        fromJson(const QJsonObject& j);
};

/** 版面（圖紙座標 mm）。產生器與繪圖共用，確保分段長度與實際可繪寬度一致。 */
struct PlanProfileLayout {
    QRectF area;           ///< 可用繪圖區（已避開標題欄）
    QRectF planRect;       ///< 平面圖帶
    QRectF profileRect;    ///< 縱斷面帶
    QRectF tableRect;      ///< 資料表（含左側標籤欄）
    double plotLeft  = 0;  ///< 框左緣
    double plotWidth = 0;  ///< 框寬度 [mm]
    double dataLeft  = 0;  ///< 資料起點（框內左側留邊界後）
    double dataWidth = 0;  ///< 資料可用寬度 [mm]（左右各留邊界）
    double rowH      = 7;
    int    rowCount  = 1;  ///< 資料表列數（含里程列）
    QVector<int>    rowKinds;  ///< 各列種類：0里程 1高程 2坡度 3曲線 4方位角
    QVector<double> rowHs;     ///< 各列高度 [mm]
};

/** paper：紙張實際尺寸 (mm，已含方向)。 */
PlanProfileLayout computePlanProfileLayout(const QSizeF& paper, const PlanProfileSettings& s,
                                           double planHeightRequest = 0.0);

/** 依設定與繪圖寬度算出實際每張長度 [m]（segmentLength>0 則取其值）。 */
double planProfileSegmentLength(const QSizeF& paper, const PlanProfileSettings& s);

} // namespace drawing
} // namespace aicad
