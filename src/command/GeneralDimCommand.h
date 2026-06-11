#pragma once
#include "Command.h"
#include "cad/sketch/SketchConstraint.h"
#include "cad/sketch/GeneralDimClassifier.h"
#include "view/CadView.h"    // DimPreviewInfo
#include <QList>
#include <QString>
#include <QVector2D>

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
        WaitMenu,       ///< 等待使用者從選單選擇束制類型
        WaitArcType,    ///< 等待使用者選 Radius / ArcLength（已棄用，由 WaitMenu 取代）
        WaitDimPlace,   ///< PlaceDimLine 模式，等待點擊確認偏移
        WaitValue,      ///< 等待使用者輸入數值/表達式
    };

    State                m_state         = State::Idle;
    QList<cad::GeomRef>  m_refs;
    cad::GeomRef         m_originalLineRef;  ///< FixedLength 切換 H/V 時保存的原始單 ref
    cad::ConstraintType  m_type          = cad::ConstraintType::FixedDistance;
    cad::DistanceMode    m_distMode      = cad::DistanceMode::PointToPoint;
    double               m_measuredValue = 0.0;
    double               m_measuredValue2= 0.0;  // CoordinateDim Y
    double               m_pendingValue  = 0.0;
    QString              m_pendingExpr;
    bool                 m_driving       = true;
    double               m_dimOffsetX    = 0.0;
    double               m_dimOffsetY    = 0.0;
    QVector2D            m_dimAnchor2D;           ///< beginPlaceDimLine 時的錨點（草圖座標）
    bool                 m_hasPending    = false; ///< execute() 帶入了預設值

    /// WaitMenu 選單選項列表，key=使用者輸入字母, value=ConstraintType
    struct MenuOption {
        QString           key;   ///< 使用者輸入（單個字母，大小寫均可）
        QString           label; ///< 顯示給使用者的說明
        cad::ConstraintType type;
        bool              needSecond = false; ///< true = 選此項後進 WaitSecond
    };
    QList<MenuOption>    m_menuOptions;

    void subscribeGeomPicked   ();
    void subscribeGeomHover    ();   ///< 新增：GetGeom 模式 hover 預覽
    void subscribeStringInput  ();
    void subscribeDimConfirmed ();
    void subscribePreview      ();
    void subscribeCancelled    ();
    void unsubscribeGeomPicked ();
    void unsubscribeAll        ();

    void onGeomPicked   (const QVariant& payload);
    void onGeomHover    (const QVariant& payload);   ///< 新增
    void onStringInput  (const QVariant& payload);
    void onDimConfirmed (const QVariant& payload);
    void onCancelled    (const QVariant&);

    void transitionToWaitSecond  ();
    void transitionToWaitMenu    (const QList<MenuOption>& options, const QString& prompt);
    void transitionToWaitArcType ();
    void transitionToWaitDimPlace();
    void transitionToWaitValue   ();
    void commitDimension         ();
    void cleanup                 ();

    /// 依目前 m_refs + 可選的 extraRef 組出預覽，推送到 CadView
    void updateDimPreview(const cad::GeomRef* extraRef = nullptr);
    void clearDimPreview ();

    double        measureCurrentValue () const;  ///< 從幾何量測現有尺寸
    double        measureCurrentValue2() const;  ///< CoordinateDim Y 分量
    QVector2D     refMidpoint2D       () const;  ///< 用於 beginPlaceDimLine 錨點
    cad::Sketch*  activeSketch        () const;
    QString       constraintTypeName  () const;
};

} // namespace aicad::command