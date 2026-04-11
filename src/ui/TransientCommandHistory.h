#ifndef TRANSIENTCOMMANDHISTORY_H
#define TRANSIENTCOMMANDHISTORY_H

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>
#include <QStringList>

namespace aicad {
namespace ui {

class TransientCommandHistory : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit TransientCommandHistory(QWidget* anchor);

    void addLine(const QString& text, bool isPrompt = false);
    void setMaxLines(int n);        // 1~3
    void beginFadeOut();

    qreal opacity() const  { return m_opacity; }
    void  setOpacity(qreal v);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void repositionAboveAnchor();
    void rebuildLabels();           // 手動 setGeometry，無 layout

    QWidget*     m_anchor;
    QVector<QLabel*> m_labels;          // 數量由 m_maxLines 決定
    QStringList  m_lines;
    QList<bool>  m_isPrompt;
    int          m_maxLines = 3;
    qreal        m_opacity  = 1.0;

    QPropertyAnimation* m_fadeAnim  = nullptr;
    QTimer*             m_fadeTimer = nullptr;

    static constexpr int LINE_PADDING = 4;   // 上下各 2px
};

} // namespace ui
} // namespace aicad
#endif
