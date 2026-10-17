#include "generator_dialogs.h"

#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace asiacad::gui {

namespace {
QString f2(double v) { return QString::number(v, 'f', 2); }
double cell(const QTableWidget* t, int r, int c, bool* ok = nullptr) {
    const auto* it = t->item(r, c);
    bool k = false;
    const double v = it ? it->text().trimmed().replace(',', '.').toDouble(&k) : 0;
    if (ok) *ok = k;
    return v;
}
void setCell(QTableWidget* t, int r, int c, const QString& s) {
    auto* it = t->item(r, c);
    if (!it) t->setItem(r, c, new QTableWidgetItem(s));
    else it->setText(s);
}
QString num(double v) { return QString::number(v, 'g', 12); }
QTableWidget* table(QWidget* p, const QStringList& head) {
    auto* t = new QTableWidget(0, static_cast<int>(head.size()), p);
    t->setHorizontalHeaderLabels(head);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setDefaultSectionSize(22);
    return t;
}
QHBoxLayout* rowButtons(QWidget* p, QTableWidget* t, const std::function<void()>& changed) {
    auto* h = new QHBoxLayout;
    auto* add = new QPushButton("+", p);
    auto* del = new QPushButton(QString::fromUtf8("−"), p);
    add->setFixedWidth(34);
    del->setFixedWidth(34);
    h->addWidget(add);
    h->addWidget(del);
    h->addStretch(1);
    QObject::connect(add, &QPushButton::clicked, p, [t, changed] {
        const int r = t->currentRow() >= 0 ? t->currentRow() + 1 : t->rowCount();
        t->insertRow(r);
        for (int c = 0; c < t->columnCount(); ++c)
            if (r > 0 && t->item(r - 1, c)) t->setItem(r, c, new QTableWidgetItem(t->item(r - 1, c)->text()));
        changed();
    });
    QObject::connect(del, &QPushButton::clicked, p, [t, changed] {
        if (t->currentRow() >= 0) t->removeRow(t->currentRow());
        changed();
    });
    return h;
}
const char* kNetKinds[] = {"SEWER", "STORM", "WATER", "GAS", "HEAT", "POWER", "TELECOM", "DRAIN"};
}  // namespace

// ---------------------------------------------------------------- 2D ko'rinish
void PlanPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), dark_ ? QColor(24, 27, 33) : QColor(250, 250, 248));
    BBox b;
    for (const auto& e : es_)
        if (e->type() != EntityType::Text) b.expand(e->bbox());
    if (!b.valid()) return;
    const double w = std::max(1e-6, b.max.x - b.min.x), h = std::max(1e-6, b.max.y - b.min.y);
    const double k = std::min((width() - 20) / w, (height() - 20) / h);
    const double ox = (width() - w * k) / 2, oy = (height() - h * k) / 2;
    auto P = [&](const Vec3& v) { return QPointF(ox + (v.x - b.min.x) * k, height() - oy - (v.y - b.min.y) * k); };
    auto color = [&](const std::string& l) {
        if (l == "USTUN") return QColor(230, 90, 80);
        if (l == "DEVOR") return QColor(230, 170, 60);
        if (l == "PLITA") return QColor(110, 160, 230);
        if (l.find("O'Q") != std::string::npos) return QColor(240, 80, 80);
        if (l == "KOCHA_CHETI") return QColor(150, 150, 150);
        if (l == "KOCHA_YUZA") return QColor(0, 0, 0, 0);
        if (l == "KVARTAL") return QColor(90, 170, 110);
        if (l == "SHAHAR_BINO") return QColor(220, 190, 120);
        if (l == "YOZUV" || l == "LOYIHA" || l == "PIKET" || l == "SHAHAR_YOZUV") return dark_ ? QColor(170, 175, 185) : QColor(90, 90, 100);
        if (l == "SHAHAR_3D") return QColor(0, 0, 0, 0);
        return dark_ ? QColor(120, 160, 220) : QColor(40, 90, 170);
    };
    for (const auto& e : es_) {
        const QColor c = color(e->layer());
        if (c.alpha() == 0) continue;
        p.setPen(QPen(c, e->layer().find("O'Q") != std::string::npos ? 1.6 : 1.0));
        p.setBrush(Qt::NoBrush);
        if (auto* l = dynamic_cast<const LineEntity*>(e.get())) p.drawLine(P(l->startPoint()), P(l->endPoint()));
        else if (auto* c2 = dynamic_cast<const CircleEntity*>(e.get())) {
            const double r = std::max(2.0, c2->radius() * k);
            p.drawEllipse(P(c2->center()), r, r);
        } else if (auto* pl = dynamic_cast<const PolylineEntity*>(e.get())) {
            const auto tv = pl->tessellated().vertices();
            QPainterPath path;
            for (std::size_t i = 0; i < tv.size(); ++i) (i ? path.lineTo(P(tv[i])) : path.moveTo(P(tv[i])));
            if (pl->isClosed()) path.closeSubpath();
            if (e->layer() == "USTUN") p.setBrush(c);
            p.drawPath(path);
        } else if (auto* t = dynamic_cast<const TextEntity*>(e.get())) {
            const double hp = t->height() * k;
            if (hp < 6) continue;
            QFont f = p.font();
            f.setPixelSize(static_cast<int>(std::min(hp, 16.0)));
            p.setFont(f);
            const QPointF a = P(t->insertion());
            p.save();
            p.translate(a);
            p.rotate(-t->rotation() * 180 / 3.14159265358979);
            const QString s = QString::fromStdString(t->text());
            const int tw = p.fontMetrics().horizontalAdvance(s);
            const double dx = t->hAlign() == TextHAlign::Center ? -tw / 2.0 : 0;
            const double dy = t->vAlign() == TextVAlign::Middle ? f.pixelSize() / 2.5 : 0;
            p.drawText(QPointF(dx, dy), s);
            p.restore();
        }
    }
}

