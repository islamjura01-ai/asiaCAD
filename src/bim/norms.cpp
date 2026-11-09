// AsiaCAD — ShNQ normativ tekshiruvi: ifodalar, qoidalar jadvali, IFC o'lchamlari, armatura himoya qatlami.
#include <asiacad/bim/norms.h>

#include <asiacad/bim/building.h>
#include <asiacad/bim/rebar.h>
#include <asiacad/i18n/json.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace asiacad::bim {

namespace {

constexpr double kPi = 3.14159265358979323846;
using Tri = std::array<Vec3, 3>;

// ======================================================================= ifodalar
struct EVal {
    NormValue v = 0.0;
    bool missing = false;
};

bool truthy(const NormValue& v) {
    if (const double* d = std::get_if<double>(&v)) return *d != 0 && !std::isnan(*d);
    return !std::get<std::string>(v).empty();
}

class Expr {
public:
    Expr(std::string_view s, const NormVars* vars, const NormProps* props) : src_(s), vars_(vars), props_(props) { tokenize(); }

    bool ok() const { return err_.empty(); }
    const std::string& error() const { return err_; }

    EVal run() {
        if (!err_.empty()) return {0.0, true};
        EVal r = ternary();
        if (err_.empty() && peek().k != Tk::End) fail("ortiqcha belgi: " + peek().s);
        return r;
    }

private:
    enum class Tk { Num, Str, Id, Op, End };
    struct Tok {
        Tk k = Tk::End;
        double num = 0;
        std::string s;
    };
    std::string_view src_;
    const NormVars* vars_;
    const NormProps* props_;
    std::vector<Tok> toks_;
    std::size_t pos_ = 0;
    std::string err_;

    void fail(const std::string& m) {
        if (err_.empty()) err_ = m;
    }
    const Tok& peek() const { return toks_[std::min(pos_, toks_.size() - 1)]; }
    bool isOp(const char* o) const { return peek().k == Tk::Op && peek().s == o; }
    bool accept(const char* o) {
        if (isOp(o)) { ++pos_; return true; }
        return false;
    }
    void expect(const char* o) {
        if (!accept(o)) fail(std::string("kutilgan: ") + o);
    }

    void tokenize() {
        std::size_t i = 0;
        const std::size_t n = src_.size();
        auto identCh = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
        while (i < n) {
            const char c = src_[i];
            if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
            if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(src_[i + 1])))) {
                std::size_t j = i;
                while (j < n && (std::isdigit(static_cast<unsigned char>(src_[j])) || src_[j] == '.')) ++j;
                if (j < n && (src_[j] == 'e' || src_[j] == 'E') && j + 1 < n &&
                    (std::isdigit(static_cast<unsigned char>(src_[j + 1])) || ((src_[j + 1] == '-' || src_[j + 1] == '+') && j + 2 < n))) {
                    j += 2;
                    while (j < n && std::isdigit(static_cast<unsigned char>(src_[j]))) ++j;
                }
                Tok t;
                t.k = Tk::Num;
                t.num = std::strtod(std::string(src_.substr(i, j - i)).c_str(), nullptr);
                // birlik: 15mm, 2.5cm, 1.2m
                auto suffix = [&](std::string_view u) { return src_.substr(j, u.size()) == u && (j + u.size() >= n || !identCh(src_[j + u.size()])); };
                if (suffix("mm")) { t.num *= 0.001; j += 2; }
                else if (suffix("cm")) { t.num *= 0.01; j += 2; }
                else if (suffix("m")) { j += 1; }
                else if (j < n && identCh(src_[j])) { fail("son ortidan noma'lum birlik"); return; }
                toks_.push_back(t);
                i = j;
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                std::size_t j = i;
                while (j < n && identCh(src_[j])) ++j;
                Tok t;
                t.k = Tk::Id;
                t.s = std::string(src_.substr(i, j - i));
                if (t.s == "and") { t.k = Tk::Op; t.s = "&&"; }
                else if (t.s == "or") { t.k = Tk::Op; t.s = "||"; }
                else if (t.s == "not") { t.k = Tk::Op; t.s = "!"; }
                toks_.push_back(t);
                i = j;
                continue;
            }
            if (c == '"' || c == '\'') {
                std::size_t j = i + 1;
                std::string s;
                while (j < n && src_[j] != c) s += src_[j++];
                if (j >= n) { fail("satr yopilmagan"); return; }
                toks_.push_back({Tk::Str, 0, s});
                i = j + 1;
                continue;
            }
            static const char* ops2[] = {"<=", ">=", "==", "!=", "&&", "||"};
            bool two = false;
            for (const char* o : ops2)
                if (src_.substr(i, 2) == o) {
                    toks_.push_back({Tk::Op, 0, o});
                    i += 2;
                    two = true;
                    break;
                }
            if (two) continue;
            if (std::string_view("<>+-*/!(),?:=").find(c) != std::string_view::npos) {
                toks_.push_back({Tk::Op, 0, c == '=' ? std::string("==") : std::string(1, c)});
                ++i;
                continue;
            }
            fail(std::string("noma'lum belgi: ") + c);
            return;
        }
        toks_.push_back({Tk::End, 0, {}});
    }

    static double numOf(const EVal& v) {
        if (const double* d = std::get_if<double>(&v.v)) return *d;
        return std::strtod(std::get<std::string>(v.v).c_str(), nullptr);
    }
    static bool isNum(const EVal& v) { return std::holds_alternative<double>(v.v); }

    EVal ternary() {
        EVal c = orExpr();
        if (accept("?")) {
            EVal a = ternary();
            expect(":");
            EVal b = ternary();
            if (c.missing) return {0.0, true};
            return truthy(c.v) ? a : b;
        }
        return c;
    }
    EVal orExpr() {
        EVal l = andExpr();
        while (accept("||")) {
            EVal r = andExpr();
            if (!l.missing && truthy(l.v)) l = {1.0, false};
            else if (!r.missing && truthy(r.v)) l = {1.0, false};
            else if (l.missing || r.missing) l = {0.0, true};
            else l = {0.0, false};
        }
        return l;
    }
    EVal andExpr() {
        EVal l = cmp();
        while (accept("&&")) {
            EVal r = cmp();
            if (!l.missing && !truthy(l.v)) l = {0.0, false};
            else if (!r.missing && !truthy(r.v)) l = {0.0, false};
            else if (l.missing || r.missing) l = {0.0, true};
            else l = {1.0, false};
        }
        return l;
    }
    EVal cmp() {
        EVal l = add();
        static const char* ops[] = {"<=", ">=", "==", "!=", "<", ">"};
        for (const char* o : ops)
            if (accept(o)) {
                EVal r = add();
                if (l.missing || r.missing) return {0.0, true};
                const std::string op = o;
                if (!isNum(l) || !isNum(r)) {
                    // satrlar: katta-kichik harfsiz solishtirish
                    if (isNum(l) != isNum(r)) return {op == "!=" ? 1.0 : 0.0, false};
                    const std::string a = utf8::foldKey(std::get<std::string>(l.v)), b = utf8::foldKey(std::get<std::string>(r.v));
                    double res = 0;
                    if (op == "==") res = a == b;
                    else if (op == "!=") res = a != b;
                    else if (op == "<") res = a < b;
                    else if (op == ">") res = a > b;
                    else if (op == "<=") res = a <= b;
                    else res = a >= b;
                    return {res, false};
                }
                const double a = numOf(l), b = numOf(r);
                double res = 0;
                if (op == "==") res = std::abs(a - b) <= 1e-9 * std::max(1.0, std::abs(a));
                else if (op == "!=") res = !(std::abs(a - b) <= 1e-9 * std::max(1.0, std::abs(a)));
                else if (op == "<") res = a < b;
                else if (op == ">") res = a > b;
                else if (op == "<=") res = a <= b + 1e-12;
                else res = a >= b - 1e-12;
                return {res, false};
            }
        return l;
    }
    EVal add() {
        EVal l = mul();
        for (;;) {
            if (accept("+")) {
                EVal r = mul();
                l = {numOf(l) + numOf(r), l.missing || r.missing};
            } else if (accept("-")) {
                EVal r = mul();
                l = {numOf(l) - numOf(r), l.missing || r.missing};
            } else break;
        }
        return l;
    }
    EVal mul() {
        EVal l = unary();
        for (;;) {
            if (accept("*")) {
                EVal r = unary();
                l = {numOf(l) * numOf(r), l.missing || r.missing};
            } else if (accept("/")) {
                EVal r = unary();
                const double d = numOf(r);
                l = {d != 0 ? numOf(l) / d : std::nan(""), l.missing || r.missing || d == 0};
            } else break;
        }
        return l;
    }
    EVal unary() {
        if (accept("-")) {
            EVal v = unary();
            return {-numOf(v), v.missing};
        }
        if (accept("+")) return unary();
        if (accept("!")) {
            EVal v = unary();
            return {truthy(v.v) ? 0.0 : 1.0, v.missing};
        }
        return primary();
    }
    std::vector<EVal> args() {
        std::vector<EVal> a;
        expect("(");
        if (accept(")")) return a;
        for (;;) {
            a.push_back(ternary());
            if (accept(",")) continue;
            expect(")");
            break;
        }
        return a;
    }
    EVal primary() {
        const Tok t = peek();
        if (t.k == Tk::Num) { ++pos_; return {t.num, false}; }
        if (t.k == Tk::Str) { ++pos_; return {t.s, false}; }
        if (accept("(")) {
            EVal v = ternary();
            expect(")");
            return v;
        }
        if (t.k == Tk::Id) {
            ++pos_;
            if (isOp("(")) {
                const auto a = args();
                const std::string f = t.s;
                auto anyMissing = [&] { return std::any_of(a.begin(), a.end(), [](const EVal& e) { return e.missing; }); };
                if (f == "min" || f == "max") {
                    if (a.empty()) { fail(f + "() — argument yo'q"); return {0.0, true}; }
                    double r = numOf(a[0]);
                    for (std::size_t i = 1; i < a.size(); ++i) r = f == "min" ? std::min(r, numOf(a[i])) : std::max(r, numOf(a[i]));
                    return {r, anyMissing()};
                }
                if (f == "abs" || f == "sqrt" || f == "floor" || f == "ceil") {
                    if (a.size() != 1) { fail(f + "() — bitta argument"); return {0.0, true}; }
                    const double x = numOf(a[0]);
                    const double r = f == "abs" ? std::abs(x) : f == "sqrt" ? std::sqrt(std::max(0.0, x)) : f == "floor" ? std::floor(x) : std::ceil(x);
                    return {r, a[0].missing};
                }
                if (f == "round") {
                    if (a.empty() || a.size() > 2) { fail("round(x[, n])"); return {0.0, true}; }
                    const double k = a.size() == 2 ? std::pow(10.0, numOf(a[1])) : 1.0;
                    return {std::round(numOf(a[0]) * k) / k, anyMissing()};
                }
                if (f == "if") {
                    if (a.size() != 3) { fail("if(shart, ha, yo'q)"); return {0.0, true}; }
                    if (a[0].missing) return {0.0, true};
                    return truthy(a[0].v) ? a[1] : a[2];
                }
                if (f == "prop" || f == "has") {
                    if (a.size() != 1 || isNum(a[0])) { fail(f + "(\"Pset.Nomi\")"); return {0.0, true}; }
                    const std::string key = utf8::foldKey(std::get<std::string>(a[0].v));
                    if (props_)
                        for (const auto& [k, v] : *props_)
                            if (utf8::foldKey(k) == key) return f == "has" ? EVal{1.0, false} : EVal{v, false};
                    return f == "has" ? EVal{0.0, false} : EVal{std::string(), true};
                }
                fail("noma'lum funksiya: " + f);
                return {0.0, true};
            }
            if (t.s == "true") return {1.0, false};
            if (t.s == "false") return {0.0, false};
            if (vars_)
                if (auto it = vars_->find(t.s); it != vars_->end()) return {it->second, false};
            return {0.0, true};
        }
        fail(t.k == Tk::End ? "ifoda tugallanmagan" : "kutilmagan: " + t.s);
        ++pos_;
        return {0.0, true};
    }
};

