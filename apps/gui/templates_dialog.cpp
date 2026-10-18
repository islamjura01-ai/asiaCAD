#include "templates_dialog.h"

#include <asiacad/bim/steel.h>
#include <asiacad/core/error.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>

namespace asiacad::gui {

namespace {
QString num(double v, int dec = 4) {
    QString s = QString::number(v, 'f', dec);
    if (s.contains('.')) {
        while (s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s.chop(1);
    }
    return s;
}
QComboBox* profileCombo(const QString& def, QWidget* parent) {
    auto* c = new QComboBox(parent);
    c->setEditable(true);
    for (const auto& p : bim::steelCatalog()) c->addItem(QString::fromStdString(p.name));
    c->setCurrentText(def);
    return c;
}
}  // namespace

QDoubleSpinBox* TemplatesDialog::dbl(QFormLayout* f, const char* key, double v, double lo, double hi, double step, int dec) {
    auto* s = new QDoubleSpinBox(this);
    s->setRange(lo, hi);
    s->setDecimals(dec);
    s->setSingleStep(step);
    s->setValue(v);
    s->setObjectName(QString::fromLatin1(key).section('.', -1));
    f->addRow(t(key), s);
    connect(s, &QDoubleSpinBox::valueChanged, this, [this] { update_(); });
    return s;
}

TemplatesDialog::TemplatesDialog(const i18n::Localizer& loc, bool dark, QWidget* parent) : GenBase(loc, dark, parent) {
    setWindowTitle(t("gui.tpl.title"));
    resize(1040, 640);
    auto* root = new QHBoxLayout(this);
    auto* split = new QSplitter(this);
    root->addWidget(split);
    auto* left = new QWidget(split);
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(left);
    lv->addWidget(tabs_, 1);
    auto page = [&](const char* title) {
        auto* sc = new QScrollArea(tabs_);
        sc->setWidgetResizable(true);
        sc->setFrameShape(QFrame::NoFrame);
        auto* w = new QWidget(sc);
        w->setObjectName("tplPage");
        w->setAttribute(Qt::WA_StyledBackground, true);
        w->setStyleSheet("#tplPage { background: transparent; }");
        sc->viewport()->setAutoFillBackground(false);
        sc->setStyleSheet("QScrollArea { background: transparent; }");
        auto* f = new QFormLayout(w);
        f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        sc->setWidget(w);
        tabs_->addTab(sc, t(title));
        return f;
    };
    auto combo = [&](QFormLayout* f, const char* key, QStringList items) {
        auto* c = new QComboBox(this);
        c->addItems(items);
        f->addRow(t(key), c);
        connect(c, &QComboBox::currentIndexChanged, this, [this] { update_(); });
        return c;
    };
    auto check = [&](QFormLayout* f, const char* key, bool on) {
        auto* c = new QCheckBox(t(key), this);
        c->setChecked(on);
        f->addRow(QString(), c);
        connect(c, &QCheckBox::toggled, this, [this] { update_(); });
        return c;
    };
    // ---- metall karkas
    {
        auto* f = page("gui.tpl.tab.steel");
        sfType_ = combo(f, "gui.tpl.sf.type", {t("gui.tpl.sf.multi"), t("gui.tpl.sf.portal")});
        sfStack_ = new QStackedWidget(this);
        auto* m = new QWidget(sfStack_);
        auto* mf = new QFormLayout(m);
        mf->setContentsMargins(0, 0, 0, 0);
        sfDx_ = new QLineEdit("6x3", m);
        sfDy_ = new QLineEdit("6x2", m);
        mf->addRow(t("gui.tpl.sf.dx"), sfDx_);
        mf->addRow(t("gui.tpl.sf.dy"), sfDy_);
        connect(sfDx_, &QLineEdit::textChanged, this, [this] { update_(); });
        connect(sfDy_, &QLineEdit::textChanged, this, [this] { update_(); });
        sfFloors_ = new QSpinBox(m);
        sfFloors_->setRange(1, 60);
        sfFloors_->setValue(2);
        mf->addRow(t("gui.tpl.sf.floors"), sfFloors_);
        connect(sfFloors_, &QSpinBox::valueChanged, this, [this] { update_(); });
        sfFloorH_ = dbl(mf, "gui.tpl.sf.floorH", 3.6, 2.0, 12, 0.1, 2);
        auto* pp = new QWidget(sfStack_);
        auto* pf = new QFormLayout(pp);
        pf->setContentsMargins(0, 0, 0, 0);
        sfSpan_ = dbl(pf, "gui.tpl.sf.span", 18, 6, 120, 1, 2);
        sfBays_ = new QSpinBox(pp);
        sfBays_->setRange(1, 60);
        sfBays_->setValue(6);
        pf->addRow(t("gui.tpl.sf.bays"), sfBays_);
        connect(sfBays_, &QSpinBox::valueChanged, this, [this] { update_(); });
        sfBay_ = dbl(pf, "gui.tpl.sf.bay", 6, 3, 18, 0.5, 2);
        sfEave_ = dbl(pf, "gui.tpl.sf.eave", 6, 2.5, 40, 0.1, 2);
        sfRidge_ = dbl(pf, "gui.tpl.sf.ridge", 7.5, 2.5, 50, 0.1, 2);
        sfPurlin_ = dbl(pf, "gui.tpl.sf.purlin", 1.5, 0.5, 6, 0.1, 2);
        sfStack_->addWidget(m);
        sfStack_->addWidget(pp);
        f->addRow(sfStack_);
        auto prof = [&](const char* key, const char* def) {
            auto* c = profileCombo(def, this);
            f->addRow(t(key), c);
            connect(c, &QComboBox::currentTextChanged, this, [this] { update_(); });
            return c;
        };
        pCol_ = prof("gui.tpl.sf.column", "I40");
        pBeam_ = prof("gui.tpl.sf.beam", "I30");
        pBeam2_ = prof("gui.tpl.sf.beam2", "I20");
        pRaf_ = prof("gui.tpl.sf.rafter", "I40");
        pPur_ = prof("gui.tpl.sf.purlinp", "[16");
        pBr_ = prof("gui.tpl.sf.brace", "L75x6");
        sfBracing_ = check(f, "gui.tpl.sf.bracing", true);
        sfType_->setCurrentIndex(1);
    }
    // ---- zina
    {
        auto* f = page("gui.tpl.tab.stair");
        stType_ = combo(f, "gui.tpl.st.type", {t("gui.tpl.st.u"), t("gui.tpl.st.straight")});
        stH_ = dbl(f, "gui.tpl.st.H", 3.3, 0.5, 8, 0.05, 3);
        stW_ = dbl(f, "gui.tpl.st.width", 1.2, 0.6, 4, 0.05, 2);
        stGap_ = dbl(f, "gui.tpl.st.gap", 0.1, 0, 2, 0.05, 2);
        stTread_ = dbl(f, "gui.tpl.st.tread", 0.30, 0.2, 0.45, 0.01, 3);
        stRiser_ = dbl(f, "gui.tpl.st.riser", 0.17, 0.1, 0.22, 0.005, 3);
        stLanding_ = dbl(f, "gui.tpl.st.landing", 1.5, 0.6, 5, 0.05, 2);
        stWaist_ = dbl(f, "gui.tpl.st.waist", 0.15, 0.08, 0.4, 0.01, 2);
        stSteel_ = check(f, "gui.tpl.st.steel", false);
        stStringer_ = profileCombo("[16", this);
        f->addRow(t("gui.tpl.st.stringer"), stStringer_);
        connect(stStringer_, &QComboBox::currentTextChanged, this, [this] { update_(); });
    }
    // ---- rampa
    {
        auto* f = page("gui.tpl.tab.ramp");
        rpKind_ = combo(f, "gui.tpl.rp.kind", {t("gui.tpl.rp.car"), t("gui.tpl.rp.ped")});
        rpH_ = dbl(f, "gui.tpl.rp.H", 3.3, 0.2, 12, 0.05, 3);
        rpW_ = dbl(f, "gui.tpl.rp.width", 3.5, 0.9, 12, 0.1, 2);
        rpSlope_ = dbl(f, "gui.tpl.rp.slope", 15, 1, 25, 0.5, 1);
        rpTrans_ = dbl(f, "gui.tpl.rp.trans", 3.6, 0, 10, 0.1, 2);
        rpEvery_ = dbl(f, "gui.tpl.rp.every", 0.8, 0.2, 3, 0.05, 2);
        rpLand_ = dbl(f, "gui.tpl.rp.landing", 1.5, 0.9, 5, 0.1, 2);
        rpWall_ = dbl(f, "gui.tpl.rp.wallT", 0.25, 0.1, 1, 0.05, 2);
        rpPar_ = dbl(f, "gui.tpl.rp.parapet", 0.3, 0, 1.5, 0.05, 2);
        rpFloor_ = dbl(f, "gui.tpl.rp.floorL", 6, 0, 30, 0.5, 2);
        connect(rpKind_, &QComboBox::currentIndexChanged, this, [this](int k) {  // tur bo'yicha odatiy qiymatlar
            const QSignalBlocker b1(rpSlope_), b2(rpW_);
            rpSlope_->setValue(k == 0 ? 15 : 8);
            rpW_->setValue(k == 0 ? 3.5 : 1.5);
            update_();
        });
    }
    // ---- tirgak devor
    {
        auto* f = page("gui.tpl.tab.rwall");
        rwType_ = combo(f, "gui.tpl.rw.type", {t("gui.tpl.rw.l"), t("gui.tpl.rw.t"), t("gui.tpl.rw.g")});
        rwType_->setCurrentIndex(1);
        rwH_ = dbl(f, "gui.tpl.rw.H", 3, 0.5, 15, 0.1, 2);
        rwTop_ = dbl(f, "gui.tpl.rw.tTop", 0.25, 0.1, 2, 0.05, 2);
        rwBot_ = dbl(f, "gui.tpl.rw.tBot", 0.4, 0.1, 4, 0.05, 2);
        rwB_ = dbl(f, "gui.tpl.rw.B", 2.1, 0.3, 12, 0.05, 2);
        rwToe_ = dbl(f, "gui.tpl.rw.toe", 0.5, 0, 4, 0.05, 2);
        rwBase_ = dbl(f, "gui.tpl.rw.hBase", 0.4, 0.2, 2, 0.05, 2);
        rwLen_ = dbl(f, "gui.tpl.rw.length", 20, 0.5, 500, 1, 2);
        rwPhi_ = dbl(f, "gui.tpl.rw.phi", 30, 10, 45, 1, 1);
        rwGamma_ = dbl(f, "gui.tpl.rw.gamma", 18, 12, 23, 0.5, 1);
        rwQ_ = dbl(f, "gui.tpl.rw.q", 10, 0, 100, 1, 1);
        rwMu_ = dbl(f, "gui.tpl.rw.mu", 0.5, 0.2, 0.8, 0.05, 2);
        rwR_ = dbl(f, "gui.tpl.rw.R", 200, 50, 1000, 10, 0);
        connect(rwType_, &QComboBox::currentIndexChanged, this, [this](int k) {  // gravitatsion — yo'g'on old yuz
            const QSignalBlocker b1(rwTop_), b2(rwB_);
            if (k == 2) { rwTop_->setValue(0.5); rwB_->setValue(std::max(1.2, rwH_->value() * 0.45)); }
            update_();
        });
    }
    // ---- joylash (haqiqiy koordinata va dengiz sathidan belgi)
    auto* place = new QGroupBox(t("gui.tpl.place"), left);
    auto* pg = new QFormLayout(place);
    auto big = [&](const char* key, double lo, double hi, int dec) {
        auto* s = new QDoubleSpinBox(place);
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setObjectName(QString::fromLatin1(key).section('.', -1));
        pg->addRow(t(key), s);
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { update_(); });
        return s;
    };
    atX_ = big("gui.tpl.at.x", -1e8, 1e8, 3);
    atY_ = big("gui.tpl.at.y", -1e8, 1e8, 3);
    atRot_ = big("gui.tpl.at.rot", -360, 360, 3);
    atZ_ = big("gui.tpl.at.z", -500, 9000, 3);
    auto* pick = new QPushButton(t("gui.tpl.pick"), place);
    pick->setObjectName("pick");
    pick->setToolTip(t("gui.tpl.pick.tip"));
    pg->addRow(pick);
    connect(pick, &QPushButton::clicked, this, [this] { emit pickRequested(2, {"gui.prompt.tplbase", "gui.prompt.tpldir"}); });
    lv->addWidget(place);
    // ---- o'ng: plan / 3D, xulosa
    auto* right = new QWidget(split);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    views_ = new QTabWidget(right);
    plan_ = new PlanPreview(views_);
    plan_->setDark(dark);
    view3d_ = new DriveView(views_);
    view3d_->setRenderScale(1.0);
    view3d_->setFov(50);
    view3d_->setScene(&scene_);
    views_->addTab(view3d_, t("gui.tpl.view3d"));
    views_->addTab(plan_, t("gui.tpl.viewplan"));
    rv->addWidget(views_, 3);
    connect(view3d_, &DriveView::lookChanged, this, [this] {  // sichqoncha — orbita (markaz atrofida)
        orbitYaw_ += view3d_->yaw();
        orbitPitch_ = std::clamp(orbitPitch_ - view3d_->pitch(), 5.0, 85.0);
        view3d_->setLook(0, 0);
        const double y = orbitYaw_ * M_PI / 180, p = orbitPitch_ * M_PI / 180;
        view3d_->setCamera(center_ + Vec3{-std::cos(p) * std::cos(y), -std::cos(p) * std::sin(y), std::sin(p)} * orbitR_, center_);
    });
    info_ = new QLabel(right);
    info_->setObjectName("info");
    info_->setWordWrap(true);
    info_->setTextFormat(Qt::RichText);
    info_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    info_->setMinimumHeight(150);
    rv->addWidget(info_, 2);
    auto* btns = new QHBoxLayout;
    btns->addStretch();
    auto* draw = new QPushButton(t("gui.tpl.draw"), right);
    draw->setObjectName("draw");
    draw->setDefault(true);
    auto* close = new QPushButton(t("gui.close"), right);
    btns->addWidget(draw);
    btns->addWidget(close);
    rv->addLayout(btns);
    connect(draw, &QPushButton::clicked, this, [this] {
        if (valid_) emit commandReady(command());
    });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({400, 640});
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { update_(); });
    update_();
}

