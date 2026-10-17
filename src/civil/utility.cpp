#include <asiacad/civil/utility.h>

#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/json.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <cmath>
#include <format>
#include <set>

namespace asiacad::civil {

namespace {
constexpr double kPI = 3.14159265358979323846;
const UtilKind kAll[] = {UtilKind::Water, UtilKind::Sewer, UtilKind::Storm, UtilKind::Gas, UtilKind::Heat, UtilKind::Power,
                         UtilKind::Telecom, UtilKind::Drain, UtilKind::Other, UtilKind::Ground, UtilKind::Ignore};
std::string f2(double v) {
    std::string s = std::format("{:.2f}", v);
    for (auto& c : s) if (c == '.') c = ',';
    return s;
}

struct Label {
    double x, y, value;
    char type;
    std::string layer;
    int src = 0;
    bool used = false;
};
struct TextLbl {          // quduq nomi ("070A") yoki material ("MBB", "PVC")
    double x, y;
    std::string text;
    int src = 0;
};
struct Leader {
    double ax, ay, bx, by;
    int src = 0;
};
// Faqat yozuvlar qatlami: undagi chiziqlar — yo'naltiruvchi (leader), aylanalar — yozuv ramkasi, quvur emas
UtilNode makeNode(UtilKind k, const std::string& layer, double x, double y, double r) {
    UtilNode n;
    n.kind = k;
    n.layer = layer;
    n.x = x;
    n.y = y;
    n.r = r;
    return n;
}
bool labelLayer(const std::string& layer) {
    const std::string u = utf8::toUpper(layer);
    for (const char* k : {"ETIKET", "ETİKET", "LABEL", "YOZUV", "PODPIS", "ПОДПИС", "НАДПИС", "TEXT", "MATN", "ANNO", "YAZI"})
        if (u.find(utf8::toUpper(k)) != std::string::npos) return true;
    return false;
}
// Quduq nomi: "070A", "K-12", "КК5", "B12a" — raqam va harf bor, nuqta/ikki nuqta yo'q, qisqa
bool nodeName(const std::string& s0) {
    std::string s;
    for (char c : s0)
        if (c != ' ') s += c;
    const auto u = utf8::decode(s);
    if (u.empty() || u.size() > 8) return false;
    bool dig = false, let = false;
    for (char32_t c : u) {
        if (c >= U'0' && c <= U'9') dig = true;
        else if ((c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') || (c >= U'А' && c <= U'я') || c == U'-') let = let || c != U'-';
        else return false;
    }
    return dig && let && u.size() >= 2;
}
// Material: faqat harflar, 2..8 ta ("MBB", "PVC", "HDPE", "KORUGE", "ЧУГ")
bool materialName(const std::string& s0) {
    std::string s;
    for (char c : s0)
        if (c != ' ') s += c;
    const auto u = utf8::decode(s);
    if (u.size() < 2 || u.size() > 8) return false;
    for (char32_t c : u)
        if (!((c >= U'A' && c <= U'Z') || (c >= U'А' && c <= U'Я') || c == U'Ç' || c == U'Ş' || c == U'Ü' || c == U'Ö' || c == U'Ğ' || c == U'İ' ||
              c == U'-'))
            return false;
    return u.front() != U'-' && u.back() != U'-';  // "MBB", "PE-BA", "PVC", "KRG" 
}

double segDist(double px, double py, const UtilVertex& a, const UtilVertex& b, double* t = nullptr) {
    const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    double u = l2 > 0 ? ((px - a.x) * dx + (py - a.y) * dy) / l2 : 0;
    u = std::clamp(u, 0.0, 1.0);
    if (t) *t = u;
    return std::hypot(a.x + u * dx - px, a.y + u * dy - py);
}

// Matnni ASCII ko'rinishga yaqinlashtirish: kirill "Н/н/Д/у" va o'nlik vergul
std::string norm(const std::string& s) {
    std::string o = s;
    auto rep = [&](const std::string& a, const std::string& b) {
        for (std::size_t p = 0; (p = o.find(a, p)) != std::string::npos; p += b.size()) o.replace(p, a.size(), b);
    };
    rep("\xC3\x98", "@");           // Ø
    rep("\xC3\xB8", "@");           // ø
    rep("\xE2\x8C\x80", "@");       // ⌀
    rep("%%C", "@");
    rep("%%c", "@");
    rep("\xD0\x94\xD1\x83", "DN");  // Ду
    rep("\xD0\x94\xD0\xA3", "DN");  // ДУ
    rep("\xD0\x9D=", "H=");         // Н=
    rep("\xD0\xBD=", "h=");         // н=
    rep("\xE2\x96\xBD", "~");       // ▽
    rep("\xE2\x88\x87", "~");       // ∇
    rep("\xD0\xBE\xD1\x82\xD0\xBC.", "~");  // отм.
    rep("\xD0\xBE\xD1\x82\xD0\xBC", "~");   // отм
    for (auto& c : o)
        if (c == ',') c = '.';
    return o;
}
bool readNum(const std::string& s, std::size_t p, double& v, std::size_t* end = nullptr) {
    while (p < s.size() && (s[p] == ' ' || s[p] == '=' || s[p] == ':')) ++p;
    std::size_t q = p;
    if (q < s.size() && (s[q] == '+' || s[q] == '-')) ++q;
    bool digit = false;
    while (q < s.size() && (std::isdigit(static_cast<unsigned char>(s[q])) || s[q] == '.')) { digit |= s[q] != '.'; ++q; }
    if (!digit) return false;
    try { v = std::stod(s.substr(p, q - p)); } catch (...) { return false; }
    if (end) *end = q;
    return true;
}
} // namespace

const char* utilKey(UtilKind k) {
    switch (k) {
        case UtilKind::Water: return "WATER";
        case UtilKind::Sewer: return "SEWER";
        case UtilKind::Storm: return "STORM";
        case UtilKind::Gas: return "GAS";
        case UtilKind::Heat: return "HEAT";
        case UtilKind::Power: return "POWER";
        case UtilKind::Telecom: return "TELECOM";
        case UtilKind::Drain: return "DRAIN";
        case UtilKind::Other: return "OTHER";
        case UtilKind::Ground: return "GROUND";
        case UtilKind::Ignore: return "IGNORE";
    }
    return "IGNORE";
}
bool utilFromKey(const std::string& s, UtilKind& out) {
    for (UtilKind k : kAll)
        if (s == utilKey(k)) { out = k; return true; }
    return false;
}
bool isGravity(UtilKind k) { return k == UtilKind::Sewer || k == UtilKind::Storm || k == UtilKind::Drain; }
bool isCable(UtilKind k) { return k == UtilKind::Power || k == UtilKind::Telecom; }
std::vector<UtilKind> utilKinds() {
    return {UtilKind::Water, UtilKind::Sewer, UtilKind::Storm, UtilKind::Gas, UtilKind::Heat, UtilKind::Power, UtilKind::Telecom,
            UtilKind::Drain, UtilKind::Other};
}

UtilKind guessUtil(const std::string& layer) {
    const std::string u = utf8::toUpper(layer);
    auto has = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys)
            if (u.find(utf8::toUpper(k)) != std::string::npos) return true;
        return false;
    };
    auto starts = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys) {
            const std::string K = utf8::toUpper(k);
            if (u.rfind(K, 0) == 0 && (u.size() == K.size() || !std::isalpha(static_cast<unsigned char>(u[K.size()])))) return true;
        }
        return false;
    };
    if (!u.empty() && u[0] == '@') return UtilKind::Ignore;  // NetCAD izoh/xref qatlamlari ("@Kapak_ASu", "@XREF_YOL")
    // so'zlar (Turkiya: ASU — atık su, YSU — yağmur suyu, ISU/İSU — içme suyu)
    std::set<std::string> tok;
    {
        std::string cur;
        for (char c : u) {
            if (c == '_' || c == '-' || c == ' ' || c == '.' || c == '|' || c == '$') { if (!cur.empty()) tok.insert(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) tok.insert(cur);
    }
    auto tk = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys)
            if (tok.count(utf8::toUpper(k))) return true;
        return false;
    };
    if (tk({"YSU", "YAGMUR", "YAĞMUR"})) return UtilKind::Storm;
    if (tk({"ASU", "ATIKSU", "ATIK"})) return UtilKind::Sewer;
    if (tk({"ISU", "İSU", "ICMESU", "İÇMESU"})) return UtilKind::Water;
    if (has({"YOMG", "ЁМҒ", "ЛИВН", "ДОЖД", "STORM", "YAĞMUR"}) || starts({"К2", "K2"})) return UtilKind::Storm;
    if (has({"DRENAJ", "ДРЕН", "DRAIN"})) return UtilKind::Drain;
    if (has({"KANAL", "КАНАЛ", "SEWER", "ATIKSU"}) || starts({"К1", "K1", "К3", "K3", "К", "K"})) return UtilKind::Sewer;
    if (has({"SUV", "VODO", "ВОДО", "WATER", "İÇMESUYU", "ICMESUYU"}) || starts({"В1", "B1", "В2", "B2", "В", "V"})) return UtilKind::Water;
    if (has({"GAZ", "ГАЗ", "GAS", "DOĞALGAZ"}) || starts({"Г", "G"})) return UtilKind::Gas;
    if (has({"ISSIQ", "ИССИҚ", "ТЕПЛ", "HEAT", "ISITMA"}) || starts({"ТС", "TS", "T1", "Т1"})) return UtilKind::Heat;
    if (has({"ELEKTR", "ЭЛЕКТР", "КАБЕЛ", "KABEL", "CABLE", "POWER", "0.4", "10KV", "10КВ", "ЛЭП"}) || starts({"W", "Э"})) return UtilKind::Power;
    if (has({"ALOQA", "АЛОҚА", "СВЯЗ", "TELEKOM", "TELECOM", "ТЕЛЕФ", "TELEF", "OPTIK", "ОПТИ", "FIBER"})) return UtilKind::Telecom;
    if (has({"YER_SATH", "YER SATH", "YERSATH", "REL'EF", "RELYEF", "RELEF", "РЕЛЬЕФ", "ПОВЕРХН", "GROUND", "TERRAIN", "ARAZİ", "TOPO",
             "ОТМЕТК", "BELGI", "ZEMIN", "ZEMİN"}))
        return UtilKind::Ground;
    return UtilKind::Ignore;
}

