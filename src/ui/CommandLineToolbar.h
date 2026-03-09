#ifndef COMMANDLINETOOLBAR_H
#define COMMANDLINETOOLBAR_H

#include <QWidget>
#include <QToolButton>
#include <QHBoxLayout>
#include <QCheckBox>

namespace aicad {
namespace ui {

class CommandLineToolbar : public QWidget {
    Q_OBJECT

public:
    explicit CommandLineToolbar(QWidget* parent = nullptr);

    QToolButton* pinButton() const { return m_pinButton; }
    QToolButton* copyButton() const { return m_copyButton; }
    QToolButton* searchButton() const { return m_searchButton; }
    QToolButton* settingsButton() const { return m_settingsButton; }
    QToolButton* clearButton() const { return m_clearButton; }
    QToolButton* expandButton() const { return m_expandButton; }

    QCheckBox* autoCompleteCheck() const { return m_autoCompleteCheck; }
    QCheckBox* echoCheck() const { return m_echoCheck; }

signals:
    void pinToggled(bool pinned);
    void copyRequested();
    void searchRequested();
    void settingsRequested();
    void clearRequested();
    void expandToggled(bool expanded);
    void autoCompleteToggled(bool enabled);
    void echoToggled(bool enabled);

private:
    void setupUI();
    void createButtons();
    void createOptions();

    QHBoxLayout* m_layout;

    QToolButton* m_pinButton;
    QToolButton* m_copyButton;
    QToolButton* m_searchButton;
    QToolButton* m_settingsButton;
    QToolButton* m_clearButton;
    QToolButton* m_expandButton;

    QCheckBox* m_autoCompleteCheck;
    QCheckBox* m_echoCheck;
};

} // namespace ui
} // namespace aicad

#endif
