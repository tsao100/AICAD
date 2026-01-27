//src/command/BasicCommands.cpp

/**
 * @file BasicCommands.cpp
 * @brief 實作 menu.txt 中定義的基本命令
 */

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "command/CommandTypes.h"
#include "cad/Document.h"
#include "cad/Plane.h"
#include "cad/Sketch.h"
#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/EventBus.h"
#include "ui/UIManager.h"
#include "view/CadView.h"
#include "view/RubberBand.h"
#include <QFileDialog>

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
        , m_selectedPlane(cad::Plane::xy())
    {}

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

        // 如果有參數，直接使用
        if (!context.args.isEmpty()) {
            QString planeStr = context.args[0].toUpper();

            if (planeStr == "XY") {
                m_selectedPlane = cad::Plane::xy();
            } else if (planeStr == "XZ") {
                m_selectedPlane = cad::Plane::xz();
            } else if (planeStr == "YZ") {
                m_selectedPlane = cad::Plane::yz();
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

        QString planeName = planeData["plane"].toString().toUpper();

        qDebug() << "[SketchCommand] Plane selected:" << planeName;

        if (planeName == "XY") {
            m_selectedPlane = cad::Plane::xy();
        } else if (planeName == "XZ") {
            m_selectedPlane = cad::Plane::xz();
        } else if (planeName == "YZ") {
            m_selectedPlane = cad::Plane::yz();
        } else {
            if (bus) bus->publish("command.error", "Invalid plane selected");
            return;
        }

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
                 << "on" << m_selectedPlane.displayName();

        // 發布事件通知其他模組
        if (bus) {
            bus->publish(core::Events::FEATURE_CREATED, sketch->name());

            QVariantMap sketchData;
            sketchData["plane"] = m_selectedPlane.displayName();
            sketchData["sketchId"] = sketch->id();
            sketchData["sketchName"] = name;
            bus->publish("sketch.created", sketchData);

            bus->publish("command.message",
                         QString("Sketch '%1' created on %2 plane")
                             .arg(name).arg(m_selectedPlane.displayName()));
        }

        return CommandResult::Success(
            QString("Sketch '%1' created").arg(name));
    }

    QString getUsage() const override {
        return "Usage: SKETCH [XY|XZ|YZ]";
    }

private:
    bool m_waitingForPlane;
    cad::Plane m_selectedPlane;
};

REGISTER_COMMAND("sketch", SketchCommand);

// ===========================================
// Rectangle 命令
// ===========================================
class RectCommand : public Command {
public:
    RectCommand() : Command("rect", "Draw Rectangle") {}
    
    CommandResult execute(const CommandContext& context) override {
        if (context.args.size() < 4) {
            return CommandResult::Failure("Need 4 arguments: x1 y1 x2 y2");
        }
        
        bool ok;
        double x1 = context.args[0].toDouble(&ok);
        if (!ok) return CommandResult::Failure("Invalid x1");
        
        double y1 = context.args[1].toDouble(&ok);
        if (!ok) return CommandResult::Failure("Invalid y1");
        
        double x2 = context.args[2].toDouble(&ok);
        if (!ok) return CommandResult::Failure("Invalid x2");
        
        double y2 = context.args[3].toDouble(&ok);
        if (!ok) return CommandResult::Failure("Invalid y2");
        
        // TODO: 實際繪製矩形
        outputMessage(QString("Drawing rectangle from (%1,%2) to (%3,%4)")
                     .arg(x1).arg(y1).arg(x2).arg(y2));
        
        return CommandResult::Success("Rectangle created");
    }
    
    QString getUsage() const override {
        return "Usage: rect x1 y1 x2 y2";
    }
};

REGISTER_COMMAND("rect", RectCommand);

// ===========================================
// Circle 命令
// ===========================================
class CircleCommand : public Command {
public:
    CircleCommand() : Command("circle", "Draw Circle") {}
    
    CommandResult execute(const CommandContext& context) override {
        if (context.args.size() < 3) {
            return CommandResult::Failure("Need 3 arguments: x y radius");
        }
        
        bool ok;
        double x = context.args[0].toDouble(&ok);
        if (!ok) return CommandResult::Failure("Invalid x");
        
        double y = context.args[1].toDouble(&ok);
        if (!ok) return CommandResult::Failure("Invalid y");
        
        double r = context.args[2].toDouble(&ok);
        if (!ok) return CommandResult::Failure("Invalid radius");
        
        if (r <= 0) {
            return CommandResult::Failure("Radius must be positive");
        }
        
        outputMessage(QString("Drawing circle at (%1,%2) with radius %3")
                     .arg(x).arg(y).arg(r));
        
        return CommandResult::Success("Circle created");
    }
    
    QString getUsage() const override {
        return "Usage: circle x y radius";
    }
};

REGISTER_COMMAND("circle", CircleCommand);

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
        if (fileName.isEmpty()) {
            // 沒有檔名，需要使用者選擇
            fileName = QFileDialog::getSaveFileName(
                nullptr,
                "Save Document",
                "",
                "AICAD Files (*.aicad);;All Files (*)"
            );
            
            if (fileName.isEmpty()) {
                return CommandResult::Failure("Save cancelled");
            }
        }
        
        if (doc->save(fileName)) {
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
// Load 命令
// ===========================================
class LoadCommand : public Command {
public:
    LoadCommand() : Command("load", "Load Document") {}
    
    CommandResult execute(const CommandContext& context) override {
        Application* app = Application::instance();
        DocumentManager* docMgr = app->documentManager();
        
        QString fileName = QFileDialog::getOpenFileName(
            nullptr,
            "Open Document",
            "",
            "AICAD Files (*.aicad);;All Files (*)"
        );
        
        if (fileName.isEmpty()) {
            return CommandResult::Failure("Load cancelled");
        }
        
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
