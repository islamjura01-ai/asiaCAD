#include <asiacad/bim/rebar.h>

#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/json.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

namespace asiacad::bim {

namespace {

constexpr double kPI = 3.14159265358979323846;

double len3(const std::vector<Vec3>& p, bool closed) {
    double l = 0;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) l += p[i].distanceTo(p[i + 1]);
    if (closed && p.size() > 2) l += p.back().distanceTo(p.front());
    return l;
}

// Ko'pburchakni (teshiklar bilan) gorizontal/vertikal skan chiziqda kesish -> [a,b] oraliqlar
std::vector<std::pair<double, double>> scan(const std::vector<std::vector<Vec3>>& rings, double c, bool horizontal) {
    std::vector<double> xs;
    for (const auto& r : rings)
        for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
            const Vec3 &a = r[j], &b = r[i];
            const double ya = horizontal ? a.y : a.x, yb = horizontal ? b.y : b.x;
            const double xa = horizontal ? a.x : a.y, xb = horizontal ? b.x : b.y;
            if ((ya > c) != (yb > c)) xs.push_back(xa + (xb - xa) * (c - ya) / (yb - ya));
        }
    std::sort(xs.begin(), xs.end());
    std::vector<std::pair<double, double>> out;
    for (std::size_t i = 0; i + 1 < xs.size(); i += 2) out.push_back({xs[i], xs[i + 1]});
    return out;
}

std::vector<Vec3> insetRect(const std::vector<Vec3>& o, double d) {
    // to'rtburchak (4 uch) uchun: markazga qarab d ga ichkariga
    Vec3 c;
    for (const auto& p : o) c = c + p;
    c = c / static_cast<double>(o.size());
    std::vector<Vec3> out;
    const std::size_t n = o.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& p = o[i];
        const Vec3& a = o[(i + n - 1) % n];
        const Vec3& b = o[(i + 1) % n];
        auto nrm = [](Vec3 u) { const double l = u.length2d(); return l < 1e-12 ? Vec3{} : Vec3{u.x / l, u.y / l, 0}; };
        const Vec3 e1 = nrm(p - a), e2 = nrm(b - p);
        // ichkari normal (CCW kontur — chap tomon)
        const Vec3 n1{-e1.y, e1.x, 0}, n2{-e2.y, e2.x, 0};
        Vec3 bis = n1 + n2;
        const double bl = bis.length2d();
        if (bl < 1e-9) { out.push_back(p); continue; }
        bis = bis / bl;
        const double cosHalf = std::max(0.2, bis.dot(n1));
        out.push_back(p + bis * (d / cosHalf));
    }
    (void)c;
    return out;
}

double signedArea(const std::vector<Vec3>& v) {
    double a = 0;
    for (std::size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) a += (v[j].x * v[i].y - v[i].x * v[j].y);
    return a / 2;
}

const char* kRef = "SP 63.13330 / ShNQ 2.03.01-24";

} // namespace

const char* roleKey(BarRole r) {
    switch (r) {
        case BarRole::Main: return "MAIN";
        case BarRole::Stirrup: return "STIR";
        case BarRole::BottomX: return "BX";
        case BarRole::BottomY: return "BY";
        case BarRole::TopX: return "TX";
        case BarRole::TopY: return "TY";
        case BarRole::Vertical: return "VERT";
        case BarRole::Horizontal: return "HOR";
        case BarRole::Tie: return "TIE";
        case BarRole::Chair: return "CHAIR";
    }
    return "MAIN";
}

double barMassPerM(double d) { return kPI * d * d / 4 * 7850.0; }

