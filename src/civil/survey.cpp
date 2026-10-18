#include <asiacad/civil/survey.h>

#include <asiacad/core/block.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <numeric>
#include <set>
#include <sstream>

namespace asiacad::civil {

namespace {
constexpr double kPI = 3.14159265358979323846;
using Entities = std::vector<std::unique_ptr<IEntity>>;

bool toNum(std::string s, double& v) {
    if (s.empty()) return false;
    for (auto& c : s)
        if (c == ',') c = '.';
    try {
        std::size_t k = 0;
        v = std::stod(s, &k);
        return k == s.size() && std::isfinite(v);
    } catch (...) {
        return false;
    }
}
std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n\"");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n\"");
    return s.substr(a, b - a + 1);
}
std::vector<std::string> splitLine(const std::string& line) {
    // ajratuvchi: tab / ';' bo'lsa — ular; aks holda vergul (agar o'nlik vergul bo'lmasa) yoki bo'sh joy
    std::vector<std::string> out;
    char d = 0;
    if (line.find('\t') != std::string::npos) d = '\t';
    else if (line.find(';') != std::string::npos) d = ';';
    else {
        // "1,100.5,200.3" — vergul ajratuvchi; "1 100,5 200,3" — vergul o'nlik (bo'sh joy bilan 3+ ustun)
        std::stringstream ws(line);
        std::string w;
        int nw = 0;
        while (ws >> w) ++nw;
        if (nw < 3 && line.find(',') != std::string::npos) d = ',';
    }
    if (d) {
        std::stringstream ss(line);
        std::string t;
        while (std::getline(ss, t, d)) out.push_back(trim(t));
    } else {
        std::stringstream ss(line);
        std::string t;
        while (ss >> t) out.push_back(trim(t));
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}
double dist2d(const Vec3& a, const Vec3& b) { return std::hypot(a.x - b.x, a.y - b.y); }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 unit(const Vec3& v) {
    const double l = v.length();
    return l < 1e-15 ? Vec3{0, 0, 0} : v / l;
}
bool inPoly(const std::vector<Vec3>& poly, double x, double y) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > y) != (poly[j].y > y) && x < (poly[j].x - poly[i].x) * (y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
    return in;
}
// 2D: poliliniyaga eng yaqin nuqta (Z — segmentda interpolyatsiya), urinma va segment indeksi
struct Foot {
    Vec3 q;
    Vec3 tan;
    double d = 1e300;
    double s = 0;   // boshidan masofa (2D)
};
Foot footPlan(const DesignRef& r, const Vec3& p) {
    Foot f;
    const std::size_t n = r.pts.size();
    if (n == 1) {
        f.q = r.pts[0];
        f.d = dist2d(p, f.q);
        return f;
    }
    double acc = 0;
    const std::size_t m = r.closed ? n : n - 1;
    for (std::size_t i = 0; i < m; ++i) {
        const Vec3& a = r.pts[i];
        const Vec3& b = r.pts[(i + 1) % n];
        const double dx = b.x - a.x, dy = b.y - a.y, L2 = dx * dx + dy * dy, L = std::sqrt(L2);
        if (L2 < 1e-18) continue;
        const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / L2, 0.0, 1.0);
        const Vec3 q{a.x + t * dx, a.y + t * dy, a.z + t * (b.z - a.z)};
        const double d = dist2d(p, q);
        if (d < f.d - 1e-12) {
            f.d = d;
            f.q = q;
            f.tan = {dx / L, dy / L, 0};
            f.s = acc + t * L;
        }
        acc += L;
    }
    return f;
}
// 3D: poliliniyaga eng yaqin nuqta
struct Foot3 {
    Vec3 q, dir;
    double d = 1e300, s = 0;
};
Foot3 foot3d(const DesignRef& r, const Vec3& p) {
    Foot3 f;
    double acc = 0;
    for (std::size_t i = 0; i + 1 < r.pts.size(); ++i) {
        const Vec3 a = r.pts[i], ab = r.pts[i + 1] - a;
        const double L2 = ab.dot(ab), L = std::sqrt(L2);
        if (L2 < 1e-18) continue;
        const double t = std::clamp((p - a).dot(ab) / L2, 0.0, 1.0);
        const Vec3 q = a + ab * t;
        const double d = p.distanceTo(q);
        if (d < f.d) {
            f.d = d;
            f.q = q;
            f.dir = ab / L;
            f.s = acc + t * L;
        }
        acc += L;
    }
    return f;
}
double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v.size() % 2 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2;
}
// XY bo'yicha bog'langan guruhlar (bitta bog'lanish, radius r)
std::vector<int> clusters(const std::vector<Vec3>& p, double r) {
    std::vector<int> g(p.size(), -1);
    int k = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (g[i] >= 0) continue;
        std::vector<std::size_t> st{i};
        g[i] = k;
        while (!st.empty()) {
            const auto a = st.back();
            st.pop_back();
            for (std::size_t j = 0; j < p.size(); ++j)
                if (g[j] < 0 && dist2d(p[a], p[j]) <= r) {
                    g[j] = k;
                    st.push_back(j);
                }
        }
        ++k;
    }
    return g;
}
}  // namespace

