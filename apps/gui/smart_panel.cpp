#include "smart_panel.h"

#include <asiacad/bim/building.h>
#include <asiacad/civil/pipe_catalog.h>
#include <asiacad/core/entities.h>
#include <asiacad/civil/road.h>
#include <asiacad/civil/sewer.h>
#include <asiacad/civil/utility.h>
#include <asiacad/civil/waterway.h>
#include <asiacad/core/props.h>
#include <asiacad/i18n/json.h>

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <set>

namespace asiacad::gui {

namespace {
QString fmt(double v, int dec) {
    if (std::abs(v) < 0.5 * std::pow(10.0, -dec)) v = 0;
    return QString::number(v, 'f', dec);
}
}  // namespace

SmartPanel::SmartPanel(const Document& doc, const i18n::Localizer& loc, QWidget* parent) : QWidget(parent), doc_(doc), loc_(loc) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
}

QString SmartPanel::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

QLineEdit* SmartPanel::field(const QString& key) const { return body_ ? body_->findChild<QLineEdit*>("f_" + key) : nullptr; }

void SmartPanel::addSection(const QString& title) {
    auto* l = new QLabel(title, body_);
    l->setObjectName(lay_->count() == 0 ? "smartTitle" : "smartSection");
    l->setStyleSheet(QStringLiteral("font-weight:700; padding-top:6px;"));
    l->setWordWrap(true);
    if (title_.isEmpty()) title_ = title;
    lay_->addWidget(l);
    form_ = new QFormLayout;
    form_->setLabelAlignment(Qt::AlignLeft);
    form_->setHorizontalSpacing(8);
    form_->setVerticalSpacing(3);
    lay_->addLayout(form_);
}

QLineEdit* SmartPanel::addField(const QString& key, const QString& label, const QString& value, bool editable,
                                std::function<void(const QString&)> onEdit) {
    auto* e = new QLineEdit(value, body_);
    e->setObjectName("f_" + key);
    e->setReadOnly(!editable);
    if (!editable) e->setStyleSheet(QStringLiteral("QLineEdit{background:transparent;border:none;}"));
    e->setMinimumWidth(90);
    auto* l = new QLabel(label, body_);
    l->setObjectName("muted");
    l->setWordWrap(true);
    form_->addRow(l, e);
    if (editable && onEdit) {
        const QString orig = value;
        connect(e, &QLineEdit::editingFinished, this, [e, orig, onEdit] {
            const QString v = e->text().trimmed();
            if (v == orig) return;
            // panel qayta quriladi — signal manbasi o'chmasligi uchun keyingi aylanishda
            QTimer::singleShot(0, e->parent(), [onEdit, v] { onEdit(v); });
        });
    }
    return e;
}

void SmartPanel::addInfo(const QString& label, const QString& value, const QColor& color) {
    auto* l = new QLabel(label, body_);
    l->setObjectName("muted");
    l->setWordWrap(true);
    auto* v = new QLabel(value, body_);
    v->setWordWrap(true);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (color.isValid()) v->setStyleSheet(QStringLiteral("color:%1;").arg(color.name()));
    form_->addRow(l, v);
}

void SmartPanel::rebuild(const std::vector<EntityId>& sel) {
    if (body_) {
        body_->hide();
        body_->deleteLater();
    }
    title_.clear();
    body_ = new QWidget(this);
    lay_ = new QVBoxLayout(body_);
    lay_->setContentsMargins(0, 0, 0, 0);
    lay_->setSpacing(2);
    layout()->addWidget(body_);
    if (!sel.empty()) {
        bool done = false;
        if (const IEntity* e = doc_.find(sel.front()); e && !e->xget("sewer").empty()) {
            // bir nechta obyekt tanlansa ham — hammasi bitta quvur/quduqqa tegishli bo'lsa
            bool same = true;
            for (EntityId id : sel)
                if (const IEntity* o = doc_.find(id); !o || o->xget("sewer") != e->xget("sewer") || o->xget("seg") != e->xget("seg") ||
                                                      o->xget("mh") != e->xget("mh"))
                    same = false;
            if (same) done = buildSewer(sel.front());
        }
        if (!done && sel.size() == 1) {
            if (const IEntity* e = doc_.find(sel.front())) {
                if (!e->xget("util").empty()) done = buildUtil(sel.front());
                else if (!e->xget("road").empty()) done = buildRoad(sel.front());
                else if (!e->xget("ww").empty()) done = buildWw(sel.front());
            }
        }
        if (!done && sel.size() == 1) buildBim(sel.front());
        if (!done) buildGeneric(sel);
        if (sel.size() == 1) buildLink(sel.front());
    }
    lay_->addStretch(1);
    body_->show();
}

