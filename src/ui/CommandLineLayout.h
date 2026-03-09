#ifndef COMMANDLINELAYOUT_H
#define COMMANDLINELAYOUT_H

#include <QObject>
#include <QWidget>
#include <QVBoxLayout>
#include <QMap>
#include <QSettings>

namespace aicad {
namespace ui {

// 區域標識
enum class CommandLineArea {
    Toolbar,
    History,
    Options,
    Input,
    StatusBar
};

// 區域狀態
struct AreaState {
    bool visible;
    int height;
    int minHeight;
    int maxHeight;
    bool collapsed;
    bool locked;

    AreaState()
        : visible(true)
        , height(100)
        , minHeight(20)
        , maxHeight(500)
        , collapsed(false)
        , locked(false) {}

    AreaState(bool v, int h, int minH, int maxH, bool c, bool l)
        : visible(v)
        , height(h)
        , minHeight(minH)
        , maxHeight(maxH)
        , collapsed(c)
        , locked(l) {}
};

// 佈局模式
enum class LayoutMode {
    Compact,    // 只顯示輸入框
    Standard,   // 輸入框 + 3行歷史
    Extended,   // 完整功能 + 6行歷史
    Full        // 最大化（20行歷史）
};

class CommandLineLayout : public QObject {
    Q_OBJECT

public:
    explicit CommandLineLayout(QWidget* parentWidget, QObject* parent = nullptr);
    ~CommandLineLayout() override;

    // 區域管理
    void addArea(CommandLineArea area, QWidget* widget);
    void removeArea(CommandLineArea area);
    QWidget* getArea(CommandLineArea area) const;

    // 顯示/隱藏
    void setAreaVisible(CommandLineArea area, bool visible, bool animated = true);
    bool isAreaVisible(CommandLineArea area) const;
    void toggleArea(CommandLineArea area);

    // 大小管理
    void setAreaHeight(CommandLineArea area, int height);
    int getAreaHeight(CommandLineArea area) const;
    void setAreaMinHeight(CommandLineArea area, int minHeight);
    void setAreaMaxHeight(CommandLineArea area, int maxHeight);

    // 佈局模式
    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const { return m_layoutMode; }

    // 歷史記錄行數
    void setHistoryLineCount(int lines);
    int historyLineCount() const { return m_historyLineCount; }

    // 展開/收合
    void expandHistory();
    void collapseHistory();

    // 總高度
    int totalHeight() const;

    // 狀態保存/恢復
    void saveState(QSettings* settings);
    void restoreState(QSettings* settings);

signals:
    void areaVisibilityChanged(CommandLineArea area, bool visible);
    void areaSizeChanged(CommandLineArea area, int size);
    void layoutModeChanged(LayoutMode mode);
    void historyLineCountChanged(int lines);

public slots:
    void onHistoryExpandRequested();

private:
    void setupLayout();
    void updateLayout();
    void applyAreaState(CommandLineArea area);
    void applyLayoutMode();
    void animateAreaHeight(QWidget* widget, int from, int to);

    QWidget* m_parentWidget;
    QVBoxLayout* m_mainLayout;

    QMap<CommandLineArea, QWidget*> m_areas;
    QMap<CommandLineArea, AreaState> m_areaStates;

    LayoutMode m_layoutMode;
    int m_historyLineCount;

    // 預設尺寸
    static constexpr int DEFAULT_TOOLBAR_HEIGHT = 30;
    static constexpr int DEFAULT_HISTORY_HEIGHT = 90;
    static constexpr int DEFAULT_OPTIONS_HEIGHT = 40;
    static constexpr int DEFAULT_INPUT_HEIGHT = 30;
    static constexpr int DEFAULT_STATUSBAR_HEIGHT = 22;
    static constexpr int HISTORY_LINE_HEIGHT = 20;
};

} // namespace ui
} // namespace aicad

#endif
