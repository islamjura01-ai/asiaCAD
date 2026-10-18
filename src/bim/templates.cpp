#include <asiacad/bim/templates.h>

#include <asiacad/bim/steel.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/json.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace asiacad::bim {

namespace {
constexpr double kPI = 3.14159265358979323846;

struct Out {
    Entities& es;
    void put(std::unique_ptr<IEntity> e, const std::string& layer) {
        e->setLayer(layer);
        es.push_back(std::move(e));
    }
    void line(const Vec3& a, const Vec3& b, const std::string& l) { put(std::make_unique<LineEntity>(a, b), l); }
    void poly(std::vector<Vec3> v, bool closed, const std::string& l) { put(std::make_unique<PolylineEntity>(std::move(v), closed), l); }
    // gorizontal to'rtburchak prizma: pastki va ustki yopiq konturlar (Haydash sahnasida — jism)
    void boxXY(double x0, double y0, double x1, double y1, double z0, double z1, const std::string& l) {
        poly({{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}}, true, l);
        poly({{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}, true, l);
    }
    // 3D yuza (yassi to'rtburchak / ko'pburchak)
    void face(std::vector<Vec3> v, const std::string& l) { poly(std::move(v), true, l); }
    void text(const Vec3& p, double h, const std::string& s, const std::string& l, double rot = 0, TextHAlign ha = TextHAlign::Left) {
        put(std::make_unique<TextEntity>(p, h, s, rot, ha, TextVAlign::Bottom), l);
    }
};

std::string fz(double z) {  // belgi: +3.300 / -1.250 (vergul bilan)
    std::string s = std::format("{:+.3f}", z);
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}
double steelMass(const std::string& prof, double L) {
    const auto p = findSteel(prof);
    return p ? p->mass * L / 1000 : 0;
}
void placeAll(Entities& es, const Placement& at) {
    const Transform t = at.transform();
    for (auto& e : es) e->transform(t);
}
json::Value parseOr(const std::string& t) {
    try {
        return json::parse(t);
    } catch (const json::ParseError& e) {
        throw Error("gen.err.format", {e.what()});
    }
}
double num(const json::Value& o, const char* k, double d) {
    const json::Value* v = o.find(k);
    return v && v->isNumber() ? v->num() : d;
}
std::string str(const json::Value& o, const char* k, const std::string& d) {
    const json::Value* v = o.find(k);
    return v && v->isString() ? v->str() : d;
}
bool boolean(const json::Value& o, const char* k, bool d) {
    const json::Value* v = o.find(k);
    return v && v->isBool() ? v->boolean() : d;
}
std::string atJson(const Placement& a) { return std::format("\"x\":{},\"y\":{},\"rot\":{},\"z0\":{}", a.x, a.y, a.rotDeg, a.z0); }
Placement atFrom(const json::Value& r) { return {num(r, "x", 0), num(r, "y", 0), num(r, "rot", 0), num(r, "z0", 0)}; }
std::string listJ(const std::vector<double>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) s += std::format("{}{}", i ? "," : "", v[i]);
    return s + "]";
}
std::vector<double> listFrom(const json::Value& r, const char* k, std::vector<double> d) {
    const json::Value* v = r.find(k);
    if (!v || !v->isArray()) return d;
    std::vector<double> o;
    for (const auto& x : v->arr())
        if (x.isNumber()) o.push_back(x.num());
    return o.empty() ? d : o;
}
}  // namespace

Transform Placement::transform() const {
    return Transform::translation({x, y, z0}) * Transform::rotation({0, 0, 0}, rotDeg * kPI / 180);
}

