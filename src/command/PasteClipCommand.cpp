/**
 * @file PasteClipCommand.cpp
 * @brief 見 PasteClipCommand.h 檔頭說明。
 */
#include "PasteClipCommand.h"

#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../core/WinClipboardEmf.h"
#include "../cad/Sketch.h"
#include "../cad/sketch/SketchGeomTransformUtil.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"
#include "../cad/Plane.h"
#include "../view/RubberBand.h"

#include <AIS_InteractiveContext.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Quantity_Color.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Elips.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPointF>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QDebug>
#include <cmath>

namespace aicad {
namespace command {

using namespace cad;

namespace {

/// 超過這個頂點數的折線改成單一 SketchPolyline，不拆成逐段 Line＋Coincident
/// （樣條／橢圓弧被攤平後常有上百點，逐段拆會產生大量約束拖慢求解）。
constexpr int kMaxChainVertices = 100;

// QSettings 落地位置，比照專案內其他命令（見 PolygonCommand.cpp）
const char* kSettingsOrg   = "AICAD";
const char* kSettingsApp   = "AICAD";
const char* kSettingsGroup = "PasteClipCommand";
const char* kKeyMarkerLen  = "markerLength";
constexpr double kDefaultMarkerLen = 1.0;

QString fmt(double v) { return QString::number(v, 'g', 6); }

/**
 * 與 Sketch::addLineChainGeom() 相同的結果（逐段獨立 SketchLine ＋ 相鄰 Coincident，
 * 與 LineCommand 連續畫線一致，TRIM/FILLET/CHAMFER 依賴這種結構），差別是：
 *   - addLineGeom(emitSignals=false)，不逐筆重繪
 *   - addConstraint(..., solve=false)，不逐筆求解
 * addLineChainGeom() 內部每加一個 Coincident 就 solveConstraints() 一次，貼上整張圖
 * 時會變成 O(N) 次完整求解；這裡改成整批建完、由呼叫端最後統一 solve 一次。
 */
QStringList addLineChainNoSolve(Sketch* sk, const QVector<QVector2D>& pts, bool closed)
{
    QStringList ids;
    if (!sk || pts.size() < 2) return ids;

    for (int i = 0; i + 1 < pts.size(); ++i)
        ids.append(sk->addLineGeom(pts[i], pts[i + 1], QString(), QString(),
                                   GeomRole::Normal, /*emitSignals=*/false));
    if (closed)
        ids.append(sk->addLineGeom(pts.last(), pts.first(), QString(), QString(),
                                   GeomRole::Normal, /*emitSignals=*/false));

    for (int i = 0; i + 1 < ids.size(); ++i)
        sk->addConstraint(SketchConstraint::makeCoincident(
            GeomRef(ids[i], GeomHandle::End), GeomRef(ids[i + 1], GeomHandle::Start)), false);
    if (closed && ids.size() >= 2)
        sk->addConstraint(SketchConstraint::makeCoincident(
            GeomRef(ids.last(), GeomHandle::End), GeomRef(ids.first(), GeomHandle::Start)), false);
    return ids;
}

} // namespace

PasteClipCommand::PasteClipCommand()
    : Command("PASTECLIP",
              "Paste a drawing copied from AutoCAD (clipboard EMF) into the sketch (alias: PC)")
{
}

QString PasteClipCommand::getUsage() const
{
    return "Usage: PASTECLIP [file.emf] — copy objects in AutoCAD (Ctrl+C), then run PASTECLIP "
           "(or press Ctrl+V in sketch mode) and click the insertion point. If the drawing has an "
           "L-shaped scale marker at its lower-left, the scale is automatic and the command ends "
           "at the click (option L sets the marker length; default 1, remembered). Without a "
           "marker you can type a scale factor (e.g. 0.001), W<width> or H<height>; Esc finishes. "
           "Text, bitmaps and hatch fills are not pasted.";
}

cad::Sketch* PasteClipCommand::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult PasteClipCommand::execute(const CommandContext& ctx)
{
    auto* cmdMgr = core::CommandLineManager::instance();

    Sketch* sk = activeSketch();
    if (!sk) {
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    std::vector<unsigned char> bytes;
    if (!loadEmf(ctx.args, bytes))
        return CommandResult::Failure("Nothing to paste.");

    m_result = core::emf::parse(bytes.data(), bytes.size());
    if (!m_result.ok) {
        if (cmdMgr)
            cmdMgr->printError(QString("Cannot paste: %1.").arg(QString::fromStdString(m_result.error)));
        return CommandResult::Failure(QString::fromStdString(m_result.error));
    }

    // 比例尺標記：EMF 沒有 CAD 單位，用圖形左下角的 L 形標記反推倍率
    m_marker    = core::emf::ScaleMarker();
    m_markerLen = loadMarkerLength();   // 上次用 [Length(L)] 設定的標記長度（預設 1）
    {
        m_marker = core::emf::extractScaleMarker(m_result);
        if (cmdMgr) {
            if (m_marker.found) {
                cmdMgr->printSuccess(
                    QString("✅ Scale marker found (~%2 px long, assumed length %1; use option L to "
                            "change). Scale is set automatically; the marker lines are not pasted.")
                        .arg(fmt(m_markerLen)).arg(QString::number(m_marker.pixels, 'f', 1)));
                if (m_marker.pixels < 100.0)
                    cmdMgr->printWarning(
                        QString("⚠️  The marker is only ~%1 px long, so the scale may be off by about "
                                "±%2%. Draw a longer marker (e.g. 1/10 of the drawing width) and set its "
                                "length with option L.")
                            .arg(QString::number(m_marker.pixels, 'f', 0))
                            .arg(QString::number(100.0 / m_marker.pixels, 'f', 1)));
            } else {
                cmdMgr->printMessage(
                    "No scale marker found (an L of equal horizontal + vertical lines at the "
                    "lower-left of the drawing, with free ends).");
            }
        }
    }

    m_created.clear();
    m_factor = 1.0;
    m_state  = State::WaitInsertPoint;

    subscribeAll();
    setWaitingForInput();
    beginInsertStage();
    return CommandResult::Success("Waiting for insertion point...");
}

double PasteClipCommand::initialFactor() const
{
    if (m_marker.found && m_marker.lengthMm > 0.0) {
        const double f = m_markerLen / m_marker.lengthMm;
        if (f > 0.0 && std::isfinite(f)) return f;
    }
    return 1.0;
}

core::emf::Pt PasteClipCommand::anchorPoint() const
{
    if (m_marker.found) return m_marker.corner;
    return core::emf::Pt{ m_result.minX, m_result.minY };
}

void PasteClipCommand::showInsertPreview()
{
    using core::emf::PrimKind;
    hideInsertPreview();

    Sketch* sk = activeSketch();
    cad::Plane* plane = sk ? sk->plane() : nullptr;
    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (!plane || !cadView || cadView->context().IsNull() || m_result.prims.empty()) return;

    // 以「基準點 = 平面原點 (0,0)」建好幽靈圖形，之後只靠平移跟游標
    const double f = initialFactor();
    const core::emf::Pt a = anchorPoint();
    auto W = [&](const core::emf::Pt& p) {
        const QVector3D w = plane->toWorld(QVector2D(float((p.x - a.x) * f), float((p.y - a.y) * f)));
        return gp_Pnt(w.x(), w.y(), w.z());
    };
    const QVector3D n3 = plane->normal(), x3 = plane->xAxis(), y3 = plane->yAxis();
    const gp_Dir nrm(n3.x(), n3.y(), n3.z());

    BRep_Builder bb;
    TopoDS_Compound comp;
    bb.MakeCompound(comp);

    for (const core::emf::Primitive& pr : m_result.prims) {
        switch (pr.kind) {
        case PrimKind::Polyline: {
            BRepBuilderAPI_MakePolygon mk;
            for (const auto& p : pr.pts) mk.Add(W(p));
            if (pr.closed) mk.Close();
            if (mk.IsDone()) bb.Add(comp, mk.Wire());
            break;
        }
        case PrimKind::Circle: {
            const double r = pr.r * f;
            if (r <= 1e-12) break;
            const gp_Ax2 ax(W(pr.center), nrm, gp_Dir(x3.x(), x3.y(), x3.z()));
            BRepBuilderAPI_MakeEdge me(gp_Circ(ax, r));
            if (me.IsDone()) bb.Add(comp, me.Edge());
            break;
        }
        case PrimKind::Arc: {
            GC_MakeArcOfCircle arc(W(pr.pts[0]), W(pr.pts[1]), W(pr.pts[2]));
            if (arc.IsDone()) {
                BRepBuilderAPI_MakeEdge me(arc.Value());
                if (me.IsDone()) bb.Add(comp, me.Edge());
            }
            break;
        }
        case PrimKind::Ellipse: {
            const double major = pr.r * f, minor = pr.r2 * f;
            if (major <= 1e-12 || minor <= 1e-12 || minor > major) break;
            const double c = std::cos(pr.angleRad), s = std::sin(pr.angleRad);
            const gp_Dir xd(x3.x() * c + y3.x() * s, x3.y() * c + y3.y() * s, x3.z() * c + y3.z() * s);
            BRepBuilderAPI_MakeEdge me(gp_Elips(gp_Ax2(W(pr.center), nrm, xd), major, minor));
            if (me.IsDone()) bb.Add(comp, me.Edge());
            break;
        }
        }
    }

    m_ghost = new AIS_Shape(comp);
    m_ghost->SetColor(Quantity_Color(0.35, 0.80, 1.00, Quantity_TOC_RGB));
    m_ghost->SetWidth(1.0);
    m_ghost->SetZLayer(Graphic3d_ZLayerId_Top);   // 疊在草圖幾何之上
    // selectionMode = -1：不可被選取／不參與 hover，單純顯示
    cadView->context()->Display(m_ghost, AIS_WireFrame, -1, /*theToUpdateViewer=*/false);

    // 游標來源：RubberBand 設成 Line 模式但不加點 → 它自己什麼都不畫，只負責持續餵游標
    if (auto* band = cadView->rubberBand()) {
        band->setPlane(plane);
        band->setMode(view::RubberBandMode::Line);
        band->clearPoints();
        m_ghostConn = QObject::connect(band, &view::RubberBand::updated, this,
                                       [this] { updateInsertPreview(); });
    }
    // 若游標已在畫布上，立刻定位（不必等第一次滑鼠移動）
    updateInsertPreview();
}

void PasteClipCommand::updateInsertPreview()
{
    if (m_ghost.IsNull() || m_state != State::WaitInsertPoint) return;

    Sketch* sk = activeSketch();
    cad::Plane* plane = sk ? sk->plane() : nullptr;
    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    auto* band    = cadView ? cadView->rubberBand() : nullptr;
    if (!plane || !band || !band->hasCurrentPoint() || cadView->context().IsNull()) return;

    const QPointF cp = band->currentPoint();
    const QVector3D w  = plane->toWorld(QVector2D(float(cp.x()), float(cp.y())));
    const QVector3D w0 = plane->toWorld(QVector2D(0.0f, 0.0f));
    gp_Trsf t;
    t.SetTranslation(gp_Vec(w.x() - w0.x(), w.y() - w0.y(), w.z() - w0.z()));
    cadView->context()->SetLocation(m_ghost, TopLoc_Location(t));
    cadView->context()->UpdateCurrentViewer();
}

void PasteClipCommand::hideInsertPreview()
{
    QObject::disconnect(m_ghostConn);
    m_ghostConn = QMetaObject::Connection();

    if (!m_ghost.IsNull()) {
        auto* uiMgr   = core::Application::instance()->uiManager();
        auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
        if (cadView && !cadView->context().IsNull()) {
            cadView->context()->Remove(m_ghost, /*theToUpdateViewer=*/true);
        }
        m_ghost.Nullify();
        // RubberBand 回到未啟用狀態（沒有加過點，不會清到別人的預覽）
        if (cadView && cadView->rubberBand()) {
            cadView->rubberBand()->setMode(view::RubberBandMode::None);
            cadView->rubberBand()->clearPoints();
        }
    }
}

void PasteClipCommand::beginInsertStage()
{
    m_state = State::WaitInsertPoint;

    // GetPoint：點選平面上任一點作為插入點（與 MOVE/COPY 取基準點同一機制）
    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetPoint);

    showInsertPreview();   // 幽靈圖形跟隨游標（標記長度改變後會以新倍率重建）

    auto* cmdMgr = core::CommandLineManager::instance();
    if (!cmdMgr) return;
    // ⚠️ 提示文字「不能」以 "[PASTECLIP]" 這種方括號開頭：
    // CommandLineWidget::setCommandOptions() 把提示裡「第一組 [...]」當成選項列解析
    // （InputParser::parsePrompt），"[" 之前的文字當成輸入框左側的 prefix。開頭放方括號
    // 會讓 "PASTECLIP" 被當成唯一的選項 chip，真正的 [Length(L)] 反而被忽略，使用者只
    // 看到一個一直不變的 "PASTECLIP"。因此：
    //   - 物件數量／尺寸等資訊改成獨立一行訊息（寫進命令歷史）
    //   - 提示本身只有「指令動作 + 目前長度」當 prefix，後面接 [Length(L)] 選項 chip
    cmdMgr->printMessage(
        QString("PASTECLIP: %1 object(s), size %2 x %3 (before scaling)%4.")
            .arg(int(m_result.prims.size()))
            .arg(fmt(m_result.maxX - m_result.minX))
            .arg(fmt(m_result.maxY - m_result.minY))
            .arg(m_marker.found ? "; the marker's corner will be placed at the insertion point"
                                : "; the lower-left corner will be placed at the insertion point"));
    cmdMgr->showPrompt(
        QString("Specify insertion point (marker length %1) or [Length(L)]:").arg(fmt(m_markerLen)));
    // 讓 L 選項能被命令列攔截（滑鼠點取走 POINT_ACQUIRED，是獨立通道，可並存）。
    // 見 ChamferCommand::beginFirstObjectStage() 的同款說明。
    cmdMgr->waitForInput(core::InputType::Option);
}

void PasteClipCommand::onOptionSelected(const QVariant& payload)
{
    if (m_state != State::WaitInsertPoint) return;
    const QString opt = payload.toString().trimmed();
    // 命令列可能送 shortcut "L" 或 label "Length"，兩種都接受
    if (opt.compare(QStringLiteral("L"),      Qt::CaseInsensitive) != 0 &&
        opt.compare(QStringLiteral("Length"), Qt::CaseInsensitive) != 0)
        return;
    beginMarkerLengthStage();
}

void PasteClipCommand::beginMarkerLengthStage()
{
    m_state = State::WaitMarkerLen;
    hideInsertPreview();

    // 取點模式先關掉，避免輸入長度期間滑鼠點擊被當成插入點
    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(QString("Enter the scale marker length (current %1):")
                               .arg(fmt(m_markerLen)));
        cmdMgr->waitForInput(core::InputType::Number);
    }
}

double PasteClipCommand::loadMarkerLength()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.beginGroup(kSettingsGroup);
    double len = settings.value(kKeyMarkerLen, kDefaultMarkerLen).toDouble();
    settings.endGroup();
    if (!(len > 0.0) || !std::isfinite(len)) len = kDefaultMarkerLen;
    return len;
}

