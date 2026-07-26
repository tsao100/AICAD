/**
 * @file SketchEditCommand.cpp
 */
#include "command/SketchEditCommand.h"
#include "cad/Sketch.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "ui/UIManager.h"

#include <QUndoStack>
#include <QDebug>

namespace aicad {
namespace command {

// ── Static factory ────────────────────────────────────────────────────────────

void SketchEditCommand::push(cad::Sketch* sketch,
                             const QJsonObject& before,
                             const QJsonObject& after,
                             const QString& text)
{
    if (!sketch) return;

    auto* app = core::Application::instance();
    if (!app) return;
    auto* uiMgr = app->uiManager();
    if (!uiMgr) return;
    QUndoStack* stack = uiMgr->undoStack();
    if (!stack) return;

    stack->push(new SketchEditCommand(sketch, before, after, text));
}

// ── Constructor ───────────────────────────────────────────────────────────────

SketchEditCommand::SketchEditCommand(cad::Sketch* sketch,
                                     const QJsonObject& before,
                                     const QJsonObject& after,
                                     const QString& text,
                                     QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , m_sketch(sketch)
    , m_before(before)
    , m_after(after)
{
}

// ── 共用：還原後通知 CadView 刷新 ───────────────────────────────────────────
//
// Sketch::fromJson() 內部只呼叫 Sketch::rebuild()，不是
// Document::rebuildFeature()——後者才會 emit featureShapeUpdated 讓
// CadView::displayAllFeatures() 觸發（見 AICAD 既有的架構筆記）。
// 因此這裡比照所有其他 Sketch 變更 handler 的既定作法，變更後另外
// publish FEATURE_UPDATED，確保 3D 畫面與 FeatureBrowser 都會刷新。
//
static void notifySketchUpdated(cad::Sketch* sketch)
{
    if (!sketch) return;
    if (auto* bus = core::Application::instance()->eventBus())
        bus->publish(core::Events::FEATURE_UPDATED, sketch->name());
}

// ── undo ──────────────────────────────────────────────────────────────────────

void SketchEditCommand::undo()
{
    if (!m_sketch) return;
    qDebug() << "[SketchEditCommand] undo:" << text();
    m_sketch->fromJson(m_before);
    notifySketchUpdated(m_sketch);
}

// ── redo ──────────────────────────────────────────────────────────────────────

void SketchEditCommand::redo()
{
    if (!m_sketch) return;
    qDebug() << "[SketchEditCommand] redo:" << text();
    m_sketch->fromJson(m_after);
    notifySketchUpdated(m_sketch);
}

} // namespace command
} // namespace aicad
