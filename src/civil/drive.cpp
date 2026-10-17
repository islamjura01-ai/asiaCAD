#include <asiacad/civil/drive.h>

#include <asiacad/core/entities.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <unordered_map>

namespace asiacad::civil {

namespace {
Vec3 crossV(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 unit(const Vec3& v) {
    const double l = v.length();
    return l < 1e-15 ? Vec3{1, 0, 0} : v / l;
}

// quloq kesish (XY), CCW ga keltirib
std::vector<std::array<int, 3>> triangulate(const std::vector<Vec3>& poly) {
    std::vector<std::array<int, 3>> out;
    const int n = static_cast<int>(poly.size());
    if (n < 3) return out;
    double area = 0;
    for (int i = 0; i < n; ++i) {
        const auto& a = poly[i];
        const auto& b = poly[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) idx[i] = area >= 0 ? i : n - 1 - i;
    auto crossz = [&](int a, int b, int c) {
        return (poly[b].x - poly[a].x) * (poly[c].y - poly[a].y) - (poly[b].y - poly[a].y) * (poly[c].x - poly[a].x);
    };
    auto inside = [&](int p, int a, int b, int c) { return crossz(a, b, p) >= 0 && crossz(b, c, p) >= 0 && crossz(c, a, p) >= 0; };
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool cut = false;
        const int m = static_cast<int>(idx.size());
        for (int i = 0; i < m; ++i) {
            const int a = idx[(i + m - 1) % m], b = idx[i], c = idx[(i + 1) % m];
            if (crossz(a, b, c) <= 1e-12) continue;
            bool ok = true;
            for (int j : idx)
                if (j != a && j != b && j != c && inside(j, a, b, c)) { ok = false; break; }
            if (!ok) continue;
            out.push_back({a, b, c});
            idx.erase(idx.begin() + i);
            cut = true;
            break;
        }
        if (!cut) break;  // buzilgan kontur — qolganini yelpig'ich
    }
    for (std::size_t i = 1; i + 1 < idx.size(); ++i) out.push_back({idx[0], idx[i], idx[i + 1]});
    return out;
}

std::vector<Vec3> pointsOf(const IEntity& e) {
    if (auto* l = dynamic_cast<const LineEntity*>(&e)) return {l->startPoint(), l->endPoint()};
    if (auto* pl = dynamic_cast<const PolylineEntity*>(&e)) {
        auto v = pl->hasBulges() ? pl->tessellated(deg2rad(3)).vertices() : pl->vertices();
        if (pl->isClosed() && !v.empty()) v.push_back(v.front());
        return v;
    }
    if (auto* a = dynamic_cast<const ArcEntity*>(&e)) {
        std::vector<Vec3> v;
        const double L = a->length();
        const int n = std::max(8, static_cast<int>(L / 1.0));
        for (int i = 0; i <= n; ++i) v.push_back(a->pointAtDistance(L * i / n));
        return v;
    }
    if (auto* c = dynamic_cast<const CircleEntity*>(&e)) {
        std::vector<Vec3> v;
        for (int i = 0; i <= 24; ++i) v.push_back(c->pointAtDistance(c->length() * i / 24));
        return v;
    }
    return {};
}

std::uint32_t massColor(double h) {
    // balandlik bo'yicha iliq ranglar (past — qumrang, baland — terakota)
    const double k = std::clamp(h / 60.0, 0.0, 1.0);
    const int r = static_cast<int>(222 - 40 * k), g = static_cast<int>(208 - 90 * k), b = static_cast<int>(176 - 100 * k);
    return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
}
}  // namespace

// ---------------------------------------------------------------- sahna
void DriveScene::addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, std::uint32_t rgb, bool obstacle) {
    tris.push_back({{a, b, c}, rgb, obstacle});
    tris.push_back({{a, c, d}, rgb, obstacle});
}

void DriveScene::addPrism(const std::vector<Vec3>& o0, double z0, double z1, std::uint32_t rgb) {
    std::vector<Vec3> o = o0;
    if (o.size() > 3 && o.front().distanceTo(o.back()) < 1e-9) o.pop_back();
    if (o.size() < 3) return;
    for (std::size_t i = 0; i < o.size(); ++i) {
        const auto& a = o[i];
        const auto& b = o[(i + 1) % o.size()];
        addQuad({a.x, a.y, z0}, {b.x, b.y, z0}, {b.x, b.y, z1}, {a.x, a.y, z1}, rgb);
    }
    for (const auto& t : triangulate(o)) {
        tris.push_back({{Vec3{o[t[0]].x, o[t[0]].y, z1}, Vec3{o[t[1]].x, o[t[1]].y, z1}, Vec3{o[t[2]].x, o[t[2]].y, z1}}, rgb});
        tris.push_back({{Vec3{o[t[0]].x, o[t[0]].y, z0}, Vec3{o[t[2]].x, o[t[2]].y, z0}, Vec3{o[t[1]].x, o[t[1]].y, z0}}, rgb});
    }
}

void DriveScene::append(const DriveScene& o) {
    tris.insert(tris.end(), o.tris.begin(), o.tris.end());
    segs.insert(segs.end(), o.segs.begin(), o.segs.end());
}

BBox DriveScene::bounds() const {
    BBox b;
    for (const auto& t : tris)
        for (const auto& p : t.p) b.expand(p);
    for (const auto& s : segs) {
        b.expand(s.a);
        b.expand(s.b);
    }
    return b;
}

DriveScene sceneFromDocument(const Document& doc) {
    DriveScene sc;
    struct Ring { std::vector<Vec3> pts; std::set<long long> zs; };
    std::unordered_map<std::string, Ring> rings;
    std::function<void(const IEntity&)> visit = [&](const IEntity& e) {
        if (auto* ins = dynamic_cast<const InsertEntity*>(&e)) {
            for (const auto& c : ins->explode()) visit(*c);
            return;
        }
        if (e.layer().rfind("KOR_", 0) == 0) return;  // koridor yuzasi alohida (modeldan) quriladi
        if (auto* pl = dynamic_cast<const PolylineEntity*>(&e); pl && pl->isClosed() && !pl->hasBulges() && pl->vertices().size() >= 3) {
            const auto& v = pl->vertices();
            bool flat = true;
            for (const auto& q : v) flat = flat && std::abs(q.z - v[0].z) < 1e-6;
            if (flat) {
                std::string key;
                for (const auto& q : v) key += std::format("{:.2f},{:.2f};", q.x, q.y);
                auto& r = rings[key];
                if (r.pts.empty()) r.pts = v;
                r.zs.insert(std::llround(v[0].z * 1000));
                if (std::abs(v[0].z) < 1e-9) return;  // 2D konturning o'zi — chizilmaydi
            }
        }
        auto pts = pointsOf(e);
        bool has3d = false;
        for (const auto& q : pts) has3d = has3d || std::abs(q.z) > 1e-9;
        if (!has3d) return;
        // vertikal qirralar prizma bilan beriladi — takrorlanmasin
        if (pts.size() == 2 && std::hypot(pts[0].x - pts[1].x, pts[0].y - pts[1].y) < 1e-6) return;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) sc.segs.push_back({pts[i], pts[i + 1], 0xd8dce4});
    };
    doc.forEach([&](const IEntity& e) { visit(e); });
    for (const auto& [k, r] : rings) {
        if (r.zs.size() < 2) continue;
        const double z0 = *r.zs.begin() / 1000.0, z1 = *r.zs.rbegin() / 1000.0;
        sc.addPrism(r.pts, z0, z1, massColor(z1 - z0));
    }
    // prizmaga aylangan konturlarning chiziqlari olib tashlanadi (yuzalar bor)
    if (!rings.empty()) {
        std::set<std::string> edges;
        for (const auto& [k, r] : rings) {
            if (r.zs.size() < 2) continue;
            for (long long z : r.zs)
                for (std::size_t i = 0; i < r.pts.size(); ++i) {
                    const auto& a = r.pts[i];
                    edges.insert(std::format("{:.2f},{:.2f},{:.3f}", a.x, a.y, z / 1000.0));
                }
        }
        std::erase_if(sc.segs, [&](const DriveScene::Seg& s) {
            return edges.count(std::format("{:.2f},{:.2f},{:.3f}", s.a.x, s.a.y, s.a.z)) &&
                   edges.count(std::format("{:.2f},{:.2f},{:.3f}", s.b.x, s.b.y, s.b.z));
        });
    }
    return sc;
}

