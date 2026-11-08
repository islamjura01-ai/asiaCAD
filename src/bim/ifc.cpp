// AsiaCAD — IFC o'quvchi (STEP fizik fayli). Tashqi kutubxonasiz: indeks -> argumentlar talab bo'yicha.
#include <asiacad/bim/ifc.h>
#include <asiacad/io/xlsx.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace asiacad::bim {

namespace {

constexpr double kPi = 3.14159265358979323846;

// ======================================================================= STEP qiymatlari
struct V {
    enum K : std::uint8_t { Null, Star, Num, Str, Enum, Ref, List, Typed } k = Null;
    bool isInt = false;
    double num = 0;
    int ref = 0;
    std::string s;          // Str (dekodlangan), Enum (nuqtasiz), Typed (tur nomi)
    std::vector<V> list;    // List, Typed (ichki qiymatlar)
};

void skipWs(const char*& p, const char* e) {
    while (p < e) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++p; continue; }
        if (c == '/' && p + 1 < e && p[1] == '*') {
            const char* q = p + 2;
            while (q + 1 < e && !(q[0] == '*' && q[1] == '/')) ++q;
            p = q + 1 < e ? q + 2 : e;
            continue;
        }
        break;
    }
}

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

void parseValue(const char*& p, const char* e, V& out, int depth = 0);

void parseListInto(const char*& p, const char* e, std::vector<V>& list, int depth) {
    // p — '(' da
    ++p;
    skipWs(p, e);
    if (p < e && *p == ')') { ++p; return; }
    while (p < e) {
        list.emplace_back();
        parseValue(p, e, list.back(), depth + 1);
        skipWs(p, e);
        if (p < e && *p == ',') { ++p; continue; }
        if (p < e && *p == ')') { ++p; break; }
        // buzilgan: keyingi ',' yoki ')' gacha
        while (p < e && *p != ',' && *p != ')') ++p;
        if (p < e && *p == ',') { ++p; continue; }
        if (p < e) ++p;
        break;
    }
}

void parseValue(const char*& p, const char* e, V& out, int depth) {
    skipWs(p, e);
    if (p >= e || depth > 64) { out.k = V::Null; return; }
    const char c = *p;
    if (c == '$') { out.k = V::Null; ++p; return; }
    if (c == '*') { out.k = V::Star; ++p; return; }
    if (c == '#') {
        ++p;
        long v = 0;
        while (p < e && std::isdigit(static_cast<unsigned char>(*p))) v = v * 10 + (*p++ - '0');
        out.k = V::Ref;
        out.ref = static_cast<int>(v);
        return;
    }
    if (c == '\'') {
        ++p;
        std::string raw;
        while (p < e) {
            if (*p == '\'') {
                if (p + 1 < e && p[1] == '\'') { raw += '\''; p += 2; continue; }
                ++p;
                break;
            }
            raw += *p++;
        }
        out.k = V::Str;
        out.s = ifcDecodeString(raw);
        return;
    }
    if (c == '"') {
        ++p;
        while (p < e && *p != '"') out.s += *p++;
        if (p < e) ++p;
        out.k = V::Str;
        return;
    }
    if (c == '.') {
        ++p;
        while (p < e && *p != '.') out.s += *p++;
        if (p < e) ++p;
        out.k = V::Enum;
        return;
    }
    if (c == '(') {
        out.k = V::List;
        parseListInto(p, e, out.list, depth);
        return;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+') {
        const char* q = p;
        bool real = false;
        if (*q == '-' || *q == '+') ++q;
        while (q < e && (std::isdigit(static_cast<unsigned char>(*q)) || *q == '.' || *q == 'E' || *q == 'e' ||
                         ((*q == '-' || *q == '+') && (q[-1] == 'E' || q[-1] == 'e')))) {
            if (*q == '.' || *q == 'E' || *q == 'e') real = true;
            ++q;
        }
        std::string tmp(p, q);
        out.k = V::Num;
        out.num = std::strtod(tmp.c_str(), nullptr);
        out.isInt = !real;
        p = q;
        return;
    }
    if (std::isalpha(static_cast<unsigned char>(c))) {
        const char* q = p;
        while (q < e && identChar(*q)) ++q;
        out.s.assign(p, q);
        for (auto& ch : out.s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        p = q;
        skipWs(p, e);
        out.k = V::Typed;
        if (p < e && *p == '(') parseListInto(p, e, out.list, depth);
        return;
    }
    // noma'lum belgi
    ++p;
    out.k = V::Null;
}

double num(const V& v, double def = 0) {
    if (v.k == V::Num) return v.num;
    if (v.k == V::Typed && !v.list.empty()) return num(v.list[0], def);
    return def;
}
int ref(const V& v) { return v.k == V::Ref ? v.ref : 0; }
std::string str(const V& v) {
    if (v.k == V::Str || v.k == V::Enum) return v.s;
    if (v.k == V::Typed && !v.list.empty()) return str(v.list[0]);
    return {};
}
const std::vector<V>& listOf(const V& v) {
    static const std::vector<V> empty;
    if (v.k == V::List) return v.list;
    if (v.k == V::Typed && v.list.size() == 1 && v.list[0].k == V::List) return v.list[0].list;
    if (v.k == V::Typed) return v.list;
    return empty;
}
bool boolOf(const V& v, bool def) {
    if (v.k == V::Enum) return v.s == "T" || v.s == "TRUE";
    if (v.k == V::Typed && !v.list.empty()) return boolOf(v.list[0], def);
    return def;
}

std::string valueText(const V& v) {
    switch (v.k) {
    case V::Null: return {};
    case V::Star: return "*";
    case V::Num: {
        if (v.isInt) return std::to_string(static_cast<long long>(v.num));
        std::ostringstream os;
        os.precision(10);
        os << v.num;
        return os.str();
    }
    case V::Str: return v.s;
    case V::Enum:
        if (v.s == "T") return "TRUE";
        if (v.s == "F") return "FALSE";
        if (v.s == "U") return "UNKNOWN";
        return v.s;
    case V::Ref: return "#" + std::to_string(v.ref);
    case V::Typed:
        if (v.list.size() == 1) return valueText(v.list[0]);
        [[fallthrough]];
    case V::List: {
        std::string r;
        for (const auto& x : v.list) {
            if (!r.empty()) r += ", ";
            r += valueText(x);
        }
        return r;
    }
    }
    return {};
}

// ======================================================================= STEP fayl indeksi
class Step {
public:
    struct Rec {
        std::uint32_t type = 0;
        std::uint32_t a = 0, b = 0;   // argumentlar '(' ... ')' (b — ')' o'rni)
    };
    std::string text;
    std::vector<std::string> types{""};
    std::unordered_map<std::string, std::uint32_t> typeIdx;
    std::unordered_map<int, Rec> recs;
    std::vector<int> order;           // fayldagi tartib
    std::string schema, application;

    bool index(std::string& err) {
        const char* base = text.data();
        const char* e = base + text.size();
        // sarlavha
        if (text.find("ISO-10303-21") > 512) { err = "ifc.err.format"; return false; }
        std::size_t dataPos = findWord("DATA;", 0), dataStart = std::string::npos;
        if (dataPos != std::string::npos) dataStart = dataPos + 5;
        else if ((dataPos = findWord("DATA(", 0)) != std::string::npos) {   // IFC4X3: DATA('nom', ...);
            const std::size_t semi = text.find(';', dataPos);
            if (semi != std::string::npos) dataStart = semi + 1;
        }
        if (dataStart == std::string::npos) { err = "ifc.err.format"; return false; }
        header(dataPos);
        const char* p = base + dataStart;
        while (p < e) {
            skipWs(p, e);
            if (p >= e) break;
            if (*p != '#') {
                if (e - p >= 6 && std::strncmp(p, "ENDSEC", 6) == 0) break;
                // DATA ichida nomli bo'lim (IFC4X3: DATA('...')) yoki axlat — ';' gacha
                while (p < e && *p != ';') ++p;
                if (p < e) ++p;
                continue;
            }
            ++p;
            long id = 0;
            while (p < e && std::isdigit(static_cast<unsigned char>(*p))) id = id * 10 + (*p++ - '0');
            skipWs(p, e);
            if (p >= e || *p != '=') { skipStatement(p, e); continue; }
            ++p;
            skipWs(p, e);
            Rec r;
            if (p < e && std::isalpha(static_cast<unsigned char>(*p))) {
                const char* q = p;
                while (q < e && identChar(*q)) ++q;
                std::string t(p, q);
                for (auto& ch : t) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                auto it = typeIdx.find(t);
                if (it == typeIdx.end()) {
                    it = typeIdx.emplace(t, static_cast<std::uint32_t>(types.size())).first;
                    types.push_back(t);
                }
                r.type = it->second;
                p = q;
                skipWs(p, e);
            }
            if (p < e && *p == '(') {
                r.a = static_cast<std::uint32_t>(p - base);
                const char* close = matchParen(p, e);
                r.b = static_cast<std::uint32_t>(close - base);
                p = close < e ? close + 1 : e;
            }
            skipStatement(p, e);
            if (id > 0) {
                recs[static_cast<int>(id)] = r;
                order.push_back(static_cast<int>(id));
            }
        }
        if (recs.empty()) { err = "ifc.err.empty"; return false; }
        return true;
    }

    const std::string& typeOf(int id) const {
        auto it = recs.find(id);
        return it == recs.end() ? types[0] : types[it->second.type];
    }
    bool is(int id, const char* t) const { return typeOf(id) == t; }
    std::uint32_t typeCode(const char* t) const {
        auto it = typeIdx.find(t);
        return it == typeIdx.end() ? 0xffffffffu : it->second;
    }

    std::vector<V> args(int id) const {
        std::vector<V> out;
        auto it = recs.find(id);
        if (it == recs.end() || it->second.type == 0 || it->second.b <= it->second.a) return out;
        const char* p = text.data() + it->second.a;
        const char* e = text.data() + it->second.b + 1;
        parseListInto(p, e, out, 0);
        return out;
    }

private:
    std::size_t findWord(const char* w, std::size_t from) const {
        std::size_t pos = from;
        const std::size_t n = std::strlen(w);
        while ((pos = text.find(w, pos)) != std::string::npos) {
            if (pos == 0 || !identChar(text[pos - 1])) return pos;
            pos += n;
        }
        return std::string::npos;
    }
    static const char* matchParen(const char* p, const char* e) {
        int depth = 0;
        while (p < e) {
            const char c = *p;
            if (c == '\'') {
                ++p;
                while (p < e) {
                    if (*p == '\'') {
                        if (p + 1 < e && p[1] == '\'') { p += 2; continue; }
                        break;
                    }
                    ++p;
                }
            } else if (c == '"') {
                ++p;
                while (p < e && *p != '"') ++p;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                if (--depth == 0) return p;
            } else if (c == ';' && depth > 0) {
                // yopilmagan qavs — bayonot oxiri
                return p - 1;
            }
            ++p;
        }
        return e;
    }
    static void skipStatement(const char*& p, const char* e) {
        while (p < e && *p != ';') {
            if (*p == '\'') {
                ++p;
                while (p < e) {
                    if (*p == '\'') {
                        if (p + 1 < e && p[1] == '\'') { p += 2; continue; }
                        break;
                    }
                    ++p;
                }
            }
            if (p < e) ++p;
        }
        if (p < e) ++p;
    }
    void header(std::size_t dataPos) {
        const std::string h = text.substr(0, dataPos);
        auto parseAt = [&](const char* word) -> std::vector<V> {
            const std::size_t at = h.find(word);
            std::vector<V> out;
            if (at == std::string::npos) return out;
            const char* p = h.data() + at + std::strlen(word);
            const char* e = h.data() + h.size();
            skipWs(p, e);
            if (p < e && *p == '(') parseListInto(p, e, out, 0);
            return out;
        };
        const auto fs = parseAt("FILE_SCHEMA");
        if (!fs.empty()) {
            const auto& l = listOf(fs[0]);
            if (!l.empty()) schema = str(l[0]);
        }
        const auto fn = parseAt("FILE_NAME");
        if (fn.size() >= 6) {
            application = str(fn[5]);
            if (application.empty()) application = str(fn[4]);
        }
    }
};

// ======================================================================= geometriya yordamchilari
struct Xf {
    double m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
    Vec3 p(const Vec3& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3], m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]};
    }
    Vec3 d(const Vec3& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    Xf operator*(const Xf& o) const {
        Xf r;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 4; ++j) {
                double s = 0;
                for (int k = 0; k < 3; ++k) s += m[i][k] * o.m[k][j];
                if (j == 3) s += m[i][3];
                r.m[i][j] = s;
            }
        }
        return r;
    }
    static Xf basis(const Vec3& x, const Vec3& y, const Vec3& z, const Vec3& o) {
        Xf r;
        const Vec3 c[3] = {x, y, z};
        for (int j = 0; j < 3; ++j) {
            r.m[0][j] = c[j].x;
            r.m[1][j] = c[j].y;
            r.m[2][j] = c[j].z;
        }
        r.m[0][3] = o.x;
        r.m[1][3] = o.y;
        r.m[2][3] = o.z;
        return r;
    }
    double scale() const { return std::cbrt(std::abs(m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                                                     m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))); }
};

Vec3 norm(const Vec3& v) {
    const double l = v.length();
    return l > 1e-15 ? v / l : Vec3{0, 0, 0};
}

using Tri = std::array<Vec3, 3>;

// Hajm (divergensiya teoremasi). Katta koordinatalarda (UTM) aniqlik yo'qolmasligi uchun — birinchi uchga nisbatan
double signedVolume(const std::vector<Tri>& t, std::size_t from = 0) {
    if (from >= t.size()) return 0;
    const Vec3 r = t[from][0];
    double v = 0;
    for (std::size_t i = from; i < t.size(); ++i) v += (t[i][0] - r).dot((t[i][1] - r).cross(t[i][2] - r));
    return v / 6.0;
}

