#include "sewer_dialog.h"
#include "table_bulk.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStandardPaths>

#include <asiacad/civil/excel.h>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <cmath>
#include <fstream>
#include <sstream>

namespace asiacad::gui {

using namespace asiacad::civil;

namespace {
enum MhCol { MName, MX, MY, MRim, MInv, MD, MArea, MCount };
enum SgCol { SRoute, SPipe, SSlope, SFlow, SLen, SInvUp, SInvDn, SFill, SVel, SStatus, SCount };

double toNum(const QString& s, bool* ok = nullptr) {
    QString t = s.trimmed();
    t.replace(',', '.');
    return t.toDouble(ok);
}
QString fmt(double v, int dec = 2) { return QString::number(v, 'f', dec); }
QTableWidgetItem* ro(const QString& s) {
    auto* it = new QTableWidgetItem(s);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    return it;
}
} // namespace

SewerDialog::SewerDialog(const i18n::Localizer& loc, std::function<std::vector<Vec3>()> pick, bool dark, QWidget* parent)
    : QDialog(parent), loc_(loc), pick_(std::move(pick)) {
    setWindowTitle(t("gui.sew.title"));
    resize(1540, 860);
    buildUi();
    pipe3d_->setDark(dark);
    net3d_->setDark(dark);
    fillTemplates();
    fillMaterials();
    // boshlang'ich tarmoq: tanlangan poliliniya yoki namuna
    SewerNetwork n;
    n.name = "K1";
    const auto v = pick_ ? pick_() : std::vector<Vec3>{};
    if (v.size() >= 2) {
        for (std::size_t i = 0; i < v.size(); ++i)
            n.manholes.push_back({"K1-" + std::to_string(i + 1), v[i].x, v[i].y, v[i].z, std::nullopt, 0});
    } else {
        n.manholes = {{"K1-1", 0, 0, 100.0, std::nullopt, 0}, {"K1-2", 40, 0, 99.85, std::nullopt, 0}, {"K1-3", 85, 0, 99.70, std::nullopt, 0}};
    }
    setNetwork(n);
    selectTemplate(1);  // ko'cha tarmog'i PVX Ø200
}

QString SewerDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

QString SewerDialog::templatesPath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/pipe_templates.json";
}