// ---------------------------------------------------------------- umumiy
GenBase::GenBase(const i18n::Localizer& loc, bool dark, QWidget* parent) : QDialog(parent), loc_(loc), dark_(dark) {}

QString GenBase::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

QString GenBase::writeTemp(const std::string& json, const QString& name) const {
    const QString path = QDir::temp().filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(json.data(), static_cast<qint64>(json.size()));
    return path;
}

// ---------------------------------------------------------------- GRID
GridInputDialog::GridInputDialog(const i18n::Localizer& loc, bool dark, QWidget* parent) : GenBase(loc, dark, parent) {
    setWindowTitle(t("gui.gen.grid.title"));
    resize(820, 520);
    auto* root = new QHBoxLayout(this);
    auto* left = new QWidget(this);
    auto* lv = new QVBoxLayout(left);
    auto* hint = new QLabel(t("gui.gen.grid.hint"), left);
    hint->setWordWrap(true);
    lv->addWidget(hint);
    auto* presets = new QComboBox(left);
    presets->addItem(t("gui.gen.grid.preset"), QStringList{});
    presets->addItem(t("gui.gen.grid.p.house"), QStringList{"6x4", "6x2", "9"});
    presets->addItem(t("gui.gen.grid.p.office"), QStringList{"9x6", "9x3", "12"});
    presets->addItem(t("gui.gen.grid.p.school"), QStringList{"6x10", "6,3,6", "4"});
    presets->addItem(t("gui.gen.grid.p.mall"), QStringList{"12x8", "12x6", "3"});
    presets->addItem(t("gui.gen.grid.p.tower"), QStringList{"8.4x4", "8.4x4", "25"});
    lv->addWidget(presets);
    auto* form = new QFormLayout;
    auto dspin = [&](double lo, double hi, double step, int dec, double v) {
        auto* s = new QDoubleSpinBox(left);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        s->setValue(v);
        return s;
    };
    dx_ = new QLineEdit("6x5,4.5", left);
    dy_ = new QLineEdit("6x3", left);
    colB_ = dspin(0.2, 3, 0.05, 2, 0.4);
    colH_ = dspin(0.2, 3, 0.05, 2, 0.4);
    walls_ = new QCheckBox(t("gui.gen.grid.walls"), left);
    walls_->setChecked(true);
    wallT_ = dspin(0.1, 1, 0.05, 2, 0.25);
    slab_ = new QCheckBox(t("gui.gen.grid.slab"), left);
    slab_->setChecked(true);
    floors_ = new QSpinBox(left);
    floors_->setRange(1, 150);
    floors_->setValue(5);
    floorH_ = dspin(2.4, 12, 0.1, 2, 3.0);
    x0_ = dspin(-1e7, 1e7, 1, 3, 0);
    y0_ = dspin(-1e7, 1e7, 1, 3, 0);
    form->addRow(t("gui.gen.grid.dx"), dx_);
    form->addRow(t("gui.gen.grid.dy"), dy_);
    auto* col = new QHBoxLayout;
    col->addWidget(colB_);
    col->addWidget(new QLabel(QString::fromUtf8("×"), left));
    col->addWidget(colH_);
    form->addRow(t("gui.gen.grid.col"), col);
    form->addRow(walls_, wallT_);
    form->addRow(slab_);
    form->addRow(t("gui.bim.floors"), floors_);
    form->addRow(t("gui.bim.height"), floorH_);
    form->addRow("X0", x0_);
    form->addRow("Y0", y0_);
    lv->addLayout(form);
    info_ = new QLabel(left);
    info_->setWordWrap(true);
    lv->addWidget(info_);
    lv->addStretch(1);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, left);
    bb->button(QDialogButtonBox::Ok)->setText(t("gui.gen.draw"));
    bb->button(QDialogButtonBox::Cancel)->setText(t("gui.gen.cancel"));
    lv->addWidget(bb);
    root->addWidget(left);
    prev_ = new PlanPreview(this);
    prev_->setDark(dark);
    root->addWidget(prev_, 1);
    connect(bb, &QDialogButtonBox::accepted, this, [this] { if (valid()) accept(); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(presets, &QComboBox::currentIndexChanged, this, [this, presets](int i) {
        const auto l = presets->itemData(i).toStringList();
        if (l.size() < 3) return;
        setSpacing(l[0], l[1]);
        floors_->setValue(l[2].toInt());
    });
    for (auto* e : {dx_, dy_}) connect(e, &QLineEdit::textChanged, this, [this] { update_(); });
    for (auto* s : {colB_, colH_, wallT_, floorH_, x0_, y0_}) connect(s, &QDoubleSpinBox::valueChanged, this, [this] { update_(); });
    connect(floors_, &QSpinBox::valueChanged, this, [this] { update_(); });
    for (auto* c : {walls_, slab_}) connect(c, &QCheckBox::toggled, this, [this] { update_(); });
    update_();
}

void GridInputDialog::setSpacing(const QString& dx, const QString& dy) {
    dx_->setText(dx);
    dy_->setText(dy);
}

civil::GridSpec GridInputDialog::spec() const {
    civil::GridSpec g;
    g.dx = civil::parseSpacing(dx_->text().toStdString());
    g.dy = civil::parseSpacing(dy_->text().toStdString());
    g.colB = colB_->value();
    g.colH = colH_->value();
    g.walls = walls_->isChecked();
    g.wallT = wallT_->value();
    g.slab = slab_->isChecked();
    g.floors = floors_->value();
    g.floorH = floorH_->value();
    g.x0 = x0_->value();
    g.y0 = y0_->value();
    return g;
}
bool GridInputDialog::valid() const {
    const auto g = spec();
    return !g.dx.empty() && !g.dy.empty();
}
QString GridInputDialog::command() const { return QStringLiteral("GRID \"%1\"").arg(writeTemp(civil::gridToJson(spec()), "asiacad_grid.json")); }

void GridInputDialog::update_() {
    const auto g = spec();
    dx_->setStyleSheet(g.dx.empty() ? "border:1px solid #e04850" : "");
    dy_->setStyleSheet(g.dy.empty() ? "border:1px solid #e04850" : "");
    if (g.dx.empty() || g.dy.empty()) {
        info_->setText(t("gen.err.spacing", {g.dx.empty() ? dx_->text() : dy_->text()}));
        prev_->setEntities({});
        return;
    }
    double W = 0, D = 0;
    for (double d : g.dx) W += d;
    for (double d : g.dy) D += d;
    const auto n = (g.dx.size() + 1) * (g.dy.size() + 1);
    info_->setText(t("gui.gen.grid.info", {QString::number(g.dx.size() + 1), QString::number(g.dy.size() + 1), QString::number(n), f2(W), f2(D),
                                           QString::number(W * D * g.floors, 'f', 0), f2(g.floors * g.floorH)}));
    prev_->setEntities(civil::generateGrid(g));
}

// ---------------------------------------------------------------- ALIGN
AlignInputDialog::AlignInputDialog(const i18n::Localizer& loc, bool dark, bool canal, QWidget* parent)
    : GenBase(loc, dark, parent), canal_(canal) {
    setWindowTitle(t(canal ? "gui.gen.align.title_canal" : "gui.gen.align.title"));
    resize(940, 560);
    auto* root = new QHBoxLayout(this);
    auto* left = new QWidget(this);
    left->setMinimumWidth(360);
    auto* lv = new QVBoxLayout(left);
    auto* hint = new QLabel(t("gui.gen.align.hint"), left);
    hint->setWordWrap(true);
    lv->addWidget(hint);
    lv->addWidget(new QLabel("<b>" + t("gui.gen.align.pi") + "</b>", left));
    pi_ = table(left, {"X", "Y", "R, m"});
    lv->addWidget(pi_, 3);
    lv->addLayout(rowButtons(left, pi_, [this] { update_(); }));
    lv->addWidget(new QLabel("<b>" + t("gui.gen.align.pvi") + "</b>", left));
    pvi_ = table(left, {t("xl.c.pk") + ", m", t(canal ? "ww.lbl.bottom" : "xl.c.design")});
    lv->addWidget(pvi_, 2);
    lv->addLayout(rowButtons(left, pvi_, [this] { update_(); }));
    info_ = new QLabel(left);
    info_->setWordWrap(true);
    lv->addWidget(info_);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, left);
    bb->button(QDialogButtonBox::Ok)->setText(t("gui.gen.draw"));
    bb->button(QDialogButtonBox::Cancel)->setText(t("gui.gen.cancel"));
    lv->addWidget(bb);
    root->addWidget(left);
    prev_ = new PlanPreview(this);
    prev_->setDark(dark);
    root->addWidget(prev_, 1);
    connect(bb, &QDialogButtonBox::accepted, this, [this] { if (spec().pi.size() >= 2) accept(); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(pi_, &QTableWidget::itemChanged, this, [this] { update_(); });
    connect(pvi_, &QTableWidget::itemChanged, this, [this] { update_(); });
    civil::AlignSpec a;
    a.pi = {{0, 0, 0}, {300, 0, canal ? 100.0 : 400.0}, {550, 200, 0}};
    a.pvi = canal ? std::vector<civil::AlignPVI>{{0, 450.0}, {600, 449.4}} : std::vector<civil::AlignPVI>{{0, 450.0}, {250, 452.0}, {600, 451.0}};
    setSpec(a);
}

void AlignInputDialog::setSpec(const civil::AlignSpec& a) {
    const QSignalBlocker b1(pi_), b2(pvi_);
    pi_->setRowCount(static_cast<int>(a.pi.size()));
    for (int i = 0; i < pi_->rowCount(); ++i) {
        const auto& q = a.pi[static_cast<std::size_t>(i)];
        setCell(pi_, i, 0, num(q.x));
        setCell(pi_, i, 1, num(q.y));
        setCell(pi_, i, 2, num(q.r));
    }
    pvi_->setRowCount(static_cast<int>(a.pvi.size()));
    for (int i = 0; i < pvi_->rowCount(); ++i) {
        setCell(pvi_, i, 0, num(a.pvi[static_cast<std::size_t>(i)].s));
        setCell(pvi_, i, 1, f2(a.pvi[static_cast<std::size_t>(i)].z));
    }
    update_();
}

civil::AlignSpec AlignInputDialog::spec() const {
    civil::AlignSpec a;
    if (canal_) a.axisLayer = "CANAL_O'Q";
    for (int r = 0; r < pi_->rowCount(); ++r) {
        bool ox, oy;
        const double x = cell(pi_, r, 0, &ox), y = cell(pi_, r, 1, &oy);
        if (ox && oy) a.pi.push_back({x, y, std::max(0.0, cell(pi_, r, 2))});
    }
    for (int r = 0; r < pvi_->rowCount(); ++r) {
        bool os, oz;
        const double s = cell(pvi_, r, 0, &os), z = cell(pvi_, r, 1, &oz);
        if (os && oz) a.pvi.push_back({s, z});
    }
    std::sort(a.pvi.begin(), a.pvi.end(), [](const auto& p, const auto& q) { return p.s < q.s; });
    return a;
}
QString AlignInputDialog::command() const { return QStringLiteral("ALIGN \"%1\"").arg(writeTemp(civil::alignToJson(spec()), "asiacad_align.json")); }

void AlignInputDialog::update_() {
    const auto a = spec();
    if (a.pi.size() < 2) {
        info_->setText(t("gen.err.align_pts"));
        prev_->setEntities({});
        return;
    }
    try {
        std::vector<std::string> w;
        auto es = civil::generateAlignment(a, &w);
        double L = 0;
        int arcs = 0;
        if (auto* pl = dynamic_cast<PolylineEntity*>(es.front().get())) {
            L = pl->length2d();
            for (double b : pl->bulges()) arcs += std::abs(b) > 1e-12;
        }
        QString s = t("gen.msg.align", {f2(L), QString::number(arcs), QString::number(a.pvi.size())});
        for (const auto& x : w) {
            const auto f = QString::fromStdString(x).split('|');
            s += "\n⚠ " + (f[0] == "pvi" ? t("gen.warn.pvi", {f.value(1), f.value(2)}) : t("gen.warn.radius", {f.value(0), f.value(1), f.value(2)}));
        }
        if (a.pvi.empty()) s += "\n⚠ " + t("gui.gen.align.nopvi");
        info_->setText(s);
        prev_->setEntities(std::move(es));
    } catch (const Error& e) {
        info_->setText(QString::fromStdString(loc_.tr(e.key(), e.args())));
        prev_->setEntities({});
    }
}

// ---------------------------------------------------------------- UTILNET
NetInputDialog::NetInputDialog(const i18n::Localizer& loc, bool dark, QWidget* parent) : GenBase(loc, dark, parent) {
    setWindowTitle(t("gui.gen.net.title"));
    resize(1000, 600);
    auto* root = new QHBoxLayout(this);
    auto* left = new QWidget(this);
    left->setMinimumWidth(460);
    auto* lv = new QVBoxLayout(left);
    auto* hint = new QLabel(t("gui.gen.net.hint"), left);
    hint->setWordWrap(true);
    lv->addWidget(hint);
    lv->addWidget(new QLabel("<b>" + t("gui.gen.net.nodes") + "</b>", left));
    nodes_ = table(left, {t("gui.gen.net.name"), "X", "Y", t("gui.gen.net.rim"), t("gui.gen.net.inv")});
    lv->addWidget(nodes_, 3);
    lv->addLayout(rowButtons(left, nodes_, [this] { update_(); }));
    auto* ph = new QHBoxLayout;
    ph->addWidget(new QLabel("<b>" + t("gui.gen.net.pipes") + "</b>", left));
    ph->addStretch(1);
    auto* chainB = new QPushButton(t("gui.gen.net.chain"), left);
    ph->addWidget(chainB);
    lv->addLayout(ph);
    pipes_ = table(left, {t("gui.gen.net.from"), t("gui.gen.net.to"), t("gui.gen.net.kind"), QString::fromUtf8("Ø, mm")});
    lv->addWidget(pipes_, 2);
    auto* pb = new QHBoxLayout;
    auto* addP = new QPushButton("+", left);
    auto* delP = new QPushButton(QString::fromUtf8("−"), left);
    addP->setFixedWidth(34);
    delP->setFixedWidth(34);
    pb->addWidget(addP);
    pb->addWidget(delP);
    pb->addStretch(1);
    lv->addLayout(pb);
    info_ = new QLabel(left);
    info_->setWordWrap(true);
    lv->addWidget(info_);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, left);
    bb->button(QDialogButtonBox::Ok)->setText(t("gui.gen.draw"));
    bb->button(QDialogButtonBox::Cancel)->setText(t("gui.gen.cancel"));
    lv->addWidget(bb);
    root->addWidget(left);
    prev_ = new PlanPreview(this);
    prev_->setDark(dark);
    root->addWidget(prev_, 1);
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        try {
            const auto n = spec();
            civil::generateNetwork(n);
            if (!n.nodes.empty()) accept();
        } catch (const Error&) {}
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(chainB, &QPushButton::clicked, this, &NetInputDialog::chain);
    connect(addP, &QPushButton::clicked, this, [this] {
        const int n = pipes_->rowCount();
        addPipeRow(n + 1, n + 2, "SEWER", 300);
        update_();
    });
    connect(delP, &QPushButton::clicked, this, [this] {
        if (pipes_->currentRow() >= 0) pipes_->removeRow(pipes_->currentRow());
        update_();
    });
    connect(nodes_, &QTableWidget::itemChanged, this, [this] { if (!updating_) update_(); });
    connect(pipes_, &QTableWidget::itemChanged, this, [this] { if (!updating_) update_(); });
    civil::NetSpec n;
    n.nodes = {{"K1", 0, 0, 480.50, 478.20}, {"K2", 40, 0, 480.20, 477.95}, {"K3", 80, 10, 479.90, 477.70}, {"K4", 120, 10, 479.70, 477.45}};
    n.pipes = {{0, 1, "SEWER", 300}, {1, 2, "SEWER", 300}, {2, 3, "SEWER", 300}};
    setSpec(n);
}

void NetInputDialog::addPipeRow(int from, int to, const std::string& kind, double dn) {
    const bool u = updating_;
    updating_ = true;
    const int r = pipes_->rowCount();
    pipes_->insertRow(r);
    setCell(pipes_, r, 0, QString::number(from));
    setCell(pipes_, r, 1, QString::number(to));
    auto* cb = new QComboBox(pipes_);
    for (const char* k : kNetKinds) cb->addItem(t((std::string("util.kind.") + k).c_str()), QString(k));
    cb->setCurrentIndex(std::max(0, cb->findData(QString::fromStdString(kind))));
    connect(cb, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) update_(); });
    pipes_->setCellWidget(r, 2, cb);
    setCell(pipes_, r, 3, num(dn));
    updating_ = u;
}

