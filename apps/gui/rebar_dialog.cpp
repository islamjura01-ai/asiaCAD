#include "rebar_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <asiacad/civil/excel.h>

#include <cmath>
#include <fstream>
#include <sstream>

namespace asiacad::gui {

using bim::BarRole;
using bim::ElemKind;

namespace {
// Tahrirlanadigan turlar — fundamentdan tomgacha tartibda
const ElemKind kEdit[6] = {ElemKind::FoundationSlab, ElemKind::StripFooting, ElemKind::Column,
                           ElemKind::Wall,           ElemKind::Slab,         ElemKind::Roof};
int editIndex(ElemKind k) {
    for (int i = 0; i < 6; ++i)
        if (kEdit[i] == k) return i;
    return -1;
}
bool isMesh(ElemKind k) { return k == ElemKind::FoundationSlab || k == ElemKind::StripFooting || k == ElemKind::Slab || k == ElemKind::Roof; }
QColor diaColor(double d) {
    const int mm = static_cast<int>(std::lround(d * 1000));
    switch (mm) {
        case 6: return QColor(150, 150, 160);
        case 8: return QColor(120, 200, 120);
        case 10: return QColor(80, 190, 220);
        case 12: return QColor(70, 130, 240);
        case 14: return QColor(150, 100, 240);
        case 16: return QColor(230, 90, 200);
        case 18: return QColor(240, 90, 120);
        case 20: return QColor(240, 130, 50);
        case 22: return QColor(230, 180, 40);
        case 25: return QColor(220, 60, 50);
        default: return mm < 10 ? QColor(140, 160, 140) : QColor(200, 70, 70);
    }
}
QColor kindAccent(ElemKind k) {
    switch (k) {
        case ElemKind::FoundationSlab: case ElemKind::StripFooting: return QColor(150, 140, 122);
        case ElemKind::Column: return QColor(214, 170, 110);
        case ElemKind::Wall: return QColor(170, 166, 156);
        case ElemKind::Slab: return QColor(120, 150, 190);
        case ElemKind::Roof: return QColor(90, 120, 175);
        default: return QColor(160, 160, 160);
    }
}
QString f1(double v) { return QString::number(v, 'f', 1); }
QString f3(double v) { return QString::number(v, 'f', 3); }
} // namespace

RebarDialog::RebarDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent)
    : QDialog(parent), loc_(loc), doc_(doc) {
    setWindowTitle(t("gui.rb.title"));
    resize(1640, 900);
    rs_ = bim::defaultRebarSettings();
    templates_ = bim::builtinRebarTemplates();
    QFile f(templatesPath());
    if (f.open(QIODevice::ReadOnly)) {
        try {
            for (auto& u : bim::templatesFromJson(f.readAll().toStdString())) {
                u.user = true;
                templates_.push_back(u);
            }
        } catch (...) {
        }
    }
    buildUi();
    view_->setDark(dark);
    fillSidebar();
    layers_ = bim::autoLayerMap(doc_);
    loadEditor();
    recompute();
}

QString RebarDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

QString RebarDialog::templatesPath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/rebar_templates.json";
}

void RebarDialog::setBuilding(const bim::BuildingParams& p, const std::map<std::string, ElemKind>& layers, const bim::FloorPlans& fp) {
    bp_ = p;
    layers_ = layers;
    fp_ = fp;
    upTo_->setRange(0, p.floors);
    upTo_->setValue(p.floors);
    recompute();
}

void RebarDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    // ---- 1: yon shablonlar paneli
    side_ = new TemplateSidebar(t("gui.side.title"), t("gui.side.search"), t("gui.side.apply"), t("gui.side.hint"), split);

    // ---- 2: element turi parametrlari
    auto* scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setObjectName("sewerLeft");
    scroll->setMinimumWidth(330);
    auto* left = new QWidget;
    left->setObjectName("sewerLeftBody");
    scroll->setWidget(left);
    auto* lv = new QVBoxLayout(left);
    {
        auto* st = new QLabel(t("gui.rb.source"), left);
        st->setObjectName("title");
        lv->addWidget(st);
        source_ = new QComboBox(left);
        source_->addItem(t("gui.rb.src_tpl"));
        source_->addItem(t("gui.rb.src_kj"));
        lv->addWidget(source_);
        set_ = new DrawingSetWidget(loc_, t("gui.ds.title_kj"),
                                    {{"SLAB", t("bim.kind.SLAB")}, {"ROOF", t("bim.kind.ROOF")}, {"FOUND", t("bim.kind.FOUND")},
                                     {"WALL", t("bim.kind.WALL")}, {"COLUMN", t("bim.kind.COLUMN")},
                                     {"PROJECT", t("gui.ds.role_project")}},
                                    true, left);
        lv->addWidget(set_);
        // KJ loyiha (qolip + armatura bitta faylda): chizmada yo'q qiymatlar
        kjBox_ = new QWidget(left);
        auto* kf = new QFormLayout(kjBox_);
        kf->setContentsMargins(0, 0, 0, 0);
        auto* kt = new QLabel(t("gui.kj.params"), kjBox_);
        kt->setObjectName("title");
        kf->addRow(kt);
        auto kspin = [&](double lo, double hi, double step, int dec, double val) {
            auto* sp = new QDoubleSpinBox(kjBox_);
            sp->setRange(lo, hi);
            sp->setSingleStep(step);
            sp->setDecimals(dec);
            sp->setValue(val);
            connect(sp, &QDoubleSpinBox::valueChanged, this, [this, sp] {
                if (updating_) return;
                if (sp == kjDatum_) kjDatumAuto_->setChecked(false);   // qo'lda kiritildi
                if (sp == kjFTop_) kjFTopAuto_->setChecked(false);
                kp_.datumAuto = kjDatumAuto_->isChecked();
                kp_.datum = kjDatum_->value();
                kp_.foundTopAuto = kjFTopAuto_->isChecked();
                kp_.originX = kjX0_->value();
                kp_.originY = kjY0_->value();
                kp_.rotDeg = kjRot_->value();
                kp_.foundTop = kjFTop_->value();
                kp_.foundT = kjFT_->value();
                kp_.cover = kjCover_->value();
                kp_.outletIn = kjIn_->value();
                kp_.outletOut = kjOut_->value();
                recompute();
            });
            return sp;
        };
        kjDatum_ = kspin(-500, 9000, 0.01, 3, kp_.datum);
        kjDatumAuto_ = new QCheckBox(t("gui.kj.datum_auto"), kjBox_);
        kjDatumAuto_->setChecked(kp_.datumAuto);
        kjFTopAuto_ = new QCheckBox(t("gui.kj.datum_auto"), kjBox_);
        kjFTopAuto_->setChecked(kp_.foundTopAuto);
        for (auto* cb : {kjDatumAuto_, kjFTopAuto_})
            connect(cb, &QCheckBox::toggled, this, [this] {
                if (updating_) return;
                kp_.datumAuto = kjDatumAuto_->isChecked();
                kp_.foundTopAuto = kjFTopAuto_->isChecked();
                recompute();
            });
        kjX0_ = kspin(-1e7, 1e7, 1, 3, kp_.originX);
        kjY0_ = kspin(-1e7, 1e7, 1, 3, kp_.originY);
        kjRot_ = kspin(-360, 360, 0.1, 4, kp_.rotDeg);
        kjFTop_ = kspin(-20, 20, 0.05, 3, kp_.foundTop);
        kjFT_ = kspin(0.1, 3, 0.05, 2, kp_.foundT);
        kjCover_ = kspin(0.01, 0.1, 0.005, 3, kp_.cover);
        kjIn_ = kspin(0, 3, 0.05, 2, kp_.outletIn);
        kjOut_ = kspin(0, 5, 0.05, 2, kp_.outletOut);
        {
            auto* row = new QWidget(kjBox_);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(kjDatum_, 1);
            h->addWidget(kjDatumAuto_);
            kf->addRow(t("gui.kj.datum"), row);
        }
        kf->addRow(t("gui.kj.x0"), kjX0_);
        kf->addRow(t("gui.kj.y0"), kjY0_);
        kf->addRow(t("gui.kj.rot"), kjRot_);
        {
            auto* row = new QWidget(kjBox_);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(kjFTop_, 1);
            h->addWidget(kjFTopAuto_);
            kf->addRow(t("gui.kj.ftop"), row);
        }
        kf->addRow(t("gui.kj.ft"), kjFT_);
        kf->addRow(t("gui.kj.cover"), kjCover_);
        kf->addRow(t("gui.kj.out_in"), kjIn_);
        kf->addRow(t("gui.kj.out_out"), kjOut_);
        kjBox_->setVisible(false);
        lv->addWidget(kjBox_);
        connect(source_, &QComboBox::currentIndexChanged, this, [this] { recompute(); });
        connect(set_, &DrawingSetWidget::changed, this, [this] {
            if (set_->count() > 0 && source_->currentIndex() == 0) source_->setCurrentIndex(1);  // recompute signal orqali
            else recompute();
        });
    }
    auto* lt = new QLabel(t("gui.rb.kind"), left);
    lt->setObjectName("title");
    lv->addWidget(lt);
    kind_ = new QComboBox(left);
    for (ElemKind k : kEdit) kind_->addItem(t((std::string("bim.kind.") + bim::kindKey(k)).c_str()));
    lv->addWidget(kind_);

    auto dspin = [&](double lo, double hi, double step, int dec) {
        auto* s = new QDoubleSpinBox(left);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        return s;
    };
    auto diaSpin = [&] {
        auto* s = dspin(6, 40, 2, 0);
        return s;
    };
    form_ = new QFormLayout;
    int ri = 0;
    auto addRow = [&](const QString& label, QWidget* w) {
        form_->addRow(label, w);
        rowW_[ri++] = w;
    };
    dMain_ = diaSpin();
    addRow(t("gui.rb.main"), dMain_);                      // 0
    n_ = new QSpinBox(left);
    n_->setRange(4, 40);
    n_->setSingleStep(2);
    addRow(t("gui.rb.n"), n_);                              // 1
    dStir_ = diaSpin();
    addRow(t("gui.rb.stir"), dStir_);                       // 2
    sStir_ = dspin(50, 600, 10, 0);
    addRow(t("gui.rb.sstir"), sStir_);                      // 3
    sStirEnd_ = dspin(50, 600, 10, 0);
    addRow(t("gui.rb.sstirend"), sStirEnd_);                // 4
    const char* lk[4] = {"gui.rb.l1", "gui.rb.l2", "gui.rb.l3", "gui.rb.l4"};
    for (int i = 0; i < 4; ++i) {                           // 5..8
        auto* w = new QWidget(left);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        d_[i] = diaSpin();
        s_[i] = dspin(50, 600, 10, 0);
        h->addWidget(d_[i]);
        h->addWidget(s_[i]);
        addRow(t(lk[i]), w);
    }
    top_ = new QCheckBox(t("gui.rb.top"), left);
    addRow(QString(), top_);                                // 9
    chairs_ = dspin(0, 10, 0.1, 1);
    addRow(t("gui.rb.chairs"), chairs_);                    // 10
    ties_ = dspin(0, 20, 0.5, 1);
    addRow(t("gui.rb.ties"), ties_);                        // 11
    cover_ = dspin(10, 100, 5, 0);
    form_->addRow(t("gui.rb.cover"), cover_);
    lap_ = dspin(20, 80, 5, 0);
    form_->addRow(t("gui.rb.lap"), lap_);
    auto* clsW = new QWidget(left);
    auto* ch = new QHBoxLayout(clsW);
    ch->setContentsMargins(0, 0, 0, 0);
    clsMain_ = new QComboBox(clsW);
    clsStir_ = new QComboBox(clsW);
    for (const char* c : {"A240", "A300", "A400", "A600", "A800", "A500C"}) {
        clsMain_->addItem(c);
        clsStir_->addItem(c);
    }
    ch->addWidget(clsMain_);
    ch->addWidget(clsStir_);
    form_->addRow(t("gui.rb.cls"), clsW);
    lv->addLayout(form_);
    legend_ = new QLabel(left);
    legend_->setObjectName("muted");
    legend_->setWordWrap(true);
    lv->addWidget(legend_);
    auto* save = new QPushButton(t("gui.rb.savetpl"), left);
    lv->addWidget(save);
    auto* note = new QLabel(t("gui.rb.note"), left);
    note->setWordWrap(true);
    note->setObjectName("muted");
    lv->addWidget(note);
    lv->addStretch(1);

    // ---- 3: 3D karkas
    auto* mid = new QWidget(split);
    auto* mv = new QVBoxLayout(mid);
    mv->setContentsMargins(6, 0, 6, 0);
    auto* fl = new QHBoxLayout;
    fl->addWidget(new QLabel(t("gui.rb.show"), mid));
    for (int i = 0; i < 6; ++i) {
        showKind_[i] = new QCheckBox(t((std::string("bim.kind.") + bim::kindKey(kEdit[i])).c_str()), mid);
        showKind_[i]->setChecked(true);
        fl->addWidget(showKind_[i]);
    }
    concrete_ = new QCheckBox(t("gui.rb.concrete"), mid);
    concrete_->setChecked(true);
    fl->addWidget(concrete_);
    fl->addStretch(1);
    mv->addLayout(fl);
    view_ = new Pipe3DView(mid);
    mv->addWidget(view_, 1);
    auto* sh = new QHBoxLayout;
    upToLbl_ = new QLabel(mid);
    upTo_ = new QSlider(Qt::Horizontal, mid);
    upTo_->setRange(0, bp_.floors);
    upTo_->setValue(bp_.floors);
    sh->addWidget(upToLbl_);
    sh->addWidget(upTo_, 1);
    mv->addLayout(sh);

    // ---- 4: ведомость, jami, tekshiruv
    auto* tabs = new QTabWidget(split);
    sched_ = new QTableWidget(0, 7, tabs);
    sched_->setHorizontalHeaderLabels({t("gui.rb.col.pos"), t("gui.rb.col.elem"), QString::fromUtf8("Ø"), t("gui.rb.col.cls"),
                                       t("gui.rb.col.len"), t("gui.rb.col.cnt"), t("gui.rb.col.mass")});
    sched_->verticalHeader()->setVisible(false);
    sched_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    sched_->horizontalHeader()->setStretchLastSection(true);
    sched_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    totals_ = new QListWidget(tabs);
    checks_ = new QListWidget(tabs);
    checks_->setWordWrap(true);
    tabs->addTab(sched_, t("gui.rb.sched"));
    tabs->addTab(totals_, t("gui.rb.totals"));
    tabs->addTab(checks_, t("gui.bim.checks"));
    split->setSizes({250, 330, 500, 560});

    // ---- past
    auto* bottom = new QHBoxLayout;
    summary_ = new QLabel(this);
    summary_->setObjectName("title");
    auto* toDwg = new QPushButton(t("gui.rb.todwg"), this);
    toDwg->setObjectName("accent");
    auto* close = new QPushButton(QString::fromUtf8("✕"), this);
    close->setFixedWidth(44);
    bottom->addWidget(summary_, 1);
    auto* xlStatus = new QLabel(this);
    xlStatus->setObjectName("muted");
    bottom->addWidget(xlStatus);
    excel_ = new ExcelLink(
        loc_, this, "armatura_vedomost", [this] { return excel::rebarWorkbook(bp_, layers_, bm_, rs_, rm_, [this](const std::string& k, const std::vector<std::string>& a) { return loc_.tr(k, a); }); },
        [this](const xlsx::Workbook& wb) {
            excel::rebarFromWorkbook(wb, bp_, layers_, rs_, [this](const std::string& k, const std::vector<std::string>& a) { return loc_.tr(k, a); });
            upTo_->setRange(0, bp_.floors);
            upTo_->setValue(bp_.floors);
            loadEditor();
            recompute();
        });
    excel_->setStatusLabel(xlStatus);
    excel_->addButtons(bottom);
    bottom->addWidget(toDwg);
    bottom->addWidget(close);
    root->addLayout(bottom);

    connect(side_, &TemplateSidebar::templateActivated, this, &RebarDialog::applyTemplate);
    connect(kind_, &QComboBox::currentIndexChanged, this, [this] { loadEditor(); });
    for (auto* s : {dMain_, dStir_, sStir_, sStirEnd_, chairs_, ties_, cover_, lap_, d_[0], d_[1], d_[2], d_[3], s_[0], s_[1], s_[2], s_[3]})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) { storeEditor(); recompute(); } });
    connect(n_, &QSpinBox::valueChanged, this, [this] { if (!updating_) { storeEditor(); recompute(); } });
    connect(top_, &QCheckBox::toggled, this, [this] { if (!updating_) { storeEditor(); recompute(); } });
    for (auto* c : {clsMain_, clsStir_})
        connect(c, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) { storeEditor(); recompute(); } });
    for (auto* c : showKind_) connect(c, &QCheckBox::toggled, this, [this] { render(); });
    connect(concrete_, &QCheckBox::toggled, this, [this] { render(); });
    connect(upTo_, &QSlider::valueChanged, this, [this] { render(); });
    connect(save, &QPushButton::clicked, this, &RebarDialog::saveTemplate);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(toDwg, &QPushButton::clicked, this, [this] {
        std::string js = bim::paramsToJson(bp_, layers_);
        const auto p = js.find_last_of('}');
        if (p != std::string::npos) js = js.substr(0, p) + ",\"rebar\":" + bim::rebarToJson(rs_) + "}";
        const QString path = QDir::temp().filePath("asiacad-rebar.json");
        std::ofstream f(path.toStdString(), std::ios::binary | std::ios::trunc);
        f << js;
        f.close();
        emit scheduleRequested(path);
    });
}

