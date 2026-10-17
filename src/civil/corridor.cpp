#include <asiacad/civil/corridor.h>

#include <asiacad/civil/road.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/json.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <set>

namespace asiacad::civil {

namespace {
constexpr double kPI = 3.14159265358979323846;
std::string fc(double v, int dec = 2) {
    std::string s = std::format("{:.{}f}", v, dec);
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}
double idw(const std::vector<std::array<double, 3>>& g, double x, double y, double def) {
    if (g.empty()) return def;
    constexpr std::size_t K = 8;
    std::pair<double, double> best[K];
    std::size_t n = 0;
    for (const auto& p : g) {
        const double d2 = (p[0] - x) * (p[0] - x) + (p[1] - y) * (p[1] - y);
        if (d2 < 1e-12) return p[2];
        if (n == K && d2 >= best[K - 1].first) continue;
        std::size_t k = n < K ? n++ : K - 1;
        while (k > 0 && best[k - 1].first > d2) { best[k] = best[k - 1]; --k; }
        best[k] = {d2, p[2]};
    }
    double sw = 0, sz = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 1 / best[i].first;
        sw += w;
        sz += w * best[i].second;
    }
    return sz / sw;
}
}  // namespace

// ---------------------------------------------------------------- qismlar katalogi
const char* subKey(SubKind k) {
    switch (k) {
        case SubKind::Lane: return "LANE";
        case SubKind::Shoulder: return "SHOULDER";
        case SubKind::Curb: return "CURB";
        case SubKind::Sidewalk: return "SIDEWALK";
        case SubKind::Median: return "MEDIAN";
        case SubKind::GenericLink: return "LINK";
        case SubKind::Daylight: return "DAYLIGHT";
        case SubKind::DaylightDitch: return "DAYLIGHT_DITCH";
        case SubKind::DaylightBench: return "DAYLIGHT_BENCH";
        case SubKind::Ditch: return "DITCH";
        case SubKind::ChannelHalf: return "CHANNEL";
        case SubKind::FlumeHalf: return "FLUME";
        case SubKind::PipeTrenchHalf: return "TRENCH";
        case SubKind::RetainingWall: return "WALL";
    }
    return "LINK";
}
std::vector<SubKind> subKinds() {
    return {SubKind::GenericLink, SubKind::Lane,          SubKind::Shoulder,      SubKind::Median,   SubKind::Curb,
            SubKind::Sidewalk,    SubKind::Daylight,      SubKind::DaylightDitch, SubKind::DaylightBench, SubKind::Ditch,
            SubKind::ChannelHalf, SubKind::FlumeHalf,     SubKind::PipeTrenchHalf, SubKind::RetainingWall};
}
bool subFromKey(const std::string& s, SubKind& out) {
    for (SubKind k : subKinds())
        if (s == subKey(k)) { out = k; return true; }
    return false;
}
bool subIsCenter(SubKind k) {
    return k == SubKind::Median || k == SubKind::ChannelHalf || k == SubKind::FlumeHalf || k == SubKind::PipeTrenchHalf;
}
bool subIsDaylight(SubKind k) {
    return k == SubKind::Daylight || k == SubKind::DaylightDitch || k == SubKind::DaylightBench || k == SubKind::PipeTrenchHalf ||
           k == SubKind::RetainingWall;
}
const char* subCategory(SubKind k) {
    switch (k) {
        case SubKind::GenericLink: return "BASIC";
        case SubKind::Lane: return "LANES";
        case SubKind::Shoulder: return "SHOULDERS";
        case SubKind::Median: return "MEDIANS";
        case SubKind::Curb:
        case SubKind::Sidewalk: return "CURBS";
        case SubKind::Daylight:
        case SubKind::DaylightDitch:
        case SubKind::DaylightBench: return "DAYLIGHT";
        case SubKind::Ditch:
        case SubKind::ChannelHalf:
        case SubKind::FlumeHalf: return "CHANNELS";
        case SubKind::PipeTrenchHalf: return "TRENCH";
        case SubKind::RetainingWall: return "WALLS";
    }
    return "BASIC";
}

double Subassembly::get(const std::string& k, double def) const {
    for (const auto& q : p)
        if (q.key == k) return q.value;
    return def;
}
void Subassembly::set(const std::string& k, double v) {
    for (auto& q : p)
        if (q.key == k) { q.value = v; return; }
    p.push_back({k, v});
}

Subassembly defaultSub(SubKind k) {
    Subassembly s;
    s.kind = k;
    switch (k) {
        case SubKind::Lane: s.p = {{"w", 3.5}, {"slope", -2}, {"t1", 0.12}, {"t2", 0.20}, {"t3", 0.30}}; break;
        case SubKind::Shoulder: s.p = {{"w", 2.5}, {"slope", -4}, {"t", 0.30}}; break;
        case SubKind::Curb: s.p = {{"h", 0.15}, {"w", 0.15}, {"d", 0.30}}; break;
        case SubKind::Sidewalk: s.p = {{"w", 2.25}, {"slope", 2}, {"t", 0.08}, {"tb", 0.15}}; break;
        case SubKind::Median: s.p = {{"w", 5.0}, {"h", 0.15}, {"d", 0.5}}; break;
        case SubKind::GenericLink: s.p = {{"w", 1.0}, {"slope", 0}, {"t", 0}}; break;
        case SubKind::Daylight: s.p = {{"mc", 1.5}, {"mf", 1.5}}; break;
        case SubKind::DaylightDitch: s.p = {{"m1", 3}, {"d", 0.6}, {"b", 0.4}, {"mc", 1.5}, {"mf", 1.5}}; break;
        case SubKind::DaylightBench: s.p = {{"mc", 1.5}, {"H", 6}, {"bw", 2}, {"mf", 1.5}}; break;
        case SubKind::Ditch: s.p = {{"m1", 1.5}, {"d", 0.5}, {"b", 0.5}, {"m2", 1.5}}; break;
        case SubKind::ChannelHalf: s.p = {{"b", 1.5}, {"h", 1.2}, {"m", 1.0}, {"t", 0.15}, {"c", 1.0}}; break;
        case SubKind::FlumeHalf: s.p = {{"B", 1.24}, {"h", 0.6}, {"t", 0.06}, {"pad", 0.10}, {"padw", 0.20}, {"c", 0.5}}; break;
        case SubKind::PipeTrenchHalf: s.p = {{"D", 0.53}, {"e", 0.30}, {"tb", 0.15}, {"m", 0.5}}; break;
        case SubKind::RetainingWall: s.p = {{"h", 0.8}, {"tw", 0.3}, {"wf", 1.4}, {"tf", 0.4}, {"de", 0.8}}; break;
    }
    return s;
}

