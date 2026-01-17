/**
 * @file FeatureBrowser.cpp
 * @brief FeatureBrowser 類別實作
 * @author James
 * @date 2025-01-07
 */

#include "core/EventBus.h"
#include "core/Application.h"
#include "cad/Feature.h"
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
    
    // ✅ 設定三欄：名稱、類型、可見性
    d->treeWidget->setColumnCount(3);
    QStringList headers;
    headers << "Name" << "Type" << "Visible";
    d->treeWidget->setHeaderLabels(headers);

    // 設定欄位寬度
    d->treeWidget->setColumnWidth(0, 150);
    d->treeWidget->setColumnWidth(1, 80);
    d->treeWidget->setColumnWidth(2, 50);

    // ✅ 啟用項目點擊檢測
    d->treeWidget->setItemsExpandable(true);
    d->treeWidget->setExpandsOnDoubleClick(false);

    setWidget(d->treeWidget);
}

void FeatureBrowser::connectSignals() {
    connect(d->treeWidget, &QTreeWidget::itemClicked,
            this, &FeatureBrowser::onItemClicked);
    
    connect(d->treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &FeatureBrowser::onItemDoubleClicked);
    
    connect(d->treeWidget, &QTreeWidget::customContextMenuRequested,
            this, &FeatureBrowser::onCustomContextMenu);
    // ✅ 可見性切換
    connect(d->treeWidget, &QTreeWidget::itemChanged,
            this, &FeatureBrowser::onItemVisibilityToggled);
}



void FeatureBrowser::setCurrentDocument(cad::Document* document) {
    qDebug() << "[FeatureBrowser] Setting current document:" 
             << (document ? document->fileName() : "null");
    // ✅ 斷開舊文件的信號
    if (d->document) {
        disconnect(d->document, nullptr, this, nullptr);
    }

    d->document = document;

    // ✅ 連接新文件的信號
    if (d->document) {
        connect(d->document, &cad::Document::treeStructureChanged,
                this, &FeatureBrowser::onTreeStructureChanged);

        connect(d->document, &cad::Document::featureAdded,
                this, &FeatureBrowser::onTreeStructureChanged);

        connect(d->document, &cad::Document::featureRemoved,
                this, &FeatureBrowser::onTreeStructureChanged);
    }

    refresh();
}

void FeatureBrowser::refresh() {
    qDebug() << "[FeatureBrowser] Refreshing...";

    clear();
    m_itemMap.clear();

    if (!d->document) {
        qDebug() << "[FeatureBrowser] No document to display";
        return;
    }

    // ✅ 從 Document 取得 tree 結構
    QVector<FeatureTreeItem> items = d->document->getFeatureTreeItems();

    qDebug() << "[FeatureBrowser] Building tree with" << items.size() << "items";

    buildTreeFromData(items);

    d->treeWidget->expandAll();
    d->treeWidget->resizeColumnToContents(0);
}

void FeatureBrowser::buildTreeFromData(const QVector<FeatureTreeItem>& items) {
    // ✅ 第一遍：建立所有頂層項目
    for (const FeatureTreeItem& itemData : items) {
        if (itemData.parentId.isEmpty()) {
            QTreeWidgetItem* item = createTreeWidgetItem(itemData);
            d->treeWidget->addTopLevelItem(item);
            m_itemMap[itemData.id] = item;
        }
    }

    // ✅ 第二遍：建立子項目
    for (const FeatureTreeItem& itemData : items) {
        if (!itemData.parentId.isEmpty()) {
            QTreeWidgetItem* item = createTreeWidgetItem(itemData);

            // 找到父項目
            QTreeWidgetItem* parentItem = m_itemMap.value(itemData.parentId);
            if (parentItem) {
                parentItem->addChild(item);
            } else {
                // 找不到父項目，加入頂層
                d->treeWidget->addTopLevelItem(item);
            }

            m_itemMap[itemData.id] = item;
        }
    }

    qDebug() << "[FeatureBrowser] Tree built with" << m_itemMap.size() << "items";
}

QTreeWidgetItem* FeatureBrowser::createTreeWidgetItem(const FeatureTreeItem& itemData) {
    QTreeWidgetItem* item = new QTreeWidgetItem();

    // ✅ 設定欄位內容
    item->setText(0, itemData.name);
    item->setText(1, itemData.typeString());

    // ✅ 設定可見性勾選框
    item->setCheckState(2, itemData.visible ? Qt::Checked : Qt::Unchecked);

    // ✅ 設定圖示
    item->setIcon(0, getIconForType(itemData.type));

    // ✅ 儲存項目資料
    item->setData(0, Qt::UserRole, itemData.id);
    item->setData(0, Qt::UserRole + 1, static_cast<int>(itemData.type));

    // ✅ 資料夾不可選取（只能展開/收合）
    if (itemData.type == ItemType::Folder) {
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
    }

    return item;
}

QIcon FeatureBrowser::getIconForType(ItemType type) {
    // ✅ 根據類型返回圖示
    switch (type) {
    case ItemType::Folder:
        return QIcon(":/icons/folder.png");
    case ItemType::Origin:
        return QIcon(":/icons/origin.png");
    case ItemType::Plane:
        return QIcon(":/icons/plane.png");
    case ItemType::Axis:
        return QIcon(":/icons/axis.png");
    case ItemType::Point:
        return QIcon(":/icons/point.png");
    case ItemType::Sketch:
        return QIcon(":/icons/sketch.png");
    case ItemType::Extrude:
        return QIcon(":/icons/extrude.png");
    default:
        return QIcon();
    }
}

void FeatureBrowser::onTreeStructureChanged() {
    qDebug() << "[FeatureBrowser] Tree structure changed, refreshing...";
    refresh();
}

void FeatureBrowser::onItemVisibilityToggled(QTreeWidgetItem* item, int column) {
    if (column != 2) return;  // 只處理可見性欄位

    QString itemId = item->data(0, Qt::UserRole).toString();
    bool visible = (item->checkState(2) == Qt::Checked);

    qDebug() << "[FeatureBrowser] Visibility toggled:" << itemId << visible;

    // ✅ 透過 EventBus 發布可見性變更
    core::EventBus* bus = core::Application::instance()->eventBus();

    QVariantMap data;
    data["itemId"] = itemId;
    data["visible"] = visible;

    bus->publish("feature.visibility-changed", data);
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
    if (!item) return;

    QString itemId = item->data(0, Qt::UserRole).toString();
    int itemType = item->data(0, Qt::UserRole + 1).toInt();

    qDebug() << "[FeatureBrowser] Item clicked:" << item->text(0) << "ID:" << itemId;

    // ✅ 資料夾項目不發送選取事件
    if (static_cast<ItemType>(itemType) == ItemType::Folder) {
        return;
    }

    Q_EMIT featureSelected(itemId.toInt());
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
