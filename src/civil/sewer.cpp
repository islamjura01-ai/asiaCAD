#include <asiacad/civil/sewer.h>

#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/json.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

namespace asiacad::civil {

namespace {

constexpr double kPI = 3.14159265358979323846;

// ---------------------------------------------------------------- normativ jadvallar
// SNiP 2.04.03-85 "Kanalizatsiya. Tashqi tarmoqlar va inshootlar" (eski norma) — matn bilan solishtirilgan:
// 2.33 eng kichik diametrlar, 2.34 va 16-jadval (to'lish, eng kichik tezlik), 2.36 eng katta tezlik, 2.41 nishabliklar,
// 4.5 ulanish burchagi, 4.7 shelyga bo'yicha ulash, 4.8 chuqurlik. Quduqlar oralig'i va o'lchami — SP 32.13330.2018 6.3.1
// (SNiP 2.04.03-85 ning dolzarblashtirilgan tahriri, qiymatlar bir xil).
const char* kRef = "SNiP 2.04.03-85";
const char* clauseOf(const std::string& key) {
    static const std::pair<const char*, const char*> t[] = {
        {"sewer.chk.min_dn", "2.33"},        {"sewer.chk.min_slope", "2.41"},    {"sewer.chk.slope_just", "2.41"},
        {"sewer.chk.capacity", "2.34"},      {"sewer.chk.fill", "2.34, 16-jadval"}, {"sewer.chk.vmin", "2.34, 16-jadval"},
        {"sewer.chk.vmax", "2.36"},          {"sewer.chk.cover", "4.8"},         {"sewer.chk.frost", "4.8"},
        {"sewer.chk.angle", "4.5"},          {"sewer.chk.dn_decrease", "4.7"},
        {"sewer.chk.spacing", "SP 32.13330.2018, 6.3.1"}, {"sewer.chk.manhole_d", "SP 32.13330.2018, 6.3.1"}};
    for (const auto& [k, c] : t)
        if (key == k) return c;
    return "";
}
const char* kStormRef = "SHNQ 2.04.22-25, 62-70";

double lerp(const double* xs, const double* ys, int n, double x) {
    if (x <= xs[0]) return ys[0];
    for (int i = 1; i < n; ++i)
        if (x <= xs[i]) return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    return ys[n - 1];
}

double maxFill(int dn, bool storm) {
    if (storm) return 1.0;  // yomg'ir tarmog'i to'liq kesimga hisoblanadi
    if (dn <= 250) return 0.6;
    if (dn <= 400) return 0.7;
    if (dn <= 900) return 0.75;
    return 0.8;
}
double minVelocity(int dn, bool storm = false, double P = 1.0) {
    if (storm && P <= 0.33 + 1e-9) return 0.6;  // 16-jadval, 3-izoh
    if (dn <= 250) return 0.7;
    if (dn <= 400) return 0.8;
    if (dn <= 500) return 0.9;
    if (dn <= 800) return 1.0;
    if (dn <= 1200) return 1.15;  // 900 (H/D 0,75) va 1000-1200 (H/D 0,8)
    if (dn <= 1500) return 1.3;
    return 1.5;
}
double maxVelocity(PipeMaterial m, bool storm = false) { return storm ? (isMetal(m) ? 10.0 : 7.0) : (isMetal(m) ? 8.0 : 4.0); }  // 2.36
double maxSpacing(int dn) {
    if (dn <= 150) return 35;
    if (dn <= 450) return 50;
    if (dn <= 600) return 75;
    if (dn <= 900) return 100;
    if (dn <= 1400) return 150;
    if (dn <= 2000) return 200;
    return 250;
}
double minSlopeFor(int dn) {  // 2.41
    if (dn <= 150) return 0.008;
    if (dn <= 200) return 0.007;
    return 1.0 / dn;  // normada yo'q — amaliy qoida i ≥ 1/D (ogohlantirish darajasi)
}
double minSlopeJustified(int dn) { return dn <= 150 ? 0.007 : 0.005; }  // 2.41: "obosnovaniye" bilan ayrim uchastkalarda
// SP 32.13330.2018 6.3.1: D <= 600 — 1000×1000 mm; D >= 700 — (D+400)×(D+500) mm. Bu yerda — kattaroq o'lcham.
// D <= 150 va chuqurlik <= 1,2 m da 700 mm — SNiP 2.04.03-85 4.15 izohi (matni bilan solishtirilmagan).
double requiredManhole(int dn, bool street, double depth) {
    if (dn <= 150 && !street && depth <= 1.2 + 1e-9) return 0.7;
    if (dn <= 600) return 1.0;
    return (dn + 500) / 1000.0;
}
constexpr double kMinCover = 0.7;  // quvur ustigacha, m

std::string num(double v, int dec = 2) {
    std::string s = std::format("{:.{}f}", v, dec);
    for (auto& c : s) if (c == '.') c = ',';
    if (s == "-0,00") s = "0,00";
    return s;
}

Vec3 unit(const Vec3& v) {
    const double l = v.length2d();
    return l < 1e-12 ? Vec3{1, 0, 0} : Vec3{v.x / l, v.y / l, 0};
}
double readable(double a) {
    a = std::remainder(a, 2 * kPI);
    if (a > kPI / 2 + 1e-9) a -= kPI;
    if (a <= -kPI / 2 + 1e-9) a += kPI;
    return a;
}

} // namespace

int SewerResult::errors() const {
    return static_cast<int>(std::count_if(issues.begin(), issues.end(), [](const Issue& i) { return i.level == Issue::Error; }));
}
int SewerResult::warnings() const {
    return static_cast<int>(std::count_if(issues.begin(), issues.end(), [](const Issue& i) { return i.level == Issue::Warning; }));
}

// ---------------------------------------------------------------- gidravlika
double partialFlow(double d, double fill, double slope, double n, double* velocity) {
    fill = std::clamp(fill, 0.0, 1.0);
    if (d <= 0 || slope <= 0 || n <= 0 || fill <= 0) {
        if (velocity) *velocity = 0;
        return 0;
    }
    double area, perim;
    if (fill >= 1.0) {
        area = kPI * d * d / 4;
        perim = kPI * d;
    } else {
        const double th = 2 * std::acos(1 - 2 * fill);
        area = d * d / 8 * (th - std::sin(th));
        perim = d * th / 2;
    }
    const double r = area / perim;
    const double v = std::pow(r, 2.0 / 3.0) * std::sqrt(slope) / n;
    if (velocity) *velocity = v;
    return v * area;
}

double fillForFlow(double d, double q, double slope, double n) {
    if (q <= 0) return 0;
    const double fPeak = 0.938;  // doiraviy kesimda sarf maksimumi
    const double qPeak = partialFlow(d, fPeak, slope, n);
    if (qPeak <= 0) return -1;
    if (q > qPeak) return q / partialFlow(d, 1.0, slope, n);  // > 1: sig'imdan oshgan
    double lo = 0, hi = fPeak;
    for (int i = 0; i < 80; ++i) {
        const double mid = (lo + hi) / 2;
        (partialFlow(d, mid, slope, n) < q ? lo : hi) = mid;
    }
    return (lo + hi) / 2;
}

// ---------------------------------------------------------------- yomg'ir suvlari (SHNQ 2.04.22-25)
double stormA(const StormInput& s) {
    // A = q20·20^n·(1 + lgP / lg mr)^γ
    const double lgmr = std::log10(std::max(1.0001, s.mr));
    return s.q20 * std::pow(20.0, s.n) * std::pow(1 + std::log10(std::max(1e-6, s.P)) / lgmr, s.gamma);
}
double stormZImpervious(double A, double n) {
    static const double as[] = {300, 400, 500, 600, 700, 800, 1000, 1200, 1500};
    static const double lo[] = {0.32, 0.30, 0.29, 0.28, 0.27, 0.26, 0.25, 0.24, 0.23};  // n < 0.65
    static const double hi[] = {0.33, 0.31, 0.30, 0.29, 0.28, 0.27, 0.26, 0.25, 0.24};  // n ≥ 0.65
    return lerp(as, n < 0.65 ? lo : hi, 9, A);
}
double stormZmid(const StormInput& s) {
    // 5-jadval: z qoplama koeffitsiyenti (birinchi tur — 6-jadvaldan)
    const double z[7] = {stormZImpervious(stormA(s), s.n), 0.224, 0.145, 0.125, 0.090, 0.064, 0.038};
    double sum = 0, w = 0;
    for (int i = 0; i < 7; ++i) {
        sum += z[i] * std::max(0.0, s.share[static_cast<std::size_t>(i)]);
        w += std::max(0.0, s.share[static_cast<std::size_t>(i)]);
    }
    return w > 0 ? sum / w : z[0];
}
double stormBeta(double n) {
    static const double ns[] = {0.40, 0.50, 0.60, 0.70};
    static const double bs[] = {0.80, 0.75, 0.70, 0.65};
    return lerp(ns, bs, 4, n);
}
double stormAreaK(double F) {
    if (F < 500) return 1.0;
    static const double fs[] = {500, 1000, 2000, 4000, 6000, 8000, 10000};
    static const double ks[] = {0.95, 0.90, 0.85, 0.80, 0.70, 0.60, 0.55};
    return lerp(fs, ks, 7, F);
}
double stormShortK(double t) {
    if (t >= 10) return 1.0;
    static const double ts[] = {5, 7, 10};
    static const double ks[] = {0.8, 0.9, 1.0};
    return lerp(ts, ks, 3, t);
}
double stormFlow(const StormInput& s, double F, double t) {
    if (F <= 0) return 0;
    t = std::max(t, 1.0);
    const double A = stormA(s);
    const double qr = stormZmid(s) * std::pow(A, 1.2) * F / std::pow(t, 1.2 * s.n - 0.1) * stormShortK(t) * stormAreaK(F);
    return stormBeta(s.n) * qr;
}

// ---------------------------------------------------------------- hisob va tekshiruv
SewerResult compute(const SewerNetwork& n, const PipeCatalog& cat) {
    SewerResult r;
    const std::size_t nm = n.manholes.size();
    r.manholes.resize(nm);
    if (nm < 2) {
        r.issues.push_back({Issue::Error, "sewer.chk.too_few", {}, "", -1, -1});
        return r;
    }
    if (n.segments.size() != nm - 1) {
        r.issues.push_back({Issue::Error, "sewer.chk.segments", {std::to_string(nm - 1), std::to_string(n.segments.size())}, "", -1, -1});
        return r;
    }
    const bool storm = n.system == "K2";
    auto add = [&](Issue::Level lv, const char* key, std::vector<std::string> a, int seg, int mh) {
        const std::string c = clauseOf(key);
        r.issues.push_back({lv, key, std::move(a), c.empty() ? std::string(kRef) : (c.rfind("SP", 0) == 0 ? c : std::string(kRef) + ", " + c), seg, mh});
    };
    const auto& m0 = n.manholes.front();
    double invOut = m0.invert ? *m0.invert : m0.rim - n.startDepth;
    r.manholes[0].invOut = r.manholes[0].invIn = invOut;
    const PipeSpec* prev = nullptr;
    std::map<std::string, QuantityRow> qty;
    const bool useStorm = storm && n.storm.enabled;
    double cumF = 0, flowTime = n.storm.tcon + n.storm.tcan;  // 11-formula: tr = tcon + tcan + tp

    for (std::size_t i = 0; i + 1 < nm; ++i) {
        const auto& a = n.manholes[i];
        const auto& b = n.manholes[i + 1];
        const Segment& sg = n.segments[i];
        SegmentResult s;
        s.spec = cat.find(sg.specId);
        const std::string sid = std::to_string(i + 1);
        if (!s.spec) {
            add(Issue::Error, "sewer.chk.spec", {sid, sg.specId}, static_cast<int>(i), -1);
            r.segments.push_back(s);
            continue;
        }
        const PipeSpec& p = *s.spec;
        const double din = p.id_mm() / 1000.0, dout = p.od / 1000.0;
        s.length = std::hypot(b.x - a.x, b.y - a.y);
        s.slope = sg.slope > 0 ? sg.slope : n.defaultSlope;
        // quduqda quvurlar "shelyga" (ustki yasovchi) bo'yicha ulanadi: kattaroq quvur pastroq tushadi
        if (i > 0 && prev && !n.manholes[i].invert) {
            const double step = std::max(0.0, din - prev->id_mm() / 1000.0);
            invOut = r.manholes[i].invIn - step;
        } else if (i > 0 && n.manholes[i].invert) {
            invOut = *n.manholes[i].invert;
        }
        r.manholes[i].invOut = invOut;
        s.invUp = invOut;
        s.invDown = n.pressure ? b.rim - (a.rim - s.invUp) : s.invUp - s.slope * s.length;
        if (n.pressure) s.slope = s.length > 0 ? (s.invUp - s.invDown) / s.length : 0;
        s.depthUp = a.rim - s.invUp;
        s.depthDown = b.rim - s.invDown;
        s.coverMin = std::min(a.rim - (s.invUp + dout), b.rim - (s.invDown + dout));
        s.maxFill = n.pressure ? 1.0 : maxFill(p.dn, storm);
        s.vMin = minVelocity(p.dn, storm, n.storm.P);
        s.vMax = maxVelocity(p.material, storm);
        s.maxSpacing = maxSpacing(p.dn);
        s.minSlope = minSlopeFor(p.dn);
        const double gSlope = std::max(s.slope, 1e-6);
        s.qFull = partialFlow(din, 1.0, gSlope, p.manningN, &s.vFull) * 1000.0;
        s.flowLs = sg.flowLs;
        if (useStorm) {
            cumF += n.manholes[i].areaHa;
            s.stormF = cumF;
            s.stormT = flowTime;
            s.stormZ = stormZmid(n.storm);
            s.flowLs = stormFlow(n.storm, cumF, flowTime);
        }
        if (s.flowLs > 0) {
            const double qLs = s.flowLs;
            if (n.pressure) {
                s.fill = 1.0;
                s.velocity = qLs / 1000.0 / (kPI * din * din / 4);
            } else {
                s.fill = fillForFlow(din, qLs / 1000.0, gSlope, p.manningN);
                if (s.fill > 0 && s.fill <= 1.0) partialFlow(din, s.fill, gSlope, p.manningN, &s.velocity);
                else s.velocity = s.vFull;
            }
        }
        if (useStorm) flowTime += 0.017 * s.length / std::max(0.3, s.velocity > 0 ? s.velocity : s.vFull);  // 13-formula
        r.manholes[i + 1].invIn = s.invDown;
        r.manholes[i + 1].invOut = s.invDown;
        invOut = s.invDown;

        // ---- tekshiruvlar
        const int si = static_cast<int>(i);
        const int minDn = storm ? (n.street ? 250 : 200) : (n.street ? 200 : 150);  // 2.33
        if (!n.pressure && p.dn < minDn) add(Issue::Error, "sewer.chk.min_dn", {sid, std::to_string(p.dn), std::to_string(minDn)}, si, -1);
        if (!n.pressure) {
            if (p.dn <= 200 && s.slope + 1e-12 < minSlopeJustified(p.dn))
                add(Issue::Error, "sewer.chk.min_slope", {sid, num(s.slope, 4), num(minSlopeJustified(p.dn), 4), std::to_string(p.dn)}, si, -1);
            else if (p.dn <= 200 && s.slope + 1e-12 < s.minSlope)
                add(Issue::Warning, "sewer.chk.slope_just", {sid, num(s.slope, 4), num(s.minSlope, 4), std::to_string(p.dn)}, si, -1);
            else if (p.dn > 200 && s.slope + 1e-12 < s.minSlope)
                add(Issue::Warning, "sewer.chk.slope_rule", {sid, num(s.slope, 4), num(s.minSlope, 4)}, si, -1);
            if (s.length > s.maxSpacing + 1e-9)
                add(Issue::Error, "sewer.chk.spacing", {sid, num(s.length), num(s.maxSpacing, 0), std::to_string(p.dn)}, si, -1);
        }
        if (s.flowLs > 0) {
            if (!n.pressure && s.fill > 1.0)
                add(Issue::Error, "sewer.chk.capacity", {sid, num(s.flowLs, 1), num(s.qFull, 1)}, si, -1);
            else if (!n.pressure && s.fill > s.maxFill + 1e-9)
                add(Issue::Error, "sewer.chk.fill", {sid, num(s.fill), num(s.maxFill)}, si, -1);
            if (!n.pressure && s.velocity + 1e-9 < s.vMin)
                add(Issue::Warning, "sewer.chk.vmin", {sid, num(s.velocity), num(s.vMin)}, si, -1);
            if (s.velocity > s.vMax + 1e-9)
                add(Issue::Error, "sewer.chk.vmax", {sid, num(s.velocity), num(s.vMax, 0)}, si, -1);
        }
        if (s.coverMin + 1e-9 < kMinCover) add(Issue::Error, "sewer.chk.cover", {sid, num(s.coverMin), num(kMinCover, 1)}, si, -1);
        const double frostMin = n.frostDepth - (p.dn <= 500 ? 0.3 : 0.5);
        if (std::min(s.depthUp, s.depthDown) + 1e-9 < frostMin)
            add(Issue::Warning, "sewer.chk.frost", {sid, num(std::min(s.depthUp, s.depthDown)), num(frostMin)}, si, -1);
        if (prev && p.dn < prev->dn && !n.pressure)
            add(Issue::Error, "sewer.chk.dn_decrease", {sid, std::to_string(prev->dn), std::to_string(p.dn)}, si, -1);
        if (!p.verified) add(Issue::Info, "sewer.chk.unverified", {sid, p.id}, si, -1);
        if (useStorm)
            r.issues.push_back({Issue::Info, "sewer.chk.storm_flow", {sid, num(s.stormF, 2), num(s.stormT, 1), num(s.stormZ, 3), num(s.flowLs, 1)},
                                kStormRef, si, -1});
        if (s.invDown > b.rim) add(Issue::Error, "sewer.chk.above_ground", {sid}, si, -1);
        // 4.5: ulanadigan va chiqaruvchi quvurlar orasidagi burchak >= 90° (ya'ni burilish <= 90°)
        if (i > 0 && !n.pressure) {
            const auto& z = n.manholes[i - 1];
            const double ux = a.x - z.x, uy = a.y - z.y, vx = b.x - a.x, vy = b.y - a.y;
            const double lu = std::hypot(ux, uy), lv2 = std::hypot(vx, vy);
            if (lu > 1e-9 && lv2 > 1e-9) {
                const double cosT = std::clamp((ux * vx + uy * vy) / (lu * lv2), -1.0, 1.0);
                const double between = 180.0 - std::acos(cosT) * 180.0 / kPI;
                if (between + 1e-6 < 90.0) add(Issue::Error, "sewer.chk.angle", {a.name, num(between, 0)}, si, static_cast<int>(i));
            }
        }

        // ---- miqdorlar
        auto& q = qty[p.id];
        q.specId = p.id;
        q.length += s.length;
        q.massKg += s.length * p.mass();
        r.totalLength += s.length;
        const double bw = std::max(0.7, dout + 0.6);  // xandaq tubi kengligi (taxminiy)
        const double avgD = (s.depthUp + s.depthDown) / 2 + 0.1;
        const double v = bw * avgD * s.length;
        s.trenchW = bw;
        s.trench = v;
        s.bedding = 0.1 * bw * s.length;
        s.backfill = v - s.bedding - kPI * dout * dout / 4 * s.length;
        r.trenchVolume += v;
        r.beddingVolume += s.bedding;
        r.backfillVolume += s.backfill;
        r.segments.push_back(s);
        prev = s.spec;
    }

    // ---- quduqlar
    for (std::size_t k = 0; k < nm; ++k) {
        auto& mr = r.manholes[k];
        int dn = 0;
        if (k > 0 && r.segments[k - 1].spec) dn = std::max(dn, r.segments[k - 1].spec->dn);
        if (k + 1 < nm && k < r.segments.size() && r.segments[k].spec) dn = std::max(dn, r.segments[k].spec->dn);
        mr.depth = n.manholes[k].rim - std::min(mr.invIn, mr.invOut);
        mr.required = requiredManhole(dn, n.street, mr.depth);
        mr.diameter = n.manholes[k].diameter > 0 ? n.manholes[k].diameter : mr.required;
        if (mr.diameter + 1e-9 < mr.required)
            add(Issue::Error, "sewer.chk.manhole_d", {n.manholes[k].name, num(mr.diameter), num(mr.required), std::to_string(dn)}, -1,
                static_cast<int>(k));
        r.manholeCount[mr.diameter] += 1;
    }
    for (auto& [id, q] : qty) r.pipes.push_back(q);
    for (std::size_t i = 0; i < r.segments.size() && i + 1 < r.manholes.size(); ++i) {
        auto& sg = r.segments[i];
        if (!sg.spec) continue;
        sg.pipeLength = std::max(0.0, sg.length - r.manholes[i].diameter / 2 - r.manholes[i + 1].diameter / 2 + n.segments[i].insUp + n.segments[i].insDown);
    }
    return r;
}

// ---------------------------------------------------------------- chizma
std::vector<std::string> sewerLayers(const SewerNetwork& n) {
    return {n.name + "_QUVUR", n.name + "_QUDUQ", n.name + "_YOZUV", n.name + "_PROFIL"};
}

std::vector<std::unique_ptr<IEntity>> drawPlan(const SewerNetwork& n, const SewerResult& r) {
    std::vector<std::unique_ptr<IEntity>> out;
    const auto layers = sewerLayers(n);
    const double h = n.textHeight;
    auto put = [&](std::unique_ptr<IEntity> e, int layer, int color = kColorByLayer) {
        e->setLayer(layers[static_cast<std::size_t>(layer)]);
        e->setColor(color);
        out.push_back(std::move(e));
    };
    const std::size_t nm = n.manholes.size();
    // aqlli obyekt bog'i: tanlanganda tuzatish paneli tarmoq elementini taniydi
    auto tag = [&](std::size_t from, const char* k, std::size_t idx) {
        for (std::size_t j = from; j < out.size(); ++j) {
            out[j]->xset("sewer", n.name);
            out[j]->xset(k, std::to_string(idx));
        }
    };
    for (std::size_t k = 0; k < nm; ++k) {
        const std::size_t first = out.size();
        const auto& m = n.manholes[k];
        const double rad = (k < r.manholes.size() ? r.manholes[k].diameter : 1.0) / 2;
        const Vec3 c{m.x, m.y, 0};
        put(std::make_unique<CircleEntity>(c, rad), 1);
        put(std::make_unique<TextEntity>(c + Vec3{rad + 0.3 * h, rad + 0.3 * h, 0}, h, m.name), 2);
        if (k < r.manholes.size()) {
            put(std::make_unique<TextEntity>(c + Vec3{rad + 0.3 * h, -rad - 0.3 * h, 0}, h * 0.7,
                                             num(m.rim) + " / " + num(std::min(r.manholes[k].invIn, r.manholes[k].invOut)), 0,
                                             TextHAlign::Left, TextVAlign::Top),
                2);
        }
        tag(first, "mh", k);
    }
    for (std::size_t i = 0; i + 1 < nm && i < r.segments.size(); ++i) {
        const auto& s = r.segments[i];
        if (!s.spec) continue;
        const std::size_t first = out.size();
        const Vec3 a{n.manholes[i].x, n.manholes[i].y, 0}, b{n.manholes[i + 1].x, n.manholes[i + 1].y, 0};
        const Vec3 u = unit(b - a);
        const double ra = r.manholes[i].diameter / 2, rb = r.manholes[i + 1].diameter / 2;
        if (s.length <= ra + rb) continue;
        const int aci = materialAci(s.spec->material);
        const Vec3 p0 = a + u * ra, p1 = b - u * rb;
        put(std::make_unique<LineEntity>(p0, p1), 0, aci);
        // oqim yo'nalishi strelkasi
        const Vec3 nn{-u.y, u.x, 0};
        const Vec3 tip = a + (b - a) * 0.62, base = tip - u * (h * 1.2);
        std::vector<PolylineEntity> loop;
        loop.emplace_back(std::vector<Vec3>{tip, base + nn * (h * 0.35), base - nn * (h * 0.35)}, true);
        put(std::make_unique<HatchEntity>(std::move(loop), "SOLID"), 0, aci);
        // yozuv: material, diametr, nishablik, uzunlik
        const double ang = readable(std::atan2(u.y, u.x));
        const Vec3 up{-std::sin(ang), std::cos(ang), 0};
        const Vec3 mid = (a + b) * 0.5;
        const std::string lbl = std::format("{} {} \xC3\x98{}", materialKey(s.spec->material), s.spec->klass, s.spec->dn);
        const std::string geo = n.pressure ? "L=" + num(s.length) : "i=" + num(s.slope, 4) + "  L=" + num(s.length);
        put(std::make_unique<TextEntity>(mid + up * (h * 0.5), h * 0.8, lbl, ang, TextHAlign::Center, TextVAlign::Bottom), 2);
        put(std::make_unique<TextEntity>(mid - up * (h * 0.5), h * 0.7, geo, ang, TextHAlign::Center, TextVAlign::Top), 2);
        tag(first, "seg", i);
    }
    return out;
}

std::vector<std::unique_ptr<IEntity>> drawProfile(const SewerNetwork& n, const SewerResult& r, const Vec3& o,
                                                  const ProfileLabels& L) {
    std::vector<std::unique_ptr<IEntity>> out;
    const auto layers = sewerLayers(n);
    const std::string layer = layers[3];
    const double h = n.textHeight, k = n.vScale;
    const std::size_t nm = n.manholes.size();
    if (nm < 2 || r.segments.size() + 1 != nm) return out;
    auto put = [&](std::unique_ptr<IEntity> e, int color = kColorByLayer) {
        e->setLayer(layer);
        e->setColor(color);
        out.push_back(std::move(e));
    };
    auto line = [&](Vec3 a, Vec3 b, int color = kColorByLayer) { put(std::make_unique<LineEntity>(a, b), color); };
    auto text = [&](Vec3 p, double th, const std::string& s, double rot = 0, TextHAlign ha = TextHAlign::Center,
                    TextVAlign va = TextVAlign::Middle) { put(std::make_unique<TextEntity>(p, th, s, rot, ha, va)); };

    std::vector<double> ch(nm, 0);
    for (std::size_t i = 1; i < nm; ++i) ch[i] = ch[i - 1] + r.segments[i - 1].length;
    const double total = ch.back();
    double minEl = 1e300, maxEl = -1e300;
    for (std::size_t i = 0; i < nm; ++i) {
        minEl = std::min({minEl, r.manholes[i].invIn, r.manholes[i].invOut});
        maxEl = std::max(maxEl, n.manholes[i].rim);
    }
    const double datum = std::floor(minEl - 1.0);

    // jadval qatorlari (yuqoridan pastga)
    struct Row { const std::string* title; double height; };
    const Row rows[] = {{&L.ground, 7 * h}, {&L.invert, 7 * h}, {&L.depth, 7 * h}, {&L.slope, 4 * h},
                        {&L.distance, 4 * h}, {&L.pipe, 4 * h}, {&L.manhole, 4 * h}};
    double tableH = 0;
    for (const auto& rw : rows) tableH += rw.height;
    const double W = 26 * h;  // sarlavha ustuni
    const double x0 = o.x - W, x1 = o.x + total;
    std::vector<double> rowTop;
    double y = o.y + tableH;
    line({x0, y, 0}, {x1, y, 0});
    for (const auto& rw : rows) {
        rowTop.push_back(y);
        text({x0 + h * 0.6, y - rw.height / 2, 0}, h * 0.8, *rw.title, 0, TextHAlign::Left, TextVAlign::Middle);
        y -= rw.height;
        line({x0, y, 0}, {x1, y, 0});
    }
    line({x0, o.y, 0}, {x0, o.y + tableH, 0});
    line({o.x, o.y, 0}, {o.x, o.y + tableH, 0});
    line({x1, o.y, 0}, {x1, o.y + tableH, 0});

    const double yBase = o.y + tableH + 3 * h;
    auto Y = [&](double el) { return yBase + (el - datum) * k; };
    text({x0 + h * 0.6, yBase, 0}, h * 0.8, L.datum + " " + num(datum), 0, TextHAlign::Left, TextVAlign::Bottom);
    line({o.x, yBase, 0}, {x1, yBase, 0}, 8);

    // yer sathi
    std::vector<Vec3> ground;
    for (std::size_t i = 0; i < nm; ++i) ground.push_back({o.x + ch[i], Y(n.manholes[i].rim), 0});
    put(std::make_unique<PolylineEntity>(ground), 3);

    // quvurlar (tubi va usti) va quduqlar
    for (std::size_t i = 0; i + 1 < nm; ++i) {
        const auto& s = r.segments[i];
        if (!s.spec) continue;
        const int aci = materialAci(s.spec->material);
        const double dOut = s.spec->od / 1000.0;
        line({o.x + ch[i], Y(s.invUp), 0}, {o.x + ch[i + 1], Y(s.invDown), 0}, aci);
        line({o.x + ch[i], Y(s.invUp + dOut), 0}, {o.x + ch[i + 1], Y(s.invDown + dOut), 0}, aci);
        const double xm = o.x + (ch[i] + ch[i + 1]) / 2;
        // jadval: nishablik, masofa, quvur
        text({xm, rowTop[3] - rows[3].height / 2, 0}, h * 0.8,
             n.pressure ? "-" : num(s.slope * 1000, 1) + "\xE2\x80\xB0");  // ‰
        text({xm, rowTop[4] - rows[4].height / 2, 0}, h * 0.8, num(s.length));
        text({xm, rowTop[5] - rows[5].height / 2, 0}, h * 0.7,
             std::format("{} {} \xC3\x98{}", materialKey(s.spec->material), s.spec->klass, s.spec->dn));
        // nishablik qatorida yo'nalish chizig'i
        const double yt = rowTop[3] - rows[3].height * 0.15, yb = rowTop[3] - rows[3].height * 0.85;
        line({o.x + ch[i], yt, 0}, {o.x + ch[i + 1], yb, 0}, 8);
    }
    for (std::size_t i = 0; i < nm; ++i) {
        const double x = o.x + ch[i];
        const auto& mr = r.manholes[i];
        const double half = mr.diameter / 2;
        const double bot = std::min(mr.invIn, mr.invOut) - 0.1;
        put(std::make_unique<PolylineEntity>(std::vector<Vec3>{{x - half, Y(bot), 0}, {x + half, Y(bot), 0},
                                                              {x + half, Y(n.manholes[i].rim), 0}, {x - half, Y(n.manholes[i].rim), 0}},
                                             true),
            7);
        // jadval ustunlari
        for (int rw = 0; rw < 3; ++rw) line({x, rowTop[rw], 0}, {x, rowTop[rw] - rows[rw].height, 0}, 8);
        line({x, rowTop[3], 0}, {x, o.y, 0}, 8);
        const double rot = kPI / 2;
        text({x, rowTop[0] - rows[0].height / 2, 0}, h * 0.75, num(n.manholes[i].rim), rot);
        if (std::abs(mr.invIn - mr.invOut) > 0.005 && i > 0 && i + 1 < nm) {
            text({x - h * 0.9, rowTop[1] - rows[1].height / 2, 0}, h * 0.7, num(mr.invIn), rot);
            text({x + h * 0.9, rowTop[1] - rows[1].height / 2, 0}, h * 0.7, num(mr.invOut), rot);
        } else {
            text({x, rowTop[1] - rows[1].height / 2, 0}, h * 0.75, num(i == 0 ? mr.invOut : mr.invIn), rot);
        }
        text({x, rowTop[2] - rows[2].height / 2, 0}, h * 0.75, num(mr.depth), rot);
        text({x, rowTop[6] - rows[6].height / 2, 0}, h * 0.75, n.manholes[i].name);
        text({x, Y(n.manholes[i].rim) + h * 0.8, 0}, h * 0.8, n.manholes[i].name, 0, TextHAlign::Center, TextVAlign::Bottom);
    }
    (void)maxEl;
    for (auto& e : out) { e->xset("sewer", n.name); e->xset("part", "profile"); }
    return out;
}

// ---------------------------------------------------------------- JSON
std::string toJson(const SewerNetwork& n) {
    auto esc = [](const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') o += '\\';
            o += c;
        }
        return o;
    };
    std::ostringstream os;
    os << std::format("{{\n  \"format\": \"asiacad-sewer\", \"version\": 1,\n  \"name\": \"{}\", \"system\": \"{}\", \"street\": {}, "
                      "\"pressure\": {},\n  \"startDepth\": {}, \"slope\": {}, \"frost\": {}, \"textHeight\": {}, \"vscale\": {},\n",
                      esc(n.name), esc(n.system), n.street ? "true" : "false", n.pressure ? "true" : "false", n.startDepth,
                      n.defaultSlope, n.frostDepth, n.textHeight, n.vScale);
    {
        const auto& st = n.storm;
        os << std::format("  \"storm\": {{\"enabled\": {}, \"q20\": {}, \"n\": {}, \"mr\": {}, \"gamma\": {}, \"P\": {}, \"tcon\": {}, "
                          "\"tcan\": {}, \"share\": [{}, {}, {}, {}, {}, {}, {}]}},\n",
                          st.enabled ? "true" : "false", st.q20, st.n, st.mr, st.gamma, st.P, st.tcon, st.tcan, st.share[0], st.share[1],
                          st.share[2], st.share[3], st.share[4], st.share[5], st.share[6]);
    }
    os << "  \"manholes\": [\n";
    for (std::size_t i = 0; i < n.manholes.size(); ++i) {
        const auto& m = n.manholes[i];
        os << std::format("    {{\"name\": \"{}\", \"x\": {}, \"y\": {}, \"rim\": {}", esc(m.name), m.x, m.y, m.rim);
        if (m.invert) os << std::format(", \"invert\": {}", *m.invert);
        if (m.diameter > 0) os << std::format(", \"d\": {}", m.diameter);
        if (m.areaHa > 0) os << std::format(", \"area\": {}", m.areaHa);
        os << (i + 1 < n.manholes.size() ? "},\n" : "}\n");
    }
    os << "  ],\n  \"segments\": [\n";
    for (std::size_t i = 0; i < n.segments.size(); ++i) {
        const auto& s = n.segments[i];
        os << std::format("    {{\"spec\": \"{}\", \"slope\": {}, \"flow\": {}", esc(s.specId), s.slope, s.flowLs);
        if (s.insUp != 0 || s.insDown != 0) os << std::format(", \"insUp\": {}, \"insDown\": {}", s.insUp, s.insDown);
        os << (i + 1 < n.segments.size() ? "},\n" : "}\n");
    }
    os << "  ]\n}\n";
    return os.str();
}

