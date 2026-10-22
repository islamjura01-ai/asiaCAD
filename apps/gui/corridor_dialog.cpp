#include "corridor_dialog.h"
#include "table_bulk.h"

#include <asiacad/civil/excel.h>
#include <asiacad/civil/road.h>

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
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>

namespace asiacad::gui {

using civil::SubKind;

namespace {
QString f2(double v) { return QString::number(v, 'f', 2); }
QString f1(double v) { return QString::number(v, 'f', 1); }
const QColor kGround(160, 115, 70), kCut(200, 120, 70), kFill(120, 170, 90);
const char* kCats[] = {"BASIC", "LANES", "SHOULDERS", "MEDIANS", "CURBS", "DAYLIGHT", "CHANNELS", "TRENCH", "WALLS", "TUNNEL"};

// profil: yer va loyiha
class CorProfileView : public QWidget {
public:
    explicit CorProfileView(QWidget* parent) : QWidget(parent) { setMinimumHeight(200); }
    const civil::Corridor* c = nullptr;
    QString g, d;
    bool dark = true;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor bg = dark ? QColor(18, 22, 34) : QColor(250, 250, 252), fg = dark ? QColor(200, 205, 220) : QColor(40, 40, 50);
        p.fillRect(rect(), bg);
        if (!c || c->sec.size() < 2) return;
        double zmin = 1e18, zmax = -1e18;
        for (const auto& s : c->sec) {
            zmin = std::min({zmin, s.ground, s.design});
            zmax = std::max({zmax, s.ground, s.design});
        }
        zmin = std::floor(zmin - 1);
        zmax = std::ceil(zmax + 1);
        const double L = std::max(1e-6, c->sec.back().s);
        const QRectF r = QRectF(rect()).adjusted(60, 30, -16, -30);
        auto P = [&](double s, double z) { return QPointF(r.left() + s / L * r.width(), r.bottom() - (z - zmin) / (zmax - zmin) * r.height()); };
        p.setPen(QPen(fg.darker(180), 1));
        const double dz = std::max(0.5, std::round((zmax - zmin) / 6));
        for (double z = zmin; z <= zmax + 1e-9; z += dz) {
            p.drawLine(P(0, z), P(L, z));
            p.drawText(QRectF(0, P(0, z).y() - 8, 56, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(z, 'f', 1));
        }
        QPainterPath pg, pd;
        for (std::size_t i = 0; i < c->sec.size(); ++i) {
            const auto& s = c->sec[i];
            if (i == 0) { pg.moveTo(P(s.s, s.ground)); pd.moveTo(P(s.s, s.design)); }
            else { pg.lineTo(P(s.s, s.ground)); pd.lineTo(P(s.s, s.design)); }
        }
        p.setPen(QPen(kGround, 2));
        p.drawPath(pg);
        p.setPen(QPen(QColor(235, 70, 70), 2.4));
        p.drawPath(pd);
        p.setPen(fg);
        for (const auto& s : c->sec)
            if (std::abs(std::fmod(s.s, 100.0)) < 0.01)
                p.drawText(QRectF(P(s.s, zmin).x() - 30, r.bottom() + 4, 60, 14), Qt::AlignCenter, QString::number(s.s / 100, 'f', 0));
        QFont f = p.font();
        f.setBold(true);
        p.setFont(f);
        p.setPen(kGround);
        p.drawText(QPointF(r.left(), 18), "— " + g);
        p.setPen(QColor(235, 70, 70));
        p.drawText(QPointF(r.left() + 170, 18), "— " + d);
    }
};

// palitra belgisi: qism kesimining kichik rasmi
QIcon subIcon(SubKind k) {
    QPixmap pm(44, 22);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    civil::Assembly a;
    a.symmetric = false;
    a.right = {civil::defaultSub(k)};
    const bool day = civil::subIsDaylight(k) && k != SubKind::PipeTrenchHalf && k != SubKind::RetainingWall;
    const civil::Section s = civil::buildSection(a, 0, [&](double o) { return day ? -1.0 + 0.0 * o : (k == SubKind::PipeTrenchHalf ? 1.2 : -0.6); }, 20);
    double o0 = 1e9, o1 = -1e9, z0 = 1e9, z1 = -1e9;
    for (const auto& q : s.pts) { o0 = std::min(o0, q.o); o1 = std::max(o1, q.o); z0 = std::min(z0, q.z); z1 = std::max(z1, q.z); }
    for (const auto& sh : s.shapes)
        for (const auto& q : sh.poly) { o0 = std::min(o0, q.first); o1 = std::max(o1, q.first); z0 = std::min(z0, q.second); z1 = std::max(z1, q.second); }
    if (o1 <= o0) return QIcon(pm);
    const double k2 = std::min(40 / (o1 - o0), 18 / std::max(0.3, z1 - z0));
    auto P = [&](double o, double z) { return QPointF(2 + (o - o0) * k2, 20 - (z - z0) * k2); };
    for (const auto& sh : s.shapes) {
        if (sh.material.rfind("_", 0) == 0 || sh.material == "BACKFILL") continue;
        QPolygonF poly;
        for (const auto& q : sh.poly) poly << P(q.first, q.second);
        p.setPen(Qt::NoPen);
        p.setBrush(materialColor(sh.material));
        p.drawPolygon(poly);
    }
    QPainterPath path;
    for (std::size_t i = 0; i < s.pts.size(); ++i) {
        if (i == 0) path.moveTo(P(s.pts[i].o, s.pts[i].z));
        else path.lineTo(P(s.pts[i].o, s.pts[i].z));
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(235, 235, 240), 1.4));
    p.drawPath(path);
    return QIcon(pm);
}
}  // namespace