std::vector<Assembly> builtinAssemblies() {
    std::vector<Assembly> v;
    auto sub = [](SubKind k, std::initializer_list<std::pair<const char*, double>> ov) {
        Subassembly s = defaultSub(k);
        for (const auto& [key, val] : ov) s.set(key, val);
        return s;
    };
    auto road = [&](const char* name, std::vector<Subassembly> r) {
        Assembly a;
        a.name = name;
        a.group = "ROAD";
        a.right = std::move(r);
        v.push_back(a);
    };
    road("Yo'l II toifa (2×3.75 m, kyuvet)", {sub(SubKind::Lane, {{"w", 3.75}}), sub(SubKind::Shoulder, {{"w", 3.75}}),
                                              sub(SubKind::DaylightDitch, {})});
    road("Yo'l III toifa (2×3.50 m, kyuvet)", {sub(SubKind::Lane, {{"w", 3.5}}), sub(SubKind::Shoulder, {{"w", 2.5}}),
                                               sub(SubKind::DaylightDitch, {})});
    road("Yo'l IV toifa (2×3.00 m)", {sub(SubKind::Lane, {{"w", 3.0}, {"t1", 0.08}}), sub(SubKind::Shoulder, {{"w", 2.0}}),
                                      sub(SubKind::Daylight, {})});
    road("Magistral 4×3.75 m, ajratuvchi 5 m", {sub(SubKind::Median, {}), sub(SubKind::Lane, {{"w", 3.75}}),
                                                sub(SubKind::Lane, {{"w", 3.75}}), sub(SubKind::Shoulder, {{"w", 3.75}}),
                                                sub(SubKind::DaylightBench, {})});
    road("Shahar ko'chasi: 2×3.50, bordyur, trotuar", {sub(SubKind::Lane, {{"w", 3.5}}), sub(SubKind::Curb, {}),
                                                        sub(SubKind::Sidewalk, {}), sub(SubKind::GenericLink, {{"w", 1.0}, {"slope", 2}}),
                                                        sub(SubKind::Daylight, {})});
    {
        Assembly a;
        a.name = "Yo'l + tirgak devor (o'ngda)";
        a.group = "ROAD";
        a.symmetric = false;
        a.right = {sub(SubKind::Lane, {{"w", 3.5}}), sub(SubKind::Shoulder, {{"w", 1.0}}), sub(SubKind::RetainingWall, {})};
        a.left = {sub(SubKind::Lane, {{"w", 3.5}}), sub(SubKind::Shoulder, {{"w", 2.5}}), sub(SubKind::Daylight, {})};
        v.push_back(a);
    }
    auto canal = [&](const char* name, std::vector<Subassembly> r, double q, double n) {
        Assembly a;
        a.name = name;
        a.group = "CANAL";
        a.right = std::move(r);
        a.q = q;
        a.n = n;
        v.push_back(a);
    };
    canal("Beton kanal trapetsiya b=1.5 h=1.2 (1:1)", {sub(SubKind::ChannelHalf, {}), sub(SubKind::Daylight, {{"mf", 2.0}})}, 0.5, 0.015);
    canal("Beton kanal to'rtburchak 1.0×1.0", {sub(SubKind::ChannelHalf, {{"b", 1.0}, {"h", 1.0}, {"m", 0}, {"t", 0.2}}),
                                               sub(SubKind::Daylight, {})},
          0.3, 0.015);
    canal("Tuproq ariq 0.6×0.8 (1:1.5)", {sub(SubKind::ChannelHalf, {{"b", 0.6}, {"h", 0.8}, {"m", 1.5}, {"t", 0}, {"c", 0.8}}),
                                          sub(SubKind::Daylight, {})},
          0.2, 0.025);
    canal("Lotok ЛР-6 yostiqda", {sub(SubKind::FlumeHalf, {}), sub(SubKind::Daylight, {})}, 0.25, 0.014);
    canal("Damba ichida kanal b=2.0 h=1.8 (1:1.5)", {sub(SubKind::ChannelHalf, {{"b", 2.0}, {"h", 1.8}, {"m", 1.5}, {"t", 0.2}, {"c", 3.0}}),
                                                      sub(SubKind::Daylight, {{"mf", 2.0}})},
          2.0, 0.015);
    auto trench = [&](const char* name, Subassembly s) {
        Assembly a;
        a.name = name;
        a.group = "TRENCH";
        a.right = {std::move(s)};
        v.push_back(a);
    };
    trench("Quvur transheyasi Ø500 (1:0.5)", sub(SubKind::PipeTrenchHalf, {}));
    trench("Quvur transheyasi Ø1000, vertikal devor (tirgakli)", sub(SubKind::PipeTrenchHalf, {{"D", 1.0}, {"e", 0.4}, {"tb", 0.2}, {"m", 0}}));
    return v;
}

double SecShape::area() const {
    double a = 0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const auto& p = poly[i];
        const auto& q = poly[(i + 1) % poly.size()];
        a += p.first * q.second - q.first * p.second;
    }
    return std::abs(a) / 2;
}
double SecShape::centroidO() const {
    double a = 0, cx = 0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const auto& p = poly[i];
        const auto& q = poly[(i + 1) % poly.size()];
        const double cr = p.first * q.second - q.first * p.second;
        a += cr;
        cx += (p.first + q.first) * cr;
    }
    return std::abs(a) > 1e-12 ? cx / (3 * a) : 0;
}

int Corridor::errors() const {
    int n = 0;
    for (const auto& i : issues) n += i.error;
    return n;
}

CorridorParams defaultCorridorParams() {
    CorridorParams p;
    p.assemblies = {builtinAssemblies()[1]};
    return p;
}

