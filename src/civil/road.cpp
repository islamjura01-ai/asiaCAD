#include <asiacad/civil/road.h>

#include <asiacad/civil/utility.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/json.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>

namespace asiacad::civil {

namespace {
constexpr double kPI = 3.14159265358979323846;
std::string fc(double v, int dec = 2) {
    std::string s = std::format("{:.{}f}", v, dec);
    for (auto& c : s) if (c == '.') c = ',';
    return s;
}
struct AxisPt {
    double x, y, z;
    bool arc;
    double radius;
};

// Poliliniyani nuqtalarga (yoylar ~2 m qadam bilan)
std::vector<AxisPt> sampleCurve(const IEntity& e) {
    std::vector<AxisPt> out;
    if (auto* l = dynamic_cast<const LineEntity*>(&e)) {
        out.push_back({l->startPoint().x, l->startPoint().y, l->startPoint().z, false, 0});
        out.push_back({l->endPoint().x, l->endPoint().y, l->endPoint().z, false, 0});
        return out;
    }
    auto* pl = dynamic_cast<const PolylineEntity*>(&e);
    if (!pl) return out;
    const auto& v = pl->vertices();
    const auto& b = pl->bulges();
    const std::size_t nseg = pl->segmentCount();
    for (std::size_t i = 0; i < nseg; ++i) {
        const Vec3 a = v[i], c = v[(i + 1) % v.size()];
        if (out.empty()) out.push_back({a.x, a.y, a.z, false, 0});
        const double bu = i < b.size() ? b[i] : 0;
        if (std::abs(bu) > 1e-9) {
            const double chord = std::hypot(c.x - a.x, c.y - a.y);
            const double th = 4 * std::atan(bu);
            const double R = chord / (2 * std::sin(std::abs(th) / 2));
            // markaz
            const double mx = (a.x + c.x) / 2, my = (a.y + c.y) / 2;
            const double ux = (c.x - a.x) / chord, uy = (c.y - a.y) / chord;
            const double h = chord / 2 * (1 - bu * bu) / (2 * bu);
            const double cx = mx - uy * h, cy = my + ux * h;
            const double a0 = std::atan2(a.y - cy, a.x - cx);
            const int n = std::max(2, static_cast<int>(std::ceil(R * std::abs(th) / 2.0)));
            for (int k = 1; k <= n; ++k) {
                const double ang = a0 + th * k / n;
                const double z = a.z + (c.z - a.z) * k / n;
                out.push_back({cx + R * std::cos(ang), cy + R * std::sin(ang), z, k < n, R});
            }
            out.back().arc = false;
        } else {
            out.push_back({c.x, c.y, c.z, false, 0});
        }
    }
    return out;
}
} // namespace

const char* roadLayerKey(RoadLayer k) {
    switch (k) {
        case RoadLayer::Axis: return "AXIS";
        case RoadLayer::Ground: return "GROUND";
        case RoadLayer::Design: return "DESIGN";
        case RoadLayer::Ignore: return "IGNORE";
    }
    return "IGNORE";
}
bool roadLayerFromKey(const std::string& s, RoadLayer& out) {
    for (RoadLayer k : {RoadLayer::Axis, RoadLayer::Ground, RoadLayer::Design, RoadLayer::Ignore})
        if (s == roadLayerKey(k)) { out = k; return true; }
    return false;
}
RoadLayer guessRoadLayer(const std::string& layer) {
    const std::string u = utf8::toUpper(layer);
    auto has = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys)
            if (u.find(utf8::toUpper(k)) != std::string::npos) return true;
        return false;
    };
    if (has({"O'Q", "OʻQ", "O`Q", "OQ_", "_OQ", "ОСЬ", "AXIS", "CENTER", "EKSEN", "ЎҚ", "CL_", "_CL"}) || u == "OQ" || u == "CL") return RoadLayer::Axis;
    if (has({"LOYIHA", "ЛОЙИҲА", "ПРОЕКТ", "DESIGN", "PROJE", "КРАСН", "QIZIL"})) return RoadLayer::Design;
    if (has({"YER_SATH", "YER SATH", "YERSATH", "REL'EF", "RELYEF", "RELEF", "РЕЛЬЕФ", "GORIZONTAL", "ГОРИЗОНТАЛ", "CONTOUR", "GROUND",
             "TERRAIN", "TOPO", "ЧЁРН", "ЧЕРН", "ZEMIN", "ZEMİN"}))
        return RoadLayer::Ground;
    return RoadLayer::Ignore;
}