// ---------------------------------------------------------------- nuqtalar fayli
std::vector<SurveyPoint> readPoints(const std::string& text, const PointFileFormat& f, std::vector<std::string>* skipped) {
    std::vector<SurveyPoint> out;
    std::stringstream ss(text);
    std::string line;
    int auto_ = 0;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line = line.substr(3);  // BOM
        const std::string tl = trim(line);
        if (tl.empty() || tl[0] == '#' || tl.rfind("//", 0) == 0) continue;
        const auto t = splitLine(tl);
        std::vector<double> v(t.size());
        std::vector<bool> isn(t.size());
        for (std::size_t i = 0; i < t.size(); ++i) isn[i] = toNum(t[i], v[i]);
        // nom ustuni: birinchi qiymat son emas, yoki 4+ ustunda birinchi uchtadan keyin ham son bo'lsa
        std::size_t k = 0;
        std::string name;
        int numsAfter = 0;
        for (std::size_t i = 1; i < t.size() && isn[i]; ++i) ++numsAfter;
        const bool named = t.size() >= 2 && (!isn[0] || (f.hasName && numsAfter >= 3));
        if (named) {
            name = t[0];
            k = 1;
        }
        if (t.size() < k + 2 || !isn[k] || !isn[k + 1]) {
            if (skipped) skipped->push_back(tl);   // sarlavha yoki noto'g'ri qator
            continue;
        }
        const double a = v[k], b = v[k + 1];
        const double z = t.size() > k + 2 && isn[k + 2] ? v[k + 2] : 0;
        SurveyPoint sp;
        sp.name = name.empty() ? std::to_string(++auto_) : name;
        sp.p = f.geodetic ? Vec3{b, a, z} : Vec3{a, b, z};
        const std::size_t ci = k + (t.size() > k + 2 && isn[k + 2] ? 3 : 2);
        if (ci < t.size()) {
            for (std::size_t i = ci; i < t.size(); ++i) sp.code += (i > ci ? " " : "") + t[i];
        }
        out.push_back(std::move(sp));
    }
    return out;
}

std::string writePoints(const std::vector<SurveyPoint>& pts, const PointFileFormat& f) {
    const std::string d = f.delim ? std::string(1, f.delim) : std::string(" ");
    std::string s;
    for (const auto& p : pts) {
        const double a = f.geodetic ? p.p.y : p.p.x, b = f.geodetic ? p.p.x : p.p.y;
        if (f.hasName) s += p.name + d;
        s += std::format("{:.{}f}{}{:.{}f}{}{:.{}f}", a, f.decimals, d, b, f.decimals, d, p.p.z, f.decimals);
        if (!p.code.empty()) s += d + p.code;
        s += "\r\n";
    }
    return s;
}

Entities pointEntities(const std::vector<SurveyPoint>& pts, double h) {
    Entities es;
    // bir vertikaldagi (rejada ustma-ust) nuqtalar: yozuvlari pastga qatorlab (yuqoridagi nuqta birinchi) — bir-birini yopmasin
    std::vector<int> row(pts.size(), 0);
    for (std::size_t i = 0; i < pts.size(); ++i)
        for (std::size_t j = 0; j < pts.size(); ++j)
            if (i != j && dist2d(pts[i].p, pts[j].p) < h * 0.5 && (pts[j].p.z > pts[i].p.z || (pts[j].p.z == pts[i].p.z && j < i))) ++row[i];
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const auto& p = pts[i];
        const double dy = -row[i] * (p.code.empty() ? 2.4 : 3.3) * h;
        auto pe = std::make_unique<PointEntity>(p.p);
        pe->setLayer("SYOMKA");
        es.push_back(std::move(pe));
        auto n = std::make_unique<TextEntity>(Vec3{p.p.x + h * 0.4, p.p.y + h * 0.3 + dy, p.p.z}, h, p.name);
        n->setLayer("SYOMKA_NOMI");
        es.push_back(std::move(n));
        auto z = std::make_unique<TextEntity>(Vec3{p.p.x + h * 0.4, p.p.y - h * 1.3 + dy, p.p.z}, h * 0.8, std::format("{:.3f}", p.p.z));
        z->setLayer("SYOMKA_BELGI");
        es.push_back(std::move(z));
        if (!p.code.empty()) {
            auto c = std::make_unique<TextEntity>(Vec3{p.p.x + h * 0.4, p.p.y - h * 2.5 + dy, p.p.z}, h * 0.7, p.code);
            c->setLayer("SYOMKA_KOD");
            es.push_back(std::move(c));
        }
    }
    return es;
}