UtilParams defaultUtilParams() {
    UtilParams p;
    p.depth = {{UtilKind::Water, 1.5}, {UtilKind::Sewer, 1.3}, {UtilKind::Storm, 1.0}, {UtilKind::Gas, 1.0}, {UtilKind::Heat, 1.0},
               {UtilKind::Power, 0.8}, {UtilKind::Telecom, 0.7}, {UtilKind::Drain, 1.0}, {UtilKind::Other, 1.0}};
    p.dn = {{UtilKind::Water, 110}, {UtilKind::Sewer, 200}, {UtilKind::Storm, 300}, {UtilKind::Gas, 63}, {UtilKind::Heat, 159},
            {UtilKind::Power, 50}, {UtilKind::Telecom, 110}, {UtilKind::Drain, 160}, {UtilKind::Other, 100}};
    // eng kam qatlam: kanalizatsiya 0,7 (SNiP 2.04.03-85 4.8); qolganlari — umumiy amaliyot, loyihada aniqlang
    p.minCover = {{UtilKind::Water, 1.0}, {UtilKind::Sewer, 0.7}, {UtilKind::Storm, 0.7}, {UtilKind::Gas, 0.8}, {UtilKind::Heat, 0.5},
                  {UtilKind::Power, 0.7}, {UtilKind::Telecom, 0.5}, {UtilKind::Drain, 0.7}, {UtilKind::Other, 0.5}};
    return p;
}

double UtilLine::length() const {
    double l = 0;
    for (std::size_t i = 0; i + 1 < v.size(); ++i) l += std::sqrt(std::pow(v[i + 1].x - v[i].x, 2) + std::pow(v[i + 1].y - v[i].y, 2) + std::pow(v[i + 1].z - v[i].z, 2));
    return l;
}
int UtilModel::errors() const {
    int n = 0;
    for (const auto& i : issues) n += i.error;
    return n;
}

char parseUtilLabel(const std::string& text, double& value) {
    const std::string s = norm(text);
    {   // nishablik: "1:365", "1/365", "i=0.005", "i=5‰"
        std::string t;
        for (char c : s)
            if (c != ' ') t += c;
        if (t.size() > 2 && t[0] == '1' && (t[1] == ':' || t[1] == '/')) {
            double v;
            std::size_t end = 0;
            if (readNum(t, 2, v, &end) && end == t.size() && v >= 10 && v <= 100000) { value = 1.0 / v; return 's'; }
        }
        if (t.size() > 2 && (t[0] == 'i' || t[0] == 'I') && t[1] == '=') {
            double v;
            std::size_t end = 0;
            if (readNum(t, 2, v, &end)) {
                const bool permil = t.find("\xE2\x80\xB0") != std::string::npos;
                if (permil) v /= 1000;
                if (v > 0 && v < 0.5) { value = v; return 's'; }
            }
        }
        // uzunlik: "48.1m", "48.1 m", "L=48.1"
        double v;
        std::size_t end = 0;
        if (t.size() > 1 && t.back() == 'm' && readNum(t, 0, v, &end) && end + 1 == t.size() && v > 0.5 && v < 10000) { value = v; return 'l'; }
        if (t.size() > 2 && (t[0] == 'L' || t[0] == 'l') && t[1] == '=' && readNum(t, 2, v, &end) && (end == t.size() || (end + 1 == t.size() && t.back() == 'm')) &&
            v > 0.5 && v < 10000) { value = v; return 'l'; }
    }
    // diametr: @200, DN300, d=110 (oldidagi "2@" — soni)
    for (const char* mk : {"@", "DN", "dn", "Dn", "d=", "D="}) {
        const std::size_t p = s.find(mk);
        if (p == std::string::npos) continue;
        double v;
        if (readNum(s, p + std::strlen(mk), v) && v >= 10 && v <= 3000) { value = v; return 'd'; }
    }
    // chuqurlik: h=1.80, H=2.1
    for (const char* mk : {"h=", "H="}) {
        const std::size_t p = s.find(mk);
        double v;
        if (p != std::string::npos && readNum(s, p + 2, v) && v > 0 && v < 30) { value = v; return 'h'; }
    }
    // belgi: ~98.45, yoki faqat son (kasr qismli)
    if (const std::size_t p = s.find('~'); p != std::string::npos) {
        double v;
        if (readNum(s, p + 1, v)) { value = v; return 'z'; }
    }
    std::string t;
    for (char c : s)
        if (c != ' ') t += c;
    if (!t.empty() && t.find('.') != std::string::npos) {
        double v;
        std::size_t end = 0;
        if (readNum(t, 0, v, &end) && end == t.size() && std::abs(v) >= 1) { value = v; return 'z'; }
    }
    return 0;
}