std::vector<RoadCategory> roadCategories() {
    // SNiP 2.05.02-85: 4-jadval (polosalar, kenglik), 10-jadval (eng kichik radius, eng katta nishablik) — tasdiqlanmagan
    return {{"I-a", 150, 4, 3.75, 3.75, 0.75, 0.030, 1200},
            {"I-b", 120, 4, 3.75, 3.75, 0.75, 0.040, 800},
            {"II", 120, 2, 3.75, 3.75, 0.75, 0.040, 800},
            {"III", 100, 2, 3.50, 2.50, 0.50, 0.050, 600},
            {"IV", 80, 2, 3.00, 2.00, 0.50, 0.060, 300},
            {"V", 60, 1, 4.50, 1.75, 0.00, 0.070, 150}};
}

RoadParams defaultRoadParams() {
    RoadParams p;
    p.cat = roadCategories()[3];
    p.pavement = {{"road.pav.ab_fine", 0.05, true}, {"road.pav.ab_coarse", 0.07, true}, {"road.pav.crushed", 0.20, false},
                  {"road.pav.sandgravel", 0.30, false}};
    return p;
}

int RoadModel::errors() const {
    int n = 0;
    for (const auto& i : issues) n += i.error;
    return n;
}

std::string pk(double s) {
    const int h = static_cast<int>(std::floor(s / 100 + 1e-9));
    std::string r = std::format("{:05.2f}", std::max(0.0, s - h * 100.0));
    for (auto& c : r) if (c == '.') c = ',';
    return std::format("PK{}+{}", h, r);
}

std::map<std::string, RoadLayer> autoRoadLayers(const Document& doc) {
    std::map<std::string, RoadLayer> m;
    for (const auto& [name, l] : doc.layers()) m[name] = guessRoadLayer(name);
    return m;
}

namespace {
double groundAt(const std::vector<std::array<double, 3>>& g, double x, double y, double def) {
    if (g.empty()) return def;
    constexpr std::size_t K = 8;
    std::pair<double, double> best[K];
    std::size_t n = 0;
    for (const auto& p : g) {
        const double d = std::hypot(p[0] - x, p[1] - y);
        if (d < 1e-6) return p[2];
        if (n == K && d >= best[K - 1].first) continue;
        std::size_t k = n < K ? n++ : K - 1;
        while (k > 0 && best[k - 1].first > d) { best[k] = best[k - 1]; --k; }
        best[k] = {d, p[2]};
    }
    double sw = 0, sz = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 1 / (best[i].first * best[i].first);
        sw += w;
        sz += w * best[i].second;
    }
    return sz / sw;
}
} // namespace