bool SmartPanel::buildSewer(EntityId id) {
    const IEntity* e = doc_.find(id);
    const QString net = QString::fromStdString(e->xget("sewer"));
    const std::string js = doc_.dictGet("sewer:" + net.toStdString());
    if (js.empty()) return false;
    civil::SewerNetwork n;
    try { n = civil::fromJson(js); } catch (...) { return false; }
    const auto r = civil::compute(n);
    const QString segS = QString::fromStdString(e->xget("seg")), mhS = QString::fromStdString(e->xget("mh"));
    const auto issueColor = [](civil::Issue::Level l) { return l == civil::Issue::Error ? QColor(232, 80, 80) : l == civil::Issue::Warning ? QColor(230, 160, 40) : QColor(110, 160, 220); };
    auto nav = [&](const QString& prevTag, const QString& nextTag, const char* prevKey, const char* nextKey) {
        auto* h = new QHBoxLayout;
        for (const auto& [tag, key] : {std::pair{prevTag, prevKey}, std::pair{nextTag, nextKey}}) {
            if (tag.isEmpty()) continue;
            auto* b = new QPushButton(t(key), body_);
            b->setObjectName(QString::fromLatin1(key).section('.', -1));
            connect(b, &QPushButton::clicked, this, [this, tag] { emit select(tag); });
            h->addWidget(b);
        }
        lay_->addLayout(h);
    };
    const QString base = QStringLiteral("PIPEEDIT \"%1\" ").arg(net);
    if (!segS.isEmpty()) {
        const int i = segS.toInt();
        if (i < 0 || i >= static_cast<int>(r.segments.size()) || !r.segments[static_cast<std::size_t>(i)].spec) return false;
        const auto& s = r.segments[static_cast<std::size_t>(i)];
        const auto& sg = n.segments[static_cast<std::size_t>(i)];
        const QString a = QString::fromStdString(n.manholes[static_cast<std::size_t>(i)].name), b = QString::fromStdString(n.manholes[static_cast<std::size_t>(i) + 1].name);
        const QString tag = QStringLiteral("sewer:%1:seg:%2").arg(net).arg(i);
        const QString cmd = base + QStringLiteral("SEG=%1 ").arg(i + 1);
        auto run = [this, cmd, tag](const QString& kv) { emit edit(cmd + kv, tag); };
        addSection(t("gui.pp.pipe", {net, a, b}));
        // material / diametr
        auto* spec = new QComboBox(body_);
        spec->setObjectName("f_SPEC");
        spec->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        spec->setMinimumContentsLength(9);
        for (const auto& p : civil::PipeCatalog::builtin().specs()) spec->addItem(QString::fromStdString(p.label()), QString::fromStdString(p.id));
        spec->setCurrentIndex(spec->findData(QString::fromStdString(sg.specId)));
        connect(spec, &QComboBox::activated, this, [this, spec, run](int k) {
            const QString id = spec->itemData(k).toString();
            QTimer::singleShot(0, this, [run, id] { run("SPEC=" + id); });
        });
        auto* sl = new QLabel(t("gui.pp.spec"), body_);
        sl->setObjectName("muted");
        form_->addRow(sl, spec);
        addField("SLOPE", t("gui.pp.slope"), fmt(s.slope * 1000, 2), !n.pressure, [run](const QString& v) { run("SLOPE=" + v); });
        addField("INVUP", t("gui.pp.invup"), fmt(s.invUp, 3), true, [run](const QString& v) { run("INVUP=" + v); });
        addField("INVDOWN", t("gui.pp.invdown"), fmt(s.invDown, 3), !n.pressure, [run](const QString& v) { run("INVDOWN=" + v); });
        addField("INSUP", t("gui.pp.insup", {a}), fmt(sg.insUp * 100, 1), true, [run](const QString& v) { run("INSUP=" + v); });
        addField("INSDOWN", t("gui.pp.insdown", {b}), fmt(sg.insDown * 100, 1), true, [run](const QString& v) { run("INSDOWN=" + v); });
        addField("FLOW", t("gui.pp.flow"), fmt(s.flowLs, 2), !(n.system == "K2" && n.storm.enabled), [run](const QString& v) { run("FLOW=" + v); });
        addSection(t("gui.pp.calc"));
        const double od = s.spec->od / 1000.0;
        addInfo(t("gui.pp.len"), fmt(s.length, 2) + " m");
        addInfo(t("gui.pp.pipelen"), fmt(s.pipeLength, 2) + " m");
        addInfo(t("gui.pp.top"), fmt(s.invUp + od, 3) + " → " + fmt(s.invDown + od, 3));
        addInfo(t("gui.pp.depth"), fmt(s.depthUp, 2) + " → " + fmt(s.depthDown, 2) + " m");
        addInfo(t("gui.pp.cover"), fmt(s.coverMin, 2) + " m", s.coverMin < n.frostDepth ? QColor(230, 160, 40) : QColor());
        addInfo(t("gui.pp.qfull"), fmt(s.qFull, 1) + " l/s · " + fmt(s.vFull, 2) + " m/s");
        if (s.flowLs > 0) addInfo(t("gui.pp.fill"), fmt(s.fill, 2) + " · " + fmt(s.velocity, 2) + " m/s", s.fill > s.maxFill ? QColor(232, 80, 80) : QColor());
        addInfo(t("gui.pp.minslope"), fmt(s.minSlope * 1000, 1) + " ‰", s.slope < s.minSlope - 1e-12 ? QColor(232, 80, 80) : QColor());
        addInfo(t("gui.pp.trench"), fmt(s.trenchW, 2) + " m · " + fmt(s.trench, 1) + " m³");
        addInfo(t("gui.pp.bedback"), fmt(s.bedding, 2) + " / " + fmt(s.backfill, 1) + " m³");
        for (const auto& is : r.issues)
            if (is.segment == i) addInfo(QStringLiteral("⚠"), QString::fromStdString(loc_.tr(is.key, is.args)), issueColor(is.level));
        nav(QStringLiteral("sewer:%1:mh:%2").arg(net).arg(i), QStringLiteral("sewer:%1:mh:%2").arg(net).arg(i + 1), "gui.pp.mhup", "gui.pp.mhdown");
        auto* xl = new QPushButton(t("gui.pp.xlsx", {net}), body_);
        xl->setObjectName("netXlsx");
        connect(xl, &QPushButton::clicked, this, [this, net] {
            const QString f = QFileDialog::getSaveFileName(this, t("gui.pp.xlsx", {net}), net + QStringLiteral("_smeta.xlsx"), QStringLiteral("Excel (*.xlsx)"));
            if (!f.isEmpty()) emit edit(QStringLiteral("PIPEEDIT \"%1\" \"XLSX=%2\"").arg(net, f), QString());
        });
        lay_->addWidget(xl);
        nav(i > 0 ? QStringLiteral("sewer:%1:seg:%2").arg(net).arg(i - 1) : QString(),
            i + 1 < static_cast<int>(r.segments.size()) ? QStringLiteral("sewer:%1:seg:%2").arg(net).arg(i + 1) : QString(), "gui.pp.prev", "gui.pp.next");
        return true;
    }
    if (mhS.isEmpty()) return false;
    const int k = mhS.toInt();
    if (k < 0 || k >= static_cast<int>(n.manholes.size()) || k >= static_cast<int>(r.manholes.size())) return false;
    const auto& m = n.manholes[static_cast<std::size_t>(k)];
    const auto& mr = r.manholes[static_cast<std::size_t>(k)];
    const QString tag = QStringLiteral("sewer:%1:mh:%2").arg(net).arg(k);
    const QString cmd = base + QStringLiteral("MH=%1 ").arg(k + 1);
    auto run = [this, cmd, tag](const QString& kv) { emit edit(cmd + kv, tag); };
    addSection(t("gui.pp.mh", {QString::fromStdString(m.name), net}));
    addField("NAME", t("gui.pp.name"), QString::fromStdString(m.name), true, [run](const QString& v) { run("\"NAME=" + v + "\""); });
    addField("X", QStringLiteral("X"), fmt(m.x, 3), true, [run](const QString& v) { run("X=" + v); });
    addField("Y", QStringLiteral("Y"), fmt(m.y, 3), true, [run](const QString& v) { run("Y=" + v); });
    addField("RIM", t("gui.pp.rim"), fmt(m.rim, 3), true, [run](const QString& v) { run("RIM=" + v); });
    addField("INV", t("gui.pp.inv"), m.invert ? fmt(*m.invert, 3) : QString(), true, [run](const QString& v) { run("INV=" + (v.isEmpty() ? QStringLiteral("AUTO") : v)); });
    addField("D", t("gui.pp.d"), fmt(m.diameter, 2), true, [run](const QString& v) { run("D=" + v); });
    addSection(t("gui.pp.calc"));
    addInfo(t("gui.pp.invin"), k > 0 ? fmt(mr.invIn, 3) : QStringLiteral("—"));
    addInfo(t("gui.pp.invout"), k + 1 < static_cast<int>(n.manholes.size()) ? fmt(mr.invOut, 3) : QStringLiteral("—"));
    if (k > 0 && k + 1 < static_cast<int>(n.manholes.size())) addInfo(t("gui.pp.drop"), fmt((mr.invIn - mr.invOut) * 100, 1) + " sm");
    addInfo(t("gui.pp.mhdepth"), fmt(mr.depth, 2) + " m");
    addInfo(t("gui.pp.dused"), fmt(mr.diameter, 2) + " m (" + t("gui.pp.dreq") + " " + fmt(mr.required, 2) + ")", mr.diameter + 1e-9 < mr.required ? QColor(232, 80, 80) : QColor());
    for (const auto& is : r.issues)
        if (is.manhole == k) addInfo(QStringLiteral("⚠"), QString::fromStdString(loc_.tr(is.key, is.args)), issueColor(is.level));
    nav(k > 0 ? QStringLiteral("sewer:%1:seg:%2").arg(net).arg(k - 1) : QString(),
        k + 1 < static_cast<int>(n.manholes.size()) ? QStringLiteral("sewer:%1:seg:%2").arg(net).arg(k) : QString(), "gui.pp.pipein", "gui.pp.pipeout");
    return true;
}