// ---------------------------------------------------------------- metall karkas
TplResult makeSteelFrame(const SteelFrameSpec& s) {
    TplResult r;
    Out o{r.entities};
    for (const auto& n : {s.column, s.beam, s.beam2, s.rafter, s.purlin, s.brace})
        if (!findSteel(n)) throw Error("km.err.unknown", {n});
    std::map<std::string, double> len;
    auto member = [&](const std::string& prof, const Vec3& a, const Vec3& b) {
        o.line(a, b, "KM_" + findSteel(prof)->name);
        len[prof] += a.distanceTo(b);
    };
    if (s.type == 0) {
        if (s.dx.empty() || s.dy.empty() || s.floors < 1) throw Error("gen.err.grid_empty", {});
        std::vector<double> X{0}, Y{0};
        for (double d : s.dx) X.push_back(X.back() + d);
        for (double d : s.dy) Y.push_back(Y.back() + d);
        const double Ht = s.floors * s.floorH;
        for (double x : X)
            for (double y : Y) member(s.column, {x, y, 0}, {x, y, Ht});
        for (int f = 1; f <= s.floors; ++f) {
            const double z = f * s.floorH;
            for (double y : Y)
                for (std::size_t i = 0; i + 1 < X.size(); ++i) member(s.beam, {X[i], y, z}, {X[i + 1], y, z});
            for (double x : X)
                for (std::size_t j = 0; j + 1 < Y.size(); ++j) member(s.beam2, {x, Y[j], z}, {x, Y[j + 1], z});
            if (s.bracing)  // tik bog'lamalar: chetki Y qatorlarida birinchi X oralig'ida, har qavatda
                for (double y : {Y.front(), Y.back()}) {
                    member(s.brace, {X[0], y, z - s.floorH}, {X[1], y, z});
                    member(s.brace, {X[1], y, z - s.floorH}, {X[0], y, z});
                }
        }
        r.info.push_back({"tpl.i.size", std::format("{:g} × {:g} m, {} × {:g} m", X.back(), Y.back(), s.floors, s.floorH)});
    } else {
        if (s.bays < 1 || s.span <= 0 || s.ridge < s.eave) throw Error("tpl.err.portal", {});
        const double Lx = s.bays * s.bay;
        auto zRoof = [&](double y) { return y <= s.span / 2 ? s.eave + (s.ridge - s.eave) * y / (s.span / 2) : s.eave + (s.ridge - s.eave) * (s.span - y) / (s.span / 2); };
        for (int i = 0; i <= s.bays; ++i) {
            const double x = i * s.bay;
            member(s.column, {x, 0, 0}, {x, 0, s.eave});
            member(s.column, {x, s.span, 0}, {x, s.span, s.eave});
            member(s.rafter, {x, 0, s.eave}, {x, s.span / 2, s.ridge});
            member(s.rafter, {x, s.span / 2, s.ridge}, {x, s.span, s.eave});
        }
        // progonlar: har ikki qiyalikda (gorizontal proyeksiya qadami), karniz va tizma bo'ylab
        std::vector<double> ys;
        const int n = std::max(1, static_cast<int>(std::ceil(s.span / 2 / std::max(0.3, s.purlinStep))));
        for (int k = 0; k <= n; ++k) {
            const double y = s.span / 2 * k / n;
            ys.push_back(y);
            if (k < n) ys.push_back(s.span - y);
        }
        for (double y : ys) member(s.purlin, {0, y, zRoof(y)}, {Lx, y, zRoof(y)});
        if (s.bracing && s.bays >= 2)
            for (int i : {0, s.bays - 1}) {
                const double x0 = i * s.bay, x1 = x0 + s.bay;
                for (double y : {0.0, s.span}) {  // tik bog'lamalar (devorlar tekisligida)
                    member(s.brace, {x0, y, 0}, {x1, y, s.eave});
                    member(s.brace, {x1, y, 0}, {x0, y, s.eave});
                }
                for (auto [ya, yb] : {std::pair{0.0, s.span / 2}, {s.span / 2, s.span}}) {  // tom bog'lamalari
                    member(s.brace, {x0, ya, zRoof(ya)}, {x1, yb, zRoof(yb)});
                    member(s.brace, {x1, ya, zRoof(ya)}, {x0, yb, zRoof(yb)});
                }
            }
        const double slope = std::atan2(s.ridge - s.eave, s.span / 2) * 180 / kPI;
        r.info.push_back({"tpl.i.size", std::format("{:g} × {:g} m, h={:g}…{:g} m, {:.1f}°", Lx, s.span, s.eave, s.ridge, slope)});
        if (slope < 3) r.issues.push_back({false, "tpl.chk.roof_flat", {std::format("{:.1f}", slope)}});
    }
    for (const auto& [p, L] : len) {
        r.steel += steelMass(p, L);
        r.info.push_back({"tpl.i.member", std::format("{}: {:.1f} m, {:.2f} t", findSteel(p)->name, L, steelMass(p, L))});
    }
    r.info.push_back({"tpl.i.steel", std::format("{:.2f} t", r.steel)});
    placeAll(r.entities, s.at);
    return r;
}

