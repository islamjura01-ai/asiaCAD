#include <asiacad/civil/generators.h>

#include <asiacad/civil/road.h>
#include <asiacad/civil/utility.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/json.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace asiacad::civil {

namespace {
constexpr double kPI = 3.14159265358979323846;

template <class T>
void add(Entities& out, std::unique_ptr<T> e, const char* layer) {
    e->setLayer(layer);
    out.push_back(std::move(e));
}
void rect(Entities& out, double x0, double y0, double x1, double y1, const char* layer, double z = 0) {
    add(out, std::make_unique<PolylineEntity>(std::vector<Vec3>{{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}}, true), layer);
}
void text(Entities& out, double x, double y, double h, const std::string& s, const char* layer, double rot = 0,
          TextHAlign ha = TextHAlign::Left, TextVAlign va = TextVAlign::Baseline) {
    add(out, std::make_unique<TextEntity>(Vec3{x, y, 0}, h, s, rot, ha, va), layer);
}
std::string esc(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '"' || c == '\\') r += '\\';
        r += c;
    }
    return r;
}
json::Value parseOr(const std::string& text) {
    try {
        return json::parse(text);
    } catch (const json::ParseError& e) {
        throw Error("gen.err.format", {e.what()});
    }
}
double num(const json::Value& o, const char* k, double def) {
    const json::Value* v = o.find(k);
    return v && v->isNumber() ? v->num() : def;
}
std::string str(const json::Value& o, const char* k, const std::string& def) {
    const json::Value* v = o.find(k);
    return v && v->isString() ? v->str() : def;
}
std::vector<double> nums(const json::Value& o, const char* k) {
    std::vector<double> r;
    if (const auto* a = o.find(k); a && a->isArray())
        for (const auto& v : a->arr())
            if (v.isNumber()) r.push_back(v.num());
    return r;
}
std::string list(const std::vector<double>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) s += std::format("{}{}", i ? "," : "", v[i]);
    return s + "]";
}
}  // namespace

// ---------------------------------------------------------------- qadamlar
std::vector<double> parseSpacing(const std::string& s0) {
    std::vector<double> r;
    std::string s;
    for (char c : s0) s += (c == ';' || c == ' ' || c == '\t') ? ',' : (c == 'X' || c == '*') ? 'x' : c;
    // "×" (U+00D7), kirill "х"/"Х" -> x
    for (const char* m : {"\xC3\x97", "\xD1\x85", "\xD0\xA5"})
        for (std::size_t p; (p = s.find(m)) != std::string::npos;) s.replace(p, 2, "x");
    std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        std::string tok = s.substr(i, j - i);
        i = j + 1;
        if (tok.empty()) continue;
        int n = 1;
        double v = 0;
        try {
            const std::size_t x = tok.find('x');
            std::size_t used = 0;
            if (x == std::string::npos) {
                v = std::stod(tok, &used);
                if (used != tok.size()) return {};
            } else {
                // "6x5" — 6 m, 5 marta (oddiy yozuv: qadam × soni); "5x6" ham shunday o'qiladi: kattasi qadam emas, birinchisi qadam
                const std::string a = tok.substr(0, x), b = tok.substr(x + 1);
                v = std::stod(a, &used);
                if (used != a.size()) return {};
                const double c = std::stod(b, &used);
                if (used != b.size() || c < 1 || c > 500 || std::abs(c - std::round(c)) > 1e-9) return {};
                n = static_cast<int>(std::lround(c));
            }
        } catch (...) {
            return {};
        }
        if (!(v > 0.05 && v < 1000)) return {};
        for (int k = 0; k < n; ++k) r.push_back(v);
    }
    return r;
}

std::string gridLetter(int i) {
    static const char* L[] = {"А", "Б", "В", "Г", "Д", "Е", "Ж", "И", "К", "Л", "М", "Н", "П", "Р", "С", "Т", "У", "Ф", "Ш", "Э", "Ю", "Я"};
    constexpr int n = 22;
    if (i < n) return L[i];
    return std::string(L[i % n]) + std::to_string(i / n);
}