std::vector<SurveyPoint> collectPoints(const Document& doc, const std::vector<EntityId>& ids) {
    std::vector<SurveyPoint> out;
    // nom yozuvlari (SYOMKA_NOMI yoki *NOM*/*NAME* qatlamlari) — eng yaqini
    std::vector<const TextEntity*> labels, codes;
    doc.forEach([&](const IEntity& e) {
        if (auto* t = dynamic_cast<const TextEntity*>(&e)) {
            if (e.layer() == "SYOMKA_KOD") { codes.push_back(t); return; }
            const std::string L = utf8::toUpper(e.layer());
            if (L == "SYOMKA_NOMI" || L.find("NOM") != std::string::npos || L.find("NAME") != std::string::npos || L.find("НОМ") != std::string::npos)
                labels.push_back(t);
        }
    });
    for (EntityId id : ids) {
        const IEntity* e = doc.find(id);
        if (!e) continue;
        SurveyPoint sp;
        bool ok = false;
        if (auto* p = dynamic_cast<const PointEntity*>(e)) { sp.p = p->position(); ok = true; }
        else if (auto* in = dynamic_cast<const InsertEntity*>(e)) {
            sp.p = in->insertionPoint();
            ok = true;
            for (const auto& a : in->attributes()) {
                const std::string tg = utf8::toUpper(a.tag);
                if (tg == "N" || tg == "NAME" || tg == "NUM" || tg == "NOMI" || tg == "PNT" || tg == "НОМЕР") sp.name = a.text.text();
                if (tg == "Z" || tg == "H" || tg == "ELEV") { double z; if (toNum(a.text.text(), z)) sp.p.z = z; }
            }
        }
        if (!ok) continue;
        // yozuvlar nuqtaning o'z balandligida bo'ladi (bir vertikaldagi nuqtalar adashmasin); nom uchun — topilmasa, istalgan balandlikdan
        auto nearest = [&](const std::vector<const TextEntity*>& v, double rk, bool anyZ) -> std::string {
            double best = 1e300;
            std::string out;
            for (const auto* t : v) {
                const double d2 = dist2d(t->insertion(), sp.p);
                if (d2 >= t->height() * rk) continue;
                if (!anyZ && std::abs(t->insertion().z - sp.p.z) > 1e-3) continue;
                if (d2 < best) { best = d2; out = t->text(); }
            }
            return out;
        };
        if (sp.name.empty()) sp.name = nearest(labels, 12, false);
        if (sp.name.empty()) sp.name = nearest(labels, 4, true);
        sp.code = nearest(codes, 14, false);
        if (sp.code.empty() && e->layer() != "SYOMKA") sp.code = e->layer();
        out.push_back(std::move(sp));
    }
    return out;
}

std::vector<DesignRef> collectRefs(const Document& doc, const std::vector<EntityId>& ids) {
    std::vector<DesignRef> out;
    for (EntityId id : ids) {
        const IEntity* e = doc.find(id);
        if (!e) continue;
        DesignRef r;
        r.name = e->layer() + " #" + std::to_string(id);
        if (auto* l = dynamic_cast<const LineEntity*>(e)) r.pts = {l->startPoint(), l->endPoint()};
        else if (auto* pl = dynamic_cast<const PolylineEntity*>(e)) {
            r.pts = pl->tessellated().vertices();
            r.closed = pl->isClosed();
        } else if (auto* c = dynamic_cast<const CircleEntity*>(e)) {   // qoziq: markaz (POINT) / kontur (PLAN)
            r.pts = {c->center()};
            DesignRef ring;
            ring.name = r.name;
            ring.closed = true;
            for (int i = 0; i < 72; ++i) ring.pts.push_back(c->center() + Vec3{std::cos(i * kPI / 36), std::sin(i * kPI / 36), 0} * c->radius());
            out.push_back(std::move(ring));
            out.back().name += " (O)";
        } else if (auto* a = dynamic_cast<const ArcEntity*>(e)) {
            const int n = std::max(4, static_cast<int>(a->sweep() / (kPI / 36)));
            for (int i = 0; i <= n; ++i) r.pts.push_back(a->pointAtAngle(a->startAngle() + a->sweep() * i / n));
        } else if (auto* p = dynamic_cast<const PointEntity*>(e)) r.pts = {p->position()};
        else if (auto* in = dynamic_cast<const InsertEntity*>(e)) r.pts = {in->insertionPoint()};
        if (!r.pts.empty()) out.push_back(std::move(r));
    }
    return out;
}

const char* devKindKey(DevKind k) {
    switch (k) {
    case DevKind::Plan: return "PLAN";
    case DevKind::Height: return "HEIGHT";
    case DevKind::Vertical: return "VERT";
    case DevKind::Axis: return "AXIS";
    case DevKind::Point: return "POINT";
    }
    return "PLAN";
}
bool devKindFrom(const std::string& s0, DevKind& k) {
    const std::string s = utf8::toUpper(s0);
    for (DevKind c : {DevKind::Plan, DevKind::Height, DevKind::Vertical, DevKind::Axis, DevKind::Point})
        if (s == devKindKey(c)) { k = c; return true; }
    if (s == "VERTICAL" || s == "V") { k = DevKind::Vertical; return true; }
    if (s == "H" || s == "Z") { k = DevKind::Height; return true; }
    if (s == "P" || s == "PILE") { k = DevKind::Point; return true; }
    if (s == "A" || s == "ANCHOR") { k = DevKind::Axis; return true; }
    return false;
}