void PasteClipCommand::saveMarkerLength(double len)
{
    if (!(len > 0.0) || !std::isfinite(len)) return;
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.beginGroup(kSettingsGroup);
    settings.setValue(kKeyMarkerLen, len);
    settings.endGroup();
}

bool PasteClipCommand::loadEmf(const QStringList& args, std::vector<unsigned char>& bytes)
{
    auto* cmdMgr = core::CommandLineManager::instance();

    auto assign = [&bytes](const QByteArray& ba) {
        bytes.assign(reinterpret_cast<const unsigned char*>(ba.constData()),
                     reinterpret_cast<const unsigned char*>(ba.constData()) + ba.size());
        return !bytes.empty();
    };

    // 測試：PASTECLIP file.emf
    if (!args.isEmpty() && args.first().endsWith(".emf", Qt::CaseInsensitive)) {
        QFile f(args.first());
        if (!f.open(QIODevice::ReadOnly)) {
            if (cmdMgr) cmdMgr->printError(QString("Cannot open file: %1").arg(args.first()));
            return false;
        }
        return assign(f.readAll());
    }

    // 剪貼簿文字 "AICAD-EMF:<base64>"（Windows 端 tools/AICAD-CopyEmf.ps1 產生；
    // VirtualBox 共用剪貼簿只傳文字／點陣圖，EMF 本身過不來，所以先轉成文字）
    if (auto* cb = QGuiApplication::clipboard()) {
        static const QString kPrefix = QStringLiteral("AICAD-EMF:");
        const QString text = cb->text().trimmed();
        if (text.startsWith(kPrefix) &&
            assign(QByteArray::fromBase64(text.mid(kPrefix.size()).toLatin1())))
            return true;
    }

    // Windows 原生（AICAD 直接跑在裝有 AutoCAD 的 Windows 上）
    std::string formats;
    if (core::readClipboardEmf(bytes, formats))
        return true;

    if (cmdMgr) {
        cmdMgr->printError("Nothing to paste: the clipboard has no AutoCAD drawing (EMF).");
        cmdMgr->printMessage(
            QString("Clipboard formats: %1. In AutoCAD select objects and press Ctrl+C. "
                    "If AICAD runs in a VM, run tools/AICAD-CopyEmf.ps1 on Windows next, "
                    "then PASTECLIP here.").arg(QString::fromStdString(formats)));
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────
// 事件訂閱
// ─────────────────────────────────────────────────────────────────────────

void PasteClipCommand::subscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;

    bus->subscribe(core::Events::POINT_ACQUIRED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onPointAcquired(v); },
                                      Qt::QueuedConnection);
        });
    bus->subscribe(core::Events::NUMBER_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onNumberInput(v); },
                                      Qt::QueuedConnection);
        });
    bus->subscribe(core::Events::OPTION_SELECTED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onOptionSelected(v); },
                                      Qt::QueuedConnection);
        });
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void PasteClipCommand::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::POINT_ACQUIRED,    this);
    bus->unsubscribe(core::Events::NUMBER_INPUT,      this);
    bus->unsubscribe(core::Events::OPTION_SELECTED,   this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

// ─────────────────────────────────────────────────────────────────────────
// 插入
// ─────────────────────────────────────────────────────────────────────────

void PasteClipCommand::onPointAcquired(const QVariant& payload)
{
    if (m_state != State::WaitInsertPoint) return;

    const QPointF p = payload.toMap().value("point").value<QPointF>();
    insertAt(QVector2D(float(p.x()), float(p.y())));
}

void PasteClipCommand::insertAt(const QVector2D& insertPt)
{
    using core::emf::PrimKind;
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk = activeSketch();
    if (!sk) { cleanup(); return; }

    hideInsertPreview();   // 正式建立幾何前先拿掉幽靈圖形
    m_insertPt = insertPt;
    m_created.clear();

    // 倍率：有比例尺標記時 = 標記實際長度 ÷ 標記在圖片中的長度；否則 1:1，由使用者手動輸入倍率
    m_factor = 1.0;
    bool autoScaled = false;
    if (m_marker.found) {
        m_factor   = initialFactor();
        autoScaled = true;
    }

    // EMF 毫米座標 → 草圖座標：左下角對齊插入點，1:1
    // 有標記時以標記角點為基準（圖形相對位置 = 真實座標差），否則以圖形左下角
    const double ox = m_marker.found ? m_marker.corner.x : m_result.minX;
    const double oy = m_marker.found ? m_marker.corner.y : m_result.minY;
    auto conv = [&](const core::emf::Pt& p) {
        return QVector2D(float(double(insertPt.x()) + (p.x - ox) * m_factor),
                         float(double(insertPt.y()) + (p.y - oy) * m_factor));
    };

    int nLines = 0, nPoly = 0, nCircles = 0, nArcs = 0, nEllipses = 0, nFailed = 0;

    {
        // 一次貼上會連續建立大量幾何；逐筆都 emit geometryChanged／rebuildRequested
        // 會觸發 N 次完整重建（見 Sketch::removeMany() 的同類說明）。暫時擋掉信號，
        // 全部建完再統一 emit 一次。
        QSignalBlocker blocker(sk);

        for (const core::emf::Primitive& pr : m_result.prims) {
            switch (pr.kind) {
            case PrimKind::Polyline: {
                QVector<QVector2D> pts;
                pts.reserve(int(pr.pts.size()));
                for (const auto& p : pr.pts) pts.append(conv(p));

                if (pts.size() <= kMaxChainVertices) {
                    const QStringList ids = addLineChainNoSolve(sk, pts, pr.closed);
                    if (ids.isEmpty()) { ++nFailed; break; }
                    m_created += ids;
                    nLines += ids.size();
                } else {
                    const QString id = sk->addPolylineGeom(pts, pr.closed);
                    if (id.isEmpty()) { ++nFailed; break; }
                    m_created.append(id);
                    ++nPoly;
                }
                break;
            }
            case PrimKind::Circle: {
                const QString id = sk->addCircleGeom(conv(pr.center), pr.r * m_factor);
                if (id.isEmpty()) ++nFailed; else { m_created.append(id); ++nCircles; }
                break;
            }
            case PrimKind::Arc: {
                const QString id = sk->addArcGeom(conv(pr.pts[0]), conv(pr.pts[1]), conv(pr.pts[2]));
                if (id.isEmpty()) ++nFailed; else { m_created.append(id); ++nArcs; }
                break;
            }
            case PrimKind::Ellipse: {
                const QString id = sk->addEllipseGeom(conv(pr.center), pr.r * m_factor, pr.r2 * m_factor, pr.angleRad);
                if (id.isEmpty()) ++nFailed; else { m_created.append(id); ++nEllipses; }
                break;
            }
            }
        }
    }
    // 信號擋掉期間漏掉的 constraintAdded 等通知，由這一次統一求解補上
    // （求解會更新約束符號覆蓋層；新約束的點本來就重合，求解是 no-op 級別的快速收斂）。
    sk->solveConstraints();
    Q_EMIT sk->geometryChanged();
    Q_EMIT sk->rebuildRequested();

    if (cmdMgr) {
        QStringList parts;
        if (nLines)    parts << QString("%1 line(s)").arg(nLines);
        if (nPoly)     parts << QString("%1 polyline(s)").arg(nPoly);
        if (nCircles)  parts << QString("%1 circle(s)").arg(nCircles);
        if (nArcs)     parts << QString("%1 arc(s)").arg(nArcs);
        if (nEllipses) parts << QString("%1 ellipse(s)").arg(nEllipses);
        cmdMgr->printSuccess(QString("✅ Pasted %1.").arg(parts.isEmpty() ? "nothing" : parts.join(", ")));
        if (autoScaled)
            cmdMgr->printSuccess(QString("✅ Pasted at size %1 x %2 (scale %3 from %4).")
                                     .arg(fmt((m_result.maxX - m_result.minX) * m_factor))
                                     .arg(fmt((m_result.maxY - m_result.minY) * m_factor))
                                     .arg(fmt(m_factor))
                                     .arg("the scale marker"));
        if (nFailed)
            cmdMgr->printWarning(QString("⚠️  %1 object(s) could not be created.").arg(nFailed));
        printSummary();
    }

    if (m_created.isEmpty()) { cleanup(); return; }

    // 有比例尺標記：倍率已自動套用，點完插入點就結束（不需要再按 Esc）。
    if (m_marker.found) {
        if (cmdMgr) cmdMgr->printMessage("PASTECLIP done.");
        cleanup();
        return;
    }

    // 沒有標記：保留手動輸入倍率階段（W／H／倍數），Esc 結束；取點模式結束，回到一般草圖互動
    m_state  = State::WaitScale;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    if (cmdMgr) {
        cmdMgr->showPrompt(
            "Enter a scale factor (e.g. 0.001), W<width> or H<height> to resize, "
            "or press Esc to finish:");
        cmdMgr->waitForInput(core::InputType::Number);
    }
}

void PasteClipCommand::printSummary() const
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (!cmdMgr) return;

    if (m_result.recoveredCircles || m_result.recoveredArcs)
        cmdMgr->printMessage(
            QString("Recovered %1 circle(s) and %2 arc(s) from tessellated polylines.")
                .arg(m_result.recoveredCircles).arg(m_result.recoveredArcs));

    QStringList skipped;
    if (m_result.textRecords)   skipped << QString("%1 text item(s)").arg(m_result.textRecords);
    if (m_result.bitmapRecords) skipped << QString("%1 bitmap(s)").arg(m_result.bitmapRecords);
    if (m_result.filledSkipped) skipped << QString("%1 filled shape(s)/hatch").arg(m_result.filledSkipped);
    if (!skipped.isEmpty())
        cmdMgr->printWarning(QString("⚠️  Not pasted (not geometry in the clipboard picture): %1.")
                                 .arg(skipped.join(", ")));

    if (!m_marker.found)
        cmdMgr->printMessage(
            "Note: the clipboard holds a picture, so the size depends on AutoCAD's zoom when you "
            "copied. Use the scale prompt, or draw an L marker (equal horizontal + vertical lines, "
            "1 unit) at the drawing's lower-left corner in AutoCAD for automatic scaling.");
}

