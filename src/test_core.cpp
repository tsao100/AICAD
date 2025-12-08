/**
 * @file test_core.cpp
 * @brief 核心系統測試程式
 * @author Jack
 * @date 2024-12-04
 * 
 * 編譯: g++ test_core.cpp -o test_core $(pkg-config --cflags --libs Qt5Core)
 */

#include <QCoreApplication>
#include <QDebug>
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/DocumentManager.h"

using namespace aicad::core;

// 測試物件
class TestReceiver : public QObject {
    Q_OBJECT
public:
    TestReceiver() {
        setObjectName("TestReceiver");
    }
    
    void onEvent(const QVariant& data) {
        qDebug() << "[TestReceiver] Received event with data:" << data;
    }
};

void testEventBus() {
    qDebug() << "\n=== Testing EventBus ===";
    
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();
    
    TestReceiver receiver;
    
    // 訂閱事件
    bus->subscribe(Events::FEATURE_CREATED, &receiver, 
        [](const QVariant& data) {
            qDebug() << "[Lambda] Feature created:" << data.toString();
        });
    
    // 發布事件
    bus->publish(Events::FEATURE_CREATED, "Sketch1");
    bus->publish(Events::FEATURE_CREATED, "Extrude1");
    
    // 檢查訂閱者數量
    qDebug() << "Subscribers for FEATURE_CREATED:" 
             << bus->subscriberCount(Events::FEATURE_CREATED);
    
    // 取消訂閱
    bus->unsubscribe(Events::FEATURE_CREATED, &receiver);
    qDebug() << "After unsubscribe:" 
             << bus->subscriberCount(Events::FEATURE_CREATED);
}

void testDocumentManager() {
    qDebug() << "\n=== Testing DocumentManager ===";
    
    Application* app = Application::instance();
    DocumentManager* mgr = app->documentManager();
    
    // 建立文件
    auto* doc1 = mgr->createDocument();
    qDebug() << "Created document:" << doc1->fileName();
    
    auto* doc2 = mgr->createDocument("MyProject");
    qDebug() << "Created document:" << doc2->fileName();
    
    // 檢查文件數量
    qDebug() << "Total documents:" << mgr->documentCount();
    
    // 取得當前文件
    auto* current = mgr->currentDocument();
    qDebug() << "Current document:" << (current ? current->fileName() : "None");
    
    // 尋找文件
    auto* found = mgr->findDocument("MyProject");
    qDebug() << "Found document:" << (found ? found->fileName() : "Not found");
    
    // 關閉文件
    mgr->closeDocument(doc1);
    qDebug() << "After closing, total documents:" << mgr->documentCount();
    
    // 關閉所有文件
    mgr->closeAll();
    qDebug() << "After closeAll, total documents:" << mgr->documentCount();
}

void testApplication() {
    qDebug() << "\n=== Testing Application ===";
    
    Application* app = Application::instance();
    
    // 檢查單例
    Application* app2 = Application::instance();
    qDebug() << "Singleton check:" << (app == app2 ? "PASS" : "FAIL");
    
    // 檢查版本資訊
    qDebug() << "Application name:" << app->applicationName();
    qDebug() << "Version:" << app->version();
    qDebug() << "Is initialized:" << app->isInitialized();
    
    // 檢查模組
    qDebug() << "EventBus:" << (app->eventBus() ? "OK" : "NULL");
    qDebug() << "DocumentManager:" << (app->documentManager() ? "OK" : "NULL");
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    qDebug() << "===========================================";
    qDebug() << "  AICAD Core System Test";
    qDebug() << "===========================================";
    
    // 初始化應用程式
    Application* aicadApp = Application::instance();
    if (!aicadApp->initialize()) {
        qCritical() << "Failed to initialize AICAD";
        return 1;
    }
    
    // 執行測試
    testApplication();
    testEventBus();
    testDocumentManager();
    
    // 關閉
    aicadApp->shutdown();
    
    qDebug() << "\n===========================================";
    qDebug() << "  All tests completed";
    qDebug() << "===========================================";
    
    return 0;
}

#include "test_core.moc"