// ---------------------------------------------------------------- bitta kesim
namespace {
struct Cur {
    double u, z, ud, zd;  // ustki ulanish nuqtasi va asos (datum) ulanishi, u — o'qdan tashqariga
};
struct SideOut {
    std::vector<SecPoint> top;                                // u bo'yicha (o'qdan tashqariga)
    std::vector<std::pair<double, double>> datum;
    std::vector<SecShape> shapes;                             // u koordinatada
    bool daylight = true;
};

// ray (u0,z0) dan k qiyalik bilan: ground - line ishorasi o'zgaradigan birinchi nuqta
bool rayToGround(const std::function<double(double)>& g, double u0, double z0, double k, double maxU, double& u, double& z, bool vertical = false) {
    if (vertical) {  // tik devor
        u = u0;
        z = g(u0);
        return true;
    }
    auto f = [&](double t) { return g(u0 + t) - (z0 + k * t); };
    const double f0 = f(0);
    if (std::abs(f0) < 1e-9) { u = u0; z = z0; return true; }
    double a = 0, fa = f0;
    for (double t = 0.25; t <= maxU + 1e-9; t += 0.25) {
        const double ft = f(t);
        if ((ft > 0) != (fa > 0)) {
            double lo = a, hi = t;
            for (int i = 0; i < 50; ++i) {
                const double mid = (lo + hi) / 2;
                ((f(mid) > 0) == (fa > 0) ? lo : hi) = mid;
            }
            u = u0 + (lo + hi) / 2;
            z = z0 + k * (lo + hi) / 2;
            return true;
        }
        a = t;
        fa = ft;
    }
    u = u0 + maxU;
    z = z0 + k * maxU;
    return false;
}

void buildSide(const std::vector<Subassembly>& subs, double design, const std::function<double(double)>& g, double maxD, SideOut& out) {
    Cur c{0, design, 0, design};
    bool started = false;
    auto top = [&](double u, double z, const char* code) { out.top.push_back({u, z, code, 1}); };
    auto dat = [&](double u, double z) {
        if (!out.datum.empty() && std::abs(out.datum.back().first - u) < 1e-9 && std::abs(out.datum.back().second - z) < 1e-9) return;
        out.datum.push_back({u, z});
    };
    auto shape = [&](const char* m, std::vector<std::pair<double, double>> poly) { out.shapes.push_back({m, std::move(poly)}); };
    for (const auto& s : subs) {
        const SubKind k = s.kind;
        if (!started) {
            started = true;
            if (!subIsCenter(k)) {
                top(0, design, "CROWN");
                dat(0, design);
            }
        }
        switch (k) {
            case SubKind::Lane:
            case SubKind::Shoulder:
            case SubKind::GenericLink:
            case SubKind::Sidewalk: {
                const double w = s.get("w"), sl = s.get("slope") / 100;
                const double z1 = c.z + w * sl;
                std::vector<std::pair<double, const char*>> layers;
                if (k == SubKind::Lane) layers = {{s.get("t1"), "ASPHALT"}, {s.get("t2"), "BASE"}, {s.get("t3"), "SUBBASE"}};
                else if (k == SubKind::Shoulder) layers = {{s.get("t"), "SHOULDER"}};
                else if (k == SubKind::Sidewalk) layers = {{s.get("t"), "SIDEWALK"}, {s.get("tb"), "BASE"}};
                else layers = {{s.get("t"), "GENERIC"}};
                double d0 = 0;
                for (const auto& [t, m] : layers) {
                    if (t <= 0) continue;
                    shape(m, {{c.u, c.z - d0}, {c.u + w, z1 - d0}, {c.u + w, z1 - d0 - t}, {c.u, c.z - d0 - t}});
                    d0 += t;
                }
                dat(c.ud, c.zd);
                dat(c.u, c.z - d0);
                dat(c.u + w, z1 - d0);
                top(c.u + w, z1, k == SubKind::Lane ? "ETW" : k == SubKind::Shoulder ? "ETS" : k == SubKind::Sidewalk ? "SW_OUT" : "LINK");
                c = {c.u + w, z1, c.u + w, z1 - d0};
                break;
            }
            case SubKind::Curb: {
                const double h = s.get("h"), w = s.get("w"), d = s.get("d");
                top(c.u, c.z + h, "CURB_TOP");
                top(c.u + w, c.z + h, "BACK_CURB");
                shape("CONCRETE", {{c.u, c.z + h}, {c.u + w, c.z + h}, {c.u + w, c.z - d}, {c.u, c.z - d}});
                dat(c.ud, c.zd);
                dat(c.u, c.z - d);
                dat(c.u + w, c.z - d);
                c = {c.u + w, c.z + h, c.u + w, c.z - d};
                break;
            }
            case SubKind::Median: {
                const double w = s.get("w") / 2, h = s.get("h"), d = s.get("d");
                top(0, c.z + h, "MEDIAN_C");
                top(w, c.z + h, "MEDIAN_E");
                top(w, c.z, "MEDIAN_F");
                shape("CONCRETE", {{std::max(0.0, w - 0.15), c.z + h}, {w, c.z + h}, {w, c.z - 0.3}, {std::max(0.0, w - 0.15), c.z - 0.3}});
                dat(0, c.z - d);
                dat(w, c.z - d);
                c = {w, c.z, w, c.z - d};
                break;
            }
            case SubKind::Ditch: {
                const double m1 = s.get("m1"), d = s.get("d"), b = s.get("b"), m2 = s.get("m2");
                const double u1 = c.u + m1 * d, u2 = u1 + b, u3 = u2 + m2 * d;
                top(u1, c.z - d, "DITCH_IN");
                top(u2, c.z - d, "DITCH_OUT");
                top(u3, c.z, "DITCH_TOP");
                dat(c.ud, c.zd);
                dat(c.u, c.z);
                dat(u1, c.z - d);
                dat(u2, c.z - d);
                dat(u3, c.z);
                c = {u3, c.z, u3, c.z};
                break;
            }
            case SubKind::ChannelHalf: {
                const double b = s.get("b") / 2, h = s.get("h"), m = s.get("m"), t = s.get("t"), cb = s.get("c");
                const double z0 = c.z;
                // ichki: A(0,z0) B(b,z0) C(b+mh, z0+h); tashqi — qalinlik t ga parallel siljigan
                const double L = std::sqrt(1 + m * m);
                const double nx = 1 / L, nz = -m / L;               // yon devor tashqi normali
                // B' = yon devor siljigan chizig'i ∩ z = z0 - t
                const double bx = b + nx * t, bz = z0 + nz * t;      // B + n t
                const double ux = m / L, uz = 1 / L;                 // yon devor yo'nalishi
                const double lam = uz > 1e-12 ? ((z0 - t) - bz) / uz : 0;
                const double Bx = bx + ux * lam, Bz = z0 - t;
                const double lamC = uz > 1e-12 ? ((z0 + h) - bz) / uz : 0;
                const double Cx = bx + ux * lamC;
                const double cx = b + m * h;
                top(0, z0, "CH_BOTTOM_C");
                top(b, z0, "CH_BOTTOM");
                top(cx, z0 + h, "CH_TOP");
                if (t > 0) {
                    top(Cx, z0 + h, "CH_LINING");
                    shape("CONCRETE", {{0, z0}, {b, z0}, {cx, z0 + h}, {Cx, z0 + h}, {Bx, Bz}, {0, z0 - t}});
                }
                const double ub = (t > 0 ? Cx : cx) + cb;
                if (cb > 0) top(ub, z0 + h, "BANK");
                dat(0, z0 - t);
                dat(t > 0 ? Bx : b, z0 - t);
                dat(t > 0 ? Cx : cx, z0 + h);
                dat(ub, z0 + h);
                c = {ub, z0 + h, ub, z0 + h};
                break;
            }
            case SubKind::FlumeHalf: {
                const double B = s.get("B") / 2, h = s.get("h"), t = s.get("t"), pad = s.get("pad"), padw = s.get("padw"), cb = s.get("c");
                const double z0 = c.z;
                std::vector<std::pair<double, double>> inner;
                for (int i = 0; i <= 6; ++i) {
                    const double u = B * i / 6.0;
                    inner.push_back({u, z0 + h * (u / B) * (u / B)});
                }
                for (std::size_t i = 0; i < inner.size(); ++i)
                    top(inner[i].first, inner[i].second, i == 0 ? "FL_BOTTOM_C" : i + 1 == inner.size() ? "FL_TOP" : "FL");
                std::vector<std::pair<double, double>> poly = inner;
                poly.push_back({B + t, z0 + h});
                for (int i = 6; i >= 0; --i) {  // tashqi yuza: normal bo'yicha t ga
                    const double u = B * i / 6.0;
                    const double dz = 2 * h * u / (B * B);  // hosila
                    const double L = std::sqrt(1 + dz * dz);
                    poly.push_back({u + t * dz / L, z0 + h * (u / B) * (u / B) - t / L});
                }
                shape("CONCRETE", poly);
                const double pw = B + t + padw;
                if (pad > 0) shape("BEDDING", {{0, z0 - t}, {pw, z0 - t}, {pw, z0 - t - pad}, {0, z0 - t - pad}});
                top(B + t, z0 + h, "FL_OUT");
                const double ub = B + t + cb;
                if (cb > 0) top(ub, z0 + h, "BANK");
                dat(0, z0 - t - pad);
                dat(pw, z0 - t - pad);
                dat(B + t, z0 + h);
                dat(ub, z0 + h);
                c = {ub, z0 + h, ub, z0 + h};
                break;
            }
            case SubKind::PipeTrenchHalf: {
                const double D = s.get("D"), e = s.get("e"), tb = s.get("tb"), m = s.get("m");
                const double z0 = c.z, W = D / 2 + e;
                std::vector<std::pair<double, double>> pipe;
                for (int i = 0; i <= 12; ++i) {
                    const double a = -kPI / 2 + kPI * i / 12;
                    pipe.push_back({D / 2 * std::cos(a), z0 + D / 2 + D / 2 * std::sin(a)});
                }
                shape("PIPE", pipe);
                if (tb > 0) shape("BEDDING", {{0, z0}, {W + (m > 0 ? m * tb : 0), z0}, {W, z0 - tb}, {0, z0 - tb}});
                top(0, z0 - tb, "TR_BOTTOM_C");
                top(W, z0 - tb, "TR_BOTTOM");
                double ue, ze;
                out.daylight = rayToGround(g, W, z0 - tb, m > 0 ? 1 / m : 0, maxD, ue, ze, m <= 0);
                top(ue, ze, "TR_TOP");
                dat(0, z0 - tb);
                dat(W, z0 - tb);
                dat(ue, ze);
                // qayta ko'mish: transheya (to'shamadan yuqori, yergacha) − quvur
                std::vector<std::pair<double, double>> bf{{0, z0}, {W + (m > 0 ? m * tb : 0), z0}, {ue, ze}};
                for (int i = 10; i >= 0; --i) {
                    const double u = ue * i / 10.0;
                    bf.push_back({u, g(u)});
                }
                shape("BACKFILL", bf);
                shape("_MINUS_BACKFILL", pipe);  // quvur hajmi qayta ko'mishdan ayriladi
                c = {ue, ze, ue, ze};
                return;
            }
            case SubKind::RetainingWall: {
                const double h = s.get("h"), tw = s.get("tw"), wf = s.get("wf"), tf = s.get("tf"), de = s.get("de");
                const double gb = std::min(c.z, g(c.u + tw));
                const double zb = gb - de;
                const double ov = std::max(0.0, (wf - tw) / 2);
                shape("CONCRETE", {{c.u, c.z + h}, {c.u + tw, c.z + h}, {c.u + tw, zb}, {c.u, zb}});
                shape("CONCRETE", {{c.u - ov, zb}, {c.u + tw + ov, zb}, {c.u + tw + ov, zb - tf}, {c.u - ov, zb - tf}});
                top(c.u, c.z + h, "WALL_TOP");
                top(c.u + tw, c.z + h, "WALL_TOP_O");
                const double gt = g(c.u + tw);
                top(c.u + tw, gt, "WALL_TOE");
                dat(c.ud, c.zd);
                dat(c.u, zb - tf);
                dat(c.u + tw + ov, zb - tf);
                dat(c.u + tw + ov, std::min(gt, zb));
                c = {c.u + tw, gt, c.u + tw, gt};
                return;
            }
            case SubKind::Daylight:
            case SubKind::DaylightDitch:
            case SubKind::DaylightBench: {
                const double mc = s.get("mc", 1.5), mf = s.get("mf", 1.5);
                dat(c.ud, c.zd);
                dat(c.u, c.z);
                const bool cut = g(c.u) >= c.z;
                double ue, ze;
                if (!cut) {
                    out.daylight = rayToGround(g, c.u, c.z, -1 / mf, maxD, ue, ze);
                } else if (k == SubKind::DaylightDitch) {
                    const double m1 = s.get("m1"), d = s.get("d"), b = s.get("b");
                    const double u1 = c.u + m1 * d, u2 = u1 + b;
                    top(u1, c.z - d, "DITCH_IN");
                    top(u2, c.z - d, "DITCH_OUT");
                    dat(u1, c.z - d);
                    dat(u2, c.z - d);
                    out.daylight = rayToGround(g, u2, c.z - d, 1 / mc, maxD, ue, ze);
                } else if (k == SubKind::DaylightBench) {
                    const double H = s.get("H", 6), bw = s.get("bw", 2);
                    double u = c.u, z = c.z;
                    out.daylight = false;
                    for (int it = 0; it < 50 && u - c.u < maxD; ++it) {
                        double uu, zz;
                        if (rayToGround(g, u, z, 1 / mc, H * mc, uu, zz) && zz <= z + H + 1e-9 && (g(u + H * mc) > z + H || std::abs(zz - (z + H)) > 1e-6)) {
                            ue = uu;
                            ze = zz;
                            out.daylight = true;
                            break;
                        }
                        u += H * mc;
                        z += H;
                        top(u, z, "BENCH_IN");
                        dat(u, z);
                        if (rayToGround(g, u, z, 0, bw, uu, zz) && uu < u + bw - 1e-9) {
                            ue = uu;
                            ze = zz;
                            out.daylight = true;
                            break;
                        }
                        u += bw;
                        top(u, z, "BENCH_OUT");
                        dat(u, z);
                        ue = u;
                        ze = z;
                    }
                } else {
                    out.daylight = rayToGround(g, c.u, c.z, 1 / mc, maxD, ue, ze);
                }
                top(ue, ze, "DAYLIGHT");
                dat(ue, ze);
                c = {ue, ze, ue, ze};
                return;
            }
        }
    }
    dat(c.ud, c.zd);
}
}  // namespace

