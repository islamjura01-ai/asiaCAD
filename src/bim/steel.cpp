#include <asiacad/bim/steel.h>

#include <asiacad/core/entities.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace asiacad::bim {

namespace {
constexpr double kRho = 7.85e-3;   // kg / (sm² · m) — po'lat zichligi 7850 kg/m³
constexpr double kPI = 3.14159265358979323846;
}

const char* steelFamilyKey(SteelFamily f) {
    switch (f) {
        case SteelFamily::IBeam: return "IBEAM";
        case SteelFamily::Channel: return "CHANNEL";
        case SteelFamily::Angle: return "ANGLE";
        case SteelFamily::Pipe: return "PIPE";
        case SteelFamily::SquareTube: return "RHS";
    }
    return "IBEAM";
}

const std::vector<SteelProfile>& steelCatalog() {
    static const std::vector<SteelProfile> v = [] {
        std::vector<SteelProfile> c;
        // GOST 8239-89: № h b s t A m Ix Wx
        const double I[][9] = {{10, 100, 55, 4.5, 7.2, 12.0, 9.46, 198, 39.7},     {12, 120, 64, 4.8, 7.3, 14.7, 11.5, 350, 58.4},
                               {14, 140, 73, 4.9, 7.5, 17.4, 13.7, 572, 81.7},     {16, 160, 81, 5.0, 7.8, 20.2, 15.9, 873, 109},
                               {18, 180, 90, 5.1, 8.1, 23.4, 18.4, 1290, 143},     {20, 200, 100, 5.2, 8.4, 26.8, 21.0, 1840, 184},
                               {22, 220, 110, 5.4, 8.7, 30.6, 24.0, 2550, 232},    {24, 240, 115, 5.6, 9.5, 34.8, 27.3, 3460, 289},
                               {27, 270, 125, 6.0, 9.8, 40.2, 31.5, 5010, 371},    {30, 300, 135, 6.5, 10.2, 46.5, 36.5, 7080, 472},
                               {33, 330, 140, 7.0, 11.2, 53.8, 42.2, 9840, 597},   {36, 360, 145, 7.5, 12.3, 61.9, 48.6, 13380, 743},
                               {40, 400, 155, 8.3, 13.0, 72.6, 57.0, 19062, 953},  {45, 450, 160, 9.0, 14.2, 84.7, 66.5, 27696, 1231},
                               {50, 500, 170, 10.0, 15.2, 100, 78.5, 39727, 1589}, {55, 550, 180, 11.0, 16.5, 118, 92.6, 55962, 2035},
                               {60, 600, 190, 12.0, 17.8, 138, 108, 76806, 2560}};
        for (const auto& r : I)
            c.push_back({std::format("I{:g}", r[0]), SteelFamily::IBeam, r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], "GOST 8239-89"});
        // GOST 8240-97 (У): № h b s t A m Ix Wx
        const double U[][9] = {{5, 50, 32, 4.4, 7.0, 6.16, 4.84, 22.8, 9.1},      {6.5, 65, 36, 4.4, 7.2, 7.51, 5.90, 48.6, 15.0},
                               {8, 80, 40, 4.5, 7.4, 8.98, 7.05, 89.4, 22.4},      {10, 100, 46, 4.5, 7.6, 10.9, 8.59, 174, 34.8},
                               {12, 120, 52, 4.8, 7.8, 13.3, 10.4, 304, 50.6},     {14, 140, 58, 4.9, 8.1, 15.6, 12.3, 491, 70.2},
                               {16, 160, 64, 5.0, 8.4, 18.1, 14.2, 747, 93.4},     {18, 180, 70, 5.1, 8.7, 20.7, 16.3, 1090, 121},
                               {20, 200, 76, 5.2, 9.0, 23.4, 18.4, 1520, 152},     {22, 220, 82, 5.4, 9.5, 26.7, 21.0, 2110, 192},
                               {24, 240, 90, 5.6, 10.0, 30.6, 24.0, 2900, 242},    {27, 270, 95, 6.0, 10.5, 35.2, 27.7, 4160, 308},
                               {30, 300, 100, 6.5, 11.0, 40.5, 31.8, 5810, 387},   {33, 330, 105, 7.0, 11.7, 46.5, 36.5, 7980, 484},
                               {36, 360, 110, 7.5, 12.6, 53.4, 41.9, 10820, 601},  {40, 400, 115, 8.0, 13.5, 61.5, 48.3, 15220, 761}};
        for (const auto& r : U)
            c.push_back({std::format("[{:g}", r[0]), SteelFamily::Channel, r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], "GOST 8240-97"});
        // GOST 8509-93: b t A m
        const double L[][4] = {{50, 5, 4.80, 3.77},   {63, 5, 6.13, 4.81},   {63, 6, 7.28, 5.72},   {75, 6, 8.78, 6.89},
                               {75, 8, 11.5, 9.02},   {90, 7, 12.3, 9.64},   {100, 8, 15.6, 12.25}, {100, 10, 19.2, 15.1},
                               {125, 10, 24.3, 19.1}, {140, 10, 27.3, 21.45}, {160, 12, 37.4, 29.35}};
        for (const auto& r : L)
            c.push_back({std::format("L{:g}x{:g}", r[0], r[1]), SteelFamily::Angle, r[0], r[0], r[1], r[1], r[2], r[3], 0, 0, "GOST 8509-93"});
        // trubalar (tez-tez ishlatiladiganlar), kvadrat trubalar — formuladan
        for (auto [D, t] : {std::pair{57.0, 3.5}, {76.0, 4.0}, {89.0, 4.0}, {108.0, 4.0}, {114.0, 4.5}, {133.0, 5.0}, {159.0, 6.0}, {219.0, 8.0}, {273.0, 8.0}, {325.0, 8.0}}) {
            const double A = kPI * (D - t) * t / 100;
            const double Ix = kPI / 64 * (std::pow(D, 4) - std::pow(D - 2 * t, 4)) / 1e4;
            c.push_back({std::format("TR{:g}x{:g}", D, t), SteelFamily::Pipe, D, D, t, t, A, A * kRho * 100, Ix, Ix / (D / 20), "GOST 10704-91"});
        }
        for (auto [B, t] : {std::pair{50.0, 4.0}, {60.0, 4.0}, {80.0, 4.0}, {100.0, 5.0}, {120.0, 5.0}, {140.0, 6.0}, {160.0, 6.0}, {200.0, 8.0}}) {
            const double A = (B * B - (B - 2 * t) * (B - 2 * t)) / 100;
            const double Ix = (std::pow(B, 4) - std::pow(B - 2 * t, 4)) / 12 / 1e4;
            c.push_back({std::format("KV{:g}x{:g}", B, t), SteelFamily::SquareTube, B, B, t, t, A, A * kRho * 100, Ix, Ix / (B / 20), "GOST 30245-2003"});
        }
        return c;
    }();
    return v;
}

