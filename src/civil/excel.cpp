#include <asiacad/civil/excel.h>

#include <asiacad/core/error.h>

#include <cmath>
#include <format>

namespace asiacad::excel {

using xlsx::Cell;
using xlsx::Sheet;
using xlsx::Workbook;

namespace {

constexpr int H = 1, N2 = 2, N3 = 3, TOT = 4, INT = 5, ERR = 6, WARN = 7;

std::string T(const Tr& tr, const std::string& k, const std::vector<std::string>& a = {}) { return tr(k, a); }
Cell hd(const Tr& tr, const std::string& k) { return Cell::s(T(tr, k), H); }
std::string sref(const std::string& name) {  // formulada varaq nomi: 'Bo''laklar'!
    std::string o = "'";
    for (char c : name) { o += c; if (c == '\'') o += '\''; }
    return o + "'!";
}
std::string r1(int row) { return std::to_string(row + 1); }  // 0-asosli -> Excel qatori

const Sheet* findSheet(const Workbook& wb, const Tr& tr, const std::string& key, std::size_t index) {
    if (const Sheet* s = wb.find(T(tr, key))) return s;
    for (const auto& s : wb.sheets)  // boshqa tilda saqlangan bo'lsa ham: kalitning lotin qismi
        if (s.name == key) return &s;
    return index < wb.sheets.size() ? &wb.sheets[index] : nullptr;
}

// ---- parametr varaqlari: A kalit | B qiymat | C izoh
struct ParamWriter {
    Sheet& s;
    const Tr& tr;
    void num(const std::string& key, double v, int st = N3) { s.add({Cell::s(key), Cell::n(v, st), Cell::s(T(tr, "xl.p." + key))}); }
    void str(const std::string& key, const std::string& v) { s.add({Cell::s(key), Cell::s(v, WARN), Cell::s(T(tr, "xl.p." + key))}); }
};
std::map<std::string, std::pair<std::optional<double>, std::string>> readParams(const Sheet& s) {
    std::map<std::string, std::pair<std::optional<double>, std::string>> m;
    for (std::size_t r = 1; r < s.rows.size(); ++r) {
        const std::string k = s.str(r, 0);
        if (!k.empty()) m[k] = {s.num(r, 1), s.str(r, 1)};
    }
    return m;
}
template <class F> void pnum(const std::map<std::string, std::pair<std::optional<double>, std::string>>& m, const char* k, F set) {
    auto it = m.find(k);
    if (it != m.end() && it->second.first) set(*it->second.first);
}
void pstr(const std::map<std::string, std::pair<std::optional<double>, std::string>>& m, const char* k, std::string& out) {
    auto it = m.find(k);
    if (it != m.end() && !it->second.second.empty()) out = it->second.second;
}

std::string levelName(const Tr& tr, int lv) { return lv == 0 ? T(tr, "bim.level.found") : T(tr, "bim.level.n", {std::to_string(lv)}); }
std::string kindName(const Tr& tr, bim::ElemKind k) { return T(tr, std::string("bim.kind.") + bim::kindKey(k)); }

void issuesSheet(Workbook& wb, const Tr& tr, const std::vector<std::tuple<int, std::string, std::string, std::string>>& rows) {
    Sheet& s = wb.add(T(tr, "xl.sh.checks"));
    s.add({hd(tr, "xl.c.level"), hd(tr, "xl.c.element"), hd(tr, "xl.c.message"), hd(tr, "xl.c.ref")});
    s.widths = {16, 14, 90, 30};
    for (const auto& [lv, el, msg, ref] : rows) {
        const int st = lv == 2 ? ERR : lv == 1 ? WARN : 0;
        s.add({Cell::s(T(tr, lv == 2 ? "sewer.lvl.error" : lv == 1 ? "sewer.lvl.warning" : "sewer.lvl.info"), st), Cell::s(el, st), Cell::s(msg, st),
               Cell::s(ref, st)});
    }
    if (rows.empty()) s.add({Cell::s(T(tr, "gui.bim.ok"))});
}

// ---- bino varaqlari (bino va armatura kitoblarida umumiy)
void buildingParamSheets(Workbook& wb, const bim::BuildingParams& p, const std::map<std::string, bim::ElemKind>& layers, const Tr& tr) {
    Sheet& s = wb.add(T(tr, "xl.sh.params"));
    s.add({hd(tr, "xl.c.key"), hd(tr, "xl.c.value"), hd(tr, "xl.c.desc")});
    s.widths = {18, 14, 60};
    s.autoFilter = false;
    ParamWriter w{s, tr};
    w.num("floors", p.floors, INT);
    w.num("floorHeight", p.floorHeight);
    w.num("slabT", p.slabT);
    w.num("roofT", p.roofT);
    w.num("foundTop", p.foundTop);
    w.num("foundT", p.foundT);
    w.num("foundOverhang", p.foundOverhang);
    w.num("stripFoundation", p.stripFoundation ? 1 : 0, INT);
    w.num("stripWidth", p.stripWidth);
    w.num("leanT", p.leanT);
    w.num("leanOverhang", p.leanOverhang);
    w.num("wallT", p.wallT);
    w.str("classLean", p.classLean);
    w.str("classFound", p.classFound);
    w.str("classFrame", p.classFrame);
    w.str("classSlab", p.classSlab);

    Sheet& l = wb.add(T(tr, "xl.sh.layers"));
    l.add({hd(tr, "gui.bim.col.layer"), hd(tr, "xl.c.kindkey"), hd(tr, "gui.bim.col.kind")});
    l.widths = {24, 12, 30};
    for (const auto& [name, k] : layers) {
        if (name == "PTO" || name == "ARM_VEDOMOST") continue;
        l.add({Cell::s(name), Cell::s(bim::kindKey(k), WARN), Cell::s(kindName(tr, k))});
    }
    Sheet& legend = wb.sheets.back();
    legend.add({});
    legend.add({Cell::s(T(tr, "xl.kinds"), H)});
    for (bim::ElemKind k : {bim::ElemKind::Lean, bim::ElemKind::FoundationSlab, bim::ElemKind::StripFooting, bim::ElemKind::Column, bim::ElemKind::Wall,
                            bim::ElemKind::Slab, bim::ElemKind::Opening, bim::ElemKind::Outline, bim::ElemKind::Ignore})
        legend.add({Cell::s(""), Cell::s(bim::kindKey(k)), Cell::s(kindName(tr, k))});
}

void buildingParamsFrom(const Workbook& wb, bim::BuildingParams& p, std::map<std::string, bim::ElemKind>& layers, const Tr& tr) {
    const Sheet* s = findSheet(wb, tr, "xl.sh.params", 0);
    if (!s) throw Error("xlsx.err.sheet", {T(tr, "xl.sh.params")});
    const auto m = readParams(*s);
    pnum(m, "floors", [&](double v) { p.floors = std::clamp(static_cast<int>(std::lround(v)), 1, 200); });
    pnum(m, "floorHeight", [&](double v) { p.floorHeight = v; });
    pnum(m, "slabT", [&](double v) { p.slabT = v; });
    pnum(m, "roofT", [&](double v) { p.roofT = v; });
    pnum(m, "foundTop", [&](double v) { p.foundTop = v; });
    pnum(m, "foundT", [&](double v) { p.foundT = v; });
    pnum(m, "foundOverhang", [&](double v) { p.foundOverhang = v; });
    pnum(m, "stripFoundation", [&](double v) { p.stripFoundation = v != 0; });
    pnum(m, "stripWidth", [&](double v) { p.stripWidth = v; });
    pnum(m, "leanT", [&](double v) { p.leanT = v; });
    pnum(m, "leanOverhang", [&](double v) { p.leanOverhang = v; });
    pnum(m, "wallT", [&](double v) { p.wallT = v; });
    pstr(m, "classLean", p.classLean);
    pstr(m, "classFound", p.classFound);
    pstr(m, "classFrame", p.classFrame);
    pstr(m, "classSlab", p.classSlab);
    if (const Sheet* l = findSheet(wb, tr, "xl.sh.layers", 1)) {
        for (std::size_t r = 1; r < l->rows.size(); ++r) {
            const std::string name = l->str(r, 0);
            bim::ElemKind k;
            if (!name.empty() && bim::kindFromKey(l->str(r, 1), k)) layers[name] = k;
        }
    }
}

void ptoSheet(Workbook& wb, const bim::BuildingModel& m, const Tr& tr) {
    Sheet& s = wb.add(T(tr, "xl.sh.pto"));
    s.add({hd(tr, "gui.bim.col.level"), hd(tr, "gui.bim.col.elem"), hd(tr, "gui.bim.col.count"), hd(tr, "gui.bim.col.vol"),
           hd(tr, "gui.bim.col.form"), hd(tr, "gui.bim.col.class")});
    s.widths = {14, 22, 8, 12, 12, 8};
    for (const auto& r : m.pto)
        s.add({Cell::s(levelName(tr, r.level)), Cell::s(kindName(tr, r.kind)), Cell::n(r.count, INT), Cell::n(r.volume, N2),
               Cell::n(r.kind == bim::ElemKind::Lean ? 0 : r.formwork, N2), Cell::s(r.concrete)});
    const int last = static_cast<int>(s.rows.size());  // oxirgi ma'lumot qatori (1-asosli)
    s.add({Cell::s(T(tr, "gui.bim.total"), TOT), Cell::s("", TOT), Cell::s("", TOT), Cell::f(std::format("SUM(D2:D{})", last), m.totalVolume, TOT),
           Cell::f(std::format("SUM(E2:E{})", last), m.totalFormwork, TOT), Cell::s("", TOT)});
    s.add({});
    s.add({Cell::s(T(tr, "xl.byclass"), H), Cell::s("", H), Cell::s("", H), hd(tr, "gui.bim.col.vol")});
    for (const auto& [c, v] : m.byConcrete)
        s.add({Cell::s(c), {}, {}, Cell::f(std::format("SUMIF($F$2:$F${},A{},$D$2:$D${})", last, s.rows.size() + 1, last), v, N2)});
}

} // namespace

// ================================================================ kanalizatsiya
Workbook sewerWorkbook(const civil::SewerNetwork& n, const civil::SewerResult& r, const Tr& tr) {
    Workbook wb;
    // 1. Quduqlar (kiritish + natija)
    Sheet& m = wb.add(T(tr, "xl.sh.manholes"));
    m.add({hd(tr, "xl.c.name"), Cell::s("X", H), Cell::s("Y", H), hd(tr, "xl.c.rim"), hd(tr, "xl.c.invert"), hd(tr, "xl.c.mhd"), hd(tr, "xl.c.area"),
           hd(tr, "xl.c.invin"), hd(tr, "xl.c.invout"), hd(tr, "xl.c.depth"), hd(tr, "xl.c.mhreq")});
    m.widths = {10, 12, 12, 11, 13, 11, 10, 12, 12, 11, 12};
    for (std::size_t i = 0; i < n.manholes.size(); ++i) {
        const auto& mh = n.manholes[i];
        const auto* res = i < r.manholes.size() ? &r.manholes[i] : nullptr;
        const int row = static_cast<int>(m.rows.size());
        m.add({Cell::s(mh.name, WARN), Cell::n(mh.x, N3), Cell::n(mh.y, N3), Cell::n(mh.rim, N2),
               mh.invert ? Cell::n(*mh.invert, N2) : Cell::s("", WARN), mh.diameter > 0 ? Cell::n(mh.diameter, N2) : Cell::s("", WARN),
               Cell::n(mh.areaHa, N3), Cell::n(res ? res->invIn : 0, N2), Cell::n(res ? res->invOut : 0, N2),
               Cell::f(std::format("D{0}-MIN(H{0},I{0})", r1(row)), res ? res->depth : 0, N2), Cell::n(res ? res->required : 0, N2)});
    }
    // 2. Bo'laklar
    Sheet& g = wb.add(T(tr, "xl.sh.segments"));
    g.add({Cell::s("№", H), hd(tr, "xl.c.from"), hd(tr, "xl.c.to"), hd(tr, "xl.c.pipe"), hd(tr, "xl.c.slope"), hd(tr, "xl.c.flow"), hd(tr, "xl.c.len"),
           hd(tr, "xl.c.invup"), hd(tr, "xl.c.invdown"), Cell::s("DN", H), hd(tr, "xl.c.kgm"), hd(tr, "xl.c.mass"), hd(tr, "xl.c.fill"),
           hd(tr, "xl.c.vel"), hd(tr, "xl.c.qfull"), hd(tr, "xl.c.status")});
    g.widths = {5, 9, 9, 18, 10, 9, 10, 11, 11, 7, 9, 11, 8, 9, 10, 40};
    const std::string ms = sref(T(tr, "xl.sh.manholes"));
    for (std::size_t i = 0; i < n.segments.size(); ++i) {
        const auto& sg = n.segments[i];
        const auto* s = i < r.segments.size() ? &r.segments[i] : nullptr;
        const int a = static_cast<int>(i) + 2, b = a + 1;  // quduqlar varag'idagi qatorlar
        const int row = static_cast<int>(g.rows.size());
        std::string status;
        int st = 0;
        for (const auto& is : r.issues)
            if (is.segment == static_cast<int>(i) && is.level != civil::Issue::Info) {
                if (!status.empty()) status += "; ";
                status += T(tr, is.key, is.args);
                st = std::max(st, is.level == civil::Issue::Error ? 2 : 1);
            }
        const double kgm = s && s->spec ? s->spec->mass() : 0;
        g.add({Cell::n(static_cast<double>(i + 1), INT), Cell::s(n.manholes[i].name), Cell::s(i + 1 < n.manholes.size() ? n.manholes[i + 1].name : std::string()),
               Cell::s(sg.specId, WARN), sg.slope > 0 ? Cell::n(sg.slope, 0) : Cell::s("", WARN), Cell::n(sg.flowLs, N2),
               Cell::f(std::format("SQRT(({0}B{2}-{0}B{1})^2+({0}C{2}-{0}C{1})^2)", ms, a, b), s ? s->length : 0, N2),
               Cell::n(s ? s->invUp : 0, N2), Cell::n(s ? s->invDown : 0, N2), Cell::n(s && s->spec ? s->spec->dn : 0, INT), Cell::n(kgm, N2),
               Cell::f(std::format("G{0}*K{0}", r1(row)), (s ? s->length : 0) * kgm, N2), Cell::n(s ? s->fill : 0, N2),
               Cell::n(s ? s->velocity : 0, N2), Cell::n(s ? s->qFull : 0, N2),
               Cell::s(status.empty() ? "OK" : status, st == 2 ? ERR : st == 1 ? WARN : 0)});
    }
    const int lastSeg = static_cast<int>(g.rows.size());
    g.add({Cell::s(T(tr, "gui.bim.total"), TOT), {}, {}, {}, {}, {}, Cell::f(std::format("SUM(G2:G{})", lastSeg), r.totalLength, TOT), {}, {}, {}, {},
           Cell::f(std::format("SUM(L2:L{})", lastSeg), [&] { double t = 0; for (const auto& x : r.pipes) t += x.massKg; return t; }(), TOT)});
    // 3. Hajmlar
    Sheet& q = wb.add(T(tr, "xl.sh.quant"));
    q.add({hd(tr, "xl.c.pipe"), hd(tr, "xl.c.len"), hd(tr, "xl.c.kgm"), hd(tr, "xl.c.mass")});
    q.widths = {22, 12, 10, 12};
    const std::string gs = sref(T(tr, "xl.sh.segments"));
    for (const auto& p : r.pipes) {
        const int row = static_cast<int>(q.rows.size());
        const auto* spec = civil::PipeCatalog::builtin().find(p.specId);
        q.add({Cell::s(p.specId), Cell::f(std::format("SUMIF({0}$D$2:$D${1},A{2},{0}$G$2:$G${1})", gs, lastSeg, r1(row)), p.length, N2),
               Cell::n(spec ? spec->mass() : 0, N2), Cell::f(std::format("B{0}*C{0}", r1(row)), p.massKg, N2)});
    }
    const int lastQ = static_cast<int>(q.rows.size());
    q.add({Cell::s(T(tr, "gui.bim.total"), TOT), Cell::f(std::format("SUM(B2:B{})", lastQ), r.totalLength, TOT), Cell::s("", TOT),
           Cell::f(std::format("SUM(D2:D{})", lastQ), [&] { double t = 0; for (const auto& x : r.pipes) t += x.massKg; return t; }(), TOT)});
    q.add({});
    q.add({hd(tr, "xl.c.work"), hd(tr, "xl.c.qty")});
    q.add({Cell::s(T(tr, "xl.trench")), Cell::n(r.trenchVolume, N2)});
    q.add({Cell::s(T(tr, "xl.bedding")), Cell::n(r.beddingVolume, N2)});
    q.add({Cell::s(T(tr, "xl.backfill")), Cell::n(r.backfillVolume, N2)});
    for (const auto& [d, c] : r.manholeCount) q.add({Cell::s(T(tr, "xl.manholes", {std::format("{:.2f}", d)})), Cell::n(c, INT)});
    // 4. Parametrlar
    Sheet& p = wb.add(T(tr, "xl.sh.params"));
    p.add({hd(tr, "xl.c.key"), hd(tr, "xl.c.value"), hd(tr, "xl.c.desc")});
    p.widths = {16, 14, 60};
    p.autoFilter = false;
    ParamWriter w{p, tr};
    w.str("name", n.name);
    w.str("system", n.system);
    w.num("street", n.street ? 1 : 0, INT);
    w.num("pressure", n.pressure ? 1 : 0, INT);
    w.num("startDepth", n.startDepth);
    w.num("slope", n.defaultSlope);
    w.num("frost", n.frostDepth);
    w.num("textHeight", n.textHeight);
    w.num("vscale", n.vScale);
    w.num("storm", n.storm.enabled ? 1 : 0, INT);
    w.num("q20", n.storm.q20);
    w.num("n", n.storm.n);
    w.num("mr", n.storm.mr);
    w.num("gamma", n.storm.gamma);
    w.num("P", n.storm.P);
    w.num("tcon", n.storm.tcon);
    w.num("tcan", n.storm.tcan);
    for (int i = 0; i < 7; ++i) w.num("share" + std::to_string(i + 1), n.storm.share[static_cast<std::size_t>(i)]);
    // 5. Tekshiruv
    std::vector<std::tuple<int, std::string, std::string, std::string>> iss;
    for (const auto& is : r.issues) {
        std::string el = is.segment >= 0 ? T(tr, "xl.seg", {std::to_string(is.segment + 1)})
                       : is.manhole >= 0 && static_cast<std::size_t>(is.manhole) < n.manholes.size() ? n.manholes[static_cast<std::size_t>(is.manhole)].name
                                                                                                      : "";
        iss.emplace_back(is.level == civil::Issue::Error ? 2 : is.level == civil::Issue::Warning ? 1 : 0, el, T(tr, is.key, is.args), is.ref);
    }
    issuesSheet(wb, tr, iss);
    return wb;
}

void sewerFromWorkbook(const Workbook& wb, civil::SewerNetwork& n, const Tr& tr) {
    const Sheet* m = findSheet(wb, tr, "xl.sh.manholes", 0);
    const Sheet* g = findSheet(wb, tr, "xl.sh.segments", 1);
    if (!m) throw Error("xlsx.err.sheet", {T(tr, "xl.sh.manholes")});
    if (const Sheet* p = findSheet(wb, tr, "xl.sh.params", 3)) {
        const auto pm = readParams(*p);
        pstr(pm, "name", n.name);
        pstr(pm, "system", n.system);
        pnum(pm, "street", [&](double v) { n.street = v != 0; });
        pnum(pm, "pressure", [&](double v) { n.pressure = v != 0; });
        pnum(pm, "startDepth", [&](double v) { n.startDepth = v; });
        pnum(pm, "slope", [&](double v) { n.defaultSlope = v; });
        pnum(pm, "frost", [&](double v) { n.frostDepth = v; });
        pnum(pm, "textHeight", [&](double v) { n.textHeight = v; });
        pnum(pm, "vscale", [&](double v) { n.vScale = v; });
        pnum(pm, "storm", [&](double v) { n.storm.enabled = v != 0; });
        pnum(pm, "q20", [&](double v) { n.storm.q20 = v; });
        pnum(pm, "n", [&](double v) { n.storm.n = v; });
        pnum(pm, "mr", [&](double v) { n.storm.mr = v; });
        pnum(pm, "gamma", [&](double v) { n.storm.gamma = v; });
        pnum(pm, "P", [&](double v) { n.storm.P = v; });
        pnum(pm, "tcon", [&](double v) { n.storm.tcon = v; });
        pnum(pm, "tcan", [&](double v) { n.storm.tcan = v; });
        for (int i = 0; i < 7; ++i) pnum(pm, ("share" + std::to_string(i + 1)).c_str(), [&](double v) { n.storm.share[static_cast<std::size_t>(i)] = v; });
    }
    std::vector<civil::Manhole> mhs;
    for (std::size_t r = 1; r < m->rows.size(); ++r) {
        const std::string name = m->str(r, 0);
        const auto x = m->num(r, 1), y = m->num(r, 2), rim = m->num(r, 3);
        if (name.empty() && !x) continue;
        if (!x || !y || !rim) throw Error("xlsx.err.row", {m->name, std::to_string(r + 1)});
        civil::Manhole mh;
        mh.name = name.empty() ? std::to_string(mhs.size() + 1) : name;
        mh.x = *x;
        mh.y = *y;
        mh.rim = *rim;
        if (auto v = m->num(r, 4)) mh.invert = *v;
        if (auto v = m->num(r, 5)) mh.diameter = *v;
        if (auto v = m->num(r, 6)) mh.areaHa = *v;
        mhs.push_back(mh);
    }
    if (mhs.size() < 2) throw Error("sewer.chk.too_few");
    std::vector<civil::Segment> segs;
    if (g)
        for (std::size_t r = 1; r < g->rows.size() && segs.size() + 1 < mhs.size(); ++r) {
            const std::string spec = g->str(r, 3);
            if (spec.empty()) continue;
            civil::Segment s;
            s.specId = spec;
            if (auto v = g->num(r, 4)) s.slope = *v;
            if (auto v = g->num(r, 5)) s.flowLs = *v;
            segs.push_back(s);
        }
    const std::string fallback = !segs.empty() ? segs.back().specId : !n.segments.empty() ? n.segments.back().specId : "PVC-SN8-200";
    while (segs.size() + 1 < mhs.size()) segs.push_back({fallback, 0, 0});
    n.manholes = std::move(mhs);
    n.segments = std::move(segs);
}

// ================================================================ bino
Workbook buildingWorkbook(const bim::BuildingParams& p, const std::map<std::string, bim::ElemKind>& layers, const bim::BuildingModel& m,
                          const Tr& tr) {
    Workbook wb;
    buildingParamSheets(wb, p, layers, tr);
    ptoSheet(wb, m, tr);
    std::vector<std::tuple<int, std::string, std::string, std::string>> iss;
    for (const auto& is : m.issues) iss.emplace_back(is.error ? 2 : 1, "", T(tr, is.key, is.args), "");
    issuesSheet(wb, tr, iss);
    return wb;
}

void buildingFromWorkbook(const Workbook& wb, bim::BuildingParams& p, std::map<std::string, bim::ElemKind>& layers, const Tr& tr) {
    buildingParamsFrom(wb, p, layers, tr);
}

// ================================================================ armatura
Workbook rebarWorkbook(const bim::BuildingParams& p, const std::map<std::string, bim::ElemKind>& layers, const bim::BuildingModel& m,
                       const bim::RebarSettings& rs, const bim::RebarModel& rm, const Tr& tr) {
    Workbook wb;
    // 1. Vedomost
    Sheet& v = wb.add(T(tr, "xl.sh.sched"));
    v.add({hd(tr, "gui.rb.col.pos"), hd(tr, "gui.bim.col.level"), hd(tr, "gui.bim.col.elem"), hd(tr, "xl.c.role"), Cell::s("Ø, mm", H),
           hd(tr, "gui.rb.col.cls"), hd(tr, "gui.rb.col.len"), hd(tr, "gui.rb.col.cnt"), hd(tr, "xl.c.lsum"), hd(tr, "xl.c.kgm"), hd(tr, "gui.rb.col.mass")});
    v.widths = {6, 13, 18, 14, 8, 8, 9, 8, 11, 9, 12};
    for (const auto& r : rm.schedule) {
        const int row = static_cast<int>(v.rows.size());
        const std::string R = r1(row);
        v.add({Cell::n(r.pos, INT), Cell::s(levelName(tr, r.level)), Cell::s(kindName(tr, r.kind)),
               Cell::s(T(tr, std::string("rebar.role.") + bim::roleKey(r.role))), Cell::n(std::round(r.d * 1000), INT), Cell::s(r.cls),
               Cell::n(r.length, N2), Cell::n(r.count, INT), Cell::f(std::format("G{0}*H{0}", R), r.totalLength, N2),
               Cell::f(std::format("PI()*(E{0}/1000)^2/4*7850", R), bim::barMassPerM(r.d), N3), Cell::f(std::format("I{0}*J{0}", R), r.mass, N2)});
    }
    const int last = static_cast<int>(v.rows.size());
    double cnt = 0, lsum = 0;
    for (const auto& r : rm.schedule) { cnt += r.count; lsum += r.totalLength; }
    v.add({Cell::s(T(tr, "gui.bim.total"), TOT), {}, {}, {}, {}, {}, {}, Cell::f(std::format("SUM(H2:H{})", last), cnt, TOT),
           Cell::f(std::format("SUM(I2:I{})", last), lsum, TOT), {}, Cell::f(std::format("SUM(K2:K{})", last), rm.totalMass, TOT)});
    // 2. Diametrlar bo'yicha
    Sheet& d = wb.add(T(tr, "xl.sh.bydia"));
    d.add({Cell::s("Ø, mm", H), hd(tr, "gui.rb.col.cls"), hd(tr, "gui.rb.col.mass"), Cell::s("t", H)});
    d.widths = {9, 9, 14, 10};
    const std::string vs = sref(T(tr, "xl.sh.sched"));
    std::map<std::pair<int, std::string>, double> seen;
    for (const auto& r : rm.schedule) seen[{static_cast<int>(std::lround(r.d * 1000)), r.cls}] += r.mass;
    for (const auto& [k, kg] : seen) {
        const int row = static_cast<int>(d.rows.size());
        d.add({Cell::n(k.first, INT), Cell::s(k.second),
               Cell::f(std::format("SUMIFS({0}$K$2:$K${1},{0}$E$2:$E${1},A{2},{0}$F$2:$F${1},B{2})", vs, last, r1(row)), kg, N2),
               Cell::f(std::format("C{}/1000", r1(row)), kg / 1000, N3)});
    }
    const int lastD = static_cast<int>(d.rows.size());
    d.add({Cell::s(T(tr, "gui.bim.total"), TOT), Cell::s("", TOT), Cell::f(std::format("SUM(C2:C{})", lastD), rm.totalMass, TOT),
           Cell::f(std::format("SUM(D2:D{})", lastD), rm.totalMass / 1000, TOT)});
    d.add({});
    d.add({Cell::s(T(tr, "rebar.tbl.wire")), {}, Cell::n(rm.wireKg, N2)});
    d.add({Cell::s(T(tr, "xl.nodes")), {}, Cell::n(static_cast<double>(rm.nodes), INT)});
    d.add({Cell::s(T(tr, "rebar.role.CHAIR")), {}, Cell::n(rm.chairs, INT)});
    d.add({Cell::s(T(tr, "rebar.role.TIE")), {}, Cell::n(rm.ties, INT)});
    // 3. Shablonlar (tahrirlanadi)
    Sheet& t = wb.add(T(tr, "xl.sh.rtpl"));
    t.add({hd(tr, "xl.c.kindkey"), hd(tr, "gui.bim.col.kind"), hd(tr, "xl.c.name"), hd(tr, "gui.rb.cover"), hd(tr, "gui.rb.lap"), hd(tr, "xl.c.clsmain"),
           hd(tr, "xl.c.clsstir"), Cell::s("d1", H), Cell::s("s1", H), Cell::s("d2", H), Cell::s("s2", H), hd(tr, "gui.rb.top"), Cell::s("d3", H),
           Cell::s("s3", H), Cell::s("d4", H), Cell::s("s4", H), hd(tr, "gui.rb.chairs"), hd(tr, "gui.rb.ties"), hd(tr, "gui.rb.n"),
           hd(tr, "gui.rb.stir"), hd(tr, "gui.rb.sstir"), hd(tr, "gui.rb.sstirend")});
    t.widths = {9, 18, 30, 10, 9, 9, 9, 6, 6, 6, 6, 9, 6, 6, 6, 6, 9, 9, 9, 9, 10, 10};
    auto mm = [](double v) { return Cell::n(std::round(v * 10000) / 10, INT); };
    for (const auto& [k, tp] : rs.byKind) {
        const std::string nm = tp.name.rfind("rtpl.", 0) == 0 ? T(tr, tp.name) : tp.name;
        t.add({Cell::s(bim::kindKey(k)), Cell::s(kindName(tr, k)), Cell::s(nm), mm(tp.cover), Cell::n(tp.lapD, INT), Cell::s(tp.clsMain, WARN),
               Cell::s(tp.clsStir, WARN), mm(tp.d1), mm(tp.s1), mm(tp.d2), mm(tp.s2), Cell::n(tp.topMesh ? 1 : 0, INT), mm(tp.d3), mm(tp.s3), mm(tp.d4),
               mm(tp.s4), Cell::n(tp.chairsPerM2, N2), Cell::n(tp.tiesPerM2, N2), Cell::n(tp.nMain, INT), mm(tp.dStir), mm(tp.sStir), mm(tp.sStirEnd)});
    }
    t.add({});
    t.add({Cell::s(T(tr, "xl.rtpl.note"))});
    // 4. Bino varaqlari + PTO
    buildingParamSheets(wb, p, layers, tr);
    ptoSheet(wb, m, tr);
    // 5. Tekshiruv
    std::vector<std::tuple<int, std::string, std::string, std::string>> iss;
    for (const auto& is : rm.issues) iss.emplace_back(is.error ? 2 : 1, "", T(tr, is.key, is.args), "ShNQ 2.03.01-24");
    for (const auto& is : m.issues) iss.emplace_back(is.error ? 2 : 1, "", T(tr, is.key, is.args), "");
    issuesSheet(wb, tr, iss);
    return wb;
}

void rebarFromWorkbook(const Workbook& wb, bim::BuildingParams& p, std::map<std::string, bim::ElemKind>& layers, bim::RebarSettings& rs,
                       const Tr& tr) {
    // bino varaqlari: 3-(parametrlar), 4-(qatlamlar) — tartib bo'yicha zaxira
    if (const Sheet* s = findSheet(wb, tr, "xl.sh.params", 3)) {
        Workbook tmp;
        tmp.sheets.push_back(*s);
        tmp.sheets.back().name = T(tr, "xl.sh.params");
        if (const Sheet* l = findSheet(wb, tr, "xl.sh.layers", 4)) {
            tmp.sheets.push_back(*l);
            tmp.sheets.back().name = T(tr, "xl.sh.layers");
        }
        buildingParamsFrom(tmp, p, layers, tr);
    }
    const Sheet* t = findSheet(wb, tr, "xl.sh.rtpl", 2);
    if (!t) throw Error("xlsx.err.sheet", {T(tr, "xl.sh.rtpl")});
    for (std::size_t r = 1; r < t->rows.size(); ++r) {
        bim::ElemKind k;
        if (!bim::kindFromKey(t->str(r, 0), k)) continue;
        bim::RebarTemplate tp = rs.byKind.count(k) ? rs.byKind[k] : bim::RebarTemplate{};
        tp.kind = k;
        const std::string nm = t->str(r, 2);
        if (!nm.empty() && T(tr, tp.name) != nm) {
            tp.name = nm;
            tp.user = true;
        }
        auto m_ = [&](std::size_t c, double& out) { if (auto v = t->num(r, c)) out = *v / 1000; };
        m_(3, tp.cover);
        if (auto v = t->num(r, 4)) tp.lapD = *v;
        if (const auto c = t->str(r, 5); !c.empty()) tp.clsMain = c;
        if (const auto c = t->str(r, 6); !c.empty()) tp.clsStir = c;
        m_(7, tp.d1); m_(8, tp.s1); m_(9, tp.d2); m_(10, tp.s2);
        if (auto v = t->num(r, 11)) tp.topMesh = *v != 0;
        m_(12, tp.d3); m_(13, tp.s3); m_(14, tp.d4); m_(15, tp.s4);
        if (auto v = t->num(r, 16)) tp.chairsPerM2 = *v;
        if (auto v = t->num(r, 17)) tp.tiesPerM2 = *v;
        if (auto v = t->num(r, 18)) tp.nMain = std::max(1, static_cast<int>(std::lround(*v)));
        m_(19, tp.dStir); m_(20, tp.sStir); m_(21, tp.sStirEnd);
        rs.byKind[k] = tp;
    }
}

// ================================================================ yer osti tarmoqlari
namespace {
std::string utilName(const Tr& tr, civil::UtilKind k) { return T(tr, std::string("util.kind.") + civil::utilKey(k)); }
std::string srcName(const Tr& tr, civil::UtilVertex::Src s) {
    static const char* k[] = {"util.src.z", "util.src.label", "util.src.depth", "util.src.interp", "util.src.default", "util.src.manual"};
    return T(tr, k[static_cast<int>(s)]);
}
} // namespace

Workbook utilityWorkbook(const civil::UtilModel& m, const civil::UtilParams& p, const Tr& tr) {
    Workbook wb;
    Sheet& l = wb.add(T(tr, "xl.sh.lines"));
    l.add({Cell::s("ID", H), hd(tr, "gui.bim.col.layer"), hd(tr, "xl.c.kindkey"), hd(tr, "gui.bim.col.kind"), Cell::s("Ø, mm", H), hd(tr, "xl.c.len"),
           hd(tr, "xl.c.zstart"), hd(tr, "xl.c.zend"), hd(tr, "xl.c.covermin"), hd(tr, "xl.c.status")});
    l.widths = {6, 18, 10, 18, 8, 10, 10, 10, 12, 50};
    for (std::size_t i = 0; i < m.lines.size(); ++i) {
        const auto& ln = m.lines[i];
        std::string status;
        int st = 0;
        for (const auto& is : m.issues)
            if (is.line == static_cast<int>(i)) {
                if (!status.empty()) status += "; ";
                status += T(tr, is.key, is.args);
                st = std::max(st, is.error ? 2 : 1);
            }
        double cover = 1e9;
        for (const auto& v : ln.v) cover = std::min(cover, v.ground - v.z - ln.dn / 2000.0);
        l.add({Cell::n(ln.id, INT), Cell::s(ln.layer), Cell::s(civil::utilKey(ln.kind), WARN), Cell::s(utilName(tr, ln.kind)), Cell::n(ln.dn, WARN),
               Cell::n(ln.length(), N2), Cell::n(ln.v.front().z, N3), Cell::n(ln.v.back().z, N3), Cell::n(cover, N2),
               Cell::s(status.empty() ? "OK" : status, st == 2 ? ERR : st == 1 ? WARN : 0)});
    }
    const int lastL = static_cast<int>(l.rows.size());
    l.add({Cell::s(T(tr, "gui.bim.total"), TOT), {}, {}, {}, {}, Cell::f(std::format("SUM(F2:F{})", lastL), [&] { double t = 0; for (const auto& x : m.lines) t += x.length(); return t; }(), TOT)});
    l.add({});
    l.add({Cell::s(T(tr, "xl.kinds"), H)});
    for (civil::UtilKind k : civil::utilKinds()) l.add({Cell::s(""), Cell::s(""), Cell::s(civil::utilKey(k)), Cell::s(utilName(tr, k))});

    Sheet& v = wb.add(T(tr, "xl.sh.points"));
    v.add({Cell::s("ID", H), Cell::s("№", H), Cell::s("X", H), Cell::s("Y", H), hd(tr, "xl.c.zaxis"), hd(tr, "xl.c.zinv"), hd(tr, "xl.c.ground"),
           hd(tr, "xl.c.depthtop"), hd(tr, "xl.c.source")});
    v.widths = {6, 5, 12, 12, 11, 11, 11, 12, 18};
    for (const auto& ln : m.lines)
        for (std::size_t j = 0; j < ln.v.size(); ++j) {
            const auto& q = ln.v[j];
            const int row = static_cast<int>(v.rows.size());
            const double r = ln.dn / 2000.0;
            v.add({Cell::n(ln.id, INT), Cell::n(static_cast<double>(j + 1), INT), Cell::n(q.x, N3), Cell::n(q.y, N3), Cell::n(q.z, WARN),
                   Cell::f(std::format("E{}-{}", r1(row), r), q.z - r, N3), Cell::n(q.ground, N3),
                   Cell::f(std::format("G{0}-E{0}-{1}", r1(row), r), q.ground - q.z - r, N2), Cell::s(srcName(tr, q.src))});
        }

    Sheet& c = wb.add(T(tr, "xl.sh.cross"));
    c.add({hd(tr, "xl.c.linea"), hd(tr, "xl.c.lineb"), Cell::s("X", H), Cell::s("Y", H), hd(tr, "xl.c.za"), hd(tr, "xl.c.zb"), hd(tr, "xl.c.clear"),
           hd(tr, "xl.c.required"), hd(tr, "xl.c.status")});
    c.widths = {22, 22, 11, 11, 10, 10, 11, 10, 10};
    for (const auto& x : m.crossings) {
        const auto& A = m.lines[static_cast<std::size_t>(x.a)];
        const auto& B = m.lines[static_cast<std::size_t>(x.b)];
        const bool bad = x.clear + 1e-9 < x.required;
        const int row = static_cast<int>(c.rows.size());
        c.add({Cell::s(std::format("{} · {}", A.id, utilName(tr, A.kind))), Cell::s(std::format("{} · {}", B.id, utilName(tr, B.kind))), Cell::n(x.x, N2),
               Cell::n(x.y, N2), Cell::n(x.za, N3), Cell::n(x.zb, N3), Cell::n(x.clear, N2), Cell::n(x.required, N2),
               Cell::s(bad ? "✗" : "✓", bad ? ERR : 0)});
        (void)row;
    }

    Sheet& q = wb.add(T(tr, "xl.sh.quant"));
    q.add({hd(tr, "gui.bim.col.kind"), Cell::s("Ø, mm", H), hd(tr, "xl.c.len")});
    q.widths = {22, 10, 12};
    std::map<std::pair<std::string, double>, double> agg;
    for (const auto& ln : m.lines) agg[{utilName(tr, ln.kind), ln.dn}] += ln.length();
    for (const auto& [k, len] : agg) q.add({Cell::s(k.first), Cell::n(k.second, INT), Cell::n(len, N2)});
    const int lastQ = static_cast<int>(q.rows.size());
    q.add({Cell::s(T(tr, "gui.bim.total"), TOT), Cell::s("", TOT), Cell::f(std::format("SUM(C2:C{})", lastQ), [&] { double t = 0; for (const auto& [k2, x] : agg) t += x; return t; }(), TOT)});
    q.add({});
    q.add({Cell::s(T(tr, "util.nodes"), H), Cell::n(static_cast<double>(m.nodes.size()), INT)});

    std::vector<std::tuple<int, std::string, std::string, std::string>> iss;
    for (const auto& is : m.issues)
        iss.emplace_back(is.error ? 2 : 1, is.line >= 0 ? std::to_string(m.lines[static_cast<std::size_t>(is.line)].id) : std::string(),
                         T(tr, is.key, is.args), std::format("X={:.2f} Y={:.2f}", is.x, is.y));
    issuesSheet(wb, tr, iss);
    (void)p;
    return wb;
}

void utilityFromWorkbook(const Workbook& wb, civil::UtilModel& m, const civil::UtilParams& p, const Tr& tr) {
    std::map<int, std::size_t> byId;
    for (std::size_t i = 0; i < m.lines.size(); ++i) byId[m.lines[i].id] = i;
    if (const Sheet* l = findSheet(wb, tr, "xl.sh.lines", 0)) {
        for (std::size_t r = 1; r < l->rows.size(); ++r) {
            const auto id = l->num(r, 0);
            if (!id || !byId.count(static_cast<int>(*id))) continue;
            auto& ln = m.lines[byId[static_cast<int>(*id)]];
            civil::UtilKind k;
            if (civil::utilFromKey(l->str(r, 2), k)) ln.kind = k;
            if (const auto d = l->num(r, 4); d && std::abs(*d - ln.dn) > 1e-6) {
                ln.dn = *d;
                ln.dnManual = true;
            }
        }
    }
    if (const Sheet* v = findSheet(wb, tr, "xl.sh.points", 1)) {
        for (std::size_t r = 1; r < v->rows.size(); ++r) {
            const auto id = v->num(r, 0), j = v->num(r, 1), z = v->num(r, 4);
            if (!id || !j || !z || !byId.count(static_cast<int>(*id))) continue;
            auto& ln = m.lines[byId[static_cast<int>(*id)]];
            const std::size_t k = static_cast<std::size_t>(std::lround(*j)) - 1;
            if (k >= ln.v.size()) continue;
            if (std::abs(ln.v[k].z - *z) > 1e-6) {
                ln.v[k].z = *z;
                ln.v[k].src = civil::UtilVertex::Manual;
            }
            if (const auto x = v->num(r, 2)) ln.v[k].x = *x;
            if (const auto y = v->num(r, 3)) ln.v[k].y = *y;
            if (const auto g = v->num(r, 6)) ln.v[k].ground = *g;
        }
    }
    civil::checkUtilities(m, p);
}

// ================================================================ yo'l
Workbook roadWorkbook(const civil::RoadModel& m, const civil::RoadParams& p, const Tr& tr) {
    Workbook wb;
    Sheet& s = wb.add(T(tr, "xl.sh.stations"));
    s.add({hd(tr, "xl.c.pk"), hd(tr, "xl.c.s"), Cell::s("X", H), Cell::s("Y", H), hd(tr, "xl.c.ground"), hd(tr, "xl.c.design"), hd(tr, "xl.c.workz"),
           hd(tr, "xl.c.grade"), hd(tr, "xl.c.fillA"), hd(tr, "xl.c.cutA"), hd(tr, "xl.c.fillV"), hd(tr, "xl.c.cutV"), hd(tr, "xl.c.width")});
    s.widths = {13, 10, 12, 12, 11, 12, 10, 10, 10, 10, 11, 11, 11};
    const int n = static_cast<int>(m.st.size());
    for (int i = 0; i < n; ++i) {
        const auto& t = m.st[static_cast<std::size_t>(i)];
        const std::string R = r1(i + 1), Rn = r1(i + 2);
        const bool lastRow = i + 1 == n;
        const auto* nx = lastRow ? nullptr : &m.st[static_cast<std::size_t>(i + 1)];
        const double fillV = nx ? (t.fillArea + nx->fillArea) / 2 * (nx->s - t.s) : 0;
        const double cutV = nx ? (t.cutArea + nx->cutArea) / 2 * (nx->s - t.s) : 0;
        const double width = t.section.empty() ? 0 : t.section.front().first - t.section.back().first;
        s.add({Cell::s(civil::pk(t.s)), Cell::n(t.s, N2), Cell::n(t.x, N3), Cell::n(t.y, N3), Cell::n(t.ground, N2), Cell::n(t.design, WARN),
               Cell::f(std::format("F{0}-E{0}", R), t.design - t.ground, N2),
               lastRow ? Cell::s("") : Cell::f(std::format("(F{1}-F{0})/(B{1}-B{0})*1000", R, Rn), t.grade * 1000, N2), Cell::n(t.fillArea, N2),
               Cell::n(t.cutArea, N2), lastRow ? Cell::s("") : Cell::f(std::format("(I{0}+I{1})/2*(B{1}-B{0})", R, Rn), fillV, N2),
               lastRow ? Cell::s("") : Cell::f(std::format("(J{0}+J{1})/2*(B{1}-B{0})", R, Rn), cutV, N2), Cell::n(width, N2)});
    }
    s.add({Cell::s(T(tr, "gui.bim.total"), TOT), Cell::f(std::format("B{}-B2", n + 1), m.length, TOT), {}, {}, {}, {}, {}, {}, {}, {},
           Cell::f(std::format("SUM(K2:K{})", n + 1), m.fillVolume, TOT), Cell::f(std::format("SUM(L2:L{})", n + 1), m.cutVolume, TOT)});

    Sheet& q = wb.add(T(tr, "xl.sh.quant"));
    q.add({hd(tr, "xl.c.work"), hd(tr, "xl.c.thick"), hd(tr, "xl.c.width"), hd(tr, "xl.c.len"), hd(tr, "xl.c.qty")});
    q.rows[0][0] = hd(tr, "gui.rd.pavement");
    q.widths = {44, 11, 12, 12, 14};
    const double w = 2 * (p.cat.lanes * p.cat.laneWidth / 2 + p.cat.strengthened);
    const std::string ss = sref(T(tr, "xl.sh.stations"));
    for (const auto& l : p.pavement) {
        const int row = static_cast<int>(q.rows.size());
        q.add({Cell::s(l.name.rfind("road.", 0) == 0 ? T(tr, l.name) : l.name), Cell::n(l.thickness, N3), Cell::n(w, N2),
               Cell::f(std::format("{}B{}", ss, n + 2), m.length, N2), Cell::f(std::format("B{0}*C{0}*D{0}", r1(row)), l.thickness * w * m.length, N2)});
    }
    q.add({});
    q.add({Cell::s(T(tr, "xl.asphalt")), {}, Cell::n(w, N2), Cell::f(std::format("{}B{}", ss, n + 2), m.length, N2),
           Cell::f(std::format("C{0}*D{0}", r1(static_cast<int>(q.rows.size()))), m.asphaltArea, TOT)});
    q.add({Cell::s(T(tr, "xl.c.fillV")), {}, {}, {}, Cell::f(std::format("{}K{}", ss, n + 2), m.fillVolume, TOT)});
    q.add({Cell::s(T(tr, "xl.c.cutV")), {}, {}, {}, Cell::f(std::format("{}L{}", ss, n + 2), m.cutVolume, TOT)});

    Sheet& pr = wb.add(T(tr, "xl.sh.params"));
    pr.add({hd(tr, "xl.c.key"), hd(tr, "xl.c.value"), hd(tr, "xl.c.desc")});
    pr.widths = {16, 14, 50};
    pr.autoFilter = false;
    auto row = [&](const char* key, double v, const char* desc) { pr.add({Cell::s(key), Cell::n(v, WARN), Cell::s(T(tr, desc))}); };
    pr.add({Cell::s("cat"), Cell::s(p.cat.id, WARN), Cell::s(T(tr, ("road.cat." + p.cat.id).c_str()))});
    row("lanes", p.cat.lanes, "gui.rd.lanes");
    row("laneWidth", p.cat.laneWidth, "gui.rd.lanew");
    row("shoulder", p.cat.shoulder, "gui.rd.shoulder");
    row("strengthened", p.cat.strengthened, "gui.rd.strength");
    row("crossfall", p.crossfall * 100, "gui.rd.crossfall");
    row("shoulderFall", p.shoulderFall * 100, "gui.rd.shfall");
    row("slopeFill", p.slopeFill, "gui.rd.slope");
    row("slopeCut", p.slopeCut, "gui.rd.slope");
    row("maxGrade", p.cat.maxGrade * 1000, "gui.rd.maxgrade");
    row("minRadius", p.cat.minRadius, "gui.rd.minradius");
    for (std::size_t i = 0; i < p.pavement.size(); ++i)
        pr.add({Cell::s("pav" + std::to_string(i + 1)), Cell::n(p.pavement[i].thickness, WARN),
                Cell::s(p.pavement[i].name.rfind("road.", 0) == 0 ? T(tr, p.pavement[i].name) : p.pavement[i].name)});

    std::vector<std::tuple<int, std::string, std::string, std::string>> iss;
    for (const auto& is : m.issues) iss.emplace_back(is.error ? 2 : 1, civil::pk(is.s), T(tr, is.key, is.args), "SNiP 2.05.02-85");
    issuesSheet(wb, tr, iss);
    return wb;
}

void roadFromWorkbook(const Workbook& wb, civil::RoadModel& m, civil::RoadParams& p, const Tr& tr) {
    if (const Sheet* pr = findSheet(wb, tr, "xl.sh.params", 2)) {
        const auto pm = readParams(*pr);
        pnum(pm, "lanes", [&](double v) { p.cat.lanes = std::max(1, static_cast<int>(std::lround(v))); });
        pnum(pm, "laneWidth", [&](double v) { p.cat.laneWidth = v; });
        pnum(pm, "shoulder", [&](double v) { p.cat.shoulder = v; });
        pnum(pm, "strengthened", [&](double v) { p.cat.strengthened = v; });
        pnum(pm, "crossfall", [&](double v) { p.crossfall = v / 100; });
        pnum(pm, "shoulderFall", [&](double v) { p.shoulderFall = v / 100; });
        pnum(pm, "slopeFill", [&](double v) { p.slopeFill = v; });
        pnum(pm, "slopeCut", [&](double v) { p.slopeCut = v; });
        pnum(pm, "maxGrade", [&](double v) { p.cat.maxGrade = v / 1000; });
        pnum(pm, "minRadius", [&](double v) { p.cat.minRadius = v; });
        for (std::size_t i = 0; i < p.pavement.size(); ++i)
            pnum(pm, ("pav" + std::to_string(i + 1)).c_str(), [&](double v) { p.pavement[i].thickness = v; });
    }
    const Sheet* s = findSheet(wb, tr, "xl.sh.stations", 0);
    if (!s) throw Error("xlsx.err.sheet", {T(tr, "xl.sh.stations")});
    for (std::size_t r = 1; r < s->rows.size(); ++r) {
        const auto ch = s->num(r, 1), z = s->num(r, 5);
        if (!ch || !z) continue;
        for (auto& t : m.st)
            if (std::abs(t.s - *ch) < 0.01) {
                if (std::abs(t.design - *z) > 1e-6) {
                    t.design = *z;
                    t.control = true;
                }
                break;
            }
    }
    civil::recomputeRoad(m, p);
}

// ---------------------------------------------------------------- suv yo'li
Workbook wwWorkbook(const civil::WwModel& m, const civil::WwParams& p, const Tr& tr) {
    Workbook wb;
    Sheet& s = wb.add(T(tr, "xl.sh.stations"));
    s.add({hd(tr, "xl.c.pk"), hd(tr, "xl.c.s"), Cell::s("X", H), Cell::s("Y", H), hd(tr, "xl.c.ground"), hd(tr, "ww.lbl.bottom"),
           hd(tr, "xl.c.grade"), hd(tr, "gui.ww.col.yn"), hd(tr, "gui.ww.col.v"), hd(tr, "ww.lbl.water"), hd(tr, "xl.c.cutA"), hd(tr, "xl.c.fillA"),
           hd(tr, "xl.c.cutV"), hd(tr, "xl.c.fillV")});
    s.widths = {13, 10, 12, 12, 11, 12, 10, 10, 9, 11, 10, 10, 11, 11};
    const int n = static_cast<int>(m.st.size());
    for (int i = 0; i < n; ++i) {
        const auto& t = m.st[static_cast<std::size_t>(i)];
        const std::string R = r1(i + 1), Rn = r1(i + 2);
        const bool lastRow = i + 1 == n;
        const auto* nx = lastRow ? nullptr : &m.st[static_cast<std::size_t>(i + 1)];
        const double cutV = nx ? (t.cut + nx->cut) / 2 * (nx->s - t.s) : 0;
        const double fillV = nx ? (t.fill + nx->fill) / 2 * (nx->s - t.s) : 0;
        s.add({Cell::s(civil::pk(t.s)), Cell::n(t.s, N2), Cell::n(t.x, N3), Cell::n(t.y, N3), Cell::n(t.ground, N2), Cell::n(t.bottom, WARN),
               lastRow ? Cell::s("") : Cell::f(std::format("(F{0}-F{1})/(B{1}-B{0})*1000", R, Rn), t.slope * 1000, N2), Cell::n(t.depthN, N3),
               Cell::n(t.v, N2), Cell::n(t.water, N2), Cell::n(t.cut, N2), Cell::n(t.fill, N2),
               lastRow ? Cell::s("") : Cell::f(std::format("(K{0}+K{1})/2*(B{1}-B{0})", R, Rn), cutV, N2),
               lastRow ? Cell::s("") : Cell::f(std::format("(L{0}+L{1})/2*(B{1}-B{0})", R, Rn), fillV, N2)});
    }
    s.add({Cell::s(T(tr, "gui.bim.total"), TOT), Cell::f(std::format("B{}-B2", n + 1), m.length, TOT), {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
           Cell::f(std::format("SUM(M2:M{})", n + 1), m.cutVolume, TOT), Cell::f(std::format("SUM(N2:N{})", n + 1), m.fillVolume, TOT)});

    Sheet& q = wb.add(T(tr, "xl.sh.quant"));
    q.add({hd(tr, "xl.c.work"), hd(tr, "xl.c.qty")});
    q.widths = {48, 16};
    const std::string ss = sref(T(tr, "xl.sh.stations"));
    q.add({Cell::s(T(tr, "gui.ww.q.len")), Cell::f(std::format("{}B{}", ss, n + 2), m.length, N2)});
    q.add({Cell::s(T(tr, "xl.c.cutV")), Cell::f(std::format("{}M{}", ss, n + 2), m.cutVolume, TOT)});
    q.add({Cell::s(T(tr, "xl.c.fillV")), Cell::f(std::format("{}N{}", ss, n + 2), m.fillVolume, TOT)});
    q.add({Cell::s(T(tr, "gui.ww.q.concrete")), Cell::n(m.concrete, TOT)});
    if (p.sec.type == civil::WwType::Flume) q.add({Cell::s(T(tr, "gui.ww.q.pieces")), Cell::n(m.pieces, INT)});
    q.add({Cell::s(T(tr, "gui.ww.q.capacity")), Cell::n(m.capacity, N3)});
    if (p.sec.type == civil::WwType::PressurePipe) q.add({Cell::s(T(tr, "gui.ww.q.headloss")), Cell::n(m.headLoss, N2)});

    Sheet& pr = wb.add(T(tr, "xl.sh.params"));
    pr.add({hd(tr, "xl.c.key"), hd(tr, "xl.c.value"), hd(tr, "xl.c.desc")});
    pr.widths = {16, 14, 50};
    pr.autoFilter = false;
    auto row = [&](const char* key, double v, const char* desc) { pr.add({Cell::s(key), Cell::n(v, WARN), Cell::s(T(tr, desc))}); };
    pr.add({Cell::s("type"), Cell::s(civil::wwTypeKey(p.sec.type), WARN), Cell::s(T(tr, std::string("ww.type.") + civil::wwTypeKey(p.sec.type)))});
    pr.add({Cell::s("name"), Cell::s(p.sec.name, WARN), Cell::s(T(tr, "gui.ww.section"))});
    row("q", p.q, "gui.ww.q");
    row("b", p.sec.b, "gui.ww.b");
    row("h", p.sec.h, "gui.ww.h");
    row("m", p.sec.m, "gui.ww.m");
    row("B", p.sec.B, "gui.ww.B");
    row("d", p.sec.d, "gui.ww.d");
    row("t", p.sec.t, "gui.ww.t");
    row("n", p.sec.n, "gui.ww.n");
    row("freeboard", p.sec.freeboard, "gui.ww.fb");
    row("vmin", p.sec.vmin, "gui.ww.vmin");
    row("vmax", p.sec.vmax, "gui.ww.vmax");
    row("pieceLen", p.sec.pieceLen, "gui.ww.piece");
    row("chw", p.sec.chw, "gui.ww.chw");
    row("step", p.step, "gui.ww.step");
    row("depth", p.depth, "gui.ww.depth");
    row("slope", p.slope * 1000, "gui.ww.slope");
    row("headStart", p.headStart, "gui.ww.head");

    std::vector<std::tuple<int, std::string, std::string, std::string>> iss;
    for (const auto& is : m.issues) iss.emplace_back(is.error ? 2 : 1, civil::pk(is.s), T(tr, is.key, is.args), "SNiP 2.06.03-85");
    issuesSheet(wb, tr, iss);
    return wb;
}

void wwFromWorkbook(const Workbook& wb, civil::WwModel& m, civil::WwParams& p, const Tr& tr) {
    if (const Sheet* pr = findSheet(wb, tr, "xl.sh.params", 2)) {
        const auto pm = readParams(*pr);
        pnum(pm, "q", [&](double v) { p.q = v; });
        pnum(pm, "b", [&](double v) { p.sec.b = v; });
        pnum(pm, "h", [&](double v) { p.sec.h = v; });
        pnum(pm, "m", [&](double v) { p.sec.m = v; });
        pnum(pm, "B", [&](double v) { p.sec.B = v; });
        pnum(pm, "d", [&](double v) { p.sec.d = v; });
        pnum(pm, "t", [&](double v) { p.sec.t = v; });
        pnum(pm, "n", [&](double v) { p.sec.n = v; });
        pnum(pm, "freeboard", [&](double v) { p.sec.freeboard = v; });
        pnum(pm, "vmin", [&](double v) { p.sec.vmin = v; });
        pnum(pm, "vmax", [&](double v) { p.sec.vmax = v; });
        pnum(pm, "pieceLen", [&](double v) { p.sec.pieceLen = v; });
        pnum(pm, "chw", [&](double v) { p.sec.chw = v; });
        pnum(pm, "step", [&](double v) { p.step = v; });
        pnum(pm, "depth", [&](double v) { p.depth = v; });
        pnum(pm, "slope", [&](double v) { p.slope = v / 1000; });
        pnum(pm, "headStart", [&](double v) { p.headStart = v; });
    }
    const Sheet* s = findSheet(wb, tr, "xl.sh.stations", 0);
    if (!s) throw Error("xlsx.err.sheet", {T(tr, "xl.sh.stations")});
    for (std::size_t r = 1; r < s->rows.size(); ++r) {
        const auto ch = s->num(r, 1), z = s->num(r, 5);
        if (!ch || !z) continue;
        for (auto& t : m.st)
            if (std::abs(t.s - *ch) < 0.01) {
                if (std::abs(t.bottom - *z) > 1e-6) {
                    t.bottom = *z;
                    t.control = true;
                }
                break;
            }
    }
    civil::recomputeWaterway(m, p);
}

} // namespace asiacad::excel
