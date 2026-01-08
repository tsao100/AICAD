/**
 * @file test_integration.cpp
 * @brief 階段 1 + 階段 2 整合測試
 * @author AICAD Team
 * @date 2025-01-08
 */

#include <QCoreApplication>
#include <QDebug>

// 階段 1: 核心系統
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/DocumentManager.h"

// 階段 2: CAD 模組
#include "cad/Plane.h"
#include "cad/Feature.h"
#include "cad/Sketch.h"
#include "cad/Document.h"
#include "cad/Extrude.h"

// 階段 3: 幾何建構
#include "geometry/GeometryBuilder.h"

using namespace aicad;

void testCoreSystem() {
    qDebug() << "\n=== Testing Core System (Stage 1) ===";
    
    // 初始化應用程式
    core::Application* app = core::Application::instance();
    
    if (!app->initialize()) {
        qCritical() << "Failed to initialize application!";
        return;
    }
    
    qDebug() << "Application:" << app->applicationName();
    qDebug() << "Version:" << app->version();
    qDebug() << "Initialized:" << app->isInitialized();
    
    // 測試 EventBus
    core::EventBus* eventBus = app->eventBus();
    if (eventBus) {
        qDebug() << "EventBus: OK";
        
        // 訂閱事件
        QObject context;
        eventBus->subscribe(core::Events::DOCUMENT_CREATED, &context,
            [](const QVariant& data) {
                qDebug() << "  [Event] Document created:" << data.toString();
            });
    }
    
    // 測試 DocumentManager
    core::DocumentManager* docMgr = app->documentManager();
    if (docMgr) {
        qDebug() << "DocumentManager: OK";
        qDebug() << "Document count:" << docMgr->documentCount();
    }
}

void testCADModule() {
    qDebug() << "\n=== Testing CAD Module (Stage 2) ===";
    
    core::Application* app = core::Application::instance();
    core::DocumentManager* docMgr = app->documentManager();
    
    // 建立文件
    cad::Document* doc = docMgr->createDocument("TestProject.aicad");
    qDebug() << "Created document:" << doc->fileName();
    
    // 建立草圖
    cad::Sketch* sketch1 = doc->createSketch(cad::Plane::xy(), "Sketch 1");
    qDebug() << "Created sketch:" << sketch1->name() 
             << "on" << sketch1->plane().displayName();
    
    // 加入矩形
    sketch1->addRectangle(QVector2D(0, 0), QVector2D(100, 50));
    sketch1->rebuild();
    qDebug() << "Added rectangle, has valid shape:" << sketch1->hasValidShape();
    
    // 建立擠出
    cad::Extrude* extrude = doc->createExtrude(sketch1, 25.0);
    qDebug() << "Created extrude:" << extrude->name();
    extrude->rebuild();
    qDebug() << "Extrude rebuilt, has valid shape:" << extrude->hasValidShape();
    
    // 建立第二個草圖
    cad::Sketch* sketch2 = doc->createSketch(cad::Plane::xz(), "Sketch 2");
    sketch2->addCircle(QVector2D(0, 0), 25);
    sketch2->rebuild();
    qDebug() << "Created second sketch with circle";
    
    // 檢查文件狀態
    qDebug() << "Document features:" << doc->featureCount();
    qDebug() << "Document modified:" << doc->isModified();
    
    // 列出所有特徵
    qDebug() << "Features in document:";
    for (cad::Feature* feature : doc->features()) {
        qDebug() << "  -" << feature->name() 
                 << "(" << feature->typeString() << ")"
                 << "visible:" << feature->isVisible()
                 << "valid:" << feature->hasValidShape();
    }
}

void testDocumentOperations() {
    qDebug() << "\n=== Testing Document Operations ===";
    
    core::Application* app = core::Application::instance();
    core::DocumentManager* docMgr = app->documentManager();
    
    // 建立文件並儲存
    cad::Document* doc = docMgr->createDocument("SaveTest.aicad");
    cad::Sketch* sketch = doc->createSketch(cad::Plane::xy());
    sketch->addLine(QVector2D(0, 0), QVector2D(100, 100));
    sketch->addLine(QVector2D(100, 100), QVector2D(200, 0));
    sketch->rebuild();
    
    bool saved = doc->save("test_save.json");
    qDebug() << "Save result:" << (saved ? "SUCCESS" : "FAILED");
    
    // 開啟文件
    cad::Document* loadedDoc = docMgr->openDocument("test_save.json");
    if (loadedDoc) {
        qDebug() << "Loaded document:" << loadedDoc->fileName();
        qDebug() << "Loaded features:" << loadedDoc->featureCount();
        
        // 驗證特徵
        for (cad::Feature* feature : loadedDoc->features()) {
            qDebug() << "  Loaded feature:" << feature->name();
        }
    } else {
        qDebug() << "Failed to load document";
    }
}