Section buildSection(const Assembly& a, double design, const std::function<double(double)>& ground, double maxD, double waterSlope) {
    Section sec;
    sec.design = design;
    sec.ground = ground(0);
    SideOut R, L;
    buildSide(a.right, design, ground, maxD, R);
    buildSide(a.symmetric ? a.right : a.left, design, [&](double u) { return ground(-u); }, maxD, L);
    sec.daylightR = R.daylight;
    sec.daylightL = L.daylight;
    // ustki chiziq va asos: chap (teskari, offset manfiy) + o'ng; markaz nuqtasi bir marta
    for (auto it = L.top.rbegin(); it != L.top.rend(); ++it) sec.pts.push_back({-it->o, it->z, it->code, -1});
    for (const auto& q : R.top) {
        if (!sec.pts.empty() && std::abs(q.o) < 1e-9 && std::abs(sec.pts.back().o) < 1e-9 && std::abs(sec.pts.back().z - q.z) < 1e-9) {
            sec.pts.back().side = 0;
            continue;
        }
        sec.pts.push_back({q.o, q.z, q.code, 1});
    }
    for (auto it = L.datum.rbegin(); it != L.datum.rend(); ++it) sec.datum.push_back({-it->first, it->second});
    for (const auto& q : R.datum) {
        if (!sec.datum.empty() && std::abs(sec.datum.back().first - q.first) < 1e-9 && std::abs(sec.datum.back().second - q.second) < 1e-9) continue;
        sec.datum.push_back(q);
    }
    for (const auto& sh : R.shapes) sec.shapes.push_back(sh);
    for (auto sh : L.shapes) {
        for (auto& q : sh.poly) q.first = -q.first;
        sec.shapes.push_back(std::move(sh));
    }
    // suv sathi (kanal/lotok, Q > 0)
    if (a.q > 0 && waterSlope > 0) {
        for (const auto& sb : a.right) {
            if (sb.kind != SubKind::ChannelHalf && sb.kind != SubKind::FlumeHalf) continue;
            WwSection ws;
            ws.n = a.n;
            if (sb.kind == SubKind::ChannelHalf) {
                ws.type = WwType::ConcreteCanal;
                ws.b = sb.get("b");
                ws.h = sb.get("h");
                ws.m = sb.get("m");
            } else {
                ws.type = WwType::Flume;
                ws.B = sb.get("B");
                ws.h = sb.get("h");
            }
            const double yn = wwNormalDepth(ws, a.q, waterSlope);
            const double y = yn < 0 ? ws.h : yn;
            sec.water = design + y;
            SecShape w{"WATER", {}};
            if (sb.kind == SubKind::ChannelHalf) {
                const double hb = ws.b / 2, T = hb + ws.m * y;
                w.poly = {{-T, design + y}, {T, design + y}, {hb, design}, {-hb, design}};
            } else {
                const double T = wwTopWidth(ws, y) / 2;
                for (int i = -6; i <= 6; ++i) {
                    const double u = T * i / 6.0;
                    w.poly.push_back({u, design + ws.h * (u / (ws.B / 2)) * (u / (ws.B / 2))});
                }
                std::reverse(w.poly.begin(), w.poly.end());
                w.poly.insert(w.poly.begin(), {-T, design + y});
                w.poly.insert(w.poly.begin(), {T, design + y});
            }
            sec.shapes.push_back(w);
            break;
        }
    }
    // yer chizig'i va kesma/to'kma (asos chizig'i bilan yer orasidagi maydon)
    if (sec.datum.size() >= 2) {
        double o0 = 1e18, o1 = -1e18;
        for (const auto& q : sec.datum) { o0 = std::min(o0, q.first); o1 = std::max(o1, q.first); }
        for (const auto& q : sec.pts) { o0 = std::min(o0, q.o); o1 = std::max(o1, q.o); }
        for (double o = o0 - 5; o <= o1 + 5 + 1e-9; o += 0.5) sec.groundLine.push_back({o, ground(o)});
        // aniq integral: tanaffus nuqtalari — asos chizig'i uchlari + yer 0.25 m qadamda; har oraliqda ikkala chiziq to'g'ri
        const double da = sec.datum.front().first, db = sec.datum.back().first;
        std::vector<double> bp;
        for (const auto& q : sec.datum) bp.push_back(q.first);
        for (double o = std::ceil(da / 0.25) * 0.25; o < db; o += 0.25) bp.push_back(o);
        std::sort(bp.begin(), bp.end());
        bp.erase(std::unique(bp.begin(), bp.end(), [](double x, double y) { return std::abs(x - y) < 1e-9; }), bp.end());
        auto segAt = [&](double o) -> int {
            for (std::size_t i = 0; i + 1 < sec.datum.size(); ++i) {
                const double lo = std::min(sec.datum[i].first, sec.datum[i + 1].first), hi = std::max(sec.datum[i].first, sec.datum[i + 1].first);
                if (hi - lo > 1e-12 && o > lo && o < hi) return static_cast<int>(i);
            }
            return -1;
        };
        double cutM = 0, fillM = 0;
        auto addPiece = [&](double o1, double f1, double o2, double f2) {  // f — bitta ishorada
            const double a2 = (f1 + f2) / 2 * (o2 - o1);
            const double cm = std::abs(f1 + f2) > 1e-15 ? o1 + (o2 - o1) * (f1 + 2 * f2) / (3 * (f1 + f2)) : (o1 + o2) / 2;  // trapetsiya markazi
            if (a2 >= 0) { sec.cut += a2; cutM += a2 * cm; }
            else { sec.fill -= a2; fillM -= a2 * cm; }
        };
        for (std::size_t i = 0; i + 1 < bp.size(); ++i) {
            const double o1 = bp[i], o2 = bp[i + 1];
            const int sg = segAt((o1 + o2) / 2);
            if (sg < 0) continue;
            const auto& P = sec.datum[static_cast<std::size_t>(sg)];
            const auto& Q = sec.datum[static_cast<std::size_t>(sg) + 1];
            auto D = [&](double o) { return P.second + (Q.second - P.second) * (o - P.first) / (Q.first - P.first); };
            const double f1 = ground(o1) - D(o1), f2 = ground(o2) - D(o2);
            if ((f1 >= 0) == (f2 >= 0)) addPiece(o1, f1, o2, f2);
            else {
                const double oz = o1 + f1 / (f1 - f2) * (o2 - o1);
                addPiece(o1, f1, oz, 0);
                addPiece(oz, 0, o2, f2);
            }
        }
        if (sec.cut < 1e-9) sec.cut = 0;
        if (sec.fill < 1e-9) sec.fill = 0;
        sec.cutCentroid = sec.cut > 1e-9 ? cutM / sec.cut : 0;
        sec.fillCentroid = sec.fill > 1e-9 ? fillM / sec.fill : 0;
    }
    return sec;
}