// ---------------------------------------------------------------- UI
void SewerDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    // ---- yon shablonlar paneli
    side_ = new TemplateSidebar(t("gui.side.title"), t("gui.side.search"), t("gui.side.apply"), t("gui.side.hint"), split);
    connect(side_, &TemplateSidebar::templateActivated, this, [this](const QVariant& v) { selectTemplate(v.toInt()); });

    // ---- chap: parametrlar
    auto* leftScroll = new QScrollArea(split);
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->setObjectName("sewerLeft");
    leftScroll->setMinimumWidth(390);   // forma qatorlari (yorliq + maydon) to'liq sig'sin — gorizontal surilmasin
    auto* left = new QWidget;
    left->setObjectName("sewerLeftBody");
    leftScroll->setWidget(left);
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 6, 0);
    auto* form = new QFormLayout;
    tpl_ = new QComboBox(left);
    name_ = new QLineEdit("K1", left);
    system_ = new QComboBox(left);
    for (const char* s : {"K1", "K2", "K3", "N"}) system_->addItem(t((std::string("gui.sew.sys.") + s).c_str()), s);
    street_ = new QCheckBox(t("gui.sew.street"), left);
    pressure_ = new QCheckBox(t("gui.sew.pressure"), left);
    material_ = new QComboBox(left);
    size_ = new QComboBox(left);
    specInfo_ = new QLabel(left);
    specInfo_->setWordWrap(true);
    specInfo_->setObjectName("muted");
    auto spin = [&](double lo, double hi, double step, int dec, double val) {
        auto* s = new QDoubleSpinBox(left);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        s->setValue(val);
        return s;
    };
    slope_ = spin(0, 0.2, 0.0005, 4, 0.007);
    depth_ = spin(0.3, 15, 0.1, 2, 1.8);
    frost_ = spin(0, 3, 0.1, 2, 0.8);
    textH_ = spin(0.1, 20, 0.1, 2, 1.0);
    form->addRow(t("gui.sew.template"), tpl_);
    form->addRow(t("gui.sew.name"), name_);
    form->addRow(t("gui.sew.system"), system_);
    form->addRow(QString(), street_);
    form->addRow(QString(), pressure_);
    form->addRow(t("gui.sew.material"), material_);
    form->addRow(t("gui.sew.size"), size_);
    form->addRow(QString(), specInfo_);
    form->addRow(t("gui.sew.slope"), slope_);
    form->addRow(t("gui.sew.depth"), depth_);
    form->addRow(t("gui.sew.frost"), frost_);
    form->addRow(t("gui.sew.texth"), textH_);
    for (auto* cb : {tpl_, system_, material_, size_}) {
        cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        cb->setMinimumContentsLength(12);
    }
    street_->setText(t("gui.sew.street"));
    lv->addLayout(form);
    auto* applyAll = new QPushButton(t("gui.sew.apply"), left);
    auto* saveTpl = new QPushButton(t("gui.sew.savetpl"), left);
    lv->addWidget(applyAll);
    lv->addWidget(saveTpl);
    // yomg'ir sarfi (K2) — SHNQ 2.04.22-25
    storm_ = new QGroupBox(t("gui.sew.storm"), left);
    storm_->setCheckable(true);
    storm_->setChecked(false);
    auto* sf = new QFormLayout(storm_);
    q20_ = spin(5, 500, 5, 1, 50);
    nPar_ = spin(0.3, 1.0, 0.01, 2, 0.55);
    mr_ = spin(2, 300, 5, 0, 60);
    gamma_ = spin(0.5, 3, 0.01, 2, 1.54);
    period_ = spin(0.33, 20, 0.33, 2, 1.0);
    tcon_ = spin(1, 30, 1, 1, 5);
    tcan_ = spin(0, 60, 0.5, 1, 0);
    sf->addRow("q20, l/s·ga", q20_);
    sf->addRow("n", nPar_);
    sf->addRow("mr", mr_);
    sf->addRow(QString::fromUtf8("γ"), gamma_);
    sf->addRow(t("gui.sew.P"), period_);
    sf->addRow(t("gui.sew.tcon"), tcon_);
    sf->addRow(t("gui.sew.tcan"), tcan_);
    auto* stNote = new QLabel(t("gui.sew.storm_note"), storm_);
    stNote->setWordWrap(true);
    stNote->setObjectName("muted");
    sf->addRow(stNote);
    auto* surfTitle = new QLabel(t("gui.sew.surfaces"), storm_);
    surfTitle->setObjectName("muted");
    sf->addRow(surfTitle);
    for (int i = 0; i < 7; ++i) {
        share_[i] = spin(0, 100, 5, 0, i == 0 ? 100 : 0);
        sf->addRow(t(("gui.sew.surf." + std::to_string(i)).c_str()), share_[i]);
    }
    lv->addWidget(storm_);
    auto* note = new QLabel(t("gui.sew.norm_note"), left);
    note->setWordWrap(true);
    note->setObjectName("muted");
    lv->addWidget(note);
    lv->addStretch(1);

    // ---- o'rta: jadvallar
    auto* mid = new QWidget(split);
    auto* mv = new QVBoxLayout(mid);
    mv->setContentsMargins(6, 0, 6, 0);
    auto* mhTitle = new QLabel(t("gui.sew.manholes"), mid);
    mhTitle->setObjectName("title");
    mv->addWidget(mhTitle);
    mh_ = new QTableWidget(0, MCount, mid);
    mh_->setHorizontalHeaderLabels({t("gui.sew.col.name"), "X", "Y", t("gui.sew.col.rim"), t("gui.sew.col.inv"), t("gui.sew.col.d"),
                                    t("gui.sew.col.area")});
    mh_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mh_->verticalHeader()->setVisible(false);
    mv->addWidget(mh_, 2);
    auto* mhBtns = new QHBoxLayout;
    auto* fromPl = new QPushButton(t("gui.sew.frompline"), mid);
    auto* addB = new QPushButton(t("gui.sew.add"), mid);
    auto* remB = new QPushButton(t("gui.sew.remove"), mid);
    auto* openB = new QPushButton(t("gui.sew.open"), mid);
    auto* saveB = new QPushButton(t("gui.sew.save"), mid);
    for (auto* b : {fromPl, addB, remB, openB, saveB}) mhBtns->addWidget(b);
    mv->addLayout(mhBtns);
    auto* sgTitle = new QLabel(t("gui.sew.segs"), mid);
    sgTitle->setObjectName("title");
    mv->addWidget(sgTitle);
    segs_ = new QTableWidget(0, SCount, mid);
    segs_->setHorizontalHeaderLabels({"#", t("gui.sew.col.pipe"), "i", t("gui.sew.col.flow"), "L, m", QString::fromUtf8("Tub ↑"),
                                      QString::fromUtf8("Tub ↓"), "h/d", "v, m/s", t("gui.sew.col.status")});
    segs_->horizontalHeaderItem(SInvUp)->setText(t("gui.sew.col.inv").section(' ', 0, 0) + QString::fromUtf8(" \u2191"));
    segs_->horizontalHeaderItem(SInvDn)->setText(t("gui.sew.col.inv").section(' ', 0, 0) + QString::fromUtf8(" \u2193"));
    segs_->verticalHeader()->setVisible(false);
    segs_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    segs_->horizontalHeader()->setSectionResizeMode(SPipe, QHeaderView::Fixed);
    segs_->setColumnWidth(SPipe, 190);
    segs_->horizontalHeader()->setSectionResizeMode(SStatus, QHeaderView::Stretch);
    mv->addWidget(segs_, 3);

    // ---- o'ng: 3D, tekshiruv, hajmlar
    tabs_ = new QTabWidget(split);
    pipe3d_ = new Pipe3DView(tabs_);
    // tarmoq 3D va ostida Xossalar (tanlangan quduq / quvur) — o'ng ustun tor, shuning uchun ustma-ust
    auto* netBox = new QSplitter(Qt::Vertical, tabs_);
    net3d_ = new Pipe3DView(netBox);
    props_ = new PropPanel(netBox);
    props_->empty(t("gui.xp.hint"));
    props_->setMinimumHeight(160);
    netBox->addWidget(net3d_);
    netBox->addWidget(props_);
    netBox->setStretchFactor(0, 1);
    netBox->setStretchFactor(1, 1);
    netBox->setSizes({400, 400});
    connect(net3d_, &Pipe3DView::picked, this, &SewerDialog::onPicked);
    checks_ = new QListWidget(tabs_);
    checks_->setWordWrap(true);
    qty_ = new QPlainTextEdit(tabs_);
    qty_->setReadOnly(true);
    tabs_->addTab(pipe3d_, t("gui.sew.view3d"));
    tabs_->addTab(netBox, t("gui.sew.view3dnet"));
    tabs_->addTab(checks_, t("gui.sew.checks"));
    tabs_->addTab(qty_, t("gui.sew.qty"));
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 0);
    split->setStretchFactor(2, 3);
    split->setStretchFactor(3, 2);
    split->setSizes({260, 400, 660, 400});

    // ---- past: holat va tugmalar
    auto* bottom = new QHBoxLayout;
    status_ = new QLabel(this);
    status_->setObjectName("title");
    profile_ = new QCheckBox(t("gui.sew.profile"), this);
    profile_->setChecked(true);
    auto* drawB = new QPushButton(t("gui.sew.draw"), this);
    drawB->setObjectName("accent");
    auto* closeB = new QPushButton(QString::fromStdString(loc_.tr("gui.unsaved.title")).isEmpty() ? "OK" : "✕", this);
    closeB->setFixedWidth(44);
    bottom->addWidget(status_, 1);
    auto* xlStatus = new QLabel(this);
    xlStatus->setObjectName("muted");
    bottom->addWidget(xlStatus);
    excel_ = new ExcelLink(
        loc_, this, "kanalizatsiya", [this] { return excel::sewerWorkbook(network(), res_, [this](const std::string& k, const std::vector<std::string>& a) { return loc_.tr(k, a); }); },
        [this](const xlsx::Workbook& wb) {
            auto n = network();
            excel::sewerFromWorkbook(wb, n, [this](const std::string& k, const std::vector<std::string>& a) { return loc_.tr(k, a); });
            setNetwork(n);
        });
    excel_->setStatusLabel(xlStatus);
    excel_->addButtons(bottom);
    bottom->addWidget(profile_);
    bottom->addWidget(drawB);
    bottom->addWidget(closeB);
    root->addLayout(bottom);

    // ---- signallar
    connect(tpl_, &QComboBox::activated, this, &SewerDialog::onTemplate);
    connect(material_, &QComboBox::currentIndexChanged, this, [this] { fillSizes(); });
    connect(size_, &QComboBox::currentIndexChanged, this, &SewerDialog::onSpecChanged);
    connect(applyAll, &QPushButton::clicked, this, &SewerDialog::applySpecToAll);
    connect(saveTpl, &QPushButton::clicked, this, &SewerDialog::saveTemplate);
    connect(fromPl, &QPushButton::clicked, this, &SewerDialog::fromPolyline);
    connect(addB, &QPushButton::clicked, this, &SewerDialog::addManhole);
    connect(remB, &QPushButton::clicked, this, &SewerDialog::removeManhole);
    connect(openB, &QPushButton::clicked, this, &SewerDialog::openFile);
    connect(saveB, &QPushButton::clicked, this, &SewerDialog::saveFile);
    connect(drawB, &QPushButton::clicked, this, &SewerDialog::draw);
    connect(closeB, &QPushButton::clicked, this, &QDialog::close);
    connect(mh_, &QTableWidget::itemChanged, this, [this] { if (!updating_) recompute(); });
    connect(segs_, &QTableWidget::itemChanged, this, [this] { if (!updating_) recompute(); });
    connect(storm_, &QGroupBox::toggled, this, [this] { if (!updating_) recompute(); });
    for (auto* sp : {q20_, nPar_, mr_, gamma_, period_, tcon_, tcan_})
        connect(sp, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) recompute(); });
    for (auto* sp : share_)
        connect(sp, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) recompute(); });
    for (auto* s : {slope_, depth_, frost_, textH_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) recompute(); });
    connect(street_, &QCheckBox::toggled, this, [this] { if (!updating_) recompute(); });
    connect(pressure_, &QCheckBox::toggled, this, [this] { if (!updating_) recompute(); });
    connect(system_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_) return;
        updating_ = true;
        pressure_->setChecked(system_->currentData().toString() == "N");
        updating_ = false;
        recompute();
    });
    connect(name_, &QLineEdit::textEdited, this, [this] { recompute(); });
}

