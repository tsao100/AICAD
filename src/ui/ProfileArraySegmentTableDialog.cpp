#include "ProfileArraySegmentTableDialog.h"
#include "ProfileArrayStationTableDialog.h"
#include "cad/AlignedProfileArray.h"
#include "cad/Document.h"
#include "cad/Sketch.h"
#include "cad/ProfileLoftSolid.h"
#include "cad/ChamferSolid.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace aicad {
namespace ui {

namespace {

enum Col { ColName = 0, ColStart, ColEnd, ColProfile, ColThick, ColW, ColInterval,
           ColAlignment,   ///< 唯讀：該段所依賴的線形（不參與 readSegments／貼上）
           ColFixedCount };

const QStringList& fixedHeaders() {
    static const QStringList h = {
        QObject::tr("段名稱 Name"),  QObject::tr("起點里程 Start [m]"),
        QObject::tr("終點里程 End [m]"), QObject::tr("Profile"),
        QObject::tr("Thick"),        QObject::tr("w"),
        QObject::tr("間距 Interval [m]"), QObject::tr("線形 Alignment") };
    return h;
}

/// 比對用：去掉 [..] (..) 與空白、轉小寫。 "起點里程 Start [m]" → "起點里程start"
QString normHeader(const QString& s) {
    QString t = s;
    t.remove(QRegularExpression(QStringLiteral("\\[[^\\]]*\\]|\\([^)]*\\)|（[^）]*）")));
    t.remove(QRegularExpression(QStringLiteral("\\s+")));
    return t.toLower();
}

/// 欄標題 → 固定欄索引；不是固定欄回 -1。接受中/英文別名。
int fixedColumnOf(const QString& header) {
    static const QList<QStringList> aliases = {
        { "name", "段名稱", "段名", "段名稱name" },
        { "start", "起", "起點", "起點里程", "起點里程start" },
        { "end", "終", "終點", "終點里程", "終點里程end" },
        { "profile", "使用的profile" },
        { "thick" },
        { "w" },
        { "interval", "間距", "間距interval" },
        { "alignment", "線形", "線形alignment" } };
    const QString n = normHeader(header);
    for (int i = 0; i < aliases.size(); ++i)
        if (aliases[i].contains(n)) return i;
    return -1;
}

bool isValidParamName(const QString& s) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return re.match(s).hasMatch();
}

bool parseNum(const QString& text, double& out) {
    QString t = text.trimmed();
    t.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    out = t.toDouble(&ok);
    return ok;
}

QString fmt(double v) {
    QString s = QString::number(v, 'f', 6);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    return s == QLatin1String("-0") ? QStringLiteral("0") : s;
}

QString oneLine(QString s) {
    s.replace(QLatin1Char('\t'), QLatin1Char(' '));
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return s;
}

/// Profile 欄：下拉選單（文件內所有 Sketch 的名稱）。
class ProfileDelegate : public QStyledItemDelegate {
public:
    ProfileDelegate(std::function<QStringList()> names, QObject* parent)
        : QStyledItemDelegate(parent), m_names(std::move(names)) {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                          const QModelIndex&) const override {
        auto* cb = new QComboBox(parent);
        cb->addItems(m_names());
        return cb;
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        auto* cb = static_cast<QComboBox*>(editor);
        const int i = cb->findText(index.data(Qt::EditRole).toString());
        if (i >= 0) cb->setCurrentIndex(i);
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override {
        model->setData(index, static_cast<QComboBox*>(editor)->currentText(), Qt::EditRole);
    }
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex&) const override {
        editor->setGeometry(option.rect);
    }
private:
    std::function<QStringList()> m_names;
};

/// 表格：編輯器沒有焦點時，Ctrl+C / Ctrl+V / Delete 交給對話框處理
/// （編輯中的 QLineEdit 先收到按鍵，所以不會搶走文字框內的複製貼上）。
class SegmentTable : public QTableWidget {
public:
    using QTableWidget::QTableWidget;
    std::function<void()> onCopy, onPaste, onClear;
protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (e->matches(QKeySequence::Copy)  && onCopy)  { onCopy();  return; }
        if (e->matches(QKeySequence::Paste) && onPaste) { onPaste(); return; }
        if (e->key() == Qt::Key_Delete && state() != EditingState && onClear) { onClear(); return; }
        QTableWidget::keyPressEvent(e);
    }
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

ProfileArraySegmentTableDialog::ProfileArraySegmentTableDialog(
    cad::AlignedProfileArray* arr, QWidget* parent)
    : QDialog(parent)
    , m_array(arr)
{
    setWindowTitle(tr("沿線斷面陣列 — 分段資料表 — %1").arg(arr ? arr->name() : QString()));
    resize(980, 460);

    auto* root = new QVBoxLayout(this);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    root->addWidget(m_info);

    // ── 工具列 ──
    auto* bar = new QHBoxLayout;
    auto addBtn = [&](const QString& text, const QString& tip, auto fn) {
        auto* b = new QPushButton(text, this);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, fn);
        bar->addWidget(b);
        return b;
    };
    addBtn(tr("新增列 Add Row"), tr("在目前列之後新增一段"), [this] { addRow(); });
    addBtn(tr("刪除列 Delete Row"), tr("刪除選取的列"), [this] { deleteRows(); });
    addBtn(tr("新增欄位 Add Column"),
           tr("新增一個「profile 參數名」欄位；有填值的段會以該值覆寫該參數"),
           [this] { addColumn(); });
    addBtn(tr("清除 Clear Cells"),
           tr("清空選取的 Thick／w／自訂欄位儲存格（Delete 鍵）"), [this] { clearCells(); });
    addBtn(tr("全部清除 Clear All"), tr("刪除所有列"), [this] { clearAll(); });
    addBtn(tr("複製 Copy"),
           tr("複製選取內容（Ctrl+C）。選取整列時連同欄標題一起複製，可用於貼到別處新增列"),
           [this] { copySelection(); });
    addBtn(tr("貼上 Paste"),
           tr("貼上（Ctrl+V）。含欄標題 → 新增為新的列；不含 → 從目前儲存格起覆寫"),
           [this] { paste(); });
    bar->addStretch(1);
    root->addLayout(bar);

