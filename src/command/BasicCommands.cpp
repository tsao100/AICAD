//src/command/BasicCommands.cpp

/**
 * @file BasicCommands.cpp
 * @brief 實作 menu.txt 中定義的基本命令
 */

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "command/CommandTypes.h"
#include "cad/Document.h"
#include "cad/PlaneManager.h"
#include "cad/Sketch.h"
#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "core/DocumentManager.h"
#include "core/EventBus.h"
#include "ui/UIManager.h"
#include "view/CadView.h"
#include "railway/AlignmentDocument.h"
#include <QFileDialog>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>

namespace aicad {
namespace command {

using namespace core;
using namespace command;
using namespace view;

// ===========================================
// Sketch 命令
// ===========================================

class SketchCommand : public Command {
public:
    SketchCommand()
        : Command("sketch", "Create Sketch")
        , m_waitingForPlane(false)
        , m_selectedPlane(nullptr)
    {
        // 在建構子中取得平面
        //cad::PlaneManager* manager = cad::PlaneManager::instance();
        //m_selectedPlane = manager->activePlane();

        // 如果沒有活動平面，建立 XY 平面
       // if (!m_selectedPlane) {
       //     m_selectedPlane = manager->createPlane(cad::Plane::Type::XY, "XY");
       // }

    }

    CommandResult execute(const CommandContext& context) override {
        qDebug() << "[SketchCommand] Executing...";
        setState(CommandState::Running);

        core::Application* app = core::Application::instance();
        core::DocumentManager* docMgr = app->documentManager();
        core::EventBus* bus = app->eventBus();

        if (!bus) {
            return CommandResult::Failure("EventBus not available");
        }

        cad::Document* doc = docMgr->currentDocument();
        if (!doc) {
            if (bus) bus->publish("command.error", "No active document");
            return CommandResult::Failure("No active document");
        }

        cad::PlaneManager* manager = cad::PlaneManager::instance();

        // 如果有參數，直接使用
        if (!context.args.isEmpty()) {
            QString planeStr = context.args[0].toUpper();

            if (planeStr == "XY") {
                m_selectedPlane = manager->createPlane(cad::Plane::Type::XY, "XY");
            } else if (planeStr == "XZ") {
                m_selectedPlane = manager->createPlane(cad::Plane::Type::XZ, "XZ");
            } else if (planeStr == "YZ") {
                m_selectedPlane = manager->createPlane(cad::Plane::Type::YZ, "YZ");
            } else {
                if (bus) bus->publish("command.error", "Invalid plane. Use XY, XZ, or YZ.");
                return CommandResult::Failure("Invalid plane");
            }

            return createSketchOnPlane(doc, bus);
        }

        // ✅ 互動模式：請求在視圖中選取平面
        m_waitingForPlane = true;

        doc->showAllReferenceGeometry();
        ui::UIManager* uiMgr = app->uiManager();
        view::CadView* cadView = uiMgr->cadView();

        // ✅ Step 1: FitAll first
        cadView->fitAll();

        if (bus) {
            QVariantMap requestData;
            requestData["commandId"] = "sketch";
            requestData["selectionMode"] = "plane";  // 指定選取模式

            // 發布請求事件
            bus->publish("command.request-plane-selection", requestData);

            // ✅ 訂閱回應事件（一次性訂閱）
            bus->subscribe("plane.selected", this,
                           [this, doc, bus](const QVariant& data) {
                               if (!m_waitingForPlane) return;

                               onPlaneSelected(data, doc, bus);

                               // 取消訂閱
                               bus->unsubscribe("plane.selected", this);
                           });
        }

        return CommandResult::Success("Click on a plane to select...");
    }

private:
    void onPlaneSelected(const QVariant& data, cad::Document* doc, core::EventBus* bus) {
        m_waitingForPlane = false;

        QVariantMap planeData = data.toMap();

        // 檢查是否取消
        if (planeData["cancelled"].toBool()) {
            if (bus) bus->publish("command.message", "Sketch creation cancelled");
            return;
        }

        QString planeStr = planeData["plane"].toString().toUpper();

        qDebug() << "[SketchCommand] Plane selected:" << planeStr;

        cad::PlaneManager* manager = cad::PlaneManager::instance();

        if (planeStr == "XY") {
            m_selectedPlane = manager->xyPlane();
        } else if (planeStr == "XZ") {
            m_selectedPlane = manager->xzPlane();
        } else if (planeStr == "YZ") {
            m_selectedPlane = manager->yzPlane();
        } else {
            if (bus) bus->publish("command.error", "Invalid plane selected");
            return;
        }
        if (m_selectedPlane) manager->setActivePlane(m_selectedPlane);

        doc->hideAllReferenceGeometry();

        createSketchOnPlane(doc, bus);
    }

