#include <asiacad/civil/waterway.h>

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
constexpr double kG = 9.81;
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
bool isPipe(WwType t) { return t == WwType::PressurePipe || t == WwType::GravityPipe; }
}  // namespace

char wwLabel(const std::string& text, double& v) {
    std::string u = utf8::toUpper(text);
    for (auto& c : u)
        if (c == ',') c = '.';
    auto starts = [&](std::initializer_list<const char*> ks) {
        for (const char* k : ks) {
            const std::string K = utf8::toUpper(k);
            if (u.rfind(K, 0) == 0) return K.size();
        }
        return std::string::npos;
    };
    auto numAfter = [&](std::size_t p) {
        while (p < u.size() && !(std::isdigit(static_cast<unsigned char>(u[p])) || u[p] == '-')) ++p;
        if (p >= u.size()) return false;
        try {
            std::size_t used = 0;
            v = std::stod(u.substr(p), &used);
            return used > 0;
        } catch (...) {
            return false;
        }
    };
    // tur aniq ko'rsatilgan yozuvlar
    if (auto k = starts({"ДНО", "Д=", "ЛОТОК", "Л ", "Л=", "TUB", "TABAN", "INV", "IL ", "IL=", "BOTTOM", "ЛОТ."}); k != std::string::npos)
        return numAfter(k) ? 'b' : 0;
    if (auto k = starts({"УВ", "ГВ", "НПУ", "ФПУ", "SUV SATH", "SUV", "WL", "SU KOT"}); k != std::string::npos) return numAfter(k) ? 'w' : 0;
    if (auto k = starts({"ЗЕМЛЯ", "ЗЕМ", "З ", "З=", "YER", "GL", "ZEMİN", "ZEMIN", "ЧЁРН", "ЧЕРН"}); k != std::string::npos)
        return numAfter(k) ? 'g' : 0;
    if (parseUtilLabel(text, v) == 'z') return 'z';  // "▽485,20", "485.20", "отм. 485.20"
    return 0;
}

const char* wwTypeKey(WwType t) {
    switch (t) {
        case WwType::PressurePipe: return "PPIPE";
        case WwType::GravityPipe: return "GPIPE";
        case WwType::ConcreteCanal: return "CANAL";
        case WwType::EarthDitch: return "DITCH";
        case WwType::Flume: return "FLUME";
    }
    return "CANAL";
}
bool wwTypeFromKey(const std::string& s, WwType& out) {
    for (WwType t : {WwType::PressurePipe, WwType::GravityPipe, WwType::ConcreteCanal, WwType::EarthDitch, WwType::Flume})
        if (s == wwTypeKey(t)) { out = t; return true; }
    return false;
}
bool wwOpen(WwType t) { return !isPipe(t); }

std::vector<WwSection> builtinWwSections() {
    std::vector<WwSection> v;
    auto canal = [&](const char* name, double b, double h, double m, double t) {
        WwSection s;
        s.type = WwType::ConcreteCanal;
        s.name = name;
        s.b = b; s.h = h; s.m = m; s.t = t;
        s.n = 0.015; s.freeboard = h >= 1.5 ? 0.3 : 0.2; s.vmin = 0.4; s.vmax = 4.0;
        v.push_back(s);
    };
    canal("Beton kanal 0.6×0.6", 0.6, 0.6, 0, 0.12);
    canal("Beton kanal 1.0×1.0", 1.0, 1.0, 0, 0.15);
    canal("Beton kanal 1.5×1.2 (1:1)", 1.5, 1.2, 1.0, 0.15);
    canal("Beton kanal 2.0×1.8 (1:1.5)", 2.0, 1.8, 1.5, 0.20);
    canal("Beton kanal 3.0×2.5 (1:1.5)", 3.0, 2.5, 1.5, 0.20);
    auto ditch = [&](const char* name, double b, double h, double m, double vmax) {
        WwSection s;
        s.type = WwType::EarthDitch;
        s.name = name;
        s.b = b; s.h = h; s.m = m; s.t = 0;
        s.n = 0.025; s.freeboard = 0.15; s.vmin = 0.3; s.vmax = vmax;
        v.push_back(s);
    };
    ditch("Ariq 0.4×0.5 (1:1, qumoq)", 0.4, 0.5, 1.0, 0.7);
    ditch("Ariq 0.6×0.8 (1:1.5, soz)", 0.6, 0.8, 1.5, 1.0);
    ditch("Ariq 1.0×1.2 (1:1.5, soz)", 1.0, 1.2, 1.5, 1.0);
    // parabolik lotoklar (ЛР tipi) — o'lchamlar taxminiy, aniq katalogdan tekshirilsin
    auto flume = [&](const char* name, double B, double h, double t) {
        WwSection s;
        s.type = WwType::Flume;
        s.name = name;
        s.B = B; s.h = h; s.b = 0; s.m = 0; s.t = t;
        s.n = 0.014; s.freeboard = 0.1; s.vmin = 0.4; s.vmax = 4.0; s.pieceLen = 6.0;
        v.push_back(s);
    };
    flume("Lotok ЛР-4 (B≈0.84, h=0.40)", 0.84, 0.40, 0.05);
    flume("Lotok ЛР-6 (B≈1.24, h=0.60)", 1.24, 0.60, 0.06);
    flume("Lotok ЛР-8 (B≈1.60, h=0.80)", 1.60, 0.80, 0.07);
    flume("Lotok ЛР-10 (B≈2.00, h=1.00)", 2.00, 1.00, 0.08);
    auto pipe = [&](WwType ty, const char* name, double d, double n, double t, double vmax) {
        WwSection s;
        s.type = ty;
        s.name = name;
        s.d = d; s.h = d; s.n = n; s.t = t; s.vmin = ty == WwType::PressurePipe ? 0.5 : 0.7; s.vmax = vmax; s.freeboard = 0;
        v.push_back(s);
    };
    pipe(WwType::PressurePipe, "PE100 SDR17 Ø315 (ichki 0.278)", 0.278, 0.011, 0.019, 3.0);
    pipe(WwType::PressurePipe, "PE100 SDR17 Ø500 (ichki 0.441)", 0.441, 0.011, 0.030, 3.0);
    pipe(WwType::PressurePipe, "Po'lat Ø820×10 (ichki 0.800)", 0.800, 0.012, 0.010, 3.0);
    pipe(WwType::GravityPipe, "Temir-beton Ø800", 0.8, 0.014, 0.08, 4.0);
    pipe(WwType::GravityPipe, "Temir-beton Ø1200", 1.2, 0.014, 0.10, 4.0);
    pipe(WwType::GravityPipe, "PE gofra Ø600", 0.6, 0.012, 0.04, 5.0);
    return v;
}