// ---------------------------------------------------------------- tahlil
DevResult analyze(const std::vector<SurveyPoint>& pts, const std::vector<DesignRef>& refs, const DevParams& prm) {
    DevResult R;
    R.kind = prm.kind;
    auto lineRefs = [&] {
        std::vector<const DesignRef*> v;
        for (const auto& r : refs)
            if (r.pts.size() >= 2) v.push_back(&r);
        return v;
    };
    switch (prm.kind) {
    case DevKind::Plan: {
        const auto L = lineRefs();
        if (L.empty()) throw Error("dev.err.noref", {});
        for (const auto& sp : pts) {
            Foot best;
            const DesignRef* br = nullptr;
            for (const auto* r : L) {
                const Foot f = footPlan(*r, sp.p);
                if (f.d < best.d) { best = f; br = r; }
            }
            if (!br || best.d > prm.maxDist) { R.unmatched.push_back(sp.name); continue; }
            DevRow row;
            row.name = sp.name;
            row.ref = br->name;
            row.p = sp.p;
            row.foot = {best.q.x, best.q.y, sp.p.z};
            row.dx = sp.p.x - best.q.x;
            row.dy = sp.p.y - best.q.y;
            // ishora: yopiq kontur — tashqari +; ochiq — yo'nalishga nisbatan chap +
            double sgn;
            if (br->closed) sgn = inPoly(br->pts, sp.p.x, sp.p.y) ? -1 : 1;
            else sgn = (best.tan.x * row.dy - best.tan.y * row.dx) >= 0 ? 1 : -1;
            row.d = sgn * best.d;
            row.h = best.s;
            row.allow = prm.tol;
            R.rows.push_back(row);
        }
        // B / P: mahalliy guruhda (≈1.5 m) Z medianasidan yuqori — B, past — P (guruhda 2+ nuqta va balandlik farqi bo'lsa)
        std::vector<Vec3> ps;
        for (const auto& r : R.rows) ps.push_back(r.p);
        const auto g = clusters(ps, prm.cluster);
        std::map<int, std::vector<std::size_t>> grp;
        for (std::size_t i = 0; i < g.size(); ++i) grp[g[i]].push_back(i);
        for (const auto& [k, idx] : grp) {
            if (idx.size() < 2) continue;
            std::vector<double> zs;
            double lo = 1e300, hi = -1e300;
            for (auto i : idx) { zs.push_back(R.rows[i].p.z); lo = std::min(lo, R.rows[i].p.z); hi = std::max(hi, R.rows[i].p.z); }
            if (hi - lo < 0.3) continue;
            const double m = median(zs);
            for (auto i : idx) R.rows[i].cls = R.rows[i].p.z > m || (R.rows[i].p.z == m && R.rows[i].p.z == hi) ? 'B' : 'P';
        }
        break;
    }
    case DevKind::Height: {
        for (const auto& sp : pts) {
            double zd = prm.designZ;
            std::string rn = "Z";
            Vec3 q = sp.p;
            bool have = prm.useDesignZ;
            if (!refs.empty()) {
                Foot best;
                const DesignRef* br = nullptr;
                for (const auto& r : refs) {
                    const Foot f = footPlan(r, sp.p);
                    if (f.d < best.d) { best = f; br = &r; }
                }
                if (br && best.d <= prm.maxDist) {
                    zd = best.q.z;
                    rn = br->name;
                    q = best.q;
                    have = true;
                }
            }
            if (!have) { R.unmatched.push_back(sp.name); continue; }
            DevRow row;
            row.name = sp.name;
            row.ref = rn;
            row.p = sp.p;
            row.foot = {sp.p.x, sp.p.y, zd};
            (void)q;
            row.dz = sp.p.z - zd;
            row.d = row.dz;
            row.allow = prm.tol;
            R.rows.push_back(row);
        }
        break;
    }
    case DevKind::Vertical: {
        std::vector<Vec3> ps;
        for (const auto& s : pts) ps.push_back(s.p);
        const auto g = clusters(ps, prm.cluster);
        std::map<int, std::vector<std::size_t>> grp;
        for (std::size_t i = 0; i < g.size(); ++i) grp[g[i]].push_back(i);
        for (auto& [k, idx] : grp) {
            std::sort(idx.begin(), idx.end(), [&](auto a, auto b) { return pts[a].p.z < pts[b].p.z; });
            const SurveyPoint& base = pts[idx.front()];
            // loyiha o'qi: eng yaqin loyiha nuqtasi / chizig'i (rejada); bo'lmasa — pastki nuqta
            Vec3 axis = base.p;
            std::string rn = base.name;
            bool designAxis = false;
            {
                double best = prm.cluster;
                for (const auto& r : refs) {
                    const Foot f = footPlan(r, base.p);
                    if (f.d < best) { best = f.d; axis = {f.q.x, f.q.y, base.p.z}; rn = r.name; designAxis = true; }
                }
            }
            if (idx.size() < 2 && !designAxis) { R.unmatched.push_back(base.name); continue; }
            DevElement el;
            el.ref = rn;
            for (std::size_t j = designAxis ? 0 : 1; j < idx.size(); ++j) {
                const SurveyPoint& sp = pts[idx[j]];
                DevRow row;
                row.name = sp.name;
                row.ref = rn;
                row.p = sp.p;
                row.foot = {axis.x, axis.y, sp.p.z};
                row.dx = sp.p.x - axis.x;
                row.dy = sp.p.y - axis.y;
                row.h = sp.p.z - base.p.z;
                row.d = std::hypot(row.dx, row.dy);
                row.allow = prm.tol + (prm.tolRatio > 0 ? row.h / prm.tolRatio : 0);
                el.maxDev = std::max(el.maxDev, row.d);
                ++el.n;
                R.rows.push_back(row);
            }
            if (el.n) {
                // o'lchangan og'ish burchagi (yuqori nuqta bo'yicha)
                const DevRow& top = R.rows.back();
                el.measIncl = top.h > 1e-9 ? 90 - std::atan2(top.d, top.h) * 180 / kPI : 90;
                el.designIncl = 90;
                el.measAz = std::atan2(top.dx, top.dy) * 180 / kPI;   // geodezik azimut (shimoldan soat bo'yicha)
                if (el.measAz < 0) el.measAz += 360;
                R.elements.push_back(el);
            }
        }
        break;
    }
    case DevKind::Axis: {
        const auto L = lineRefs();
        if (L.empty()) throw Error("dev.err.noref", {});
        std::map<const DesignRef*, std::vector<std::size_t>> byRef;
        for (const auto& sp : pts) {
            Foot3 best;
            const DesignRef* br = nullptr;
            for (const auto* r : L) {
                const Foot3 f = foot3d(*r, sp.p);
                if (f.d < best.d) { best = f; br = r; }
            }
            if (!br || best.d > prm.maxDist) { R.unmatched.push_back(sp.name); continue; }
            DevRow row;
            row.name = sp.name;
            row.ref = br->name;
            row.p = sp.p;
            row.foot = best.q;
            const Vec3 v = sp.p - best.q;
            row.dx = v.x;
            row.dy = v.y;
            row.dz = v.z;
            row.d = best.d;
            row.h = best.s;
            // yon (rejada, o'qqa perpendikulyar gorizontal) va tik tashkil etuvchi; vertikal o'qda — dX/dY
            Vec3 nh = cross(best.dir, {0, 0, 1});
            if (nh.length() < 1e-6) { row.lat = v.x; row.vert = v.y; }
            else {
                nh = unit(nh);
                const Vec3 nv = unit(cross(nh, best.dir));
                row.lat = v.dot(nh);
                row.vert = v.dot(nv);
            }
            row.allow = prm.tol;
            byRef[br].push_back(R.rows.size());
            R.rows.push_back(row);
        }
        for (const auto& [r, idx] : byRef) {
            DevElement el;
            el.ref = r->name;
            el.n = static_cast<int>(idx.size());
            const Vec3 dd = r->pts.back() - r->pts.front();
            el.designIncl = std::atan2(-dd.z, std::hypot(dd.x, dd.y)) * 180 / kPI;   // pastga qiyalik +
            el.designAz = std::fmod(std::atan2(dd.x, dd.y) * 180 / kPI + 360, 360);
            for (auto i : idx) el.maxDev = std::max(el.maxDev, R.rows[i].d);
            if (idx.size() >= 2) {   // o'lchangan o'q: o'q bo'yicha eng chetki ikki nuqta
                auto mn = *std::min_element(idx.begin(), idx.end(), [&](auto a, auto b) { return R.rows[a].h < R.rows[b].h; });
                auto mx = *std::max_element(idx.begin(), idx.end(), [&](auto a, auto b) { return R.rows[a].h < R.rows[b].h; });
                const Vec3 m = R.rows[mx].p - R.rows[mn].p;
                if (m.length() > 1e-6) {
                    el.measIncl = std::atan2(-m.z, std::hypot(m.x, m.y)) * 180 / kPI;
                    el.measAz = std::fmod(std::atan2(m.x, m.y) * 180 / kPI + 360, 360);
                } else { el.measIncl = el.designIncl; el.measAz = el.designAz; }
            } else { el.measIncl = el.designIncl; el.measAz = el.designAz; }
            R.elements.push_back(el);
        }
        break;
    }
    case DevKind::Point: {
        std::vector<const DesignRef*> P;
        for (const auto& r : refs)
            if (r.pts.size() == 1) P.push_back(&r);
        if (P.empty()) throw Error("dev.err.noref", {});
        // juftlar: masofa bo'yicha ochko'z (har biri bir marta)
        struct Pair { double d; std::size_t i, j; };
        std::vector<Pair> prs;
        for (std::size_t i = 0; i < pts.size(); ++i)
            for (std::size_t j = 0; j < P.size(); ++j) {
                const double d = dist2d(pts[i].p, P[j]->pts[0]);
                if (d <= prm.maxDist) prs.push_back({d, i, j});
            }
        std::sort(prs.begin(), prs.end(), [](const Pair& a, const Pair& b) { return a.d < b.d; });
        std::vector<bool> ui(pts.size()), uj(P.size());
        std::vector<std::pair<std::size_t, std::size_t>> match;
        for (const auto& pr : prs)
            if (!ui[pr.i] && !uj[pr.j]) { ui[pr.i] = uj[pr.j] = true; match.push_back({pr.i, pr.j}); }
        std::sort(match.begin(), match.end(), [&](auto a, auto b) { return a.second < b.second; });
        for (auto [i, j] : match) {
            const Vec3 q = P[j]->pts[0];
            DevRow row;
            row.name = pts[i].name;
            row.ref = P[j]->name;
            row.p = pts[i].p;
            row.foot = q;
            row.dx = row.p.x - q.x;
            row.dy = row.p.y - q.y;
            const double zd = std::abs(q.z) > 1e-9 ? q.z : (prm.useDesignZ ? prm.designZ : row.p.z);
            row.dz = row.p.z - zd;
            row.d = std::hypot(row.dx, row.dy);
            row.allow = prm.tol;
            R.rows.push_back(row);
        }
        for (std::size_t i = 0; i < pts.size(); ++i)
            if (!ui[i]) R.unmatched.push_back(pts[i].name);
        for (std::size_t j = 0; j < P.size(); ++j)
            if (!uj[j]) R.unmatched.push_back("(" + P[j]->name + ")");
        break;
    }
    }
    double s = 0, s2 = 0;
    for (auto& r : R.rows) {
        r.ok = std::abs(r.d) <= r.allow + 1e-9;
        R.nOk += r.ok;
        R.maxAbs = std::max(R.maxAbs, std::abs(r.d));
        s += std::abs(r.d);
        s2 += r.d * r.d;
    }
    if (!R.rows.empty()) {
        R.mean = s / R.rows.size();
        R.rms = std::sqrt(s2 / R.rows.size());
    }
    return R;
}

