#ifndef TRANSIENTCOMMANDHISTORY_H
#define TRANSIENTCOMMANDHISTORY_H

#include <QObject>
#include <QLabel>
#include <QTimer>
#include <QVariantAnimation>
#include <QGraphicsOpacityEffect>
#include <QStringList>

namespace aicad {
namespace ui {

// 不再是 QWidget，純粹管理直接掛在 cadView 上的 QLabel 群
class TransientCommandHistory : public QObject {
    Q_OBJECT

public:
    // anchor = CommandLineWidget，cadView = labels 的 parent
    explicit TransientCommandHistory(QWidget* anchor, QWidget* cadView);
    ~TransientCommandHistory() override;

    void addLine(const QString& text, bool isPrompt = false);
    void setMaxLines(int n);
    void beginFadeOut();

    // anchor 位置改變時（CommandLineWidget move/resize）呼叫
    void updatePosition();

private:
    void repositionLabels();    // 計算每個 label 的 geometry

    QWidget*             m_anchor;
    QWidget*             m_cadView;
    QVector<QLabel*>     m_labels;       // parent = m_cadView
    QVector<QGraphicsOpacityEffect*> m_effects;  // 對應每個 label

    QStringList          m_lines;
    QList<bool>          m_isPrompt;
    int                  m_maxLines = 3;

    QTimer*              m_fadeTimer = nullptr;
    QVariantAnimation*  m_fadeAnim  = nullptr;   // 動畫目標：所有 effect

    static constexpr int LINE_H       = 22;   // 每行高度 px
    static constexpr int SIDE_PAD     =  6;   // 左右內距
    static constexpr int GAP_BELOW    =  2;   // 與 anchor 上緣的間距
};

} // namespace ui
} // namespace aicad
#endif
