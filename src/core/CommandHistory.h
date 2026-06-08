#ifndef COMMANDHISTORY_H
#define COMMANDHISTORY_H

#include <QObject>
#include <QStringList>
#include <QDateTime>

namespace aicad {
namespace core {

struct HistoryEntry {
    QString command;
    QDateTime timestamp;
    bool successful;
    QString result;

    HistoryEntry()
        : successful(true) {}

    HistoryEntry(const QString& cmd, bool success = true, const QString& res = QString())
        : command(cmd)
        , timestamp(QDateTime::currentDateTime())
        , successful(success)
        , result(res) {}
};

class CommandHistory : public QObject {
    Q_OBJECT

public:
    explicit CommandHistory(QObject* parent = nullptr);
    ~CommandHistory() override;

    // 添加記錄
    void addEntry(const QString& command, bool successful = true, const QString& result = QString());

    // 查詢
    QStringList commands() const;
    QList<HistoryEntry> entries() const { return m_entries; }
    int count() const { return m_entries.size(); }
    bool isEmpty() const { return m_entries.isEmpty(); }

    HistoryEntry at(int index) const;
    QString commandAt(int index) const;

    // 搜尋
    QList<HistoryEntry> search(const QString& keyword) const;
    QList<HistoryEntry> filterByDate(const QDateTime& from, const QDateTime& to) const;

    // 清除
    void clear();
    void clearBefore(const QDateTime& date);

    // 檔案操作
    bool saveToFile(const QString& filePath) const;
    bool loadFromFile(const QString& filePath);

    // 設定
    void setMaxEntries(int max);
    int maxEntries() const { return m_maxEntries; }

signals:
    void entryAdded(const HistoryEntry& entry);
    void historyCleared();

private:
    void trimToMaxSize();

    QList<HistoryEntry> m_entries;
    int m_maxEntries;

    static constexpr int DEFAULT_MAX_ENTRIES = 1000;
};

} // namespace core
} // namespace aicad

#endif
