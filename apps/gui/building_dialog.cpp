#include "building_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <fstream>

#include <asiacad/civil/excel.h>

namespace asiacad::gui {

using bim::ElemKind;

namespace {
const ElemKind kKinds[] = {ElemKind::Ignore, ElemKind::Outline, ElemKind::Column, ElemKind::Wall, ElemKind::Slab,
                           ElemKind::Opening, ElemKind::FoundationSlab, ElemKind::Lean, ElemKind::StripFooting};
QColor kindColor(ElemKind k) {
    switch (k) {
        case ElemKind::Lean: return QColor(128, 126, 120);
        case ElemKind::FoundationSlab: return QColor(150, 140, 122);
        case ElemKind::StripFooting: return QColor(150, 140, 122);
        case ElemKind::Column: return QColor(214, 170, 110);
        case ElemKind::Wall: return QColor(200, 196, 186);
        case ElemKind::Slab: return QColor(176, 186, 198);
        case ElemKind::Roof: return QColor(120, 150, 185);
        default: return QColor(180, 180, 180);
    }
}
QString f2(double v) { return QString::number(v, 'f', 2); }
} // namespace

BuildingDialog::BuildingDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent)
    : QDialog(parent), loc_(loc), doc_(doc) {
    setWindowTitle(t("gui.bim.title"));
    resize(1500, 860);
    buildUi();
    view_->setDark(dark);
    fillLayers();
    recompute();
}

QString BuildingDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

void BuildingDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    // ---- yon shablonlar paneli: bino tiplari
    side_ = new TemplateSidebar(t("gui.side.title"), t("gui.side.search"), t("gui.side.apply"), t("gui.side.hint"), split);
    side_->addSection(t("gui.side.typology"));
    for (const char* id : {"frame9", "frame16", "office5", "cottage2", "school3"}) {
        const QString nm = t((std::string("btpl.") + id).c_str());
        side_->addTemplate(nm.section(QString::fromUtf8(" · "), 0, 0), nm.section(QString::fromUtf8(" · "), 1), QString::fromLatin1(id),
                           QColor(217, 70, 106));
    }
    connect(side_, &TemplateSidebar::templateActivated, this, [this](const QVariant& v) { applyTypology(v.toString()); });

    // ---- chap: qatlamlar va parametrlar
    auto* scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setObjectName("sewerLeft");
    scroll->setMinimumWidth(360);
    auto* left = new QWidget;
    left->setObjectName("sewerLeftBody");
    scroll->setWidget(left);
    auto* lv = new QVBoxLayout(left);
    set_ = new DrawingSetWidget(loc_, t("gui.ds.title_bim"), {{"PLAN", t("gui.ds.role.plan")}, {"FOUND", t("gui.ds.role.found")}}, true, left);
    lv->addWidget(set_);
    auto* lt = new QLabel(t("gui.bim.layers"), left);
    lt->setObjectName("title");
    lv->addWidget(lt);
    layers_ = new QTableWidget(0, 2, left);
    layers_->setHorizontalHeaderLabels({t("gui.bim.col.layer"), t("gui.bim.col.kind")});
    layers_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layers_->verticalHeader()->setVisible(false);
    layers_->setMinimumHeight(200);
    lv->addWidget(layers_);
    auto* reread = new QPushButton(t("gui.bim.reread"), left);
    lv->addWidget(reread);

    auto* form = new QFormLayout;
    auto dspin = [&](double lo, double hi, double step, int dec, double v) {
        auto* s = new QDoubleSpinBox(left);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        s->setValue(v);
        return s;
    };
    const bim::BuildingParams d;
    floors_ = new QSpinBox(left);
    floors_->setRange(1, 120);
    floors_->setValue(d.floors);
    height_ = dspin(2.4, 12, 0.1, 2, d.floorHeight);
    slab_ = dspin(0.06, 1.0, 0.01, 2, d.slabT);
    roof_ = dspin(0.06, 1.0, 0.01, 2, d.roofT);
    ftop_ = dspin(-20, 5, 0.05, 3, d.foundTop);
    ft_ = dspin(0.2, 5, 0.05, 2, d.foundT);
    fover_ = dspin(0, 5, 0.05, 2, d.foundOverhang);
    strip_ = new QCheckBox(t("gui.bim.strip"), left);
    stripW_ = dspin(0.3, 5, 0.05, 2, d.stripWidth);
    lean_ = dspin(0.03, 0.5, 0.01, 2, d.leanT);
    wallT_ = dspin(0.08, 1.5, 0.01, 2, d.wallT);
    zeroAbs_ = dspin(-500, 9000, 0.01, 3, d.zeroAbs);
    zeroAuto_ = new QCheckBox(t("gui.kj.datum_auto"), left);
    zeroAuto_->setChecked(d.zeroAbsAuto);
    {
        auto* row = new QWidget(left);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(zeroAbs_, 1);
        h->addWidget(zeroAuto_);
        form->addRow(t("gui.kj.datum"), row);
    }
    form->addRow(t("gui.bim.floors"), floors_);
    form->addRow(t("gui.bim.height"), height_);
    form->addRow(t("gui.bim.slab"), slab_);
    form->addRow(t("gui.bim.roof"), roof_);
    form->addRow(t("gui.bim.ftop"), ftop_);
    form->addRow(t("gui.bim.ft"), ft_);
    form->addRow(t("gui.bim.fover"), fover_);
    form->addRow(QString(), strip_);
    form->addRow(t("gui.bim.stripw"), stripW_);
    form->addRow(t("gui.bim.lean"), lean_);
    form->addRow(t("gui.bim.wallt"), wallT_);
    lv->addLayout(form);
    auto* ct = new QLabel(t("gui.bim.classes"), left);
    ct->setObjectName("title");
    lv->addWidget(ct);
    auto* cf = new QFormLayout;
    auto cls = [&](const char* def) {
        auto* c = new QComboBox(left);
        for (const char* k : {"B7.5", "B10", "B15", "B20", "B22.5", "B25", "B30", "B35", "B40", "B45", "B50"}) c->addItem(k);
        c->setCurrentText(def);
        return c;
    };
    clsLean_ = cls(d.classLean.c_str());
    clsFound_ = cls(d.classFound.c_str());
    clsFrame_ = cls(d.classFrame.c_str());
    clsSlab_ = cls(d.classSlab.c_str());
    cf->addRow(t("gui.bim.cls.lean"), clsLean_);
    cf->addRow(t("gui.bim.cls.found"), clsFound_);
    cf->addRow(t("gui.bim.cls.frame"), clsFrame_);
    cf->addRow(t("gui.bim.cls.slab"), clsSlab_);
    lv->addLayout(cf);
    auto* note = new QLabel(t("gui.bim.note"), left);
    note->setWordWrap(true);
    note->setObjectName("muted");
    lv->addWidget(note);
    lv->addStretch(1);

    // ---- o'rta: 3D
    auto* mid = new QWidget(split);
    auto* mv = new QVBoxLayout(mid);
    mv->setContentsMargins(6, 0, 6, 0);
    view_ = new Pipe3DView(mid);
    mv->addWidget(view_, 1);
    auto* sh = new QHBoxLayout;
    showLbl_ = new QLabel(mid);
    showUpTo_ = new QSlider(Qt::Horizontal, mid);
    showUpTo_->setRange(0, d.floors);
    showUpTo_->setValue(d.floors);
    sh->addWidget(showLbl_);
    sh->addWidget(showUpTo_, 1);
    mv->addLayout(sh);

    // ---- o'ng: PTO va tekshiruv
    auto* tabs = new QTabWidget(split);
    pto_ = new QTableWidget(0, 6, tabs);
    pto_->setHorizontalHeaderLabels({t("gui.bim.col.level"), t("gui.bim.col.elem"), t("gui.bim.col.count"), t("gui.bim.col.vol"),
                                     t("gui.bim.col.form"), t("gui.bim.col.class")});
    pto_->verticalHeader()->setVisible(false);
    pto_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    pto_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    pto_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    checks_ = new QListWidget(tabs);
    checks_->setWordWrap(true);
    tabs->addTab(pto_, t("gui.bim.pto"));
    tabs->addTab(checks_, t("gui.bim.checks"));
    split->setSizes({250, 370, 660, 440});

    // ---- past
    auto* bottom = new QHBoxLayout;
    summary_ = new QLabel(this);
    summary_->setObjectName("title");
    auto* toRebar = new QPushButton(t("gui.bim.rebar"), this);
    auto* toDwg = new QPushButton(t("gui.bim.totable"), this);
    toDwg->setObjectName("accent");
    auto* close = new QPushButton(QString::fromUtf8("✕"), this);
    close->setFixedWidth(44);
    bottom->addWidget(summary_, 1);
    auto* xlStatus = new QLabel(this);
    xlStatus->setObjectName("muted");
    bottom->addWidget(xlStatus);
    excel_ = new ExcelLink(
        loc_, this, "bino_PTO", [this] { return excel::buildingWorkbook(params(), layerMap(), model_, [this](const std::string& k, const std::vector<std::string>& a) { return loc_.tr(k, a); }); },
        [this](const xlsx::Workbook& wb) {
            auto p = params();
            auto lm = layerMap();
            excel::buildingFromWorkbook(wb, p, lm, [this](const std::string& k, const std::vector<std::string>& a) { return loc_.tr(k, a); });
            setParams(p, lm);
        });
    excel_->setStatusLabel(xlStatus);
    excel_->addButtons(bottom);
    bottom->addWidget(toRebar);
    bottom->addWidget(toDwg);
    bottom->addWidget(close);
    root->addLayout(bottom);

    connect(reread, &QPushButton::clicked, this, &BuildingDialog::reread);
    connect(set_, &DrawingSetWidget::changed, this, [this] {
        int maxFloor = 0;
        for (const auto& f : set_->sheets())
            if (f.enabled && f.role == "PLAN") maxFloor = std::max(maxFloor, f.floorTo);
        if (maxFloor > floors_->value()) floors_->setValue(maxFloor);  // recompute signal orqali
        else recompute();
    });
    connect(toRebar, &QPushButton::clicked, this, &BuildingDialog::rebarRequested);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(toDwg, &QPushButton::clicked, this, [this] {
        const QString path = QDir::temp().filePath("asiacad-building.json");
        std::ofstream f(path.toStdString(), std::ios::binary | std::ios::trunc);
        f << bim::paramsToJson(params(), layerMap());
        f.close();
        emit tableRequested(path);
    });
    connect(floors_, &QSpinBox::valueChanged, this, [this](int n) {
        showUpTo_->setRange(0, n);
        showUpTo_->setValue(n);
        if (!updating_) recompute();
    });
    for (auto* s : {height_, slab_, roof_, ftop_, ft_, fover_, stripW_, lean_, wallT_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) recompute(); });
    connect(zeroAbs_, &QDoubleSpinBox::valueChanged, this, [this] {
        if (updating_) return;
        zeroAuto_->setChecked(false);  // qo'lda kiritildi (toggled -> recompute)
        recompute();
    });
    connect(zeroAuto_, &QCheckBox::toggled, this, [this] { if (!updating_) recompute(); });
    connect(strip_, &QCheckBox::toggled, this, [this] { if (!updating_) recompute(); });
    for (auto* c : {clsLean_, clsFound_, clsFrame_, clsSlab_})
        connect(c, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) recompute(); });
    connect(showUpTo_, &QSlider::valueChanged, this, [this] { render(); });
}