const char* wwLayerKey(WwLayer k) {
    switch (k) {
        case WwLayer::Axis: return "AXIS";
        case WwLayer::Ground: return "GROUND";
        case WwLayer::Design: return "DESIGN";
        case WwLayer::Ignore: return "IGNORE";
    }
    return "IGNORE";
}
bool wwLayerFromKey(const std::string& s, WwLayer& out) {
    for (WwLayer k : {WwLayer::Axis, WwLayer::Ground, WwLayer::Design, WwLayer::Ignore})
        if (s == wwLayerKey(k)) { out = k; return true; }
    return false;
}
WwLayer guessWwLayer(const std::string& layer) {
    const std::string u = utf8::toUpper(layer);
    auto has = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys)
            if (u.find(utf8::toUpper(k)) != std::string::npos) return true;
        return false;
    };
    if (has({"YOZUV", "PODPIS", "ПОДПИС", "ETIKET", "TEXT", "BELGI", "ОТМЕТ", "KOT", "LOYIHA", "ПРОЕКТ", "DESIGN"})) return WwLayer::Design;
    if (has({"KANAL", "КАНАЛ", "ARIQ", "АРИҚ", "АРЫК", "ARIK", "LOTOK", "ЛОТОК", "SUV_YO", "SUV YO", "SUVYO", "ВОДОВОД", "ВОДОТОК",
             "WATERWAY", "CANAL", "DITCH", "FLUME", "SULAMA", "ИРРИГ", "IRRIG", "SUG'OR", "СУҒОР", "ARIK", "KANALET"}))
        return WwLayer::Axis;
    if (has({"YER_SATH", "YER SATH", "REL'EF", "RELYEF", "RELEF", "РЕЛЬЕФ", "GORIZONTAL", "ГОРИЗОНТАЛ", "CONTOUR", "GROUND", "TERRAIN",
             "TOPO", "ZEMIN", "ZEMİN", "SYOMKA", "СЪЕМК", "СЪЁМК", "HALIHAZIR"}))
        return WwLayer::Ground;
    return WwLayer::Ignore;
}
std::map<std::string, WwLayer> autoWwLayers(const Document& doc) {
    std::map<std::string, WwLayer> m;
    for (const auto& [name, l] : doc.layers()) m[name] = guessWwLayer(name);
    return m;
}

WwParams defaultWwParams() {
    WwParams p;
    p.sec = builtinWwSections()[2];
    return p;
}

int WwModel::errors() const {
    int n = 0;
    for (const auto& i : issues) n += i.error;
    return n;
}

// ---------------------------------------------------------------- gidravlika
double wwFullDepth(const WwSection& s) { return isPipe(s.type) ? s.d : s.h; }

