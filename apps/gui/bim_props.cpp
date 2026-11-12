#include "bim_props.h"

#include <asiacad/core/entities.h>
#include <asiacad/core/props.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace asiacad::gui {

namespace {
using bim::ElemKind;

// Takrorlanmas uchlar (yopuvchi nuqtasiz)
std::vector<Vec3> ringOf(const std::vector<Vec3>& v) {
    std::vector<Vec3> r;
    for (const auto& p : v)
        if (r.empty() || r.back().distanceTo(p) > 1e-9) r.push_back(p);
    if (r.size() > 2 && r.front().distanceTo(r.back()) < 1e-9) r.pop_back();
    return r;
}
// Kontur imzosi: tartibsiz uchlar to'plami (0.1 mm) — boshlanish nuqtasi va yo'nalishga bog'liq emas
std::string signature(const std::vector<Vec3>& v0) {
    const auto v = ringOf(v0);
    std::vector<std::pair<long long, long long>> k;
    for (const auto& p : v) k.push_back({std::llround(p.x * 1e4), std::llround(p.y * 1e4)});
    std::sort(k.begin(), k.end());
    std::string s;
    for (const auto& [x, y] : k) s += std::to_string(x) + "," + std::to_string(y) + ";";
    return s;
}
double ringArea(const std::vector<Vec3>& v) {
    double a = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const Vec3& p = v[i];
        const Vec3& q = v[(i + 1) % v.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return std::abs(a) / 2;
}
bool inRing(const std::vector<Vec3>& poly, double x, double y) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec3 &a = poly[i], &b = poly[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}
Vec3 centroidOf(const std::vector<Vec3>& v) {
    Vec3 c;
    for (const auto& p : v) c = c + p;
    return v.empty() ? c : c * (1.0 / static_cast<double>(v.size()));
}
// manba bilan bog'liq turlar: zina katagi -> zina pog'onalari, shaxta ichki konturi -> shaxta devori, to'siq devor -> bo'laklari
bool relatedKinds(ElemKind src, ElemKind pr) {
    if (src == pr) return true;
    return (src == ElemKind::Slab && pr == ElemKind::Roof) || (src == ElemKind::Roof && pr == ElemKind::Slab);
}
QString tr(const i18n::Localizer& loc, const char* key, const QStringList& args = {}) {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc.tr(key, a));
}
QString kindName(const i18n::Localizer& loc, ElemKind k) { return QString::fromStdString(loc.tr(std::string("bim.kind.") + bim::kindKey(k))); }
const char* defaultLayer(ElemKind k) {
    switch (k) {
        case ElemKind::Column: return "USTUN";
        case ElemKind::Wall: return "DEVOR";
        case ElemKind::Slab: return "PLITA";
        case ElemKind::Roof: return "TOM";
        case ElemKind::Opening: return "TESHIK";
        case ElemKind::Stair: return "ZINA";
        case ElemKind::Shaft: return "LIFT";
        case ElemKind::Infill: return "TOSIQ";
        case ElemKind::FoundationSlab: return "FUNDAMENT";
        case ElemKind::StripFooting: return "LENTA";
        case ElemKind::Lean: return "PADBETON";
        default: return "0";
    }
}
// Shu turga bog'langan qatlam (chizmada bor); bo'lmasa — yangi nom (qatlam yaratiladi)
QString layerFor(const Document& doc, const std::map<std::string, ElemKind>& lm, ElemKind k, bool* create) {
    for (const auto& [name, kk] : lm)
        if (kk == k && doc.layer(name).has_value()) {
            if (create) *create = false;
            return QString::fromStdString(name);
        }
    if (create) *create = !doc.layer(defaultLayer(k)).has_value();
    return QString::fromLatin1(defaultLayer(k));
}
}  // namespace

QString cmdNum(double v) {
    if (std::abs(v) < 5e-7) v = 0;
    QString s = QString::number(v, 'f', 4);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) s.chop(1);
    return s;
}
QString cmdPt(double x, double y) { return cmdNum(x) + "," + cmdNum(y); }

void BimSources::build(const Document& doc, const std::map<std::string, bim::ElemKind>& layerMap) {
    sig_.clear();
    rings_.clear();
    doc.forEach([&](const IEntity& e) {
        const auto* pl = dynamic_cast<const PolylineEntity*>(&e);
        if (!pl || !pl->isClosed()) return;
        const auto it = layerMap.find(e.layer());
        const ElemKind k = it != layerMap.end() ? it->second : bim::guessKind(e.layer());
        if (k == ElemKind::Ignore || k == ElemKind::Outline) return;
        Ring r;
        r.id = e.id();
        r.kind = k;
        r.pts = ringOf(pl->vertices());
        if (r.pts.size() < 3) return;
        r.area = ringArea(r.pts);
        sig_.emplace(signature(r.pts), r.id);
        rings_.push_back(std::move(r));
    });
}

