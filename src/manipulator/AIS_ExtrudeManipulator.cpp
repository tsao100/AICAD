#include "AIS_ExtrudeManipulator.h"

#include <QString>

#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <TopLoc_Location.hxx>
#include <Poly_Triangulation.hxx>
#include <Select3D_SensitiveTriangulation.hxx>
#include <TopExp_Explorer.hxx>

#include <StdPrs_ShadedShape.hxx>
#include <StdPrs_WFShape.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Prs3d_Arrow.hxx>
#include <Prs3d_Text.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <SelectMgr_Selection.hxx>
#include <Select3D_SensitiveCylinder.hxx>
#include <Select3D_SensitiveBox.hxx>
#include <SelectMgr_EntityOwner.hxx>

#include <gp_Trsf.hxx>
#include <gp_Ax3.hxx>

IMPLEMENT_STANDARD_RTTIEXT(aicad::manipulator::AIS_ExtrudeManipulator,
                           AIS_InteractiveObject)

namespace aicad::manipulator {

namespace {
// 把本地 Z 向圓柱/錐體轉換到任意方向的輔助函式
gp_Trsf directionTransform(const gp_Pnt& origin, const gp_Dir& dir)
{
    gp_Ax3 localAx(origin, dir);   // Z → dir
    gp_Trsf trsf;
    trsf.SetTransformation(localAx, gp_Ax3());  // world → local
    return trsf;
}
} // namespace

// ──────────────────────────────────────────────
AIS_ExtrudeManipulator::AIS_ExtrudeManipulator(const gp_Pnt& baseCenter,
                                               const gp_Dir& extrudeDir,
                                               double        height,
                                               const TopoDS_Shape& profileFace)
    : m_base(baseCenter)
    , m_dir(extrudeDir)
    , m_height(height)
    , m_profile(profileFace)
{}

void AIS_ExtrudeManipulator::SetHeight(double height)
{
    m_height = height;
    SetToUpdate();
}

void AIS_ExtrudeManipulator::SetSymmetric(bool s) { m_symmetric = s; SetToUpdate(); }
void AIS_ExtrudeManipulator::SetReversed (bool r) { m_reversed  = r; SetToUpdate(); }

gp_Pnt AIS_ExtrudeManipulator::ArrowTipPosition() const
{
    double totalLen = m_height + kScreenConeHeight;
    return m_base.Translated(gp_Vec(m_dir) * totalLen);
}

double AIS_ExtrudeManipulator::ComputeHeightFromDrag(const gp_Pnt& worldStart,
                                                     const gp_Pnt& worldCurrent) const
{
    // 投影到拉伸軸
    gp_Vec delta = gp_Vec(worldStart, worldCurrent);
    double proj  = delta.Dot(gp_Vec(m_dir));
    double sign  = m_reversed ? -1.0 : 1.0;
    return std::max(0.1, m_height + sign * proj);
}

// ── Compute (Presentation) ──────────────────────────────────────────────────
void AIS_ExtrudeManipulator::Compute(
    const Handle(PrsMgr_PresentationManager)&,
    const Handle(Prs3d_Presentation)& prs,
    Standard_Integer /*mode*/)
{
    prs->Clear();

    Quantity_Color arrowColor(1.0, 0.6, 0.1, Quantity_TOC_RGB);   // 橘黃色
    Quantity_Color coneColor (1.0, 0.8, 0.2, Quantity_TOC_RGB);   // 金黃色
    Quantity_Color flipColor (0.3, 0.8, 1.0, Quantity_TOC_RGB);   // 藍色

    buildArrow(prs, arrowColor, coneColor);
    buildFlipButton(prs);
    buildHeightLabel(prs);
}

void AIS_ExtrudeManipulator::buildArrow(const Handle(Prs3d_Presentation)& prs,
                                        const Quantity_Color& shaftColor,
                                        const Quantity_Color& coneColor)
{
    // ── 依縮放反算世界尺寸 ──
    const double shaftR  = worldSize(kScreenShaftRadius);
    const double coneR   = worldSize(kScreenConeRadius);
    const double coneH   = worldSize(kScreenConeHeight);
    const double shaftL  = worldSize(kScreenShaftLength);

    //double sign = m_reversed ? -1.0 : 1.0;
    gp_Pnt shaftEnd = m_base.Translated(gp_Vec(m_dir) * shaftL);

    // 1. 箭桿 Cylinder
    {
        gp_Trsf trsf = directionTransform(m_base, m_dir);
        BRepPrimAPI_MakeCylinder mkCyl(shaftR, shaftL);
        mkCyl.Build();
        if (mkCyl.IsDone()) {
            TopoDS_Shape shaft =
                BRepBuilderAPI_Transform(mkCyl.Shape(), trsf, true).Shape();
            myDrawer->ShadingAspect()->SetColor(shaftColor);
            StdPrs_ShadedShape::Add(prs, shaft, myDrawer);
        }
    }


    // 2. 錐頭，base 接在 shaftEnd，apex 朝外
    {
        gp_Trsf trsf = directionTransform(shaftEnd, m_dir);
        BRepPrimAPI_MakeCone mkCone(coneR, 0.0, coneH);
        mkCone.Build();
        if (mkCone.IsDone()) {
            TopoDS_Shape cone =
                BRepBuilderAPI_Transform(mkCone.Shape(), trsf, true).Shape();
            myDrawer->ShadingAspect()->SetColor(coneColor);
            StdPrs_ShadedShape::Add(prs, cone, myDrawer);
        }
    }


    // 3. 對稱反向箭頭
    if (m_symmetric) {
        gp_Dir negDir = m_dir.Reversed();
        gp_Pnt symEnd = m_base.Translated(gp_Vec(negDir) * shaftL);

        gp_Trsf trsfCyl = directionTransform(m_base, negDir);
        BRepPrimAPI_MakeCylinder mkCyl2(shaftR, shaftL);
        mkCyl2.Build();
        if (mkCyl2.IsDone()) {
            TopoDS_Shape shaft2 =
                BRepBuilderAPI_Transform(mkCyl2.Shape(), trsfCyl, true).Shape();
            myDrawer->ShadingAspect()->SetColor(shaftColor);
            StdPrs_ShadedShape::Add(prs, shaft2, myDrawer);
        }

        gp_Trsf trsf2 = directionTransform(symEnd, negDir);
        BRepPrimAPI_MakeCone mkCone2(coneR, 0.0, coneH);
        mkCone2.Build();
        if (mkCone2.IsDone()) {
            TopoDS_Shape cone2 =
                BRepBuilderAPI_Transform(mkCone2.Shape(), trsf2, true).Shape();
            StdPrs_ShadedShape::Add(prs, cone2, myDrawer);
        }
    }

    // 4. 繪製 profile 輪廓（高亮橙色 wireframe）
    if (!m_profile.IsNull()) {
        Handle(Graphic3d_Group) grpPro = prs->NewGroup();
        Handle(Prs3d_LineAspect) laProfile =
            new Prs3d_LineAspect(shaftColor, Aspect_TOL_SOLID, 1.5);
        grpPro->SetGroupPrimitivesAspect(laProfile->Aspect());
        StdPrs_WFShape::Add(prs, m_profile, myDrawer);
    }
}

void AIS_ExtrudeManipulator::buildFlipButton(const Handle(Prs3d_Presentation)& prs)
{
    const double flipOffset  = worldSize(kScreenFlipOffset);
    const double flipHalfLen = worldSize(kScreenFlipHalfLen);
    const double arrowLen    = worldSize(4.0);  // Prs3d_Arrow 長度

    // 雙向箭頭：畫在 base 下方偏移處，始終顯示於螢幕
    Quantity_Color flipColor(0.3, 0.8, 1.0, Quantity_TOC_RGB);

    // 找一個垂直於 m_dir 的側向偏移方向
    gp_Dir perp;
    if (std::abs(m_dir.X()) < 0.9)
        perp = gp_Dir(m_dir.Crossed(gp_Dir(1, 0, 0)));
    else
        perp = gp_Dir(m_dir.Crossed(gp_Dir(0, 1, 0)));

    gp_Pnt flipCenter = m_base.Translated(gp_Vec(perp) * flipOffset);
    gp_Pnt flipA = flipCenter.Translated(gp_Vec(m_dir) *  flipHalfLen);
    gp_Pnt flipB = flipCenter.Translated(gp_Vec(m_dir) * -flipHalfLen);

    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Prs3d_LineAspect) la =
        new Prs3d_LineAspect(flipColor, Aspect_TOL_SOLID, 2.0);
    grp->SetGroupPrimitivesAspect(la->Aspect());