void testEventBusIntegration() {
    qDebug() << "\n=== Testing EventBus Integration ===";
    
    core::Application* app = core::Application::instance();
    core::EventBus* eventBus = app->eventBus();
    core::DocumentManager* docMgr = app->documentManager();
    
    // 訂閱多個事件
    QObject context;
    int eventCount = 0;
    
    eventBus->subscribe(core::Events::DOCUMENT_CREATED, &context,
        [&eventCount](const QVariant& data) {
            qDebug() << "  [Event] DOCUMENT_CREATED:" << data.toString();
            eventCount++;
        });
    
    eventBus->subscribe(core::Events::FEATURE_CREATED, &context,
        [&eventCount](const QVariant& data) {
            qDebug() << "  [Event] FEATURE_CREATED:" << data.toString();
            eventCount++;
        });
    
    // 觸發事件
    cad::Document* doc = docMgr->createDocument("EventTest.aicad");
    
    // 手動發布特徵事件（未來會由 Feature 自動發布）
    eventBus->publish(core::Events::FEATURE_CREATED, "Sketch 1");
    
    qDebug() << "Total events received:" << eventCount;
}

void testMultipleDocuments() {
    qDebug() << "\n=== Testing Multiple Documents ===";
    
    core::Application* app = core::Application::instance();
    core::DocumentManager* docMgr = app->documentManager();
    
    // 建立多個文件
    cad::Document* doc1 = docMgr->createDocument("Part1.aicad");
    cad::Document* doc2 = docMgr->createDocument("Part2.aicad");
    cad::Document* doc3 = docMgr->createDocument("Assembly.aicad");
    
    // 在每個文件中建立特徵
    doc1->createSketch(cad::Plane::xy())->addRectangle(
        QVector2D(0, 0), QVector2D(50, 50));
    
    doc2->createSketch(cad::Plane::xz())->addCircle(
        QVector2D(0, 0), 25);
    
    doc3->createSketch(cad::Plane::yz())->addLine(
        QVector2D(0, 0), QVector2D(100, 100));
    
    qDebug() << "Total documents:" << docMgr->documentCount();
    qDebug() << "Current document:" << docMgr->currentDocument()->fileName();
    
    // 列出所有文件
    qDebug() << "All documents:";
    for (cad::Document* doc : docMgr->documents()) {
        qDebug() << "  -" << doc->fileName() 
                 << "features:" << doc->featureCount()
                 << "modified:" << doc->isModified();
    }
    
    // 測試切換當前文件
    docMgr->setCurrentDocument(doc1);
    qDebug() << "Switched to:" << docMgr->currentDocument()->fileName();
    
    // 測試關閉文件
    docMgr->closeDocument(doc2, true);
    qDebug() << "After closing doc2, total:" << docMgr->documentCount();
}

