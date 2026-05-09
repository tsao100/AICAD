// src/cad/commands/GripMoveCommand.h
#pragma once
#include <QUndoCommand>
#include <gp_Pnt.hxx>
#include "cad/grips/GripProvider.h"

namespace aicad::cad {

class GripMoveCommand : public QUndoCommand
{
public:
    GripMoveCommand(std::function<void(const gp_Pnt&)> applyFn,
                    const QString& gripId,
                    const gp_Pnt&  before,
                    const gp_Pnt&  after,
                    IGripProvider* provider,      // 用於 End 通知
                    QUndoCommand*  parent = nullptr)
        : QUndoCommand(QString("Move grip %1").arg(gripId), parent)
        , m_apply(std::move(applyFn))
        , m_provider(provider)
        , m_gripId(gripId)
        , m_before(before)
        , m_after(after)
    {}

    void undo() override {
        m_apply(m_before);
        if (m_provider)
            m_provider->onGripDragEnd(m_gripId, m_after, m_before);
    }

    void redo() override {
        m_apply(m_after);
        if (m_provider)
            m_provider->onGripDragEnd(m_gripId, m_before, m_after);
    }

private:
    std::function<void(const gp_Pnt&)> m_apply;
    IGripProvider* m_provider;
    QString        m_gripId;
    gp_Pnt         m_before, m_after;
};

} // namespace aicad::cad