namespace {
// Quvur ko'pincha bir nechta parallel chiziq bilan chiziladi (tashqi/ichki devor, o'q): uzunligi va uchlari mos,
// bir-biridan bundleWidth ichida — bitta o'q chizig'iga (o'rtadagisi) birlashtiriladi.
void mergeBundles(UtilModel& m, std::vector<int>& lineSrc, double width) {
    const std::size_t n = m.lines.size();
    std::vector<int> parent(n);
    for (std::size_t i = 0; i < n; ++i) parent[i] = static_cast<int>(i);
    std::function<int(int)> root = [&](int i) { return parent[static_cast<std::size_t>(i)] == i ? i : parent[static_cast<std::size_t>(i)] = root(parent[static_cast<std::size_t>(i)]); };
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < n; ++i)
        if (m.lines[i].v.size() == 2) idx.push_back(i);
    auto mid = [&](std::size_t i) { return std::array<double, 2>{(m.lines[i].v[0].x + m.lines[i].v[1].x) / 2, (m.lines[i].v[0].y + m.lines[i].v[1].y) / 2}; };
    std::sort(idx.begin(), idx.end(), [&](auto a, auto b) { return mid(a)[0] < mid(b)[0]; });
    for (std::size_t a = 0; a < idx.size(); ++a) {
        const auto& A = m.lines[idx[a]];
        const double ax = A.v[1].x - A.v[0].x, ay = A.v[1].y - A.v[0].y, la = std::hypot(ax, ay);
        if (la < 1e-6) continue;
        for (std::size_t b = a + 1; b < idx.size() && mid(idx[b])[0] - mid(idx[a])[0] <= width; ++b) {
            const auto& B = m.lines[idx[b]];
            if (B.kind != A.kind || lineSrc[idx[a]] != lineSrc[idx[b]]) continue;
            const double bx = B.v[1].x - B.v[0].x, by = B.v[1].y - B.v[0].y, lb = std::hypot(bx, by);
            if (lb < 1e-6 || std::abs(la - lb) > std::max(0.3, 0.03 * la)) continue;
            if (std::abs(ax * by - ay * bx) / (la * lb) > 0.02) continue;
            const auto ma = mid(idx[a]), mb = mid(idx[b]);
            if (std::hypot(ma[0] - mb[0], ma[1] - mb[1]) > width) continue;
            parent[static_cast<std::size_t>(root(static_cast<int>(idx[a])))] = root(static_cast<int>(idx[b]));
        }
    }
    std::map<int, std::vector<std::size_t>> groups;
    for (auto i : idx) groups[root(static_cast<int>(i))].push_back(i);
    std::vector<bool> drop(n, false);
    for (auto& [g, mem] : groups) {
        if (mem.size() < 2) continue;
        // o'rtadagi chiziq (birinchisiga nisbatan perpendikulyar siljish medianasi)
        const auto& F = m.lines[mem[0]];
        const double fx = F.v[1].x - F.v[0].x, fy = F.v[1].y - F.v[0].y, fl = std::hypot(fx, fy);
        std::vector<std::pair<double, std::size_t>> off;
        for (auto i : mem) {
            const auto c = mid(i);
            off.push_back({((c[0] - F.v[0].x) * -fy + (c[1] - F.v[0].y) * fx) / fl, i});
        }
        std::sort(off.begin(), off.end());
        const std::size_t keep = off[off.size() / 2].second;
        for (auto i : mem)
            if (i != keep) drop[i] = true;
    }
    std::vector<UtilLine> out;
    std::vector<int> src;
    for (std::size_t i = 0; i < n; ++i)
        if (!drop[i]) { out.push_back(std::move(m.lines[i])); src.push_back(lineSrc[i]); }
    for (std::size_t i = 0; i < out.size(); ++i) out[i].id = static_cast<int>(i) + 1;
    m.lines = std::move(out);
    lineSrc = std::move(src);
}
} // namespace

std::map<std::string, UtilKind> autoUtilLayers(const Document& doc) {
    std::map<std::string, UtilKind> m;
    for (const auto& [name, l] : doc.layers()) m[name] = guessUtil(name);
    return m;
}

UtilModel readUtilities(const Document& doc, const std::map<std::string, UtilKind>& layers, const UtilParams& p) {
    return readUtilities(std::vector<UtilSource>{{&doc, layers, UtilKind::Ignore, ""}}, p);
}