void RebarDialog::fillSidebar() {
    side_->clear();
    for (ElemKind k : kEdit) {
        bool any = false;
        for (int i = 0; i < static_cast<int>(templates_.size()); ++i) {
            const auto& tp = templates_[i];
            if (tp.kind != k) continue;
            if (!any) side_->addSection(t((std::string("bim.kind.") + bim::kindKey(k)).c_str()));
            any = true;
            const QString name = tp.name.rfind("rtpl.", 0) == 0 ? t(tp.name.c_str()) : QString::fromStdString(tp.name);
            // "Ustun · 4Ø16 + ..." — nuqtadan keyingi qism izoh sifatida
            const QString head = name.section(QString::fromUtf8(" · "), 0, 0);
            const QString tail = name.section(QString::fromUtf8(" · "), 1);
            side_->addTemplate(tail.isEmpty() ? head : tail, t("gui.rb.coversub", {QString::number(std::lround(tp.cover * 1000))}), i,
                               kindAccent(k), tp.user);
        }
    }
}

void RebarDialog::selectKind(ElemKind k) {
    const int i = editIndex(k);
    if (i >= 0) kind_->setCurrentIndex(i);
}

void RebarDialog::applyTemplate(const QVariant& data) {
    const int i = data.toInt();
    if (i < 0 || i >= static_cast<int>(templates_.size())) return;
    rs_.byKind[templates_[i].kind] = templates_[i];
    if (editIndex(templates_[i].kind) == kind_->currentIndex()) loadEditor();
    else selectKind(templates_[i].kind);  // loadEditor() signal orqali
    recompute();
}