void TemplatesDialog::setPage(int p) { tabs_->setCurrentIndex(std::clamp(p, 0, 3)); }
int TemplatesDialog::page() const { return tabs_->currentIndex(); }
std::size_t TemplatesDialog::previewCount() const { return plan_->count(); }

bim::Placement TemplatesDialog::placement() const { return {atX_->value(), atY_->value(), atRot_->value(), atZ_->value()}; }

void TemplatesDialog::setPlacement(double x, double y, double rotDeg, double z0) {
    const QSignalBlocker b1(atX_), b2(atY_), b3(atRot_), b4(atZ_);
    atX_->setValue(x);
    atY_->setValue(y);
    atRot_->setValue(rotDeg);
    atZ_->setValue(z0);
    update_();
}

void TemplatesDialog::setPicked(const std::vector<Vec3>& pts) {
    if (pts.empty()) return;
    double rot = atRot_->value();
    if (pts.size() >= 2 && (pts[1] - pts[0]).length2d() > 1e-9) rot = std::atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x) * 180 / M_PI;
    // nuqta 3D bo'lsa (masalan, belgisi bor obyektdan) — uning z si belgi sifatida
    setPlacement(pts[0].x, pts[0].y, rot, std::abs(pts[0].z) > 1e-9 ? pts[0].z : atZ_->value());
}