UtilModel readUtilities(const std::vector<UtilSource>& sources, const UtilParams& p) {
    UtilModel m;
    std::vector<Label> labels;
    std::vector<TextLbl> names, materials;
    std::vector<Leader> leaders;
    std::vector<std::array<double, 3>> groundPts;
    std::vector<int> lineSrc;  // chiziq -> manba
    int cur = 0;
    const UtilSource* src = nullptr;
    bool srcHasKnown = false;
    auto kindOf = [&](const std::string& layer) {
        auto it = src->layers.find(layer);
        const UtilKind k = it != src->layers.end() ? it->second : guessUtil(layer);
        // fayl uchun tur majburiy berilgan bo'lsa: nomidan tarmoq ekani tanilgan qatlamlar shu turga o'tadi
        // (genplan/yo'l xreflari, "0" qatlam — yo'q); faylda bunday qatlam umuman bo'lmasa — yer sathidan boshqa hammasi.
        if (src->force != UtilKind::Ignore && k != UtilKind::Ground) {
            if (!srcHasKnown) return src->force;
            return (k == UtilKind::Ignore || k == UtilKind::Other) ? UtilKind::Ignore : src->force;
        }
        return k;
    };
    std::function<void(const IEntity&)> visit = [&](const IEntity& e) {
        if (auto* ins = dynamic_cast<const InsertEntity*>(&e)) {
            for (const auto& c : ins->explode()) visit(*c);
            for (const auto& a : ins->attributes()) visit(a.text);
            return;
        }
        if (!e.layer().empty() && e.layer()[0] == '@') return;  // izoh bloklari, xref
        if (labelLayer(e.layer())) {
            if (auto* t = dynamic_cast<const TextEntity*>(&e)) {
                double v;
                if (const char c = parseUtilLabel(t->text(), v)) labels.push_back({t->insertion().x, t->insertion().y, v, c, e.layer(), cur});
                else if (nodeName(t->text())) names.push_back({t->insertion().x, t->insertion().y, t->text(), cur});
                else if (materialName(t->text())) materials.push_back({t->insertion().x, t->insertion().y, t->text(), cur});
            } else if (auto* l = dynamic_cast<const LineEntity*>(&e)) {
                leaders.push_back({l->startPoint().x, l->startPoint().y, l->endPoint().x, l->endPoint().y, cur});
            }
            return;
        }
        const UtilKind k = kindOf(e.layer());
        if (k == UtilKind::Ignore) {
            // tarmoq qatlamida bo'lmagan yozuvlar ham (masalan "YOZUV" qatlami) — diametr/belgi bo'lishi mumkin
            if (auto* t = dynamic_cast<const TextEntity*>(&e)) {
                double v;
                if (const char c = parseUtilLabel(t->text(), v)) labels.push_back({t->insertion().x, t->insertion().y, v, c, e.layer(), cur});
            }
            return;
        }
        if (auto* t = dynamic_cast<const TextEntity*>(&e)) {
            double v;
            const char c = parseUtilLabel(t->text(), v);
            if (k == UtilKind::Ground && c == 'z') groundPts.push_back({t->insertion().x, t->insertion().y, v});
            else if (c) labels.push_back({t->insertion().x, t->insertion().y, v, c, e.layer(), cur});
            return;
        }
        if (k == UtilKind::Ground) {
            if (auto* pt = dynamic_cast<const PointEntity*>(&e)) groundPts.push_back({pt->position().x, pt->position().y, pt->position().z});
            return;
        }
        if (auto* c = dynamic_cast<const CircleEntity*>(&e)) {
            if (c->radius() >= 0.2 && c->radius() <= 3.0) m.nodes.push_back(makeNode(k, e.layer(), c->center().x, c->center().y, c->radius()));
            return;
        }
        std::vector<Vec3> pts;
        if (auto* l = dynamic_cast<const LineEntity*>(&e)) pts = {l->startPoint(), l->endPoint()};
        else if (auto* pl = dynamic_cast<const PolylineEntity*>(&e)) {
            pts = pl->vertices();
            if (pl->isClosed() && pts.size() > 2) {  // yopiq kontur — kamera/quduq sifatida
                double cx = 0, cy = 0;
                for (const auto& q : pts) { cx += q.x; cy += q.y; }
                cx /= static_cast<double>(pts.size());
                cy /= static_cast<double>(pts.size());
                double r = 0;
                for (const auto& q : pts) r = std::max(r, std::hypot(q.x - cx, q.y - cy));
                if (r <= 3.0) { m.nodes.push_back(makeNode(k, e.layer(), cx, cy, r)); return; }
                pts.push_back(pts.front());
            }
        }
        if (pts.size() < 2) return;
        UtilLine ln;
        ln.id = static_cast<int>(m.lines.size()) + 1;
        ln.kind = k;
        ln.layer = src->tag.empty() ? e.layer() : src->tag + " · " + e.layer();
        lineSrc.push_back(cur);
        bool hasZ = false;
        for (const auto& q : pts) hasZ |= std::abs(q.z) > 1e-9;
        for (const auto& q : pts) ln.v.push_back({q.x, q.y, q.z, 0, hasZ ? UtilVertex::FromZ : UtilVertex::Default});
        m.lines.push_back(std::move(ln));
    };
    for (std::size_t si = 0; si < sources.size(); ++si) {
        src = &sources[si];
        cur = static_cast<int>(si);
        srcHasKnown = false;
        if (src->doc && src->force != UtilKind::Ignore)
            for (const auto& [name, l] : src->doc->layers()) {
                auto it = src->layers.find(name);
                const UtilKind k = it != src->layers.end() ? it->second : guessUtil(name);
                if (k != UtilKind::Ignore && k != UtilKind::Other && k != UtilKind::Ground && !labelLayer(name)) { srcHasKnown = true; break; }
            }
        if (src->doc) src->doc->forEach([&](const IEntity& e) { visit(e); });
    }
    mergeBundles(m, lineSrc, p.bundleWidth);
    // ---- quduqlar: ichma-ich aylanalar (tashqi/ichki devor, qopqoq) — bitta quduq (eng kattasi)
    {
        std::vector<std::size_t> ord(m.nodes.size());
        for (std::size_t i = 0; i < ord.size(); ++i) ord[i] = i;
        std::sort(ord.begin(), ord.end(), [&](auto a, auto b) { return m.nodes[a].r > m.nodes[b].r; });
        std::vector<UtilNode> kept;
        for (auto i : ord) {
            const auto& n = m.nodes[i];
            bool inside = false;
            for (const auto& k : kept)
                if (k.kind == n.kind && std::hypot(k.x - n.x, k.y - n.y) + n.r <= k.r + 0.05) { inside = true; break; }
            if (!inside) kept.push_back(n);
        }
        m.nodes = std::move(kept);
    }
    // ---- quduq belgilari: markazdan nodeLabelRadius ichidagi ≥ 2 ta belgi — eng kattasi qopqoq, qolganlari tub
    {
        std::vector<std::vector<std::size_t>> near(m.nodes.size());
        for (std::size_t li = 0; li < labels.size(); ++li) {
            const auto& lb = labels[li];
            if (lb.type != 'z') continue;
            int bi = -1;
            double bd = p.nodeLabelRadius;
            // yo'naltiruvchi chiziq bo'ylab yozilgan belgilar ("489.52 / 485.09" chiziq ustida va ostida)
            double bl = 4.0;
            for (const auto& ld : leaders) {
                if (ld.src != lb.src) continue;
                const UtilVertex a{ld.ax, ld.ay, 0, 0, UtilVertex::Default}, b{ld.bx, ld.by, 0, 0, UtilVertex::Default};
                const double dl = segDist(lb.x, lb.y, a, b);
                if (dl >= bl) continue;
                for (std::size_t ni = 0; ni < m.nodes.size(); ++ni) {
                    const auto& n = m.nodes[ni];
                    if (std::min(std::hypot(n.x - ld.ax, n.y - ld.ay), std::hypot(n.x - ld.bx, n.y - ld.by)) <= n.r + 1.5) {
                        bl = dl;
                        bi = static_cast<int>(ni);
                    }
                }
            }
            if (bi >= 0) bd = -1;
            for (std::size_t ni = 0; ni < m.nodes.size() && bd >= 0; ++ni) {
                const double d = std::hypot(m.nodes[ni].x - lb.x, m.nodes[ni].y - lb.y);
                if (d < bd) { bd = d; bi = static_cast<int>(ni); }
            }
            if (bi >= 0) near[static_cast<std::size_t>(bi)].push_back(li);
        }
        for (std::size_t ni = 0; ni < m.nodes.size(); ++ni) {
            if (near[ni].size() < 2) continue;
            std::vector<double> v;
            for (auto li : near[ni]) { v.push_back(labels[li].value); labels[li].used = true; }
            std::sort(v.begin(), v.end());
            auto& n = m.nodes[ni];
            n.ground = v.back();
            v.pop_back();
            n.inverts = v;
            n.labeled = true;
            groundPts.push_back({n.x, n.y, n.ground});
        }
        // nomlar: yo'naltiruvchi chiziq orqali (bir uchi yozuv yonida, ikkinchisi quduqda), bo'lmasa eng yaqin quduq
        for (const auto& nm : names) {
            int bi = -1;
            double bd = 1e18;
            for (const auto& ld : leaders) {
                if (ld.src != nm.src) continue;
                const double da = std::hypot(ld.ax - nm.x, ld.ay - nm.y), db = std::hypot(ld.bx - nm.x, ld.by - nm.y);
                const double ox = da < db ? ld.bx : ld.ax, oy = da < db ? ld.by : ld.ay;
                if (std::min(da, db) > 8) continue;
                for (std::size_t ni = 0; ni < m.nodes.size(); ++ni) {
                    const double d = std::hypot(m.nodes[ni].x - ox, m.nodes[ni].y - oy);
                    if (d <= m.nodes[ni].r + 1.5 && std::min(da, db) + d < bd) { bd = std::min(da, db) + d; bi = static_cast<int>(ni); }
                }
            }
            if (bi < 0)
                for (std::size_t ni = 0; ni < m.nodes.size(); ++ni) {
                    const double d = std::hypot(m.nodes[ni].x - nm.x, m.nodes[ni].y - nm.y);
                    if (d < p.nodeLabelRadius * 3 && d < bd) { bd = d; bi = static_cast<int>(ni); }
                }
            if (bi >= 0 && m.nodes[static_cast<std::size_t>(bi)].name.empty()) {
                std::string t;
                for (char c : nm.text)
                    if (c != ' ') t += c;
                m.nodes[static_cast<std::size_t>(bi)].name = t;
            }
        }
    }
    // ---- belgisi yo'q quduqlar (faqat nomi bor): tub belgisi tarmoq bo'yicha baholanadi —
    //      zanjir ichida bo'lsa ikki tomondagi belgili quduqlar orasida masofa bo'yicha, shox boshida — qo'shni quduqdan
    //      quvur yozuvidagi nishablik bilan yuqoriga. Ogohlantirish beriladi (qo'lda tuzatish uchun).
    {
        auto nodeNear = [&](const UtilVertex& v, UtilKind k) {
            int bi = -1;
            double bd = 1e18;
            for (std::size_t ni = 0; ni < m.nodes.size(); ++ni) {
                const auto& n = m.nodes[ni];
                if (n.kind != k) continue;
                const double d = std::hypot(n.x - v.x, n.y - v.y);
                if (d <= n.r + 1.5 && d < bd) { bd = d; bi = static_cast<int>(ni); }
            }
            return bi;
        };
        struct Edge { int to; double len; std::size_t line; };
        std::vector<std::vector<Edge>> adj(m.nodes.size());
        for (std::size_t i = 0; i < m.lines.size(); ++i) {
            const int a = nodeNear(m.lines[i].v.front(), m.lines[i].kind), b = nodeNear(m.lines[i].v.back(), m.lines[i].kind);
            if (a < 0 || b < 0 || a == b) continue;
            const double L = std::hypot(m.nodes[a].x - m.nodes[b].x, m.nodes[a].y - m.nodes[b].y);
            adj[a].push_back({b, L, i});
            adj[b].push_back({a, L, i});
        }
        // nishablik yozuvi chiziqlarga hali biriktirilmagan — shu yerda yaqin yozuvdan
        auto slopeNear = [&](std::size_t li) {
            const auto& ln = m.lines[li];
            double bd = p.labelRadius * 2, sl = 0;
            for (const auto& lb : labels)
                if (lb.type == 's' && lb.src == lineSrc[li]) {
                    const double d = segDist(lb.x, lb.y, ln.v.front(), ln.v.back());
                    if (d < bd) { bd = d; sl = lb.value; }
                }
            return sl;
        };
        std::vector<double> est(m.nodes.size(), 0);
        std::vector<bool> has(m.nodes.size(), false);
        for (std::size_t ni = 0; ni < m.nodes.size(); ++ni)
            if (!m.nodes[ni].inverts.empty()) { has[ni] = true; est[ni] = m.nodes[ni].inverts.front(); }
        for (int pass = 0; pass < 50; ++pass) {
            bool changed = false;
            for (std::size_t ni = 0; ni < m.nodes.size(); ++ni) {
                if (has[ni] || adj[ni].empty()) continue;
                // ikki yo'nalishda zanjir bo'ylab birinchi belgili quduq
                std::vector<std::pair<double, double>> ends;  // (masofa, belgi)
                for (const auto& e0 : adj[ni]) {
                    int prev = static_cast<int>(ni), cur = e0.to;
                    double dist = e0.len;
                    for (int step = 0; step < 200 && !has[static_cast<std::size_t>(cur)] && adj[static_cast<std::size_t>(cur)].size() == 2; ++step) {
                        const auto& nx = adj[static_cast<std::size_t>(cur)][0].to == prev ? adj[static_cast<std::size_t>(cur)][1] : adj[static_cast<std::size_t>(cur)][0];
                        prev = cur;
                        cur = nx.to;
                        dist += nx.len;
                    }
                    if (has[static_cast<std::size_t>(cur)]) ends.push_back({dist, est[static_cast<std::size_t>(cur)]});
                }
                if (ends.size() >= 2) {
                    std::sort(ends.begin(), ends.end());
                    const auto [d1, z1] = ends[0];
                    const auto [d2, z2] = ends[1];
                    est[ni] = z1 + (z2 - z1) * d1 / (d1 + d2);
                } else if (ends.size() == 1 && adj[ni].size() == 1) {
                    const double sl = slopeNear(adj[ni][0].line);
                    est[ni] = ends[0].second + (sl > 0 ? sl : 0.005) * ends[0].first;  // shox boshi — yuqorida
                } else {
                    continue;
                }
                has[ni] = true;
                changed = true;
            }
            if (!changed) break;
        }
        for (std::size_t ni = 0; ni < m.nodes.size(); ++ni)
            if (m.nodes[ni].inverts.empty() && has[ni] && !adj[ni].empty()) {
                m.nodes[ni].inverts = {std::round(est[ni] * 100) / 100};
                m.nodes[ni].estimated = true;
            }
    }
    m.groundPoints = static_cast<int>(groundPts.size());

    // ---- yer sathi (IDW, 6 ta eng yaqin)
    auto groundAt = [&](double x, double y) {
        if (groundPts.empty()) return p.ground;
        std::vector<std::pair<double, double>> d;
        d.reserve(groundPts.size());
        for (const auto& g : groundPts) d.push_back({std::hypot(g[0] - x, g[1] - y), g[2]});
        const std::size_t k = std::min<std::size_t>(6, d.size());
        std::partial_sort(d.begin(), d.begin() + static_cast<long>(k), d.end());
        if (d[0].first < 1e-6) return d[0].second;
        double sw = 0, sz = 0;
        for (std::size_t i = 0; i < k; ++i) {
            const double w = 1 / (d[i].first * d[i].first);
            sw += w;
            sz += w * d[i].second;
        }
        return sz / sw;
    };
    for (auto& ln : m.lines)
        for (auto& v : ln.v) v.ground = groundAt(v.x, v.y);
    for (auto& n : m.nodes) n.ground = groundAt(n.x, n.y);

    // ---- diametr yozuvlari: eng yaqin chiziq (radius ichida; shu qatlam ustun)
    struct DnLabel { double x, y; int line; int src; };
    std::vector<DnLabel> dnLabels;
    for (const auto& lb : labels) {
        if (lb.type != 'd') continue;
        int best = -1;
        double bd = labelLayer(lb.layer) ? std::max(p.labelRadius, 4.5) : p.labelRadius;
        for (std::size_t i = 0; i < m.lines.size(); ++i) {
            const auto& ln = m.lines[i];
            for (std::size_t j = 0; j + 1 < ln.v.size(); ++j) {
                if (lineSrc[i] != lb.src) break;  // boshqa fayldagi yozuv bu chiziqqa tegishli emas
                double d = segDist(lb.x, lb.y, ln.v[j], ln.v[j + 1]);
                if (ln.layer == lb.layer || ln.layer.ends_with(" · " + lb.layer)) d *= 0.5;  // o'z qatlami afzal
                if (d < bd) { bd = d; best = static_cast<int>(i); }
            }
        }
        if (best >= 0) {
            m.lines[static_cast<std::size_t>(best)].dn = lb.value;
            m.lines[static_cast<std::size_t>(best)].dnFromLabel = true;
            dnLabels.push_back({lb.x, lb.y, best, lb.src});
        }
    }
    // ---- nishablik, uzunlik, material: shu Ø yozuvi guruhi (8 m ichida) chizig'iga, bo'lmasa eng yaqin chiziqqa
    auto lineFor = [&](double x, double y, int srcIdx) {
        int best = -1;
        double bd = 8;
        for (const auto& d : dnLabels)
            if (d.src == srcIdx && std::hypot(d.x - x, d.y - y) < bd) { bd = std::hypot(d.x - x, d.y - y); best = d.line; }
        if (best >= 0) return best;
        bd = p.labelRadius * 1.5;
        for (std::size_t i = 0; i < m.lines.size(); ++i) {
            if (lineSrc[i] != srcIdx) continue;
            for (std::size_t j = 0; j + 1 < m.lines[i].v.size(); ++j) {
                const double d = segDist(x, y, m.lines[i].v[j], m.lines[i].v[j + 1]);
                if (d < bd) { bd = d; best = static_cast<int>(i); }
            }
        }
        return best;
    };
    for (const auto& lb : labels) {
        if (lb.type != 's' && lb.type != 'l') continue;
        const int li = lineFor(lb.x, lb.y, lb.src);
        if (li < 0) continue;
        (lb.type == 's' ? m.lines[static_cast<std::size_t>(li)].slopeLabel : m.lines[static_cast<std::size_t>(li)].lengthLabel) = lb.value;
    }
    for (const auto& mt : materials) {
        const int li = lineFor(mt.x, mt.y, mt.src);
        if (li >= 0 && m.lines[static_cast<std::size_t>(li)].material.empty()) {
            std::string t;
            for (char c : mt.text)
                if (c != ' ') t += c;
            m.lines[static_cast<std::size_t>(li)].material = t;
        }
    }
    for (auto& ln : m.lines)
        if (ln.dn <= 0) ln.dn = p.dn.count(ln.kind) ? p.dn.at(ln.kind) : 100;

    // ---- belgi/chuqurlik yozuvlari: eng yaqin uchga (har uchga eng yaqin yozuv)
    struct Pick { double d = 1e18; double v = 0; char t = 0; };
    std::map<std::pair<std::size_t, std::size_t>, Pick> pick;
    for (const auto& lb : labels) {
        if ((lb.type != 'z' && lb.type != 'h') || lb.used) continue;
        std::size_t bi = 0, bj = 0;
        double bd = p.labelRadius;
        bool found = false;
        for (std::size_t i = 0; i < m.lines.size(); ++i)
            for (std::size_t j = 0; j < m.lines[i].v.size(); ++j) {
                if (m.lines[i].v[j].src == UtilVertex::FromZ || lineSrc[i] != lb.src) continue;
                double d = std::hypot(m.lines[i].v[j].x - lb.x, m.lines[i].v[j].y - lb.y);
                if (m.lines[i].layer == lb.layer || m.lines[i].layer.ends_with(" · " + lb.layer)) d *= 0.5;
                if (d < bd) { bd = d; bi = i; bj = j; found = true; }
            }
        if (!found) continue;
        auto& pk = pick[{bi, bj}];
        if (bd < pk.d) pk = {bd, lb.value, lb.type};
    }
    for (const auto& [ij, pk] : pick) {
        auto& ln = m.lines[ij.first];
        auto& v = ln.v[ij.second];
        const double r = ln.dn / 2000.0;
        if (pk.t == 'z') {
            v.z = isGravity(ln.kind) ? pk.v + r : pk.v;  // o'z oqimli: belgi — quvur tubi
            v.src = UtilVertex::FromLabel;
        } else {
            v.z = v.ground - pk.v - r;                   // chuqurlik — quvur ustigacha
            v.src = UtilVertex::FromDepth;
        }
    }
    // ---- quduqlar orasidagi quvurlar: uchlari quduqqa tegadi; tub belgilari quduq yozuvidan
    //      (bir nechta tub bo'lsa — yozuvdagi nishablikka eng mos juftlik; yo'q bo'lsa yuqoridagi quduqdan eng pasti)
    auto nodeAt = [&](const UtilVertex& v, UtilKind k) {
        int bi = -1;
        double bd = 1e18;
        for (std::size_t ni = 0; ni < m.nodes.size(); ++ni) {
            const auto& n = m.nodes[ni];
            if (n.kind != k) continue;
            const double d = std::hypot(n.x - v.x, n.y - v.y);
            if (d <= n.r + 1.5 && d < bd) { bd = d; bi = static_cast<int>(ni); }
        }
        return bi;
    };
    for (auto& ln : m.lines) {
        ln.from = nodeAt(ln.v.front(), ln.kind);
        ln.to = nodeAt(ln.v.back(), ln.kind);
        if (ln.from < 0 || ln.to < 0 || ln.from == ln.to) continue;
        const auto& A = m.nodes[static_cast<std::size_t>(ln.from)];
        const auto& B = m.nodes[static_cast<std::size_t>(ln.to)];
        if (A.inverts.empty() || B.inverts.empty()) continue;
        const double L = std::max(0.01, std::hypot(B.x - A.x, B.y - A.y));
        double ia = A.inverts.front(), ib = B.inverts.front();
        if (ln.slopeLabel > 0) {
            double best = 1e18;
            for (double a : A.inverts)
                for (double b : B.inverts) {
                    const double e = std::abs(std::abs(a - b) / L - ln.slopeLabel);
                    if (e < best - 1e-12) { best = e; ia = a; ib = b; }
                }
        } else {
            const bool aUp = A.inverts.back() >= B.inverts.back();
            const auto& up = aUp ? A.inverts : B.inverts;
            const auto& dn = aUp ? B.inverts : A.inverts;
            const double u = up.front();
            double d = dn.front();
            for (double x : dn)
                if (x < u) d = x;  // eng yuqori, lekin chiqishdan past
            ia = aUp ? u : d;
            ib = aUp ? d : u;
        }
        const double r = ln.dn / 2000.0;
        for (auto& v : ln.v) {
            const double t = std::clamp(((v.x - A.x) * (B.x - A.x) + (v.y - A.y) * (B.y - A.y)) / (L * L), 0.0, 1.0);
            v.z = ia + t * (ib - ia) + (isGravity(ln.kind) ? r : 0);
            v.src = UtilVertex::FromLabel;
        }
        if (A.labeled && B.labeled && A.ground > 0 && B.ground > 0) {
            ln.v.front().ground = A.ground;
            ln.v.back().ground = B.ground;
        }
    }
    // ---- interpolyatsiya / standart
    for (auto& ln : m.lines) {
        std::vector<double> ch(ln.v.size(), 0);
        for (std::size_t j = 1; j < ln.v.size(); ++j) ch[j] = ch[j - 1] + std::hypot(ln.v[j].x - ln.v[j - 1].x, ln.v[j].y - ln.v[j - 1].y);
        std::vector<std::size_t> known;
        for (std::size_t j = 0; j < ln.v.size(); ++j)
            if (ln.v[j].src != UtilVertex::Default) known.push_back(j);
        const double r = ln.dn / 2000.0, dep = p.depth.count(ln.kind) ? p.depth.at(ln.kind) : 1.0;
        for (std::size_t j = 0; j < ln.v.size(); ++j) {
            auto& v = ln.v[j];
            if (v.src != UtilVertex::Default) continue;
            if (known.empty()) { v.z = v.ground - dep - r; continue; }
            auto it = std::lower_bound(known.begin(), known.end(), j);
            if (it == known.begin()) {  // boshida: birinchi ma'lum chuqurlikni saqlab
                const auto& k = ln.v[*it];
                v.z = v.ground - (k.ground - k.z);
            } else if (it == known.end()) {
                const auto& k = ln.v[known.back()];
                v.z = v.ground - (k.ground - k.z);
            } else {
                const std::size_t a = *(it - 1), b = *it;
                const double t = (ch[j] - ch[a]) / std::max(1e-9, ch[b] - ch[a]);
                v.z = ln.v[a].z + t * (ln.v[b].z - ln.v[a].z);
            }
            v.src = UtilVertex::Interpolated;
        }
        if (known.empty())
            for (auto& v : ln.v) v.src = UtilVertex::Default;
    }
    // quduq tubi: unga tegadigan chiziqlarning eng past tubidan 0,1 m pastda
    for (auto& n : m.nodes) {
        double low = n.ground - (p.depth.count(n.kind) ? p.depth.at(n.kind) : 1.0) - 0.5;
        bool any = false;
        for (const auto& ln : m.lines)
            for (const auto& v : ln.v)
                if (std::hypot(v.x - n.x, v.y - n.y) <= n.r + 0.3) {
                    const double inv = v.z - ln.dn / 2000.0;
                    low = any ? std::min(low, inv) : inv;
                    any = true;
                }
        n.bottom = n.inverts.empty() ? low - 0.1 : n.inverts.front() - 0.1;
    }
    checkUtilities(m, p);
    return m;
}

