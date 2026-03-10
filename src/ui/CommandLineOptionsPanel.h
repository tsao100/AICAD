#ifndef COMMANDLINEOPTIONSPANEL_H
#define COMMANDLINEOPTIONSPANEL_H

#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMap>

namespace aicad {
namespace ui {

struct CommandOption {
    QString key;
    QString label;
    QString description;
    QString shortcut;
    bool isDefault;

    CommandOption()
        : isDefault(false) {}

    CommandOption(const QString& k, const QString& l, const QString& d = QString(), const QString& s = QString(), bool def = false)
        : key(k), label(l), description(d), shortcut(s), isDefault(def) {}
};

class CommandLineOptionsPanel : public QWidget {
    Q_OBJECT

public:
    explicit CommandLineOptionsPanel(QWidget* parent = nullptr);

    void setOptions(const QList<CommandOption>& options);
    void clearOptions();

    void highlightOption(const QString& key);
    void setDefaultOption(const QString& key);

signals:
    void optionSelected(const QString& key);
    void optionHovered(const QString& key, const QString& description);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void createOptionButton(const CommandOption& option);
    void clearButtons();

    QHBoxLayout* m_layout;
    QMap<QString, QPushButton*> m_buttons;
    QString m_currentDefault;
};

} // namespace ui
} // namespace aicad

Q_DECLARE_METATYPE(aicad::ui::CommandOption)
Q_DECLARE_METATYPE(QList<aicad::ui::CommandOption>)

#endif