void RebarDialog::loadEditor() {
    const ElemKind k = kEdit[std::max(0, kind_->currentIndex())];
    const auto& tp = rs_.byKind[k];
    updating_ = true;
    const bool col = k == ElemKind::Column, wall = k == ElemKind::Wall, mesh = isMesh(k);
    const bool vis[12] = {col, col, col, col, col, !col, !col, mesh, mesh, !col, mesh, wall};
    for (int i = 0; i < 12; ++i) form_->setRowVisible(rowW_[i], vis[i]);
    dMain_->setValue(tp.d1 * 1000);
    n_->setValue(tp.nMain);
    dStir_->setValue(tp.dStir * 1000);
    sStir_->setValue(tp.sStir * 1000);
    sStirEnd_->setValue(tp.sStirEnd * 1000);
    const double dd[4] = {tp.d1, tp.d2, tp.d3, tp.d4}, ss[4] = {tp.s1, tp.s2, tp.s3, tp.s4};
    for (int i = 0; i < 4; ++i) {
        d_[i]->setValue(dd[i] * 1000);
        s_[i]->setValue(ss[i] * 1000);
    }
    top_->setChecked(tp.topMesh);
    chairs_->setValue(tp.chairsPerM2);
    ties_->setValue(tp.tiesPerM2);
    cover_->setValue(tp.cover * 1000);
    lap_->setValue(tp.lapD);
    clsMain_->setCurrentText(QString::fromStdString(tp.clsMain));
    clsStir_->setCurrentText(QString::fromStdString(tp.clsStir));
    legend_->setText(col ? QString() : t(wall ? "gui.rb.legend_wall" : "gui.rb.legend_mesh"));
    updating_ = false;
}

