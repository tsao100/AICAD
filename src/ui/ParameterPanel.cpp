#include "ParameterPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QMessageBox>
#include <QColor>
#include <QFont>
#include <QDebug>
#include <QInputDialog>
#include <QMenu>
#include <QShortcut>
#include <QKeySequence>
#include <QSet>

namespace aicad::ui {

// ─────────────────────────────────────────────────────────────────────────────
// 建構
// ─────────────────────────────────────────────────────────────────────────────

ParameterPanel::ParameterPanel(QWidget* parent)
    : QDockWidget(tr("參數"), parent)
{
    setObjectName("ParameterPanel");
    setupUI();

    // Ctrl+P 切換顯示/隱藏
    auto* sc = new QShortcut(QKeySequence("Ctrl+P"), this);
    connect(sc, &QShortcut::activated, this, [this] {
        setVisible(!isVisible());
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// UI 初始化
// ─────────────────────────────────────────────────────────────────────────────

void ParameterPanel::setupUI() {
    QWidget* container = new QWidget(this);
    setWidget(container);

    QVBoxLayout* root = new QVBoxLayout(container);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // 上下文標籤
    m_contextLabel = new QLabel(tr("（未選擇）"), container);
    m_contextLabel->setStyleSheet("font-weight: bold; color: #555;");
    root->addWidget(m_contextLabel);

    // Instance 列表標籤（master 模式下顯示）
    m_instancesLabel = new QLabel(container);
    m_instancesLabel->setVisible(false);
    m_instancesLabel->setWordWrap(true);
    m_instancesLabel->setStyleSheet("color: #777; font-size: 11px;");
    root->addWidget(m_instancesLabel);

    // 參數表
    m_table = new QTableWidget(0, 4, container);
    m_table->setHorizontalHeaderLabels({tr("名稱"), tr("表達式"), tr("值"), tr("來源")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    root->addWidget(m_table, 1);

    // 依賴提示
    m_dependentsLabel = new QLabel(container);
    m_dependentsLabel->setVisible(false);
    m_dependentsLabel->setStyleSheet("color: #888; font-size: 10px;");
    m_dependentsLabel->setWordWrap(true);
    root->addWidget(m_dependentsLabel);

    // 右鍵選單：重命名 / 刪除本地參數
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        int row = m_table->rowAt(pos.y());
        if (row < 0) return;
        QTableWidgetItem* ni = m_table->item(row, COL_NAME);
        if (!ni) return;
        QString name = ni->text().trimmed();

        aicad::core::ParameterStore* store =
            m_mode == PanelMode::Master ? m_masterStore
          : (m_instance ? m_instance->parameterStore() : nullptr);
        if (!store) return;

        QMenu menu(this);
        QAction* actRename = menu.addAction(tr("重命名參數…"));
        QAction* actDelete = menu.addAction(tr("刪除本地參數"));
        actDelete->setEnabled(store->hasLocal(name));

        QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
        if (chosen == actRename) {
            bool ok = false;
            QString newName = QInputDialog::getText(
                this, tr("重命名參數"),
                tr("新名稱（影響所有引用此參數的表達式）："),
                QLineEdit::Normal, name, &ok);
            if (ok && !newName.trimmed().isEmpty() && newName != name) {
                if (!store->rename(name, newName.trimmed()))
                    QMessageBox::warning(this, tr("重命名失敗"),
                        tr("名稱「%1」已存在或重命名失敗。").arg(newName));
                else {
                    if (m_mode == PanelMode::Master) rebuildMasterTable();
                    else rebuildInstanceTable();
                }
            }
        } else if (chosen == actDelete) {
            store->removeLocal(name);
            if (m_mode == PanelMode::Master) rebuildMasterTable();
            else rebuildInstanceTable();
        }
    });

    // 按鈕列
    QHBoxLayout* btnRow = new QHBoxLayout;
    m_btnAdd     = new QPushButton(tr("新增 / 覆寫"), container);
    m_btnClear   = new QPushButton(tr("清除覆寫"), container);
    m_btnClearAll = new QPushButton(tr("清除全部"), container);
    m_btnClear->setEnabled(false);
    m_btnClearAll->setEnabled(false);
    btnRow->addWidget(m_btnAdd);
    btnRow->addWidget(m_btnClear);
    btnRow->addWidget(m_btnClearAll);
    btnRow->addStretch();
    root->addLayout(btnRow);

    // 連接訊號
    connect(m_btnAdd,      &QPushButton::clicked, this, &ParameterPanel::onAddOrOverride);
    connect(m_btnClear,    &QPushButton::clicked, this, &ParameterPanel::onClearOverride);
    connect(m_btnClearAll, &QPushButton::clicked, this, &ParameterPanel::onClearAll);
    connect(m_table, &QTableWidget::cellChanged, this, &ParameterPanel::onCellChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &ParameterPanel::onSelectionChanged);
}

// ─────────────────────────────────────────────────────────────────────────────
// 切換模式
// ─────────────────────────────────────────────────────────────────────────────

void ParameterPanel::showMaster(
        aicad::core::ParameterStore* store,
        const QString& sketchName,
        const QList<aicad::cad::SketchInstance*>& instances)
{
    m_mode        = PanelMode::Master;
    m_masterStore = store;
    m_instance    = nullptr;
    m_instances   = instances;

    m_contextLabel->setText(tr("草圖：%1").arg(sketchName));
    m_btnClear->setVisible(false);
    m_btnClearAll->setVisible(false);
    m_btnAdd->setText(tr("新增參數"));

    // 顯示 instance 列表
    if (!instances.isEmpty()) {
        QStringList names;
        for (auto* inst : instances) names << inst->name();
        m_instancesLabel->setText(tr("副本：%1").arg(names.join(", ")));
        m_instancesLabel->setVisible(true);
    } else {
        m_instancesLabel->setVisible(false);
    }

    // 訂閱 store 更新
    if (m_masterStore) {
        connect(m_masterStore, &aicad::core::ParameterStore::parameterChanged,
                this, &ParameterPanel::onParameterChanged, Qt::UniqueConnection);
    }

    rebuildMasterTable();
}

void ParameterPanel::showInstance(aicad::cad::SketchInstance* instance) {
    m_mode        = PanelMode::Instance;
    m_instance    = instance;
    m_masterStore = instance ? instance->masterSketch()
                                   ? instance->masterSketch()->parameterStore()
                                   : nullptr
                             : nullptr;

    if (instance) {
        m_contextLabel->setText(tr("副本：%1").arg(instance->name()));

        // 訂閱 instance store
        connect(instance->parameterStore(),
                &aicad::core::ParameterStore::parameterChanged,
                this, &ParameterPanel::onParameterChanged, Qt::UniqueConnection);
    }

    m_btnClear->setVisible(true);
    m_btnClearAll->setVisible(true);
    m_btnAdd->setText(tr("覆寫參數"));
    m_instancesLabel->setVisible(false);

    rebuildInstanceTable();
}

void ParameterPanel::clearPanel() {
    m_updating = true;
    m_table->setRowCount(0);
    m_updating = false;
    m_masterStore = nullptr;
    m_instance    = nullptr;
    m_instances.clear();
    m_contextLabel->setText(tr("（未選擇）"));
    m_dependentsLabel->setVisible(false);
    m_instancesLabel->setVisible(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// 表格重建
// ─────────────────────────────────────────────────────────────────────────────

void ParameterPanel::applyRowStyle(int row, const QString& source) {
    // source: "local" / "master" / "global" / "override"
    QColor bg;
    if      (source == "override") bg = QColor(0xFFF9E0);  // 淡黃：instance 覆寫
    else if (source == "master")   bg = QColor(0xF0F0F0);  // 淡灰：繼承 master
    else if (source == "global")   bg = QColor(0xE8F0FF);  // 淡藍：全域
    else                           bg = Qt::white;

    bool readOnly = (source == "master" || source == "global");

    for (int col = 0; col < m_table->columnCount(); ++col) {
        QTableWidgetItem* item = m_table->item(row, col);
        if (!item) continue;
        item->setBackground(bg);

        // 表達式欄和名稱欄：繼承值設為唯讀
        if (col == COL_NAME || col == COL_EXPR) {
            Qt::ItemFlags f = item->flags();
            if (readOnly) f &= ~Qt::ItemIsEditable;
            else          f |=  Qt::ItemIsEditable;
            item->setFlags(f);
        }

        // 值欄和來源欄永遠唯讀
        if (col == COL_VALUE || col == COL_SOURCE) {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        }

        // 繼承值用斜體顯示
        QFont font = item->font();
        font.setItalic(readOnly);
        item->setFont(font);
    }
}

void ParameterPanel::rebuildMasterTable() {
    m_updating = true;
    m_table->setRowCount(0);
    if (!m_masterStore) { m_updating = false; return; }

    QStringList allNames = m_masterStore->names();
    allNames.sort();

    for (const QString& name : allNames) {
        int row = m_table->rowCount();
        m_table->insertRow(row);

        QString expr  = m_masterStore->expression(name);
        double  val   = m_masterStore->value(name);
        QString source = m_masterStore->hasLocal(name) ? "local" : "global";

        m_table->setItem(row, COL_NAME,   new QTableWidgetItem(name));
        m_table->setItem(row, COL_EXPR,   new QTableWidgetItem(expr));
        m_table->setItem(row, COL_VALUE,  new QTableWidgetItem(
            QString::number(val, 'f', 4)));
        m_table->setItem(row, COL_SOURCE, new QTableWidgetItem(
            source == "local" ? tr("草圖") : tr("全域")));

        applyRowStyle(row, source);

        // 依賴圖視覺提示：若其他參數依賴此參數，在名稱欄加 ★ tooltip
        QStringList deps = m_masterStore->dependentsOf(name);
        if (!deps.isEmpty()) {
            QTableWidgetItem* ni2 = m_table->item(row, COL_NAME);
            if (ni2) {
                ni2->setToolTip(tr("被以下參數引用：%1").arg(deps.join(", ")));
                QFont f = ni2->font(); f.setBold(true); ni2->setFont(f);
            }
        }
        // 若此參數依賴其他參數，在表達式欄加 tooltip 顯示依賴鏈
        QString exprCheck = m_masterStore->expression(name);
        if (!exprCheck.isEmpty()) {
            bool isLit = false; exprCheck.toDouble(&isLit);
            if (!isLit) {
                QTableWidgetItem* ei2 = m_table->item(row, COL_EXPR);
                if (ei2) ei2->setToolTip(tr("求值後 = %1").arg(
                    QString::number(m_masterStore->value(name), 'f', 4)));
            }
        }
    }
    m_updating = false;
}

void ParameterPanel::rebuildInstanceTable() {
    m_updating = true;
    m_table->setRowCount(0);
    if (!m_instance) { m_updating = false; return; }

    // 合併：master 所有參數 + instance 覆寫
    aicad::core::ParameterStore* instStore   = m_instance->parameterStore();
    aicad::core::ParameterStore* masterStore = m_instance->masterSketch()
        ? m_instance->masterSketch()->parameterStore() : nullptr;

    QStringList allNames = instStore->names();
    if (masterStore) {
        for (const QString& n : masterStore->names())
            if (!allNames.contains(n)) allNames.append(n);
    }
    allNames.sort();

    for (const QString& name : allNames) {
        int row = m_table->rowCount();
        m_table->insertRow(row);

        bool isOverride = instStore->hasLocal(name);
        QString expr  = isOverride
                        ? instStore->expression(name)
                        : (masterStore ? masterStore->expression(name) : QString());
        double  val   = instStore->value(name);
        QString source = isOverride ? "override"
                       : (masterStore && masterStore->hasLocal(name)) ? "master"
                       : "global";

        m_table->setItem(row, COL_NAME,   new QTableWidgetItem(name));
        m_table->setItem(row, COL_EXPR,   new QTableWidgetItem(expr));
        m_table->setItem(row, COL_VALUE,  new QTableWidgetItem(
            QString::number(val, 'f', 4)));

        QString srcLabel = source == "override" ? tr("副本覆寫")
                         : source == "master"   ? tr("繼承 master")
                         :                        tr("繼承全域");
        m_table->setItem(row, COL_SOURCE, new QTableWidgetItem(srcLabel));

        applyRowStyle(row, source);
    }
    m_updating = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// 驗證表達式
// ─────────────────────────────────────────────────────────────────────────────

bool ParameterPanel::validateExpression(const QString& name,
                                         const QString& expr,
                                         aicad::core::ParameterStore* store)
{
    if (!store) return false;
    if (store->wouldCreateCycle(name, expr)) {
        QMessageBox::warning(this, tr("循環依賴"),
            tr("參數「%1」的表達式「%2」會造成循環依賴，已拒絕。").arg(name, expr));
        return false;
    }
    auto [ok, val] = store->evaluate(expr);
    if (!ok) {
        QMessageBox::warning(this, tr("表達式錯誤"),
            tr("無法求值：%1").arg(expr));
        return false;
    }
    Q_UNUSED(val)
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots
// ─────────────────────────────────────────────────────────────────────────────

void ParameterPanel::onAddOrOverride() {
    aicad::core::ParameterStore* target = nullptr;
    if (m_mode == PanelMode::Master && m_masterStore)
        target = m_masterStore;
    else if (m_mode == PanelMode::Instance && m_instance)
        target = m_instance->parameterStore();
    if (!target) return;

    // 「覆寫」路徑：只有在選取列是「本層尚未定義」的繼承列（來源為
    // master/global，表格中為唯讀）時才適用──把目前顯示的繼承值複製
    // 一份成為本層的本地值。已經是本層本地值的列可直接雙擊儲存格編輯，
    // 不需要透過按鈕；因此這裡一律 fall through 去新增一列，
    // 這樣連續按按鈕才能持續新增多個參數，而不是每次都覆寫同一列。
    int row = m_table->currentRow();
    if (row >= 0) {
        QTableWidgetItem* ni = m_table->item(row, COL_NAME);
        QTableWidgetItem* ei = m_table->item(row, COL_EXPR);
        QString name = ni ? ni->text().trimmed() : QString();
        QString expr = ei ? ei->text().trimmed() : QString();

        if (!name.isEmpty() && !expr.isEmpty() && !target->hasLocal(name)) {
            if (validateExpression(name, expr, target)) {
                target->setLocal(name, expr);
                Q_EMIT parameterEdited(name, expr);
                if (m_mode == PanelMode::Master) rebuildMasterTable();
                else                             rebuildInstanceTable();
            }
            return;
        }
    }

    // 新增一列，讓使用者直接在表格裡輸入全新參數。
    // 名稱需與目前表格中已存在的名稱不重複，避免使用者尚未改名前
    // 就被 onCellChanged 誤判為「編輯既有參數」。
    QSet<QString> existingNames;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (QTableWidgetItem* existing = m_table->item(r, COL_NAME))
            existingNames.insert(existing->text().trimmed());
    }
    QString candidate = "new_param";
    int suffix = 1;
    while (existingNames.contains(candidate))
        candidate = QString("new_param%1").arg(++suffix);

    m_updating = true;
    int newRow = m_table->rowCount();
    m_table->insertRow(newRow);
    m_table->setItem(newRow, COL_NAME,   new QTableWidgetItem(candidate));
    m_table->setItem(newRow, COL_EXPR,   new QTableWidgetItem("0"));
    m_table->setItem(newRow, COL_VALUE,  new QTableWidgetItem("0.0000"));
    m_table->setItem(newRow, COL_SOURCE, new QTableWidgetItem(
        m_mode == PanelMode::Instance ? tr("副本覆寫") : tr("草圖")));
    applyRowStyle(newRow, m_mode == PanelMode::Instance ? "override" : "local");
    m_updating = false;

    m_table->setCurrentCell(newRow, COL_NAME);
    m_table->editItem(m_table->item(newRow, COL_NAME));
}

void ParameterPanel::onClearOverride() {
    if (m_mode != PanelMode::Instance || !m_instance) return;
    int row = m_table->currentRow();
    if (row < 0) return;

    QTableWidgetItem* ni = m_table->item(row, COL_NAME);
    if (!ni) return;
    QString name = ni->text().trimmed();

    if (!m_instance->isOverridden(name)) return;

    m_instance->clearOverride(name);
    Q_EMIT overrideCleared(name);
    rebuildInstanceTable();
}

void ParameterPanel::onClearAll() {
    if (m_mode != PanelMode::Instance || !m_instance) return;
    int ret = QMessageBox::question(this,
        tr("清除全部覆寫"),
        tr("確定要清除「%1」的所有 instance 覆寫？\n清除後將完全繼承 master 參數。")
            .arg(m_instance->name()));
    if (ret != QMessageBox::Yes) return;

    m_instance->clearAllOverrides();
    rebuildInstanceTable();
}

void ParameterPanel::onCellChanged(int row, int col) {
    if (m_updating) return;
    if (col != COL_NAME && col != COL_EXPR) return;

    QTableWidgetItem* ni = m_table->item(row, COL_NAME);
    QTableWidgetItem* ei = m_table->item(row, COL_EXPR);
    if (!ni || !ei) return;

    QString name = ni->text().trimmed();
    QString expr = ei->text().trimmed();
    if (name.isEmpty() || expr.isEmpty()) return;

    aicad::core::ParameterStore* target = nullptr;
    if (m_mode == PanelMode::Master && m_masterStore)
        target = m_masterStore;
    else if (m_mode == PanelMode::Instance && m_instance)
        target = m_instance->parameterStore();
    if (!target) return;

    if (!validateExpression(name, expr, target)) {
        // 求值失敗：恢復原值
        m_updating = true;
        if (target->has(name)) {
            ni->setText(name);
            ei->setText(target->expression(name));
        }
        m_updating = false;
        return;
    }

    target->setLocal(name, expr);

    // 更新值欄
    m_updating = true;
    QTableWidgetItem* vi = m_table->item(row, COL_VALUE);
    if (!vi) { vi = new QTableWidgetItem; m_table->setItem(row, COL_VALUE, vi); }
    vi->setText(QString::number(target->value(name), 'f', 4));
    m_updating = false;

    Q_EMIT parameterEdited(name, expr);
}

void ParameterPanel::onParameterChanged(const QString& name, double value) {
    // 更新表格中對應列的值欄
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem* ni = m_table->item(row, COL_NAME);
        if (ni && ni->text() == name) {
            m_updating = true;
            QTableWidgetItem* vi = m_table->item(row, COL_VALUE);
            if (!vi) { vi = new QTableWidgetItem; m_table->setItem(row, COL_VALUE, vi); }
            vi->setText(QString::number(value, 'f', 4));
            m_updating = false;
            break;
        }
    }
}

void ParameterPanel::onSelectionChanged() {
    int row = m_table->currentRow();
    if (row < 0) {
        m_dependentsLabel->setVisible(false);
        m_btnClear->setEnabled(false);
        return;
    }

    QTableWidgetItem* ni = m_table->item(row, COL_NAME);
    if (!ni) return;
    QString name = ni->text().trimmed();

    // 顯示依賴此參數的其他參數
    aicad::core::ParameterStore* store = nullptr;
    if (m_mode == PanelMode::Master) store = m_masterStore;
    else if (m_mode == PanelMode::Instance && m_instance)
        store = m_instance->parameterStore();

    if (store) {
        QStringList deps = store->dependentsOf(name);
        if (!deps.isEmpty()) {
            m_dependentsLabel->setText(tr("被依賴：%1").arg(deps.join(", ")));
            m_dependentsLabel->setVisible(true);
        } else {
            m_dependentsLabel->setVisible(false);
        }
    }

    // Instance 模式下，選到有覆寫值的列才啟用清除按鈕
    if (m_mode == PanelMode::Instance && m_instance) {
        m_btnClear->setEnabled(m_instance->isOverridden(name));
        m_btnClearAll->setEnabled(!m_instance->overriddenParams().isEmpty());
    }
}

} // namespace aicad::ui