DriveScene sceneFromCorridor(const Corridor& c) {
    DriveScene sc;
    if (c.sec.size() < 2) return sc;
    auto W = [](const Section& s, double o, double z) { return Vec3{s.x + s.dy * o, s.y - s.dx * o, z}; };
    auto groundAt = [](const Section& s, double o) {
        const auto& g = s.groundLine;
        if (g.empty()) return s.ground;
        if (o <= g.front().first) return g.front().second;
        for (std::size_t i = 1; i < g.size(); ++i)
            if (o <= g[i].first) {
                const double t = (o - g[i - 1].first) / std::max(1e-9, g[i].first - g[i - 1].first);
                return g[i - 1].second + t * (g[i].second - g[i - 1].second);
            }
        return g.back().second;
    };
    auto matAt = [](const Section& s, double o) -> std::string {
        static const char* pri[] = {"ASPHALT", "SIDEWALK", "CONCRETE", "SHOULDER"};
        for (const char* m : pri)
            for (const auto& sh : s.shapes) {
                if (sh.material != m || sh.poly.empty()) continue;
                double lo = 1e18, hi = -1e18;
                for (const auto& q : sh.poly) { lo = std::min(lo, q.first); hi = std::max(hi, q.first); }
                if (o >= lo - 1e-6 && o <= hi + 1e-6) return m;
            }
        return {};
    };
    auto color = [&](const Section& s, double o, double z) -> std::uint32_t {
        const std::string m = matAt(s, o);
        if (m == "ASPHALT") return 0x4a4d52;
        if (m == "SIDEWALK") return 0xb9b4aa;
        if (m == "CONCRETE") return 0xa9a59c;
        if (m == "SHOULDER") return 0x8d877b;
        return z >= groundAt(s, o) - 0.01 ? 0x6f9a52 : 0x9b7a55;  // to'kma — o't, kesma — tuproq
    };
    // har kesimning ustki chizig'i bir xil sonli nuqtalarga keltiriladi (qiyalik nuqtalari soni farq qilishi mumkin)
    auto resample = [](const Section& s, std::size_t n) {
        std::vector<std::pair<double, double>> v;
        const auto& p = s.pts;
        if (p.size() == n) {
            for (const auto& q : p) v.push_back({q.o, q.z});
            return v;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const double t = n > 1 ? static_cast<double>(i) * (p.size() - 1) / (n - 1) : 0;
            const std::size_t a = std::min(static_cast<std::size_t>(t), p.size() - 1), b = std::min(a + 1, p.size() - 1);
            const double f = t - a;
            v.push_back({p[a].o + f * (p[b].o - p[a].o), p[a].z + f * (p[b].z - p[a].z)});
        }
        return v;
    };
    for (std::size_t i = 0; i + 1 < c.sec.size(); ++i) {
        const auto& A = c.sec[i];
        const auto& B = c.sec[i + 1];
        if (A.pts.size() < 2 || B.pts.size() < 2) continue;
        const std::size_t n = std::max(A.pts.size(), B.pts.size());
        const auto a = resample(A, n), b = resample(B, n);
        for (std::size_t k = 0; k + 1 < n; ++k) {
            const double om = (a[k].first + a[k + 1].first) / 2, zm = (a[k].second + a[k + 1].second) / 2;
            sc.addQuad(W(A, a[k].first, a[k].second), W(A, a[k + 1].first, a[k + 1].second), W(B, b[k + 1].first, b[k + 1].second),
                       W(B, b[k].first, b[k].second), color(A, om, zm));
        }
        // suv yuzasi
        if (A.water > 0 && B.water > 0) {
            auto ext = [](const Section& s, double& lo, double& hi) {
                lo = 1e18; hi = -1e18;
                for (const auto& sh : s.shapes)
                    if (sh.material == "WATER")
                        for (const auto& q : sh.poly) { lo = std::min(lo, q.first); hi = std::max(hi, q.first); }
                return lo < hi;
            };
            double l0, h0, l1, h1;
            if (ext(A, l0, h0) && ext(B, l1, h1))
                sc.addQuad(W(A, l0, A.water), W(A, h0, A.water), W(B, h1, B.water), W(B, l1, B.water), 0x3f7fbf, false);
        }
        // yo'l chiziqlari: o'q (uzuq) va asfalt chetlari
        auto edge = [&](const Section& s, bool hiSide, double& o, double& z) {
            double lo = 1e18, hi = -1e18;
            for (const auto& sh : s.shapes)
                if (sh.material == "ASPHALT")
                    for (const auto& q : sh.poly) { lo = std::min(lo, q.first); hi = std::max(hi, q.first); }
            if (lo > hi) return false;
            o = hiSide ? hi - 0.15 : lo + 0.15;
            z = -1e18;
            for (std::size_t k = 0; k + 1 < s.pts.size(); ++k)
                if (o >= s.pts[k].o && o <= s.pts[k + 1].o) {
                    const double t = (o - s.pts[k].o) / std::max(1e-9, s.pts[k + 1].o - s.pts[k].o);
                    z = s.pts[k].z + t * (s.pts[k + 1].z - s.pts[k].z);
                }
            return z > -1e17;
        };
        for (bool side : {false, true}) {
            double oa, za, ob, zb;
            if (edge(A, side, oa, za) && edge(B, side, ob, zb)) sc.segs.push_back({W(A, oa, za + 0.02), W(B, ob, zb + 0.02), 0xf2f2f2});
        }
        if (!matAt(A, 0).empty() && matAt(A, 0) == "ASPHALT" && (i % 2 == 0))
            sc.segs.push_back({W(A, 0, A.design + 0.02), W(B, 0, B.design + 0.02), 0xf2f2f2});
    }
    return sc;
}