void checkUtilities(UtilModel& m, const UtilParams& p) {
    m.issues.clear();
    m.crossings.clear();
    m.lengthByKind.clear();
    auto issue = [&](bool e, const char* k, std::vector<std::string> a, int line, double x, double y) {
        m.issues.push_back({e, k, std::move(a), line, x, y});
    };
    for (std::size_t i = 0; i < m.lines.size(); ++i) {
        const auto& ln = m.lines[i];
        const int li = static_cast<int>(i);
        const std::string id = std::to_string(ln.id);
        m.lengthByKind[ln.kind] += ln.length();
        if (!ln.dnFromLabel && !ln.dnManual && !isCable(ln.kind)) issue(false, "util.chk.no_dn", {id, ln.layer, std::format("{:.0f}", ln.dn)}, li, ln.v[0].x, ln.v[0].y);
        bool anyKnown = false;
        for (const auto& v : ln.v) anyKnown |= v.src != UtilVertex::Default;
        if (!anyKnown) issue(false, "util.chk.no_z", {id, ln.layer}, li, ln.v[0].x, ln.v[0].y);
        // qatlam (quvur ustigacha)
        const double r = ln.dn / 2000.0, need = p.minCover.count(ln.kind) ? p.minCover.at(ln.kind) : 0.5;
        double worst = 1e9;
        const UtilVertex* wv = nullptr;
        for (const auto& v : ln.v) {
            const double c = v.ground - (v.z + r);
            if (c < worst) { worst = c; wv = &v; }
        }
        if (wv && worst < 0) issue(true, "util.chk.above", {id, f2(-worst)}, li, wv->x, wv->y);
        else if (wv && worst + 1e-9 < need) issue(true, "util.chk.cover", {id, f2(worst), f2(need)}, li, wv->x, wv->y);
        // loyiha yozuvidagi nishablik ("1:365") ↔ quduq tub belgilaridan hisoblangan nishablik
        if (isGravity(ln.kind) && ln.slopeLabel > 0 && ln.v.size() >= 2 && ln.v.front().src == UtilVertex::FromLabel &&
            ln.v.back().src == UtilVertex::FromLabel) {
            const double L = std::hypot(ln.v.back().x - ln.v.front().x, ln.v.back().y - ln.v.front().y);
            if (L > 1) {
                const double sl = std::abs(ln.v.front().z - ln.v.back().z) / L;
                if (std::abs(sl - ln.slopeLabel) > std::max(0.001, ln.slopeLabel * 0.25))
                    issue(false, "util.chk.slope_label", {id, std::format("{:.4f}", ln.slopeLabel), std::format("{:.4f}", sl)}, li, ln.v.front().x,
                          ln.v.front().y);
            }
        }
        // o'z oqimli: nishablik ishorasi o'zgarmasligi kerak
        if (isGravity(ln.kind) && ln.v.size() >= 3) {
            int sign = 0;
            for (std::size_t j = 0; j + 1 < ln.v.size(); ++j) {
                const double dz = ln.v[j + 1].z - ln.v[j].z;
                const int s = dz > 1e-4 ? 1 : dz < -1e-4 ? -1 : 0;
                if (s == 0) continue;
                if (sign != 0 && s != sign) {
                    issue(true, "util.chk.counter", {id, std::to_string(j + 1)}, li, ln.v[j].x, ln.v[j].y);
                    break;
                }
                sign = s;
            }
        }
    }
    // ---- uzilishlar: uch nuqta boshqa (shu turdagi) chiziqqa yaqin, lekin tegmagan
    for (std::size_t i = 0; i < m.lines.size(); ++i) {
        const auto& ln = m.lines[i];
        for (const UtilVertex* e : {&ln.v.front(), &ln.v.back()}) {
            double best = 1e18;
            bool touch = false;
            for (std::size_t k = 0; k < m.lines.size() && !touch; ++k) {
                if (m.lines[k].kind != ln.kind) continue;
                const auto& o = m.lines[k];
                for (std::size_t j = 0; j + 1 < o.v.size(); ++j) {
                    if (k == i) {  // o'zi: faqat qo'shni bo'lmagan segmentlar
                        const bool front = e == &ln.v.front();
                        if ((front && j == 0) || (!front && j + 2 == o.v.size())) continue;
                    }
                    const double d = segDist(e->x, e->y, o.v[j], o.v[j + 1]);
                    if (d < 0.01) { touch = true; break; }
                    best = std::min(best, d);
                }
            }
            bool nearNode = false;
            for (const auto& n : m.nodes)
                if (n.kind == ln.kind && std::hypot(e->x - n.x, e->y - n.y) <= n.r + 0.3) nearNode = true;
            if (!touch && !nearNode && best < p.gapTol) issue(true, "util.chk.gap", {std::to_string(ln.id), f2(best)}, static_cast<int>(i), e->x, e->y);
        }
    }
    // ---- kesishmalar (turli tarmoqlar): plan bo'yicha kesishish nuqtasida vertikal masofa
    auto zAt = [](const UtilLine& ln, std::size_t j, double t) { return ln.v[j].z + t * (ln.v[j + 1].z - ln.v[j].z); };
    for (std::size_t a = 0; a < m.lines.size(); ++a)
        for (std::size_t b = a + 1; b < m.lines.size(); ++b) {
            const auto& A = m.lines[a];
            const auto& B = m.lines[b];
            if (A.kind == B.kind) continue;
            for (std::size_t i = 0; i + 1 < A.v.size(); ++i)
                for (std::size_t j = 0; j + 1 < B.v.size(); ++j) {
                    const double x1 = A.v[i].x, y1 = A.v[i].y, x2 = A.v[i + 1].x, y2 = A.v[i + 1].y;
                    const double x3 = B.v[j].x, y3 = B.v[j].y, x4 = B.v[j + 1].x, y4 = B.v[j + 1].y;
                    const double den = (x2 - x1) * (y4 - y3) - (y2 - y1) * (x4 - x3);
                    if (std::abs(den) < 1e-12) continue;
                    const double t = ((x3 - x1) * (y4 - y3) - (y3 - y1) * (x4 - x3)) / den;
                    const double u = ((x3 - x1) * (y2 - y1) - (y3 - y1) * (x2 - x1)) / den;
                    if (t < 0 || t > 1 || u < 0 || u > 1) continue;
                    UtilCrossing c;
                    c.a = static_cast<int>(a);
                    c.b = static_cast<int>(b);
                    c.x = x1 + t * (x2 - x1);
                    c.y = y1 + t * (y2 - y1);
                    c.za = zAt(A, i, t);
                    c.zb = zAt(B, j, u);
                    c.clear = std::abs(c.za - c.zb) - (A.dn + B.dn) / 2000.0;
                    c.required = isCable(A.kind) || isCable(B.kind) ? p.clearCable : p.clearPipe;
                    m.crossings.push_back(c);
                    if (c.clear + 1e-9 < c.required)
                        issue(true, c.clear < 0 ? "util.chk.clash" : "util.chk.clear",
                              {std::to_string(A.id), std::to_string(B.id), f2(c.clear), f2(c.required)}, static_cast<int>(a), c.x, c.y);
                }
        }
}