void SewerDialog::fillTemplates() {
    templates_ = PipeCatalog::builtin().templates;
    try {
        for (auto& tp : PipeCatalog::loadTemplates(templatesPath().toStdString())) templates_.push_back(tp);
    } catch (...) {
    }
    tpl_->clear();
    for (const auto& tp : templates_) {
        const QString nm = tp.name.rfind("tpl.", 0) == 0 ? t(tp.name.c_str()) : QString::fromStdString(tp.name);
        tpl_->addItem((tp.user ? QString::fromUtf8("★ ") : QString()) + nm);
    }
    // yon panel: material bo'yicha bo'limlar
    side_->clear();
    for (auto m : PipeCatalog::builtin().materials()) {
        bool any = false;
        for (int i = 0; i < static_cast<int>(templates_.size()); ++i) {
            const auto& tp = templates_[static_cast<std::size_t>(i)];
            const PipeSpec* s = PipeCatalog::builtin().find(tp.specId);
            if (!s || s->material != m) continue;
            if (!any) side_->addSection(t((std::string("pipe.mat.") + materialKey(m)).c_str()));
            any = true;
            const QString nm = tp.name.rfind("tpl.", 0) == 0 ? t(tp.name.c_str()) : QString::fromStdString(tp.name);
            const QColor acc = m == PipeMaterial::Concrete ? QColor(150, 140, 122)
                             : m == PipeMaterial::Steel || m == PipeMaterial::DuctileIron ? QColor(120, 130, 150)
                             : m == PipeMaterial::AsbestosCement ? QColor(170, 170, 160)
                             : QColor(230, 120, 50);
            side_->addTemplate(nm, QStringLiteral("%1 · i=%2 · %3 m").arg(QString::fromStdString(tp.system)).arg(tp.slope, 0, 'g', 3).arg(tp.startDepth, 0, 'f', 2),
                               i, acc, tp.user);
        }
    }
}

void SewerDialog::fillMaterials() {
    material_->blockSignals(true);
    material_->clear();
    for (auto m : PipeCatalog::builtin().materials())
        material_->addItem(t((std::string("pipe.mat.") + materialKey(m)).c_str()), QString::fromLatin1(materialKey(m)));
    material_->blockSignals(false);
    fillSizes();
}

void SewerDialog::fillSizes() {
    PipeMaterial m;
    if (!materialFromKey(material_->currentData().toString().toStdString(), m)) return;
    const QString keep = size_->currentData().toString();
    size_->blockSignals(true);
    size_->clear();
    for (const auto* s : PipeCatalog::builtin().byMaterial(m))
        size_->addItem(QStringLiteral("%1 · Ø%2 × %3%4")
                           .arg(QString::fromStdString(s->klass))
                           .arg(s->dn)
                           .arg(s->wall, 0, 'g', 3)
                           .arg(s->verified ? QString() : QStringLiteral("  *")),
                       QString::fromStdString(s->id));
    const int k = size_->findData(keep);
    size_->setCurrentIndex(k >= 0 ? k : 0);
    size_->blockSignals(false);
    onSpecChanged();
}

const PipeSpec* SewerDialog::currentSpec() const { return PipeCatalog::builtin().find(size_->currentData().toString().toStdString()); }