void recomputeRoad(RoadModel& m, const RoadParams& p) {
    m.issues.clear();
    m.cutVolume = m.fillVolume = m.asphaltArea = 0;
    m.pavementVolumes.clear();
    auto issue = [&](bool e, const char* k, std::vector<std::string> a, double s) { m.issues.push_back({e, k, std::move(a), s}); };
    if (m.st.size() < 2) {
        issue(true, "road.chk.no_axis", {}, 0);
        return;
    }
    const double wc = p.cat.lanes * p.cat.laneWidth / 2, ws = p.cat.shoulder;
    double pavT = 0;
    for (const auto& l : p.pavement) pavT += l.thickness;
    const double wPav = wc + p.cat.strengthened;
    for (auto& s : m.st) {
        const double nx = -s.dy, ny = s.dx;  // chap normal
        auto g = [&](double o) { return groundAt(m.groundPts, s.x + nx * o, s.y + ny * o, p.ground); };
        s.ground = g(0);
        auto side = [&](int sign) {
            std::vector<std::pair<double, double>> pts;
            const double ze = s.design - wc * p.crossfall, zs = ze - ws * p.shoulderFall;
            pts.push_back({sign * wc, ze});
            pts.push_back({sign * (wc + ws), zs});
            const double o0 = wc + ws;
            const bool fill = zs >= g(sign * o0);
            const double m_ = fill ? p.slopeFill : p.slopeCut;
            double o = o0, z = zs, prevDiff = zs - g(sign * o0);
            bool hit = false;
            for (int k = 1; k <= 400 && !hit; ++k) {
                const double on = o0 + 0.25 * k;
                const double zn = fill ? zs - (on - o0) / m_ : zs + (on - o0) / m_;
                const double diff = zn - g(sign * on);
                if ((fill && diff <= 0) || (!fill && diff >= 0)) {
                    const double t = prevDiff / (prevDiff - diff);
                    o = o + t * (on - o);
                    z = z + t * (zn - z);
                    hit = true;
                } else {
                    o = on;
                    z = zn;
                    prevDiff = diff;
                }
            }
            pts.push_back({sign * o, z});
            return pts;
        };
        auto L = side(1), R = side(-1);
        s.section.clear();
        for (auto it = L.rbegin(); it != L.rend(); ++it) s.section.push_back(*it);
        s.section.push_back({0, s.design});
        for (const auto& q : R) s.section.push_back(q);
        // to'kma/kesma maydon (yo'l to'shamasi tagidan)
        auto surf = [&](double o) {
            for (std::size_t i = 0; i + 1 < s.section.size(); ++i) {
                const auto& a = s.section[i];
                const auto& b = s.section[i + 1];
                if ((o <= a.first && o >= b.first) || (o >= a.first && o <= b.first)) {
                    const double t = std::abs(b.first - a.first) < 1e-12 ? 0 : (o - a.first) / (b.first - a.first);
                    return a.second + t * (b.second - a.second);
                }
            }
            return s.design;
        };
        s.cutArea = s.fillArea = 0;
        const double oL = s.section.front().first, oR = s.section.back().first;
        const double d = 0.25;
        for (double o = oR; o < oL - 1e-9; o += d) {
            const double om = o + d / 2;
            const double sub = surf(om) - (std::abs(om) <= wPav ? pavT : 0);
            const double diff = sub - g(om);
            if (diff > 0) s.fillArea += diff * d;
            else s.cutArea += -diff * d;
        }
    }
    // nishabliklar, hajmlar
    for (std::size_t i = 0; i + 1 < m.st.size(); ++i) {
        auto& a = m.st[i];
        const auto& b = m.st[i + 1];
        const double ds = b.s - a.s;
        a.grade = ds > 1e-9 ? (b.design - a.design) / ds : 0;
        m.cutVolume += (a.cutArea + b.cutArea) / 2 * ds;
        m.fillVolume += (a.fillArea + b.fillArea) / 2 * ds;
    }
    m.st.back().grade = m.st.size() >= 2 ? m.st[m.st.size() - 2].grade : 0;
    m.length = m.st.back().s - m.st.front().s;
    for (const auto& l : p.pavement) {
        m.pavementVolumes.push_back({l.name, l.thickness * 2 * wPav * m.length});
        if (l.asphalt && m.asphaltArea == 0) m.asphaltArea = 2 * wPav * m.length;
    }
    // ---- tekshiruv
    if (m.groundPoints == 0) issue(true, "road.chk.no_ground", {fc(p.ground)}, 0);
    if (m.designPoints == 0) issue(false, "road.chk.no_design", {fc(p.raise)}, 0);
    for (std::size_t i = 0; i + 1 < m.st.size();) {
        if (std::abs(m.st[i].grade) > p.cat.maxGrade + 1e-9) {
            std::size_t j = i;
            double worst = 0;
            while (j + 1 < m.st.size() && std::abs(m.st[j].grade) > p.cat.maxGrade + 1e-9) worst = std::max(worst, std::abs(m.st[j++].grade));
            issue(true, "road.chk.grade", {pk(m.st[i].s), pk(m.st[j].s), fc(worst * 1000, 0), fc(p.cat.maxGrade * 1000, 0)}, m.st[i].s);
            i = j;
        } else {
            ++i;
        }
    }
    for (const auto& c : m.curves)
        if (c.radius + 1e-6 < p.cat.minRadius)
            issue(true, "road.chk.radius", {pk(c.s0), fc(c.radius, 0), fc(p.cat.minRadius, 0)}, c.s0);
    // burchak nuqtalar (yoysiz)
    for (std::size_t i = 1; i + 1 < m.st.size(); ++i) {
        const auto& a = m.st[i - 1];
        const auto& b = m.st[i];
        bool vertex = false;
        for (double vs : m.vertexS) vertex |= std::abs(vs - b.s) < 1e-6;
        if (!vertex) continue;
        const auto& c = m.st[i + 1];
        const double a1 = std::atan2(b.y - a.y, b.x - a.x), a2 = std::atan2(c.y - b.y, c.x - b.x);
        double d = std::abs(a2 - a1) * 180 / kPI;
        if (d > 180) d = 360 - d;
        if (d > 1.0) issue(true, "road.chk.nocurve", {pk(b.s), fc(d, 1)}, b.s);
    }
    // to'kma balandligi katta (> 6 m) — ogohlantirish
    for (const auto& s : m.st)
        if (s.design - s.ground > 6) { issue(false, "road.chk.highfill", {pk(s.s), fc(s.design - s.ground)}, s.s); break; }
}