QColor materialColor(const std::string& m) {
    if (m == "ASPHALT") return QColor(60, 62, 70);
    if (m == "BASE") return QColor(190, 160, 110);
    if (m == "SUBBASE") return QColor(215, 195, 150);
    if (m == "SHOULDER") return QColor(150, 140, 120);
    if (m == "CONCRETE") return QColor(175, 178, 182);
    if (m == "SIDEWALK") return QColor(200, 120, 110);
    if (m == "BEDDING") return QColor(230, 210, 140);
    if (m == "PIPE") return QColor(70, 130, 210);
    if (m == "BACKFILL") return QColor(150, 110, 70, 110);
    if (m == "WATER") return QColor(60, 150, 230, 170);
    if (m == "LINING") return QColor(160, 165, 172);
    if (m == "EXCAVATION") return QColor(150, 110, 70, 90);
    if (m == "INVERT") return QColor(110, 105, 96);
    return QColor(140, 140, 160);
}

// ---------------------------------------------------------------- kesim ko'rinishi
void SectionView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor bg = dark_ ? QColor(18, 22, 34) : QColor(250, 250, 252), fg = dark_ ? QColor(200, 205, 220) : QColor(40, 40, 50);
    p.fillRect(rect(), bg);
    if (!s_ || s_->pts.empty()) return;
    double o0 = 1e18, o1 = -1e18, z0 = 1e18, z1 = -1e18;
    auto ext = [&](double o, double z) { o0 = std::min(o0, o); o1 = std::max(o1, o); z0 = std::min(z0, z); z1 = std::max(z1, z); };
    for (const auto& q : s_->pts) ext(q.o, q.z);
    for (const auto& sh : s_->shapes)
        for (const auto& q : sh.poly) ext(q.first, q.second);
    for (const auto& q : s_->groundLine)
        if (q.first >= o0 - 3 && q.first <= o1 + 3) ext(q.first, q.second);
    o0 -= 1; o1 += 1; z0 -= 0.5; z1 += 0.8;
    const QRectF r = QRectF(rect()).adjusted(52, 26, -12, -22);
    // gorizontal — kenglikka; vertikal — ko'pi bilan 5 baravar bo'rttirilgan (yupqa qatlamlar ko'rinsin)
    const double kx = r.width() / (o1 - o0);
    const double kz = std::min(r.height() / (z1 - z0), kx * 5);
    const double cx = r.center().x() - (o0 + o1) / 2 * kx, cy = r.center().y() + (z0 + z1) / 2 * kz;
    auto P = [&](double o, double z) { return QPointF(cx + o * kx, cy - z * kz); };
    // to'r
    p.setPen(QPen(fg.darker(220), 1));
    const double dz = std::max(0.5, std::round((z1 - z0) / 5 * 2) / 2);
    for (double z = std::ceil(z0 / dz) * dz; z <= z1; z += dz) {
        p.drawLine(QPointF(r.left(), P(0, z).y()), QPointF(r.right(), P(0, z).y()));
        p.drawText(QRectF(0, P(0, z).y() - 8, 48, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(z, 'f', 1));
    }
    // kesma / to'kma (yer va asos chizig'i orasi)
    if (s_->datum.size() >= 2 && !s_->groundLine.empty()) {
        auto gAt = [&](double o) {
            const auto& g = s_->groundLine;
            for (std::size_t i = 0; i + 1 < g.size(); ++i)
                if (o >= g[i].first && o <= g[i + 1].first)
                    return g[i].second + (g[i + 1].second - g[i].second) * (o - g[i].first) / (g[i + 1].first - g[i].first);
            return g.back().second;
        };
        for (std::size_t i = 0; i + 1 < s_->datum.size(); ++i) {
            const auto& a = s_->datum[i];
            const auto& b = s_->datum[i + 1];
            if (std::abs(b.first - a.first) < 1e-9) continue;
            const int n = std::max(1, static_cast<int>(std::abs(b.first - a.first) / 0.25));
            for (int j = 0; j < n; ++j) {
                const double oa = a.first + (b.first - a.first) * j / n, ob = a.first + (b.first - a.first) * (j + 1) / n;
                const double za = a.second + (b.second - a.second) * j / n, zb = a.second + (b.second - a.second) * (j + 1) / n;
                const double ga = gAt(oa), gb = gAt(ob);
                QColor c = (ga + gb) / 2 > (za + zb) / 2 ? kCut : kFill;
                c.setAlpha(80);
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawPolygon(QPolygonF{P(oa, za), P(ob, zb), P(ob, gb), P(oa, ga)});
            }
        }
    }
    // materiallar
    for (const auto& sh : s_->shapes) {
        if (sh.material.rfind("_", 0) == 0) continue;
        QPolygonF poly;
        for (const auto& q : sh.poly) poly << P(q.first, q.second);
        p.setPen(QPen(materialColor(sh.material).darker(140), 0.8));
        p.setBrush(materialColor(sh.material));
        p.drawPolygon(poly);
    }
    // yer, asos, ustki chiziq
    QPainterPath g, d, t;
    for (std::size_t i = 0; i < s_->groundLine.size(); ++i)
        (i == 0 ? g.moveTo(P(s_->groundLine[i].first, s_->groundLine[i].second)) : g.lineTo(P(s_->groundLine[i].first, s_->groundLine[i].second)));
    for (std::size_t i = 0; i < s_->datum.size(); ++i)
        (i == 0 ? d.moveTo(P(s_->datum[i].first, s_->datum[i].second)) : d.lineTo(P(s_->datum[i].first, s_->datum[i].second)));
    for (std::size_t i = 0; i < s_->pts.size(); ++i) (i == 0 ? t.moveTo(P(s_->pts[i].o, s_->pts[i].z)) : t.lineTo(P(s_->pts[i].o, s_->pts[i].z)));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kGround, 2));
    p.drawPath(g);
    p.setPen(QPen(QColor(235, 200, 80), 1.2, Qt::DashLine));
    p.drawPath(d);
    p.setPen(QPen(dark_ ? QColor(245, 245, 250) : QColor(30, 30, 40), 1.8));
    p.drawPath(t);
    for (const auto& q : s_->pts) {
        p.setBrush(q.code == "DAYLIGHT" ? QColor(120, 200, 90) : QColor(235, 70, 70));
        p.setPen(Qt::NoPen);
        p.drawEllipse(P(q.o, q.z), 2.6, 2.6);
    }
    p.setPen(fg);
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    p.drawText(QPointF(r.left(), 16), title_);
    f.setBold(false);
    p.setFont(f);
    p.drawText(QRectF(r.left(), r.bottom() + 2, r.width(), 18), Qt::AlignLeft,
               QStringLiteral("▽ %1   ⛰ %2   ▲ %3 m²   ▼ %4 m²").arg(f2(s_->design), f2(s_->ground), f2(s_->cut), f2(s_->fill)));
}