// ---------------------------------------------------------------- koridor
namespace {
const Assembly& assemblyAt(const CorridorParams& p, double s, int* idx = nullptr) {
    int ai = 0;
    for (const auto& r : p.regions)
        if (s >= r.s0 - 1e-9 && s <= r.s1 + 1e-9) { ai = r.assembly; break; }
    ai = std::clamp(ai, 0, static_cast<int>(p.assemblies.size()) - 1);
    if (idx) *idx = ai;
    return p.assemblies[static_cast<std::size_t>(ai)];
}
std::map<std::string, double> shapeAreas(const Section& s, std::map<std::string, double>* centroid = nullptr) {
    std::map<std::string, double> a, m;
    for (const auto& sh : s.shapes) {
        if (sh.material == "WATER") continue;
        const double ar = sh.area();
        if (sh.material.rfind("_MINUS_", 0) == 0) {
            const std::string k = sh.material.substr(7);
            a[k] -= ar;
            m[k] -= ar * sh.centroidO();
        } else {
            a[sh.material] += ar;
            m[sh.material] += ar * sh.centroidO();
        }
    }
    if (centroid)
        for (const auto& [k, v] : a) (*centroid)[k] = std::abs(v) > 1e-12 ? m[k] / v : 0;
    return a;
}
}  // namespace