    // ── 表格 ──
    auto* table = new SegmentTable(this);
    m_table = table;
    // 起／終點里程改變 → 重算該列「線形」欄（該段依賴哪條線形）
    connect(table, &QTableWidget::cellChanged, this, [this](int r, int c) {
        if (c == ColStart || c == ColEnd) refreshAlignmentCell(r);
    });
    table->onCopy  = [this] { copySelection(); };
    table->onPaste = [this] { paste(); };
    table->onClear = [this] { clearCells(); };
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->verticalHeader()->setDefaultSectionSize(24);
    table->setItemDelegateForColumn(ColProfile, new ProfileDelegate([this] {
        QStringList n;
        for (const auto& p : profiles()) n << p.first;
        return n;
    }, table));
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(tr("複製 Copy"), this, [this] { copySelection(); });
        menu.addAction(tr("貼上 Paste"), this, [this] { paste(); });
        menu.addSeparator();
        menu.addAction(tr("新增列 Add Row"), this, [this] { addRow(); });
        menu.addAction(tr("刪除列 Delete Row"), this, [this] { deleteRows(); });
        menu.addAction(tr("新增欄位 Add Column"), this, [this] { addColumn(); });
        menu.addSeparator();
        menu.addAction(tr("清除 Clear Cells"), this, [this] { clearCells(); });
        menu.addAction(tr("全部清除 Clear All"), this, [this] { clearAll(); });
        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });
    root->addWidget(m_table, 1);

    // ── 底部按鈕 ──
    auto* bottom = new QHBoxLayout;
    auto* stations = new QPushButton(tr("測站表 Stations…"), this);
    stations->setToolTip(tr("檢視各測站的里程／cant／H／方位角（唯讀）"));
    connect(stations, &QPushButton::clicked, this, [this] { showStations(); });
    bottom->addWidget(stations);
    bottom->addStretch(1);
    auto* revert = new QPushButton(tr("還原 Revert"), this);
    revert->setToolTip(tr("丟棄尚未套用的修改，重新載入目前陣列的資料"));
    connect(revert, &QPushButton::clicked, this, [this] { loadFromArray(); });
    bottom->addWidget(revert);
    auto* applyBtn = new QPushButton(tr("套用 Apply"), this);
    applyBtn->setDefault(true);
    connect(applyBtn, &QPushButton::clicked, this, [this] { apply(); });
    bottom->addWidget(applyBtn);
    auto* close = new QPushButton(tr("關閉 Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    bottom->addWidget(close);
    root->addLayout(bottom);

    loadFromArray();
}

// ── 資料 ⇄ 表格 ──────────────────────────────────────────────────────────────

QList<QPair<QString, QString>> ProfileArraySegmentTableDialog::profiles() const {
    QList<QPair<QString, QString>> out;
    if (!m_array) return out;
    cad::Document* doc = m_array->document();
    if (!doc) return out;
    for (cad::Feature* f : doc->features())
        if (auto* sk = qobject_cast<cad::Sketch*>(f))
            out.append({ sk->name(), sk->id() });
    return out;
}

void ProfileArraySegmentTableDialog::loadFromArray() {
    if (!m_array) { m_table->setRowCount(0); m_table->setColumnCount(0); return; }

    const QList<cad::ProfileArraySegment> segs = m_array->segments();
    const QStringList extras = m_array->extraColumns();

    // 檔案裡的段若用到「extraColumns 未列出」的參數名，也補成欄位（不遺失資料）。
    QStringList cols = extras;
    for (const auto& s : segs)
        for (auto it = s.extra.cbegin(); it != s.extra.cend(); ++it)
            if (!cols.contains(it.key())) cols.append(it.key());

    m_table->blockSignals(true);
    m_table->clear();
    m_table->setRowCount(0);
    m_table->setColumnCount(ColFixedCount + cols.size());
    QStringList headers = fixedHeaders();
    headers += cols;
    m_table->setHorizontalHeaderLabels(headers);

    const auto prof = profiles();
    auto nameOf = [&prof](const QString& id) {
        for (const auto& p : prof) if (p.second == id) return p.first;
        return QString();
    };

    for (const auto& s : segs) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        auto put = [&](int c, const QString& t) { m_table->setItem(r, c, new QTableWidgetItem(t)); };
        put(ColName,     s.name);
        // 每列攜帶該段的穩定 id：列順序改變、段改名、中間刪列時，陣列才能把每個 Loft
        // 對回自己的段（新增的列沒有 id → 套用時由陣列配發並建立新的 Loft）。
        if (auto* nameItem = m_table->item(r, ColName))
            nameItem->setData(Qt::UserRole + 10, s.id);
        put(ColStart,    fmt(s.start));
        put(ColEnd,      fmt(s.end));
        const QString pn = nameOf(s.profileId);
        put(ColProfile,  pn.isEmpty() ? tr("(找不到 not found)") : pn);
        put(ColThick,    s.hasThick ? fmt(s.thick) : QString());
        put(ColW,        s.hasW ? fmt(s.w) : QString());
        put(ColInterval, fmt(s.interval));
        for (int k = 0; k < cols.size(); ++k)
            put(ColFixedCount + k, s.extra.contains(cols[k]) ? fmt(s.extra.value(cols[k])) : QString());
        refreshAlignmentCell(r);
    }
    m_table->blockSignals(false);
    m_table->resizeColumnsToContents();
    updateInfo();
}