// ======================================================================= geometriya
Vec3 closestOnTri(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    // Ericson, "Real-Time Collision Detection" 5.1.5
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = ab.dot(ap), d2 = ac.dot(ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const Vec3 bp = p - b;
    const double d3 = ab.dot(bp), d4 = ac.dot(bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    const Vec3 cp = p - c;
    const double d5 = ab.dot(cp), d6 = ac.dot(cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const double den = 1.0 / (va + vb + vc);
    return a + ab * (vb * den) + ac * (vc * den);
}

// Möller–Trumbore: t (> 0) yoki -1
double rayTri(const Vec3& o, const Vec3& d, const Tri& t) {
    const Vec3 e1 = t[1] - t[0], e2 = t[2] - t[0];
    const Vec3 h = d.cross(e2);
    const double a = e1.dot(h);
    if (std::abs(a) < 1e-14) return -1;
    const double f = 1.0 / a;
    const Vec3 s = o - t[0];
    const double u = f * s.dot(h);
    if (u < 0 || u > 1) return -1;
    const Vec3 q = s.cross(e1);
    const double v = f * d.dot(q);
    if (v < 0 || u + v > 1) return -1;
    const double tt = f * e2.dot(q);
    return tt > 1e-10 ? tt : -1;
}

bool inBox(const BBox& b, const Vec3& p, double eps) {
    return p.x >= b.min.x - eps && p.x <= b.max.x + eps && p.y >= b.min.y - eps && p.y <= b.max.y + eps && p.z >= b.min.z - eps &&
           p.z <= b.max.z + eps;
}

Vec3 unitv(const Vec3& v) {
    const double l = v.length();
    return l > 1e-15 ? v / l : Vec3{0, 0, 0};
}

// Gorizontal kesim konturidan: polosa eni (min o'lcham) va max o'lcham. To'rtburchak b×h -> (min, max) aniq;
// doira -> diametr; o'zgarmas enli egri polosa (devor) -> eni.
struct SectionDims {
    double minDim = 0, maxDim = 0, area = 0;
    bool ok = false;
};

SectionDims sectionDims(const IfcModel& m, int ei, double z) {
    SectionDims r;
    const auto loops = ifcSection(m, z, {ei}, {});
    double best = 0;
    for (const auto& L : loops) {
        if (!L.closed || L.pts.size() < 3) continue;
        double A = 0, P = 0;
        for (std::size_t i = 0; i < L.pts.size(); ++i) {
            const Vec3& a = L.pts[i];
            const Vec3& b = L.pts[(i + 1) % L.pts.size()];
            A += a.x * b.y - b.x * a.y;
            P += a.distanceTo(b);
        }
        A = std::abs(A) * 0.5;
        if (A <= best || P <= 0) continue;
        best = A;
        const double s = P / 2, disc = s * s - 4 * A;
        r.area = A;
        if (disc > 1e-12) {
            r.minDim = (s - std::sqrt(disc)) / 2;
            r.maxDim = (s + std::sqrt(disc)) / 2;
        } else {
            r.minDim = r.maxDim = 2 * A / s;
        }
        r.ok = true;
    }
    return r;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::optional<double> propNum(const IfcElement& e, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        const std::string fk = utf8::foldKey(k);
        for (const auto& [pk, pv] : e.props) {
            const std::string f = utf8::foldKey(pk);
            if (f.size() >= fk.size() && f.compare(f.size() - fk.size(), fk.size(), fk) == 0) {
                char* end = nullptr;
                const double v = std::strtod(pv.c_str(), &end);
                if (end != pv.c_str()) return v;
            }
        }
    }
    return std::nullopt;
}

// ======================================================================= armatura: beton birlashmasi va himoya qatlami
struct Solid {
    int el = -1;
    BBox box;
    std::vector<Tri> tris;
    double angle = 0;
};

class SolidIndex {
public:
    explicit SolidIndex(std::vector<Solid> s) : solids(std::move(s)) {
        BBox all;
        for (const auto& so : solids) {
            all.expand(so.box.min);
            all.expand(so.box.max);
        }
        if (!all.valid()) return;
        x0_ = all.min.x;
        y0_ = all.min.y;
        const double w = all.max.x - all.min.x, h = all.max.y - all.min.y;
        cs_ = std::max({0.5, std::sqrt(std::max(1e-9, w * h) / 400000.0)});
        nx_ = std::max(1, static_cast<int>(std::ceil(w / cs_)) + 1);
        ny_ = std::max(1, static_cast<int>(std::ceil(h / cs_)) + 1);
        cells_.resize(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_));
        for (std::size_t i = 0; i < solids.size(); ++i) {
            const auto& b = solids[i].box;
            for (int y = cy(b.min.y); y <= cy(b.max.y); ++y)
                for (int x = cx(b.min.x); x <= cx(b.max.x); ++x) cells_[static_cast<std::size_t>(y) * nx_ + x].push_back(static_cast<int>(i));
        }
    }
    std::vector<Solid> solids;

    const std::vector<int>& cell(const Vec3& p) const {
        static const std::vector<int> none;
        if (cells_.empty()) return none;
        const int x = cx(p.x), y = cy(p.y);
        return cells_[static_cast<std::size_t>(y) * nx_ + x];
    }
    std::vector<int> range(const Vec3& lo, const Vec3& hi) const {
        std::vector<int> r;
        if (cells_.empty()) return r;
        for (int y = cy(lo.y); y <= cy(hi.y); ++y)
            for (int x = cx(lo.x); x <= cx(hi.x); ++x)
                for (int i : cells_[static_cast<std::size_t>(y) * nx_ + x])
                    if (std::find(r.begin(), r.end(), i) == r.end()) r.push_back(i);
        return r;
    }
    bool inside(int si, const Vec3& p) const {
        const Solid& s = solids[static_cast<std::size_t>(si)];
        if (!inBox(s.box, p, 1e-7)) return false;
        static const Vec3 dir = unitv({0.01371, 0.02113, 1.0});   // og'ma nur — qirralarga tushmaslik uchun
        int hits = 0;
        for (const auto& t : s.tris)
            if (rayTri(p, dir, t) > 0) ++hits;
        return hits % 2 == 1;
    }
    std::vector<int> containing(const Vec3& p) const {
        std::vector<int> r;
        for (int i : cell(p))
            if (inside(i, p)) r.push_back(i);
        return r;
    }
    bool insideAny(const Vec3& p) const {
        for (int i : cell(p))
            if (inside(i, p)) return true;
        return false;
    }

private:
    double x0_ = 0, y0_ = 0, cs_ = 1;
    int nx_ = 0, ny_ = 0;
    std::vector<std::vector<int>> cells_;
    int cx(double x) const { return std::clamp(static_cast<int>((x - x0_) / cs_), 0, nx_ - 1); }
    int cy(double y) const { return std::clamp(static_cast<int>((y - y0_) / cs_), 0, ny_ - 1); }
};

// p nuqtadan beton birlashmasining tashqi sirtigacha eng qisqa masofa (p birlashma ichida; C — p ni o'z ichiga olganlar)
double coverAt(const SolidIndex& ix, const Vec3& p, const std::vector<int>& C, double lmax) {
    // 1) Har bir C elementining eng yaqin sirt nuqtasi; birlashma masofasi >= max_s d_s.
    double dmax = -1;
    Vec3 qbest;
    for (int si : C) {
        double best = 1e300;
        Vec3 bq;
        for (const auto& t : ix.solids[static_cast<std::size_t>(si)].tris) {
            const Vec3 q = closestOnTri(p, t[0], t[1], t[2]);
            const double d = (q - p).length();
            if (d < best) { best = d; bq = q; }
        }
        if (best > dmax) { dmax = best; qbest = bq; }
    }
    if (dmax < 1e-9) return 0;   // o'q sirtda
    // eng yaqin nuqta ochiq (tashqarida beton yo'q) bo'lsa — aniq javob
    const Vec3 out = qbest + unitv(qbest - p) * 0.002;
    if (!ix.insideAny(out)) return dmax;
    // 2) Ichki tutashuv: 26 yo'nalish bo'yicha birlashmadan chiqish (chiqish yuzasi tekisligigacha perpendikulyar)
    const auto cand = ix.range(p - Vec3{lmax, lmax, lmax}, p + Vec3{lmax, lmax, lmax});
    std::vector<int> local;
    for (int si : cand) {
        const BBox& b = ix.solids[static_cast<std::size_t>(si)].box;
        if (b.max.x < p.x - lmax || b.min.x > p.x + lmax || b.max.y < p.y - lmax || b.min.y > p.y + lmax || b.max.z < p.z - lmax || b.min.z > p.z + lmax)
            continue;
        local.push_back(si);
    }
    const double ang = C.empty() ? 0 : ix.solids[static_cast<std::size_t>(C.front())].angle;
    const double ca = std::cos(ang), sa = std::sin(ang);
    double best = lmax;
    struct Ev {
        double t;
        int s;
        Vec3 n;
    };
    std::vector<Ev> ev;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz) {
                if (!dx && !dy && !dz) continue;
                Vec3 d = unitv({static_cast<double>(dx), static_cast<double>(dy), static_cast<double>(dz)});
                d = {d.x * ca - d.y * sa, d.x * sa + d.y * ca, d.z};
                ev.clear();
                for (int si : local)
                    for (const auto& t : ix.solids[static_cast<std::size_t>(si)].tris) {
                        const double tt = rayTri(p, d, t);
                        if (tt > 0 && tt <= lmax) ev.push_back({tt, si, unitv((t[1] - t[0]).cross(t[2] - t[0]))});
                    }
                std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
                std::set<int> in(C.begin(), C.end());
                for (std::size_t i = 0; i < ev.size();) {
                    std::size_t j = i;
                    std::set<int> toggled;
                    Vec3 nOut;
                    while (j < ev.size() && ev[j].t - ev[i].t < 1e-7) {
                        if (toggled.insert(ev[j].s).second) {
                            if (in.count(ev[j].s)) {
                                in.erase(ev[j].s);
                                nOut = ev[j].n;
                            } else in.insert(ev[j].s);
                        }
                        ++j;
                    }
                    if (in.empty()) {
                        best = std::min(best, ev[i].t * std::abs(d.dot(nOut)));
                        break;
                    }
                    i = j;
                }
            }
    return best;
}

struct BarResult {
    double cover = 1e300;     // aniq himoya qatlami (sirtdan), m
    Vec3 at;
    std::map<int, int> votes; // solid -> nuqtalar soni
    int samples = 0, embedded = 0;
};

BarResult analyseBar(const SolidIndex& ix, const std::vector<Vec3>& pts, double r, double step, bool closed) {
    BarResult res;
    double L = 0;
    for (std::size_t i = 1; i < pts.size(); ++i) L += pts[i].distanceTo(pts[i - 1]);
    std::vector<double> ss;
    if (L <= 1e-9) ss = {0};
    else if (closed) {
        const int n = std::max(4, static_cast<int>(std::ceil(L / step)));
        for (int i = 0; i < n; ++i) ss.push_back(L * (i + 0.5) / n);
    } else if (L <= 0.12) ss = {L / 2};
    else {
        // uchlardan 50 mm ichkarida (sterjen uchining torets himoya qatlami — alohida talab)
        const double a = 0.05, b = L - 0.05;
        const int n = std::max(2, static_cast<int>(std::ceil((b - a) / step)) + 1);
        for (int i = 0; i < n; ++i) ss.push_back(a + (b - a) * i / (n - 1));
    }
    std::size_t seg = 1;
    double acc = 0;
    for (double s : ss) {
        while (seg + 1 < pts.size() && acc + pts[seg].distanceTo(pts[seg - 1]) < s) {
            acc += pts[seg].distanceTo(pts[seg - 1]);
            ++seg;
        }
        Vec3 p = pts.front();
        if (pts.size() >= 2) {
            const double sl = pts[seg].distanceTo(pts[seg - 1]);
            const double k = sl > 1e-12 ? std::clamp((s - acc) / sl, 0.0, 1.0) : 0.0;
            p = pts[seg - 1] + (pts[seg] - pts[seg - 1]) * k;
        }
        ++res.samples;
        const auto C = ix.containing(p);
        if (C.empty()) continue;
        ++res.embedded;
        for (int si : C) ++res.votes[si];
        double c = coverAt(ix, p, C, 0.25);
        if (c < 1e-9 && pts.size() >= 2) {
            // nuqta ichki tutashuv yuzasida (masalan plita usti = ustun osti): sterjen o'qi bo'ylab ±1 mm da qayta o'lchash.
            // Haqiqatan sirtda yotgan sterjen uchun ikkalasi ham ~0 — xato saqlanadi.
            const Vec3 tan = unitv(pts[seg] - pts[seg - 1]);
            for (const double sh : {0.001, -0.001}) {
                const Vec3 q = p + tan * sh;
                if (const auto Cq = ix.containing(q); !Cq.empty()) c = std::max(c, coverAt(ix, q, Cq, 0.25));
            }
        }
        c -= r;
        if (c < res.cover) {
            res.cover = c;
            res.at = p;
        }
    }
    return res;
}

bool isStirrupBar(const IfcElement& e, const std::vector<Vec3>& pts) {
    static const std::set<std::string> roles{"LIGATURE", "SHEAR", "STIRRUP", "RING", "SPACEBAR", "PUNCHING"};
    if (roles.count(e.predefined)) return true;
    if (e.predefined == "MAIN" || e.predefined == "EDGE" || e.predefined == "ANCHORING") return false;
    const std::string n = utf8::foldKey(e.name + " " + e.objectType);
    for (const char* k : {"XOMUT", "ХОМУТ", "STIRRUP", "BUGEL", "BÜGEL", "LINK", "TIE", "ШПИЛЬК", "SHPILKA"})
        if (n.find(utf8::foldKey(k)) != std::string::npos) return true;
    if (pts.size() >= 4) {
        double L = 0;
        for (std::size_t i = 1; i < pts.size(); ++i) L += pts[i].distanceTo(pts[i - 1]);
        if (L > 0 && pts.front().distanceTo(pts.back()) < 0.3 * L) return true;   // yopiq yoki deyarli yopiq kontur
    }
    return false;
}

const std::set<std::string>& structuralCats() {
    static const std::set<std::string> s{"WALL", "SLAB", "COLUMN", "BEAM", "FOOTING", "PILE", "STAIR", "ROOF", "PROXY", "OTHER"};
    return s;
}

// ======================================================================= qoidalar
NormRule mk(const char* id, const char* norm, const char* clause, std::vector<std::string> cats, const char* when, const char* param,
            const char* mn, const char* mx, int sev, bool verified, const char* unit, const char* uz, const char* ru, const char* en,
            const char* note = "", bool concreteOnly = false, bool enabled = true) {
    NormRule r;
    r.id = id;
    r.norm = norm;
    r.clause = clause;
    r.cats = std::move(cats);
    r.when = when;
    r.param = param;
    r.min = mn;
    r.max = mx;
    r.severity = sev;
    r.verified = verified;
    r.unit = unit;
    r.title = {{"uz", uz}, {"ru", ru}, {"en", en}};
    r.note = note;
    r.concreteOnly = concreteOnly;
    r.enabled = enabled;
    return r;
}

std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (const unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) o += std::format("\\u{:04x}", c);
        else o += static_cast<char>(c);
    }
    return o + "\"";
}

} // namespace