// ---- quloq kesish (teshiklar ko'prik bilan birlashtiriladi). pts — 2D (z e'tiborsiz); loops — indekslar.
double area2(const std::vector<Vec3>& P, const std::vector<int>& loop) {
    double a = 0;
    for (std::size_t i = 0, n = loop.size(); i < n; ++i) {
        const Vec3& p = P[loop[i]];
        const Vec3& q = P[loop[(i + 1) % n]];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5;
}

double cross2(const Vec3& a, const Vec3& b, const Vec3& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

bool inTri(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const double d1 = cross2(a, b, p), d2 = cross2(b, c, p), d3 = cross2(c, a, p);
    return d1 > 0 && d2 > 0 && d3 > 0;   // qat'iy ichida (chegaradagilar to'sqinlik qilmaydi)
}

void cleanLoop(const std::vector<Vec3>& P, std::vector<int>& loop) {
    std::vector<int> r;
    for (int i : loop) {
        if (!r.empty() && P[r.back()].equals(P[i], 1e-9)) continue;
        r.push_back(i);
    }
    while (r.size() > 1 && P[r.front()].equals(P[r.back()], 1e-9)) r.pop_back();
    loop.swap(r);
}

std::vector<std::array<int, 3>> triangulate(const std::vector<Vec3>& P, std::vector<int> outer, std::vector<std::vector<int>> holes) {
    std::vector<std::array<int, 3>> out;
    cleanLoop(P, outer);
    if (outer.size() < 3) return out;
    if (area2(P, outer) < 0) std::reverse(outer.begin(), outer.end());
    // teshiklarni ko'prik bilan ulash (eng o'ngdagi teshik birinchi)
    for (auto& h : holes) {
        cleanLoop(P, h);
        if (area2(P, h) > 0) std::reverse(h.begin(), h.end());
    }
    holes.erase(std::remove_if(holes.begin(), holes.end(), [](const std::vector<int>& h) { return h.size() < 3; }), holes.end());
    auto maxX = [&](const std::vector<int>& h) {
        std::size_t k = 0;
        for (std::size_t i = 1; i < h.size(); ++i)
            if (P[h[i]].x > P[h[k]].x) k = i;
        return k;
    };
    std::sort(holes.begin(), holes.end(), [&](const auto& a, const auto& b) { return P[a[maxX(a)]].x > P[b[maxX(b)]].x; });
    std::vector<int> poly = outer;
    for (const auto& h : holes) {
        const std::size_t mi = maxX(h);
        const Vec3 M = P[h[mi]];
        double bestX = 1e300;
        int bestEdge = -1;
        for (std::size_t i = 0, n = poly.size(); i < n; ++i) {
            const Vec3& a = P[poly[i]];
            const Vec3& b = P[poly[(i + 1) % n]];
            if ((a.y > M.y) == (b.y > M.y)) continue;
            const double x = a.x + (M.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (x >= M.x - 1e-12 && x < bestX) { bestX = x; bestEdge = static_cast<int>(i); }
        }
        if (bestEdge < 0) continue;
        const std::size_t n = poly.size();
        std::size_t cand = P[poly[bestEdge]].x > P[poly[(bestEdge + 1) % n]].x ? static_cast<std::size_t>(bestEdge) : (bestEdge + 1) % n;
        const Vec3 I{bestX, M.y, 0};
        // uchburchak (M, I, cand) ichidagi uchlar — eng kichik burchakli
        double bestAng = 1e300;
        for (std::size_t i = 0; i < n; ++i) {
            if (i == cand) continue;
            const Vec3& q = P[poly[i]];
            const Vec3& c = P[poly[cand]];
            if (q.x < M.x) continue;
            const double s1 = cross2(M, I, q), s2 = cross2(I, c, q), s3 = cross2(c, M, q);
            const bool inside = (s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0);
            if (!inside) continue;
            const double ang = std::abs(q.y - M.y) / std::max(1e-12, q.x - M.x);
            if (ang < bestAng) { bestAng = ang; cand = i; }
        }
        std::vector<int> np;
        np.reserve(n + h.size() + 2);
        for (std::size_t i = 0; i <= cand; ++i) np.push_back(poly[i]);
        for (std::size_t k = 0; k <= h.size(); ++k) np.push_back(h[(mi + k) % h.size()]);
        np.push_back(poly[cand]);
        for (std::size_t i = cand + 1; i < n; ++i) np.push_back(poly[i]);
        poly.swap(np);
    }
    // quloq kesish
    std::vector<int> idx = poly;
    if (idx.size() > 4000) {   // juda katta — yelpig'ich
        for (std::size_t i = 1; i + 1 < idx.size(); ++i) out.push_back({idx[0], idx[i], idx[i + 1]});
        return out;
    }
    std::size_t guard = 0;
    std::size_t i = 0;
    while (idx.size() > 3 && guard < idx.size() * 3 + 16) {
        const std::size_t n = idx.size();
        const int a = idx[(i + n - 1) % n], b = idx[i % n], c = idx[(i + 1) % n];
        const Vec3 &A = P[a], &B = P[b], &C = P[c];
        bool ear = cross2(A, B, C) > 1e-14;
        if (ear) {
            for (std::size_t k = 0; k < n && ear; ++k) {
                const int q = idx[k];
                if (q == a || q == b || q == c) continue;
                const Vec3& Q = P[q];
                if (Q.equals(A, 1e-12) || Q.equals(B, 1e-12) || Q.equals(C, 1e-12)) continue;
                if (inTri(Q, A, B, C)) ear = false;
            }
        }
        if (ear) {
            out.push_back({a, b, c});
            idx.erase(idx.begin() + static_cast<long>(i % n));
            guard = 0;
            if (i >= idx.size()) i = 0;
        } else {
            ++i;
            ++guard;
            if (i >= idx.size()) i = 0;
        }
    }
    if (idx.size() == 3) {
        if (cross2(P[idx[0]], P[idx[1]], P[idx[2]]) > 1e-14) out.push_back({idx[0], idx[1], idx[2]});
    } else if (idx.size() > 3) {   // buzilgan — qolganini yelpig'ich
        for (std::size_t k = 1; k + 1 < idx.size(); ++k)
            if (std::abs(cross2(P[idx[0]], P[idx[k]], P[idx[k + 1]])) > 1e-14) out.push_back({idx[0], idx[k], idx[k + 1]});
    }
    return out;
}

// Tekis ko'pburchak (3D) -> uchburchaklar, aylanish yo'nalishi tashqi kontur bo'yicha
void triangulateFace(const std::vector<Vec3>& outer, const std::vector<std::vector<Vec3>>& holes, std::vector<Tri>& out) {
    if (outer.size() < 3) return;
    if (outer.size() == 3 && holes.empty()) { out.push_back({outer[0], outer[1], outer[2]}); return; }
    Vec3 n{0, 0, 0};
    for (std::size_t i = 0, k = outer.size(); i < k; ++i) {
        const Vec3& a = outer[i];
        const Vec3& b = outer[(i + 1) % k];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    n = norm(n);
    if (n.length() < 0.5) return;
    if (outer.size() == 4 && holes.empty()) {
        // qavariq to'rtburchak — tez yo'l
        const Vec3 c1 = (outer[1] - outer[0]).cross(outer[2] - outer[1]);
        const Vec3 c2 = (outer[2] - outer[1]).cross(outer[3] - outer[2]);
        const Vec3 c3 = (outer[3] - outer[2]).cross(outer[0] - outer[3]);
        const Vec3 c4 = (outer[0] - outer[3]).cross(outer[1] - outer[0]);
        if (c1.dot(n) > 0 && c2.dot(n) > 0 && c3.dot(n) > 0 && c4.dot(n) > 0) {
            out.push_back({outer[0], outer[1], outer[2]});
            out.push_back({outer[0], outer[2], outer[3]});
            return;
        }
    }
    const Vec3 ref = std::abs(n.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const Vec3 e1 = norm(ref - n * ref.dot(n));
    const Vec3 e2 = n.cross(e1);
    std::vector<Vec3> P3, P2;
    std::vector<int> o;
    std::vector<std::vector<int>> hs;
    auto add = [&](const std::vector<Vec3>& loop, std::vector<int>& ids) {
        for (const auto& q : loop) {
            ids.push_back(static_cast<int>(P3.size()));
            P3.push_back(q);
            P2.push_back({q.dot(e1), q.dot(e2), 0});
        }
    };
    add(outer, o);
    for (const auto& h : holes) {
        hs.emplace_back();
        add(h, hs.back());
    }
    for (const auto& t : triangulate(P2, o, hs)) out.push_back({P3[t[0]], P3[t[1]], P3[t[2]]});
}

// Tekislik bo'yicha kesish: (p - o)·n <= 0 tomoni qoladi; kesim yuzasi yopiladi
void clipByPlane(std::vector<Tri>& tris, const Vec3& o, const Vec3& n) {
    std::vector<Tri> out;
    out.reserve(tris.size());
    std::vector<std::pair<Vec3, Vec3>> cut;
    bool any = false;
    for (const auto& t : tris) {
        double d[3];
        int neg = 0;
        for (int i = 0; i < 3; ++i) {
            d[i] = (t[i] - o).dot(n);
            if (d[i] <= 1e-9) ++neg;
        }
        if (neg == 3) { out.push_back(t); continue; }
        any = true;
        if (neg == 0) continue;
        // Sutherland–Hodgman bitta uchburchak uchun
        std::vector<Vec3> poly;
        std::vector<Vec3> onPlane;
        for (int i = 0; i < 3; ++i) {
            const Vec3& a = t[i];
            const Vec3& b = t[(i + 1) % 3];
            const double da = d[i], db = d[(i + 1) % 3];
            const bool ina = da <= 1e-9, inb = db <= 1e-9;
            if (ina) poly.push_back(a);
            if (ina != inb) {
                const double k = da / (da - db);
                const Vec3 x = a + (b - a) * k;
                poly.push_back(x);
                onPlane.push_back(x);
            }
        }
        for (std::size_t i = 1; i + 1 < poly.size(); ++i) out.push_back({poly[0], poly[i], poly[i + 1]});
        if (onPlane.size() == 2) cut.push_back({onPlane[0], onPlane[1]});
    }
    if (!any) return;
    tris.swap(out);
    // kesim konturlarini zanjirlash va yopish
    const double tol = 1e-6;
    std::vector<bool> used(cut.size(), false);
    for (std::size_t s = 0; s < cut.size(); ++s) {
        if (used[s]) continue;
        used[s] = true;
        std::vector<Vec3> loop{cut[s].first, cut[s].second};
        bool grown = true;
        while (grown) {
            grown = false;
            for (std::size_t k = 0; k < cut.size(); ++k) {
                if (used[k]) continue;
                if (cut[k].first.equals(loop.back(), tol)) { loop.push_back(cut[k].second); used[k] = true; grown = true; }
                else if (cut[k].second.equals(loop.back(), tol)) { loop.push_back(cut[k].first); used[k] = true; grown = true; }
            }
            if (loop.size() > 3 && loop.front().equals(loop.back(), tol)) break;
        }
        if (loop.size() < 4 || !loop.front().equals(loop.back(), tol)) continue;
        loop.pop_back();
        std::vector<Tri> cap;
        triangulateFace(loop, {}, cap);
        for (auto& c : cap) {
            const Vec3 cn = (c[1] - c[0]).cross(c[2] - c[0]);
            if (cn.dot(n) < 0) std::swap(c[1], c[2]);   // qopqoq normali tashqariga (+n)
            tris.push_back(c);
        }
    }
}

// Quvur (armatura) uchburchaklari — faqat hajm/ko'rinish zaxirasi uchun emas; ko'rinish GUI da quvur sifatida
double polylineLength(const std::vector<Vec3>& p) {
    double l = 0;
    for (std::size_t i = 1; i < p.size(); ++i) l += p[i].distanceTo(p[i - 1]);
    return l;
}

// Suv o'tkazmaslik: har qirra aynan ikki uchburchakda (yopiq sirt) — hajm hisoblash mumkin
bool watertight(const std::vector<Tri>& t) {
    if (t.size() < 4) return false;
    auto key = [](const Vec3& p) {
        auto q = [](double v) { return static_cast<long long>(std::llround(v * 1e5)); };
        return std::array<long long, 3>{q(p.x), q(p.y), q(p.z)};
    };
    std::map<std::pair<std::array<long long, 3>, std::array<long long, 3>>, int> edges;
    for (const auto& tr : t)
        for (int i = 0; i < 3; ++i) {
            auto a = key(tr[i]), b = key(tr[(i + 1) % 3]);
            if (a == b) continue;
            if (b < a) std::swap(a, b);
            ++edges[{a, b}];
        }
    for (const auto& [k, n] : edges)
        if (n != 2) return false;
    return !edges.empty();
}

// ---- qavariq jismlar: tekisliklar (tashqi normal), ayirish va kesishma
struct Plane {
    Vec3 n;
    double d = 0;   // n·p = d
};

bool convexPlanes(const std::vector<Tri>& t, std::vector<Plane>& out, Vec3& centre) {
    out.clear();
    if (t.size() < 4) return false;
    Vec3 c{0, 0, 0};
    for (const auto& tr : t) c += (tr[0] + tr[1] + tr[2]) / 3.0;
    c = c / static_cast<double>(t.size());
    centre = c;
    for (const auto& tr : t) {
        Vec3 n = (tr[1] - tr[0]).cross(tr[2] - tr[0]);
        const double l = n.length();
        if (l < 1e-12) continue;
        n = n / l;
        double d = n.dot(tr[0]);
        if (n.dot(c) > d) { n = n * -1.0; d = -d; }
        bool dup = false;
        for (const auto& p : out)
            if (p.n.dot(n) > 1 - 1e-6 && std::abs(p.d - d) < 1e-5) { dup = true; break; }
        if (!dup) out.push_back({n, d});
        if (out.size() > 260) return false;   // juda ko'p yoq — qavariq deb ishlanmaydi
    }
    for (const auto& tr : t)
        for (const auto& v : tr)
            for (const auto& p : out)
                if (p.n.dot(v) - p.d > 1e-4) return false;
    return out.size() >= 4;
}

// ko'pburchakni tekislik bo'yicha ajratish: front (n·p > d) va back
void splitPoly(const std::vector<Vec3>& poly, const Plane& pl, std::vector<Vec3>& front, std::vector<Vec3>& back) {
    front.clear();
    back.clear();
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& a = poly[i];
        const Vec3& b = poly[(i + 1) % n];
        const double da = pl.n.dot(a) - pl.d, db = pl.n.dot(b) - pl.d;
        if (da > 1e-9) front.push_back(a);
        else back.push_back(a);
        if ((da > 1e-9) != (db > 1e-9)) {
            const double k = da / (da - db);
            const Vec3 x = a + (b - a) * k;
            front.push_back(x);
            back.push_back(x);
        }
    }
    if (front.size() < 3) front.clear();
    if (back.size() < 3) back.clear();
}

void fan(const std::vector<Vec3>& poly, std::vector<Tri>& out, bool flip = false) {
    for (std::size_t i = 1; i + 1 < poly.size(); ++i) {
        if (flip) out.push_back({poly[0], poly[i + 1], poly[i]});
        else out.push_back({poly[0], poly[i], poly[i + 1]});
    }
}

// mesh minus qavariq B (B tekisliklari, cheksiz bo'lishi mumkin): B ichidagi bo'laklar olib tashlanadi.
// B tekisligida yotgan uchburchak: normali tekislik normali bilan bir tomonga — B ichida (teshik yuzasi), aks holda tashqarida.
void subtractConvex(std::vector<Tri>& mesh, const std::vector<Plane>& B) {
    std::vector<Tri> out;
    out.reserve(mesh.size());
    std::vector<Vec3> f, b;
    for (const auto& tr : mesh) {
        bool outside = false;
        const Vec3 tn = (tr[1] - tr[0]).cross(tr[2] - tr[0]);
        for (const auto& p : B) {
            const double d0 = p.n.dot(tr[0]) - p.d, d1 = p.n.dot(tr[1]) - p.d, d2 = p.n.dot(tr[2]) - p.d;
            if (d0 > 1e-9 && d1 > 1e-9 && d2 > 1e-9) { outside = true; break; }
            if (std::abs(d0) < 1e-7 && std::abs(d1) < 1e-7 && std::abs(d2) < 1e-7 && tn.dot(p.n) < 0) { outside = true; break; }   // tashqaridan tegib turadi
        }
        if (outside) { out.push_back(tr); continue; }
        std::vector<Vec3> rest{tr[0], tr[1], tr[2]};
        for (const auto& p : B) {
            if (rest.empty()) break;
            splitPoly(rest, p, f, b);
            if (!f.empty()) fan(f, out);
            rest = b;
        }
        // rest — B ichida: tashlanadi
    }
    mesh.swap(out);
}

// Qavariq ko'pyoqlik (tekisliklar kesishmasi) yuzalari: (tekislik indeksi, ko'pburchak). c, R — chegaralovchi soha
std::vector<std::pair<std::size_t, std::vector<Vec3>>> polytopeFaces(const std::vector<Plane>& P, const Vec3& c, double R) {
    std::vector<std::pair<std::size_t, std::vector<Vec3>>> out;
    std::vector<Vec3> f, b;
    for (std::size_t i = 0; i < P.size(); ++i) {
        const Vec3& n = P[i].n;
        const Vec3 o = c - n * (n.dot(c) - P[i].d);
        const Vec3 ref = std::abs(n.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        const Vec3 e1 = norm(ref - n * ref.dot(n)), e2 = n.cross(e1);
        std::vector<Vec3> poly{o - e1 * R - e2 * R, o + e1 * R - e2 * R, o + e1 * R + e2 * R, o - e1 * R + e2 * R};
        for (std::size_t j = 0; j < P.size() && !poly.empty(); ++j) {
            if (j == i) continue;
            splitPoly(poly, P[j], f, b);
            poly = b;
        }
        if (poly.size() >= 3) out.emplace_back(i, std::move(poly));
    }
    return out;
}

double convexVolume(const std::vector<Tri>& faces) {
    if (faces.empty()) return 0;
    Vec3 c{0, 0, 0};
    for (const auto& t : faces) c += (t[0] + t[1] + t[2]) / 3.0;
    c = c / static_cast<double>(faces.size());
    double v = 0;
    for (const auto& t : faces) v += std::abs((t[0] - c).dot((t[1] - c).cross(t[2] - c))) / 6.0;
    return v;
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// ======================================================================= IFC o'quvchi
struct Style {
    std::uint32_t rgb = 0;
    double alpha = 1.0;
    bool set = false;
};

struct Region {
    std::vector<Vec3> outer;
    std::vector<std::vector<Vec3>> holes;
};

struct Mesh {
    std::map<std::pair<std::uint32_t, int>, std::vector<Tri>> parts;   // (rgb, alpha*1000) -> uchburchaklar
    std::vector<std::pair<std::vector<Vec3>, double>> bars;
    double volume = 0;
    // hajm va kesimlar uchun: jism qavariq bo'laklar birlashmasi (har biri tekisliklar kesishmasi).
    // convexOk=false — biror bo'lak qavariq emas: ayirish faqat ko'rinishda, hajm taxminiy
    std::vector<std::vector<Plane>> pieces;
    bool convexOk = true;
    std::size_t tris() const {
        std::size_t n = 0;
        for (const auto& [k, v] : parts) n += v.size();
        return n;
    }
};

class Reader {
public:
    Reader(const Step& s, IfcModel& m, bool georef) : s_(s), m_(m), wantGeo_(georef) {}

    void run() {
        m_.schema = s_.schema;
        m_.application = s_.application;
        m_.entityCount = static_cast<int>(s_.recs.size());
        units();
        georeference();
        styles();
        relations();
        storeys();
        products();
        finish();
    }

private:
    const Step& s_;
    IfcModel& m_;
    bool wantGeo_;
    double L_ = 1.0;               // uzunlik birligi -> m
    double A_ = 1.0;               // burchak birligi -> radian
    Xf geo_;
    std::unordered_map<int, Xf> placeCache_;
    std::unordered_set<int> placeBusy_;
    std::unordered_map<int, Style> itemStyle_, matStyle_;
    std::unordered_map<int, int> containedIn_, parentOf_, typeOf_;
    std::unordered_map<int, std::vector<int>> matsOf_;            // element/tip -> material yozuvlari
    std::unordered_map<int, std::vector<int>> psetsOf_;           // element/tip -> xossalar to'plamlari
    std::unordered_map<int, int> storeyIdx_;                      // storey #id -> indeks
    std::unordered_map<int, std::vector<int>> voidsOf_;           // element -> teshiklar (IfcOpeningElement)
    int openingsCut_ = 0;

    // ---- yordamchilar
    std::vector<V> A(int id) const { return s_.args(id); }
    const std::string& T(int id) const { return s_.typeOf(id); }

    Vec3 point(int id) const {
        const auto a = A(id);
        if (a.empty()) return {};
        const auto& c = listOf(a[0]);
        return Vec3{c.size() > 0 ? num(c[0]) : 0, c.size() > 1 ? num(c[1]) : 0, c.size() > 2 ? num(c[2]) : 0} * L_;
    }
    Vec3 direction(int id, const Vec3& def) const {
        if (!id) return def;
        const auto a = A(id);
        if (a.empty()) return def;
        const auto& c = listOf(a[0]);
        const Vec3 v{c.size() > 0 ? num(c[0]) : 0, c.size() > 1 ? num(c[1]) : 0, c.size() > 2 ? num(c[2]) : 0};
        const Vec3 r = norm(v);
        return r.length() < 0.5 ? def : r;
    }
    static Xf frame(Vec3 z, Vec3 x, const Vec3& o) {
        z = norm(z);
        if (z.length() < 0.5) z = {0, 0, 1};
        x = x - z * x.dot(z);
        if (x.length() < 1e-9) x = std::abs(z.x) < 0.9 ? Vec3{1, 0, 0} - z * z.x : Vec3{0, 1, 0} - z * z.y;
        x = norm(x);
        return Xf::basis(x, z.cross(x), z, o);
    }
    Xf axis(int id) const {
        if (!id) return {};
        const std::string& t = T(id);
        const auto a = A(id);
        if (a.empty()) return {};
        if (t == "IFCAXIS2PLACEMENT3D") {
            const Vec3 o = point(ref(a[0]));
            const Vec3 z = a.size() > 1 ? direction(ref(a[1]), {0, 0, 1}) : Vec3{0, 0, 1};
            const Vec3 x = a.size() > 2 ? direction(ref(a[2]), {1, 0, 0}) : Vec3{1, 0, 0};
            return frame(z, x, o);
        }
        if (t == "IFCAXIS2PLACEMENT2D") {
            const Vec3 o = point(ref(a[0]));
            Vec3 x = a.size() > 1 ? direction(ref(a[1]), {1, 0, 0}) : Vec3{1, 0, 0};
            x.z = 0;
            x = norm(x);
            if (x.length() < 0.5) x = {1, 0, 0};
            return Xf::basis(x, {-x.y, x.x, 0}, {0, 0, 1}, o);
        }
        if (t == "IFCAXIS1PLACEMENT") {
            const Vec3 o = point(ref(a[0]));
            const Vec3 z = a.size() > 1 ? direction(ref(a[1]), {0, 0, 1}) : Vec3{0, 0, 1};
            return frame(z, {1, 0, 0}, o);
        }
        return {};
    }
    Xf placement(int id) {
        if (!id) return {};
        if (auto it = placeCache_.find(id); it != placeCache_.end()) return it->second;
        if (placeBusy_.count(id)) return {};   // sikl
        placeBusy_.insert(id);
        Xf r;
        if (T(id) == "IFCLOCALPLACEMENT") {
            const auto a = A(id);
            if (a.size() >= 2) r = placement(ref(a[0])) * axis(ref(a[1]));
        }
        placeBusy_.erase(id);
        placeCache_[id] = r;
        return r;
    }
    Xf transformOp(int id) const {
        if (!id) return {};
        const std::string& t = T(id);
        const auto a = A(id);
        if (a.size() < 3) return {};
        const bool d3 = t.find("3D") != std::string::npos;
        const Vec3 x = direction(ref(a[0]), {1, 0, 0});
        const Vec3 o = point(ref(a[2]));
        const double sc = a.size() > 3 && a[3].k != V::Null ? num(a[3], 1.0) : 1.0;
        Vec3 z{0, 0, 1};
        double s2 = sc, s3 = sc;
        if (d3) {
            if (a.size() > 4) z = direction(ref(a[4]), {0, 0, 1});
            if (t.find("NONUNIFORM") != std::string::npos) {
                if (a.size() > 5 && a[5].k != V::Null) s2 = num(a[5], sc);
                if (a.size() > 6 && a[6].k != V::Null) s3 = num(a[6], sc);
            }
        } else if (t.find("NONUNIFORM") != std::string::npos && a.size() > 4 && a[4].k != V::Null) {
            s2 = num(a[4], sc);
        }
        Xf f = frame(z, x, o);
        for (int i = 0; i < 3; ++i) {
            f.m[i][0] *= sc;
            f.m[i][1] *= s2;
            f.m[i][2] *= s3;
        }
        return f;
    }

    // ---- birliklar
    double siScale(int id) const {
        const auto a = A(id);
        if (a.size() < 4) return 1.0;
        const std::string prefix = str(a[2]);
        static const std::map<std::string, double> P{{"EXA", 1e18}, {"PETA", 1e15}, {"TERA", 1e12}, {"GIGA", 1e9},  {"MEGA", 1e6},
                                                     {"KILO", 1e3}, {"HECTO", 1e2}, {"DECA", 1e1},  {"DECI", 1e-1}, {"CENTI", 1e-2},
                                                     {"MILLI", 1e-3}, {"MICRO", 1e-6}, {"NANO", 1e-9}};
        auto it = P.find(prefix);
        return it == P.end() ? 1.0 : it->second;
    }
    double unitScale(int id, const char* unitType, double radDefault) const {
        const std::string& t = T(id);
        const auto a = A(id);
        if (t == "IFCSIUNIT") return siScale(id);
        if (t == "IFCCONVERSIONBASEDUNIT" || t == "IFCCONVERSIONBASEDUNITWITHOFFSET") {
            if (a.size() < 4) return radDefault;
            const int mw = ref(a[3]);
            const auto b = A(mw);
            if (b.size() < 2) return radDefault;
            double f = num(b[0], 1.0);
            if (const int u = ref(b[1]); u && T(u) == "IFCSIUNIT") f *= siScale(u);
            const std::string nm = upper(str(a[2]));
            if (std::string(unitType) == "PLANEANGLEUNIT" && f <= 0) return kPi / 180;
            if (f <= 0) {
                if (nm.find("FOOT") != std::string::npos) return 0.3048;
                if (nm.find("INCH") != std::string::npos) return 0.0254;
                return radDefault;
            }
            return f;
        }
        return radDefault;
    }
    void units() {
        for (int id : s_.order) {
            if (T(id) != "IFCPROJECT") continue;
            const auto a = A(id);
            if (a.size() > 2) m_.project = str(a[2]);
            if (a.size() > 5 && m_.project.empty()) m_.project = str(a[5]);
            const int ua = a.size() > 8 ? ref(a[8]) : 0;
            if (!ua) break;
            const auto ub = A(ua);
            if (ub.empty()) break;
            for (const auto& u : listOf(ub[0])) {
                const int uid = ref(u);
                if (!uid) continue;
                const auto c = A(uid);
                if (c.size() < 2) continue;
                const std::string ut = str(c[1]);
                if (ut == "LENGTHUNIT") L_ = unitScale(uid, "LENGTHUNIT", 1.0);
                else if (ut == "PLANEANGLEUNIT") A_ = unitScale(uid, "PLANEANGLEUNIT", 1.0);
            }
            break;
        }
        m_.unitScale = L_;
    }
    void georeference() {
        if (!wantGeo_) return;
        for (int id : s_.order) {
            if (T(id) != "IFCMAPCONVERSION") continue;
            const auto a = A(id);
            if (a.size() < 5) break;
            double mapU = L_;
            if (const int crs = ref(a[1]); crs && T(crs) == "IFCPROJECTEDCRS") {
                const auto c = A(crs);
                if (c.size() > 6 && ref(c[6])) mapU = unitScale(ref(c[6]), "LENGTHUNIT", L_);
            }
            const double E = num(a[2]) * mapU, N = num(a[3]) * mapU, H = num(a[4]) * mapU;
            const double ax = a.size() > 5 && a[5].k != V::Null ? num(a[5], 1) : 1, ay = a.size() > 6 && a[6].k != V::Null ? num(a[6], 0) : 0;
            const double ang = std::atan2(ay, ax);
            const double c = std::cos(ang), s = std::sin(ang);
            geo_ = Xf::basis({c, s, 0}, {-s, c, 0}, {0, 0, 1}, {E, N, H});
            m_.georef = std::abs(E) + std::abs(N) + std::abs(H) > 1e-9 || std::abs(ang) > 1e-12;
            break;
        }
    }

    // ---- uslublar (ranglar)
    Style styleFrom(int id, int depth = 0) const {
        Style st;
        if (!id || depth > 8) return st;
        const std::string& t = T(id);
        const auto a = A(id);
        if (t == "IFCSTYLEDITEM") {
            if (a.size() > 1)
                for (const auto& v : listOf(a[1])) {
                    st = styleFrom(ref(v), depth + 1);
                    if (st.set) return st;
                }
            return st;
        }
        if (t == "IFCPRESENTATIONSTYLEASSIGNMENT") {
            if (!a.empty())
                for (const auto& v : listOf(a[0])) {
                    st = styleFrom(ref(v), depth + 1);
                    if (st.set) return st;
                }
            return st;
        }
        if (t == "IFCSURFACESTYLE") {
            if (a.size() > 2)
                for (const auto& v : listOf(a[2])) {
                    st = styleFrom(ref(v), depth + 1);
                    if (st.set) return st;
                }
            return st;
        }
        if (t == "IFCSURFACESTYLESHADING" || t == "IFCSURFACESTYLERENDERING") {
            if (a.empty()) return st;
            const auto c = A(ref(a[0]));
            if (c.size() >= 4) {
                auto ch = [](double v) { return static_cast<std::uint32_t>(std::clamp(std::lround(v * 255.0), 0L, 255L)); };
                st.rgb = (ch(num(c[1])) << 16) | (ch(num(c[2])) << 8) | ch(num(c[3]));
                if (st.rgb == 0) st.rgb = 0x010101;   // 0 — "berilmagan" belgisi
                if (a.size() > 1 && a[1].k != V::Null) st.alpha = std::clamp(1.0 - num(a[1]), 0.05, 1.0);
                st.set = true;
            }
            return st;
        }
        return st;
    }
    void styles() {
        for (int id : s_.order) {
            const std::string& t = T(id);
            if (t == "IFCSTYLEDITEM") {
                const auto a = A(id);
                if (a.empty() || !ref(a[0])) continue;
                const Style st = styleFrom(id);
                if (st.set) itemStyle_.emplace(ref(a[0]), st);
            } else if (t == "IFCMATERIALDEFINITIONREPRESENTATION") {
                const auto a = A(id);
                if (a.size() < 4) continue;
                const int mat = ref(a[3]);
                for (const auto& r : listOf(a[2])) {
                    const auto ra = A(ref(r));
                    if (ra.size() < 4) continue;
                    for (const auto& it : listOf(ra[3])) {
                        const Style st = styleFrom(ref(it));
                        if (st.set) { matStyle_.emplace(mat, st); break; }
                    }
                    if (matStyle_.count(mat)) break;
                }
            }
        }
    }

    // ---- munosabatlar
    void relations() {
        for (int id : s_.order) {
            const std::string& t = T(id);
            if (t.compare(0, 6, "IFCREL") != 0) continue;
            const auto a = A(id);
            if (t == "IFCRELCONTAINEDINSPATIALSTRUCTURE" && a.size() >= 6) {
                const int st = ref(a[5]);
                for (const auto& v : listOf(a[4]))
                    if (ref(v)) containedIn_[ref(v)] = st;
            } else if ((t == "IFCRELAGGREGATES" || t == "IFCRELNESTS") && a.size() >= 6) {
                const int par = ref(a[4]);
                for (const auto& v : listOf(a[5]))
                    if (ref(v) && !parentOf_.count(ref(v))) parentOf_[ref(v)] = par;
            } else if (t == "IFCRELDEFINESBYTYPE" && a.size() >= 6) {
                const int ty = ref(a[5]);
                for (const auto& v : listOf(a[4]))
                    if (ref(v)) typeOf_[ref(v)] = ty;
            } else if (t == "IFCRELASSOCIATESMATERIAL" && a.size() >= 6) {
                const int mat = ref(a[5]);
                for (const auto& v : listOf(a[4]))
                    if (ref(v)) matsOf_[ref(v)].push_back(mat);
            } else if (t == "IFCRELVOIDSELEMENT" && a.size() >= 6) {
                if (ref(a[4]) && ref(a[5])) voidsOf_[ref(a[4])].push_back(ref(a[5]));
            } else if (t == "IFCRELDEFINESBYPROPERTIES" && a.size() >= 6) {
                const int ps = ref(a[5]);
                for (const auto& v : listOf(a[4]))
                    if (ref(v)) psetsOf_[ref(v)].push_back(ps);
            }
        }
    }

    void storeys() {
        std::vector<IfcStorey> list;
        for (int id : s_.order) {
            const std::string& t = T(id);
            if (t == "IFCSITE" && m_.site.empty()) {
                const auto a = A(id);
                if (a.size() > 2) m_.site = str(a[2]);
            } else if (t == "IFCBUILDING" && m_.building.empty()) {
                const auto a = A(id);
                if (a.size() > 2) m_.building = str(a[2]);
            } else if (t == "IFCBUILDINGSTOREY") {
                const auto a = A(id);
                IfcStorey s;
                s.id = id;
                if (a.size() > 2) s.name = str(a[2]);
                if (s.name.empty() && a.size() > 7) s.name = str(a[7]);
                if (a.size() > 9 && a[9].k != V::Null) s.elevation = num(a[9]) * L_;
                else if (a.size() > 5) s.elevation = placement(ref(a[5])).p({0, 0, 0}).z;
                list.push_back(s);
            }
        }
        std::stable_sort(list.begin(), list.end(), [](const IfcStorey& a, const IfcStorey& b) { return a.elevation < b.elevation; });
        for (std::size_t i = 0; i < list.size(); ++i) storeyIdx_[list[i].id] = static_cast<int>(i);
        m_.storeys = std::move(list);
    }
    int storeyOf(int id) const {
        for (int k = 0; k < 24 && id; ++k) {
            if (auto it = storeyIdx_.find(id); it != storeyIdx_.end()) return it->second;
            if (auto it = containedIn_.find(id); it != containedIn_.end()) { id = it->second; continue; }
            if (auto it = parentOf_.find(id); it != parentOf_.end()) { id = it->second; continue; }
            break;
        }
        return -1;
    }

    // ---- materiallar
    void materialNames(int id, std::vector<std::string>& out, std::vector<int>& mats, int depth = 0) const {
        if (!id || depth > 6) return;
        const std::string& t = T(id);
        const auto a = A(id);
        auto addMat = [&](int mid) {
            if (!mid) return;
            const auto b = A(mid);
            if (b.empty()) return;
            const std::string n = str(b[0]);
            mats.push_back(mid);
            if (!n.empty() && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        };
        if (t == "IFCMATERIAL") addMat(id);
        else if (t == "IFCMATERIALLIST") {
            if (!a.empty())
                for (const auto& v : listOf(a[0])) addMat(ref(v));
        } else if (t == "IFCMATERIALLAYERSETUSAGE" || t == "IFCMATERIALPROFILESETUSAGE" || t == "IFCMATERIALPROFILESETUSAGETAPERING") {
            if (!a.empty()) materialNames(ref(a[0]), out, mats, depth + 1);
        } else if (t == "IFCMATERIALLAYERSET") {
            if (!a.empty())
                for (const auto& v : listOf(a[0])) {
                    const auto l = A(ref(v));
                    if (!l.empty()) addMat(ref(l[0]));
                }
        } else if (t == "IFCMATERIALPROFILESET") {
            if (a.size() > 2)
                for (const auto& v : listOf(a[2])) {
                    const auto l = A(ref(v));
                    if (l.size() > 2) addMat(ref(l[2]));
                }
        } else if (t == "IFCMATERIALCONSTITUENTSET") {
            if (a.size() > 2)
                for (const auto& v : listOf(a[2])) {
                    const auto l = A(ref(v));
                    if (l.size() > 2) addMat(ref(l[2]));
                }
        } else if (t == "IFCMATERIALLAYER" || t == "IFCMATERIALLAYERWITHOFFSETS") {
            if (!a.empty()) addMat(ref(a[0]));
        }
    }

    // ---- xossalar
    void properties(int psId, std::vector<std::pair<std::string, std::string>>& out) const {
        const std::string& t = T(psId);
        const auto a = A(psId);
        if (out.size() >= 80) return;
        if (t == "IFCPROPERTYSET" && a.size() >= 5) {
            const std::string set = str(a[2]);
            for (const auto& v : listOf(a[4])) {
                const int pid = ref(v);
                const auto p = A(pid);
                if (p.empty()) continue;
                const std::string& pt = T(pid);
                std::string val;
                if (pt == "IFCPROPERTYSINGLEVALUE" && p.size() > 2) val = valueText(p[2]);
                else if (pt == "IFCPROPERTYENUMERATEDVALUE" && p.size() > 2) val = valueText(p[2]);
                else if (pt == "IFCPROPERTYLISTVALUE" && p.size() > 2) val = valueText(p[2]);
                else if (pt == "IFCPROPERTYBOUNDEDVALUE" && p.size() > 3) val = valueText(p[3]) + " … " + valueText(p[2]);
                else continue;
                if (val.empty()) continue;
                out.emplace_back(set + "." + str(p[0]), val);
                if (out.size() >= 80) return;
            }
        } else if (t == "IFCELEMENTQUANTITY" && a.size() >= 6) {
            const std::string set = str(a[2]);
            for (const auto& v : listOf(a[5])) {
                const int qid = ref(v);
                const auto q = A(qid);
                if (q.size() < 4) continue;
                const std::string& qt = T(qid);
                double val = num(q[3]);
                if (qt == "IFCQUANTITYLENGTH") val *= L_;
                else if (qt == "IFCQUANTITYAREA") val *= L_ * L_;
                else if (qt == "IFCQUANTITYVOLUME") val *= L_ * L_ * L_;
                std::ostringstream os;
                os.precision(6);
                os << val;
                out.emplace_back(set + "." + str(q[0]), os.str());
                if (out.size() >= 80) return;
            }
        }
    }

    // ---- profillar (2D, profil koordinatalarida, m)
    void circle(std::vector<Vec3>& out, double r, double rx = -1, int seg = 0) const {
        const double ry = r;
        if (rx < 0) rx = r;
        const int n = seg > 0 ? seg : std::clamp(static_cast<int>(std::max(rx, ry) * 2 * kPi / 0.05), 16, 48);
        for (int i = 0; i < n; ++i) out.push_back({rx * std::cos(2 * kPi * i / n), ry * std::sin(2 * kPi * i / n), 0});
    }
    static std::vector<Vec3> rect(double x, double y) { return {{-x / 2, -y / 2, 0}, {x / 2, -y / 2, 0}, {x / 2, y / 2, 0}, {-x / 2, y / 2, 0}}; }

    std::vector<Region> profile(int id, int depth = 0) {
        std::vector<Region> out;
        if (!id || depth > 6) return out;
        const std::string& t = T(id);
        const auto a = A(id);
        auto pos = [&](std::size_t i) { return i < a.size() && ref(a[i]) ? axis(ref(a[i])) : Xf{}; };
        auto d = [&](std::size_t i, double def = 0) { return i < a.size() && a[i].k != V::Null ? num(a[i], def) * L_ : def * L_; };
        auto apply = [&](std::vector<Region>& rs, const Xf& f) {
            for (auto& r : rs) {
                for (auto& p : r.outer) p = f.p(p);
                for (auto& h : r.holes)
                    for (auto& p : h) p = f.p(p);
            }
        };
        Region r;
        if (t == "IFCRECTANGLEPROFILEDEF" || t == "IFCROUNDEDRECTANGLEPROFILEDEF") {
            r.outer = rect(d(3), d(4));
        } else if (t == "IFCRECTANGLEHOLLOWPROFILEDEF") {
            const double x = d(3), y = d(4), w = d(5);
            r.outer = rect(x, y);
            if (w > 0 && x > 2 * w && y > 2 * w) r.holes.push_back(rect(x - 2 * w, y - 2 * w));
        } else if (t == "IFCCIRCLEPROFILEDEF") {
            circle(r.outer, d(3));
        } else if (t == "IFCCIRCLEHOLLOWPROFILEDEF") {
            const double R = d(3), w = d(4);
            circle(r.outer, R);
            if (w > 0 && w < R) {
                std::vector<Vec3> h;
                circle(h, R - w, -1, static_cast<int>(r.outer.size()));
                r.holes.push_back(h);
            }
        } else if (t == "IFCELLIPSEPROFILEDEF") {
            circle(r.outer, d(4), d(3));
        } else if (t == "IFCISHAPEPROFILEDEF" || t == "IFCASYMMETRICISHAPEPROFILEDEF") {
            const double w = d(3), h = d(4), tw = d(5), tf = d(6);
            const double wt = t == "IFCASYMMETRICISHAPEPROFILEDEF" && a.size() > 8 && a[8].k != V::Null ? d(8) : w;
            const double tft = t == "IFCASYMMETRICISHAPEPROFILEDEF" && a.size() > 9 && a[9].k != V::Null ? d(9) : tf;
            r.outer = {{-w / 2, -h / 2, 0}, {w / 2, -h / 2, 0}, {w / 2, -h / 2 + tf, 0}, {tw / 2, -h / 2 + tf, 0}, {tw / 2, h / 2 - tft, 0},
                       {wt / 2, h / 2 - tft, 0}, {wt / 2, h / 2, 0}, {-wt / 2, h / 2, 0}, {-wt / 2, h / 2 - tft, 0}, {-tw / 2, h / 2 - tft, 0},
                       {-tw / 2, -h / 2 + tf, 0}, {-w / 2, -h / 2 + tf, 0}};
        } else if (t == "IFCLSHAPEPROFILEDEF") {
            const double h = d(3), w = a.size() > 4 && a[4].k != V::Null ? d(4) : d(3), th = d(5);
            // IFC: chap pastki burchak (-w/2, -h/2), vertikal oyoq chapda
            r.outer = {{-w / 2, -h / 2, 0}, {w / 2, -h / 2, 0}, {w / 2, -h / 2 + th, 0}, {-w / 2 + th, -h / 2 + th, 0}, {-w / 2 + th, h / 2, 0}, {-w / 2, h / 2, 0}};
        } else if (t == "IFCUSHAPEPROFILEDEF") {
            const double h = d(3), w = d(4), tw = d(5), tf = d(6);
            r.outer = {{-w / 2, -h / 2, 0}, {w / 2, -h / 2, 0}, {w / 2, -h / 2 + tf, 0}, {-w / 2 + tw, -h / 2 + tf, 0},
                       {-w / 2 + tw, h / 2 - tf, 0}, {w / 2, h / 2 - tf, 0}, {w / 2, h / 2, 0}, {-w / 2, h / 2, 0}};
        } else if (t == "IFCTSHAPEPROFILEDEF") {
            const double h = d(3), w = d(4), tw = d(5), tf = d(6);
            r.outer = {{-tw / 2, -h / 2, 0}, {tw / 2, -h / 2, 0}, {tw / 2, h / 2 - tf, 0}, {w / 2, h / 2 - tf, 0},
                       {w / 2, h / 2, 0}, {-w / 2, h / 2, 0}, {-w / 2, h / 2 - tf, 0}, {-tw / 2, h / 2 - tf, 0}};
        } else if (t == "IFCCSHAPEPROFILEDEF") {
            const double h = d(3), w = d(4), th = d(5), g = d(6);
            r.outer = {{-w / 2, -h / 2, 0}, {w / 2, -h / 2, 0}, {w / 2, -h / 2 + g, 0}, {w / 2 - th, -h / 2 + g, 0}, {w / 2 - th, -h / 2 + th, 0},
                       {-w / 2 + th, -h / 2 + th, 0}, {-w / 2 + th, h / 2 - th, 0}, {w / 2 - th, h / 2 - th, 0}, {w / 2 - th, h / 2 - g, 0},
                       {w / 2, h / 2 - g, 0}, {w / 2, h / 2, 0}, {-w / 2, h / 2, 0}};
        } else if (t == "IFCZSHAPEPROFILEDEF") {
            const double h = d(3), w = d(4), tw = d(5), tf = d(6);
            r.outer = {{-tw / 2, -h / 2, 0}, {w - tw / 2, -h / 2, 0}, {w - tw / 2, -h / 2 + tf, 0}, {tw / 2, -h / 2 + tf, 0}, {tw / 2, h / 2, 0},
                       {tw / 2 - w, h / 2, 0}, {tw / 2 - w, h / 2 - tf, 0}, {-tw / 2, h / 2 - tf, 0}};
        } else if (t == "IFCARBITRARYCLOSEDPROFILEDEF" || t == "IFCARBITRARYPROFILEDEFWITHVOIDS") {
            if (a.size() > 2) curve(ref(a[2]), r.outer);
            if (t == "IFCARBITRARYPROFILEDEFWITHVOIDS" && a.size() > 3)
                for (const auto& v : listOf(a[3])) {
                    std::vector<Vec3> h;
                    curve(ref(v), h);
                    if (h.size() >= 3) r.holes.push_back(h);
                }
            closeLoop(r.outer);
            for (auto& h : r.holes) closeLoop(h);
            if (r.outer.size() >= 3) out.push_back(r);
            return out;   // o'z Position'i yo'q
        } else if (t == "IFCDERIVEDPROFILEDEF") {
            if (a.size() > 3) {
                out = profile(ref(a[2]), depth + 1);
                apply(out, transformOp(ref(a[3])));
            }
            return out;
        } else if (t == "IFCCOMPOSITEPROFILEDEF") {
            if (a.size() > 2)
                for (const auto& v : listOf(a[2])) {
                    auto sub = profile(ref(v), depth + 1);
                    out.insert(out.end(), sub.begin(), sub.end());
                }
            return out;
        } else {
            return out;
        }
        if (r.outer.size() >= 3) out.push_back(r);
        apply(out, pos(2));
        return out;
    }
    static void closeLoop(std::vector<Vec3>& p) {
        std::vector<Vec3> r;
        for (const auto& q : p)
            if (r.empty() || !r.back().equals(q, 1e-9)) r.push_back(q);
        while (r.size() > 1 && r.front().equals(r.back(), 1e-9)) r.pop_back();
        p.swap(r);
    }

    // ---- egri chiziqlar -> siniq chiziq (m)
    void arc3(const Vec3& A, const Vec3& B, const Vec3& C, std::vector<Vec3>& out) const {
        const Vec3 u = B - A, v = C - A, w = u.cross(v);
        const double w2 = w.dot(w);
        if (w2 < 1e-20) { out.push_back(B); out.push_back(C); return; }
        const Vec3 c = A + (v.cross(w) * u.dot(u) + w.cross(u) * v.dot(v)) / (2 * w2);
        const Vec3 n = norm(w);
        const Vec3 e1 = norm(A - c), e2 = n.cross(e1);
        const double r = (A - c).length();
        double ang = std::atan2((C - c).dot(e2), (C - c).dot(e1));
        if (ang <= 0) ang += 2 * kPi;
        const int seg = std::clamp(static_cast<int>(std::ceil(ang / (kPi / 18))), 2, 72);
        for (int i = 1; i <= seg; ++i) {
            const double t = ang * i / seg;
            out.push_back(c + e1 * (r * std::cos(t)) + e2 * (r * std::sin(t)));
        }
    }
    void conicArc(const Xf& f, double rx, double ry, double t0, double t1, bool sense, std::vector<Vec3>& out) const {
        double sweep = sense ? t1 - t0 : t0 - t1;
        while (sweep <= 1e-12) sweep += 2 * kPi;
        while (sweep > 2 * kPi + 1e-9) sweep -= 2 * kPi;
        const int seg = std::clamp(static_cast<int>(std::ceil(sweep / (kPi / 18))), 2, 72);
        for (int i = 0; i <= seg; ++i) {
            const double t = sense ? t0 + sweep * i / seg : t0 - sweep * i / seg;
            out.push_back(f.p({rx * std::cos(t), ry * std::sin(t), 0}));
        }
    }
    std::vector<Vec3> pointList(int id) const {
        std::vector<Vec3> pts;
        const auto a = A(id);
        if (a.empty()) return pts;
        for (const auto& c : listOf(a[0])) {
            const auto& q = listOf(c);
            pts.push_back(Vec3{q.size() > 0 ? num(q[0]) : 0, q.size() > 1 ? num(q[1]) : 0, q.size() > 2 ? num(q[2]) : 0} * L_);
        }
        return pts;
    }
    void curve(int id, std::vector<Vec3>& out, int depth = 0) const {
        if (!id || depth > 10) return;
        const std::string& t = T(id);
        const auto a = A(id);
        auto push = [&](const Vec3& p) {
            if (out.empty() || !out.back().equals(p, 1e-9)) out.push_back(p);
        };
        if (t == "IFCPOLYLINE") {
            if (!a.empty())
                for (const auto& v : listOf(a[0])) push(point(ref(v)));
        } else if (t == "IFCINDEXEDPOLYCURVE") {
            if (a.empty()) return;
            const auto pts = pointList(ref(a[0]));
            auto P = [&](const V& v) -> const Vec3* {
                const long i = std::lround(num(v)) - 1;
                return i >= 0 && i < static_cast<long>(pts.size()) ? &pts[static_cast<std::size_t>(i)] : nullptr;
            };
            if (a.size() < 2 || a[1].k == V::Null) {
                for (const auto& q : pts) push(q);
                return;
            }
            for (const auto& seg : listOf(a[1])) {
                const auto& ix = listOf(seg);
                if (seg.k == V::Typed && seg.s == "IFCARCINDEX" && ix.size() >= 3) {
                    const Vec3 *p0 = P(ix[0]), *p1 = P(ix[1]), *p2 = P(ix[2]);
                    if (!p0 || !p1 || !p2) continue;
                    push(*p0);
                    std::vector<Vec3> tmp;
                    arc3(*p0, *p1, *p2, tmp);
                    for (const auto& q : tmp) push(q);
                } else {
                    for (const auto& v : ix)
                        if (const Vec3* q = P(v)) push(*q);
                }
            }
        } else if (t == "IFCCOMPOSITECURVE" || t == "IFCCOMPOSITECURVEONSURFACE") {
            if (a.empty()) return;
            for (const auto& v : listOf(a[0])) {
                const auto sa = A(ref(v));
                if (sa.size() < 3) continue;
                std::vector<Vec3> seg;
                curve(ref(sa[2]), seg, depth + 1);
                if (!boolOf(sa[1], true)) std::reverse(seg.begin(), seg.end());
                for (const auto& q : seg) push(q);
            }
        } else if (t == "IFCTRIMMEDCURVE") {
            if (a.size() < 5) return;
            const int basis = ref(a[0]);
            const std::string& bt = T(basis);
            const bool sense = boolOf(a[3], true);
            const bool preferParam = str(a[4]) == "PARAMETER";
            auto trimPoint = [&](const V& v, Vec3& p) {
                for (const auto& x : listOf(v))
                    if (x.k == V::Ref && T(x.ref) == "IFCCARTESIANPOINT") { p = point(x.ref); return true; }
                return false;
            };
            auto trimParam = [&](const V& v, double& t) {
                for (const auto& x : listOf(v)) {
                    if (x.k == V::Typed && x.s == "IFCPARAMETERVALUE") { t = num(x); return true; }
                    if (x.k == V::Num) { t = x.num; return true; }
                }
                return false;
            };
            if (bt == "IFCLINE") {
                const auto la = A(basis);
                if (la.size() < 2) return;
                const Vec3 o = point(ref(la[0]));
                const auto va = A(ref(la[1]));
                const Vec3 dir = va.size() >= 2 ? direction(ref(va[0]), {1, 0, 0}) * (num(va[1], 1) * L_) : Vec3{L_, 0, 0};
                Vec3 p0, p1;
                double t0 = 0, t1 = 0;
                const bool hp0 = trimPoint(a[1], p0), hp1 = trimPoint(a[2], p1);
                const bool ht0 = trimParam(a[1], t0), ht1 = trimParam(a[2], t1);
                if ((preferParam || !hp0) && ht0) p0 = o + dir * t0;
                if ((preferParam || !hp1) && ht1) p1 = o + dir * t1;
                push(p0);
                push(p1);
            } else if (bt == "IFCCIRCLE" || bt == "IFCELLIPSE") {
                const auto ca = A(basis);
                if (ca.size() < 2) return;
                const Xf f = axis(ref(ca[0]));
                const double rx = num(ca[1]) * L_, ry = bt == "IFCELLIPSE" && ca.size() > 2 ? num(ca[2]) * L_ : rx;
                auto angleOf = [&](const Vec3& p) {
                    const Vec3 o = f.p({0, 0, 0});
                    const Vec3 ex = f.d({1, 0, 0}), ey = f.d({0, 1, 0});
                    return std::atan2((p - o).dot(ey) / std::max(ry, 1e-12), (p - o).dot(ex) / std::max(rx, 1e-12));
                };
                double t0 = 0, t1 = 2 * kPi;
                Vec3 p;
                double tp = 0;
                if ((preferParam || !trimPoint(a[1], p)) && trimParam(a[1], tp)) t0 = tp * A_;
                else if (trimPoint(a[1], p)) t0 = angleOf(p);
                if ((preferParam || !trimPoint(a[2], p)) && trimParam(a[2], tp)) t1 = tp * A_;
                else if (trimPoint(a[2], p)) t1 = angleOf(p);
                std::vector<Vec3> tmp;
                conicArc(f, rx, ry, t0, t1, sense, tmp);
                for (const auto& q : tmp) push(q);
            } else {
                curve(basis, out, depth + 1);
            }
        } else if (t == "IFCCIRCLE" || t == "IFCELLIPSE") {
            if (a.size() < 2) return;
            const Xf f = axis(ref(a[0]));
            const double rx = num(a[1]) * L_, ry = t == "IFCELLIPSE" && a.size() > 2 ? num(a[2]) * L_ : rx;
            const int n = std::clamp(static_cast<int>(std::max(rx, ry) * 2 * kPi / 0.05), 16, 72);
            for (int i = 0; i < n; ++i) push(f.p({rx * std::cos(2 * kPi * i / n), ry * std::sin(2 * kPi * i / n), 0}));
        } else if (t.compare(0, 15, "IFCBSPLINECURVE") == 0 || t == "IFCRATIONALBSPLINECURVEWITHKNOTS") {
            if (a.size() > 1)
                for (const auto& v : listOf(a[1])) push(point(ref(v)));   // taxminiy: boshqaruv nuqtalari
        }
    }

    // ---- elementar jismlar
    void addTris(Mesh& m, const Style& st, std::vector<Tri>&& t, bool closed) {
        if (t.empty()) return;
        const bool solid = closed || watertight(t);
        if (solid) m.volume += std::abs(signedVolume(t));
        trackPiece(m, t, solid);
        auto& dst = m.parts[{st.set ? st.rgb : 0u, st.set ? static_cast<int>(std::lround(st.alpha * 1000)) : 1000}];
        if (dst.empty()) dst = std::move(t);
        else dst.insert(dst.end(), t.begin(), t.end());
    }

    void extrude(const std::vector<Region>& regs, const Xf& f, const Vec3& dir, double depth, std::vector<Tri>& out) const {
        const Vec3 dv = dir * depth;
        for (const auto& r : regs) {
            std::vector<Vec3> P;
            std::vector<int> o;
            std::vector<std::vector<int>> hs;
            for (const auto& q : r.outer) { o.push_back(static_cast<int>(P.size())); P.push_back(q); }
            for (const auto& h : r.holes) {
                hs.emplace_back();
                for (const auto& q : h) { hs.back().push_back(static_cast<int>(P.size())); P.push_back(q); }
            }
            const auto tri = triangulate(P, o, hs);
            const bool flip = dv.z < 0;   // pastga cho'zilgan — normal yo'nalishi
            for (const auto& t : tri) {
                const Vec3 a = f.p(P[t[0]]), b = f.p(P[t[1]]), c = f.p(P[t[2]]);
                const Vec3 a1 = f.p(P[t[0]] + dv), b1 = f.p(P[t[1]] + dv), c1 = f.p(P[t[2]] + dv);
                if (!flip) {
                    out.push_back({a, c, b});
                    out.push_back({a1, b1, c1});
                } else {
                    out.push_back({a, b, c});
                    out.push_back({a1, c1, b1});
                }
            }
            auto sides = [&](std::vector<Vec3> loop, bool hole) {
                if (loop.size() < 2) return;
                double ar = 0;
                for (std::size_t i = 0; i < loop.size(); ++i) {
                    const Vec3& p = loop[i];
                    const Vec3& q = loop[(i + 1) % loop.size()];
                    ar += p.x * q.y - q.x * p.y;
                }
                if ((ar < 0) != hole) std::reverse(loop.begin(), loop.end());   // tashqi — CCW, teshik — CW
                for (std::size_t i = 0; i < loop.size(); ++i) {
                    const Vec3& p = loop[i];
                    const Vec3& q = loop[(i + 1) % loop.size()];
                    const Vec3 a = f.p(p), b = f.p(q), c = f.p(q + dv), d = f.p(p + dv);
                    if (!flip) {
                        out.push_back({a, b, c});
                        out.push_back({a, c, d});
                    } else {
                        out.push_back({a, c, b});
                        out.push_back({a, d, c});
                    }
                }
            };
            sides(r.outer, false);
            for (const auto& h : r.holes) sides(h, true);
        }
    }

    void revolve(const std::vector<Region>& regs, const Xf& f, const Vec3& ao, const Vec3& ad, double angle, std::vector<Tri>& out) const {
        const int seg = std::clamp(static_cast<int>(std::ceil(std::abs(angle) / (kPi / 12))), 3, 48);
        auto rot = [&](const Vec3& p, double t) {
            const Vec3 v = p - ao;
            const Vec3 k = ad;
            const double c = std::cos(t), s = std::sin(t);
            return ao + v * c + k.cross(v) * s + k * (k.dot(v) * (1 - c));
        };
        for (const auto& r : regs) {
            auto ring = [&](const std::vector<Vec3>& loop) {
                for (int i = 0; i < seg; ++i) {
                    const double t0 = angle * i / seg, t1 = angle * (i + 1) / seg;
                    for (std::size_t k = 0; k < loop.size(); ++k) {
                        const Vec3& p = loop[k];
                        const Vec3& q = loop[(k + 1) % loop.size()];
                        const Vec3 a = f.p(rot(p, t0)), b = f.p(rot(q, t0)), c = f.p(rot(q, t1)), d = f.p(rot(p, t1));
                        out.push_back({a, b, c});
                        out.push_back({a, c, d});
                    }
                }
            };
            ring(r.outer);
            for (const auto& h : r.holes) ring(h);
            if (std::abs(angle) < 2 * kPi - 1e-6) {
                std::vector<Tri> cap;
                std::vector<Vec3> o0, o1;
                for (const auto& q : r.outer) { o0.push_back(f.p(q)); o1.push_back(f.p(rot(q, angle))); }
                triangulateFace(o0, {}, cap);
                for (auto& c : cap) std::swap(c[1], c[2]);
                triangulateFace(o1, {}, cap);
                out.insert(out.end(), cap.begin(), cap.end());
            }
        }
    }

    void loopPoints(int boundId, std::vector<Vec3>& pts, const Xf& f) const {
        const auto ba = A(boundId);   // IfcFaceBound(Bound, Orientation)
        if (ba.size() < 2) return;
        const int loop = ref(ba[0]);
        const std::string& lt = T(loop);
        const auto la = A(loop);
        if (lt == "IFCPOLYLOOP") {
            if (!la.empty())
                for (const auto& v : listOf(la[0])) pts.push_back(f.p(point(ref(v))));
        } else if (lt == "IFCEDGELOOP") {
            if (!la.empty())
                for (const auto& v : listOf(la[0])) {
                    // IfcOrientedEdge(EdgeStart*, EdgeEnd*, EdgeElement, Orientation)
                    const auto oe = A(ref(v));
                    if (oe.size() < 4) continue;
                    const auto ec = A(ref(oe[2]));   // IfcEdgeCurve(EdgeStart, EdgeEnd, EdgeGeometry, SameSense)
                    if (ec.size() < 2) continue;
                    const bool fwd = boolOf(oe[3], true);
                    const int vs = ref(ec[fwd ? 0 : 1]);
                    const auto vp = A(vs);
                    if (!vp.empty()) pts.push_back(f.p(point(ref(vp[0]))));
                }
        }
        if (!boolOf(ba[1], true)) std::reverse(pts.begin(), pts.end());
        closeLoop(pts);
    }
    void face(int fid, const Xf& f, std::vector<Tri>& out) const {
        const auto fa = A(fid);   // IfcFace(Bounds) / IfcAdvancedFace(Bounds, FaceSurface, SameSense)
        if (fa.empty()) return;
        std::vector<Vec3> outer;
        std::vector<std::vector<Vec3>> holes;
        const auto& bounds = listOf(fa[0]);
        int outerIdx = -1;
        for (std::size_t i = 0; i < bounds.size(); ++i)
            if (T(ref(bounds[i])) == "IFCFACEOUTERBOUND") { outerIdx = static_cast<int>(i); break; }
        if (outerIdx < 0) outerIdx = 0;
        for (std::size_t i = 0; i < bounds.size(); ++i) {
            std::vector<Vec3> pts;
            loopPoints(ref(bounds[i]), pts, f);
            if (pts.size() < 3) continue;
            if (static_cast<int>(i) == outerIdx) outer = std::move(pts);
            else holes.push_back(std::move(pts));
        }
        if (outer.size() < 3) return;
        const std::size_t before = out.size();
        triangulateFace(outer, holes, out);
        if (fa.size() > 2 && T(fid) == "IFCADVANCEDFACE" && !boolOf(fa[2], true))
            for (std::size_t i = before; i < out.size(); ++i) std::swap(out[i][1], out[i][2]);
    }
    void shell(int sid, const Xf& f, std::vector<Tri>& out) const {
        const auto sa = A(sid);
        if (sa.empty()) return;
        for (const auto& v : listOf(sa[0])) face(ref(v), f, out);
    }

    void indexedFaces(int id, const std::string& t, const std::vector<V>& a, const Xf& f, std::vector<Tri>& out) const {
        if (a.empty()) return;
        auto pts = pointList(ref(a[0]));
        for (auto& p : pts) p = f.p(p);
        std::vector<long> pn;
        const std::size_t pnIdx = t == "IFCTRIANGULATEDFACESET" || t == "IFCTRIANGULATEDIRREGULARNETWORK" ? 4 : 3;
        if (a.size() > pnIdx && a[pnIdx].k == V::List)
            for (const auto& v : a[pnIdx].list) pn.push_back(std::lround(num(v)));
        auto P = [&](const V& v, Vec3& q) {
            long i = std::lround(num(v));
            if (!pn.empty()) i = i >= 1 && i <= static_cast<long>(pn.size()) ? pn[static_cast<std::size_t>(i - 1)] : 0;
            if (i < 1 || i > static_cast<long>(pts.size())) return false;
            q = pts[static_cast<std::size_t>(i - 1)];
            return true;
        };
        if (t == "IFCTRIANGULATEDFACESET" || t == "IFCTRIANGULATEDIRREGULARNETWORK") {
            if (a.size() < 4) return;
            for (const auto& tri : listOf(a[3])) {
                const auto& ix = listOf(tri);
                if (ix.size() < 3) continue;
                Tri q;
                if (P(ix[0], q[0]) && P(ix[1], q[1]) && P(ix[2], q[2])) out.push_back(q);
            }
        } else {   // IFCPOLYGONALFACESET(Coordinates, Closed, Faces, PnIndex)
            if (a.size() < 3) return;
            for (const auto& fv : listOf(a[2])) {
                const auto fa = A(ref(fv));
                if (fa.empty()) continue;
                std::vector<Vec3> outer;
                for (const auto& v : listOf(fa[0])) {
                    Vec3 q;
                    if (P(v, q)) outer.push_back(q);
                }
                std::vector<std::vector<Vec3>> holes;
                if (fa.size() > 1)
                    for (const auto& hl : listOf(fa[1])) {
                        std::vector<Vec3> h;
                        for (const auto& v : listOf(hl)) {
                            Vec3 q;
                            if (P(v, q)) h.push_back(q);
                        }
                        if (h.size() >= 3) holes.push_back(h);
                    }
                closeLoop(outer);
                triangulateFace(outer, holes, out);
            }
        }
        (void)id;
    }

    bool isClosedFaceSet(const std::string& t, const std::vector<V>& a) const {
        const std::size_t ci = t == "IFCPOLYGONALFACESET" ? 1 : 2;
        return a.size() > ci && boolOf(a[ci], false);
    }

    // Element bo'lagi: item -> Mesh. depth — rekursiya himoyasi
    void item(int id, const Xf& f, const Style& inherited, Mesh& m, int depth = 0) {
        if (!id || depth > 12) return;
        Style st = inherited;
        if (auto it = itemStyle_.find(id); it != itemStyle_.end()) st = it->second;
        const std::string& t = T(id);
        const auto a = A(id);
        std::vector<Tri> tris;
        if (t == "IFCEXTRUDEDAREASOLID" || t == "IFCEXTRUDEDAREASOLIDTAPERED") {
            if (a.size() < 4) return;
            const auto regs = profile(ref(a[0]));
            const Xf pf = f * axis(ref(a[1]));
            const Vec3 dir = direction(ref(a[2]), {0, 0, 1});
            const double depthV = num(a[3]) * L_;
            extrude(regs, pf, dir, depthV, tris);
            addTris(m, st, std::move(tris), true);
        } else if (t == "IFCREVOLVEDAREASOLID") {
            if (a.size() < 4) return;
            const auto regs = profile(ref(a[0]));
            const Xf pf = f * axis(ref(a[1]));
            const auto ax = A(ref(a[2]));   // IfcAxis1Placement (profil tekisligida)
            const Vec3 ao = ax.empty() ? Vec3{} : point(ref(ax[0]));
            const Vec3 ad = ax.size() > 1 ? direction(ref(ax[1]), {0, 1, 0}) : Vec3{0, 1, 0};
            revolve(regs, pf, ao, ad, num(a[3]) * A_, tris);
            addTris(m, st, std::move(tris), true);
        } else if (t == "IFCFACETEDBREP" || t == "IFCFACETEDBREPWITHVOIDS" || t == "IFCADVANCEDBREP" || t == "IFCADVANCEDBREPWITHVOIDS" ||
                   t == "IFCMANIFOLDSOLIDBREP") {
            if (a.empty()) return;
            shell(ref(a[0]), f, tris);
            std::vector<Tri> voids;
            if (a.size() > 1)
                for (const auto& v : listOf(a[1])) shell(ref(v), f, voids);
            double vol = std::abs(signedVolume(tris)) - std::abs(signedVolume(voids));
            tris.insert(tris.end(), voids.begin(), voids.end());
            m.volume += std::max(0.0, vol);
            trackPiece(m, tris, voids.empty());
            auto& dst = m.parts[{st.set ? st.rgb : 0u, st.set ? static_cast<int>(std::lround(st.alpha * 1000)) : 1000}];
            dst.insert(dst.end(), tris.begin(), tris.end());
        } else if (t == "IFCSHELLBASEDSURFACEMODEL" || t == "IFCFACEBASEDSURFACEMODEL") {
            if (a.empty()) return;
            for (const auto& v : listOf(a[0])) shell(ref(v), f, tris);
            addTris(m, st, std::move(tris), false);
        } else if (t == "IFCCLOSEDSHELL" || t == "IFCOPENSHELL" || t == "IFCCONNECTEDFACESET") {
            shell(id, f, tris);
            addTris(m, st, std::move(tris), t == "IFCCLOSEDSHELL");
        } else if (t == "IFCFACE" || t == "IFCFACESURFACE" || t == "IFCADVANCEDFACE") {
            face(id, f, tris);
            addTris(m, st, std::move(tris), false);
        } else if (t == "IFCTRIANGULATEDFACESET" || t == "IFCPOLYGONALFACESET" || t == "IFCTRIANGULATEDIRREGULARNETWORK") {
            indexedFaces(id, t, a, f, tris);
            addTris(m, st, std::move(tris), isClosedFaceSet(t, a));
        } else if (t == "IFCMAPPEDITEM") {
            if (a.size() < 2) return;
            const auto rm = A(ref(a[0]));   // IfcRepresentationMap(MappingOrigin, MappedRepresentation)
            if (rm.size() < 2) return;
            Xf origin;
            if (ref(rm[0])) {
                const std::string& ot = T(ref(rm[0]));
                origin = ot.rfind("IFCCARTESIANTRANSFORMATIONOPERATOR", 0) == 0 ? transformOp(ref(rm[0])) : axis(ref(rm[0]));
            }
            const Xf nf = f * transformOp(ref(a[1])) * origin;
            representationItems(ref(rm[1]), nf, st, m, depth + 1);
        } else if (t == "IFCBOOLEANRESULT" || t == "IFCBOOLEANCLIPPINGRESULT") {
            if (a.size() < 3) return;
            const std::string op = str(a[0]);
            Mesh sub;
            item(ref(a[1]), f, st, sub, depth + 1);
            const int second = ref(a[2]);
            if (op == "DIFFERENCE") {
                std::vector<Plane> B;
                if (subtrahend(second, f, st, depth, B)) cutConvex(sub, B);
            } else if (op == "UNION") {
                item(second, f, st, sub, depth + 1);
            }
            // INTERSECTION — birinchi operand (kam uchraydi)
            merge(m, std::move(sub));
        } else if (t == "IFCSWEPTDISKSOLID" || t == "IFCSWEPTDISKSOLIDPOLYGONAL") {
            if (a.size() < 2) return;
            std::vector<Vec3> axisPts;
            curve(ref(a[0]), axisPts);
            for (auto& p : axisPts) p = f.p(p);
            const double r = num(a[1]) * L_ * f.scale();
            if (axisPts.size() >= 2 && r > 0) {
                m.volume += kPi * r * r * polylineLength(axisPts);
                m.bars.emplace_back(std::move(axisPts), r);
            }
        } else if (t == "IFCCSGSOLID") {
            if (!a.empty()) item(ref(a[0]), f, st, m, depth + 1);
        } else if (t == "IFCBLOCK") {
            if (a.size() < 4) return;
            const Xf pf = f * axis(ref(a[0]));
            const double x = num(a[1]) * L_, y = num(a[2]) * L_, z = num(a[3]) * L_;
            extrude({Region{{{0, 0, 0}, {x, 0, 0}, {x, y, 0}, {0, y, 0}}, {}}}, pf, {0, 0, 1}, z, tris);
            addTris(m, st, std::move(tris), true);
        } else if (t == "IFCRIGHTCIRCULARCYLINDER") {
            if (a.size() < 3) return;
            const Xf pf = f * axis(ref(a[0]));
            Region r;
            circle(r.outer, num(a[2]) * L_);
            extrude({r}, pf, {0, 0, 1}, num(a[1]) * L_, tris);
            addTris(m, st, std::move(tris), true);
        } else if (t == "IFCBOUNDINGBOX") {
            if (a.size() < 4) return;
            const Vec3 c = point(ref(a[0]));
            const double x = num(a[1]) * L_, y = num(a[2]) * L_, z = num(a[3]) * L_;
            Xf pf = f;
            pf = pf * Xf::basis({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, c);
            extrude({Region{{{0, 0, 0}, {x, 0, 0}, {x, y, 0}, {0, y, 0}}, {}}}, pf, {0, 0, 1}, z, tris);
            addTris(m, st, std::move(tris), true);
        }
    }
    // Ayiriladigan qism -> qavariq soha tekisliklari (tashqi normal; ichkari: n·p <= d). Yarim fazo — cheksiz soha.
    bool subtrahend(int id, const Xf& f, const Style& st, int depth, std::vector<Plane>& B) {
        B.clear();
        const std::string& t = T(id);
        const auto a = A(id);
        if (t == "IFCHALFSPACESOLID" || t == "IFCPOLYGONALBOUNDEDHALFSPACE" || t == "IFCBOXEDHALFSPACE") {
            if (a.size() < 2) return false;
            const auto pl = A(ref(a[0]));   // IfcPlane(Position)
            if (pl.empty()) return false;
            const Xf pp = f * axis(ref(pl[0]));
            const Vec3 o = pp.p({0, 0, 0});
            Vec3 n = norm(pp.d({0, 0, 1}));
            // ISO 10303-42: AgreementFlag TRUE — yarim fazo normal "qochadigan" tomonda: n·(p-o) <= 0
            if (!boolOf(a[1], true)) n = n * -1.0;
            B.push_back({n, n.dot(o)});
            if (t == "IFCPOLYGONALBOUNDEDHALFSPACE" && a.size() >= 4) {
                // cheksiz prizma: chegara ko'pburchagi (Position XY) Position Z bo'yicha
                const Xf bp = f * axis(ref(a[2]));
                std::vector<Vec3> poly;
                curve(ref(a[3]), poly);
                closeLoop(poly);
                if (poly.size() < 3) return false;
                std::vector<Vec3> w;
                Vec3 c{0, 0, 0};
                for (const auto& q : poly) { w.push_back(bp.p({q.x, q.y, 0})); c += w.back(); }
                c = c / static_cast<double>(w.size());
                const Vec3 z = norm(bp.d({0, 0, 1}));
                for (std::size_t i = 0; i < w.size(); ++i) {
                    const Vec3 e = w[(i + 1) % w.size()] - w[i];
                    Vec3 pn = norm(e.cross(z));
                    if (pn.length() < 0.5) continue;
                    if (pn.dot(c - w[i]) > 0) pn = pn * -1.0;
                    B.push_back({pn, pn.dot(w[i])});
                }
                // qavariqlik: hamma uchlar har bir yon tekislik ichida
                for (std::size_t k = 1; k < B.size(); ++k)
                    for (const auto& q : w)
                        if (B[k].n.dot(q) - B[k].d > 1e-6) { B.clear(); return false; }
            }
            return true;
        }
        // oddiy jism (quti, ekstruziya): qavariq bo'lsa
        Mesh m2;
        item(id, f, st, m2, depth + 1);
        std::vector<Tri> all;
        for (const auto& [k, v] : m2.parts) all.insert(all.end(), v.begin(), v.end());
        Vec3 c;
        return convexPlanes(all, B, c);
    }

    // Qavariq ko'pyoqlik hajmi (tekisliklar bo'yicha)
    static double piecePlanesVolume(const std::vector<Plane>& P, const Vec3& c, double R, std::vector<std::pair<std::size_t, std::vector<Vec3>>>* facesOut = nullptr) {
        auto faces = polytopeFaces(P, c, R);
        std::vector<Tri> tr;
        for (const auto& [idx, poly] : faces) fan(poly, tr);
        const double v = convexVolume(tr);
        if (facesOut) *facesOut = std::move(faces);
        return v;
    }

    // mesh minus qavariq soha B (cheksiz bo'lishi mumkin).
    //  * jism qavariq bo'laklardan iborat bo'lsa — har bo'lakdan aniq ayiriladi (P − B = qavariq bo'laklar), kesim yuzalari, aniq hajm
    //  * aks holda: bitta tekislik — qopqoqli kesish; ko'p tekislik — faqat ko'rinish (hajm o'zgarmaydi)
    bool cutConvex(Mesh& mesh, const std::vector<Plane>& B) {
        if (B.empty()) return false;
        BBox bb;
        for (const auto& [k, v] : mesh.parts)
            for (const auto& t : v)
                for (const auto& q : t) bb.expand(q);
        if (!bb.valid()) return false;
        const Vec3 c = (bb.min + bb.max) * 0.5;
        const double R = 2.0 * (bb.max - bb.min).length() + 1.0;
        if (!mesh.convexOk || mesh.pieces.empty()) {
            if (B.size() == 1) {   // qopqoqli tekislik kesimi
                const Vec3 o = B[0].n * B[0].d;
                const bool had = mesh.volume > 0;
                mesh.volume = 0;
                for (auto& [k, v] : mesh.parts) {
                    clipByPlane(v, o, B[0].n * -1.0);
                    if (had) mesh.volume += std::abs(signedVolume(v));
                }
                return true;
            }
            for (auto& [k, v] : mesh.parts) subtractConvex(v, B);
            return true;
        }
        double removed = 0;
        std::vector<Tri> caps;
        std::vector<std::vector<Plane>> next;
        for (const auto& P : mesh.pieces) {
            // P ∩ B (B ning P bilan ustma-ust tekisliklari tashlanadi)
            std::vector<Plane> I = P;
            std::vector<std::size_t> bIdx;   // decomposition uchun B tekisliklari
            bool touch = false;
            for (std::size_t bi = 0; bi < B.size(); ++bi) {
                const auto& b = B[bi];
                bool same = false;
                for (const auto& a : P) {
                    if (a.n.dot(b.n) > 1 - 1e-7 && std::abs(a.d - b.d) < 1e-6) { same = true; break; }
                    if (a.n.dot(b.n) < -1 + 1e-7 && std::abs(a.d + b.d) < 1e-6) { touch = true; break; }
                }
                if (touch) break;
                if (!same) { I.push_back(b); bIdx.push_back(bi); }
            }
            std::vector<std::pair<std::size_t, std::vector<Vec3>>> faces;
            const double v = touch ? 0.0 : piecePlanesVolume(I, c, R, &faces);
            if (v < 1e-10) { next.push_back(P); continue; }
            removed += v;
            for (const auto& [idx, poly] : faces)
                if (idx >= P.size()) fan(poly, caps, true);   // B chegarasidagi yuzalar — kesim (tirqish) yuzalari
            // P − B = ⋃_k P ∩ (B_0..B_{k-1} ichida) ∩ (B_k tashqarisida)
            std::vector<Plane> acc = P;
            for (std::size_t k : bIdx) {
                std::vector<Plane> q = acc;
                q.push_back({B[k].n * -1.0, -B[k].d});
                if (piecePlanesVolume(q, c, R) > 1e-10) next.push_back(std::move(q));
                acc.push_back(B[k]);
            }
        }
        if (removed < 1e-12) return false;
        mesh.pieces = std::move(next);
        for (auto& [k, v] : mesh.parts) subtractConvex(v, B);
        auto best = mesh.parts.begin();
        for (auto it = mesh.parts.begin(); it != mesh.parts.end(); ++it)
            if (it->second.size() > best->second.size()) best = it;
        best->second.insert(best->second.end(), caps.begin(), caps.end());
        if (mesh.volume > 0) mesh.volume = std::max(0.0, mesh.volume - removed);
        return true;
    }

    // Teshiklar (IfcRelVoidsElement): har bir teshik itemi qavariq soha sifatida ayiriladi
    void cutOpenings(Mesh& mesh, const std::vector<int>& openings) {
        for (int op : openings) {
            const auto oa = s_.args(op);
            if (oa.size() < 7 || !ref(oa[6])) continue;
            const int rep = pickRepresentation(ref(oa[6]));
            if (!rep) continue;
            const auto ra = s_.args(rep);
            if (ra.size() < 4) continue;
            const Xf f = geo_ * placement(ref(oa[5]));
            bool any = false;
            for (const auto& v : listOf(ra[3])) {
                std::vector<Plane> B;
                if (subtrahend(ref(v), f, Style{}, 0, B) && cutConvex(mesh, B)) any = true;
            }
            if (any) ++openingsCut_;
        }
    }

    static void trackPiece(Mesh& m, const std::vector<Tri>& t, bool solid) {
        if (!m.convexOk) return;
        std::vector<Plane> P;
        Vec3 c;
        if (solid && convexPlanes(t, P, c)) m.pieces.push_back(std::move(P));
        else { m.convexOk = false; m.pieces.clear(); }
    }
    static void merge(Mesh& dst, Mesh&& src) {
        dst.convexOk = dst.convexOk && src.convexOk;
        if (dst.convexOk) for (auto& p : src.pieces) dst.pieces.push_back(std::move(p));
        else dst.pieces.clear();
        for (auto& [k, v] : src.parts) {
            auto& d = dst.parts[k];
            d.insert(d.end(), v.begin(), v.end());
        }
        for (auto& b : src.bars) dst.bars.push_back(std::move(b));
        dst.volume += src.volume;
    }
    void representationItems(int repId, const Xf& f, const Style& st, Mesh& m, int depth) {
        const auto ra = A(repId);   // IfcShapeRepresentation(ContextOfItems, Identifier, Type, Items)
        if (ra.size() < 4) return;
        for (const auto& v : listOf(ra[3])) item(ref(v), f, st, m, depth);
    }
    // Ko'rinish uchun eng mos tasvir: Body > Facetation > boshqa jismli; Box — oxirgi zaxira
    int pickRepresentation(int pdsId) const {
        const auto pa = A(pdsId);   // IfcProductDefinitionShape(Name, Description, Representations)
        if (pa.size() < 3) return 0;
        int body = 0, facet = 0, other = 0, box = 0;
        for (const auto& v : listOf(pa[2])) {
            const int r = ref(v);
            const auto ra = A(r);
            if (ra.size() < 4) continue;
            const std::string id = upper(str(ra[1])), ty = upper(str(ra[2]));
            if (ty == "CURVE2D" || ty == "ANNOTATION2D" || ty == "GEOMETRICCURVESET" || ty == "GEOMETRICSET" || id == "AXIS" || id == "FOOTPRINT" ||
                id == "PLAN" || id == "ANNOTATION" || id == "PROFILE" || id == "CLEARANCE" || id == "REFERENCE" || (id == "SURFACE" && ty == "CURVE3D"))
                continue;
            if (id == "BODY" && !body) body = r;
            else if (id == "FACETATION" && !facet) facet = r;
            else if (id == "BOX" || ty == "BOUNDINGBOX") { if (!box) box = r; }
            else if (!other) other = r;
        }
        return body ? body : facet ? facet : other ? other : box;
    }

    void products() {
        static const std::unordered_set<std::string> skip{"IFCOPENINGELEMENT", "IFCOPENINGSTANDARDCASE", "IFCVIRTUALELEMENT", "IFCANNOTATION",
                                                          "IFCGRID", "IFCSPATIALZONE", "IFCPROJECT", "IFCBUILDING", "IFCBUILDINGSTOREY",
                                                          "IFCALIGNMENT", "IFCALIGNMENTHORIZONTAL", "IFCALIGNMENTVERTICAL", "IFCALIGNMENTCANT",
                                                          "IFCALIGNMENTSEGMENT", "IFCREFERENT", "IFCPORT", "IFCDISTRIBUTIONPORT", "IFCFACILITYPART",
                                                          "IFCFACILITY", "IFCBRIDGE", "IFCROAD", "IFCRAILWAY", "IFCMARINEFACILITY"};
        const std::uint32_t pds = s_.typeCode("IFCPRODUCTDEFINITIONSHAPE");
        for (int id : s_.order) {
            const auto& rec = s_.recs.at(id);
            if (rec.type == 0) continue;
            const std::string& t = s_.types[rec.type];
            if (t.compare(0, 3, "IFC") != 0 || t.compare(0, 6, "IFCREL") == 0) continue;
            // IfcProduct: (GlobalId, OwnerHistory, Name, Description, ObjectType, ObjectPlacement, Representation, ...)
            // tez tekshiruv: 7-argument PDS ga havolami
            if (t.find("PROFILE") != std::string::npos || t.find("STYLE") != std::string::npos || t.ends_with("TYPE")) continue;
            {   // birinchi argument — GUID satri bo'lishi kerak (nuqtalar, yo'nalishlar va h.k. tez o'tkaziladi)
                const char* q = s_.text.data() + rec.a + 1;
                const char* qe = s_.text.data() + rec.b;
                while (q < qe && (*q == ' ' || *q == '\r' || *q == '\n' || *q == '\t')) ++q;
                if (q >= qe || *q != '\'') continue;
            }
            const auto a = A(id);
            if (a.size() < 7 || a[0].k != V::Str || !ref(a[6])) continue;
            if (pds == 0xffffffffu || s_.recs.count(ref(a[6])) == 0 || s_.recs.at(ref(a[6])).type != pds) continue;
            if (t == "IFCOPENINGELEMENT" || t == "IFCOPENINGSTANDARDCASE") { ++m_.openings; continue; }
            if (skip.count(t)) continue;
            IfcElement el;
            el.id = id;
            el.type = t;
            el.guid = a[0].s;
            el.name = str(a[2]);
            el.objectType = str(a[4]);
            if (a.size() > 7 && a[7].k == V::Str) el.tag = a[7].s;
            el.storey = storeyOf(id);
            // material
            std::vector<std::string> mn;
            std::vector<int> mats;
            if (auto it = matsOf_.find(id); it != matsOf_.end())
                for (int mm : it->second) materialNames(mm, mn, mats);
            const int ty = typeOf_.count(id) ? typeOf_.at(id) : 0;
            if (mn.empty() && ty)
                if (auto it = matsOf_.find(ty); it != matsOf_.end())
                    for (int mm : it->second) materialNames(mm, mn, mats);
            for (const auto& s : mn) {
                if (!el.material.empty()) el.material += "; ";
                el.material += s;
            }
            Style matSt;
            for (int mm : mats)
                if (auto it = matStyle_.find(mm); it != matStyle_.end()) { matSt = it->second; break; }
            // xossalar
            if (ty) {
                const auto ta = A(ty);
                if (ta.size() > 2 && !str(ta[2]).empty()) el.props.emplace_back("Type", str(ta[2]));
                if (ta.size() > 5)
                    for (const auto& v : listOf(ta[5])) properties(ref(v), el.props);   // HasPropertySets
            }
            if (auto it = psetsOf_.find(id); it != psetsOf_.end())
                for (int ps : it->second) properties(ps, el.props);
            // geometriya
            const int rep = pickRepresentation(ref(a[6]));
            if (!rep) { ++m_.skipped; continue; }
            const Xf f = geo_ * placement(ref(a[5]));
            Mesh mesh;
            representationItems(rep, f, Style{}, mesh, 0);
            if (mesh.tris() == 0 && mesh.bars.empty()) { ++m_.skipped; continue; }
            if (auto it = voidsOf_.find(id); it != voidsOf_.end()) cutOpenings(mesh, it->second);
            const std::uint32_t defRgb = matSt.set ? matSt.rgb : ifcDefaultColor(t);
            const double defAlpha = matSt.set ? matSt.alpha : (t == "IFCSPACE" ? 0.25 : 1.0);
            std::map<std::pair<std::uint32_t, int>, std::size_t> slot;
            for (auto& [k, v] : mesh.parts) {
                const std::uint32_t rgb = k.first ? k.first : defRgb;
                const int al = k.first ? k.second : static_cast<int>(std::lround(defAlpha * 1000));
                auto it = slot.find({rgb, al});
                if (it == slot.end()) {
                    it = slot.emplace(std::make_pair(rgb, al), el.parts.size()).first;
                    el.parts.push_back({rgb, al / 1000.0, {}});
                }
                auto& d = el.parts[it->second].tris;
                for (const auto& tr : v) {
                    for (const auto& p : tr) el.box.expand(p);
                    d.push_back(tr);
                }
            }
            for (auto& b : mesh.bars) {
                for (const auto& p : b.first) el.box.expand(p);
                el.barLength += polylineLength(b.first);
                el.barMass += kPi * b.second * b.second * polylineLength(b.first) * 7850.0;
                el.bars.push_back(std::move(b));
            }
            el.volume = mesh.volume;
            if (el.storey >= 0) ++m_.storeys[static_cast<std::size_t>(el.storey)].elements;
            if (el.box.valid()) {
                m_.box.expand(el.box.min);
                m_.box.expand(el.box.max);
            }
            m_.elements.push_back(std::move(el));
        }
    }
    void finish() {
        if (m_.georef) m_.warnings.push_back("ifc.warn.georef");
        m_.openingsCut = openingsCut_;
        if (m_.openings > openingsCut_) m_.warnings.push_back("ifc.warn.openings");
        if (m_.skipped > 0) m_.warnings.push_back("ifc.warn.skipped");
    }
};

} // namespace

// ======================================================================= ochiq API
std::size_t IfcElement::triangleCount() const {
    std::size_t n = 0;
    for (const auto& p : parts) n += p.tris.size();
    return n;
}

std::size_t IfcModel::triangleCount() const {
    std::size_t n = 0;
    for (const auto& e : elements) n += e.triangleCount();
    return n;
}

std::string ifcDecodeString(const std::string& s) {
    auto utf8 = [](std::string& out, std::uint32_t cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    };
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] != '\\') { out += s[i++]; continue; }
        if (s.compare(i, 4, "\\X2\\") == 0 || s.compare(i, 4, "\\X4\\") == 0) {
            const int w = s[i + 2] == '2' ? 4 : 8;
            std::size_t j = i + 4;
            std::uint32_t hi = 0;
            while (j + static_cast<std::size_t>(w) <= s.size() && s.compare(j, 4, "\\X0\\") != 0) {
                std::uint32_t cp = 0;
                bool ok = true;
                for (int k = 0; k < w; ++k) {
                    const int h = hex(s[j + static_cast<std::size_t>(k)]);
                    if (h < 0) { ok = false; break; }
                    cp = cp * 16 + static_cast<std::uint32_t>(h);
                }
                if (!ok) break;
                j += static_cast<std::size_t>(w);
                if (w == 4 && cp >= 0xD800 && cp < 0xDC00) { hi = cp; continue; }
                if (w == 4 && cp >= 0xDC00 && cp < 0xE000 && hi) { cp = 0x10000 + ((hi - 0xD800) << 10) + (cp - 0xDC00); hi = 0; }
                utf8(out, cp);
            }
            if (s.compare(j, 4, "\\X0\\") == 0) j += 4;
            i = j;
            continue;
        }
        if (s.compare(i, 3, "\\X\\") == 0 && i + 5 <= s.size() && hex(s[i + 3]) >= 0 && hex(s[i + 4]) >= 0) {
            utf8(out, static_cast<std::uint32_t>(hex(s[i + 3]) * 16 + hex(s[i + 4])));
            i += 5;
            continue;
        }
        if (s.compare(i, 3, "\\S\\") == 0 && i + 3 < s.size()) {
            utf8(out, static_cast<std::uint32_t>(static_cast<unsigned char>(s[i + 3])) + 128);
            i += 4;
            continue;
        }
        if (s.compare(i, 2, "\\P") == 0 && i + 3 < s.size() && s[i + 3] == '\\') { i += 4; continue; }   // kod sahifasi
        if (s.compare(i, 2, "\\\\") == 0) { out += '\\'; i += 2; continue; }
        out += s[i++];
    }
    return out;
}

const char* ifcCategory(const std::string& t) {
    static const std::vector<std::pair<const char*, const char*>> map{
        {"IFCWALL", "WALL"},          {"IFCCURTAINWALL", "CURTAIN"}, {"IFCSLAB", "SLAB"},
        {"IFCCOLUMN", "COLUMN"},      {"IFCBEAM", "BEAM"},           {"IFCFOOTING", "FOOTING"},
        {"IFCPILE", "PILE"},          {"IFCSTAIR", "STAIR"},         {"IFCRAMP", "STAIR"},
        {"IFCWINDOW", "WINDOW"},      {"IFCDOOR", "DOOR"},           {"IFCROOF", "ROOF"},
        {"IFCRAILING", "RAILING"},    {"IFCCOVERING", "COVERING"},   {"IFCPLATE", "PLATE"},
        {"IFCMEMBER", "MEMBER"},      {"IFCREINFORCINGBAR", "REBAR"}, {"IFCREINFORCINGMESH", "REBAR"},
        {"IFCTENDON", "REBAR"},       {"IFCPIPE", "PIPE"},           {"IFCFLOWSEGMENT", "PIPE"},
        {"IFCDUCT", "DUCT"},          {"IFCCABLE", "MEP"},           {"IFCFLOW", "MEP"},
        {"IFCDISTRIBUTION", "MEP"},   {"IFCENERGYCONVERSION", "MEP"}, {"IFCAIRTERMINAL", "MEP"},
        {"IFCSANITARY", "MEP"},       {"IFCLIGHT", "MEP"},           {"IFCVALVE", "MEP"},
        {"IFCPUMP", "MEP"},           {"IFCTANK", "MEP"},            {"IFCFURNISHING", "FURNITURE"},
        {"IFCFURNITURE", "FURNITURE"}, {"IFCSYSTEMFURNITURE", "FURNITURE"}, {"IFCSPACE", "SPACE"},
        {"IFCSITE", "SITE"},          {"IFCGEOGRAPHICELEMENT", "SITE"}, {"IFCEARTHWORKS", "SITE"},
        {"IFCCHIMNEY", "OTHER"},      {"IFCBUILDINGELEMENTPROXY", "PROXY"}, {"IFCELEMENTASSEMBLY", "OTHER"},
    };
    for (const auto& [k, v] : map)
        if (t.rfind(k, 0) == 0) return v;
    return "OTHER";
}

std::uint32_t ifcDefaultColor(const std::string& t) {
    const std::string c = ifcCategory(t);
    if (c == "WALL") return 0xd8d2c4;
    if (c == "SLAB") return 0xa9b8c8;
    if (c == "COLUMN" || c == "BEAM") return 0xb5aea2;
    if (c == "FOOTING" || c == "PILE") return 0x958f85;
    if (c == "STAIR") return 0xd9c9a3;
    if (c == "WINDOW" || c == "CURTAIN") return 0x86b8e0;
    if (c == "DOOR") return 0x9a7a55;
    if (c == "ROOF") return 0xa05a46;
    if (c == "RAILING" || c == "MEMBER" || c == "PLATE") return 0x8f99a6;
    if (c == "REBAR") return 0xc0603a;
    if (c == "PIPE") return 0x4f8fc0;
    if (c == "DUCT") return 0x9db0c0;
    if (c == "MEP") return 0x6aa58a;
    if (c == "FURNITURE") return 0xb79068;
    if (c == "SPACE") return 0x7fb0ff;
    if (c == "SITE") return 0x8fae6a;
    if (c == "COVERING") return 0xcdbfa8;
    return 0xbdbdbd;
}

std::vector<IfcSummaryRow> ifcSummary(const IfcModel& m) {
    std::map<std::string, IfcSummaryRow> rows;
    for (const auto& e : m.elements) {
        auto& r = rows[ifcCategory(e.type)];
        r.category = ifcCategory(e.type);
        ++r.count;
        if (r.category != "SPACE") r.volume += e.volume;
        r.barLength += e.barLength;
        r.barMass += e.barMass;
    }
    std::vector<IfcSummaryRow> out;
    for (auto& [k, v] : rows) out.push_back(v);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
    return out;
}

bool parseIfc(const std::string& text, IfcModel& out, std::string& err, bool georef) {
    out = IfcModel{};
    Step s;
    s.text = text;
    if (!s.index(err)) return false;
    Reader r(s, out, georef);
    r.run();
    if (out.elements.empty()) {
        err = "ifc.err.nogeom";
        return false;
    }
    return true;
}

bool readIfc(const std::string& path, IfcModel& out, std::string& err, bool georef) {
#ifdef _WIN32
    // UTF-8 yo'l (kirill / o'zbek harflari) — keng belgili nomga
    std::wstring w;
    {
        std::size_t i = 0;
        while (i < path.size()) {
            const unsigned char c = static_cast<unsigned char>(path[i]);
            std::uint32_t cp = c;
            int extra = 0;
            if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
            else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
            else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
            ++i;
            for (int k = 0; k < extra && i < path.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(path[i]) & 0x3F);
            if (cp >= 0x10000) {
                cp -= 0x10000;
                w += static_cast<wchar_t>(0xD800 + (cp >> 10));
                w += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
            } else w += static_cast<wchar_t>(cp);
        }
    }
    std::ifstream f(w.c_str(), std::ios::binary);
#else
    std::ifstream f(path, std::ios::binary);
#endif
    if (!f) { err = "ifc.err.open"; return false; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (text.size() >= 2 && text[0] == 'P' && text[1] == 'K') {   // .ifczip — ichidagi birinchi .ifc
        try {
            const auto files = xlsx::zip::extract(text);
            for (const auto& [name, data] : files) {
                std::string low = name;
                for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (low.size() > 4 && low.compare(low.size() - 4, 4, ".ifc") == 0) return parseIfc(data, out, err, georef);
            }
        } catch (...) {
        }
        err = "ifc.err.zip";
        return false;
    }
    if (text.find("<ifcXML") != std::string::npos || text.find("<?xml") == 0) { err = "ifc.err.xml"; return false; }
    return parseIfc(text, out, err, georef);
}

} // namespace asiacad::bim