bim::SteelFrameSpec TemplatesDialog::steelSpec() const {
    bim::SteelFrameSpec s;
    s.type = sfType_->currentIndex();
    s.dx = civil::parseSpacing(sfDx_->text().toStdString());
    s.dy = civil::parseSpacing(sfDy_->text().toStdString());
    s.floors = sfFloors_->value();
    s.floorH = sfFloorH_->value();
    s.span = sfSpan_->value();
    s.bays = sfBays_->value();
    s.bay = sfBay_->value();
    s.eave = sfEave_->value();
    s.ridge = sfRidge_->value();
    s.purlinStep = sfPurlin_->value();
    s.column = pCol_->currentText().toStdString();
    s.beam = pBeam_->currentText().toStdString();
    s.beam2 = pBeam2_->currentText().toStdString();
    s.rafter = pRaf_->currentText().toStdString();
    s.purlin = pPur_->currentText().toStdString();
    s.brace = pBr_->currentText().toStdString();
    s.bracing = sfBracing_->isChecked();
    s.at = placement();
    return s;
}
bim::StairSpec TemplatesDialog::stairSpec() const {
    bim::StairSpec s;
    s.type = stType_->currentIndex();
    s.H = stH_->value();
    s.width = stW_->value();
    s.gap = stGap_->value();
    s.tread = stTread_->value();
    s.riserMax = stRiser_->value();
    s.landing = stLanding_->value();
    s.waist = stWaist_->value();
    s.steel = stSteel_->isChecked();
    s.stringer = stStringer_->currentText().toStdString();
    s.at = placement();
    return s;
}
bim::RampSpec TemplatesDialog::rampSpec() const {
    bim::RampSpec s;
    s.kind = rpKind_->currentIndex();
    s.H = rpH_->value();
    s.width = rpW_->value();
    s.slope = rpSlope_->value();
    s.trans = rpTrans_->value();
    s.landingEvery = rpEvery_->value();
    s.landingL = rpLand_->value();
    s.wallT = rpWall_->value();
    s.parapet = rpPar_->value();
    s.floorL = rpFloor_->value();
    s.at = placement();
    return s;
}
bim::RetainSpec TemplatesDialog::retainSpec() const {
    bim::RetainSpec s;
    s.type = rwType_->currentIndex();
    s.H = rwH_->value();
    s.tTop = rwTop_->value();
    s.tBot = rwBot_->value();
    s.B = rwB_->value();
    s.toe = rwToe_->value();
    s.hBase = rwBase_->value();
    s.length = rwLen_->value();
    s.phi = rwPhi_->value();
    s.gamma = rwGamma_->value();
    s.q = rwQ_->value();
    s.mu = rwMu_->value();
    s.Rgr = rwR_->value();
    s.at = placement();
    return s;
}

