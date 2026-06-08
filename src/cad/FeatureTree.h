/**
 * @file FeatureTree.h
 * @brief FeatureTree 類別定義
 * @author TODO
 * @date 2026-01-07
 */

#ifndef AICAD_CAD_FEATURETREE_H
#define AICAD_CAD_FEATURETREE_H

#include <QObject>

namespace aicad {
namespace cad {

/**
 * @brief FeatureTree 類別
 * 
 * TODO: 添加類別說明
 */
class FeatureTree : public QObject {
    Q_OBJECT
    
public:
    explicit FeatureTree(QObject* parent = nullptr);
    ~FeatureTree() override;
    
    // TODO: 添加公開方法
    
Q_SIGNALS:
    // TODO: 添加信號
    
private:
    // TODO: 添加私有成員
    
    class Private;
    Private* d;
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_FEATURETREE_H
