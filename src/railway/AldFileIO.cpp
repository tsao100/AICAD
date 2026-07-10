/**
 * @file AldFileIO.cpp
 * @brief Implementation of AldFileIO — see AldFileIO.h for file format details.
 */
#include "AldFileIO.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <cstring>
#include <cmath>

namespace aicad {
namespace railway {

// ============================================================================
//  Field helpers
// ============================================================================

QString AldFileIO::trimField(const QByteArray& field)
{
    // ALD 文字欄位為 ASCII，右側以空白填滿；部份舊資料在欄位中夾雜 NUL（例如
    // 記錄尾端未寫滿的區塊），一併視為填充字元去除。
    QByteArray b = field;
    while (!b.isEmpty() && (b.back() == ' ' || b.back() == '\0'))
        b.chop(1);
    int start = 0;
    while (start < b.size() && (b[start] == ' ' || b[start] == '\0'))
        ++start;
    return QString::fromLatin1(b.mid(start));
}

double AldFileIO::parseAsciiDouble(const QByteArray& field)
{
    const QString s = trimField(field);
    if (s.isEmpty())
        return 0.0;
    bool ok = false;
    const double v = s.toDouble(&ok);
    if (!ok) {
        qWarning() << "[AldFileIO] Failed to parse numeric field:" << s;
        return 0.0;
    }
    return v;
}

double AldFileIO::parseAzimuthDMS(const QByteArray& field)
{
    // 格式："DDD-MM-SS.sss"（度-分-秒），例如 "156-11-14.811"
    const QString s = trimField(field);
    if (s.isEmpty())
        return 0.0;

    static const QRegularExpression re(
        QStringLiteral("^(-?\\d+)-(\\d+)-(\\d+(?:\\.\\d+)?)$"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch()) {
        qWarning() << "[AldFileIO] Failed to parse azimuth field:" << s;
        return 0.0;
    }

    const double deg = m.captured(1).toDouble();
    const double min = m.captured(2).toDouble();
    const double sec = m.captured(3).toDouble();
    const double sign = (deg < 0.0) ? -1.0 : 1.0;
    const double decimalDeg = sign * (std::abs(deg) + min / 60.0 + sec / 3600.0);
    return decimalDeg * M_PI / 180.0;
}

double AldFileIO::readLEDouble(const char* p)
{
    // VB6 Put 語句以原生記憶體位元組寫出 Double；x86/x64 為 little-endian，
    // 與 MSVC/GCC 執行平台一致，直接位元複製即可。
    double v = 0.0;
    std::memcpy(&v, p, sizeof(double));
    return v;
}

// ============================================================================
//  Write-side field helpers
// ============================================================================

QByteArray AldFileIO::packField(const QString& text, int width)
{
    QByteArray b = text.toLatin1();
    if (b.size() > width)
        b = b.left(width);
    if (b.size() < width)
        b.append(width - b.size(), ' ');
    return b;
}

QByteArray AldFileIO::packNumericField(double value, int width, int maxDecimals)
{
    for (int dec = maxDecimals; dec >= 0; --dec) {
        const QString s = QString::number(value, 'f', dec);
        if (s.size() <= width)
            return packField(s, width);
    }
    // 極端情況（數值本身超出欄寬）：以整數截斷，避免記錄長度錯位。
    const QString s = QString::number(value, 'f', 0);
    return packField(s.left(width), width);
}

QByteArray AldFileIO::packAzimuthField(double radians, int width)
{
    // 反算 "DDD-MM-SS.sss"（度-分-秒），與 parseAzimuthDMS 對稱。
    double deg = radians * 180.0 / M_PI;
    deg = std::fmod(deg, 360.0);
    if (deg < 0.0)
        deg += 360.0;

    int dd = static_cast<int>(deg);
    double remMin = (deg - dd) * 60.0;
    int mm = static_cast<int>(remMin);
    double ss = (remMin - mm) * 60.0;

    QString secStr = QString::number(ss, 'f', 3);
    if (secStr.toDouble() >= 60.0) {
        ss = 0.0;
        ++mm;
        secStr = QString::number(ss, 'f', 3);
    }
    if (mm >= 60) {
        mm -= 60;
        ++dd;
    }
    if (dd >= 360)
        dd -= 360;

    // 秒欄位固定 6 字元寬（"SS.sss"），前端補零。
    secStr = secStr.rightJustified(6, QLatin1Char('0'));

    const QString s = QStringLiteral("%1-%2-%3")
        .arg(dd, 3, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(secStr);

    return packField(s, width);
}

QByteArray AldFileIO::packLEDouble(double v)
{
    QByteArray b(sizeof(double), '\0');
    std::memcpy(b.data(), &v, sizeof(double));
    return b;
}

// ============================================================================
//  readPrj
// ============================================================================

QVector<AldFileIO::PrjEntry> AldFileIO::readPrj(const QString& prjFilePath,
                                                 QString* errorMessage)
{
    QVector<PrjEntry> result;

    QFile f(prjFilePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("無法開啟 PRJ 檔: %1").arg(prjFilePath);
        return result;
    }

    QTextStream ts(&f);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    ts.setCodec("UTF-8");
#endif

    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.isEmpty())
            continue;

        PrjEntry entry;
        entry.hFileName = line;

        // trackId = 去除副檔名 ".ALD" 後，再去除結尾的 "H"（水平線形代碼）
        QString stem = line;
        if (stem.endsWith(QStringLiteral(".ALD"), Qt::CaseInsensitive))
            stem.chop(4);
        if (stem.endsWith(QLatin1Char('H'), Qt::CaseInsensitive))
            stem.chop(1);
        entry.trackId = stem;

        result.append(entry);
    }

    return result;
}

// ============================================================================
//  verticalFileNameFor
// ============================================================================

QString AldFileIO::verticalFileNameFor(const QString& hFileName)
{
    // 結尾必須為 "H.ALD"（大小寫不敏感）；取代為 "V.ALD"，其餘保留原檔名字元。
    if (!hFileName.endsWith(QStringLiteral("H.ALD"), Qt::CaseInsensitive))
        return QString();

    QString base = hFileName;
    base.chop(5);  // 去掉 "H.ALD" (5 字元)

    // 副檔名沿用原檔名最後 4 個字元的大小寫風格（".ALD" 或 ".ald"）。
    const QString ext = hFileName.right(4);
    return base + QStringLiteral("V") + ext;
}

// ============================================================================
//  writePrj
// ============================================================================

bool AldFileIO::writePrj(const QString& prjFilePath,
                          const QVector<PrjEntry>& entries,
                          QString* errorMessage)
{
    QFile f(prjFilePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("無法開啟 PRJ 檔以寫入: %1").arg(prjFilePath);
        return false;
    }

    QTextStream ts(&f);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    ts.setCodec("UTF-8");
#endif

    for (const PrjEntry& entry : entries)
        ts << entry.hFileName << "\r\n";

    ts.flush();
    if (f.error() != QFile::NoError) {
        if (errorMessage)
            *errorMessage = QStringLiteral("寫入 PRJ 檔失敗: %1 (%2)")
                .arg(prjFilePath, f.errorString());
        return false;
    }
    return true;
}

// ============================================================================
//  readHorizontalALD
// ============================================================================

QVector<AlignmentPoint> AldFileIO::readHorizontalALD(const QString& filePath,
                                                       QString* errorMessage)
{
    QVector<AlignmentPoint> pts;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("無法開啟水平線形檔: %1").arg(filePath);
        return pts;
    }