// ======================================================================= ochiq API
std::string NormRule::titleFor(const std::string& lang) const {
    auto get = [&](const std::string& l) -> const std::string* {
        auto it = title.find(l);
        return it != title.end() && !it->second.empty() ? &it->second : nullptr;
    };
    if (auto* s = get(lang)) return *s;
    if (lang == "kk" || lang == "ky")
        if (auto* s = get("ru")) return *s;
    if (lang == "tr")
        if (auto* s = get("en")) return *s;
    for (const char* l : {"uz", "ru", "en"})
        if (auto* s = get(l)) return *s;
    return id;
}

std::vector<NormRule> defaultNormRules() {
    const char* RC = "ShNQ 2.03.01-24";
    const char* NV = "Qiymat normaning matni bilan solishtirilmagan — tasdiqlang yoki tuzating.";
    return {
        // ---- armatura himoya qatlami (8-bob, konstruktiv talablar) — Armatura 3D modulidagi tekshiruv bilan bir xil
        mk("rc.cover.slab", RC, "376a", {"REBAR"}, "host == \"SLAB\"", "cover", "max(if(hostT <= 100mm, 10mm, 15mm), d)", "", 2, true, "mm",
           "Plita armaturasi himoya qatlami", "Защитный слой арматуры плиты", "Slab rebar concrete cover"),
        mk("rc.cover.wall.long", RC, "376a", {"REBAR"}, "host == \"WALL\" && vertical && !stirrup", "cover",
           "max(if(hostT <= 100mm, 10mm, 15mm), d)", "", 2, true, "mm", "Devor: bo'ylama armatura himoya qatlami",
           "Стена: защитный слой продольной арматуры", "Wall: longitudinal rebar cover"),
        mk("rc.cover.wall.tr", RC, "378", {"REBAR"}, "host == \"WALL\" && (!vertical || stirrup)", "cover",
           "max(if(hostT < 250mm, 10mm, 15mm), d)", "", 2, true, "mm", "Devor: ko'ndalang armatura himoya qatlami",
           "Стена: защитный слой поперечной арматуры", "Wall: transverse rebar cover"),
        mk("rc.cover.col.long", RC, "376b", {"REBAR"}, "host == \"COLUMN\" && !stirrup", "cover", "max(20mm, d)", "", 2, true, "mm",
           "Ustun: bo'ylama armatura himoya qatlami", "Колонна: защитный слой продольной арматуры", "Column: longitudinal rebar cover"),
        mk("rc.cover.col.tr", RC, "378", {"REBAR"}, "host == \"COLUMN\" && stirrup", "cover", "max(if(hostMin < 250mm, 10mm, 15mm), d)", "", 2, true,
           "mm", "Ustun: xomut himoya qatlami", "Колонна: защитный слой хомутов", "Column: stirrup cover"),
        mk("rc.cover.beam.long", RC, "376a, 376b", {"REBAR"}, "host == \"BEAM\" && !stirrup", "cover", "max(if(hostH >= 250mm, 20mm, 15mm), d)", "",
           1, false, "mm", "To'sin: bo'ylama armatura himoya qatlami", "Балка: защитный слой продольной арматуры", "Beam: longitudinal rebar cover",
           NV),
        mk("rc.cover.beam.tr", RC, "378", {"REBAR"}, "host == \"BEAM\" && stirrup", "cover", "max(if(hostH < 250mm, 10mm, 15mm), d)", "", 1, false,
           "mm", "To'sin: xomut himoya qatlami", "Балка: защитный слой хомутов", "Beam: stirrup cover", NV),
        mk("rc.cover.found", RC, "376v", {"REBAR"}, "host == \"FOOTING\"", "cover", "max(if(lean, 35mm, 70mm), d)", "", 2, true, "mm",
           "Fundament armaturasi himoya qatlami (padbetonka bilan 35, usiz 70 mm)", "Защитный слой арматуры фундамента (с подготовкой 35, без — 70 мм)",
           "Footing rebar cover (35 mm with blinding, 70 mm without)"),
        mk("rc.col.dmin", RC, "411", {"REBAR"}, "host == \"COLUMN\" && !stirrup", "d", "12mm", "", 2, true, "mm", "Ustun bo'ylama armaturasi diametri",
           "Диаметр продольной арматуры колонны", "Column longitudinal bar diameter"),
        mk("rc.col.dmax", RC, "409", {"REBAR"}, "host == \"COLUMN\" && !stirrup", "d", "", "40mm", 2, true, "mm",
           "Ustun bo'ylama armaturasi diametri (eng katta)", "Диаметр продольной арматуры колонны (наибольший)", "Column longitudinal bar diameter (max)"),
        mk("rc.stir.dmin", RC, "434", {"REBAR"}, "host == \"COLUMN\" && stirrup", "d", "max(5mm, hostMainD / 4)", "", 2, true, "mm",
           "Ustun xomuti diametri (≥ 0,25d va ≥ 5 mm)", "Диаметр хомутов колонны (≥ 0,25d и ≥ 5 мм)", "Column stirrup diameter (≥ 0.25d and ≥ 5 mm)"),
        // ---- tasdiqlanmagan (ogohlantirish): qiymatlar KMK 2.08.01 / SP 54 / SNiP 21-01-97 dan; amaldagi ShNQ matni bilan solishtiring
        mk("slab.min_t", RC, "", {"SLAB"}, "pred == \"FLOOR\" || pred == \"ROOF\" || pred == \"\"", "thickness", "50mm", "", 1, false, "mm",
           "Monolit plita qalinligi (qavatlararo yopma)", "Толщина монолитной плиты перекрытия", "Cast-in-place slab thickness", NV, true),
        mk("stair.width", "ShNQ 2.08.01-24", "", {"STAIR"}, "type == \"IFCSTAIRFLIGHT\"", "width", "1.05m", "", 1, false, "m",
           "Zinapoya marshi eni", "Ширина лестничного марша", "Stair flight width",
           "Ko'p xonadonli turar-joy (KMK 2.08.01 / SP 54 qiymati). Normaning matni bilan solishtirilmagan."),
        mk("stair.riser", "ShNQ 2.08.01-24", "", {"STAIR"}, "type == \"IFCSTAIRFLIGHT\"", "riser", "", "0.18m", 1, false, "mm",
           "Pog'ona balandligi", "Высота ступени", "Riser height", NV),
        mk("stair.tread", "ShNQ 2.08.01-24", "", {"STAIR"}, "type == \"IFCSTAIRFLIGHT\"", "tread", "0.25m", "", 1, false, "mm",
           "Pog'ona eni", "Ширина проступи", "Tread depth", NV),
        mk("railing.height", "ShNQ 2.08.01-24", "", {"RAILING"}, "pred != \"HANDRAIL\"", "height", "0.9m", "", 1, false, "m",
           "To'siq (panjara) balandligi", "Высота ограждения", "Guard height", NV),
        mk("door.exit.width", "ShNQ 2.01.02-04", "", {"DOOR"}, "prop(\"Pset_DoorCommon.FireExit\") == \"TRUE\" || prop(\"Pset_DoorCommon.IsExternal\") == \"TRUE\"",
           "width", "0.8m", "", 1, false, "m", "Evakuatsiya chiqishi (eshik) eni", "Ширина эвакуационного выхода", "Exit door width",
           "Faqat FireExit yoki IsExternal = TRUE eshiklar (SNiP 21-01-97 qiymati). Normaning matni bilan solishtirilmagan."),
        mk("door.exit.height", "ShNQ 2.01.02-04", "", {"DOOR"}, "prop(\"Pset_DoorCommon.FireExit\") == \"TRUE\" || prop(\"Pset_DoorCommon.IsExternal\") == \"TRUE\"",
           "height", "1.9m", "", 1, false, "m", "Evakuatsiya chiqishi (eshik) balandligi", "Высота эвакуационного выхода", "Exit door height",
           "Faqat FireExit yoki IsExternal = TRUE eshiklar (SNiP 21-01-97 qiymati). Normaning matni bilan solishtirilmagan."),
        mk("space.height", "ShNQ 2.08.01-24", "", {"SPACE"}, "", "height", "2.5m", "", 1, false, "m", "Xona balandligi (poldan shiftgacha)",
           "Высота помещения (от пола до потолка)", "Room height (floor to ceiling)",
           "Turar-joy xonalari; iqlim kichik hududiga ko'ra 2,5 yoki 2,7 m — normadan aniqlang. Standart holatda o'chiq.", false, false),
    };
}

