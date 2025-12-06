// tests/unit/view/test_rubberband.cpp

#include <QtTest>
#include "view/RubberBand.h"
#include <V3d_View.hxx>

using namespace aicad::view;

class TestRubberBand : public QObject {
    Q_OBJECT
    
private Q_SLOTS:
    void initTestCase();
    void cleanup();
    
    void testSetMode();
    void testLineMode();
    void testRectangleMode();
    void testPolylineMode();
    void testClear();
    void testSignals();
    
private:
    Handle(V3d_View) createMockView();
};

void TestRubberBand::initTestCase() {
    qDebug() << "=== RubberBand 測試開始 ===";
}

void TestRubberBand::cleanup() {
    // 每個測試後清理
}

Handle(V3d_View) TestRubberBand::createMockView() {
    // 建立簡單的 OCCT 視圖用於測試
    // 注意: 這需要完整的 OCCT 初始化
    // 在實際測試中可能需要 Mock 物件
    return Handle(V3d_View)();
}

void TestRubberBand::testSetMode() {
    Handle(V3d_View) view = createMockView();
    if (view.IsNull()) {
        QSKIP("Cannot create OCCT view for testing");
    }
    
    RubberBand rubberBand(view);
    
    rubberBand.setMode(RubberBandMode::Line);
    QCOMPARE(rubberBand.mode(), RubberBandMode::Line);
    
    rubberBand.setMode(RubberBandMode::Rectangle);
    QCOMPARE(rubberBand.mode(), RubberBandMode::Rectangle);
}

void TestRubberBand::testLineMode() {
    Handle(V3d_View) view = createMockView();
    if (view.IsNull()) {
        QSKIP("Cannot create OCCT view for testing");
    }
    
    RubberBand rubberBand(view);
    rubberBand.setMode(RubberBandMode::Line);
    
    QVector2D base(0, 0);
    QVector2D current(100, 50);
    
    rubberBand.setBasePoint(base);
    QCOMPARE(rubberBand.basePoint(), base);
    
    rubberBand.updateCurrentPoint(current);
    QCOMPARE(rubberBand.currentPoint(), current);
}

void TestRubberBand::testRectangleMode() {
    Handle(V3d_View) view = createMockView();
    if (view.IsNull()) {
        QSKIP("Cannot create OCCT view for testing");
    }
    
    RubberBand rubberBand(view);
    rubberBand.setMode(RubberBandMode::Rectangle);
    
    rubberBand.setBasePoint(QVector2D(0, 0));
    rubberBand.updateCurrentPoint(QVector2D(100, 50));
    
    // 驗證基本功能
    QVERIFY(rubberBand.mode() == RubberBandMode::Rectangle);
}

void TestRubberBand::testPolylineMode() {
    Handle(V3d_View) view = createMockView();
    if (view.IsNull()) {
        QSKIP("Cannot create OCCT view for testing");
    }
    
    RubberBand rubberBand(view);
    rubberBand.setMode(RubberBandMode::Polyline);
    
    rubberBand.setBasePoint(QVector2D(0, 0));
    rubberBand.addPoint(QVector2D(100, 0));
    rubberBand.addPoint(QVector2D(100, 50));
    
    QCOMPARE(rubberBand.points().size(), 3);
}

void TestRubberBand::testClear() {
    Handle(V3d_View) view = createMockView();
    if (view.IsNull()) {
        QSKIP("Cannot create OCCT view for testing");
    }
    
    RubberBand rubberBand(view);
    rubberBand.setMode(RubberBandMode::Polyline);
    rubberBand.setBasePoint(QVector2D(0, 0));
    rubberBand.addPoint(QVector2D(100, 0));
    
    QVERIFY(rubberBand.points().size() > 0);
    
    rubberBand.clear();
    
    QCOMPARE(rubberBand.points().size(), 0);
    QCOMPARE(rubberBand.mode(), RubberBandMode::None);
}

void TestRubberBand::testSignals() {
    Handle(V3d_View) view = createMockView();
    if (view.IsNull()) {
        QSKIP("Cannot create OCCT view for testing");
    }
    
    RubberBand rubberBand(view);
    
    QSignalSpy pointAddedSpy(&rubberBand, &RubberBand::pointAdded);
    QSignalSpy clearedSpy(&rubberBand, &RubberBand::cleared);
    
    rubberBand.setMode(RubberBandMode::Polyline);
    rubberBand.setBasePoint(QVector2D(0, 0));
    rubberBand.addPoint(QVector2D(100, 0));
    
    QCOMPARE(pointAddedSpy.count(), 1);
    
    rubberBand.clear();
    QCOMPARE(clearedSpy.count(), 1);
}

QTEST_MAIN(TestRubberBand)
#include "test_rubberband.moc"