std::vector<RebarTemplate> builtinRebarTemplates() {
    std::vector<RebarTemplate> v;
    auto mk = [&](const char* id, ElemKind k) -> RebarTemplate& {
        RebarTemplate t;
        t.id = id;
        t.name = std::string("rtpl.") + id;
        t.kind = k;
        v.push_back(t);
        return v.back();
    };
    // fundament plitasi
    {
        auto& t = mk("found_16_200", ElemKind::FoundationSlab);
        t.cover = 0.040; t.d1 = t.d2 = t.d3 = t.d4 = 0.016; t.s1 = t.s2 = t.s3 = t.s4 = 0.2; t.chairsPerM2 = 1.0;
    }
    {
        auto& t = mk("found_20_200", ElemKind::FoundationSlab);
        t.cover = 0.050; t.d1 = t.d2 = t.d3 = t.d4 = 0.020; t.s1 = t.s2 = t.s3 = t.s4 = 0.2; t.chairsPerM2 = 1.2;
    }
    {
        auto& t = mk("strip_14", ElemKind::StripFooting);
        t.cover = 0.040; t.d1 = 0.014; t.s1 = 0.15; t.d2 = 0.010; t.s2 = 0.3; t.d3 = 0.014; t.s3 = 0.15; t.d4 = 0.010; t.s4 = 0.3;
    }
    // ustunlar
    {
        auto& t = mk("col_4x16", ElemKind::Column);
        t.nMain = 4; t.d1 = 0.016; t.dStir = 0.008; t.sStir = 0.2; t.sStirEnd = 0.1; t.cover = 0.030;
    }
    {
        auto& t = mk("col_8x20", ElemKind::Column);
        t.nMain = 8; t.d1 = 0.020; t.dStir = 0.008; t.sStir = 0.25; t.sStirEnd = 0.1; t.cover = 0.035;
    }
    {
        auto& t = mk("col_12x25", ElemKind::Column);
        t.nMain = 12; t.d1 = 0.025; t.dStir = 0.010; t.sStir = 0.3; t.sStirEnd = 0.1; t.cover = 0.040;
    }
    // devorlar
    {
        auto& t = mk("wall_12_200", ElemKind::Wall);
        t.d1 = 0.012; t.s1 = 0.2; t.d2 = 0.010; t.s2 = 0.2; t.cover = 0.025; t.topMesh = true; t.tiesPerM2 = 4;
    }
    {
        auto& t = mk("wall_16_150", ElemKind::Wall);
        t.d1 = 0.016; t.s1 = 0.15; t.d2 = 0.012; t.s2 = 0.2; t.cover = 0.030; t.topMesh = true; t.tiesPerM2 = 6;
    }
    // plitalar
    {
        auto& t = mk("slab_12_200", ElemKind::Slab);
        t.d1 = t.d2 = t.d3 = t.d4 = 0.012; t.s1 = t.s2 = t.s3 = t.s4 = 0.2; t.cover = 0.020;
    }
    {
        auto& t = mk("slab_10_200", ElemKind::Slab);
        t.d1 = t.d2 = 0.010; t.s1 = t.s2 = 0.2; t.d3 = t.d4 = 0.010; t.s3 = t.s4 = 0.2; t.cover = 0.020;
    }
    {
        auto& t = mk("slab_14_150", ElemKind::Slab);
        t.d1 = t.d2 = 0.014; t.s1 = t.s2 = 0.15; t.d3 = t.d4 = 0.012; t.s3 = t.s4 = 0.2; t.cover = 0.025;
    }
    {
        auto& t = mk("roof_12_200", ElemKind::Roof);
        t.d1 = t.d2 = t.d3 = t.d4 = 0.012; t.s1 = t.s2 = t.s3 = t.s4 = 0.2; t.cover = 0.025;
    }
    return v;
}

RebarSettings defaultRebarSettings() {
    RebarSettings rs;
    for (const auto& t : builtinRebarTemplates())
        if (!rs.byKind.count(t.kind)) rs.byKind[t.kind] = t;
    return rs;
}