// ---------------------------------------------------------------- dialog
CorridorDialog::CorridorDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent)
    : QDialog(parent), loc_(loc), doc_(doc) {
    setWindowTitle(t("gui.cor.title"));
    resize(1720, 960);
    asms_ = civil::builtinAssemblies();
    p_ = civil::defaultCorridorParams();
    buildUi();
    view_->setDark(dark);
    secView_->setDark(dark);
    preview_view_->setDark(dark);
    static_cast<CorProfileView*>(profile_)->dark = dark;
    std::map<std::string, QString> mats;
    for (const char* m : {"ASPHALT", "BASE", "SUBBASE", "SHOULDER", "CONCRETE", "SIDEWALK", "BEDDING", "PIPE", "BACKFILL", "WATER", "GENERIC"})
        mats[m] = t((std::string("cor.mat.") + m).c_str());
    secView_->setMaterialNames(mats);
    fillPalette();
    fillLayers();
    selIndex_ = p_.assemblies.front().right.empty() ? -1 : 0;
    fillAssembly();
    reread();
}

QString CorridorDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}
QString CorridorDialog::subName(SubKind k) const { return t((std::string("cor.sub.") + civil::subKey(k)).c_str()); }

void CorridorDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    // ---- 1: palitra (vertikal yorliqlar, Civil 3D tool palette kabi)
    palette_ = new QTabWidget(split);
    palette_->setTabPosition(QTabWidget::East);
    palette_->setMinimumWidth(280);

    // ---- 2: yig'ma muharriri
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
    auto title = [&](const char* k) {
        auto* l = new QLabel(t(k), left);
        l->setObjectName("title");
        lv->addWidget(l);
        return l;
    };
    title("gui.cor.assembly");
    asmName_ = new QLabel(left);
    asmName_->setObjectName("muted");
    asmName_->setWordWrap(true);
    lv->addWidget(asmName_);
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(t("gui.cor.addto"), left));
        addSide_ = new QComboBox(left);
        addSide_->addItem(t("gui.cor.side.both"), 0);
        addSide_->addItem(t("gui.cor.side.right"), 1);
        addSide_->addItem(t("gui.cor.side.left"), -1);
        row->addWidget(addSide_, 1);
        lv->addLayout(row);
    }
    symmetric_ = new QCheckBox(t("gui.cor.symmetric"), left);
    lv->addWidget(symmetric_);
    auto* lists = new QHBoxLayout;
    auto mkList = [&](const char* k, QListWidget*& w) {
        auto* box = new QVBoxLayout;
        auto* l = new QLabel(t(k), left);
        l->setObjectName("muted");
        box->addWidget(l);
        w = new QListWidget(left);
        w->setMinimumHeight(150);
        box->addWidget(w);
        lists->addLayout(box);
    };
    mkList("gui.cor.side.left", leftList_);
    mkList("gui.cor.side.right", rightList_);
    lv->addLayout(lists);
    auto* btns = new QHBoxLayout;
    auto* up = new QPushButton(QString::fromUtf8("↑"), left);
    auto* down = new QPushButton(QString::fromUtf8("↓"), left);
    auto* del = new QPushButton(QString::fromUtf8("✕ ") + t("gui.cor.remove"), left);
    btns->addWidget(up);
    btns->addWidget(down);
    btns->addWidget(del, 1);
    lv->addLayout(btns);
    title("gui.cor.params");
    params_ = new QTableWidget(0, 2, left);
    params_->setHorizontalHeaderLabels({t("xl.c.key"), t("xl.c.value")});
    params_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    params_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    params_->verticalHeader()->setVisible(false);
    params_->setMinimumHeight(160);
    lv->addWidget(params_);
    {
        auto* row = new QHBoxLayout;
        auto* l = new QLabel(t("gui.cor.preview"), left);
        l->setObjectName("title");
        row->addWidget(l, 1);
        previewCase_ = new QComboBox(left);
        previewCase_->addItem(t("gui.cor.case.fill"));
        previewCase_->addItem(t("gui.cor.case.cut"));
        row->addWidget(previewCase_);
        lv->addLayout(row);
    }
    preview_view_ = new SectionView(left);
    preview_view_->setMinimumHeight(190);
    lv->addWidget(preview_view_);
    auto dspin = [&](double lo, double hi, double step, int dec) {
        auto* s = new QDoubleSpinBox(left);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        return s;
    };
    title("gui.cor.corridor");
    auto* form = new QFormLayout;
    q_ = dspin(0, 1000, 0.05, 3);
    n_ = dspin(0.008, 0.06, 0.001, 3);
    step_ = dspin(1, 200, 5, 1);
    maxDl_ = dspin(5, 1000, 10, 0);
    ground_ = dspin(-500, 9000, 0.5, 2);
    form->addRow(t("gui.ww.q"), q_);
    form->addRow(t("gui.ww.n"), n_);
    form->addRow(t("gui.cor.step"), step_);
    form->addRow(t("gui.cor.maxdl"), maxDl_);
    form->addRow(t("gui.ut.ground"), ground_);
    lv->addLayout(form);
    auto* note = new QLabel(t("gui.cor.note"), left);
    note->setWordWrap(true);
    note->setObjectName("muted");
    lv->addWidget(note);
    lv->addStretch(1);

    // ---- 3: 3D / kesimlar / profil
    auto* mid = new QWidget(split);
    auto* mv = new QVBoxLayout(mid);
    mv->setContentsMargins(6, 0, 6, 0);
    midTabs_ = new QTabWidget(mid);
    auto* v3 = new QWidget(midTabs_);
    auto* v3l = new QVBoxLayout(v3);
    v3l->setContentsMargins(0, 0, 0, 0);
    view_ = new Model3DView(v3);
    v3l->addWidget(view_, 1);
    auto* vx = new QHBoxLayout;
    vx->addWidget(new QLabel(t("gui.ut.vexag"), v3));
    vexag_ = new QDoubleSpinBox(v3);
    vexag_->setRange(1, 20);
    vexag_->setValue(2);
    vexag_->setDecimals(0);
    vx->addWidget(vexag_);
    vx->addStretch(1);
    v3l->addLayout(vx);
    auto* sv = new QWidget(midTabs_);
    auto* svl = new QVBoxLayout(sv);
    svl->setContentsMargins(0, 0, 0, 0);
    secView_ = new SectionView(sv);
    svl->addWidget(secView_, 1);
    auto* sr = new QHBoxLayout;
    auto* prev = new QPushButton(QString::fromUtf8("◀"), sv);
    auto* next = new QPushButton(QString::fromUtf8("▶"), sv);
    secSlider_ = new QSlider(Qt::Horizontal, sv);
    secLbl_ = new QLabel(sv);
    secLbl_->setMinimumWidth(120);
    sr->addWidget(prev);
    sr->addWidget(secSlider_, 1);
    sr->addWidget(next);
    sr->addWidget(secLbl_);
    svl->addLayout(sr);
    auto* pv = new CorProfileView(midTabs_);
    profile_ = pv;
    midTabs_->addTab(v3, "3D");
    midTabs_->addTab(sv, t("gui.cor.sections"));
    midTabs_->addTab(pv, t("gui.rd.profile"));
    mv->addWidget(midTabs_, 1);

    // ---- 4: natijalar
    auto* tabs = new QTabWidget(split);
    vol_ = new QTableWidget(0, 4, tabs);
    vol_->setHorizontalHeaderLabels({t("gui.cor.col.mat"), t("gui.cor.col.avg"), t("gui.cor.col.exact"), t("gui.cor.col.diff")});
    vol_->verticalHeader()->setVisible(false);
    vol_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    st_ = new QTableWidget(0, 6, tabs);
    st_->setHorizontalHeaderLabels({t("xl.c.pk"), t("xl.c.ground"), t("xl.c.design"), t("xl.c.workz"), t("xl.c.cutA"), t("xl.c.fillA")});
    st_->verticalHeader()->setVisible(false);
    st_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    checks_ = new QListWidget(tabs);
    checks_->setWordWrap(true);
    connect(checks_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        const QVariant v = it->data(Qt::UserRole);
        if (v.isValid()) emit locateRequested(v.toPointF().x(), v.toPointF().y());
    });
    layers_ = new QTableWidget(0, 2, tabs);
    layers_->setHorizontalHeaderLabels({t("gui.bim.col.layer"), t("gui.bim.col.kind")});
    layers_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layers_->verticalHeader()->setVisible(false);
    tabs->addTab(vol_, t("gui.rd.quant"));
    tabs->addTab(st_, t("gui.rd.stations"));
    tabs->addTab(checks_, t("gui.bim.checks"));
    tabs->addTab(layers_, t("gui.rd.layers"));
    split->setStretchFactor(2, 3);
    split->setStretchFactor(3, 2);
    split->setSizes({290, 340, 640, 450});

    // ---- past
    auto* bottom = new QHBoxLayout;
    auto* scratch = new QPushButton(t("gui.gen.scratch"), this);
    scratch->setToolTip(t("gui.gen.scratch.tip"));
    bottom->addWidget(scratch);
    connect(scratch, &QPushButton::clicked, this, [this] { emit scratchRequested(); });
    summary_ = new QLabel(this);
    summary_->setObjectName("title");
    bottom->addWidget(summary_, 1);
    auto* xlStatus = new QLabel(this);
    xlStatus->setObjectName("muted");
    bottom->addWidget(xlStatus);
    const excel::Tr tr = [this](const std::string& k, const std::vector<std::string>& a) { return loc_.tr(k, a); };
    excel_ = new ExcelLink(
        loc_, this, "koridor", [this, tr] { return excel::corridorWorkbook(c_, p_, tr); },
        [this, tr](const xlsx::Workbook& wb) {
            excel::corridorFromWorkbook(wb, c_, p_, tr);
            fillAssembly();
            fillTables();
            render();
        });
    excel_->setStatusLabel(xlStatus);
    excel_->addButtons(bottom);
    auto* draw = new QPushButton(t("gui.cor.draw"), this);
    draw->setObjectName("accent");
    auto* close = new QPushButton(QString::fromUtf8("✕"), this);
    close->setFixedWidth(44);
    bottom->addWidget(draw);
    bottom->addWidget(close);
    root->addLayout(bottom);

    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(draw, &QPushButton::clicked, this, [this] {
        const QString path = QDir::temp().filePath("asiacad-corridor.json");
        std::ofstream f(path.toStdString(), std::ios::binary | std::ios::trunc);
        f << civil::corridorToJson(c_, p_);
        f.close();
        emit drawRequested(path);
    });
    auto select = [this](int side) {
        return [this, side] {
            if (updating_) return;
            QListWidget* w = side > 0 ? rightList_ : leftList_;
            if (w->currentRow() < 0) return;
            selSide_ = side;
            selIndex_ = w->currentRow();
            updating_ = true;
            (side > 0 ? leftList_ : rightList_)->clearSelection();
            updating_ = false;
            fillParams();
        };
    };
    connect(rightList_, &QListWidget::currentRowChanged, this, select(1));
    connect(leftList_, &QListWidget::currentRowChanged, this, select(-1));
    auto moveSel = [this](int dir) {
        auto& a = p_.assemblies.front();
        auto& v = (selSide_ > 0 || a.symmetric) ? a.right : a.left;
        const int j = selIndex_ + dir;
        if (selIndex_ < 0 || j < 0 || j >= static_cast<int>(v.size())) return;
        std::swap(v[static_cast<std::size_t>(selIndex_)], v[static_cast<std::size_t>(j)]);
        selIndex_ = j;
        fillAssembly();
        modelChanged();
    };
    connect(up, &QPushButton::clicked, this, [moveSel] { moveSel(-1); });
    connect(down, &QPushButton::clicked, this, [moveSel] { moveSel(1); });
    connect(del, &QPushButton::clicked, this, [this] { removeSub(selSide_, selIndex_); });
    connect(symmetric_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_) return;
        auto& a = p_.assemblies.front();
        if (!on && a.symmetric) a.left = a.right;
        a.symmetric = on;
        fillAssembly();
        modelChanged();
    });
    connect(params_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (updating_ || it->column() != 1) return;
        bool ok = false;
        const double v = it->text().replace(',', '.').toDouble(&ok);
        const auto key = params_->item(it->row(), 0)->data(Qt::UserRole).toString().toStdString();
        if (ok) setParam(selSide_, selIndex_, key, v);
    });
    connect(previewCase_, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    for (auto* s : {q_, n_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] {
            if (updating_) return;
            p_.assemblies.front().q = q_->value();
            p_.assemblies.front().n = n_->value();
            modelChanged();
        });
    for (auto* s : {step_, maxDl_, ground_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) reread(); });
    connect(vexag_, &QDoubleSpinBox::valueChanged, this, [this] { render(); });
    connect(secSlider_, &QSlider::valueChanged, this, [this](int i) { showStation(i); });
    connect(prev, &QPushButton::clicked, this, [this] { secSlider_->setValue(secSlider_->value() - 1); });
    connect(next, &QPushButton::clicked, this, [this] { secSlider_->setValue(secSlider_->value() + 1); });
    connect(st_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (updating_ || it->column() != 2) return;
        bool ok = false;
        const double z = it->text().replace(',', '.').toDouble(&ok);
        if (ok) setDesign(it->row(), z);
    });
    connect(st_, &QTableWidget::currentCellChanged, this, [this](int r) {
        if (!updating_ && r >= 0) secSlider_->setValue(r);
    });
}

