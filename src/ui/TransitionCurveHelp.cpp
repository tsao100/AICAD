#include "ui/TransitionCurveHelp.h"

#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QDesktopServices>
#include <QStringList>

namespace aicad {
namespace ui {

// ────────────────────────────────────────────────────────────────────────────
//  show / locateHelpFile / locateVendorDir / showPlain / showInBrowser
//
//  主要路徑：不內嵌 QWebEngineView（不拉 Qt WebEngine／Chromium 進
//  AICAD），改成產生一個獨立的 HTML（marked.js 把 Markdown 轉成 HTML，
//  MathJax——SVG output，見 web/vendor/mathjax/tex-svg.js——對
//  $...$／$$...$$／\(...\)／\[...\] 界定的 LaTeX 公式重新排版），寫進系統
//  暫存目錄，交給 QDesktopServices::openUrl() 用使用者系統上已安裝的預設
//  瀏覽器開啟；排版運算完全由那個瀏覽器自己的 JS 引擎執行。選 SVG 而非
//  CHTML output 是因為 SVG 把字型輪廓內嵌在同一個 JS 檔裡，不需要另外
//  載入 woff 字型檔，整包 vendor 資產可以直接離線運作，不依賴任何 CDN。
//
//  重要：showInBrowser() 產生的內嵌 JS 在呼叫 marked.parse() 前後都會做
//  protectMath()／restoreMath() 保護——這一段跟選用哪個數學排版引擎
//  （MathJax 或 KaTeX）完全無關，是獨立的必要修正：CommonMark（marked.js
//  遵循的規範）自己有一套反斜線跳脫規則，會把 \\[3mm]、\!、\, 這類 LaTeX
//  語法咬爛（例如 \\[3mm] 被咬成 \[3mm]，少一個反斜線），所以必須在
//  marked.js 動手之前，先把 $...$／$$...$$／\(...\)／\[...\] 整段抓出來
//  保護，等 markdown 解析完再換回原始位元組。詳見函式內註解。
//
//  找不到 web/vendor 資產時（例如尚未部署／舊安裝）退回 showPlain() 的
//  QTextBrowser 陽春顯示（文字看得到，但公式只會是原始 LaTeX 純文字）。
// ────────────────────────────────────────────────────────────────────────────

QString TransitionCurveHelp::locateHelpFile()
{
    // 候選路徑順序比照 Application.cpp 載入 menu.txt 的既有慣例：
    // 目前工作目錄 → 上層目錄（開發時常見的 build/ 子目錄執行情境）→
    // 原始碼目錄（in-source build）→ PROJECT_SOURCE_DIR（.pro 檔所在的
    // 原始碼目錄，見下方說明）→ applicationDirPath()（安裝後的執行檔
    // 所在目錄）。找到第一個實際存在的檔案就採用。
    //
    // 根本原因（Windows 下按鈕沒反應）：help/ 與 web/vendor/ 這兩個資料夾
    // 目前並未在 AICAD.pro 的建置流程中被複製到輸出目錄（只有 menu.txt
    // 有對應的 copydata / QMAKE_POST_LINK 複製步驟），上面四個候選路徑
    // 全部仰賴「執行檔的工作目錄／所在目錄」剛好跟原始碼目錄同層或相鄰。
    // 在 Linux 上 Jack 是直接在原始碼目錄做 in-source build 並執行，
    // 工作目錄本來就等於原始碼目錄，candidate 1（"help/..."）剛好命中；
    // 但 Qt Creator＋MSVC 在 Windows 上預設是 shadow build（執行檔在另一
    // 個跟原始碼樹不同層的 build-xxx-Debug/ 目錄下），上面四個候選路徑
    // 沒有一個能命中，locateHelpFile() 回傳空字串，show() 於是彈出「Help
    // file not found」訊息框——按鈕本身其實有反應，只是每次都找不到檔案。
    // PROJECT_SOURCE_DIR 是 AICAD.pro 裡已經有的編譯期巨集（.pro 檔自身
    // 所在目錄，開發機上不論是否 shadow build 都固定指向原始碼樹，
    // Application.cpp 載入 Draw/1.aicad 用的正是同一個巨集），加進候選
    // 清單後不論工作目錄為何、也不需要改建置腳本，就能在兩個平台上都
    // 找到檔案；仍保留 applicationDirPath() 作為最後一個候選，日後若把
    // help/web 兩個資料夾納入安裝包、與執行檔部署在一起，也能正常運作。
    static const char* kRelPath = "help/TransitionCurveLiteratureReferences.md";
    QStringList candidates = {
        QString::fromLatin1(kRelPath),
        QStringLiteral("../%1").arg(kRelPath),
        QStringLiteral("../../%1").arg(kRelPath),
    };
#ifdef PROJECT_SOURCE_DIR
    candidates << QString::fromLatin1(PROJECT_SOURCE_DIR) + QStringLiteral("/%1").arg(kRelPath);
#endif
    candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/%1").arg(kRelPath);
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path))
            return path;
    }
    return QString();
}