void SewerDialog::onSpecChanged() {
    const PipeSpec* s = currentSpec();
    if (!s) return;
    const QString mat = t((std::string("pipe.mat.") + materialKey(s->material)).c_str());
    specInfo_->setText(t("gui.sew.spec_info", {QString::fromStdString(s->standard) + " · " + QString::fromStdString(s->klass),
                                                fmt(s->od, 1), fmt(s->wall, 1), fmt(s->id_mm(), 1), fmt(s->mass(), 2),
                                                fmt(s->manningN, 3), s->verified ? t("gui.sew.verified") : t("gui.sew.unverified")}));
    pipe3d_->showPipe(*s, mat);
}

void SewerDialog::selectTemplate(int i) {
    if (i < 0 || i >= static_cast<int>(templates_.size())) return;
    tpl_->setCurrentIndex(i);
    onTemplate(i);
}

void SewerDialog::onTemplate(int i) {
    if (i < 0 || i >= static_cast<int>(templates_.size())) return;
    const auto& tp = templates_[static_cast<std::size_t>(i)];
    const PipeSpec* s = PipeCatalog::builtin().find(tp.specId);
    if (!s) return;
    updating_ = true;
    system_->setCurrentIndex(std::max(0, system_->findData(QString::fromStdString(tp.system))));
    pressure_->setChecked(tp.system == "N");
    storm_->setChecked(tp.system == "K2");
    street_->setChecked(tp.street);
    slope_->setValue(tp.slope);
    depth_->setValue(tp.startDepth);
    material_->setCurrentIndex(std::max(0, material_->findData(QString::fromLatin1(materialKey(s->material)))));
    fillSizes();
    size_->setCurrentIndex(std::max(0, size_->findData(QString::fromStdString(tp.specId))));
    for (int r = 0; r < mh_->rowCount(); ++r)
        if (tp.manholeD > 0) mh_->item(r, MD)->setText("");  // avto (normaga ko'ra)
    updating_ = false;
    onSpecChanged();
    applySpecToAll();
}

QComboBox* SewerDialog::specCombo(int row) const { return qobject_cast<QComboBox*>(segs_->cellWidget(row, SPipe)); }

void SewerDialog::applySpecToAll() {
    const PipeSpec* s = currentSpec();
    if (!s) return;
    updating_ = true;
    for (int r = 0; r < segs_->rowCount(); ++r) {
        if (auto* c = specCombo(r)) c->setCurrentIndex(std::max(0, c->findData(QString::fromStdString(s->id))));
        segs_->item(r, SSlope)->setText(pressure_->isChecked() ? QString() : fmt(slope_->value(), 4));
    }
    updating_ = false;
    recompute();
}

void SewerDialog::syncSegmentsRows() {
    const int need = std::max(0, mh_->rowCount() - 1);
    const PipeSpec* cur = currentSpec();
    while (segs_->rowCount() > need) segs_->removeRow(segs_->rowCount() - 1);
    while (segs_->rowCount() < need) {
        const int r = segs_->rowCount();
        segs_->insertRow(r);
        auto* cb = new QComboBox(segs_);
        for (const auto& sp : PipeCatalog::builtin().specs())
            cb->addItem(QString::fromStdString(sp.label()) + (sp.verified ? QString() : QStringLiteral(" *")), QString::fromStdString(sp.id));
        if (cur) cb->setCurrentIndex(std::max(0, cb->findData(QString::fromStdString(cur->id))));
        connect(cb, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) recompute(); });
        segs_->setCellWidget(r, SPipe, cb);
        segs_->setItem(r, SSlope, new QTableWidgetItem(fmt(slope_->value(), 4)));
        segs_->setItem(r, SFlow, new QTableWidgetItem(""));
        for (int c : {SRoute, SLen, SInvUp, SInvDn, SFill, SVel, SStatus}) segs_->setItem(r, c, ro(""));
    }
}

// ---------------------------------------------------------------- model <-> UI
SewerNetwork SewerDialog::network() const {
    SewerNetwork n;
    n.name = name_->text().trimmed().isEmpty() ? "K1" : name_->text().trimmed().toStdString();
    n.system = system_->currentData().toString().toStdString();
    n.street = street_->isChecked();
    n.pressure = pressure_->isChecked();
    n.startDepth = depth_->value();
    n.defaultSlope = slope_->value();
    n.frostDepth = frost_->value();
    n.textHeight = textH_->value();
    n.storm.enabled = storm_->isChecked() && n.system == "K2";
    n.storm.q20 = q20_->value();
    n.storm.n = nPar_->value();
    n.storm.mr = mr_->value();
    n.storm.gamma = gamma_->value();
    n.storm.P = period_->value();
    n.storm.tcon = tcon_->value();
    n.storm.tcan = tcan_->value();
    for (int i = 0; i < 7; ++i) n.storm.share[static_cast<std::size_t>(i)] = share_[i]->value() / 100.0;
    for (int r = 0; r < mh_->rowCount(); ++r) {
        auto txt = [&](int c) { return mh_->item(r, c) ? mh_->item(r, c)->text() : QString(); };
        Manhole m;
        m.name = txt(MName).toStdString();
        m.x = toNum(txt(MX));
        m.y = toNum(txt(MY));
        m.rim = toNum(txt(MRim));
        bool ok = false;
        const double inv = toNum(txt(MInv), &ok);
        if (ok && !txt(MInv).trimmed().isEmpty()) m.invert = inv;
        m.diameter = toNum(txt(MD));
        m.areaHa = toNum(txt(MArea));
        n.manholes.push_back(m);
    }
    for (int r = 0; r < segs_->rowCount(); ++r) {
        Segment s;
        if (auto* c = specCombo(r)) s.specId = c->currentData().toString().toStdString();
        s.slope = segs_->item(r, SSlope) ? toNum(segs_->item(r, SSlope)->text()) : 0;
        s.flowLs = segs_->item(r, SFlow) ? toNum(segs_->item(r, SFlow)->text()) : 0;
        n.segments.push_back(s);
    }
    return n;
}

