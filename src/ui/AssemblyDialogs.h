/**
 * @file AssemblyDialogs.h
 * @brief 零件組合的對話框：
 *   • AssemblyPartDialog — 插入／編輯零件：名稱、單位縮放、初始位置與轉角、固定
 *   • MateDialog         — 新增／編輯配合：類型、兩個幾何參照（零件的基準／面／邊／頂點，或世界座標軸／平面；
 *                          可在 3D 視圖點選）、取向（同向／反向）、距離或夾角的值（可用參數運算式）
 */
#pragma once

#include "cad/AssemblyMate.h"

#include <QDialog>
#include <QVector>
#include <functional>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace aicad {
namespace cad { class Document; }
namespace ui {

struct AssemblyPartParams {
    QString name;
    double  scale = 1.0;                    ///< 零件座標 → 組合座標的縮放
    double  x = 0, y = 0, z = 0;            ///< 位置（組合單位）
    double  rx = 0, ry = 0, rz = 0;         ///< 繞世界 X、Y、Z 的轉角（度；先 X、再 Y、最後 Z）
    bool    fixed = false;
    bool    autoSync = true;
};

class AssemblyPartDialog : public QDialog {
    Q_OBJECT
public:
    AssemblyPartDialog(const AssemblyPartParams& init, bool insertMode, QWidget* parent = nullptr);
    AssemblyPartParams params() const;

private:
    QLineEdit*      m_name = nullptr;
    QDoubleSpinBox *m_scale = nullptr, *m_x = nullptr, *m_y = nullptr, *m_z = nullptr,
                   *m_rx = nullptr, *m_ry = nullptr, *m_rz = nullptr;
    QCheckBox      *m_fixed = nullptr, *m_autoSync = nullptr;
};

class MateDialog : public QDialog {
    Q_OBJECT
public:
    /// 在 3D 視圖點選一個面／邊／頂點，回傳（零件本地座標的）參照；失敗或取消回傳 false
    using PickFn = std::function<bool(cad::MateRefData& out, QString* error)>;

    /**
     * @param initial     編輯既有配合時的定義；新增時為 nullptr
     * @param presetPartA 新增時預設的「參照 1」所屬零件 id（可為空）
     * @param defaultName 名稱欄位的預設值
     */
    MateDialog(cad::Document* doc, const cad::AssemblyMate::Data* initial, const QString& presetPartA,
               const QString& defaultName, PickFn pick, QWidget* parent = nullptr);

    cad::AssemblyMate::Data data() const;
    QString mateName() const;

private:
    struct Slot {
        QComboBox*   part = nullptr;
        QComboBox*   ref  = nullptr;
        QPushButton* pick = nullptr;
        QLabel*      info = nullptr;
        QVector<cad::MateRefData> cands;
    };

    void buildSlot(int i, QGroupBox* box);
    void populateRefs(int i, const cad::MateRefData* select, const cad::MateRefData* extra);
    cad::MateRefData currentRef(int i) const;
    void onPick(int i);
    void autoFill();                 ///< 依目前幾何預填取向與數值（使用者尚未手動改過時）
    void refreshState();

    cad::Document*  m_doc = nullptr;
    PickFn          m_pick;
    Slot            m_slot[2];
    QComboBox*      m_type = nullptr;
    QComboBox*      m_orient = nullptr;
    QLineEdit*      m_value = nullptr;
    QLabel*         m_valueUnit = nullptr;
    QLineEdit*      m_name = nullptr;
    QLabel*         m_status = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    bool            m_valueTouched = false;
    bool            m_orientTouched = false;
    bool            m_building = false;
};

} // namespace ui
} // namespace aicad
