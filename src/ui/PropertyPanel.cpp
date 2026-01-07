/**
 * @file PropertyPanel.cpp
 * @brief PropertyPanel 類別實作
 * @author James
 * @date 2025-01-07
 */

#include "PropertyPanel.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QDebug>

namespace aicad {
namespace ui {

class PropertyPanel::Private {
public:
    Private()
        : tableWidget(nullptr)
        , updating(false)
    {
    }
    
    QTableWidget* tableWidget;
    bool updating;  // 防止遞迴更新
};

PropertyPanel::PropertyPanel(QWidget* parent)
    : QDockWidget("Properties", parent)
    , d(new Private())
{
    qDebug() << "[PropertyPanel] Created";
    
    setupUI();
    connectSignals();
    
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
}

PropertyPanel::~PropertyPanel() {
    qDebug() << "[PropertyPanel] Destroyed";
    delete d;
}

void PropertyPanel::setupUI() {
    // 建立表格視圖
    d->tableWidget = new QTableWidget(this);
    d->tableWidget->setColumnCount(2);
    
    QStringList headers;
    headers << "Property" << "Value";
    d->tableWidget->setHorizontalHeaderLabels(headers);
    
    // 設定表格屬性
    d->tableWidget->setAlternatingRowColors(true);
    d->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    d->tableWidget->verticalHeader()->setVisible(false);
    
    // 調整欄位寬度
    d->tableWidget->horizontalHeader()->setStretchLastSection(true);
    d->tableWidget->setColumnWidth(0, 120);
    
    setWidget(d->tableWidget);
}

void PropertyPanel::connectSignals() {
    connect(d->tableWidget, &QTableWidget::cellChanged,
            this, &PropertyPanel::onCellChanged);
}

void PropertyPanel::clear() {
    qDebug() << "[PropertyPanel] Clearing properties";
    
    d->updating = true;
    d->tableWidget->setRowCount(0);
    d->updating = false;
}

void PropertyPanel::addProperty(const QString& name, const QVariant& value, bool editable) {
    qDebug() << "[PropertyPanel] Adding property:" << name << "=" << value.toString();
    
    d->updating = true;
    
    int row = d->tableWidget->rowCount();
    d->tableWidget->insertRow(row);
    
    // 屬性名稱（不可編輯）
    QTableWidgetItem* nameItem = new QTableWidgetItem(name);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    d->tableWidget->setItem(row, 0, nameItem);
    
    // 屬性值
    QTableWidgetItem* valueItem = new QTableWidgetItem(value.toString());
    if (!editable) {
        valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
    }
    d->tableWidget->setItem(row, 1, valueItem);
    
    d->updating = false;
}

void PropertyPanel::setPropertyValue(const QString& name, const QVariant& value) {
    int row = findPropertyRow(name);
    
    if (row < 0) {
        qWarning() << "[PropertyPanel] Property not found:" << name;
        return;
    }
    
    qDebug() << "[PropertyPanel] Setting property:" << name << "=" << value.toString();
    
    d->updating = true;
    
    QTableWidgetItem* item = d->tableWidget->item(row, 1);
    if (item) {
        item->setText(value.toString());
    }
    
    d->updating = false;
}

QVariant PropertyPanel::getPropertyValue(const QString& name) const {
    int row = findPropertyRow(name);
    
    if (row < 0) {
        return QVariant();
    }
    
    QTableWidgetItem* item = d->tableWidget->item(row, 1);
    if (item) {
        return item->text();
    }
    
    return QVariant();
}

void PropertyPanel::onCellChanged(int row, int column) {
    if (d->updating) {
        return;
    }
    
    if (column != 1) {
        return;  // 只處理值欄位的變更
    }
    
    QTableWidgetItem* nameItem = d->tableWidget->item(row, 0);
    QTableWidgetItem* valueItem = d->tableWidget->item(row, 1);
    
    if (!nameItem || !valueItem) {
        return;
    }
    
    QString name = nameItem->text();
    QString value = valueItem->text();
    
    qDebug() << "[PropertyPanel] Property changed:" << name << "=" << value;
    
    Q_EMIT propertyChanged(name, value);
}

int PropertyPanel::findPropertyRow(const QString& name) const {
    for (int row = 0; row < d->tableWidget->rowCount(); ++row) {
        QTableWidgetItem* item = d->tableWidget->item(row, 0);
        if (item && item->text() == name) {
            return row;
        }
    }
    
    return -1;
}

} // namespace ui
} // namespace aicad