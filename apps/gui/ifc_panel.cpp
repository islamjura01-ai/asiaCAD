#include "ifc_panel.h"

#include <asiacad/core/error.h>
#include <asiacad/io/xlsx.h>

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <format>
#include <map>

namespace asiacad::gui {

QString ifcElementName(const i18n::Localizer& loc, const bim::IfcElement& e) {
    if (!e.name.empty()) return QString::fromStdString(e.name);
    return QString::fromStdString(loc.tr(std::string("ifc.cat.") + bim::ifcCategory(e.type)));
}

IfcPanel::IfcPanel(const i18n::Localizer& loc, std::vector<IfcLoaded>& models, QWidget* parent)
    : QDialog(parent), loc_(loc), models_(models) {
    setObjectName("ifcPanel");
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(860, 560);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);
    title_ = new QLabel(this);
    title_->setObjectName("dialogTitle");
    root->addWidget(title_);

    auto* split = new QSplitter(Qt::Horizontal, this);
    // chap: modellar va qavatlar (belgilash — ko'rinish)
    tree_ = new QTreeWidget(split);
    tree_->setObjectName("ifcTree");
    tree_->setColumnCount(2);
    tree_->setRootIsDecorated(true);
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setMinimumWidth(260);
    // o'ng: ma'lumot, ogohlantirishlar, jamlama jadvali
    auto* right = new QWidget(split);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(4, 0, 0, 0);
    rv->setSpacing(6);
    info_ = new QLabel(right);
    info_->setWordWrap(true);
    info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rv->addWidget(info_);
    warn_ = new QLabel(right);
    warn_->setObjectName("muted");
    warn_->setWordWrap(true);
    rv->addWidget(warn_);
    sumTitle_ = new QLabel(right);
    sumTitle_->setObjectName("sectionTitle");
    rv->addWidget(sumTitle_);
    sum_ = new QTableWidget(right);
    sum_->setObjectName("ifcSummary");
    sum_->setColumnCount(4);
    sum_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sum_->setSelectionBehavior(QAbstractItemView::SelectRows);
    sum_->verticalHeader()->setVisible(false);
    sum_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c) sum_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    rv->addWidget(sum_, 1);
    split->addWidget(tree_);
    split->addWidget(right);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    root->addWidget(split, 1);

    auto* bar = new QHBoxLayout;
    add_ = new QPushButton(this);
    add_->setObjectName("ifcAdd");
    add_->setProperty("accent", true);
    remove_ = new QPushButton(this);
    remove_->setObjectName("ifcRemove");
    export_ = new QPushButton(this);
    export_->setObjectName("ifcExport");
    show3d_ = new QPushButton(this);
    show3d_->setObjectName("ifcShow3d");
    close_ = new QPushButton(this);
    bar->addWidget(add_);
    bar->addWidget(remove_);
    bar->addWidget(export_);
    bar->addStretch(1);
    bar->addWidget(show3d_);
    bar->addWidget(close_);
    root->addLayout(bar);

    connect(add_, &QPushButton::clicked, this, &IfcPanel::addRequested);
    connect(show3d_, &QPushButton::clicked, this, &IfcPanel::show3DRequested);
    connect(close_, &QPushButton::clicked, this, &QDialog::hide);
    connect(remove_, &QPushButton::clicked, this, [this] {
        const int i = current();
        if (i >= 0) emit removeRequested(i);
    });
    connect(export_, &QPushButton::clicked, this, [this] {
        const int i = current();
        if (i < 0) return;
        const QFileInfo fi(models_[static_cast<std::size_t>(i)].path);
        QString f = QFileDialog::getSaveFileName(this, t("gui.ifc.export"), fi.absolutePath() + "/" + fi.completeBaseName() + "_PTO.xlsx",
                                                 QStringLiteral("Excel (*.xlsx)"));
        if (f.isEmpty()) return;
        if (!f.endsWith(".xlsx", Qt::CaseInsensitive)) f += ".xlsx";
        if (exportXlsx(f)) QMessageBox::information(this, t("gui.ifc.export"), t("gui.ifc.exported", {QDir::toNativeSeparators(f)}));
        else QMessageBox::warning(this, t("gui.ifc.export"), t("xlsx.err.write", {f}));
    });
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { if (!filling_) fillDetails(); });
    connect(tree_, &QTreeWidget::itemChanged, this, &IfcPanel::onItemChanged);
    retranslate();
}