    // 主線段
    Handle(Graphic3d_ArrayOfPolylines) poly = new Graphic3d_ArrayOfPolylines(2);
    poly->AddVertex((float)flipA.X(), (float)flipA.Y(), (float)flipA.Z());
    poly->AddVertex((float)flipB.X(), (float)flipB.Y(), (float)flipB.Z());
    grp->AddPrimitiveArray(poly);

    // 兩端小錐（用 Prs3d_Arrow 畫線式箭頭，最簡單）
    Prs3d_Arrow::Draw(prs->NewGroup(), flipA, m_dir,           M_PI/12.0, arrowLen);
    Prs3d_Arrow::Draw(prs->NewGroup(), flipB, m_dir.Reversed(), M_PI/12.0, arrowLen);
}

void AIS_ExtrudeManipulator::buildHeightLabel(const Handle(Prs3d_Presentation)& prs)
{
    gp_Pnt labelPos = ArrowTipPosition().Translated(gp_Vec(5, 5, 5));
    TCollection_ExtendedString txt(
        QString("%1 mm").arg(m_height, 0, 'f', 2).toStdString().c_str());
    Prs3d_Text::Draw(prs->NewGroup(), myDrawer->TextAspect(), txt, labelPos);
}

double AIS_ExtrudeManipulator::worldSize(double screenSize) const
{
    if (m_view.IsNull()) return screenSize;
    // V3d_View::Scale() 回傳「world 單位 / pixel」的比例
    // 我們要的是：world_size = screenSize / scale
    double scale = m_view->Scale();   // pixels per world unit
    if (scale < 1e-10) return screenSize;
    return screenSize / scale * 5.0;
}