// ---------------------------------------------------------------- GRID
Entities generateGrid(const GridSpec& g) {
    Entities out;
    if (g.dx.empty() || g.dy.empty()) throw Error("gen.err.grid_empty", {});
    std::vector<double> X{g.x0}, Y{g.y0};
    for (double d : g.dx) X.push_back(X.back() + d);
    for (double d : g.dy) Y.push_back(Y.back() + d);
    const double ext = 2.0, br = 0.5;  // o'q chizig'i chiqishi va marka radiusi, m
    // o'qlar va markalar
    for (std::size_t i = 0; i < X.size(); ++i) {
        add(out, std::make_unique<LineEntity>(Vec3{X[i], Y.front() - ext, 0}, Vec3{X[i], Y.back() + ext, 0}), "BINO_TORI");
        for (double yb : {Y.front() - ext - br, Y.back() + ext + br}) {
            add(out, std::make_unique<CircleEntity>(Vec3{X[i], yb, 0}, br), "BINO_TORI");
            text(out, X[i], yb, br, std::to_string(i + 1), "BINO_TORI", 0, TextHAlign::Center, TextVAlign::Middle);
        }
    }
    for (std::size_t j = 0; j < Y.size(); ++j) {
        add(out, std::make_unique<LineEntity>(Vec3{X.front() - ext, Y[j], 0}, Vec3{X.back() + ext, Y[j], 0}), "BINO_TORI");
        for (double xb : {X.front() - ext - br, X.back() + ext + br}) {
            add(out, std::make_unique<CircleEntity>(Vec3{xb, Y[j], 0}, br), "BINO_TORI");
            text(out, xb, Y[j], br, gridLetter(static_cast<int>(j)), "BINO_TORI", 0, TextHAlign::Center, TextVAlign::Middle);
        }
    }
    // o'lchov zanjiri yozuvlari (qadamlar) — pastda
    for (std::size_t i = 0; i + 1 < X.size(); ++i)
        text(out, (X[i] + X[i + 1]) / 2, Y.front() - ext - 2 * br - 0.9, 0.35, std::format("{:g}", (X[i + 1] - X[i]) * 1000), "BINO_TORI", 0,
             TextHAlign::Center);
    for (std::size_t j = 0; j + 1 < Y.size(); ++j)
        text(out, X.front() - ext - 2 * br - 0.9, (Y[j] + Y[j + 1]) / 2, 0.35, std::format("{:g}", (Y[j + 1] - Y[j]) * 1000), "BINO_TORI", kPI / 2,
             TextHAlign::Center);
    // ustunlar — har kesishmada
    const double hb = g.colB / 2, hh = g.colH / 2;
    for (double x : X)
        for (double y : Y) rect(out, x - hb, y - hh, x + hb, y + hh, "USTUN");
    // tashqi devorlar: ustunlar orasida, o'q bo'yicha markazlangan (ustun bilan ustma-ust tushmaydi)
    if (g.walls && g.wallT > 0) {
        const double t = g.wallT / 2;
        const char* wl = g.infill ? "TOSIQ_DEVOR" : "DEVOR";
        for (double y : {Y.front(), Y.back()})
            for (std::size_t i = 0; i + 1 < X.size(); ++i)
                if (X[i + 1] - hb - (X[i] + hb) > 0.05) rect(out, X[i] + hb, y - t, X[i + 1] - hb, y + t, wl);
        for (double x : {X.front(), X.back()})
            for (std::size_t j = 0; j + 1 < Y.size(); ++j)
                if (Y[j + 1] - hh - (Y[j] + hh) > 0.05) rect(out, x - t, Y[j] + hh, x + t, Y[j + 1] - hh, wl);
    }
    if (g.core && g.floors >= 2) {
        const std::size_t i = g.dx.size() / 2;
        const double m = std::max(hb, hh) + 0.05;
        const double bx = X[i] + m, by = Y[0] + m, bw = X[i + 1] - X[i] - 2 * m, bd = Y[1] - Y[0] - 2 * m;
        const double sw = 2.7, sl = std::min(6.3, bd);
        if (bw >= sw && sl >= 4.5) {
            rect(out, bx, by, bx + sw, by + sl, "ZINA");
            if (g.floors > 5 && bw >= sw + 0.45 + 1.8) rect(out, bx + sw + 0.45, by, bx + sw + 0.45 + 1.8, by + 2.0, "LIFT_SHAXTA");
        }
    }
    if (g.slab) {
        const double o = std::max({g.overhang, hb, hh, g.walls ? g.wallT / 2 : 0.0});
        rect(out, X.front() - o, Y.front() - o, X.back() + o, Y.back() + o, "PLITA");
    }
    return out;
}

