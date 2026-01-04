#include "ui/feature/FeatureBrowser.h"

#include "OcafDocument.h"   // or core/document/Document.h
#include <TDF_Label.hxx>

FeatureBrowser::FeatureBrowser(QWidget* parent)
    : QDockWidget(tr("Feature Tree"), parent)
{
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);

    setWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked,
            this, &FeatureBrowser::onItemClicked);
}

void FeatureBrowser::setDocument(OcafDocument* doc)
{
    m_document = doc;
    refresh();
}

void FeatureBrowser::refresh()
{
    if (!m_document)
        return;

    buildTree();
}

void FeatureBrowser::buildTree()
{
    m_tree->clear();

    const QVector<TDF_Label> features = m_document->getFeatures();

    for (const TDF_Label& label : features) {
        const int featureId = m_document->getFeatureId(label);
        const QString name = m_document->getFeatureName(label);
        const FeatureType type = m_document->getFeatureType(label);

        QString typeStr;
        switch (type) {
        case FeatureType::Sketch:   typeStr = "Sketch"; break;
        case FeatureType::Extrude:  typeStr = "Extrude"; break;
        default:                    typeStr = "Unknown"; break;
        }

        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, QString("%1 [%2]").arg(name, typeStr));
        item->setData(0, Qt::UserRole, featureId);

        m_tree->addTopLevelItem(item);
    }
}

void FeatureBrowser::onItemClicked(QTreeWidgetItem* item, int)
{
    if (!item)
        return;

    const int featureId = item->data(0, Qt::UserRole).toInt();
    emit featureSelected(featureId);
}