// ---------------------------------------------------------------- zina
TplResult makeStair(const StairSpec& s) {
    TplResult r;
    Out o{r.entities};
    if (s.H <= 0.3 || s.width <= 0.3 || s.tread <= 0.1 || s.riserMax <= 0.05) throw Error("tpl.err.stair", {});
    int n = static_cast<int>(std::ceil(s.H / s.riserMax - 1e-9));
    if (s.type == 0 && n % 2) ++n;
    n = std::max(n, s.type == 0 ? 4 : 2);
    const double h = s.H / n, b = s.tread, w = s.width;
    const double teff = s.waist * std::sqrt(b * b + h * h) / b;
    const int perFlight = s.type == 0 ? n / 2 : n;
    const double flightL = (perFlight - 1) * b;
    const std::string L3 = s.steel ? "ZINA_3D_METALL" : "ZINA_3D", LP = "ZINA_PLAN";
    double concrete = 0, stringerL = 0;
    auto step = [&](double u0, double u1, double v0, double v1, double zt) {
        const double zb = s.steel ? zt - 0.04 : zt - h / 2 - teff;  // metall — 40 mm list (pog'ona)
        o.boxXY(std::min(u0, u1), v0, std::max(u0, u1), v1, zb, zt, L3);
        if (!s.steel) concrete += std::abs(u1 - u0) * (v1 - v0) * (zt - zb);
        o.line({u0, v0, zt}, {u0, v1, zt}, LP);  // plan: pog'ona qirrasi
    };
    auto slab = [&](double u0, double u1, double v0, double v1, double z, double t) {
        o.boxXY(u0, v0, u1, v1, z - t, z, "ZINA_3D");
        concrete += (u1 - u0) * (v1 - v0) * t;
    };
    auto arrow = [&](std::vector<Vec3> pts) {  // plan: yo'nalish strelkasi (yuqoriga)
        o.poly(pts, false, LP);
        const Vec3 a = pts[pts.size() - 2], c = pts.back();
        const Vec3 d = Vec3{c.x - a.x, c.y - a.y, 0} / std::max(1e-9, (c - a).length2d());
        const Vec3 nrm{-d.y, d.x, 0};
        o.poly({c - d * 0.25 + nrm * 0.1, c, c - d * 0.25 - nrm * 0.1}, false, LP);
    };
    auto stringers = [&](double u0, double u1, double v0, double v1, double z0, double z1) {
        if (!s.steel) return;
        for (double v : {v0 + 0.05, v1 - 0.05}) {
            o.line({u0, v, z0 - 0.15}, {u1, v, z1 - 0.15}, "KM_" + findSteel(s.stringer)->name);
            stringerL += std::hypot(u1 - u0, z1 - z0);
        }
    };
    if (s.steel && !findSteel(s.stringer)) throw Error("km.err.unknown", {s.stringer});
    if (s.type == 0) {
        const double W = 2 * w + s.gap;
        for (int i = 0; i + 1 < perFlight; ++i) step(i * b, (i + 1) * b, 0, w, (i + 1) * h);
        const double zmid = perFlight * h;
        stringers(0, flightL, 0, w, 0, zmid);
        slab(flightL, flightL + s.landing, 0, W, zmid, 0.2);
        for (int j = 0; j + 1 < perFlight; ++j) step(flightL - j * b, flightL - (j + 1) * b, w + s.gap, W, zmid + (j + 1) * h);
        stringers(flightL, 0, w + s.gap, W, zmid, s.H);
        slab(-s.landing, 0, 0, W, s.H, 0.2);   // qavat maydonchasi (yuqori; pastkisi — qavat poli)
        o.poly({{-s.landing, 0, 0}, {flightL + s.landing, 0, 0}, {flightL + s.landing, W, 0}, {-s.landing, W, 0}}, true, LP);
        arrow({{0.15, w / 2, 0}, {flightL + s.landing / 2, w / 2, 0}, {flightL + s.landing / 2, w + s.gap + w / 2, 0}, {0.15, w + s.gap + w / 2, 0}});
        r.info.push_back({"tpl.i.size", std::format("{:.2f} × {:.2f} m", flightL + 2 * s.landing, W)});
    } else {
        for (int i = 0; i + 1 < perFlight; ++i) step(i * b, (i + 1) * b, 0, w, (i + 1) * h);
        stringers(0, flightL, 0, w, 0, s.H);
        slab(-s.landing, 0, 0, w, 0, 0.2);
        slab(flightL, flightL + s.landing, 0, w, s.H, 0.2);
        o.poly({{-s.landing, 0, 0}, {flightL + s.landing, 0, 0}, {flightL + s.landing, w, 0}, {-s.landing, w, 0}}, true, LP);
        arrow({{0.15, w / 2, 0}, {flightL + 0.3, w / 2, 0}});
        r.info.push_back({"tpl.i.size", std::format("{:.2f} × {:.2f} m", flightL + 2 * s.landing, w)});
    }
    o.text({0.2, -0.45, 0}, 0.2, std::format("{}×{:.0f}×{:.0f}  ({:+.3f})", n, h * 1000, b * 1000, s.at.z0), LP);
    r.concrete = concrete;
    r.steel = s.steel ? steelMass(s.stringer, stringerL) + perFlight * (s.type == 0 ? 2 : 1) * b * w * 0.004 * 7.85 : 0;
    r.info.push_back({"tpl.i.steps", std::format("{} × h={:.0f} mm, b={:.0f} mm, 2h+b={:.0f} mm", n, h * 1000, b * 1000, (2 * h + b) * 1000)});
    r.info.push_back({"tpl.i.levels", fz(s.at.z0) + " … " + fz(s.at.z0 + s.H)});
    if (r.concrete > 0) r.info.push_back({"tpl.i.concrete", std::format("{:.2f} m³", r.concrete)});
    if (r.steel > 0) r.info.push_back({"tpl.i.steel", std::format("{:.3f} t", r.steel)});
    const double bl = 2 * h + b;
    if (bl < 0.60 - 1e-9 || bl > 0.65 + 1e-9) r.issues.push_back({false, "tpl.chk.blondel", {std::format("{:.0f}", bl * 1000)}});
    if (w < 1.05 - 1e-9) r.issues.push_back({w < 0.9, "tpl.chk.stair_width", {std::format("{:.2f}", w)}});
    if (perFlight > 18) r.issues.push_back({false, "tpl.chk.stair_steps", {std::to_string(perFlight)}});
    if (s.type == 0 && s.landing < w - 1e-9) r.issues.push_back({true, "tpl.chk.landing", {std::format("{:.2f}", s.landing), std::format("{:.2f}", w)}});
    placeAll(r.entities, s.at);
    return r;
}