std::string normRulesToJson(const std::vector<NormRule>& rules) {
    std::string s = "{\n  \"format\": \"asiacad-norms\",\n  \"version\": 1,\n  \"rules\": [\n";
    for (std::size_t i = 0; i < rules.size(); ++i) {
        const auto& r = rules[i];
        s += "    {\n";
        s += "      \"id\": " + jsonStr(r.id) + ",\n";
        s += std::string("      \"enabled\": ") + (r.enabled ? "true" : "false") + ",\n";
        s += "      \"norm\": " + jsonStr(r.norm) + ",\n";
        s += "      \"clause\": " + jsonStr(r.clause) + ",\n";
        s += "      \"categories\": [";
        for (std::size_t k = 0; k < r.cats.size(); ++k) s += (k ? ", " : "") + jsonStr(r.cats[k]);
        s += "],\n";
        s += std::string("      \"concreteOnly\": ") + (r.concreteOnly ? "true" : "false") + ",\n";
        s += "      \"when\": " + jsonStr(r.when) + ",\n";
        s += "      \"param\": " + jsonStr(r.param) + ",\n";
        s += "      \"min\": " + jsonStr(r.min) + ",\n";
        s += "      \"max\": " + jsonStr(r.max) + ",\n";
        s += std::string("      \"severity\": ") + (r.severity >= 2 ? "\"error\"" : "\"warning\"") + ",\n";
        s += std::string("      \"verified\": ") + (r.verified ? "true" : "false") + ",\n";
        s += "      \"unit\": " + jsonStr(r.unit) + ",\n";
        s += "      \"title\": {";
        bool first = true;
        for (const auto& [l, t] : r.title) {
            s += (first ? "" : ", ") + jsonStr(l) + ": " + jsonStr(t);
            first = false;
        }
        s += "},\n";
        s += "      \"note\": " + jsonStr(r.note) + "\n";
        s += std::string("    }") + (i + 1 < rules.size() ? ",\n" : "\n");
    }
    return s + "  ]\n}\n";
}

