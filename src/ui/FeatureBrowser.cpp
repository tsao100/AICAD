/**
 * @file FeatureBrowser.cpp
 * @brief FeatureBrowser 類別實作
 * @author James
 * @date 2025-01-07
 */

#include "core/EventBus.h"
#include "core/Application.h"
#include "core/DocumentManager.h"
#include "cad/Feature.h"
#include "FeatureBrowser.h"
#include "FeatureTreeItem.h"
#include "cad/Document.h"
#include "cad/SketchInstance.h"

#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QTreeWidgetItem>
#include <QMenu>
#include <QHeaderView>
#include <QDebug>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QShortcut>
#include <QKeySequence>
#include <QSettings>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QStringList>

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
                         == static_cast<int>(ItemType::Railway) ||
                         index.data(Qt::UserRole + 1).toInt()
                         == static_cast<int>(ItemType::ComponentLayout));
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
                {
                    const auto ty = static_cast<ui::ItemType>(itype);
                    if (ty == ui::ItemType::Drawings || ty == ui::ItemType::DrawingBook ||
                        ty == ui::ItemType::DrawingSheetItem)
                        return true;   // 圖冊／圖紙節點沒有可見性切換
                    if (ty == ui::ItemType::DepotViewFolder || ty == ui::ItemType::DepotViewItem) {
                        // 機廠矩形視圖：切換是否顯示在 View 中（由 UIManager 接手）
                        const bool next = !index.data(Qt::UserRole + 2).toBool();
                        if (auto* tw = static_cast<AccessibleTreeWidget*>(const_cast<QWidget*>(opt.widget))) {
                            if (QTreeWidgetItem* item = tw->itemFromIdx(index)) {
                                tw->blockSignals(true);
                                item->setData(0, Qt::UserRole + 2, next);
                                tw->blockSignals(false);
                                tw->update(index);
                            }
                        }
                        QVariantMap dm;
                        dm["itemId"] = index.data(Qt::UserRole).toString();
                        dm["visible"] = next;
                        core::Application::instance()->eventBus()->publish("depotview.visibility-changed", dm);
                        return true;
                    }
                }
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
                    // EventBus 為同步呼叫：Document / CadView / ViewManager / UIManager 各自會
                    // 重繪（Railway 資料夾還會逐條線路、逐個 overlay 重繪）。用 batch 事件包住，
                    // 讓這一次眼睛切換最多只重繪一次。
                    struct BatchGuard {
                        core::EventBus* b;
                        explicit BatchGuard(core::EventBus* bus) : b(bus) { b->publish("view.batch-begin", QVariant()); }
                        ~BatchGuard() { b->publish("view.batch-end", QVariant()); }
                    } guard(bus);
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

namespace {
/// 剪貼簿目前是否有可貼上的 AICAD 草圖
bool clipboardHasSketch()
{
    const QMimeData* md = QGuiApplication::clipboard()->mimeData();
    return md && md->hasFormat(cad::Document::sketchClipboardMimeType());
}

// 摺疊狀態：依文件檔名存在 QSettings（只記「被摺疊」的項目 id；新項目預設展開）
QString collapsedKey(const QString& docName)
{
    return QStringLiteral("FeatureBrowser/collapsed/") + (docName.isEmpty() ? QStringLiteral("(untitled)") : docName);
}
// 記憶體快取：每個文件的摺疊集合；QSettings 只在第一次讀取，寫入以計時器合併（避免每次點擊都做磁碟 I/O）
QHash<QString, QSet<QString>>& collapsedCache() { static QHash<QString, QSet<QString>> h; return h; }
QSet<QString>& collapsedOf(const QString& docName)
{
    auto& h = collapsedCache();
    auto it = h.find(docName);
    if (it == h.end()) {
        QSettings st;
        const QStringList l = st.value(collapsedKey(docName)).toStringList();
        it = h.insert(docName, QSet<QString>(l.begin(), l.end()));
    }
    return it.value();
}
// 「預設收合」的節點類型（Loft：底下是該分段的所有測站草圖，動輒上百個）。
// 這類節點相反地只記「使用者明確展開過」的 id；沒記錄 = 收合。
bool collapsedByDefault(ItemType t) { return t == ItemType::Loft; }
QString expandedKey(const QString& docName)
{
    return QStringLiteral("FeatureBrowser/expanded/") + (docName.isEmpty() ? QStringLiteral("(untitled)") : docName);
}
QHash<QString, QSet<QString>>& expandedCache() { static QHash<QString, QSet<QString>> h; return h; }
QSet<QString>& expandedOf(const QString& docName)
{
    auto& h = expandedCache();
    auto it = h.find(docName);
    if (it == h.end()) {
        QSettings st;
        const QStringList l = st.value(expandedKey(docName)).toStringList();
        it = h.insert(docName, QSet<QString>(l.begin(), l.end()));
    }
    return it.value();
}
void flushCollapsed()
{
    QSettings st;
    const auto& h = collapsedCache();
    for (auto it = h.begin(); it != h.end(); ++it)
        st.setValue(collapsedKey(it.key()), QStringList(it.value().begin(), it.value().end()));
    const auto& e = expandedCache();
    for (auto it = e.begin(); it != e.end(); ++it)
        st.setValue(expandedKey(it.key()), QStringList(it.value().begin(), it.value().end()));
}
void setCollapsedItem(const QString& docName, const QString& id, bool collapsed, ItemType type)
{
    if (id.isEmpty()) return;
    if (collapsedByDefault(type)) {            // 預設收合的節點：記「展開」
        QSet<QString>& e = expandedOf(docName);
        if (collapsed) e.remove(id); else e.insert(id);
    } else {
        QSet<QString>& s = collapsedOf(docName);
        if (collapsed) s.insert(id); else s.remove(id);
    }
    static bool pending = false;
    if (!pending) {
        pending = true;
        QTimer::singleShot(1000, qApp, []() { pending = false; flushCollapsed(); });
    }
}
} // namespace

