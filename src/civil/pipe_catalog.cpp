#include <asiacad/civil/pipe_catalog.h>

#include <asiacad/i18n/json.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace asiacad::civil {

namespace {
constexpr double kPi = 3.14159265358979323846;

struct MatInfo {
    PipeMaterial m;
    const char* key;
    double density;  // t/m³
    int aci;
    unsigned rgb;
    bool metal;
};
constexpr MatInfo kMats[] = {
    {PipeMaterial::PVC, "PVC", 1.40, 30, 0xC8632B, false},             // RAL 8023 — to'q sariq-jigarrang
    {PipeMaterial::PP, "PP", 0.91, 8, 0x8A8F96, false},                // gofrirlangan PP — kulrang
    {PipeMaterial::PE, "PE", 0.955, 250, 0x2B2D31, false},             // PE100 — qora
    {PipeMaterial::AsbestosCement, "AC", 1.75, 9, 0xA3A8A4, false},     // asbest-sement — och kulrang
    {PipeMaterial::Steel, "STEEL", 7.85, 5, 0x5B6570, true},            // po'lat
    {PipeMaterial::DuctileIron, "DI", 7.05, 251, 0x3E4047, true},       // VChShG cho'yan — bitum qora
    {PipeMaterial::Concrete, "RC", 2.50, 253, 0xB9B3A6, false},         // temir-beton
};
const MatInfo& info(PipeMaterial m) {
    for (const auto& i : kMats)
        if (i.m == m) return i;
    return kMats[0];
}

PipeSpec make(PipeMaterial m, const char* standard, const char* klass, int dn, double od, double wall, double n,
              bool verified, bool pressure = false, bool socket = true, double mass = 0) {
    PipeSpec s;
    s.material = m;
    s.standard = standard;
    s.klass = klass;
    s.dn = dn;
    s.od = od;
    s.wall = wall;
    s.manningN = n;
    s.verified = verified;
    s.pressure = pressure;
    s.socket = socket;
    s.massKgM = mass;
    s.id = std::format("{}-{}-{}", info(m).key, klass, dn);
    return s;
}

PipeCatalog buildCatalog() {
    PipeCatalog c;
    auto& v = c.specs_;
    // PVC-U bosimsiz (EN 1401, GOST R 54475): SN4 = SDR41, SN8 = SDR34 — devor = OD/SDR (min 3.2)
    for (auto [od, w4, w8] : {std::tuple{110, 3.2, 3.2}, {160, 4.0, 4.7}, {200, 4.9, 5.9}, {250, 6.2, 7.3},
                              {315, 7.7, 9.2}, {400, 9.8, 11.7}, {500, 12.3, 14.6}}) {
        v.push_back(make(PipeMaterial::PVC, "EN 1401 / GOST R 54475", "SN4", od, od, w4, 0.011, true));
        v.push_back(make(PipeMaterial::PVC, "EN 1401 / GOST R 54475", "SN8", od, od, w8, 0.011, true));
    }
    // PP gofrirlangan ikki qatlamli SN8 (ichki diametr bo'yicha) — ishlab chiqaruvchiga bog'liq, namunaviy
    for (auto [id, od] : {std::pair{150, 173.0}, {200, 229.0}, {250, 288.0}, {300, 343.0}, {400, 458.0}, {500, 573.0}})
        v.push_back(make(PipeMaterial::PP, "GOST R 54475 (gofra)", "SN8", id, od, (od - id) / 2, 0.011, false, false, true,
                         0));
    // PE100 bosimli (GOST 18599): SDR17 (PN10), SDR11 (PN16) — payvand ulanish
    for (auto [od, w17, w11] : {std::tuple{110, 6.6, 10.0}, {160, 9.5, 14.6}, {200, 11.9, 18.2}, {225, 13.4, 20.5},
                                {250, 14.8, 22.7}, {315, 18.7, 28.6}, {400, 23.7, 36.3}}) {
        v.push_back(make(PipeMaterial::PE, "GOST 18599 PE100", "SDR17", od, od, w17, 0.010, true, true, false));
        v.push_back(make(PipeMaterial::PE, "GOST 18599 PE100", "SDR11", od, od, w11, 0.010, true, true, false));
    }
    // Asbest-sement bosimsiz BNT (GOST 1839-80, 1-jadval) — massa jadvaldan
    for (auto [dn, od, w, m] : {std::tuple{100, 118.0, 9.0, 6.1}, {150, 161.0, 10.0, 9.4}, {200, 211.0, 11.0, 13.2},
                                {300, 307.0, 14.0, 25.0}, {400, 402.0, 17.0, 40.5}})
        v.push_back(make(PipeMaterial::AsbestosCement, "GOST 1839-80", "BNT", dn, od, w, 0.012, true, false, false, m));
    // Po'lat elektr payvandli (GOST 10704-91) — devor loyihaga ko'ra tanlanadi, bu yerda namunaviy
    for (auto [dn, od, w] : {std::tuple{100, 108.0, 4.0}, {150, 159.0, 4.5}, {200, 219.0, 6.0}, {250, 273.0, 7.0},
                             {300, 325.0, 8.0}, {350, 377.0, 9.0}, {400, 426.0, 9.0}, {500, 530.0, 8.0}})
        v.push_back(make(PipeMaterial::Steel, "GOST 10704-91", "St20", dn, od, w, 0.012, false, true, false));
    // Cho'yan VChShG K9 (GOST ISO 2531): e = 9·(0.5 + 0.001·DN), kamida 6 mm
    for (auto [dn, od] : {std::pair{100, 118.0}, {150, 170.0}, {200, 222.0}, {250, 274.0}, {300, 326.0}, {400, 429.0},
                          {500, 532.0}}) {
        const double e = std::max(6.0, std::round(9 * (0.5 + 0.001 * dn) * 10) / 10);
        v.push_back(make(PipeMaterial::DuctileIron, "GOST ISO 2531", "K9", dn, od, e, 0.013, true, true, true));
    }
    // Temir-beton bosimsiz TS (GOST 6482) — ichki diametr bo'yicha; devor qalinligi namunaviy
    for (auto [dn, t] : {std::pair{400, 50.0}, {500, 55.0}, {600, 60.0}, {800, 80.0}, {1000, 90.0}, {1200, 100.0}})
        v.push_back(make(PipeMaterial::Concrete, "GOST 6482", "TS", dn, dn + 2 * t, t, 0.014, false));

    auto tpl = [&](const char* id, const char* sys, const char* spec, double slope, double depth, double mh, bool street) {
        PipeTemplate t;
        t.id = id;
        t.name = std::string("tpl.") + id;
        t.system = sys;
        t.specId = spec;
        t.slope = slope;
        t.startDepth = depth;
        t.manholeD = mh;
        t.street = street;
        c.templates.push_back(t);
    };
    tpl("yard_pvc160", "K1", "PVC-SN4-160", 0.008, 1.2, 1.0, false);
    tpl("street_pvc200", "K1", "PVC-SN8-200", 0.007, 1.8, 1.0, true);
    tpl("collector_pvc315", "K1", "PVC-SN8-315", 0.0035, 2.5, 1.0, true);
    tpl("gofra_pp300", "K1", "PP-SN8-300", 0.0035, 2.5, 1.0, true);
    tpl("repair_ac200", "K1", "AC-BNT-200", 0.007, 1.8, 1.0, true);
    tpl("storm_rc500", "K2", "RC-TS-500", 0.002, 2.0, 1.0, true);
    tpl("pressure_pe160", "N", "PE-SDR17-160", 0.0, 1.8, 1.0, true);
    tpl("casing_steel400", "K1", "STEEL-St20-400", 0.005, 2.5, 1.5, true);
    return c;
}

std::string esc(const std::string& s) {
    std::string o;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') o += '\\';
        if (ch == '\n') { o += "\\n"; continue; }
        o += ch;
    }
    return o;
}
} // namespace