void NetInputDialog::setSpec(const civil::NetSpec& n) {
    updating_ = true;
    nodes_->setRowCount(static_cast<int>(n.nodes.size()));
    for (int i = 0; i < nodes_->rowCount(); ++i) {
        const auto& q = n.nodes[static_cast<std::size_t>(i)];
        setCell(nodes_, i, 0, QString::fromStdString(q.name));
        setCell(nodes_, i, 1, num(q.x));
        setCell(nodes_, i, 2, num(q.y));
        setCell(nodes_, i, 3, f2(q.ground));
        setCell(nodes_, i, 4, f2(q.invert));
    }
    pipes_->setRowCount(0);
    for (const auto& p : n.pipes) addPipeRow(p.from + 1, p.to + 1, p.kind, p.dn);
    updating_ = false;
    update_();
}

void NetInputDialog::chain() {
    std::string kind = "SEWER";
    double dn = 300;
    if (pipes_->rowCount() > 0) {
        if (auto* cb = qobject_cast<QComboBox*>(pipes_->cellWidget(0, 2))) kind = cb->currentData().toString().toStdString();
        dn = cell(pipes_, 0, 3);
    }
    updating_ = true;
    pipes_->setRowCount(0);
    for (int i = 1; i < nodes_->rowCount(); ++i) addPipeRow(i, i + 1, kind, dn);
    updating_ = false;
    update_();
}

