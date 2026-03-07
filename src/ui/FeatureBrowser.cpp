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
#include <QHeaderView>
#include <QDebug>

namespace aicad {
namespace ui {

class FeatureBrowser::Private {
public:
    Private()
        : document(nullptr)
        , treeWidget(nullptr)
    {}

    cad::Document* document;
    QTreeWidget*   treeWidget;
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
    d->treeWidget = new QTreeWidget(this);

    // ✅ 修正：設定 3 欄（名稱 / 類型 / 可見性）
    d->treeWidget->setColumnCount(3);
    d->treeWidget->setHeaderLabels({"Name", "Type", "Visible"});

    // 調整欄寬
    d->treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    d->treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    d->treeWidget->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    d->treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    d->treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    d->treeWidget->setAlternatingRowColors(true);

    setWidget(d->treeWidget);
}

void FeatureBrowser::connectSignals() {
    connect(d->treeWidget, &QTreeWidget::itemClicked,
            this, &FeatureBrowser::onItemClicked);

    connect(d->treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &FeatureBrowser::onItemDoubleClicked);

    connect(d->treeWidget, &QTreeWidget::customContextMenuRequested,
            this, &FeatureBrowser::onCustomContextMenu);

    // ✅ 可見性切換（column 2）
    connect(d->treeWidget, &QTreeWidget::itemChanged,
            this, &FeatureBrowser::onItemVisibilityToggled);
}

void FeatureBrowser::setCurrentDocument(cad::Document* document) {
    qDebug() << "[FeatureBrowser] Setting current document:"
             << (document ? document->fileName() : "null");

    // 斷開舊文件
    if (d->document) {
        disconnect(d->document, nullptr, this, nullptr);
    }

    d->document = document;

    if (d->document) {
        connect(d->document, &cad::Document::treeStructureChanged,
                this, &FeatureBrowser::onTreeStructureChanged);

        connect(d->document, &cad::Document::featureAdded,
                this, &FeatureBrowser::onTreeStructureChanged);

        connect(d->document, &cad::Document::featureRemoved,
                this, &FeatureBrowser::onTreeStructureChanged);

        // ✅ 新增：監聽 allFeaturesLoaded（載入檔案完成後刷新）
        connect(d->document, &cad::Document::allFeaturesLoaded,
                this, &FeatureBrowser::onTreeStructureChanged);
    }

    refresh();
}

void FeatureBrowser::refresh() {
    qDebug() << "[FeatureBrowser] Refreshing...";

    // ✅ 暫停 itemChanged 信號，避免 rebuild 時觸發 visibility toggle
    d->treeWidget->blockSignals(true);

    clear();
    m_itemMap.clear();

    if (!d->document) {
        qDebug() << "[FeatureBrowser] No document to display";
        d->treeWidget->blockSignals(false);
        return;
    }

    QVector<FeatureTreeItem> items = d->document->getFeatureTreeItems();
    qDebug() << "[FeatureBrowser] Building tree with" << items.size() << "items";

    buildTreeFromData(items);

    d->treeWidget->expandAll();
    d->treeWidget->resizeColumnToContents(0);
    d->treeWidget->resizeColumnToContents(1);

    d->treeWidget->blockSignals(false);
}

void FeatureBrowser::buildTreeFromData(const QVector<FeatureTreeItem>& items) {
    // 第一遍：建立頂層項目
    for (const FeatureTreeItem& itemData : items) {
        if (itemData.parentId.isEmpty()) {
            QTreeWidgetItem* item = createTreeWidgetItem(itemData);
            d->treeWidget->addTopLevelItem(item);
            m_itemMap[itemData.id] = item;
        }
    }

    // 第二遍：建立子項目
    for (const FeatureTreeItem& itemData : items) {
        if (!itemData.parentId.isEmpty()) {
            QTreeWidgetItem* item = createTreeWidgetItem(itemData);

            QTreeWidgetItem* parentItem = m_itemMap.value(itemData.parentId);
            if (parentItem) {
                parentItem->addChild(item);
            } else {
                // 找不到父項目，加入頂層
                qWarning() << "[FeatureBrowser] Parent not found for:"
                           << itemData.name << "parentId:" << itemData.parentId;
                d->treeWidget->addTopLevelItem(item);
            }

            m_itemMap[itemData.id] = item;
        }
    }

    d->treeWidget->blockSignals(false); // ← ADD THIS
    qDebug() << "[FeatureBrowser] Tree built with" << m_itemMap.size() << "items";
}

QTreeWidgetItem* FeatureBrowser::createTreeWidgetItem(const FeatureTreeItem& itemData) {
    QTreeWidgetItem* item = new QTreeWidgetItem();

    // ✅ Column 0：名稱 + 圖示
    item->setText(0, itemData.name);
    item->setIcon(0, getIconForType(itemData.type));

    // ✅ Column 1：類型字串
    item->setText(1, itemData.typeString());

    // ✅ Column 2：可見性 checkbox
    item->setCheckState(2, itemData.visible ? Qt::Checked : Qt::Unchecked);

    // ✅ 儲存 ID（字串形式，不強制 toInt()）
    item->setData(0, Qt::UserRole,     itemData.id);
    item->setData(0, Qt::UserRole + 1, static_cast<int>(itemData.type));

    // ✅ 資料夾：不可選取，粗體
    if (itemData.type == ItemType::Folder) {
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
    }

    return item;
}

QIcon FeatureBrowser::getIconForType(ItemType type) {
    switch (type) {
    case ItemType::Folder:  return QIcon(":/icons/folder.png");
    case ItemType::Origin:  return QIcon(":/icons/origin.png");
    case ItemType::Plane:   return QIcon(":/icons/plane.png");
    case ItemType::Axis:    return QIcon(":/icons/axis.png");
    case ItemType::Point:   return QIcon(":/icons/point.png");
    case ItemType::Sketch:  return QIcon(":/icons/sketch.png");
    case ItemType::Extrude: return QIcon(":/icons/extrude.png");
    default:                return QIcon();
    }
}

void FeatureBrowser::onTreeStructureChanged() {
    qDebug() << "[FeatureBrowser] Tree structure changed, refreshing...";
    refresh();
}

void FeatureBrowser::onItemVisibilityToggled(QTreeWidgetItem* item, int column) {
    if (column != 2) return;  // 只處理可見性欄位

    // ✅ ID 保持字串，不轉 int
    QString itemId = item->data(0, Qt::UserRole).toString();
    bool visible   = (item->checkState(2) == Qt::Checked);

    qDebug() << "[FeatureBrowser] Visibility toggled:" << itemId << visible;

    core::EventBus* bus = core::Application::instance()->eventBus();
    QVariantMap data;
    data["itemId"]  = itemId;
    data["visible"] = visible;
    bus->publish("feature.visibility-changed", data);
}

void FeatureBrowser::clear() {
    d->treeWidget->clear();
}

void FeatureBrowser::selectFeature(const QString& featureId) {
    qDebug() << "[FeatureBrowser] Selecting feature:" << featureId;

    QTreeWidgetItemIterator it(d->treeWidget);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toString() == featureId) {
            d->treeWidget->setCurrentItem(*it);
            (*it)->setSelected(true);
            return;
        }
        ++it;
    }