void RebarDialog::storeEditor() {
    const ElemKind k = kEdit[std::max(0, kind_->currentIndex())];
    auto& tp = rs_.byKind[k];
    tp.kind = k;
    if (k == ElemKind::Column) {
        tp.d1 = dMain_->value() / 1000;
        tp.nMain = n_->value();
        tp.dStir = dStir_->value() / 1000;
        tp.sStir = sStir_->value() / 1000;
        tp.sStirEnd = sStirEnd_->value() / 1000;
    } else {
        double* dd[4] = {&tp.d1, &tp.d2, &tp.d3, &tp.d4};
        double* ss[4] = {&tp.s1, &tp.s2, &tp.s3, &tp.s4};
        const int n = k == ElemKind::Wall ? 2 : 4;
        for (int i = 0; i < n; ++i) {
            *dd[i] = d_[i]->value() / 1000;
            *ss[i] = s_[i]->value() / 1000;
        }
        tp.topMesh = top_->isChecked();
        tp.chairsPerM2 = chairs_->value();
        tp.tiesPerM2 = ties_->value();
    }
    tp.cover = cover_->value() / 1000;
    tp.lapD = lap_->value();
    tp.clsMain = clsMain_->currentText().toStdString();
    tp.clsStir = clsStir_->currentText().toStdString();
}

