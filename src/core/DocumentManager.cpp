/**
 * @file DocumentManager.cpp
 * @brief DocumentManager 類別實作
 * @author Jack (Updated for Stage 2)
 * @date 2025-01-08
 */

#include "DocumentManager.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "cad/Document.h"

#include <QDebug>
#include <QVector>
#include <QPointer>
#include <QFileInfo>
#include <QMessageBox>

namespace aicad {
namespace core {

class DocumentManager::Private {
public:
    QVector<QPointer<cad::Document>> documents;
    QPointer<cad::Document> currentDocument;
    int nextDocumentNumber = 1;
};

DocumentManager::DocumentManager(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[DocumentManager] Created";
}

DocumentManager::~DocumentManager() {
    qDebug() << "[DocumentManager] Destroying...";
    closeAll(true);
    delete d;
}

cad::Document* DocumentManager::createDocument(const QString& name) {
    QString documentName = name.isEmpty() ? generateUniqueName() : name;

    qDebug() << "[DocumentManager] Creating document:" << documentName;

    // 建立新文件
    cad::Document* doc = new cad::Document(this);
    doc->setFileName(documentName);

    // 連接信號
    connect(doc, &cad::Document::modifiedChanged,
            this, &DocumentManager::onDocumentModified);
    connect(doc, &cad::Document::fileNameChanged,
            this, &DocumentManager::onDocumentFileNameChanged);

    // ✅ 連接參考幾何初始化完成信號
    connect(doc, &cad::Document::referenceGeometryInitialized,
            this, [this, doc]() {
                qDebug() << "[DocumentManager] Reference geometry initialized for:"
                         << doc->fileName();

                // 發布事件通知 UI
                core::Application* app = core::Application::instance();
                if (app && app->eventBus()) {
                    app->eventBus()->publish("document.reference-geometry-ready",
                                             doc->fileName());
                }
            });

    // 加入管理列表
    d->documents.append(doc);

    // 設為當前文件
    setCurrentDocument(doc);

    qDebug() << "[DocumentManager] Document created. Total documents:" << d->documents.size();

    Q_EMIT documentCreated(doc);
    Q_EMIT documentCountChanged(d->documents.size());

    // ✅ 注意：實際的參考幾何初始化會在 UIManager 中進行
    // 因為需要 AIS 上下文（來自 CadView）

    return doc;
}

cad::Document* DocumentManager::openDocument(const QString& filePath) {
    if (filePath.isEmpty()) {
        qWarning() << "[DocumentManager] Cannot open document with empty path";
        return nullptr;
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qWarning() << "[DocumentManager] File does not exist:" << filePath;
        return nullptr;
    }

    qDebug() << "[DocumentManager] Opening document:" << filePath;

    // 檢查是否已開啟：
    // ⚠️ 修正：原本拿 docPtr->fileName()（load() 存的是完整路徑，例如
    // "/home/x/Draw/Plinth.aicad"）去跟 fileInfo.fileName()（只有檔名，
    // 例如 "Plinth.aicad"）比較，兩者格式不同，比對永遠不會相等，導致
    // 這段「已開啟就重用」的邏輯形同虛設——每次重新載入同一個檔案都會
    // 建立一個全新的 Document，舊的那個既不在 d->documents 移除、也沒有
    // 任何地方會 delete 它，就這樣被洩漏在記憶體裡。舊 Document 底下的
    // AlignedProfileArray 因為永遠不會被解構，也就永遠不會呼叫
    // PlaneManager::deletePlane() 清掉它建立的測站平面，於是每次重新載入
    // 都在 PlaneManager 這個全域單例裡留下一批不會再用到、名稱重複的
    // Plane——這正是 log 裡「...Station0 (1)」這種重複命名的來源，也是
    // 多次重新載入同一個檔案後偶發當掉的根本原因之一。改用完整路徑（正規
    // 化過）比較，讓「已開啟」判斷真的生效。
    const QString canonicalNewPath = QFileInfo(filePath).canonicalFilePath();
    for (const QPointer<cad::Document>& docPtr : d->documents) {
        if (docPtr.isNull()) continue;
        const QString canonicalOpenPath = QFileInfo(docPtr->fileName()).canonicalFilePath();
        if (!canonicalOpenPath.isEmpty() && canonicalOpenPath == canonicalNewPath) {
            qDebug() << "[DocumentManager] Document already open:" << filePath;
            setCurrentDocument(docPtr.data());
            return docPtr.data();
        }
    }

    // 建立新文件並載入
    cad::Document* doc = new cad::Document(this);

    if (!doc->load(filePath)) {
        qWarning() << "[DocumentManager] Failed to load document:" << filePath;
        delete doc;
        return nullptr;
    }

    // 連接信號
    connect(doc, &cad::Document::modifiedChanged,
            this, &DocumentManager::onDocumentModified);
    connect(doc, &cad::Document::fileNameChanged,
            this, &DocumentManager::onDocumentFileNameChanged);

    // 加入管理列表
    d->documents.append(doc);
    setCurrentDocument(doc);

    qDebug() << "[DocumentManager] Document opened. Total documents:" << d->documents.size();

    Q_EMIT documentOpened(doc);
    Q_EMIT documentCountChanged(d->documents.size());

    return doc;
}

bool DocumentManager::closeDocument(cad::Document* document, bool force) {
    if (!document) {
        return false;
    }

    QString docName = document->fileName();
    qDebug() << "[DocumentManager] Closing document:" << docName << "Force:" << force;

    // 檢查未儲存變更
    if (!force && document->isModified()) {
        qWarning() << "[DocumentManager] Document has unsaved changes:" << docName;
        // TODO: 實際應用中應該彈出對話框詢問使用者
        // 這裡暫時允許關閉
    }

    // 從列表中移除
    for (int i = 0; i < d->documents.size(); ++i) {
        if (d->documents[i] == document) {
            d->documents.removeAt(i);
            break;
        }
    }

    // 如果關閉的是當前文件，切換到其他文件
    if (d->currentDocument == document) {
        if (!d->documents.isEmpty()) {
            setCurrentDocument(d->documents.first());
        } else {
            setCurrentDocument(nullptr);
        }
    }

    // 斷開信號連接
    disconnect(document, nullptr, this, nullptr);

    // 刪除文件
    document->deleteLater();

    qDebug() << "[DocumentManager] Document closed. Remaining:" << d->documents.size();

    Q_EMIT documentClosed(docName);
    Q_EMIT documentCountChanged(d->documents.size());

    return true;
}

bool DocumentManager::closeAll(bool force) {
    qDebug() << "[DocumentManager] Closing all documents. Force:" << force;

    if (!force && hasUnsavedChanges()) {
        qWarning() << "[DocumentManager] Some documents have unsaved changes";
        // TODO: 彈出對話框確認
    }

    // 複製列表，因為 closeDocument 會修改它
    QVector<cad::Document*> docsToClose;
    for (const QPointer<cad::Document>& docPtr : d->documents) {
        if (!docPtr.isNull()) {
            docsToClose.append(docPtr.data());
        }
    }

    // 關閉所有文件
    for (cad::Document* doc : docsToClose) {
        closeDocument(doc, force);
    }

    qDebug() << "[DocumentManager] All documents closed";
    return true;
}

QVector<cad::Document*> DocumentManager::documents() const {
    QVector<cad::Document*> result;
    for (const QPointer<cad::Document>& docPtr : d->documents) {
        if (!docPtr.isNull()) {
            result.append(docPtr.data());
        }
    }
    return result;
}

int DocumentManager::documentCount() const {
    // 只計算有效的文件
    int count = 0;
    for (const QPointer<cad::Document>& docPtr : d->documents) {
        if (!docPtr.isNull()) {
            count++;
        }
    }
    return count;
}

void DocumentManager::setCurrentDocument(cad::Document* document) {
    if (d->currentDocument == document) {
        return;
    }

    d->currentDocument = document;

    QString docName = document ? document->fileName() : "None";
    qDebug() << "[DocumentManager] Current document changed to:" << docName;

    Q_EMIT currentDocumentChanged(document);
}

cad::Document* DocumentManager::currentDocument() const {
    return d->currentDocument.data();
}

cad::Document* DocumentManager::findDocument(const QString& fileName) const {
    for (const QPointer<cad::Document>& docPtr : d->documents) {
        if (!docPtr.isNull() && docPtr->fileName() == fileName) {
            return docPtr.data();
        }
    }
    return nullptr;
}

bool DocumentManager::hasUnsavedChanges() const {
    for (const QPointer<cad::Document>& docPtr : d->documents) {
        if (!docPtr.isNull() && docPtr->isModified()) {
            return true;
        }
    }
    return false;
}

QString DocumentManager::generateUniqueName() {
    QString name;
    do {
        name = QString("Untitled_%1.aicad").arg(d->nextDocumentNumber++);
    } while (findDocument(name) != nullptr);

    return name;
}

void DocumentManager::onDocumentModified(bool modified) {
    cad::Document* doc = qobject_cast<cad::Document*>(sender());
    if (doc) {
        qDebug() << "[DocumentManager] Document" << doc->fileName()
                 << "modified:" << modified;
    }
}

void DocumentManager::onDocumentFileNameChanged(const QString& fileName) {
    cad::Document* doc = qobject_cast<cad::Document*>(sender());
    if (doc) {
        qDebug() << "[DocumentManager] Document file name changed to:" << fileName;
    }
}

} // namespace core
} // namespace aicad