// ---------------------------------------------------------------- rampa
TplResult makeRamp(const RampSpec& s) {
    TplResult r;
    Out o{r.entities};
    if (s.H <= 0 || s.width <= 0.5 || s.slope <= 0.5) throw Error("tpl.err.ramp", {});
    const double i = s.slope / 100;
    // profil: (u, z) sinish nuqtalari, z — yer (0) dan pastga
    std::vector<std::pair<double, double>> pr{{0, 0}};
    if (s.kind == 0) {
        double Lt = s.trans, Lm = (s.H - i * Lt) / i;
        if (Lm < 0) { Lm = 0; Lt = s.H / i; }
        pr.push_back({Lt, -i / 2 * Lt});
        pr.push_back({Lt + Lm, -i / 2 * Lt - i * Lm});
        pr.push_back({2 * Lt + Lm, -s.H});
    } else {
        double z = 0, u = 0;
        while (-z < s.H - 1e-9) {
            const double rise = std::min(s.landingEvery, s.H + z);
            u += rise / i;
            z -= rise;
            pr.push_back({u, z});
            if (-z < s.H - 1e-9) {
                u += s.landingL;
                pr.push_back({u, z});
            }
        }
    }
    const double Lr = pr.back().first;
    pr.push_back({Lr + s.floorL, -s.H});
    auto zAt = [&](double u) {
        for (std::size_t k = 1; k < pr.size(); ++k)
            if (u <= pr[k].first + 1e-9) {
                const double t = (u - pr[k - 1].first) / std::max(1e-9, pr[k].first - pr[k - 1].first);
                return pr[k - 1].second + t * (pr[k].second - pr[k - 1].second);
            }
        return pr.back().second;
    };
    // namuna nuqtalar (sinishlar + har 1 m)
    std::vector<double> us;
    for (const auto& p : pr) us.push_back(p.first);
    for (double u = 0; u < pr.back().first; u += 1.0) us.push_back(u);
    std::sort(us.begin(), us.end());
    us.erase(std::unique(us.begin(), us.end(), [](double a, double b) { return std::abs(a - b) < 1e-6; }), us.end());
    const double W = s.width, t = s.wallT;
    double wallV = 0;
    for (std::size_t k = 0; k + 1 < us.size(); ++k) {
        const double u0 = us[k], u1 = us[k + 1], z0 = zAt(u0), z1 = zAt(u1);
        o.face({{u0, 0, z0}, {u1, 0, z1}, {u1, W, z1}, {u0, W, z0}}, "RAMPA_3D");  // yo'l yuzasi
        for (double v0 : {-t, W}) {  // yon devorlar: poydevordan (rampa −0.3) parapetgacha
            const double zb = std::min(z0, z1) - 0.3, zt = s.parapet;
            o.boxXY(u0, v0, u1, v0 + t, zb, zt, "RAMPA_DEVOR");
            wallV += (u1 - u0) * t * (zt - zb);
        }
    }
    // plan: kontur, nishablik strelkalari, belgilar
    o.poly({{0, 0, 0}, {pr.back().first, 0, 0}, {pr.back().first, W, 0}, {0, W, 0}}, true, "RAMPA");
    for (std::size_t k = 0; k + 1 < pr.size(); ++k) {
        const double du = pr[k + 1].first - pr[k].first, dz = pr[k].second - pr[k + 1].second;
        if (du < 1e-6) continue;
        const double um = (pr[k].first + pr[k + 1].first) / 2;
        if (dz > 1e-6) {
            o.line({um - std::min(1.5, du / 3), W / 2, 0}, {um + std::min(1.5, du / 3), W / 2, 0}, "RAMPA");
            o.poly({{um + std::min(1.5, du / 3) - 0.3, W / 2 + 0.12, 0}, {um + std::min(1.5, du / 3), W / 2, 0}, {um + std::min(1.5, du / 3) - 0.3, W / 2 - 0.12, 0}}, false, "RAMPA");
            o.text({um, W / 2 + 0.15, 0}, 0.25, std::format("i={:.1f}%", dz / du * 100), "RAMPA", 0, TextHAlign::Center);
        }
        o.line({pr[k].first, 0, 0}, {pr[k].first, W, 0}, "RAMPA");
    }
    for (const auto& p : pr) o.text({p.first + 0.05, -0.6, 0}, 0.22, "\xE2\x96\xBD" + fz(s.at.z0 + p.second), "RAMPA", kPI / 2);
    r.concrete = wallV;
    double maxI = 0;
    for (std::size_t k = 0; k + 1 < pr.size(); ++k) {
        const double du = pr[k + 1].first - pr[k].first;
        if (du > 1e-6) maxI = std::max(maxI, (pr[k].second - pr[k + 1].second) / du);
    }
    r.info.push_back({"tpl.i.size", std::format("L={:.2f} m (+{:.1f}), W={:.2f} m", Lr, s.floorL, W)});
    r.info.push_back({"tpl.i.levels", fz(s.at.z0) + " … " + fz(s.at.z0 - s.H)});
    r.info.push_back({"tpl.i.slope", std::format("{:.1f}%", maxI * 100)});
    r.info.push_back({"tpl.i.concrete", std::format("{:.2f} m³", r.concrete)});
    // tekshiruvlar (SP 113.13330 / SP 59.13330 — taxminiy, tasdiqlanmagan)
    if (s.kind == 0) {
        if (maxI > 0.18 + 1e-9) r.issues.push_back({true, "tpl.chk.ramp_car", {std::format("{:.1f}", maxI * 100), "18"}});
        else if (maxI > 0.15 + 1e-9) r.issues.push_back({false, "tpl.chk.ramp_open", {std::format("{:.1f}", maxI * 100)}});
        if (W < 3.5 - 1e-9) r.issues.push_back({false, "tpl.chk.ramp_width", {std::format("{:.2f}", W), "3.50"}});
    } else {
        if (maxI > 0.0833 + 1e-4) r.issues.push_back({true, "tpl.chk.ramp_ped", {std::format("{:.1f}", maxI * 100)}});
        if (W < 1.0 - 1e-9) r.issues.push_back({false, "tpl.chk.ramp_width", {std::format("{:.2f}", W), "1.00"}});
    }
    placeAll(r.entities, s.at);
    return r;
}

