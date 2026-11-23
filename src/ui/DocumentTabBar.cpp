/**
 * @file DocumentTabBar.cpp
 */
#include "DocumentTabBar.h"

#include "cad/Document.h"

#include <QFileInfo>

namespace aicad {
namespace ui {

DocumentTabBar::DocumentTabBar(QWidget* parent)
    : QTabBar(parent)
{
    setDocumentMode(true);
    setExpanding(false);
    setMovable(false);
    setTabsClosable(true);
    setUsesScrollButtons(true);
    setVisible(false);          // 只有一個文件時不佔空間

    connect(this, &QTabBar::currentChanged, this, [this](int index) {
        if (m_syncing || index < 0 || index >= m_docs.size() || !m_docs[index]) return;
        Q_EMIT documentActivated(m_docs[index]);
    });
    connect(this, &QTabBar::tabCloseRequested, this, [this](int index) {
        if (index < 0 || index >= m_docs.size() || !m_docs[index]) return;
        Q_EMIT documentCloseRequested(m_docs[index]);
    });
}

QString DocumentTabBar::titleOf(const cad::Document* doc)
{
    if (!doc) return QString();
    const QString fn = doc->fileName();
    QString t = fn.isEmpty() ? tr("未命名") : QFileInfo(fn).fileName();
    if (doc->isModified()) t += QStringLiteral(" *");
    return t;
}

void DocumentTabBar::sync(const QVector<cad::Document*>& docs, cad::Document* current)
{
    m_syncing = true;

    for (const QMetaObject::Connection& c : std::as_const(m_conns)) disconnect(c);
    m_conns.clear();

    // 移除舊頁籤
    while (count() > 0) removeTab(count() - 1);
    m_docs.clear();

    int currentIndex = -1;
    for (cad::Document* d : docs) {
        if (!d) continue;
        const int idx = addTab(titleOf(d));
        setTabToolTip(idx, d->fileName());
        m_docs.append(d);
        if (d == current) currentIndex = idx;
        // 修改狀態／檔名變了 → 更新標題
        m_conns << connect(d, &cad::Document::modifiedChanged, this, [this](bool) { refreshTitles(); });
        m_conns << connect(d, &cad::Document::fileNameChanged, this, [this]()     { refreshTitles(); });
    }
    if (currentIndex >= 0) setCurrentIndex(currentIndex);

    setTabsClosable(count() > 1);     // 最後一個文件不提供關閉鈕
    setVisible(count() > 1);          // 有兩個以上文件才顯示頁籤

    m_syncing = false;
}

void DocumentTabBar::refreshTitles()
{
    for (int i = 0; i < m_docs.size() && i < count(); ++i)
        if (m_docs[i]) {
            setTabText(i, titleOf(m_docs[i]));
            setTabToolTip(i, m_docs[i]->fileName());
        }
}

} // namespace ui
} // namespace aicad