std::vector<NormRule> normRulesFromJson(const std::string& text) {
    const json::Value root = json::parse(text);
    const json::Value* arr = root.isArray() ? &root : root.find("rules");
    if (!arr || !arr->isArray()) throw std::runtime_error("\"rules\" ro'yxati topilmadi");
    std::vector<NormRule> out;
    auto str = [](const json::Value& o, const char* k) -> std::string {
        const json::Value* v = o.find(k);
        if (!v) return {};
        if (v->isString()) return v->str();
        if (v->isNumber()) return std::format("{}", v->num());
        return {};
    };
    auto boolean = [](const json::Value& o, const char* k, bool def) {
        const json::Value* v = o.find(k);
        return v && v->isBool() ? v->boolean() : def;
    };
    std::set<std::string> ids;
    for (const auto& v : arr->arr()) {
        if (!v.isObject()) continue;
        NormRule r;
        r.id = str(v, "id");
        if (r.id.empty()) r.id = "rule" + std::to_string(out.size() + 1);
        while (!ids.insert(r.id).second) r.id += "_";
        r.enabled = boolean(v, "enabled", true);
        r.norm = str(v, "norm");
        r.clause = str(v, "clause");
        if (const json::Value* c = v.find("categories"); c && c->isArray())
            for (const auto& x : c->arr())
                if (x.isString() && !x.str().empty()) r.cats.push_back(x.str());
        r.concreteOnly = boolean(v, "concreteOnly", false);
        r.when = str(v, "when");
        r.param = str(v, "param");
        r.min = str(v, "min");
        r.max = str(v, "max");
        const std::string sev = lower(str(v, "severity"));
        r.severity = sev == "warning" || sev == "1" ? 1 : 2;
        r.verified = boolean(v, "verified", false);
        if (const json::Value* u = v.find("unit"); u && u->isString()) r.unit = u->str();
        if (const json::Value* t = v.find("title"); t && t->isObject()) {
            for (const auto& [l, x] : t->obj())
                if (x.isString()) r.title[l] = x.str();
        } else if (t && t->isString()) {
            r.title["uz"] = t->str();
        }
        r.note = str(v, "note");
        if (r.param.empty()) throw std::runtime_error("\"" + r.id + "\": \"param\" bo'sh");
        out.push_back(std::move(r));
    }
    return out;
}

std::optional<NormValue> normEval(const std::string& expr, const NormVars& vars, const NormProps* props, std::string* err) {
    Expr e(expr, &vars, props);
    const EVal v = e.run();
    if (!e.ok()) {
        if (err) *err = e.error();
        return std::nullopt;
    }
    if (v.missing) return std::nullopt;
    if (const double* d = std::get_if<double>(&v.v); d && std::isnan(*d)) return std::nullopt;
    return v.v;
}

bool normSyntaxOk(const std::string& expr, std::string* err) {
    Expr e(expr, nullptr, nullptr);
    e.run();
    if (!e.ok() && err) *err = e.error();
    return e.ok();
}

std::string normFormat(double v, const std::string& unit) {
    if (unit == "mm") {
        const double mm = v * 1000;
        if (std::abs(mm - std::round(mm)) < 0.005 || std::abs(mm) >= 100) return std::format("{:.0f} mm", mm);
        if (std::abs(mm * 10 - std::round(mm * 10)) < 0.05) return std::format("{:.1f} mm", mm);
        return std::format("{:.2f} mm", mm);
    }
    if (unit == "m") return std::format("{:.3f} m", v);
    if (unit.empty()) return std::format("{:.3g}", v);
    return std::format("{:.3f} {}", v, unit);
}

bool isConcreteMaterial(const std::string& material) {
    if (material.empty()) return false;
    const std::string f = utf8::foldKey(material);
    for (const char* k : {"BETON", "БЕТОН", "CONCRETE", "ЖЕЛЕЗОБЕТ", "ТЕМИР-БЕТОН", "TEMIR-BETON", "МОНОЛИТ", "MONOLIT", "STAHLBETON", "BETONARME", "CAST-IN-PLACE",
                          "CAST IN PLACE", "PRECAST", "ЖБ", "Ж/Б"})
        if (f.find(utf8::foldKey(k)) != std::string::npos) return true;
    // beton sinfi: B15, B25, B30, C25/30, M300
    for (std::size_t i = 0; i + 2 < f.size(); ++i)
        if ((f[i] == 'B' || f[i] == 'C' || f[i] == 'M') && (i == 0 || !std::isalpha(static_cast<unsigned char>(f[i - 1]))) &&
            std::isdigit(static_cast<unsigned char>(f[i + 1])) && std::isdigit(static_cast<unsigned char>(f[i + 2]))) {
            if (f[i] == 'M' && (i + 3 >= f.size() || !std::isdigit(static_cast<unsigned char>(f[i + 3])))) continue;   // M10 — bolt
            return true;
        }
    return false;
}