void SewerDialog::setNetwork(const SewerNetwork& n) {
    updating_ = true;
    name_->setText(QString::fromStdString(n.name));
    system_->setCurrentIndex(std::max(0, system_->findData(QString::fromStdString(n.system))));
    street_->setChecked(n.street);
    pressure_->setChecked(n.pressure);
    depth_->setValue(n.startDepth);
    slope_->setValue(n.defaultSlope);
    frost_->setValue(n.frostDepth);
    textH_->setValue(n.textHeight);
    storm_->setChecked(n.storm.enabled);
    q20_->setValue(n.storm.q20);
    nPar_->setValue(n.storm.n);
    mr_->setValue(n.storm.mr);
    gamma_->setValue(n.storm.gamma);
    period_->setValue(n.storm.P);
    tcon_->setValue(n.storm.tcon);
    tcan_->setValue(n.storm.tcan);
    for (int i = 0; i < 7; ++i) share_[i]->setValue(n.storm.share[static_cast<std::size_t>(i)] * 100.0);
    BulkFill bulk(mh_);
    mh_->setRowCount(0);
    for (const auto& m : n.manholes) {
        const int r = mh_->rowCount();
        mh_->insertRow(r);
        mh_->setItem(r, MName, new QTableWidgetItem(QString::fromStdString(m.name)));
        mh_->setItem(r, MX, new QTableWidgetItem(fmt(m.x, 3)));
        mh_->setItem(r, MY, new QTableWidgetItem(fmt(m.y, 3)));
        mh_->setItem(r, MRim, new QTableWidgetItem(fmt(m.rim, 2)));
        mh_->setItem(r, MInv, new QTableWidgetItem(m.invert ? fmt(*m.invert, 2) : QString()));
        mh_->setItem(r, MD, new QTableWidgetItem(m.diameter > 0 ? fmt(m.diameter, 2) : QString()));
        mh_->setItem(r, MArea, new QTableWidgetItem(m.areaHa > 0 ? fmt(m.areaHa, 2) : QString()));
    }
    // material/o'lcham tanlovini birinchi bo'lak quvuriga moslash
    if (!n.segments.empty())
        if (const PipeSpec* sp = PipeCatalog::builtin().find(n.segments.front().specId)) {
            material_->setCurrentIndex(std::max(0, material_->findData(QString::fromLatin1(materialKey(sp->material)))));
            fillSizes();
            size_->setCurrentIndex(std::max(0, size_->findData(QString::fromStdString(sp->id))));
        }
    segs_->setRowCount(0);
    syncSegmentsRows();
    for (int r = 0; r < segs_->rowCount() && r < static_cast<int>(n.segments.size()); ++r) {
        const auto& s = n.segments[static_cast<std::size_t>(r)];
        if (auto* c = specCombo(r); c && !s.specId.empty()) c->setCurrentIndex(std::max(0, c->findData(QString::fromStdString(s.specId))));
        segs_->item(r, SSlope)->setText(s.slope > 0 ? fmt(s.slope, 4) : QString());
        segs_->item(r, SFlow)->setText(s.flowLs > 0 ? fmt(s.flowLs, 2) : QString());
    }
    updating_ = false;
    recompute();
}

bool SewerDialog::withProfile() const { return profile_->isChecked(); }
void SewerDialog::showTab(int i) { tabs_->setCurrentIndex(i); }

// ---------------------------------------------------------------- hisob va ko'rsatish
void SewerDialog::recompute() {
    updating_ = true;
    syncSegmentsRows();
    const SewerNetwork n = network();
    res_ = compute(n);
    const QColor errBg(232, 60, 60, 70), warnBg(240, 170, 40, 70);
    for (int r = 0; r < segs_->rowCount(); ++r) {
        const auto ri = static_cast<std::size_t>(r);
        segs_->item(r, SRoute)->setText(QStringLiteral("%1 → %2").arg(QString::fromStdString(n.manholes[ri].name),
                                                                           QString::fromStdString(n.manholes[ri + 1].name)));
        if (ri < res_.segments.size() && res_.segments[ri].spec) {
            const auto& s = res_.segments[ri];
            segs_->item(r, SLen)->setText(fmt(s.length));
            segs_->item(r, SInvUp)->setText(fmt(s.invUp));
            segs_->item(r, SInvDn)->setText(fmt(s.invDown));
            const bool hasFlow = s.flowLs > 0;
            if (n.storm.enabled) {  // sarf yomg'ir hisobidan — tahrirlanmaydi
                segs_->item(r, SFlow)->setText(fmt(s.flowLs, 1));
                segs_->item(r, SFlow)->setFlags(segs_->item(r, SFlow)->flags() & ~Qt::ItemIsEditable);
                segs_->item(r, SFlow)->setToolTip(QStringLiteral("F=%1 ga, t=%2 min, z=%3").arg(fmt(s.stormF), fmt(s.stormT, 1), fmt(s.stormZ, 3)));
            } else {
                segs_->item(r, SFlow)->setFlags(segs_->item(r, SFlow)->flags() | Qt::ItemIsEditable);
            }
            segs_->item(r, SFill)->setText(hasFlow ? fmt(s.fill) : QString());
            segs_->item(r, SVel)->setText(hasFlow ? fmt(s.velocity) : fmt(s.vFull) + QStringLiteral(" (Q=%1)").arg(fmt(s.qFull, 1)));
        }
        int worst = -1;
        QString first;
        for (const auto& is : res_.issues)
            if (is.segment == r && is.level != Issue::Info) {
                if (static_cast<int>(is.level) > worst) {
                    worst = static_cast<int>(is.level);
                    first = QString::fromStdString(loc_.tr(is.key, is.args));
                }
            }
        segs_->item(r, SStatus)->setText(worst < 0 ? QString::fromUtf8("✓") : first.section(':', 1).trimmed());
        segs_->item(r, SStatus)->setToolTip(first);
        const QColor bg = worst == Issue::Error ? errBg : worst == Issue::Warning ? warnBg : QColor(0, 0, 0, 0);
        for (int c = 0; c < SCount; ++c)
            if (auto* it = segs_->item(r, c)) it->setBackground(bg);
    }
    // quduqlar: diametr xatosi
    for (int r = 0; r < mh_->rowCount(); ++r) {
        bool bad = false;
        for (const auto& is : res_.issues)
            if (is.manhole == r && is.level == Issue::Error) bad = true;
        for (int c = 0; c < MCount; ++c)
            if (auto* it = mh_->item(r, c)) it->setBackground(bad ? errBg : QColor(0, 0, 0, 0));
        if (mh_->item(r, MD) && static_cast<std::size_t>(r) < res_.manholes.size())
            mh_->item(r, MD)->setToolTip(QStringLiteral("min %1 m").arg(fmt(res_.manholes[static_cast<std::size_t>(r)].required)));
    }
    // tekshiruv ro'yxati
    checks_->clear();
    auto* noteItem = new QListWidgetItem(t("gui.sew.norm_note"));
    noteItem->setForeground(QColor(140, 150, 175));
    checks_->addItem(noteItem);
    for (const auto& is : res_.issues) {
        const char* lv = is.level == Issue::Error ? "sewer.lvl.error" : is.level == Issue::Warning ? "sewer.lvl.warning" : "sewer.lvl.info";
        auto* it = new QListWidgetItem(QString::fromStdString(loc_.tr(lv) + ": " + loc_.tr(is.key, is.args)) +
                                       (is.ref.empty() ? QString() : QStringLiteral("  [%1]").arg(QString::fromStdString(is.ref))));
        it->setForeground(is.level == Issue::Error ? QColor(232, 70, 80) : is.level == Issue::Warning ? QColor(230, 160, 30)
                                                                                                    : QColor(120, 140, 180));
        checks_->addItem(it);
    }
    if (res_.errors() == 0 && res_.warnings() == 0) checks_->addItem(t("gui.sew.ok"));
    // hajmlar
    QString q;
    for (const auto& row : res_.pipes) {
        const PipeSpec* s = PipeCatalog::builtin().find(row.specId);
        q += t("gui.sew.qty_pipe", {s ? QString::fromStdString(s->label()) : QString::fromStdString(row.specId), fmt(row.length), fmt(row.massKg, 0)}) + "\n";
    }
    int mhTotal = 0;
    for (const auto& [d, c] : res_.manholeCount) {
        q += t("gui.sew.qty_mh", {fmt(d), QString::number(c)}) + "\n";
        mhTotal += c;
    }
    q += t("gui.sew.qty_earth", {fmt(res_.trenchVolume, 1), fmt(res_.beddingVolume, 1), fmt(res_.backfillVolume, 1)}) + "\n\n";
    q += t("gui.sew.total", {fmt(res_.totalLength), QString::number(mhTotal)});
    qty_->setPlainText(q);
    net3d_->showNetwork(n, res_);
    // holat
    if (res_.errors() == 0 && res_.warnings() == 0) {
        status_->setText(t("gui.sew.ok"));
        status_->setStyleSheet("color:#2FA36B;");
    } else {
        status_->setText(QStringLiteral("%1: %2   %3: %4")
                             .arg(QString::fromStdString(loc_.tr("sewer.lvl.error")))
                             .arg(res_.errors())
                             .arg(QString::fromStdString(loc_.tr("sewer.lvl.warning")))
                             .arg(res_.warnings()));
        status_->setStyleSheet(res_.errors() ? "color:#E8464F;" : "color:#E6A01E;");
    }
    updating_ = false;
}