    qWarning() << "[FeatureBrowser] Feature not found:" << featureId;
}

// ✅ 保留 int 版本以相容舊呼叫端（轉成 QString 後委派）
void FeatureBrowser::selectFeature(int featureId) {
    selectFeature(QString::number(featureId));
}

void FeatureBrowser::onItemClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item) return;

    QString itemId   = item->data(0, Qt::UserRole).toString();
    int     itemType = item->data(0, Qt::UserRole + 1).toInt();

    qDebug() << "[FeatureBrowser] Item clicked:" << item->text(0) << "ID:" << itemId;

    // 資料夾不發選取事件
    if (static_cast<ItemType>(itemType) == ItemType::Folder) {
        return;
    }

    // ✅ 改發字串版信號（featureSelectedById），舊 int 版也一併 emit
    Q_EMIT featureSelectedById(itemId);
    Q_EMIT featureSelected(0);  // 相容舊槽，傳 0 表示「以字串為準」
}

void FeatureBrowser::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item) return;

    QString itemId = item->data(0, Qt::UserRole).toString();
    qDebug() << "[FeatureBrowser] Item double-clicked:" << item->text(0) << "ID:" << itemId;

    Q_EMIT featureDoubleClickedById(itemId);
    Q_EMIT featureDoubleClicked(0);
}

void FeatureBrowser::onCustomContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = d->treeWidget->itemAt(pos);
    if (!item) return;

    QString itemId  = item->data(0, Qt::UserRole).toString();
    QPoint globalPos = d->treeWidget->mapToGlobal(pos);

    qDebug() << "[FeatureBrowser] Context menu for:" << item->text(0);

    Q_EMIT featureContextMenuById(itemId, globalPos);
    Q_EMIT featureContextMenu(0, globalPos);
}

} // namespace ui
} // namespace aicad