bool ProfileArraySegmentTableDialog::readSegments(QList<cad::ProfileArraySegment>& segs,
                                                  QStringList& extraColumns,
                                                  QString& error) const
{
    segs.clear();
    extraColumns.clear();
    for (int c = ColFixedCount; c < m_table->columnCount(); ++c)
        extraColumns << m_table->horizontalHeaderItem(c)->text();

    const auto prof = profiles();
    QSet<QString> usedNames;

    auto text = [this](int r, int c) {
        const QTableWidgetItem* it = m_table->item(r, c);
        return it ? it->text().trimmed() : QString();
    };

    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto where = [&](int c) {
            return tr("第 %1 列「%2」").arg(r + 1).arg(m_table->horizontalHeaderItem(c)->text());
        };
        cad::ProfileArraySegment s;

        s.name = text(r, ColName);
        if (const auto* nameItem = m_table->item(r, ColName))
            s.id = nameItem->data(Qt::UserRole + 10).toString();
        if (s.name.isEmpty())            { error = tr("%1：段名稱不可空白").arg(where(ColName)); return false; }
        if (usedNames.contains(s.name))  { error = tr("%1：段名稱「%2」重複").arg(where(ColName), s.name); return false; }
        usedNames.insert(s.name);

        if (!parseNum(text(r, ColStart), s.start))       { error = tr("%1：不是數字").arg(where(ColStart));    return false; }
        if (!parseNum(text(r, ColEnd), s.end))           { error = tr("%1：不是數字").arg(where(ColEnd));      return false; }
        if (!parseNum(text(r, ColInterval), s.interval)) { error = tr("%1：不是數字").arg(where(ColInterval)); return false; }
        if (s.end < s.start)     { error = tr("%1：終點里程不可小於起點里程").arg(where(ColEnd)); return false; }
        if (s.interval <= 0.0)   { error = tr("%1：間距必須大於 0").arg(where(ColInterval)); return false; }

        const QString pn = text(r, ColProfile);
        for (const auto& p : prof) if (p.first == pn) { s.profileId = p.second; break; }
        if (s.profileId.isEmpty()) { error = tr("%1：找不到名為「%2」的草圖").arg(where(ColProfile), pn); return false; }

        const QString th = text(r, ColThick);
        if (!th.isEmpty()) {
            if (!parseNum(th, s.thick)) { error = tr("%1：不是數字（空白 = 不覆寫）").arg(where(ColThick)); return false; }
            s.hasThick = true;
        }
        const QString w = text(r, ColW);
        if (!w.isEmpty()) {
            if (!parseNum(w, s.w)) { error = tr("%1：不是數字（空白 = 不覆寫）").arg(where(ColW)); return false; }
            s.hasW = true;
        }

        for (int c = ColFixedCount; c < m_table->columnCount(); ++c) {
            const QString v = text(r, c);
            if (v.isEmpty()) continue;
            double d = 0.0;
            if (!parseNum(v, d)) { error = tr("%1：不是數字（空白 = 不覆寫）").arg(where(c)); return false; }
            s.extra.insert(m_table->horizontalHeaderItem(c)->text(), d);
        }
        segs.append(s);
    }
    return true;
}