// ---------------------------------------------------------------- amallar
void SewerDialog::addManhole() {
    updating_ = true;
    const int r = mh_->rowCount();
    double x = 0, y = 0, rim = 100;
    if (r > 0) {
        x = toNum(mh_->item(r - 1, MX)->text()) + 40;
        y = toNum(mh_->item(r - 1, MY)->text());
        rim = toNum(mh_->item(r - 1, MRim)->text()) - 0.2;
    }
    mh_->insertRow(r);
    mh_->setItem(r, MName, new QTableWidgetItem(QStringLiteral("%1-%2").arg(name_->text()).arg(r + 1)));
    mh_->setItem(r, MX, new QTableWidgetItem(fmt(x, 3)));
    mh_->setItem(r, MY, new QTableWidgetItem(fmt(y, 3)));
    mh_->setItem(r, MRim, new QTableWidgetItem(fmt(rim, 2)));
    mh_->setItem(r, MInv, new QTableWidgetItem(""));
    mh_->setItem(r, MD, new QTableWidgetItem(""));
    mh_->setItem(r, MArea, new QTableWidgetItem(""));
    updating_ = false;
    recompute();
}

void SewerDialog::removeManhole() {
    const int r = mh_->currentRow() >= 0 ? mh_->currentRow() : mh_->rowCount() - 1;
    if (r < 0 || mh_->rowCount() <= 2) return;
    mh_->removeRow(r);
    segs_->removeRow(std::min(r, segs_->rowCount() - 1));
    recompute();
}

void SewerDialog::fromPolyline() {
    const auto v = pick_ ? pick_() : std::vector<Vec3>{};
    if (v.size() < 2) {
        QMessageBox::information(this, windowTitle(), t("gui.sew.need_pline"));
        return;
    }
    SewerNetwork n = network();
    n.manholes.clear();
    for (std::size_t i = 0; i < v.size(); ++i)
        n.manholes.push_back({n.name + "-" + std::to_string(i + 1), v[i].x, v[i].y, v[i].z, std::nullopt, 0});
    n.segments.clear();
    setNetwork(n);
    applySpecToAll();
}

void SewerDialog::openFile() {
    const QString f = QFileDialog::getOpenFileName(this, t("gui.sew.open"), {}, t("gui.sew.filter"));
    if (f.isEmpty()) return;
    std::ifstream in(f.toStdString(), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        setNetwork(fromJson(ss.str()));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, windowTitle(), QString::fromUtf8(e.what()));
    }
}

void SewerDialog::saveFile() {
    QString f = QFileDialog::getSaveFileName(this, t("gui.sew.save"), name_->text() + ".kan.json", t("gui.sew.filter"));
    if (f.isEmpty()) return;
    std::ofstream out(f.toStdString(), std::ios::binary | std::ios::trunc);
    out << toJson(network());
}