double wwArea(const WwSection& s, double y) {
    y = std::max(0.0, y);
    if (isPipe(s.type)) {
        y = std::min(y, s.d);
        const double th = 2 * std::acos(std::clamp(1 - 2 * y / s.d, -1.0, 1.0));
        return s.d * s.d / 8 * (th - std::sin(th));
    }
    if (s.B > 0) {  // parabola
        const double w = s.B * std::sqrt(y / s.h);
        return 2.0 / 3.0 * w * y;
    }
    return (s.b + s.m * y) * y;
}
double wwTopWidth(const WwSection& s, double y) {
    y = std::max(0.0, y);
    if (isPipe(s.type)) {
        y = std::min(y, s.d);
        const double th = 2 * std::acos(std::clamp(1 - 2 * y / s.d, -1.0, 1.0));
        return s.d * std::sin(th / 2);
    }
    if (s.B > 0) return s.B * std::sqrt(y / s.h);
    return s.b + 2 * s.m * y;
}
double wwPerimeter(const WwSection& s, double y) {
    y = std::max(0.0, y);
    if (isPipe(s.type)) {
        y = std::min(y, s.d);
        const double th = 2 * std::acos(std::clamp(1 - 2 * y / s.d, -1.0, 1.0));
        return s.d * th / 2;
    }
    if (s.B > 0) {  // y = a x², yoy uzunligi
        const double u = wwTopWidth(s, y) / 2;
        if (u < 1e-9) return 0;
        const double a = s.h / ((s.B / 2) * (s.B / 2));
        const double k = 2 * a;
        return 2 * 0.5 * (u * std::sqrt(1 + k * k * u * u) + std::asinh(k * u) / k);
    }
    return s.b + 2 * y * std::sqrt(1 + s.m * s.m);
}
double wwFlow(const WwSection& s, double y, double slope) {
    if (slope <= 0 || y <= 0) return 0;
    const double A = wwArea(s, y), P = wwPerimeter(s, y);
    if (P <= 0) return 0;
    return A * std::pow(A / P, 2.0 / 3.0) * std::sqrt(slope) / s.n;
}
double wwNormalDepth(const WwSection& s, double q, double slope) {
    if (q <= 0) return 0;
    if (slope <= 0) return -1;
    const double ymax = isPipe(s.type) ? 0.938 * s.d : s.h;  // quvurda eng katta sarf ~0.938D da
    if (wwFlow(s, ymax, slope) < q) return -1;
    double lo = 1e-6, hi = ymax;
    for (int it = 0; it < 80; ++it) {
        const double mid = (lo + hi) / 2;
        (wwFlow(s, mid, slope) < q ? lo : hi) = mid;
    }
    return (lo + hi) / 2;
}