bool corridorSurfaceZ(const Corridor& c, double x, double y, double& z) {
    if (c.sec.size() < 2) return false;
    // nuqtaning o'qdagi proyeksiyasi: eng yaqin ikki qo'shni kesim
    std::size_t best = 0;
    double bd = 1e18;
    for (std::size_t i = 0; i < c.sec.size(); ++i) {
        const double d = std::hypot(c.sec[i].x - x, c.sec[i].y - y);
        if (d < bd) { bd = d; best = i; }
    }
    auto zAtSec = [&](const Section& s, double& out) {
        const double o = (x - s.x) * s.dy - (y - s.y) * s.dx;  // W(): x + dy*o, y - dx*o
        const auto& p = s.pts;
        if (p.size() < 2 || o < p.front().o - 1e-6 || o > p.back().o + 1e-6) return false;
        for (std::size_t k = 0; k + 1 < p.size(); ++k)
            if (o >= p[k].o - 1e-9 && o <= p[k + 1].o + 1e-9) {
                const double t = (o - p[k].o) / std::max(1e-9, p[k + 1].o - p[k].o);
                out = p[k].z + t * (p[k + 1].z - p[k].z);
                return true;
            }
        return false;
    };
    const auto& S = c.sec[best];
    const double along = (x - S.x) * S.dx + (y - S.y) * S.dy;
    const std::size_t nb = along >= 0 ? std::min(best + 1, c.sec.size() - 1) : (best ? best - 1 : 0);
    double za, zb;
    const bool oa = zAtSec(S, za), ob = nb != best && zAtSec(c.sec[nb], zb);
    if (!oa && !ob) return false;
    if (oa && ob) {
        const double L = std::max(1e-9, std::abs(c.sec[nb].s - S.s));
        const double t = std::clamp(std::abs(along) / L, 0.0, 1.0);
        z = za + t * (zb - za);
    } else z = oa ? za : zb;
    return true;
}