RoadModel buildRoad(const Document& doc, const std::map<std::string, RoadLayer>& layers, const RoadParams& p) {
    RoadModel m;
    auto kindOf = [&](const std::string& l) {
        auto it = layers.find(l);
        return it != layers.end() ? it->second : guessRoadLayer(l);
    };
    std::vector<std::vector<AxisPt>> chains;
    struct DLabel { double x, y, z; };
    std::vector<DLabel> dl;
    std::function<void(const IEntity&)> visit = [&](const IEntity& e) {
        if (auto* ins = dynamic_cast<const InsertEntity*>(&e)) {
            for (const auto& c : ins->explode()) visit(*c);
            for (const auto& a : ins->attributes()) visit(a.text);
            return;
        }
        const RoadLayer k = kindOf(e.layer());
        if (k == RoadLayer::Ignore) return;
        if (auto* t = dynamic_cast<const TextEntity*>(&e)) {
            double v;
            if (parseUtilLabel(t->text(), v) != 'z') return;
            if (k == RoadLayer::Ground) m.groundPts.push_back({t->insertion().x, t->insertion().y, v});
            else dl.push_back({t->insertion().x, t->insertion().y, v});
            return;
        }
        if (k == RoadLayer::Ground) {
            if (auto* pt = dynamic_cast<const PointEntity*>(&e)) m.groundPts.push_back({pt->position().x, pt->position().y, pt->position().z});
            else if (auto* pl = dynamic_cast<const PolylineEntity*>(&e))
                for (const auto& q : pl->vertices())
                    if (std::abs(q.z) > 1e-9) m.groundPts.push_back({q.x, q.y, q.z});
            return;
        }
        if (k == RoadLayer::Axis) {
            auto pts = sampleCurve(e);
            if (pts.size() >= 2) chains.push_back(std::move(pts));
        }
    };
    doc.forEach([&](const IEntity& e) { visit(e); });
    m.groundPoints = static_cast<int>(m.groundPts.size());
    // zanjirlash: eng uzunidan boshlab uchlari tutashganlarini qo'shish
    auto len = [](const std::vector<AxisPt>& c) {
        double l = 0;
        for (std::size_t i = 0; i + 1 < c.size(); ++i) l += std::hypot(c[i + 1].x - c[i].x, c[i + 1].y - c[i].y);
        return l;
    };
    std::vector<AxisPt> axis;
    if (!chains.empty()) {
        std::sort(chains.begin(), chains.end(), [&](const auto& a, const auto& b) { return len(a) > len(b); });
        axis = chains.front();
        std::vector<bool> used(chains.size(), false);
        used[0] = true;
        for (bool grew = true; grew;) {
            grew = false;
            for (std::size_t i = 1; i < chains.size(); ++i) {
                if (used[i]) continue;
                auto c = chains[i];
                auto near = [](const AxisPt& a, const AxisPt& b) { return std::hypot(a.x - b.x, a.y - b.y) < 0.05; };
                if (near(axis.back(), c.front())) { axis.insert(axis.end(), c.begin() + 1, c.end()); }
                else if (near(axis.back(), c.back())) { std::reverse(c.begin(), c.end()); axis.insert(axis.end(), c.begin() + 1, c.end()); }
                else if (near(axis.front(), c.back())) { axis.insert(axis.begin(), c.begin(), c.end() - 1); }
                else if (near(axis.front(), c.front())) { std::reverse(c.begin(), c.end()); axis.insert(axis.begin(), c.begin(), c.end() - 1); }
                else continue;
                used[i] = grew = true;
            }
        }
    }
    if (axis.size() < 2) {
        recomputeRoad(m, p);
        return m;
    }
    // o'q bo'yicha masofalar
    std::vector<double> ch(axis.size(), 0);
    for (std::size_t i = 1; i < axis.size(); ++i) ch[i] = ch[i - 1] + std::hypot(axis[i].x - axis[i - 1].x, axis[i].y - axis[i - 1].y);
    const double total = ch.back();
    auto at = [&](double s, double& x, double& y, double& z, double& dx, double& dy) {
        std::size_t i = static_cast<std::size_t>(std::upper_bound(ch.begin(), ch.end(), s) - ch.begin());
        i = std::clamp<std::size_t>(i, 1, axis.size() - 1);
        const double seg = ch[i] - ch[i - 1];
        const double t = seg > 1e-12 ? (s - ch[i - 1]) / seg : 0;
        x = axis[i - 1].x + t * (axis[i].x - axis[i - 1].x);
        y = axis[i - 1].y + t * (axis[i].y - axis[i - 1].y);
        z = axis[i - 1].z + t * (axis[i].z - axis[i - 1].z);
        dx = (axis[i].x - axis[i - 1].x) / std::max(1e-12, seg);
        dy = (axis[i].y - axis[i - 1].y) / std::max(1e-12, seg);
    };
    // yoylar va burilish nuqtalari
    for (std::size_t i = 1; i + 1 < axis.size(); ++i)
        if (!axis[i].arc && !axis[i - 1].arc) {
            // yoy boshlanishi emas: oddiy uch
            if (axis[i + 1].arc || axis[i].radius > 0) continue;
            m.vertexS.push_back(ch[i]);
        }
    for (std::size_t i = 1; i < axis.size();) {
        if (axis[i].radius > 0) {
            const std::size_t s0 = i - 1;
            const double R = axis[i].radius;
            while (i < axis.size() && axis[i].radius > 0 && std::abs(axis[i].radius - R) < 1e-6) ++i;
            m.curves.push_back({ch[s0], ch[i - 1], R});
        } else {
            ++i;
        }
    }
    // piketlar: har step + uchlar
    std::vector<double> ss;
    for (double s = 0; s < total - 1e-6; s += p.step) ss.push_back(s);
    ss.push_back(total);
    for (double v : m.vertexS) ss.push_back(v);
    std::sort(ss.begin(), ss.end());
    ss.erase(std::unique(ss.begin(), ss.end(), [](double a, double b) { return std::abs(a - b) < 0.01; }), ss.end());
    bool axis3d = false;
    for (const auto& a : axis) axis3d |= std::abs(a.z) > 1e-9;
    // loyiha nazorat nuqtalari: yozuvlar -> o'qqa proyeksiya
    std::vector<std::pair<double, double>> ctrl;
    for (const auto& d : dl) {
        double bestD = p.labelRadius, bestS = -1;
        for (std::size_t i = 0; i + 1 < axis.size(); ++i) {
            const double ax = axis[i].x, ay = axis[i].y, bx = axis[i + 1].x, by = axis[i + 1].y;
            const double l2 = (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
            const double u = l2 > 0 ? std::clamp(((d.x - ax) * (bx - ax) + (d.y - ay) * (by - ay)) / l2, 0.0, 1.0) : 0;
            const double dd = std::hypot(ax + u * (bx - ax) - d.x, ay + u * (by - ay) - d.y);
            if (dd < bestD) { bestD = dd; bestS = ch[i] + u * (ch[i + 1] - ch[i]); }
        }
        if (bestS >= 0) {
            // burilish nuqtasi ichki tomonida turgan yozuv qo'shni segmentga tushib qolmasin: uch yaqin bo'lsa — uchga
            for (std::size_t i = 0; i < axis.size(); ++i)
                if (!axis[i].arc && std::hypot(axis[i].x - d.x, axis[i].y - d.y) <= 1.25 * bestD + 1e-9 &&
                    std::abs(ch[i] - bestS) < p.labelRadius) {
                    bestS = ch[i];
                    break;
                }
            ctrl.push_back({bestS, d.z});
        }
    }
    if (axis3d)
        for (std::size_t i = 0; i < axis.size(); ++i) ctrl.push_back({ch[i], axis[i].z});
    std::sort(ctrl.begin(), ctrl.end());
    m.designPoints = static_cast<int>(ctrl.size());
    for (const auto& c : ctrl) ss.push_back(c.first);
    std::sort(ss.begin(), ss.end());
    ss.erase(std::unique(ss.begin(), ss.end(), [](double a, double b) { return std::abs(a - b) < 0.01; }), ss.end());
    for (double s : ss) {
        RoadStation st;
        st.s = s;
        double z;
        at(s, st.x, st.y, z, st.dx, st.dy);
        st.ground = groundAt(m.groundPts, st.x, st.y, p.ground);
        for (const auto& c : ctrl) st.control |= std::abs(c.first - s) < 0.01;
        m.st.push_back(st);
    }
    // loyiha belgilari
    if (!ctrl.empty()) {
        for (auto& st : m.st) {
            if (st.s <= ctrl.front().first) st.design = ctrl.front().second;
            else if (st.s >= ctrl.back().first) st.design = ctrl.back().second;
            else {
                auto it = std::upper_bound(ctrl.begin(), ctrl.end(), std::make_pair(st.s, -1e18));
                const auto& b = *it;
                const auto& a = *(it - 1);
                st.design = a.second + (b.second - a.second) * (st.s - a.first) / std::max(1e-9, b.first - a.first);
            }
        }
    } else {  // yer sathini silliqlash (±50 m) + raise
        for (auto& st : m.st) {
            double sum = 0;
            int n = 0;
            for (const auto& o : m.st)
                if (std::abs(o.s - st.s) <= 50) { sum += o.ground; ++n; }
            st.design = sum / n + p.raise;
        }
    }
    recomputeRoad(m, p);
    return m;
}

// ---------------------------------------------------------------- JSON
std::string roadToJson(const RoadModel& m, const RoadParams& p) {
    std::string s = std::format(
        "{{\"format\":\"asiacad-road\",\"version\":1,\"cat\":\"{}\",\"speed\":{},\"lanes\":{},\"laneWidth\":{},\"shoulder\":{},\"strengthened\":{},"
        "\"maxGrade\":{},\"minRadius\":{},\"crossfall\":{},\"shoulderFall\":{},\"slopeFill\":{},\"slopeCut\":{},\"step\":{},\"ground\":{},"
        "\"raise\":{},\"groundPoints\":{},\"designPoints\":{},\"pavement\":[",
        p.cat.id, p.cat.speed, p.cat.lanes, p.cat.laneWidth, p.cat.shoulder, p.cat.strengthened, p.cat.maxGrade, p.cat.minRadius, p.crossfall,
        p.shoulderFall, p.slopeFill, p.slopeCut, p.step, p.ground, p.raise, m.groundPoints, m.designPoints);
    for (std::size_t i = 0; i < p.pavement.size(); ++i)
        s += std::format("{}[\"{}\",{},{}]", i ? "," : "", p.pavement[i].name, p.pavement[i].thickness, p.pavement[i].asphalt);
    s += "],\"st\":[";
    for (std::size_t i = 0; i < m.st.size(); ++i) {
        const auto& t = m.st[i];
        s += std::format("{}[{},{},{},{},{},{},{},{}]", i ? "," : "", t.s, t.x, t.y, t.dx, t.dy, t.ground, t.design, t.control);
    }
    s += "],\"vertexS\":[";
    for (std::size_t i = 0; i < m.vertexS.size(); ++i) s += std::format("{}{}", i ? "," : "", m.vertexS[i]);
    s += "],\"curves\":[";
    for (std::size_t i = 0; i < m.curves.size(); ++i) s += std::format("{}[{},{},{}]", i ? "," : "", m.curves[i].s0, m.curves[i].s1, m.curves[i].radius);
    s += "],\"groundPts\":[";
    for (std::size_t i = 0; i < m.groundPts.size(); ++i) s += std::format("{}[{},{},{}]", i ? "," : "", m.groundPts[i][0], m.groundPts[i][1], m.groundPts[i][2]);
    return s + "]}";
}

void roadFromJson(const std::string& text, RoadModel& m, RoadParams& p) {
    json::Value root;
    try {
        root = json::parse(text);
    } catch (const json::ParseError& e) {
        throw Error("road.err.format", {e.what()});
    }
    auto num = [&](const char* k, double def) {
        const json::Value* v = root.find(k);
        return v && v->isNumber() ? v->num() : def;
    };
    if (const auto* c = root.find("cat"); c && c->isString()) p.cat.id = c->str();
    p.cat.speed = static_cast<int>(num("speed", p.cat.speed));
    p.cat.lanes = static_cast<int>(num("lanes", p.cat.lanes));
    p.cat.laneWidth = num("laneWidth", p.cat.laneWidth);
    p.cat.shoulder = num("shoulder", p.cat.shoulder);
    p.cat.strengthened = num("strengthened", p.cat.strengthened);
    p.cat.maxGrade = num("maxGrade", p.cat.maxGrade);
    p.cat.minRadius = num("minRadius", p.cat.minRadius);
    p.crossfall = num("crossfall", p.crossfall);
    p.shoulderFall = num("shoulderFall", p.shoulderFall);
    p.slopeFill = num("slopeFill", p.slopeFill);
    p.slopeCut = num("slopeCut", p.slopeCut);
    p.step = num("step", p.step);
    p.ground = num("ground", p.ground);
    p.raise = num("raise", p.raise);
    if (const auto* pv = root.find("pavement"); pv && pv->isArray()) {
        p.pavement.clear();
        for (const auto& l : pv->arr())
            if (l.isArray() && l.arr().size() >= 3)
                p.pavement.push_back({l.arr()[0].str(), l.arr()[1].num(), l.arr()[2].isBool() && l.arr()[2].boolean()});
    }
    m = {};
    m.groundPoints = static_cast<int>(num("groundPoints", 0));
    m.designPoints = static_cast<int>(num("designPoints", 0));
    if (const auto* a = root.find("st"); a && a->isArray())
        for (const auto& q : a->arr()) {
            if (!q.isArray() || q.arr().size() < 8) continue;
            const auto& v = q.arr();
            RoadStation t;
            t.s = v[0].num(); t.x = v[1].num(); t.y = v[2].num(); t.dx = v[3].num(); t.dy = v[4].num();
            t.ground = v[5].num(); t.design = v[6].num(); t.control = v[7].isBool() && v[7].boolean();
            m.st.push_back(t);
        }
    if (const auto* a = root.find("vertexS"); a && a->isArray())
        for (const auto& q : a->arr()) m.vertexS.push_back(q.num());
    if (const auto* a = root.find("curves"); a && a->isArray())
        for (const auto& q : a->arr())
            if (q.isArray() && q.arr().size() >= 3) m.curves.push_back({q.arr()[0].num(), q.arr()[1].num(), q.arr()[2].num()});
    if (const auto* a = root.find("groundPts"); a && a->isArray())
        for (const auto& q : a->arr())
            if (q.isArray() && q.arr().size() >= 3) m.groundPts.push_back({q.arr()[0].num(), q.arr()[1].num(), q.arr()[2].num()});
    recomputeRoad(m, p);
}

// ---------------------------------------------------------------- chizma
std::vector<std::unique_ptr<IEntity>> drawRoad3D(const RoadModel& m) {
    std::vector<std::unique_ptr<IEntity>> out;
    if (m.st.size() < 2) return out;
    auto put = [&](std::vector<Vec3> pts, const char* layer) {
        auto e = std::make_unique<PolylineEntity>(std::move(pts), false);
        e->setLayer(layer);
        out.push_back(std::move(e));
    };
    auto world = [](const RoadStation& s, double o, double z) { return Vec3{s.x - s.dy * o, s.y + s.dx * o, z}; };
    const std::size_t n = m.st.front().section.size();
    std::vector<std::vector<Vec3>> lines(n);
    for (std::size_t i = 0; i < m.st.size(); ++i) {
        const auto& s = m.st[i];
        std::vector<Vec3> sec;
        for (std::size_t k = 0; k < s.section.size(); ++k) {
            const Vec3 w = world(s, s.section[k].first, s.section[k].second);
            sec.push_back(w);
            if (k < n) lines[k].push_back(w);
        }
        put(sec, "YOL_3D_KESIM");
        out.back()->xset("st", std::to_string(i));  // tuzatish paneli: shu piket
    }
    for (std::size_t k = 0; k < n; ++k) {
        const char* layer = k == n / 2 ? "YOL_3D_OQ" : (k == 0 || k + 1 == n) ? "YOL_3D_QIYALIK" : (k == 1 || k + 2 == n) ? "YOL_3D_YOQA" : "YOL_3D_QATNOV";
        put(lines[k], layer);
    }
    for (auto& e : out) e->xset("road", "main");
    return out;
}

std::vector<std::unique_ptr<IEntity>> drawRoadProfile(const RoadModel& m, const Vec3& o0, double vs, double h, const std::string& groundLbl,
                                                      const std::string& designLbl, const std::string& pkLbl) {
    std::vector<std::unique_ptr<IEntity>> out;
    if (m.st.size() < 2) return out;
    auto put = [&](std::unique_ptr<IEntity> e) {
        e->setLayer("YOL_PROFIL");
        out.push_back(std::move(e));
    };
    double zmin = 1e18, zmax = -1e18;
    for (const auto& s : m.st) {
        zmin = std::min({zmin, s.ground, s.design});
        zmax = std::max({zmax, s.ground, s.design});
    }
    const double datum = std::floor(zmin) - 2;
    const double rowH = h * 3, tableH = rowH * 3;
    // berilgan nuqta — profilning chap YUQORI burchagi (chizma pastga qarab joylashadi)
    const Vec3 o{o0.x, o0.y - (tableH + (zmax - datum) * vs + h * 2), 0};
    const double y0 = o.y + tableH;
    auto P = [&](double s, double z) { return Vec3{o.x + s, y0 + (z - datum) * vs, 0}; };
    std::vector<Vec3> g, d;
    for (const auto& s : m.st) {
        g.push_back(P(s.s, s.ground));
        d.push_back(P(s.s, s.design));
    }
    put(std::make_unique<PolylineEntity>(g, false));
    put(std::make_unique<PolylineEntity>(d, false));
    const double L = m.st.back().s;
    // jadval ramkasi
    for (int r = 0; r <= 3; ++r) put(std::make_unique<LineEntity>(Vec3{o.x - h * 12, o.y + r * rowH, 0}, Vec3{o.x + L, o.y + r * rowH, 0}));
    put(std::make_unique<LineEntity>(Vec3{o.x, o.y, 0}, Vec3{o.x, y0 + (zmax - datum) * vs + h * 2, 0}));
    const std::string lbl[3] = {pkLbl, groundLbl, designLbl};
    for (int r = 0; r < 3; ++r)
        put(std::make_unique<TextEntity>(Vec3{o.x - h * 11.5, o.y + (2 - r) * rowH + rowH / 2, 0}, h * 0.8, lbl[r], 0, TextHAlign::Left, TextVAlign::Middle));
    put(std::make_unique<TextEntity>(Vec3{o.x - h * 11.5, y0 + h, 0}, h * 0.8, "UB " + fc(datum), 0));
    for (const auto& s : m.st) {
        const double x = o.x + s.s;
        put(std::make_unique<LineEntity>(Vec3{x, o.y, 0}, Vec3{x, y0, 0}));
        put(std::make_unique<LineEntity>(Vec3{x, y0, 0}, P(s.s, std::max(s.ground, s.design))));
        put(std::make_unique<TextEntity>(Vec3{x - h * 0.2, o.y + 2 * rowH + rowH / 2, 0}, h * 0.7, pk(s.s), kPI / 2, TextHAlign::Center, TextVAlign::Bottom));
        put(std::make_unique<TextEntity>(Vec3{x - h * 0.2, o.y + rowH + rowH / 2, 0}, h * 0.7, fc(s.ground), kPI / 2, TextHAlign::Center, TextVAlign::Bottom));
        put(std::make_unique<TextEntity>(Vec3{x - h * 0.2, o.y + rowH / 2, 0}, h * 0.7, fc(s.design), kPI / 2, TextHAlign::Center, TextVAlign::Bottom));
    }
    return out;
}

} // namespace asiacad::civil