QString TemplatesDialog::command() const {
    switch (page()) {
    case SteelFrame: return QStringLiteral("STEELFRAME \"%1\"").arg(writeTemp(bim::steelFrameToJson(steelSpec()), "asiacad_steelframe.json"));
    case Stair: return QStringLiteral("STAIR \"%1\"").arg(writeTemp(bim::stairToJson(stairSpec()), "asiacad_stair.json"));
    case Ramp: return QStringLiteral("RAMP \"%1\"").arg(writeTemp(bim::rampToJson(rampSpec()), "asiacad_ramp.json"));
    default: return QStringLiteral("RWALL \"%1\"").arg(writeTemp(bim::retainToJson(retainSpec()), "asiacad_rwall.json"));
    }
}

void TemplatesDialog::update_() {
    if (!info_) return;  // qurilish davomida
    const int pg = page();
    if (sfStack_) sfStack_->setCurrentIndex(sfType_->currentIndex());
    if (stGap_) stGap_->setEnabled(stType_->currentIndex() == 0);
    if (stStringer_) stStringer_->setEnabled(stSteel_->isChecked());
    if (rpTrans_) {
        rpTrans_->setEnabled(rpKind_->currentIndex() == 0);
        rpEvery_->setEnabled(rpKind_->currentIndex() == 1);
        rpLand_->setEnabled(rpKind_->currentIndex() == 1);
    }
    if (rwToe_) {
        rwToe_->setEnabled(rwType_->currentIndex() == 1);
        rwBot_->setEnabled(rwType_->currentIndex() != 2);
    }
    bim::TplResult r;
    QString html;
    valid_ = false;
    try {
        // ko'rinish — joylashsiz (lokal), chizmaga — joylash bilan
        switch (pg) {
        case SteelFrame: { auto s = steelSpec(); s.at = {}; r = bim::makeSteelFrame(s); break; }
        case Stair: { auto s = stairSpec(); s.at = {0, 0, 0, atZ_->value()}; r = bim::makeStair(s); break; }
        case Ramp: { auto s = rampSpec(); s.at = {0, 0, 0, atZ_->value()}; r = bim::makeRamp(s); break; }
        default: { auto s = retainSpec(); s.at = {0, 0, 0, atZ_->value()}; r = bim::makeRetainingWall(s); break; }
        }
        valid_ = true;
    } catch (const Error& e) {
        html = "<span style='color:#e05a50'>" + QString::fromStdString(loc_.tr(e.key(), e.args())).toHtmlEscaped() + "</span>";
    }
    if (valid_) {
        html += "<table cellspacing='2'>";
        for (const auto& [k, v] : r.info)
            html += "<tr><td style='color:#8a93a3'>" + QString::fromStdString(loc_.tr(k)).toHtmlEscaped() + "</td><td><b>" +
                    QString::fromStdString(v).toHtmlEscaped() + "</b></td></tr>";
        html += "</table>";
        if (r.issues.empty()) html += "<div style='color:#3fae5a'>✔ " + t("gui.tpl.ok").toHtmlEscaped() + "</div>";
        for (const auto& is : r.issues)
            html += QString("<div style='color:%1'>%2 ").arg(is.error ? "#e05a50" : "#e0a526", is.error ? "✖" : "⚠") +
                    QString::fromStdString(loc_.tr(is.key, is.args)).toHtmlEscaped() + "</div>";
        if (pg == RetainingWall || pg == Ramp || pg == Stair) html += "<div style='color:#8a93a3;font-size:small'>" + t("gui.tpl.note").toHtmlEscaped() + "</div>";
        const Vec3 o{atX_->value(), atY_->value(), atZ_->value()};
        html += "<div style='color:#8a93a3'>" + t("gui.tpl.where", {num(o.x, 3), num(o.y, 3), QString::number(o.z, 'f', 3), num(atRot_->value(), 2)}).toHtmlEscaped() + "</div>";
        // 3D ko'rinish
        Document tmp;
        for (const auto& e : r.entities) tmp.add(e->clone());
        scene_ = civil::sceneFromDocument(tmp);
        const BBox b = scene_.bounds();
        if (b.valid()) {
            center_ = (b.min + b.max) * 0.5;
            orbitR_ = std::max(6.0, (b.max - b.min).length() * 1.15);
            const double y = orbitYaw_ * M_PI / 180, p = orbitPitch_ * M_PI / 180;
            view3d_->setDrawDistance(orbitR_ * 4);
            view3d_->setCamera(center_ + Vec3{-std::cos(p) * std::cos(y), -std::cos(p) * std::sin(y), std::sin(p)} * orbitR_, center_);
        }
        view3d_->setScene(&scene_);
        plan_->setEntities(std::move(r.entities));
    } else {
        scene_ = {};
        view3d_->setScene(&scene_);
        plan_->setEntities({});
    }
    info_->setText(html);
}