void CorridorDialog::fillPalette() {
    // 1-yorliq: yig'malar
    auto* asmSide = new TemplateSidebar(t("gui.cor.pal.assemblies"), t("gui.side.search"), t("gui.side.apply"), t("gui.side.hint"), palette_);
    std::string grp;
    for (std::size_t i = 0; i < asms_.size(); ++i) {
        const auto& a = asms_[i];
        if (a.group != grp) {
            grp = a.group;
            asmSide->addSection(t(("cor.grp." + grp).c_str()));
        }
        QStringList parts;
        for (const auto& s : a.right) parts << subName(s.kind);
        asmSide->addTemplate(QString::fromStdString(a.name), parts.join(" · "), QVariant(static_cast<int>(i)),
                             grp == "ROAD" ? QColor(70, 72, 78) : grp == "CANAL" ? QColor(60, 150, 230) : QColor(150, 110, 70));
    }
    connect(asmSide, &TemplateSidebar::templateActivated, this, [this](const QVariant& v) {
        const int i = v.toInt();
        if (i >= 0 && i < static_cast<int>(asms_.size())) applyAssembly(asms_[static_cast<std::size_t>(i)]);
    });
    palette_->addTab(asmSide, t("gui.cor.pal.assemblies"));
    // qolgan yorliqlar: qismlar bo'limlar bo'yicha
    for (const char* cat : kCats) {
        auto* list = new QListWidget(palette_);
        list->setIconSize(QSize(44, 22));
        list->setSpacing(2);
        for (SubKind k : civil::subKinds()) {
            if (std::string(civil::subCategory(k)) != cat) continue;
            auto* it = new QListWidgetItem(subIcon(k), subName(k));
            it->setData(Qt::UserRole, QString::fromLatin1(civil::subKey(k)));
            it->setToolTip(t((std::string("cor.tip.") + civil::subKey(k)).c_str()));
            list->addItem(it);
        }
        connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
            SubKind k;
            if (civil::subFromKey(it->data(Qt::UserRole).toString().toStdString(), k)) addSub(k, addSide_->currentData().toInt());
        });
        palette_->addTab(list, t((std::string("cor.cat.") + cat).c_str()));
    }
}