    const QByteArray data = f.readAll();
    const int n = data.size() / kHRecordSize;
    if (n <= 0 || data.size() % kHRecordSize != 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral(
                "檔案長度不符 AlignmentData 記錄大小 (%1 bytes)：%2 (%3 bytes)")
                .arg(kHRecordSize).arg(filePath).arg(data.size());
        qWarning() << "[AldFileIO] readHorizontalALD size mismatch:" << filePath
                   << "size=" << data.size();
        // 仍嘗試讀取完整記錄的部份（容錯：忽略末端不足一筆的殘餘 bytes）
    }

    pts.reserve(n);

    // 已知的緩和曲線（transition curve）類型關鍵字；其餘非數字、非 "STRAIGHT"
    // 的內容一律視為 curveType 文字（供 AlignmentElementFactory 判斷緩和曲線
    // 子類型，未匹配時其自身會退回 Clothoid 預設值，故無需在此嚴格驗證）。
    for (int i = 0; i < n; ++i) {
        const char* rec = data.constData() + static_cast<qint64>(i) * kHRecordSize;

        AlignmentPoint pt;
        pt.plat            = trimField(QByteArray(rec + 0, 3));
        pt.upDown          = trimField(QByteArray(rec + 3, 1));
        pt.tsc             = trimField(QByteArray(rec + 4, 2));
        pt.easting         = parseAsciiDouble(QByteArray(rec + 6, 16));
        pt.northing        = parseAsciiDouble(QByteArray(rec + 22, 17));
        pt.chainage        = parseAsciiDouble(QByteArray(rec + 39, 15));
        pt.contChainage    = parseAsciiDouble(QByteArray(rec + 54, 15));
        pt.azimuth         = parseAzimuthDMS (QByteArray(rec + 69, 13));
        pt.length          = parseAsciiDouble(QByteArray(rec + 82, 15));

        const QString rct  = trimField(QByteArray(rec + 97, 8));
        if (rct.isEmpty() || rct.compare(QStringLiteral("STRAIGHT"), Qt::CaseInsensitive) == 0) {
            pt.radius    = 0.0;
            pt.curveType.clear();
        } else {
            bool ok = false;
            const double r = rct.toDouble(&ok);
            if (ok) {
                pt.radius = std::abs(r);
                pt.curveType.clear();
            } else {
                // 緩和曲線類型名稱（CLOTHOID/HALFSINE/PARABOLA/CUBICJPN/CUBICECI/
                // SPIRAL…）或個別檔案中偶見的資料訛誤字串。對於直線（TT）與圓弧
                // （TC/CT）元素此欄位並不影響幾何計算，僅緩和曲線（TS/ST/CS/SC）
                // 才會實際使用 curveType 判斷子類型。
                pt.curveType = rct.toUpper();
                pt.radius    = 0.0;
            }
        }

        pt.circularCurveNo = trimField(QByteArray(rec + 105, 9));
        pt.cant            = readLEDouble(rec + 114);
        pt.gaugeWidening   = readLEDouble(rec + 122);
        pt.speedLimit      = readLEDouble(rec + 130);
        pt.text1           = trimField(QByteArray(rec + 138, 25));
        pt.text2           = trimField(QByteArray(rec + 163, 25));
        pt.real1           = readLEDouble(rec + 188);
        pt.real2           = readLEDouble(rec + 196);

        pts.append(pt);
    }