// ---------------------------------------------------------------- geo-joylash
GeoPlaceDialog::GeoPlaceDialog(const i18n::Localizer& loc, bool dark, const QString& selArg, int selCount, QWidget* parent)
    : GenBase(loc, dark, parent), sel_(selArg) {
    setWindowTitle(t("gui.geo.title"));
    resize(560, 520);
    auto* v = new QVBoxLayout(this);
    auto* hint = new QLabel(t("gui.geo.hint", {QString::number(selCount)}), this);
    hint->setWordWrap(true);
    v->addWidget(hint);
    auto spin = [&](QWidget* parent, double lo, double hi, int dec, const QString& name) {
        auto* s = new QDoubleSpinBox(parent);
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setObjectName(name);
        s->setMinimumWidth(130);
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { update_(); });
        return s;
    };
    geo_ = new QCheckBox(t("gui.geo.swap"), this);
    geo_->setChecked(true);
    v->addWidget(geo_);
    auto pointBox = [&](int which, QDoubleSpinBox*& lx, QDoubleSpinBox*& ly, QDoubleSpinBox** lz, QDoubleSpinBox*& wx, QDoubleSpinBox*& wy,
                        QDoubleSpinBox** wh) {
        auto* g = new QGroupBox(t(which == 1 ? "gui.geo.p1" : "gui.geo.p2"), this);
        auto* f = new QFormLayout(g);
        auto* row = new QHBoxLayout;
        lx = spin(g, -1e8, 1e8, 4, QString("lx%1").arg(which));
        ly = spin(g, -1e8, 1e8, 4, QString("ly%1").arg(which));
        row->addWidget(lx);
        row->addWidget(ly);
        if (lz) {
            *lz = spin(g, -1e5, 1e5, 3, "lz1");
            row->addWidget(*lz);
        }
        auto* pk = new QPushButton(t("gui.geo.pick"), g);
        pk->setObjectName(QString("pick%1").arg(which));
        row->addWidget(pk);
        connect(pk, &QPushButton::clicked, this, [this, which] { emit pickRequested(which); });
        f->addRow(t("gui.geo.local"), row);
        auto* row2 = new QHBoxLayout;
        wx = spin(g, -1e9, 1e9, 4, QString("wx%1").arg(which));
        wy = spin(g, -1e9, 1e9, 4, QString("wy%1").arg(which));
        row2->addWidget(wx);
        row2->addWidget(wy);
        if (wh) {
            *wh = spin(g, -500, 9000, 3, "wh1");
            row2->addWidget(*wh);
        }
        f->addRow(t(wh ? "gui.geo.world" : "gui.geo.world2"), row2);
        v->addWidget(g);
        return g;
    };
    pointBox(1, lx1_, ly1_, &lz1_, wx1_, wy1_, &wh1_);
    keepZ_ = new QCheckBox(t("gui.geo.keepz"), this);
    v->addWidget(keepZ_);
    connect(keepZ_, &QCheckBox::toggled, this, [this](bool on) { wh1_->setEnabled(!on); update_(); });
    second_ = new QCheckBox(t("gui.geo.second"), this);
    second_->setChecked(true);
    v->addWidget(second_);
    p2box_ = pointBox(2, lx2_, ly2_, nullptr, wx2_, wy2_, nullptr);
    scale_ = new QCheckBox(t("gui.geo.scale"), this);
    v->addWidget(scale_);
    connect(second_, &QCheckBox::toggled, this, [this](bool on) { p2box_->setEnabled(on); scale_->setEnabled(on); update_(); });
    connect(geo_, &QCheckBox::toggled, this, [this] { update_(); });
    connect(scale_, &QCheckBox::toggled, this, [this] { update_(); });
    res_ = new QLabel(this);
    res_->setObjectName("result");
    res_->setWordWrap(true);
    v->addWidget(res_);
    v->addStretch();
    auto* btns = new QHBoxLayout;
    btns->addStretch();
    auto* apply = new QPushButton(t("gui.geo.apply"), this);
    apply->setObjectName("apply");
    apply->setDefault(true);
    auto* close = new QPushButton(t("gui.close"), this);
    btns->addWidget(apply);
    btns->addWidget(close);
    v->addLayout(btns);
    connect(apply, &QPushButton::clicked, this, [this] {
        if (valid_) emit commandReady(command());
    });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    update_();
}