SewerNetwork fromJson(const std::string& text) {
    const json::Value root = json::parse(text);
    if (!root.isObject()) throw Error("sewer.err.format", {"root"});
    auto str = [](const json::Value& v, const char* k, const std::string& d) {
        const json::Value* x = v.find(k);
        return x && x->isString() ? x->str() : d;
    };
    auto numv = [](const json::Value& v, const char* k, double d) {
        const json::Value* x = v.find(k);
        return x && x->isNumber() ? x->num() : d;
    };
    auto boolv = [](const json::Value& v, const char* k, bool d) {
        const json::Value* x = v.find(k);
        return x && x->isBool() ? x->boolean() : d;
    };
    SewerNetwork n;
    n.name = str(root, "name", "K1");
    n.system = str(root, "system", "K1");
    n.street = boolv(root, "street", true);
    n.pressure = boolv(root, "pressure", false);
    n.startDepth = numv(root, "startDepth", 1.5);
    n.defaultSlope = numv(root, "slope", 0.008);
    n.frostDepth = numv(root, "frost", 0.8);
    n.textHeight = numv(root, "textHeight", 1.0);
    n.vScale = numv(root, "vscale", 10);
    if (const json::Value* st = root.find("storm"); st && st->isObject()) {
        auto& s2 = n.storm;
        s2.enabled = boolv(*st, "enabled", false);
        s2.q20 = numv(*st, "q20", s2.q20);
        s2.n = numv(*st, "n", s2.n);
        s2.mr = numv(*st, "mr", s2.mr);
        s2.gamma = numv(*st, "gamma", s2.gamma);
        s2.P = numv(*st, "P", s2.P);
        s2.tcon = numv(*st, "tcon", s2.tcon);
        s2.tcan = numv(*st, "tcan", s2.tcan);
        if (const json::Value* sh = st->find("share"); sh && sh->isArray())
            for (std::size_t i = 0; i < 7 && i < sh->arr().size(); ++i)
                if (sh->arr()[i].isNumber()) s2.share[i] = sh->arr()[i].num();
    }
    const json::Value* ms = root.find("manholes");
    if (!ms || !ms->isArray()) throw Error("sewer.err.format", {"manholes"});
    for (const auto& v : ms->arr()) {
        if (!v.isObject()) throw Error("sewer.err.format", {"manholes"});
        Manhole m;
        m.name = str(v, "name", "K" + std::to_string(n.manholes.size() + 1));
        m.x = numv(v, "x", 0);
        m.y = numv(v, "y", 0);
        m.rim = numv(v, "rim", 0);
        if (const json::Value* iv = v.find("invert"); iv && iv->isNumber()) m.invert = iv->num();
        m.diameter = numv(v, "d", 0);
        m.areaHa = numv(v, "area", 0);
        n.manholes.push_back(m);
    }
    if (const json::Value* ss = root.find("segments"); ss && ss->isArray()) {
        for (const auto& v : ss->arr()) {
            if (!v.isObject()) throw Error("sewer.err.format", {"segments"});
            n.segments.push_back({str(v, "spec", ""), numv(v, "slope", 0), numv(v, "flow", 0), numv(v, "insUp", 0), numv(v, "insDown", 0)});
        }
    }
    return n;
}

} // namespace asiacad::civil