// ---------------------------------------------------------------- chizmaga
Entities annotate(const DevResult& R, const DevParams& prm) {
    Entities es;
    const double h = prm.textH, A = prm.arrowLen;
    auto put = [&](std::unique_ptr<IEntity> e, const char* layer, int color) {
        e->setLayer(layer);
        e->setColor(color);
        es.push_back(std::move(e));
    };
    // strelka: boshi b, yo'nalish u (birlik, rejada), uzunlik A — hammasi z balandlikda
    auto arrow = [&](const Vec3& b, Vec3 u, int color) {
        const double l = std::hypot(u.x, u.y);
        if (l < 1e-12) return;
        u = {u.x / l, u.y / l, 0};
        const Vec3 tip = b + u * A, nrm{-u.y, u.x, 0};
        const double hl = std::min(A * 0.35, h * 1.4), hw = hl * 0.35;
        put(std::make_unique<LineEntity>(b, tip - u * hl), "DEV_STRELKA", color);
        PolylineEntity head({tip, tip - u * hl + nrm * hw, tip - u * hl - nrm * hw}, true);
        put(std::make_unique<HatchEntity>(std::vector<PolylineEntity>{head}, "SOLID"), "DEV_STRELKA", color);
        put(std::make_unique<PolylineEntity>(head), "DEV_STRELKA", color);
    };
    // rejada ustma-ust nuqtalar (yuqori/past): qiymatlari qatorlab — bir-birini yopmasin (yuqoridagi birinchi)
    std::vector<int> stack(R.rows.size(), 0);
    for (std::size_t i = 0; i < R.rows.size(); ++i)
        for (std::size_t j = 0; j < R.rows.size(); ++j)
            if (i != j && dist2d(R.rows[i].p, R.rows[j].p) < h * 0.5 && (R.rows[j].p.z > R.rows[i].p.z || (R.rows[j].p.z == R.rows[i].p.z && j < i))) {
                // strelkalar qarama-qarshi bo'lsa — yozuvlar to'qnashmaydi
                const auto& a = R.rows[i];
                const auto& b = R.rows[j];
                const double ux = a.p.x - a.foot.x, uy = a.p.y - a.foot.y, vx = b.p.x - b.foot.x, vy = b.p.y - b.foot.y;
                const double la = std::hypot(ux, uy), lb = std::hypot(vx, vy);
                const bool opposite = R.kind != DevKind::Height && la > 1e-9 && lb > 1e-9 && (ux * vx + uy * vy) / (la * lb) < -0.5;
                if (!opposite) ++stack[i];
            }
    std::size_t ri = 0;
    auto mm = [](double v, bool sign) { return sign ? std::format("{:+.0f}", v * 1000) : std::format("{:.0f}", std::abs(v) * 1000); };
    for (const auto& r : R.rows) {
        const int k = stack[ri++];
        const int col = r.ok ? 3 : 1;   // yashil / qizil
        const double z = r.p.z;
        Vec3 dir{r.p.x - r.foot.x, r.p.y - r.foot.y, 0};
        std::string val;
        switch (R.kind) {
        case DevKind::Plan:
            val = mm(r.d, true);
            if (std::hypot(dir.x, dir.y) < 1e-9) dir = {1, 0, 0};
            break;
        case DevKind::Height:
            val = mm(r.dz, true);
            break;
        case DevKind::Vertical:
            val = mm(r.d, false);
            break;
        case DevKind::Axis:
            val = mm(r.d, false);
            break;
        case DevKind::Point:
            val = mm(r.d, false);
            break;
        }
        const Vec3 at{r.p.x, r.p.y, z};
        if (R.kind == DevKind::Height) {
            // tik strelka o'rniga: ▲/▼ belgisi va qiymat
            // nom yozuvi o'ngda — qiymat chapda
            put(std::make_unique<TextEntity>(at + Vec3{-h * 0.4, h * 0.3 - k * 1.4 * h, 0}, h, (r.dz >= 0 ? "\xE2\x96\xB2" : "\xE2\x96\xBC") + val, 0,
                                             TextHAlign::Right, TextVAlign::Bottom),
                "DEV_QIYMAT", col);
            continue;
        }
        if (std::hypot(dir.x, dir.y) > 1e-9) arrow(at, dir, col);
        // qiymat — doim gorizontal, strelka uchidan keyin; B/P harfi — qiymat oldida (strelka tomonda)
        const double l = std::hypot(dir.x, dir.y);
        const Vec3 u = l > 1e-9 ? Vec3{dir.x / l, dir.y / l, 0} : Vec3{1, 0, 0};
        const TextHAlign ha = u.x < -0.3 ? TextHAlign::Right : (u.x > 0.3 ? TextHAlign::Left : TextHAlign::Center);
        Vec3 tp = at + u * (A + h * (0.4 + 0.6 * std::abs(u.y)));
        if (k) tp = ha == TextHAlign::Center ? tp + u * (k * 1.4 * h) : tp - Vec3{0, k * 1.4 * h, 0};
        const bool bp = R.kind == DevKind::Plan && r.cls;
        const double lw = h * 0.85;   // harf kengligi (taxminiy)
        Vec3 vp = tp, lp = tp;
        if (bp) {
            if (ha == TextHAlign::Left) vp = tp + Vec3{lw, 0, 0};
            else if (ha == TextHAlign::Right) vp = tp - Vec3{lw, 0, 0};
            else lp = tp - Vec3{(0.6 * h * static_cast<double>(val.size()) + lw) / 2, 0, 0};
        }
        put(std::make_unique<TextEntity>(vp, h, val, 0, ha, TextVAlign::Middle), "DEV_QIYMAT", col);
        if (bp)
            put(std::make_unique<TextEntity>(lp, h * 0.9, std::string(1, r.cls), 0, ha == TextHAlign::Center ? TextHAlign::Right : ha, TextVAlign::Middle), "DEV_BP",
                r.cls == 'B' ? 3 : 5);
        if (R.kind == DevKind::Vertical) {   // H — ostida kichik
            put(std::make_unique<TextEntity>(tp + Vec3{0, -h * 1.2, 0}, h * 0.7, std::format("H={:.2f}", r.h), 0, ha, TextVAlign::Middle), "DEV_QIYMAT", 8);
        }
    }
    return es;
}

