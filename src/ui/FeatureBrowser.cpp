/**
 * @file FeatureBrowser.cpp
 * @brief FeatureBrowser 類別實作
 * @author James
 * @date 2025-01-07
 */

#include "FeatureBrowser.h"
#include "cad/Document.h"

#include <QVBoxLayout>
#include <QTreeWidgetItem>
#include <QMenu>
#include <QDebug>

namespace aicad {
namespace ui {

class FeatureBrowser::Private {
public:
    Private()
        : document(nullptr)
        , treeWidget(nullptr)
    {
    }
    
    cad::Document* document;
    QTreeWidget* treeWidget;
};

FeatureBrowser::FeatureBrowser(QWidget* parent)
    : QDockWidget("Feature Browser", parent)
    , d(new Private())
{
    qDebug() << "[FeatureBrowser] Created";
    
    setupUI();
    connectSignals();
    
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
}

FeatureBrowser::~FeatureBrowser() {
    qDebug() << "[FeatureBrowser] Destroyed";
    delete d;
}

void FeatureBrowser::setupUI() {
    // 建立樹狀視圖
    d->treeWidget = new QTreeWidget(this);
    d->treeWidget->setHeaderLabel("Features");
    d->treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    d->treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    d->treeWidget->setAlternatingRowColors(true);
    
    // 設定欄位
    d->treeWidget->setColumnCount(2);
    QStringList headers;
    headers << "Name" << "Type";
    d->treeWidget->setHeaderLabels(headers);
    
    setWidget(d->treeWidget);
}

void FeatureBrowser::connectSignals() {
    connect(d->treeWidget, &QTreeWidget::itemClicked,
            this, &FeatureBrowser::onItemClicked);
    
    connect(d->treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &FeatureBrowser::onItemDoubleClicked);
    
    connect(d->treeWidget, &QTreeWidget::customContextMenuRequested,
            this, &FeatureBrowser::onCustomContextMenu);
}

void FeatureBrowser::setCurrentDocument(cad::Document* document) {
    qDebug() << "[FeatureBrowser] Setting current document:" 
             << (document ? document->fileName() : "null");
    
    d->document = document;
    refresh();
}

void FeatureBrowser::refresh() {
    qDebug() << "[FeatureBrowser] Refreshing...";
    
    clear();
    
    if (!d->document) {
        qDebug() << "[FeatureBrowser] No document to display";
        return;
    }
    
    // TODO: 實際實作時，應該從 Document 取得特徵列表
    // 這裡先用範例資料
    
    QTreeWidgetItem* sketchItem = new QTreeWidgetItem(d->treeWidget);
    sketchItem->setText(0, "Sketch 1");
    sketchItem->setText(1, "Sketch");
    sketchItem->setData(0, Qt::UserRole, 1); // Feature ID
    
    QTreeWidgetItem* extrudeItem = new QTreeWidgetItem(d->treeWidget);
    extrudeItem->setText(0, "Extrude 1");
    extrudeItem->setText(1, "Extrude");
    extrudeItem->setData(0, Qt::UserRole, 2); // Feature ID
    
    d->treeWidget->expandAll();
    d->treeWidget->resizeColumnToContents(0);
    d->treeWidget->resizeColumnToContents(1);
    
    qDebug() << "[FeatureBrowser] Refresh completed. Items:" << d->treeWidget->topLevelItemCount();
}

void FeatureBrowser::clear() {
    d->treeWidget->clear();
}

void FeatureBrowser::selectFeature(int featureId) {
    qDebug() << "[FeatureBrowser] Selecting feature:" << featureId;
    
    // 遍歷所有項目找到對應的特徵
    QTreeWidgetItemIterator it(d->treeWidget);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == featureId) {
            d->treeWidget->setCurrentItem(*it);
            (*it)->setSelected(true);
            qDebug() << "[FeatureBrowser] Feature selected:" << (*it)->text(0);
            return;
        }
        ++it;
    }
    
    qWarning() << "[FeatureBrowser] Feature not found:" << featureId;
}

void FeatureBrowser::onItemClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    
    if (!item) {
        return;
    }
    
    int featureId = item->data(0, Qt::UserRole).toInt();
    qDebug() << "[FeatureBrowser] Item clicked:" << item->text(0) << "ID:" << featureId;
    
    Q_EMIT featureSelected(featureId);
}

void FeatureBrowser::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    
    if (!item) {
        return;
    }
    
    int featureId = item->data(0, Qt::UserRole).toInt();
    qDebug() << "[FeatureBrowser] Item double-clicked:" << item->text(0) << "ID:" << featureId;
    
    Q_EMIT featureDoubleClicked(featureId);
}

void FeatureBrowser::onCustomContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = d->treeWidget->itemAt(pos);
    
    if (!item) {
        return;
    }
    
    int featureId = item->data(0, Qt::UserRole).toInt();
    QPoint globalPos = d->treeWidget->mapToGlobal(pos);
    
    qDebug() << "[FeatureBrowser] Context menu requested for:" 
             << item->text(0) << "at" << globalPos;
    
    Q_EMIT featureContextMenu(featureId, globalPos);
}

} // namespace ui
} // namespace aicad