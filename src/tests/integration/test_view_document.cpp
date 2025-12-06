// tests/integration/test_view_document.cpp

#include <QtTest>
#include "view/ViewManager.h"
#include "cad/Document.h"
#include "cad/features/Sketch.h"

class TestViewDocument : public QObject {
    Q_OBJECT
    
private Q_SLOTS:
    void testDisplayFeature();
    void testUpdateFeature();
    void testMultipleFeatures();
};

void TestViewDocument::testDisplayFeature() {
    // 建立文件和特徵
    Document* doc = new Document(this);
    Sketch* sketch = new Sketch(CustomPlane::XY(), doc);
    
    // 添加到文件
    doc->addFeature(sketch);
    
    // 建立視圖並顯示
    ViewManager viewMgr;
    CadView* view = viewMgr.createView();
    view->setDocument(doc);
    
    // 驗證
    QVERIFY(view->document() == doc);
}