std::optional<SteelProfile> findSteel(const std::string& raw) {
    std::string u = utf8::toUpper(raw);
    std::string s;
    for (char ch : u)
        if (ch != ' ') s += ch;
    // kirill va muqobil yozuvlar
    for (auto [from, to] : {std::pair<std::string, std::string>{"ДВУТАВР", "I"}, {"ШВЕЛЛЕР", "["}, {"УГОЛОК", "L"}, {"ТРУБА", "TR"},
                            {"\xD0\x94", "I"}, {"\xD0\xA8", "["}, {"\xD0\x93", "L"}, {"\xD0\xA2\xD0\xA0", "TR"}, {"\xC3\x97", "X"},
                            {"\xD0\xA5", "X"}, {"*", "X"}, {"\xC3\x98", "TR"}, {"U", "["}, {"C", "["}, {"Л", "L"}})
        for (std::size_t p; !from.empty() && (p = s.find(from)) != std::string::npos;) s.replace(p, from.size(), to);
    for (auto& ch : s)
        if (ch == ',') ch = '.';
    std::string su = s;
    for (auto& ch : su) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (const auto& p : steelCatalog()) {
        std::string n = p.name;
        for (auto& ch : n) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (n == su) return p;
    }
    // katalogda yo'q truba / kv. truba: o'lchamdan
    auto two = [&](const std::string& pre, double& a, double& b) {
        if (su.rfind(pre, 0) != 0) return false;
        const auto x = su.find('X', pre.size());
        if (x == std::string::npos) return false;
        try {
            a = std::stod(su.substr(pre.size(), x - pre.size()));
            b = std::stod(su.substr(x + 1));
        } catch (...) {
            return false;
        }
        return a > 2 * b && b > 0;
    };
    double a, b;
    if (two("TR", a, b)) {
        const double A = kPI * (a - b) * b / 100, Ix = kPI / 64 * (std::pow(a, 4) - std::pow(a - 2 * b, 4)) / 1e4;
        return SteelProfile{std::format("TR{:g}x{:g}", a, b), SteelFamily::Pipe, a, a, b, b, A, A * kRho * 100, Ix, Ix / (a / 20), "GOST 10704-91"};
    }
    if (two("KV", a, b)) {
        const double A = (a * a - (a - 2 * b) * (a - 2 * b)) / 100, Ix = (std::pow(a, 4) - std::pow(a - 2 * b, 4)) / 12 / 1e4;
        return SteelProfile{std::format("KV{:g}x{:g}", a, b), SteelFamily::SquareTube, a, a, b, b, A, A * kRho * 100, Ix, Ix / (a / 20), "GOST 30245-2003"};
    }
    return std::nullopt;
}

