// pch.h — 預編譯標頭（僅 MSVC 啟用，見 AICAD.pro）
// 只放「幾乎每個 .cpp 都會用到、且很少改動」的 Qt／標準函式庫標頭。
// 刻意不放：Eigen、OCCT（只有少數檔案用到，放進來反而拖慢其他檔案）、ECL
// （LispEngine.h 對 Qt 巨集 slots/signals/emit 有專門處理，不要動它的 include 順序）。
#if defined(__cplusplus)

// 標準函式庫
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

// Qt Core
#include <QDebug>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QVector>

// Qt Widgets（UI 檔案大量使用）
#include <QAction>
#include <QMenu>
#include <QWidget>

#endif
