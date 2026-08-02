#ifndef AUTOCOMPLETEMODEL_H
#define AUTOCOMPLETEMODEL_H

#include <QAbstractListModel>
#include <QStringList>
#include <QMap>

namespace aicad {
namespace ui {

struct CommandInfo {
    QString name;
    QString alias;
    QString description;
    QString category;
    int usageCount;

    CommandInfo()
        : usageCount(0) {}

    CommandInfo(const QString& n, const QString& a = QString(),
                const QString& d = QString(), const QString& c = QString())
        : name(n)
        , alias(a)
        , description(d)
        , category(c)
        , usageCount(0) {}
};

class AutoCompleteModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        AliasRole,
        DescriptionRole,
        CategoryRole,
        UsageCountRole
    };

    explicit AutoCompleteModel(QObject* parent = nullptr);
    ~AutoCompleteModel() override;

    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 命令管理
    void addCommand(const CommandInfo& info);
    void removeCommand(const QString& name);
    void clear();

    // 過濾
    void setFilter(const QString& filter);
    QString filter() const { return m_filter; }

    // 排序
    void sortByUsage();
    void sortByName();

    // 使用統計
    void incrementUsage(const QString& name);

public slots:
    void updateFromAlias();

private:
    void applyFilter();

    QList<CommandInfo> m_allCommands;
    QList<CommandInfo> m_filteredCommands;
    QString m_filter;
};

} // namespace ui
} // namespace aicad

#endif