void GeoPlaceDialog::setPicked(int which, const Vec3& p) {
    if (which == 1) {
        const QSignalBlocker b1(lx1_), b2(ly1_), b3(lz1_);
        lx1_->setValue(p.x);
        ly1_->setValue(p.y);
        lz1_->setValue(p.z);
    } else {
        const QSignalBlocker b1(lx2_), b2(ly2_);
        lx2_->setValue(p.x);
        ly2_->setValue(p.y);
    }
    update_();
}
void GeoPlaceDialog::setWorld(int which, double X, double Y, double H) {
    if (which == 1) {
        const QSignalBlocker b1(wx1_), b2(wy1_), b3(wh1_);
        wx1_->setValue(X);
        wy1_->setValue(Y);
        wh1_->setValue(H);
    } else {
        const QSignalBlocker b1(wx2_), b2(wy2_);
        wx2_->setValue(X);
        wy2_->setValue(Y);
    }
    update_();
}
void GeoPlaceDialog::setSecond(bool on) { second_->setChecked(on); }
QString GeoPlaceDialog::resultText() const { return res_->text(); }

QString GeoPlaceDialog::command() const {
    QString c = QStringLiteral("GEOPLACE %1 %2,%3,%4 %5,%6").arg(sel_, num(lx1_->value()), num(ly1_->value()), num(lz1_->value()), num(wx1_->value()), num(wy1_->value()));
    if (!keepZ_->isChecked()) c += "," + num(wh1_->value(), 3);
    if (second_->isChecked()) c += QStringLiteral(" %1,%2 %3,%4").arg(num(lx2_->value()), num(ly2_->value()), num(wx2_->value()), num(wy2_->value()));
    if (geo_->isChecked()) c += " GEO";
    if (second_->isChecked() && scale_->isChecked()) c += " SCALE";
    return c;
}