DriveScene groundAround(const std::vector<Vec3>& path, const std::function<double(double, double)>& groundAt, double hw, double step) {
    DriveScene sc;
    if (path.size() < 2 || !groundAt) return sc;
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const auto& p : path) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    x0 -= hw; y0 -= hw; x1 += hw; y1 += hw;
    step = std::max(step, std::max(x1 - x0, y1 - y0) / 160);  // to'r 160×160 dan oshmasin
    const int nx = static_cast<int>(std::ceil((x1 - x0) / step)), ny = static_cast<int>(std::ceil((y1 - y0) / step));
    // faqat yo'lga yaqin kataklar
    auto nearPath = [&](double x, double y) {
        for (std::size_t i = 0; i + 1 < path.size(); i += std::max<std::size_t>(1, path.size() / 400)) {
            const auto& a = path[i];
            const auto& b = path[std::min(path.size() - 1, i + std::max<std::size_t>(1, path.size() / 400))];
            const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
            const double t = l2 > 0 ? std::clamp(((x - a.x) * dx + (y - a.y) * dy) / l2, 0.0, 1.0) : 0;
            if (std::hypot(a.x + t * dx - x, a.y + t * dy - y) <= hw + step) return true;
        }
        return false;
    };
    std::vector<double> z((nx + 1) * (ny + 1));
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i) z[j * (nx + 1) + i] = groundAt(x0 + i * step, y0 + j * step);
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const double cx = x0 + (i + 0.5) * step, cy = y0 + (j + 0.5) * step;
            if (!nearPath(cx, cy)) continue;
            auto P = [&](int a, int b) { return Vec3{x0 + a * step, y0 + b * step, z[b * (nx + 1) + a] - 0.05}; };  // yo'l yuzasi ustida qolsin
            sc.addQuad(P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1), ((i + j) % 2) ? 0x7a9a5c : 0x75955a, false);
        }
    return sc;
}