RebarModel buildRebar(const BuildingModel& bm, const BuildingParams& bp, const RebarSettings& rs) {
    RebarModel m;
    auto issue = [&](bool e, const char* k, std::vector<std::string> a) { m.issues.push_back({e, k, std::move(a)}); };
    auto stockLaps = [&](double L, double d, double lapD) {
        const int n = L > rs.stockLength ? static_cast<int>(std::ceil(L / rs.stockLength)) - 1 : 0;
        return n * lapD * d;
    };
    auto addBar = [&](ElemKind k, int lv, BarRole role, double d, const std::string& cls, std::vector<Vec3> pts, double extra, bool closed,
                      bool draw = true) {
        Bar b{k, lv, role, d, cls, std::move(pts), 0};
        b.length = len3(b.pts, closed) + extra;
        (void)draw;
        m.bars.push_back(b);
    };

    // ---- normativ tekshiruv (bir marta har tur uchun)
    for (const auto& [k, t] : rs.byKind) {
        const std::string name = kindKey(k);
        if ((k == ElemKind::Slab || k == ElemKind::Roof || k == ElemKind::Wall) && t.cover < 0.020 - 1e-9)
            issue(true, "rebar.chk.cover", {name, std::format("{:.0f}", t.cover * 1000), "20"});
        if (k == ElemKind::Column && t.cover < 0.025 - 1e-9) issue(true, "rebar.chk.cover", {name, std::format("{:.0f}", t.cover * 1000), "25"});
        if ((k == ElemKind::FoundationSlab || k == ElemKind::StripFooting) && t.cover < 0.035 - 1e-9)
            issue(true, "rebar.chk.cover", {name, std::format("{:.0f}", t.cover * 1000), "35"});
        if (k == ElemKind::Column) {
            if (t.d1 < 0.012 - 1e-9) issue(true, "rebar.chk.col_d", {std::format("{:.0f}", t.d1 * 1000)});
            const double smax = std::min(15 * t.d1, 0.5);
            if (t.sStir > smax + 1e-9) issue(true, "rebar.chk.stir_step", {std::format("{:.0f}", t.sStir * 1000), std::format("{:.0f}", smax * 1000)});
            const double dmin = std::max(0.006, t.d1 / 4);
            if (t.dStir < dmin - 1e-9) issue(true, "rebar.chk.stir_d", {std::format("{:.0f}", t.dStir * 1000), std::format("{:.0f}", dmin * 1000)});
            if (t.nMain < 4) issue(true, "rebar.chk.col_n", {std::to_string(t.nMain)});
        }
        if (k == ElemKind::Slab || k == ElemKind::Roof) {
            const double h = k == ElemKind::Roof ? bp.roofT : bp.slabT;
            const double smax = h <= 0.15 ? 0.2 : std::min(1.5 * h, 0.4);
            for (double s : {t.s1, t.s2})
                if (s > smax + 1e-9) issue(true, "rebar.chk.slab_step", {name, std::format("{:.0f}", s * 1000), std::format("{:.0f}", smax * 1000)});
        }
        if (k == ElemKind::Wall && t.s1 > 0.4 + 1e-9) issue(false, "rebar.chk.wall_step", {std::format("{:.0f}", t.s1 * 1000)});
    }

    std::map<int, double> slabTop;  // qavat -> plita qalinligi (nahlyostka hisobida)
    for (const auto& pr : bm.prisms) {
        auto it = rs.byKind.find(pr.kind == ElemKind::Roof && !rs.byKind.count(ElemKind::Roof) ? ElemKind::Slab : pr.kind);
        if (it == rs.byKind.end()) continue;
        const RebarTemplate& t = it->second;
        const int lv = pr.level;
        const bool topFloor = lv == bp.floors;

        if (pr.kind == ElemKind::Column) {
            const auto& o = pr.outline;
            const double inset = t.cover + t.dStir + t.d1 / 2;
            std::vector<Vec3> pos;
            if (o.size() == 4) {
                const auto r = insetRect(o, inset);
                pos = r;  // burchaklar
                const int extra = std::max(0, t.nMain - 4);
                // qolganlarini tomon uzunligiga mutanosib taqsimlash
                double per[4], total = 0;
                for (int i = 0; i < 4; ++i) total += per[i] = r[static_cast<std::size_t>(i)].distanceTo(r[static_cast<std::size_t>((i + 1) % 4)]);
                int left = extra;
                for (int i = 0; i < 4 && left > 0; ++i) {
                    int ni = static_cast<int>(std::round(extra * per[i] / total));
                    if (i == 3) ni = left;
                    ni = std::min(ni, left);
                    left -= ni;
                    const Vec3 a = r[static_cast<std::size_t>(i)], b = r[static_cast<std::size_t>((i + 1) % 4)];
                    for (int k = 1; k <= ni; ++k) pos.push_back(a + (b - a) * (static_cast<double>(k) / (ni + 1)));
                }
            } else {  // aylana (ko'pburchak)
                Vec3 c;
                for (const auto& p : o) c = c + p;
                c = c / static_cast<double>(o.size());
                const double R = std::sqrt(std::abs(signedArea(o)) / kPI) - inset;
                for (int k = 0; k < t.nMain; ++k) {
                    const double a = 2 * kPI * k / t.nMain;
                    pos.push_back(c + Vec3{R * std::cos(a), R * std::sin(a), 0});
                }
            }
            // bo'ylama: 1-qavat fundament ichidan, boshqalar — plita ustidan; yuqoriga nahlyostka (tomdan tashqari)
            const double zs = lv == 1 ? pr.z0 - (bp.foundT - 0.1) : pr.z0;
            const double lap = topFloor ? 0.0 : t.lapD * t.d1;
            const double ze = topFloor ? pr.z1 + bp.roofT - t.cover : pr.z1 + bp.slabT + lap;
            const double foot = lv == 1 ? 0.25 : 0.0;  // fundamentdagi gorizontal tovon
            for (const auto& p : pos)
                addBar(pr.kind, lv, BarRole::Main, t.d1, t.clsMain, {{p.x, p.y, zs}, {p.x, p.y, ze}}, foot + stockLaps(ze - zs, t.d1, t.lapD), false);
            // xomutlar
            if (o.size() >= 3) {
                const auto ring = insetRect(o, t.cover + t.dStir / 2);
                const double H = pr.z1 - pr.z0;
                double side = 0;
                for (std::size_t i = 0; i < o.size(); ++i) side = std::max(side, o[i].distanceTo(o[(i + 1) % o.size()]));
                const double dense = std::min(H / 2, std::max({side, 0.5, H / 6}));
                std::vector<double> zz;
                for (double z = pr.z0 + 0.05; z <= pr.z0 + dense; z += t.sStirEnd) zz.push_back(z);
                for (double z = (zz.empty() ? pr.z0 : zz.back()) + t.sStir; z < pr.z1 - dense; z += t.sStir) zz.push_back(z);
                for (double z = pr.z1 - dense; z <= pr.z1 - 0.05 + 1e-9; z += t.sStirEnd)
                    if (zz.empty() || z > zz.back() + 1e-6) zz.push_back(z);
                const double hooks = 2 * std::max(0.075, 6 * t.dStir);  // 135° ilgaklar
                for (double z : zz) {
                    std::vector<Vec3> rr;
                    for (const auto& q : ring) rr.push_back({q.x, q.y, z});
                    addBar(pr.kind, lv, BarRole::Stirrup, t.dStir, t.clsStir, rr, hooks, true);
                }
                m.nodes += static_cast<long>(zz.size() * pos.size());
            }
            const double as = pos.size() * kPI * t.d1 * t.d1 / 4, ac = std::abs(signedArea(o));
            if (ac > 0 && as / ac < 0.001 && lv == 1) issue(false, "rebar.chk.mu", {std::format("{:.2f}", as / ac * 100), "0.10"});
        } else if (pr.kind == ElemKind::Wall) {
            const auto& o = pr.outline;
            if (o.size() != 4) continue;
            // uzun o'q
            std::size_t e = 0;
            for (std::size_t i = 1; i < 4; ++i)
                if (o[i].distanceTo(o[(i + 1) % 4]) > o[e].distanceTo(o[(e + 1) % 4])) e = i;
            const Vec3 a = o[e], b = o[(e + 1) % 4], c2 = o[(e + 2) % 4];
            const double L = a.distanceTo(b), T = b.distanceTo(c2);
            const Vec3 u = (b - a) / L, nIn = (c2 - b) / T;  // a-b yuzidan ichkariga
            std::vector<double> faces;
            if (t.topMesh && T > 2 * (t.cover + t.d1 + t.d2) + 0.02) faces = {t.cover + t.d2 + t.d1 / 2, T - (t.cover + t.d2 + t.d1 / 2)};
            else faces = {T / 2};
            const double lap = topFloor ? 0.0 : t.lapD * t.d1;
            const double zs = lv == 1 ? pr.z0 - (bp.foundT - 0.1) : pr.z0;
            const double ze = topFloor ? pr.z1 + bp.roofT - t.cover : pr.z1 + bp.slabT + lap;
            int nv = 0, nh = 0;
            for (double f : faces) {
                for (double x = t.cover + t.d1 / 2; x <= L - t.cover + 1e-9; x += t.s1, ++nv) {
                    const Vec3 p = a + u * x + nIn * f;
                    addBar(pr.kind, lv, BarRole::Vertical, t.d1, t.clsMain, {{p.x, p.y, zs}, {p.x, p.y, ze}},
                           (lv == 1 ? 0.25 : 0.0) + stockLaps(ze - zs, t.d1, t.lapD), false);
                }
                const double fh = f < T / 2 ? f - (t.d1 + t.d2) / 2 : f + (t.d1 + t.d2) / 2;
                for (double z = pr.z0 + 0.05; z <= pr.z1 - 0.05 + 1e-9; z += t.s2, ++nh) {
                    const Vec3 p0 = a + u * t.cover + nIn * fh, p1 = a + u * (L - t.cover) + nIn * fh;
                    addBar(pr.kind, lv, BarRole::Horizontal, t.d2, t.clsMain, {{p0.x, p0.y, z}, {p1.x, p1.y, z}},
                           stockLaps(L, t.d2, t.lapD), false);
                }
            }
            m.nodes += static_cast<long>(nv / static_cast<int>(faces.size())) * (nh / static_cast<int>(faces.size())) * static_cast<long>(faces.size());
            const int ties = static_cast<int>(std::ceil(L * (pr.z1 - pr.z0) * t.tiesPerM2));
            if (faces.size() == 2 && ties > 0) {
                m.ties += ties;
                for (int k = 0; k < ties; ++k)
                    m.bars.push_back({pr.kind, lv, BarRole::Tie, 0.006, t.clsStir, {}, T - 2 * t.cover + 0.1});
            }
        } else if (pr.kind == ElemKind::Slab || pr.kind == ElemKind::Roof || pr.kind == ElemKind::FoundationSlab ||
                   pr.kind == ElemKind::StripFooting) {
            std::vector<std::vector<Vec3>> rings{pr.outline};
            rings.insert(rings.end(), pr.holes.begin(), pr.holes.end());
            BBox bb;
            for (const auto& p : pr.outline) bb.expand(p);
            auto mesh = [&](double d, double s, bool xDir, double z, BarRole role) {
                int bars = 0;
                const double lo = (xDir ? bb.min.y : bb.min.x) + t.cover + d / 2, hi = (xDir ? bb.max.y : bb.max.x) - t.cover - d / 2;
                for (double c = lo; c <= hi + 1e-9; c += s) {
                    for (auto [x0, x1] : scan(rings, c, xDir)) {
                        x0 += t.cover;
                        x1 -= t.cover;
                        if (x1 - x0 < 0.1) continue;
                        const Vec3 p0 = xDir ? Vec3{x0, c, z} : Vec3{c, x0, z}, p1 = xDir ? Vec3{x1, c, z} : Vec3{c, x1, z};
                        addBar(pr.kind, lv, role, d, t.clsMain, {p0, p1}, stockLaps(x1 - x0, d, t.lapD), false);
                        ++bars;
                    }
                }
                return bars;
            };
            const double zb = pr.z0 + t.cover + t.d1 / 2;
            mesh(t.d1, t.s1, true, zb, BarRole::BottomX);
            mesh(t.d2, t.s2, false, zb + (t.d1 + t.d2) / 2, BarRole::BottomY);
            const double area = pr.area();
            m.nodes += static_cast<long>(area / (t.s1 * t.s2));
            if (t.topMesh && pr.z1 - pr.z0 > 2 * t.cover + t.d1 + t.d2 + t.d3 + t.d4 + 0.02) {
                const double zt = pr.z1 - t.cover - t.d3 / 2;
                mesh(t.d3, t.s3, true, zt, BarRole::TopX);
                mesh(t.d4, t.s4, false, zt - (t.d3 + t.d4) / 2, BarRole::TopY);
                m.nodes += static_cast<long>(area / (t.s3 * t.s4));
                const int ch = static_cast<int>(std::ceil(area * t.chairsPerM2));
                m.chairs += ch;
                const double chairLen = 2 * (pr.z1 - pr.z0 - 2 * t.cover) + 0.4;
                for (int k = 0; k < ch; ++k) m.bars.push_back({pr.kind, lv, BarRole::Chair, 0.010, t.clsMain, {}, chairLen});
            }
            slabTop[lv] = pr.z1 - pr.z0;
        }
    }

    // ---- ведомость
    std::map<std::tuple<int, int, int, long, std::string, long>, ScheduleRow> rows;
    for (const auto& b : m.bars) {
        const long dl = std::lround(b.d * 1000), ll = std::lround(b.length * 100);
        auto& r = rows[{static_cast<int>(b.kind), b.level, static_cast<int>(b.role), dl, b.cls, ll}];
        r.kind = b.kind;
        r.level = b.level;
        r.role = b.role;
        r.d = b.d;
        r.cls = b.cls;
        r.length = ll / 100.0;
        r.count += 1;
        r.totalLength += b.length;
        r.mass += b.length * barMassPerM(b.d);
    }
    int pos = 1;
    for (auto& [k, r] : rows) {
        r.pos = pos++;
        m.schedule.push_back(r);
        m.totalMass += r.mass;
        m.massByDia[std::format("\xC3\x98{} {}", std::lround(r.d * 1000), r.cls)] += r.mass;
    }
    m.wireKg = m.nodes * rs.wirePerNode * rs.wireKgPerM;
    // 3D uchun shpilka/stullarni olib tashlash
    m.bars.erase(std::remove_if(m.bars.begin(), m.bars.end(), [](const Bar& b) { return b.pts.empty(); }), m.bars.end());
    (void)kRef;
    return m;
}