void GeoPlaceDialog::update_() {
    if (!res_) return;
    const bool g = geo_->isChecked();
    auto W = [&](double a, double b, double h) { return g ? Vec3{b, a, h} : Vec3{a, b, h}; };
    const Vec3 p1{lx1_->value(), ly1_->value(), lz1_->value()};
    const Vec3 P1 = W(wx1_->value(), wy1_->value(), keepZ_->isChecked() ? p1.z : wh1_->value());
    const Vec3 p2{lx2_->value(), ly2_->value(), 0};
    const Vec3 P2 = W(wx2_->value(), wy2_->value(), 0);
    valid_ = !sel_.isEmpty();
    QString s;
    try {
        const bool two = second_->isChecked();
        const auto fit = bim::geoFit(p1, P1, two ? &p2 : nullptr, two ? &P2 : nullptr, scale_->isChecked());
        s = t("gui.geo.res", {num(fit.rotDeg, 4), QString::number(P1.z - p1.z, 'f', 3)});
        if (two) {
            s += "\n" + t("gui.geo.res2", {QString::number(fit.scale, 'f', 6), QString::number(fit.residual, 'f', 3)});
            if (!scale_->isChecked() && std::abs(fit.scale - 1) > 1e-3) s += "\n⚠ " + t("gui.geo.warnscale");
        }
    } catch (const Error& e) {
        valid_ = false;
        s = QString::fromStdString(loc_.tr(e.key(), e.args()));
    }
    if (sel_.isEmpty()) s = t("gui.geo.nosel");
    res_->setText(s);
}

} // namespace asiacad::gui