const char* materialKey(PipeMaterial m) { return info(m).key; }
bool materialFromKey(const std::string& k, PipeMaterial& out) {
    for (const auto& i : kMats)
        if (k == i.key) { out = i.m; return true; }
    return false;
}
bool isMetal(PipeMaterial m) { return info(m).metal; }
double materialDensity(PipeMaterial m) { return info(m).density; }
int materialAci(PipeMaterial m) { return info(m).aci; }
unsigned materialRgb(PipeMaterial m) { return info(m).rgb; }

double PipeSpec::mass() const {
    if (massKgM > 0) return massKgM;
    // halqa kesimi maydoni (m²) × zichlik (t/m³) × 1000 = kg/m
    const double ro = od / 2000.0, ri = id_mm() / 2000.0;
    return kPi * (ro * ro - ri * ri) * materialDensity(material) * 1000.0;
}

std::string PipeSpec::label() const {
    return std::format("{} {} \xC3\x98{}\xC3\x97{:g}", materialKey(material), klass, dn, wall);
}

const PipeCatalog& PipeCatalog::builtin() {
    static const PipeCatalog c = buildCatalog();
    return c;
}

const PipeSpec* PipeCatalog::find(const std::string& id) const {
    for (const auto& s : specs_)
        if (s.id == id) return &s;
    return nullptr;
}

