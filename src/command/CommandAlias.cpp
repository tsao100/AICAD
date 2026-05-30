#include "CommandAlias.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

namespace aicad {
namespace command {

CommandAlias* CommandAlias::s_instance = nullptr;

CommandAlias* CommandAlias::instance() {
    if (!s_instance) {
        s_instance = new CommandAlias();
    }
    return s_instance;
}

CommandAlias::CommandAlias(QObject* parent)
    : QObject(parent)
{
    loadDefaults();
    qDebug() << "[CommandAlias] Created with" << m_aliases.size() << "aliases";
}

CommandAlias::~CommandAlias() = default;

void CommandAlias::loadDefaults() {
    registerSystemAliases();
    emit aliasesReloaded();
}

void CommandAlias::registerSystemAliases() {
    // Drawing Commands
    registerAlias("L", "LINE", "Draw a line", true);
    registerAlias("C", "CIRCLE", "Draw a circle", true);
    registerAlias("A", "ARC", "Draw an arc", true);
    registerAlias("PL", "POLYLINE", "Draw a polyline", true);
    registerAlias("POL", "POLYGON", "Draw a polygon", true);
    registerAlias("REC", "RECT", "Draw a rectangle", true);
    registerAlias("EL", "ELLIPSE", "Draw an ellipse", true);
    registerAlias("SPL", "SPLINE", "Draw a spline", true);

    // Modify Commands
    registerAlias("E", "ERASE", "Erase objects", true);
    registerAlias("M", "MOVE", "Move objects", true);
    registerAlias("CO", "COPY", "Copy objects", true);
    registerAlias("RO", "ROTATE", "Rotate objects", true);
    registerAlias("MI", "MIRROR", "Mirror objects", true);
    registerAlias("SC", "SCALE", "Scale objects", true);
    registerAlias("TR", "TRIM", "Trim objects", true);
    registerAlias("EX", "EXTEND", "Extend objects", true);
    registerAlias("F", "FILLET", "Fillet objects", true);
    registerAlias("CHA", "CHAMFER", "Chamfer objects", true);
    registerAlias("O", "OFFSET", "Offset objects", true);
    registerAlias("AR", "ARRAY", "Array objects", true);

    // View Commands
    registerAlias("Z", "ZOOM", "Zoom view", true);
    registerAlias("P", "PAN", "Pan view", true);
    registerAlias("R", "REDRAW", "Redraw view", true);
    registerAlias("RE", "REGEN", "Regenerate view", true);

    // Utility Commands
    registerAlias("U", "UNDO", "Undo last action", true);
    registerAlias("REDO", "REDO", "Redo last undo", true);

    // Sketch Commands (AICAD specific)
    registerAlias("SK",  "SKETCH",  "Create a sketch", true);
    registerAlias("EXT", "EXTRUDE", "Extrude a profile", true);
    registerAlias("REV", "REVOLVE", "Revolve a profile", true);

    // Railway Alignment Commands
    registerAlias("FT",  "ALIGNMENTFIXTANGENT",  "Add Fixed Tangent to horizontal alignment",    true);
    registerAlias("FC",  "ALIGNMENTFIXCURVE",    "Add Fixed Curve (3-point arc) to alignment",   true);
    registerAlias("AFC", "ALIGNMENTFLOATCURVE",  "Add Floating Curve between two tangents",      true);
    registerAlias("SCS", "ALIGNMENTSCS",         "Add SCS (Spiral-Circular-Spiral) curve",       true);
    registerAlias("TCL", "TRACK",                "New Track Centerline",                         true);

    // ── Step 15：縱斷面命令 ──────────────────────────────────────────────────
    registerAlias("VADD",   "VALIGNFLOATVCURVE", "Add VIP with vertical curve (K value)",        true);
    registerAlias("VMOVE",  "VALIGNMOVEPVI",     "Drag VIP in vertical profile",                 true);
    registerAlias("VSETK",  "VALIGNSETK",        "Set K value for selected VIP",                 true);

    // ── Step 19 ───────────────────────────────────────────────────────────────
    registerAlias("PV",  "PROFILEVIEW",      "Show vertical alignment profile view",         true);
    registerAlias("VCG", "VALIGNCHECKGRADE", "Check grades against limit, highlight violations", true);

    // ── Phase 9：草圖約束命令別名 ─────────────────────────────────────────────
    // Geometric
    registerAlias("COI", "COINCIDENT",    "Coincident constraint",             true);
    registerAlias("HOR", "HORIZONTAL",    "Horizontal constraint",             true);
    registerAlias("VER", "VERTICAL",      "Vertical constraint",               true);
    registerAlias("PAR", "PARALLEL",      "Parallel constraint",               true);
    registerAlias("PER", "PERPENDICULAR", "Perpendicular constraint",          true);
    registerAlias("TAN", "TANGENT",       "Tangent constraint",                true);
    registerAlias("CCN", "CONCENTRIC",    "Concentric constraint",             true);
    registerAlias("EQL", "EQUALLEN",      "Equal length constraint",           true);
    registerAlias("EQR", "EQUALRAD",      "Equal radius constraint",           true);
    registerAlias("COL", "COLLINEAR",     "Collinear constraint",              true);
    registerAlias("MID", "MIDPOINT",      "Midpoint constraint",               true);
    registerAlias("SYM", "SYMMETRIC",     "Symmetric constraint",              true);
    registerAlias("POC", "POINTONCURVE",  "Point on curve constraint",         true);
    // Dimensional
    registerAlias("DIM", "DIST",          "Distance dimension constraint",     true);
    registerAlias("ANG", "ANGLE",         "Angle dimension constraint",        true);
    // Management
    registerAlias("DCO", "DELCON",        "Delete constraint",                 true);
    registerAlias("ECO", "EDITCON",       "Edit dimension constraint",         true);
    registerAlias("LSC", "LISTCON",       "List all constraints",              true);
}

void CommandAlias::registerAlias(const QString& alias, const QString& command, const QString& description, bool isSystem) {
    QString upperAlias = alias.toUpper();
    QString upperCommand = command.toUpper();

    AliasDefinition def(upperAlias, upperCommand, description, isSystem);
    m_aliases[upperAlias] = def;

    emit aliasAdded(upperAlias, upperCommand);
}

void CommandAlias::unregisterAlias(const QString& alias) {
    QString upperAlias = alias.toUpper();

    // 不能移除系統別名
    if (m_aliases.contains(upperAlias) && m_aliases[upperAlias].isSystem) {
        qWarning() << "[CommandAlias] Cannot remove system alias:" << upperAlias;
        return;
    }

    if (m_aliases.remove(upperAlias)) {
        emit aliasRemoved(upperAlias);
    }
}

QString CommandAlias::resolveAlias(const QString& input) const {
    QString upperInput = input.trimmed().toUpper();

    if (m_aliases.contains(upperInput)) {
        return m_aliases[upperInput].fullCommand;
    }

    return input;
}

bool CommandAlias::hasAlias(const QString& alias) const {
    return m_aliases.contains(alias.toUpper());
}

bool CommandAlias::loadFromFile(const QString& filePath) {
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[CommandAlias] Cannot open file:" << filePath;
        return false;
    }