std::string gridToJson(const GridSpec& g) {
    return std::format(
        "{{\"format\":\"asiacad-grid\",\"version\":1,\"dx\":{},\"dy\":{},\"x0\":{},\"y0\":{},\"colB\":{},\"colH\":{},\"walls\":{},\"infill\":{},\"wallT\":{},"
        "\"slab\":{},\"overhang\":{},\"floors\":{},\"floorH\":{},\"core\":{}}}",
        list(g.dx), list(g.dy), g.x0, g.y0, g.colB, g.colH, g.walls, g.infill, g.wallT, g.slab, g.overhang, g.floors, g.floorH, g.core);
}
GridSpec gridFromJson(const std::string& t) {
    const auto r = parseOr(t);
    GridSpec g;
    auto spacing = [&](const char* k) {
        if (const auto* v = r.find(k); v && v->isString()) return parseSpacing(v->str());
        return nums(r, k);
    };
    g.dx = spacing("dx");
    g.dy = spacing("dy");
    g.x0 = num(r, "x0", g.x0); g.y0 = num(r, "y0", g.y0);
    g.colB = num(r, "colB", g.colB); g.colH = num(r, "colH", g.colH);
    if (const auto* v = r.find("walls"); v && v->isBool()) g.walls = v->boolean();
    if (const auto* v = r.find("infill"); v && v->isBool()) g.infill = v->boolean();
    if (const auto* v = r.find("slab"); v && v->isBool()) g.slab = v->boolean();
    if (const auto* v = r.find("core"); v && v->isBool()) g.core = v->boolean();
    g.wallT = num(r, "wallT", g.wallT);
    g.overhang = num(r, "overhang", g.overhang);
    g.floors = static_cast<int>(num(r, "floors", g.floors));
    g.floorH = num(r, "floorH", g.floorH);
    return g;
}