EntityId BimSources::find(const bim::Prism& pr) const {
    if (const auto it = sig_.find(signature(pr.outline)); it != sig_.end()) return it->second;
    // shaxta: ichki konturi (teshik) — chizmadagi shaxta
    for (const auto& h : pr.holes)
        if (pr.kind == ElemKind::Shaft)
            if (const auto it = sig_.find(signature(h)); it != sig_.end()) return it->second;
    // zina pog'onalari, to'siq devor bo'laklari: markazi ichida bo'lgan eng kichik shu turdagi kontur
    if (pr.kind != ElemKind::Stair && pr.kind != ElemKind::Infill && pr.kind != ElemKind::Shaft) return 0;
    const Vec3 c = centroidOf(ringOf(pr.outline));
    EntityId best = 0;
    double ba = 1e300;
    for (const auto& r : rings_)
        if (relatedKinds(r.kind, pr.kind) && r.area < ba && inRing(r.pts, c.x, c.y)) {
            ba = r.area;
            best = r.id;
        }
    return best;
}

QString bimPrismKey(const bim::Prism& pr, EntityId src, int ordinal) {
    if (src) return QStringLiteral("e:%1:%2:%3").arg(src).arg(pr.level).arg(QString::fromLatin1(bim::kindKey(pr.kind)));
    return QStringLiteral("p:%1:%2:%3").arg(QString::fromLatin1(bim::kindKey(pr.kind))).arg(pr.level).arg(ordinal);
}

