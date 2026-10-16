#include "interpreter.h"

#include <asiacad/civil/sewer.h>
#include <asiacad/core/dimension.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/grips.h>
#include <asiacad/core/geom2d.h>
#include <asiacad/core/modify.h>
#include <asiacad/core/error.h>
#include <asiacad/core/snap.h>
#include <asiacad/i18n/json.h>
#include <asiacad/i18n/utf8.h>
#include <asiacad/io/acad_file.h>
#include <asiacad/io/dxf_reader.h>
#include <asiacad/io/dxf_writer.h>

#include <format>
#include <fstream>
#include <map>
#include <sstream>

namespace asiacad::cli {

namespace {

// Bo'shliq bo'yicha ajratish, "qo'shtirnoq ichidagi matn" bitta token
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false, has = false;
    for (char ch : line) {
        if (ch == '"') { quoted = !quoted; has = true; continue; }
        if (!quoted && (ch == ' ' || ch == '\t' || ch == '\r')) {
            if (has) { out.push_back(cur); cur.clear(); has = false; }
            continue;
        }
        cur.push_back(ch);
        has = true;
    }
    if (has) out.push_back(cur);
    return out;
}

std::string f4(double v) { return std::format("{:.4f}", v); }
std::string idStr(EntityId id) { return std::to_string(id); }

// Kengaytma kichik harfda, nuqtasiz ("" — yo'q)
std::string extOf(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string e = path.substr(dot + 1);
    for (auto& c : e) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return e;
}
bool forced(const std::vector<std::string>& a) { return !a.empty() && a.back() == "!"; }

} // namespace

const char* commandSyntax(const std::string& c) {
    static const std::map<std::string, const char*> m = {
        {"LINE", "p1 p2 [p3 ...]"},
        {"PLINE", "p1 p2 ... [{CLOSE}]"},
        {"CIRCLE", "p r"},
        {"ARC", "p1 p2 p3"},
        {"POINT", "p [p ...]"},
        {"TEXT", "p h \"...\" [°]"},
        {"LAYER", "[name [1..255] [{ON}|{OFF}|{LOCK}|{UNLOCK}|{FREEZE}|{THAW}]]"},
        {"CLAYER", "name"},
        {"COLOR", "1..255 | {BYLAYER}"},
        {"MOVE", "sel dx,dy | sel p1 p2"},
        {"COPY", "sel dx,dy [n]"},
        {"ROTATE", "sel p °"},
        {"SCALE", "sel p k"},
        {"MIRROR", "sel p1 p2 [{COPY}]"},
        {"ERASE", "sel"},
        {"CHPROP", "sel [{LAYER}=name] [{COLOR}=n]"},
        {"UNDO", "[n]"},
        {"REDO", "[n]"},
        {"LIST", "[sel]"},
        {"AREA", "sel"},
        {"DIST", "p1 p2"},
        {"EXTENTS", ""},
        {"SAVE", "[file.acad | file.dxf]"},
        {"OPEN", "file.acad | file.dxf [!]"},
        {"NEW", "[!]"},
        {"IMPORT", "file.dxf"},
        {"BLOCK", "[name p sel]"},
        {"OSNAP", "p [{END}|{MID}|{CEN}|{QUA}|{INT}|{PER}|{TAN}|{NEA}|{NOD}|{INS} ...] [r]"},
        {"SELECT", "sel"},
        {"INSERT", "name p [k] [°]"},
        {"EXPLODE", "sel"},
        {"HATCH", "sel [pattern] [k] [°]"},
        {"GRIP", "id n p"},
        {"OFFSET", "d sel p"},
        {"TRIM", "sel p [p ...]"},
        {"EXTEND", "sel p [p ...]"},
        {"FILLET", "p1 p2 [r]"},
        {"SEWER", "file.kan.json [x,y | -]"},
        {"PIPES", "[PVC|PP|PE|AC|STEEL|DI|RC]"},
        {"DIMLINEAR", "p1 p2 p [h] [0..6]"},
        {"DIMALIGNED", "p1 p2 p [h] [0..6]"},
        {"DIMRADIUS", "sel p [h] [0..6]"},
        {"DIMDIAMETER", "sel p [h] [0..6]"},
        {"LANG", "[uz|ru|kk|ky|tr|en]"},
        {"HELP", ""},
        {"EXIT", "[!]"},
    };
    auto it = m.find(c);
    return it == m.end() ? "" : it->second;
}

// {CLOSE} kabi joylarni faol tildagi kalit so'z bilan almashtiradi
static std::string localizeSyntax(const i18n::Localizer& loc, std::string s) {
    for (std::size_t p; (p = s.find('{')) != std::string::npos;) {
        const auto q = s.find('}', p);
        if (q == std::string::npos) break;
        s.replace(p, q - p + 1, loc.keywordName(s.substr(p + 1, q - p - 1)));
    }
    return s;
}

void Interpreter::say(const std::string& key, const std::vector<std::string>& args) {
    out_ << loc_.tr(key, args) << '\n';
}

void Interpreter::need(const Args& a, std::size_t n, const std::string& canon) const {
    if (a.size() < n)
        throw Error("err.usage", {loc_.commandName(canon) + " " + localizeSyntax(loc_, commandSyntax(canon))});
}

double Interpreter::parseNumber(const std::string& tok) const {
    std::size_t pos = 0;
    double v = 0;
    try { v = std::stod(tok, &pos); } catch (...) { pos = 0; }
    if (pos == 0 || pos != tok.size()) throw Error("err.number_expected", {tok});
    return v;
}

std::vector<double> Interpreter::parseNumbers(const std::string& csv) const {
    std::vector<double> v;
    std::stringstream ss(csv);
    std::string part;
    while (std::getline(ss, part, ',')) v.push_back(parseNumber(part));
    return v;
}