DriveScene drapeLinework(const Document& doc, const std::vector<Vec3>& path, double radius, const std::function<double(double, double)>& zf) {
    DriveScene sc;
    if (path.size() < 2 || !zf) return sc;
    // yo'l atrofi: siyrak nuqtalar
    std::vector<Vec3> ps;
    for (std::size_t i = 0; i < path.size(); i += std::max<std::size_t>(1, path.size() / 300)) ps.push_back(path[i]);
    ps.push_back(path.back());
    auto near = [&](const Vec3& q) {
        for (const auto& p : ps)
            if (std::hypot(p.x - q.x, p.y - q.y) <= radius) return true;
        return false;
    };
    auto surfaceLayer = [](const std::string& l) {
        std::string u;
        for (char c : l) u += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        for (const char* k : {"YUZA", "ASFALT", "ASPHALT", "SURFACE", "TROTUAR", "PAVEMENT"})
            if (u.find(k) != std::string::npos) return true;
        return false;
    };
    std::function<void(const IEntity&)> visit = [&](const IEntity& e) {
        if (auto* ins = dynamic_cast<const InsertEntity*>(&e)) {
            for (const auto& c : ins->explode()) visit(*c);
            return;
        }
        auto pts = pointsOf(e);
        if (pts.size() < 2) return;
        for (const auto& q : pts)
            if (std::abs(q.z) > 1e-9) return;  // 3D — sceneFromDocument da
        bool any = false;
        for (const auto& q : pts) any = any || near(q);
        if (!any) return;
        if (auto* pl = dynamic_cast<const PolylineEntity*>(&e); pl && pl->isClosed() && surfaceLayer(e.layer())) {
            std::vector<Vec3> o(pts.begin(), pts.end() - 1);
            for (const auto& t : triangulate(o)) {
                std::array<Vec3, 3> tri;
                for (int k = 0; k < 3; ++k) tri[k] = {o[t[k]].x, o[t[k]].y, zf(o[t[k]].x, o[t[k]].y) + 0.01};
                sc.tris.push_back({tri, 0x4a4d52, false});
            }
            return;
        }
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const Vec3 a = pts[i], b = pts[i + 1];
            const int n = std::max(1, static_cast<int>(std::hypot(b.x - a.x, b.y - a.y) / 10));
            Vec3 prev{a.x, a.y, zf(a.x, a.y) + 0.04};
            for (int k = 1; k <= n; ++k) {
                const Vec3 q = a + (b - a) * (static_cast<double>(k) / n);
                const Vec3 cur{q.x, q.y, zf(q.x, q.y) + 0.04};
                sc.segs.push_back({prev, cur, 0xe6e6e0});
                prev = cur;
            }
        }
    };
    doc.forEach([&](const IEntity& e) { visit(e); });
    return sc;
}