void recomputeCorridor(Corridor& c, const CorridorParams& p) {
    c.issues.clear();
    c.volumes.clear();
    c.cutAvg = c.fillAvg = c.cutExact = c.fillExact = 0;
    auto issue = [&](bool e, const char* k, std::vector<std::string> a, double s) { c.issues.push_back({e, k, std::move(a), s}); };
    if (p.assemblies.empty()) {
        issue(true, "cor.chk.no_assembly", {}, 0);
        return;
    }
    if (c.sec.size() < 2) {
        issue(true, "cor.chk.no_axis", {}, 0);
        return;
    }
    if (c.groundPoints == 0) issue(true, "cor.chk.no_ground", {fc(p.ground)}, 0);
    if (c.designPoints == 0) issue(false, "cor.chk.no_design", {}, 0);
    c.length = c.sec.back().s - c.sec.front().s;
    auto groundFn = [&](double x, double y, double dx, double dy) {
        return [&, x, y, dx, dy](double o) { return idw(c.groundPts, x + dy * o, y - dx * o, p.ground); };
    };
    auto slopeAt = [&](std::size_t i) {
        const std::size_t a = i + 1 < c.sec.size() ? i : i - 1;
        const double ds = std::max(1e-9, c.sec[a + 1].s - c.sec[a].s);
        return (c.sec[a].design - c.sec[a + 1].design) / ds;
    };
    bool dlMissing = false;
    for (std::size_t i = 0; i < c.sec.size(); ++i) {
        auto& s = c.sec[i];
        int ai = 0;
        const Assembly& a = assemblyAt(p, s.s, &ai);
        Section n = buildSection(a, s.design, groundFn(s.x, s.y, s.dx, s.dy), p.maxDaylight, slopeAt(i));
        n.s = s.s; n.x = s.x; n.y = s.y; n.dx = s.dx; n.dy = s.dy; n.region = ai;
        s = std::move(n);
        if ((!s.daylightL || !s.daylightR) && !dlMissing) {
            dlMissing = true;
            issue(true, "cor.chk.daylight", {pk(s.s), fc(p.maxDaylight, 0)}, s.s);
        }
    }
    // suv yo'li: teskari nishablik
    for (std::size_t i = 0; i + 1 < c.sec.size(); ++i) {
        const Assembly& a = assemblyAt(p, c.sec[i].s);
        if (a.group == "ROAD") continue;
        if (c.sec[i].design - c.sec[i + 1].design <= 0) {
            issue(true, "cor.chk.reverse", {pk(c.sec[i].s), fc((c.sec[i].design - c.sec[i + 1].design) / std::max(1e-9, c.sec[i + 1].s - c.sec[i].s) * 1000)},
                  c.sec[i].s);
            break;
        }
    }
    // hajmlar: o'rtacha maydon va aniq (Simpson o'rta kesim bilan + egri chiziq Pappus tuzatmasi)
    for (std::size_t i = 0; i + 1 < c.sec.size(); ++i) {
        const auto& A = c.sec[i];
        const auto& B = c.sec[i + 1];
        const double L = B.s - A.s;
        if (L <= 1e-9) continue;
        // o'rta kesim
        const double xm = (A.x + B.x) / 2, ym = (A.y + B.y) / 2;
        double dxm = A.dx + B.dx, dym = A.dy + B.dy;
        const double dl = std::hypot(dxm, dym);
        if (dl > 1e-9) { dxm /= dl; dym /= dl; } else { dxm = A.dx; dym = A.dy; }
        const Assembly& am = assemblyAt(p, (A.s + B.s) / 2);
        const Section M = buildSection(am, (A.design + B.design) / 2, groundFn(xm, ym, dxm, dym), p.maxDaylight, slopeAt(i));
        double dth = std::atan2(B.dy, B.dx) - std::atan2(A.dy, A.dx);
        while (dth > kPI) dth -= 2 * kPI;
        while (dth < -kPI) dth += 2 * kPI;
        auto exact = [&](double a, double m, double b, double ca, double cm, double cb) {
            const double V = L / 6 * (a + 4 * m + b);
            const double tot = a + 4 * m + b;
            const double e = std::abs(tot) > 1e-12 ? (a * ca + 4 * m * cm + b * cb) / tot : 0;
            return V * (1 + e * dth / L);  // chapga burilishda o'ngdagi massa uzoqroq (o — o'ngga +)
        };
        c.cutAvg += (A.cut + B.cut) / 2 * L;
        c.fillAvg += (A.fill + B.fill) / 2 * L;
        c.cutExact += exact(A.cut, M.cut, B.cut, A.cutCentroid, M.cutCentroid, B.cutCentroid);
        c.fillExact += exact(A.fill, M.fill, B.fill, A.fillCentroid, M.fillCentroid, B.fillCentroid);
        std::map<std::string, double> ca, cm, cb;
        const auto aa = shapeAreas(A, &ca), mm = shapeAreas(M, &cm), bb = shapeAreas(B, &cb);
        std::set<std::string> keys;
        for (const auto& [k, v] : aa) keys.insert(k);
        for (const auto& [k, v] : mm) keys.insert(k);
        for (const auto& [k, v] : bb) keys.insert(k);
        auto g = [](const std::map<std::string, double>& m, const std::string& k) {
            auto it = m.find(k);
            return it == m.end() ? 0.0 : it->second;
        };
        for (const auto& k : keys) {
            auto& v = c.volumes[k];
            v.first += (g(aa, k) + g(bb, k)) / 2 * L;
            v.second += exact(g(aa, k), g(mm, k), g(bb, k), g(ca, k), g(cm, k), g(cb, k));
        }
    }
}