// ---------------------------------------------------------------- qayta hisob
void recomputeWaterway(WwModel& m, const WwParams& p) {
    m.issues.clear();
    m.cutVolume = m.fillVolume = m.concrete = 0;
    m.pieces = 0;
    m.capacity = 1e18;
    m.headLoss = 0;
    auto issue = [&](bool e, const char* k, std::vector<std::string> a, double s) { m.issues.push_back({e, k, std::move(a), s}); };
    if (m.st.size() < 2) {
        issue(true, "ww.chk.no_axis", {}, 0);
        m.capacity = 0;
        return;
    }
    if (m.groundPoints == 0) issue(true, "ww.chk.no_ground", {fc(p.ground)}, 0);
    if (m.designPoints == 0) issue(false, "ww.chk.no_design", {fc(p.slope * 1000, 1)}, 0);
    const WwSection& s = p.sec;
    const bool pipe = isPipe(s.type), pressure = s.type == WwType::PressurePipe;
    m.length = m.st.back().s - m.st.front().s;
    // nazorat (yozuv/qo'lda) belgilar orasidagi piketlar — chiziqli (qo'lda tuzatilgandan keyin ham)
    {
        std::vector<std::size_t> c;
        for (std::size_t i = 0; i < m.st.size(); ++i)
            if (m.st[i].control) c.push_back(i);
        if (!c.empty())
            for (std::size_t i = 0; i < m.st.size(); ++i) {
                if (m.st[i].control) continue;
                auto it = std::upper_bound(c.begin(), c.end(), i);
                if (it == c.begin()) m.st[i].bottom = m.st[c.front()].bottom;
                else if (it == c.end()) m.st[i].bottom = m.st[c.back()].bottom;
                else {
                    const auto& a = m.st[*(it - 1)];
                    const auto& b = m.st[*it];
                    m.st[i].bottom = a.bottom + (b.bottom - a.bottom) * (m.st[i].s - a.s) / std::max(1e-9, b.s - a.s);
                }
            }
    }
    for (std::size_t i = 0; i < m.st.size(); ++i) {
        auto& a = m.st[i];
        if (i + 1 < m.st.size()) {
            const double ds = std::max(1e-9, m.st[i + 1].s - a.s);
            a.slope = (a.bottom - m.st[i + 1].bottom) / ds;
        } else {
            a.slope = m.st[i - 1].slope;
        }
    }
    // tekshiruvlar oraliqlar bo'yicha bir marta (birinchi uchragan piket bilan)
    std::map<std::string, bool> once;
    auto issueOnce = [&](bool e, const char* k, std::vector<std::string> a, double st) {
        if (once[k]) return;
        once[k] = true;
        issue(e, k, std::move(a), st);
    };
    const double A = pipe ? kPI * s.d * s.d / 4 : 0;
    double head = 0;
    if (pressure) head = p.headStart > 0 ? p.headStart : m.st.front().bottom + s.d + 10.0;
    for (std::size_t i = 0; i < m.st.size(); ++i) {
        auto& t = m.st[i];
        if (pressure) {
            if (i > 0) {
                const double L = std::hypot(t.s - m.st[i - 1].s, t.bottom - m.st[i - 1].bottom);
                const double hf = 10.67 * L * std::pow(p.q, 1.852) / (std::pow(s.chw, 1.852) * std::pow(s.d, 4.87));
                head -= hf;
                m.headLoss += hf;
            }
            t.water = head;
            t.v = A > 0 ? p.q / A : 0;
            t.depthN = s.d;
            t.fr = 0;
            if (head < t.bottom + s.d) issueOnce(true, "ww.chk.vacuum", {pk(t.s), fc(head - t.bottom - s.d)}, t.s);
        } else {
            if (t.slope <= 0) {
                issueOnce(true, "ww.chk.reverse", {pk(t.s), fc(t.slope * 1000, 2)}, t.s);
                t.depthN = -1;
                t.v = 0;
                t.water = t.bottom;
            } else {
                const double yn = wwNormalDepth(s, p.q, t.slope);
                t.depthN = yn;
                if (yn < 0) {
                    issueOnce(true, "ww.chk.overflow", {pk(t.s), fc(p.q, 3), fc(wwFlow(s, pipe ? 0.938 * s.d : s.h, t.slope), 3)}, t.s);
                    t.water = t.bottom + wwFullDepth(s);
                    t.v = 0;
                } else {
                    const double Ay = wwArea(s, yn);
                    t.v = Ay > 0 ? p.q / Ay : 0;
                    const double T = wwTopWidth(s, yn);
                    t.fr = T > 0 ? t.v / std::sqrt(kG * Ay / T) : 0;
                    t.water = t.bottom + yn;
                    if (!pipe && yn + s.freeboard > s.h + 1e-9) issueOnce(true, "ww.chk.freeboard", {pk(t.s), fc(s.h - yn), fc(s.freeboard)}, t.s);
                    if (pipe && yn > 0.8 * s.d) issueOnce(false, "ww.chk.fill", {pk(t.s), fc(yn / s.d)}, t.s);
                }
                const double ycap = pipe ? 0.938 * s.d : std::max(0.0, s.h - s.freeboard);
                m.capacity = std::min(m.capacity, wwFlow(s, ycap, t.slope));
            }
        }
        if (t.v > 0 && t.v < s.vmin) issueOnce(false, "ww.chk.silting", {pk(t.s), fc(t.v), fc(s.vmin)}, t.s);
        if (t.v > s.vmax) issueOnce(true, "ww.chk.erosion", {pk(t.s), fc(t.v), fc(s.vmax)}, t.s);
        if (!pressure && t.fr > 0.8 && t.fr < 1.2) issueOnce(false, "ww.chk.critical", {pk(t.s), fc(t.fr)}, t.s);
        // ko'ndalang maydonlar: kotlovan (tub − devor qalinligi dan yergacha), ochiq o'zanda qirg'oq yerdan baland bo'lsa — dambalar
        const double outerB = pipe ? s.d + 2 * s.t : (s.B > 0 ? s.B * 0.5 : s.b) + 2 * s.t;
        const double zb = t.bottom - s.t;
        const double W0 = outerB + 2 * p.trenchExtra;
        const double dh = t.ground - zb;
        t.cut = dh > 0 ? (W0 + p.cutSlope * dh) * dh : 0;
        if (pipe) t.cut = std::max(0.0, t.cut);
        const double top = t.bottom + wwFullDepth(s);
        t.fill = 0;
        if (!pipe && top > t.ground) {
            const double e = top - t.ground;
            t.fill = 2 * (1.0 + 1.5 * e) * e;  // ikki damba: tepasi 1.0 m, qiyalik 1:1.5
        }
        if (pipe && t.ground - (t.bottom + s.d + s.t) < p.depth - 1e-6 && t.ground - (t.bottom + s.d + s.t) >= 0)
            issueOnce(false, "ww.chk.cover", {pk(t.s), fc(t.ground - (t.bottom + s.d + s.t)), fc(p.depth)}, t.s);
        if (pipe && t.ground < t.bottom + s.d + s.t) issueOnce(true, "ww.chk.above", {pk(t.s), fc(t.bottom + s.d + s.t - t.ground)}, t.s);
    }
    for (std::size_t i = 1; i < m.st.size(); ++i) {
        const double L = m.st[i].s - m.st[i - 1].s;
        m.cutVolume += (m.st[i].cut + m.st[i - 1].cut) / 2 * L;
        m.fillVolume += (m.st[i].fill + m.st[i - 1].fill) / 2 * L;
    }
    if (s.t > 0 && !pipe) m.concrete = wwPerimeter(s, s.h) * s.t * m.length;
    if (s.t > 0 && pipe) m.concrete = 0;
    if (s.type == WwType::Flume) m.pieces = static_cast<int>(std::ceil(m.length / std::max(0.5, s.pieceLen) - 1e-9));
    if (m.capacity > 1e17) m.capacity = 0;
}