// ---------------------------------------------------------------- tirgak devor
std::vector<Vec3> retainSection(const RetainSpec& s) {
    const double toe = s.type == 1 ? s.toe : 0;
    if (s.type == 2) {  // gravitatsion: old yuz tik, orqa yuz qiya; tagida poydevor
        return {{-0.1, -s.hBase}, {s.B + 0.1, -s.hBase}, {s.B + 0.1, 0}, {s.B, 0}, {s.tTop, s.H}, {0, s.H}, {0, 0}, {-0.1, 0}};
    }
    return {{-toe, -s.hBase}, {s.B - toe, -s.hBase}, {s.B - toe, 0}, {s.tBot, 0}, {s.tTop, s.H}, {0, s.H}, {0, 0}, {-toe, 0}};
}

RetainCheck retainCheck(const RetainSpec& s) {
    RetainCheck c;
    const double Ht = s.H + s.hBase;
    const double ka = std::pow(std::tan((45 - s.phi / 2) * kPI / 180), 2);
    const double Ps = 0.5 * ka * s.gamma * Ht * Ht, Pq = ka * s.q * Ht;
    c.Pa = Ps + Pq;
    c.Mo = Ps * Ht / 3 + Pq * Ht / 2;
    // og'irliklar (1 m), momentlar old tovon chetiga nisbatan
    const auto sec = retainSection(s);
    double A = 0, Sx = 0;
    for (std::size_t i = 0; i < sec.size(); ++i) {
        const auto& p = sec[i];
        const auto& q = sec[(i + 1) % sec.size()];
        const double cr = p.x * q.y - q.x * p.y;
        A += cr;
        Sx += (p.x + q.x) * cr;
    }
    A /= 2;
    const double xc = Sx / (6 * A);
    c.area = std::abs(A);
    const double xToe = sec.front().x;  // eng chap nuqta (old tovon)
    const double Wc = 24 * c.area;
    double W = Wc, Mr = Wc * (xc - xToe);
    if (s.type != 2) {  // orqa tovon ustidagi grunt (+ yuklama)
        const double x0 = s.tBot, x1 = s.B - (s.type == 1 ? s.toe : 0);
        if (x1 > x0) {
            const double Ws = s.gamma * s.H * (x1 - x0) + s.q * (x1 - x0);
            W += Ws;
            Mr += Ws * ((x0 + x1) / 2 - xToe);
        }
    }
    c.W = W;
    c.Mr = Mr;
    c.FSo = c.Mo > 0 ? Mr / c.Mo : 99;
    c.FSs = c.Pa > 0 ? s.mu * W / c.Pa : 99;
    const double Bf = sec[1].x - sec[0].x;
    const double xr = (Mr - c.Mo) / W;  // natija nuqtasi old tovondan
    c.e = Bf / 2 - xr;
    c.qmax = W / Bf * (1 + 6 * c.e / Bf);
    c.qmin = W / Bf * (1 - 6 * c.e / Bf);
    return c;
}