std::vector<int> NormReport::worstByElement(std::size_t elements) const {
    std::vector<int> w(elements, 0);
    for (const auto& v : items)
        if (v.element >= 0 && static_cast<std::size_t>(v.element) < elements) w[static_cast<std::size_t>(v.element)] = std::max(w[static_cast<std::size_t>(v.element)], v.severity);
    return w;
}

std::vector<NormSubject> ifcNormSubjects(const IfcModel& m, int* rebarSamples) {
    std::vector<NormSubject> out;
    const std::size_t n = m.elements.size();
    std::vector<NormVars> base(n);
    // ---- 1. har element: umumiy va toifaga xos o'lchamlar
    for (std::size_t i = 0; i < n; ++i) {
        const auto& e = m.elements[i];
        NormVars& v = base[i];
        const std::string cat = ifcCategory(e.type);
        v["cat"] = cat;
        v["type"] = e.type;
        v["pred"] = e.predefined;
        v["name"] = e.name;
        v["material"] = e.material;
        v["tag"] = e.tag;
        v["concrete"] = isConcreteMaterial(e.material) ? 1.0 : 0.0;
        double elev = 0;
        if (e.storey >= 0 && e.storey < static_cast<int>(m.storeys.size())) {
            elev = m.storeys[static_cast<std::size_t>(e.storey)].elevation;
            v["storey"] = m.storeys[static_cast<std::size_t>(e.storey)].name;
            v["elev"] = elev;
            v["level"] = static_cast<double>(e.storey);
        }
        v["volume"] = e.volume;
        if (!e.box.valid()) continue;
        const double h = e.box.max.z - e.box.min.z;
        v["height"] = h;
        v["zBottom"] = e.box.min.z;
        v["zTop"] = e.box.max.z;
        v["relBottom"] = e.box.min.z - elev;
        // lokal o'qlar bo'yicha rejadagi o'lchamlar
        const double ca = std::cos(e.angle), sa = std::sin(e.angle);
        double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300, horiz = 0;
        for (const auto& part : e.parts)
            for (const auto& t : part.tris) {
                for (const auto& p : t) {
                    const double lx = p.x * ca + p.y * sa, ly = -p.x * sa + p.y * ca;
                    x0 = std::min(x0, lx);
                    x1 = std::max(x1, lx);
                    y0 = std::min(y0, ly);
                    y1 = std::max(y1, ly);
                }
                const Vec3 nn = (t[1] - t[0]).cross(t[2] - t[0]);
                const double l = nn.length();
                if (l > 0 && std::abs(nn.z) / l > 0.3) horiz += l * 0.5;
            }
        for (const auto& b : e.bars)
            for (const auto& p : b.first) {
                const double lx = p.x * ca + p.y * sa, ly = -p.x * sa + p.y * ca;
                x0 = std::min(x0, lx);
                x1 = std::max(x1, lx);
                y0 = std::min(y0, ly);
                y1 = std::max(y1, ly);
            }
        if (x1 >= x0) {
            v["dx"] = x1 - x0;
            v["dy"] = y1 - y0;
            v["planMin"] = std::min(x1 - x0, y1 - y0);
            v["planMax"] = std::max(x1 - x0, y1 - y0);
        }
        const double planMin = x1 >= x0 ? std::min(x1 - x0, y1 - y0) : 0, planMax = x1 >= x0 ? std::max(x1 - x0, y1 - y0) : 0;
        if (cat == "WALL") {
            SectionDims sd;
            for (double f : {0.5, 0.3, 0.7, 0.15}) {
                sd = sectionDims(m, static_cast<int>(i), e.box.min.z + h * f);
                if (sd.ok) break;
            }
            v["thickness"] = sd.ok ? sd.minDim : planMin;
            v["length"] = planMax;
        } else if (cat == "COLUMN" || cat == "PILE") {
            const SectionDims sd = sectionDims(m, static_cast<int>(i), e.box.min.z + h * 0.5);
            v["secMin"] = sd.ok ? sd.minDim : planMin;
            v["secMax"] = sd.ok ? sd.maxDim : planMax;
            if (sd.ok) v["secArea"] = sd.area;
            v["thickness"] = sd.ok ? sd.minDim : planMin;
        } else if (cat == "SLAB" || cat == "FOOTING" || cat == "ROOF" || cat == "COVERING") {
            v["thickness"] = e.volume > 1e-9 && horiz > 1e-9 ? 2 * e.volume / horiz : h;
            v["area"] = horiz / 2;
        } else if (cat == "BEAM") {
            v["depth"] = h;
            v["width"] = planMin;
            v["length"] = planMax;
            v["thickness"] = h;
        } else if (cat == "DOOR" || cat == "WINDOW") {
            v["width"] = e.num.count("OverallWidth") ? e.num.at("OverallWidth") : planMax;
            v["height"] = e.num.count("OverallHeight") ? e.num.at("OverallHeight") : h;
            if (cat == "WINDOW") v["sill"] = e.box.min.z - elev;
        } else if (cat == "SPACE") {
            v["area"] = horiz / 2;
        } else if (cat == "STAIR") {
            v["width"] = e.num.count("Width") ? e.num.at("Width") : planMin;
            v["length"] = planMax;
            // pog'ona: atribut -> Pset -> geometriya (gorizontal yuqori yuzalar sathlari)
            std::optional<double> riser, tread, risers;
            if (e.num.count("RiserHeight")) riser = e.num.at("RiserHeight");
            if (e.num.count("TreadLength")) tread = e.num.at("TreadLength");
            if (e.num.count("NumberOfRisers")) risers = e.num.at("NumberOfRisers");
            if (!riser) riser = propNum(e, {".RiserHeight"});
            if (!tread) tread = propNum(e, {".TreadLength", ".TreadLengthAtInnerSide"});
            if (!risers) risers = propNum(e, {".NumberOfRiser", ".NumberOfRisers"});
            if ((!riser || !tread) && e.box.valid()) {
                std::map<long long, std::pair<double, double>> lv;   // z (mm) -> run o'qi bo'yicha [min, max]
                const bool runX = (x1 - x0) >= (y1 - y0);
                for (const auto& part : e.parts)
                    for (const auto& t : part.tris) {
                        const Vec3 nn = (t[1] - t[0]).cross(t[2] - t[0]);
                        const double l = nn.length();
                        if (l <= 1e-12 || std::abs(nn.z) / l < 0.95) continue;
                        const long long z = std::llround((t[0].z + t[1].z + t[2].z) / 3 * 200);   // 5 mm
                        auto& r = lv.try_emplace(z, 1e300, -1e300).first->second;
                        for (const auto& p : t) {
                            const double run = runX ? p.x * ca + p.y * sa : -p.x * sa + p.y * ca;
                            r.first = std::min(r.first, run);
                            r.second = std::max(r.second, run);
                        }
                    }
                if (lv.size() >= 4) {
                    std::vector<double> dz, depth;
                    double prev = 0;
                    bool first = true;
                    for (const auto& [z, r] : lv) {
                        if (!first) dz.push_back((z - prev) / 200.0);
                        prev = static_cast<double>(z);
                        first = false;
                        depth.push_back(r.second - r.first);
                    }
                    auto median = [](std::vector<double> x) {
                        std::nth_element(x.begin(), x.begin() + static_cast<long>(x.size() / 2), x.end());
                        return x[x.size() / 2];
                    };
                    if (!riser) riser = median(dz);
                    if (!tread) tread = median(depth);
                    if (!risers) risers = static_cast<double>(lv.size());
                }
            }
            if (!riser && risers && *risers > 0) riser = h / *risers;
            if (riser) v["riser"] = *riser;
            if (tread) v["tread"] = *tread;
            if (risers) v["risers"] = *risers;
        }
        for (const auto& [k, x] : e.num) v["attr" + k] = x;
    }
    // ---- 2. armatura: beton birlashmasi, joylashgan element, himoya qatlami
    std::vector<int> rebarEls;
    double totalLen = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (!m.elements[i].bars.empty()) {
            rebarEls.push_back(static_cast<int>(i));
            for (const auto& b : m.elements[i].bars)
                for (std::size_t k = 1; k < b.first.size(); ++k) totalLen += b.first[k].distanceTo(b.first[k - 1]);
        }
    int samples = 0;
    if (!rebarEls.empty()) {
        std::vector<Solid> solids;
        for (std::size_t i = 0; i < n; ++i) {
            const auto& e = m.elements[i];
            if (!e.bars.empty() || e.volume <= 1e-9 || !structuralCats().count(ifcCategory(e.type))) continue;
            Solid s;
            s.el = static_cast<int>(i);
            s.box = e.box;
            s.angle = e.angle;
            for (const auto& p : e.parts) s.tris.insert(s.tris.end(), p.tris.begin(), p.tris.end());
            if (!s.tris.empty()) solids.push_back(std::move(s));
        }
        const SolidIndex ix(std::move(solids));
        const double step = std::max(0.15, totalLen / 250000.0);
        struct Group {
            int el, host;
            bool stirrup, vertical;
            double cover = 1e300, d = 0, dMin = 1e300, len = 0;
            int bars = 0, samples = 0, embedded = 0;
            Vec3 at;
        };
        std::vector<Group> groups;
        for (int ei : rebarEls) {
            const auto& e = m.elements[static_cast<std::size_t>(ei)];
            std::map<std::tuple<int, bool, bool>, std::size_t> gi;
            for (const auto& b : e.bars) {
                const auto& pts = b.first;
                const bool stir = isStirrupBar(e, pts);
                const BarResult r = analyseBar(ix, pts, b.second, step, stir && pts.size() >= 4);
                samples += r.samples;
                int host = -1, best = 0;
                for (const auto& [si, c] : r.votes)
                    if (c > best || (c == best && host >= 0 && ix.solids[static_cast<std::size_t>(si)].tris.size() < ix.solids[static_cast<std::size_t>(host)].tris.size())) {
                        best = c;
                        host = si;
                    }
                const Vec3 chord = pts.back() - pts.front();
                const bool vertical = chord.length() > 1e-9 && std::abs(chord.z) / chord.length() > 0.8;
                const auto key = std::make_tuple(host, stir, vertical);
                auto it = gi.find(key);
                if (it == gi.end()) {
                    it = gi.emplace(key, groups.size()).first;
                    Group ng;
                    ng.el = ei;
                    ng.host = host;
                    ng.stirrup = stir;
                    ng.vertical = vertical;
                    groups.push_back(ng);
                }
                Group& g = groups[it->second];
                ++g.bars;
                g.samples += r.samples;
                g.embedded += r.embedded;
                g.d = std::max(g.d, 2 * b.second);
                g.dMin = std::min(g.dMin, 2 * b.second);
                for (std::size_t k = 1; k < pts.size(); ++k) g.len += pts[k].distanceTo(pts[k - 1]);
                if (r.cover < g.cover) {
                    g.cover = r.cover;
                    g.at = r.at;
                }
            }
        }
        // host bo'yicha eng katta bo'ylama diametr (xomut talabi uchun) va padbetonka
        std::map<int, double> hostMainD;
        for (const auto& g : groups)
            if (g.host >= 0 && !g.stirrup) hostMainD[g.host] = std::max(hostMainD[g.host], g.d);
        auto leanUnder = [&](int si) {
            const auto& F = m.elements[static_cast<std::size_t>(ix.solids[static_cast<std::size_t>(si)].el)];
            const double fa = (F.box.max.x - F.box.min.x) * (F.box.max.y - F.box.min.y);
            for (const auto& s : ix.solids) {
                if (s.el == ix.solids[static_cast<std::size_t>(si)].el) continue;
                const auto& L = m.elements[static_cast<std::size_t>(s.el)];
                const double t = L.box.max.z - L.box.min.z;
                if (t > 0.25 || std::abs(L.box.max.z - F.box.min.z) > 0.03) continue;
                const double ox = std::min(L.box.max.x, F.box.max.x) - std::max(L.box.min.x, F.box.min.x);
                const double oy = std::min(L.box.max.y, F.box.max.y) - std::max(L.box.min.y, F.box.min.y);
                if (ox > 0 && oy > 0 && ox * oy >= 0.5 * fa) return true;
            }
            const std::string nm = utf8::foldKey(F.name + " " + F.material);
            for (const char* k : {"PADBETON", "ПОДБЕТ", "ПОДГОТОВК", "BLINDING", "LEAN"})
                if (nm.find(utf8::foldKey(k)) != std::string::npos) return true;
            return false;
        };
        for (const auto& g : groups) {
            const auto& e = m.elements[static_cast<std::size_t>(g.el)];
            NormSubject s;
            s.element = g.el;
            s.cat = "REBAR";
            s.vars = base[static_cast<std::size_t>(g.el)];
            s.vars["d"] = g.d;
            s.vars["dMin"] = g.dMin;
            s.vars["stirrup"] = g.stirrup ? 1.0 : 0.0;
            s.vars["vertical"] = g.vertical ? 1.0 : 0.0;
            s.vars["bars"] = static_cast<double>(g.bars);
            s.vars["length"] = g.len;
            s.vars["embedded"] = g.samples > 0 ? static_cast<double>(g.embedded) / g.samples : 0.0;
            s.at = g.at;
            if (g.host >= 0 && g.cover < 1e299) {
                const int hel = ix.solids[static_cast<std::size_t>(g.host)].el;
                const NormVars& hv = base[static_cast<std::size_t>(hel)];
                s.vars["cover"] = std::max(0.0, g.cover);
                s.vars["host"] = ifcCategory(m.elements[static_cast<std::size_t>(hel)].type);
                s.vars["hostName"] = m.elements[static_cast<std::size_t>(hel)].name;
                auto hnum = [&](const char* k) -> std::optional<double> {
                    auto it = hv.find(k);
                    if (it == hv.end()) return std::nullopt;
                    if (const double* d = std::get_if<double>(&it->second)) return *d;
                    return std::nullopt;
                };
                if (auto t = hnum("thickness")) s.vars["hostT"] = *t;
                if (auto t = hnum("height")) s.vars["hostH"] = *t;
                if (auto t = hnum("secMin")) s.vars["hostMin"] = *t;
                else if (auto t2 = hnum("planMin")) s.vars["hostMin"] = *t2;
                if (auto it = hostMainD.find(g.host); it != hostMainD.end()) s.vars["hostMainD"] = it->second;
                if (ifcCategory(m.elements[static_cast<std::size_t>(hel)].type) == std::string("FOOTING")) s.vars["lean"] = leanUnder(g.host) ? 1.0 : 0.0;
            } else if (!e.box.valid()) {
                continue;
            } else {
                s.at = (e.box.min + e.box.max) * 0.5;
            }
            s.where = std::format("{} × Ø{:.0f}", g.bars, g.d * 1000);
            out.push_back(std::move(s));
        }
    }
    if (rebarSamples) *rebarSamples = samples;
    // ---- 3. qolgan elementlar
    for (std::size_t i = 0; i < n; ++i) {
        const auto& e = m.elements[i];
        if (!e.bars.empty()) continue;
        NormSubject s;
        s.element = static_cast<int>(i);
        s.cat = ifcCategory(e.type);
        s.vars = std::move(base[i]);
        s.at = e.box.valid() ? Vec3{(e.box.min.x + e.box.max.x) / 2, (e.box.min.y + e.box.max.y) / 2, e.box.max.z} : e.origin;
        out.push_back(std::move(s));
    }
    return out;
}

