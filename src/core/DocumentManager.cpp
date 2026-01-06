/**
 * @file DocumentManager.cpp
 * @brief DocumentManager 類別實作
 * @author Jack
 * @date 2024-12-04
 */

#include "DocumentManager.h"
#include "cad/Document.h"

#include <QDebug>
#include <QVector>
#include <QPointer>
#include <QFileInfo>

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
    
    // 建立新文件 (目前使用樁類別)
    cad::Document* doc = new cad::Document(this);
    doc->setFileName(documentName);
    doc->setObjectName(documentName);
    
    // 加入管理列表
    d->documents.append(doc);
    
    // 設為當前文件
    setCurrentDocument(doc);
    
    qDebug() << "[DocumentManager] Document created. Total documents:" << d->documents.size();
    
    Q_EMIT documentCreated(documentName);
    Q_EMIT documentCountChanged(d->documents.size());
    
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
    
    // 檢查是否已開啟
    QString fileName = fileInfo.fileName();
    for (const QPointer<cad::Document>& docPtr : d->documents) {
        if (!docPtr.isNull() && docPtr->fileName() == fileName) {
            qDebug() << "[DocumentManager] Document already open:" << fileName;
            setCurrentDocument(docPtr.data());
            return docPtr.data();
        }
    }
    
    // 建立新文件並載入
    cad::Document* doc = new cad::Document(this);
    doc->setFileName(fileName);
    doc->setObjectName(fileName);
    
    if (!doc->load(filePath)) {
        qWarning() << "[DocumentManager] Failed to load document:" << filePath;
        delete doc;
        return nullptr;
    }
    
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

cad::Document* DocumentManager::findDocument(const QString& name) const {
    for (const QPointer<cad::Document>& docPtr : d->documents) {
        if (!docPtr.isNull() && docPtr->fileName() == name) {
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
        name = QString("Untitled_%1").arg(d->nextDocumentNumber++);
    } while (findDocument(name) != nullptr);
    
    return name;
}

} // namespace core
} // namespace aicad