civil::NetSpec NetInputDialog::spec() const {
    civil::NetSpec n;
    for (int r = 0; r < nodes_->rowCount(); ++r) {
        civil::NetNode q;
        q.name = nodes_->item(r, 0) ? nodes_->item(r, 0)->text().trimmed().toStdString() : "";
        q.x = cell(nodes_, r, 1);
        q.y = cell(nodes_, r, 2);
        q.ground = cell(nodes_, r, 3);
        q.invert = cell(nodes_, r, 4);
        n.nodes.push_back(q);
    }
    for (int r = 0; r < pipes_->rowCount(); ++r) {
        civil::NetPipe p;
        p.from = static_cast<int>(cell(pipes_, r, 0)) - 1;
        p.to = static_cast<int>(cell(pipes_, r, 1)) - 1;
        if (auto* cb = qobject_cast<QComboBox*>(pipes_->cellWidget(r, 2))) p.kind = cb->currentData().toString().toStdString();
        p.dn = cell(pipes_, r, 3);
        n.pipes.push_back(p);
    }
    return n;
}
QString NetInputDialog::command() const { return QStringLiteral("UTILNET \"%1\"").arg(writeTemp(civil::netToJson(spec()), "asiacad_net.json")); }

void NetInputDialog::update_() {
    const auto n = spec();
    try {
        auto es = civil::generateNetwork(n);
        double L = 0;
        QStringList warn;
        for (const auto& p : n.pipes) {
            const auto& a = n.nodes[static_cast<std::size_t>(p.from)];
            const auto& b = n.nodes[static_cast<std::size_t>(p.to)];
            const double l = std::hypot(b.x - a.x, b.y - a.y);
            L += l;
            const bool grav = p.kind == "SEWER" || p.kind == "STORM" || p.kind == "DRAIN";
            if (grav && l > 0 && a.invert != 0 && b.invert != 0 && a.invert <= b.invert)
                warn << t("gui.gen.net.reverse", {QString::fromStdString(a.name), QString::fromStdString(b.name)});
            for (const auto* q : {&a, &b})
                if (q->invert != 0 && q->ground - q->invert < 0.7) warn << t("gui.gen.net.shallow", {QString::fromStdString(q->name), f2(q->ground - q->invert)});
        }
        warn.removeDuplicates();
        QString s = t("gui.gen.net.info", {QString::number(n.nodes.size()), QString::number(n.pipes.size()), QString::number(L, 'f', 1)});
        for (const auto& w : warn) s += "\n⚠ " + w;
        info_->setText(s);
        prev_->setEntities(std::move(es));
    } catch (const Error& e) {
        info_->setText(QString::fromStdString(loc_.tr(e.key(), e.args())));
        prev_->setEntities({});
    }
}

