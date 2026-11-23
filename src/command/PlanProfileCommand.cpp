/**
 * @file PlanProfileCommand.cpp
 */
#include "command/PlanProfileCommand.h"

#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/CommandLineManager.h"
#include "cad/Document.h"
#include "drawing/DrawingSheet.h"
#include "drawing/DrawingSheetManager.h"
#include "drawing/DrawingSheetWidget.h"   // 內含 DrawingSheetDialog
#include "drawing/PlanProfileSettings.h"
#include "drawing/PlanProfileGenerator.h"
#include "drawing/PlanProfileDialog.h"
#include "drawing/PlanProfilePainter.h"
#include "railway/RailwayAlignment.h"
#include "view/CadView.h"
#include "command/DrawingBookActions.h"
#include "core/EventBus.h"
#include "core/geometry/ProjectOrigin.h"
#include "command/CommandTypes.h"
#include <QtMath>
#include <QTimer>
#include <algorithm>
#include <QVariantMap>

#include <QWidget>
#include <QJsonArray>

namespace aicad {
namespace command {

using core::Application;

namespace {

drawing::DrawingSheetManager* managerFor(cad::Document* doc)
{
    for (QObject* child : doc->children())
        if (auto* mgr = qobject_cast<drawing::DrawingSheetManager*>(child))
            return mgr;
    auto* mgr = new drawing::DrawingSheetManager(doc);
    mgr->setSourceDocument(doc);
    mgr->loadFromJson(doc->drawingSheetsData());
    return mgr;
}

} // namespace

PlanProfileCommand::PlanProfileCommand()
    : Command("planprofile", "Plan & Profile Sheets")
{}

QString PlanProfileCommand::getUsage() const
{
    return "Usage: planprofile [子命令] — 無子命令＝開啟平縱面圖參數對話框。子命令：\n"
           "  GEN [線形…]         可列多條起始線形（名稱或id，空白分隔）；以目前設定（作用中平縱面圖的設定，否則「我的預設」）直接產生\n"
           "  LOAD <檔>           載入 .aicad_ppcfg 設定檔後產生\n"
           "  SAVE <檔>           將目前設定存成 .aicad_ppcfg\n"
           "  INDEX               開啟圖目錄，調整圖的順序（會重新編號並重建圖目錄）\n"
           "  INFO                列出線形、里程範圍與分段預覽\n"
           "（圖紙與設定隨文件存檔時一併寫入 .aicad）";
}

CommandResult PlanProfileCommand::execute(const CommandContext& context)
{
    Application* app = Application::instance();
    core::CommandLineManager* clm = app->commandLineManager();
    cad::Document* doc = app->documentManager()->currentDocument();
    if (!doc) return CommandResult::Failure("No active document");

    auto say = [&](const QString& m) { if (clm) clm->printMessage(QStringLiteral("[PPS] ") + m); };

    if (doc->trackCenterLines().isEmpty())
        return CommandResult::Failure("文件中沒有線形（TrackCenterLine），請先建立或匯入線形");

    drawing::DrawingSheetManager* mgr = managerFor(doc);

    const QString sub = context.args.isEmpty() ? QString()
                                               : context.args.first().trimmed().toUpper();
    const QStringList rest = context.args.mid(1);

    // 起始設定：作用中的平縱面圖 → 我的預設
    drawing::PlanProfileSettings s = drawing::PlanProfileSettings::loadDefault();
    if (const auto* cur = drawing::planProfileSettingsOf(mgr->activeSheet()))
        s = *cur;
    auto findTcl = [&](const QString& key) -> QString {
        for (railway::TrackCenterLine* t : doc->trackCenterLines())
            if (t && (t->id() == key || t->name() == key)) return t->id();
        return QString();
    };
    {   // 起始線形：保留仍存在者；全沒有時取第一條
        QStringList ok;
        for (const QString& id : s.startIds()) if (doc->findTrackCenterLine(id)) ok << id;
        if (ok.isEmpty()) ok << doc->trackCenterLines().first()->id();
        s.tclIds = ok;
        s.tclId = ok.first();
    }

    if (sub == "INFO") {
        for (railway::TrackCenterLine* t : doc->trackCenterLines()) {
            double a = 0, b = 0;
            const bool ok = drawing::planProfileChainageRange(t, a, b);
            say(QStringLiteral("線形 %1 (id=%2)  里程 %3 ~ %4").arg(t->name(), t->id())
                    .arg(ok ? a : 0, 0, 'f', 2).arg(ok ? b : 0, 0, 'f', 2));
        }
        QString err;
        const auto segs = drawing::planProfileSegments(doc, s, &err);
        if (segs.isEmpty()) say(QStringLiteral("目前設定無法分段：%1").arg(err));
        else say(QStringLiteral("以線形 %1、1:%2 分為 %3 張").arg(s.startIds().join(QStringLiteral("、"))).arg(s.hScale, 0, 'f', 0).arg(segs.size()));
        return CommandResult::Success();
    }

    if (sub == "INDEX") {
        QString bookId;
        if (mgr->activeSheet()) bookId = mgr->activeSheet()->bookId();
        if (bookId.isEmpty() && !mgr->books().isEmpty()) bookId = mgr->books().first().id;
        if (drawing::runPlanProfileIndexEditor(mgr, doc, static_cast<QWidget*>(context.cadView), bookId))
            say(QStringLiteral("已依新順序重新編號並重建圖目錄。"));
        return CommandResult::Success();
    }

    if (sub == "SAVE") {
        if (rest.isEmpty()) return CommandResult::Failure("用法：PPS SAVE <檔案路徑>");
        QString err;
        if (!s.saveToFile(rest.join(' '), &err)) return CommandResult::Failure(err);
        say(QStringLiteral("設定已存到 %1").arg(rest.join(' ')));
        return CommandResult::Success();
    }

    if (sub == "LOAD") {
        if (rest.isEmpty()) return CommandResult::Failure("用法：PPS LOAD <檔案路徑>");
        QString err;
        const QStringList keep = s.startIds();
        if (!s.loadFromFile(rest.join(' '), &err)) return CommandResult::Failure(err);
        QStringList ok;
        for (const QString& id : s.startIds()) if (doc->findTrackCenterLine(id)) ok << id;
        s.tclIds = ok.isEmpty() ? keep : ok;
        s.tclId = s.tclIds.first();
    } else if (sub == "GEN") {
        if (!rest.isEmpty()) {
            QStringList ids;
            const QString whole = findTcl(rest.join(' '));      // 名稱含空白時整串比對
            if (!whole.isEmpty()) ids << whole;
            else for (const QString& a : rest) {
                const QString id = findTcl(a);
                if (id.isEmpty()) return CommandResult::Failure(QStringLiteral("找不到線形：%1").arg(a));
                ids << id;
            }
            s.tclIds = ids;
            s.tclId = ids.first();
        }
    } else {
        // 互動：對話框
        QWidget* parentWidget = static_cast<QWidget*>(context.cadView);
        drawing::PlanProfileDialog dlg(doc, s, parentWidget);
        if (dlg.exec() != QDialog::Accepted)
            return CommandResult::Success("Cancelled");
        s = dlg.settings();
    }

    QString err;
    const int n = drawing::generatePlanProfileSheets(mgr, doc, s, &err);
    if (n <= 0)
        return CommandResult::Failure(err.isEmpty() ? QStringLiteral("產生失敗") : err);

    doc->setModified(true);
    say(QStringLiteral("已產生 %1 張平縱面圖（存檔時隨 .aicad 保存）。").arg(n));

    if (sub.isEmpty() || sub == "LOAD") {
        if (drawing::DrawingSheet* first = mgr->activeSheet()) {
            QWidget* parentWidget = static_cast<QWidget*>(context.cadView);
            const QJsonArray before = mgr->toJson();
            drawing::DrawingSheetDialog dlg(first, parentWidget);
            dlg.exec();
            if (mgr->toJson() != before) doc->setModified(true);
        }
    }
    return CommandResult::Success(QStringLiteral("Generated %1 plan/profile sheets").arg(n));
}

REGISTER_COMMAND("planprofile", PlanProfileCommand);

// ─────────────────────────────────────────────────────────────────────────────
// DEPOTVIEW
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/** 取得要修改的圖冊：作用中圖紙所屬圖冊，否則第一個圖冊。 */
drawing::DrawingBook* depotBook(drawing::DrawingSheetManager* mgr)
{
    QString bookId;
    if (mgr->activeSheet()) bookId = mgr->activeSheet()->bookId();
    if (bookId.isEmpty() && !mgr->books().isEmpty()) bookId = mgr->books().first().id;
    return bookId.isEmpty() ? nullptr : mgr->findBook(bookId);
}

/** 寫回視圖清單並重算圖冊。 */
bool depotApply(drawing::DrawingSheetManager* mgr, cad::Document* doc, const QString& bookId,
                drawing::PlanProfileSettings g, const QVector<drawing::DepotView>& views, QString* err,
                bool full = false)
{
    g.depotViews = views;
    g.generateDepot = true;
    g.replaceExisting = true;
    // 只動視圖時走快速路徑（只重建機廠圖頁與圖目錄）；GEN 才整本重算
    const bool ok = full ? drawing::updatePlanProfileBook(mgr, doc, bookId, g, err)
                         : drawing::updatePlanProfileDepotViews(mgr, doc, bookId, views, err);
    if (!ok) return false;
    doc->setModified(true);
    refreshDepotOverlays(doc);
    return true;
}

} // namespace

DepotViewCommand::DepotViewCommand()
    : Command("depotview", "Depot Plan Views")
{}

DepotViewCommand::~DepotViewCommand() {}

QString DepotViewCommand::getUsage() const
{
    return "Usage: depotview [子命令] — 無子命令＝在 View 中點選新增矩形視圖。子命令：\n"
           "  LIST                         列出視圖\n"
           "  ADD 名稱 E N 寬 高 [轉角 排序 比例]   以 TM2 座標新增（轉角：逆時針度數，比例 0＝自動）\n"
           "  SET 序號 key=值 …            key：name e n w h rot order scale align（線形名稱或 id，- 取消）ch（對齊里程）\n"
           "  DEL 序號                     刪除視圖\n"
           "  SIZE                         列出目前圖紙在常用比例下矩形視圖最大可到多大\n"
           "  ORDER 序號 [新序號]          變更視圖序號（不給新序號會開對話框）\n"
           "  EDIT 序號                    在 View 中以滑鼠編輯：左鍵拖曳、右鍵點一下開選單、ESC 結束\n"
           "  GEN                          依目前視圖重算圖冊\n"
           "互動新增：①在一段直線上依序點兩點（決定轉角，視圖 X 軸沿該方向）②點矩形第一角 ③點對角。";
}

void DepotViewCommand::prompt(const QString& msg)
{
    core::Application::instance()->eventBus()->publish(core::Events::COMMAND_PROMPT, msg);
    outputMessage(msg);
}

CommandResult DepotViewCommand::execute(const CommandContext& context)
{
    core::Application* app = core::Application::instance();
    core::EventBus* bus = app->eventBus();
    cad::Document* doc = app->documentManager()->currentDocument();
    if (!doc) return CommandResult::Failure("No active document");
    drawing::DrawingSheetManager* mgr = managerFor(doc);
    drawing::DrawingBook* book = depotBook(mgr);
    if (!book) return CommandResult::Failure("沒有圖冊：請先用 PPS 產生平縱面圖冊");
    const QString bookId = book->id;
    drawing::PlanProfileSettings g;
    g.fromJson(book->settings);

    const QString sub = context.args.isEmpty() ? QString() : context.args.first().trimmed().toUpper();
    const QStringList rest = context.args.mid(1);
    auto findTcl = [&](const QString& key) -> QString {
        for (railway::TrackCenterLine* t : doc->trackCenterLines())
            if (t && (t->id() == key || t->name() == key)) return t->id();
        return QString();
    };

    if (sub == "LIST") {
        if (g.depotViews.isEmpty()) outputMessage(QStringLiteral("尚無機廠視圖。"));
        for (int i = 0; i < g.depotViews.size(); ++i) {
            const drawing::DepotView& v = g.depotViews[i];
            const railway::TrackCenterLine* t = doc->findTrackCenterLine(v.alignTcl);
            outputMessage(QStringLiteral("%1. %2  中心(E %3, N %4)  %5×%6 m  轉角 %7°%8  排序 %9  比例 %10")
                .arg(i + 1).arg(v.name).arg(v.e, 0, 'f', 3).arg(v.n, 0, 'f', 3).arg(v.w, 0, 'f', 1).arg(v.h, 0, 'f', 1)
                .arg(v.rotDeg, 0, 'f', 3)
                .arg(t ? QStringLiteral(" 對齊 %1@%2").arg(t->name()).arg(v.alignCh, 0, 'f', 2) : QString())
                .arg(v.order).arg(v.scale > 0 ? QStringLiteral("1:%1").arg(v.scale, 0, 'f', 0) : QStringLiteral("自動")));
        }
        return CommandResult::Success();
    }
    if (sub == "ADD") {
        if (rest.size() < 5) return CommandResult::Failure("用法：DV ADD 名稱 E N 寬 高 [轉角 排序 比例]");
        drawing::DepotView v;
        v.name = rest[0];
        v.e = rest[1].toDouble(); v.n = rest[2].toDouble();
        v.w = rest[3].toDouble(); v.h = rest[4].toDouble();
        if (!(v.w > 0) || !(v.h > 0)) return CommandResult::Failure("寬、高必須大於 0");
        if (rest.size() > 5) v.rotDeg = rest[5].toDouble();
        v.order = rest.size() > 6 ? rest[6].toInt() : g.depotViews.size() + 1;
        if (rest.size() > 7) v.scale = rest[7].toDouble();
        QVector<drawing::DepotView> views = g.depotViews;
        views.append(v);
        QString err;
        if (!depotApply(mgr, doc, bookId, g, views, &err)) return CommandResult::Failure(err);
        return CommandResult::Success(QStringLiteral("已新增視圖「%1」").arg(v.name));
    }
    if (sub == "SET") {
        if (rest.size() < 2) return CommandResult::Failure("用法：DV SET 序號 key=值 …");
        const int idx = rest[0].toInt() - 1;
        if (idx < 0 || idx >= g.depotViews.size()) return CommandResult::Failure("序號超出範圍");
        QVector<drawing::DepotView> views = g.depotViews;
        drawing::DepotView& v = views[idx];
        for (int i = 1; i < rest.size(); ++i) {
            const int eq = rest[i].indexOf(QLatin1Char('='));
            if (eq <= 0) return CommandResult::Failure(QStringLiteral("格式錯誤：%1（應為 key=值）").arg(rest[i]));
            const QString k = rest[i].left(eq).toLower(), val = rest[i].mid(eq + 1);
            if (k == "name") v.name = val;
            else if (k == "e") v.e = val.toDouble();
            else if (k == "n") v.n = val.toDouble();
            else if (k == "w") v.w = val.toDouble();
            else if (k == "h") v.h = val.toDouble();
            else if (k == "rot") v.rotDeg = val.toDouble();
            else if (k == "order") v.order = val.toInt();
            else if (k == "scale") v.scale = val.toDouble();
            else if (k == "ch") v.alignCh = val.toDouble();
            else if (k == "align") {
                if (val == QLatin1String("-")) v.alignTcl.clear();
                else {
                    const QString id = findTcl(val);
                    if (id.isEmpty()) return CommandResult::Failure(QStringLiteral("找不到線形：%1").arg(val));
                    v.alignTcl = id;
                }
            } else return CommandResult::Failure(QStringLiteral("未知的 key：%1").arg(k));
        }
        QString err;
        if (!depotApply(mgr, doc, bookId, g, views, &err)) return CommandResult::Failure(err);
        return CommandResult::Success(QStringLiteral("已更新視圖 %1").arg(idx + 1));
    }
    if (sub == "DEL") {
        if (rest.isEmpty()) return CommandResult::Failure("用法：DV DEL 序號");
        const int idx = rest[0].toInt() - 1;
        if (idx < 0 || idx >= g.depotViews.size()) return CommandResult::Failure("序號超出範圍");
        QVector<drawing::DepotView> views = g.depotViews;
        views.removeAt(idx);
        QString err;
        if (!depotApply(mgr, doc, bookId, g, views, &err)) return CommandResult::Failure(err);
        return CommandResult::Success(QStringLiteral("已刪除視圖 %1").arg(idx + 1));
    }
    if (sub == "GEN") {
        QString err;
        if (!depotApply(mgr, doc, bookId, g, g.depotViews, &err, true)) return CommandResult::Failure(err);
        return CommandResult::Success("已重算圖冊");
    }
    if (sub == "SIZE") {
        for (const QString& ln : drawing::planProfileDepotSizeHint(g).split(QLatin1Char('\n'))) outputMessage(ln);
        return CommandResult::Success();
    }
    if (sub == "ORDER") {
        const int idx = rest.isEmpty() ? -1 : rest[0].toInt() - 1;
        if (idx < 0 || idx >= g.depotViews.size()) return CommandResult::Failure("用法：DV ORDER 序號 [新序號]");
        if (rest.size() < 2) {      // 沒給新序號：開對話框
            runDrawingBookAction(doc, static_cast<QWidget*>(context.cadView), QStringLiteral("depotOrder"),
                                 QStringLiteral("dview_%1_%2").arg(bookId).arg(idx));
            return CommandResult::Success();
        }
        const int np = rest[1].toInt();
        QVector<drawing::DepotView> views = g.depotViews;
        QVector<int> ord;
        for (int i = 0; i < views.size(); ++i) ord.append(i);
        std::stable_sort(ord.begin(), ord.end(), [&](int a, int b) { return views[a].order < views[b].order; });
        ord.removeOne(idx);
        ord.insert(std::max(0, std::min(np - 1, int(ord.size()))), idx);
        for (int pos = 0; pos < ord.size(); ++pos) views[ord[pos]].order = pos + 1;
        QString err;
        if (!depotApply(mgr, doc, bookId, g, views, &err)) return CommandResult::Failure(err);
        return CommandResult::Success(QStringLiteral("視圖 %1 的序號改為 %2").arg(idx + 1).arg(np));
    }
    if (sub == "EDIT") {
        const int idx = rest.isEmpty() ? -1 : rest[0].toInt() - 1;
        if (idx < 0 || idx >= g.depotViews.size()) return CommandResult::Failure("用法：DV EDIT 序號");
        runDrawingBookAction(doc, nullptr, QStringLiteral("depotEditView"),
                             QStringLiteral("dview_%1_%2").arg(bookId).arg(idx));
        return CommandResult::Success("在 View 中編輯：左鍵拖曳、右鍵點一下開選單、ESC 結束");
    }
    if (!sub.isEmpty()) return CommandResult::Failure(getUsage());

    // ── 互動 ──
    m_stage = 0;
    m_done = false;
    QVariantMap viewSetup;
    viewSetup["mode"] = "sketching";
    viewSetup["rubberBandMode"] = "line";
    viewSetup["gripsEnabled"] = false;      // 只需取點：關掉 grips，避免每次滑鼠移動都做 hit-test 造成卡頓
    bus->publish("command.request-view-setup", viewSetup);
    bus->subscribe(core::Events::POINT_ACQUIRED, this, [this](const QVariant& data) {
        const QPointF p = data.toMap()["point"].value<QPointF>();
        QMetaObject::invokeMethod(this, [this, p]() { handlePoint(p); }, Qt::QueuedConnection);
    });
    bus->subscribe(core::Events::POINT_CANCELLED, this, [this](const QVariant&) {
        QMetaObject::invokeMethod(this, [this]() { handleCancelled(); }, Qt::QueuedConnection);
    });
    setState(CommandState::Running);
    prompt(QStringLiteral("在一段直線上指定第一點（決定視圖轉角；ESC 取消）："));
    return CommandResult::Success("Waiting for input");
}

void DepotViewCommand::handlePoint(const QPointF& p)
{
    if (m_done) return;
    core::EventBus* bus = core::Application::instance()->eventBus();
    auto rubberFrom = [&](const QPointF& a) {
        QVariantMap m;
        m["action"] = "clearAndAdd";
        m["point"] = QVariant::fromValue(a);
        bus->publish("command.update-rubber-band", m);
    };
    switch (m_stage) {
    case 0:
        m_a = p; m_stage = 1;
        rubberFrom(p);
        prompt(QStringLiteral("指定該直線上的第二點："));
        break;
    case 1:
        if (std::hypot(p.x() - m_a.x(), p.y() - m_a.y()) < 1e-6) { prompt(QStringLiteral("兩點重合，請重新指定第二點：")); return; }
        m_b = p; m_stage = 2;
        {
            QVariantMap m; m["action"] = "clear";
            bus->publish("command.update-rubber-band", m);
        }
        outputMessage(QStringLiteral("轉角 %1°").arg(qRadiansToDegrees(std::atan2(m_b.y() - m_a.y(), m_b.x() - m_a.x())), 0, 'f', 3));
        prompt(QStringLiteral("指定矩形第一角點："));
        break;
    case 2:
        m_c1 = p; m_stage = 3;
        rubberFrom(p);
        prompt(QStringLiteral("指定矩形對角點："));
        break;
    default:
        {
            const double th = std::atan2(m_b.y() - m_a.y(), m_b.x() - m_a.x());
            const double ex = std::cos(th), ey = std::sin(th);
            const double dx = p.x() - m_c1.x(), dy = p.y() - m_c1.y();
            const double w = std::fabs(dx * ex + dy * ey), h = std::fabs(-dx * ey + dy * ex);
            if (w < 1e-6 || h < 1e-6) { prompt(QStringLiteral("矩形寬或高為 0，請重新指定對角點：")); return; }
            const QPointF mid((m_c1.x() + p.x()) / 2, (m_c1.y() + p.y()) / 2);
            core::geometry::ProjectOrigin::ensureDefault();
            const QPointF g = core::geometry::ProjectOrigin::instance().toGlobal(mid);

            cad::Document* doc = core::Application::instance()->documentManager()->currentDocument();
            drawing::DrawingSheetManager* mgr = doc ? managerFor(doc) : nullptr;
            drawing::DrawingBook* book = mgr ? depotBook(mgr) : nullptr;
            if (!book) { m_done = true; Q_EMIT finished(CommandResult::Failure("沒有圖冊")); return; }
            const QString bookId = book->id;
            drawing::PlanProfileSettings s;
            s.fromJson(book->settings);
            drawing::DepotView v;
            v.order = s.depotViews.size() + 1;
            v.name = QStringLiteral("機廠視圖 %1").arg(v.order);
            v.e = g.x(); v.n = g.y(); v.w = w; v.h = h;
            v.rotDeg = qRadiansToDegrees(th);
            QVector<drawing::DepotView> views = s.depotViews;
            views.append(v);
            QString err;
            m_done = true;
            if (!depotApply(mgr, doc, bookId, s, views, &err)) {
                Q_EMIT finished(CommandResult::Failure(err.isEmpty() ? QStringLiteral("重算失敗") : err));
                return;
            }
            outputMessage(QStringLiteral("已新增「%1」：中心 E %2 N %3、%4×%5 m、轉角 %6°。可用 DV SET 改名稱／排序／比例／對齊。")
                              .arg(v.name).arg(v.e, 0, 'f', 3).arg(v.n, 0, 'f', 3).arg(w, 0, 'f', 1).arg(h, 0, 'f', 1)
                              .arg(v.rotDeg, 0, 'f', 3));
            {
                const double fit = drawing::planProfileDepotFitScale(s, w, h);
                outputMessage(fit > 0 ? QStringLiteral("此矩形可用的最小標準比例：1:%1（DV SIZE 可查各比例的最大矩形）。").arg(fit, 0, 'f', 0)
                                      : QStringLiteral("此矩形超過 1:5000 一張圖紙的範圍，請縮小或分成多個視圖。"));
            }
            Q_EMIT finished(CommandResult::Success("Depot view added"));
            // 新增後直接進入 View 內編輯（拖放／右鍵選單可微調）
            const QString editId = QStringLiteral("dview_%1_%2").arg(bookId).arg(views.size() - 1);
            QTimer::singleShot(0, [doc, editId]() { runDrawingBookAction(doc, nullptr, QStringLiteral("depotEditView"), editId); });
        }
        break;
    }
}

void DepotViewCommand::handleCancelled()
{
    m_done = true;
    Q_EMIT finished(CommandResult::Success("Depot view cancelled"));
}

void DepotViewCommand::cleanup()
{
    core::EventBus* bus = core::Application::instance()->eventBus();
    bus->unsubscribeAll(this);
    QVariantMap req;
    req["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", req);
    m_stage = 0;
    m_done = false;
}

REGISTER_COMMAND("depotview", DepotViewCommand);

} // namespace command
} // namespace aicad