// ---------------------------------------------------------------- chizmadan
WwModel buildWaterway(const Document& doc, const std::map<std::string, WwLayer>& layers, const WwParams& p) {
    WwModel m;
    auto kindOf = [&](const std::string& l) {
        auto it = layers.find(l);
        return it != layers.end() ? it->second : guessWwLayer(l);
    };
    std::vector<std::vector<Vec3>> chains;
    struct DL { double x, y, z; };
    std::vector<DL> dl;
    std::function<void(const IEntity&)> visit = [&](const IEntity& e) {
        if (auto* ins = dynamic_cast<const InsertEntity*>(&e)) {
            for (const auto& c : ins->explode()) visit(*c);
            for (const auto& a : ins->attributes()) visit(a.text);
            return;
        }
        const WwLayer k = kindOf(e.layer());
        if (auto* t = dynamic_cast<const TextEntity*>(&e)) {
            double v;
            const char c = wwLabel(t->text(), v);
            if (!c || c == 'w') return;  // suv sathi yozuvi — hisoblanadi, solishtirish uchun emas
            // turi yozilgan ("дно", "земля") — istalgan qatlamda; oddiy son — faqat tarmoq/loyiha/yer qatlamlarida
            if (c == 'z' && k == WwLayer::Ignore) return;
            if (!e.layer().empty() && e.layer()[0] == '@') return;
            if (c == 'g' || (c == 'z' && k == WwLayer::Ground)) m.groundPts.push_back({t->insertion().x, t->insertion().y, v});
            else dl.push_back({t->insertion().x, t->insertion().y, v});
            return;
        }
        if (k == WwLayer::Ignore) return;
        if (k == WwLayer::Ground) {
            if (auto* pt = dynamic_cast<const PointEntity*>(&e)) m.groundPts.push_back({pt->position().x, pt->position().y, pt->position().z});
            else if (auto* pl = dynamic_cast<const PolylineEntity*>(&e))
                for (const auto& q : pl->vertices())
                    if (std::abs(q.z) > 1e-9) m.groundPts.push_back({q.x, q.y, q.z});
            return;
        }
        if (k == WwLayer::Axis) {
            std::vector<Vec3> pts;
            if (auto* l = dynamic_cast<const LineEntity*>(&e)) pts = {l->startPoint(), l->endPoint()};
            else if (auto* pl = dynamic_cast<const PolylineEntity*>(&e)) pts = pl->tessellated(deg2rad(3.0)).vertices();
            if (pts.size() >= 2) chains.push_back(std::move(pts));
        }
    };
    doc.forEach([&](const IEntity& e) { visit(e); });
    m.groundPoints = static_cast<int>(m.groundPts.size());
    auto len = [](const std::vector<Vec3>& c) {
        double l = 0;
        for (std::size_t i = 0; i + 1 < c.size(); ++i) l += std::hypot(c[i + 1].x - c[i].x, c[i + 1].y - c[i].y);
        return l;
    };
    std::vector<Vec3> axis;
    if (!chains.empty()) {
        std::sort(chains.begin(), chains.end(), [&](const auto& a, const auto& b) { return len(a) > len(b); });
        axis = chains.front();
        std::vector<bool> used(chains.size(), false);
        used[0] = true;
        auto near = [](const Vec3& a, const Vec3& b) { return std::hypot(a.x - b.x, a.y - b.y) < 0.05; };
        for (bool grew = true; grew;) {
            grew = false;
            for (std::size_t i = 1; i < chains.size(); ++i) {
                if (used[i]) continue;
                auto c = chains[i];
                if (near(axis.back(), c.front())) axis.insert(axis.end(), c.begin() + 1, c.end());
                else if (near(axis.back(), c.back())) { std::reverse(c.begin(), c.end()); axis.insert(axis.end(), c.begin() + 1, c.end()); }
                else if (near(axis.front(), c.back())) axis.insert(axis.begin(), c.begin(), c.end() - 1);
                else if (near(axis.front(), c.front())) { std::reverse(c.begin(), c.end()); axis.insert(axis.begin(), c.begin(), c.end() - 1); }
                else continue;
                used[i] = grew = true;
            }
        }
    }
    if (axis.size() < 2) {
        recomputeWaterway(m, p);
        return m;
    }
    std::vector<double> ch(axis.size(), 0);
    for (std::size_t i = 1; i < axis.size(); ++i) ch[i] = ch[i - 1] + std::hypot(axis[i].x - axis[i - 1].x, axis[i].y - axis[i - 1].y);
    const double total = ch.back();
    auto at = [&](double s, WwStation& t) {
        std::size_t i = static_cast<std::size_t>(std::upper_bound(ch.begin(), ch.end(), s) - ch.begin());
        i = std::clamp<std::size_t>(i, 1, axis.size() - 1);
        const double seg = std::max(1e-12, ch[i] - ch[i - 1]);
        const double u = (s - ch[i - 1]) / seg;
        t.x = axis[i - 1].x + u * (axis[i].x - axis[i - 1].x);
        t.y = axis[i - 1].y + u * (axis[i].y - axis[i - 1].y);
        t.dx = (axis[i].x - axis[i - 1].x) / seg;
        t.dy = (axis[i].y - axis[i - 1].y) / seg;
        return axis[i - 1].z + u * (axis[i].z - axis[i - 1].z);
    };
    // loyiha nazorat nuqtalari: yozuvlar -> o'qqa; 3D o'q bo'lsa — Z
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
            // burilish nuqtasining ichki tomonidagi yozuv qo'shni segmentga tushib qolmasin — yaqin uchga
            for (std::size_t i = 0; i < axis.size(); ++i)
                if (std::hypot(axis[i].x - d.x, axis[i].y - d.y) <= 1.25 * bestD + 1e-9 && std::abs(ch[i] - bestS) < p.labelRadius) {
                    bestS = ch[i];
                    break;
                }
            ctrl.push_back({bestS, d.z});
        }
    }
    bool axis3d = false;
    for (const auto& a : axis) axis3d |= std::abs(a.z) > 1e-9;
    if (axis3d)
        for (std::size_t i = 0; i < axis.size(); ++i) ctrl.push_back({ch[i], axis[i].z});
    std::sort(ctrl.begin(), ctrl.end());
    m.designPoints = static_cast<int>(ctrl.size());
    std::vector<double> ss;
    for (double s = 0; s < total - 1e-6; s += p.step) ss.push_back(s);
    ss.push_back(total);
    for (std::size_t i = 1; i + 1 < axis.size(); ++i) {  // burilish nuqtalari (yoy bo'laklari emas)
        const double a1 = std::atan2(axis[i].y - axis[i - 1].y, axis[i].x - axis[i - 1].x);
        const double a2 = std::atan2(axis[i + 1].y - axis[i].y, axis[i + 1].x - axis[i].x);
        double da = std::abs(a2 - a1);
        if (da > kPI) da = 2 * kPI - da;
        if (da > deg2rad(5.0)) ss.push_back(ch[i]);
    }
    for (const auto& c : ctrl) ss.push_back(c.first);
    std::sort(ss.begin(), ss.end());
    ss.erase(std::unique(ss.begin(), ss.end(), [](double a, double b) { return std::abs(a - b) < 0.01; }), ss.end());
    for (double s : ss) {
        WwStation t;
        t.s = s;
        at(s, t);
        t.ground = idw(m.groundPts, t.x, t.y, p.ground);
        for (const auto& c : ctrl) t.control |= std::abs(c.first - s) < 0.01;
        m.st.push_back(t);
    }
    if (!ctrl.empty()) {
        for (auto& t : m.st) {
            if (t.s <= ctrl.front().first) t.bottom = ctrl.front().second;
            else if (t.s >= ctrl.back().first) t.bottom = ctrl.back().second;
            else {
                auto it = std::upper_bound(ctrl.begin(), ctrl.end(), std::make_pair(t.s, -1e18));
                const auto& b = *it;
                const auto& a = *(it - 1);
                t.bottom = a.second + (b.second - a.second) * (t.s - a.first) / std::max(1e-9, b.first - a.first);
            }
        }
    } else {  // loyiha yo'q: boshida yerdan chuqurlikda, keyin nishablik bilan pasayadi
        const bool pipe = isPipe(p.sec.type);
        const double start = m.st.front().ground - (pipe ? p.depth + p.sec.d + p.sec.t : p.sec.h);
        for (auto& t : m.st) t.bottom = start - p.slope * t.s;
    }
    recomputeWaterway(m, p);
    return m;
}

