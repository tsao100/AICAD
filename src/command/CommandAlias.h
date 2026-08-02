#ifndef COMMANDALIAS_H
#define COMMANDALIAS_H

#include <QObject>
#include <QMap>
#include <QString>

namespace aicad {
namespace command {

struct AliasDefinition {
    QString shortcut;
    QString fullCommand;
    QString description;
    bool isSystem;

    AliasDefinition()
        : isSystem(false) {}

    AliasDefinition(const QString& s, const QString& f, const QString& d = QString(), bool sys = false)
        : shortcut(s)
        , fullCommand(f)
        , description(d)
        , isSystem(sys) {}
};

class CommandAlias : public QObject {
    Q_OBJECT

public:
    static CommandAlias* instance();

    // 別名管理
    void registerAlias(const QString& alias, const QString& command,
                       const QString& description = QString(), bool ok=true);
    void unregisterAlias(const QString& alias);
    QString resolveAlias(const QString& input) const;
    bool hasAlias(const QString& alias) const;

    // 檔案操作
    bool loadFromFile(const QString& filePath);
    bool saveToFile(const QString& filePath);
    void loadDefaults();

    // 查詢
    QStringList allAliases() const;
    QStringList allCommands() const;
    AliasDefinition getDefinition(const QString& alias) const;
    QStringList getAliasesForCommand(const QString& command) const;

signals:
    void aliasAdded(const QString& alias, const QString& command);
    void aliasRemoved(const QString& alias);
    void aliasesReloaded();

private:
    explicit CommandAlias(QObject* parent = nullptr);
    ~CommandAlias() override;

    void registerSystemAliases();

    static CommandAlias* s_instance;
    QMap<QString, AliasDefinition> m_aliases;
};

} // namespace command
} // namespace aicad

#endif