    return pts;
}

// ============================================================================
//  writeHorizontalALD
// ============================================================================

bool AldFileIO::writeHorizontalALD(const QString& filePath,
                                    const QVector<AlignmentPoint>& points,
                                    QString* errorMessage)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("無法開啟水平線形檔以寫入: %1").arg(filePath);
        return false;
    }

    QByteArray data;
    data.reserve(points.size() * kHRecordSize);

    for (const AlignmentPoint& pt : points) {
        data += packField(pt.plat, 3);
        data += packField(pt.upDown, 1);
        data += packField(pt.tsc, 2);
        data += packNumericField(pt.easting, 16);
        data += packNumericField(pt.northing, 17);
        data += packNumericField(pt.chainage, 15);
        data += packNumericField(pt.contChainage, 15);
        data += packAzimuthField(pt.azimuth, 13);
        data += packNumericField(pt.length, 15);

        // RadiusCurveType (8 bytes): 直線 → "STRAIGHT"；緩和曲線 → curveType 文字
        // （CLOTHOID/HALFSINE/PARABOLA/CUBICJPN/CUBICECI 皆恰為 8 字元）；
        // 圓弧 → 半徑數字文字。
        if (!pt.curveType.isEmpty())
            data += packField(pt.curveType, 8);
        else if (pt.radius != 0.0)
            data += packNumericField(std::abs(pt.radius), 8, 3);
        else
            data += packField(QStringLiteral("STRAIGHT"), 8);

        data += packField(pt.circularCurveNo, 9);
        data += packLEDouble(pt.cant);
        data += packLEDouble(pt.gaugeWidening);
        data += packLEDouble(pt.speedLimit);
        data += packField(pt.text1, 25);
        data += packField(pt.text2, 25);
        data += packLEDouble(pt.real1);
        data += packLEDouble(pt.real2);
    }

    const qint64 written = f.write(data);
    if (written != data.size()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("寫入水平線形檔失敗: %1 (%2)")
                .arg(filePath, f.errorString());
        return false;
    }
    return true;
}