// ---------------------------------------------------------------- JSON
std::string wwToJson(const WwModel& m, const WwParams& p) {
    const auto& s = p.sec;
    std::string o = std::format(
        "{{\"format\":\"asiacad-waterway\",\"version\":1,\"type\":\"{}\",\"name\":\"{}\",\"b\":{},\"h\":{},\"m\":{},\"B\":{},\"d\":{},\"t\":{},"
        "\"n\":{},\"chw\":{},\"freeboard\":{},\"pieceLen\":{},\"vmin\":{},\"vmax\":{},\"q\":{},\"step\":{},\"ground\":{},\"depth\":{},"
        "\"slope\":{},\"headStart\":{},\"cutSlope\":{},\"trenchExtra\":{},\"groundPoints\":{},\"designPoints\":{},\"st\":[",
        wwTypeKey(s.type), s.name, s.b, s.h, s.m, s.B, s.d, s.t, s.n, s.chw, s.freeboard, s.pieceLen, s.vmin, s.vmax, p.q, p.step, p.ground,
        p.depth, p.slope, p.headStart, p.cutSlope, p.trenchExtra, m.groundPoints, m.designPoints);
    for (std::size_t i = 0; i < m.st.size(); ++i) {
        const auto& t = m.st[i];
        o += std::format("{}[{},{},{},{},{},{},{},{}]", i ? "," : "", t.s, t.x, t.y, t.dx, t.dy, t.ground, t.bottom, t.control);
    }
    o += "],\"groundPts\":[";
    for (std::size_t i = 0; i < m.groundPts.size(); ++i)
        o += std::format("{}[{},{},{}]", i ? "," : "", m.groundPts[i][0], m.groundPts[i][1], m.groundPts[i][2]);
    return o + "]}";
}