// ---------------------------------------------------------------- Excel / CSV
namespace {
struct Col { const char* key; std::function<xlsx::Cell(const DevRow&)> cell; std::function<std::string(const DevRow&)> txt; };
std::vector<Col> columns(DevKind k, bool geo) {
    using C = xlsx::Cell;
    auto mmv = [](double v) { return std::round(v * 10000) / 10; };
    std::vector<Col> c;
    c.push_back({"dev.col.name", [](const DevRow& r) { return C::s(r.name); }, [](const DevRow& r) { return r.name; }});
    auto X = [geo](const Vec3& p) { return geo ? p.y : p.x; };
    auto Y = [geo](const Vec3& p) { return geo ? p.x : p.y; };
    c.push_back({geo ? "dev.col.xn" : "dev.col.x", [=](const DevRow& r) { return C::n(X(r.p), 3); }, [=](const DevRow& r) { return std::format("{:.3f}", X(r.p)); }});
    c.push_back({geo ? "dev.col.ye" : "dev.col.y", [=](const DevRow& r) { return C::n(Y(r.p), 3); }, [=](const DevRow& r) { return std::format("{:.3f}", Y(r.p)); }});
    c.push_back({"dev.col.z", [](const DevRow& r) { return C::n(r.p.z, 3); }, [](const DevRow& r) { return std::format("{:.3f}", r.p.z); }});
    if (k == DevKind::Plan) c.push_back({"dev.col.bp", [](const DevRow& r) { return C::s(r.cls ? std::string(1, r.cls) : ""); }, [](const DevRow& r) { return r.cls ? std::string(1, r.cls) : std::string(); }});
    if (k == DevKind::Vertical) c.push_back({"dev.col.h", [](const DevRow& r) { return C::n(r.h, 3); }, [](const DevRow& r) { return std::format("{:.3f}", r.h); }});
    if (k == DevKind::Axis) c.push_back({"dev.col.along", [](const DevRow& r) { return C::n(r.h, 3); }, [](const DevRow& r) { return std::format("{:.3f}", r.h); }});
    if (k == DevKind::Height) c.push_back({"dev.col.zd", [](const DevRow& r) { return C::n(r.foot.z, 3); }, [](const DevRow& r) { return std::format("{:.3f}", r.foot.z); }});
    if (k == DevKind::Vertical || k == DevKind::Point || k == DevKind::Axis) {
        const double sx = 1;
        c.push_back({geo ? "dev.col.dxn" : "dev.col.dx", [=](const DevRow& r) { return C::n(mmv(geo ? r.dy : r.dx) * sx, 5); }, [=](const DevRow& r) { return std::format("{:.1f}", mmv(geo ? r.dy : r.dx)); }});
        c.push_back({geo ? "dev.col.dye" : "dev.col.dy", [=](const DevRow& r) { return C::n(mmv(geo ? r.dx : r.dy), 5); }, [=](const DevRow& r) { return std::format("{:.1f}", mmv(geo ? r.dx : r.dy)); }});
    }
    if (k == DevKind::Point || k == DevKind::Axis) c.push_back({"dev.col.dz", [=](const DevRow& r) { return C::n(mmv(r.dz), 5); }, [=](const DevRow& r) { return std::format("{:.1f}", mmv(r.dz)); }});
    if (k == DevKind::Axis) {
        c.push_back({"dev.col.lat", [=](const DevRow& r) { return C::n(mmv(r.lat), 5); }, [=](const DevRow& r) { return std::format("{:.1f}", mmv(r.lat)); }});
        c.push_back({"dev.col.vert", [=](const DevRow& r) { return C::n(mmv(r.vert), 5); }, [=](const DevRow& r) { return std::format("{:.1f}", mmv(r.vert)); }});
    }
    c.push_back({"dev.col.dev", [=](const DevRow& r) { return C::n(mmv(r.d), r.ok ? 5 : 6); }, [=](const DevRow& r) { return std::format("{:.1f}", mmv(r.d)); }});
    c.push_back({"dev.col.allow", [=](const DevRow& r) { return C::n(mmv(r.allow), 5); }, [=](const DevRow& r) { return std::format("{:.1f}", mmv(r.allow)); }});
    c.push_back({"dev.col.status", [](const DevRow& r) { return C::s(r.ok ? "OK" : "!", r.ok ? 0 : 6); }, [](const DevRow& r) { return std::string(r.ok ? "OK" : "!"); }});
    c.push_back({"dev.col.ref", [](const DevRow& r) { return C::s(r.ref); }, [](const DevRow& r) { return r.ref; }});
    return c;
}
}  // namespace