void SmartPanel::navRow(const QString& prevTag, const QString& nextTag, const char* prevKey, const char* nextKey) {
    auto* h = new QHBoxLayout;
    for (const auto& [tag, key] : {std::pair{prevTag, prevKey}, std::pair{nextTag, nextKey}}) {
        if (tag.isEmpty()) continue;
        auto* b = new QPushButton(t(key), body_);
        b->setObjectName(QString::fromLatin1(key).section('.', -1));
        connect(b, &QPushButton::clicked, this, [this, tag] { emit select(tag); });
        h->addWidget(b);
    }
    lay_->addLayout(h);
}

bool SmartPanel::buildUtil(EntityId id) {
    const IEntity* e = doc_.find(id);
    const std::string js = doc_.dictGet("util:main");
    if (js.empty()) return false;
    civil::UtilModel m;
    auto p = civil::defaultUtilParams();
    try { civil::utilFromJson(js, m, p); } catch (...) { return false; }
    civil::checkUtilities(m, p);
    const QString lineS = QString::fromStdString(e->xget("line")), nodeS = QString::fromStdString(e->xget("node"));
    const QColor red(232, 80, 80), amber(230, 160, 40);
    if (!lineS.isEmpty()) {
        const int i = lineS.toInt();
        if (i < 0 || i >= static_cast<int>(m.lines.size())) return false;
        const auto& ln = m.lines[static_cast<std::size_t>(i)];
        const QString kind = QString::fromStdString(loc_.tr(std::string("util.kind.") + civil::utilKey(ln.kind)));
        const QString cmd = QStringLiteral("OBJEDIT UTIL LINE=%1 ").arg(i + 1), tag = QStringLiteral("util:main:line:%1").arg(i);
        auto run = [this, cmd, tag](const QString& kv) { emit edit(cmd + kv, tag); };
        addSection(t("gui.pp.uline", {kind, QString::number(i + 1)}));
        addField("DN", t("gui.pp.dn"), fmt(ln.dn, 0), true, [run](const QString& v) { run("DN=" + v); });
        addField("ZUP", t("gui.pp.zup"), fmt(ln.v.front().z, 3), true, [run](const QString& v) { run("ZUP=" + v); });
        addField("ZDOWN", t("gui.pp.zdown"), fmt(ln.v.back().z, 3), true, [run](const QString& v) { run("ZDOWN=" + v); });
        double cover = 1e9, l2 = 0;
        for (std::size_t k = 0; k < ln.v.size(); ++k) {
            cover = std::min(cover, ln.v[k].ground - ln.v[k].z - ln.dn / 2000.0);
            if (k) l2 += std::hypot(ln.v[k].x - ln.v[k - 1].x, ln.v[k].y - ln.v[k - 1].y);
        }
        addField("DEPTH", t("gui.pp.ucover"), fmt(cover, 2), true, [run](const QString& v) { run("DEPTH=" + v); });
        addSection(t("gui.pp.calc"));
        addInfo(t("gui.pp.ukind"), kind + " · " + QString::fromStdString(ln.layer));
        addInfo(t("gui.pp.len"), fmt(l2, 2) + " m (3D " + fmt(ln.length(), 2) + ")");
        addInfo(t("gui.pp.uground"), fmt(ln.v.front().ground, 2) + " → " + fmt(ln.v.back().ground, 2));
        const double minCover = p.minCover.count(ln.kind) ? p.minCover.at(ln.kind) : 0;
        addInfo(t("gui.pp.cover"), fmt(cover, 2) + " m (≥ " + fmt(minCover, 2) + ")", cover + 1e-9 < minCover ? red : QColor());
        if (l2 > 0) addInfo(t("gui.pp.slopepm"), fmt((ln.v.front().z - ln.v.back().z) / l2 * 1000, 2) + " ‰",
                            civil::isGravity(ln.kind) && ln.v.back().z > ln.v.front().z ? red : QColor());
        for (const auto& c : m.crossings) {
            if (c.a != i && c.b != i) continue;
            const auto& o = m.lines[static_cast<std::size_t>(c.a == i ? c.b : c.a)];
            addInfo(t("gui.pp.ucross", {QString::fromStdString(loc_.tr(std::string("util.kind.") + civil::utilKey(o.kind)))}),
                    fmt(c.clear, 2) + " m (≥ " + fmt(c.required, 2) + ")", c.clear + 1e-9 < c.required ? red : QColor());
        }
        for (const auto& is : m.issues)
            if (is.line == i) addInfo(QStringLiteral("⚠"), QString::fromStdString(loc_.tr(is.key, is.args)), is.error ? red : amber);
        navRow(i > 0 ? QStringLiteral("util:main:line:%1").arg(i - 1) : QString(),
               i + 1 < static_cast<int>(m.lines.size()) ? QStringLiteral("util:main:line:%1").arg(i + 1) : QString(), "gui.pp.prevobj", "gui.pp.nextobj");
        return true;
    }
    if (nodeS.isEmpty()) return false;
    const int k = nodeS.toInt();
    if (k < 0 || k >= static_cast<int>(m.nodes.size())) return false;
    const auto& n = m.nodes[static_cast<std::size_t>(k)];
    const QString cmd = QStringLiteral("OBJEDIT UTIL NODE=%1 ").arg(k + 1), tag = QStringLiteral("util:main:node:%1").arg(k);
    auto run = [this, cmd, tag](const QString& kv) { emit edit(cmd + kv, tag); };
    addSection(t("gui.pp.unode", {n.name.empty() ? QString::number(k + 1) : QString::fromStdString(n.name)}));
    addField("NAME", t("gui.pp.name"), QString::fromStdString(n.name), true, [run](const QString& v) { run("\"NAME=" + v + "\""); });
    addField("GROUND", t("gui.pp.rim"), fmt(n.ground, 3), true, [run](const QString& v) { run("GROUND=" + v); });
    addField("BOTTOM", t("gui.pp.ubottom"), fmt(n.bottom, 3), true, [run](const QString& v) { run("BOTTOM=" + v); });
    addSection(t("gui.pp.calc"));
    addInfo(t("gui.pp.mhdepth"), fmt(n.ground - n.bottom, 2) + " m");
    if (n.estimated) addInfo(QStringLiteral("⚠"), t("gui.pp.uestimated"), amber);
    QStringList inv;
    for (double v : n.inverts) inv << fmt(v, 3);
    if (!inv.isEmpty()) addInfo(t("gui.pp.uinverts"), inv.join(", "));
    navRow(k > 0 ? QStringLiteral("util:main:node:%1").arg(k - 1) : QString(),
           k + 1 < static_cast<int>(m.nodes.size()) ? QStringLiteral("util:main:node:%1").arg(k + 1) : QString(), "gui.pp.prevobj", "gui.pp.nextobj");
    return true;
}