std::vector<std::vector<Vec3>> steelSection(const SteelProfile& p) {
    const double h = p.h / 1000, b = p.b / 1000, s = p.s / 1000, t = p.t / 1000;
    const double y0 = -h / 2, y1 = h / 2;
    switch (p.family) {
        case SteelFamily::IBeam:
            return {{{-b / 2, y0}, {b / 2, y0}, {b / 2, y0 + t}, {s / 2, y0 + t}, {s / 2, y1 - t}, {b / 2, y1 - t}, {b / 2, y1}, {-b / 2, y1},
                     {-b / 2, y1 - t}, {-s / 2, y1 - t}, {-s / 2, y0 + t}, {-b / 2, y0 + t}}};
        case SteelFamily::Channel: {
            const double x0 = -b / 2;  // devor chapda
            return {{{x0, y0}, {x0 + b, y0}, {x0 + b, y0 + t}, {x0 + s, y0 + t}, {x0 + s, y1 - t}, {x0 + b, y1 - t}, {x0 + b, y1}, {x0, y1}}};
        }
        case SteelFamily::Angle: {
            const double x0 = -b / 2, yb = -h / 2;
            return {{{x0, yb}, {x0 + b, yb}, {x0 + b, yb + t}, {x0 + t, yb + t}, {x0 + t, yb + h}, {x0, yb + h}}};
        }
        case SteelFamily::Pipe: {
            std::vector<Vec3> o, in;
            for (int i = 0; i < 32; ++i) {
                const double a = 2 * kPI * i / 32;
                o.push_back({h / 2 * std::cos(a), h / 2 * std::sin(a)});
                in.push_back({(h / 2 - t) * std::cos(a), (h / 2 - t) * std::sin(a)});
            }
            return {o, in};
        }
        case SteelFamily::SquareTube:
            return {{{-b / 2, y0}, {b / 2, y0}, {b / 2, y1}, {-b / 2, y1}}, {{-b / 2 + t, y0 + t}, {b / 2 - t, y0 + t}, {b / 2 - t, y1 - t}, {-b / 2 + t, y1 - t}}};
    }
    return {};
}

std::string steelLayer(const SteelProfile& p) { return "KM_" + p.name; }

std::vector<SteelMember> readSteel(const Document& doc, std::vector<std::string>* unknown) {
    std::vector<SteelMember> out;
    std::map<std::string, std::optional<SteelProfile>> cache;
    doc.forEach([&](const IEntity& e) {
        const std::string& l = e.layer();
        const std::string u = utf8::toUpper(l);
        if (u.rfind("KM_", 0) != 0 && u.rfind("КМ_", 0) != 0) return;
        auto it = cache.find(l);
        if (it == cache.end()) {
            const std::string pn = u.rfind("KM_", 0) == 0 ? l.substr(3) : l.substr(std::string("КМ_").size());
            it = cache.emplace(l, findSteel(pn)).first;
            if (!it->second && unknown) unknown->push_back(l);
        }
        if (!it->second) return;
        std::vector<Vec3> pts;
        if (auto* ln = dynamic_cast<const LineEntity*>(&e)) pts = {ln->startPoint(), ln->endPoint()};
        else if (auto* pl = dynamic_cast<const PolylineEntity*>(&e)) {
            pts = pl->vertices();
            if (pl->isClosed() && !pts.empty()) pts.push_back(pts.front());
        }
        for (std::size_t i = 0; i + 1 < pts.size(); ++i)
            if (pts[i].distanceTo(pts[i + 1]) > 1e-6) out.push_back({e.id(), *it->second, pts[i], pts[i + 1]});
    });
    return out;
}

SteelSpec steelSpec(const std::vector<SteelMember>& ms, double reservePct) {
    SteelSpec sp;
    std::map<std::string, SteelSpecRow> rows;
    for (const auto& m : ms) {
        auto& r = rows[m.profile.name];
        r.profile = m.profile.name;
        r.standard = m.profile.standard;
        r.family = m.profile.family;
        r.count += 1;
        r.length += m.length();
        r.mass += m.length() * m.profile.mass / 1000;
    }
    for (auto& [k, r] : rows) sp.rows.push_back(r);
    std::sort(sp.rows.begin(), sp.rows.end(), [](const SteelSpecRow& a, const SteelSpecRow& b) {
        if (a.family != b.family) return static_cast<int>(a.family) < static_cast<int>(b.family);
        auto num = [](const std::string& n) {
            std::size_t i = 0;
            while (i < n.size() && !(std::isdigit(static_cast<unsigned char>(n[i])))) ++i;
            try { return std::stod(n.substr(i)); } catch (...) { return 0.0; }
        };
        return num(a.profile) < num(b.profile);
    });
    for (const auto& r : sp.rows) sp.total += r.mass;
    sp.reserve = sp.total * reservePct / 100;
    return sp;
}

} // namespace asiacad::bim