void FeatureBrowser::connectSignals() {
    // Ctrl+C / Ctrl+V：僅在樹狀圖取得焦點時有效（WidgetShortcut），不搶走其他視窗的快捷鍵
    {
        auto* scCopy = new QShortcut(QKeySequence::Copy, d->treeWidget);
        scCopy->setContext(Qt::WidgetShortcut);
        connect(scCopy, &QShortcut::activated, this, [this] {
            QTreeWidgetItem* cur = d->treeWidget->currentItem();
            if (!cur) return;
            const ItemType t = static_cast<ItemType>(cur->data(0, Qt::UserRole + 1).toInt());
            if (t != ItemType::Sketch) return;
            const QString id = cur->data(0, Qt::UserRole).toString();
            if (auto* doc = core::Application::instance()->documentManager()->currentDocument())
                if (qobject_cast<cad::SketchInstance*>(doc->findFeature(id))) return;  // 測站不可複製
            Q_EMIT copySketchRequested(id);
        });
        // F2：重新命名目前選取的草圖（測站節點與其他類型不適用）
        auto* scRename = new QShortcut(QKeySequence(Qt::Key_F2), d->treeWidget);
        scRename->setContext(Qt::WidgetShortcut);
        connect(scRename, &QShortcut::activated, this, [this] {
            QTreeWidgetItem* cur = d->treeWidget->currentItem();
            if (!cur) return;
            const ItemType t = static_cast<ItemType>(cur->data(0, Qt::UserRole + 1).toInt());
            if (t != ItemType::Sketch) return;
            const QString id = cur->data(0, Qt::UserRole).toString();
            if (auto* doc = core::Application::instance()->documentManager()->currentDocument())
                if (qobject_cast<cad::SketchInstance*>(doc->findFeature(id))) return;
            Q_EMIT renameSketchRequested(id);
        });
        auto* scPaste = new QShortcut(QKeySequence::Paste, d->treeWidget);
        scPaste->setContext(Qt::WidgetShortcut);
        connect(scPaste, &QShortcut::activated, this, [this] {
            if (clipboardHasSketch()) Q_EMIT pasteSketchRequested();
        });
    }
    auto onExp = [this](QTreeWidgetItem* it, bool expanded) {
        if (!it || !d->document) return;
        const ItemType t = static_cast<ItemType>(it->data(0, Qt::UserRole + 1).toInt());
        setCollapsedItem(d->document->fileName(), it->data(0, Qt::UserRole).toString(), !expanded, t);
    };
    connect(d->treeWidget, &QTreeWidget::itemExpanded, this, [onExp](QTreeWidgetItem* it) { onExp(it, true); });
    connect(d->treeWidget, &QTreeWidget::itemCollapsed, this, [onExp](QTreeWidgetItem* it) { onExp(it, false); });

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
    d->treeWidget->setUpdatesEnabled(false);

    clear();
    m_itemMap.clear();

    if (!d->document) {
        qDebug() << "[FeatureBrowser] No document to display";
        d->treeWidget->setUpdatesEnabled(true);
        d->treeWidget->blockSignals(false);
        return;
    }

    QVector<FeatureTreeItem> items = d->document->getFeatureTreeItems();
    qDebug() << "[FeatureBrowser] Building tree with" << items.size() << "items";

    buildTreeFromData(items);

    {
        // 還原先前的展開／摺疊狀態（只有被摺疊過的才收起，其餘預設展開）
        const QString docName = d->document->fileName();      // 具名區域變數，避免參考綁到暫存物件
        const QSet<QString>& collapsed = collapsedOf(docName);
        const QSet<QString>& expanded  = expandedOf(docName);
        for (auto it = m_itemMap.begin(); it != m_itemMap.end(); ++it) {
            if (!it.value() || it.value()->childCount() <= 0) continue;
            const ItemType t = static_cast<ItemType>(it.value()->data(0, Qt::UserRole + 1).toInt());
            // Loft（測站草圖的父節點）預設收合，使用者展開過才記住；其餘維持「預設展開」
            const bool exp = collapsedByDefault(t) ? expanded.contains(it.key())
                                                   : !collapsed.contains(it.key());
            it.value()->setExpanded(exp);
        }
    }
    d->treeWidget->resizeColumnToContents(0);
    d->treeWidget->resizeColumnToContents(1);

    d->treeWidget->setUpdatesEnabled(true);
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

    // 第二遍：建立子項目。
    // 父項目本身也可能是子項目（階層 ≥ 3：陣列 → Loft → 測站草圖），而 items 的順序是
    // 「建立順序」，測站草圖常在它的 Loft 之前（Loft 是 rebuild 之後才補建的）。
    // 單趟依序處理時，找不到尚未建立的父項目就會被錯放到頂層 —— 所以改成多趟：
    // 每趟只處理「父項目已建立」的項目，直到沒有進展；剩下的（真的找不到父項目／循環）才放頂層。
    // 同一父項目的子項目在同一趟內依 items 順序加入，兄弟順序不變。
    QVector<const FeatureTreeItem*> pending;
    for (const FeatureTreeItem& itemData : items)
        if (!itemData.parentId.isEmpty()) pending.append(&itemData);

    while (!pending.isEmpty()) {
        QVector<const FeatureTreeItem*> deferred;
        for (const FeatureTreeItem* itemData : pending) {
            QTreeWidgetItem* parentItem = m_itemMap.value(itemData->parentId, nullptr);
            if (!parentItem) { deferred.append(itemData); continue; }
            QTreeWidgetItem* item = createTreeWidgetItem(*itemData);
            parentItem->addChild(item);
            m_itemMap[itemData->id] = item;
        }
        if (deferred.size() == pending.size()) {
            // 沒有任何進展：剩下的找不到父項目（或互為父子），加入頂層
            for (const FeatureTreeItem* itemData : deferred) {
                qWarning() << "[FeatureBrowser] Parent not found for:"
                           << itemData->name << "parentId:" << itemData->parentId;
                QTreeWidgetItem* item = createTreeWidgetItem(*itemData);
                d->treeWidget->addTopLevelItem(item);
                m_itemMap[itemData->id] = item;
            }
            break;
        }
        pending = deferred;
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
        itemData.type == ItemType::Railway ||
        itemData.type == ItemType::ComponentLayout ||
        itemData.type == ItemType::Drawings) {
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
    case ItemType::Pattern:         return QIcon(":/icons/sketch.png");      // 沿線斷面陣列（暫沿用 sketch 圖示）
    case ItemType::SlabPlan:        return QIcon(":/icons/sketch.png");      // 道版 XY 平面圖（暫沿用 sketch 圖示）
    case ItemType::Component:       return QIcon(":/icons/extrude.png");     // 3D 組件安裝（暫沿用 extrude 圖示）
    case ItemType::DefinedPlane:    return QIcon(":/icons/plane.png");       // 定義平面
    case ItemType::ComponentLayout: return QIcon(":/icons/folder.png");      // 組件安裝位置（資料夾）
    case ItemType::Loft:            return QIcon(":/icons/extrude.png");     // 放樣實體（暫沿用 extrude 圖示）
    case ItemType::Drawings:        return QIcon(":/icons/folder.png");
    case ItemType::DrawingBook:     return QIcon(":/icons/folder.png");
    case ItemType::DrawingSheetItem:return QIcon(":/icons/plane.png");
    case ItemType::DepotViewFolder: return QIcon(":/icons/folder.png");
    case ItemType::DepotViewItem:   return QIcon(":/icons/sketch.png");
    case ItemType::Chamfer:         return QIcon(":/icons/extrude.png");     // 倒角特徵（暫沿用 extrude 圖示，待補專用圖示）
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
        static_cast<ItemType>(itemType) == ItemType::Railway ||
        static_cast<ItemType>(itemType) == ItemType::ComponentLayout ||
        static_cast<ItemType>(itemType) == ItemType::Drawings ||
        static_cast<ItemType>(itemType) == ItemType::DrawingBook ||
        static_cast<ItemType>(itemType) == ItemType::DepotViewFolder ||
        static_cast<ItemType>(itemType) == ItemType::DepotViewItem ||
        static_cast<ItemType>(itemType) == ItemType::DrawingSheetItem) {
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

    {
        const ItemType ty = static_cast<ItemType>(item->data(0, Qt::UserRole + 1).toInt());
        if (ty == ItemType::DrawingBook || ty == ItemType::DrawingSheetItem) {
            Q_EMIT drawingActionRequested(QStringLiteral("open"), itemId);
            return;
        }
        if (ty == ItemType::Component) {                 // 雙擊組件 → 另開 CADView 編輯來源 *.aicad
            Q_EMIT openComponentSourceRequested(itemId);
            return;
        }
        if (ty == ItemType::ComponentLayout) {           // 雙擊資料夾 → 編輯各組件安裝位置
            Q_EMIT editComponentLayoutRequested();
            return;
        }
        if (ty == ItemType::Drawings || ty == ItemType::DepotViewFolder) return;
        if (ty == ItemType::DepotViewItem) {
            Q_EMIT drawingActionRequested(QStringLiteral("depotEdit"), itemId);
            return;
        }
    }

    Q_EMIT featureDoubleClickedById(itemId);
    Q_EMIT featureDoubleClicked(0);
}

// 哪些節點類型在 View 中有幾何可聚焦（Plane/Axis 為無限大參考、VAlignment 在縱斷面視窗、
// 圖冊／圖紙不在 3D View，皆不適用）
static bool itemSupportsFocus(ItemType t)
{
    switch (t) {
    case ItemType::Sketch:
    case ItemType::Extrude:
    case ItemType::Feature:
    case ItemType::Loft:
    case ItemType::Chamfer:
    case ItemType::Pattern:
    case ItemType::SlabPlan:
    case ItemType::Component:
    case ItemType::DefinedPlane:
    case ItemType::TrackCenterLine:
        return true;
    default:
        return false;
    }
}

// 目前顯示狀態：優先用外部解析器，否則用樹狀圖眼睛狀態
static bool itemCurrentlyVisible(const std::function<int(const QString&)>& resolver,
                                 const QString& id, QTreeWidgetItem* item)
{
    if (resolver) {
        const int r = resolver(id);
        if (r >= 0) return r != 0;
    }
    return item->data(0, Qt::UserRole + 2).toBool();
}

void FeatureBrowser::onCustomContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = d->treeWidget->itemAt(pos);
    if (!item) {
        // 空白處：只提供「貼上草圖」（剪貼簿有草圖資料時）
        if (clipboardHasSketch()) {
            QMenu menu(this);
            QAction* actPaste = menu.addAction(tr("貼上草圖"));
            connect(actPaste, &QAction::triggered, this, [this] {
                Q_EMIT pasteSketchRequested();
            });
            menu.exec(d->treeWidget->mapToGlobal(pos));
        }
        return;
    }

    showItemContextMenu(item, d->treeWidget->mapToGlobal(pos));
}