void CorridorDialog::fillLayers() {
    const auto prev = layerMap();  // foydalanuvchi tanlovlari saqlanadi
    updating_ = true;
    layers_->setRowCount(0);
    for (const auto& [name, l] : doc_.layers()) {
        if (name.rfind("KOR_", 0) == 0) continue;
        const civil::RoadLayer r = civil::guessRoadLayer(name);
        civil::WwLayer w = civil::guessWwLayer(name);
        if (r == civil::RoadLayer::Axis) w = civil::WwLayer::Axis;
        else if (w == civil::WwLayer::Ignore && r == civil::RoadLayer::Ground) w = civil::WwLayer::Ground;
        else if (w == civil::WwLayer::Ignore && r == civil::RoadLayer::Design) w = civil::WwLayer::Design;
        const int row = layers_->rowCount();
        layers_->insertRow(row);
        auto* it = new QTableWidgetItem(QString::fromStdString(name));
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        layers_->setItem(row, 0, it);
        auto* cb = new QComboBox(layers_);
        for (auto kk : {civil::WwLayer::Ignore, civil::WwLayer::Axis, civil::WwLayer::Ground, civil::WwLayer::Design})
            cb->addItem(t((std::string("cor.layer.") + civil::wwLayerKey(kk)).c_str()), QString::fromLatin1(civil::wwLayerKey(kk)));
        if (auto pv = prev.find(name); pv != prev.end()) w = pv->second;
        cb->setCurrentIndex(std::max(0, cb->findData(QString::fromLatin1(civil::wwLayerKey(w)))));
        connect(cb, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) reread(); });
        layers_->setCellWidget(row, 1, cb);
    }
    updating_ = false;
}

