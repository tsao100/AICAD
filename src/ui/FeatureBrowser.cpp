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
#include "FeatureTreeItem.h"
#include "cad/Document.h"

#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QTreeWidgetItem>
#include <QMenu>
#include <QHeaderView>
#include <QDebug>

namespace aicad {
namespace ui {

// ─── Add before FeatureBrowser class in FeatureBrowser.cpp ───────────────────
class AccessibleTreeWidget : public QTreeWidget {
public:
    explicit AccessibleTreeWidget(QWidget* parent = nullptr)
        : QTreeWidget(parent) {}

    QTreeWidgetItem* itemFromIdx(const QModelIndex& index) const {
        return itemFromIndex(index);   // calls the protected method
    }
};

// ─── Custom delegate ─────────────────────────────────────────────────────────
class FeatureItemDelegate : public QStyledItemDelegate {
public:
    static constexpr int EYE_ICON_W = 20;
    static constexpr int PADDING    = 4;

    explicit FeatureItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    // ─── FeatureItemDelegate::paint() — eye LEFT, feature icon, then name ────────
    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem o = opt;
        initStyleOption(&o, index);
        o.text = QString(); // suppress default text/icon rendering

        p->save();

        QStyle* style = o.widget ? o.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &o, p, o.widget);

        QRect r = o.rect.adjusted(PADDING, 0, -PADDING, 0);

        // ── 1. Eye icon (leftmost) ──────────────────────────────────────────────
        bool visible = index.data(Qt::UserRole + 2).toBool();
        QIcon eyeIcon = visible
                            ? QIcon(":/icons/eyeOpen.png")
                            : QIcon(":/icons/eyeClose.png");

        QRect eyeRect = eyeIconRect(o.rect);          // fixed left position
        eyeIcon.paint(p, eyeRect, Qt::AlignCenter,
                      (opt.state & QStyle::State_MouseOver)
                          ? QIcon::Active : QIcon::Normal);
        r.setLeft(eyeRect.right() + PADDING);

        // ── 2. Feature icon ─────────────────────────────────────────────────────
        QIcon featureIcon = index.data(Qt::DecorationRole).value<QIcon>();
        if (!featureIcon.isNull()) {
            QRect iconRect(r.left(), r.top() + (r.height() - 16) / 2, 16, 16);
            featureIcon.paint(p, iconRect);
            r.setLeft(iconRect.right() + PADDING);
        }

        // ── 3. Name text ─────────────────────────────────────────────────────────
        bool isFolder = (index.data(Qt::UserRole + 1).toInt()
                         == static_cast<int>(ItemType::Folder) ||
                         index.data(Qt::UserRole + 1).toInt()
                         == static_cast<int>(ItemType::Railway));
        if (isFolder) {
            QFont f = p->font(); f.setBold(true); p->setFont(f);
        }
        p->setPen((opt.state & QStyle::State_Selected)
                      ? opt.palette.highlightedText().color()
                      : opt.palette.text().color());
        p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, o.text.isEmpty()
                                                             ? index.data(Qt::DisplayRole).toString() : o.text);

        p->restore();
    }

    // Eye icon is anchored to the LEFT of the full item rect (before indent)
    QRect eyeIconRect(const QRect& itemRect) const {
        return QRect(itemRect.left(),                           // ← left edge
                     itemRect.top() + (itemRect.height() - 16) / 2,
                     EYE_ICON_W, 16);
    }

    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& opt,
                     const QModelIndex& index) override
    {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (eyeIconRect(opt.rect).contains(me->pos())) {
                // ── E.2 VAlignment eye toggle → 開啟/關閉 VAlignProfileView ──
                // eyeOpen = 開啟縱斷面 dock；eyeClose = 關閉
                int itype = index.data(Qt::UserRole + 1).toInt();
                if (static_cast<ui::ItemType>(itype) == ui::ItemType::VAlignment) {
                    // 取出 itemId（格式 "valign_<tclId>"）
                    QString vid = index.data(Qt::UserRole).toString();
                    bool cur = index.data(Qt::UserRole + 2).toBool();
                    bool next = !cur;
                    // 更新 eye icon
                    if (auto* tw = static_cast<AccessibleTreeWidget*>(
                            const_cast<QWidget*>(opt.widget))) {
                        if (QTreeWidgetItem* item = tw->itemFromIdx(index)) {
                            tw->blockSignals(true);
                            item->setData(0, Qt::UserRole + 2, next);
                            tw->blockSignals(false);
                            tw->update(index);
                        }
                    }
                    // 發布 valign-visibility-changed
                    if (!vid.isEmpty()) {
                        const QString tclId = vid.startsWith("valign_")
                                              ? vid.mid(7) : vid;
                        core::EventBus* bus = core::Application::instance()->eventBus();
                        QVariantMap d2;
                        d2["tclId"]   = tclId;
                        d2["visible"] = next;
                        bus->publish("railway.valign-visibility-changed", d2);
                        qDebug() << "[FeatureBrowser] VAlign eye:" << tclId << "visible=" << next;
                    }
                    return true;
                }

                bool cur = index.data(Qt::UserRole + 2).toBool();
                bool next = !cur;

                // ── 1. Update item data directly (QTreeWidget model requires this) ──
                //    model->setData on a QTreeWidget doesn't always emit dataChanged,
                //    so we update the item directly and publish EventBus ourselves.
                if (auto* tw = static_cast<AccessibleTreeWidget*>(
                        const_cast<QWidget*>(opt.widget))) {
                    if (QTreeWidgetItem* item = tw->itemFromIdx(index)) {
                        // Block treeWidget signals to avoid spurious itemChanged
                        tw->blockSignals(true);
                        item->setData(0, Qt::UserRole + 2, next);
                        tw->blockSignals(false);
                        tw->update(index);   // repaint eye icon
                    }
                }

                // ── 2. Publish EventBus directly ─────────────────────────────────
                QString id = index.data(Qt::UserRole).toString();
                if (!id.isEmpty()) {
                    qDebug() << "[FeatureBrowser] Eye toggled:" << id << "visible=" << next;
                    core::EventBus* bus = core::Application::instance()->eventBus();
                    QVariantMap data;
                    data["itemId"]  = id;
                    data["visible"] = next;
                    bus->publish("feature.visibility-changed", data);
                }
                return true;
            }
        }
        return QStyledItemDelegate::editorEvent(event, model, opt, index);
    }
};