// ---------------------------------------------------------------- yo'l
bool curveHasZ(const IEntity& e) {
    for (const auto& q : pointsOf(e))
        if (std::abs(q.z) > 1e-9) return true;
    return false;
}

DrivePath makeDrivePath(const std::vector<Vec3>& pts0, double step) {
    DrivePath d;
    std::vector<Vec3> pts;
    for (const auto& q : pts0)
        if (pts.empty() || std::hypot(q.x - pts.back().x, q.y - pts.back().y) > 1e-6) pts.push_back(q);
    if (pts.size() < 2) return d;
    d.p.push_back(pts[0]);
    d.s.push_back(0);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const Vec3 a = pts[i - 1], b = pts[i];
        const double L = std::hypot(b.x - a.x, b.y - a.y);
        const int n = std::max(1, static_cast<int>(std::ceil(L / step)));
        for (int k = 1; k <= n; ++k) {
            const double t = static_cast<double>(k) / n;
            d.p.push_back(a + (b - a) * t);
            d.s.push_back(d.s.back() + L / n);
        }
    }
    return d;
}

DrivePath makeDrivePath(const IEntity& curve, double step, const std::function<double(double, double)>& zFn) {
    auto pts = pointsOf(curve);
    if (zFn)
        for (auto& q : pts) q.z = zFn(q.x, q.y);
    DrivePath d = makeDrivePath(pts, step);
    if (zFn)
        for (auto& q : d.p) q.z = zFn(q.x, q.y);
    return d;
}

Vec3 DrivePath::at(double st) const {
    if (p.empty()) return {};
    if (st <= 0) return p.front();
    if (st >= s.back()) return p.back();
    const auto it = std::upper_bound(s.begin(), s.end(), st);
    const std::size_t i = static_cast<std::size_t>(it - s.begin());
    const double t = (st - s[i - 1]) / std::max(1e-12, s[i] - s[i - 1]);
    return p[i - 1] + (p[i] - p[i - 1]) * t;
}

Vec3 DrivePath::dir(double st, double ds) const {
    const double L = length();
    double a = std::clamp(st - ds / 2, 0.0, L), b = std::clamp(st + ds / 2, 0.0, L);
    if (b - a < 1e-9) { a = std::max(0.0, L - ds); b = L; }
    return unit(at(b) - at(a));
}

double DrivePath::grade(double st) const {
    const double L = length();
    const double a = std::clamp(st - 5, 0.0, L), b = std::clamp(st + 5, 0.0, L);
    return b - a > 1e-6 ? (zAt(b) - zAt(a)) / (b - a) : 0;
}

DriveCamera driveCamera(const DrivePath& p, double st, double eyeH, double lookAhead) {
    DriveCamera c;
    const Vec3 a = p.at(st);
    c.eye = {a.x, a.y, a.z + eyeH};
    const double L = p.length();
    if (st + lookAhead <= L) {
        const Vec3 b = p.at(st + lookAhead);
        c.target = {b.x, b.y, b.z + eyeH};
    } else {
        const Vec3 d = p.dir(st);
        c.target = c.eye + d * lookAhead;
    }
    return c;
}