// ---------------------------------------------------------------- JSON
namespace {
std::string tplJson(const RebarTemplate& t) {
    return std::format("{{\"id\":\"{}\",\"name\":\"{}\",\"kind\":\"{}\",\"cover\":{},\"lapD\":{},\"clsMain\":\"{}\",\"clsStir\":\"{}\","
                       "\"d1\":{},\"s1\":{},\"d2\":{},\"s2\":{},\"top\":{},\"d3\":{},\"s3\":{},\"d4\":{},\"s4\":{},\"chairs\":{},\"ties\":{},"
                       "\"n\":{},\"dStir\":{},\"sStir\":{},\"sStirEnd\":{}}}",
                       t.id, t.name, kindKey(t.kind), t.cover, t.lapD, t.clsMain, t.clsStir, t.d1, t.s1, t.d2, t.s2, t.topMesh ? "true" : "false",
                       t.d3, t.s3, t.d4, t.s4, t.chairsPerM2, t.tiesPerM2, t.nMain, t.dStir, t.sStir, t.sStirEnd);
}
RebarTemplate tplFrom(const json::Value& v) {
    RebarTemplate t;
    auto num = [&](const char* k, double d) {
        const json::Value* x = v.find(k);
        return x && x->isNumber() ? x->num() : d;
    };
    auto str = [&](const char* k, const std::string& d) {
        const json::Value* x = v.find(k);
        return x && x->isString() ? x->str() : d;
    };
    t.id = str("id", "");
    t.name = str("name", t.id);
    kindFromKey(str("kind", "SLAB"), t.kind);
    t.cover = num("cover", t.cover);
    t.lapD = num("lapD", t.lapD);
    t.clsMain = str("clsMain", t.clsMain);
    t.clsStir = str("clsStir", t.clsStir);
    t.d1 = num("d1", t.d1), t.s1 = num("s1", t.s1), t.d2 = num("d2", t.d2), t.s2 = num("s2", t.s2);
    if (const json::Value* x = v.find("top"); x && x->isBool()) t.topMesh = x->boolean();
    t.d3 = num("d3", t.d3), t.s3 = num("s3", t.s3), t.d4 = num("d4", t.d4), t.s4 = num("s4", t.s4);
    t.chairsPerM2 = num("chairs", t.chairsPerM2);
    t.tiesPerM2 = num("ties", t.tiesPerM2);
    t.nMain = static_cast<int>(num("n", t.nMain));
    t.dStir = num("dStir", t.dStir), t.sStir = num("sStir", t.sStir), t.sStirEnd = num("sStirEnd", t.sStirEnd);
    // xavfsiz chegaralar (nol qadam cheksiz siklga olib kelmasin)
    for (double* s : {&t.s1, &t.s2, &t.s3, &t.s4, &t.sStir, &t.sStirEnd}) *s = std::max(*s, 0.03);
    return t;
}
} // namespace