void RebarDialog::saveTemplate() {
    storeEditor();
    bool ok = false;
    const QString name = QInputDialog::getText(this, t("gui.rb.savetpl"), t("gui.rb.tplname"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    const ElemKind k = kEdit[std::max(0, kind_->currentIndex())];
    auto tp = rs_.byKind[k];
    tp.user = true;
    tp.name = (t((std::string("bim.kind.") + bim::kindKey(k)).c_str()) + QString::fromUtf8(" · ") + name).toStdString();
    tp.id = "user_" + std::to_string(templates_.size());
    templates_.push_back(tp);
    std::vector<bim::RebarTemplate> users;
    for (const auto& x : templates_)
        if (x.user) users.push_back(x);
    QFile f(templatesPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QByteArray::fromStdString(bim::templatesToJson(users)));
    fillSidebar();
}

void RebarDialog::setShowConcrete(bool on) { concrete_->setChecked(on); render(); }
void RebarDialog::setUpTo(int level) { upTo_->setValue(level); render(); }

void RebarDialog::setSourceKj(bool kj) { source_->setCurrentIndex(kj ? 1 : 0); }

void RebarDialog::recompute() {
    bm_ = bim::buildModel(bim::readPlan(doc_, layers_), bp_, fp_);
    imp_ = {};
    if (source_ && source_->currentIndex() == 1) {
        // KJ chizmalardan: chizilgan sterjenlar + spetsifikatsiya bilan taqqoslash
        std::vector<bim::RebarSheetInput> in;
        const auto sheets = set_->loaded();
        for (const auto& sh : sheets) {
            if (!sh.error.empty() || !sh.doc) continue;
            ElemKind k = ElemKind::Slab;
            if (sh.file.role == "ROOF") k = ElemKind::Roof;
            else if (sh.file.role == "FOUND") k = ElemKind::FoundationSlab;
            else if (sh.file.role == "WALL") k = ElemKind::Wall;
            else if (sh.file.role == "COLUMN") k = ElemKind::Column;
            in.push_back({sh.doc.get(), k, sh.file.floorFrom, sh.file.floorTo, civil::fileName(sh.file.path)});
        }
        // bitta faylda butun KJ loyiha (o'qlar bilan ko'rinishlar) — "PROJECT" roli yoki avto aniqlash
        projectMode_ = false;
        for (const auto& sh : sheets) {
            if (!sh.error.empty() || !sh.doc) continue;
            bool proj = sh.file.role == "PROJECT";
            if (!proj) {
                int known = 0;
                for (const auto& v : bim::detectKjViews(*sh.doc, sh.dims)) known += v.kind != bim::KjViewKind::Other;
                proj = known >= 3;
            }
            if (!proj) continue;
            kj_ = bim::buildKjProject(*sh.doc, sh.dims, kp_);
            projectMode_ = true;
            break;
        }
        if (kjBox_) kjBox_->setVisible(projectMode_);
        if (projectMode_ && kjDatum_) {  // qo'llanilgan qiymatlar (chizmadan olingan bo'lsa ham) maydonlarda ko'rinsin
            const bool was = updating_;
            updating_ = true;
            kjDatum_->setValue(kj_.datum);
            kjFTop_->setValue(kj_.foundTop);
            updating_ = was;
        }
        if (projectMode_) {
            bm_ = kj_.bm;
            rm_ = {};
            rm_.bars = kj_.bars;
            rm_.schedule = kj_.schedule;
            rm_.massByDia = kj_.massByDia;
            rm_.totalMass = kj_.totalMass;
            rm_.issues = kj_.issues;
            in.clear();
        }
        if (!projectMode_) imp_ = bim::importRebar(in, bm_, rs_.byKind.count(ElemKind::Slab) ? rs_.byKind.at(ElemKind::Slab).cover : 0.025);
        if (!projectMode_) {
        rm_ = {};
        rm_.bars = imp_.bars;
        rm_.schedule = imp_.schedule;
        int pos = 0;
        for (auto& r : rm_.schedule)
            if (r.pos == 0) r.pos = ++pos;
        rm_.massByDia = imp_.massByDia;
        rm_.totalMass = imp_.totalMass;
        rm_.issues = imp_.issues;
        }
    } else {
        projectMode_ = false;
        if (kjBox_) kjBox_->setVisible(false);
        rm_ = bim::buildRebar(bm_, bp_, rs_);
    }
    auto levelName = [&](int lv) { return lv == 0 ? t("bim.level.found") : t("bim.level.n", {QString::number(lv)}); };
    sched_->setRowCount(0);
    auto row = [&](const QStringList& cells, bool bold) {
        const int r = sched_->rowCount();
        sched_->insertRow(r);
        for (int c = 0; c < cells.size(); ++c) {
            auto* it = new QTableWidgetItem(cells[c]);
            if (c >= 4) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (bold) {
                QFont f = it->font();
                f.setBold(true);
                it->setFont(f);
            }
            sched_->setItem(r, c, it);
        }
    };
    long nbars = 0;
    for (const auto& r : rm_.schedule) {
        nbars += r.count;
        row({QString::number(r.pos),
             levelName(r.level) + " · " + t((std::string("bim.kind.") + bim::kindKey(r.kind)).c_str()) + " · " +
                 t((std::string("rebar.role.") + bim::roleKey(r.role)).c_str()),
             QString::number(std::lround(r.d * 1000)), QString::fromStdString(r.cls), QString::number(r.length, 'f', 2), QString::number(r.count),
             f1(r.mass)},
            false);
    }
    row({QString(), t("gui.bim.total"), {}, {}, {}, QString::number(nbars), f1(rm_.totalMass)}, true);

    totals_->clear();
    for (const auto& [dia, kg] : rm_.massByDia) {
        auto* it = new QListWidgetItem(QString::fromStdString(dia) + QString::fromUtf8(" — ") + f1(kg) + " kg");
        const QString num = QString::fromStdString(dia).section(' ', 0, 0).mid(1);
        it->setIcon([&] {
            QPixmap pm(14, 14);
            pm.fill(diaColor(num.toDouble() / 1000));
            return QIcon(pm);
        }());
        totals_->addItem(it);
    }
    totals_->addItem(t("gui.bim.total") + ": " + f3(rm_.totalMass / 1000) + " t");
    totals_->addItem(t("rebar.tbl.wire") + ": " + f1(rm_.wireKg) + " kg");
    totals_->addItem(t("rebar.role.CHAIR") + ": " + QString::number(rm_.chairs));
    totals_->addItem(t("rebar.role.TIE") + ": " + QString::number(rm_.ties));
    if (source_ && source_->currentIndex() == 1 && projectMode_) {
        {
            auto* it = new QListWidgetItem(kj_.datumKnown ? t("gui.kj.abs", {QString::number(kj_.datum, 'f', 3), QString::number(kj_.datum + kj_.foundTop, 'f', 3),
                                                                         QString::number(kj_.datum + kj_.slabTop, 'f', 3)})
                                                          : t("gui.kj.rel"));
            it->setForeground(kj_.datumKnown ? QColor(80, 200, 120) : QColor(232, 70, 80));
            totals_->addItem(it);
        }
        totals_->addItem(t("gui.kj.concrete", {f1(bm_.totalVolume), f1(bm_.totalFormwork)}));
        for (const auto& r : bm_.pto)
            totals_->addItem(QStringLiteral("  %1: %2 — %3 m³")
                                 .arg(t((std::string("bim.kind.") + bim::kindKey(r.kind)).c_str()))
                                 .arg(r.count)
                                 .arg(f1(r.volume)));
        int known = 0;
        for (const auto& v : kj_.views) known += v.kind != bim::KjViewKind::Other;
        totals_->addItem(t("gui.kj.views", {QString::number(known)}));
        for (const auto& v : kj_.views) {
            if (v.kind == bim::KjViewKind::Other) continue;
            totals_->addItem(QStringLiteral("  %1 — %2").arg(t((std::string("kj.view.") + bim::kjViewKey(v.kind)).c_str()),
                                                            QString::fromStdString(v.title).trimmed()));
        }
    } else if (source_ && source_->currentIndex() == 1) {
        totals_->addItem(t("gui.rb.kj_marks", {QString::number(imp_.marks), QString::number(imp_.unlabeled)}));
        totals_->addItem(t("gui.rb.kj_spec", {f1(imp_.specMass), f1(imp_.totalMass)}));
        for (const auto& r : imp_.spec)
            totals_->addItem(QStringLiteral("  %1 %2: Ø%3 %4 L=%5 × %6 = %7 kg")
                                 .arg(t("gui.rb.col.pos"))
                                 .arg(r.pos)
                                 .arg(std::lround(r.d * 1000))
                                 .arg(QString::fromStdString(r.cls))
                                 .arg(r.length, 0, 'f', 2)
                                 .arg(r.count)
                                 .arg(f1(r.mass)));
    }

    checks_->clear();
    for (const auto& is : rm_.issues) {
        auto* it = new QListWidgetItem(QString::fromStdString(loc_.tr(is.error ? "sewer.lvl.error" : "sewer.lvl.warning") + ": " +
                                                             loc_.tr(is.key, is.args)));
        it->setForeground(is.error ? QColor(232, 70, 80) : QColor(230, 160, 30));
        checks_->addItem(it);
    }
    if (rm_.issues.empty()) checks_->addItem(t("gui.bim.ok"));
    summary_->setText(t("gui.rb.summary", {QString::number(nbars), f3(rm_.totalMass / 1000), f1(rm_.wireKg), QString::number(rm_.nodes)}));
    render();
}

void RebarDialog::render() {
    const int upTo = upTo_->value();
    upToLbl_->setText(t("gui.rb.upto") + QString::number(upTo));
    bool show[6];
    for (int i = 0; i < 6; ++i) show[i] = showKind_[i]->isChecked();
    auto visible = [&](ElemKind k, int lv) {
        const int i = editIndex(k);
        return lv <= upTo && (i < 0 || show[i]);
    };
    view_->beginScene(t("gui.rb.title"), summary_->text());
    if (concrete_->isChecked())
        for (const auto& pr : bm_.prisms) {
            if (pr.kind == ElemKind::Lean || !visible(pr.kind, pr.level)) continue;
            std::vector<P3> o;
            for (const auto& q : pr.outline) o.push_back({q.x, q.y, 0});
            std::vector<std::vector<P3>> hs;
            for (const auto& h : pr.holes) {
                std::vector<P3> hh;
                for (const auto& q : h) hh.push_back({q.x, q.y, 0});
                hs.push_back(hh);
            }
            view_->addPrism(o, hs, pr.z0, pr.z1, kindAccent(pr.kind), 38);
        }
    for (const auto& b : rm_.bars) {
        if (!visible(b.kind, b.level)) continue;
        std::vector<P3> pts;
        pts.reserve(b.pts.size());
        for (const auto& p : b.pts) pts.push_back({p.x, p.y, p.z});
        view_->addPolyline(pts, diaColor(b.d), b.role == BarRole::Stirrup ? 1.0 : std::clamp(b.d * 110, 1.0, 3.0),
                           b.role == BarRole::Stirrup);
    }
    view_->endScene(true);
}

} // namespace asiacad::gui
