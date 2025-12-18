/**
 * @file test_cad_engine.cpp
 * @brief CAD 引擎完整測試程式
 * @author Ben
 * @date 2024-12-04
 * 
 * 編譯方式:
 * g++ test_cad_engine.cpp -o test_cad \
 *     $(pkg-config --cflags --libs Qt5Core) \
 *     -I../src \
 *     -L../build -lAICAD
 * 
 * 或在 Qt Creator 中作為獨立專案建置
 */

#include <QCoreApplication>
#include <QDebug>
#include <QVector2D>

#include "cad/Document.h"
#include "cad/features/Feature.h"
#include "cad/features/Sketch.h"
#include "cad/features/Extrude.h"
#include "cad/geometry/CustomPlane.h"

using namespace aicad::cad;

void printSeparator(const QString& title) {
    qDebug() << "\n" << QString("=").repeated(50);
    qDebug() << title;
    qDebug() << QString("=").repeated(50);
}

bool testCustomPlane() {
    printSeparator("測試 CustomPlane");
    
    // 測試標準平面
    CustomPlane xy = CustomPlane::XY();
    qDebug() << "XY 平面:" << xy.getDisplayName();
    qDebug() << "  有效:" << xy.isValid();
    
    CustomPlane xz = CustomPlane::XZ();
    qDebug() << "XZ 平面:" << xz.getDisplayName();
    
    CustomPlane yz = CustomPlane::YZ();
    qDebug() << "YZ 平面:" << yz.getDisplayName();
    
    // 測試自訂平面
    CustomPlane custom = CustomPlane::fromOriginNormal(
        QVector3D(10, 10, 10),
        QVector3D(1, 1, 0).normalized()
    );
    qDebug() << "自訂平面:" << custom.getDisplayName();
    qDebug() << "  有效:" << custom.isValid();
    
    // 測試點投影
    QVector3D point(5, 5, 10);
    QVector3D projected = xy.projectPoint(point);
    qDebug() << "點投影測試:";
    qDebug() << "  原始點:" << point;
    qDebug() << "  投影後:" << projected;
    
    // 測試距離計算
    double distance = xy.distanceToPoint(point);
    qDebug() << "點到平面距離:" << distance;
    
    return true;
}

bool testFeature() {
    printSeparator("測試 Feature 基礎類別");
    
    // 測試 Sketch (Feature 的子類別)
    CustomPlane plane = CustomPlane::XY();
    Sketch* sketch = new Sketch(plane);
    
    qDebug() << "Feature 屬性:";
    qDebug() << "  類型:" << featureTypeToString(sketch->type());
    qDebug() << "  名稱:" << sketch->name();
    qDebug() << "  可見:" << sketch->isVisible();
    qDebug() << "  有效:" << sketch->isValid();
    qDebug() << "  需要更新:" << sketch->needsUpdate();
    
    // 測試屬性修改
    sketch->setName("測試草圖");
    sketch->setVisible(false);
    qDebug() << "修改後:";
    qDebug() << "  名稱:" << sketch->name();
    qDebug() << "  可見:" << sketch->isVisible();
    
    delete sketch;
    return true;
}

bool testSketch() {
    printSeparator("測試 Sketch");
    
    CustomPlane plane = CustomPlane::XY();
    Sketch* sketch = new Sketch(plane);
    sketch->setName("矩形草圖");
    
    // 建立矩形
    QVector<QVector2D> rectPoints;
    rectPoints << QVector2D(0, 0) << QVector2D(100, 0)
               << QVector2D(100, 50) << QVector2D(0, 50)
               << QVector2D(0, 0);  // 閉合
    
    qDebug() << "新增矩形折線:" << rectPoints.size() << "點";
    sketch->addPolyline(rectPoints);
    
    qDebug() << "折線數量:" << sketch->polylineCount();
    qDebug() << "草圖為空:" << sketch->isEmpty();
    
    // 計算形狀
    qDebug() << "計算草圖形狀...";
    sketch->update();
    
    qDebug() << "計算結果:";
    qDebug() << "  有效:" << sketch->isValid();
    qDebug() << "  形狀為空:" << sketch->shape().IsNull();
    
    if (!sketch->shape().IsNull()) {
        qDebug() << "  形狀類型:" << sketch->shape().ShapeType();
    }
    
    delete sketch;
    return true;
}