std::map<std::string, civil::WwLayer> CorridorDialog::layerMap() const {
    std::map<std::string, civil::WwLayer> m;
    for (int r = 0; r < layers_->rowCount(); ++r) {
        civil::WwLayer k = civil::WwLayer::Ignore;
        if (auto* cb = qobject_cast<QComboBox*>(layers_->cellWidget(r, 1))) civil::wwLayerFromKey(cb->currentData().toString().toStdString(), k);
        m[layers_->item(r, 0)->text().toStdString()] = k;
    }
    return m;
}

void CorridorDialog::fillAssembly() {
    updating_ = true;
    const auto& a = p_.assemblies.front();
    asmName_->setText(QString::fromStdString(a.name) + "  ·  " + t(("cor.grp." + (a.group.empty() ? std::string("ROAD") : a.group)).c_str()));
    symmetric_->setChecked(a.symmetric);
    auto fill = [&](QListWidget* w, const std::vector<civil::Subassembly>& v) {
        w->clear();
        for (const auto& s : v) {
            QStringList ps;
            for (std::size_t i = 0; i < s.p.size() && i < 3; ++i) ps << QStringLiteral("%1=%2").arg(QString::fromStdString(s.p[i].key)).arg(s.p[i].value);
            auto* it = new QListWidgetItem(subIcon(s.kind), subName(s.kind) + "\n" + ps.join(" "));
            w->addItem(it);
        }
    };
    fill(rightList_, a.right);
    fill(leftList_, a.symmetric ? a.right : a.left);
    leftList_->setEnabled(!a.symmetric);
    q_->setValue(a.q);
    n_->setValue(a.n);
    step_->setValue(p_.step);
    maxDl_->setValue(p_.maxDaylight);
    ground_->setValue(p_.ground);
    QListWidget* w = selSide_ > 0 || a.symmetric ? rightList_ : leftList_;
    if (selIndex_ >= w->count()) selIndex_ = w->count() - 1;
    if (selIndex_ >= 0) w->setCurrentRow(selIndex_);
    updating_ = false;
    fillParams();
    updatePreview();
}

void CorridorDialog::fillParams() {
    const bool was = updating_;
    updating_ = true;
    params_->setRowCount(0);
    const auto& a = p_.assemblies.front();
    const auto& v = (selSide_ > 0 || a.symmetric) ? a.right : a.left;
    if (selIndex_ >= 0 && selIndex_ < static_cast<int>(v.size())) {
        for (const auto& q : v[static_cast<std::size_t>(selIndex_)].p) {
            const int r = params_->rowCount();
            params_->insertRow(r);
            auto* k = new QTableWidgetItem(t(("cor.p." + q.key).c_str()));
            k->setData(Qt::UserRole, QString::fromStdString(q.key));
            k->setFlags(k->flags() & ~Qt::ItemIsEditable);
            params_->setItem(r, 0, k);
            auto* val = new QTableWidgetItem(QString::number(q.value));
            val->setBackground(QColor(255, 242, 204, 60));
            params_->setItem(r, 1, val);
        }
    }
    updating_ = was;
}

void CorridorDialog::updatePreview() {
    const bool cut = previewCase_->currentIndex() == 1;
    const auto& a = p_.assemblies.front();
    const bool water = a.group != "ROAD";
    // namuna yer: to'kmada — loyihadan 1.5 m past, kesmada — 2 m baland (kanalda — qirg'oqdan 1 m yuqori), ozgina nishab
    double top = 0;
    for (const auto& s : a.right)
        if (s.kind == SubKind::ChannelHalf || s.kind == SubKind::FlumeHalf) top = s.get("h");
    const double base = cut ? (water ? top + 1.0 : 2.0) : (water ? top - 0.8 : -1.5);
    preview_ = civil::buildSection(a, 100.0, [&](double o) { return 100.0 + base + 0.02 * o; }, p_.maxDaylight, water ? 0.001 : 0);
    preview_view_->setSection(&preview_, cut ? t("gui.cor.case.cut") : t("gui.cor.case.fill"));
}

void CorridorDialog::applyAssembly(const civil::Assembly& a) {
    p_.assemblies = {a};
    selSide_ = 1;
    selIndex_ = a.right.empty() ? -1 : 0;
    fillAssembly();
    if (c_.designPoints == 0) reread();  // loyiha belgisi yo'q — yig'ma turiga bog'liq
    else modelChanged();
}

void CorridorDialog::addSub(SubKind k, int side) {
    auto& a = p_.assemblies.front();
    const civil::Subassembly s = civil::defaultSub(k);
    auto insert = [&](std::vector<civil::Subassembly>& v) {
        // markaziy qismlar boshiga; qiyalikdan keyin qo'shilmaydi — undan oldinga
        if (civil::subIsCenter(k)) {
            v.insert(v.begin(), s);
            return 0;
        }
        std::size_t pos = v.size();
        while (pos > 0 && civil::subIsDaylight(v[pos - 1].kind) && !civil::subIsDaylight(k)) --pos;
        if (civil::subIsDaylight(k))
            while (pos > 0 && civil::subIsDaylight(v[pos - 1].kind)) {  // eski qiyalik almashtiriladi
                v.erase(v.begin() + static_cast<long>(pos - 1));
                --pos;
            }
        v.insert(v.begin() + static_cast<long>(pos), s);
        return static_cast<int>(pos);
    };
    if (side == 0 || a.symmetric) {
        const int i = insert(a.right);
        if (!a.symmetric) insert(a.left);
        selSide_ = 1;
        selIndex_ = i;
    } else {
        selSide_ = side;
        selIndex_ = insert(side > 0 ? a.right : a.left);
    }
    fillAssembly();
    modelChanged();
}