// ---------------------------------------------------------------- ALIGN
Entities generateAlignment(const AlignSpec& a, std::vector<std::string>* warnings) {
    Entities out;
    if (a.pi.size() < 2) throw Error("gen.err.align_pts", {});
    const std::size_t n = a.pi.size();
    // har burilishda tangens uzunligi T = R·tan(Δ/2); qo'shni tangenslar segmentdan oshmasin
    std::vector<double> T(n, 0), bulge(n, 0), R(n, 0);
    auto dir = [&](std::size_t i, std::size_t j, double& ux, double& uy) {
        const double dx = a.pi[j].x - a.pi[i].x, dy = a.pi[j].y - a.pi[i].y, l = std::hypot(dx, dy);
        ux = l > 0 ? dx / l : 1;
        uy = l > 0 ? dy / l : 0;
        return l;
    };
    std::vector<double> seg(n, 0);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        double ux, uy;
        seg[i] = dir(i, i + 1, ux, uy);
        if (seg[i] < 1e-6) throw Error("gen.err.align_dup", {std::to_string(i + 1)});
    }
    for (std::size_t i = 1; i + 1 < n; ++i) {
        if (a.pi[i].r <= 0) continue;
        double ax, ay, bx, by;
        dir(i - 1, i, ax, ay);
        dir(i, i + 1, bx, by);
        const double cr = ax * by - ay * bx, dt = ax * bx + ay * by;
        const double delta = std::atan2(std::abs(cr), dt);  // burilish burchagi
        if (delta < 1e-6) continue;
        R[i] = a.pi[i].r;
        T[i] = R[i] * std::tan(delta / 2);
        bulge[i] = (cr > 0 ? 1 : -1) * std::tan(delta / 4);
    }
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const double need = T[i] + T[i + 1];
        if (need > seg[i] + 1e-9) {
            const double k = seg[i] / need;
            for (std::size_t j : {i, i + 1})
                if (T[j] > 0) {
                    const double t2 = T[j] * k;
                    if (warnings) warnings->push_back(std::format("{}|{:.1f}|{:.1f}", j + 1, R[j], R[j] * k));
                    R[j] *= k;
                    T[j] = t2;
                }
        }
    }
    std::vector<Vec3> pts;
    std::vector<double> bul;
    pts.push_back({a.pi[0].x, a.pi[0].y, 0});
    bul.push_back(0);
    for (std::size_t i = 1; i + 1 < n; ++i) {
        if (T[i] <= 0) {
            pts.push_back({a.pi[i].x, a.pi[i].y, 0});
            bul.push_back(0);
            continue;
        }
        double ax, ay, bx, by;
        dir(i - 1, i, ax, ay);
        dir(i, i + 1, bx, by);
        pts.push_back({a.pi[i].x - ax * T[i], a.pi[i].y - ay * T[i], 0});
        bul.push_back(bulge[i]);
        pts.push_back({a.pi[i].x + bx * T[i], a.pi[i].y + by * T[i], 0});
        bul.push_back(0);
    }
    pts.push_back({a.pi[n - 1].x, a.pi[n - 1].y, 0});
    bul.push_back(0);
    // nol uzunlikli segmentlarni olib tashlash (ikki yoy tutashganda)
    for (std::size_t i = 1; i < pts.size();)
        if (std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y) < 1e-9) {
            bul[i - 1] = bul[i];
            pts.erase(pts.begin() + static_cast<long>(i));
            bul.erase(bul.begin() + static_cast<long>(i));
        } else ++i;
    auto pl = std::make_unique<PolylineEntity>(pts, bul, false);
    const PolylineEntity axis = *pl;
    add(out, std::move(pl), a.axisLayer.c_str());
    const double L = axis.length2d();
    // piket belgilari (har 100 m) — alohida qatlamda, o'q o'qilishiga xalaqit bermaydi
    auto frame = [&](double s, double& x, double& y, double& nx, double& ny) {
        s = std::clamp(s, 0.0, L);
        const Vec3 p = axis.pointAtDistance(s);
        const double s2 = s + 0.05 <= L ? s + 0.05 : s - 0.05;
        const Vec3 q = axis.pointAtDistance(s2);
        double dx = (q.x - p.x) * (s2 > s ? 1 : -1), dy = (q.y - p.y) * (s2 > s ? 1 : -1);
        const double l = std::hypot(dx, dy);
        dx /= l;
        dy /= l;
        x = p.x;
        y = p.y;
        nx = -dy;
        ny = dx;
    };
    for (double s = 0; s <= L + 1e-6; s += 100) {
        double x, y, nx, ny;
        frame(s, x, y, nx, ny);
        add(out, std::make_unique<LineEntity>(Vec3{x - nx * 1.5, y - ny * 1.5, 0}, Vec3{x + nx * 1.5, y + ny * 1.5, 0}), "PIKET");
        text(out, x + nx * 2, y + ny * 2, 1.5, std::format("PK{}", static_cast<int>(std::lround(s / 100))), "PIKET", std::atan2(ny, nx));
    }
    for (const auto& v : a.pvi) {
        if (v.s < -1e-6 || v.s > L + 1e-6) {
            if (warnings) warnings->push_back(std::format("pvi|{:.2f}|{:.2f}", v.s, L));
            continue;
        }
        double x, y, nx, ny;
        frame(v.s, x, y, nx, ny);
        text(out, x + nx * a.labelOffset, y + ny * a.labelOffset, 1.0, std::format("{:.2f}", v.z), a.designLayer.c_str());
    }
    return out;
}

