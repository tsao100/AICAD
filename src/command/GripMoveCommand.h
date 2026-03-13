// src/cad/commands/GripMoveCommand.h
#pragma once
#include <QUndoCommand>
#include <gp_Pnt.hxx>
#include "cad/grips/GripProvider.h"

namespace aicad::cad {

class GripMoveCommand : public QUndoCommand
{
public:
    GripMoveCommand(IGripProvider* provider,
                    const QString& gripId,
                    const gp_Pnt&  before,
                    const gp_Pnt&  after,
                    QUndoCommand*  parent = nullptr)
        : QUndoCommand(QString("Move grip %1").arg(gripId), parent)
        , m_provider(provider)
        , m_gripId(gripId)
        , m_before(before)
        , m_after(after)
    {}

    void undo() override {
        m_provider->onGripDrag(m_gripId, m_before);
    }

    void redo() override {
        m_provider->onGripDrag(m_gripId, m_after);
    }

private:
    IGripProvider* m_provider;
    QString        m_gripId;
    gp_Pnt         m_before, m_after;
};

} // namespace aicad::cad