bool SmartPanel::buildRoad(EntityId id) {
    const IEntity* e = doc_.find(id);
    const std::string js = doc_.dictGet("road:main");
    if (js.empty() || e->xget("st").empty()) return false;
    civil::RoadModel m;
    auto p = civil::defaultRoadParams();
    try { civil::roadFromJson(js, m, p); } catch (...) { return false; }
    const int i = QString::fromStdString(e->xget("st")).toInt();
    if (i < 0 || i >= static_cast<int>(m.st.size())) return false;
    const auto& s = m.st[static_cast<std::size_t>(i)];
    const QString cmd = QStringLiteral("OBJEDIT ROAD ST=%1 ").arg(i + 1), tag = QStringLiteral("road:main:st:%1").arg(i);
    auto run = [this, cmd, tag](const QString& kv) { emit edit(cmd + kv, tag); };
    addSection(t("gui.pp.rst", {QString::fromStdString(civil::pk(s.s))}));
    addField("DESIGN", t("gui.pp.design"), fmt(s.design, 3), true, [run](const QString& v) { run("DESIGN=" + v); });
    addSection(t("gui.pp.calc"));
    addInfo(t("gui.pp.ground"), fmt(s.ground, 3));
    const double w = s.design - s.ground;
    addInfo(t("gui.pp.work"), (w >= 0 ? "+" : "") + fmt(w, 2) + " m", w >= 0 ? QColor(220, 90, 70) : QColor(70, 140, 220));
    addInfo(t("gui.pp.grade"), fmt(s.grade * 1000, 1) + " ‰", std::abs(s.grade) > p.cat.maxGrade + 1e-9 ? QColor(232, 80, 80) : QColor());
    addInfo(t("gui.pp.cutfill"), fmt(s.cutArea, 2) + " / " + fmt(s.fillArea, 2) + " m²");
    addInfo(t("gui.pp.control"), QString::fromStdString(loc_.keywordName(s.control ? "ON" : "OFF")));
    for (const auto& is : m.issues)
        if (std::abs(is.s - s.s) < 1e-6) addInfo(QStringLiteral("⚠"), QString::fromStdString(loc_.tr(is.key, is.args)), is.error ? QColor(232, 80, 80) : QColor(230, 160, 40));
    navRow(i > 0 ? QStringLiteral("road:main:st:%1").arg(i - 1) : QString(),
           i + 1 < static_cast<int>(m.st.size()) ? QStringLiteral("road:main:st:%1").arg(i + 1) : QString(), "gui.pp.prevpk", "gui.pp.nextpk");
    return true;
}

