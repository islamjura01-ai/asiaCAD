#include "ifc_panel.h"
#include "theme.h"

#include <asiacad/core/error.h>
#include <asiacad/io/xlsx.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QTabWidget>
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

bool ifcIsStakeoutElement(const bim::IfcElement& e) {
    const std::string c = bim::ifcCategory(e.type);
    return c == "COLUMN" || c == "PILE" || c == "FOOTING";
}

QString ifcElementName(const i18n::Localizer& loc, const bim::IfcElement& e) {
    if (!e.name.empty()) return QString::fromStdString(e.name);
    return QString::fromStdString(loc.tr(std::string("ifc.cat.") + bim::ifcCategory(e.type)));
}

IfcPanel::IfcPanel(const i18n::Localizer& loc, std::vector<IfcLoaded>& models, QWidget* parent)
    : QDialog(parent), loc_(loc), models_(models) {
    setObjectName("ifcPanel");
    windowButtons(this);   // IFC palitrasi — suzuvchi oyna (3D bilan yonma-yon)
    resize(1020, 640);
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
    // o'ng: varaqlar — Tarkib | Elementlar | Reja va razbivka
    tabs_ = new QTabWidget(split);
    tabs_->setObjectName("ifcTabs");
    auto* right = new QWidget(tabs_);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(8, 8, 8, 8);
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
    georef_ = new QCheckBox(right);
    georef_->setObjectName("ifcGeoref");
    rv->addWidget(georef_);
    tabs_->addTab(right, QString());

    // ---- Elementlar: qidiruv, tur filtri, jadval (ikki marta bosish — 3D da topish)
    auto* pe = new QWidget(tabs_);
    auto* ev = new QVBoxLayout(pe);
    ev->setContentsMargins(8, 8, 8, 8);
    ev->setSpacing(6);
    auto* er = new QHBoxLayout;
    search_ = new QLineEdit(pe);
    search_->setObjectName("ifcSearch");
    search_->setClearButtonEnabled(true);
    catFilter_ = new QComboBox(pe);
    catFilter_->setObjectName("ifcCatFilter");
    catFilter_->setMinimumWidth(170);
    er->addWidget(search_, 1);
    er->addWidget(catFilter_);
    ev->addLayout(er);
    elems_ = new QTableWidget(pe);
    elems_->setObjectName("ifcElements");
    elems_->setColumnCount(8);
    elems_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    elems_->setSelectionBehavior(QAbstractItemView::SelectRows);
    elems_->setSelectionMode(QAbstractItemView::SingleSelection);
    elems_->verticalHeader()->setVisible(false);
    elems_->verticalHeader()->setDefaultSectionSize(24);
    elems_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    elems_->horizontalHeader()->setStretchLastSection(false);
    elems_->setSortingEnabled(true);
    ev->addWidget(elems_, 1);
    elemCount_ = new QLabel(pe);
    elemCount_->setObjectName("muted");
    ev->addWidget(elemCount_);
    tabs_->addTab(pe, QString());

    // ---- Reja va razbivka: qavat kesimi, o'qlar, nuqtalar -> chizma; razbivka jadvali
    auto* pp = new QWidget(tabs_);
    auto* pv = new QVBoxLayout(pp);
    pv->setContentsMargins(10, 10, 10, 10);
    pv->setSpacing(8);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    planStorey_ = new QComboBox(pp);
    planStorey_->setObjectName("ifcPlanStorey");
    cutH_ = new QDoubleSpinBox(pp);
    cutH_->setObjectName("ifcCutH");
    cutH_->setRange(-50, 500);
    cutH_->setDecimals(3);
    cutH_->setSingleStep(0.1);
    cutH_->setValue(1.0);
    cutH_->setSuffix(QStringLiteral(" m"));
    lblStorey_ = new QLabel(pp);
    lblCut_ = new QLabel(pp);
    form->addRow(lblStorey_, planStorey_);
    form->addRow(lblCut_, cutH_);
    planZ_ = new QLabel(pp);
    planZ_->setObjectName("muted");
    form->addRow(QString(), planZ_);
    pv->addLayout(form);
    chkCut_ = new QCheckBox(pp);
    chkAxes_ = new QCheckBox(pp);
    chkPoints_ = new QCheckBox(pp);
    chkLabels_ = new QCheckBox(pp);
    for (auto* c : {chkCut_, chkAxes_, chkPoints_, chkLabels_}) {
        c->setChecked(true);
        pv->addWidget(c);
    }
    chkLabels_->setContentsMargins(22, 0, 0, 0);
    planHint_ = new QLabel(pp);
    planHint_->setObjectName("muted");
    planHint_->setWordWrap(true);
    pv->addWidget(planHint_);
    pv->addStretch(1);
    auto* pb = new QHBoxLayout;
    planBtn_ = new QPushButton(pp);
    planBtn_->setObjectName("ifcPlanBtn");
    planBtn_->setProperty("accent", true);
    stakeBtn_ = new QPushButton(pp);
    stakeBtn_->setObjectName("ifcStakeBtn");
    pb->addWidget(planBtn_);
    pb->addWidget(stakeBtn_);
    pb->addStretch(1);
    pv->addLayout(pb);
    tabs_->addTab(pp, QString());

    // ---- ShNQ tekshiruvi: qoidalar jadvali bo'yicha nomuvofiqliklar, 3D da ranglash, dalolatnoma
    auto* pn = new QWidget(tabs_);
    auto* nv = new QVBoxLayout(pn);
    nv->setContentsMargins(8, 8, 8, 8);
    nv->setSpacing(6);
    auto* nr = new QHBoxLayout;
    normRun_ = new QPushButton(pn);
    normRun_->setObjectName("ifcNormRun");
    normRun_->setProperty("accent", true);
    normRulesBtn_ = new QPushButton(pn);
    normRulesBtn_->setObjectName("ifcNormRules");
    normExport_ = new QPushButton(pn);
    normExport_->setObjectName("ifcNormExport");
    nr->addWidget(normRun_);
    nr->addWidget(normRulesBtn_);
    nr->addWidget(normExport_);
    nr->addStretch(1);
    nv->addLayout(nr);
    normSummary_ = new QLabel(pn);
    normSummary_->setWordWrap(true);
    normSummary_->setTextFormat(Qt::RichText);
    nv->addWidget(normSummary_);
    auto* nf = new QHBoxLayout;
    normFilter_ = new QComboBox(pn);
    normFilter_->setObjectName("ifcNormFilter");
    normColor_ = new QCheckBox(pn);
    normColor_->setObjectName("ifcNormColor");
    normLabels_ = new QCheckBox(pn);
    normLabels_->setObjectName("ifcNormLabels");
    nf->addWidget(normFilter_);
    nf->addWidget(normColor_);
    nf->addWidget(normLabels_);
    nf->addStretch(1);
    nv->addLayout(nf);
    normTable_ = new QTableWidget(pn);
    normTable_->setObjectName("ifcNormTable");
    normTable_->setColumnCount(7);
    normTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    normTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    normTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    normTable_->verticalHeader()->setVisible(false);
    normTable_->verticalHeader()->setDefaultSectionSize(24);
    normTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    normTable_->horizontalHeader()->setStretchLastSection(true);
    nv->addWidget(normTable_, 1);
    tabs_->addTab(pn, QString());

    split->addWidget(tree_);
    split->addWidget(tabs_);
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
    connect(search_, &QLineEdit::textChanged, this, [this] { fillElements(); });
    connect(catFilter_, &QComboBox::currentIndexChanged, this, [this] { if (!filling_) fillElements(); });
    auto activate = [this](int row) {
        const int i = current();
        if (i < 0 || row < 0) return;
        if (auto* it = elems_->item(row, 0)) emit elementActivated(i, it->data(Qt::UserRole).toString());
    };
    connect(elems_, &QTableWidget::cellDoubleClicked, this, [activate](int r, int) { activate(r); });
    connect(elems_, &QTableWidget::cellActivated, this, [activate](int r, int) { activate(r); });
    connect(planStorey_, &QComboBox::currentIndexChanged, this, [this] { updatePlanZ(); });
    connect(cutH_, &QDoubleSpinBox::valueChanged, this, [this] { updatePlanZ(); });
    connect(chkPoints_, &QCheckBox::toggled, chkLabels_, &QCheckBox::setEnabled);
    connect(planBtn_, &QPushButton::clicked, this, [this] {
        const int i = current();
        if (i >= 0) emit planRequested(i, planOptions());
    });
    connect(stakeBtn_, &QPushButton::clicked, this, [this] {
        const int i = current();
        if (i < 0) return;
        const QFileInfo fi(models_[static_cast<std::size_t>(i)].path);
        QString f = QFileDialog::getSaveFileName(this, t("gui.ifc.stake"), fi.absolutePath() + "/" + fi.completeBaseName() + "_razbivka.xlsx",
                                                 QStringLiteral("Excel (*.xlsx)"));
        if (f.isEmpty()) return;
        if (!f.endsWith(".xlsx", Qt::CaseInsensitive)) f += ".xlsx";
        if (exportStakeout(f)) QMessageBox::information(this, t("gui.ifc.stake"), t("gui.ifc.exported", {QDir::toNativeSeparators(f)}));
        else QMessageBox::warning(this, t("gui.ifc.stake"), t("xlsx.err.write", {f}));
    });
    connect(normRun_, &QPushButton::clicked, this, [this] {
        const int i = current();
        if (i >= 0) emit normCheckRequested(i);
    });
    connect(normRulesBtn_, &QPushButton::clicked, this, &IfcPanel::normRulesRequested);
    connect(normFilter_, &QComboBox::currentIndexChanged, this, [this] { if (!filling_) fillNorms(); });
    connect(normColor_, &QCheckBox::toggled, this, [this](bool on) {
        const int i = current();
        if (filling_ || i < 0) return;
        models_[static_cast<std::size_t>(i)].normColors = on;
        emit normViewChanged();
    });
    connect(normLabels_, &QCheckBox::toggled, this, [this](bool on) {
        const int i = current();
        if (filling_ || i < 0) return;
        models_[static_cast<std::size_t>(i)].normLabels = on;
        emit normViewChanged();
    });
    connect(normTable_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        const int i = current();
        if (i < 0 || r < 0) return;
        if (auto* it = normTable_->item(r, 1)) emit elementActivated(i, it->data(Qt::UserRole).toString());
    });
    connect(normExport_, &QPushButton::clicked, this, [this] {
        const int i = current();
        if (i < 0 || !models_[static_cast<std::size_t>(i)].norms) return;
        const QFileInfo fi(models_[static_cast<std::size_t>(i)].path);
        QString f = QFileDialog::getSaveFileName(this, t("gui.norm.export"), fi.absolutePath() + "/" + fi.completeBaseName() + "_ShNQ.xlsx",
                                                 QStringLiteral("Excel (*.xlsx)"));
        if (f.isEmpty()) return;
        if (!f.endsWith(".xlsx", Qt::CaseInsensitive)) f += ".xlsx";
        if (exportNorms(f)) QMessageBox::information(this, t("gui.norm.export"), t("gui.ifc.exported", {QDir::toNativeSeparators(f)}));
        else QMessageBox::warning(this, t("gui.norm.export"), t("xlsx.err.write", {f}));
    });
    connect(georef_, &QCheckBox::toggled, this, [this](bool on) {
        const int i = current();
        if (!filling_ && i >= 0 && models_[static_cast<std::size_t>(i)].georef != on) emit reloadRequested(i, on);
    });
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
    tabs_->setTabText(0, t("gui.ifc.tab.summary"));
    tabs_->setTabText(1, t("gui.ifc.tab.elements"));
    tabs_->setTabText(2, t("gui.ifc.tab.plan"));
    tabs_->setTabText(3, t("gui.norm.tab"));
    normRun_->setText(t("gui.norm.run"));
    normRulesBtn_->setText(t("gui.norm.rules"));
    normExport_->setText(t("gui.norm.export"));
    normColor_->setText(t("gui.norm.color"));
    normLabels_->setText(t("gui.norm.labels"));
    {
        const bool was = filling_;
        filling_ = true;
        const int k = std::max(0, normFilter_->currentIndex());
        normFilter_->clear();
        normFilter_->addItems({t("gui.norm.f.all"), t("gui.norm.f.errors"), t("gui.norm.f.warnings")});
        normFilter_->setCurrentIndex(k);
        filling_ = was;
    }
    normTable_->setHorizontalHeaderLabels({t("gui.norm.col.sev"), t("gui.ifc.col.name"), t("gui.norm.col.rule"), t("gui.norm.col.value"),
                                           t("gui.norm.col.limit"), t("gui.norm.col.ref"), t("gui.ifc.col.storey")});
    georef_->setText(t("gui.ifc.georef"));
    georef_->setToolTip(t("gui.ifc.georef.tip"));
    search_->setPlaceholderText(t("gui.ifc.search"));
    elems_->setHorizontalHeaderLabels({t("gui.ifc.col.name"), t("gui.ifc.col.cat"), t("gui.ifc.col.storey"), t("gui.ifc.col.material"),
                                       t("gui.ifc.col.volume"), QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
    lblStorey_->setText(t("gui.ifc.plan.storey"));
    lblCut_->setText(t("gui.ifc.plan.cut"));
    chkCut_->setText(t("gui.ifc.plan.chkcut"));
    chkAxes_->setText(t("gui.ifc.plan.chkaxes"));
    chkPoints_->setText(t("gui.ifc.plan.chkpoints"));
    chkLabels_->setText(t("gui.ifc.plan.chklabels"));
    planHint_->setText(t("gui.ifc.plan.hint"));
    planBtn_->setText(t("gui.ifc.plan.btn"));
    stakeBtn_->setText(t("gui.ifc.stake"));
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

void IfcPanel::fillElements() {
    const int i = current();
    elems_->setSortingEnabled(false);
    elems_->setRowCount(0);
    if (i < 0) { elemCount_->clear(); return; }
    const auto& m = *models_[static_cast<std::size_t>(i)].model;
    const QString q = search_->text().trimmed();
    const QString cat = catFilter_->currentData().toString();
    constexpr int kMax = 5000;   // katta modelda jadval tez bo'lsin — qidiruv bilan toraytiriladi
    int found = 0, shown = 0;
    elems_->setRowCount(std::min<int>(kMax, static_cast<int>(m.elements.size())));
    for (const auto& e : m.elements) {
        const std::string c = bim::ifcCategory(e.type);
        if (!cat.isEmpty() && cat.toStdString() != c) continue;
        const QString name = ifcElementName(loc_, e);
        const QString catName = QString::fromStdString(loc_.tr("ifc.cat." + c));
        const QString storey = e.storey >= 0 && e.storey < static_cast<int>(m.storeys.size())
                                   ? QString::fromStdString(m.storeys[static_cast<std::size_t>(e.storey)].name) : QString();
        const QString mat = QString::fromStdString(e.material);
        if (!q.isEmpty()) {
            const QString hay = name + ' ' + catName + ' ' + QString::fromStdString(e.type) + ' ' + mat + ' ' + QString::fromStdString(e.tag) + ' ' +
                                QString::fromStdString(e.guid) + ' ' + storey + ' ' + QString::fromStdString(e.objectType);
            if (!hay.contains(q, Qt::CaseInsensitive)) continue;
        }
        ++found;
        if (shown >= kMax) continue;
        auto* n = new QTableWidgetItem(name);
        n->setData(Qt::UserRole, QString::fromStdString(e.guid));
        n->setToolTip(QString::fromStdString(e.type + (e.tag.empty() ? std::string() : "  ·  Tag: " + e.tag) + "\nGUID: " + e.guid));
        elems_->setItem(shown, 0, n);
        elems_->setItem(shown, 1, new QTableWidgetItem(catName));
        elems_->setItem(shown, 2, new QTableWidgetItem(storey));
        elems_->setItem(shown, 3, new QTableWidgetItem(mat));
        auto num = [&](int col, double v, int dec, bool show) {
            auto* it = new QTableWidgetItem;
            if (show) it->setData(Qt::DisplayRole, QString::number(v, 'f', dec).toDouble());
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            elems_->setItem(shown, col, it);
        };
        num(4, e.volume, 3, e.volume > 0 && c != "SPACE");
        num(5, e.origin.x, 3, true);
        num(6, e.origin.y, 3, true);
        num(7, e.box.valid() ? e.box.min.z : e.origin.z, 3, true);
        ++shown;
    }
    elems_->setRowCount(shown);
    elems_->setSortingEnabled(true);
    elems_->resizeColumnsToContents();
    if (elems_->columnWidth(0) > 260) elems_->setColumnWidth(0, 260);
    if (elems_->columnWidth(3) > 200) elems_->setColumnWidth(3, 200);
    elemCount_->setText(found > shown ? t("gui.ifc.found.more", {QString::number(found), QString::number(shown)})
                                      : t("gui.ifc.found", {QString::number(found)}));
}

void IfcPanel::fillPlan() {
    const int i = current();
    const bool have = i >= 0;
    planBtn_->setEnabled(have);
    stakeBtn_->setEnabled(have);
    const int keep = planStorey_->currentData().isValid() ? planStorey_->currentData().toInt() : -100;
    const bool was = filling_;
    filling_ = true;
    planStorey_->clear();
    if (have) {
        const auto& m = *models_[static_cast<std::size_t>(i)].model;
        for (std::size_t s = 0; s < m.storeys.size(); ++s) planStorey_->addItem(storeyLabel(m, static_cast<int>(s)), static_cast<int>(s));
        planStorey_->addItem(t("gui.ifc.plan.absolute"), -1);
        const int k = planStorey_->findData(keep);
        planStorey_->setCurrentIndex(k >= 0 ? k : 0);
        chkAxes_->setEnabled(!m.axes.empty());
        chkAxes_->setToolTip(m.axes.empty() ? t("gui.ifc.plan.noaxes") : t("gui.ifc.plan.axes", {QString::number(m.axes.size())}));
        int stake = 0;
        for (const auto& e : m.elements) stake += ifcIsStakeoutElement(e) ? 1 : 0;
        chkPoints_->setToolTip(t("gui.ifc.plan.points", {QString::number(stake)}));
    }
    filling_ = was;
    updatePlanZ();
}

void IfcPanel::updatePlanZ() {
    const int i = current();
    if (i < 0) { planZ_->clear(); return; }
    const auto o = planOptions();
    planZ_->setText(t("gui.ifc.plan.z", {QString::fromStdString(std::format("{:+.3f}", o.z))}));
    lblCut_->setText(o.storey >= 0 ? t("gui.ifc.plan.cut") : t("gui.ifc.plan.cutabs"));
}

IfcPlanOptions IfcPanel::planOptions() const {
    IfcPlanOptions o;
    const int i = current();
    o.storey = planStorey_->currentData().isValid() ? planStorey_->currentData().toInt() : -1;
    o.z = cutH_->value();
    if (i >= 0 && o.storey >= 0) {
        const auto& m = *models_[static_cast<std::size_t>(i)].model;
        if (o.storey < static_cast<int>(m.storeys.size())) o.z += m.storeys[static_cast<std::size_t>(o.storey)].elevation;
    }
    o.cut = chkCut_->isChecked();
    o.axes = chkAxes_->isChecked() && chkAxes_->isEnabled();
    o.points = chkPoints_->isChecked();
    o.labels = chkLabels_->isChecked() && o.points;
    return o;
}

void IfcPanel::showNormTab() { tabs_->setCurrentIndex(3); }

void IfcPanel::fillNorms() {
    const int i = current();
    normTable_->setRowCount(0);
    const bool have = i >= 0;
    normRun_->setEnabled(have);
    const IfcLoaded* L = have ? &models_[static_cast<std::size_t>(i)] : nullptr;
    const bool rep = L && L->norms;
    normExport_->setEnabled(rep);
    normColor_->setEnabled(rep);
    normLabels_->setEnabled(rep);
    {
        const bool was = filling_;
        filling_ = true;
        normColor_->setChecked(L ? L->normColors : true);
        normLabels_->setChecked(L ? L->normLabels : true);
        filling_ = was;
    }
    if (!rep) {
        normSummary_->setText(have ? t("gui.norm.notrun") : QString());
        return;
    }
    const auto& R = *L->norms;
    const auto& rules = L->normRules;
    const auto& m = *L->model;
    int enabled = 0, verified = 0;
    for (const auto& r : rules) {
        enabled += r.enabled;
        verified += r.enabled && r.verified;
    }
    QString sum = t("gui.norm.summary", {QString::number(R.errors), QString::number(R.warnings), QString::number(R.subjects), QString::number(enabled),
                                         QString::number(R.seconds, 'f', R.seconds < 0.1 ? 3 : 2)});
    if (R.errors == 0 && R.warnings == 0) sum = QStringLiteral("<span style='color:#3fb950'>✔ </span>") + sum;
    sum += "<br><span style='color:#8b96b0'>" + t("gui.norm.legend", {QString::number(verified), QString::number(enabled - verified)}) + "</span>";
    for (const auto& e : R.ruleErrors) sum += "<br><span style='color:#E5484D'>" + t("gui.norm.ruleerr", {QString::fromStdString(e).toHtmlEscaped()}) + "</span>";
    normSummary_->setText(sum);
    const int filter = normFilter_->currentIndex();
    const std::string lang = loc_.language();
    int r = 0;
    normTable_->setSortingEnabled(false);
    normTable_->setRowCount(static_cast<int>(std::min<std::size_t>(R.items.size(), 5000)));
    for (const auto& v : R.items) {
        if (r >= 5000) break;
        if ((filter == 1 && v.severity < 2) || (filter == 2 && v.severity >= 2)) continue;
        if (v.rule < 0 || v.rule >= static_cast<int>(rules.size()) || v.element < 0 || v.element >= static_cast<int>(m.elements.size())) continue;
        const auto& rule = rules[static_cast<std::size_t>(v.rule)];
        const auto& e = m.elements[static_cast<std::size_t>(v.element)];
        const QColor col = v.severity >= 2 ? QColor(229, 72, 77) : QColor(245, 165, 36);
        auto* sev = new QTableWidgetItem(QStringLiteral("● ") + t(v.severity >= 2 ? "gui.norm.sev.error" : "gui.norm.sev.warning"));
        sev->setForeground(col);
        normTable_->setItem(r, 0, sev);
        QString name = ifcElementName(loc_, e);
        if (!v.where.empty()) name += QStringLiteral("  (") + QString::fromStdString(v.where) + ")";
        auto* n = new QTableWidgetItem(name);
        n->setData(Qt::UserRole, QString::fromStdString(e.guid));
        n->setToolTip(QString::fromStdString(e.type + "\nGUID: " + e.guid));
        normTable_->setItem(r, 1, n);
        normTable_->setItem(r, 6, new QTableWidgetItem(e.storey >= 0 && e.storey < static_cast<int>(m.storeys.size())
                                                           ? QString::fromStdString(m.storeys[static_cast<std::size_t>(e.storey)].name) : QString()));
        auto* rt = new QTableWidgetItem(QString::fromStdString(rule.titleFor(lang)));
        if (!rule.note.empty()) rt->setToolTip(QString::fromStdString(rule.note));
        normTable_->setItem(r, 2, rt);
        auto* val = new QTableWidgetItem(QString::fromStdString(bim::normFormat(v.value, rule.unit)));
        val->setForeground(col);
        val->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        normTable_->setItem(r, 3, val);
        auto* lim = new QTableWidgetItem(QString::fromUtf8(v.belowMin ? "≥ " : "≤ ") + QString::fromStdString(bim::normFormat(v.limit, rule.unit)));
        lim->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        normTable_->setItem(r, 4, lim);
        QString ref = QString::fromStdString(rule.norm + (rule.clause.empty() ? std::string() : ", " + rule.clause));
        if (!rule.verified) ref += QStringLiteral(" · ") + t("gui.norm.unverified");
        auto* rf = new QTableWidgetItem(ref);
        if (!rule.verified) {
            rf->setForeground(QColor(139, 150, 176));
            rf->setToolTip(t("gui.norm.unverified.tip"));
        }
        normTable_->setItem(r, 5, rf);
        ++r;
    }
    normTable_->setRowCount(r);
    normTable_->resizeColumnsToContents();
    if (normTable_->columnWidth(1) > 190) normTable_->setColumnWidth(1, 190);
    if (normTable_->columnWidth(2) > 230) normTable_->setColumnWidth(2, 230);
    if (normTable_->columnWidth(5) > 170) normTable_->setColumnWidth(5, 170);
}

bool IfcPanel::exportNorms(const QString& path) const {
    const int i = current();
    if (i < 0 || !models_[static_cast<std::size_t>(i)].norms) return false;
    const auto& L = models_[static_cast<std::size_t>(i)];
    const auto& R = *L.norms;
    const auto& rules = L.normRules;
    const auto& m = *L.model;
    const std::string lang = loc_.language();
    using xlsx::Cell;
    xlsx::Workbook wb;
    auto tr = [&](const char* k) { return loc_.tr(k); };
    // 1. Nomuvofiqliklar
    auto& s1 = wb.add(tr("gui.norm.x.items"));
    s1.widths = {6, 14, 32, 18, 18, 34, 12, 12, 20, 10, 14, 26, 40};
    s1.add({Cell::s("№", 1), Cell::s(tr("gui.norm.col.sev"), 1), Cell::s(tr("gui.ifc.col.name"), 1), Cell::s(tr("gui.ifc.col.cat"), 1),
            Cell::s(tr("gui.ifc.col.storey"), 1), Cell::s(tr("gui.norm.col.rule"), 1), Cell::s(tr("gui.norm.col.value"), 1),
            Cell::s(tr("gui.norm.col.limit"), 1), Cell::s(tr("gui.norm.col.norm"), 1), Cell::s(tr("gui.norm.col.clause"), 1),
            Cell::s(tr("gui.norm.col.verified"), 1), Cell::s("GUID", 1), Cell::s(tr("gui.norm.col.note"), 1)});
    int n = 0;
    for (const auto& v : R.items) {
        if (v.rule < 0 || v.rule >= static_cast<int>(rules.size()) || v.element < 0 || v.element >= static_cast<int>(m.elements.size())) continue;
        const auto& rule = rules[static_cast<std::size_t>(v.rule)];
        const auto& e = m.elements[static_cast<std::size_t>(v.element)];
        std::string name = ifcElementName(loc_, e).toStdString();
        if (!v.where.empty()) name += " (" + v.where + ")";
        s1.add({Cell::n(++n, 5), Cell::s(tr(v.severity >= 2 ? "gui.norm.sev.error" : "gui.norm.sev.warning")), Cell::s(name),
                Cell::s(loc_.tr(std::string("ifc.cat.") + bim::ifcCategory(e.type))),
                Cell::s(e.storey >= 0 && e.storey < static_cast<int>(m.storeys.size()) ? m.storeys[static_cast<std::size_t>(e.storey)].name : std::string()),
                Cell::s(rule.titleFor(lang)), Cell::s(bim::normFormat(v.value, rule.unit)),
                Cell::s(std::string(v.belowMin ? "≥ " : "≤ ") + bim::normFormat(v.limit, rule.unit)), Cell::s(rule.norm), Cell::s(rule.clause),
                Cell::s(tr(rule.verified ? "gui.norm.yes" : "gui.norm.no")), Cell::s(e.guid), Cell::s(rule.note)});
    }
    // 2. Qoidalar bo'yicha jamlama
    auto& s2 = wb.add(tr("gui.norm.x.rules"));
    s2.widths = {20, 40, 20, 10, 12, 12, 14, 14};
    s2.add({Cell::s("ID", 1), Cell::s(tr("gui.norm.col.rule"), 1), Cell::s(tr("gui.norm.col.norm"), 1), Cell::s(tr("gui.norm.col.clause"), 1),
            Cell::s(tr("gui.norm.col.checked"), 1), Cell::s(tr("gui.norm.col.failed"), 1), Cell::s(tr("gui.norm.col.sev"), 1),
            Cell::s(tr("gui.norm.col.verified"), 1)});
    for (std::size_t k = 0; k < rules.size(); ++k) {
        const auto& rule = rules[k];
        if (!rule.enabled) continue;
        s2.add({Cell::s(rule.id), Cell::s(rule.titleFor(lang)), Cell::s(rule.norm), Cell::s(rule.clause),
                Cell::n(k < R.applicable.size() ? R.applicable[k] : 0, 5), Cell::n(k < R.failed.size() ? R.failed[k] : 0, 5),
                Cell::s(tr(rule.severity >= 2 ? "gui.norm.sev.error" : "gui.norm.sev.warning")), Cell::s(tr(rule.verified ? "gui.norm.yes" : "gui.norm.no"))});
    }
    try {
        xlsx::save(wb, path.toStdString());
    } catch (const Error&) {
        return false;
    }
    return true;
}

void IfcPanel::highlightGuid(const QString& guid) {
    for (int r = 0; r < elems_->rowCount(); ++r)
        if (auto* it = elems_->item(r, 0); it && it->data(Qt::UserRole).toString() == guid) {
            elems_->setCurrentCell(r, 0);
            elems_->scrollToItem(it, QAbstractItemView::PositionAtCenter);
            return;
        }
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
        georef_->setVisible(false);
        elems_->setRowCount(0);
        elemCount_->clear();
        fillPlan();
        fillNorms();
        return;
    }
    const auto& m = *models_[static_cast<std::size_t>(i)].model;
    {
        // tur filtri (shu modeldagi toifalar)
        filling_ = true;
        const QString keep = catFilter_->currentData().toString();
        catFilter_->clear();
        catFilter_->addItem(t("gui.ifc.allcats"), QString());
        for (const auto& r : bim::ifcSummary(m)) catFilter_->addItem(QString::fromStdString(loc_.tr("ifc.cat." + r.category)), QString::fromStdString(r.category));
        const int k = catFilter_->findData(keep);
        catFilter_->setCurrentIndex(k >= 0 ? k : 0);
        georef_->setVisible(m.hasMapConversion);
        georef_->setChecked(models_[static_cast<std::size_t>(i)].georef);
        filling_ = false;
        fillElements();
        fillPlan();
        fillNorms();
    }
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
    s3.widths = {34, 22, 20, 22, 28, 12, 12, 14, 14, 10, 10, 12, 26};
    s3.add({Cell::s(tr("gui.ifc.col.name"), 1), Cell::s(tr("gui.ifc.col.cat"), 1), Cell::s(tr("gui.ifc.col.type"), 1), Cell::s(tr("gui.ifc.col.storey"), 1),
            Cell::s(tr("gui.ifc.col.material"), 1), Cell::s(tr("gui.ifc.col.volume"), 1), Cell::s(tr("gui.ifc.col.rebar"), 1), Cell::s("X", 1), Cell::s("Y", 1),
            Cell::s(tr("gui.ifc.col.zbot"), 1), Cell::s(tr("gui.ifc.col.ztop"), 1), Cell::s("Tag", 1), Cell::s("GUID", 1)});
    for (const auto& e : m.elements) {
        const bool has = e.storey >= 0 && e.storey < static_cast<int>(m.storeys.size());
        s3.add({Cell::s(ifcElementName(loc_, e).toStdString()), Cell::s(loc_.tr(std::string("ifc.cat.") + bim::ifcCategory(e.type))), Cell::s(e.type),
                Cell::s(has ? m.storeys[static_cast<std::size_t>(e.storey)].name : std::string()), Cell::s(e.material), Cell::n(e.volume, 3),
                Cell::n(e.barMass, 2), Cell::n(e.origin.x, 3), Cell::n(e.origin.y, 3), Cell::n(e.box.valid() ? e.box.min.z : e.origin.z, 3),
                Cell::n(e.box.valid() ? e.box.max.z : e.origin.z, 3), Cell::s(e.tag), Cell::s(e.guid)});
    }
    try {
        xlsx::save(wb, path.toStdString());
    } catch (const Error&) {
        return false;
    }
    return true;
}

bool IfcPanel::exportStakeout(const QString& path) const {
    const int i = current();
    if (i < 0) return false;
    const auto& m = *models_[static_cast<std::size_t>(i)].model;
    using xlsx::Cell;
    xlsx::Workbook wb;
    auto tr = [&](const char* k) { return loc_.tr(k); };
    // 1. Razbivka: ustun, qoziq, fundament markazlari (qavat, X bo'yicha tartib)
    auto& s1 = wb.add(tr("gui.ifc.x.stake"));
    s1.widths = {6, 12, 30, 20, 22, 14, 14, 11, 11, 10};
    s1.add({Cell::s("№", 1), Cell::s("Tag", 1), Cell::s(tr("gui.ifc.col.name"), 1), Cell::s(tr("gui.ifc.col.cat"), 1), Cell::s(tr("gui.ifc.col.storey"), 1),
            Cell::s("X", 1), Cell::s("Y", 1), Cell::s(tr("gui.ifc.col.zbot"), 1), Cell::s(tr("gui.ifc.col.ztop"), 1), Cell::s(tr("gui.ifc.col.rot"), 1)});
    std::vector<const bim::IfcElement*> list;
    for (const auto& e : m.elements)
        if (ifcIsStakeoutElement(e)) list.push_back(&e);
    std::stable_sort(list.begin(), list.end(), [](const auto* a, const auto* b) {
        if (a->storey != b->storey) return a->storey < b->storey;
        if (std::abs(a->origin.y - b->origin.y) > 0.01) return a->origin.y < b->origin.y;
        return a->origin.x < b->origin.x;
    });
    int n = 0;
    for (const auto* e : list) {
        const bool has = e->storey >= 0 && e->storey < static_cast<int>(m.storeys.size());
        double deg = e->angle * 180.0 / 3.14159265358979323846;
        if (deg < 0) deg += 360;
        s1.add({Cell::n(++n, 5), Cell::s(e->tag), Cell::s(ifcElementName(loc_, *e).toStdString()), Cell::s(loc_.tr(std::string("ifc.cat.") + bim::ifcCategory(e->type))),
                Cell::s(has ? m.storeys[static_cast<std::size_t>(e->storey)].name : std::string()), Cell::n(e->origin.x, 3), Cell::n(e->origin.y, 3),
                Cell::n(e->box.valid() ? e->box.min.z : e->origin.z, 3), Cell::n(e->box.valid() ? e->box.max.z : e->origin.z, 3), Cell::n(deg, 2)});
    }
    // 2. Koordinata o'qlari
    auto& s2 = wb.add(tr("gui.ifc.x.axes"));
    s2.widths = {10, 14, 14, 14, 14, 12};
    s2.add({Cell::s(tr("gui.ifc.col.axis"), 1), Cell::s("X1", 1), Cell::s("Y1", 1), Cell::s("X2", 1), Cell::s("Y2", 1), Cell::s(tr("gui.ifc.col.len"), 1)});
    for (const auto& a : m.axes) {
        const auto& p = a.pts.front();
        const auto& q = a.pts.back();
        s2.add({Cell::s(a.tag), Cell::n(p.x, 3), Cell::n(p.y, 3), Cell::n(q.x, 3), Cell::n(q.y, 3), Cell::n(p.distanceTo(q), 3)});
    }
    try {
        xlsx::save(wb, path.toStdString());
    } catch (const Error&) {
        return false;
    }
    return true;
}

} // namespace asiacad::gui
