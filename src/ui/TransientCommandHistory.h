#ifndef TRANSIENTCOMMANDHISTORY_H
#define TRANSIENTCOMMANDHISTORY_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>
#include <QPropertyAnimation>
#include <QStringList>

namespace aicad {
namespace ui {

// 命令進行時浮動在命令列上方，指令結束後淡出
class TransientCommandHistory : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit TransientCommandHistory(QWidget* anchor);

    void addLine(const QString& text, bool isPrompt = false);
    void setMaxLines(int n);     // 1~3

    // 命令結束後呼叫 → fade out
    void beginFadeOut();

    qreal opacity() const { return m_opacity; }
    void  setOpacity(qreal v);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void repositionAboveAnchor();
    void rebuildLabels();

    QWidget*     m_anchor;
    QVBoxLayout* m_layout;
    QStringList  m_lines;
    QList<bool>  m_isPrompt;
    int          m_maxLines = 3;

    qreal        m_opacity  = 1.0;

    QPropertyAnimation* m_fadeAnim = nullptr;
    QTimer*             m_fadeTimer= nullptr;
};

} // namespace ui
} // namespace aicad

#endif