// ---------------------------------------------------------------- CITY
CityDialog::CityDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent) : GenBase(loc, dark, parent), doc_(doc) {
    setWindowTitle(t("gui.gen.city.title"));
    resize(1320, 760);
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);
    side_ = new TemplateSidebar(t("gui.gen.city.tpl"), t("gui.side.search"), t("gui.side.apply"), t("gui.gen.city.tplhint"), split);
    struct Tpl { const char* key; int nx, ny; double bw, bd, st, cw, sb, w, d, gap; int f0, f1; };
    static const Tpl tpls[] = {
        {"gui.gen.city.t.low", 6, 4, 80, 60, 15, 7, 5, 12, 10, 10, 2, 3},
        {"gui.gen.city.t.mid", 4, 3, 120, 90, 24, 14, 6, 60, 15, 20, 5, 9},
        {"gui.gen.city.t.high", 4, 3, 150, 110, 30, 15, 8, 54, 16, 32, 12, 17},
        {"gui.gen.city.t.tower", 3, 3, 160, 160, 36, 21, 10, 30, 30, 45, 18, 30},
        {"gui.gen.city.t.center", 6, 5, 100, 100, 30, 15, 0.5, 98, 98, 0, 4, 7},
        {"gui.gen.city.t.big", 20, 15, 140, 100, 30, 15, 8, 60, 16, 28, 9, 16},
    };
    side_->addSection(t("gui.gen.city.sec"));
    for (std::size_t i = 0; i < std::size(tpls); ++i) {
        const auto& p = tpls[i];
        side_->addTemplate(t(p.key), t("gui.gen.city.tsub", {QString::number(p.nx * p.ny), QString::number(p.f0), QString::number(p.f1)}),
                           QVariant(static_cast<int>(i)), QColor(90, 170, 110));
    }
    connect(side_, &TemplateSidebar::templateActivated, this, [this](const QVariant& v) {
        const auto& p = tpls[v.toInt()];
        civil::CitySpec c = spec();
        c.nx = p.nx; c.ny = p.ny; c.blockW = p.bw; c.blockD = p.bd; c.street = p.st; c.carriage = p.cw; c.setback = p.sb;
        c.bldW = p.w; c.bldD = p.d; c.gap = p.gap; c.floorsMin = p.f0; c.floorsMax = p.f1;
        setSpec(c);
    });

    auto* mid = new QWidget(split);
    auto* form = new QFormLayout(mid);
    auto dspin = [&](double lo, double hi, double step, int dec, double v) {
        auto* s = new QDoubleSpinBox(mid);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        s->setValue(v);
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) recompute(); });
        return s;
    };
    auto ispin = [&](int lo, int hi, int v) {
        auto* s = new QSpinBox(mid);
        s->setRange(lo, hi);
        s->setValue(v);
        connect(s, &QSpinBox::valueChanged, this, [this] { if (!updating_) recompute(); });
        return s;
    };
    const civil::CitySpec d;
    nx_ = ispin(1, 200, d.nx);
    ny_ = ispin(1, 200, d.ny);
    blockW_ = dspin(20, 2000, 5, 1, d.blockW);
    blockD_ = dspin(20, 2000, 5, 1, d.blockD);
    street_ = dspin(6, 200, 1, 1, d.street);
    carriage_ = dspin(3, 150, 0.5, 2, d.carriage);
    setback_ = dspin(0, 100, 0.5, 2, d.setback);
    bldW_ = dspin(6, 1000, 1, 1, d.bldW);
    bldD_ = dspin(6, 1000, 1, 1, d.bldD);
    gap_ = dspin(0, 500, 1, 1, d.gap);
    fmin_ = ispin(1, 150, d.floorsMin);
    fmax_ = ispin(1, 150, d.floorsMax);
    floorH_ = dspin(2.4, 12, 0.1, 2, d.floorH);
    x0_ = dspin(-1e7, 1e7, 10, 2, d.x0);
    y0_ = dspin(-1e7, 1e7, 10, 2, d.y0);
    ground_ = dspin(-500, 9000, 0.5, 2, d.ground);
    seed_ = ispin(1, 99999, static_cast<int>(d.seed));
    auto pair = [&](QWidget* a, QWidget* b) {
        auto* h = new QHBoxLayout;
        h->addWidget(a);
        h->addWidget(new QLabel(QString::fromUtf8("×"), mid));
        h->addWidget(b);
        return h;
    };
    form->addRow(t("gui.gen.city.blocks"), pair(nx_, ny_));
    form->addRow(t("gui.gen.city.block"), pair(blockW_, blockD_));
    form->addRow(t("gui.gen.city.street"), street_);
    form->addRow(t("gui.gen.city.carriage"), carriage_);
    form->addRow(t("gui.gen.city.setback"), setback_);
    form->addRow(t("gui.gen.city.bld"), pair(bldW_, bldD_));
    form->addRow(t("gui.gen.city.gap"), gap_);
    auto* fl = new QHBoxLayout;
    fl->addWidget(fmin_);
    fl->addWidget(new QLabel(QString::fromUtf8("…"), mid));
    fl->addWidget(fmax_);
    form->addRow(t("gui.gen.city.floors"), fl);
    form->addRow(t("gui.bim.height"), floorH_);
    form->addRow("X0, Y0", pair(x0_, y0_));
    form->addRow(t("gui.gen.city.ground"), ground_);
    form->addRow(t("gui.gen.city.seed"), seed_);
    stats_ = new QLabel(mid);
    stats_->setWordWrap(true);
    stats_->setTextFormat(Qt::RichText);
    form->addRow(stats_);

    auto* tabs = new QTabWidget(split);
    prev_ = new PlanPreview(tabs);
    prev_->setDark(dark);
    view_ = new Pipe3DView(tabs);
    view_->setDark(dark);
    tabs->addTab(view_, "3D");
    tabs->addTab(prev_, t("gui.gen.city.plan"));
    split->setStretchFactor(2, 1);
    split->setSizes({260, 330, 730});

    auto* bottom = new QHBoxLayout;
    bottom->addStretch(1);
    auto* draw = new QPushButton(t("gui.gen.draw"), this);
    draw->setDefault(true);
    auto* close = new QPushButton(QString::fromUtf8("✕"), this);
    bottom->addWidget(draw);
    bottom->addWidget(close);
    root->addLayout(bottom);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(draw, &QPushButton::clicked, this, [this] { emit drawRequested(writeTemp(civil::cityToJson(spec()), "asiacad_city.json")); });
    recompute();
}