bool SmartPanel::buildWw(EntityId id) {
    const IEntity* e = doc_.find(id);
    const std::string js = doc_.dictGet("ww:main");
    if (js.empty() || e->xget("st").empty()) return false;
    civil::WwModel m;
    auto p = civil::defaultWwParams();
    try { civil::wwFromJson(js, m, p); } catch (...) { return false; }
    const int i = QString::fromStdString(e->xget("st")).toInt();
    if (i < 0 || i >= static_cast<int>(m.st.size())) return false;
    const auto& s = m.st[static_cast<std::size_t>(i)];
    const QString cmd = QStringLiteral("OBJEDIT WW ST=%1 ").arg(i + 1), tag = QStringLiteral("ww:main:st:%1").arg(i);
    auto run = [this, cmd, tag](const QString& kv) { emit edit(cmd + kv, tag); };
    addSection(t("gui.pp.wst", {QString::fromStdString(civil::pk(s.s)), QString::fromStdString(p.sec.name)}));
    addField("BOTTOM", t("gui.pp.wbottom"), fmt(s.bottom, 3), true, [run](const QString& v) { run("BOTTOM=" + v); });
    addSection(t("gui.pp.calc"));
    addInfo(t("gui.pp.ground"), fmt(s.ground, 3));
    addInfo(t("gui.pp.wdepth"), fmt(s.ground - s.bottom, 2) + " m");
    addInfo(t("gui.pp.slopepm"), fmt(s.slope * 1000, 2) + " ‰", s.slope <= 0 && !civil::wwOpen(p.sec.type) ? QColor(232, 80, 80) : QColor());
    if (s.depthN > 0) addInfo(t("gui.pp.wnormal"), fmt(s.depthN, 3) + " m");
    addInfo(t("gui.pp.wwater"), fmt(s.water, 3));
    addInfo(t("gui.pp.wvel"), fmt(s.v, 2) + " m/s · Fr " + fmt(s.fr, 2),
            (s.v > 0 && (s.v < p.sec.vmin || s.v > p.sec.vmax)) ? QColor(230, 160, 40) : QColor());
    addInfo(t("gui.pp.cutfill"), fmt(s.cut, 2) + " / " + fmt(s.fill, 2) + " m²");
    for (const auto& is : m.issues)
        if (std::abs(is.s - s.s) < 1e-6) addInfo(QStringLiteral("⚠"), QString::fromStdString(loc_.tr(is.key, is.args)), is.error ? QColor(232, 80, 80) : QColor(230, 160, 40));
    navRow(i > 0 ? QStringLiteral("ww:main:st:%1").arg(i - 1) : QString(),
           i + 1 < static_cast<int>(m.st.size()) ? QStringLiteral("ww:main:st:%1").arg(i + 1) : QString(), "gui.pp.prevpk", "gui.pp.nextpk");
    return true;
}

