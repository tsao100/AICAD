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

    // ✅ Task H: 為約束命令補充參數語法提示（顯示在 description 欄後面）
    static const QHash<QString, QString> kArgHints = {
        // 幾何約束
        {QStringLiteral("COINCIDENT"),    QStringLiteral(" — [geom1] [geom2]")},
        {QStringLiteral("HORIZONTAL"),    QStringLiteral(" — [lineUuid]")},
        {QStringLiteral("VERTICAL"),      QStringLiteral(" — [lineUuid]")},
        {QStringLiteral("PARALLEL"),      QStringLiteral(" — [uuid1] [uuid2]")},
        {QStringLiteral("PERPENDICULAR"), QStringLiteral(" — [uuid1] [uuid2]")},
        {QStringLiteral("TANGENT"),       QStringLiteral(" — [uuid1] [uuid2]")},
        {QStringLiteral("CONCENTRIC"),    QStringLiteral(" — [uuid1] [uuid2]")},
        {QStringLiteral("EQUALLEN"),      QStringLiteral(" — [uuid1] [uuid2]")},
        {QStringLiteral("EQUALRAD"),      QStringLiteral(" — [uuid1] [uuid2]")},
        {QStringLiteral("COLLINEAR"),     QStringLiteral(" — [uuid1] [uuid2]")},
        {QStringLiteral("MIDPOINT"),      QStringLiteral(" — [pointUuid] [lineUuid]")},
        {QStringLiteral("POINTONCURVE"),  QStringLiteral(" — [pointUuid] [curveUuid]")},
        {QStringLiteral("SYMMETRIC"),     QStringLiteral(" — [uuid1] [uuid2] [axisUuid]")},
        {QStringLiteral("FIX"),           QStringLiteral(" — [uuid]")},
        // 尺寸約束
        {QStringLiteral("DIST"),          QStringLiteral(" — [value|expr]  → 選兩點")},
        {QStringLiteral("RAD"),           QStringLiteral(" — [value|expr]  → 選圓/弧")},
        {QStringLiteral("ANGLE"),         QStringLiteral(" — [value|expr]  → 選兩線")},
        {QStringLiteral("FIXX"),          QStringLiteral(" — [value|expr]  → 選點")},
        {QStringLiteral("FIXY"),          QStringLiteral(" — [value|expr]  → 選點")},
        // 管理
        {QStringLiteral("DELCON"),        QStringLiteral(" — [uuid]")},
        {QStringLiteral("EDITCON"),       QStringLiteral(" — [uuid] [newExpr]")},
        {QStringLiteral("LISTCON"),       QStringLiteral("")},
        {QStringLiteral("CONINFO"),       QStringLiteral(" — [uuid]")},
        {QStringLiteral("SOLVE"),         QStringLiteral("")},
        {QStringLiteral("DOF"),           QStringLiteral("")},
        {QStringLiteral("CONVIS"),        QStringLiteral(" — [ON|OFF|type]")},
        {QStringLiteral("LISTPOINTS"),    QStringLiteral("")},
    };

    beginResetModel();
    m_allCommands.clear();

    for (const QString& cmd : commands) {
        QStringList aliases = alias->getAliasesForCommand(cmd);
        QString aliasStr = aliases.isEmpty() ? QString() : aliases.first();

        command::AliasDefinition def = alias->getDefinition(aliasStr);

        CommandInfo info;
        info.name  = cmd;
        info.alias = aliasStr;
        // 附加參數語法提示
        info.description = def.description + kArgHints.value(cmd.toUpper());

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
            info.category = QStringLiteral("Sketch Constraint");
        else if (sketchDimCmds.contains(cmd.toUpper()))
            info.category = QStringLiteral("Sketch Dimension");
        else if (sketchMgmtCmds.contains(cmd.toUpper()))
            info.category = QStringLiteral("Sketch Management");
        else
            info.category = QStringLiteral("General");

        m_allCommands.append(info);
    }

    m_filteredCommands = m_allCommands;

    endResetModel();
}

} // namespace ui
} // namespace aicad