// ---------------------------------------------------------------- JSON
std::string utilToJson(const UtilModel& m, const UtilParams& p) {
    std::string s = std::format("{{\"format\":\"asiacad-utilities\",\"version\":1,\"ground\":{},\"labelRadius\":{},\"gapTol\":{},\"clearPipe\":{},"
                                "\"clearCable\":{},\"kinds\":{{",
                                p.ground, p.labelRadius, p.gapTol, p.clearPipe, p.clearCable);
    bool first = true;
    for (UtilKind k : utilKinds()) {
        s += std::format("{}\"{}\":{{\"depth\":{},\"dn\":{},\"minCover\":{}}}", first ? "" : ",", utilKey(k), p.depth.count(k) ? p.depth.at(k) : 1.0,
                         p.dn.count(k) ? p.dn.at(k) : 100, p.minCover.count(k) ? p.minCover.at(k) : 0.5);
        first = false;
    }
    s += "},\"lines\":[";
    for (std::size_t i = 0; i < m.lines.size(); ++i) {
        const auto& ln = m.lines[i];
        std::string layer;
        for (char c : ln.layer) { if (c == '"' || c == '\\') layer += '\\'; layer += c; }
        s += std::format("{}{{\"id\":{},\"kind\":\"{}\",\"layer\":\"{}\",\"dn\":{},\"dnLabel\":{},\"dnManual\":{},\"v\":[", i ? "," : "", ln.id,
                         utilKey(ln.kind), layer, ln.dn, ln.dnFromLabel, ln.dnManual);
        for (std::size_t j = 0; j < ln.v.size(); ++j)
            s += std::format("{}[{},{},{},{},{}]", j ? "," : "", ln.v[j].x, ln.v[j].y, ln.v[j].z, ln.v[j].ground, static_cast<int>(ln.v[j].src));
        s += "]}";
    }
    s += "],\"nodes\":[";
    for (std::size_t i = 0; i < m.nodes.size(); ++i) {
        const auto& n = m.nodes[i];
        s += std::format("{}[\"{}\",{},{},{},{},{}]", i ? "," : "", utilKey(n.kind), n.x, n.y, n.r, n.ground, n.bottom);
    }
    return s + "]}";
}