Corridor buildCorridor(const Document& doc, const CorridorParams& p) {
    Corridor c;
    // qatlamlar: yo'l va suv yo'li taxminlari birlashtiriladi
    std::map<std::string, WwLayer> lm = p.layers;
    if (lm.empty())
        for (const auto& [name, l] : doc.layers()) {
            const RoadLayer r = guessRoadLayer(name);
            WwLayer w = guessWwLayer(name);
            if (r == RoadLayer::Axis) w = WwLayer::Axis;
            else if (w == WwLayer::Ignore && r == RoadLayer::Ground) w = WwLayer::Ground;
            else if (w == WwLayer::Ignore && r == RoadLayer::Design) w = WwLayer::Design;
            lm[name] = w;
        }
    WwParams wp = defaultWwParams();
    wp.step = p.step;
    wp.ground = p.ground;
    const WwModel m = buildWaterway(doc, lm, wp);
    c.groundPts = m.groundPts;
    c.groundPoints = m.groundPoints;
    c.designPoints = m.designPoints;
    for (const auto& t : m.st) {
        Section s;
        s.s = t.s; s.x = t.x; s.y = t.y; s.dx = t.dx; s.dy = t.dy;
        s.ground = t.ground;
        s.design = t.bottom;
        c.sec.push_back(s);
    }
    if (c.designPoints == 0 && !c.sec.empty() && !p.assemblies.empty()) {
        // loyiha belgisi yo'q: yo'l — silliqlangan yer + 0.5 m; kanal/transheya — yerdan kesim chuqurligida
        const Assembly& a = p.assemblies.front();
        double depth = 0.5;
        if (a.group != "ROAD")
            for (const auto& sb : a.right) {
                if (sb.kind == SubKind::ChannelHalf || sb.kind == SubKind::FlumeHalf) depth = -sb.get("h", 1.0);
                if (sb.kind == SubKind::PipeTrenchHalf) depth = -(sb.get("D", 0.5) + 1.2);
            }
        for (auto& s : c.sec) {
            double sum = 0;
            int n = 0;
            for (const auto& o : c.sec)
                if (std::abs(o.s - s.s) <= 50) { sum += o.ground; ++n; }
            s.design = sum / n + depth;
        }
    }
    recomputeCorridor(c, p);
    return c;
}

// ---------------------------------------------------------------- JSON
std::string assemblyToJson(const Assembly& a) {
    auto side = [](const std::vector<Subassembly>& v) {
        std::string s = "[";
        for (std::size_t i = 0; i < v.size(); ++i) {
            s += std::format("{}{{\"kind\":\"{}\",\"p\":{{", i ? "," : "", subKey(v[i].kind));
            for (std::size_t j = 0; j < v[i].p.size(); ++j) s += std::format("{}\"{}\":{}", j ? "," : "", v[i].p[j].key, v[i].p[j].value);
            s += "}}";
        }
        return s + "]";
    };
    std::string name;
    for (char ch : a.name) {
        if (ch == '"' || ch == '\\') name += '\\';
        name += ch;
    }
    return std::format("{{\"name\":\"{}\",\"group\":\"{}\",\"symmetric\":{},\"q\":{},\"n\":{},\"right\":{},\"left\":{}}}", name, a.group,
                       a.symmetric ? "true" : "false", a.q, a.n, side(a.right), side(a.left));
}

