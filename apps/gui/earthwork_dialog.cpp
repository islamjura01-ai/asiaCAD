#include "earthwork_dialog.h"

#include "icons.h"

#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>
#include <format>

namespace asiacad::gui {

namespace {
QString n(double v) { return QString::number(v, 'g', 12); }
}  // namespace

QString EarthworkDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

EarthworkDialog::EarthworkDialog(const i18n::Localizer& loc, const Document& doc, std::function<std::vector<EntityId>()> selection, QWidget* parent)
    : QDialog(parent), loc_(loc), doc_(doc), selection_(std::move(selection)) {
    setWindowTitle(t("gui.ew.title"));
    resize(1120, 620);
    auto* root = new QHBoxLayout(this);
    // ---- chap: ma'lumot
    auto* left = new QVBoxLayout;
    root->addLayout(left, 0);
    auto* data = new QGroupBox(t("gui.ew.data"), this);
    auto* df = new QFormLayout(data);
    eg_ = new QComboBox(data);
    eg_->setObjectName("eg");
    df->addRow(t("gui.ew.eg"), eg_);
    fgMode_ = new QComboBox(data);
    fgMode_->setObjectName("fgMode");
    fgMode_->addItems({t("gui.ew.fg.tin"), t("gui.ew.fg.flat"), t("gui.ew.fg.slope"), t("gui.ew.fg.balance")});
    df->addRow(t("gui.ew.fg"), fgMode_);
    fg_ = new QComboBox(data);
    fg_->setObjectName("fg");
    df->addRow(t("gui.ew.fglayer"), fg_);
    auto spin = [&](QFormLayout* f, const char* key, double v, double lo, double hi, int dec, double step, const QString& name) {
        auto* s = new QDoubleSpinBox(this);
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setSingleStep(step);
        s->setValue(v);
        s->setObjectName(name);
        f->addRow(t(key), s);
        return s;
    };
    z_ = spin(df, "gui.ew.z", 0, -1000, 9000, 3, 0.1, "z");
    ix_ = spin(df, "gui.ew.ix", 0, -500, 500, 2, 1, "ix");
    iy_ = spin(df, "gui.ew.iy", 0, -500, 500, 2, 1, "iy");
    maxEdge_ = spin(df, "gui.ew.maxedge", 0, 0, 10000, 1, 5, "maxedge");
    dyn_ = new QCheckBox(t("gui.ew.dynamic"), data);
    dyn_->setObjectName("dynamic");
    dyn_->setChecked(true);
    dyn_->setToolTip(t("gui.ew.dynamic.tip"));
    df->addRow(dyn_);
    auto pickRow = [&](const char* key, QLabel*& info, const QString& name, std::function<void()> f) {
        auto* h = new QHBoxLayout;
        info = new QLabel(data);
        info->setObjectName(name + "Info");
        h->addWidget(info, 1);
        auto* b = new QPushButton(t("gui.dev.takesel"), data);
        b->setObjectName(name);
        connect(b, &QPushButton::clicked, this, std::move(f));
        h->addWidget(b);
        df->addRow(t(key), h);
    };
    pickRow("gui.ew.path", pathInfo_, "takePath", [this] { takePath(); });
    pickRow("gui.ew.bound", boundInfo_, "takeBound", [this] { takeBoundary(); });
    {
        auto* h = new QHBoxLayout;
        atInfo_ = new QLabel(data);
        h->addWidget(atInfo_, 1);
        auto* b = new QPushButton(t("gui.ew.pick"), data);
        b->setObjectName("pickAt");
        connect(b, &QPushButton::clicked, this, [this] { emit pickRequested(); });
        h->addWidget(b);
        df->addRow(t("gui.ew.at"), h);
    }
    left->addWidget(data);
    auto* hint = new QLabel(t("gui.ew.hint"), this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#8a93a3;");
    left->addWidget(hint);
    left->addStretch();
    connect(fgMode_, &QComboBox::currentIndexChanged, this, [this] { updateInfo(); });
    // ---- o'ng: sahifalar
    auto* right = new QVBoxLayout;
    root->addLayout(right, 1);
    tabs_ = new QTabWidget(this);
    right->addWidget(tabs_, 1);
    auto page = [&](const char* title, const char* icon) {
        auto* w = new QWidget(tabs_);
        auto* f = new QFormLayout(w);
        tabs_->addTab(w, makeIcon(icon, QColor("#0E9F8E")), t(title));
        return f;
    };
    auto buttons = [&](QFormLayout* f, bool excel, bool calc) {
        auto* h = new QHBoxLayout;
        auto* d = new QPushButton(t("gui.ew.draw"), this);
        d->setObjectName(QString("draw%1").arg(tabs_->count() - 1));
        h->addWidget(d);
        connect(d, &QPushButton::clicked, this, [this] { runTab(false); });
        if (excel) {
            auto* x = new QPushButton(makeIcon("excel", QColor("#1D6F42")), t("gui.dev.excel"), this);
            h->addWidget(x);
            connect(x, &QPushButton::clicked, this, [this] { runTab(true); });
        }
        if (calc) {
            auto* c = new QPushButton(t("gui.ew.calc"), this);
            c->setObjectName(QString("calc%1").arg(tabs_->count() - 1));
            h->addWidget(c);
            connect(c, &QPushButton::clicked, this, [this] { compute(); });
        }
        h->addStretch();
        f->addRow(h);
    };
    {   // yuza va gorizontallar
        auto* f = page("gui.ew.tab.surface", "terrain");
        cStep_ = spin(f, "gui.ew.cstep", 0.5, 0.01, 100, 2, 0.1, "cstep");
        cMajor_ = spin(f, "gui.ew.cmajor", 2.5, 0, 500, 2, 0.5, "cmajor");
        auto* h = new QHBoxLayout;
        auto* tin = new QPushButton(t("gui.ew.drawtin"), this);
        tin->setObjectName("drawTin");
        auto* con = new QPushButton(t("gui.ew.drawcontour"), this);
        con->setObjectName("drawContour");
        h->addWidget(tin);
        h->addWidget(con);
        h->addStretch();
        f->addRow(h);
        connect(tin, &QPushButton::clicked, this, [this] { emit commandReady(command(Surface)); });
        connect(con, &QPushButton::clicked, this, [this] {
            emit commandReady(QStringLiteral("CONTOUR %1 STEP=%2 MAJOR=%3%4").arg(layerToken(eg_), n(cStep_->value()), n(cMajor_->value()),
                                                                                  maxEdge_->value() > 0 ? " MAXEDGE=" + n(maxEdge_->value()) : QString()) +
                               (dyn_->isChecked() ? QString() : QStringLiteral(" STATIC")));
        });
    }
    {
        auto* f = page("gui.ew.tab.profile", "profile");
        pStep_ = spin(f, "gui.ew.step", 20, 1, 1000, 1, 5, "pstep");
        pH_ = spin(f, "gui.ew.hscale", 1000, 10, 100000, 0, 100, "phs");
        pV_ = spin(f, "gui.ew.vscale", 100, 1, 100000, 0, 10, "pvs");
        pPk_ = spin(f, "gui.ew.pk0", 0, -1e6, 1e7, 2, 100, "ppk");
        buttons(f, true, false);
    }
    {
        auto* f = page("gui.ew.tab.sections", "sections");
        sStep_ = spin(f, "gui.ew.step", 20, 1, 1000, 1, 5, "sstep");
        sL_ = spin(f, "gui.ew.left", 20, 0, 1000, 1, 1, "sl");
        sR_ = spin(f, "gui.ew.right", 20, 0, 1000, 1, 1, "sr");
        sScale_ = spin(f, "gui.ew.scale", 200, 10, 10000, 0, 50, "sscale");
        sCols_ = new QSpinBox(this);
        sCols_->setRange(1, 20);
        sCols_->setValue(4);
        f->addRow(t("gui.ew.cols"), sCols_);
        buttons(f, true, true);
    }
    {
        auto* f = page("gui.ew.tab.carto", "cartogram");
        kCell_ = spin(f, "gui.ew.cell", 20, 0.5, 1000, 2, 5, "kcell");
        buttons(f, true, true);
    }
    {
        auto* f = page("gui.ew.tab.volume", "volume");
        vCell_ = spin(f, "gui.ew.vcell", 0, 0, 100, 2, 0.5, "vcell");
        buttons(f, false, true);
    }
    res_ = new QLabel(this);
    res_->setObjectName("result");
    res_->setTextFormat(Qt::RichText);
    res_->setWordWrap(true);
    res_->setMinimumHeight(110);
    res_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(res_);
    refreshLayers();
}

void EarthworkDialog::setTab(int i) { tabs_->setCurrentIndex(std::clamp(i, 0, 4)); }
int EarthworkDialog::tab() const { return tabs_->currentIndex(); }

void EarthworkDialog::refreshLayers() {
    for (auto* c : {eg_, fg_}) {
        const QString cur = c->currentData().toString();
        const QSignalBlocker b(c);
        c->clear();
        for (const auto& [name, L] : doc_.layers()) {
            const auto k = doc_.idsOnLayer(name).size();
            if (k) c->addItem(QStringLiteral("%1 (%2)").arg(QString::fromStdString(name)).arg(k), QString::fromStdString(name));
        }
        int i = c->findData(cur);
        if (i < 0 && c == eg_) i = c->findData("SYOMKA");
        c->setCurrentIndex(std::max(0, i));
    }
    updateInfo();
}
void EarthworkDialog::setGroundLayer(const QString& l) {
    refreshLayers();
    const int i = eg_->findData(l);
    if (i >= 0) eg_->setCurrentIndex(i);
}
void EarthworkDialog::setDesignLayer(const QString& l) {
    refreshLayers();
    const int i = fg_->findData(l);
    if (i >= 0) fg_->setCurrentIndex(i);
}
void EarthworkDialog::setDesignMode(int m) { fgMode_->setCurrentIndex(std::clamp(m, 0, 3)); }
void EarthworkDialog::setDesignZ(double z) { z_->setValue(z); }
void EarthworkDialog::setAt(const Vec3& p) {
    at_ = p;
    updateInfo();
}

void EarthworkDialog::takePath() {
    path_ = kInvalidId;
    for (EntityId id : selection_ ? selection_() : std::vector<EntityId>{})
        if (const IEntity* e = doc_.find(id); e && civil::pathOf(*e).size() >= 2) { path_ = id; break; }
    updateInfo();
}
void EarthworkDialog::takeBoundary() {
    bound_ = kInvalidId;
    for (EntityId id : selection_ ? selection_() : std::vector<EntityId>{})
        if (const auto* pl = dynamic_cast<const PolylineEntity*>(doc_.find(id)); pl && pl->isClosed()) { bound_ = id; break; }
    updateInfo();
}

void EarthworkDialog::updateInfo() {
    const int m = fgMode_->currentIndex();
    fg_->setEnabled(m == 0);
    z_->setEnabled(m == 1 || m == 2);
    ix_->setEnabled(m == 2 || m == 3);
    iy_->setEnabled(m == 2 || m == 3);
    auto desc = [&](EntityId id) {
        const IEntity* e = doc_.find(id);
        return e ? QStringLiteral("#%1 %2").arg(id).arg(QString::fromStdString(e->layer())) : t("gui.dev.none");
    };
    pathInfo_->setText(desc(path_));
    boundInfo_->setText(desc(bound_));
    atInfo_->setText(at_ ? QStringLiteral("%1, %2").arg(at_->x, 0, 'f', 2).arg(at_->y, 0, 'f', 2) : t("gui.ew.auto"));
}

QString EarthworkDialog::layerToken(const QComboBox* c) const {
    return "\"" + QString::fromStdString(loc_.keywordName("LAYER")) + ":" + c->currentData().toString() + "\"";
}

QString EarthworkDialog::command(int tb, const QString& xlsx) const {
    QString c = commandBody(tb, xlsx);
    if (tb != Volume && dyn_ && !dyn_->isChecked()) c += QStringLiteral(" STATIC");
    return c;
}

QString EarthworkDialog::commandBody(int tb, const QString& xlsx) const {
    const QString eg = "\"EG=" + QString::fromStdString(loc_.keywordName("LAYER")) + ":" + eg_->currentData().toString() + "\"";
    QString design;
    const int m = fgMode_->currentIndex();
    // qiya tekislik tayanchi: chegara markazi, bo'lmasa o'q boshi
    Vec3 base;
    if (const IEntity* e = doc_.find(bound_)) {
        const BBox b = e->bbox();
        base = (b.min + b.max) * 0.5;
    } else if (const IEntity* e2 = doc_.find(path_)) {
        const auto v = civil::pathOf(*e2);
        if (!v.empty()) base = v.front();
    }
    if (m == 0) design = " \"FG=" + QString::fromStdString(loc_.keywordName("LAYER")) + ":" + fg_->currentData().toString() + "\"";
    else if (m == 1) design = " Z=" + n(z_->value());
    else if (m == 2) design = QStringLiteral(" Z=%1 IX=%2 IY=%3 BASE=%4,%5").arg(n(z_->value()), n(ix_->value()), n(iy_->value()), n(base.x), n(base.y));
    else design = QStringLiteral(" BALANCE IX=%1 IY=%2 BASE=%3,%4").arg(n(ix_->value()), n(iy_->value()), n(base.x), n(base.y));
    const QString me = maxEdge_->value() > 0 ? " MAXEDGE=" + n(maxEdge_->value()) : QString();
    const QString bd = doc_.find(bound_) ? QStringLiteral(" BOUND=%1").arg(bound_) : QString();
    const QString at = at_ ? QStringLiteral(" AT=%1,%2").arg(n(at_->x), n(at_->y)) : QString();
    const QString xl = xlsx.isEmpty() ? QString() : " \"XLSX=" + xlsx + "\"";
    switch (tb) {
    case Surface: return QStringLiteral("TIN %1%2%3").arg(layerToken(eg_), me, bd);
    case Profile:
        return QStringLiteral("PROFILE %1 %2%3 STEP=%4 H=%5 V=%6 PK0=%7%8%9").arg(QString::number(path_), eg, m == 3 ? QString() : design, n(pStep_->value()),
                                                                                n(pH_->value()), n(pV_->value()), n(pPk_->value()), at, me) + xl;
    case Sections:
        return QStringLiteral("XSECTIONS %1 %2%3 STEP=%4 L=%5 R=%6 SCALE=%7 COLS=%8%9").arg(QString::number(path_), eg, design, n(sStep_->value()), n(sL_->value()),
                                                                                         n(sR_->value()), n(sScale_->value()), QString::number(sCols_->value()), at) + me + xl;
    case Cartogram: return QStringLiteral("CARTOGRAM %1%2 CELL=%3%4%5").arg(eg, design, n(kCell_->value()), bd, me) + xl;
    default: return QStringLiteral("VOLUME %1%2%3%4%5").arg(eg, design, bd, me, vCell_->value() > 0 ? " CELL=" + n(vCell_->value()) : QString());
    }
}

void EarthworkDialog::runTab(bool excel) {
    const int tb = tab();
    if ((tb == Profile || tb == Sections) && !doc_.find(path_)) {
        res_->setText("<span style='color:#e05a50'>" + t("terr.err.path").toHtmlEscaped() + "</span>");
        return;
    }
    QString x;
    if (excel) {
        x = QFileDialog::getSaveFileName(this, t("gui.dev.excel"), tb == Profile ? "profil.xlsx" : tb == Sections ? "razrez.xlsx" : "kartogramma.xlsx", "Excel (*.xlsx)");
        if (x.isEmpty()) return;
        if (!x.endsWith(".xlsx", Qt::CaseInsensitive)) x += ".xlsx";
    }
    emit commandReady(command(tb, x));
}

bool EarthworkDialog::compute() {
    const int tb = tab();
    QString html;
    try {
        const auto pts = civil::terrainPoints(doc_, doc_.idsOnLayer(eg_->currentData().toString().toStdString()));
        if (pts.size() < 3) throw Error("terr.err.points", {eg_->currentData().toString().toStdString(), std::to_string(pts.size())});
        const auto eg = civil::Tin::build(pts, nullptr, maxEdge_->value());
        civil::Tin fgTin;
        civil::Design d;
        const int m = fgMode_->currentIndex();
        if (m == 0) {
            fgTin = civil::Tin::build(civil::terrainPoints(doc_, doc_.idsOnLayer(fg_->currentData().toString().toStdString())), nullptr, maxEdge_->value());
            d.tin = &fgTin;
        } else if (m == 1 || m == 2) {
            d.plane = true;
            d.z0 = z_->value();
        }
        if (m >= 2) {
            d.plane = m == 2;
            d.ix = ix_->value() / 1000;
            d.iy = iy_->value() / 1000;
            if (const IEntity* e = doc_.find(bound_)) { const BBox b = e->bbox(); d.base = (b.min + b.max) * 0.5; }
            else if (const IEntity* e2 = doc_.find(path_)) { const auto v = civil::pathOf(*e2); if (!v.empty()) d.base = v.front(); }
            if (m == 3) { d.plane = true; d.z0 = 0; }   // balansda nishabliklar saqlanadi
        }
        std::vector<Vec3> bound;
        if (const IEntity* e = doc_.find(bound_)) bound = civil::pathOf(*e);
        auto f1 = [](double v) { return QString::number(v, 'f', 1); };
        if (tb == Sections) {
            const IEntity* e = doc_.find(path_);
            if (!e) throw Error("terr.err.path", {});
            civil::SectionSpec s;
            s.step = sStep_->value();
            s.left = sL_->value();
            s.right = sR_->value();
            if (m == 3) throw Error("terr.err.nofg", {});
            const auto xs = civil::sampleSections(civil::pathOf(*e), eg, d, s);
            const auto v = civil::sectionVolumes(xs);
            html = t("gui.ew.res.sections", {QString::number(xs.size()), f1(v.fill), f1(v.cut), f1(v.fill - v.cut)}).toHtmlEscaped();
        } else if (tb == Cartogram || tb == Volume) {
            civil::VolumeResult v;
            if (tb == Cartogram) {
                civil::CartoSpec s;
                s.cell = kCell_->value();
                s.boundary = bound;
                s.balance = m == 3;
                const auto r = civil::cartogram(eg, d, s);
                v.fill = r.fill;
                v.cut = r.cut;
                v.area = r.area;
                v.cell = r.cell;
                v.balanceZ = r.balanceZ;
            } else v = civil::volumeBetween(eg, d, bound, vCell_->value(), m == 3);
            html = t("gui.ew.res.volume", {f1(v.area), f1(v.fill), f1(-v.cut), f1(std::abs(v.fill + v.cut) < 0.05 ? 0.0 : v.fill + v.cut), QString::number(v.cell, 'f', 2)}).toHtmlEscaped();
            if (!std::isnan(v.balanceZ)) html += "<br><b>" + t("terr.msg.h0", {QString::number(v.balanceZ, 'f', 3)}).toHtmlEscaped() + "</b>";
        } else {
            html = t("gui.ew.res.surface", {QString::number(eg.points().size()), QString::number(eg.triangles().size()), f1(eg.area())}).toHtmlEscaped();
        }
    } catch (const Error& e) {
        res_->setText("<span style='color:#e05a50'>" + QString::fromStdString(loc_.tr(e.key(), e.args())).toHtmlEscaped() + "</span>");
        return false;
    }
    res_->setText(html);
    return true;
}

} // namespace asiacad::gui