void SewerDialog::saveTemplate() {
    const PipeSpec* s = currentSpec();
    if (!s) return;
    bool ok = false;
    const QString nm = QInputDialog::getText(this, t("gui.sew.savetpl"), t("gui.sew.tplname"), QLineEdit::Normal,
                                             QString::fromStdString(s->label()), &ok);
    if (!ok || nm.trimmed().isEmpty()) return;
    PipeTemplate tp;
    tp.id = "user_" + std::to_string(templates_.size() + 1);
    tp.name = nm.trimmed().toStdString();
    tp.system = system_->currentData().toString().toStdString();
    tp.specId = s->id;
    tp.slope = slope_->value();
    tp.startDepth = depth_->value();
    tp.street = street_->isChecked();
    tp.user = true;
    std::vector<PipeTemplate> user;
    for (const auto& x : templates_)
        if (x.user) user.push_back(x);
    user.push_back(tp);
    try {
        PipeCatalog::saveTemplates(templatesPath().toStdString(), user);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, windowTitle(), QString::fromUtf8(e.what()));
        return;
    }
    fillTemplates();
    tpl_->setCurrentIndex(tpl_->count() - 1);
}

// ---------------------------------------------------------------- Xossalar
void SewerDialog::editNet(const std::function<void(SewerNetwork&)>& fn) {
    SewerNetwork n = network();
    fn(n);
    while (n.segments.size() + 1 < n.manholes.size()) n.segments.push_back(n.segments.empty() ? Segment{} : n.segments.back());
    if (n.manholes.size() >= 1 && n.segments.size() >= n.manholes.size()) n.segments.resize(n.manholes.size() - 1);
    setNetwork(n);
}

