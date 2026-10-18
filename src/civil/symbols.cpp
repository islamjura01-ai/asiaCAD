#include <asiacad/civil/symbols.h>

#include <asiacad/core/draft.h>
#include <asiacad/core/entities.h>

#include <array>
#include <cmath>

namespace asiacad::civil {

namespace {
constexpr double kPI = 3.14159265358979323846;

struct B {
    std::vector<std::unique_ptr<IEntity>> es;
    template <class T>
    void put(std::unique_ptr<T> e, int aci) {
        e->setLayer("0");
        e->setColor(aci);
        es.push_back(std::move(e));
    }
    void line(double x0, double y0, double x1, double y1, int c) { put(std::make_unique<LineEntity>(Vec3{x0, y0}, Vec3{x1, y1}), c); }
    void circle(double x, double y, double r, int c) { put(std::make_unique<CircleEntity>(Vec3{x, y}, r), c); }
    void poly(std::vector<Vec3> v, bool closed, int c, std::vector<double> bl = {}) {
        if (bl.empty()) put(std::make_unique<PolylineEntity>(std::move(v), closed), c);
        else put(std::make_unique<PolylineEntity>(std::move(v), std::move(bl), closed), c);
    }
    void fill(const PolylineEntity& loop, int c) { put(std::make_unique<HatchEntity>(std::vector<PolylineEntity>{loop}, "SOLID"), c); }
    void fillRect(double x0, double y0, double x1, double y1, int c, int outline = -1) {
        PolylineEntity r({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, true);
        fill(r, c);
        if (outline >= 0) put(std::make_unique<PolylineEntity>(r), outline);
    }
    void fillRound(double x0, double y0, double x1, double y1, double rr, int c, int outline = -1) {
        auto r = makeRectangle({x0, y0}, {x1, y1}, rr);
        fill(*r, c);
        if (outline >= 0) put(std::move(r), outline);
    }
    void fillCircle(double x, double y, double r, int c, int outline = -1) {
        PolylineEntity p({{x + r, y}, {x - r, y}}, {1.0, 1.0}, true);
        fill(p, c);
        if (outline >= 0) circle(x, y, r, outline);
    }
    void text(double x, double y, double h, const char* s, int c) {
        put(std::make_unique<TextEntity>(Vec3{x, y}, h, s, 0, TextHAlign::Center, TextVAlign::Middle), c);
    }
};

// to'lqinli toj (bargli daraxt): n ta qavariq yoy
PolylineEntity crown(double R, int n, double bump) {
    std::vector<Vec3> v;
    std::vector<double> bl;
    for (int i = 0; i < n; ++i) {
        const double a = 2 * kPI * i / n;
        v.push_back({R * std::cos(a), R * std::sin(a)});
        bl.push_back(bump);
    }
    return PolylineEntity(std::move(v), std::move(bl), true);
}
PolylineEntity star(double r0, double r1, int n) {
    std::vector<Vec3> v;
    for (int i = 0; i < 2 * n; ++i) {
        const double a = kPI * i / n, r = i % 2 ? r0 : r1;
        v.push_back({r * std::cos(a), r * std::sin(a)});
    }
    return PolylineEntity(std::move(v), true);
}

void vehicle(B& b, double L, double W, int body, bool bus) {
    b.fillRound(-L / 2, -W / 2, L / 2, W / 2, std::min(W, L) * 0.18, body, 250);
    // g'ildiraklar (qora, kuzov ostidan chiqib turadi)
    for (double x : {-L / 2 + L * 0.18, L / 2 - L * 0.2})
        for (double y : {-W / 2 - 0.06, W / 2 + 0.06}) b.fillRect(x - 0.35, y - 0.12, x + 0.35, y + 0.12, 250);
    if (bus) {
        b.fillRect(L / 2 - 1.0, -W / 2 + 0.2, L / 2 - 0.25, W / 2 - 0.2, 151);   // old oyna
        for (double x = -L / 2 + 0.8; x < L / 2 - 1.6; x += 1.6) {
            b.fillRect(x, W / 2 - 0.28, x + 1.2, W / 2 - 0.12, 151);
            b.fillRect(x, -W / 2 + 0.12, x + 1.2, -W / 2 + 0.28, 151);
        }
    } else {
        // old va orqa oyna (trapetsiya), tom
        b.fill(PolylineEntity({{L * 0.08, -W * 0.38}, {L * 0.24, -W * 0.3}, {L * 0.24, W * 0.3}, {L * 0.08, W * 0.38}}, true), 151);
        b.fill(PolylineEntity({{-L * 0.3, -W * 0.36}, {-L * 0.2, -W * 0.3}, {-L * 0.2, W * 0.3}, {-L * 0.3, W * 0.36}}, true), 151);
        b.poly({{-L * 0.2, -W * 0.3}, {L * 0.08, -W * 0.3}, {L * 0.08, W * 0.3}, {-L * 0.2, W * 0.3}}, true, 250);
        // faralar
        b.fillRect(L / 2 - 0.12, -W / 2 + 0.15, L / 2, -W / 2 + 0.45, 2);
        b.fillRect(L / 2 - 0.12, W / 2 - 0.45, L / 2, W / 2 - 0.15, 2);
    }
}
}  // namespace

const std::vector<SymbolInfo>& symbolCatalog() {
    static const std::vector<SymbolInfo> v = {
        {"TREE", "GREEN"},       {"CONIFER", "GREEN"},   {"SHRUB", "GREEN"},     {"FLOWERBED", "GREEN"},  {"LAWN", "GREEN"},
        {"CAR", "TRANSPORT"},    {"TRUCK", "TRANSPORT"}, {"BUS", "TRANSPORT"},   {"BICYCLE", "TRANSPORT"},
        {"PERSON", "PEOPLE"},    {"PERSON2", "PEOPLE"},
        {"LAMP", "STREET"},      {"TRAFFIC", "STREET"},  {"BUSSTOP", "STREET"},  {"BENCH", "STREET"},     {"BIN", "STREET"},
        {"HYDRANT", "STREET"},   {"MANHOLE", "STREET"},  {"FOUNTAIN", "STREET"},
        {"PARKING", "SIGNS"},    {"CROSSWALK", "SIGNS"}, {"ARROW", "SIGNS"},     {"NORTH", "SIGNS"},
    };
    return v;
}

std::string symbolBlockName(const std::string& key) { return "SYM_" + key; }
std::string symbolKeyOf(const std::string& n) {
    std::string u;
    for (char c : n) u += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u.rfind("SYM_", 0) == 0 ? u.substr(4) : std::string();
}

BlockPtr makeSymbol(const std::string& key) {
    B b;
    if (key == "TREE") {
        const auto c = crown(2.4, 11, 0.32);
        b.fill(c, 82);
        b.put(std::make_unique<PolylineEntity>(c), 94);
        b.fill(crown(1.3, 7, 0.3), 92);
        b.fillCircle(0, 0, 0.18, 34);
    } else if (key == "CONIFER") {
        const auto s = star(1.1, 1.8, 9);
        b.fill(s, 96);
        b.put(std::make_unique<PolylineEntity>(s), 106);
        for (int i = 0; i < 9; ++i) {
            const double a = 2 * kPI * i / 9;
            b.line(0, 0, 1.6 * std::cos(a), 1.6 * std::sin(a), 106);
        }
    } else if (key == "SHRUB") {
        for (auto [x, y, r] : {std::array<double, 3>{-0.5, 0, 0.65}, {0.45, 0.2, 0.6}, {0.1, -0.45, 0.55}}) {
            PolylineEntity cc = crown(r, 7, 0.35);
            cc.transform(Transform::translation({x, y, 0}));
            b.fill(cc, 72);
            b.put(std::make_unique<PolylineEntity>(cc), 94);
        }
    } else if (key == "FLOWERBED") {
        auto e = makeEllipse({0, 0}, 1.8, 1.0, 0, 48);
        b.fill(*e, 74);
        b.put(std::move(e), 94);
        const int cols[] = {1, 6, 2, 30, 210, 1, 6, 2, 30};
        int k = 0;
        for (double x = -1.2; x <= 1.21; x += 0.6)
            for (double y = -0.45; y <= 0.46; y += 0.45)
                if ((x * x) / (1.6 * 1.6) + (y * y) / (0.85 * 0.85) <= 1) b.fillCircle(x, y, 0.12, cols[k++ % 9]);
    } else if (key == "LAWN") {
        b.fillRect(-5, -5, 5, 5, 84, 94);
        for (int i = 0; i < 12; ++i) {
            const double x = -4 + (i % 4) * 2.6, y = -3.8 + (i / 4) * 3.4;
            b.line(x, y, x + 0.25, y + 0.5, 94);
            b.line(x + 0.25, y + 0.5, x + 0.5, y, 94);
        }
    } else if (key == "CAR") {
        vehicle(b, 4.5, 1.8, 1, false);
    } else if (key == "TRUCK") {
        b.fillRound(-4.0, -1.25, 2.2, 1.25, 0.15, 8, 250);        // kuzov (yuk)
        b.fillRound(2.4, -1.2, 4.0, 1.2, 0.3, 5, 250);           // kabina
        b.fillRect(3.55, -1.0, 3.85, 1.0, 151);
        for (double x : {-3.0, -1.6, 3.0})
            for (double y : {-1.32, 1.32}) b.fillRect(x - 0.45, y - 0.12, x + 0.45, y + 0.12, 250);
    } else if (key == "BUS") {
        vehicle(b, 12.0, 2.5, 2, true);
    } else if (key == "BICYCLE") {
        b.circle(-0.55, 0, 0.33, 5);
        b.circle(0.55, 0, 0.33, 5);
        b.poly({{-0.55, 0}, {-0.1, 0.35}, {0.4, 0.35}, {0.55, 0}, {0.05, 0}, {-0.55, 0}}, false, 5);
        b.line(0.05, 0, -0.1, 0.35, 5);
        b.line(0.4, 0.35, 0.45, 0.5, 250);
    } else if (key == "PERSON" || key == "PERSON2") {
        const int c = key == "PERSON" ? 5 : 6;
        auto sh = makeEllipse({0, 0}, 0.26, 0.14, 0, 32);
        b.fill(*sh, c);
        b.put(std::move(sh), 250);
        b.fillCircle(0, 0, 0.11, 32, 250);
        b.line(0.12, 0.08, 0.32, 0.18, c);  // qadam yo'nalishi
    } else if (key == "LAMP") {
        b.circle(0, 0, 6.0, 51);                                   // yoritish doirasi
        b.fillCircle(0, 0, 0.15, 8, 250);                          // ustun
        b.line(0, 0, 1.6, 0, 8);                                   // kronshteyn
        auto h = makeEllipse({1.75, 0}, 0.35, 0.18, 0, 24);
        b.fill(*h, 2);
        b.put(std::move(h), 250);
    } else if (key == "TRAFFIC") {
        b.fillCircle(0, 0, 0.12, 8, 250);
        b.fillRound(-0.18, 0.2, 0.18, 1.2, 0.08, 250);
        b.fillCircle(0, 1.0, 0.11, 1);
        b.fillCircle(0, 0.7, 0.11, 2);
        b.fillCircle(0, 0.4, 0.11, 3);
    } else if (key == "BUSSTOP") {
        b.fillRect(-3, -0.9, 3, 0.9, 254, 8);                      // pavilon tomi
        b.fillRect(-2.6, 0.5, 2.6, 0.75, 32);                      // o'rindiq
        b.fillCircle(3.6, -0.6, 0.35, 5, 250);
        b.text(3.6, -0.6, 0.4, "A", 7);
        b.fillRect(-3.2, -1.6, 3.2, -1.3, 2);                      // bekat chizig'i (sariq)
    } else if (key == "BENCH") {
        b.fillRect(-0.9, -0.25, 0.9, 0.25, 32, 34);
        for (double y : {-0.12, 0.0, 0.12}) b.line(-0.85, y, 0.85, y, 34);
        b.fillRect(-0.9, 0.25, 0.9, 0.35, 34);                     // suyanchiq
    } else if (key == "BIN") {
        b.fillCircle(0, 0, 0.3, 8, 250);
        b.fillCircle(0, 0, 0.18, 3);
        b.line(-0.3, 0, 0.3, 0, 250);
    } else if (key == "HYDRANT") {
        b.fillCircle(0, 0, 0.22, 1, 250);
        b.fillRect(-0.32, -0.06, 0.32, 0.06, 1);
        b.text(0, 0.45, 0.25, "PG", 1);
    } else if (key == "MANHOLE") {
        b.fillCircle(0, 0, 0.35, 252, 250);
        b.line(-0.35, 0, 0.35, 0, 250);
        b.line(0, -0.35, 0, 0.35, 250);
    } else if (key == "FOUNTAIN") {
        b.fillCircle(0, 0, 3.0, 254, 8);
        b.fillCircle(0, 0, 2.6, 151);
        b.fillCircle(0, 0, 0.5, 254, 8);
        for (int i = 0; i < 8; ++i) {
            const double a = 2 * kPI * i / 8;
            b.line(0.6 * std::cos(a), 0.6 * std::sin(a), 1.6 * std::cos(a), 1.6 * std::sin(a), 4);
        }
    } else if (key == "PARKING") {
        b.fillRound(-0.6, -0.6, 0.6, 0.6, 0.12, 5, 7);
        b.text(0, 0, 0.8, "P", 7);
    } else if (key == "CROSSWALK") {
        for (int i = -3; i <= 3; ++i) b.fillRect(i * 1.0 - 0.25, -2.0, i * 1.0 + 0.25, 2.0, 7);
    } else if (key == "ARROW") {
        b.fill(PolylineEntity({{-2.5, -0.15}, {1.0, -0.15}, {1.0, -0.5}, {2.5, 0}, {1.0, 0.5}, {1.0, 0.15}, {-2.5, 0.15}}, true), 7);
    } else if (key == "NORTH") {
        b.circle(0, 0, 1.2, 7);
        b.fill(PolylineEntity({{0, 1.1}, {0.4, -0.8}, {0, -0.4}}, true), 7);
        b.poly({{0, 1.1}, {-0.4, -0.8}, {0, -0.4}}, true, 7);
        b.text(0, 1.6, 0.6, "N", 7);
    } else {
        return nullptr;
    }
    auto def = std::make_shared<BlockDef>();
    def->name = symbolBlockName(key);
    def->base = {0, 0, 0};
    def->entities = std::move(b.es);
    return def;
}

} // namespace asiacad::civil
