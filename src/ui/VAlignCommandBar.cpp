/**
 * @file VAlignCommandBar.cpp
 * @brief Step 13 — VAlignCommandBar 實作
 */

#include "VAlignCommandBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QDebug>

namespace aicad {
namespace ui {

// ─────────────────────────────────────────────────────────────────────────────

VAlignCommandBar::VAlignCommandBar(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    applyStyle();
}

// ── UI setup ──────────────────────────────────────────────────────────────────

void VAlignCommandBar::setupUI()
{
    setFixedHeight(28);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(6);

    // 提示標籤
    m_prompt = new QLabel(QStringLiteral("▶"));
    m_prompt->setObjectName("cmdPrompt");
    m_prompt->setMinimumWidth(140);
    m_prompt->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_prompt);

    // 輸入框
    m_input = new QLineEdit;
    m_input->setObjectName("cmdInput");
    m_input->setPlaceholderText(
        "CH=  EL=  K=  LVC=  GRADE=  （Enter 確認）");
    m_input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_input);

    connect(m_input, &QLineEdit::returnPressed,
            this,    &VAlignCommandBar::onReturnPressed);
}

void VAlignCommandBar::applyStyle()
{
    setStyleSheet(R"(
        VAlignCommandBar {
            background-color: #030609;
            border-top: 1px solid #0b1a2c;
        }
        QLabel#cmdPrompt {
            background: transparent;
            color: #0d80c0;
            font-family: 'Courier New';
            font-size: 8pt;
        }
        QLineEdit#cmdInput {
            background-color: #020408;
            border: 1px solid #0b1c2e;
            border-radius: 2px;
            color: #1898e8;
            font-family: 'Courier New';
            font-size: 9pt;
            padding: 1px 6px;
            selection-background-color: #103050;
        }
        QLineEdit#cmdInput:focus {
            border-color: #1870b8;
        }
    )");
}

// ── Public API ────────────────────────────────────────────────────────────────

void VAlignCommandBar::setPrompt(const QString& text)
{
    m_prompt->setText(text.isEmpty() ? QStringLiteral("▶") : text);
}

void VAlignCommandBar::clearInput()
{
    m_input->clear();
}

void VAlignCommandBar::focusInput()
{
    m_input->setFocus();
}

// ── Parsing ───────────────────────────────────────────────────────────────────
//
//  優先嘗試每種 keyword= 格式（大小寫均接受）。
//  解析成功後 emit 對應 signal，清除輸入框。
//  無法匹配時 emit commandEntered（轉大寫）。

void VAlignCommandBar::onReturnPressed()
{
    const QString raw   = m_input->text().trimmed();
    if (raw.isEmpty()) return;

    const QString upper = raw.toUpper();

    // 工廠：keyword → signal emitter
    // 格式：KEYWORD=<數字>，數字可帶負號與小數
    static const QRegularExpression reNum(
        QStringLiteral(R"(^([A-Z1-9]+)\s*=\s*(-?\d+(?:\.\d+)?)$)"));

    const QRegularExpressionMatch m = reNum.match(upper);
    if (m.hasMatch()) {
        const QString key = m.captured(1);
        bool ok = false;
        const double val = m.captured(2).toDouble(&ok);

        if (ok) {
            if      (key == QLatin1String("CH"))    { Q_EMIT chainageSet(val); }
            else if (key == QLatin1String("EL"))    { Q_EMIT elevationSet(val); }
            else if (key == QLatin1String("K"))     { Q_EMIT kValueSet(val); }
            else if (key == QLatin1String("LVC"))   { Q_EMIT lvcSet(val); }
            else if (key == QLatin1String("GRADE")) { Q_EMIT gradeSet(val); }
            else {
                // 未知 KEY= 格式，仍轉發原始文字
                Q_EMIT commandEntered(upper);
            }
            m_input->clear();
            return;
        }
    }

    // 純命令文字（例如 "UNDO"、"ESC"）
    Q_EMIT commandEntered(upper);
    m_input->clear();
}

} // namespace ui
} // namespace aicad