civil::CitySpec CityDialog::spec() const {
    civil::CitySpec c;
    c.nx = nx_->value(); c.ny = ny_->value();
    c.blockW = blockW_->value(); c.blockD = blockD_->value();
    c.street = street_->value(); c.carriage = carriage_->value(); c.setback = setback_->value();
    c.bldW = bldW_->value(); c.bldD = bldD_->value(); c.gap = gap_->value();
    c.floorsMin = fmin_->value(); c.floorsMax = fmax_->value(); c.floorH = floorH_->value();
    c.x0 = x0_->value(); c.y0 = y0_->value(); c.ground = ground_->value();
    c.seed = static_cast<unsigned>(seed_->value());
    return c;
}

void CityDialog::setSpec(const civil::CitySpec& c) {
    updating_ = true;
    nx_->setValue(c.nx); ny_->setValue(c.ny);
    blockW_->setValue(c.blockW); blockD_->setValue(c.blockD);
    street_->setValue(c.street); carriage_->setValue(c.carriage); setback_->setValue(c.setback);
    bldW_->setValue(c.bldW); bldD_->setValue(c.bldD); gap_->setValue(c.gap);
    fmin_->setValue(c.floorsMin); fmax_->setValue(c.floorsMax); floorH_->setValue(c.floorH);
    x0_->setValue(c.x0); y0_->setValue(c.y0); ground_->setValue(c.ground);
    seed_->setValue(static_cast<int>(c.seed));
    updating_ = false;
    recompute();
}