    CommandResult createSketchOnPlane(cad::Document* doc, core::EventBus* bus) {
        QString name = QString("Sketch %1").arg(doc->featureCount() + 1);
        cad::Sketch* sketch = doc->createSketch(m_selectedPlane, name);

        if (!sketch) {
            if (bus) bus->publish("command.error", "Failed to create sketch");
            return CommandResult::Failure("Failed to create sketch");
        }

        Application* app = Application::instance();
        app->setActiveSketch(sketch);

        qDebug() << "[SketchCommand] Sketch created:" << name
                 << "on" << m_selectedPlane->name();

        bus->publish(Events::COMMAND_PROMPT,"Sketch created on " +
                                              m_selectedPlane->name());

        // 發布事件通知其他模組
        if (bus) {
            bus->publish(core::Events::FEATURE_CREATED, sketch->name());

            QVariantMap sketchData;
            sketchData["plane"] = m_selectedPlane->name();
            sketchData["sketchId"] = sketch->id();
            sketchData["sketchName"] = name;
            sketchData["sketch"]     = QVariant::fromValue(sketch);  // ✅ 攜帶指標
            bus->publish("sketch.created", sketchData);

            bus->publish("command.message",
                         QString("Sketch '%1' created on %2 plane")
                             .arg(name).arg(m_selectedPlane->name()));
        }

        // ✅ Just emit finished
        Q_EMIT finished(CommandResult::Success(
            QString("Sketch '%1' created").arg(name)));

        return CommandResult::Success(
            QString("Sketch '%1' created").arg(name));
    }

    QString getUsage() const override {
        return "Usage: SKETCH [XY|XZ|YZ]";
    }

private:
    bool m_waitingForPlane;
    cad::Plane* m_selectedPlane;
};

REGISTER_COMMAND("sketch", SketchCommand);

// ===========================================
// New 命令  — create a clean empty document
// ===========================================
class NewCommand : public Command {
public:
    NewCommand() : Command("new", "New Document") {}

    CommandResult execute(const CommandContext& context) override {
        Application*     app    = Application::instance();
        DocumentManager* docMgr = app->documentManager();
        EventBus*        bus    = app->eventBus();

        // ── 1. Confirm if there are unsaved changes ───────────────────
        if (docMgr->hasUnsavedChanges()) {
            // Publish a yes/no prompt and wait for the user's answer.
            // We use the CommandLineManager's YESNO mechanism so it stays
            // consistent with the rest of the command-line workflow.
            core::CommandLineManager* clm = app->commandLineManager();
            if (clm) {
                clm->showPrompt("Unsaved changes will be lost. Continue? [Yes/No] <No>:");
                clm->waitForInput(core::InputType::YesNo);

                // Subscribe once to the answer
                bus->subscribe(core::Events::YESNO_INPUT, this,
                    [this, app, docMgr, bus, clm](const QVariant& answer) {
                        bus->unsubscribe(core::Events::YESNO_INPUT, this);

                        QString a = answer.toString().trimmed().toLower();
                        bool accepted = (a == "y" || a == "yes");

                        if (!accepted) {
                            clm->printMessage("New document cancelled.");
                            Q_EMIT finished(CommandResult::Failure("Cancelled"));
                            return;
                        }
                        // Proceed with creating a new document
                        createNew(app, docMgr, bus, clm);
                    });

                return CommandResult::Success("Waiting for confirmation...");
            }
        }

        // ── No unsaved changes — proceed immediately ──────────────────
        createNew(app, docMgr, bus, app->commandLineManager());
        return CommandResult::Success("New document created");
    }