std::string templatesToJson(const std::vector<RebarTemplate>& ts) {
    std::string s = "[\n";
    for (std::size_t i = 0; i < ts.size(); ++i) s += "  " + tplJson(ts[i]) + (i + 1 < ts.size() ? ",\n" : "\n");
    return s + "]\n";
}

std::vector<RebarTemplate> templatesFromJson(const std::string& text) {
    std::vector<RebarTemplate> out;
    const json::Value root = json::parse(text);
    if (!root.isArray()) return out;
    for (const auto& v : root.arr())
        if (v.isObject()) {
            auto t = tplFrom(v);
            t.user = true;
            if (!t.id.empty()) out.push_back(t);
        }
    return out;
}

std::string rebarToJson(const RebarSettings& rs) {
    std::string s = std::format("{{\"stock\":{},\"wirePerNode\":{},\"templates\":[", rs.stockLength, rs.wirePerNode);
    bool first = true;
    for (const auto& [k, t] : rs.byKind) {
        s += (first ? "" : ",") + tplJson(t);
        first = false;
    }
    return s + "]}";
}

void rebarFromJson(const std::string& text, RebarSettings& rs) {
    json::Value root;
    try {
        root = json::parse(text);
    } catch (const json::ParseError& e) {
        throw Error("bim.err.format", {e.what()});
    }
    const json::Value* r = root.isObject() && root.find("rebar") ? root.find("rebar") : &root;
    if (!r->isObject()) return;
    if (const json::Value* x = r->find("stock"); x && x->isNumber()) rs.stockLength = std::max(1.0, x->num());
    if (const json::Value* x = r->find("wirePerNode"); x && x->isNumber()) rs.wirePerNode = x->num();
    if (const json::Value* ts = r->find("templates"); ts && ts->isArray())
        for (const auto& v : ts->arr())
            if (v.isObject()) {
                auto t = tplFrom(v);
                rs.byKind[t.kind] = t;
            }
}