std::string alignToJson(const AlignSpec& a) {
    std::string o = "{\"format\":\"asiacad-align\",\"version\":1,\"axisLayer\":\"" + esc(a.axisLayer) + "\",\"designLayer\":\"" + esc(a.designLayer) +
                     "\",\"pi\":[";
    for (std::size_t i = 0; i < a.pi.size(); ++i) o += std::format("{}[{},{},{}]", i ? "," : "", a.pi[i].x, a.pi[i].y, a.pi[i].r);
    o += "],\"pvi\":[";
    for (std::size_t i = 0; i < a.pvi.size(); ++i) o += std::format("{}[{},{}]", i ? "," : "", a.pvi[i].s, a.pvi[i].z);
    return o + "]}";
}
AlignSpec alignFromJson(const std::string& t) {
    const auto r = parseOr(t);
    AlignSpec a;
    a.axisLayer = str(r, "axisLayer", a.axisLayer);
    a.designLayer = str(r, "designLayer", a.designLayer);
    if (const auto* v = r.find("pi"); v && v->isArray())
        for (const auto& q : v->arr())
            if (q.isArray() && q.arr().size() >= 2)
                a.pi.push_back({q.arr()[0].num(), q.arr()[1].num(), q.arr().size() > 2 && q.arr()[2].isNumber() ? q.arr()[2].num() : 0});
    if (const auto* v = r.find("pvi"); v && v->isArray())
        for (const auto& q : v->arr())
            if (q.isArray() && q.arr().size() >= 2) a.pvi.push_back({q.arr()[0].num(), q.arr()[1].num()});
    return a;
}

// ---------------------------------------------------------------- UTILNET
namespace {
const char* netLayer(const std::string& kind) {
    if (kind == "WATER") return "SUV_TARMOGI";
    if (kind == "STORM") return "YOMGIR_KANAL";
    if (kind == "GAS") return "GAZ";
    if (kind == "HEAT") return "ISSIQLIK";
    if (kind == "POWER") return "ELEKTR_KABEL";
    if (kind == "TELECOM") return "ALOQA";
    if (kind == "DRAIN") return "DRENAJ";
    return "KANALIZATSIYA";
}
bool gravity(const std::string& k) { return k == "SEWER" || k == "STORM" || k == "DRAIN"; }
}  // namespace

Entities generateNetwork(const NetSpec& n) {
    Entities out;
    const int N = static_cast<int>(n.nodes.size());
    // har quduq uchun tur — unga ulangan birinchi quvurdan
    std::vector<std::string> kind(n.nodes.size(), "SEWER");
    std::vector<bool> seen(n.nodes.size(), false);
    for (const auto& p : n.pipes) {
        if (p.from < 0 || p.from >= N || p.to < 0 || p.to >= N || p.from == p.to) throw Error("gen.err.pipe_node", {std::to_string(p.from + 1), std::to_string(p.to + 1)});
        for (int i : {p.from, p.to})
            if (!seen[static_cast<std::size_t>(i)]) { seen[static_cast<std::size_t>(i)] = true; kind[static_cast<std::size_t>(i)] = p.kind; }
    }
    for (std::size_t i = 0; i < n.nodes.size(); ++i) {
        const auto& q = n.nodes[i];
        add(out, std::make_unique<CircleEntity>(Vec3{q.x, q.y, 0}, 0.75), netLayer(kind[i]));
        // yozuvlar: nom, qopqoq (yuqorida), tub (ostida) — quduq markazidan 9 m ichida
        if (!q.name.empty()) text(out, q.x + 1.2, q.y + 2.4, 0.8, q.name, "YOZUV");
        text(out, q.x + 1.2, q.y + 1.1, 0.6, std::format("{:.2f}", q.ground), "YOZUV");
        if (q.invert != 0) text(out, q.x + 1.2, q.y + 0.1, 0.6, std::format("{:.2f}", q.invert), "YOZUV");
    }
    for (const auto& p : n.pipes) {
        const auto& a = n.nodes[static_cast<std::size_t>(p.from)];
        const auto& b = n.nodes[static_cast<std::size_t>(p.to)];
        add(out, std::make_unique<LineEntity>(Vec3{a.x, a.y, 0}, Vec3{b.x, b.y, 0}), netLayer(p.kind));
        const double L = std::hypot(b.x - a.x, b.y - a.y);
        if (L < 1e-6) continue;
        double ux = (b.x - a.x) / L, uy = (b.y - a.y) / L;
        double rot = std::atan2(uy, ux);
        if (rot > kPI / 2 || rot < -kPI / 2) rot += kPI;  // yozuv o'qilishi chapdan o'ngga
        const double mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2, nx = -uy, ny = ux;
        text(out, mx + nx * 0.6, my + ny * 0.6, 0.6, std::format("DN{:g}", p.dn), "YOZUV", rot, TextHAlign::Center);
        text(out, mx - nx * 1.2, my - ny * 1.2, 0.6, std::format("{:.1f}m", L), "YOZUV", rot, TextHAlign::Center);
        if (gravity(p.kind) && a.invert != 0 && b.invert != 0) {
            const double i = std::abs(a.invert - b.invert) / L;
            if (i > 1e-5 && i < 0.5)
                text(out, mx + ux * std::min(6.0, L / 4) + nx * 0.6, my + uy * std::min(6.0, L / 4) + ny * 0.6, 0.6, std::format("i={:.4f}", i), "YOZUV",
                     rot, TextHAlign::Center);
        }
    }
    return out;
}