void ProfileArraySegmentTableDialog::apply() {
    if (!m_array) { reject(); return; }

    QList<cad::ProfileArraySegment> segs;
    QStringList extras;
    QString err;
    if (!readSegments(segs, extras, err)) {
        QMessageBox::warning(this, tr("資料有誤"), err);
        return;
    }
    if (segs.isEmpty()) {
        QMessageBox::warning(this, tr("沒有任何段"), tr("至少需要一列才能建立陣列。"));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = m_array->commitSegments(segs, extras);
    QApplication::restoreOverrideCursor();

    updateInfo();

    // 下游（Loft／Chamfer）重建失敗時會保留舊形狀，畫面看起來就像「沒更新」。
    // 這裡把下游的錯誤一併回報，避免使用者不知道失敗。
    QStringList downstream;
    if (cad::Document* doc = m_array->document()) {
        QList<cad::ProfileLoftSolid*> lofts;
        for (cad::Feature* f : doc->features()) {
            if (auto* l = qobject_cast<cad::ProfileLoftSolid*>(f)) {
                if (l->sourceArray() == m_array) {
                    lofts << l;
                    if (!l->errorMessage().isEmpty())
                        downstream << QStringLiteral("%1: %2").arg(l->name(), l->errorMessage());
                }
            }
        }
        for (cad::Feature* f : doc->features()) {
            if (auto* c = qobject_cast<cad::ChamferSolid*>(f)) {
                for (cad::ProfileLoftSolid* l : lofts) {
                    if (c->sourceFeature() == l && !c->errorMessage().isEmpty())
                        downstream << QStringLiteral("%1: %2").arg(c->name(), c->errorMessage());
                }
            }
        }
    }

    if (!ok || !downstream.isEmpty()) {
        QString msg = ok ? QString() : m_array->errorMessage();
        if (!downstream.isEmpty()) {
            if (!msg.isEmpty()) msg += QStringLiteral("\n\n");
            msg += tr("下游特徵重建失敗（畫面維持舊形狀）：\n") + downstream.join(QLatin1Char('\n'));
        }
        QMessageBox::warning(this, tr("重建失敗"), msg);
    }
}

void ProfileArraySegmentTableDialog::updateInfo() {
    const QString tail = tr("　|　Thick／w／自訂欄位 = profile 內的同名參數，空白 = 不覆寫　|　按「套用」才會重建");
    if (!m_array) { m_info->setText(tr("陣列已不存在")); return; }
    m_info->setText(tr("%1 段（表格）／已建立 %2 段、%3 個測站")
                        .arg(m_table->rowCount())
                        .arg(m_array->segmentCount())
                        .arg(m_array->stationCount()) + tail);
}

// ── 編輯動作 ─────────────────────────────────────────────────────────────────

QString ProfileArraySegmentTableDialog::uniqueName(const QString& wanted, int ignoreRow) const {
    QSet<QString> used;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (r == ignoreRow) continue;
        if (const auto* it = m_table->item(r, ColName)) used.insert(it->text().trimmed());
    }
    QString base = wanted.trimmed().isEmpty() ? QStringLiteral("Seg") : wanted.trimmed();
    if (!used.contains(base)) return base;
    for (int i = 2; ; ++i) {
        const QString n = base + QLatin1Char('_') + QString::number(i);
        if (!used.contains(n)) return n;
    }
}

