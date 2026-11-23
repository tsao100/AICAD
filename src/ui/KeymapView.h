/**
 * @file KeymapView.h
 * @brief Keymap View：整份文件的平面縮圖（鳥瞰索引圖），點選／拖曳可讓 CadView Pan 到該點。
 */
#ifndef AICAD_UI_KEYMAPVIEW_H
#define AICAD_UI_KEYMAPVIEW_H

#include <QDockWidget>
#include <QComboBox>
#include <QPolygonF>
#include <QRectF>
#include <QVector>

namespace aicad {
namespace ui {

class KeymapCanvas;

class KeymapView : public QDockWidget {
    Q_OBJECT
public:
    explicit KeymapView(QWidget* parent = nullptr);

    /// 設定縮圖內容：線路折線（世界 XY）與其他物件的 XY 包圍框
    /// lines = 線路折線；sketchLines = 草圖幾何（已取樣成折線，不含約束／尺寸）；boxes = 其他物件包圍框
    void setScene(const QVector<QPolygonF>& lines,
                  const QVector<QPolygonF>& sketchLines,
                  const QVector<QRectF>& boxes);

    /// 縮圖內容篩選
    enum Filter { All = 0, RailwayAndLoft = 1, SketchOnly = 2 };
    Filter filter() const;

    /// 設定目前 CadView 可視範圍（世界 XY 多邊形；空 = 不畫）
    void setViewport(const QPolygonF& viewport);

Q_SIGNALS:
    /// 使用者在縮圖上點選／拖曳；xy 為對應的世界座標
    void panRequested(const QPointF& worldXY);
    /// 使用者切換了篩選（內容需要重建）
    void filterChanged();

private:
    KeymapCanvas* m_canvas = nullptr;
    QComboBox*    m_filterBox = nullptr;
};

} // namespace ui
} // namespace aicad

#endif
