/**
 * @file PlanProfileGenerator.h
 * @brief 平縱面圖冊（Book）：沿串接線形整串分段產生圖紙、封面、圖目錄；
 *        全域（圖冊）／局部（單張）設定的套用與重算
 *
 * 圖冊 = 封面(Cover) + 圖目錄(Index，獨立圖紙) + 平縱面圖若干張。
 * 全域設定存在圖冊（DrawingBook::settings），各張圖有自己的快照 layer.settings
 * （＝全域設定疊上該張的局部覆寫 layer.overrides）。
 */
#pragma once

#include "PlanProfileSettings.h"
#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;

namespace aicad {
namespace cad { class Document; }
namespace railway { class TrackCenterLine; }
namespace drawing {

class DrawingSheetManager;
class DrawingSheet;

/**
 * 從 startId 開始，依「終點接到另一條線形起點」一路往後找，直到沒有後續線形。
 * 「接」的判定：E,N 距離 <= tolXY，且（兩線形皆有縱斷面時）高程差 <= tolZ。
 * 回傳依序的 TCL id（含 startId）。
 */
QStringList planProfileChain(cad::Document* doc, const QString& startId,
                             double tolXY, double tolZ, bool follow,
                             const QStringList& noChain = QStringList());

/**
 * 兩條起始線形（各自的串接）依序展開，並以「整串全長」分段。
 * 回傳：每個元素＝一張圖紙涵蓋的線形片段（跨線形時含多段）。失敗回傳空並填 error。
 */
QVector<QVector<PlanProfileSegment>> planProfileSegments(cad::Document* doc,
                                                         const PlanProfileSettings& s,
                                                         QString* error = nullptr);

/** 一張圖紙片段的總長度 [m] */
double planProfilePiecesLength(const QVector<PlanProfileSegment>& pieces);

/** 連續里程連動的結果（每條被連動到的線形一筆） */
struct ContChainageUpdate {
    QString tclId, name;
    double  oldStart = 0.0;   ///< 連動前起點連續里程
    double  newStart = 0.0;   ///< 連動後起點連續里程
    double  newEnd   = 0.0;   ///< 連動後終點連續里程
    QString parentId;         ///< 由哪一條線形連動而來（src 的直接鄰居為 src）
    QString parentName;
    QString link;             ///< 接法：終點接起點／起點接終點／起點對起點／終點對終點
    bool    branch   = false; ///< true＝分支：同一端點已有別的線形作為主串延續，這條是額外接上的
    int     depth    = 1;     ///< 距 src 幾層
    int     dir      = 1;     ///< 連動後的增減方向（+1 隨里程增加／−1 隨里程減少）
    bool    changed  = false; ///< false＝本來就一致，未更動
};

/**
 * 連續里程連動：src 的連續里程被使用者改過之後，把與它串接（E,N,Z 接近，且未標「單獨」）的線形
 * 依 src 重新累加（減）。四種接法都處理（同一實體點連續里程相同）：終點接起點、起點接終點、起點對起點、終點對終點。
 * 同一端點接有多條線形（有意重複／分岐）時一併連動；採廣度優先，每條線形只算一次。
 * srcContStart／srcContEnd 為 src 起點／終點目前的連續里程，srcDir 為其增減方向（+1／−1）；
 * 整串線形增減方向保持一致（沿同一軌道方向相接者同向，起點對起點／終點對終點者因里程相反而反向）。src 本身標「單獨」時不連動。
 * @return 被連動到的線形清單（不含 src）
 */
QVector<ContChainageUpdate> planProfilePropagateContChainage(cad::Document* doc, railway::TrackCenterLine* src,
                                                              double srcContStart, double srcContEnd, int srcDir,
                                                              double tolXY = 2.0, double tolZ = 1.0);

/**
 * 依設定產生圖冊。同名圖冊已存在且 s.replaceExisting 時，更新該圖冊。
 * @return 平縱面圖張數（0＝失敗，原因在 error）
 */
int generatePlanProfileSheets(DrawingSheetManager* mgr, cad::Document* doc,
                              const PlanProfileSettings& s, QString* error = nullptr);

/** 以新的全域設定更新（重算）整個圖冊：已有的圖原地更新並保留各圖的局部設定。 */
bool updatePlanProfileBook(DrawingSheetManager* mgr, cad::Document* doc,
                           const QString& bookId, const PlanProfileSettings& global,
                           QString* error = nullptr);

/** 只更新機廠矩形視圖：寫入圖冊設定、僅重建機廠平面圖頁與圖目錄（不重算平縱面圖／資料表／簡圖，快）。 */
bool updatePlanProfileDepotViews(DrawingSheetManager* mgr, cad::Document* doc,
                                 const QString& bookId, const QVector<DepotView>& views,
                                 QString* error = nullptr);

/** 單張圖的局部設定：只保存與全域不同的欄位，並重算該張。 */
bool applyPlanProfileSheetSettings(DrawingSheetManager* mgr, cad::Document* doc,
                                   DrawingSheet* sheet, const PlanProfileSettings& edited);

/** 清除單張圖的局部設定（回到全域）。 */
void clearPlanProfileSheetOverrides(DrawingSheetManager* mgr, cad::Document* doc, DrawingSheet* sheet);

/** 局部設定不可覆寫的欄位（分段、版面寬度、圖冊層級者） */
bool planProfileKeyOverridable(const QString& key);

/** 依管理器中目前的圖紙順序，重新編號圖冊內的平縱面圖（圖號、頁次、圖名）。 */
void renumberPlanProfileSheets(DrawingSheetManager* mgr, cad::Document* doc, const QString& bookId);

/** 依目前順序重建圖冊的圖目錄圖紙（獨立圖紙，既有的就地更新）。 */
void rebuildPlanProfileIndex(DrawingSheetManager* mgr, cad::Document* doc, const QString& bookId);

/** 整理圖紙順序：各圖冊依 封面→圖目錄→其餘，其後為不屬於圖冊的圖紙。 */
void normalizeSheetOrder(DrawingSheetManager* mgr);

/** 找圖紙所屬的平縱面圖設定（沒有則 nullptr）。 */
const PlanProfileSettings* planProfileSettingsOf(const DrawingSheet* sheet);

/** 圖名／圖號範本展開：{name} {n} {N} {start} {end} */
QString planProfileExpand(QString tpl, const QString& name, int n, int N,
                          const QString& a, const QString& b);

} // namespace drawing
} // namespace aicad