int ProfileArraySegmentTableDialog::appendDefaultRow(int baseRow) {
    const int r = m_table->rowCount();
    m_table->insertRow(r);

    double start = 0.0, end = 100.0, interval = 20.0;
    QString profile;
    const int base = (baseRow >= 0 && baseRow < r) ? baseRow : r - 1;
    if (base >= 0) {  // 沿用基準列：接在其終點之後、同長度、同 profile、同間距
        double ps = 0, pe = 0, pi = 20;
        parseNum(m_table->item(base, ColStart)->text(), ps);
        parseNum(m_table->item(base, ColEnd)->text(), pe);
        parseNum(m_table->item(base, ColInterval)->text(), pi);
        start = pe;
        end   = pe + ((pe > ps) ? (pe - ps) : 100.0);
        interval = pi > 0 ? pi : 20.0;
        profile = m_table->item(base, ColProfile)->text();
    } else {
        const auto prof = profiles();
        if (!prof.isEmpty()) profile = prof.first().first;
    }

    auto put = [&](int c, const QString& t) { m_table->setItem(r, c, new QTableWidgetItem(t)); };
    put(ColName,     uniqueName(QStringLiteral("Seg%1").arg(r + 1), r));
    put(ColStart,    fmt(start));
    put(ColEnd,      fmt(end));
    put(ColProfile,  profile);
    put(ColThick,    QString());
    put(ColW,        QString());
    put(ColInterval, fmt(interval));
    for (int c = ColFixedCount; c < m_table->columnCount(); ++c) put(c, QString());
    refreshAlignmentCell(r);
    return r;
}

