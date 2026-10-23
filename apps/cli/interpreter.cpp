#include "interpreter.h"

#include <asiacad/bim/building.h>
#include <asiacad/bim/kj_project.h>
#include <asiacad/civil/drawset.h>
#include <asiacad/bim/rebar.h>
#include <asiacad/civil/excel.h>
#include <asiacad/civil/road.h>
#include <asiacad/civil/sewer.h>
#include <asiacad/civil/utility.h>
#include <asiacad/civil/waterway.h>
#include <asiacad/civil/corridor.h>
#include <asiacad/civil/generators.h>
#include <asiacad/civil/drive.h>
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
#include <optional>
#include <set>
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

std::vector<std::string> tokenizeLine(const std::string& line) { return tokenize(line); }

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
        {"BUILDING", "[n] [H] [t] [x,y] | file.json [x,y]"},
        {"REBAR", "[file.json] [x,y]"},
        {"PIPES", "[PVC|PP|PE|AC|STEEL|DI|RC]"},
        {"EXCEL", "SEWER|BUILDING|REBAR|UTIL|ROAD|WATERWAY|CORRIDOR in out"},
        {"UTIL", "[file.json] [+]"},
        {"KJ", "file.dxf [fTop|-] [fT] [abs±0.000]"},
        {"ROAD", "[file.json] [+] [x,y]"},
        {"WATERWAY", "[file.json] [Q=m3/s] [SEC=n] [+] [x,y]"},
        {"CORRIDOR", "[file.json] [ASM=n] [+]"},
        {"GRID", "dx dy [b×h] [x,y] | file.json"},
        {"ALIGN", "file.json"},
        {"UTILNET", "file.json"},
        {"CITY", "[nx ny] [floors min-max] [x,y] | file.json"},
        {"DRIVE", "sel [V=km/h] [EYE=m] [OBJ=m] [STEP=m]"},
        {"RECTANG", "p1 p2 [R=r] [C=c]"},
        {"POLYGON", "n p r|p [I|C] [°]"},
        {"ELLIPSE", "p a|p b [°]"},
        {"SPLINE", "p1 p2 ... [{CLOSE}]"},
        {"STRETCH", "p1 p2 base target"},
        {"ARRAY", "sel R rows cols dx dy | sel P p n [°] [NOROT] | sel PATH id n|S=d [NOALIGN]"},
        {"CHAMFER", "p1 p2 d1 [d2]"},
        {"JOIN", "sel"},
        {"BREAK", "id p1 [p2]"},
        {"LENGTHEN", "id p ±d"},
        {"MATCHPROP", "src sel"},
        {"LAYMCUR", "id"},
        {"LAYMATCH", "sel id"},
        {"LAYOFF", "sel"},
        {"LAYFRZ", "sel"},
        {"LAYLCK", "sel"},
        {"LAYULK", "sel"},
        {"LAYON", ""},
        {"LAYISO", "sel"},
        {"LAYUNISO", ""},
        {"LEADER", "p1 p2 [p3 ...] \"...\" [h]"},
        {"DIMANGULAR", "vertex p1 p2 p [h] [DMS]"},
        {"ID", "p"},
        {"SYMBOL", "[name p[;p...] [k] [°]]"},
        {"STEEL", "[name]"},
        {"STEELSEC", "name p [k]"},
        {"STEELBEAM", "name p1 p2 [p3 ...]"},
        {"STEELSPEC", "[p] [%]"},
        {"STEELFRAME", "file.json | key=value ... [x,y[,z]]"},
        {"STAIR", "file.json | key=value ... [x,y[,z]]"},
        {"RAMP", "file.json | key=value ... [x,y[,z]]"},
        {"RWALL", "file.json | key=value ... [x,y[,z]]"},
        {"GEOPLACE", "sel p1 P1 [p2 P2] [GEO] [SCALE] [Z=H]"},
        {"PTIMPORT", "file.txt|csv|xlsx [XY] [H=h]"},
        {"PTEXPORT", "sel file.txt|csv|xlsx [XY]"},
        {"DEVIATION", "PLAN|HEIGHT|VERT|AXIS|POINT pts ref|- [TOL=mm] [MAX=m] [R=m] [Z=m] [K=n] [H=h] [A=m] [XY] [NODRAW] [XLSX=f] [CSV=f]"},
        {"DEVCLEAR", ""},
        {"TIN", "sel [MAXEDGE=m] [BOUND=id]"},
        {"CONTOUR", "sel [STEP=0.5] [MAJOR=2.5] [TH=h] [MAXEDGE=m] [BOUND=id]"},
        {"PROFILE", "path EG=sel [FG=sel | Z=z [IX=‰] [IY=‰] [BASE=x,y]] [STEP=20] [H=1000] [V=100] [PK0=m] [DATUM=z] [AT=x,y] [XLSX=f]"},
        {"XSECTIONS", "path EG=sel FG=sel|Z=z [STEP=20] [L=20] [R=20] [SAMPLE=0.5] [SCALE=200] [COLS=4] [PK0=m] [AT=x,y] [NOMARKS] [XLSX=f]"},
        {"CARTOGRAM", "EG=sel FG=sel|Z=z|BALANCE [IX=‰] [IY=‰] [BASE=x,y] [CELL=20] [BOUND=id] [ORIGIN=x,y] [TH=h] [NODRAW] [XLSX=f]"},
        {"VOLUME", "EG=sel FG=sel|Z=z|BALANCE [IX=‰] [IY=‰] [BASE=x,y] [BOUND=id] [CELL=m]"},
        {"SETPROP", "sel KEY=value [KEY=value ...]"},
        {"PIPEDRAW", "name p1 p2 [p…] [SYS=K1|K2] [SPEC=id] [SLOPE=‰] [DEPTH=m] [RIM=m]"},
        {"OBJEDIT", "UTIL LINE=n [DN=mm] [ZUP=m] [ZDOWN=m] [DEPTH=m] [KIND=k] | UTIL NODE=n [GROUND=m] [BOTTOM=m] [NAME=t] | ROAD ST=n DESIGN=m | WW ST=n BOTTOM=m"},
        {"PIPEEDIT", "net SEG=n|MH=n KEY=value ..."},
        {"LINKS", "[ON n | OFF n | DEL n | UPDATE [n]]"},
        {"SHEET", "[NEW name [A0..A4] [PORTRAIT]] | VIEW name p1 p2 [SCALE=n] [TITLE=t] | STAMP name KEY=t… | SIZE name A0..A4 [PORTRAIT] | RENAME name new | DEL name [n]"},
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
std::string localizeSyntax(const i18n::Localizer& loc, std::string s) {
    for (std::size_t p; (p = s.find('{')) != std::string::npos;) {
        const auto q = s.find('}', p);
        if (q == std::string::npos) break;
        s.replace(p, q - p + 1, loc.keywordName(s.substr(p + 1, q - p - 1)));
    }
    return s;
}