void BuildingDialog::fillLayers() {
    updating_ = true;
    const auto lm = bim::autoLayerMap(doc_);
    layers_->setRowCount(0);
    for (const auto& [name, k] : lm) {
        if (name == "PTO") continue;
        const int r = layers_->rowCount();
        layers_->insertRow(r);
        auto* it = new QTableWidgetItem(QString::fromStdString(name));
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        layers_->setItem(r, 0, it);
        auto* cb = new QComboBox(layers_);
        for (ElemKind kk : kKinds) cb->addItem(t((std::string("bim.kind.") + bim::kindKey(kk)).c_str()), QString::fromLatin1(bim::kindKey(kk)));
        cb->setCurrentIndex(std::max(0, cb->findData(QString::fromLatin1(bim::kindKey(k == ElemKind::Roof ? ElemKind::Slab : k)))));
        connect(cb, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) recompute(); });
        layers_->setCellWidget(r, 1, cb);
    }
    updating_ = false;
}

void BuildingDialog::reread() {
    fillLayers();
    recompute();
}

void BuildingDialog::setFloors(int n) { floors_->setValue(n); }

void BuildingDialog::setParams(const bim::BuildingParams& p, const std::map<std::string, ElemKind>& layers) {
    updating_ = true;
    floors_->setValue(p.floors);
    height_->setValue(p.floorHeight);
    slab_->setValue(p.slabT);
    roof_->setValue(p.roofT);
    ftop_->setValue(p.foundTop);
    zeroAbs_->setValue(p.zeroAbs);
    zeroAuto_->setChecked(p.zeroAbsAuto);
    ft_->setValue(p.foundT);
    fover_->setValue(p.foundOverhang);
    strip_->setChecked(p.stripFoundation);
    stripW_->setValue(p.stripWidth);
    lean_->setValue(p.leanT);
    wallT_->setValue(p.wallT);
    clsLean_->setCurrentText(QString::fromStdString(p.classLean));
    clsFound_->setCurrentText(QString::fromStdString(p.classFound));
    clsFrame_->setCurrentText(QString::fromStdString(p.classFrame));
    clsSlab_->setCurrentText(QString::fromStdString(p.classSlab));
    for (int r = 0; r < layers_->rowCount(); ++r) {
        auto it = layers.find(layers_->item(r, 0)->text().toStdString());
        if (it == layers.end()) continue;
        if (auto* cb = qobject_cast<QComboBox*>(layers_->cellWidget(r, 1))) {
            const int k = cb->findData(QString::fromLatin1(bim::kindKey(it->second == ElemKind::Roof ? ElemKind::Slab : it->second)));
            if (k >= 0) cb->setCurrentIndex(k);
        }
    }
    updating_ = false;
    showUpTo_->setRange(0, p.floors);
    showUpTo_->setValue(p.floors);
    recompute();
}