void fillBimElement(PropPanel& panel, const i18n::Localizer& loc, const Document& doc, const std::map<std::string, bim::ElemKind>& layerMap,
                    const bim::Prism& pr, EntityId src, const std::function<void(const QStringList&)>& run) {
    const QString kind = kindName(loc, pr.kind);
    const QString lvl = pr.level == 0 ? tr(loc, "gui.bim.found") : tr(loc, "gui.bim.level", {QString::number(pr.level)});
    panel.begin(kind + " · " + lvl);
    // ---- geometriya (modeldagi)
    panel.section(tr(loc, "gui.xp.geom"));
    const auto ring = ringOf(pr.outline);
    BBox b;
    for (const auto& p : ring) b.expand(p);
    panel.info(tr(loc, "gui.xp.levels"), QString::number(pr.z0, 'f', 3) + QString::fromUtf8(" … ") + QString::number(pr.z1, 'f', 3));
    panel.info(tr(loc, "gui.xp.height"), QString::number(pr.z1 - pr.z0, 'f', 3) + " m");
    if (b.valid()) panel.info(tr(loc, "gui.xp.extent"), QString::number(b.max.x - b.min.x, 'f', 3) + QString::fromUtf8(" × ") + QString::number(b.max.y - b.min.y, 'f', 3) + " m");
    panel.info(tr(loc, "gui.xp.area"), QString::number(pr.area(), 'f', 3) + QString::fromUtf8(" m²") +
                                          (pr.holes.empty() ? QString() : QStringLiteral(" (%1)").arg(tr(loc, "gui.xp.holes", {QString::number(pr.holes.size())}))));
    panel.info(tr(loc, "gui.xp.volume"), QString::number(pr.volume(), 'f', 3) + QString::fromUtf8(" m³"));
    if (pr.kind != ElemKind::Lean) panel.info(tr(loc, "gui.xp.form"), QString::number(pr.formwork(), 'f', 2) + QString::fromUtf8(" m²"));
    panel.info(tr(loc, "gui.xp.class"), QString::fromStdString(pr.concrete));

    const IEntity* e = src ? doc.find(src) : nullptr;
    if (!e) {
        panel.note(tr(loc, "gui.xp.nosrc"));
        return;
    }
    // ---- chizmadagi obyekt: tuzatish va qo'shimcha detallar (buyruqlar — undo bilan)
    const QString id = QString::number(src);
    panel.section(tr(loc, "gui.xp.src"));
    panel.info(tr(loc, "gui.xp.layer"), QString::fromStdString(e->layer()));
    for (const auto& f : entityProps(*e)) {   // to'rtburchak: kesim o'lchamlari (markaz va yo'nalish saqlanadi)
        if (f.key != "RECTW" && f.key != "RECTH") continue;
        const QString key = QString::fromStdString(f.key);
        panel.number(key.toLower(), tr(loc, f.key == "RECTW" ? "gui.xp.rectw" : "gui.xp.recth"), f.value, 3,
                     [run, id, key](double v) { if (v > 0.01) run({QStringLiteral("SETPROP %1 %2=%3").arg(id, key, cmdNum(v))}); }, "m");
    }
    {
        static const ElemKind kinds[] = {ElemKind::Column, ElemKind::Wall, ElemKind::Slab, ElemKind::Stair, ElemKind::Shaft, ElemKind::Infill,
                                         ElemKind::FoundationSlab, ElemKind::StripFooting, ElemKind::Lean};
        QStringList names;
        int cur = -1;
        const auto it = layerMap.find(e->layer());
        const ElemKind now = it != layerMap.end() ? it->second : bim::guessKind(e->layer());
        for (std::size_t i = 0; i < std::size(kinds); ++i) {
            names << kindName(loc, kinds[i]);
            if (kinds[i] == now || (now == ElemKind::Roof && kinds[i] == ElemKind::Slab)) cur = static_cast<int>(i);
        }
        if (cur < 0) {
            names.prepend(kindName(loc, now));
            cur = 0;
        }
        const bool shifted = names.size() > static_cast<int>(std::size(kinds));
        panel.choice("kind", tr(loc, "gui.xp.kind"), names, cur, [&doc, layerMap, run, id, shifted](int i) {
            const int k = shifted ? i - 1 : i;
            if (k < 0) return;
            bool create = false;
            const QString lay = layerFor(doc, layerMap, kinds[static_cast<std::size_t>(k)], &create);
            QStringList cmds;
            if (create) cmds << QStringLiteral("LAYER %1").arg(lay);
            cmds << QStringLiteral("CHPROP %1 LAYER=%2").arg(id, lay);
            run(cmds);
        });
    }
    const double w = b.valid() ? b.max.x - b.min.x : 1.0;
    panel.action("move", tr(loc, "gui.xp.move"), {{"dx", 0.5}, {"dy", 0.0}}, [run, id](const QList<double>& v) {
        if (std::abs(v.value(0)) + std::abs(v.value(1)) > 1e-9) run({QStringLiteral("MOVE %1 %2").arg(id, cmdPt(v.value(0), v.value(1)))});
    });
    panel.action("copy", tr(loc, "gui.xp.copy"), {{"dx", std::round((w + 1.0) * 100) / 100}, {"dy", 0.0}}, [run, id](const QList<double>& v) {
        if (std::abs(v.value(0)) + std::abs(v.value(1)) > 1e-9) run({QStringLiteral("COPY %1 %2").arg(id, cmdPt(v.value(0), v.value(1)))});
    });
    // plita / fundament / devor: teshik (proyom) — markazidan (dx, dy) siljigan to'rtburchak teshik qatlamida
    if (pr.kind == ElemKind::Slab || pr.kind == ElemKind::Roof || pr.kind == ElemKind::FoundationSlab) {
        const Vec3 c = centroidOf(ring);
        panel.action("hole", tr(loc, "gui.xp.hole"), {{"b", 1.0}, {"h", 1.0}, {"dx", 0.0}, {"dy", 0.0}},
                     [&doc, layerMap, run, c](const QList<double>& v) {
                         const double bw = v.value(0), bh = v.value(1);
                         if (bw < 0.05 || bh < 0.05) return;
                         const double x = c.x + v.value(2), y = c.y + v.value(3);
                         bool create = false;
                         const QString lay = layerFor(doc, layerMap, ElemKind::Opening, &create);
                         QStringList cmds;
                         if (create) cmds << QStringLiteral("LAYER %1").arg(lay);
                         cmds << QStringLiteral("RECTANG %1 %2").arg(cmdPt(x - bw / 2, y - bh / 2), cmdPt(x + bw / 2, y + bh / 2))
                              << QStringLiteral("CHPROP LAST LAYER=%1").arg(lay);
                         run(cmds);
                     });
    }
    panel.action("erase", tr(loc, "gui.xp.erase"), [run, id] { run({QStringLiteral("ERASE %1").arg(id)}); }, true);
    panel.note(tr(loc, "gui.xp.undo"));
}

}  // namespace asiacad::gui