bool SmartPanel::buildBim(EntityId id) {
    const IEntity* e = doc_.find(id);
    if (!e || !e->xdata().empty()) return false;
    bim::BuildingParams p;
    std::map<std::string, bim::ElemKind> lm;
    if (const std::string js = doc_.dictGet("bim:params"); !js.empty()) {
        try { bim::paramsFromJson(js, p, lm); } catch (...) {}
    }
    const auto it = lm.find(e->layer());
    const bim::ElemKind k = it != lm.end() ? it->second : bim::guessKind(e->layer());
    using K = bim::ElemKind;
    if (k == K::Ignore || k == K::Opening || k == K::Outline) return false;
    // kontur yoki markaz chizig'i (devor)
    double A = 0, P = 0, L = 0;
    if (const auto* pl = dynamic_cast<const PolylineEntity*>(e); pl && pl->isClosed()) { A = pl->area(); P = pl->length2d(); }
    else if (const auto* c = dynamic_cast<const ICurve*>(e)) L = c->length();
    else return false;
    double h = 0, thick = 0;
    int times = 1;
    std::string cls;
    const double hFloor = p.floorHeight - p.slabT;
    switch (k) {
    case K::Column: case K::Wall: h = hFloor; times = p.floors; cls = p.classFrame; thick = p.wallT; break;
    case K::Infill: h = hFloor; times = p.floors; cls = p.classInfill; thick = p.infillT; break;
    case K::Slab: h = p.slabT; times = std::max(1, p.floors - 1); cls = p.classSlab; break;
    case K::Roof: h = p.roofT; cls = p.classSlab; break;
    case K::FoundationSlab: case K::StripFooting: h = p.foundT; cls = p.classFound; thick = p.stripWidth; break;
    case K::Lean: h = p.leanT; cls = p.classLean; break;
    case K::Stair: case K::Shaft: h = hFloor; times = p.floors; cls = p.classFrame; thick = p.shaftWallT; break;
    default: return false;
    }
    if (L > 0) { A = L * thick; P = 2 * (L + thick); }  // chiziq: devor qalinligi bilan
    const QString kind = QString::fromStdString(loc_.tr(std::string("bim.kind.") + bim::kindKey(k)));
    addSection(t("gui.pp.bim", {kind}));
    if (L > 0) addInfo(t("gui.pp.bimline"), fmt(L, 2) + " × " + fmt(thick, 2) + " m");
    addInfo(t("gui.pp.bimarea"), fmt(A, 3) + " m² · P " + fmt(P, 2) + " m");
    addInfo(t("gui.pp.bimh"), fmt(h, 2) + " m" + (times > 1 ? QStringLiteral(" × %1").arg(times) : QString()));
    const double v1 = A * h;
    const double form1 = (k == K::Column || k == K::Wall || k == K::Stair || k == K::Shaft) ? P * h : (k == K::Infill || k == K::Lean) ? 0.0 : A;
    addInfo(t("gui.pp.bimvol"), fmt(v1, 3) + " m³" + (times > 1 ? " → " + fmt(v1 * times, 2) + " m³" : QString()));
    if (form1 > 0) addInfo(t("gui.pp.bimform"), fmt(form1, 2) + " m²" + (times > 1 ? " → " + fmt(form1 * times, 2) + " m²" : QString()));
    addInfo(t("gui.pp.bimclass"), QString::fromStdString(cls));
    if (doc_.dictGet("bim:params").empty()) addInfo(QStringLiteral("ℹ"), t("gui.pp.bimdefault"), QColor(110, 160, 220));
    return true;
}