QString TransitionCurveHelp::locateVendorDir()
{
    // 與 locateHelpFile() 相同的候選路徑順序（含 PROJECT_SOURCE_DIR，
    // 理由同上）；多檢查 marked.min.js 與 mathjax/tex-svg.js 兩個檔案都
    // 存在，避免只部署了一半的 vendor 資料夾被誤判為「找到了」卻在載入
    // 時才失敗。
    static const char* kRelDir = "web/vendor";
    QStringList candidates = {
        QString::fromLatin1(kRelDir),
        QStringLiteral("../%1").arg(kRelDir),
        QStringLiteral("../../%1").arg(kRelDir),
    };
#ifdef PROJECT_SOURCE_DIR
    candidates << QString::fromLatin1(PROJECT_SOURCE_DIR) + QStringLiteral("/%1").arg(kRelDir);
#endif
    candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/%1").arg(kRelDir);
    for (const QString& dir : candidates) {
        if (QFileInfo::exists(dir + QStringLiteral("/marked.min.js"))
            && QFileInfo::exists(dir + QStringLiteral("/mathjax/tex-svg.js")))
            return QFileInfo(dir).absoluteFilePath(); // 轉絕對路徑，供後續組相對路徑輸出資料夾
    }
    return QString();
}

void TransitionCurveHelp::show(QWidget* parent)
{
    const QString mdPath = locateHelpFile();
    if (mdPath.isEmpty()) {
        QMessageBox::information(parent, QObject::tr("Help"),
            QObject::tr("Help file not found:\n%1\n\n"
               "Expected under a \"help\" folder next to the working"
               " directory or the application executable.")
                .arg(QString::fromLatin1("help/TransitionCurveLiteratureReferences.md")));
        return;
    }

    QFile file(mdPath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(parent, QObject::tr("Help"),
            QObject::tr("Found help file but could not open it:\n%1").arg(mdPath));
        return;
    }
    // 直接以 QByteArray 讀取後用 fromUtf8() 轉換，而非 QTextStream::
    // setEncoding(QStringConverter::Utf8)：後者是 Qt6-only API，本專案
    // unix:!macx 區塊未強制鎖定 Qt6（見 AICAD.pro），fromUtf8() 在
    // Qt5／Qt6 皆可用，維持與既有跨平台建置設定相容。
    const QString markdown = QString::fromUtf8(file.readAll());
    file.close();

    const QString vendorDir = locateVendorDir();
    if (vendorDir.isEmpty()) {
        showPlain(parent, markdown); // 退路：見標頭檔 showPlain() 說明
        return;
    }
    showInBrowser(parent, markdown, vendorDir);
}

void TransitionCurveHelp::showPlain(QWidget* parent, const QString& markdown)
{
    auto* helpDlg = new QDialog(parent);
    helpDlg->setAttribute(Qt::WA_DeleteOnClose);
    helpDlg->setWindowTitle(QObject::tr("Help — Transition Curve Literature References"));
    helpDlg->resize(720, 640);

    auto* layout = new QVBoxLayout(helpDlg);

    auto* notice = new QLabel(
        QObject::tr("(web/vendor not found — showing plain text; formulas will appear"
           " as raw LaTeX instead of typeset math. See TransitionCurveHelp.cpp"
           " locateVendorDir() for the expected deployment path.)"), helpDlg);
    notice->setWordWrap(true);
    notice->setStyleSheet(QStringLiteral("color: #8a6d3b;"));
    layout->addWidget(notice);

    auto* browser = new QTextBrowser(helpDlg);
    browser->setOpenExternalLinks(true);
    browser->setMarkdown(markdown);
    layout->addWidget(browser);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, helpDlg);
    QObject::connect(buttons, &QDialogButtonBox::rejected, helpDlg, &QDialog::close);
    QObject::connect(buttons, &QDialogButtonBox::accepted, helpDlg, &QDialog::close);
    layout->addWidget(buttons);

    helpDlg->show();
    helpDlg->raise();
    helpDlg->activateWindow();
}