void utilFromJson(const std::string& text, UtilModel& m, UtilParams& p) {
    json::Value root;
    try {
        root = json::parse(text);
    } catch (const json::ParseError& e) {
        throw Error("util.err.format", {e.what()});
    }
    auto num = [](const json::Value* v, double def) { return v && v->isNumber() ? v->num() : def; };
    p.ground = num(root.find("ground"), p.ground);
    p.labelRadius = num(root.find("labelRadius"), p.labelRadius);
    p.gapTol = num(root.find("gapTol"), p.gapTol);
    p.clearPipe = num(root.find("clearPipe"), p.clearPipe);
    p.clearCable = num(root.find("clearCable"), p.clearCable);
    if (const json::Value* ks = root.find("kinds"); ks && ks->isObject())
        for (const auto& [key, v] : ks->obj()) {
            UtilKind k;
            if (!utilFromKey(key, k)) continue;
            p.depth[k] = num(v.find("depth"), p.depth[k]);
            p.dn[k] = num(v.find("dn"), p.dn[k]);
            p.minCover[k] = num(v.find("minCover"), p.minCover[k]);
        }
    m = {};
    if (const json::Value* ls = root.find("lines"); ls && ls->isArray())
        for (const auto& lv : ls->arr()) {
            UtilLine ln;
            ln.id = static_cast<int>(num(lv.find("id"), static_cast<double>(m.lines.size() + 1)));
            if (const auto* k = lv.find("kind"); k && k->isString()) utilFromKey(k->str(), ln.kind);
            if (const auto* l = lv.find("layer"); l && l->isString()) ln.layer = l->str();
            ln.dn = num(lv.find("dn"), 100);
            if (const auto* b = lv.find("dnLabel"); b && b->isBool()) ln.dnFromLabel = b->boolean();
            if (const auto* b = lv.find("dnManual"); b && b->isBool()) ln.dnManual = b->boolean();
            if (const auto* vs = lv.find("v"); vs && vs->isArray())
                for (const auto& q : vs->arr()) {
                    if (!q.isArray() || q.arr().size() < 5) continue;
                    const auto& a = q.arr();
                    ln.v.push_back({a[0].num(), a[1].num(), a[2].num(), a[3].num(), static_cast<UtilVertex::Src>(static_cast<int>(a[4].num()))});
                }
            if (ln.v.size() >= 2) m.lines.push_back(std::move(ln));
        }
    if (const json::Value* ns = root.find("nodes"); ns && ns->isArray())
        for (const auto& q : ns->arr()) {
            if (!q.isArray() || q.arr().size() < 6) continue;
            const auto& a = q.arr();
            UtilNode n;
            if (a[0].isString()) utilFromKey(a[0].str(), n.kind);
            n.x = a[1].num(); n.y = a[2].num(); n.r = a[3].num(); n.ground = a[4].num(); n.bottom = a[5].num();
            m.nodes.push_back(n);
        }
    checkUtilities(m, p);
}