NormReport normCheck(const std::vector<NormSubject>& subjects, const std::vector<NormRule>& rules, const IfcModel* m) {
    const auto t0 = std::chrono::steady_clock::now();
    NormReport rep;
    rep.subjects = static_cast<int>(subjects.size());
    rep.applicable.assign(rules.size(), 0);
    rep.failed.assign(rules.size(), 0);
    for (std::size_t ri = 0; ri < rules.size(); ++ri) {
        const NormRule& r = rules[ri];
        if (!r.enabled) continue;
        std::string err;
        for (const std::string* ex : {&r.when, &r.param, &r.min, &r.max})
            if (!ex->empty() && !normSyntaxOk(*ex, &err)) break;
        if (!err.empty()) {
            rep.ruleErrors.push_back(r.id + ": " + err);
            continue;
        }
        if (r.min.empty() && r.max.empty()) continue;
        for (std::size_t si = 0; si < subjects.size(); ++si) {
            const NormSubject& s = subjects[si];
            if (!r.cats.empty() && std::find(r.cats.begin(), r.cats.end(), s.cat) == r.cats.end()) continue;
            const NormProps* props = m && s.element >= 0 && s.element < static_cast<int>(m->elements.size()) ? &m->elements[static_cast<std::size_t>(s.element)].props : nullptr;
            if (r.concreteOnly) {
                auto it = s.vars.find("concrete");
                const bool conc = it != s.vars.end() && truthy(it->second);
                auto mt = s.vars.find("material");
                const bool unknown = mt == s.vars.end() || std::get_if<std::string>(&mt->second) == nullptr || std::get<std::string>(mt->second).empty();
                if (!conc && !unknown) continue;   // material noma'lum — beton deb hisoblanadi (konstruktiv toifalar)
            }
            if (!r.when.empty()) {
                const auto c = normEval(r.when, s.vars, props);
                if (!c || !truthy(*c)) continue;
            }
            const auto pv = normEval(r.param, s.vars, props);
            if (!pv || !std::holds_alternative<double>(*pv)) continue;
            const double value = std::get<double>(*pv);
            ++rep.applicable[ri];
            auto limit = [&](const std::string& ex) -> std::optional<double> {
                if (ex.empty()) return std::nullopt;
                const auto lv = normEval(ex, s.vars, props);
                if (!lv || !std::holds_alternative<double>(*lv)) return std::nullopt;
                return std::get<double>(*lv);
            };
            const auto mn = limit(r.min), mx = limit(r.max);
            NormViolation v;
            v.subject = static_cast<int>(si);
            v.element = s.element;
            v.rule = static_cast<int>(ri);
            v.severity = r.severity;
            v.value = value;
            v.at = s.at;
            v.where = s.where;
            const double tol = 1e-6;
            if (mn && value < *mn - tol) {
                v.limit = *mn;
                v.belowMin = true;
            } else if (mx && value > *mx + tol) {
                v.limit = *mx;
                v.belowMin = false;
            } else continue;
            ++rep.failed[ri];
            (r.severity >= 2 ? rep.errors : rep.warnings)++;
            rep.items.push_back(v);
        }
    }
    std::stable_sort(rep.items.begin(), rep.items.end(), [](const NormViolation& a, const NormViolation& b) {
        if (a.severity != b.severity) return a.severity > b.severity;
        if (a.element != b.element) return a.element < b.element;
        return a.rule < b.rule;
    });
    rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return rep;
}