// ============================================================================
//  readVerticalALD
// ============================================================================

QVector<VerticalAlignmentPoint> AldFileIO::readVerticalALD(const QString& filePath,
                                                             QString* errorMessage)
{
    QVector<VerticalAlignmentPoint> pts;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("無法開啟垂直線形檔: %1").arg(filePath);
        return pts;
    }

    const QByteArray data = f.readAll();
    if (data.isEmpty())
        return pts;  // 空檔（例如樣本中的 X101V.ALD）：視為無豎向資料，非錯誤

    const int n = data.size() / kVRecordSize;
    if (n <= 0 || data.size() % kVRecordSize != 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral(
                "檔案長度不符 VerticalAlignment 記錄大小 (%1 bytes)：%2 (%3 bytes)")
                .arg(kVRecordSize).arg(filePath).arg(data.size());
        qWarning() << "[AldFileIO] readVerticalALD size mismatch:" << filePath
                   << "size=" << data.size();
    }

    pts.reserve(n);

    for (int i = 0; i < n; ++i) {
        const char* rec = data.constData() + static_cast<qint64>(i) * kVRecordSize;

        VerticalAlignmentPoint pt;
        // plat/upDown 目前 VerticalAlignmentPoint 未提供對應成員以外的欄位，
        // 但結構中已有 plat/upDown，直接映射。
        pt.plat          = trimField(QByteArray(rec + 0, 3));
        pt.upDown        = trimField(QByteArray(rec + 3, 1));
        pt.chainage      = parseAsciiDouble(QByteArray(rec + 4,  15));
        pt.elevation     = parseAsciiDouble(QByteArray(rec + 19, 10));
        pt.grade         = parseAsciiDouble(QByteArray(rec + 29, 10));
        pt.kValue        = parseAsciiDouble(QByteArray(rec + 39, 15));
        pt.pviElevation  = parseAsciiDouble(QByteArray(rec + 54, 10));
        pt.lvc           = parseAsciiDouble(QByteArray(rec + 64, 10));
        pt.mo            = parseAsciiDouble(QByteArray(rec + 74, 10));

        pts.append(pt);
    }

    return pts;
}

// ============================================================================
//  writeVerticalALD
// ============================================================================

bool AldFileIO::writeVerticalALD(const QString& filePath,
                                  const QVector<VerticalAlignmentPoint>& points,
                                  QString* errorMessage)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("無法開啟垂直線形檔以寫入: %1").arg(filePath);
        return false;
    }

    // 空清單 → 寫出空檔案（對稱於 readVerticalALD 對空檔的容錯處理，例如無豎向
    // 資料的線路）。
    if (points.isEmpty())
        return true;

    QByteArray data;
    data.reserve(points.size() * kVRecordSize);

    for (const VerticalAlignmentPoint& pt : points) {
        data += packField(pt.plat, 3);
        data += packField(pt.upDown, 1);
        data += packNumericField(pt.chainage, 15);
        data += packNumericField(pt.elevation, 10);
        data += packNumericField(pt.grade, 10);
        data += packNumericField(pt.kValue, 15);
        data += packNumericField(pt.pviElevation, 10);
        data += packNumericField(pt.lvc, 10);
        data += packNumericField(pt.mo, 10);
    }

    const qint64 written = f.write(data);
    if (written != data.size()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("寫入垂直線形檔失敗: %1 (%2)")
                .arg(filePath, f.errorString());
        return false;
    }
    return true;
}

} // namespace railway
} // namespace aicad