    QString getUsage() const override {
        return "Usage: new";
    }

private:
    /// Close the current document (if any), create a fresh one, and
    /// reset the railway alignment data so the canvas is completely clean.
    void createNew(Application*     app,
                   DocumentManager* docMgr,
                   EventBus*        bus,
                   core::CommandLineManager* clm)
    {
        // 1. Cancel any active sketch / command
        app->setActiveSketch(nullptr);

        // 2. Close current document
        cad::Document* current = docMgr->currentDocument();
        if (current) {
            docMgr->closeDocument(current, /*force=*/true);
        }

        // 3. Create a fresh, empty document
        cad::Document* newDoc = docMgr->createDocument("Untitled");
        if (!newDoc) {
            if (clm) clm->printError("Failed to create new document.");
            Q_EMIT finished(CommandResult::Failure("Failed to create document"));
            return;
        }

        // 4. Reset railway alignment — load an empty JSON object so all
        //    PI/VIP lists and solved results are cleared.
        ui::UIManager* uiMgr = app->uiManager();
        if (uiMgr) {
            railway::AlignmentDocument* alignDoc = uiMgr->alignmentDocument();
            if (alignDoc) {
                alignDoc->fromJson(QJsonObject{});
                // Also clear the stored passthrough on the document itself
                newDoc->setAlignmentData(QJsonObject{});
            }

            // 5. Reset view to isometric and fit-all so the user sees a
            //    clean workspace with the origin/reference geometry visible.
            //    Note: DOCUMENT_CREATED event (subscribed in UIManager) has
            //    already called cadView->setDocument(newDoc) and
            //    onDocumentCreated() which sets Isometric + fitAll.
            //    We issue an additional fitAll after a slightly longer delay
            //    to ensure all reference geometry is fully displayed.
            view::CadView* cadView = uiMgr->cadView();
            if (cadView) {
                QTimer::singleShot(250, [cadView]() {
                    if (cadView) {
                        cadView->setViewType(view::ViewType::Isometric);
                        cadView->fitAll();
                    }
                });
            }
        }

        // 6. Notify the rest of the system
        if (clm) clm->printMessage("New document ready. Use SKETCH, LINE, HALINE, or other commands to begin.");

        Q_EMIT finished(CommandResult::Success("New document created"));
    }
};

REGISTER_COMMAND("new", NewCommand);

// ===========================================
// Save 命令
// ===========================================
class SaveCommand : public Command {
public:
    SaveCommand() : Command("save", "Save Document") {}
    
    CommandResult execute(const CommandContext& context) override {
        Application* app = Application::instance();
        DocumentManager* docMgr = app->documentManager();
        
        cad::Document* doc = docMgr->currentDocument();
        if (!doc) {
            return CommandResult::Failure("No active document");
        }
        
        QString fileName = doc->fileName();
        if (fileName.isEmpty()|| fileName == "Untitled") {
            QSettings settings("AICAD", "AICAD");
            QString lastDir = settings.value("file/lastDirectory").toString();
            // 沒有檔名，需要使用者選擇
            fileName = QFileDialog::getSaveFileName(
                nullptr,
                "Save Document",
                lastDir,
                "AICAD Files (*.aicad);;All Files (*)"
            );
            
            if (fileName.isEmpty()) {
                return CommandResult::Failure("Save cancelled");
            }
        }

        // Inject viewState and alignment data immediately before save —
        // guaranteed same doc pointer that save() writes to.
        if (context.cadView)
            doc->setViewState(context.cadView->saveViewState());
        if (context.alignmentDoc)
            doc->setAlignmentData(context.alignmentDoc->toJson());

        if (doc->save(fileName)) {
            QSettings settings("AICAD", "AICAD");
            settings.setValue("file/lastDirectory", QFileInfo(fileName).absolutePath());
            return CommandResult::Success("Document saved");
        } else {
            return CommandResult::Failure("Failed to save document");
        }
    }
    
    QString getUsage() const override {
        return "Usage: save";
    }
};

REGISTER_COMMAND("save", SaveCommand);

// ===========================================
// SaveAs 命令
// ===========================================
class SaveAsCommand : public Command {
public:
    SaveAsCommand() : Command("saveas", "Save Document As") {}

