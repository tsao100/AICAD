// tests/unit/view/test_viewmanager.cpp

#include <QtTest>
#include "view/ViewManager.h"
#include "view/CadView.h"
#include "cad/Document.h"

using namespace aicad::view;
using namespace aicad::cad;

class TestViewManager : public QObject {
    Q_OBJECT
    
private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();
    
    // 測試案例
    void testCreateView();
    void testCloseView();
    void testActiveView();
    void testActiveDocument();
    void testMultipleViews();
    void testSignals();
    
private:
    ViewManager* m_viewManager;
};

void TestViewManager::initTestCase() {
    qDebug() << "=== ViewManager 測試開始 ===";
}

void TestViewManager::cleanupTestCase() {
    qDebug() << "=== ViewManager 測試完成 ===";
}

void TestViewManager::init() {
    m_viewManager = new ViewManager();
}

void TestViewManager::cleanup() {
    delete m_viewManager;
    m_viewManager = nullptr;
}

void TestViewManager::testCreateView() {
    // 測試建立視圖
    CadView* view = m_viewManager->createView();
    
    QVERIFY(view != nullptr);
    QCOMPARE(m_viewManager->views().size(), 1);
    QCOMPARE(m_viewManager->activeView(), view);
}

void TestViewManager::testCloseView() {
    // 建立視圖
    CadView* view1 = m_viewManager->createView();
    CadView* view2 = m_viewManager->createView();
    
    QCOMPARE(m_viewManager->views().size(), 2);
    
    // 關閉一個視圖
    m_viewManager->closeView(view1);
    
    QCOMPARE(m_viewManager->views().size(), 1);
    QCOMPARE(m_viewManager->activeView(), view2);
}

void TestViewManager::testActiveView() {
    CadView* view1 = m_viewManager->createView();
    CadView* view2 = m_viewManager->createView();
    
    // 預設活動視圖是第一個
    QCOMPARE(m_viewManager->activeView(), view1);
    
    // 切換活動視圖
    m_viewManager->setActiveView(view2);
    QCOMPARE(m_viewManager->activeView(), view2);
}

void TestViewManager::testActiveDocument() {
    Document* doc = new Document(this);
    
    // 設定活動文件
    m_viewManager->setActiveDocument(doc);
    QCOMPARE(m_viewManager->activeDocument(), doc);
    
    // 建立視圖應該自動設定文件
    CadView* view = m_viewManager->createView();
    QCOMPARE(view->document(), doc);
}

void TestViewManager::testMultipleViews() {
    // 建立多個視圖
    QVector<CadView*> views;
    for (int i = 0; i < 5; ++i) {
        views.append(m_viewManager->createView());
    }
    
    QCOMPARE(m_viewManager->views().size(), 5);
    
    // 關閉所有視圖
    m_viewManager->closeAllViews();
    QCOMPARE(m_viewManager->views().size(), 0);
    QVERIFY(m_viewManager->activeView() == nullptr);
}

void TestViewManager::testSignals() {
    QSignalSpy createdSpy(m_viewManager, &ViewManager::viewCreated);
    QSignalSpy closedSpy(m_viewManager, &ViewManager::viewClosed);
    QSignalSpy activeChangedSpy(m_viewManager, &ViewManager::activeViewChanged);
    
    // 建立視圖
    CadView* view = m_viewManager->createView();
    QCOMPARE(createdSpy.count(), 1);
    QCOMPARE(activeChangedSpy.count(), 1);
    
    // 關閉視圖
    m_viewManager->closeView(view);
    QCOMPARE(closedSpy.count(), 1);
}

QTEST_MAIN(TestViewManager)
#include "test_viewmanager.moc"