std::vector<const PipeSpec*> PipeCatalog::byMaterial(PipeMaterial m) const {
    std::vector<const PipeSpec*> out;
    for (const auto& s : specs_)
        if (s.material == m) out.push_back(&s);
    return out;
}

std::vector<PipeMaterial> PipeCatalog::materials() const {
    std::vector<PipeMaterial> out;
    for (const auto& s : specs_)
        if (std::find(out.begin(), out.end(), s.material) == out.end()) out.push_back(s.material);
    return out;
}

std::string PipeCatalog::templatesToJson(const std::vector<PipeTemplate>& ts) {
    std::ostringstream os;
    os << "[\n";
    for (std::size_t i = 0; i < ts.size(); ++i) {
        const auto& t = ts[i];
        os << std::format(
            "  {{\"id\":\"{}\",\"name\":\"{}\",\"system\":\"{}\",\"spec\":\"{}\",\"slope\":{},\"depth\":{},\"manhole\":{},"
            "\"street\":{}}}{}\n",
            esc(t.id), esc(t.name), esc(t.system), esc(t.specId), t.slope, t.startDepth, t.manholeD,
            t.street ? "true" : "false", i + 1 < ts.size() ? "," : "");
    }
    os << "]\n";
    return os.str();
}

std::vector<PipeTemplate> PipeCatalog::templatesFromJson(const std::string& text) {
    const json::Value root = json::parse(text);
    if (!root.isArray()) throw std::runtime_error("templates: array expected");
    std::vector<PipeTemplate> out;
    for (const auto& v : root.arr()) {
        if (!v.isObject()) continue;
        auto s = [&](const char* k, const char* d) {
            const json::Value* x = v.find(k);
            return x && x->isString() ? x->str() : std::string(d);
        };
        auto n = [&](const char* k, double d) {
            const json::Value* x = v.find(k);
            return x && x->isNumber() ? x->num() : d;
        };
        PipeTemplate t;
        t.id = s("id", "");
        t.name = s("name", t.id.c_str());
        t.system = s("system", "K1");
        t.specId = s("spec", "");
        t.slope = n("slope", 0.008);
        t.startDepth = n("depth", 1.5);
        t.manholeD = n("manhole", 1.0);
        const json::Value* st = v.find("street");
        t.street = st && st->isBool() && st->boolean();
        t.user = true;
        if (!t.id.empty() && builtin().find(t.specId)) out.push_back(t);
    }
    return out;
}

std::vector<PipeTemplate> PipeCatalog::loadTemplates(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return templatesFromJson(ss.str());
}

void PipeCatalog::saveTemplates(const std::string& path, const std::vector<PipeTemplate>& t) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << templatesToJson(t);
}

} // namespace asiacad::civil