QString IfcPanel::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

void IfcPanel::retranslate() {
    setWindowTitle(t("gui.ifc.title"));
    title_->setText(t("gui.ifc.title"));
    tree_->setHeaderLabels({t("gui.ifc.col.model"), t("gui.ifc.col.count")});
    sumTitle_->setText(t("gui.ifc.summary"));
    sum_->setHorizontalHeaderLabels({t("gui.ifc.col.cat"), t("gui.ifc.col.count"), t("gui.ifc.col.volume"), t("gui.ifc.col.rebar")});
    add_->setText(t("gui.ifc.add"));
    remove_->setText(t("gui.ifc.remove"));
    export_->setText(t("gui.ifc.export"));
    show3d_->setText(t("gui.ifc.show3d"));
    close_->setText(t("gui.btn.close"));
    refresh();
}

QString IfcPanel::storeyLabel(const bim::IfcModel& m, int s) const {
    if (s < 0 || s >= static_cast<int>(m.storeys.size())) return t("gui.ifc.nostorey");
    const auto& st = m.storeys[static_cast<std::size_t>(s)];
    const QString name = st.name.empty() ? t("gui.ifc.storey", {QString::number(s + 1)}) : QString::fromStdString(st.name);
    return QStringLiteral("%1  (%2)").arg(name, QString::fromStdString(std::format("{:+.3f}", st.elevation)));
}

int IfcPanel::current() const {
    if (models_.empty()) return -1;
    QTreeWidgetItem* it = tree_->currentItem();
    if (!it) return 0;
    while (it->parent()) it = it->parent();
    const int i = tree_->indexOfTopLevelItem(it);
    return i >= 0 && i < static_cast<int>(models_.size()) ? i : 0;
}

void IfcPanel::setCurrent(int i) {
    if (i >= 0 && i < tree_->topLevelItemCount()) tree_->setCurrentItem(tree_->topLevelItem(i));
}

void IfcPanel::refresh() {
    const int keep = current();
    filling_ = true;
    tree_->clear();
    for (std::size_t i = 0; i < models_.size(); ++i) {
        const auto& L = models_[i];
        const auto& m = *L.model;
        auto* top = new QTreeWidgetItem(tree_);
        top->setText(0, QFileInfo(L.path).fileName());
        top->setToolTip(0, QDir::toNativeSeparators(L.path));
        top->setText(1, QString::number(m.elements.size()));
        top->setFlags(top->flags() | Qt::ItemIsUserCheckable);
        top->setCheckState(0, L.visible ? Qt::Checked : Qt::Unchecked);
        top->setData(0, Qt::UserRole, -2);
        std::map<int, int> count;
        for (const auto& e : m.elements) ++count[e.storey];
        for (const auto& [s, n] : count) {
            auto* ch = new QTreeWidgetItem(top);
            ch->setText(0, storeyLabel(m, s));
            ch->setText(1, QString::number(n));
            ch->setFlags(ch->flags() | Qt::ItemIsUserCheckable);
            ch->setCheckState(0, L.hiddenStoreys.count(s) ? Qt::Unchecked : Qt::Checked);
            ch->setData(0, Qt::UserRole, s);
        }
        top->setExpanded(true);
    }
    filling_ = false;
    if (tree_->topLevelItemCount() > 0) setCurrent(std::clamp(keep, 0, tree_->topLevelItemCount() - 1));
    remove_->setEnabled(!models_.empty());
    export_->setEnabled(!models_.empty());
    show3d_->setEnabled(!models_.empty());
    fillDetails();
}

void IfcPanel::onItemChanged(QTreeWidgetItem* it, int col) {
    if (filling_ || col != 0) return;
    QTreeWidgetItem* top = it;
    while (top->parent()) top = top->parent();
    const int i = tree_->indexOfTopLevelItem(top);
    if (i < 0 || i >= static_cast<int>(models_.size())) return;
    auto& L = models_[static_cast<std::size_t>(i)];
    const bool on = it->checkState(0) == Qt::Checked;
    if (it == top) L.visible = on;
    else {
        const int s = it->data(0, Qt::UserRole).toInt();
        if (on) L.hiddenStoreys.erase(s);
        else L.hiddenStoreys.insert(s);
    }
    emit visibilityChanged();
}

