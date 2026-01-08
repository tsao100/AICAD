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