bool FeatureBrowser::showContextMenuForItem(const QString& itemId, const QPoint& globalPos) {
    QTreeWidgetItem* item = m_itemMap.value(itemId, nullptr);
    if (!item) return false;
    showItemContextMenu(item, globalPos);
    return true;
}

void FeatureBrowser::collapseRailwayFolder() {
    if (QTreeWidgetItem* folder = m_itemMap.value(QStringLiteral("__railway_folder__"), nullptr))
        d->treeWidget->collapseItem(folder);
}

void FeatureBrowser::showItemContextMenu(QTreeWidgetItem* item, const QPoint& globalPos) {
    QString  itemId   = item->data(0, Qt::UserRole).toString();
    ItemType itemType = static_cast<ItemType>(
        item->data(0, Qt::UserRole + 1).toInt());

    if (itemType == ItemType::Folder || itemType == ItemType::Origin)
        return;

    // ── Drawing Sheets：資料夾／圖冊／圖紙 ───────────────────────────────
    if (itemType == ItemType::Drawings) {
        QMenu menu(this);
        QAction* a = menu.addAction(tr("新增平縱面圖冊…"));
        connect(a, &QAction::triggered, this, [this, itemId] {
            Q_EMIT drawingActionRequested(QStringLiteral("newbook"), itemId);
        });
        menu.exec(globalPos);
        return;
    }
    if (itemType == ItemType::DrawingBook) {
        QMenu menu(this);
        auto add = [&](const QString& text, const char* action) {
            QAction* a = menu.addAction(text);
            const QString act = QString::fromLatin1(action);
            connect(a, &QAction::triggered, this, [this, act, itemId] {
                Q_EMIT drawingActionRequested(act, itemId);
            });
        };
        add(tr("開啟（第一張）"), "open");
        menu.addSeparator();
        add(tr("圖冊設定（全域）…"), "bookSettings");
        add(tr("重新計算全部圖"), "regen");
        add(tr("調整圖順序／重建圖目錄…"), "index");
        add(tr("匯出圖冊 PDF…"), "exportPdf");
        menu.addSeparator();
        add(tr("重新命名…"), "rename");
        add(tr("刪除圖冊"), "deleteBook");
        menu.exec(globalPos);
        return;
    }
    if (itemType == ItemType::DepotViewFolder || itemType == ItemType::DepotViewItem) {
        QMenu menu(this);
        auto add = [&](const QString& text, const char* action) {
            QAction* a = menu.addAction(text);
            const QString act = QString::fromLatin1(action);
            connect(a, &QAction::triggered, this, [this, act, itemId] {
                Q_EMIT drawingActionRequested(act, itemId);
            });
        };
        if (itemType == ItemType::DepotViewFolder) {
            add(tr("新增視圖（在 View 中點選）"), "depotAdd");
            add(tr("在 View 顯示全部"), "depotShowAll");
            add(tr("隱藏全部"), "depotHideAll");
            menu.addSeparator();
            add(tr("編輯視圖清單…"), "depotEdit");
        } else {
            add(tr("在 View 中編輯（拖放／右鍵選單）"), "depotEditView");
            add(tr("詳細設定…（位置／大小／轉角／對齊／排序）"), "depotEdit");
            add(tr("變更序號…"), "depotOrder");
            add(tr("在 View 顯示"), "depotShow");
            add(tr("隱藏"), "depotHide");
            menu.addSeparator();
            add(tr("刪除此視圖"), "depotDelete");
        }
        menu.exec(globalPos);
        return;
    }
    if (itemType == ItemType::DrawingSheetItem) {
        QMenu menu(this);
        auto add = [&](const QString& text, const char* action) {
            QAction* a = menu.addAction(text);
            const QString act = QString::fromLatin1(action);
            connect(a, &QAction::triggered, this, [this, act, itemId] {
                Q_EMIT drawingActionRequested(act, itemId);
            });
        };
        add(tr("開啟"), "open");
        menu.addSeparator();
        add(tr("此圖設定（局部）…"), "sheetSettings");
        add(tr("清除局部設定（回到全域）"), "clearOverride");
        add(tr("重新計算此圖"), "regenSheet");
        menu.addSeparator();
        add(tr("刪除此圖"), "deleteSheet");
        menu.exec(globalPos);
        return;
    }

    // ── 「組件安裝位置」資料夾 ───────────────────────────────────────────
    if (itemType == ItemType::ComponentLayout) {
        QMenu menu(this);
        QAction* actEdit = menu.addAction(tr("新增／編輯各組件安裝位置…"));
        connect(actEdit, &QAction::triggered, this, [this] { Q_EMIT editComponentLayoutRequested(); });
        QAction* actIns = menu.addAction(tr("插入 3D 組件 (*.aicad)…"));
        connect(actIns, &QAction::triggered, this, [this] { Q_EMIT insertComponentRequested(); });
        menu.exec(globalPos);
        return;
    }

    // ── Railway 資料夾：提供新增線路 ─────────────────────────────────────
    if (itemType == ItemType::Railway) {
        QMenu menu(this);
        QAction* actAdd = menu.addAction(tr("新增線路中心線"));
        connect(actAdd, &QAction::triggered, this, [this] {
            Q_EMIT editAlignmentRequested(QString());  // 空 id = 新增
        });
        menu.addSeparator();
        QAction* actAllClose = menu.addAction(tr("所有線形 eyeClose"));
        connect(actAllClose, &QAction::triggered, this, [this] {
            Q_EMIT allAlignmentsVisibilityRequested(false);
        });
        QAction* actAllOpen = menu.addAction(tr("所有線形 eyeOpen"));
        connect(actAllOpen, &QAction::triggered, this, [this] {
            Q_EMIT allAlignmentsVisibilityRequested(true);
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
        menu.addSeparator();
        QAction* actCollapse = menu.addAction(tr("收合 Railway 項目"));
        connect(actCollapse, &QAction::triggered, this, [this] { collapseRailwayFolder(); });
        menu.exec(globalPos);
        return;
    }

    // ── TrackCenterLine 專屬選單 ──────────────────────────────────────────
    if (itemType == ItemType::TrackCenterLine) {
        QMenu menu(this);

        QAction* actFocus = menu.addAction(tr("聚焦 Focus"));
        connect(actFocus, &QAction::triggered, this, [this, itemId] {
            Q_EMIT focusRequested(itemId);
        });
        {
            const bool vis = itemCurrentlyVisible(m_visibilityResolver, itemId, item);
            QAction* actVis = menu.addAction(vis ? tr("隱藏 Hide (eyeClose)") : tr("顯示 Show (eyeOpen)"));
            connect(actVis, &QAction::triggered, this, [this, itemId, vis] {
                Q_EMIT visibilityRequested(itemId, !vis);
            });
        }
        QAction* actCollapse = menu.addAction(tr("收合 Railway 項目"));
        connect(actCollapse, &QAction::triggered, this, [this] { collapseRailwayFolder(); });
        menu.addSeparator();

        QAction* actEdit = menu.addAction(tr("編輯線形 (Edit Alignment)"));
        connect(actEdit, &QAction::triggered, this, [this, itemId] {
            Q_EMIT editAlignmentRequested(itemId);
        });

        menu.addSeparator();

        QAction* actTable = menu.addAction(tr("線形資料表"));
        connect(actTable, &QAction::triggered, this, [this, itemId] {
            Q_EMIT showAlignmentDataTableRequested(itemId);
        });

        QAction* actQuickTable = menu.addAction(tr("以 AQT 編輯..."));
        connect(actQuickTable, &QAction::triggered, this, [this, itemId] {
            Q_EMIT editWithQuickTableRequested(itemId);
        });

        menu.addSeparator();

        // ── 元素鏈同步（需求 7）：逐線路獨立開關，預設關閉 ──────────────────
        // 見 railway::TrackCenterLine::elementChainSyncEnabled() 的完整說明。
        bool syncEnabled = false;
        if (auto* d = core::Application::instance()->documentManager()->currentDocument()) {
            if (auto* tcl = d->findTrackCenterLine(itemId))
                syncEnabled = tcl->elementChainSyncEnabled();
        }
        QAction* actSync = menu.addAction(tr("元素鏈同步"));
        actSync->setCheckable(true);
        actSync->setChecked(syncEnabled);
        actSync->setToolTip(
            tr("關閉（預設）：保護本線的線元資料不被元素鏈重建覆寫，"
               "但「線形資料表」對話框內拖曳 grip／編輯欄位不會寫回。\n"
               "開啟：允許本線使用元素鏈重建與即時編輯寫回，"
               "但在少數線元組合下，重建可能誤判而覆蓋掉其他既有元素。"));
        connect(actSync, &QAction::toggled, this, [itemId](bool checked) {
            if (auto* d2 = core::Application::instance()->documentManager()->currentDocument()) {
                if (auto* tcl2 = d2->findTrackCenterLine(itemId)) {
                    tcl2->setElementChainSyncEnabled(checked);
                    d2->setModified(true);
                }
            }
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

    // ── AlignedProfileArray（Step 8：唯讀站位資料表）───────────────────────
    if (itemType == ItemType::Pattern) {
        QMenu menu(this);
        QAction* actFocus = menu.addAction(tr("聚焦 Focus"));
        connect(actFocus, &QAction::triggered, this, [this, itemId] {
            Q_EMIT focusRequested(itemId);
        });
        {
            const bool vis = itemCurrentlyVisible(m_visibilityResolver, itemId, item);
            QAction* actVis = menu.addAction(vis ? tr("隱藏 Hide (eyeClose)") : tr("顯示 Show (eyeOpen)"));
            connect(actVis, &QAction::triggered, this, [this, itemId, vis] {
                Q_EMIT visibilityRequested(itemId, !vis);
            });
        }
        QAction* actStClose = menu.addAction(tr("所有測站草圖 eyeClose"));
        connect(actStClose, &QAction::triggered, this, [this, itemId] {
            Q_EMIT stationSketchesVisibilityRequested(itemId, false);
        });
        QAction* actStOpen = menu.addAction(tr("所有測站草圖 eyeOpen"));
        connect(actStOpen, &QAction::triggered, this, [this, itemId] {
            Q_EMIT stationSketchesVisibilityRequested(itemId, true);
        });
        menu.addSeparator();
        QAction* actTable = menu.addAction(tr("分段資料表 (Segment Table)…"));
        connect(actTable, &QAction::triggered, this, [this, itemId] {
            Q_EMIT showProfileArrayTableRequested(itemId);
        });
        menu.addSeparator();
        QAction* actDelete = menu.addAction(QIcon(":/icons/cut.png"), tr("刪除"));
        connect(actDelete, &QAction::triggered, this, [this, itemId] {
            Q_EMIT deleteFeatureRequested(itemId);
        });
        menu.exec(globalPos);
        return;
    }

    QMenu menu(this);

    // ProfileArray 的測站節點（SketchInstance）在樹上沿用 ItemType::Sketch，但它
    // 不是 Sketch：舊版右鍵「編輯草圖」會被 qobject_cast<Sketch*> 擋掉而毫無反應。
    // 測站的內容由陣列分段表驅動，不可直接編輯，改提供「查看草圖」（唯讀，
    // 以 2D 草圖畫面顯示本站實際套用的幾何、尺寸與參數值）。
    bool isInstanceNode = false;
    if (itemType == ItemType::Sketch) {
        if (auto* doc = core::Application::instance()->documentManager()->currentDocument())
            isInstanceNode = qobject_cast<cad::SketchInstance*>(doc->findFeature(itemId)) != nullptr;
    }

    // ── 聚焦 Focus（測站節點 SketchInstance 不在 View 中顯示，不提供）────────
    if (itemSupportsFocus(itemType) && !isInstanceNode) {
        QAction* actFocus = menu.addAction(tr("聚焦 Focus"));
        connect(actFocus, &QAction::triggered, this, [this, itemId] {
            Q_EMIT focusRequested(itemId);
        });
        const bool vis = itemCurrentlyVisible(m_visibilityResolver, itemId, item);
        QAction* actVis = menu.addAction(vis ? tr("隱藏 Hide (eyeClose)") : tr("顯示 Show (eyeOpen)"));
        connect(actVis, &QAction::triggered, this, [this, itemId, vis] {
            Q_EMIT visibilityRequested(itemId, !vis);
        });
        menu.addSeparator();
    }

    if (itemType == ItemType::Component) {
        QAction* actOpenSrc = menu.addAction(tr("在另一個 CADView 編輯組件（開啟來源檔）"));
        connect(actOpenSrc, &QAction::triggered, this, [this, itemId] {
            Q_EMIT openComponentSourceRequested(itemId);
        });
        QAction* actEditPl = menu.addAction(tr("編輯安裝位置…"));
        connect(actEditPl, &QAction::triggered, this, [this, itemId] {
            Q_EMIT editComponentPlacementsRequested(itemId);
        });
        QAction* actFrame = menu.addAction(tr("建立／重設組件座標線（與斷面草圖約束）…"));
        connect(actFrame, &QAction::triggered, this, [this, itemId] {
            Q_EMIT createComponentFrameRequested(itemId);
        });
        menu.addSeparator();
        QAction* actSync = menu.addAction(tr("同步來源檔（重新載入組件）"));
        connect(actSync, &QAction::triggered, this, [this, itemId] {
            Q_EMIT syncComponentRequested(itemId);
        });
        QAction* actAuto = menu.addAction(tr("切換自動同步（來源檔存檔時）"));
        connect(actAuto, &QAction::triggered, this, [this, itemId] {
            Q_EMIT toggleComponentAutoSyncRequested(itemId);
        });
        menu.addSeparator();
    }

    if (itemType == ItemType::DefinedPlane) {
        QAction* actSk = menu.addAction(tr("在此平面建立草圖"));
        connect(actSk, &QAction::triggered, this, [this, itemId] {
            Q_EMIT createSketchOnDefinedPlaneRequested(itemId);
        });
        QAction* actEd = menu.addAction(tr("編輯平面（基準／偏移）…"));
        connect(actEd, &QAction::triggered, this, [this, itemId] {
            Q_EMIT editDefinedPlaneRequested(itemId);
        });
        QAction* actNew = menu.addAction(tr("以此平面為基準定義新平面…"));
        connect(actNew, &QAction::triggered, this, [this, itemId] {
            Q_EMIT defineNewPlaneRequested(itemId);
        });
        menu.addSeparator();
    }

    // 標準參考平面（XY／XZ／YZ，Origin 底下）：可以此為基準定義新的偏移平面
    if (itemType == ItemType::Plane && (itemId == QLatin1String("plane_xy")
                                        || itemId == QLatin1String("plane_xz")
                                        || itemId == QLatin1String("plane_yz"))) {
        const QString base = itemId == QLatin1String("plane_xy") ? QStringLiteral("XY")
                           : itemId == QLatin1String("plane_xz") ? QStringLiteral("XZ") : QStringLiteral("YZ");
        QAction* actDef = menu.addAction(tr("以此平面定義新平面（偏移）…"));
        connect(actDef, &QAction::triggered, this, [this, base] {
            Q_EMIT defineNewPlaneRequested(base);
        });
        menu.addSeparator();
    }

    if (itemType == ItemType::Extrude) {
        QAction* actExt = menu.addAction(tr("編輯擠出（方向／尺寸）…"));
        connect(actExt, &QAction::triggered, this, [this, itemId] {
            Q_EMIT editExtrudeRequested(itemId);
        });
        menu.addSeparator();
    }

    if (itemType == ItemType::SlabPlan) {
        QAction* actDxf = menu.addAction(tr("匯出 DXF…"));
        connect(actDxf, &QAction::triggered, this, [this, itemId] {
            Q_EMIT exportSlabPlanDxfRequested(itemId);
        });
        menu.addSeparator();
    }

    if (itemType == ItemType::Sketch) {
        if (isInstanceNode) {
            QAction* actView = menu.addAction(
                QIcon(":/icons/sketch.png"), tr("查看草圖"));
            connect(actView, &QAction::triggered, this, [this, itemId] {
                Q_EMIT viewSketchInstanceRequested(itemId);
            });
        } else {
            QAction* actEdit = menu.addAction(
                QIcon(":/icons/sketch.png"), tr("編輯草圖"));
            connect(actEdit, &QAction::triggered, this, [this, itemId] {
                Q_EMIT editSketchRequested(itemId);
            });

            QAction* actRename = menu.addAction(tr("重新命名 (F2)"));
            connect(actRename, &QAction::triggered, this, [this, itemId] {
                Q_EMIT renameSketchRequested(itemId);
            });

            menu.addSeparator();
            QAction* actCopy = menu.addAction(tr("複製草圖"));
            connect(actCopy, &QAction::triggered, this, [this, itemId] {
                Q_EMIT copySketchRequested(itemId);
            });
            QAction* actPaste = menu.addAction(tr("貼上草圖"));
            actPaste->setEnabled(clipboardHasSketch());
            connect(actPaste, &QAction::triggered, this, [this] {
                Q_EMIT pasteSketchRequested();
            });
        }
        menu.addSeparator();
    }

    // ── 刪除（所有可刪類型）─────────────────────────────────────────────────
    QAction* actDelete = menu.addAction(
        QIcon(":/icons/cut.png"), tr("刪除"));
    connect(actDelete, &QAction::triggered, this, [this, itemId] {
        Q_EMIT deleteFeatureRequested(itemId);
    });

    // ── 剖面（僅 Sketch；測站節點不是 Sketch，不提供）──────────────────────
    if (itemType == ItemType::Sketch && !isInstanceNode) {
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