std::vector<std::unique_ptr<IEntity>> drawSchedule(const RebarModel& m, const Vec3& o, double h, const ScheduleLabels& L) {
    std::vector<std::unique_ptr<IEntity>> out;
    const double rh = h * 2.4;
    const double cw[7] = {h * 4, h * 22, h * 5, h * 7, h * 7, h * 6, h * 8};
    double W = 0;
    for (double c : cw) W += c;
    auto put = [&](std::unique_ptr<IEntity> e) {
        e->setLayer("ARM_VEDOMOST");
        out.push_back(std::move(e));
    };
    auto line = [&](double x0, double y0, double x1, double y1) { put(std::make_unique<LineEntity>(Vec3{x0, y0, 0}, Vec3{x1, y1, 0})); };
    auto cell = [&](int col, double y, const std::string& s, bool right) {
        double x = o.x;
        for (int i = 0; i < col; ++i) x += cw[i];
        const Vec3 at = right ? Vec3{x + cw[col] - h * 0.5, y - rh / 2, 0} : Vec3{x + h * 0.5, y - rh / 2, 0};
        put(std::make_unique<TextEntity>(at, h, s, 0, right ? TextHAlign::Right : TextHAlign::Left, TextVAlign::Middle));
    };
    double y = o.y;
    put(std::make_unique<TextEntity>(Vec3{o.x, y + h * 0.8, 0}, h * 1.3, L.title));
    line(o.x, y, o.x + W, y);
    const std::string hdr[7] = {L.pos, L.element, L.d, L.cls, L.length, L.count, L.mass};
    for (int c = 0; c < 7; ++c) cell(c, y, hdr[c], c >= 4);
    y -= rh;
    line(o.x, y, o.x + W, y);
    for (const auto& r : m.schedule) {
        cell(0, y, std::to_string(r.pos), false);
        cell(1, y, (L.levelName ? L.levelName(r.level) : std::to_string(r.level)) + " · " + (L.kindName ? L.kindName(r.kind) : kindKey(r.kind)) +
                       " · " + (L.roleName ? L.roleName(r.role) : roleKey(r.role)), false);
        cell(2, y, std::format("\xC3\x98{}", std::lround(r.d * 1000)), false);
        cell(3, y, r.cls, false);
        cell(4, y, std::format("{:.2f}", r.length), true);
        cell(5, y, std::to_string(r.count), true);
        cell(6, y, std::format("{:.1f}", r.mass), true);
        y -= rh;
        line(o.x, y, o.x + W, y);
    }
    cell(1, y, L.total, false);
    cell(6, y, std::format("{:.1f}", m.totalMass), true);
    y -= rh;
    line(o.x, y, o.x + W, y);
    cell(1, y, L.wire, false);
    cell(6, y, std::format("{:.1f}", m.wireKg), true);
    y -= rh;
    line(o.x, y, o.x + W, y);
    double x = o.x;
    for (int c = 0; c <= 7; ++c) {
        line(x, o.y, x, y);
        if (c < 7) x += cw[c];
    }
    return out;
}

} // namespace asiacad::bim
