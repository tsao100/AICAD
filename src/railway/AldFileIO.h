/**
 * @file AldFileIO.h
 * @brief 讀取舊系統 .prj 專案索引檔與 .ALD (VB6 Put/Get 固定長度) 二進位線形檔。
 * @author AICAD Team
 * @date   2026-07
 *
 * 檔案格式
 * ────────
 *   .prj    純文字（CRLF 換行），每行一個 *H.ALD 檔名（不含路徑），例如：
 *             G107H.ALD
 *             G108H.ALD
 *           每一個 *H.ALD 代表一條完整線路（軌道）的水平線形；對應的垂直
 *           線形檔名為同前綴 + "V.ALD"（例如 G107H.ALD → G107V.ALD），若
 *           存在則一併載入。
 *
 *   *H.ALD  對應 VBA:
 *     Public Type Coordinates
 *         Easting  As String * 16
 *         Northing As String * 17
 *     End Type
 *     Public Type AlignmentData
 *         Plat               As String * 3
 *         UpDown             As String * 1
 *         TSC                As String * 2
 *         NE                 As Coordinates
 *         Chainage           As String * 15
 *         ContinuousChainage As String * 15
 *         Azimuth            As String * 13   ' "DDD-MM-SS.sss"
 *         length             As String * 15
 *         RadiusCurveType    As String * 8    ' "STRAIGHT" | 半徑數字 | 緩和曲線類型名稱
 *         CircularCurveNo    As String * 9
 *         Cant               As Double        ' 原生二進位 (little-endian)
 *         GaugeWidenning     As Double
 *         SpeedLimit         As Double
 *         Text1              As String * 25
 *         Text2              As String * 25
 *         Real1              As Double
 *         Real2              As Double
 *     End Type
 *   單筆記錄長度 = 204 bytes。
 *
 *   *V.ALD  對應 VBA:
 *     Public Type VerticalAlignment
 *         Plat     As String * 3
 *         UpDown   As String * 1
 *         Chainage As String * 15
 *         Elevation As String * 10
 *         Grade    As String * 10
 *         KValue   As String * 15
 *         eli      As String * 10   ' PVI 切線高程；僅豎曲線三聯記錄之中間點使用
 *         LVC      As String * 10
 *         mo       As String * 10   ' 中央縱距；僅豎曲線中間點使用
 *     End Type
 *   單筆記錄長度 = 84 bytes（全部為 ASCII 文字欄位，右側空白填滿）。
 *
 * 字串欄位一律為 ASCII、右側空白填滿；AlignmentData 中的 Cant / GaugeWidenning /
 * SpeedLimit / Real1 / Real2 為 VB6 Put 語句寫出的原生二進位 double（非文字），
 * 以小端序（little-endian）8 bytes 儲存。
 */
#pragma once

#include "RailwayAlignment.h"

#include <QString>
#include <QVector>

namespace aicad {
namespace railway {

class AldFileIO
{
public:
    /** .prj 檔案中的一筆項目：一個 *H.ALD 檔名，代表一條完整線路。 */
    struct PrjEntry {
        QString hFileName;  ///< 例如 "G107H.ALD"（不含路徑，保留原始大小寫）
        QString trackId;    ///< 去除結尾 "H.ALD" 後的線路代碼，例如 "G107"
    };

    /**
     * @brief 解析 .prj 純文字檔，依序回傳其中列出的 *H.ALD 項目。
     * 忽略空白行；非 "...H.ALD" 結尾的行仍會保留（trackId 退化為去除副檔名後的檔名）
     * 以避免漏掉例外命名的項目。
     */
    static QVector<PrjEntry> readPrj(const QString& prjFilePath,
                                      QString* errorMessage = nullptr);

    /** 讀取一個 *H.ALD 二進位檔，回傳依序排列的 AlignmentPoint 清單。 */
    static QVector<AlignmentPoint> readHorizontalALD(const QString& filePath,
                                                       QString* errorMessage = nullptr);

    /** 讀取一個 *V.ALD 二進位檔，回傳依序排列的 VerticalAlignmentPoint 清單。 */
    static QVector<VerticalAlignmentPoint> readVerticalALD(const QString& filePath,
                                                             QString* errorMessage = nullptr);

    /**
     * @brief 由 *H.ALD 檔名推導對應的 *V.ALD 檔名（結尾 "H.ALD" 大小寫不敏感比對，
     *        取代為與原檔名相同大小寫風格的 "V.ALD"）。
     * @return 對應檔名；若輸入檔名不以 "H.ALD" 結尾，回傳空字串。
     */
    static QString verticalFileNameFor(const QString& hFileName);

    /**
     * @brief 寫出 .prj 純文字檔（CRLF 換行），每行一個 *H.ALD 檔名。
     * @return 成功與否；失敗時 *errorMessage 說明原因。
     */
    static bool writePrj(const QString& prjFilePath,
                         const QVector<PrjEntry>& entries,
                         QString* errorMessage = nullptr);

    /**
     * @brief 將 AlignmentPoint 清單寫出為 *H.ALD 二進位檔（204 bytes/筆，
     *        欄位配置與 readHorizontalALD 完全對稱）。
     */
    static bool writeHorizontalALD(const QString& filePath,
                                    const QVector<AlignmentPoint>& points,
                                    QString* errorMessage = nullptr);

    /**
     * @brief 將 VerticalAlignmentPoint 清單寫出為 *V.ALD 二進位檔（84 bytes/筆，
     *        欄位配置與 readVerticalALD 完全對稱）。空清單會寫出空檔案。
     */
    static bool writeVerticalALD(const QString& filePath,
                                  const QVector<VerticalAlignmentPoint>& points,
                                  QString* errorMessage = nullptr);

    /** AlignmentData 記錄長度（bytes）。 */
    static constexpr int kHRecordSize = 204;
    /** VerticalAlignment 記錄長度（bytes）。 */
    static constexpr int kVRecordSize = 84;

private:
    static QString  trimField(const QByteArray& field);
    static double   parseAsciiDouble(const QByteArray& field);
    static double   parseAzimuthDMS(const QByteArray& field);
    static double   readLEDouble(const char* p);

    // ── write-side helpers ──────────────────────────────────────────────────
    static QByteArray packField(const QString& text, int width);
    static QByteArray packNumericField(double value, int width, int maxDecimals = 3);
    static QByteArray packAzimuthField(double radians, int width);
    static QByteArray packLEDouble(double v);
};

} // namespace railway
} // namespace aicad