IMPLEMENT_STANDARD_RTTIEXT(aicad::manipulator::ExtrudeOwner, SelectMgr_EntityOwner)

// ── ComputeSelection ────────────────────────────────────────────────────────
void AIS_ExtrudeManipulator::ComputeSelection(
    const Handle(SelectMgr_Selection)& sel,
    Standard_Integer mode)
{
    // ComputeSelection 內
    Handle(ExtrudeOwner) owner =
        new ExtrudeOwner(this, mode, /*priority*/8);

    if (mode == 1) {
        // 主箭頭：沿拉伸方向建立 Bnd_Box 作為選取體積
        // 找兩個垂直向量來建立 box
        gp_Dir perp1, perp2;
        if (std::abs(m_dir.X()) < 0.9)
            perp1 = gp_Dir(m_dir.Crossed(gp_Dir(1, 0, 0)));
        else
            perp1 = gp_Dir(m_dir.Crossed(gp_Dir(0, 1, 0)));
        perp2 = gp_Dir(m_dir.Crossed(perp1));

        double len = worldSize(kScreenShaftLength) + worldSize(kScreenConeHeight);
        double r   = worldSize(kScreenConeRadius) * 2.0;

        // 沿拉伸方向的 8 個角點 → Bnd_Box
        Bnd_Box box;
        for (int si : {-1, 1}) {
            for (int sj : {-1, 1}) {
                // 底端
                box.Add(m_base
                            .Translated(gp_Vec(perp1) * (r * si))
                            .Translated(gp_Vec(perp2) * (r * sj)));
                // 頂端
                box.Add(m_base
                            .Translated(gp_Vec(m_dir) * len)
                            .Translated(gp_Vec(perp1) * (r * si))
                            .Translated(gp_Vec(perp2) * (r * sj)));
            }
        }

        // 在 mode == 1 區塊，取代原本的 Select3D_SensitiveBox
        {
            gp_Trsf trsf = directionTransform(m_base, m_dir);  // ✅ 不再用 sign
            BRepPrimAPI_MakeCylinder mkCyl(worldSize(kScreenShaftRadius) * 2.5,
                                           m_height + worldSize(kScreenConeHeight));
            // 半徑略放大讓選取更容易
            mkCyl.Build();
            if (mkCyl.IsDone()) {
                TopoDS_Shape selShaft =
                    BRepBuilderAPI_Transform(mkCyl.Shape(), trsf, true).Shape();
                BRepMesh_IncrementalMesh mesh(selShaft, 1.0);
                TopExp_Explorer fExp(selShaft, TopAbs_FACE);
                for (; fExp.More(); fExp.Next()) {
                    TopLoc_Location loc;
                    Handle(Poly_Triangulation) tri =
                        BRep_Tool::Triangulation(TopoDS::Face(fExp.Current()), loc);
                    if (!tri.IsNull())
                        sel->Add(new Select3D_SensitiveTriangulation(owner, tri, loc, Standard_True));
                }
            }
        }

        // 如果對稱，加入反向側
        if (m_symmetric) {
            Bnd_Box box2;
            for (int si : {-1, 1}) {
                for (int sj : {-1, 1}) {
                    box2.Add(m_base
                                 .Translated(gp_Vec(perp1) * (r * si))
                                 .Translated(gp_Vec(perp2) * (r * sj)));
                    box2.Add(m_base
                                 .Translated(gp_Vec(m_dir.Reversed()) * len)
                                 .Translated(gp_Vec(perp1) * (r * si))
                                 .Translated(gp_Vec(perp2) * (r * sj)));
                }
            }
            Handle(Select3D_SensitiveBox) sbox2 =
                new Select3D_SensitiveBox(owner, box2);
            sel->Add(sbox2);
        }
        // ── profile face 選取（讓整個底面都可拖曳）──────────────────────
        if (mode == 1 && !m_profile.IsNull()) {
            // 三角剖分後建立 SensitiveTriangulation
            BRepMesh_IncrementalMesh mesh(m_profile, 1.0);   // 1 mm 容差
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri =
                BRep_Tool::Triangulation(TopoDS::Face(m_profile), loc);
            if (!tri.IsNull()) {
                Handle(Select3D_SensitiveTriangulation) sFace =
                    new Select3D_SensitiveTriangulation(owner, tri, loc,
                                                        Standard_True /*interior*/);
                sel->Add(sFace);
            }
        }
    }
    else if (mode == 2) {
        // 翻轉箭頭：沿側向偏移位置的小 box
        gp_Dir perp;
        if (std::abs(m_dir.X()) < 0.9)
            perp = gp_Dir(m_dir.Crossed(gp_Dir(1, 0, 0)));
        else
            perp = gp_Dir(m_dir.Crossed(gp_Dir(0, 1, 0)));

        gp_Pnt flipCenter = m_base.Translated(gp_Vec(perp) * worldSize(kScreenFlipOffset));
        Bnd_Box box;
        box.Add(flipCenter.Translated(gp_Vec(m_dir) *  (worldSize(kScreenFlipHalfLen) + worldSize(5.0))));
        box.Add(flipCenter.Translated(gp_Vec(m_dir) * -(worldSize(kScreenFlipHalfLen) + worldSize(5.0))));
        box.Enlarge(worldSize(6.0));

        Handle(Select3D_SensitiveBox) sbox =
            new Select3D_SensitiveBox(owner, box);
        sel->Add(sbox);
    }
}

} // namespace aicad::manipulator