void wwFromJson(const std::string& text, WwModel& m, WwParams& p) {
    json::Value root;
    try {
        root = json::parse(text);
    } catch (const json::ParseError& e) {
        throw Error("ww.err.format", {e.what()});
    }
    auto num = [&](const char* k, double def) {
        const json::Value* v = root.find(k);
        return v && v->isNumber() ? v->num() : def;
    };
    auto& s = p.sec;
    if (const auto* t = root.find("type"); t && t->isString()) wwTypeFromKey(t->str(), s.type);
    if (const auto* t = root.find("name"); t && t->isString()) s.name = t->str();
    s.b = num("b", s.b); s.h = num("h", s.h); s.m = num("m", s.m); s.B = num("B", s.B); s.d = num("d", s.d); s.t = num("t", s.t);
    s.n = num("n", s.n); s.chw = num("chw", s.chw); s.freeboard = num("freeboard", s.freeboard); s.pieceLen = num("pieceLen", s.pieceLen);
    s.vmin = num("vmin", s.vmin); s.vmax = num("vmax", s.vmax);
    p.q = num("q", p.q); p.step = num("step", p.step); p.ground = num("ground", p.ground); p.depth = num("depth", p.depth);
    p.slope = num("slope", p.slope); p.headStart = num("headStart", p.headStart); p.cutSlope = num("cutSlope", p.cutSlope);
    p.trenchExtra = num("trenchExtra", p.trenchExtra);
    m = {};
    m.groundPoints = static_cast<int>(num("groundPoints", 0));
    m.designPoints = static_cast<int>(num("designPoints", 0));
    if (const auto* a = root.find("st"); a && a->isArray())
        for (const auto& q : a->arr()) {
            if (!q.isArray() || q.arr().size() < 8) continue;
            const auto& v = q.arr();
            WwStation t;
            t.s = v[0].num(); t.x = v[1].num(); t.y = v[2].num(); t.dx = v[3].num(); t.dy = v[4].num();
            t.ground = v[5].num(); t.bottom = v[6].num(); t.control = v[7].isBool() && v[7].boolean();
            m.st.push_back(t);
        }
    if (const auto* a = root.find("groundPts"); a && a->isArray())
        for (const auto& q : a->arr())
            if (q.isArray() && q.arr().size() >= 3) m.groundPts.push_back({q.arr()[0].num(), q.arr()[1].num(), q.arr()[2].num()});
    recomputeWaterway(m, p);
}

// ---------------------------------------------------------------- chizma
namespace {
// kesim konturi (offset, balandlik tubdan) chapdan o'ngga — ochiq o'zan ichki yuzasi
std::vector<std::pair<double, double>> innerShape(const WwSection& s) {
    std::vector<std::pair<double, double>> v;
    if (s.B > 0) {
        for (int k = -6; k <= 6; ++k) {
            const double x = s.B / 2 * k / 6.0;
            v.push_back({x, s.h * (x / (s.B / 2)) * (x / (s.B / 2))});
        }
        return v;
    }
    const double T = s.b + 2 * s.m * s.h;
    return {{-T / 2, s.h}, {-s.b / 2, 0}, {s.b / 2, 0}, {T / 2, s.h}};
}
}  // namespace

std::vector<std::unique_ptr<IEntity>> drawWaterway3D(const WwModel& m, const WwParams& p) {
    std::vector<std::unique_ptr<IEntity>> out;
    if (m.st.size() < 2) return out;
    auto put = [&](std::vector<Vec3> pts, const char* layer, bool closed = false) {
        auto e = std::make_unique<PolylineEntity>(std::move(pts), closed);
        e->setLayer(layer);
        out.push_back(std::move(e));
    };
    auto world = [](const WwStation& s, double o, double z) { return Vec3{s.x - s.dy * o, s.y + s.dx * o, z}; };
    const auto& sec = p.sec;
    if (isPipe(sec.type)) {
        std::vector<Vec3> inv, crown, axis, water;
        for (const auto& t : m.st) {
            inv.push_back(world(t, 0, t.bottom));
            crown.push_back(world(t, 0, t.bottom + sec.d));
            axis.push_back(world(t, 0, t.bottom + sec.d / 2));
            water.push_back(world(t, 0, t.water));
            std::vector<Vec3> ring;
            for (int k = 0; k < 16; ++k) {
                const double a = 2 * kPI * k / 16;
                ring.push_back(world(t, sec.d / 2 * std::cos(a), t.bottom + sec.d / 2 + sec.d / 2 * std::sin(a)));
            }
            put(ring, "SUV_3D_KESIM", true);
            out.back()->xset("st", std::to_string(&t - m.st.data()));
        }
        put(inv, "SUV_3D_TUB");
        put(crown, "SUV_3D_QIRGOQ");
        put(axis, "SUV_3D_OQ");
        put(water, sec.type == WwType::PressurePipe ? "SUV_3D_PYEZOMETR" : "SUV_3D_SUV");
        for (auto& e : out) e->xset("ww", "main");
        return out;
    }
    const auto shape = innerShape(sec);
    std::vector<std::vector<Vec3>> lines(shape.size());
    std::vector<Vec3> wl, wr;
    for (const auto& t : m.st) {
        std::vector<Vec3> cs;
        for (std::size_t k = 0; k < shape.size(); ++k) {
            const Vec3 w = world(t, shape[k].first, t.bottom + shape[k].second);
            cs.push_back(w);
            lines[k].push_back(w);
        }
        put(cs, "SUV_3D_KESIM");
        out.back()->xset("st", std::to_string(&t - m.st.data()));
        if (t.depthN > 0) {
            const double T = wwTopWidth(sec, t.depthN);
            wl.push_back(world(t, -T / 2, t.water));
            wr.push_back(world(t, T / 2, t.water));
        }
    }
    for (std::size_t k = 0; k < lines.size(); ++k)
        put(lines[k], (k == 0 || k + 1 == lines.size()) ? "SUV_3D_QIRGOQ" : "SUV_3D_TUB");
    if (wl.size() >= 2) {
        put(wl, "SUV_3D_SUV");
        put(wr, "SUV_3D_SUV");
    }
    for (auto& e : out) e->xset("ww", "main");
    return out;
}