namespace {
Assembly assemblyFromValue(const json::Value& v) {
    Assembly a;
    if (const auto* x = v.find("name"); x && x->isString()) a.name = x->str();
    if (const auto* x = v.find("group"); x && x->isString()) a.group = x->str();
    if (const auto* x = v.find("symmetric"); x && x->isBool()) a.symmetric = x->boolean();
    if (const auto* x = v.find("q"); x && x->isNumber()) a.q = x->num();
    if (const auto* x = v.find("n"); x && x->isNumber()) a.n = x->num();
    auto side = [](const json::Value* arr, std::vector<Subassembly>& out) {
        if (!arr || !arr->isArray()) return;
        for (const auto& e : arr->arr()) {
            const auto* k = e.find("kind");
            SubKind sk;
            if (!k || !k->isString() || !subFromKey(k->str(), sk)) continue;
            Subassembly s = defaultSub(sk);
            if (const auto* pp = e.find("p"); pp && pp->isObject())
                for (const auto& [key, val] : pp->obj())
                    if (val.isNumber()) s.set(key, val.num());
            out.push_back(std::move(s));
        }
    };
    side(v.find("right"), a.right);
    side(v.find("left"), a.left);
    return a;
}
}  // namespace

Assembly assemblyFromJson(const std::string& text) {
    try {
        return assemblyFromValue(json::parse(text));
    } catch (const json::ParseError& e) {
        throw Error("cor.err.format", {e.what()});
    }
}

std::string corridorToJson(const Corridor& c, const CorridorParams& p) {
    std::string s = std::format("{{\"format\":\"asiacad-corridor\",\"version\":1,\"step\":{},\"ground\":{},\"maxDaylight\":{},\"groundPoints\":{},"
                                "\"designPoints\":{},\"assemblies\":[",
                                p.step, p.ground, p.maxDaylight, c.groundPoints, c.designPoints);
    for (std::size_t i = 0; i < p.assemblies.size(); ++i) s += (i ? "," : "") + assemblyToJson(p.assemblies[i]);
    s += "],\"regions\":[";
    for (std::size_t i = 0; i < p.regions.size(); ++i)
        s += std::format("{}[{},{},{}]", i ? "," : "", p.regions[i].s0, std::min(p.regions[i].s1, 1e15), p.regions[i].assembly);
    s += "],\"st\":[";
    for (std::size_t i = 0; i < c.sec.size(); ++i) {
        const auto& t = c.sec[i];
        s += std::format("{}[{},{},{},{},{},{},{}]", i ? "," : "", t.s, t.x, t.y, t.dx, t.dy, t.ground, t.design);
    }
    s += "],\"groundPts\":[";
    for (std::size_t i = 0; i < c.groundPts.size(); ++i)
        s += std::format("{}[{},{},{}]", i ? "," : "", c.groundPts[i][0], c.groundPts[i][1], c.groundPts[i][2]);
    return s + "]}";
}

void corridorFromJson(const std::string& text, Corridor& c, CorridorParams& p) {
    json::Value root;
    try {
        root = json::parse(text);
    } catch (const json::ParseError& e) {
        throw Error("cor.err.format", {e.what()});
    }
    auto num = [&](const char* k, double def) {
        const json::Value* v = root.find(k);
        return v && v->isNumber() ? v->num() : def;
    };
    p.step = num("step", p.step);
    p.ground = num("ground", p.ground);
    p.maxDaylight = num("maxDaylight", p.maxDaylight);
    if (const auto* a = root.find("assemblies"); a && a->isArray()) {
        p.assemblies.clear();
        for (const auto& e : a->arr()) p.assemblies.push_back(assemblyFromValue(e));
    }
    p.regions.clear();
    if (const auto* a = root.find("regions"); a && a->isArray())
        for (const auto& e : a->arr())
            if (e.isArray() && e.arr().size() >= 3)
                p.regions.push_back({e.arr()[0].num(), e.arr()[1].num(), static_cast<int>(e.arr()[2].num())});
    c = {};
    c.groundPoints = static_cast<int>(num("groundPoints", 0));
    c.designPoints = static_cast<int>(num("designPoints", 0));
    if (const auto* a = root.find("st"); a && a->isArray())
        for (const auto& q : a->arr()) {
            if (!q.isArray() || q.arr().size() < 7) continue;
            const auto& v = q.arr();
            Section t;
            t.s = v[0].num(); t.x = v[1].num(); t.y = v[2].num(); t.dx = v[3].num(); t.dy = v[4].num();
            t.ground = v[5].num(); t.design = v[6].num();
            c.sec.push_back(t);
        }
    if (const auto* a = root.find("groundPts"); a && a->isArray())
        for (const auto& q : a->arr())
            if (q.isArray() && q.arr().size() >= 3) c.groundPts.push_back({q.arr()[0].num(), q.arr()[1].num(), q.arr()[2].num()});
    recomputeCorridor(c, p);
}

// ---------------------------------------------------------------- chizma
std::vector<std::unique_ptr<IEntity>> drawCorridor3D(const Corridor& c) {
    std::vector<std::unique_ptr<IEntity>> out;
    if (c.sec.size() < 2) return out;
    auto put = [&](std::vector<Vec3> pts, const std::string& layer) {
        if (pts.size() < 2) return;
        auto e = std::make_unique<PolylineEntity>(std::move(pts), false);
        e->setLayer(layer);
        out.push_back(std::move(e));
    };
    auto W = [](const Section& s, double o, double z) { return Vec3{s.x + s.dy * o, s.y - s.dx * o, z}; };
    // kod bo'yicha chiziqlar: (tomon, kod, tartib) kaliti
    std::map<std::tuple<int, std::string, int>, std::vector<Vec3>> lines;
    for (const auto& s : c.sec) {
        std::map<std::pair<int, std::string>, int> cnt;
        std::vector<Vec3> sec;
        for (const auto& q : s.pts) {
            const int ord = cnt[{q.side, q.code}]++;
            lines[{q.side, q.code, ord}].push_back(W(s, q.o, q.z));
            sec.push_back(W(s, q.o, q.z));
        }
        put(sec, "KOR_KESIM");
        std::vector<Vec3> d;
        for (const auto& q : s.datum) d.push_back(W(s, q.first, q.second));
        put(d, "KOR_DATUM");
    }
    for (auto& [k, pts] : lines)
        if (pts.size() >= 2) put(pts, "KOR_" + std::get<1>(k));
    return out;
}

}  // namespace asiacad::civil