void CorridorDialog::removeSub(int side, int index) {
    auto& a = p_.assemblies.front();
    auto& v = (side > 0 || a.symmetric) ? a.right : a.left;
    if (index < 0 || index >= static_cast<int>(v.size())) return;
    v.erase(v.begin() + index);
    selIndex_ = std::min(index, static_cast<int>(v.size()) - 1);
    fillAssembly();
    modelChanged();
}

void CorridorDialog::setParam(int side, int index, const std::string& key, double val) {
    auto& a = p_.assemblies.front();
    auto& v = (side > 0 || a.symmetric) ? a.right : a.left;
    if (index < 0 || index >= static_cast<int>(v.size())) return;
    v[static_cast<std::size_t>(index)].set(key, val);
    const int keep = selIndex_;
    fillAssembly();
    selIndex_ = keep;
    modelChanged();
}

void CorridorDialog::setDesign(int i, double z) {
    if (i < 0 || i >= static_cast<int>(c_.sec.size())) return;
    c_.sec[static_cast<std::size_t>(i)].design = z;
    civil::recomputeCorridor(c_, p_);
    fillTables();
    render();
}

void CorridorDialog::reread() {
    {   // chizmada qatlamlar o'zgargan bo'lsa (yangi chizma, generator) — jadval yangilanadi
        std::size_t n = 0;
        bool same = true;
        for (const auto& [name, l] : doc_.layers()) {
            if (name.rfind("KOR_", 0) == 0) continue;
            same = same && static_cast<int>(n) < layers_->rowCount() && layers_->item(static_cast<int>(n), 0)->text().toStdString() == name;
            ++n;
        }
        if (!same || static_cast<int>(n) != layers_->rowCount()) fillLayers();
    }
    p_.step = step_->value();
    p_.maxDaylight = maxDl_->value();
    p_.ground = ground_->value();
    p_.layers = layerMap();
    c_ = civil::buildCorridor(doc_, p_);
    fillTables();
    render();
}

void CorridorDialog::modelChanged() {
    civil::recomputeCorridor(c_, p_);
    updatePreview();
    fillTables();
    render();
}

void CorridorDialog::showTab(int i) { midTabs_->setCurrentIndex(i); }

void CorridorDialog::showStation(int i) {
    if (c_.sec.empty()) {
        secView_->setSection(nullptr, {});
        return;
    }
    i = std::clamp(i, 0, static_cast<int>(c_.sec.size()) - 1);
    if (secSlider_->value() != i) secSlider_->setValue(i);  // signal qayta shu yerga keladi
    const auto& s = c_.sec[static_cast<std::size_t>(i)];
    secLbl_->setText(QString::fromStdString(civil::pk(s.s)));
    secView_->setSection(&s, QString::fromStdString(civil::pk(s.s)) + "  ·  " + QString::fromStdString(p_.assemblies[static_cast<std::size_t>(std::clamp(s.region, 0, static_cast<int>(p_.assemblies.size()) - 1))].name));
}

void CorridorDialog::fillTables() {
    updating_ = true;
    {
        BulkFill bulk(st_);
        st_->setRowCount(0);
        for (const auto& s : c_.sec) {
            const int r = st_->rowCount();
            st_->insertRow(r);
            const QStringList cells{QString::fromStdString(civil::pk(s.s)), f2(s.ground), f2(s.design), f2(s.design - s.ground), f2(s.cut), f2(s.fill)};
            for (int k = 0; k < cells.size(); ++k) {
                auto* it = new QTableWidgetItem(cells[k]);
                if (k != 2) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
                else it->setBackground(QColor(255, 242, 204, 60));
                if (k == 0 && (!s.daylightL || !s.daylightR)) it->setForeground(QColor(232, 70, 80));
                st_->setItem(r, k, it);
            }
        }
    }
    {
        BulkFill bulk(vol_);
        vol_->setRowCount(0);
        auto row = [&](const QString& name, double a, double e, bool bold) {
            const int r = vol_->rowCount();
            vol_->insertRow(r);
            const QStringList cells{name, f1(a), f1(e), std::abs(a) > 0.05 ? QString::number((e - a) / a * 100, 'f', 2) + " %" : QString("—")};
            for (int k = 0; k < 4; ++k) {
                auto* it = new QTableWidgetItem(cells[k]);
                it->setFlags(it->flags() & ~Qt::ItemIsEditable);
                if (k) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                if (bold) {
                    QFont f = it->font();
                    f.setBold(true);
                    it->setFont(f);
                }
                vol_->setItem(r, k, it);
            }
        };
        row(t("xl.c.cutV"), c_.cutAvg, c_.cutExact, true);
        row(t("xl.c.fillV"), c_.fillAvg, c_.fillExact, true);
        for (const auto& [m, ae] : c_.volumes) row(t(("cor.mat." + m).c_str()), ae.first, ae.second, false);
    }
    checks_->clear();
    for (const auto& is : c_.issues) {
        auto* it = new QListWidgetItem(QString::fromStdString(loc_.tr(is.error ? "sewer.lvl.error" : "sewer.lvl.warning") + ": " + loc_.tr(is.key, is.args)));
        it->setForeground(is.error ? QColor(232, 70, 80) : QColor(230, 160, 30));
        if (!c_.sec.empty()) {
            const auto& q = *std::min_element(c_.sec.begin(), c_.sec.end(), [&](const auto& a, const auto& b) { return std::abs(a.s - is.s) < std::abs(b.s - is.s); });
            it->setData(Qt::UserRole, QPointF(q.x, q.y));
            it->setToolTip(t("gui.gen.locate"));
        }
        checks_->addItem(it);
    }
    if (c_.issues.empty()) checks_->addItem(t("gui.bim.ok"));
    summary_->setText(t("gui.cor.summary", {f1(c_.length), QString::number(c_.sec.size()), QString::number(c_.cutExact, 'f', 0),
                                            QString::number(c_.fillExact, 'f', 0), QString::number(c_.errors())}));
    auto* pv = static_cast<CorProfileView*>(profile_);
    pv->c = &c_;
    pv->g = t("road.lbl.ground");
    pv->d = t("road.lbl.design");
    pv->update();
    secSlider_->setRange(0, std::max(0, static_cast<int>(c_.sec.size()) - 1));
    updating_ = false;
    showStation(secSlider_->value());
}

