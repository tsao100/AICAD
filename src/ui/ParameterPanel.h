#pragma once
#include <QDockWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QStringList>
#include "core/ParameterStore.h"
#include "cad/SketchInstance.h"

QT_BEGIN_NAMESPACE
class QTableWidget; class QLabel; class QPushButton;
class QToolButton; class QVBoxLayout; class QHBoxLayout;
QT_END_NAMESPACE

namespace aicad::ui {

/**
 * @brief 參數面板 Dock（Phase 7）
 *
 * 支援兩種模式：
 *   Master 模式：顯示草圖 ParameterStore 中所有參數，
 *               並列出使用此草圖的 SketchInstance。
 *   Instance 模式：顯示 instance 覆寫值，灰色繼承值，
 *                  標記 Source 欄（instance override / master default / global）。
 *
 * 快捷鍵：Ctrl+P 開啟/關閉
 */
class ParameterPanel : public QDockWidget {
    Q_OBJECT
public:
    enum class PanelMode { Master, Instance };

    explicit ParameterPanel(QWidget* parent = nullptr);

    // ── 切換模式 ─────────────────────────────────────────────────────────

    /**
     * 切換到 master 草圖的參數顯示
     * @param store       草圖層 ParameterStore
     * @param sketchName  草圖名稱（顯示用）
     * @param instances   使用此草圖的副本列表
     */
    void showMaster(aicad::core::ParameterStore* store,
                    const QString& sketchName,
                    const QList<aicad::cad::SketchInstance*>& instances = {});

    /**
     * 切換到 instance 的參數顯示
     * @param instance    SketchInstance 物件
     */
    void showInstance(aicad::cad::SketchInstance* instance);

    void clearPanel();

Q_SIGNALS:
    void parameterEdited(const QString& name, const QString& expression);
    void overrideCleared(const QString& paramName);

private Q_SLOTS:
    void onAddOrOverride();
    void onClearOverride();
    void onClearAll();
    void onCellChanged(int row, int col);
    void onParameterChanged(const QString& name, double value);
    void onSelectionChanged();

private:
    void setupUI();
    void rebuildMasterTable();
    void rebuildInstanceTable();
    bool validateExpression(const QString& name,
                            const QString& expr,
                            aicad::core::ParameterStore* store);
    void applyRowStyle(int row, const QString& source);

    // 欄位常數
    static constexpr int COL_NAME   = 0;
    static constexpr int COL_EXPR   = 1;
    static constexpr int COL_VALUE  = 2;
    static constexpr int COL_SOURCE = 3;

    PanelMode                        m_mode = PanelMode::Master;
    aicad::core::ParameterStore*     m_masterStore  = nullptr;
    aicad::cad::SketchInstance*      m_instance     = nullptr;
    QList<aicad::cad::SketchInstance*> m_instances;

    QLabel*       m_contextLabel   = nullptr;
    QLabel*       m_instancesLabel = nullptr;
    QTableWidget* m_table          = nullptr;
    QPushButton*  m_btnAdd         = nullptr;
    QPushButton*  m_btnClear       = nullptr;
    QPushButton*  m_btnClearAll    = nullptr;
    QLabel*       m_dependentsLabel = nullptr;
    bool          m_updating       = false;
};

} // namespace aicad::ui