void ProfileArraySegmentTableDialog::refreshAlignmentCell(int r)
{
    if (!m_table || r < 0 || r >= m_table->rowCount() || ColAlignment >= m_table->columnCount())
        return;

    double s = 0, e = 0;
    const auto* is = m_table->item(r, ColStart);
    const auto* ie = m_table->item(r, ColEnd);
    QString text;
    QString tip;
    if (m_array && m_array->trackCenterLine() && is && ie
        && parseNum(is->text(), s) && parseNum(ie->text(), e)) {
        const auto sa = m_array->alignmentsForRange(s, e);
        text = sa.names.join(QStringLiteral(" → "));
        tip  = tr("本段（%1 ~ %2 m）依賴的線形。\n超出主線形里程範圍時，會接續端點相連（E,N,Z 很接近）的線形。")
                   .arg(s, 0, 'f', 3).arg(e, 0, 'f', 3);
        if (sa.outOfRange) {
            text += tr("  ⚠ 超出範圍（無相連線形，將外推）");
            tip  += tr("\n⚠ 部分里程既不在主線形範圍內，也找不到端點相連的線形。");
        }
    } else {
        text = QStringLiteral("—");
    }

    auto* it = m_table->item(r, ColAlignment);
    if (!it) { it = new QTableWidgetItem; m_table->setItem(r, ColAlignment, it); }
    it->setText(text);
    it->setToolTip(tip);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);        // 唯讀：由起訖里程推得
    it->setForeground(text.contains(QStringLiteral("⚠")) ? QBrush(QColor(0xC0, 0x39, 0x2B))
                                                         : QBrush(QColor(0x55, 0x55, 0x55)));
}

void ProfileArraySegmentTableDialog::addRow() {
    // 新列接在「目前列」之後；沒有目前列就加在最後。
    const int cur = m_table->currentRow();
    const int r = appendDefaultRow(cur);
    if (cur >= 0 && cur < r) {
        // 把新列（目前在最後）搬到 cur+1：逐欄交換文字。
        QStringList row;
        for (int c = 0; c < m_table->columnCount(); ++c)
            row << m_table->item(r, c)->text();
        m_table->removeRow(r);
        m_table->insertRow(cur + 1);
        for (int c = 0; c < m_table->columnCount(); ++c)
            m_table->setItem(cur + 1, c, new QTableWidgetItem(row[c]));
        refreshAlignmentCell(cur + 1);      // 搬移時重建了儲存格 → 還原唯讀的「線形」欄
        m_table->setCurrentCell(cur + 1, ColName);
    } else {
        m_table->setCurrentCell(r, ColName);
    }
    updateInfo();
}

void ProfileArraySegmentTableDialog::deleteRows() {
    QSet<int> rows;
    for (const QModelIndex& i : m_table->selectionModel()->selectedRows()) rows.insert(i.row());
    if (rows.isEmpty())
        for (const QModelIndex& i : m_table->selectionModel()->selectedIndexes()) rows.insert(i.row());
    if (rows.isEmpty() && m_table->currentRow() >= 0) rows.insert(m_table->currentRow());
    if (rows.isEmpty()) return;

    QList<int> sorted = rows.values();
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int r : sorted) m_table->removeRow(r);
    updateInfo();
}

int ProfileArraySegmentTableDialog::addExtraColumn(const QString& name) {
    const int c = m_table->columnCount();
    m_table->insertColumn(c);
    m_table->setHorizontalHeaderItem(c, new QTableWidgetItem(name));
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->setItem(r, c, new QTableWidgetItem(QString()));
    return c;
}