// "END:10,5" — 10,5 atrofida END nuqtasi (aperture_ radiusida); PER/TAN uchun oldingi nuqta ishlatiladi
Vec3 Interpreter::parsePoint(const std::string& tok) const {
    if (const auto colon = tok.find(':'); colon != std::string::npos && colon > 0 && tok[0] != '@') {
        const std::string kw = loc_.resolveKeyword(tok.substr(0, colon));
        const SnapMode mode = snapFromName(kw);
        if (mode == 0) throw Error("err.point_format", {tok});
        const Vec3 from = lastPt_;
        const Vec3 cursor = parseRawPoint(tok.substr(colon + 1));
        const auto snaps = findSnaps(doc_, cursor, aperture_, mode, &from);
        if (snaps.empty())
            throw Error("err.snap_none", {loc_.keywordName(kw), tok.substr(colon + 1), f4(aperture_)});
        lastPt_ = snaps.front().point;
        return lastPt_;
    }
    return parseRawPoint(tok);
}

Vec3 Interpreter::parseRawPoint(const std::string& tok) const {
    const bool rel = !tok.empty() && tok[0] == '@';
    std::vector<double> v;
    std::stringstream ss(rel ? tok.substr(1) : tok);
    std::string part;
    try {
        while (std::getline(ss, part, ',')) v.push_back(parseNumber(part));
    } catch (const Error&) {
        throw Error("err.point_format", {tok});
    }
    if (v.size() < 2 || v.size() > 3) throw Error("err.point_format", {tok});
    Vec3 p{v[0], v[1], v.size() == 3 ? v[2] : 0.0};
    if (rel) p = lastPt_ + p;
    lastPt_ = p;
    return p;
}

std::vector<EntityId> Interpreter::parseSelection(const std::string& tok) const {
    const std::string kw = loc_.resolveKeyword(tok);
    if (kw == "ALL") return doc_.ids();
    if (kw == "LAST") return last_;
    if (const auto colon = tok.find(':'); colon != std::string::npos) {
        const std::string k = loc_.resolveKeyword(tok.substr(0, colon));
        const std::string rest = tok.substr(colon + 1);
        if (k == "LAYER") return doc_.idsOnLayer(rest);
        if (k == "WINDOW" || k == "CROSSING") {
            const auto v = parseNumbers(rest);
            if (v.size() != 4) throw Error("err.selection_format", {tok});
            BBox box;
            box.expand({v[0], v[1], 0});
            box.expand({v[2], v[3], 0});
            return selectWindow(doc_, box, k == "CROSSING");
        }
        if (k == "PICK") {
            const auto v = parseNumbers(rest);
            if (v.size() < 2 || v.size() > 3) throw Error("err.selection_format", {tok});
            const EntityId id = pickEntity(doc_, {v[0], v[1], 0}, v.size() == 3 ? v[2] : aperture_);
            return id == kInvalidId ? std::vector<EntityId>{} : std::vector<EntityId>{id};
        }
        throw Error("err.selection_format", {tok});
    }
    std::vector<EntityId> ids;
    std::stringstream ss(tok);
    std::string part;
    while (std::getline(ss, part, ',')) {
        const auto dash = part.find('-');
        if (dash != std::string::npos && dash > 0) {
            const auto a = static_cast<EntityId>(parseNumber(part.substr(0, dash)));
            const auto b = static_cast<EntityId>(parseNumber(part.substr(dash + 1)));
            for (EntityId i = a; i <= b; ++i) if (doc_.contains(i)) ids.push_back(i);
        } else {
            ids.push_back(static_cast<EntityId>(parseNumber(part)));
        }
    }
    return ids;
}

void Interpreter::addAndReport(const std::string& canon, std::unique_ptr<IEntity> e, const char* msgKey) {
    e->setLayer(layer_);
    e->setColor(color_);
    auto& cmd = static_cast<AddEntityCommand&>(stack_.execute(std::make_unique<AddEntityCommand>(std::move(e), canon)));
    last_ = {cmd.entityId()};
    say(msgKey, {idStr(cmd.entityId())});
}

void Interpreter::addDimension(const std::string& canon, std::unique_ptr<DimensionEntity> d) {
    const std::string txt = d->text();
    d->setLayer(layer_);
    d->setColor(color_);
    auto& cmd = static_cast<AddEntityCommand&>(stack_.execute(std::make_unique<AddEntityCommand>(std::move(d), canon)));
    last_ = {cmd.entityId()};
    say("msg.dim_created", {idStr(cmd.entityId()), txt});
}

void Interpreter::addMany(const char* name, std::vector<std::unique_ptr<IEntity>> es) {
    auto comp = std::make_unique<CompositeCommand>(name);
    std::vector<AddEntityCommand*> adds;
    for (auto& e : es) {
        e->setLayer(layer_);
        e->setColor(color_);
        auto c = std::make_unique<AddEntityCommand>(std::move(e));
        adds.push_back(c.get());
        comp->add(std::move(c));
    }
    stack_.execute(std::move(comp));
    last_.clear();
    for (auto* c : adds) last_.push_back(c->entityId());
}

bool Interpreter::run(const std::string& rawLine) {
    std::string line = rawLine;
    if (auto hash = line.find('#'); hash != std::string::npos && line.find('"') > hash) line.resize(hash);
    auto toks = tokenize(line);
    if (toks.empty()) return true;
    const std::string canon = loc_.resolveCommand(toks[0]);
    Args args(toks.begin() + 1, toks.end());
    if (canon == "EXIT") {
        if (!stack_.isDirty() || forced(args)) return false;
        say("err.prefix", {loc_.tr("err.unsaved", {loc_.commandName("EXIT")})});
        return true;
    }
    try {
        if (canon.empty()) throw Error("err.unknown_command", {toks[0]});
        dispatch(canon, args);
    } catch (const Error& ex) {
        say("err.prefix", {loc_.tr(ex.key(), ex.args())});
    } catch (const std::exception& ex) {
        say("err.prefix", {ex.what()});
    }
    return true;
}

