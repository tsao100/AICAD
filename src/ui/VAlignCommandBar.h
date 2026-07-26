/**
 * @file VAlignCommandBar.h
 * @brief Step 13 — 縱斷面命令輸入列
 *
 * 功能：
 *   放置於 VAlignEditorDockWidget 底部，解析以下格式的鍵盤輸入：
 *     CH=<數字>    → emit chainageSet(double)
 *     EL=<數字>    → emit elevationSet(double)
 *     K=<數字>     → emit kValueSet(double)
 *     LVC=<數字>   → emit lvcSet(double)
 *     GRADE=<數字> → emit gradeSet(double)   (% 單位，例如 GRADE=1.5)
 *     <其他>       → emit commandEntered(QString)  (轉大寫後轉發)
 *
 * 外觀：深色背景，與 VAlignEditorDockWidget / VAlignProfileView 風格一致。
 */
#pragma once

#include <QWidget>

class QLineEdit;
class QLabel;

namespace aicad {
namespace ui {

class VAlignCommandBar : public QWidget
{
    Q_OBJECT

public:
    explicit VAlignCommandBar(QWidget* parent = nullptr);
    ~VAlignCommandBar() override = default;

    /** 在提示區顯示文字（由命令層驅動）。 */
    void setPrompt(const QString& text);

    /** 清除輸入框（命令完成後重置）。 */
    void clearInput();

    /** 讓輸入框取得焦點。 */
    void focusInput();

Q_SIGNALS:
    /** 無法解析為特定類型時，原始文字（toUpper）轉發給命令系統。 */
    void commandEntered(const QString& text);

    void chainageSet(double ch);    ///< CH=<數字>
    void elevationSet(double el);   ///< EL=<數字>
    void kValueSet(double K);       ///< K=<數字>
    void lvcSet(double lvc);        ///< LVC=<數字>
    void gradeSet(double grade);    ///< GRADE=<數字>  [%]

private Q_SLOTS:
    void onReturnPressed();

private:
    void setupUI();
    void applyStyle();

    QLabel*    m_prompt = nullptr;
    QLineEdit* m_input  = nullptr;
};

} // namespace ui
} // namespace aicad