void SmartPanel::buildLink(EntityId id) {
    for (const auto& l : doc_.links()) {
        if (std::find(l.outputs.begin(), l.outputs.end(), id) == l.outputs.end()) continue;
        addSection(t("gui.pp.link", {QString::number(l.id)}));
        const QString canon = QString::fromStdString(l.line).section(' ', 0, 0);
        addInfo(t("gui.pp.linkcmd"), QString::fromStdString(loc_.commandName(canon.toStdString())));
        addInfo(t("gui.pp.linkstate"), QString::fromStdString(loc_.keywordName(l.on ? "ON" : "OFF")), l.on ? QColor() : QColor(230, 160, 40));
        auto* b = new QPushButton(t("gui.pp.linkupd"), body_);
        b->setObjectName("linkUpdate");
        const int lid = l.id;
        connect(b, &QPushButton::clicked, this, [this, lid] { emit edit(QStringLiteral("LINKS UPDATE %1").arg(lid), QString()); });
        lay_->addWidget(b);
        return;
    }
}

void SmartPanel::buildGeneric(const std::vector<EntityId>& sel) {
    // bir turdagi obyektlar: umumiy maydonlar (qiymatlar farq qilsa — bo'sh), o'zgartirish hammasiga
    std::vector<const IEntity*> es;
    for (EntityId id : sel)
        if (const IEntity* e = doc_.find(id)) es.push_back(e);
    if (es.empty()) return;
    for (const IEntity* e : es)
        if (e->type() != es.front()->type()) return;
    const auto f0 = entityProps(*es.front());
    if (f0.empty()) return;
    addSection(t("gui.pp.geom", {QString::fromStdString(std::string(toString(es.front()->type()))), QString::number(es.size())}));
    QStringList ids;
    for (EntityId id : sel) ids << QString::number(id);
    const QString selArg = ids.join(',');
    for (std::size_t i = 0; i < f0.size(); ++i) {
        const auto& f = f0[i];
        bool same = true;
        for (const IEntity* e : es) {
            const auto fe = entityProps(*e);
            if (i >= fe.size() || fe[i].key != f.key || (f.isText ? fe[i].text != f.text : std::abs(fe[i].value - f.value) > 1e-9)) { same = false; break; }
        }
        const QString val = !same ? QString() : f.isText ? QString::fromStdString(f.text) : fmt(f.value, f.decimals);
        const QString lbl = QString::fromStdString(loc_.tr("prop." + f.key)) + (f.unit.empty() ? QString() : ", " + QString::fromStdString(f.unit));
        const QString key = QString::fromStdString(f.key);
        addField(key, lbl, val, f.editable, [this, selArg, key](const QString& v) {
            QString vv = v;
            vv.replace('"', '\'');
            emit edit(QStringLiteral("SETPROP %1 \"%2=%3\"").arg(selArg, key, vv), QStringLiteral("ids:") + selArg);
        });
    }
}

} // namespace asiacad::gui