void ProfileArraySegmentTableDialog::addColumn() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("新增欄位"),
        tr("欄名 = profile 草圖內的參數名（例如 H、cant、d9）。\n"
           "有填值的段會以該值覆寫該參數；空白 = 不覆寫。"),
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    if (!isValidParamName(name)) {
        QMessageBox::warning(this, tr("欄名無效"),
            tr("參數名只能包含英文字母、數字與底線，且不能以數字開頭。"));
        return;
    }
    if (fixedColumnOf(name) >= 0) {
        QMessageBox::warning(this, tr("欄名重複"), tr("「%1」已是固定欄位。").arg(name));
        return;
    }
    for (int c = ColFixedCount; c < m_table->columnCount(); ++c) {
        if (m_table->horizontalHeaderItem(c)->text() == name) {
            QMessageBox::warning(this, tr("欄名重複"), tr("已經有「%1」這一欄。").arg(name));
            return;
        }
    }
    const int c = addExtraColumn(name);
    m_table->resizeColumnToContents(c);
    m_table->setCurrentCell(std::max(0, m_table->currentRow()), c);
}

void ProfileArraySegmentTableDialog::clearCells() {
    // 只清「可以空白」的欄：Thick、w、自訂欄位。其餘欄（名稱／里程／profile／間距）不能空白，略過。
    for (const QModelIndex& i : m_table->selectionModel()->selectedIndexes()) {
        const int c = i.column();
        if (c == ColThick || c == ColW || c >= ColFixedCount) {
            if (auto* it = m_table->item(i.row(), c)) it->setText(QString());
        }
    }
}

void ProfileArraySegmentTableDialog::clearAll() {
    if (m_table->rowCount() == 0) return;
    const auto btn = QMessageBox::question(
        this, tr("全部清除"),
        tr("要刪除表格中所有 %1 列嗎？（按「套用」前不會影響陣列，可用「還原」取消）")
            .arg(m_table->rowCount()));
    if (btn != QMessageBox::Yes) return;
    m_table->setRowCount(0);
    updateInfo();
}

