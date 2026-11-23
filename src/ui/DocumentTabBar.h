/**
 * @file DocumentTabBar.h
 * @brief CADView 頁籤：每個開啟的文件一個頁籤，點選即切換目前文件／視圖。
 *
 * 用途：雙擊組件（INSERTCOMP 載入的 *.aicad）會在另一個 CADView 開啟來源檔編輯；
 * 有了頁籤就能隨時切換回原先的 CADView（原文件）。只有一個文件時自動隱藏。
 */
#pragma once

#include <QPointer>
#include <QTabBar>
#include <QVector>

namespace aicad {
namespace cad { class Document; }
namespace ui {

class DocumentTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit DocumentTabBar(QWidget* parent = nullptr);

    /// 依 DocumentManager 的文件清單重建頁籤並選取 current（不會發出 documentActivated）
    void sync(const QVector<cad::Document*>& docs, cad::Document* current);

    /// 只重新整理頁籤標題（文件被修改／改名時）
    void refreshTitles();

Q_SIGNALS:
    /// 使用者點選了某個頁籤
    void documentActivated(aicad::cad::Document* doc);
    /// 使用者按了頁籤的關閉鈕
    void documentCloseRequested(aicad::cad::Document* doc);

private:
    static QString titleOf(const cad::Document* doc);

    QVector<QPointer<cad::Document>> m_docs;
    QVector<QMetaObject::Connection> m_conns;
    bool m_syncing = false;
};

} // namespace ui
} // namespace aicad
