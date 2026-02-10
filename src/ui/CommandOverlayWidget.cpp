//src/ui/CommandOverlayWidget.cpp

#include "CommandOverlayWidget.h"
#include "CommandInput.h"

#include "core/Application.h"
#include "core/EventBus.h"

#include <QVBoxLayout>
#include <QEvent>
#include <QTimer>

using namespace aicad::core;

namespace aicad::ui {

CommandOverlayWidget::CommandOverlayWidget(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint);
    //setAttribute(Qt::WA_StyledBackground, true);

    resize(360, 27);                 // 初始寬度
    setMinimumWidth(220);
    setMaximumWidth(600);

    // setStyleSheet(R"(
    //     background-color: rgba(30,30,30,200);
    //     border-radius: 6px;
    //     )");
    // setAttribute(Qt::WA_OpaquePaintEvent, true);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // spacer 把 input 推到底
    rootLayout->addStretch();

    // === Command input ===
    m_input = new CommandInput(this);
    rootLayout->addWidget(m_input);

    // // === 跟隨 CadView resize ===
     parent->installEventFilter(this);

    EventBus* bus = Application::instance()->eventBus();
    // // === 監聽 prompt 事件 ===
    bus->subscribe(
        Events::COMMAND_PROMPT,
        this,
        [this](const QVariant& v) {

            QString msg = v.value<QString>();

             handlePrompt(msg);
         }
        );

     bus->subscribe(
         Events::COMMAND_EXECUTED,
         this,
         [this](const QVariant&) {
             finishCommand();
         }
         );

     bus->subscribe(
         Events::COMMAND_CANCELLED,
         this,
         [this](const QVariant&) {
             finishCommand();
         }
         );

     raise();
     show();

     setMouseTracking(true);
}

void CommandOverlayWidget::handlePrompt(const QString& text)
{
    if (text.isEmpty())
        return;

    // 第一次 prompt：只顯示在 input
    if (!m_commandActive) {
        m_commandActive = true;
        m_activePrompt = text;
        m_input->setText(text);
        return;
    }

    // 第二次以後：
    // 把上一個 active prompt 推進 label
    appendPromptLine(m_activePrompt);

    // 更新 active prompt
    m_activePrompt = text;
    m_input->setText(text);
}

void CommandOverlayWidget::finishCommand()
{
    if (!m_commandActive)
        return;

    m_commandActive = false;
    m_activePrompt.clear();

    // input 清空或顯示 idle
    m_input->clear();

    // ⭐ 現在才開始倒數
    for (QLabel* lbl : m_promptLabels) {
        QTimer::singleShot(3000, this, [this, lbl]() {
            if (m_promptLabels.removeOne(lbl)) {
                lbl->deleteLater();
                repositionPrompts();
            }
        });
    }
}

bool CommandOverlayWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        reposition();
    }
    return QWidget::eventFilter(obj, event);
}

void CommandOverlayWidget::adjustOverlayHeight()
{
    int top = m_input->y();

    for (auto* lbl : m_promptLabels)
        top = qMin(top, lbl->y());

    int bottom = m_input->y() + m_input->height();
    int newHeight = bottom - top + 2;   // 上下 padding

    setFixedHeight(newHeight);
    reposition();
}

void CommandOverlayWidget::repositionPrompts()
{
    if (!parentWidget())
        return;

    // ① 先取得「CommandInput 左上角的 global 座標」
    QPoint OverlayTopLeftGlobal = mapToGlobal(QPoint(0, 0));

    // ⭐ 轉成「parentWidget 座標」
    int x = OverlayTopLeftGlobal.x();
    int y = OverlayTopLeftGlobal.y() - promptSpacing;


    // int x = this->x() + 365;
    // int y = this->y() - marginBottom - promptSpacing+175;

    // 從最舊 → 最新
    for (int i = m_promptLabels.size()-1; i >=0 ; --i) {
        QLabel* lbl = m_promptLabels[i];
        y -= lbl->height();
        lbl->move(x, y);
        y -= promptSpacing;
    }

    update();
}

void CommandOverlayWidget::appendPromptLine(const QString& text)
{
    if (text.isEmpty())
        return;

    while (m_promptLabels.size() > MaxPromptLines - 2) {
        QLabel* oldest = m_promptLabels.takeFirst();
        oldest->deleteLater();
    }

    // QWidget* host = parentWidget();   // CadView
    // if (!host)
    //     host = window();


    // 2️⃣ 新 label
    QLabel* label = new QLabel(text, this);

    // ⭐ 關鍵：根據文字算寬度
    QFontMetrics fm(label->font());

    QRect textRect = fm.boundingRect(
        QRect(0, 0, PromptMaxWidth, 1000),
        Qt::TextSingleLine,
        text
        );

    int contentWidth = textRect.width() + PromptHPadding;
    int finalWidth = qBound(PromptMinWidth, contentWidth, PromptMaxWidth);

    //label->setWordWrap(true);
    label->setStyleSheet(R"(
        background-color:
        rgb(235,235,235);
        color: rgb(20,20,20);
        border-radius: 4px;
        padding: 4px 6px; )");
    label->setAttribute(Qt::WA_StyledBackground, true);
    label->setAttribute(Qt::WA_OpaquePaintEvent, true);
    label->setWindowFlags(Qt::FramelessWindowHint | Qt::ToolTip);
    label->setFixedWidth(finalWidth); // 扣左右 margin
    label->adjustSize();
    label->show();
    label->raise();
    m_promptLabels.append(label);

    repositionPrompts();
}

void CommandOverlayWidget::mousePressEvent(QMouseEvent* e)
{
    const int edge = 6;
    if (e->pos().x() >= width() - edge) {
        m_resizing = true;
        m_dragStartPos = e->globalPos();
        m_startWidth = width();
        setCursor(Qt::SizeHorCursor);
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void CommandOverlayWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_resizing) {
        int dx = e->globalX() - m_dragStartPos.x();
        resize(m_startWidth + dx, height());

        for (auto* lbl : m_promptLabels)
            lbl->setFixedWidth(width());

        repositionPrompts();
        e->accept();
        return;
    }

    const int edge = 6;
    setCursor(e->pos().x() >= width() - edge
                  ? Qt::SizeHorCursor
                  : Qt::ArrowCursor);
}

void CommandOverlayWidget::mouseReleaseEvent(QMouseEvent*)
{
    m_resizing = false;
    setCursor(Qt::ArrowCursor);
}

void CommandOverlayWidget::reposition()
{
    if (!parentWidget())
        return;

    QWidget* view = parentWidget();
    const int marginBottom = 8;

    int x = (view->width() - width()) / 2;
    int y = view->height() - height() - marginBottom;

    move(x, y);
}

}
