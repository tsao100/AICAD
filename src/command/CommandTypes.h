/**
 * @file CommandTypes.h
 * @brief 命令系統的基礎類型定義
 * @author Jack
 * @date 2024-12-04
 */

#ifndef AICAD_CORE_COMMANDTYPES_H
#define AICAD_CORE_COMMANDTYPES_H

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QMap>

namespace aicad { namespace ui { class UIManager; } }

// Forward declarations for railway / UI pointers
namespace aicad {
namespace railway { class AlignmentDocument; }
namespace ui      { class VAlignProfileView; }
namespace view    { class CadView; }
}

namespace aicad {
namespace command {

/**
 * @brief 命令參數結構
 */
struct CommandContext {
    QStringList args;
    QObject* sender;
    QVariantMap data;
    bool interactive;

    /// Railway alignment document (set by UIManager before execute())
    aicad::railway::AlignmentDocument* alignmentDoc = nullptr;

    /// UIManager — gives commands access to per-TCL alignment docs
    aicad::ui::UIManager* uiManager = nullptr;

    /// Vertical alignment profile view (set by UIManager before execute())
    aicad::ui::VAlignProfileView* profileView = nullptr;

    /// 3-D viewport — gives commands access to RubberBand, snap, etc.
    aicad::view::CadView* cadView = nullptr;

    CommandContext()
        : sender(nullptr)
        , interactive(false)
    {}
};

/**
 * @brief 命令執行結果
 */
struct CommandResult {
    bool success;
    QString message;
    QVariant data;

    CommandResult(bool s = true, const QString& msg = QString())
        : success(s), message(msg) {}

    static CommandResult Success(const QString& msg = QString()) {
        return CommandResult(true, msg);
    }

    static CommandResult Failure(const QString& msg) {
        return CommandResult(false, msg);
    }
};

} // namespace core
} // namespace aicad

#endif // AICAD_CORE_COMMANDTYPES_H