// ---------------------------------------------------------------- ko'rinish masofasi
double requiredStoppingSight(double v) {
    // SNiP 2.05.02-85, 10-jadval: hisobiy tezlik (km/soat) -> to'xtash uchun ko'rinish masofasi (m). Tasdiqlanmagan.
    static const double T[][2] = {{20, 25}, {30, 45}, {40, 55}, {50, 75}, {60, 85}, {80, 150}, {100, 200}, {120, 250}, {150, 300}};
    if (v <= T[0][0]) return T[0][1];
    for (std::size_t i = 1; i < std::size(T); ++i)
        if (v <= T[i][0]) return T[i - 1][1] + (v - T[i - 1][0]) / (T[i][0] - T[i - 1][0]) * (T[i][1] - T[i - 1][1]);
    return T[std::size(T) - 1][1];
}

namespace {
// XY to'r — nur bo'ylab uchburchaklarni tez topish
struct Grid {
    double x0 = 0, y0 = 0, cell = 20;
    int nx = 0, ny = 0;
    std::vector<std::vector<int>> cells;
    const DriveScene* sc = nullptr;
    void build(const DriveScene& s, double c) {
        sc = &s;
        cell = c;
        BBox b;
        for (const auto& t : s.tris)
            if (t.obstacle)
                for (const auto& p : t.p) b.expand(p);
        if (!b.valid()) return;
        x0 = b.min.x; y0 = b.min.y;
        nx = std::max(1, static_cast<int>(std::ceil((b.max.x - x0) / cell)) + 1);
        ny = std::max(1, static_cast<int>(std::ceil((b.max.y - y0) / cell)) + 1);
        if (static_cast<double>(nx) * ny > 4e6) {  // juda katta — kattaroq katak
            const double k = std::sqrt(static_cast<double>(nx) * ny / 4e6);
            cell *= k;
            nx = static_cast<int>(nx / k) + 2;
            ny = static_cast<int>(ny / k) + 2;
        }
        cells.assign(static_cast<std::size_t>(nx) * ny, {});
        for (std::size_t i = 0; i < s.tris.size(); ++i) {
            const auto& t = s.tris[i];
            if (!t.obstacle) continue;
            double a = 1e18, bb = 1e18, c2 = -1e18, d = -1e18;
            for (const auto& p : t.p) { a = std::min(a, p.x); bb = std::min(bb, p.y); c2 = std::max(c2, p.x); d = std::max(d, p.y); }
            const int i0 = std::clamp(static_cast<int>((a - x0) / cell), 0, nx - 1), i1 = std::clamp(static_cast<int>((c2 - x0) / cell), 0, nx - 1);
            const int j0 = std::clamp(static_cast<int>((bb - y0) / cell), 0, ny - 1), j1 = std::clamp(static_cast<int>((d - y0) / cell), 0, ny - 1);
            for (int j = j0; j <= j1; ++j)
                for (int ii = i0; ii <= i1; ++ii) cells[static_cast<std::size_t>(j) * nx + ii].push_back(static_cast<int>(i));
        }
    }
    // Möller–Trumbore: segment a→b (0 < t < 1) uchburchakni kesadimi
    static bool hit(const Vec3& o, const Vec3& d, const std::array<Vec3, 3>& t) {
        const Vec3 e1 = t[1] - t[0], e2 = t[2] - t[0];
        const Vec3 pv = crossV(d, e2);
        const double det = e1.dot(pv);
        if (std::abs(det) < 1e-12) return false;
        const double inv = 1 / det;
        const Vec3 tv = o - t[0];
        const double u = tv.dot(pv) * inv;
        if (u < 0 || u > 1) return false;
        const Vec3 qv = crossV(tv, e1);
        const double v = d.dot(qv) * inv;
        if (v < 0 || u + v > 1) return false;
        const double tt = e2.dot(qv) * inv;
        return tt > 1e-4 && tt < 1 - 1e-4;
    }
    bool blocked(const Vec3& a, const Vec3& b, std::vector<int>& stamp, int& gen) const {
        if (cells.empty()) return false;
        ++gen;
        const Vec3 d = b - a;
        const double L = std::hypot(d.x, d.y);
        const int n = std::max(1, static_cast<int>(std::ceil(L / (cell * 0.5))));
        int lastC = -1;
        for (int k = 0; k <= n; ++k) {
            const double t = static_cast<double>(k) / n;
            const int ci = static_cast<int>((a.x + d.x * t - x0) / cell), cj = static_cast<int>((a.y + d.y * t - y0) / cell);
            if (ci < 0 || cj < 0 || ci >= nx || cj >= ny) continue;
            const int c = cj * nx + ci;
            if (c == lastC) continue;
            lastC = c;
            for (int ti : cells[static_cast<std::size_t>(c)]) {
                if (stamp[static_cast<std::size_t>(ti)] == gen) continue;
                stamp[static_cast<std::size_t>(ti)] = gen;
                if (hit(a, d, sc->tris[static_cast<std::size_t>(ti)].p)) return true;
            }
        }
        return false;
    }
};
}  // namespace