void CityDialog::recompute() {
    const auto c = spec();
    int gp = 0;
    const auto gf = civil::groundFromDoc(doc_, &gp);
    try {
        r_ = civil::generateCity(c, gf);
    } catch (const Error& e) {
        r_ = {};
        stats_->setText("<span style='color:#e04850'>" + QString::fromStdString(loc_.tr(e.key(), e.args())).toHtmlEscaped() + "</span>");
        prev_->setEntities({});
        view_->beginScene(t("gui.gen.city.title"), "");
        view_->endScene();
        return;
    }
    QString s = t("gui.gen.city.stats", {QString::number(c.nx * c.ny), QString::number(r_.buildings.size()), QString::number(r_.siteArea / 10000, 'f', 1),
                                          QString::number(r_.footprint, 'f', 0), QString::number(r_.gfa, 'f', 0), f2(r_.far()),
                                          f2(r_.streetLength / 1000), QString::number(r_.blockArea > 0 ? 100 * r_.footprint / r_.blockArea : 0, 'f', 1)});
    if (r_.buildings.empty()) s += "<br><span style='color:#e6a01e'>⚠ " + t("gui.gen.city.nobld").toHtmlEscaped() + "</span>";
    if (!gp) s += "<br><span style='color:#e6a01e'>⚠ " + t("gen.warn.city_ground", {f2(c.ground)}).toHtmlEscaped() + "</span>";
    else s += "<br>" + t("gui.gen.city.groundpts", {QString::number(gp)}).toHtmlEscaped();
    // insolyatsiya/yong'in: binolar orasi balandlikdan kichik bo'lsa ogohlantirish (taxminiy qoida, tasdiqlanmagan)
    if (c.gap > 0 && c.gap < c.floorsMax * c.floorH && c.bldD + c.gap <= c.blockD - 2 * c.setback)
        s += "<br><span style='color:#e6a01e'>⚠ " + t("gui.gen.city.gapwarn", {f2(c.gap), f2(c.floorsMax * c.floorH)}).toHtmlEscaped() + "</span>";
    stats_->setText(s);
    // plan: 3D qatlamsiz
    civil::Entities plan;
    for (auto& e : r_.entities)
        if (e->layer() != "SHAHAR_3D") plan.push_back(e->clone());
    prev_->setEntities(std::move(plan));
    // 3D massiv: qavatlar soniga ko'ra rang
    view_->beginScene(t("gui.gen.city.title"), t("gui.gen.city.sub3d", {QString::number(r_.buildings.size())}));
    const int fmax = std::max(1, c.floorsMax);
    const double W = c.nx * (c.blockW + c.street) + c.street, D = c.ny * (c.blockD + c.street) + c.street;
    double zg = 1e18;
    for (const auto& b : r_.buildings) zg = std::min(zg, b.base);
    if (r_.buildings.empty()) zg = c.ground;
    view_->addFace({{c.x0, c.y0, zg - 0.05}, {c.x0 + W, c.y0, zg - 0.05}, {c.x0 + W, c.y0 + D, zg - 0.05}, {c.x0, c.y0 + D, zg - 0.05}},
                   QColor(110, 110, 115), 255);
    for (int i = 0; i < c.nx; ++i)
        for (int j = 0; j < c.ny; ++j) {
            const double bx = c.x0 + c.street + i * (c.blockW + c.street), by = c.y0 + c.street + j * (c.blockD + c.street);
            view_->addFace({{bx, by, zg}, {bx + c.blockW, by, zg}, {bx + c.blockW, by + c.blockD, zg}, {bx, by + c.blockD, zg}}, QColor(96, 150, 92), 255);
        }
    for (const auto& b : r_.buildings) {
        std::vector<P3> o;
        for (const auto& p : b.footprint) o.push_back({p[0], p[1], 0});
        const double k = static_cast<double>(b.floors) / fmax;
        const QColor col = QColor::fromHsvF(0.58 - 0.5 * k, 0.35 + 0.3 * k, 0.92);
        view_->addPrism(o, {}, b.base, b.base + b.height, col);
    }
    view_->endScene(true);
}

} // namespace asiacad::gui