xlsx::Workbook toWorkbook(const DevResult& R, const DevParams& prm, const std::function<std::string(const std::string&)>& tr, bool geo) {
    xlsx::Workbook wb;
    auto& s = wb.add(tr(std::string("dev.kind.") + devKindKey(R.kind)).substr(0, 31));
    const auto cols = columns(R.kind, geo);
    std::vector<xlsx::Cell> hdr{xlsx::Cell::s("№", 1)};
    for (const auto& c : cols) hdr.push_back(xlsx::Cell::s(tr(c.key), 1));
    s.add(hdr);
    s.widths = {5, 10};
    for (std::size_t i = 1; i < cols.size(); ++i) s.widths.push_back(12);
    int i = 0;
    for (const auto& r : R.rows) {
        std::vector<xlsx::Cell> row{xlsx::Cell::n(++i, 5)};
        for (const auto& c : cols) row.push_back(c.cell(r));
        s.add(row);
    }
    // xulosa varag'i
    auto& x = wb.add(tr("dev.sheet.summary").substr(0, 31));
    x.autoFilter = false;
    x.widths = {34, 16};
    auto kv = [&](const std::string& k, xlsx::Cell v) { x.add({xlsx::Cell::s(tr(k), 1), std::move(v)}); };
    kv("dev.sum.kind", xlsx::Cell::s(tr(std::string("dev.kind.") + devKindKey(R.kind))));
    kv("dev.sum.n", xlsx::Cell::n(static_cast<double>(R.rows.size()), 5));
    kv("dev.sum.ok", xlsx::Cell::n(R.nOk, 5));
    kv("dev.sum.bad", xlsx::Cell::n(static_cast<double>(R.rows.size()) - R.nOk, R.rows.size() - R.nOk ? 6 : 5));
    kv("dev.sum.pct", xlsx::Cell::n(R.rows.empty() ? 0 : 100.0 * R.nOk / R.rows.size(), 2));
    kv("dev.sum.max", xlsx::Cell::n(R.maxAbs * 1000, 2));
    kv("dev.sum.mean", xlsx::Cell::n(R.mean * 1000, 2));
    kv("dev.sum.rms", xlsx::Cell::n(R.rms * 1000, 2));
    kv("dev.sum.tol", xlsx::Cell::n(prm.tol * 1000, 2));
    if (!R.unmatched.empty()) {
        std::string u;
        for (const auto& n : R.unmatched) u += (u.empty() ? "" : ", ") + n;
        kv("dev.sum.unmatched", xlsx::Cell::s(u, 7));
    }
    if (!R.elements.empty()) {
        auto& e = wb.add(tr("dev.sheet.elements").substr(0, 31));
        e.add({xlsx::Cell::s(tr("dev.col.ref"), 1), xlsx::Cell::s(tr("dev.col.n"), 1), xlsx::Cell::s(tr("dev.col.inclD"), 1),
               xlsx::Cell::s(tr("dev.col.inclM"), 1), xlsx::Cell::s(tr("dev.col.azD"), 1), xlsx::Cell::s(tr("dev.col.azM"), 1),
               xlsx::Cell::s(tr("dev.col.max"), 1)});
        e.widths = {24, 8, 14, 14, 14, 14, 14};
        for (const auto& el : R.elements)
            e.add({xlsx::Cell::s(el.ref), xlsx::Cell::n(el.n, 5), xlsx::Cell::n(el.designIncl, 2), xlsx::Cell::n(el.measIncl, 2),
                   xlsx::Cell::n(el.designAz, 2), xlsx::Cell::n(el.measAz, 2), xlsx::Cell::n(el.maxDev * 1000, el.maxDev <= prm.tol + 1e-9 ? 5 : 6)});
    }
    return wb;
}

std::string toCsv(const DevResult& R, const std::function<std::string(const std::string&)>& tr, bool geo, char d) {
    const auto cols = columns(R.kind, geo);
    std::string s = "\xEF\xBB\xBF\xE2\x84\x96";   // BOM + №
    for (const auto& c : cols) s += d + tr(c.key);
    s += "\r\n";
    int i = 0;
    for (const auto& r : R.rows) {
        s += std::to_string(++i);
        for (const auto& c : cols) {
            std::string v = c.txt(r);
            const std::string ck = c.key;
            if (d == ';' && ck != "dev.col.name" && ck != "dev.col.ref" && ck != "dev.col.status" && ck != "dev.col.bp")
                for (auto& ch : v)
                    if (ch == '.') ch = ',';
            s += d + v;
        }
        s += "\r\n";
    }
    return s;
}

} // namespace asiacad::civil