    QTextStream in(&file);
    int lineNum = 0;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        lineNum++;

        // 跳過空行和註釋
        if (line.isEmpty() || line.startsWith(';') || line.startsWith('#')) {
            continue;
        }

        // 格式: ALIAS, COMMAND, DESCRIPTION
        QStringList parts = line.split(',');

        if (parts.size() >= 2) {
            QString alias = parts[0].trimmed();
            QString command = parts[1].trimmed();
            QString description = parts.size() >= 3 ? parts[2].trimmed() : QString();

            // 移除 * 前綴（如果有）
            if (command.startsWith('*')) {
                command = command.mid(1);
            }

            registerAlias(alias.toUpper(), command.toUpper(), description, false);
        }
        else {
            qWarning() << "[CommandAlias] Invalid format at line" << lineNum << ":" << line;
        }
    }

    file.close();

    emit aliasesReloaded();

    qDebug() << "[CommandAlias] Loaded" << m_aliases.size() << "aliases from" << filePath;
    return true;
}

bool CommandAlias::saveToFile(const QString& filePath) {
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[CommandAlias] Cannot open file for writing:" << filePath;
        return false;
    }

    QTextStream out(&file);

    // 寫入標題
    out << "; AICAD Command Aliases\n";
    out << "; Format: ALIAS, *COMMAND, DESCRIPTION\n\n";

    // 先寫系統別名
    out << "; System Aliases\n";
    for (auto it = m_aliases.constBegin(); it != m_aliases.constEnd(); ++it) {
        const AliasDefinition& def = it.value();
        if (def.isSystem) {
            out << def.shortcut << ", *" << def.fullCommand;
            if (!def.description.isEmpty()) {
                out << ", " << def.description;
            }
            out << "\n";
        }
    }

    // 再寫用戶別名
    out << "\n; User Aliases\n";
    for (auto it = m_aliases.constBegin(); it != m_aliases.constEnd(); ++it) {
        const AliasDefinition& def = it.value();
        if (!def.isSystem) {
            out << def.shortcut << ", *" << def.fullCommand;
            if (!def.description.isEmpty()) {
                out << ", " << def.description;
            }
            out << "\n";
        }
    }

    file.close();

    qDebug() << "[CommandAlias] Saved" << m_aliases.size() << "aliases to" << filePath;
    return true;
}

QStringList CommandAlias::allAliases() const {
    return m_aliases.keys();
}

QStringList CommandAlias::allCommands() const {
    QStringList commands;

    for (auto it = m_aliases.constBegin(); it != m_aliases.constEnd(); ++it) {
        QString cmd = it.value().fullCommand;
        if (!commands.contains(cmd)) {
            commands.append(cmd);
        }
    }

    return commands;
}

AliasDefinition CommandAlias::getDefinition(const QString& alias) const {
    return m_aliases.value(alias.toUpper());
}

QStringList CommandAlias::getAliasesForCommand(const QString& command) const {
    QStringList aliases;
    QString upperCommand = command.toUpper();

    for (auto it = m_aliases.constBegin(); it != m_aliases.constEnd(); ++it) {
        if (it.value().fullCommand == upperCommand) {
            aliases.append(it.key());
        }
    }

    return aliases;
}

} // namespace command
} // namespace aicad
