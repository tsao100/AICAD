#include "ProfileArrayStationTableDialog.h"
#include "cad/AlignedProfileArray.h"
#include "railway/RailwayAlignment.h"

#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <cmath>

namespace aicad {
namespace ui {

ProfileArrayStationTableDialog::ProfileArrayStationTableDialog(
    cad::AlignedProfileArray* arr, QWidget* parent)
    : QDialog(parent)
    , m_array(arr)
{
    setWindowTitle(tr("Station Table — %1")
        .arg(m_array ? m_array->name() : QString()));
    resize(640, 420);

    auto* layout = new QVBoxLayout(this);

    auto* info = new QLabel(this);
    info->setText(m_array
        ? tr("%1 segment(s), %2 station(s), chainage %3 → %4 m")
              .arg(m_array->segmentCount())
              .arg(m_array->stationCount())
              .arg(m_array->startChainage(), 0, 'f', 3)
              .arg(m_array->endChainage(),   0, 'f', 3)
        : tr("No profile array"));
    layout->addWidget(info);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({
        tr("#"), tr("Segment"), tr("Chainage [m]"), tr("Cant [mm]"),
        tr("H [m]"), tr("Azimuth [deg]"), tr("Alignment") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);  // 唯讀
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_table);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);

    populateTable();
}

void ProfileArrayStationTableDialog::populateTable() {
    if (!m_array) return;

    const QVector<double> stations = m_array->stationChainages();
    const QVector<int> segIdx = m_array->stationSegmentIndices();
    const QList<cad::ProfileArraySegment> segs = m_array->segments();
    railway::TrackCenterLine* tcl = m_array->trackCenterLine();

    m_table->setRowCount(stations.size());
    for (int row = 0; row < stations.size(); ++row) {
        const double p = stations[row];

        auto makeItem = [](const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            return item;
        };

        m_table->setItem(row, 0, makeItem(QString::number(row)));
        const int si = (row < segIdx.size()) ? segIdx[row] : -1;
        m_table->setItem(row, 1, makeItem(si >= 0 && si < segs.size() ? segs[si].name : tr("—")));
        m_table->setItem(row, 2, makeItem(QString::number(p, 'f', 3)));

        if (tcl) {
            // 與 AlignedProfileArray 實際計算所用資料來源保持一致（優先 ALD
            // 匯入，沒有匯入時才退回目前 horizontal()/vertical()）；
            // 里程超出主線形時，使用相連線形（resolveAlignment）。
            const cad::AlignedProfileArray::AlignmentSample as = m_array->resolveAlignment(p);
            const double cant = as.tcl->getCantForCalc(as.p);
            const double h    = as.tcl->getAppliedHForCalc(as.p);
            const double az   = as.tcl->getAzimuthForCalc(as.p) * 180.0 / M_PI;
            m_table->setItem(row, 3, makeItem(QString::number(cant, 'f', 2)));
            m_table->setItem(row, 4, makeItem(QString::number(h,    'f', 3)));
            m_table->setItem(row, 5, makeItem(QString::number(az,   'f', 3)));
            QString nm = as.tcl->name();
            if (as.connected)  nm += tr(" (相連, p=%1)").arg(as.p, 0, 'f', 3);
            if (as.outOfRange) nm = tr("⚠ 超出範圍（外推）");
            m_table->setItem(row, 6, makeItem(nm));
        } else {
            for (int col = 3; col <= 6; ++col)
                m_table->setItem(row, col, makeItem(tr("—")));
        }
    }
}

} // namespace ui
} // namespace aicad