void SewerDialog::onPicked(const QString& key) {
    const SewerNetwork n = network();
    const QColor err(232, 70, 80), warn(230, 160, 30);
    const int i = key.section(':', 1).toInt();
    auto issues = [&](bool seg, int idx) {
        for (const auto& is : res_.issues)
            if ((seg ? is.segment : is.manhole) == idx && is.level != Issue::Info)
                props_->info(QString::fromUtf8(is.level == Issue::Error ? "⚠" : "!"), QString::fromStdString(loc_.tr(is.key, is.args)),
                             is.level == Issue::Error ? err : warn);
    };
    if (key.startsWith("mh:") && i >= 0 && i < static_cast<int>(n.manholes.size())) {
        const auto& m = n.manholes[static_cast<std::size_t>(i)];
        props_->begin(t("gui.xp.mh", {QString::fromStdString(m.name)}), QString::fromStdString(n.name));
        props_->section(t("gui.xp.network"));
        props_->text("name", t("gui.xp.name"), QString::fromStdString(m.name), [this, i](const QString& s) {
            if (!s.trimmed().isEmpty()) editNet([&](SewerNetwork& x) { if (i < static_cast<int>(x.manholes.size())) x.manholes[static_cast<std::size_t>(i)].name = s.trimmed().toStdString(); });
        });
        auto num = [&](const char* k, const QString& label, double v, int dec, double Manhole::*f, const QString& unit) {
            props_->number(k, label, v, dec, [this, i, f](double val) {
                editNet([&](SewerNetwork& x) { if (i < static_cast<int>(x.manholes.size())) x.manholes[static_cast<std::size_t>(i)].*f = val; });
            }, unit);
        };
        num("x", "X", m.x, 3, &Manhole::x, "m");
        num("y", "Y", m.y, 3, &Manhole::y, "m");
        num("rim", t("gui.xp.rim"), m.rim, 2, &Manhole::rim, "m");
        props_->text("inv", t("gui.xp.mhinv"), m.invert ? fmt(*m.invert, 2) : QString(), [this, i](const QString& s) {
            bool ok = false;
            const double v = toNum(s, &ok);
            editNet([&](SewerNetwork& x) {
                if (i >= static_cast<int>(x.manholes.size())) return;
                auto& mm = x.manholes[static_cast<std::size_t>(i)];
                if (s.trimmed().isEmpty() || !ok) mm.invert.reset();
                else mm.invert = v;
            });
        });
        num("d", t("gui.xp.diam0"), m.diameter, 2, &Manhole::diameter, "m");
        if (n.storm.enabled) num("area", t("gui.xp.areaha"), m.areaHa, 2, &Manhole::areaHa, "ga");
        if (i < static_cast<int>(res_.manholes.size())) {
            const auto& r = res_.manholes[static_cast<std::size_t>(i)];
            props_->section(t("gui.xp.calc"));
            if (i > 0) props_->info(t("gui.xp.invin"), fmt(r.invIn, 3) + " m");
            if (i + 1 < static_cast<int>(n.manholes.size())) props_->info(t("gui.xp.invout"), fmt(r.invOut, 3) + " m");
            props_->info(t("gui.xp.depth"), fmt(r.depth) + " m");
            props_->info(t("gui.xp.diam"), fmt(r.diameter) + QStringLiteral(" (≥ %1) m").arg(fmt(r.required)), r.diameter + 1e-9 < r.required ? err : QColor());
        }
        issues(false, i);
        props_->section(t("gui.xp.details"));
        props_->action("insert", t("gui.xp.mhins"), [this, i] {   // keyingi quduq bilan o'rtasiga (oxirgisi — davomiga 40 m)
            editNet([&](SewerNetwork& x) {
                if (i >= static_cast<int>(x.manholes.size())) return;
                Manhole a = x.manholes[static_cast<std::size_t>(i)];
                Manhole q = a;
                q.invert.reset();
                q.diameter = 0;
                q.areaHa = 0;
                if (i + 1 < static_cast<int>(x.manholes.size())) {
                    const auto& b = x.manholes[static_cast<std::size_t>(i) + 1];
                    q.x = (a.x + b.x) / 2;
                    q.y = (a.y + b.y) / 2;
                    q.rim = (a.rim + b.rim) / 2;
                } else {
                    q.x += 40;
                }
                q.name = a.name + "a";
                x.manholes.insert(x.manholes.begin() + i + 1, q);
                if (!x.segments.empty()) {
                    const std::size_t si = std::min<std::size_t>(static_cast<std::size_t>(i), x.segments.size() - 1);
                    x.segments.insert(x.segments.begin() + static_cast<std::ptrdiff_t>(si) + 1, x.segments[si]);
                }
            });
        });
        if (n.manholes.size() > 2)
            props_->action("erase", t("gui.xp.erase"), [this, i] {
                net3d_->clearSelection();
                editNet([&](SewerNetwork& x) {
                    if (i >= static_cast<int>(x.manholes.size()) || x.manholes.size() <= 2) return;
                    x.manholes.erase(x.manholes.begin() + i);
                    if (!x.segments.empty()) x.segments.erase(x.segments.begin() + std::min<std::ptrdiff_t>(i, static_cast<std::ptrdiff_t>(x.segments.size()) - 1));
                });
            }, true);
        props_->action("todwg", t("gui.xp.todwg"), [this] { draw(); });
        if (i > 0) props_->action("prev", QString::fromUtf8("◀ ") + t("gui.xp.prev"), [this, i] { net3d_->selectKey(QStringLiteral("mh:%1").arg(i - 1)); });
        if (i + 1 < static_cast<int>(n.manholes.size()))
            props_->action("next", t("gui.xp.next") + QString::fromUtf8(" ▶"), [this, i] { net3d_->selectKey(QStringLiteral("mh:%1").arg(i + 1)); });
        props_->note(t("gui.xp.sewnote"));
        props_->end();
    } else if (key.startsWith("seg:") && i >= 0 && i + 1 < static_cast<int>(n.manholes.size()) && i < segs_->rowCount()) {
        const auto& a = n.manholes[static_cast<std::size_t>(i)];
        const auto& b = n.manholes[static_cast<std::size_t>(i) + 1];
        props_->begin(t("gui.xp.seg", {QString::fromStdString(a.name), QString::fromStdString(b.name)}), QString::fromStdString(n.name));
        props_->section(t("gui.xp.network"));
        if (auto* cb = specCombo(i)) {
            QStringList items;
            for (int k = 0; k < cb->count(); ++k) items << cb->itemText(k);
            props_->choice("spec", t("gui.xp.pipe"), items, cb->currentIndex(), [this, i](int k) {
                if (auto* c = specCombo(i)) c->setCurrentIndex(k);   // jadval kombosi — hisob o'zi qayta
            });
        }
        const double sl = i < static_cast<int>(n.segments.size()) ? n.segments[static_cast<std::size_t>(i)].slope : 0;
        props_->number("slope", t("gui.xp.slope0"), sl * 1000, 2, [this, i](double v) {
            editNet([&](SewerNetwork& x) { if (i < static_cast<int>(x.segments.size())) x.segments[static_cast<std::size_t>(i)].slope = std::max(0.0, v / 1000); });
        }, QString::fromUtf8("‰"));
        if (!n.storm.enabled)
            props_->number("flow", t("gui.xp.flow"), i < static_cast<int>(n.segments.size()) ? n.segments[static_cast<std::size_t>(i)].flowLs : 0, 2,
                           [this, i](double v) {
                               editNet([&](SewerNetwork& x) { if (i < static_cast<int>(x.segments.size())) x.segments[static_cast<std::size_t>(i)].flowLs = std::max(0.0, v); });
                           }, "l/s");
        if (i < static_cast<int>(res_.segments.size())) {
            const auto& s = res_.segments[static_cast<std::size_t>(i)];
            props_->section(t("gui.xp.calc"));
            props_->info(t("gui.xp.length"), fmt(s.length) + " m");
            props_->info(t("gui.xp.slope"), fmt(s.slope * 1000, 2) + QString::fromUtf8(" ‰"), s.minSlope > 0 && s.slope + 1e-12 < s.minSlope ? err : QColor());
            props_->info(t("gui.xp.uaxis"), fmt(s.invUp, 3) + QString::fromUtf8(" → ") + fmt(s.invDown, 3));
            props_->info(t("gui.xp.depth"), fmt(s.depthUp) + QString::fromUtf8(" → ") + fmt(s.depthDown) + " m");
            props_->info(t("gui.xp.ucovermin"), fmt(s.coverMin) + " m");
            props_->info(t("gui.xp.qfull"), fmt(s.qFull, 1) + " l/s · " + fmt(s.vFull) + " m/s");
            if (s.flowLs > 0) props_->info(t("gui.xp.fillv"), fmt(s.fill) + " · " + fmt(s.velocity) + " m/s", s.maxFill > 0 && s.fill > s.maxFill ? err : QColor());
        }
        issues(true, i);
        props_->section(t("gui.xp.details"));
        props_->action("split", t("gui.xp.segsplit"), [this, i] {   // o'rtasiga quduq
            editNet([&](SewerNetwork& x) {
                if (i + 1 >= static_cast<int>(x.manholes.size())) return;
                const Manhole& p = x.manholes[static_cast<std::size_t>(i)];
                const Manhole& q2 = x.manholes[static_cast<std::size_t>(i) + 1];
                Manhole m;
                m.name = p.name + "a";
                m.x = (p.x + q2.x) / 2;
                m.y = (p.y + q2.y) / 2;
                m.rim = (p.rim + q2.rim) / 2;
                x.manholes.insert(x.manholes.begin() + i + 1, m);
                if (i < static_cast<int>(x.segments.size())) x.segments.insert(x.segments.begin() + i + 1, x.segments[static_cast<std::size_t>(i)]);
            });
        });
        props_->action("todwg", t("gui.xp.todwg"), [this] { draw(); });
        if (i > 0) props_->action("prev", QString::fromUtf8("◀ ") + t("gui.xp.prev"), [this, i] { net3d_->selectKey(QStringLiteral("seg:%1").arg(i - 1)); });
        if (i + 2 < static_cast<int>(n.manholes.size()))
            props_->action("next", t("gui.xp.next") + QString::fromUtf8(" ▶"), [this, i] { net3d_->selectKey(QStringLiteral("seg:%1").arg(i + 1)); });
        props_->note(t("gui.xp.sewnote"));
        props_->end();
    } else {
        props_->empty(t("gui.xp.hint"));
    }
}

void SewerDialog::draw() {
    const QString path = QDir::temp().filePath(QStringLiteral("asiacad-%1.kan.json").arg(name_->text().isEmpty() ? "K1" : name_->text()));
    {
        std::ofstream out(path.toStdString(), std::ios::binary | std::ios::trunc);
        out << toJson(network());
    }
    emit drawRequested(path, profile_->isChecked());
}

} // namespace asiacad::gui
