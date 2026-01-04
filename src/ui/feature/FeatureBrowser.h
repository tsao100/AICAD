#pragma once

#include <QDockWidget>
#include <QTreeWidget>

class OcafDocument;   // ✅ 正確的 forward declaration

class FeatureBrowser : public QDockWidget
{
    Q_OBJECT
public:
    explicit FeatureBrowser(QWidget* parent = nullptr);

    void setDocument(OcafDocument* doc);
    void refresh();

Q_SIGNALS:
    void featureSelected(int featureId);

private Q_SLOTS:
    void onItemClicked(QTreeWidgetItem* item, int column);

private:
    void buildTree();

private:
    OcafDocument* m_document = nullptr;
    QTreeWidget*  m_tree     = nullptr;
};
