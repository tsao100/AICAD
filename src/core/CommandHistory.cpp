#include "CommandHistory.h"
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace aicad {
namespace core {

CommandHistory::CommandHistory(QObject* parent)
    : QObject(parent)
    , m_maxEntries(DEFAULT_MAX_ENTRIES)
{
}

CommandHistory::~CommandHistory() = default;

void CommandHistory::addEntry(const QString& command, bool successful, const QString& result) {
    HistoryEntry entry(command, successful, result);
    m_entries.prepend(entry);

    trimToMaxSize();

    emit entryAdded(entry);
}

QStringList CommandHistory::commands() const {
    QStringList cmds;

    for (const HistoryEntry& entry : m_entries) {
        cmds.append(entry.command);
    }

    return cmds;
}

HistoryEntry CommandHistory::at(int index) const {
    if (index >= 0 && index < m_entries.size()) {
        return m_entries[index];
    }
    return HistoryEntry();
}

QString CommandHistory::commandAt(int index) const {
    if (index >= 0 && index < m_entries.size()) {
        return m_entries[index].command;
    }
    return QString();
}

QList<HistoryEntry> CommandHistory::search(const QString& keyword) const {
    QList<HistoryEntry> results;
    QString upperKeyword = keyword.toUpper();

    for (const HistoryEntry& entry : m_entries) {
        if (entry.command.toUpper().contains(upperKeyword)) {
            results.append(entry);
        }
    }

    return results;
}

QList<HistoryEntry> CommandHistory::filterByDate(const QDateTime& from, const QDateTime& to) const {
    QList<HistoryEntry> results;

    for (const HistoryEntry& entry : m_entries) {
        if (entry.timestamp >= from && entry.timestamp <= to) {
            results.append(entry);
        }
    }

    return results;
}

void CommandHistory::clear() {
    m_entries.clear();
    emit historyCleared();
}

void CommandHistory::clearBefore(const QDateTime& date) {
    QList<HistoryEntry> filtered;

    for (const HistoryEntry& entry : m_entries) {
        if (entry.timestamp >= date) {
            filtered.append(entry);
        }
    }

    m_entries = filtered;
}

bool CommandHistory::saveToFile(const QString& filePath) const {
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[CommandHistory] Cannot open file for writing:" << filePath;
        return false;
    }

    QJsonArray array;

    for (const HistoryEntry& entry : m_entries) {
        QJsonObject obj;
        obj["command"] = entry.command;
        obj["timestamp"] = entry.timestamp.toString(Qt::ISODate);
        obj["successful"] = entry.successful;
        obj["result"] = entry.result;

        array.append(obj);
    }

    QJsonDocument doc(array);
    file.write(doc.toJson());
    file.close();

    qDebug() << "[CommandHistory] Saved" << m_entries.size() << "entries to" << filePath;
    return true;
}

bool CommandHistory::loadFromFile(const QString& filePath) {
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[CommandHistory] Cannot open file for reading:" << filePath;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isArray()) {
        qWarning() << "[CommandHistory] Invalid JSON format";
        return false;
    }

    QJsonArray array = doc.array();

    m_entries.clear();

    for (const QJsonValue& val : array) {
        QJsonObject obj = val.toObject();

        HistoryEntry entry;
        entry.command = obj["command"].toString();
        entry.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
        entry.successful = obj["successful"].toBool(true);
        entry.result = obj["result"].toString();

        m_entries.append(entry);
    }

    qDebug() << "[CommandHistory] Loaded" << m_entries.size() << "entries from" << filePath;
    return true;
}

void CommandHistory::setMaxEntries(int max) {
    m_maxEntries = qMax(1, max);
    trimToMaxSize();
}

void CommandHistory::trimToMaxSize() {
    while (m_entries.size() > m_maxEntries) {
        m_entries.removeLast();
    }
}

} // namespace core
} // namespace aicad