std::vector<std::unique_ptr<IEntity>> drawUtilities3D(const UtilModel& m, double h) {
    std::vector<std::unique_ptr<IEntity>> out;
    auto put = [&](std::unique_ptr<IEntity> e, const std::string& layer) {
        e->setLayer(layer);
        out.push_back(std::move(e));
    };
    for (const auto& ln : m.lines) {
        std::vector<Vec3> pts;
        for (const auto& v : ln.v) pts.push_back({v.x, v.y, v.z});
        put(std::make_unique<PolylineEntity>(pts, false), std::string("3D_") + utilKey(ln.kind));
    }
    for (const auto& n : m.nodes) {
        put(std::make_unique<CircleEntity>(Vec3{n.x, n.y, n.ground}, n.r), std::string("3D_") + utilKey(n.kind));
        put(std::make_unique<LineEntity>(Vec3{n.x, n.y, n.bottom}, Vec3{n.x, n.y, n.ground}), std::string("3D_") + utilKey(n.kind));
    }
    for (const auto& c : m.crossings) {
        if (c.clear + 1e-9 >= c.required) continue;
        put(std::make_unique<CircleEntity>(Vec3{c.x, c.y, 0}, h * 0.8), "3D_KESISHMA");
        put(std::make_unique<TextEntity>(Vec3{c.x + h, c.y + h, 0}, h, std::format("{} m < {} m", f2(c.clear), f2(c.required))), "3D_KESISHMA");
    }
    return out;
}

} // namespace asiacad::civil
