// AsiaCAD CLI — aqlli obyektlar va tuzatish paneli buyruqlari:
//   SETPROP sel KEY=value …      — har qanday obyekt xossasi (X1, LENGTH, R, TEXT, ELEV …), bitta undo qadami
//   PIPEEDIT net SEG=n KEY=v …   — kanalizatsiya quvuri: SPEC SLOPE(‰) FLOW INSUP/INSDOWN(sm) INVUP INVDOWN(m)
//   PIPEEDIT net MH=n KEY=v …    — quduq: NAME X Y RIM INV(m | AUTO) D(m, 0 — avto) AREA(ga)
// Tarmoq hujjat lug'atida ("sewer:<nom>") saqlanadi; o'zgarishdan keyin plan va profil qayta chiziladi.
#include "interpreter.h"

#include <asiacad/civil/excel.h>
#include <asiacad/civil/pipe_catalog.h>
#include <asiacad/io/xlsx.h>
#include <asiacad/civil/road.h>
#include <asiacad/civil/terrain.h>
#include <asiacad/civil/utility.h>
#include <asiacad/civil/waterway.h>
#include <asiacad/core/error.h>
#include <asiacad/core/grips.h>
#include <asiacad/core/props.h>
#include <asiacad/i18n/json.h>
#include <asiacad/i18n/utf8.h>

#include <cmath>
#include <format>
#include <set>

