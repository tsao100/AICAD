/**
 * @file TrackNameSequenceDialog.h
 * @brief TRACKEXTRACT（TX）輸入 A／AUTO 後彈出的命名規則對話框。
 *
 * 依使用者慣例（例如 "B01U"）將每段自動切分產生的 TrackCenterLine 依序命名為
 * 「字首（0~1 字元）＋序號（固定 2 碼，補零）＋字尾（0~1 字元）」，序號可選擇
 * 每段遞增或遞減。命名套用到「全部」結果線段——包含原線（保留頭段的那條）
 * 本身，因為在此慣例下整條原始線形本來就代表一系列連號的實體軌道區段（如
 * B01U/B02U/B03U…），而不是只把新切出來的那幾段另外命名。
 *
 * @author AICAD Team
 */
#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QLabel;

namespace aicad {
namespace ui {

class TrackNameSequenceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TrackNameSequenceDialog(QWidget* parent = nullptr);
    ~TrackNameSequenceDialog() override = default;

    /** 字首（0 或 1 個字元；空字串代表不加字首）。 */
    QString prefix() const;
    /** 開始序號（顯示／套用時一律補零至 2 碼；超出 0~99 時碼數自動增加）。 */
    int     startSeq() const;
    /** 字尾（0 或 1 個字元；空字串代表不加字尾）。 */
    QString suffix() const;
    /** true＝每段序號遞增（+1），false＝遞減（−1）。 */
    bool    isIncrement() const;

    /**
     * @brief 依規則組出第 n 個（0-based）名稱，例如 prefix="B" seq0=1 suffix="U"
     *        increment=true 時，formatName(p,1,s,0)="B01U"、formatName(...,1)="B02U"。
     *        序號一律以 %02d 起算；若超出 0~99 範圍（連續遞增/遞減很多段時）
     *        則自然溢出為 3 碼以上，不做截斷，避免不同段撞名。
     */
    static QString formatName(const QString& prefix, int startSeq,
                              const QString& suffix, bool increment, int stepIndex);

private Q_SLOTS:
    void updatePreview();

protected:
    /** 顯示時主動 raise()＋activateWindow()，避免視窗開啟後沒有搶到前景焦點。 */
    void showEvent(QShowEvent* event) override;

private:
    QLineEdit*  m_prefixEdit    = nullptr;
    QSpinBox*   m_startSeqSpin  = nullptr;
    QLineEdit*  m_suffixEdit    = nullptr;
    QComboBox*  m_directionCombo = nullptr;
    QLabel*     m_previewLabel  = nullptr;
};

} // namespace ui
} // namespace aicad