TplResult makeRetainingWall(const RetainSpec& s) {
    TplResult r;
    Out o{r.entities};
    if (s.H <= 0.2 || s.B <= 0.2 || s.length <= 0.1 || s.tTop <= 0.05) throw Error("tpl.err.rwall", {});
    const auto sec = retainSection(s);
    // 3D: kesim (x, z) Y bo'ylab uzunlikka — yon yuzalar va ikki uch
    auto P = [&](const Vec3& q, double y) { return Vec3{q.x, y, q.y}; };
    for (std::size_t i = 0; i < sec.size(); ++i) {
        const auto& a = sec[i];
        const auto& b = sec[(i + 1) % sec.size()];
        o.face({P(a, 0), P(b, 0), P(b, s.length), P(a, s.length)}, "TIRGAK_DEVOR_3D");
    }
    for (double y : {0.0, s.length}) {
        std::vector<Vec3> cap;
        for (const auto& q : sec) cap.push_back(P(q, y));
        o.face(cap, "TIRGAK_DEVOR_3D");
    }
    // plan: devor izi va poydevor (z=0 da)
    const double xa = sec.front().x, xb = sec[1].x;
    o.poly({{xa, 0, 0}, {xb, 0, 0}, {xb, s.length, 0}, {xa, s.length, 0}}, true, "TIRGAK_DEVOR");
    o.poly({{0, 0, 0}, {std::max(s.tBot, s.tTop), 0, 0}, {std::max(s.tBot, s.tTop), s.length, 0}, {0, s.length, 0}}, true, "TIRGAK_DEVOR");
    // kesim chizmasi (yonida, plan tekisligida: z -> y), o'lchamlar va barqarorlik
    const double ox = xa - 2.0 - (xb - xa), oy = -s.H - 3.0;
    auto S = [&](double x, double z) { return Vec3{ox + x, oy + z, 0}; };
    std::vector<Vec3> sv;
    for (const auto& q : sec) sv.push_back(S(q.x, q.y));
    o.poly(sv, true, "TIRGAK_DEVOR_KESIM");
    const double th = std::clamp(s.H / 15, 0.12, 0.3);
    o.text(S(-0.4 - th * 5, s.H / 2), th, std::format("H={:g}", s.H), "TIRGAK_DEVOR_KESIM");
    o.text(S((xa + xb) / 2 - th * 2, -s.hBase - th * 1.8), th, std::format("B={:g}", xb - xa), "TIRGAK_DEVOR_KESIM");
    o.text(S(0, s.H + th * 0.6), th, std::format("{:g}", s.tTop), "TIRGAK_DEVOR_KESIM");
    o.line(S(xb - 0.0, 0), S(xb + 1.5, 0), "TIRGAK_DEVOR_KESIM");   // grunt sathi (orqada)
    o.line(S(std::max(s.tBot, s.tTop), s.H), S(xb + 1.5, s.H), "TIRGAK_DEVOR_KESIM");
    const auto c = retainCheck(s);
    o.text(S(xb + 0.3, s.H + th * 0.6), th, std::format("q={:g} kPa, φ={:g}°", s.q, s.phi), "TIRGAK_DEVOR_KESIM");
    o.text(S(xa, -s.hBase - th * 4.2), th, std::format("Ko'={:.2f}  Ks={:.2f}  qmax={:.0f} kPa", c.FSo, c.FSs, c.qmax), "TIRGAK_DEVOR_KESIM");
    r.concrete = c.area * s.length;
    r.info.push_back({"tpl.i.size", std::format("H={:g} m, B={:g} m, L={:g} m", s.H, xb - xa, s.length)});
    r.info.push_back({"tpl.i.levels", fz(s.at.z0 - s.hBase) + " … " + fz(s.at.z0 + s.H)});
    r.info.push_back({"tpl.i.concrete", std::format("{:.2f} m³ ({:.3f} m³/m)", r.concrete, c.area)});
    r.info.push_back({"tpl.i.thrust", std::format("{:.1f} kN/m", c.Pa)});
    r.info.push_back({"tpl.i.fso", std::format("{:.2f}", c.FSo)});
    r.info.push_back({"tpl.i.fss", std::format("{:.2f}", c.FSs)});
    r.info.push_back({"tpl.i.qmax", std::format("{:.0f} / {:.0f} kPa", c.qmax, c.qmin)});
    // tekshiruvlar (taxminiy — loyiha hisobi o'rnini bosmaydi)
    if (c.FSo < 1.5) r.issues.push_back({true, "tpl.chk.overturn", {std::format("{:.2f}", c.FSo)}});
    if (c.FSs < 1.5) r.issues.push_back({c.FSs < 1.2, "tpl.chk.slide", {std::format("{:.2f}", c.FSs)}});
    if (c.qmin < 0) r.issues.push_back({false, "tpl.chk.uplift", {std::format("{:.2f}", c.e), std::format("{:.2f}", (xb - xa) / 6)}});
    if (c.qmax > 1.2 * s.Rgr) r.issues.push_back({true, "tpl.chk.bearing", {std::format("{:.0f}", c.qmax), std::format("{:.0f}", 1.2 * s.Rgr)}});
    placeAll(r.entities, s.at);
    return r;
}

