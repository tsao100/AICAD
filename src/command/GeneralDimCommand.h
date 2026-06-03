#pragma once
#include "Command.h"
#include "cad/sketch/SketchConstraint.h"
#include "cad/sketch/GeneralDimClassifier.h"
#include <QList>
#include <QString>

namespace aicad::command {

/**
 * @brief GDIM — General Dimension 命令
 *
 * 先選取幾何，由 GeneralDimClassifier 推斷類型，
 * 再互動式設定尺寸線位置與數值。
 */
class GeneralDimCommand : public Command {
    Q_OBJECT
public:
    GeneralDimCommand();
    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }

private:
    enum class State {
        Idle,           ///< 等待第一個幾何
        WaitSecond,     ///< 等待第二個幾何（Point 選取後）
        WaitArcType,    ///< 等待使用者選 Radius / ArcLength
        WaitDimPlace,   ///< PlaceDimLine 模式，等待點擊確認偏移
        WaitValue,      ///< 等待使用者輸入數值/表達式
    };

    State                m_state         = State::Idle;
    QList<cad::GeomRef>  m_refs;
    cad::ConstraintType  m_type          = cad::ConstraintType::FixedDistance;
    cad::DistanceMode    m_distMode      = cad::DistanceMode::PointToPoint;
    double               m_measuredValue = 0.0;
    double               m_measuredValue2= 0.0;  // CoordinateDim Y
    double               m_pendingValue  = 0.0;
    QString              m_pendingExpr;
    bool                 m_driving       = true;
    double               m_dimOffsetX    = 0.0;
    double               m_dimOffsetY    = 0.0;
    bool                 m_hasPending    = false; ///< execute() 帶入了預設值

    void subscribeGeomPicked   ();
    void subscribeStringInput  ();
    void subscribeDimConfirmed ();
    void subscribePreview      ();
    void subscribeCancelled    ();
    void unsubscribeGeomPicked ();
    void unsubscribeAll        ();

    void onGeomPicked   (const QVariant& payload);
    void onStringInput  (const QVariant& payload);
    void onDimConfirmed (const QVariant& payload);
    void onCancelled    (const QVariant&);

    void transitionToWaitSecond  ();
    void transitionToWaitArcType ();
    void transitionToWaitDimPlace();
    void transitionToWaitValue   ();
    void commitDimension         ();
    void cleanup                 ();

    double        measureCurrentValue () const;  ///< 從幾何量測現有尺寸
    double        measureCurrentValue2() const;  ///< CoordinateDim Y 分量
    QVector2D     refMidpoint2D       () const;  ///< 用於 beginPlaceDimLine 錨點
    cad::Sketch*  activeSketch        () const;
    QString       constraintTypeName  () const;
};

} // namespace aicad::command