std::string netToJson(const NetSpec& n) {
    std::string o = "{\"format\":\"asiacad-utilnet\",\"version\":1,\"nodes\":[";
    for (std::size_t i = 0; i < n.nodes.size(); ++i) {
        const auto& q = n.nodes[i];
        o += std::format("{}{{\"name\":\"{}\",\"x\":{},\"y\":{},\"ground\":{},\"invert\":{}}}", i ? "," : "", esc(q.name), q.x, q.y, q.ground, q.invert);
    }
    o += "],\"pipes\":[";
    for (std::size_t i = 0; i < n.pipes.size(); ++i) {
        const auto& p = n.pipes[i];
        o += std::format("{}{{\"from\":{},\"to\":{},\"kind\":\"{}\",\"dn\":{}}}", i ? "," : "", p.from + 1, p.to + 1, esc(p.kind), p.dn);
    }
    return o + "]}";
}
NetSpec netFromJson(const std::string& t) {
    const auto r = parseOr(t);
    NetSpec n;
    if (const auto* v = r.find("nodes"); v && v->isArray())
        for (const auto& q : v->arr()) {
            if (!q.isObject()) continue;
            NetNode d;
            d.name = str(q, "name", "");
            d.x = num(q, "x", 0); d.y = num(q, "y", 0); d.ground = num(q, "ground", 0); d.invert = num(q, "invert", 0);
            n.nodes.push_back(d);
        }
    auto idx = [&](const json::Value& q, const char* k) {
        const json::Value* v = q.find(k);
        if (v && v->isNumber()) return static_cast<int>(v->num()) - 1;  // 1 dan
        if (v && v->isString())
            for (std::size_t i = 0; i < n.nodes.size(); ++i)
                if (n.nodes[i].name == v->str()) return static_cast<int>(i);
        return -1;
    };
    if (const auto* v = r.find("pipes"); v && v->isArray())
        for (const auto& q : v->arr()) {
            if (!q.isObject()) continue;
            NetPipe p;
            p.from = idx(q, "from");
            p.to = idx(q, "to");
            p.kind = str(q, "kind", p.kind);
            p.dn = num(q, "dn", p.dn);
            n.pipes.push_back(p);
        }
    return n;
}

