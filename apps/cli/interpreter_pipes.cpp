// AsiaCAD CLI — aqlli obyektlar va tuzatish paneli buyruqlari:
//   SETPROP sel KEY=value …      — har qanday obyekt xossasi (X1, LENGTH, R, TEXT, ELEV …), bitta undo qadami
//   PIPEEDIT net SEG=n KEY=v …   — kanalizatsiya quvuri: SPEC SLOPE(‰) FLOW INSUP/INSDOWN(sm) INVUP INVDOWN(m)
//   PIPEEDIT net MH=n KEY=v …    — quduq: NAME X Y RIM INV(m | AUTO) D(m, 0 — avto) AREA(ga)
// Tarmoq hujjat lug'atida ("sewer:<nom>") saqlanadi; o'zgarishdan keyin plan va profil qayta chiziladi.
#include "interpreter.h"

#include <asiacad/civil/pipe_catalog.h>
#include <asiacad/core/error.h>
#include <asiacad/core/grips.h>
#include <asiacad/core/props.h>
#include <asiacad/i18n/json.h>
#include <asiacad/i18n/utf8.h>

#include <format>

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

bool Interpreter::dispatchProps(const std::string& cmd, const Args& a) {
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
