#pragma once
#include "SketchConstraint.h"
#include "ConstraintSymbolAIS.h"
#include "DimensionLineAIS.h"
#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include <gp_Trsf.hxx>
#include <AIS_InteractiveContext.hxx>

namespace aicad::cad {

class Sketch;
class SketchInstance;

/**
 * @brief 約束覆蓋管理員（Phase 6）
 *
 * 統一管理草圖視埠中所有約束符號（AIS_ConstraintSymbol）
 * 與尺寸線（AIS_DimensionLine）的生命週期，
 * 支援 master 和 instance 兩種模式。
 *
 * 同一時間只 attach 一個 source（master or instance）；
 * 切換時須先 detach() 再 attachMaster() / attachInstance()。
 */
class ConstraintOverlayManager : public QObject {
    Q_OBJECT
public:
    enum class Mode { Master, Instance };

    explicit ConstraintOverlayManager(
        const Handle(AIS_InteractiveContext)& ctx,
        QObject* parent = nullptr);
    ~ConstraintOverlayManager() override;

    // ── 綁定 ─────────────────────────────────────────────────────────────
    /// 綁定到 master Sketch（進入草圖編輯模式）
    void attachMaster(Sketch* sketch, const gp_Trsf& toWorld);

    /// 綁定到 SketchInstance（進入 instance 檢視 / 編輯模式）
    void attachInstance(SketchInstance* instance, const gp_Trsf& toWorld);

    /// 解除綁定，清除所有 AIS 物件
    void detach();

    // ── 更新 ─────────────────────────────────────────────────────────────
    void rebuildAll();

    // ── Phase 3B 擴充 ─────────────────────────────────────────────────────
    /**
     * 輕量更新：僅移動尺寸線，不重建所有 Overlay
     */
    void updateDimLine(const QString& constraintUuid, double newOffsetX, double newOffsetY);

    /**
     * 取得指定約束的尺寸線 AIS 物件（供 GripProvider 使用）
     */
    Handle(AIS_DimensionLine) dimLineAISForConstraint(const QString& constraintUuid) const;
    void setVisible(bool v);
    void setTypeVisible(ConstraintType type, bool v);

Q_SIGNALS:
    /**
     * 尺寸線被點擊時發出
     * @param constraintUuid  約束 UUID
     * @param mode            來源模式（Master or Instance）
     * @param instanceId      若 mode==Instance，則為 instance 的 feature ID
     */
    void dimensionConstraintClicked(
        const QString& constraintUuid,
        ConstraintOverlayManager::Mode mode,
        const QString& instanceId);

private Q_SLOTS:
    void onSourceRebuilt();
    void onConstraintAdded(const QString& uuid);
    void onConstraintRemoved(const QString& uuid);

private:
    // 取得當前 source 的幾何列表與約束列表（master or instance clone）
    const QList<SketchGeometry*>*   sourceGeoms()       const;
    const QList<SketchConstraint>*  sourceConstraints() const;
    SolveStatus                     lastSolveStatus()   const;

    // 取得 source 中指定 uuid 的幾何（依 refs 查找）
    QList<SketchGeometry*> geomsForConstraint(const SketchConstraint& c) const;

    void createSymbolFor  (const SketchConstraint& c);
    void updateSymbolFor  (const SketchConstraint& c);
    void removeSymbolFor  (const QString& uuid);
    void clearAll();

    Handle(AIS_InteractiveContext)                m_ctx;
    Mode                                          m_mode = Mode::Master;
    Sketch*                                       m_sketch   = nullptr;
    SketchInstance*                               m_instance = nullptr;
    gp_Trsf                                       m_toWorld;
    bool                                          m_visible  = true;
    QSet<ConstraintType>                          m_hiddenTypes;

    QHash<QString, Handle(AIS_ConstraintSymbol)>  m_geomSymbols;
    QHash<QString, Handle(AIS_DimensionLine)>     m_dimLines;
};

} // namespace aicad::cad

// Register enum with Qt meta-type system
Q_DECLARE_METATYPE(aicad::cad::ConstraintOverlayManager::Mode)