void IfcPanel::fillDetails() {
    const int i = current();
    sum_->setRowCount(0);
    if (i < 0) {
        info_->setText(t("gui.ifc.empty"));
        warn_->clear();
        return;
    }
    const auto& m = *models_[static_cast<std::size_t>(i)].model;
    // qaysi qavat tanlangan bo'lsa — jadval shu qavat bo'yicha
    int onlyStorey = -100;
    if (QTreeWidgetItem* it = tree_->currentItem(); it && it->parent()) onlyStorey = it->data(0, Qt::UserRole).toInt();
    QStringList lines;
    lines << t("gui.ifc.i.schema", {QString::fromStdString(m.schema), m.application.empty() ? QStringLiteral("—") : QString::fromStdString(m.application)});
    if (!m.project.empty() || !m.building.empty())
        lines << t("gui.ifc.i.project", {m.project.empty() ? QStringLiteral("—") : QString::fromStdString(m.project),
                                         m.building.empty() ? QStringLiteral("—") : QString::fromStdString(m.building)});
    lines << t("gui.ifc.i.elements", {QString::number(m.elements.size()), QString::number(m.storeys.size()), QString::number(m.triangleCount())});
    if (m.box.valid())
        lines << t("gui.ifc.i.extent", {QString::number(m.box.max.x - m.box.min.x, 'f', 2), QString::number(m.box.max.y - m.box.min.y, 'f', 2),
                                        QString::number(m.box.max.z - m.box.min.z, 'f', 2)});
    if (onlyStorey > -100) lines << t("gui.ifc.i.filtered", {storeyLabel(m, onlyStorey)});
    info_->setText(lines.join('\n'));
    QStringList w;
    for (const auto& k : m.warnings)
        w << QStringLiteral("⚠ ") + t(k.c_str(), {QString::number(m.openings - m.openingsCut), QString::number(m.openings), QString::number(m.skipped)});
    warn_->setText(w.join('\n'));
    warn_->setVisible(!w.isEmpty());
    // jamlama
    bim::IfcModel sub;
    const bim::IfcModel* src = &m;
    if (onlyStorey > -100) {
        for (const auto& e : m.elements)
            if (e.storey == onlyStorey) {
                bim::IfcElement c;
                c.type = e.type;
                c.volume = e.volume;
                c.barLength = e.barLength;
                c.barMass = e.barMass;
                sub.elements.push_back(std::move(c));
            }
        src = &sub;
    }
    const auto rows = bim::ifcSummary(*src);
    sum_->setRowCount(static_cast<int>(rows.size()) + 1);
    double vol = 0, kg = 0;
    int n = 0, r = 0;
    auto cell = [&](int row, int col, const QString& s, bool num) {
        auto* it = new QTableWidgetItem(s);
        if (num) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sum_->setItem(row, col, it);
        return it;
    };
    for (const auto& row : rows) {
        cell(r, 0, QString::fromStdString(loc_.tr("ifc.cat." + row.category)), false);
        cell(r, 1, QString::number(row.count), true);
        cell(r, 2, row.category == "SPACE" || row.category == "REBAR" || row.volume <= 0 ? QStringLiteral("—") : QString::number(row.volume, 'f', 3), true);
        cell(r, 3, row.barMass > 0 ? QString::number(row.barMass, 'f', 1) : QStringLiteral("—"), true);
        n += row.count;
        if (row.category != "SPACE" && row.category != "SITE" && row.category != "REBAR") vol += row.volume;
        kg += row.barMass;
        ++r;
    }
    QFont bold = sum_->font();
    bold.setBold(true);
    for (auto* it : {cell(r, 0, t("gui.ifc.total"), false), cell(r, 1, QString::number(n), true), cell(r, 2, QString::number(vol, 'f', 3), true),
                     cell(r, 3, kg > 0 ? QString::number(kg, 'f', 1) : QStringLiteral("—"), true)})
        it->setFont(bold);
}