// ─────────────────────────────────────────────────────────────────────────
// 倍率校正
// ─────────────────────────────────────────────────────────────────────────

void PasteClipCommand::onNumberInput(const QVariant& payload)
{
    if (m_state == State::WaitMarkerLen) {
        auto* cm = core::CommandLineManager::instance();
        bool ok = false;
        const double v = payload.toString().trimmed().toDouble(&ok);
        if (!ok || !(v > 0.0) || !std::isfinite(v)) {
            if (cm) {
                cm->printError("Invalid length. Enter a positive number:");
                cm->waitForInput(core::InputType::Number);
            }
            return;
        }
        m_markerLen = v;
        saveMarkerLength(v);              // QSettings：下次執行沿用
        if (cm) cm->printSuccess(QString("✅ Marker length = %1.").arg(fmt(v)));
        beginInsertStage();               // 回到「指定插入點」
        return;
    }
    if (m_state != State::WaitScale) return;

    auto* cmdMgr = core::CommandLineManager::instance();
    const QString text = payload.toString().trimmed();

    const double w0 = m_result.maxX - m_result.minX;   // 剛貼上（倍率 1）時的總寬／總高
    const double h0 = m_result.maxY - m_result.minY;

    double factor = 0.0;
    static const QRegularExpression rxWH(R"(^([WwHh])\s*[=: ]?\s*([-+0-9.eE]+)$)");
    const QRegularExpressionMatch m = rxWH.match(text);
    bool ok = false;
    if (m.hasMatch()) {
        const double target = m.captured(2).toDouble(&ok);
        const bool isW = m.captured(1).toLower() == "w";
        const double base = isW ? w0 : h0;
        if (ok && target > 0.0 && base > 1e-12) factor = target / base;
        else ok = false;
    } else {
        factor = text.toDouble(&ok);
    }

    if (!ok || !(factor > 0.0) || !std::isfinite(factor)) {
        if (cmdMgr) {
            cmdMgr->printError("Invalid input. Enter a positive factor, W<width> or H<height> (Esc to finish):");
            cmdMgr->waitForInput(core::InputType::Number);
        }
        return;
    }

    applyAbsoluteFactor(factor);

    if (cmdMgr) {
        cmdMgr->printSuccess(QString("✅ Size is now %1 x %2.")
                                 .arg(fmt(w0 * m_factor)).arg(fmt(h0 * m_factor)));
        cmdMgr->showPrompt("Enter another scale factor / W<width> / H<height>, or press Esc to finish:");
        cmdMgr->waitForInput(core::InputType::Number);   // 留在倍率階段（每次派送後需重新等待）
    }
}

void PasteClipCommand::applyAbsoluteFactor(double factor)
{
    Sketch* sk = activeSketch();
    if (!sk || m_created.isEmpty()) return;

    const double rel = factor / m_factor;
    if (std::fabs(rel - 1.0) < 1e-12) return;

    const auto xf = cad::transform::Transform2D::scale(m_insertPt, rel);
    cad::transform::applyToSelection(sk, m_created, xf);
    m_factor = factor;
}

// ─────────────────────────────────────────────────────────────────────────
// 結束
// ─────────────────────────────────────────────────────────────────────────

void PasteClipCommand::onCancelled(const QVariant&)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        if (m_state == State::WaitScale)
            cmdMgr->printMessage("PASTECLIP done.");
        else
            cmdMgr->printMessage("PASTECLIP cancelled.");
    }
    cleanup();
}

void PasteClipCommand::cleanup()
{
    hideInsertPreview();
    unsubscribeAll();

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    m_state = State::Idle;
    m_created.clear();
    m_result = core::emf::Result();

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