class FeatureBrowser::Private {
public:
    Private()
        : document(nullptr)
        , treeWidget(nullptr)
    {}

    cad::Document* document;
    AccessibleTreeWidget*   treeWidget; // ← was QTreeWidget*
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

// ─── setupUI() — plus/minus branch indicators via stylesheet ─────────────────
void FeatureBrowser::setupUI() {
    d->treeWidget = new AccessibleTreeWidget(this);
    d->treeWidget->setColumnCount(1);
    d->treeWidget->setHeaderHidden(true);
    d->treeWidget->setRootIsDecorated(true);
    d->treeWidget->setIndentation(20);
    d->treeWidget->setMouseTracking(true);
    d->treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    d->treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    d->treeWidget->setItemDelegate(new FeatureItemDelegate(d->treeWidget));

    // d->treeWidget->setStyleSheet(R"(
    //     QTreeWidget {
    //         background: #f0f0f0;
    //         color: #1a1a1a;
    //         border: none;
    //         font-size: 12px;
    //     }
    //     QTreeWidget::item          { height: 24px; }
    //     QTreeWidget::item:selected { background: #0078d7; color: #ffffff; }
    //     QTreeWidget::item:hover    { background: #d6e8fb; color: #1a1a1a; }
    //     QTreeWidget::branch        { background: #f0f0f0; }
    // )");

    setWidget(d->treeWidget);
}

void FeatureBrowser::connectSignals() {
    connect(d->treeWidget, &QTreeWidget::itemClicked,
            this, &FeatureBrowser::onItemClicked);

    connect(d->treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &FeatureBrowser::onItemDoubleClicked);

    connect(d->treeWidget, &QTreeWidget::customContextMenuRequested,
            this, &FeatureBrowser::onCustomContextMenu);

    // Eye icon toggle is handled directly in delegate editorEvent above.
    // dataChanged is connected only to keep the model in sync; the actual
    // EventBus publish happens inside editorEvent to avoid double-firing.
    connect(d->treeWidget->model(), &QAbstractItemModel::dataChanged,
            this, [](const QModelIndex&, const QModelIndex&, const QVector<int>&) {
                // intentionally empty — publish is done in delegate editorEvent
            });
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

    item->setText(0, itemData.name);
    item->setIcon(0, getIconForType(itemData.type));

    // UserRole     = item id (QString)
    // UserRole + 1 = item type (int)
    // UserRole + 2 = visibility (bool)  ← read by delegate
    item->setData(0, Qt::UserRole,     itemData.id);
    item->setData(0, Qt::UserRole + 1, static_cast<int>(itemData.type));
    item->setData(0, Qt::UserRole + 2, itemData.visible);

    if (itemData.type == ItemType::Folder ||
        itemData.type == ItemType::Railway) {
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    }

    return item;
}
QIcon FeatureBrowser::getIconForType(ItemType type) {
    switch (type) {
    case ItemType::Folder:          return QIcon(":/icons/folder.png");
    case ItemType::Origin:          return QIcon(":/icons/origin.png");
    case ItemType::Plane:           return QIcon(":/icons/plane.png");
    case ItemType::Axis:            return QIcon(":/icons/axis.png");
    case ItemType::Point:           return QIcon(":/icons/point.png");
    case ItemType::Sketch:          return QIcon(":/icons/sketch.png");
    case ItemType::Extrude:         return QIcon(":/icons/extrude.png");
    case ItemType::Railway:         return QIcon(":/icons/folder.png");      // 路線資料夾
    case ItemType::TrackCenterLine: return QIcon(":/icons/sketch.png");      // 單線路
    case ItemType::VAlignment:      return QIcon(":/icons/plane.png");       // 縱斷面
    default:                        return QIcon();
    }
}

void FeatureBrowser::onTreeStructureChanged() {
    qDebug() << "[FeatureBrowser] Tree structure changed, refreshing...";
    refresh();
}

void FeatureBrowser::onItemVisibilityToggled(QTreeWidgetItem* item, int column) {
    // Visibility toggling is now handled entirely in FeatureItemDelegate::editorEvent.
    // This slot is kept to avoid linker errors but does nothing to prevent double-publish.
    Q_UNUSED(item)
    Q_UNUSED(column)
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
    if (static_cast<ItemType>(itemType) == ItemType::Folder ||
        static_cast<ItemType>(itemType) == ItemType::Railway) {
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

    QString  itemId   = item->data(0, Qt::UserRole).toString();
    ItemType itemType = static_cast<ItemType>(
        item->data(0, Qt::UserRole + 1).toInt());
    QPoint   globalPos = d->treeWidget->mapToGlobal(pos);

    if (itemType == ItemType::Folder || itemType == ItemType::Origin)
        return;

    // ── Railway 資料夾：提供新增線路 ─────────────────────────────────────
    if (itemType == ItemType::Railway) {
        QMenu menu(this);
        QAction* actAdd = menu.addAction(tr("新增線路中心線"));
        connect(actAdd, &QAction::triggered, this, [this] {
            Q_EMIT editAlignmentRequested(QString());  // 空 id = 新增
        });
        menu.exec(globalPos);
        return;
    }

    // ── VAlignment 縱斷面 ─────────────────────────────────────────────────
    if (itemType == ItemType::VAlignment) {
        // itemId 格式 = "valign_<tclId>"
        const QString tclId = itemId.mid(7);
        QMenu menu(this);
        QAction* actEdit = menu.addAction(
            QIcon(":/icons/plane.png"), tr("編輯縱斷面 (Edit VAlignment)"));
        connect(actEdit, &QAction::triggered, this, [this, tclId] {
            // 設定 vAlignVisible = true 就會觸發 valign-visibility-changed
            core::EventBus* bus = core::Application::instance()->eventBus();
            QVariantMap d2;
            d2["tclId"]   = tclId;
            d2["visible"] = true;
            bus->publish("railway.valign-visibility-changed", d2);
        });
        menu.exec(globalPos);
        return;
    }

    // ── TrackCenterLine 專屬選單 ──────────────────────────────────────────
    if (itemType == ItemType::TrackCenterLine) {
        QMenu menu(this);

        QAction* actEdit = menu.addAction(tr("編輯線形 (Edit Alignment)"));
        connect(actEdit, &QAction::triggered, this, [this, itemId] {
            Q_EMIT editAlignmentRequested(itemId);
        });

        menu.addSeparator();

        QAction* actTable = menu.addAction(tr("線形資料表"));
        connect(actTable, &QAction::triggered, this, [this, itemId] {
            Q_EMIT showAlignmentDataTableRequested(itemId);
        });

        menu.addSeparator();

        QAction* actRename = menu.addAction(tr("重新命名"));
        connect(actRename, &QAction::triggered, this, [this, itemId] {
            Q_EMIT renameTrackRequested(itemId);
        });

        QAction* actDelete = menu.addAction(QIcon(":/icons/cut.png"), tr("刪除"));
        connect(actDelete, &QAction::triggered, this, [this, itemId] {
            Q_EMIT deleteTrackRequested(itemId);
        });

        menu.exec(globalPos);
        return;
    }

    QMenu menu(this);

    // ── 編輯草圖（僅 Sketch）────────────────────────────────────────────────
    if (itemType == ItemType::Sketch) {
        QAction* actEdit = menu.addAction(
            QIcon(":/icons/sketch.png"), tr("編輯草圖"));
        connect(actEdit, &QAction::triggered, this, [this, itemId] {
            Q_EMIT editSketchRequested(itemId);
        });
        menu.addSeparator();
    }

    // ── 刪除（所有可刪類型）─────────────────────────────────────────────────
    QAction* actDelete = menu.addAction(
        QIcon(":/icons/cut.png"), tr("刪除"));
    connect(actDelete, &QAction::triggered, this, [this, itemId] {
        Q_EMIT deleteFeatureRequested(itemId);
    });

    // ── 剖面（僅 Sketch）────────────────────────────────────────────────────
    if (itemType == ItemType::Sketch) {
        menu.addSeparator();
        QAction* actSection = menu.addAction(tr("剖面"));
        connect(actSection, &QAction::triggered, this, [this, itemId] {
            Q_EMIT sectionViewRequested(itemId);
        });
    }

    menu.exec(globalPos);
}
} // namespace ui
} // namespace aicad
