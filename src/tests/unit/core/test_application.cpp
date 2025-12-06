// tests/unit/core/test_application.cpp

#include <QtTest>
#include "core/Application.h"
#include "core/EventBus.h"

class TestApplication : public QObject {
    Q_OBJECT
    
private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    
    void testSingleton();
    void testInitialize();
    void testManagers();
    void testSettings();
    void testEventBus();
    void testVersion();
};

void TestApplication::initTestCase() {
    // 測試環境初始化
}

void TestApplication::cleanupTestCase() {
    // 清理測試環境
}

void TestApplication::testSingleton() {
    using namespace aicad::core;
    
    Application* app1 = Application::instance();
    Application* app2 = Application::instance();
    
    QVERIFY(app1 != nullptr);
    QCOMPARE(app1, app2);  // 同一個實例
}

void TestApplication::testInitialize() {
    using namespace aicad::core;
    
    Application* app = Application::instance();
    
    QVERIFY(!app->isInitialized());
    QVERIFY(app->initialize());
    QVERIFY(app->isInitialized());
    
    // 重複初始化應該回傳 true 但不做任何事
    QVERIFY(app->initialize());
}

void TestApplication::testManagers() {
    using namespace aicad::core;
    
    Application* app = Application::instance();
    app->initialize();
    
    // 檢查管理器是否已建立
    QVERIFY(app->documentManager() != nullptr);
    QVERIFY(app->commandManager() != nullptr);
    QVERIFY(app->viewManager() != nullptr);
    QVERIFY(app->eventBus() != nullptr);
    
    // Lisp 引擎可能未啟用
    // QVERIFY(app->lispEngine() != nullptr);
}

void TestApplication::testSettings() {
    using namespace aicad::core;
    
    Application* app = Application::instance();
    app->initialize();
    
    // 測試設定讀寫
    app->setSetting("test/key", "test_value");
    QCOMPARE(app->setting("test/key").toString(), QString("test_value"));
    
    // 測試預設值
    QCOMPARE(app->setting("nonexistent/key", "default").toString(), 
             QString("default"));
    
    // 測試信號
    QSignalSpy spy(app, &Application::settingChanged);
    app->setSetting("test/key2", 123);
    QCOMPARE(spy.count(), 1);
}

void TestApplication::testEventBus() {
    using namespace aicad::core;
    
    Application* app = Application::instance();
    app->initialize();
    
    EventBus* eventBus = app->eventBus();
    QVERIFY(eventBus != nullptr);
    
    // 測試事件發布訂閱
    bool eventReceived = false;
    QString receivedData;
    
    eventBus->subscribe("test.event", this, [&](const QVariant& data) {
        eventReceived = true;
        receivedData = data.toString();
    });
    
    eventBus->publish("test.event", QVariant("test_data"));
    
    QVERIFY(eventReceived);
    QCOMPARE(receivedData, QString("test_data"));
}

void TestApplication::testVersion() {
    using namespace aicad::core;
    
    QString version = Application::version();
    QVERIFY(!version.isEmpty());
    QVERIFY(version.contains("."));  // 格式如 "1.0.0"
    
    QString buildDate = Application::buildDate();
    QVERIFY(!buildDate.isEmpty());
    
    QString buildInfo = Application::buildInfo();
    QVERIFY(!buildInfo.isEmpty());
    QVERIFY(buildInfo.contains("Qt"));
}

QTEST_MAIN(TestApplication)
#include "test_application.moc"
