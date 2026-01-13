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
#include "core/Application.h"
#include "core/DocumentManager.h"
#include "ui/UIManager.h"
#include "view/CadView.h"
#include <QFileDialog>

namespace aicad {
namespace command {

using namespace core;
using namespace command;

// ===========================================
// Sketch 命令
// ===========================================
class SketchCommand : public Command {
public:
    SketchCommand() : Command("sketch", "Create Sketch") {}
    
    CommandResult execute(const CommandContext& context) override {
        Application* app = Application::instance();
        DocumentManager* docMgr = app->documentManager();
        
        cad::Document* doc = docMgr->currentDocument();
        if (!doc) {
            return CommandResult::Failure("No active document");
        }
        
        // 建立草圖在 XY 平面
        cad::Sketch* sketch = doc->createSketch(cad::Plane::xy(), "Sketch");
        if (!sketch) {
            return CommandResult::Failure("Failed to create sketch");
        }
        
        return CommandResult::Success("Sketch created");
    }
    
    QString getUsage() const override {
        return "Usage: sketch";
    }
};

REGISTER_COMMAND("sketch", SketchCommand);

// ===========================================
// Line 命令
// ===========================================
class LineCommand : public Command {
public:
    LineCommand() : Command("line", "Draw Line") {}
    
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
        
        // TODO: 實際繪製直線
        outputMessage(QString("Drawing line from (%1,%2) to (%3,%4)")
                     .arg(x1).arg(y1).arg(x2).arg(y2));
        
        return CommandResult::Success("Line created");
    }
    
    QString getUsage() const override {
        return "Usage: line x1 y1 x2 y2";
    }
};

REGISTER_COMMAND("line", LineCommand);

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
