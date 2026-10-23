#include "smart_panel.h"

#include <asiacad/civil/pipe_catalog.h>
#include <asiacad/civil/sewer.h>
#include <asiacad/core/props.h>
#include <asiacad/i18n/json.h>

#include <QComboBox>
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
        for (const auto& is : r.issues)
            if (is.segment == i) addInfo(QStringLiteral("⚠"), QString::fromStdString(loc_.tr(is.key, is.args)), issueColor(is.level));
        nav(QStringLiteral("sewer:%1:mh:%2").arg(net).arg(i), QStringLiteral("sewer:%1:mh:%2").arg(net).arg(i + 1), "gui.pp.mhup", "gui.pp.mhdown");
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