void BuildingDialog::applyTypology(const QString& id) {
    struct T { const char* id; int floors; double h, slab, roof, ft, fover; bool strip; double stripW; const char *frame, *slabC, *found; };
    static const T ts[] = {
        {"frame9", 9, 3.0, 0.20, 0.22, 0.80, 0.6, false, 0.8, "B25", "B25", "B25"},
        {"frame16", 16, 3.0, 0.22, 0.25, 1.20, 1.0, false, 0.8, "B30", "B25", "B30"},
        {"office5", 5, 3.6, 0.22, 0.22, 0.70, 0.5, false, 0.8, "B25", "B25", "B25"},
        {"cottage2", 2, 3.0, 0.18, 0.18, 0.50, 0.3, true, 0.6, "B20", "B20", "B20"},
        {"school3", 3, 3.6, 0.22, 0.22, 0.70, 0.5, false, 0.8, "B30", "B30", "B25"}};
    for (const auto& x : ts) {
        if (id != QLatin1String(x.id)) continue;
        updating_ = true;
        floors_->setValue(x.floors);
        height_->setValue(x.h);
        slab_->setValue(x.slab);
        roof_->setValue(x.roof);
        ft_->setValue(x.ft);
        fover_->setValue(x.fover);
        strip_->setChecked(x.strip);
        stripW_->setValue(x.stripW);
        clsFrame_->setCurrentText(x.frame);
        clsSlab_->setCurrentText(x.slabC);
        clsFound_->setCurrentText(x.found);
        updating_ = false;
        showUpTo_->setRange(0, x.floors);
        showUpTo_->setValue(x.floors);
        recompute();
        return;
    }
}

std::map<std::string, ElemKind> BuildingDialog::layerMap() const {
    std::map<std::string, ElemKind> m;
    for (int r = 0; r < layers_->rowCount(); ++r) {
        ElemKind k = ElemKind::Ignore;
        if (auto* cb = qobject_cast<QComboBox*>(layers_->cellWidget(r, 1))) bim::kindFromKey(cb->currentData().toString().toStdString(), k);
        m[layers_->item(r, 0)->text().toStdString()] = k;
    }
    m["PTO"] = ElemKind::Ignore;
    return m;
}

bim::BuildingParams BuildingDialog::params() const {
    bim::BuildingParams p;
    p.floors = floors_->value();
    p.floorHeight = height_->value();
    p.slabT = slab_->value();
    p.roofT = roof_->value();
    p.foundTop = ftop_->value();
    p.zeroAbs = zeroAbs_->value();
    p.zeroAbsAuto = zeroAuto_->isChecked();
    p.foundT = ft_->value();
    p.foundOverhang = fover_->value();
    p.stripFoundation = strip_->isChecked();
    p.stripWidth = stripW_->value();
    p.leanT = lean_->value();
    p.wallT = wallT_->value();
    p.classLean = clsLean_->currentText().toStdString();
    p.classFound = clsFound_->currentText().toStdString();
    p.classFrame = clsFrame_->currentText().toStdString();
    p.classSlab = clsSlab_->currentText().toStdString();
    return p;
}

