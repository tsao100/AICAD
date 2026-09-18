/**
 * @file TransitionCurveHelp.h
 * @brief 共用的「螺旋線公式參考文件」顯示邏輯（F1／說明按鈕共用）。
 *
 * 從 AddSpiralCalcDialog 抽出：凡是對話框裡有 SpiralType 下拉選單
 * （AddSpiralCalcDialog／SCSCalcDialog／CompoundChainCalcDialog／
 * AlignmentReverseSCSCalcDialog 皆是），F1 快速鍵與螺旋線類型旁的「？」
 * 按鈕都呼叫同一個入口 TransitionCurveHelp::show(this)，行為完全一致：
 *
 *   - 找到 ./help/TransitionCurveLiteratureReferences.md 並讀取內容；
 *   - 若找到 web/vendor（marked.min.js + mathjax/tex-svg.js），產生一個
 *     獨立的 HTML（marked.js 轉換 Markdown 前先用 protectMath() 保護
 *     公式區段，避免 CommonMark 的反斜線跳脫規則咬爛 LaTeX 語法——見
 *     .cpp 內詳細說明；MathJax SVG output 排版 $...$／$$...$$／
 *     \(...\)／\[...\] 界定的公式），寫進系統暫存目錄，交給使用者系統上
 *     已安裝的預設瀏覽器開啟（QDesktopServices::openUrl()）——AICAD 本身
 *     不需要連結 Qt WebEngine；
 *   - 找不到 web/vendor 時退回 QTextBrowser::setMarkdown() 陽春顯示
 *     （文字看得到，但公式只會是原始 LaTeX 純文字）；
 *   - 找不到 md 檔本身則顯示提示訊息。
 *
 * 用法（在任何 QDialog 子類別的 init() 內）：
 * @code
 *   auto* helpShortcut = new QShortcut(QKeySequence(Qt::Key_F1), this);
 *   connect(helpShortcut, &QShortcut::activated, this,
 *           [this] { TransitionCurveHelp::show(this); });
 *
 *   auto* helpButton = new QPushButton(this);
 *   helpButton->setIcon(style()->standardIcon(QStyle::SP_DialogHelpButton));
 *   helpButton->setFixedSize(28, 28);
 *   helpButton->setToolTip(tr("Show transition curve literature references (F1)"));
 *   helpButton->setFocusPolicy(Qt::NoFocus);
 *   connect(helpButton, &QPushButton::clicked, this,
 *           [this] { TransitionCurveHelp::show(this); });
 * @endcode
 *
 * @author AICAD Team
 * @see AddSpiralCalcDialog（原始實作出處）
 */
#pragma once

#include <QString>

class QWidget;

namespace aicad {
namespace ui {

class TransitionCurveHelp
{
public:
    /** 顯示螺旋線公式參考文件；parent 僅用於訊息框／陽春版對話框的視窗
     *  歸屬（modal 陽春版會是 parent 的子視窗），瀏覽器路徑不受影響。 */
    static void show(QWidget* parent);

private:
    TransitionCurveHelp() = delete;   // 純靜態工具類別，不可實例化

    /** 依序嘗試數個候選路徑找出 TransitionCurveLiteratureReferences.md
     *  （與 Application.cpp 載入 menu.txt 的「目前目錄／上層目錄／
     *  applicationDirPath()」搜尋慣例一致），找到第一個存在的檔案就回傳
     *  其路徑；都找不到則回傳空字串。 */
    static QString locateHelpFile();

    /** 依相同搜尋慣例找出 web/vendor 資料夾（需同時含 marked.min.js 與
     *  mathjax/tex-svg.js 才算找到）。找不到回傳空字串，由 show() 退回
     *  showPlain() 的 QTextBrowser 陽春版顯示。 */
    static QString locateVendorDir();

    /** 陽春版說明顯示（QTextBrowser::setMarkdown()，不支援 MathJax 公式排
     *  版）：找不到 web/vendor 資產時的退路，確保至少能看到文字內容。 */
    static void showPlain(QWidget* parent, const QString& markdown);

    /** 主要說明顯示：產生獨立 HTML，交給使用者系統上已安裝的預設瀏覽器
     *  開啟；vendorDir 為 locateVendorDir() 找到的 web/vendor 絕對路徑。 */
    static void showInBrowser(QWidget* parent, const QString& markdown, const QString& vendorDir);
};

} // namespace ui
} // namespace aicad