void ProfileArraySegmentTableDialog::copySelection() {
    QItemSelectionModel* sm = m_table->selectionModel();
    QStringList lines;
    auto cellText = [this](int r, int c) {
        const auto* it = m_table->item(r, c);
        return oneLine(it ? it->text() : QString());
    };

    const QModelIndexList fullRows = sm->selectedRows();
    if (!fullRows.isEmpty()) {
        // 整列：欄標題 + 所有欄 → 貼上時會被當成「新增列」
        QList<int> rows;
        for (const QModelIndex& i : fullRows) rows << i.row();
        std::sort(rows.begin(), rows.end());
        QStringList head;
        for (int c = 0; c < m_table->columnCount(); ++c)
            head << oneLine(m_table->horizontalHeaderItem(c)->text());
        lines << head.join(QLatin1Char('\t'));
        for (int r : rows) {
            QStringList cells;
            for (int c = 0; c < m_table->columnCount(); ++c) cells << cellText(r, c);
            lines << cells.join(QLatin1Char('\t'));
        }
    } else {
        QModelIndexList idx = sm->selectedIndexes();
        if (idx.isEmpty() && m_table->currentIndex().isValid()) idx << m_table->currentIndex();
        if (idx.isEmpty()) return;
        int r0 = idx.first().row(), r1 = r0, c0 = idx.first().column(), c1 = c0;
        QSet<QPair<int,int>> sel;
        for (const QModelIndex& i : idx) {
            r0 = std::min(r0, i.row());    r1 = std::max(r1, i.row());
            c0 = std::min(c0, i.column()); c1 = std::max(c1, i.column());
            sel.insert({ i.row(), i.column() });
        }
        for (int r = r0; r <= r1; ++r) {
            QStringList cells;
            for (int c = c0; c <= c1; ++c)
                cells << (sel.contains({ r, c }) ? cellText(r, c) : QString());
            lines << cells.join(QLatin1Char('\t'));
        }
    }
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

void ProfileArraySegmentTableDialog::paste() {
    QString text = QApplication::clipboard()->text();
    if (text.trimmed().isEmpty()) return;

    QStringList rawLines = text.split(QLatin1Char('\n'));
    for (QString& l : rawLines) if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
    while (!rawLines.isEmpty() && rawLines.last().isEmpty()) rawLines.removeLast();
    if (rawLines.isEmpty()) return;

    QList<QStringList> grid;
    for (const QString& l : rawLines) grid << l.split(QLatin1Char('\t'));

    // ── 第一列是欄標題？（至少認得一個固定欄名，且沒有任何儲存格是數字）──
    bool hasHeader = false;
    {
        int known = 0;
        bool anyNumber = false;
        for (const QString& h : grid.first()) {
            double d;
            if (!h.trimmed().isEmpty() && parseNum(h, d)) anyNumber = true;
            if (fixedColumnOf(h) >= 0) ++known;
        }
        hasHeader = known >= 1 && !anyNumber;
    }

    if (hasHeader) {
        // 欄標題 → 表格欄；沒有的自訂欄名（合法參數名）自動新增為欄位。
        QVector<int> map;
        for (const QString& hRaw : grid.first()) {
            const QString h = hRaw.trimmed();
            int fc = fixedColumnOf(h);
            if (fc == ColAlignment) { map << -1; continue; }     // 唯讀欄，不可貼入
            if (fc >= 0) { map << fc; continue; }
            int found = -1;
            for (int c = ColFixedCount; c < m_table->columnCount(); ++c)
                if (m_table->horizontalHeaderItem(c)->text() == h) { found = c; break; }
            if (found < 0 && isValidParamName(h)) found = addExtraColumn(h);
            map << found;     // -1 = 無法對應，忽略這欄
        }
        for (int i = 1; i < grid.size(); ++i) {
            const int r = appendDefaultRow();        // 先填預設值，貼上的非空白儲存格再覆寫
            for (int j = 0; j < grid[i].size() && j < map.size(); ++j) {
                const int c = map[j];
                const QString v = grid[i][j].trimmed();
                if (c < 0) continue;
                // 必填欄貼上空白 → 保留預設；選填欄（Thick／w／自訂）貼上空白 → 照樣空白
                if (v.isEmpty() && c != ColThick && c != ColW && c < ColFixedCount) continue;
                m_table->item(r, c)->setText(v);
            }
            m_table->item(r, ColName)->setText(uniqueName(m_table->item(r, ColName)->text(), r));
        }
        m_table->resizeColumnsToContents();
        updateInfo();
        return;
    }

    // ── 無欄標題：從目前儲存格起覆寫；超出列數就新增列，超出欄數的部分忽略 ──
    int r0 = std::max(0, m_table->currentRow());
    int c0 = std::max(0, m_table->currentColumn());
    if (m_table->columnCount() == 0) return;

    // 單一值貼到多個選取儲存格 → 全部填入（同試算表）
    if (grid.size() == 1 && grid[0].size() == 1) {
        const QModelIndexList idx = m_table->selectionModel()->selectedIndexes();
        if (idx.size() > 1) {
            for (const QModelIndex& i : idx)
                if (i.column() != ColAlignment)
                    if (auto* it = m_table->item(i.row(), i.column())) it->setText(grid[0][0].trimmed());
            return;
        }
    }

    for (int i = 0; i < grid.size(); ++i) {
        int r = r0 + i;
        while (r >= m_table->rowCount()) appendDefaultRow();
        for (int j = 0; j < grid[i].size(); ++j) {
            const int c = c0 + j;
            if (c >= m_table->columnCount()) break;
            if (c == ColAlignment) continue;                      // 唯讀欄，不可貼入
            m_table->item(r, c)->setText(grid[i][j].trimmed());
        }
    }
    updateInfo();
}

void ProfileArraySegmentTableDialog::showStations() {
    if (!m_array) return;
    auto* dlg = new ProfileArrayStationTableDialog(m_array, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

} // namespace ui
} // namespace aicad