void testGeometryBuilder() {
    qDebug() << "\n=== Testing GeometryBuilder (Stage 3) ===";
    
    using namespace geometry;
    
    // 測試 Wire 建立
    QVector<QVector2D> points;
    points << QVector2D(0, 0) << QVector2D(100, 0)
           << QVector2D(100, 50) << QVector2D(0, 50)
           << QVector2D(0, 0);
    
    auto wireResult = GeometryBuilder::makeWire(points, cad::Plane::xy(), true);
    qDebug() << "Wire creation:" << (wireResult ? "SUCCESS" : "FAILED");
    if (!wireResult) {
        qDebug() << "Error:" << wireResult.errorMessage;
    }
    
    // 測試 Face 建立
    if (wireResult) {
        TopoDS_Wire wire = TopoDS::Wire(wireResult.shape);
        auto faceResult = GeometryBuilder::makeFace(wire);
        qDebug() << "Face creation:" << (faceResult ? "SUCCESS" : "FAILED");
        
        // 測試擠出
        if (faceResult) {
            TopoDS_Face face = TopoDS::Face(faceResult.shape);
            gp_Vec direction(0, 0, 25);
            auto solidResult = GeometryBuilder::extrude(face, direction);
            qDebug() << "Extrude:" << (solidResult ? "SUCCESS" : "FAILED");
            
            if (solidResult) {
                // 測試包圍盒
                double xmin, ymin, zmin, xmax, ymax, zmax;
                GeometryBuilder::getBoundingBox(solidResult.shape,
                                               xmin, ymin, zmin,
                                               xmax, ymax, zmax);
                qDebug() << "Bounding box:"
                         << QString("(%1,%2,%3) to (%4,%5,%6)")
                            .arg(xmin).arg(ymin).arg(zmin)
                            .arg(xmax).arg(ymax).arg(zmax);
            }
        }
    }
    
    // 測試布林運算
    qDebug() << "Testing boolean operations...";
    
    // 建立兩個立方體
    QVector<QVector2D> box1;
    box1 << QVector2D(0, 0) << QVector2D(50, 0)
         << QVector2D(50, 50) << QVector2D(0, 50)
         << QVector2D(0, 0);
    
    auto wire1 = GeometryBuilder::makeWire(box1, cad::Plane::xy(), true);
    if (wire1) {
        auto face1 = GeometryBuilder::makeFace(TopoDS::Wire(wire1.shape));
        if (face1) {
            auto solid1 = GeometryBuilder::extrude(
                TopoDS::Face(face1.shape), gp_Vec(0, 0, 50));
            
            // 建立第二個立方體（偏移）
            QVector<QVector2D> box2;
            box2 << QVector2D(25, 25) << QVector2D(75, 25)
                 << QVector2D(75, 75) << QVector2D(25, 75)
                 << QVector2D(25, 25);
            
            auto wire2 = GeometryBuilder::makeWire(box2, cad::Plane::xy(), true);
            if (wire2) {
                auto face2 = GeometryBuilder::makeFace(TopoDS::Wire(wire2.shape));
                if (face2) {
                    auto solid2 = GeometryBuilder::extrude(
                        TopoDS::Face(face2.shape), gp_Vec(0, 0, 50));
                    
                    if (solid1 && solid2) {
                        // 聯集
                        auto unionResult = GeometryBuilder::unite(
                            solid1.shape, solid2.shape);
                        qDebug() << "Union:" << (unionResult ? "SUCCESS" : "FAILED");
                        
                        // 差集
                        auto cutResult = GeometryBuilder::cut(
                            solid1.shape, solid2.shape);
                        qDebug() << "Cut:" << (cutResult ? "SUCCESS" : "FAILED");
                        
                        // 交集
                        auto commonResult = GeometryBuilder::common(
                            solid1.shape, solid2.shape);
                        qDebug() << "Common:" << (commonResult ? "SUCCESS" : "FAILED");
                    }
                }
            }
        }
    }
}

void testExtrudeFeature() {
    qDebug() << "\n=== Testing Extrude Feature ===";
    
    core::Application* app = core::Application::instance();
    core::DocumentManager* docMgr = app->documentManager();
    
    // 建立文件
    cad::Document* doc = docMgr->createDocument("ExtrudeTest.aicad");
    
    // 建立草圖
    cad::Sketch* sketch = doc->createSketch(cad::Plane::xy(), "Base Profile");
    sketch->addRectangle(QVector2D(0, 0), QVector2D(80, 40));
    sketch->rebuild();
    
    qDebug() << "Sketch created and rebuilt";
    
    // 建立擠出
    cad::Extrude* extrude = doc->createExtrude(sketch, 30.0, "Main Body");
    qDebug() << "Extrude created, rebuilding...";
    
    bool success = extrude->rebuild();
    qDebug() << "Extrude rebuild:" << (success ? "SUCCESS" : "FAILED");
    
    if (success) {
        qDebug() << "Extrude has valid shape:" << extrude->hasValidShape();
        
        // 測試修改高度
        extrude->setHeight(50.0);
        qDebug() << "Changed height to 50.0, rebuilding...";
        extrude->rebuild();
        qDebug() << "After height change, valid:" << extrude->hasValidShape();
        
        // 測試反向
        extrude->setReversed(true);
        qDebug() << "Reversed direction, rebuilding...";
        extrude->rebuild();
        qDebug() << "After reverse, valid:" << extrude->hasValidShape();
    }
    
    // 檢查文件
    qDebug() << "Document has" << doc->featureCount() << "features";
    qDebug() << "Features:";
    for (cad::Feature* feature : doc->features()) {
        qDebug() << "  -" << feature->name()
                 << "(" << feature->typeString() << ")"
                 << "error:" << feature->hasError();
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    qDebug() << "========================================";
    qDebug() << "  AICAD Integration Test";
    qDebug() << "  Stage 1 + Stage 2";
    qDebug() << "========================================";
    
    try {
        testCoreSystem();
        testCADModule();
        testDocumentOperations();
        testEventBusIntegration();
        testMultipleDocuments();
        testGeometryBuilder();
        testExtrudeFeature();
        
        qDebug() << "\n========================================";
        qDebug() << "  All integration tests completed!";
        qDebug() << "========================================";
        
        // 關閉應用程式
        core::Application* coreApp = core::Application::instance();
        coreApp->shutdown();
        
    } catch (const std::exception& e) {
        qCritical() << "\nException:" << e.what();
        return 1;
    } catch (...) {
        qCritical() << "\nUnknown exception";
        return 1;
    }
    
    return 0;
}