// ---------------------------------------------------------------- CITY
std::function<double(double, double)> groundFromDoc(const Document& doc, int* count) {
    auto pts = std::make_shared<std::vector<std::array<double, 3>>>();
    std::function<void(const IEntity&)> visit = [&](const IEntity& e) {
        if (auto* ins = dynamic_cast<const InsertEntity*>(&e)) {
            for (const auto& c : ins->explode()) visit(*c);
            for (const auto& a : ins->attributes()) visit(a.text);
            return;
        }
        if (guessRoadLayer(e.layer()) != RoadLayer::Ground) return;
        if (auto* t = dynamic_cast<const TextEntity*>(&e)) {
            double v;
            if (parseUtilLabel(t->text(), v) == 'z') pts->push_back({t->insertion().x, t->insertion().y, v});
        } else if (auto* p = dynamic_cast<const PointEntity*>(&e)) {
            pts->push_back({p->position().x, p->position().y, p->position().z});
        }
    };
    doc.forEach([&](const IEntity& e) { visit(e); });
    if (count) *count = static_cast<int>(pts->size());
    if (pts->empty()) return {};
    return [pts](double x, double y) {
        std::vector<std::pair<double, double>> d;
        d.reserve(pts->size());
        for (const auto& g : *pts) d.push_back({std::hypot(g[0] - x, g[1] - y), g[2]});
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
}

CityResult generateCity(const CitySpec& c, const std::function<double(double, double)>& groundAt) {
    CityResult r;
    if (c.nx < 1 || c.ny < 1 || c.nx > 200 || c.ny > 200) throw Error("gen.err.city_size", {});
    if (c.blockW <= 2 * c.setback || c.blockD <= 2 * c.setback || c.street <= 0 || c.carriage > c.street)
        throw Error("gen.err.city_dims", {});
    const double pitchX = c.blockW + c.street, pitchY = c.blockD + c.street;
    const double W = c.nx * pitchX + c.street, D = c.ny * pitchY + c.street;
    r.siteArea = W * D;
    auto& out = r.entities;
    auto zAt = [&](double x, double y) { return groundAt ? groundAt(x, y) : c.ground; };
    // ko'chalar: o'q (KOCHA_OQ — yo'l/koridor o'qi), qatnov qismi chetlari
    const double hc = c.carriage / 2;
    for (int i = 0; i <= c.nx; ++i) {
        const double x = c.x0 + c.street / 2 + i * pitchX;
        add(out, std::make_unique<LineEntity>(Vec3{x, c.y0, 0}, Vec3{x, c.y0 + D, 0}), "KOCHA_O'Q");
        for (double s : {-hc, hc}) add(out, std::make_unique<LineEntity>(Vec3{x + s, c.y0, 0}, Vec3{x + s, c.y0 + D, 0}), "KOCHA_CHETI");
        rect(out, x - hc, c.y0, x + hc, c.y0 + D, "KOCHA_YUZA");
        r.streetLength += D;
    }
    for (int j = 0; j <= c.ny; ++j) {
        const double y = c.y0 + c.street / 2 + j * pitchY;
        add(out, std::make_unique<LineEntity>(Vec3{c.x0, y, 0}, Vec3{c.x0 + W, y, 0}), "KOCHA_O'Q");
        for (double s : {-hc, hc}) add(out, std::make_unique<LineEntity>(Vec3{c.x0, y + s, 0}, Vec3{c.x0 + W, y + s, 0}), "KOCHA_CHETI");
        rect(out, c.x0, y - hc, c.x0 + W, y + hc, "KOCHA_YUZA");
        r.streetLength += W;
    }
    // binolar: kvartal ichida chekinishdan keyin qatorlar (uzun tomoni X bo'ylab), markazlashtirilgan
    unsigned rng = c.seed ? c.seed : 1;
    auto rnd = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return (rng >> 8) / static_cast<double>(1u << 24);
    };
    const int fmin = std::max(1, std::min(c.floorsMin, c.floorsMax)), fmax = std::max(c.floorsMin, c.floorsMax);
    const double iw = c.blockW - 2 * c.setback, id = c.blockD - 2 * c.setback;
    const int nAlong = c.bldW <= iw ? static_cast<int>(std::floor((iw + c.gap) / (c.bldW + c.gap))) : 0;
    const int nRows = c.bldD <= id ? static_cast<int>(std::floor((id + c.gap) / (c.bldD + c.gap))) : 0;
    for (int bi = 0; bi < c.nx; ++bi)
        for (int bj = 0; bj < c.ny; ++bj) {
            const double bx = c.x0 + c.street + bi * pitchX, by = c.y0 + c.street + bj * pitchY;
            rect(out, bx, by, bx + c.blockW, by + c.blockD, "KVARTAL");
            r.blockArea += c.blockW * c.blockD;
            text(out, bx + 1, by + c.blockD - 4, 3, std::format("{}-{}", bi + 1, gridLetter(bj)), "SHAHAR_YOZUV");
            if (nAlong < 1 || nRows < 1) continue;
            const double usedW = nAlong * c.bldW + (nAlong - 1) * c.gap, usedD = nRows * c.bldD + (nRows - 1) * c.gap;
            const double sx = bx + c.setback + (iw - usedW) / 2, sy = by + c.setback + (id - usedD) / 2;
            for (int a = 0; a < nAlong; ++a)
                for (int b = 0; b < nRows; ++b) {
                    const double x0 = sx + a * (c.bldW + c.gap), y0 = sy + b * (c.bldD + c.gap), x1 = x0 + c.bldW, y1 = y0 + c.bldD;
                    CityBuilding B;
                    B.footprint = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
                    B.floors = fmin + static_cast<int>(std::floor(rnd() * (fmax - fmin + 1)));
                    B.floors = std::min(B.floors, fmax);
                    B.height = B.floors * c.floorH;
                    // asos — burchaklardagi eng past yer belgisi (bino yerga botmaydi, sokol shu bo'yicha)
                    B.base = std::min({zAt(x0, y0), zAt(x1, y0), zAt(x1, y1), zAt(x0, y1)});
                    rect(out, x0, y0, x1, y1, "SHAHAR_BINO");
                    text(out, (x0 + x1) / 2, (y0 + y1) / 2, std::min(2.5, c.bldD / 4), std::format("{}", B.floors), "SHAHAR_YOZUV", 0,
                         TextHAlign::Center, TextVAlign::Middle);
                    // 3D massiv: asos va tom konturlari + vertikal qirralar (absolyut)
                    const double z0 = B.base, z1 = B.base + B.height;
                    rect(out, x0, y0, x1, y1, "SHAHAR_3D", z0);
                    rect(out, x0, y0, x1, y1, "SHAHAR_3D", z1);
                    for (const auto& p : B.footprint)
                        add(out, std::make_unique<LineEntity>(Vec3{p[0], p[1], z0}, Vec3{p[0], p[1], z1}), "SHAHAR_3D");
                    r.footprint += c.bldW * c.bldD;
                    r.gfa += c.bldW * c.bldD * B.floors;
                    r.buildings.push_back(std::move(B));
                }
        }
    return r;
}

