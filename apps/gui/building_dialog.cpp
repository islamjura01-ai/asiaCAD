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
#include <QStyle>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <cmath>
#include <QVBoxLayout>

#include <fstream>

#include <asiacad/civil/excel.h>

namespace asiacad::gui {

using bim::ElemKind;

namespace {
const ElemKind kKinds[] = {ElemKind::Ignore, ElemKind::Outline, ElemKind::Column, ElemKind::Wall, ElemKind::Slab,
                           ElemKind::Opening, ElemKind::Stair, ElemKind::Shaft, ElemKind::FoundationSlab, ElemKind::Lean, ElemKind::StripFooting,
                           ElemKind::Infill};
QColor rebarColor(double d) {   // Armatura oynasidagi bilan bir xil: diametr bo'yicha rang
    switch (static_cast<int>(std::lround(d * 1000))) {
        case 6: return QColor(150, 150, 160);  case 8: return QColor(120, 200, 120);  case 10: return QColor(80, 190, 220);
        case 12: return QColor(70, 130, 240);  case 14: return QColor(150, 100, 240); case 16: return QColor(230, 90, 200);
        case 18: return QColor(240, 90, 120);  case 20: return QColor(240, 130, 50);  case 22: return QColor(230, 180, 40);
        case 25: return QColor(220, 60, 50);   default: return QColor(200, 70, 70);
    }
}

QColor kindColor(ElemKind k) {
    switch (k) {
        case ElemKind::Lean: return QColor(128, 126, 120);
        case ElemKind::FoundationSlab: return QColor(150, 140, 122);
        case ElemKind::StripFooting: return QColor(150, 140, 122);
        case ElemKind::Column: return QColor(214, 170, 110);
        case ElemKind::Wall: return QColor(200, 196, 186);
        case ElemKind::Slab: return QColor(176, 186, 198);
        case ElemKind::Roof: return QColor(120, 150, 185);
        case ElemKind::Stair: return QColor(222, 196, 150);
        case ElemKind::Shaft: return QColor(170, 180, 205);
        case ElemKind::Infill: return QColor(228, 214, 190);
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
    // Ko'rsatish: tayyor ko'rinishlar + qatlamlar (har biri yoqib-o'chiriladi)
    {
        auto* pr = new QHBoxLayout;
        pr->setSpacing(4);
        auto* lbl = new QLabel(t("gui.bim.vis"), mid);
        lbl->setObjectName("muted");
        pr->addWidget(lbl);
        const char* presets[3] = {"gui.bim.vis.all", "gui.bim.vis.frame", "gui.bim.vis.rebar"};
        for (int i = 0; i < 3; ++i) {
            auto* b = new QPushButton(t(presets[i]), mid);
            b->setObjectName(QStringLiteral("visPreset%1").arg(i));
            b->setCursor(Qt::PointingHandCursor);
            connect(b, &QPushButton::clicked, this, [this, i] { applyPreset(i); });
            pr->addWidget(b);
        }
        pr->addSpacing(12);
        normBtn_ = new QPushButton(t("gui.bim.norm"), mid);
        normBtn_->setObjectName("visNorm");
        normBtn_->setCheckable(true);
        normBtn_->setCursor(Qt::PointingHandCursor);
        normBtn_->setToolTip(t("gui.bim.norm.tip"));
        normBtn_->setStyleSheet(QStringLiteral("QPushButton#visNorm:checked { background: rgba(229,72,77,60); border-color: #E5484D; color: #FF9A9E; }"));
        connect(normBtn_, &QPushButton::toggled, this, [this](bool on) { runNormCheck(on); });
        pr->addWidget(normBtn_);
        pr->addStretch(1);
        mv->addLayout(pr);
        auto* cr = new QHBoxLayout;
        cr->setSpacing(3);
        const char* keys[GCount] = {"gui.bim.g.found", "gui.bim.g.column", "gui.bim.g.wall", "gui.bim.g.slab",
                                    "gui.bim.g.stair", "gui.bim.g.infill", "gui.bim.g.glass", "gui.bim.g.concrete", "gui.bim.g.rebar"};
        for (int g = 0; g < GCount; ++g) {
            auto* c = new QToolButton(mid);
            c->setText(t(keys[g]));
            c->setCheckable(true);
            c->setChecked(g != GRebar);
            c->setToolTip(g == GRebar ? t("gui.bim.g.rebar_tip") : g == GConcrete ? t("gui.bim.g.concrete_tip") : t("gui.bim.g.tip"));
            c->setObjectName("visChip");
            c->setToolButtonStyle(Qt::ToolButtonTextOnly);
            c->setCursor(Qt::PointingHandCursor);
            c->setStyleSheet(QStringLiteral("QToolButton#visChip { padding: 3px 7px; border-radius: 11px; border: 1px solid rgba(140,160,210,90); }"
                                            "QToolButton#visChip:checked { background: rgba(224,165,38,46); border-color: #E0A526; color: #F1C35A; }"));
            connect(c, &QToolButton::toggled, this, [this] { render(); });
            showChip_[g] = c;
            if (g == GConcrete) cr->addSpacing(10);   // ajratgich: element turlari | beton, armatura qatlamlari
            cr->addWidget(c);
        }
        cr->addStretch(1);
        mv->addLayout(cr);
    }
    view_ = new Model3DView(mid);
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
    props_ = new PropPanel(tabs);
    props_->empty(t("gui.xp.hint"));
    tabs->addTab(props_, t("gui.xp.tab"));
    tabs_ = tabs;
    connect(view_, &Model3DView::objectPicked, this, &BuildingDialog::onPicked);
    split->setSizes({250, 370, 660, 440});

    // ---- past
    auto* bottom = new QHBoxLayout;
    auto* scratch = new QPushButton(t("gui.gen.scratch"), this);
    scratch->setToolTip(t("gui.gen.scratch.tip"));
    bottom->addWidget(scratch);
    connect(scratch, &QPushButton::clicked, this, [this] { emit scratchRequested(); });
    auto* sample = new QPushButton(t("gui.bim.sample"), this);
    sample->setObjectName("sample");
    sample->setToolTip(t("gui.bim.sample.tip"));
    sample->setProperty("accent", true);   // bo'sh chizmada — birinchi qadam
    sample_ = sample;
    bottom->addWidget(sample);
    connect(sample, &QPushButton::clicked, this, &BuildingDialog::sampleRequested);
    auto* open3d = new QPushButton(t("gui.bim.open3d"), this);
    open3d->setObjectName("open3d");
    bottom->addWidget(open3d);
    connect(open3d, &QPushButton::clicked, this, &BuildingDialog::open3DRequested);
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
        autoFloors_ = true;   // qavatlar soni — varaqlar (qo'lda yoki belgilar bo'yicha) va tipik plan bo'yicha
        recompute();
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

void BuildingDialog::fillLayers(bool keep) {
    std::map<std::string, ElemKind> was;   // keep: foydalanuvchi bog'lashlari saqlanadi (yangi qatlamlar — avto)
    if (keep) was = layerMap();
    updating_ = true;
    auto lm = bim::autoLayerMap(doc_);
    for (auto& [name, k] : lm)
        if (const auto it = was.find(name); it != was.end()) k = it->second;
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
    autoFloors_ = true;
    recompute();
}

void BuildingDialog::documentChanged() {
    fillLayers(true);
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
        if (model_.prisms.empty()) emit sampleRequested();   // chizma bo'sh — tip bo'yicha namuna bino
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
    auto p = params();
    rebarDirty_ = true;
    normDirty_ = true;
    std::vector<civil::SheetFloors> placed;
    fp_ = civil::floorPlansFrom(set_->loaded(), p.floorHeight, &placed);
    const auto typical = typicalPlan();
    // tipik qavatlar ("+11.300...+23.400") va avto varaqlar: qavatlar soni yetmasa — oshiriladi (chizma / varaqlar o'qilganda)
    if (autoFloors_) {
        autoFloors_ = false;
        int need = fp_.floors.empty() ? 0 : fp_.floors.rbegin()->first;
        if (const auto tf = bim::typicalFloors(typical, p.floorHeight); tf.count) need = std::max(need, tf.first + tf.count - 1);
        if (need > floors_->value()) {
            const QSignalBlocker b1(floors_), b2(showUpTo_);
            floors_->setValue(need);
            showUpTo_->setRange(0, need);
            showUpTo_->setValue(need);
            p = params();
        }
    }
    set_->setPlacement(placed);
    model_ = bim::buildModel(typical, p, fp_);
    // Xossalar: har prizmaning chizmadagi manbasi (yopiq poliliniya) — o'lcham, joy, tur, nusxa, teshik shu obyektga
    src_.build(doc_, layerMap());
    prismSrc_.clear();
    for (const auto& pr : model_.prisms) prismSrc_.push_back(src_.find(pr));
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
        auto* it = new QListWidgetItem(QString::fromStdString(loc_.tr(is.error ? "sewer.lvl.error" : is.info ? "sewer.lvl.info" : "sewer.lvl.warning") + ": " +
                                                             loc_.tr(is.key, is.args)));
        it->setForeground(is.error ? QColor(232, 70, 80) : is.info ? QColor(90, 170, 240) : QColor(230, 160, 30));
        checks_->addItem(it);
    }
    bool problems = false;
    for (const auto& is : model_.issues) problems |= !is.info;
    if (!problems) checks_->addItem(t("gui.bim.ok"));
    if (model_.zeroAbsKnown && zeroAuto_->isChecked() && std::abs(zeroAbs_->value() - model_.zeroAbs) > 1e-9) {
        const bool was = updating_;
        updating_ = true;
        zeroAbs_->setValue(model_.zeroAbs);  // chizmadan olingan qiymat ko'rinsin
        updating_ = was;
    }
    summary_->setText(t("gui.bim.summary", {QString::number(p.floors), QString::number(model_.prisms.size()), f2(model_.totalVolume),
                                            f2(model_.totalFormwork), QString::number(model_.heightTop, 'f', 3)}) +
                      (model_.windows + model_.doors > 0
                           ? " · " + t("gui.bim.facade", {QString::number(model_.windows), QString::number(model_.doors), f2(model_.masonry), f2(model_.glazing)})
                           : QString()));
    render();
}

int BuildingDialog::groupOf(ElemKind k) {
    switch (k) {
        case ElemKind::Lean: case ElemKind::FoundationSlab: case ElemKind::StripFooting: return GFound;
        case ElemKind::Column: return GColumn;
        case ElemKind::Wall: case ElemKind::Shaft: return GWall;
        case ElemKind::Slab: case ElemKind::Roof: return GSlab;
        case ElemKind::Stair: return GStair;
        case ElemKind::Infill: return GInfill;
        default: return -1;
    }
}

void BuildingDialog::setGroupVisible(int g, bool on) {
    if (g >= 0 && g < GCount && showChip_[g]) showChip_[g]->setChecked(on);
}

bool BuildingDialog::groupVisible(int g) const { return g >= 0 && g < GCount && showChip_[g] && showChip_[g]->isChecked(); }

void BuildingDialog::applyPreset(int preset) {
    for (int g = 0; g < GCount; ++g) {
        if (!showChip_[g]) continue;
        const QSignalBlocker block(showChip_[g]);
        bool on = true;
        if (preset == 0) on = g != GRebar;                                    // hammasi (armaturasiz)
        if (preset == 1) on = g != GInfill && g != GGlass && g != GRebar;      // karkas: monolit beton
        if (preset == 2) on = g != GInfill && g != GGlass && g != GConcrete;   // toza armatura (betonni "Beton" bilan qaytarish mumkin)
        showChip_[g]->setChecked(on);
    }
    render();
}

void BuildingDialog::render() {
    const int upTo = showUpTo_->value();
    showLbl_->setText(t("gui.bim.show") + QString::number(upTo));
    if (normOn_ && normDirty_) computeNorms();
    QString sub = t("gui.bim.summary", {QString::number(floors_->value()), QString::number(model_.prisms.size()), f2(model_.totalVolume),
                                        f2(model_.totalFormwork), QString::number(model_.heightTop, 'f', 3)});
    if (normOn_ && !model_.prisms.empty())   // ShNQ natijasi 3D sarlavhasida ham (xato bo'lmasa ham ko'rinadi)
        sub += "\n" + QString::fromUtf8(normRep_.errors || normRep_.warnings ? "⛔ " : "✔ ") +
               t("gui.bim.norm.head", {QString::number(normRep_.errors), QString::number(normRep_.warnings), QString::number(normRep_.subjects)});
    view_->beginScene(t("gui.bim.title").section(QString::fromUtf8(" —"), 0, 0), sub);
    const bool rebarOn = groupVisible(GRebar);
    if (rebarOn && rebarDirty_) {   // armatura — joriy bino va standart shablonlar bo'yicha (sozlash: "Armatura (3D)")
        rebar_ = bim::buildRebar(model_, params(), bim::defaultRebarSettings());
        rebarDirty_ = false;
    }
    const QColor errC(229, 60, 60), warnC(245, 165, 36);
    keyPrism_.clear();
    std::map<std::pair<int, int>, int> ord;   // (tur, qavat) -> tartib (manbasiz prizmalar kaliti)
    for (const auto& pr : model_.prisms) {
        const int pi = static_cast<int>(&pr - model_.prisms.data());
        const EntityId sid = pi < static_cast<int>(prismSrc_.size()) ? prismSrc_[static_cast<std::size_t>(pi)] : 0;
        const QString key = bimPrismKey(pr, sid, ord[{static_cast<int>(pr.kind), pr.level}]++);
        keyPrism_[key] = pi;
        if (pr.level > upTo) continue;
        if (const int g = groupOf(pr.kind); (g >= 0 && !groupVisible(g)) || !groupVisible(GConcrete)) continue;
        std::vector<P3> o;
        for (const auto& q : pr.outline) o.push_back({q.x, q.y, 0});
        std::vector<std::vector<P3>> hs;
        for (const auto& h : pr.holes) {
            std::vector<P3> hh;
            for (const auto& q : h) hh.push_back({q.x, q.y, 0});
            hs.push_back(hh);
        }
        const bool slab = pr.kind == ElemKind::Slab || pr.kind == ElemKind::Roof;
        const QString kind = QString::fromStdString(loc_.tr(std::string("bim.kind.") + bim::kindKey(pr.kind)));
        const QString lvl = pr.level == 0 ? t("gui.bim.found") : t("gui.bim.level", {QString::number(pr.level)});
        view_->beginObject(kind + " · " + lvl, kind,
                           {t("gui.v3d.p.volume", {QString::number(pr.volume(), 'f', 3)}), t("gui.v3d.p.levels", {QString::number(pr.z0, 'f', 3), QString::number(pr.z1, 'f', 3)}),
                            t("gui.v3d.p.concrete", {QString::fromStdString(pr.concrete)})},
                           key);
        // armatura yoqilgan bo'lsa beton shaffof — sterjenlar ko'rinadi
        QColor pc = kindColor(pr.kind);
        int pa = rebarOn ? 40 : (slab && pr.level == upTo && upTo < floors_->value() ? 140 : 255);
        if (normOn_) {   // ShNQ: buzilgan — qizil / sariq, qolganlari xira
            const auto it = normPrism_.find(static_cast<int>(&pr - model_.prisms.data()));
            if (it != normPrism_.end()) {
                pc = it->second >= 2 ? errC : warnC;
                pa = std::max(pa, rebarOn ? 120 : 235);
            } else {
                pc = QColor::fromHsv(pc.hsvHue(), pc.hsvSaturation() / 3, std::min(255, pc.value() + 20));
                pa = std::min(pa, 45);
            }
        }
        view_->addPrism(o, hs, pr.z0, pr.z1, pc, pa);
        view_->endObject();
    }
    // derazalar va eshiklar (shisha)
    for (const auto& g : model_.glass) {
        if (g.level > upTo || !groupVisible(GGlass)) continue;
        std::vector<P3> q;
        for (const auto& v : g.quad) q.push_back({v.x, v.y, v.z});
        const QString nm = t(g.door ? "gui.v3d.door" : "gui.v3d.window");
        view_->beginObject(nm + " · " + t("gui.bim.level", {QString::number(g.level)}), nm, {},
                           QStringLiteral("g:%1").arg(&g - model_.glass.data()));
        view_->addFace(q, g.door ? QColor(70, 110, 150) : QColor(120, 170, 215), g.door ? 170 : 120, true);
        view_->endObject();
    }
    if (rebarOn) {
        std::size_t nseg = 0;
        for (const auto& b : rebar_.bars) if (b.level <= upTo) nseg += b.pts.size();
        const bool tubes = nseg < 120000;
        for (const auto& b : rebar_.bars) {
            const int bi = static_cast<int>(&b - rebar_.bars.data());
            if (b.level > upTo) continue;
            if (const int g = groupOf(b.kind); g >= 0 && !groupVisible(g)) continue;   // turi o'chirilgan — sterjenlari ham
            std::vector<P3> pts;
            pts.reserve(b.pts.size());
            for (const auto& q : b.pts) pts.push_back({q.x, q.y, q.z});
            const QString dia = QString::fromUtf8("\xC3\x98") + QString::number(std::lround(b.d * 1000)) + " " + QString::fromStdString(b.cls);
            view_->beginObject(dia, t("gui.v3d.cat.rebar", {dia}), {t("gui.v3d.p.length", {QString::number(b.length, 'f', 2)})},
                               QStringLiteral("b:%1").arg(bi));
            QColor c = rebarColor(b.d);
            if (normOn_) {
                const auto it = normBar_.find(bi);
                c = it != normBar_.end() ? (it->second >= 2 ? errC : warnC) : QColor(150, 158, 175, 110);
            }
            if (tubes) view_->addTube(pts, std::max(0.003, b.d / 2), c, b.role == bim::BarRole::Stirrup, b.role == bim::BarRole::Stirrup ? 4 : 6);
            else view_->addPolyline(pts, c, 1.0, b.role == bim::BarRole::Stirrup);
            view_->endObject();
        }
    }
    view_->setEmptyText(t("gui.bim.empty"));
    if (sample_) {   // bino bor — tugma oddiy ko'rinishda
        const bool empty = model_.prisms.empty();
        if (sample_->property("accent").toBool() != empty) {
            sample_->setProperty("accent", empty);
            sample_->style()->unpolish(sample_);
            sample_->style()->polish(sample_);
        }
    }
    view_->endScene(true);
}

void BuildingDialog::onPicked(const QString& key) {
    if (key.isEmpty()) {
        props_->empty(t("gui.xp.hint"));
        return;
    }
    auto run = [this](const QStringList& cmds) { emit commandsRequested(cmds); };
    auto param = [this](QDoubleSpinBox* sb, const char* label, int dec) {   // menyu parametri (shu turdagi hammasi uchun)
        props_->number(QString::fromLatin1(label).section('.', -1), t(label), sb->value(), dec, [sb](double v) { sb->setValue(v); }, "m");
    };
    if (key.startsWith("e:") || key.startsWith("p:")) {
        const auto it = keyPrism_.find(key);
        if (it == keyPrism_.end() || it->second >= static_cast<int>(model_.prisms.size())) {
            props_->empty(t("gui.xp.hint"));
            return;
        }
        const auto& pr = model_.prisms[static_cast<std::size_t>(it->second)];
        const EntityId sid = it->second < static_cast<int>(prismSrc_.size()) ? prismSrc_[static_cast<std::size_t>(it->second)] : 0;
        fillBimElement(*props_, loc_, doc_, layerMap(), pr, sid, run);
        // menyu parametrlari: avtomatik qurilgan elementlar (fundament, padbetonka, plita qalinligi) shu yerdan tuzatiladi
        props_->section(t("gui.xp.params"));
        switch (pr.kind) {
            case ElemKind::Slab: param(slab_, "gui.bim.slab", 2); param(height_, "gui.bim.height", 2); break;
            case ElemKind::Roof: param(roof_, "gui.bim.roof", 2); param(height_, "gui.bim.height", 2); break;
            case ElemKind::Column: case ElemKind::Wall: case ElemKind::Shaft: case ElemKind::Stair: case ElemKind::Infill:
                param(height_, "gui.bim.height", 2);
                if (pr.kind == ElemKind::Wall) param(wallT_, "gui.bim.wallt", 2);
                break;
            case ElemKind::FoundationSlab: case ElemKind::StripFooting:
                param(ftop_, "gui.bim.ftop", 3); param(ft_, "gui.bim.ft", 2);
                if (pr.kind == ElemKind::StripFooting) param(stripW_, "gui.bim.stripw", 2); else param(fover_, "gui.bim.fover", 2);
                break;
            case ElemKind::Lean: param(lean_, "gui.bim.lean", 2); break;
            default: break;
        }
        props_->end();
    } else if (key.startsWith("g:")) {
        const int gi = key.mid(2).toInt();
        if (gi < 0 || gi >= static_cast<int>(model_.glass.size())) return;
        const auto& g = model_.glass[static_cast<std::size_t>(gi)];
        props_->begin(t(g.door ? "gui.v3d.door" : "gui.v3d.window") + " · " + t("gui.bim.level", {QString::number(g.level)}));
        props_->section(t("gui.xp.geom"));
        if (g.quad.size() == 4) {
            props_->info(t("gui.xp.extent"), f2(g.quad[0].distanceTo(g.quad[1])) + QString::fromUtf8(" × ") + f2(g.quad[1].distanceTo(g.quad[2])) + " m");
            props_->info(t("gui.xp.levels"), QString::number(g.quad[0].z, 'f', 3) + QString::fromUtf8(" … ") + QString::number(g.quad[2].z, 'f', 3));
        }
        props_->note(t("gui.xp.facade"));
        props_->end();
    } else if (key.startsWith("b:")) {
        const int bi = key.mid(2).toInt();
        if (bi < 0 || bi >= static_cast<int>(rebar_.bars.size())) return;
        const auto& b = rebar_.bars[static_cast<std::size_t>(bi)];
        props_->begin(QString::fromUtf8("\xC3\x98") + QString::number(std::lround(b.d * 1000)) + " " + QString::fromStdString(b.cls));
        props_->section(t("gui.xp.bar"));
        props_->info(t("gui.xp.elem"), t((std::string("bim.kind.") + bim::kindKey(b.kind)).c_str()) + " · " +
                                         (b.level == 0 ? t("gui.bim.found") : t("gui.bim.level", {QString::number(b.level)})));
        props_->info(t("gui.xp.role"), t((std::string("rebar.role.") + bim::roleKey(b.role)).c_str()));
        props_->info(t("gui.xp.length"), QString::number(b.length, 'f', 2) + " m");
        props_->info(t("gui.xp.mass"), QString::number(b.length * bim::barMassPerM(b.d), 'f', 2) + " kg");
        props_->note(t("gui.xp.rebar_menu"));
        props_->end();
    }
    if (tabs_ && tabs_->currentWidget() != props_) tabs_->setCurrentWidget(props_);
}

void BuildingDialog::setNormRules(const std::vector<bim::NormRule>* rules) {
    normRules_ = rules;
    normDirty_ = true;
    if (normOn_) render();
}

void BuildingDialog::runNormCheck(bool on) {
    if (normBtn_ && normBtn_->isChecked() != on) {
        const QSignalBlocker b(normBtn_);
        normBtn_->setChecked(on);
    }
    normOn_ = on;
    if (on) {
        normDirty_ = true;
        computeNorms();
        // armatura xatolari bo'lsa — sterjenlar ko'rinsin
        if (!normBar_.empty() && !groupVisible(GRebar)) {
            const QSignalBlocker b(showChip_[GRebar]);
            showChip_[GRebar]->setChecked(true);
        }
    } else {
        // ro'yxatdan ShNQ qatorlarini olib tashlash
        for (int i = checks_->count() - 1; i >= 0; --i)
            if (checks_->item(i)->data(Qt::UserRole).toBool()) delete checks_->takeItem(i);
    }
    render();
}

void BuildingDialog::computeNorms() {
    normDirty_ = false;
    normPrism_.clear();
    normBar_.clear();
    normRep_ = {};
    if (!normRules_ || model_.prisms.empty()) return;
    if (rebarDirty_) {
        rebar_ = bim::buildRebar(model_, params(), bim::defaultRebarSettings());
        rebarDirty_ = false;
    }
    std::vector<std::vector<int>> prismsOf;
    std::vector<int> barOf;
    auto m = bim::buildingAsIfc(model_, params(), &rebar_, &prismsOf, &barOf);
    // nomlar (ro'yxat va hisobot uchun)
    auto lvlName = [&](int lv) { return lv == 0 ? t("gui.bim.found") : t("gui.bim.level", {QString::number(lv)}); };
    for (std::size_t i = 0; i < m.elements.size(); ++i) {
        auto& e = m.elements[i];
        const QString kind = QString::fromStdString(loc_.tr(std::string("bim.kind.") + e.objectType));
        e.name = (barOf[i] >= 0 ? kind + " · " + lvlName(e.storey) + " · " + QString::fromStdString(e.name) : kind + " · " + lvlName(e.storey)).toStdString();
    }
    normRulesUsed_ = *normRules_;
    normRep_ = bim::normCheck(m, normRulesUsed_);
    for (const auto& v : normRep_.items) {
        if (v.element < 0 || v.element >= static_cast<int>(m.elements.size())) continue;
        for (int p : prismsOf[static_cast<std::size_t>(v.element)]) normPrism_[p] = std::max(normPrism_[p], v.severity);
        if (const int b = barOf[static_cast<std::size_t>(v.element)]; b >= 0) normBar_[b] = std::max(normBar_[b], v.severity);
    }
    // tekshiruv ro'yxati: ShNQ qatorlari (avvalgilari olib tashlanadi)
    for (int i = checks_->count() - 1; i >= 0; --i)
        if (checks_->item(i)->data(Qt::UserRole).toBool()) delete checks_->takeItem(i);
    auto* head = new QListWidgetItem(t("gui.bim.norm.head", {QString::number(normRep_.errors), QString::number(normRep_.warnings),
                                                             QString::number(normRep_.subjects)}));
    head->setData(Qt::UserRole, true);
    QFont f = head->font();
    f.setBold(true);
    head->setFont(f);
    checks_->insertItem(0, head);
    const std::string lang = loc_.language();
    int row = 1;
    for (const auto& v : normRep_.items) {
        if (row > 300) break;
        const auto& r = normRulesUsed_[static_cast<std::size_t>(v.rule)];
        const auto& e = m.elements[static_cast<std::size_t>(v.element)];
        auto* it = new QListWidgetItem(QString::fromUtf8(v.severity >= 2 ? "⛔ " : "⚠ ") + QString::fromStdString(e.name) + " — " +
                                       QString::fromStdString(r.titleFor(lang)) + ": " + QString::fromStdString(bim::normFormat(v.value, r.unit)) +
                                       (v.belowMin ? " < " : " > ") + QString::fromStdString(bim::normFormat(v.limit, r.unit)) + "  (" +
                                       QString::fromStdString(r.norm + (r.clause.empty() ? std::string() : ", " + r.clause)) +
                                       (r.verified ? QString() : QStringLiteral(" · ") + t("gui.norm.unverified")) + ")");
        it->setForeground(v.severity >= 2 ? QColor(232, 70, 80) : QColor(230, 160, 30));
        it->setData(Qt::UserRole, true);
        checks_->insertItem(row++, it);
    }
}

} // namespace asiacad::gui
