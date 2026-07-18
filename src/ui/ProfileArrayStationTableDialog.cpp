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
    resize(560, 420);

    auto* layout = new QVBoxLayout(this);

    auto* info = new QLabel(this);
    info->setText(m_array
        ? tr("%1 station(s), chainage %2 → %3 m, interval %4 m")
              .arg(m_array->stationCount())
              .arg(m_array->startChainage(), 0, 'f', 3)
              .arg(m_array->endChainage(),   0, 'f', 3)
              .arg(m_array->interval(),      0, 'f', 3)
        : tr("No profile array"));
    layout->addWidget(info);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({
        tr("#"), tr("Chainage [m]"), tr("Cant [mm]"),
        tr("H [m]"), tr("Azimuth [deg]") });
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
        m_table->setItem(row, 1, makeItem(QString::number(p, 'f', 3)));

        if (tcl) {
            // 與 AlignedProfileArray 實際計算所用資料來源保持一致（優先 ALD
            // 匯入，沒有匯入時才退回目前 horizontal()/vertical()）。
            const double cant = tcl->getCantForCalc(p);
            const double h    = tcl->getAppliedHForCalc(p);
            const double az   = tcl->getAzimuthForCalc(p) * 180.0 / M_PI;
            m_table->setItem(row, 2, makeItem(QString::number(cant, 'f', 2)));
            m_table->setItem(row, 3, makeItem(QString::number(h,    'f', 3)));
            m_table->setItem(row, 4, makeItem(QString::number(az,   'f', 3)));
        } else {
            for (int col = 2; col <= 4; ++col)
                m_table->setItem(row, col, makeItem(tr("—")));
        }
    }
}

} // namespace ui
} // namespace aicad