NormReport normCheck(const IfcModel& m, const std::vector<NormRule>& rules) {
    const auto t0 = std::chrono::steady_clock::now();
    int samples = 0;
    const auto subs = ifcNormSubjects(m, &samples);
    NormReport r = normCheck(subs, rules, &m);
    r.rebarSamples = samples;
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

IfcModel buildingAsIfc(const BuildingModel& bm, const BuildingParams& bp, const RebarModel* rebar, std::vector<std::vector<int>>* prismsOf,
                       std::vector<int>* barOf) {
    IfcModel m;
    m.schema = "AsiaCAD";
    m.application = "AsiaCAD Monolit bino";
    int maxLevel = 0;
    for (const auto& pr : bm.prisms) maxLevel = std::max(maxLevel, pr.level);
    for (int l = 0; l <= maxLevel; ++l) {
        IfcStorey s;
        s.id = l;
        s.name = std::to_string(l);
        s.elevation = l == 0 ? bp.foundTop + bp.zeroAbs : (l - 1) * bp.floorHeight + bp.zeroAbs;
        m.storeys.push_back(s);
    }
    if (prismsOf) prismsOf->clear();
    if (barOf) barOf->clear();
    auto add = [&](IfcElement&& e, std::vector<int> prisms, int bar) {
        for (const auto& p : e.parts)
            for (const auto& t : p.tris)
                for (const auto& q : t) e.box.expand(q);
        for (const auto& b : e.bars)
            for (const auto& q : b.first) e.box.expand(q);
        if (e.box.valid()) {
            m.box.expand(e.box.min);
            m.box.expand(e.box.max);
        }
        if (e.storey >= 0 && e.storey < static_cast<int>(m.storeys.size())) ++m.storeys[static_cast<std::size_t>(e.storey)].elements;
        m.elements.push_back(std::move(e));
        if (prismsOf) prismsOf->push_back(std::move(prisms));
        if (barOf) barOf->push_back(bar);
    };
    // zinapoya: har qavat — bitta marsh (pog'ona o'lchamlari StairInfo dan), geometriya — shu qavat zina prizmalari
    std::map<int, std::vector<int>> stairPrisms;
    for (std::size_t i = 0; i < bm.prisms.size(); ++i) {
        const auto& pr = bm.prisms[i];
        if (pr.kind == ElemKind::Stair) {
            stairPrisms[pr.level].push_back(static_cast<int>(i));
            continue;
        }
        const char* type = nullptr;
        std::string pred;
        switch (pr.kind) {
            case ElemKind::Column: type = "IFCCOLUMN"; pred = "COLUMN"; break;
            case ElemKind::Wall: case ElemKind::Shaft: type = "IFCWALL"; pred = "STANDARD"; break;
            case ElemKind::Infill: type = "IFCWALL"; pred = "PARTITIONING"; break;
            case ElemKind::Slab: type = "IFCSLAB"; pred = "FLOOR"; break;
            case ElemKind::Roof: type = "IFCSLAB"; pred = "ROOF"; break;
            case ElemKind::FoundationSlab: type = "IFCFOOTING"; pred = "FOOTING_BEAM"; break;
            case ElemKind::StripFooting: type = "IFCFOOTING"; pred = "STRIP_FOOTING"; break;
            case ElemKind::Lean: type = "IFCSLAB"; pred = "BASESLAB"; break;
            default: break;
        }
        if (!type || pr.outline.size() < 3) continue;
        IfcElement e;
        e.id = static_cast<int>(i);
        e.guid = std::format("bim-{}", i);
        e.type = type;
        e.predefined = pred;
        e.objectType = kindKey(pr.kind);
        e.storey = pr.level;
        e.material = pr.kind == ElemKind::Infill ? bp.classInfill : (pr.kind == ElemKind::Lean ? "Beton " + bp.classLean : "Beton " + pr.concrete);
        IfcPart part;
        part.rgb = 0xbdbdbd;
        part.tris = ifcExtrude(pr.outline, pr.holes, pr.z0, pr.z1);
        e.volume = pr.volume();
        // burilish: konturning eng uzun tomoni bo'yicha (devor o'qi)
        double best = 0;
        for (std::size_t k = 0; k < pr.outline.size(); ++k) {
            const Vec3 d = pr.outline[(k + 1) % pr.outline.size()] - pr.outline[k];
            if (d.length() > best) {
                best = d.length();
                e.angle = std::atan2(d.y, d.x);
            }
        }
        Vec3 c{0, 0, 0};
        for (const auto& q : pr.outline) c += q;
        c = c / static_cast<double>(pr.outline.size());
        e.origin = {c.x, c.y, pr.z0};
        e.parts.push_back(std::move(part));
        add(std::move(e), {static_cast<int>(i)}, -1);
    }
    for (const auto& st : bm.stairs) {
        IfcElement e;
        e.id = -1;
        e.guid = std::format("bim-stair-{}", st.level);
        e.type = "IFCSTAIRFLIGHT";
        e.objectType = "STAIR";
        e.storey = st.level;
        e.material = "Beton " + bp.classFrame;
        if (st.h > 0) e.num["RiserHeight"] = st.h;
        if (st.b > 0) e.num["TreadLength"] = st.b;
        if (st.risers > 0) e.num["NumberOfRisers"] = st.risers;
        IfcPart part;
        part.rgb = 0xd9c9a3;
        std::vector<int> prs;
        for (int pi : stairPrisms[st.level]) {
            const auto& pr = bm.prisms[static_cast<std::size_t>(pi)];
            auto t = ifcExtrude(pr.outline, pr.holes, pr.z0, pr.z1);
            part.tris.insert(part.tris.end(), t.begin(), t.end());
            e.volume += pr.volume();
            prs.push_back(pi);
        }
        e.parts.push_back(std::move(part));
        // marsh eni — hisobdagi qiymat (zinapoya katagi geometriyasidan emas)
        if (st.width > 0) e.num["Width"] = st.width;
        add(std::move(e), prs, -1);
    }
    if (rebar) {
        for (std::size_t i = 0; i < rebar->bars.size(); ++i) {
            const auto& b = rebar->bars[i];
            if (b.role == BarRole::Tie || b.role == BarRole::Chair || b.pts.size() < 2) continue;   // shpilka va tayanchlar — alohida talab
            IfcElement e;
            e.id = -1;
            e.guid = std::format("bim-bar-{}", i);
            e.type = "IFCREINFORCINGBAR";
            e.predefined = b.role == BarRole::Stirrup ? "LIGATURE" : "MAIN";
            e.objectType = kindKey(b.kind);
            e.name = std::format("Ø{:.0f} {}", b.d * 1000, b.cls);
            e.storey = std::clamp(b.level, 0, maxLevel);
            e.num["NominalDiameter"] = b.d;
            e.bars.push_back({b.pts, b.d / 2});
            e.barLength = b.length;
            e.barMass = kPi * b.d * b.d / 4 * b.length * 7850.0;
            add(std::move(e), {}, static_cast<int>(i));
        }
    }
    return m;
}

} // namespace asiacad::bim