void Interpreter::say(const std::string& key, const std::vector<std::string>& args) {
    if (quiet_) return;
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

void Interpreter::addLayered(const std::string& name, std::vector<std::unique_ptr<IEntity>> es) {
    auto comp = std::make_unique<CompositeCommand>(name);
    std::set<std::string> made;
    for (const auto& e : es)
        if (!doc_.layer(e->layer()) && made.insert(e->layer()).second) comp->add(std::make_unique<SetLayerCommand>(Layer{e->layer()}));
    std::vector<AddEntityCommand*> adds;
    for (auto& e : es) {
        auto c = std::make_unique<AddEntityCommand>(std::move(e), name);
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
        const std::size_t before = stack_.pushCount();
        dispatch(canon, args);
        // Dinamik bog'lanishlar: manba o'zgargan bo'lsa natija qayta quriladi va shu amal bilan bitta undo qadami bo'ladi
        static const std::set<std::string> skip{"UNDO", "REDO", "OPEN", "NEW", "LINKS", "SAVE", "SAVEAS", "LANG", "HELP", "LIST"};
        if (!doc_.links().empty() && !skip.count(canon)) {
            updateLinks(false, 0, stack_.pushCount() > before);
        }
    } catch (const Error& ex) {
        say("err.prefix", {loc_.tr(ex.key(), ex.args())});
    } catch (const std::exception& ex) {
        say("err.prefix", {ex.what()});
    }
    return true;
}

void Interpreter::dispatch(const std::string& cmd, const Args& a) {
    if (dispatchDraft(cmd, a)) return;
    if (dispatchSurvey(cmd, a)) return;
    if (dispatchTerrain(cmd, a)) return;
    if (dispatchSheet(cmd, a)) return;
    if (dispatchLinks(cmd, a)) return;
    if (dispatchProps(cmd, a)) return;
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
    } else if (cmd == "BUILDING") {
        bim::BuildingParams bp;
        auto lm = bim::autoLayerMap(doc_);
        std::size_t k = 0;
        if (!a.empty() && extOf(a[0]) == "json") {  // GUI: parametrlar + qatlam xaritasi
            std::ifstream f(a[0], std::ios::binary);
            if (!f) throw Error("acad.err.open", {a[0]});
            std::stringstream buf;
            buf << f.rdbuf();
            bim::paramsFromJson(buf.str(), bp, lm);
            k = 1;
        } else {
            if (a.size() > 0 && a[0].find(',') == std::string::npos) bp.floors = static_cast<int>(parseNumber(a[k++]));
            if (a.size() > k && a[k].find(',') == std::string::npos) bp.floorHeight = parseNumber(a[k++]);
            if (a.size() > k && a[k].find(',') == std::string::npos) bp.slabT = parseNumber(a[k++]);
        }
        const auto model = bim::buildModel(bim::readPlan(doc_, lm), bp);
        for (const auto& [ln, kk] : lm)
            if (kk != bim::ElemKind::Ignore) say("bim.msg.layer", {ln, loc_.tr(std::string("bim.kind.") + bim::kindKey(kk))});
        auto levelName = [&](int lv) { return lv == 0 ? loc_.tr("bim.level.found") : loc_.tr("bim.level.n", {std::to_string(lv)}); };
        auto kindName = [&](bim::ElemKind kk) { return loc_.tr(std::string("bim.kind.") + bim::kindKey(kk)); };
        for (const auto& r : model.pto)
            say("bim.msg.row", {levelName(r.level), kindName(r.kind), std::to_string(r.count), std::format("{:.2f}", r.volume),
                                std::format("{:.2f}", r.formwork), r.concrete});
        for (const auto& [c, v] : model.byConcrete) say("bim.msg.concrete", {c, std::format("{:.2f}", v)});
        for (const auto& is : model.issues)
            out_ << "  " << loc_.tr(is.error ? "sewer.lvl.error" : "sewer.lvl.warning") << ": " << loc_.tr(is.key, is.args) << '\n';
        say("bim.msg.total", {std::to_string(bp.floors), std::format("{:.2f}", model.totalVolume), std::format("{:.2f}", model.totalFormwork),
                              std::format("{:.3f}", model.heightTop)});
        if (a.size() > k) {  // PTO jadvalini chizmaga
            const Vec3 o = parsePoint(a[k]);
            bim::PtoLabels L{loc_.tr("bim.tbl.title"), loc_.tr("gui.bim.col.level"), loc_.tr("gui.bim.col.elem"), loc_.tr("gui.bim.col.count"),
                             loc_.tr("gui.bim.col.vol"), loc_.tr("gui.bim.col.form"), loc_.tr("gui.bim.col.class"), loc_.tr("gui.bim.total"),
                             levelName, kindName};
            auto es = bim::drawPtoTable(model, o, 0.25, L);
            auto comp = std::make_unique<CompositeCommand>(cmd);
            if (!doc_.layer("PTO")) comp->add(std::make_unique<SetLayerCommand>(Layer{"PTO"}));
            std::vector<AddEntityCommand*> adds;
            for (auto& e : es) {
                auto c = std::make_unique<AddEntityCommand>(std::move(e), cmd);
                adds.push_back(c.get());
                comp->add(std::move(c));
            }
            stack_.execute(std::move(comp));
            last_.clear();
            for (auto* c : adds) last_.push_back(c->entityId());
            say("bim.msg.table", {std::to_string(last_.size())});
        }
    } else if (cmd == "REBAR") {
        bim::BuildingParams bp;
        auto lm = bim::autoLayerMap(doc_);
        bim::RebarSettings rs = bim::defaultRebarSettings();
        std::size_t k = 0;
        if (!a.empty() && extOf(a[0]) == "json") {
            std::ifstream f(a[0], std::ios::binary);
            if (!f) throw Error("acad.err.open", {a[0]});
            std::stringstream buf;
            buf << f.rdbuf();
            bim::paramsFromJson(buf.str(), bp, lm);
            bim::rebarFromJson(buf.str(), rs);
            k = 1;
        }
        const auto bm = bim::buildModel(bim::readPlan(doc_, lm), bp);
        const auto rm = bim::buildRebar(bm, bp, rs);
        for (const auto& [dia, kg] : rm.massByDia) say("rebar.msg.dia", {dia, std::format("{:.1f}", kg)});
        for (const auto& is : rm.issues)
            out_ << "  " << loc_.tr(is.error ? "sewer.lvl.error" : "sewer.lvl.warning") << ": " << loc_.tr(is.key, is.args) << '\n';
        long nbars = 0;
        for (const auto& r : rm.schedule) nbars += r.count;
        say("rebar.msg.total", {std::format("{:.3f}", rm.totalMass / 1000), std::to_string(nbars), std::format("{:.1f}", rm.wireKg),
                                std::to_string(rm.nodes), std::to_string(rm.chairs), std::to_string(rm.ties)});
        if (a.size() > k) {
            const Vec3 o = parsePoint(a[k]);
            auto levelName = [&](int lv) { return lv == 0 ? loc_.tr("bim.level.found") : loc_.tr("bim.level.n", {std::to_string(lv)}); };
            bim::ScheduleLabels L{loc_.tr("rebar.tbl.title"), loc_.tr("gui.rb.col.pos"), loc_.tr("gui.rb.col.elem"), "\xC3\x98",
                                  loc_.tr("gui.rb.col.cls"), loc_.tr("gui.rb.col.len"), loc_.tr("gui.rb.col.cnt"), loc_.tr("gui.rb.col.mass"),
                                  loc_.tr("gui.bim.total"), loc_.tr("rebar.tbl.wire"), levelName,
                                  [&](bim::ElemKind kk) { return loc_.tr(std::string("bim.kind.") + bim::kindKey(kk)); },
                                  [&](bim::BarRole r) { return loc_.tr(std::string("rebar.role.") + bim::roleKey(r)); }};
            auto es = bim::drawSchedule(rm, o, 0.25, L);
            auto comp = std::make_unique<CompositeCommand>(cmd);
            if (!doc_.layer("ARM_VEDOMOST")) comp->add(std::make_unique<SetLayerCommand>(Layer{"ARM_VEDOMOST"}));
            std::vector<AddEntityCommand*> adds;
            for (auto& e : es) {
                auto c = std::make_unique<AddEntityCommand>(std::move(e), cmd);
                adds.push_back(c.get());
                comp->add(std::move(c));
            }
            stack_.execute(std::move(comp));
            last_.clear();
            for (auto* c : adds) last_.push_back(c->entityId());
            say("rebar.msg.table", {std::to_string(last_.size())});
        }
    } else if (cmd == "EXCEL") {
        // EXCEL <modul> <kirish> <chiqish>: .xlsx chiqishi — eksport, .xlsx kirishi — import (JSON yoziladi)
        need(a, 2, cmd);
        const std::string mod = loc_.resolveCommand(a[0]);
        const excel::Tr tr = [this](const std::string& k, const std::vector<std::string>& v) { return loc_.tr(k, v); };
        auto readAll = [](const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            if (!f) throw Error("acad.err.open", {path});
            std::stringstream b;
            b << f.rdbuf();
            return b.str();
        };
        auto writeAll = [](const std::string& path, const std::string& text) {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f) throw Error("xlsx.err.write", {path});
            f << text;
        };
        const std::string in = a.size() >= 3 ? a[1] : std::string(), out = a.back();
        if (mod == "SEWER") {
            need(a, 3, cmd);
            if (extOf(in) == "xlsx") {
                civil::SewerNetwork n;
                excel::sewerFromWorkbook(xlsx::load(in), n, tr);
                writeAll(out, civil::toJson(n));
                const auto r = civil::compute(n);
                say("xl.msg.imported", {in, std::to_string(n.manholes.size()), std::to_string(r.errors()), std::to_string(r.warnings())});
            } else {
                const auto n = civil::fromJson(readAll(in));
                xlsx::save(excel::sewerWorkbook(n, civil::compute(n), tr), out);
                say("xl.msg.exported", {out});
            }
        } else if (mod == "BUILDING" || mod == "REBAR") {
            bim::BuildingParams bp;
            auto lm = bim::autoLayerMap(doc_);
            bim::RebarSettings rs = bim::defaultRebarSettings();
            if (!in.empty() && extOf(in) == "xlsx") {
                const auto wb = xlsx::load(in);
                if (mod == "REBAR") excel::rebarFromWorkbook(wb, bp, lm, rs, tr);
                else excel::buildingFromWorkbook(wb, bp, lm, tr);
                std::string js = bim::paramsToJson(bp, lm);
                if (mod == "REBAR") js = js.substr(0, js.find_last_of('}')) + ",\"rebar\":" + bim::rebarToJson(rs) + "}";
                writeAll(out, js);
                say("xl.msg.imported", {in, std::to_string(bp.floors), "0", "0"});
            } else {
                if (!in.empty() && extOf(in) == "json") {
                    const std::string text = readAll(in);
                    bim::paramsFromJson(text, bp, lm);
                    bim::rebarFromJson(text, rs);
                }
                const auto bm = bim::buildModel(bim::readPlan(doc_, lm), bp);
                if (mod == "REBAR") xlsx::save(excel::rebarWorkbook(bp, lm, bm, rs, bim::buildRebar(bm, bp, rs), tr), out);
                else xlsx::save(excel::buildingWorkbook(bp, lm, bm, tr), out);
                say("xl.msg.exported", {out});
            }
        } else if (mod == "UTIL") {
            auto p = civil::defaultUtilParams();
            civil::UtilModel m;
            if (!in.empty() && extOf(in) == "json") {
                civil::utilFromJson(readAll(in), m, p);
            } else {
                m = civil::readUtilities(doc_, civil::autoUtilLayers(doc_), p);
            }
            if (!in.empty() && extOf(in) == "xlsx") {
                excel::utilityFromWorkbook(xlsx::load(in), m, p, tr);
                writeAll(out, civil::utilToJson(m, p));
                say("xl.msg.imported", {in, std::to_string(m.lines.size()), std::to_string(m.errors()), "0"});
            } else {
                xlsx::save(excel::utilityWorkbook(m, p, tr), out);
                say("xl.msg.exported", {out});
            }
        } else if (mod == "ROAD") {
            auto p = civil::defaultRoadParams();
            civil::RoadModel m;
            if (!in.empty() && extOf(in) == "json") civil::roadFromJson(readAll(in), m, p);
            else m = civil::buildRoad(doc_, civil::autoRoadLayers(doc_), p);
            if (!in.empty() && extOf(in) == "xlsx") {
                excel::roadFromWorkbook(xlsx::load(in), m, p, tr);
                writeAll(out, civil::roadToJson(m, p));
                say("xl.msg.imported", {in, std::to_string(m.st.size()), std::to_string(m.errors()), "0"});
            } else {
                xlsx::save(excel::roadWorkbook(m, p, tr), out);
                say("xl.msg.exported", {out});
            }
        } else if (mod == "CORRIDOR") {
            auto p = civil::defaultCorridorParams();
            civil::Corridor c;
            if (!in.empty() && extOf(in) == "json") civil::corridorFromJson(readAll(in), c, p);
            else c = civil::buildCorridor(doc_, p);
            if (!in.empty() && extOf(in) == "xlsx") {
                excel::corridorFromWorkbook(xlsx::load(in), c, p, tr);
                writeAll(out, civil::corridorToJson(c, p));
                say("xl.msg.imported", {in, std::to_string(c.sec.size()), std::to_string(c.errors()), "0"});
            } else {
                xlsx::save(excel::corridorWorkbook(c, p, tr), out);
                say("xl.msg.exported", {out});
            }
        } else if (mod == "WATERWAY") {
            auto p = civil::defaultWwParams();
            civil::WwModel m;
            if (!in.empty() && extOf(in) == "json") civil::wwFromJson(readAll(in), m, p);
            else m = civil::buildWaterway(doc_, civil::autoWwLayers(doc_), p);
            if (!in.empty() && extOf(in) == "xlsx") {
                excel::wwFromWorkbook(xlsx::load(in), m, p, tr);
                writeAll(out, civil::wwToJson(m, p));
                say("xl.msg.imported", {in, std::to_string(m.st.size()), std::to_string(m.errors()), "0"});
            } else {
                xlsx::save(excel::wwWorkbook(m, p, tr), out);
                say("xl.msg.exported", {out});
            }
        } else {
            throw Error("xl.err.module", {a[0]});
        }
    } else if (cmd == "KJ") {
        // KJ fayl.dxf [fundament usti] [qalinligi] — bitta KJ fayldan (qolip + armatura) beton va armatura
        need(a, 1, cmd);
        civil::SheetFile sf;
        sf.path = a[0];
        const auto ls = civil::loadSheet(sf);
        if (!ls.error.empty()) throw Error("acad.err.open", {a[0]});
        bim::KjParams kp;
        if (a.size() > 1 && a[1] != "-") { kp.foundTop = parseNumber(a[1]); kp.foundTopAuto = false; }
        if (a.size() > 2) kp.foundT = parseNumber(a[2]);
        if (a.size() > 3) { kp.datum = parseNumber(a[3]); kp.datumAuto = false; }
        const auto r = bim::buildKjProject(*ls.doc, ls.dims, kp);
        int used = 0;
        for (const auto& v : r.views) used += v.kind != bim::KjViewKind::Other;
        say("kj.msg.views", {std::to_string(r.views.size()), std::to_string(used)});
        for (const auto& v : r.views) {
            if (v.kind == bim::KjViewKind::Other && v.title.empty()) continue;
            say("kj.msg.view", {loc_.tr(std::string("kj.view.") + bim::kjViewKey(v.kind)), v.title, std::format("{:g}", v.scale),
                                std::to_string(v.columns + v.walls + v.dots + v.bars)});
        }
        say("kj.msg.datum", {std::format("{:.3f}", r.datum), r.datumFromDrawing && kp.datumAuto ? " — " + r.datumText : std::string{},
                             std::format("{:.3f}", r.datum + r.foundTop), std::format("{:.3f}", r.datum + r.slabTop)});
        say("kj.msg.levels", {std::format("{:+.3f}", r.foundTop), std::format("{:.2f}", kp.foundT), std::format("{:+.3f}", r.slabTop),
                              std::format("{:.2f}", r.slabT)});
        say("kj.msg.concrete", {std::format("{:.1f}", r.bm.totalVolume), r.concrete, std::format("{:.1f}", r.bm.totalFormwork)});
        for (const auto& row : r.bm.pto)
            out_ << "  " << loc_.tr(std::string("bim.kind.") + bim::kindKey(row.kind)) << ": " << row.count << " — "
                 << std::format("{:.2f} m3", row.volume) << '\n';
        say("kj.msg.steel", {std::to_string(r.bars.size()), std::format("{:.2f}", r.totalMass / 1000)});
        for (const auto& [d, kg] : r.massByDia) out_ << "  " << d << ": " << std::format("{:.0f} kg", kg) << '\n';
        for (const auto& i : r.issues)
            out_ << "  " << loc_.tr(i.error ? "sewer.lvl.error" : "sewer.lvl.warning") << ": " << loc_.tr(i.key, i.args) << '\n';
    } else if (cmd == "UTIL") {
        // UTIL — chizmani tahlil; UTIL + — 3D ni chizmaga; UTIL fayl.json — GUI'da tuzatilgan modelni chizish
        auto p = civil::defaultUtilParams();
        civil::UtilModel m;
        bool draw = false;
        if (!a.empty() && extOf(a[0]) == "json") {
            std::ifstream f(a[0], std::ios::binary);
            if (!f) throw Error("acad.err.open", {a[0]});
            std::stringstream b;
            b << f.rdbuf();
            civil::utilFromJson(b.str(), m, p);
            draw = true;
        } else {
            m = civil::readUtilities(doc_, civil::autoUtilLayers(doc_), p);
            draw = !a.empty() && a[0] == "+";
        }
        int warn = 0;
        for (const auto& i : m.issues) warn += !i.error;
        say("util.msg.summary", {std::to_string(m.lines.size()), std::to_string(m.nodes.size()), std::to_string(m.crossings.size()),
                                 std::to_string(m.groundPoints), std::to_string(m.errors()), std::to_string(warn)});
        for (const auto& [k, len] : m.lengthByKind) say("util.msg.len", {loc_.tr(std::string("util.kind.") + civil::utilKey(k)), std::format("{:.1f}", len)});
        for (const auto& i : m.issues)
            out_ << "  " << loc_.tr(i.error ? "sewer.lvl.error" : "sewer.lvl.warning") << ": " << loc_.tr(i.key, i.args)
                 << std::format("  [X={:.2f} Y={:.2f}]", i.x, i.y) << '\n';
        if (draw) {
            smartDraw("util", civil::utilToJson(m, p), civil::drawUtilities3D(m, 0.5), cmd, std::string(), true);
            say("util.msg.drawn", {std::to_string(last_.size())});
        }
    } else if (cmd == "ROAD") {
        // ROAD — tahlil; + — 3D chizmaga; x,y — bo'ylama profil; fayl.json — GUI'da tuzatilgan model (3D chiziladi)
        auto p = civil::defaultRoadParams();
        civil::RoadModel m;
        bool draw3d = false;
        std::optional<Vec3> profileAt;
        std::size_t k = 0;
        if (!a.empty() && extOf(a[0]) == "json") {
            std::ifstream f(a[0], std::ios::binary);
            if (!f) throw Error("acad.err.open", {a[0]});
            std::stringstream b;
            b << f.rdbuf();
            civil::roadFromJson(b.str(), m, p);
            draw3d = true;
            k = 1;
        } else {
            m = civil::buildRoad(doc_, civil::autoRoadLayers(doc_), p);
        }
        for (; k < a.size(); ++k) {
            if (a[k] == "+") draw3d = true;
            else profileAt = parsePoint(a[k]);
        }
        say("road.msg.summary", {std::format("{:.1f}", m.length), std::to_string(m.st.size()), std::format("{:.0f}", m.fillVolume),
                                 std::format("{:.0f}", m.cutVolume), std::format("{:.0f}", m.asphaltArea), std::to_string(m.groundPoints),
                                 std::to_string(m.designPoints), std::to_string(m.errors())});
        for (const auto& [name, v] : m.pavementVolumes) say("road.msg.pav", {name.rfind("road.", 0) == 0 ? loc_.tr(name) : name, std::format("{:.1f}", v)});
        for (const auto& i : m.issues)
            out_ << "  " << loc_.tr(i.error ? "sewer.lvl.error" : "sewer.lvl.warning") << ": " << loc_.tr(i.key, i.args) << '\n';
        if (draw3d || profileAt) {
            std::vector<std::unique_ptr<IEntity>> es;
            if (draw3d) es = civil::drawRoad3D(m);
            if (profileAt)
                for (auto& e : civil::drawRoadProfile(m, *profileAt, 10, 2.0, loc_.tr("road.lbl.ground"), loc_.tr("road.lbl.design"), loc_.tr("road.lbl.pk"))) {
                    e->xset("road", "main");
                    e->xset("part", "profile");
                    es.push_back(std::move(e));
                }
            smartDraw("road", civil::roadToJson(m, p), std::move(es), cmd, profileAt ? std::format("{},{}", profileAt->x, profileAt->y) : std::string(), draw3d);
            say("road.msg.drawn", {std::to_string(last_.size())});
        }
    } else if (cmd == "CORRIDOR") {
        // CORRIDOR — yig'ma (ASM=n, shablon raqami) bo'yicha koridor: hajmlar (o'rtacha va aniq); + — 3D chizmaga
        auto p = civil::defaultCorridorParams();
        civil::Corridor c;
        bool draw = false, fromJson = false;
        std::size_t k = 0;
        if (!a.empty() && extOf(a[0]) == "json") {
            std::ifstream f(a[0], std::ios::binary);
            if (!f) throw Error("acad.err.open", {a[0]});
            std::stringstream b;
            b << f.rdbuf();
            civil::corridorFromJson(b.str(), c, p);
            fromJson = draw = true;
            k = 1;
        }
        for (; k < a.size(); ++k) {
            const std::string up = utf8::toUpper(a[k]);
            if (a[k] == "+") draw = true;
            else if (up.rfind("ASM=", 0) == 0) {
                const auto all = civil::builtinAssemblies();
                const int i = static_cast<int>(parseNumber(a[k].substr(4)));
                if (i < 1 || i > static_cast<int>(all.size())) throw Error("err.number_expected", {a[k]});
                p.assemblies = {all[static_cast<std::size_t>(i - 1)]};
                if (fromJson) civil::recomputeCorridor(c, p);
            } else throw Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))});
        }
        if (!fromJson) c = civil::buildCorridor(doc_, p);
        say("cor.msg.summary", {p.assemblies.front().name, std::format("{:.1f}", c.length), std::to_string(c.sec.size()), std::to_string(c.groundPoints),
                                std::to_string(c.designPoints), std::to_string(c.errors())});
        say("cor.msg.earth", {std::format("{:.1f}", c.cutAvg), std::format("{:.1f}", c.cutExact), std::format("{:.1f}", c.fillAvg),
                              std::format("{:.1f}", c.fillExact)});
        for (const auto& [m, ae] : c.volumes)
            say("cor.msg.mat", {loc_.tr("cor.mat." + m), std::format("{:.1f}", ae.first), std::format("{:.1f}", ae.second)});
        for (const auto& i : c.issues)
            out_ << "  " << loc_.tr(i.error ? "sewer.lvl.error" : "sewer.lvl.warning") << ": " << loc_.tr(i.key, i.args) << '\n';
        if (draw) {
            auto es = civil::drawCorridor3D(c);
            auto comp = std::make_unique<CompositeCommand>(cmd);
            std::set<std::string> made;
            for (const auto& e : es)
                if (!doc_.layer(e->layer()) && made.insert(e->layer()).second) comp->add(std::make_unique<SetLayerCommand>(Layer{e->layer()}));
            std::vector<AddEntityCommand*> adds;
            for (auto& e : es) {
                auto cc = std::make_unique<AddEntityCommand>(std::move(e), cmd);
                adds.push_back(cc.get());
                comp->add(std::move(cc));
            }
            stack_.execute(std::move(comp));
            last_.clear();
            for (auto* cc : adds) last_.push_back(cc->entityId());
            say("road.msg.drawn", {std::to_string(last_.size())});
        }
    } else if (cmd == "GRID" || cmd == "ALIGN" || cmd == "UTILNET" || cmd == "CITY") {
        // "0 dan chizish" generatorlari: chizmaga AsiaCAD importi taniydigan obyektlar yoziladi (keyin BINO/YO'L/YEROSTI/KORIDOR o'qiydi)
        auto readFile = [](const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            if (!f) throw Error("acad.err.open", {path});
            std::stringstream b;
            b << f.rdbuf();
            return b.str();
        };
        const bool js = !a.empty() && extOf(a[0]) == "json";
        auto usage = [&]() { return Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))}); };
        if (cmd == "GRID") {
            civil::GridSpec g;
            if (js) g = civil::gridFromJson(readFile(a[0]));
            else {
                if (a.size() < 2) throw usage();
                g.dx = civil::parseSpacing(a[0]);
                g.dy = civil::parseSpacing(a[1]);
                if (g.dx.empty()) throw Error("gen.err.spacing", {a[0]});
                if (g.dy.empty()) throw Error("gen.err.spacing", {a[1]});
                for (std::size_t k = 2; k < a.size(); ++k) {
                    std::string t = a[k];
                    for (const char* m : {"\xC3\x97", "\xD1\x85"})
                        for (std::size_t q; (q = t.find(m)) != std::string::npos;) t.replace(q, 2, "x");
                    if (const auto x = t.find_first_of("xX"); x != std::string::npos && t.find(',') == std::string::npos) {
                        g.colB = parseNumber(t.substr(0, x));
                        g.colH = parseNumber(t.substr(x + 1));
                    } else {
                        const Vec3 p = parsePoint(a[k]);
                        g.x0 = p.x;
                        g.y0 = p.y;
                    }
                }
            }
            auto es = civil::generateGrid(g);
            double W = 0, D = 0;
            for (double d : g.dx) W += d;
            for (double d : g.dy) D += d;
            addLayered(cmd, std::move(es));
            say("gen.msg.grid", {std::to_string(g.dx.size() + 1), std::to_string(g.dy.size() + 1), std::to_string((g.dx.size() + 1) * (g.dy.size() + 1)),
                                 std::format("{:g}", W), std::format("{:g}", D), loc_.commandName("BUILDING")});
        } else if (cmd == "ALIGN") {
            if (!js) throw usage();
            const auto al = civil::alignFromJson(readFile(a[0]));
            std::vector<std::string> warn;
            auto es = civil::generateAlignment(al, &warn);
            double L = 0;
            int arcs = 0;
            if (auto* pl = dynamic_cast<PolylineEntity*>(es.front().get())) {
                L = pl->length2d();
                for (double b : pl->bulges()) arcs += std::abs(b) > 1e-12;
            }
            addLayered(cmd, std::move(es));
            say("gen.msg.align", {std::format("{:.2f}", L), std::to_string(arcs), std::to_string(al.pvi.size())});
            for (const auto& w : warn) {
                std::vector<std::string> f;
                for (std::size_t i = 0, j; i <= w.size(); i = j + 1) {
                    j = w.find('|', i);
                    if (j == std::string::npos) j = w.size();
                    f.push_back(w.substr(i, j - i));
                }
                out_ << "  " << loc_.tr("sewer.lvl.warning") << ": "
                     << (f[0] == "pvi" ? loc_.tr("gen.warn.pvi", {f[1], f[2]}) : loc_.tr("gen.warn.radius", {f[0], f[1], f[2]})) << '\n';
            }
        } else if (cmd == "UTILNET") {
            if (!js) throw usage();
            const auto n = civil::netFromJson(readFile(a[0]));
            addLayered(cmd, civil::generateNetwork(n));
            say("gen.msg.net", {std::to_string(n.nodes.size()), std::to_string(n.pipes.size()), loc_.commandName("UTIL")});
        } else {
            civil::CitySpec c;
            if (js) c = civil::cityFromJson(readFile(a[0]));
            else {
                std::vector<double> ints;
                for (const auto& t : a) {
                    if (t.find(',') != std::string::npos) {
                        const Vec3 p = parsePoint(t);
                        c.x0 = p.x;
                        c.y0 = p.y;
                    } else if (const auto d = t.find('-'); d != std::string::npos && d > 0) {
                        c.floorsMin = static_cast<int>(parseNumber(t.substr(0, d)));
                        c.floorsMax = static_cast<int>(parseNumber(t.substr(d + 1)));
                    } else ints.push_back(parseNumber(t));
                }
                if (ints.size() == 1 || ints.size() > 2) throw usage();
                if (ints.size() == 2) { c.nx = static_cast<int>(ints[0]); c.ny = static_cast<int>(ints[1]); }
            }
            int gp = 0;
            const auto gfun = civil::groundFromDoc(doc_, &gp);
            auto r = civil::generateCity(c, gfun);
            const std::size_t nb = r.buildings.size();
            addLayered(cmd, std::move(r.entities));
            say("gen.msg.city", {std::to_string(c.nx * c.ny), std::to_string(nb), std::format("{:.1f}", r.siteArea / 10000),
                                 std::format("{:.0f}", r.gfa), std::format("{:.2f}", r.far()), std::format("{:.2f}", r.streetLength / 1000)});
            if (!gp) out_ << "  " << loc_.tr("sewer.lvl.warning") << ": " << loc_.tr("gen.warn.city_ground", {std::format("{:.2f}", c.ground)}) << '\n';
        }
    } else if (cmd == "DRIVE") {
        // DRIVE — tanlangan chiziq bo'ylab haydash tahlili: ko'rinish masofasi (profil + 3D to'siqlar) va talab
        need(a, 1, cmd);
        const IEntity* curve = nullptr;
        for (EntityId id : parseSelection(a[0]))
            if (const IEntity* e = doc_.find(id); e && (dynamic_cast<const ICurve*>(e) != nullptr)) { curve = e; break; }
        if (!curve) throw Error("drive.err.nocurve", {});
        civil::SightParams sp;
        for (std::size_t k = 1; k < a.size(); ++k) {
            const std::string up = utf8::toUpper(a[k]);
            const auto eq = up.find('=');
            if (eq == std::string::npos) throw Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))});
            const std::string key = up.substr(0, eq);
            const double v = parseNumber(a[k].substr(eq + 1));
            if (key == "V") sp.speedKmh = v;
            else if (key == "EYE") sp.eyeH = v;
            else if (key == "OBJ") sp.objH = v;
            else if (key == "STEP") sp.step = std::max(1.0, v);
            else throw Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))});
        }
        civil::DrivePath path;
        std::string src;
        if (civil::curveHasZ(*curve)) {
            path = civil::makeDrivePath(*curve, 1.0);
            src = "drive.z.line";
        } else {
            // 2D o'q: yo'l loyiha profili (yozuvlardan), bo'lmasa — yer yuzasi
            const auto rm = civil::buildRoad(doc_, civil::autoRoadLayers(doc_), civil::defaultRoadParams());
            if (civil::guessRoadLayer(curve->layer()) == civil::RoadLayer::Axis && rm.designPoints > 0 && rm.st.size() >= 2) {
                std::vector<Vec3> pts;
                for (const auto& t : rm.st) pts.push_back({t.x, t.y, t.design});
                path = civil::makeDrivePath(pts, 1.0);
                src = "drive.z.road";
            } else if (auto g = civil::groundFromDoc(doc_)) {
                path = civil::makeDrivePath(*curve, 1.0, g);
                src = "drive.z.ground";
            } else {
                throw Error("drive.err.noz", {});
            }
        }
        const auto scene = civil::sceneFromDocument(doc_);
        const auto r = civil::sightAnalysis(path, scene, sp);
        double zmin = 1e18, zmax = -1e18;
        for (const auto& q : path.p) { zmin = std::min(zmin, q.z); zmax = std::max(zmax, q.z); }
        say("drive.msg.summary", {std::format("{:.1f}", path.length()), std::format("{:.2f}", zmin), std::format("{:.2f}", zmax),
                                  std::format("{:.1f}", r.maxGrade * 1000), loc_.tr(src)});
        say("drive.msg.sight", {std::format("{:g}", sp.speedKmh), std::format("{:.0f}", r.required), std::format("{:.1f}", r.minD),
                                civil::pk(r.minAt), std::to_string(scene.tris.size())});
        for (const auto& i : r.issues)
            out_ << "  " << loc_.tr("sewer.lvl.error") << ": "
                 << loc_.tr("drive.chk.sight", {civil::pk(i.s0), civil::pk(i.s1), std::format("{:.1f}", i.minD), std::format("{:.0f}", r.required)}) << '\n';
    } else if (cmd == "WATERWAY") {
        // WATERWAY — suv yo'li (quvur, beton kanal, ariq, lotok): tahlil; Q= sarf; SEC= shablon raqami; + — 3D; x,y — profil
        auto p = civil::defaultWwParams();
        civil::WwModel m;
        bool draw3d = false, fromJson = false;
        std::optional<Vec3> profileAt;
        std::size_t k = 0;
        if (!a.empty() && extOf(a[0]) == "json") {
            std::ifstream f(a[0], std::ios::binary);
            if (!f) throw Error("acad.err.open", {a[0]});
            std::stringstream b;
            b << f.rdbuf();
            civil::wwFromJson(b.str(), m, p);
            draw3d = fromJson = true;
            k = 1;
        }
        bool changed = false;
        for (; k < a.size(); ++k) {
            const std::string up = utf8::toUpper(a[k]);
            if (a[k] == "+") draw3d = true;
            else if (up.rfind("Q=", 0) == 0) { p.q = parseNumber(a[k].substr(2)); changed = true; }
            else if (up.rfind("SEC=", 0) == 0) {
                const auto secs = civil::builtinWwSections();
                const int i = static_cast<int>(parseNumber(a[k].substr(4)));
                if (i < 1 || i > static_cast<int>(secs.size())) throw Error("err.number_expected", {a[k]});
                p.sec = secs[static_cast<std::size_t>(i - 1)];
                changed = true;
            } else profileAt = parsePoint(a[k]);
        }
        if (!fromJson) m = civil::buildWaterway(doc_, civil::autoWwLayers(doc_), p);
        else if (changed) civil::recomputeWaterway(m, p);
        say("ww.msg.summary", {p.sec.name, loc_.tr(std::string("ww.type.") + civil::wwTypeKey(p.sec.type)), std::format("{:.1f}", m.length),
                               std::to_string(m.st.size()), std::format("{:.3f}", p.q), std::format("{:.3f}", m.capacity),
                               std::to_string(m.groundPoints), std::to_string(m.designPoints), std::to_string(m.errors())});
        say("ww.msg.volumes", {std::format("{:.0f}", m.cutVolume), std::format("{:.0f}", m.fillVolume), std::format("{:.1f}", m.concrete),
                               std::to_string(m.pieces), std::format("{:.2f}", m.headLoss)});
        if (!m.st.empty())
            say("ww.msg.levels", {std::format("{:.2f}", m.st.front().bottom), std::format("{:.2f}", m.st.back().bottom),
                                  std::format("{:.2f}", m.st.front().ground), std::format("{:.2f}", m.st.back().ground)});
        for (const auto& i : m.issues)
            out_ << "  " << loc_.tr(i.error ? "sewer.lvl.error" : "sewer.lvl.warning") << ": " << loc_.tr(i.key, i.args) << '\n';
        if (draw3d || profileAt) {
            std::vector<std::unique_ptr<IEntity>> es;
            if (draw3d) es = civil::drawWaterway3D(m, p);
            if (profileAt)
                for (auto& e : civil::drawWaterwayProfile(m, p, *profileAt, 10, 2.0, loc_.tr("road.lbl.ground"), loc_.tr("ww.lbl.bottom"),
                                                          loc_.tr("ww.lbl.water"), loc_.tr("road.lbl.pk"))) {
                    e->xset("ww", "main");
                    e->xset("part", "profile");
                    es.push_back(std::move(e));
                }
            smartDraw("ww", civil::wwToJson(m, p), std::move(es), cmd, profileAt ? std::format("{},{}", profileAt->x, profileAt->y) : std::string(), draw3d);
            say("road.msg.drawn", {std::to_string(last_.size())});
        }
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
            const std::string msg = loc_.tr(is.key, is.args);
            out_ << "  " << loc_.tr(lv) << ": " << msg
                 << (is.ref.empty() || msg.find('(') != std::string::npos ? std::string() : "  [" + is.ref + "]") << '\n';
        }
        // chizish: plan + (ixtiyoriy) profil. Tarmoq hujjat lug'atida saqlanadi — keyin PIPEEDIT / tuzatish paneli bilan o'zgartiriladi
        std::string at = "-";
        if (a.size() < 2 || a[1] != "-") {
            Vec3 o;
            if (a.size() > 1) o = parsePoint(a[1]);
            else if (const std::string prev = doc_.dictGet("sewer:" + net.name + ":at"); !prev.empty() && prev != "-") o = parseRawPoint(prev);
            else {
                BBox b = doc_.extents(true);
                for (const auto& e : civil::drawPlan(net, res)) b.expand(e->bbox());
                o = {b.max.x + 20 * net.textHeight + 26 * net.textHeight, b.min.y, 0};
            }
            at = std::format("{},{}", o.x, o.y);
        }
        sewerDraw(net, at, cmd);
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