// ---------------------------------------------------------------- JSON
std::string steelFrameToJson(const SteelFrameSpec& s) {
    return std::format("{{\"format\":\"asiacad-steelframe\",\"type\":{},\"dx\":{},\"dy\":{},\"floors\":{},\"floorH\":{},\"span\":{},\"bay\":{},"
                       "\"bays\":{},\"eave\":{},\"ridge\":{},\"purlinStep\":{},\"column\":\"{}\",\"beam\":\"{}\",\"beam2\":\"{}\",\"rafter\":\"{}\","
                       "\"purlin\":\"{}\",\"brace\":\"{}\",\"bracing\":{},{}}}",
                       s.type, listJ(s.dx), listJ(s.dy), s.floors, s.floorH, s.span, s.bay, s.bays, s.eave, s.ridge, s.purlinStep, s.column, s.beam,
                       s.beam2, s.rafter, s.purlin, s.brace, s.bracing, atJson(s.at));
}
SteelFrameSpec steelFrameFromJson(const std::string& t) {
    const auto r = parseOr(t);
    SteelFrameSpec s;
    s.type = static_cast<int>(num(r, "type", s.type));
    s.dx = listFrom(r, "dx", s.dx);
    s.dy = listFrom(r, "dy", s.dy);
    s.floors = static_cast<int>(num(r, "floors", s.floors));
    s.floorH = num(r, "floorH", s.floorH);
    s.span = num(r, "span", s.span); s.bay = num(r, "bay", s.bay); s.bays = static_cast<int>(num(r, "bays", s.bays));
    s.eave = num(r, "eave", s.eave); s.ridge = num(r, "ridge", s.ridge); s.purlinStep = num(r, "purlinStep", s.purlinStep);
    s.column = str(r, "column", s.column); s.beam = str(r, "beam", s.beam); s.beam2 = str(r, "beam2", s.beam2);
    s.rafter = str(r, "rafter", s.rafter); s.purlin = str(r, "purlin", s.purlin); s.brace = str(r, "brace", s.brace);
    s.bracing = boolean(r, "bracing", s.bracing);
    s.at = atFrom(r);
    return s;
}
std::string stairToJson(const StairSpec& s) {
    return std::format("{{\"format\":\"asiacad-stair\",\"type\":{},\"H\":{},\"width\":{},\"gap\":{},\"tread\":{},\"riserMax\":{},\"landing\":{},"
                       "\"waist\":{},\"steel\":{},\"stringer\":\"{}\",{}}}",
                       s.type, s.H, s.width, s.gap, s.tread, s.riserMax, s.landing, s.waist, s.steel, s.stringer, atJson(s.at));
}
StairSpec stairFromJson(const std::string& t) {
    const auto r = parseOr(t);
    StairSpec s;
    s.type = static_cast<int>(num(r, "type", s.type));
    s.H = num(r, "H", s.H); s.width = num(r, "width", s.width); s.gap = num(r, "gap", s.gap); s.tread = num(r, "tread", s.tread);
    s.riserMax = num(r, "riserMax", s.riserMax); s.landing = num(r, "landing", s.landing); s.waist = num(r, "waist", s.waist);
    s.steel = boolean(r, "steel", s.steel);
    s.stringer = str(r, "stringer", s.stringer);
    s.at = atFrom(r);
    return s;
}
std::string rampToJson(const RampSpec& s) {
    return std::format("{{\"format\":\"asiacad-ramp\",\"kind\":{},\"H\":{},\"width\":{},\"slope\":{},\"trans\":{},\"landingEvery\":{},\"landingL\":{},"
                       "\"wallT\":{},\"parapet\":{},\"floorL\":{},{}}}",
                       s.kind, s.H, s.width, s.slope, s.trans, s.landingEvery, s.landingL, s.wallT, s.parapet, s.floorL, atJson(s.at));
}
RampSpec rampFromJson(const std::string& t) {
    const auto r = parseOr(t);
    RampSpec s;
    s.kind = static_cast<int>(num(r, "kind", s.kind));
    s.H = num(r, "H", s.H); s.width = num(r, "width", s.width); s.slope = num(r, "slope", s.slope); s.trans = num(r, "trans", s.trans);
    s.landingEvery = num(r, "landingEvery", s.landingEvery); s.landingL = num(r, "landingL", s.landingL);
    s.wallT = num(r, "wallT", s.wallT); s.parapet = num(r, "parapet", s.parapet); s.floorL = num(r, "floorL", s.floorL);
    s.at = atFrom(r);
    return s;
}
std::string retainToJson(const RetainSpec& s) {
    return std::format("{{\"format\":\"asiacad-rwall\",\"type\":{},\"H\":{},\"tTop\":{},\"tBot\":{},\"B\":{},\"toe\":{},\"hBase\":{},\"length\":{},"
                       "\"phi\":{},\"gamma\":{},\"q\":{},\"mu\":{},\"R\":{},{}}}",
                       s.type, s.H, s.tTop, s.tBot, s.B, s.toe, s.hBase, s.length, s.phi, s.gamma, s.q, s.mu, s.Rgr, atJson(s.at));
}
RetainSpec retainFromJson(const std::string& t) {
    const auto r = parseOr(t);
    RetainSpec s;
    s.type = static_cast<int>(num(r, "type", s.type));
    s.H = num(r, "H", s.H); s.tTop = num(r, "tTop", s.tTop); s.tBot = num(r, "tBot", s.tBot); s.B = num(r, "B", s.B); s.toe = num(r, "toe", s.toe);
    s.hBase = num(r, "hBase", s.hBase); s.length = num(r, "length", s.length); s.phi = num(r, "phi", s.phi); s.gamma = num(r, "gamma", s.gamma);
    s.q = num(r, "q", s.q); s.mu = num(r, "mu", s.mu); s.Rgr = num(r, "R", s.Rgr);
    s.at = atFrom(r);
    return s;
}

// ---------------------------------------------------------------- geo-joylash
GeoFit geoFit(const Vec3& p1, const Vec3& P1, const Vec3* p2, const Vec3* P2, bool allowScale) {
    GeoFit g;
    double ang = 0, k = 1;
    if (p2 && P2) {
        const Vec3 a = *p2 - p1, b = *P2 - P1;
        if (a.length2d() < 1e-9 || b.length2d() < 1e-9) throw Error("tpl.err.geo_points", {});
        ang = std::atan2(b.y, b.x) - std::atan2(a.y, a.x);
        g.scale = b.length2d() / a.length2d();
        if (allowScale) k = g.scale;
    }
    // p -> P1 + R·k·(p - p1); Z: p1.z -> P1.z (siljish)
    Transform t = Transform::translation({-p1.x, -p1.y, -p1.z});
    Transform sc;
    sc.a = k; sc.d = k;
    t = Transform::rotation({0, 0, 0}, ang) * sc * t;
    t = Transform::translation({P1.x, P1.y, P1.z}) * t;
    g.t = t;
    g.rotDeg = ang * 180 / kPI;
    if (p2 && P2) g.residual = (t.apply(*p2) - *P2).length2d();
    return g;
}

} // namespace asiacad::bim