std::string cityToJson(const CitySpec& c) {
    return std::format(
        "{{\"format\":\"asiacad-city\",\"version\":1,\"nx\":{},\"ny\":{},\"blockW\":{},\"blockD\":{},\"street\":{},\"carriage\":{},\"setback\":{},"
        "\"bldW\":{},\"bldD\":{},\"gap\":{},\"floorsMin\":{},\"floorsMax\":{},\"floorH\":{},\"x0\":{},\"y0\":{},\"ground\":{},\"seed\":{}}}",
        c.nx, c.ny, c.blockW, c.blockD, c.street, c.carriage, c.setback, c.bldW, c.bldD, c.gap, c.floorsMin, c.floorsMax, c.floorH, c.x0, c.y0,
        c.ground, c.seed);
}
CitySpec cityFromJson(const std::string& t) {
    const auto r = parseOr(t);
    CitySpec c;
    c.nx = static_cast<int>(num(r, "nx", c.nx)); c.ny = static_cast<int>(num(r, "ny", c.ny));
    c.blockW = num(r, "blockW", c.blockW); c.blockD = num(r, "blockD", c.blockD);
    c.street = num(r, "street", c.street); c.carriage = num(r, "carriage", c.carriage); c.setback = num(r, "setback", c.setback);
    c.bldW = num(r, "bldW", c.bldW); c.bldD = num(r, "bldD", c.bldD); c.gap = num(r, "gap", c.gap);
    c.floorsMin = static_cast<int>(num(r, "floorsMin", c.floorsMin)); c.floorsMax = static_cast<int>(num(r, "floorsMax", c.floorsMax));
    c.floorH = num(r, "floorH", c.floorH);
    c.x0 = num(r, "x0", c.x0); c.y0 = num(r, "y0", c.y0); c.ground = num(r, "ground", c.ground);
    c.seed = static_cast<unsigned>(num(r, "seed", c.seed));
    return c;
}

} // namespace asiacad::civil