void CorridorDialog::render() {
    view_->beginScene(t("gui.cor.title"), summary_->text() + QStringLiteral("  ·  ×%1").arg(vexag_->value(), 0, 'f', 0));
    if (c_.sec.size() >= 2) {
        double zs = 0;
        for (const auto& s : c_.sec) zs += s.ground;
        const double z0 = zs / c_.sec.size(), k = vexag_->value();
        auto W = [&](const civil::Section& s, double o, double z) { return P3{s.x + s.dy * o, s.y - s.dx * o, z0 + (z - z0) * k}; };
        auto segColor = [](const std::string& a, const std::string& b) {
            auto has = [&](const char* x) { return a.find(x) != std::string::npos || b.find(x) != std::string::npos; };
            auto both = [&](std::initializer_list<const char*> xs) {
                bool fa = false, fb = false;
                for (const char* x : xs) { fa |= a.rfind(x, 0) == 0; fb |= b.rfind(x, 0) == 0; }
                return fa && fb;
            };
            if (has("DAYLIGHT") || has("BENCH") || has("TR_TOP") || has("WALL_TOE")) return QColor(120, 160, 80);
            if (both({"CROWN", "ETW", "MEDIAN_F"})) return QColor(70, 72, 78);
            if (has("ETS")) return QColor(150, 140, 120);
            if (has("CH_") || has("FL") || has("CURB") || has("WALL") || has("MEDIAN")) return QColor(175, 178, 182);
            if (has("SW_")) return QColor(200, 120, 110);
            if (has("DITCH") || has("BANK") || has("TR_")) return QColor(150, 115, 75);
            return QColor(130, 130, 120);
        };
        for (std::size_t i = 0; i + 1 < c_.sec.size(); ++i) {
            const auto& A = c_.sec[i];
            const auto& B = c_.sec[i + 1];
            std::map<std::tuple<int, std::string, int>, const civil::SecPoint*> mb;
            {
                std::map<std::pair<int, std::string>, int> cnt;
                for (const auto& q : B.pts) mb[{q.side, q.code, cnt[{q.side, q.code}]++}] = &q;
            }
            std::map<std::pair<int, std::string>, int> cnt;
            std::vector<std::tuple<int, std::string, int>> keys;
            for (const auto& q : A.pts) keys.push_back({q.side, q.code, cnt[{q.side, q.code}]++});
            for (std::size_t j = 0; j + 1 < A.pts.size(); ++j) {
                auto ia = mb.find(keys[j]), ib = mb.find(keys[j + 1]);
                if (ia == mb.end() || ib == mb.end()) continue;
                const auto& p0 = A.pts[j];
                const auto& p1 = A.pts[j + 1];
                view_->addFace({W(A, p0.o, p0.z), W(A, p1.o, p1.z), W(B, ib->second->o, ib->second->z), W(B, ia->second->o, ia->second->z)},
                               segColor(p0.code, p1.code), 255, false);
            }
            // suv yuzasi
            auto waterTop = [](const civil::Section& s, double& l, double& r) {
                for (const auto& sh : s.shapes)
                    if (sh.material == "WATER" && sh.poly.size() >= 2) {
                        l = std::min(sh.poly[0].first, sh.poly[1].first);
                        r = std::max(sh.poly[0].first, sh.poly[1].first);
                        return true;
                    }
                return false;
            };
            double la, ra, lb, rb;
            if (A.water > 0 && B.water > 0 && waterTop(A, la, ra) && waterTop(B, lb, rb))
                view_->addFace({W(A, la, A.water), W(A, ra, A.water), W(B, rb, B.water), W(B, lb, B.water)}, QColor(60, 150, 230), 170, false);
            // yer (qiyalikdan tashqarida 10 m)
            if (!A.pts.empty() && !B.pts.empty())
                for (int sd : {-1, 1}) {
                    const auto& ea = sd < 0 ? A.pts.front() : A.pts.back();
                    const auto& eb = sd < 0 ? B.pts.front() : B.pts.back();
                    const double ga = A.groundLine.empty() ? ea.z : (sd < 0 ? A.groundLine.front().second : A.groundLine.back().second);
                    const double gb = B.groundLine.empty() ? eb.z : (sd < 0 ? B.groundLine.front().second : B.groundLine.back().second);
                    view_->addFace({W(A, ea.o, ea.z), W(A, ea.o + sd * 10, ga), W(B, eb.o + sd * 10, gb), W(B, eb.o, eb.z)}, QColor(160, 115, 70), 80, false);
                }
        }
        for (const auto& is : c_.issues) {
            if (!is.error) continue;
            for (const auto& s : c_.sec)
                if (std::abs(s.s - is.s) < 0.01) {
                    view_->addPolyline({W(s, 0, s.design), W(s, 0, s.design + 5)}, QColor(255, 60, 60), 3);
                    view_->addLabel(W(s, 0, s.design + 5.5), QStringLiteral("⚠ ") + QString::fromStdString(civil::pk(s.s)));
                    break;
                }
        }
    }
    view_->endScene(true);
}

} // namespace asiacad::gui