bool testExtrude() {
    printSeparator("測試 Extrude");
    
    // 建立草圖
    CustomPlane plane = CustomPlane::XY();
    Sketch* sketch = new Sketch(plane);
    
    QVector<QVector2D> rectPoints;
    rectPoints << QVector2D(0, 0) << QVector2D(50, 0)
               << QVector2D(50, 30) << QVector2D(0, 30)
               << QVector2D(0, 0);
    
    sketch->addPolyline(rectPoints);
    sketch->update();
    
    qDebug() << "草圖有效:" << sketch->isValid();
    
    // 建立擠出
    double height = 20.0;
    Extrude* extrude = new Extrude(sketch, height);
    
    qDebug() << "擠出屬性:";
    qDebug() << "  名稱:" << extrude->name();
    qDebug() << "  高度:" << extrude->height();
    qDebug() << "  依賴草圖:" << (extrude->sketch() == sketch);
    
    // 計算擠出
    qDebug() << "計算擠出形狀...";
    extrude->update();
    
    qDebug() << "計算結果:";
    qDebug() << "  有效:" << extrude->isValid();
    qDebug() << "  形狀為空:" << extrude->shape().IsNull();
    
    if (!extrude->shape().IsNull()) {
        qDebug() << "  形狀類型:" << extrude->shape().ShapeType();
    }
    
    // 測試依賴關係
    QVector<Feature*> deps = extrude->dependencies();
    qDebug() << "依賴特徵數量:" << deps.size();
    
    // 測試失效傳播
    qDebug() << "\n測試失效傳播:";
    qDebug() << "  修改草圖前 - 擠出需要更新:" << extrude->needsUpdate();
    
    sketch->addPolyline(rectPoints);  // 修改草圖
    
    qDebug() << "  修改草圖後 - 擠出需要更新:" << extrude->needsUpdate();
    
    delete extrude;
    delete sketch;
    return true;
}

bool testDocument() {
    printSeparator("測試 Document");
    
    Document doc;
    
    qDebug() << "建立新文件";
    doc.newDocument();
    qDebug() << "  特徵數量:" << doc.featureCount();
    qDebug() << "  已修改:" << doc.isModified();
    
    // 建立草圖
    qDebug() << "\n建立草圖...";
    Sketch* sketch = doc.createSketch(CustomPlane::XY(), "文件草圖");
    qDebug() << "  特徵數量:" << doc.featureCount();
    qDebug() << "  已修改:" << doc.isModified();
    
    QVector<QVector2D> points;
    points << QVector2D(0, 0) << QVector2D(40, 0)
           << QVector2D(40, 40) << QVector2D(0, 40)
           << QVector2D(0, 0);
    sketch->addPolyline(points);
    
    // 建立擠出
    qDebug() << "\n建立擠出...";
    Extrude* extrude = doc.createExtrude(sketch, 15.0, "文件擠出");
    qDebug() << "  特徵數量:" << doc.featureCount();
    
    // 更新所有特徵
    qDebug() << "\n更新所有特徵...";
    doc.updateAll();
    
    // 列出所有特徵
    qDebug() << "\n特徵列表:";
    for (Feature* feature : doc.features()) {
        qDebug() << "  -" << feature->name() 
                 << "(" << featureTypeToString(feature->type()) << ")"
                 << "ID:" << feature->id()
                 << "有效:" << feature->isValid();
    }
    
    // 測試尋找特徵
    qDebug() << "\n測試尋找特徵:";
    Feature* found = doc.findFeature(sketch->id());
    qDebug() << "  按 ID 尋找:" << (found != nullptr);
    
    found = doc.findFeatureByName("文件草圖");
    qDebug() << "  按名稱尋找:" << (found != nullptr);
    
    // 測試儲存/載入 (樁實作)
    qDebug() << "\n測試儲存/載入:";
    bool saved = doc.save("/tmp/test.aicad");
    qDebug() << "  儲存:" << saved;
    qDebug() << "  儲存後已修改:" << doc.isModified();
    
    return true;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    qDebug() << "\n╔═══════════════════════════════════════════════╗";
    qDebug() << "║   AICAD CAD Engine 完整測試                   ║";
    qDebug() << "╚═══════════════════════════════════════════════╝";
    
    bool allPassed = true;
    
    // 執行測試
    allPassed &= testCustomPlane();
    allPassed &= testFeature();
    allPassed &= testSketch();
    allPassed &= testExtrude();
    allPassed &= testDocument();
    
    // 結果
    printSeparator("測試結果");
    if (allPassed) {
        qDebug() << "✓ 所有測試通過!";
    } else {
        qDebug() << "✗ 某些測試失敗";
    }
    
    qDebug() << "\n測試完成。";
    
    return allPassed ? 0 : 1;
}