    CommandResult execute(const CommandContext& context) override {
        Application* app = Application::instance();
        DocumentManager* docMgr = app->documentManager();

        cad::Document* doc = docMgr->currentDocument();
        if (!doc) {
            return CommandResult::Failure("No active document");
        }

        // Always prompt — that's the point of Save As
        QSettings settings("AICAD", "AICAD");
        QString initialPath = doc->fileName().isEmpty()
                                  ? settings.value("file/lastDirectory").toString()
                                  : doc->fileName();

        QString fileName = QFileDialog::getSaveFileName(
            nullptr,
            "Save Document As",
            initialPath,
            "AICAD Files (*.aicad);;All Files (*)"
            );

        if (fileName.isEmpty()) {
            return CommandResult::Failure("Save As cancelled");
        }

        if (context.cadView)
            doc->setViewState(context.cadView->saveViewState());
        if (context.alignmentDoc)
            doc->setAlignmentData(context.alignmentDoc->toJson());

        if (doc->save(fileName)) {
            return CommandResult::Success("Document saved as: " + QFileInfo(fileName).fileName());
        } else {
            return CommandResult::Failure("Failed to save document");
        }
    }

    QString getUsage() const override {
        return "Usage: saveas";
    }
};

REGISTER_COMMAND("saveas", SaveAsCommand);

// ===========================================
// Load 命令
// ===========================================
class LoadCommand : public Command {
public:
    LoadCommand() : Command("load", "Load Document") {}
    
    CommandResult execute(const CommandContext& context) override {
        Application* app = Application::instance();
        DocumentManager* docMgr = app->documentManager();

        QSettings settings("AICAD", "AICAD");
        QString lastDir = settings.value("file/lastDirectory").toString();

        QString fileName = QFileDialog::getOpenFileName(
            nullptr,
            "Open Document",
            lastDir,
            "AICAD Files (*.aicad);;All Files (*)"
        );
        
        if (fileName.isEmpty()) {
            return CommandResult::Failure("Load cancelled");
        }

        settings.setValue("file/lastDirectory", QFileInfo(fileName).absolutePath());
        
        cad::Document* doc = docMgr->openDocument(fileName);
        if (doc) {
            return CommandResult::Success("Document loaded");
        } else {
            return CommandResult::Failure("Failed to load document");
        }
    }
    
    QString getUsage() const override {
        return "Usage: load";
    }
};

REGISTER_COMMAND("load", LoadCommand);

// ===========================================
// View 命令
// ===========================================
class ViewTopCommand : public Command {
public:
    ViewTopCommand() : Command("view_top", "Top View") {}
    
    CommandResult execute(const CommandContext& context) override {
        Application* app = Application::instance();
        ui::UIManager* uiMgr = app->uiManager();
        view::CadView* cadView = uiMgr->cadView();
        
        if (cadView) {
            cadView->setTopView();
            return CommandResult::Success("Top view");
        }
        
        return CommandResult::Failure("No view available");
    }
};

REGISTER_COMMAND("view_top", ViewTopCommand);

class ViewFrontCommand : public Command {
public:
    ViewFrontCommand() : Command("view_front", "Front View") {}
    
    CommandResult execute(const CommandContext& context) override {
        Application* app = Application::instance();
        ui::UIManager* uiMgr = app->uiManager();
        view::CadView* cadView = uiMgr->cadView();
        
        if (cadView) {
            cadView->setFrontView();
            return CommandResult::Success("Front view");
        }
        
        return CommandResult::Failure("No view available");
    }
};

REGISTER_COMMAND("view_front", ViewFrontCommand);

class ViewRightCommand : public Command {
public:
    ViewRightCommand() : Command("view_right", "Right View") {}
    
    CommandResult execute(const CommandContext& context) override {
        Application* app = Application::instance();
        ui::UIManager* uiMgr = app->uiManager();
        view::CadView* cadView = uiMgr->cadView();
        
        if (cadView) {
            cadView->setRightView();
            return CommandResult::Success("Right view");
        }
        
        return CommandResult::Failure("No view available");
    }
};

REGISTER_COMMAND("view_right", ViewRightCommand);

class ViewIsoCommand : public Command {
public:
    ViewIsoCommand() : Command("view_iso", "Isometric View") {}
    
    CommandResult execute(const CommandContext& context) override {
        Application* app = Application::instance();
        ui::UIManager* uiMgr = app->uiManager();
        view::CadView* cadView = uiMgr->cadView();
        
        if (cadView) {
            cadView->setIsometricView();
            return CommandResult::Success("Isometric view");
        }
        
        return CommandResult::Failure("No view available");
    }
};

REGISTER_COMMAND("view_iso", ViewIsoCommand);

} // namespace commands
} // namespace aicad