SightResult sightAnalysis(const DrivePath& path, const DriveScene& scene, const SightParams& sp) {
    SightResult r;
    r.required = requiredStoppingSight(sp.speedKmh);
    const double L = path.length();
    if (L <= 0) return r;
    Grid g;
    if (sp.obstacles3d) g.build(scene, 20);
    std::vector<int> stamp(scene.tris.size(), 0);
    int gen = 0;
    const double ds = std::max(0.5, std::min(2.0, sp.step / 4));
    r.minD = 1e18;
    for (double s0 = 0; s0 <= L + 1e-6; s0 += sp.step) {
        SightSample smp;
        smp.s = std::min(s0, L);
        smp.grade = path.grade(smp.s);
        r.maxGrade = std::max(r.maxGrade, std::abs(smp.grade));
        const double ze = path.zAt(smp.s) + sp.eyeH;
        // profil: ko'z -> nishon nuri yo'l profilidan yuqorida qoladimi (eng katta burchak usuli)
        double maxSlope = -1e18, prof = std::min(sp.maxD, L - smp.s);
        for (double d = ds; smp.s + d <= L + 1e-9 && d <= sp.maxD; d += ds) {
            const double zt = path.zAt(smp.s + d);
            const double objSlope = (zt + sp.objH - ze) / d;
            if (objSlope < maxSlope - 1e-9) { prof = d - ds; break; }
            maxSlope = std::max(maxSlope, (zt - ze) / d);
        }
        smp.profile = std::max(0.0, prof);
        smp.total = smp.profile;
        if (sp.obstacles3d && !g.cells.empty()) {
            // 3D: nishon yo'l bo'ylab; birinchi to'silgan masofa (qadam 5 m, keyin aniqlashtirish)
            const Vec3 pe = path.at(smp.s);
            const Vec3 eye{pe.x, pe.y, ze};
            const double lim = smp.profile;
            double free = lim;
            for (double d = 5; d <= lim + 1e-9; d += 5) {
                const Vec3 pt = path.at(smp.s + d);
                if (g.blocked(eye, {pt.x, pt.y, pt.z + sp.objH}, stamp, gen)) {
                    double lo = d - 5, hi = d;  // ikkiga bo'lish
                    for (int it = 0; it < 6; ++it) {
                        const double m = (lo + hi) / 2;
                        const Vec3 q = path.at(smp.s + m);
                        if (g.blocked(eye, {q.x, q.y, q.z + sp.objH}, stamp, gen)) hi = m;
                        else lo = m;
                    }
                    free = lo;
                    break;
                }
            }
            smp.total = std::min(smp.profile, free);
        }
        // yo'l oxiriga yaqin — masofa yo'l tugashi bilan cheklangan, kamchilik hisoblanmaydi
        const bool endLimited = L - smp.s < r.required;
        if (!endLimited && smp.total < r.minD) { r.minD = smp.total; r.minAt = smp.s; }
        if (!endLimited && smp.total < r.required) {
            if (!r.issues.empty() && smp.s - r.issues.back().s1 <= sp.step + 1e-6) {
                r.issues.back().s1 = smp.s;
                r.issues.back().minD = std::min(r.issues.back().minD, smp.total);
            } else {
                r.issues.push_back({smp.s, smp.s, smp.total});
            }
        }
        r.samples.push_back(smp);
    }
    if (r.minD > 1e17) r.minD = std::min(sp.maxD, L);
    return r;
}

} // namespace asiacad::civil
