#include "AutoCompleteModel.h"
#include "command/CommandAlias.h"
#include <algorithm>

namespace aicad {
namespace ui {

AutoCompleteModel::AutoCompleteModel(QObject* parent)
    : QAbstractListModel(parent)
{
    // 從 CommandAlias 初始化
    updateFromAlias();
}

AutoCompleteModel::~AutoCompleteModel() = default;

int AutoCompleteModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_filteredCommands.size();
}

QVariant AutoCompleteModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_filteredCommands.size()) {
        return QVariant();
    }

    const CommandInfo& info = m_filteredCommands[index.row()];

    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return info.name;

    case AliasRole:
        return info.alias;

    case DescriptionRole:
        return info.description;

    case CategoryRole:
        return info.category;

    case UsageCountRole:
        return info.usageCount;

    default:
        return QVariant();
    }
}

QHash<int, QByteArray> AutoCompleteModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[AliasRole] = "alias";
    roles[DescriptionRole] = "description";
    roles[CategoryRole] = "category";
    roles[UsageCountRole] = "usageCount";
    return roles;
}

void AutoCompleteModel::addCommand(const CommandInfo& info) {
    m_allCommands.append(info);
    applyFilter();
}

void AutoCompleteModel::removeCommand(const QString& name) {
    for (int i = 0; i < m_allCommands.size(); ++i) {
        if (m_allCommands[i].name == name) {
            m_allCommands.removeAt(i);
            break;
        }
    }
    applyFilter();
}

void AutoCompleteModel::clear() {
    beginResetModel();
    m_allCommands.clear();
    m_filteredCommands.clear();
    endResetModel();
}

void AutoCompleteModel::setFilter(const QString& filter) {
    m_filter = filter.trimmed().toUpper();
    applyFilter();
}

void AutoCompleteModel::applyFilter() {
    beginResetModel();

    m_filteredCommands.clear();

    if (m_filter.isEmpty()) {
        m_filteredCommands = m_allCommands;
    } else {
        for (const CommandInfo& info : m_allCommands) {
            // 匹配命令名稱或別名
            if (info.name.toUpper().startsWith(m_filter) ||
                info.alias.toUpper().startsWith(m_filter)) {
                m_filteredCommands.append(info);
            }
        }
    }

    // 按使用次數排序
    sortByUsage();

    endResetModel();
}

void AutoCompleteModel::sortByUsage() {
    std::sort(m_filteredCommands.begin(), m_filteredCommands.end(),
              [](const CommandInfo& a, const CommandInfo& b) {
                  return a.usageCount > b.usageCount;
              });
}

void AutoCompleteModel::sortByName() {
    std::sort(m_filteredCommands.begin(), m_filteredCommands.end(),
              [](const CommandInfo& a, const CommandInfo& b) {
                  return a.name < b.name;
              });
}

void AutoCompleteModel::incrementUsage(const QString& name) {
    for (CommandInfo& info : m_allCommands) {
        if (info.name == name) {
            info.usageCount++;
            break;
        }
    }

    applyFilter();
}

void AutoCompleteModel::updateFromAlias() {
    auto* alias = command::CommandAlias::instance();

    QStringList commands = alias->allCommands();

    beginResetModel();
    m_allCommands.clear();

    for (const QString& cmd : commands) {
        QStringList aliases = alias->getAliasesForCommand(cmd);
        QString aliasStr = aliases.isEmpty() ? QString() : aliases.first();

        command::AliasDefinition def = alias->getDefinition(aliasStr);

        CommandInfo info;
        info.name = cmd;
        info.alias = aliasStr;
        info.description = def.description;

        // Phase 9：依命令名稱分配類別
        static const QStringList sketchConstraintCmds = {
            "COINCIDENT","HORIZONTAL","VERTICAL","PARALLEL","PERPENDICULAR",
            "TANGENT","CONCENTRIC","EQUALLEN","EQUALRAD","COLLINEAR",
            "MIDPOINT","SYMMETRIC","POINTONCURVE","FIX",
            "COI","HOR","VER","PAR","PER","TAN","CCN","EQL","EQR","COL","MID","SYM","POC"
        };
        static const QStringList sketchDimCmds = {
            "DIST","RAD","ANGLE","FIXX","FIXY","DIM","ANG"
        };
        static const QStringList sketchMgmtCmds = {
            "DELCON","EDITCON","LISTCON","CONINFO","SOLVE","DOF","CONVIS","LISTPOINTS",
            "DCO","ECO","LSC"
        };

        if (sketchConstraintCmds.contains(cmd.toUpper()))
            info.category = "Sketch Constraint";
        else if (sketchDimCmds.contains(cmd.toUpper()))
            info.category = "Sketch Dimension";
        else if (sketchMgmtCmds.contains(cmd.toUpper()))
            info.category = "Sketch Management";
        else
            info.category = "General";

        m_allCommands.append(info);
    }

    m_filteredCommands = m_allCommands;

    endResetModel();
}

} // namespace ui
} // namespace aicad