bool IfcPanel::exportXlsx(const QString& path) const {
    const int i = current();
    if (i < 0) return false;
    const auto& m = *models_[static_cast<std::size_t>(i)].model;
    using xlsx::Cell;
    xlsx::Workbook wb;
    auto tr = [&](const char* k) { return loc_.tr(k); };
    // 1. Jamlama
    auto& s1 = wb.add(tr("gui.ifc.x.summary"));
    s1.widths = {28, 10, 14, 14, 14};
    s1.add({Cell::s(tr("gui.ifc.col.cat"), 1), Cell::s(tr("gui.ifc.col.count"), 1), Cell::s(tr("gui.ifc.col.volume"), 1),
            Cell::s(tr("gui.ifc.col.barlen"), 1), Cell::s(tr("gui.ifc.col.rebar"), 1)});
    const auto rows = bim::ifcSummary(m);
    for (const auto& r : rows)
        s1.add({Cell::s(loc_.tr("ifc.cat." + r.category)), Cell::n(r.count, 5), Cell::n(r.category == "SPACE" ? 0 : r.volume, 3), Cell::n(r.barLength, 2),
                Cell::n(r.barMass, 2)});
    const int last = static_cast<int>(rows.size()) + 1;
    s1.add({Cell::s(tr("gui.ifc.total"), 4), Cell::f(std::format("SUM(B2:B{})", last), 0, 4), Cell::f(std::format("SUM(C2:C{})", last), 0, 4),
            Cell::f(std::format("SUM(D2:D{})", last), 0, 4), Cell::f(std::format("SUM(E2:E{})", last), 0, 4)});
    // 2. Qavatlar bo'yicha
    auto& s2 = wb.add(tr("gui.ifc.x.storeys"));
    s2.widths = {28, 12, 28, 10, 14};
    s2.add({Cell::s(tr("gui.ifc.col.storey"), 1), Cell::s(tr("gui.ifc.col.elev"), 1), Cell::s(tr("gui.ifc.col.cat"), 1), Cell::s(tr("gui.ifc.col.count"), 1),
            Cell::s(tr("gui.ifc.col.volume"), 1)});
    std::map<std::pair<int, std::string>, std::pair<int, double>> bySt;
    for (const auto& e : m.elements) {
        auto& v = bySt[{e.storey, bim::ifcCategory(e.type)}];
        ++v.first;
        v.second += e.volume;
    }
    for (const auto& [k, v] : bySt) {
        const bool has = k.first >= 0 && k.first < static_cast<int>(m.storeys.size());
        const std::string nm = has ? m.storeys[static_cast<std::size_t>(k.first)].name : tr("gui.ifc.nostorey");
        s2.add({Cell::s(nm), has ? Cell::n(m.storeys[static_cast<std::size_t>(k.first)].elevation, 3) : Cell::s(""), Cell::s(loc_.tr("ifc.cat." + k.second)),
                Cell::n(v.first, 5), Cell::n(k.second == "SPACE" ? 0 : v.second, 3)});
    }
    // 3. Elementlar
    auto& s3 = wb.add(tr("gui.ifc.x.elements"));
    s3.widths = {34, 22, 20, 22, 28, 12, 12, 26};
    s3.add({Cell::s(tr("gui.ifc.col.name"), 1), Cell::s(tr("gui.ifc.col.cat"), 1), Cell::s(tr("gui.ifc.col.type"), 1), Cell::s(tr("gui.ifc.col.storey"), 1),
            Cell::s(tr("gui.ifc.col.material"), 1), Cell::s(tr("gui.ifc.col.volume"), 1), Cell::s(tr("gui.ifc.col.rebar"), 1), Cell::s("GUID", 1)});
    for (const auto& e : m.elements) {
        const bool has = e.storey >= 0 && e.storey < static_cast<int>(m.storeys.size());
        s3.add({Cell::s(ifcElementName(loc_, e).toStdString()), Cell::s(loc_.tr(std::string("ifc.cat.") + bim::ifcCategory(e.type))), Cell::s(e.type),
                Cell::s(has ? m.storeys[static_cast<std::size_t>(e.storey)].name : std::string()), Cell::s(e.material), Cell::n(e.volume, 3),
                Cell::n(e.barMass, 2), Cell::s(e.guid)});
    }
    try {
        xlsx::save(wb, path.toStdString());
    } catch (const Error&) {
        return false;
    }
    return true;
}

} // namespace asiacad::gui
