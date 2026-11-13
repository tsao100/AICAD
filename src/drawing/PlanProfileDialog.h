/**
 * @file PlanProfileDialog.h
 * @brief 平縱面圖參數對話框：預設值＋使用者調整，可存成設定檔／載入／設為預設
 */
#pragma once

#include "PlanProfileSettings.h"
#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTabWidget;
class QTableWidget;

namespace aicad { namespace drawing { class DrawingSheetManager; } }

namespace aicad {
namespace cad { class Document; }
namespace drawing {

class PlanProfileDialog : public QDialog {
    Q_OBJECT
public:
    PlanProfileDialog(cad::Document* doc, const PlanProfileSettings& initial,
                      QWidget* parent = nullptr);

    /** 對話框目前的設定（按下「產生」後取用）。 */
    PlanProfileSettings settings() const;

    /**
     * 局部模式（單張圖的設定）：分段、紙張、線形、圖冊層級的欄位停用，
     * 只能改該張圖自己的版面／顯示／刻度等。
     */
    void setLocalMode(bool local);

    /** 圖冊設定（全域）模式：確定鈕改為「套用並更新圖紙」，與 PPS 命令的「產生圖紙」區別。 */
    void setApplyMode(bool on);

    /** 切到「機廠視圖」分頁並選取第 row 列（row<0 不選）。 */
    void showDepotTab(int row = -1);

    void accept() override;

private Q_SLOTS:
    void onLoad();
    void onSave();
    void onSaveDefault();
    void onResetFactory();
    void onLoadUserDefault();
    void updateInfo();

private:
    void apply(const PlanProfileSettings& s);
    void addDepotRow(const DepotView& v);
    QVector<DepotView> depotViews() const;

    cad::Document* m_doc = nullptr;

    QListWidget* m_tclList = nullptr;
    QListWidget *m_hzList = nullptr, *m_vtList = nullptr;   ///< 資料表欄位（勾選＋排序）
    QPushButton *m_hzReset = nullptr, *m_vtReset = nullptr;
    QSpinBox *m_tblDec = nullptr, *m_tblAzDec = nullptr, *m_remarkDec = nullptr;
    QCheckBox* m_radSigned = nullptr;
    QPushButton* m_okBtn = nullptr;
    QTableWidget* m_depotTbl = nullptr;
    QCheckBox* m_genDepot = nullptr;
    QPushButton *m_depotAdd = nullptr, *m_depotDel = nullptr;
    int m_depotTabIdx = 0;
    QLabel* m_depotHint = nullptr;
    void updateDepotHint();
    QTabWidget* m_tabs = nullptr;   ///< 資料表數值／方位角秒的小數位數
    QPushButton *m_tclUp = nullptr, *m_tclDown = nullptr, *m_tclSolo = nullptr;
    QStringList checkedIds() const;
    QStringList soloIds() const;                  ///< 標為「單獨（不成串）」的線形
    void applySoloText(QListWidgetItem* it);
    void setCheckedIds(const QStringList& ids);
    QCheckBox *m_follow = nullptr, *m_neighbors = nullptr, *m_genIndex = nullptr, *m_genTables = nullptr, *m_genSchema = nullptr, *m_genCover = nullptr;
    QDoubleSpinBox *m_joinTol = nullptr, *m_joinTolZ = nullptr;
    QLineEdit* m_bookName = nullptr;
    QCheckBox* m_fullRange = nullptr;
    QDoubleSpinBox *m_startCh = nullptr, *m_endCh = nullptr;

    QDoubleSpinBox *m_hScale = nullptr, *m_vScale = nullptr;
    QCheckBox* m_autoV = nullptr;
    QDoubleSpinBox *m_segLen = nullptr, *m_overlap = nullptr;

    QComboBox *m_paper = nullptr, *m_orient = nullptr;
    QDoubleSpinBox *m_planH = nullptr, *m_profH = nullptr, *m_rowH = nullptr,
                   *m_labelW = nullptr, *m_textSize = nullptr;

    QDoubleSpinBox *m_minor = nullptr, *m_major = nullptr, *m_elevStep = nullptr, *m_tblInt = nullptr;
    QLineEdit* m_prefix = nullptr;
    QCheckBox *m_km = nullptr, *m_cont = nullptr;
    QSpinBox* m_elevDec = nullptr;

    QDoubleSpinBox* m_coordGrid = nullptr;
    QCheckBox *m_north = nullptr, *m_keyPts = nullptr, *m_curveData = nullptr;
    QCheckBox *m_grid = nullptr, *m_pvi = nullptr, *m_gradeLbl = nullptr;
    QCheckBox *m_rowElev = nullptr, *m_rowGrade = nullptr, *m_rowCurve = nullptr, *m_rowAz = nullptr;

    QLineEdit *m_titleTpl = nullptr, *m_numberTpl = nullptr;
    QCheckBox* m_replace = nullptr;

    QLabel* m_info = nullptr;
    bool m_local = false;
};

/**
 * 圖目錄／順序編輯器：列出管理器中的圖紙（圖目錄除外），可上下調整順序；
 * 按 OK 後重新編號平縱面圖並重建圖目錄。回傳是否有變更。
 */
bool runPlanProfileIndexEditor(DrawingSheetManager* mgr, cad::Document* doc, QWidget* parent,
                               const QString& bookId);

} // namespace drawing
} // namespace aicad