void TransitionCurveHelp::showInBrowser(QWidget* parent, const QString& markdown, const QString& vendorDir)
{
    Q_UNUSED(parent); // 瀏覽器是獨立行程，不需要 Qt parent；保留參數只為與 showPlain() 簽章一致、未來若要在失敗時回退也方便

    // 渲染資料夾：系統暫存目錄下固定名稱的子資料夾，每次顯示說明都整份
    // 覆寫──不用 QTemporaryDir：其解構子會在使用者的瀏覽器可能還在讀取
    // 檔案時就把資料夾砍掉，有讀取競態風險；固定資料夾由作業系統自行
    // 回收即可，內容都是可重建的暫存產物。help.html 與 marked.min.js／
    // mathjax/tex-svg.js 三者放在「同一個」資料夾下，html 用純相對路徑
    // （"marked.min.js"、"mathjax/tex-svg.js"）引用──系統瀏覽器直接
    // navigate 到一個 file:// URL 時（不同於 QWebEngineView::setHtml()
    // 那種會產生 opaque origin 的情況），會拿到正常的 file:// origin，
    // 讀取同資料夾下其他 file:// 資源是所有主流瀏覽器都支援、不需要
    // 額外設定的標準行為。
    const QString renderDir = QDir::tempPath() + QStringLiteral("/AICAD_help_render");
    QDir().mkpath(renderDir + QStringLiteral("/mathjax"));

    // 每次都重新複製一份（檔案總共不到 2MB，本機磁碟複製成本可忽略）：
    // 確保 web/vendor 裡的檔案若被更新過，說明頁面也會跟著換新，不會
    // 因為渲染資料夾裡殘留舊版檔案而顯示過期內容。QFile::copy() 若目的
    // 檔已存在會直接失敗，所以先各自刪除舊檔再複製。
    const struct { QString rel; } kAssetFiles[] = {
        { QStringLiteral("marked.min.js") },
        { QStringLiteral("mathjax/tex-svg.js") },
    };
    for (const auto& asset : kAssetFiles) {
        const QString dst = renderDir + QStringLiteral("/") + asset.rel;
        QFile::remove(dst);
        if (!QFile::copy(vendorDir + QStringLiteral("/") + asset.rel, dst)) {
            // 複製失敗（例如磁碟空間不足）→ 退回陽春版，至少看得到文字。
            showPlain(parent, markdown);
            return;
        }
    }

    // 防止 markdown 內容中若剛好出現「</script」字樣（不分大小寫），提前
    // 結束下面用來塞原始 Markdown 的 <script type="text/plain"> 區塊——
    // script 元素的內容是 HTML「raw text」，不會做實體解碼，所以不需要對
    // < 或 & 做一般 HTML escaping，只需要單獨防這一種情況。
    QString escapedMd = markdown;
    escapedMd.replace(QStringLiteral("</script"), QStringLiteral("<\\/script"), Qt::CaseInsensitive);

    // MathJax 設定：關掉開機自動排版（startup.typeset=false），改成等
    // MathJax.startup.promise 完成、且 marked.js 已經把 Markdown 轉成
    // HTML 塞進 #content 之後，才手動呼叫一次 typesetPromise()——避免
    // MathJax 自動排版跑在我們注入內容「之前」而排版不到任何公式（官方
    // 文件建議的「動態內容」標準寫法）。
    //
    // protectMath()／restoreMath()：CommonMark（marked.js 遵循的規範）
    // 自己有一套反斜線跳脫規則——「\」後面接 ASCII 標點會被當成「跳脫該
    // 字元」處理，輸出時吃掉一個反斜線。這會直接咬爛 LaTeX 語法，最典型
    // 的例子是 \\[3mm]（換行＋自訂間距）被 marked.parse() 咬成
    // \[3mm]（少一個反斜線，MathJax 就認不得了）；矩陣換行用的裸 \\、
    // 底線 _、星號 * 這類 markdown 也會拿來做語法判斷的字元一樣有風險。
    // 標準解法（Pandoc、mkdocs-material 的 arithmatex 都是同一套做法）：
    // 在丟給 marked.parse() 之前，先把 $...$／$$...$$／\(...\)／\[...\]
    // 整段抓出來，換成一個不含任何 markdown 特殊字元的純英數字佔位字串
    // （markdown 看到純英數字不會做任何跳脫／斜體判斷，原封不動保留），
    // 等 marked.parse() 跑完之後，再把佔位字串換回「原始、完全沒被
    // markdown 動過」的公式文字——只針對 &／<／> 三個字元做 HTML
    // escaping（讓瀏覽器的 HTML 解析器不會把公式裡剛好出現的 < 或 > 誤
    // 認成標籤），backslash／底線／星號都不去動，MathJax 拿到的會是跟
    // .md 檔裡一模一樣的原始 LaTeX 語法。這一段跟選用 MathJax 或 KaTeX
    // 無關，兩者都吃 marked.js 先跑過一次的結果，都需要這層保護。
    const QString html = QStringLiteral(R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<title>Transition Curve Literature References</title>
<style>
  body { font-family: -apple-system, "Segoe UI", "Microsoft JhengHei", sans-serif;
         margin: 24px 32px; max-width: none; line-height: 1.55; }
  table { border-collapse: collapse; margin: 8px 0; width: 100%; table-layout: fixed; }
  td, th { border: 1px solid #ccc; padding: 4px 10px; text-align: left; overflow-wrap: break-word; vertical-align: top; }
  /* 公式速查表（本文件裡唯一一張 6 欄表格）最後兩欄是 x(L)/y(L) 泰勒展開式，
     公式明顯比其他欄位長，加寬並讓兩欄等寬；table-layout:fixed 下用
     nth-child 鎖定這兩欄的欄寬，其餘欄位平分剩餘寬度。本文件其餘表格
     （如對照總表）欄數較少，選不到第 5、6 欄，不受影響。單一儲存格若仍
     容不下極長的展開式，讓該格自己橫向捲動，不擠壓相鄰欄位或撐開整個
     表格。 */
  td:nth-child(5), th:nth-child(5),
  td:nth-child(6), th:nth-child(6) { width: 24%; }
  td:nth-child(5), td:nth-child(6) { overflow-x: auto; }
  code { background: #f2f2f2; padding: 1px 4px; border-radius: 3px; }
  h1, h2 { border-bottom: 1px solid #ddd; padding-bottom: 4px; }
</style>
<script>
  window.MathJax = {
    tex: {
      inlineMath:  [['$', '$'], ['\\(', '\\)']],
      displayMath: [['$$', '$$'], ['\\[', '\\]']]
    },
    svg: { fontCache: 'global' },
    startup: { typeset: false }
  };
</script>
<script src="mathjax/tex-svg.js"></script>
<script src="marked.min.js"></script>
</head>
<body>
<script type="text/plain" id="md-source">%1</script>
<div id="content">%2</div>
<script>
  function protectMath(text) {
    const store = [];
    function stash(m) {
      const idx = store.push(m) - 1;
      return 'ZZMATHJAXZZ' + idx + 'ZZ';
    }
    const protectedText = text
      .replace(/\$\$[\s\S]*?\$\$/g, stash)
      .replace(/\\\[[\s\S]*?\\\]/g, stash)
      .replace(/\$(?:[^$\\]|\\.)*\$/g, stash)
      .replace(/\\\([\s\S]*?\\\)/g, stash);
    return { protectedText: protectedText, store: store };
  }
  function restoreMath(html, store) {
    return html.replace(/ZZMATHJAXZZ(\d+)ZZ/g, function (_, i) {
      return store[Number(i)]
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
    });
  }

  const raw = document.getElementById('md-source').textContent;
  const protectedResult = protectMath(raw);
  const parsedHtml = marked.parse(protectedResult.protectedText);
  document.getElementById('content').innerHTML = restoreMath(parsedHtml, protectedResult.store);
  window.MathJax.startup.promise.then(function () {
    MathJax.typesetPromise();
  });
</script>
</body></html>
)HTML")
        .arg(escapedMd, QObject::tr("Loading\xE2\x80\xA6"));

    const QString htmlPath = renderDir + QStringLiteral("/help.html");
    QFile htmlFile(htmlPath);
    if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showPlain(parent, markdown); // 暫存目錄寫不進去（極端情況）→ 退回陽春版
        return;
    }
    htmlFile.write(html.toUtf8());
    htmlFile.close();

    // 交給使用者系統上已安裝、設定好的預設瀏覽器開啟——不是 AICAD 自己
    // 內嵌的視窗，所以排版（marked.js／MathJax）完全由使用者自己的
    // Chrome／Edge／Firefox 執行，AICAD 執行檔本身不需要連結 Qt
    // WebEngine，二進位檔大小、建置時間都不受影響。
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(htmlPath))) {
        QMessageBox::warning(parent, QObject::tr("Help"),
            QObject::tr("Could not open the default web browser to show:\n%1\n\n"
               "You can open this file manually.").arg(htmlPath));
    }
}

} // namespace ui
} // namespace aicad
