/**
 * @file DimExpressionDialog.h
 * @brief GDIM 尺寸約束「自動命名參數」運算式編輯框。
 *
 * 對應需求：
 *   1. GDIM 建立的尺寸約束自動命名參數（d1, d2, a1, a2…），
 *      見 ConstraintCommands.h/.cpp 的 autoParamPrefix()/nextAutoParamName()、
 *      以及 GeneralDimCommand::commitDimension() 的自動註冊邏輯。
 *   2. GDIM 尺寸約束建立後，自動彈出本對話框編輯其運算式；編輯期間可以
 *      直接點選草圖裡「其他」既有的尺寸標註，把它的自動命名參數插入目前
 *      正在編輯的運算式（例如點另一個標註 d1，遊標處插入 "d1"），組出像
 *      "d1*2+5" 這樣的算式。
 *
 * 設計取捨：使用「非模態」QDialog（而非 QInputDialog::getText() 那種真正
 * 阻塞事件循環的 modal 對話框）——原因是「編輯期間點選畫面上其他尺寸標註」
 * 這個需求，必須讓 CadView 在對話框開著的時候仍然能收到滑鼠事件，modal
 * 對話框會整個擋掉，做不到。點選解析交給 CadView::dimensionRefPicked()
 * 訊號（見 CadView.h 的 setRefPickModeActive() 說明）。
 *
 * ⚠️ 修改：「插入參考」不再需要另外按一顆 toggle 按鈕才啟用——對話框一
 * 打開就自動呼叫 setRefPickModeActive(true)，全程都能直接點選畫面上其他
 * 尺寸標註插入參數名稱；對話框關閉（確定/取消/直接關閉視窗）時才關掉。
 * 原本的按鈕已移除。
 */
#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;
class QLabel;

namespace aicad {

namespace cad { class Sketch; }
namespace view { class CadView; }

namespace ui {

class DimExpressionDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param sk              目前作用中的 Sketch（用於 ParameterStore 求值/
     *                        查詢約束型別，供顯示用）。
     * @param constraintUuid  剛建立、要編輯運算式的約束 UUID。
     * @param autoParamName   建立時自動註冊的名稱（例如 "d3"），顯示在
     *                        編輯框旁邊的標籤「d3 =」，非必要（找不到時
     *                        退回顯示 UUID 前幾碼）。
     * @param currentExpr     目前的運算式（ParameterStore 裡 autoParamName
     *                        的定義），預先填入編輯框。
     * @param cadView         供自動開關 setRefPickModeActive()、接收
     *                        dimensionRefPicked() 訊號。可為 nullptr（此時
     *                        「插入參考」功能停用，其餘功能不受影響）。
     */
    DimExpressionDialog(cad::Sketch* sk,
                        const QString& constraintUuid,
                        const QString& autoParamName,
                        const QString& currentExpr,
                        view::CadView* cadView,
                        QWidget* parent = nullptr);
    ~DimExpressionDialog() override;

Q_SIGNALS:
    /**
     * 使用者按下「確定」，且運算式非空時發出。實際套用（呼叫
     * command::applyDimensionEdit()）由外部（UIManager）接手處理，本對話框
     * 不直接依賴 command:: 命名空間，維持單純的 UI 元件。
     */
    void expressionAccepted(const QString& constraintUuid, const QString& newExpr);

protected:
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void onDimensionRefPicked(const QString& refConstraintUuid);
    void onAccept();
    void onReject();

private:
    /// refConstraintUuid 對應的自動命名參數（例如 "d1"）；找不到（該約束
    /// 不是 driving、或建立時尚未有自動命名參數功能）時回傳空字串。
    QString autoNameForConstraint(const QString& refConstraintUuid) const;

    cad::Sketch*    m_sketch;
    QString         m_constraintUuid;
    view::CadView*  m_cadView;

    QLabel*      m_titleLabel      = nullptr;
    QLineEdit*   m_exprEdit        = nullptr;
    QLabel*      m_hintLabel       = nullptr;
    QPushButton* m_okButton        = nullptr;
    QPushButton* m_cancelButton    = nullptr;
};

} // namespace ui
} // namespace aicad
