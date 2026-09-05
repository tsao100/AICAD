/**
 * @file TrackNameSequenceDialog.cpp
 * @brief 見 TrackNameSequenceDialog.h 檔頭說明。
 */
#include "ui/TrackNameSequenceDialog.h"

#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QShowEvent>

namespace aicad {
namespace ui {

TrackNameSequenceDialog::TrackNameSequenceDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("命名規則設定 — TRACKEXTRACT Auto"));
    setModal(true);

    m_prefixEdit = new QLineEdit(this);
    m_prefixEdit->setMaxLength(1);
    m_prefixEdit->setPlaceholderText(tr("例：B"));
    m_prefixEdit->setFixedWidth(60);

    m_startSeqSpin = new QSpinBox(this);
    m_startSeqSpin->setRange(0, 99);
    m_startSeqSpin->setValue(1);
    m_startSeqSpin->setFixedWidth(70);

    m_suffixEdit = new QLineEdit(this);
    m_suffixEdit->setMaxLength(1);
    m_suffixEdit->setPlaceholderText(tr("例：U"));
    m_suffixEdit->setFixedWidth(60);

    m_directionCombo = new QComboBox(this);
    m_directionCombo->addItem(tr("遞增 Increment (+1)"));
    m_directionCombo->addItem(tr("遞減 Decrement (-1)"));

    auto* form = new QFormLayout();
    form->addRow(tr("字首 Prefix（1 碼，可留空）："), m_prefixEdit);
    form->addRow(tr("開始序號 Start No.（2 碼）："), m_startSeqSpin);
    form->addRow(tr("字尾 Suffix（1 碼，可留空）："), m_suffixEdit);
    form->addRow(tr("序號方向 Direction："), m_directionCombo);

    m_previewLabel = new QLabel(this);
    m_previewLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_previewLabel->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addWidget(m_previewLabel);
    mainLayout->addWidget(buttons);

    connect(m_prefixEdit,     &QLineEdit::textChanged,          this, &TrackNameSequenceDialog::updatePreview);
    connect(m_startSeqSpin,   qOverload<int>(&QSpinBox::valueChanged),
            this, &TrackNameSequenceDialog::updatePreview);
    connect(m_suffixEdit,     &QLineEdit::textChanged,          this, &TrackNameSequenceDialog::updatePreview);
    connect(m_directionCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TrackNameSequenceDialog::updatePreview);

    updatePreview();
}

QString TrackNameSequenceDialog::prefix() const
{
    return m_prefixEdit->text();
}

int TrackNameSequenceDialog::startSeq() const
{
    return m_startSeqSpin->value();
}

QString TrackNameSequenceDialog::suffix() const
{
    return m_suffixEdit->text();
}

bool TrackNameSequenceDialog::isIncrement() const
{
    return m_directionCombo->currentIndex() == 0;
}

QString TrackNameSequenceDialog::formatName(const QString& prefix, int startSeq,
                                             const QString& suffix, bool increment,
                                             int stepIndex)
{
    const int seq = startSeq + (increment ? stepIndex : -stepIndex);
    // 一律至少 2 碼補零；超出 0~99（seq<0 或 seq>99）時自然增加碼數，不截斷，
    // 避免大量連續切分時撞名（見標頭檔說明）。
    const QString seqText = QStringLiteral("%1").arg(seq, 2, 10, QLatin1Char('0'));
    return prefix + seqText + suffix;
}

void TrackNameSequenceDialog::updatePreview()
{
    const QString p = prefix();
    const QString s = suffix();
    const int seq0 = startSeq();
    const bool inc = isIncrement();

    QStringList names;
    for (int i = 0; i < 3; ++i)
        names << formatName(p, seq0, s, inc, i);

    m_previewLabel->setText(tr("預覽 Preview：%1 → %2 → %3 → ...")
                                 .arg(names[0], names[1], names[2]));
}

void TrackNameSequenceDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // 確保視窗顯示時主動搶到前景焦點（避免從非同步／非使用者當下直接點擊
    // 觸發的路徑跳出來的 exec() 視窗，開啟後卻沒有真正跳到最前面）。
    raise();
    activateWindow();
}

} // namespace ui
} // namespace aicad