void Interpreter::dispatch(const std::string& cmd, const Args& a) {
    if (cmd == "LINE") {
        need(a, 2, cmd);
        std::vector<Vec3> pts;
        for (const auto& t : a) pts.push_back(parsePoint(t));
        std::vector<std::unique_ptr<IEntity>> es;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) es.push_back(std::make_unique<LineEntity>(pts[i], pts[i + 1]));
        addMany("LINE", std::move(es));
        say("msg.lines_created", {std::to_string(last_.size()), idStr(last_.front()), idStr(last_.back())});
    } else if (cmd == "PLINE") {
        need(a, 2, cmd);
        bool closed = false;
        std::vector<Vec3> pts;
        for (const auto& t : a) {
            if (loc_.isKeyword(t, "CLOSE")) closed = true;
            else pts.push_back(parsePoint(t));
        }
        if (pts.size() < 2) throw Error("err.min_points");
        addAndReport(cmd, std::make_unique<PolylineEntity>(std::move(pts), closed), "msg.pline_created");
    } else if (cmd == "CIRCLE") {
        need(a, 2, cmd);
        const Vec3 c = parsePoint(a[0]);
        const double r = parseNumber(a[1]);
        if (r <= 0) throw Error("err.radius_positive");
        addAndReport(cmd, std::make_unique<CircleEntity>(c, r), "msg.circle_created");
    } else if (cmd == "ARC") {
        need(a, 3, cmd);
        const Vec3 p1 = parsePoint(a[0]), p2 = parsePoint(a[1]), p3 = parsePoint(a[2]);
        addAndReport(cmd, std::make_unique<ArcEntity>(ArcEntity::fromThreePoints(p1, p2, p3)), "msg.arc_created");
    } else if (cmd == "POINT") {
        need(a, 1, cmd);
        std::vector<std::unique_ptr<IEntity>> es;
        for (const auto& t : a) es.push_back(std::make_unique<PointEntity>(parsePoint(t)));
        addMany("POINT", std::move(es));
        say("msg.points_created", {std::to_string(last_.size())});
    } else if (cmd == "TEXT") {
        need(a, 3, cmd);
        const Vec3 p = parsePoint(a[0]);
        const double h = parseNumber(a[1]);
        const double rot = a.size() > 3 ? deg2rad(parseNumber(a[3])) : 0.0;
        addAndReport(cmd, std::make_unique<TextEntity>(p, h, a[2], rot), "msg.text_created");
    } else if (cmd == "LAYER") {
        if (a.empty()) {
            for (const auto& [n, l] : doc_.layers()) {
                std::string flags;
                if (!l.visible) flags += loc_.tr("layer.off") + " ";
                if (l.locked) flags += loc_.tr("layer.locked") + " ";
                if (l.frozen) flags += loc_.tr("layer.frozen") + " ";
                if (n == layer_) flags += loc_.tr("layer.current");
                out_ << std::format("  {:<20} {}={:<3} {}\n", n, loc_.tr("layer.color"), l.color, flags);
            }
            return;
        }
        Layer l = doc_.layer(a[0]).value_or(Layer{a[0]});
        for (std::size_t i = 1; i < a.size(); ++i) {
            const std::string kw = loc_.resolveKeyword(a[i]);
            if (kw == "ON") l.visible = true;
            else if (kw == "OFF") l.visible = false;
            else if (kw == "LOCK") l.locked = true;
            else if (kw == "UNLOCK") l.locked = false;
            else if (kw == "FREEZE") l.frozen = true;
            else if (kw == "THAW") l.frozen = false;
            else l.color = static_cast<int>(parseNumber(a[i]));
        }
        if (l.color < 1 || l.color > 255) throw Error("err.aci_range");
        stack_.execute(std::make_unique<SetLayerCommand>(l));
        say("msg.done", {loc_.commandName(cmd)});
    } else if (cmd == "CLAYER") {
        need(a, 1, cmd);
        if (!doc_.layer(a[0])) stack_.execute(std::make_unique<SetLayerCommand>(Layer{a[0]}));
        layer_ = a[0];
        say("msg.layer_current", {layer_});
    } else if (cmd == "COLOR") {
        need(a, 1, cmd);
        if (loc_.isKeyword(a[0], "BYLAYER")) {
            color_ = kColorByLayer;
        } else {
            const int c = static_cast<int>(parseNumber(a[0]));
            if (c < 1 || c > 255) throw Error("err.aci_range");
            color_ = c;
        }
        say("msg.color_current", {color_ == kColorByLayer ? loc_.keywordName("BYLAYER") : std::to_string(color_)});
    } else if (cmd == "GRIP") {
        need(a, 3, cmd);
        const auto id = static_cast<EntityId>(parseNumber(a[0]));
        const IEntity* e = doc_.find(id);
        if (!e) throw Error("core.not_found", {a[0]});
        const double idx = parseNumber(a[1]);
        if (idx < 0 || idx != std::floor(idx)) throw Error("core.grip_index", {a[1], std::to_string(gripPoints(*e).size())});
        auto repl = withGripMoved(*e, static_cast<std::size_t>(idx), parsePoint(a[2]));
        stack_.execute(std::make_unique<ReplaceEntityCommand>(id, std::move(repl)));
        last_ = {id};
        say("msg.grip_moved", {idStr(id), a[1]});
    } else if (cmd == "OFFSET") {
        need(a, 3, cmd);
        const double d = parseNumber(a[0]);
        if (d <= 0) throw Error("err.distance_positive");
        const auto ids = parseSelection(a[1]);
        if (ids.empty()) throw Error("core.no_selection");
        const Vec3 side = parsePoint(a[2]);
        auto comp = std::make_unique<CompositeCommand>(cmd);
        std::vector<AddEntityCommand*> adds;
        for (EntityId id : ids) {
            const IEntity* e = doc_.find(id);
            if (!e) throw Error("core.not_found", {idStr(id)});
            auto c = std::make_unique<AddEntityCommand>(offsetEntity(*e, d, side), cmd);
            adds.push_back(c.get());
            comp->add(std::move(c));
        }
        stack_.execute(std::move(comp));
        last_.clear();
        for (auto* c : adds) last_.push_back(c->entityId());
        say("msg.offset_created", {std::to_string(last_.size()), idStr(last_.front())});
    } else if (cmd == "TRIM" || cmd == "EXTEND") {
        need(a, 2, cmd);
        const auto edgeIds = parseSelection(a[0]);
        for (std::size_t i = 1; i < a.size(); ++i) {
            const Vec3 p = parsePoint(a[i]);
            const EntityId id = pickEntity(doc_, p, aperture_);
            const IEntity* obj = doc_.find(id);
            if (!obj) throw Error("err.pick_none", {a[i]});
            std::vector<geom::Prim> edges;
            for (EntityId eid : edgeIds) {
                if (eid == id) continue;
                if (const IEntity* ed = doc_.find(eid); ed && doc_.isVisible(eid)) {
                    auto pr = geom::primitivesOf(*ed);
                    edges.insert(edges.end(), pr.begin(), pr.end());
                }
            }
            if (cmd == "EXTEND") {
                stack_.execute(std::make_unique<ReplaceEntityCommand>(id, extendEntity(*obj, edges, p), cmd));
                last_ = {id};
                say("msg.extend_done", {idStr(id)});
                continue;
            }
            auto pieces = trimEntity(*obj, edges, p);
            auto comp = std::make_unique<CompositeCommand>(cmd);
            if (pieces.empty()) {
                comp->add(std::make_unique<DeleteCommand>(std::vector<EntityId>{id}));
            } else {
                comp->add(std::make_unique<ReplaceEntityCommand>(id, std::move(pieces.front()), cmd));
                for (std::size_t k = 1; k < pieces.size(); ++k) comp->add(std::make_unique<AddEntityCommand>(std::move(pieces[k]), cmd));
            }
            const std::size_t n = pieces.size();
            stack_.execute(std::move(comp));
            last_ = n ? std::vector<EntityId>{id} : std::vector<EntityId>{};
            say("msg.trim_done", {idStr(id), std::to_string(n)});
        }
    } else if (cmd == "FILLET") {
        need(a, 2, cmd);
        if (a.size() > 2) {
            const double r = parseNumber(a[2]);
            if (r < 0) throw Error("err.fillet_radius");
            filletR_ = r;
        }
        const Vec3 p1 = parsePoint(a[0]), p2 = parsePoint(a[1]);
        const EntityId i1 = pickEntity(doc_, p1, aperture_), i2 = pickEntity(doc_, p2, aperture_);
        auto* l1 = dynamic_cast<const LineEntity*>(doc_.find(i1));
        auto* l2 = dynamic_cast<const LineEntity*>(doc_.find(i2));
        if (!l1 || !l2 || i1 == i2) throw Error("err.fillet_lines");
        auto res = filletLines(*l1, p1, *l2, p2, filletR_);
        auto comp = std::make_unique<CompositeCommand>(cmd);
        comp->add(std::make_unique<ReplaceEntityCommand>(i1, std::move(res.first), cmd));
        comp->add(std::make_unique<ReplaceEntityCommand>(i2, std::move(res.second), cmd));
        AddEntityCommand* arc = nullptr;
        if (res.arc) {
            auto c = std::make_unique<AddEntityCommand>(std::move(res.arc), cmd);
            arc = c.get();
            comp->add(std::move(c));
        }
        stack_.execute(std::move(comp));
        last_ = {i1, i2};
        if (arc) last_.push_back(arc->entityId());
        say("msg.fillet_done", {idStr(i1), idStr(i2), f4(filletR_)});
    } else if (cmd == "PIPES") {
        const auto& cat = civil::PipeCatalog::builtin();
        for (const auto& sp : cat.specs()) {
            if (!a.empty() && a[0] != civil::materialKey(sp.material)) continue;
            out_ << std::format("  {:<18} {:<24} DN{:<5} {:>6.1f} x {:<5.1f} mm  {:>7.2f} kg/m  n={:.3f}{}\n", sp.id, sp.standard, sp.dn,
                                sp.od, sp.wall, sp.mass(), sp.manningN, sp.verified ? "" : "  *");
        }
        say("sewer.msg.pipes_note");
    } else if (cmd == "SEWER") {
        need(a, 1, cmd);
        std::ifstream f(a[0], std::ios::binary);
        if (!f) throw Error("acad.err.open", {a[0]});
        std::stringstream buf;
        buf << f.rdbuf();
        civil::SewerNetwork net;
        try {
            net = civil::fromJson(buf.str());
        } catch (const json::ParseError& ex) {
            throw Error("sewer.err.format", {ex.what()});
        }
        const auto res = civil::compute(net);
        // natijalar va tekshiruvlar
        for (std::size_t i = 0; i < res.segments.size(); ++i) {
            const auto& sg = res.segments[i];
            if (!sg.spec) continue;
            say("sewer.msg.segment", {std::to_string(i + 1), net.manholes[i].name, net.manholes[i + 1].name, sg.spec->label(),
                                      f4(sg.length), std::format("{:.4f}", sg.slope), std::format("{:.2f}", sg.invUp),
                                      std::format("{:.2f}", sg.invDown), std::format("{:.1f}", sg.qFull),
                                      std::format("{:.2f}", sg.vFull)});
        }
        for (const auto& is : res.issues) {
            const char* lv = is.level == civil::Issue::Error ? "sewer.lvl.error" : is.level == civil::Issue::Warning ? "sewer.lvl.warning" : "sewer.lvl.info";
            out_ << "  " << loc_.tr(lv) << ": " << loc_.tr(is.key, is.args) << '\n';
        }
        // chizish: plan + (ixtiyoriy) profil
        std::vector<std::unique_ptr<IEntity>> es = civil::drawPlan(net, res);
        if (a.size() < 2 || a[1] != "-") {
            Vec3 o;
            if (a.size() > 1) o = parsePoint(a[1]);
            else {
                BBox b = doc_.extents(true);
                for (const auto& e : es) b.expand(e->bbox());
                o = {b.max.x + 20 * net.textHeight + 26 * net.textHeight, b.min.y, 0};
            }
            civil::ProfileLabels lb{loc_.tr("sewer.prof.ground"), loc_.tr("sewer.prof.invert"), loc_.tr("sewer.prof.depth"),
                                    loc_.tr("sewer.prof.slope"), loc_.tr("sewer.prof.distance"), loc_.tr("sewer.prof.pipe"),
                                    loc_.tr("sewer.prof.manhole"), loc_.tr("sewer.prof.datum")};
            auto pr = civil::drawProfile(net, res, o, lb);
            for (auto& e : pr) es.push_back(std::move(e));
        }
        auto comp = std::make_unique<CompositeCommand>(cmd);
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
        say("sewer.msg.summary", {net.name, std::to_string(net.manholes.size()), f4(res.totalLength), std::to_string(res.errors()),
                                  std::to_string(res.warnings()), std::to_string(last_.size())});
    } else if (cmd == "DIMLINEAR" || cmd == "DIMALIGNED") {
        need(a, 3, cmd);
        const Vec3 p1 = parsePoint(a[0]), p2 = parsePoint(a[1]), loc = parsePoint(a[2]);
        const double h = a.size() > 3 ? parseNumber(a[3]) : dimHeight_;
        const int dec = a.size() > 4 ? static_cast<int>(parseNumber(a[4])) : -1;
        if (h <= 0) throw Error("err.height_positive");
        auto d = cmd == "DIMLINEAR" ? std::make_unique<DimensionEntity>(DimensionEntity::linear(p1, p2, loc, h, dec))
                                    : std::make_unique<DimensionEntity>(DimKind::Aligned, p1, p2, loc, h, dec);
        addDimension(cmd, std::move(d));
    } else if (cmd == "DIMRADIUS" || cmd == "DIMDIAMETER") {
        need(a, 2, cmd);
        Vec3 c;
        double r = 0;
        bool found = false;
        for (EntityId id : parseSelection(a[0])) {
            const IEntity* e = doc_.find(id);
            if (auto* ci = dynamic_cast<const CircleEntity*>(e)) { c = ci->center(); r = ci->radius(); found = true; break; }
            if (auto* ar = dynamic_cast<const ArcEntity*>(e)) { c = ar->center(); r = ar->radius(); found = true; break; }
        }
        if (!found) throw Error("err.dim_need_circle");
        const Vec3 loc = parsePoint(a[1]);
        const double h = a.size() > 2 ? parseNumber(a[2]) : dimHeight_;
        const int dec = a.size() > 3 ? static_cast<int>(parseNumber(a[3])) : -1;
        if (h <= 0) throw Error("err.height_positive");
        Vec3 dir{1, 0, 0};
        if (const Vec3 v = loc - c; v.length2d() > 1e-12) dir = Vec3{v.x, v.y, 0} / v.length2d();
        auto d = cmd == "DIMRADIUS" ? std::make_unique<DimensionEntity>(DimKind::Radius, c, c + dir * r, loc, h, dec)
                                    : std::make_unique<DimensionEntity>(DimKind::Diameter, c - dir * r, c + dir * r, loc, h, dec);
        addDimension(cmd, std::move(d));
    } else if (cmd == "MOVE") {
        need(a, 2, cmd);
        Vec3 d = parsePoint(a[1]);
        if (a.size() > 2) d = parsePoint(a[2]) - d;
        stack_.execute(std::make_unique<TransformCommand>(parseSelection(a[0]), Transform::translation(d), cmd));
        say("msg.done", {loc_.commandName(cmd)});
    } else if (cmd == "COPY") {
        need(a, 2, cmd);
        const Vec3 d = parsePoint(a[1]);
        const int n = a.size() > 2 ? static_cast<int>(parseNumber(a[2])) : 1;
        if (n < 1) throw Error("err.count_min");
        std::vector<Transform> pl;
        for (int i = 1; i <= n; ++i) pl.push_back(Transform::translation(d * i));
        auto& c = static_cast<CopyCommand&>(stack_.execute(std::make_unique<CopyCommand>(parseSelection(a[0]), pl)));
        last_ = c.createdIds();
        say("msg.copies_created", {std::to_string(last_.size())});
    } else if (cmd == "ROTATE") {
        need(a, 3, cmd);
        const Vec3 base = parsePoint(a[1]);
        stack_.execute(std::make_unique<TransformCommand>(
            parseSelection(a[0]), Transform::rotation(base, deg2rad(parseNumber(a[2]))), cmd));
        say("msg.done", {loc_.commandName(cmd)});
    } else if (cmd == "SCALE") {
        need(a, 3, cmd);
        const double s = parseNumber(a[2]);
        if (s <= 0) throw Error("err.factor_positive");
        stack_.execute(std::make_unique<TransformCommand>(parseSelection(a[0]), Transform::scaling(parsePoint(a[1]), s), cmd));
        say("msg.done", {loc_.commandName(cmd)});
    } else if (cmd == "MIRROR") {
        need(a, 3, cmd);
        const auto sel = parseSelection(a[0]);
        const Vec3 p1 = parsePoint(a[1]), p2 = parsePoint(a[2]);
        const Transform t = Transform::mirror(p1, p2);
        if (a.size() > 3 && loc_.isKeyword(a[3], "COPY")) {
            auto& c = static_cast<CopyCommand&>(stack_.execute(std::make_unique<CopyCommand>(sel, std::vector{t})));
            last_ = c.createdIds();
            say("msg.copies_created", {std::to_string(last_.size())});
        } else {
            stack_.execute(std::make_unique<TransformCommand>(sel, t, cmd));
            say("msg.done", {loc_.commandName(cmd)});
        }
    } else if (cmd == "ERASE") {
        need(a, 1, cmd);
        const auto sel = parseSelection(a[0]);
        stack_.execute(std::make_unique<DeleteCommand>(sel));
        say("msg.erased", {std::to_string(sel.size())});
    } else if (cmd == "CHPROP") {
        need(a, 2, cmd);
        std::optional<std::string> layer;
        std::optional<int> color;
        for (std::size_t i = 1; i < a.size(); ++i) {
            const auto eq = a[i].find('=');
            if (eq == std::string::npos) throw Error("err.key_value", {a[i]});
            const std::string k = loc_.resolveKeyword(a[i].substr(0, eq));
            const std::string v = a[i].substr(eq + 1);
            if (k == "LAYER") layer = v;
            else if (k == "COLOR") color = loc_.isKeyword(v, "BYLAYER") ? kColorByLayer : static_cast<int>(parseNumber(v));
            else throw Error("err.unknown_property", {a[i].substr(0, eq)});
        }
        stack_.execute(std::make_unique<ChangePropsCommand>(parseSelection(a[0]), layer, color));
        say("msg.done", {loc_.commandName(cmd)});
    } else if (cmd == "UNDO" || cmd == "REDO") {
        const bool isUndo = cmd == "UNDO";
        const int n = a.empty() ? 1 : static_cast<int>(parseNumber(a[0]));
        int done = 0;
        for (; done < n; ++done) {
            // Command::name() kanonik ("LINE"); foydalanuvchiga mahalliy nom ko'rsatamiz
            const std::string nm = isUndo ? stack_.undoName() : stack_.redoName();
            if (!(isUndo ? stack_.undo() : stack_.redo())) break;
            say(isUndo ? "msg.undo" : "msg.redo", {loc_.commandName(nm)});
        }
        if (done == 0) say(isUndo ? "msg.nothing_to_undo" : "msg.nothing_to_redo");
    } else if (cmd == "LIST") {
        const auto sel = a.empty() ? doc_.ids() : parseSelection(a[0]);
        for (EntityId id : sel) {
            const IEntity* e = doc_.find(id);
            if (!e) { out_ << "  "; say("msg.id_not_found", {idStr(id)}); continue; }
            out_ << std::format("  #{:<5} [{}] {}\n", id, e->layer(), e->describe());
        }
        say("msg.list_total", {std::to_string(sel.size())});
    } else if (cmd == "AREA") {
        need(a, 1, cmd);
        double area = 0, per = 0;
        for (EntityId id : parseSelection(a[0])) {
            const IEntity* e = doc_.find(id);
            if (!e) throw Error("core.not_found", {idStr(id)});
            if (auto* p = dynamic_cast<const PolylineEntity*>(e)) {
                if (!p->isClosed()) throw Error("err.not_closed", {idStr(id)});
                area += p->area();
                per += p->length2d();
            } else if (auto* c = dynamic_cast<const CircleEntity*>(e)) {
                area += c->area();
                per += c->length();
            } else if (auto* h = dynamic_cast<const HatchEntity*>(e)) {
                area += h->area();
                for (const auto& l : h->loops()) per += l.length2d();
            } else {
                throw Error("err.no_area", {idStr(id)});
            }
        }
        say("msg.area", {f4(area), f4(per)});
    } else if (cmd == "DIST") {
        need(a, 2, cmd);
        const Vec3 p = parsePoint(a[0]), q = parsePoint(a[1]);
        const Vec3 d = q - p;
        double az = rad2deg(std::atan2(d.x, d.y));  // geodezik azimut: shimoldan soat yo'nalishida
        if (az < 0) az += 360;
        say("msg.dist", {f4(d.length()), f4(d.length2d()), f4(d.x), f4(d.y), f4(d.z), f4(az)});
    } else if (cmd == "EXTENTS") {
        const BBox b = doc_.extents();
        if (!b.valid()) { say("msg.doc_empty"); return; }
        say("msg.extents", {f4(b.min.x), f4(b.min.y), f4(b.max.x), f4(b.max.y)});
    } else if (cmd == "SAVE") {
        std::string path = a.empty() ? currentFile_ : a[0];
        if (path.empty()) throw Error("err.no_filename");
        const std::string ext = extOf(path);
        if (ext == "dxf") {  // eksport: joriy fayl va "saqlangan" holati o'zgarmaydi
            if (!saveDxf(doc_, path)) throw Error("err.write_failed", {path});
            say("msg.exported", {path});
            return;
        }
        if (ext.empty()) path += ".acad";
        saveAcad(doc_, path);
        currentFile_ = path;
        stack_.markClean();
        say("msg.saved", {path});
    } else if (cmd == "OSNAP") {
        need(a, 1, cmd);
        const Vec3 from = lastPt_;
        const Vec3 cursor = parsePoint(a[0]);
        unsigned modes = 0;
        for (std::size_t i = 1; i < a.size(); ++i) {
            const SnapMode m = snapFromName(loc_.resolveKeyword(a[i]));
            if (m) modes |= m;
            else {
                const double r = parseNumber(a[i]);
                if (r <= 0) throw Error("err.factor_positive");
                aperture_ = r;  // keyingi END:x,y kiritishlar uchun ham eslab qolinadi
            }
        }
        if (!modes) modes = SnapAll;
        const auto res = findSnaps(doc_, cursor, aperture_, modes, &from);
        if (res.empty()) { say("msg.snap_none", {f4(cursor.x) + ", " + f4(cursor.y), f4(aperture_)}); return; }
        for (std::size_t i = 0; i < res.size() && i < 12; ++i) {
            const auto& r = res[i];
            say("msg.snap_item", {loc_.keywordName(std::string(snapName(r.mode))), f4(r.point.x), f4(r.point.y), f4(r.point.z),
                                  idStr(r.id), f4(r.distance)});
        }
    } else if (cmd == "SELECT") {
        need(a, 1, cmd);
        const auto sel = parseSelection(a[0]);
        std::string list;
        for (std::size_t i = 0; i < sel.size() && i < 50; ++i) list += (i ? "," : "") + idStr(sel[i]);
        if (sel.size() > 50) list += ",…";
        last_ = sel;
        say("msg.selected", {std::to_string(sel.size()), list});
    } else if (cmd == "BLOCK") {
        if (a.empty()) {
            if (doc_.blocks().empty()) { say("msg.no_blocks"); return; }
            for (const auto& [k, b] : doc_.blocks())
                say("msg.block_item", {b->name, std::to_string(b->entities.size()), std::to_string(doc_.blockUsage(b->name))});
            return;
        }
        need(a, 3, cmd);
        const std::string& name = a[0];
        if (name.empty() || name.front() == '*' || name.find_first_of("<>/\\\":;?*|=`") != std::string::npos)
            throw Error("err.block_name", {name});
        if (doc_.block(name)) throw Error("err.block_exists", {name});
        const Vec3 base = parsePoint(a[1]);
        const auto sel = parseSelection(a[2]);
        auto def = std::make_shared<BlockDef>();
        def->name = name;
        def->base = base;
        for (EntityId id : sel)
            if (const IEntity* e = doc_.find(id)) def->entities.push_back(e->clone());
        auto ins = std::make_unique<InsertEntity>(def, Transform{});  // blok WCS'da: joylashuv — birlik matritsa
        ins->setLayer(layer_);
        auto comp = std::make_unique<CompositeCommand>("BLOCK");
        comp->add(std::make_unique<AddBlockCommand>(def));
        comp->add(std::make_unique<DeleteCommand>(sel));
        auto add = std::make_unique<AddEntityCommand>(std::move(ins), "BLOCK");
        auto* addPtr = add.get();
        comp->add(std::move(add));
        stack_.execute(std::move(comp));
        last_ = {addPtr->entityId()};
        say("msg.block_created", {name, std::to_string(def->entities.size()), idStr(addPtr->entityId())});
    } else if (cmd == "INSERT") {
        need(a, 2, cmd);
        const BlockPtr def = doc_.block(a[0]);
        if (!def) throw Error("err.block_missing", {a[0]});
        const Vec3 p = parsePoint(a[1]);
        const double k = a.size() > 2 ? parseNumber(a[2]) : 1.0;
        if (k == 0) throw Error("err.factor_positive");
        const double rot = a.size() > 3 ? deg2rad(parseNumber(a[3])) : 0.0;
        addAndReport(cmd, std::make_unique<InsertEntity>(def, InsertEntity::placement(def->base, p, k, k, k, rot)),
                     "msg.insert_created");
    } else if (cmd == "EXPLODE") {
        need(a, 1, cmd);
        std::vector<EntityId> done;
        std::vector<std::unique_ptr<IEntity>> parts;
        for (EntityId id : parseSelection(a[0])) {
            const IEntity* e = doc_.find(id);
            if (!e) throw Error("core.not_found", {idStr(id)});
            std::vector<std::unique_ptr<IEntity>> ps;
            if (auto* in = dynamic_cast<const InsertEntity*>(e)) ps = in->explode();
            else if (auto* dm = dynamic_cast<const DimensionEntity*>(e)) ps = dm->explode();
            else if (auto* pl = dynamic_cast<const PolylineEntity*>(e)) ps = pl->explodeSegments();
            else if (auto* h = dynamic_cast<const HatchEntity*>(e))
                for (const auto& l : h->loops()) {
                    auto c = std::make_unique<PolylineEntity>(l);
                    c->setLayer(h->layer());
                    c->setColor(h->color());
                    ps.push_back(std::move(c));
                }
            else continue;
            done.push_back(id);
            for (auto& x : ps) parts.push_back(std::move(x));
        }
        if (done.empty()) throw Error("err.nothing_to_explode");
        const std::size_t nParts = parts.size();
        auto comp = std::make_unique<CompositeCommand>("EXPLODE");
        comp->add(std::make_unique<DeleteCommand>(done));
        std::vector<AddEntityCommand*> adds;
        for (auto& x : parts) {
            auto c = std::make_unique<AddEntityCommand>(std::move(x), "EXPLODE");
            adds.push_back(c.get());
            comp->add(std::move(c));
        }
        stack_.execute(std::move(comp));
        last_.clear();
        for (auto* c : adds) last_.push_back(c->entityId());
        say("msg.exploded", {std::to_string(done.size()), std::to_string(nParts)});
    } else if (cmd == "HATCH") {
        need(a, 1, cmd);
        std::vector<PolylineEntity> loops;
        for (EntityId id : parseSelection(a[0])) {
            const IEntity* e = doc_.find(id);
            if (!e) throw Error("core.not_found", {idStr(id)});
            const auto* pl = dynamic_cast<const PolylineEntity*>(e);
            if (e->type() == EntityType::Circle) {
                loops.push_back(*toBulgePolyline(*e));
            } else if (pl && pl->isClosed() && pl->vertices().size() >= 3) {
                loops.push_back(*pl);
            } else {
                throw Error("err.hatch_boundary", {idStr(id)});
            }
        }
        if (loops.empty()) throw Error("core.no_selection");
        const std::string pattern = a.size() > 1 ? a[1] : std::string("SOLID");
        const double k = a.size() > 2 ? parseNumber(a[2]) : 1.0;
        if (k <= 0) throw Error("err.factor_positive");
        const double ang = a.size() > 3 ? deg2rad(parseNumber(a[3])) : 0.0;
        auto h = std::make_unique<HatchEntity>(std::move(loops), pattern, k, ang);
        const std::string area = f4(h->area()), n = std::to_string(h->loops().size());
        h->setLayer(layer_);
        h->setColor(color_);
        auto& c = static_cast<AddEntityCommand&>(stack_.execute(std::make_unique<AddEntityCommand>(std::move(h), cmd)));
        last_ = {c.entityId()};
        say("msg.hatch_created", {idStr(c.entityId()), n, area});
    } else if (cmd == "NEW") {
        if (stack_.isDirty() && !forced(a)) throw Error("err.unsaved", {loc_.commandName(cmd)});
        doc_ = Document();
        stack_.clear();
        currentFile_.clear();
        layer_ = "0";
        color_ = kColorByLayer;
        last_.clear();
        say("msg.new_doc");
    } else if (cmd == "OPEN") {
        need(a, 1, cmd);
        if (stack_.isDirty() && !forced(a)) throw Error("err.unsaved", {loc_.commandName(cmd)});
        const std::string& path = a[0];
        std::vector<Message> warnings;
        Document loaded;
        if (extOf(path) == "dxf") {  // DXF — yangi chizma sifatida (joriy fayl nomi berilmaydi)
            DxfImportResult res = readDxfFile(path);
            for (const auto& l : res.layers)
                if (!loaded.setLayer(l)) loaded.addLayer(l);
            for (auto& e : res.entities) loaded.add(std::move(e));
            warnings = std::move(res.warnings);
            currentFile_.clear();
        } else {
            AcadLoadResult res = loadAcad(path);
            loaded = std::move(res.doc);
            warnings = std::move(res.warnings);
            currentFile_ = path;
        }
        doc_ = std::move(loaded);
        stack_.clear();
        if (!doc_.layer(layer_)) layer_ = "0";
        color_ = kColorByLayer;
        last_.clear();
        say("msg.opened", {path, std::to_string(doc_.size()), std::to_string(doc_.layers().size())});
        for (const auto& w : warnings) out_ << "  ! " << loc_.tr(w.key, w.args) << '\n';
    } else if (cmd == "IMPORT") {
        need(a, 1, cmd);
        DxfImportResult res = readDxfFile(a[0]);
        const std::size_t n = res.entities.size();
        std::size_t newLayers = 0;
        stack_.execute(makeImportCommand(doc_, res, &newLayers));
        const auto all = doc_.ids();
        last_.assign(all.end() - static_cast<std::ptrdiff_t>(std::min(n, all.size())), all.end());
        say("msg.import_done", {std::to_string(n), std::to_string(newLayers), a[0], res.release, res.encoding});
        auto join = [](const std::map<std::string, std::size_t>& m) {
            std::string s;
            for (const auto& [k, v] : m) s += (s.empty() ? "" : ", ") + k + "×" + std::to_string(v);
            return s;
        };
        if (!res.imported.empty()) say("msg.import_types", {join(res.imported)});
        if (!res.skipped.empty()) say("msg.import_skipped", {join(res.skipped)});
        if (res.insUnits != 0) say("msg.import_units", {std::string(insUnitsName(res.insUnits))});
        for (const auto& w : res.warnings) out_ << "  ! " << loc_.tr(w.key, w.args) << '\n';
    } else if (cmd == "LANG") {
        if (!a.empty()) {
            std::string code = a[0];
            for (auto& ch : code) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            if (!loc_.setLanguage(code)) throw Error("err.unknown_lang", {a[0]});
        }
        say("msg.lang_current", {loc_.languageName(loc_.language()), loc_.language()});
        std::string all;
        for (const auto& c : loc_.languages()) all += (all.empty() ? "" : ", ") + c + " (" + loc_.languageName(c) + ")";
        say("msg.lang_available", {all});
    } else if (cmd == "HELP") {
        help();
    } else {
        throw Error("err.unknown_command", {cmd});
    }
}

void Interpreter::help() {
    say("help.title");
    for (const auto& c : loc_.canonicalCommands()) {
        const std::string name = loc_.commandName(c);
        // Kenglikni UTF-8 belgilar soni bo'yicha tekislash (kirill 2 bayt)
        const auto pad = [](const std::string& s, std::size_t w) {
            const std::size_t n = utf8::length(s);
            return s + std::string(n < w ? w - n : 1, ' ');
        };
        std::string line = "  " + pad(name, 15);
        if (name != c) line += pad("(" + c + ")", 10);
        else line += std::string(10, ' ');
        line += pad(localizeSyntax(loc_, commandSyntax(c)), 34);
        line += loc_.tr("desc." + c);
        out_ << line << '\n';
    }
    say("help.legend", {loc_.keywordName("ALL"), loc_.keywordName("LAST"), loc_.keywordName("LAYER"), loc_.keywordName("END"),
                        loc_.keywordName("WINDOW"), loc_.keywordName("CROSSING"), loc_.keywordName("PICK")});
}

} // namespace asiacad::cli