bim::PlanInput BuildingDialog::typicalPlan() const { return bim::readPlan(doc_, layerMap()); }

void BuildingDialog::recompute() {
    const auto p = params();
    fp_ = civil::floorPlansFrom(set_->loaded());
    model_ = bim::buildModel(typicalPlan(), p, fp_);
    // PTO
    pto_->setRowCount(0);
    auto row = [&](const QStringList& cells, bool bold) {
        const int r = pto_->rowCount();
        pto_->insertRow(r);
        for (int c = 0; c < cells.size(); ++c) {
            auto* it = new QTableWidgetItem(cells[c]);
            if (c >= 2 && c <= 4) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (bold) {
                QFont f = it->font();
                f.setBold(true);
                it->setFont(f);
            }
            pto_->setItem(r, c, it);
        }
    };
    for (const auto& r : model_.pto)
        row({r.level == 0 ? t("bim.level.found") : t("bim.level.n", {QString::number(r.level)}),
             t((std::string("bim.kind.") + bim::kindKey(r.kind)).c_str()), QString::number(r.count), f2(r.volume),
             r.kind == ElemKind::Lean ? QStringLiteral("-") : f2(r.formwork), QString::fromStdString(r.concrete)},
            false);
    row({t("gui.bim.total"), QString(), QString(), f2(model_.totalVolume), f2(model_.totalFormwork), QString()}, true);
    for (const auto& [c, v] : model_.byConcrete) row({QString(), t("bim.msg.concrete", {QString::fromStdString(c), f2(v)}), {}, {}, {}, {}}, false);
    // tekshiruv
    checks_->clear();
    for (const auto& is : model_.issues) {
        auto* it = new QListWidgetItem(QString::fromStdString(loc_.tr(is.error ? "sewer.lvl.error" : "sewer.lvl.warning") + ": " +
                                                             loc_.tr(is.key, is.args)));
        it->setForeground(is.error ? QColor(232, 70, 80) : QColor(230, 160, 30));
        checks_->addItem(it);
    }
    if (model_.issues.empty()) checks_->addItem(t("gui.bim.ok"));
    if (model_.zeroAbsKnown && zeroAuto_->isChecked() && std::abs(zeroAbs_->value() - model_.zeroAbs) > 1e-9) {
        const bool was = updating_;
        updating_ = true;
        zeroAbs_->setValue(model_.zeroAbs);  // chizmadan olingan qiymat ko'rinsin
        updating_ = was;
    }
    summary_->setText(t("gui.bim.summary", {QString::number(p.floors), QString::number(model_.prisms.size()), f2(model_.totalVolume),
                                            f2(model_.totalFormwork), QString::number(model_.heightTop, 'f', 3)}));
    render();
}

void BuildingDialog::render() {
    const int upTo = showUpTo_->value();
    showLbl_->setText(t("gui.bim.show") + QString::number(upTo));
    view_->beginScene(t("gui.bim.title").section(QString::fromUtf8(" —"), 0, 0),
                      t("gui.bim.summary", {QString::number(floors_->value()), QString::number(model_.prisms.size()), f2(model_.totalVolume),
                                            f2(model_.totalFormwork), QString::number(model_.heightTop, 'f', 3)}));
    for (const auto& pr : model_.prisms) {
        if (pr.level > upTo) continue;
        std::vector<P3> o;
        for (const auto& q : pr.outline) o.push_back({q.x, q.y, 0});
        std::vector<std::vector<P3>> hs;
        for (const auto& h : pr.holes) {
            std::vector<P3> hh;
            for (const auto& q : h) hh.push_back({q.x, q.y, 0});
            hs.push_back(hh);
        }
        const bool slab = pr.kind == ElemKind::Slab || pr.kind == ElemKind::Roof;
        view_->addPrism(o, hs, pr.z0, pr.z1, kindColor(pr.kind), slab && pr.level == upTo && upTo < floors_->value() ? 140 : 255);
    }
    view_->endScene(true);
}

} // namespace asiacad::gui