std::vector<std::unique_ptr<IEntity>> drawWaterwayProfile(const WwModel& m, const WwParams& p, const Vec3& o0, double vs, double h,
                                                          const std::string& groundLbl, const std::string& bottomLbl,
                                                          const std::string& waterLbl, const std::string& pkLbl) {
    std::vector<std::unique_ptr<IEntity>> out;
    if (m.st.size() < 2) return out;
    auto put = [&](std::unique_ptr<IEntity> e) {
        e->setLayer("SUV_PROFIL");
        out.push_back(std::move(e));
    };
    const double full = wwFullDepth(p.sec);
    double zmin = 1e18, zmax = -1e18;
    for (const auto& s : m.st) {
        zmin = std::min({zmin, s.ground, s.bottom - p.sec.t});
        zmax = std::max({zmax, s.ground, s.bottom + full, s.water});
    }
    const double datum = std::floor(zmin) - 2;
    const double rowH = h * 3, tableH = rowH * 4;
    const Vec3 o{o0.x, o0.y - (tableH + (zmax - datum) * vs + h * 2), 0};
    const double y0 = o.y + tableH;
    auto P = [&](double s, double z) { return Vec3{o.x + s, y0 + (z - datum) * vs, 0}; };
    std::vector<Vec3> g, b, top, w;
    for (const auto& s : m.st) {
        g.push_back(P(s.s, s.ground));
        b.push_back(P(s.s, s.bottom));
        top.push_back(P(s.s, s.bottom + full));
        w.push_back(P(s.s, s.water));
    }
    put(std::make_unique<PolylineEntity>(g, false));
    put(std::make_unique<PolylineEntity>(b, false));
    put(std::make_unique<PolylineEntity>(top, false));
    put(std::make_unique<PolylineEntity>(w, false));
    const double L = m.st.back().s;
    for (int r = 0; r <= 4; ++r) put(std::make_unique<LineEntity>(Vec3{o.x - h * 12, o.y + r * rowH, 0}, Vec3{o.x + L, o.y + r * rowH, 0}));
    put(std::make_unique<LineEntity>(Vec3{o.x, o.y, 0}, Vec3{o.x, y0 + (zmax - datum) * vs + h * 2, 0}));
    const std::string lbl[4] = {pkLbl, groundLbl, bottomLbl, waterLbl};
    for (int r = 0; r < 4; ++r)
        put(std::make_unique<TextEntity>(Vec3{o.x - h * 11.5, o.y + (3 - r) * rowH + rowH / 2, 0}, h * 0.8, lbl[r], 0, TextHAlign::Left,
                                         TextVAlign::Middle));
    put(std::make_unique<TextEntity>(Vec3{o.x - h * 11.5, y0 + h, 0}, h * 0.8, "UB " + fc(datum), 0));
    for (const auto& s : m.st) {
        const double x = o.x + s.s;
        put(std::make_unique<LineEntity>(Vec3{x, o.y, 0}, Vec3{x, y0, 0}));
        put(std::make_unique<LineEntity>(Vec3{x, y0, 0}, P(s.s, std::max(s.ground, s.bottom + full))));
        const std::string vals[4] = {pk(s.s), fc(s.ground), fc(s.bottom), fc(s.water)};
        for (int r = 0; r < 4; ++r)
            put(std::make_unique<TextEntity>(Vec3{x - h * 0.2, o.y + (3 - r) * rowH + rowH / 2, 0}, h * 0.7, vals[r], kPI / 2, TextHAlign::Center,
                                             TextVAlign::Bottom));
    }
    return out;
}

}  // namespace asiacad::civil