namespace asiacad::cli {

std::size_t Interpreter::sewerDraw(const civil::SewerNetwork& net, const std::string& at, const std::string& cmd) {
    const auto res = civil::compute(net);
    std::vector<std::unique_ptr<IEntity>> es = civil::drawPlan(net, res);
    if (!at.empty() && at != "-") {
        const Vec3 o = parseRawPoint(at);
        civil::ProfileLabels lb{loc_.tr("sewer.prof.ground"), loc_.tr("sewer.prof.invert"), loc_.tr("sewer.prof.depth"),
                                loc_.tr("sewer.prof.slope"), loc_.tr("sewer.prof.distance"), loc_.tr("sewer.prof.pipe"),
                                loc_.tr("sewer.prof.manhole"), loc_.tr("sewer.prof.datum")};
        for (auto& e : civil::drawProfile(net, res, o, lb)) es.push_back(std::move(e));
    }
    auto comp = std::make_unique<CompositeCommand>(cmd);
    comp->add(std::make_unique<DictCommand>("sewer:" + net.name, civil::toJson(net), cmd));
    comp->add(std::make_unique<DictCommand>("sewer:" + net.name + ":at", at, cmd));
    std::vector<EntityId> old;  // shu tarmoqning eski chizmasi
    doc_.forEach([&](const IEntity& e) { if (e.xget("sewer") == net.name) old.push_back(e.id()); });
    if (!old.empty()) comp->add(std::make_unique<DeleteCommand>(old));
    const int colors[] = {30, 7, 7, 8};
    int li = 0;
    for (const auto& ln : civil::sewerLayers(net)) {
        if (!doc_.layer(ln)) {
            Layer l{ln};
            l.color = colors[li];
            comp->add(std::make_unique<SetLayerCommand>(l));
        }
        ++li;
    }
    std::vector<AddEntityCommand*> adds;
    for (auto& e : es) {
        auto c = std::make_unique<AddEntityCommand>(std::move(e), cmd);
        adds.push_back(c.get());
        comp->add(std::move(c));
    }
    stack_.execute(std::move(comp));
    last_.clear();
    for (auto* c : adds) last_.push_back(c->entityId());
    return last_.size();
}

std::size_t Interpreter::smartDraw(const std::string& kind, const std::string& json, std::vector<std::unique_ptr<IEntity>> es,
                                    const std::string& cmd, const std::string& at, bool replace) {
    auto comp = std::make_unique<CompositeCommand>(cmd);
    comp->add(std::make_unique<DictCommand>(kind + ":main", json, cmd));
    if (!at.empty()) comp->add(std::make_unique<DictCommand>(kind + ":main:at", at, cmd));
    std::vector<EntityId> old;
    doc_.forEach([&](const IEntity& e) {
        if (e.xget(kind) != "main") return;
        if (replace || (!at.empty() && e.xget("part") == "profile")) old.push_back(e.id());
    });
    if (!old.empty()) comp->add(std::make_unique<DeleteCommand>(old));
    std::set<std::string> made;
    for (const auto& e : es)
        if (!doc_.layer(e->layer()) && made.insert(e->layer()).second) comp->add(std::make_unique<SetLayerCommand>(Layer{e->layer()}));
    std::vector<AddEntityCommand*> adds;
    for (auto& e : es) {
        if (e->xget(kind).empty()) e->xset(kind, "main");
        auto c = std::make_unique<AddEntityCommand>(std::move(e), cmd);
        adds.push_back(c.get());
        comp->add(std::move(c));
    }
    stack_.execute(std::move(comp));
    last_.clear();
    for (auto* c : adds) last_.push_back(c->entityId());
    return last_.size();
}

namespace {
// Gravitatsion quvurda oraliq uchlar boshi-oxiri orasida chiziqli (2D masofa bo'yicha)
void interpolateZ(civil::UtilLine& ln) {
    if (ln.v.size() < 3) return;
    std::vector<double> d(ln.v.size(), 0);
    for (std::size_t i = 1; i < ln.v.size(); ++i) d[i] = d[i - 1] + std::hypot(ln.v[i].x - ln.v[i - 1].x, ln.v[i].y - ln.v[i - 1].y);
    if (d.back() <= 0) return;
    for (std::size_t i = 1; i + 1 < ln.v.size(); ++i) {
        ln.v[i].z = ln.v.front().z + (ln.v.back().z - ln.v.front().z) * d[i] / d.back();
        ln.v[i].src = civil::UtilVertex::Manual;
    }
}
}  // namespace

bool Interpreter::dispatchProps(const std::string& cmd, const Args& a) {
    if (cmd == "PIPEDRAW") {
        // PIPEDRAW nom p1 p2 … [SYS=K1|K2] [SPEC=id] [SLOPE=‰] [DEPTH=m] [RIM=m] — quduqlar oqim yo'nalishida;
        // yer belgisi chizmadagi yer nuqtalaridan (TIN), bo'lmasa RIM; oxirgi nuqta boshqa tarmoq qudug'iga tushsa — ulanish
        if (a.size() < 3) throw Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))});
        civil::SewerNetwork net;
        net.name = a[0];
        if (!doc_.dictGet("sewer:" + net.name).empty()) throw Error("pipe.err.exists", {net.name});
        std::vector<Vec3> pts;
        std::string spec = "PVC-SN8-200";
        double slope = 0, rimFix = std::nan("");
        for (std::size_t i = 1; i < a.size(); ++i) {
            const auto eq = a[i].find('=');
            if (eq == std::string::npos) { pts.push_back(parsePoint(a[i])); continue; }
            const std::string k = utf8::toUpper(a[i].substr(0, eq)), v = a[i].substr(eq + 1);
            if (k == "SYS") net.system = utf8::toUpper(v);
            else if (k == "SPEC") spec = v;
            else if (k == "SLOPE") slope = parseNumber(v) / 1000.0;
            else if (k == "DEPTH") net.startDepth = parseNumber(v);
            else if (k == "RIM") rimFix = parseNumber(v);
            else throw Error("prop.err.key", {k});
        }
        if (pts.size() < 2) throw Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))});
        const auto* ps = civil::PipeCatalog::builtin().find(spec);
        if (!ps) throw Error("sewer.chk.spec", {"1", spec});
        net.defaultSlope = slope > 0 ? slope : ps->dn <= 150 ? 0.008 : ps->dn <= 200 ? 0.007 : 1.0 / ps->dn;
        // mavjud tarmoq quduqlari (ulanish va yer belgisi uchun)
        struct Mh { std::string net, name; double x, y, rim, inv, din; };
        std::vector<Mh> existing;
        for (const auto& [k, v] : doc_.dict()) {
            if (k.rfind("sewer:", 0) != 0 || k.find(":at") != std::string::npos) continue;
            try {
                const auto other = civil::fromJson(v);
                const auto r = civil::compute(other);
                for (std::size_t j = 0; j < other.manholes.size() && j < r.manholes.size(); ++j) {
                    double din = 0;  // shu quduqdan chiquvchi (yo'q bo'lsa kiruvchi) quvur ichki diametri
                    const std::size_t sj = j < r.segments.size() ? j : j - 1;
                    if (sj < r.segments.size() && r.segments[sj].spec) din = r.segments[sj].spec->id_mm() / 1000.0;
                    existing.push_back({other.name, other.manholes[j].name, other.manholes[j].x, other.manholes[j].y, other.manholes[j].rim,
                                        std::min(r.manholes[j].invIn, r.manholes[j].invOut), din});
                }
            } catch (...) {
            }
        }
        auto near = [&](const Vec3& p) -> const Mh* {
            for (const auto& m : existing)
                if (std::hypot(m.x - p.x, m.y - p.y) < 0.6) return &m;
            return nullptr;
        };
        // yer yuzasi: sewer chizmasi va bog'lanish natijalaridan tashqari barcha yer nuqtalari
        std::vector<EntityId> ground;
        doc_.forEach([&](const IEntity& e) { if (e.xget("sewer").empty()) ground.push_back(e.id()); });
        civil::Tin tin;
        if (std::isnan(rimFix)) {
            auto tp = civil::terrainPoints(doc_, notLinkOutput(ground));
            if (tp.size() >= 3) tin = civil::Tin::build(tp);
        }
        int fromTin = 0;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            civil::Manhole m;
            m.x = pts[i].x;
            m.y = pts[i].y;
            m.name = net.name + "-" + std::to_string(i + 1);
            double z = 0;
            if (const Mh* e = near(pts[i])) { m.x = e->x; m.y = e->y; m.rim = e->rim; m.name = e->name; }
            else if (!std::isnan(rimFix)) m.rim = rimFix;
            else if (!tin.empty() && tin.z(m.x, m.y, z)) { m.rim = z; ++fromTin; }
            else if (std::abs(pts[i].z) > 1e-9) m.rim = pts[i].z;
            else m.rim = 100.0;
            net.manholes.push_back(m);
        }
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) net.segments.push_back({spec, 0, 0});
        // Shoxobcha: mavjud quduqqa ulansa — lotok "shelyga" (quvur ustlari) bo'yicha moslanadi: boshlanishda kerakli chuqurlik saqlanadi,
        // nishablik shunga tanlanadi (normadan kam emas, 30 ‰ dan ko'p bo'lsa — qolgani quduqda perepad)
        if (const Mh* j = near(pts.back())) {
            double L = 0;
            for (std::size_t i = 0; i + 1 < net.manholes.size(); ++i)
                L += std::hypot(net.manholes[i + 1].x - net.manholes[i].x, net.manholes[i + 1].y - net.manholes[i].y);
            const double din = ps->id_mm() / 1000.0;
            const double target = j->inv + std::max(0.0, j->din - din);
            const double startMin = net.manholes.front().rim - net.startDepth;
            const double minS = slope > 0 ? slope : net.defaultSlope;
            double sl = minS, invUp = startMin;
            if (L > 0) {
                const double need = (startMin - target) / L;
                if (need >= minS) sl = std::min(need, std::max(minS, 0.03));
                else invUp = target + minS * L;   // boshi chuqurroq
            }
            net.manholes.front().invert = invUp;
            for (auto& sg : net.segments) sg.slope = sl;
            say("pipe.msg.branch", {j->net, j->name, std::format("{:.1f}", sl * 1000), std::format("{:.2f}", net.manholes.front().rim - invUp)});
        }
        // profil joyi: chizmaning o'ng tomoni (SEWER kabi)
        BBox b = doc_.extents(true);
        const auto res = civil::compute(net);
        for (const auto& e : civil::drawPlan(net, res)) b.expand(e->bbox());
        const std::string at = std::format("{},{}", b.max.x + 46 * net.textHeight, b.min.y);
        sewerDraw(net, at, cmd);
        say("pipe.msg.drawn", {net.name, std::to_string(net.manholes.size()), std::format("{:.2f}", res.totalLength), std::to_string(fromTin),
                               std::to_string(res.errors()), std::to_string(res.warnings())});
        // ulanish: oxirgi quduq boshqa tarmoq qudug'i bo'lsa — lotoklar farqi
        if (const Mh* j = near(pts.back()); j && !res.segments.empty()) {
            const double drop = res.segments.back().invDown - j->inv;
            say(drop >= -1e-9 ? "pipe.msg.join" : "pipe.warn.join", {j->net, j->name, std::format("{:.1f}", drop * 100)});
        }
        return true;
    }
    if (cmd == "OBJEDIT") {
        // OBJEDIT UTIL LINE=n DN=mm ZUP=m ZDOWN=m DEPTH=m KIND=k | UTIL NODE=n GROUND BOTTOM NAME | ROAD ST=n DESIGN=m | WW ST=n BOTTOM=m
        if (a.size() < 3) throw Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))});
        const std::string kind = utf8::toUpper(a[0]);
        const std::string dk = kind == "UTIL" ? "util" : kind == "ROAD" ? "road" : kind == "WW" ? "ww" : std::string();
        if (dk.empty()) throw Error("obj.err.kind", {a[0]});
        const std::string js = doc_.dictGet(dk + ":main");
        if (js.empty()) throw Error("obj.err.none", {kind});
        std::string elemKey;
        int idx = -1;
        std::vector<std::pair<std::string, std::string>> kv;
        for (std::size_t i = 1; i < a.size(); ++i) {
            const auto eq = a[i].find('=');
            if (eq == std::string::npos || eq == 0) throw Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))});
            const std::string k = utf8::toUpper(a[i].substr(0, eq)), v = a[i].substr(eq + 1);
            if (k == "LINE" || k == "NODE" || k == "ST") { elemKey = k; idx = static_cast<int>(parseNumber(v)) - 1; }
            else kv.emplace_back(k, v);
        }
        if (idx < 0) throw Error("obj.err.index", {kind});
        const std::string at = doc_.dictGet(dk + ":main:at");
        std::string label;
        int errors = 0;
        if (dk == "util") {
            civil::UtilModel m;
            auto p = civil::defaultUtilParams();
            civil::utilFromJson(js, m, p);
            if (elemKey == "LINE") {
                if (idx >= static_cast<int>(m.lines.size())) throw Error("obj.err.index", {kind});
                auto& ln = m.lines[static_cast<std::size_t>(idx)];
                bool zEdit = false;
                for (const auto& [k, v] : kv) {
                    if (k == "DN") { ln.dn = parseNumber(v); ln.dnManual = true; }
                    else if (k == "ZUP") { ln.v.front().z = parseNumber(v); ln.v.front().src = civil::UtilVertex::Manual; zEdit = true; }
                    else if (k == "ZDOWN") { ln.v.back().z = parseNumber(v); ln.v.back().src = civil::UtilVertex::Manual; zEdit = true; }
                    else if (k == "DEPTH") {  // quvur ustigacha qatlam, hamma uchlarda
                        const double c = parseNumber(v);
                        for (auto& q : ln.v) { q.z = q.ground - c - ln.dn / 2000.0; q.src = civil::UtilVertex::Manual; }
                    } else if (k == "KIND") {
                        civil::UtilKind uk;
                        if (!civil::utilFromKey(utf8::toUpper(v), uk)) throw Error("prop.err.value", {v});
                        ln.kind = uk;
                    } else throw Error("prop.err.key", {k});
                }
                if (zEdit && civil::isGravity(ln.kind)) interpolateZ(ln);
                label = std::to_string(idx + 1);
            } else if (elemKey == "NODE") {
                if (idx >= static_cast<int>(m.nodes.size())) throw Error("obj.err.index", {kind});
                auto& n = m.nodes[static_cast<std::size_t>(idx)];
                for (const auto& [k, v] : kv) {
                    if (k == "GROUND") n.ground = parseNumber(v);
                    else if (k == "BOTTOM") { n.bottom = parseNumber(v); n.estimated = false; }
                    else if (k == "NAME") n.name = v;
                    else throw Error("prop.err.key", {k});
                }
                label = n.name.empty() ? std::to_string(idx + 1) : n.name;
            } else throw Error("obj.err.index", {kind});
            civil::checkUtilities(m, p);
            errors = m.errors();
            smartDraw("util", civil::utilToJson(m, p), civil::drawUtilities3D(m, 0.5), cmd, std::string(), true);
        } else if (dk == "road") {
            civil::RoadModel m;
            auto p = civil::defaultRoadParams();
            civil::roadFromJson(js, m, p);
            if (elemKey != "ST" || idx >= static_cast<int>(m.st.size())) throw Error("obj.err.index", {kind});
            for (const auto& [k, v] : kv) {
                if (k == "DESIGN") { m.st[static_cast<std::size_t>(idx)].design = parseNumber(v); m.st[static_cast<std::size_t>(idx)].control = true; }
                else throw Error("prop.err.key", {k});
            }
            civil::recomputeRoad(m, p);
            errors = m.errors();
            auto es = civil::drawRoad3D(m);
            if (!at.empty())
                for (auto& e : civil::drawRoadProfile(m, parseRawPoint(at), 10, 2.0, loc_.tr("road.lbl.ground"), loc_.tr("road.lbl.design"), loc_.tr("road.lbl.pk"))) {
                    e->xset("part", "profile");
                    es.push_back(std::move(e));
                }
            label = civil::pk(m.st[static_cast<std::size_t>(idx)].s);
            smartDraw("road", civil::roadToJson(m, p), std::move(es), cmd, at, true);
        } else {
            civil::WwModel m;
            auto p = civil::defaultWwParams();
            civil::wwFromJson(js, m, p);
            if (elemKey != "ST" || idx >= static_cast<int>(m.st.size())) throw Error("obj.err.index", {kind});
            for (const auto& [k, v] : kv) {
                if (k == "BOTTOM") { m.st[static_cast<std::size_t>(idx)].bottom = parseNumber(v); m.st[static_cast<std::size_t>(idx)].control = true; }
                else throw Error("prop.err.key", {k});
            }
            civil::recomputeWaterway(m, p);
            errors = m.errors();
            auto es = civil::drawWaterway3D(m, p);
            if (!at.empty())
                for (auto& e : civil::drawWaterwayProfile(m, p, parseRawPoint(at), 10, 2.0, loc_.tr("road.lbl.ground"), loc_.tr("ww.lbl.bottom"),
                                                          loc_.tr("ww.lbl.water"), loc_.tr("road.lbl.pk"))) {
                    e->xset("part", "profile");
                    es.push_back(std::move(e));
                }
            label = civil::pk(m.st[static_cast<std::size_t>(idx)].s);
            smartDraw("ww", civil::wwToJson(m, p), std::move(es), cmd, at, true);
        }
        say("obj.msg.edited", {loc_.tr("obj.kind." + dk), label, std::to_string(errors)});
        return true;
    }
    if (cmd != "SETPROP" && cmd != "PIPEEDIT") return false;
    auto usage = [&]() { return Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))}); };
    if (a.size() < 2) throw usage();
    std::vector<std::pair<std::string, std::string>> kv;
    for (std::size_t i = 1; i < a.size(); ++i) {
        const auto eq = a[i].find('=');
        if (eq == std::string::npos || eq == 0) throw usage();
        kv.emplace_back(utf8::toUpper(a[i].substr(0, eq)), a[i].substr(eq + 1));
    }

    if (cmd == "SETPROP") {
        const auto ids = parseSelection(a[0]);
        if (ids.empty()) throw Error("core.no_selection");
        auto comp = std::make_unique<CompositeCommand>(cmd);
        std::size_t changed = 0;
        for (EntityId id : ids) {
            const IEntity* e = doc_.find(id);
            if (!e || !doc_.isEditable(id)) continue;
            std::unique_ptr<IEntity> cur = e->clone();
            bool any = false;
            for (const auto& [k, v] : kv)
                if (auto n = withProp(*cur, k, v)) { cur = std::move(n); any = true; }
            if (!any) continue;
            comp->add(std::make_unique<ReplaceEntityCommand>(id, std::move(cur), cmd));
            ++changed;
        }
        if (!changed) throw Error("prop.err.key", {kv.front().first});
        stack_.execute(std::move(comp));
        last_ = ids;
        say("prop.msg.changed", {std::to_string(changed)});
        return true;
    }

    // PIPEEDIT
    const std::string name = a[0];
    const std::string js = doc_.dictGet("sewer:" + name);
    if (js.empty()) throw Error("pipe.err.net", {name});
    civil::SewerNetwork net;
    try {
        net = civil::fromJson(js);
    } catch (const json::ParseError& ex) {
        throw Error("sewer.err.format", {ex.what()});
    }
    const auto before = civil::compute(net);
    for (const auto& [k, v] : kv)
        if (k == "XLSX") {  // smeta / hajmlar jadvali (quduqlar, bo'laklar, hajmlar, yer ishlari)
            auto tr = [this](const std::string& key, const std::vector<std::string>& args) { return loc_.tr(key, args); };
            xlsx::save(excel::sewerWorkbook(net, before, tr), v);
            say("dev.msg.saved", {v});
            return true;
        }
    int seg = -1, mh = -1;
    for (const auto& [k, v] : kv) {
        if (k == "SEG") seg = static_cast<int>(parseNumber(v)) - 1;
        if (k == "MH") mh = static_cast<int>(parseNumber(v)) - 1;
    }
    if ((seg < 0) == (mh < 0)) throw usage();
    if (seg >= static_cast<int>(net.segments.size()) || mh >= static_cast<int>(net.manholes.size())) throw Error("pipe.err.index", {name});
    for (const auto& [k, v] : kv) {
        if (k == "SEG" || k == "MH") continue;
        if (seg >= 0) {
            auto& s = net.segments[static_cast<std::size_t>(seg)];
            const auto& r = before.segments[static_cast<std::size_t>(seg)];
            if (k == "SPEC") {
                if (!civil::PipeCatalog::builtin().find(v)) throw Error("sewer.chk.spec", {std::to_string(seg + 1), v});
                s.specId = v;
            } else if (k == "SLOPE") s.slope = parseNumber(v) / 1000.0;
            else if (k == "FLOW") s.flowLs = parseNumber(v);
            else if (k == "INSUP") s.insUp = parseNumber(v) / 100.0;
            else if (k == "INSDOWN") s.insDown = parseNumber(v) / 100.0;
            else if (k == "INVUP") net.manholes[static_cast<std::size_t>(seg)].invert = parseNumber(v);
            else if (k == "INVDOWN") {  // oxiri belgisi -> nishablik
                if (r.length <= 0) throw Error("prop.err.value", {v});
                const double up = net.manholes[static_cast<std::size_t>(seg)].invert.value_or(r.invUp);
                const double sl = (up - parseNumber(v)) / r.length;
                if (sl <= 0) throw Error("pipe.err.slope", {v});
                s.slope = sl;
            } else throw Error("prop.err.key", {k});
        } else {
            auto& m = net.manholes[static_cast<std::size_t>(mh)];
            if (k == "NAME") m.name = v;
            else if (k == "X") m.x = parseNumber(v);
            else if (k == "Y") m.y = parseNumber(v);
            else if (k == "RIM") m.rim = parseNumber(v);
            else if (k == "INV") {
                if (utf8::toUpper(v) == "AUTO" || v == "-" || v.empty()) m.invert.reset(); else m.invert = parseNumber(v);
            } else if (k == "D") m.diameter = parseNumber(v);
            else if (k == "AREA") m.areaHa = parseNumber(v);
            else throw Error("prop.err.key", {k});
        }
    }
    const std::string at = doc_.dictGet("sewer:" + name + ":at");
    sewerDraw(net, at.empty() ? "-" : at, cmd);
    const auto res = civil::compute(net);
    say(seg >= 0 ? "pipe.msg.seg" : "pipe.msg.mh",
        {name, seg >= 0 ? net.manholes[static_cast<std::size_t>(seg)].name + " → " + net.manholes[static_cast<std::size_t>(seg) + 1].name
                        : net.manholes[static_cast<std::size_t>(mh)].name,
         std::to_string(res.errors()), std::to_string(res.warnings())});
    return true;
}

} // namespace asiacad::cli
