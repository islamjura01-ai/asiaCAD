#include <asiacad/bim/rebar_import.h>

#include <asiacad/core/entities.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>

namespace asiacad::bim {

namespace {
// Matnni soddalashtirish: Ø belgilari -> '@', kirill A/С -> lotin, vergul -> nuqta
std::string normText(const std::string& s0) {
    std::string s = s0;
    auto rep = [&](const std::string& a, const std::string& b) {
        for (std::size_t p = 0; (p = s.find(a, p)) != std::string::npos; p += b.size()) s.replace(p, a.size(), b);
    };
    rep("\xC3\x98", "@");          // Ø
    rep("\xC3\xB8", "@");          // ø
    rep("\xE2\x8C\x80", "@");      // ⌀
    rep("\xE2\x88\x85", "@");      // ∅
    rep("%%C", "@");
    rep("%%c", "@");
    rep("\xD0\x90", "A");          // А
    rep("\xD0\xB0", "a");          // а
    rep("\xD0\xA1", "C");          // С
    rep("\xD1\x81", "c");          // с
    rep("\xD0\x92", "B");          // В (sinf B500)
    for (auto& c : s)
        if (c == ',') c = '.';
    return s;
}
bool num(const std::string& s, std::size_t p, double& v, std::size_t* end = nullptr) {
    while (p < s.size() && (s[p] == ' ' || s[p] == '=' || s[p] == ':' || s[p] == '.')) ++p;
    std::size_t q = p;
    bool dig = false;
    while (q < s.size() && (std::isdigit(static_cast<unsigned char>(s[q])) || (s[q] == '.' && dig))) { dig = true; ++q; }
    if (!dig) return false;
    if (s[q - 1] == '.') --q;
    v = std::stod(s.substr(p, q - p));
    if (end) *end = q;
    return true;
}
std::string upper(const std::string& s) { return utf8::toUpper(s); }
std::string key(const char* k) { return normText(upper(k)); }  // qidiruv kaliti matn bilan bir xil normallashadi
double massPerM(double d) { return 3.14159265358979323846 * d * d / 4 * 7850.0; }
} // namespace

std::string normRebarClass(const std::string& raw) {
    std::string s;
    for (char c : normText(raw)) {
        if (c == '-' || c == ' ') continue;
        s += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    static const std::pair<const char*, const char*> roman[] = {{"AIIIB", "A400"}, {"AIII", "A400"}, {"AII", "A300"}, {"AIV", "A600"},
                                                                {"AI", "A240"},    {"AV", "A800"}};
    for (const auto& [r, c] : roman)
        if (s.rfind(r, 0) == 0) return c;
    return s;
}

bool isRebarLayer(const std::string& layer) {
    const std::string u = upper(layer);
    for (const char* k : {"ARM", "АРМ", "REBAR", "STERJEN", "СТЕРЖ", "DONATI", "ДОНАТ", "BAR_", "_BAR"})
        if (u.find(upper(k)) != std::string::npos) return true;
    return false;
}
int faceFromLayer(const std::string& layer) {
    const std::string u = upper(layer);
    for (const char* k : {"ВЕРХ", "YUQORI", "ЮҚОРИ", "TOP", "VERX", "VERH", "ÜST"})
        if (u.find(upper(k)) != std::string::npos) return 2;
    for (const char* k : {"НИЗ", "НИЖ", "PASTKI", "ПАСТКИ", "BOT", "NIZ", "NIJ", "ALT"})
        if (u.find(upper(k)) != std::string::npos) return 1;
    return 0;
}

bool parseBarMark(const std::string& text, BarMark& m) {
    // yagona kanonik ko'rinish: katta harf + normallashtirish (offsetlar bitta satrda)
    std::string s = normText(upper(text));
    const std::string& u = s;
    m = {};
    // pozitsiya: "Поз.3", "Poz 3", "Pos.3"
    for (const char* k : {"ПОЗ", "POZ", "POS"}) {
        const std::string K = key(k);
        const auto p = s.find(K);
        if (p == std::string::npos) continue;
        double v;
        std::size_t e = 0;
        if (num(s, p + K.size(), v, &e)) {
            m.pos = static_cast<int>(v);
            s.replace(p, e - p, std::string(e - p, ' '));  // keyin "soni" bilan adashmasin
        }
        break;
    }
    const auto at = s.find('@');
    if (at == std::string::npos) return false;
    double v;
    std::size_t e = 0;
    if (!num(s, at + 1, v, &e) || v < 3 || v > 60) return false;
    m.d = v / 1000;
    {   // soni: Ø oldidagi butun son ("4Ø16", "4 Ø16")
        std::size_t q = at;
        while (q > 0 && s[q - 1] == ' ') --q;
        std::size_t r = q;
        while (r > 0 && std::isdigit(static_cast<unsigned char>(s[r - 1]))) --r;
        if (r < q && (r == 0 || !std::isalpha(static_cast<unsigned char>(s[r - 1])))) m.count = std::stoi(s.substr(r, q - r));
    }
    for (std::size_t p = e; p < s.size(); ++p) {  // sinf
        if (s[p] != 'A' && s[p] != 'B') continue;
        std::size_t q = p + 1;
        while (q < s.size() && (std::isalnum(static_cast<unsigned char>(s[q])) || s[q] == '-')) ++q;
        const std::string n = normRebarClass(s.substr(p, q - p));
        if (n.size() >= 4 && std::isdigit(static_cast<unsigned char>(n[1]))) { m.cls = n; break; }
        p = q;
    }
    if (const auto p = u.find("L="); p != std::string::npos && num(s, p + 2, v)) m.length = v > 50 ? v / 1000 : v;
    for (const char* k : {"ШАГ", "Ш.", "QADAM", "ҚАДАМ", "S=", "STEP", "ADIM"}) {
        const std::string K = key(k);
        const auto p = u.find(K);
        if (p != std::string::npos && num(u, p + K.size(), v)) { m.spacing = v > 20 ? v / 1000 : v; break; }
    }
    if (m.spacing == 0)
        if (const auto p2 = s.find('@', e); p2 != std::string::npos && num(s, p2 + 1, v) && v >= 50) m.spacing = v / 1000;
    for (const char* k : {"ВЕРХ", "YUQORI", "ЮҚОРИ", "TOP", "VERX", "VERH"})
        if (u.find(key(k)) != std::string::npos) m.face = 2;
    for (const char* k : {"НИЗ", "НИЖ", "PASTKI", "ПАСТКИ", "BOTTOM", "NIZ"})
        if (u.find(key(k)) != std::string::npos) m.face = 1;
    return true;
}

// ---------------------------------------------------------------- spetsifikatsiya
std::vector<SpecRow> readSpecification(const Document& doc, BBox* region) {
    struct T { double x, y, h; std::string s; };
    std::vector<T> ts;
    std::function<void(const IEntity&)> visit = [&](const IEntity& e) {
        if (auto* ins = dynamic_cast<const InsertEntity*>(&e)) {
            for (const auto& c : ins->explode()) visit(*c);
            return;
        }
        if (auto* t = dynamic_cast<const TextEntity*>(&e)) ts.push_back({t->insertion().x, t->insertion().y, t->height(), t->text()});
    };
    doc.forEach([&](const IEntity& e) { visit(e); });
    std::vector<SpecRow> out;
    auto has = [](const std::string& s, std::initializer_list<const char*> ks) {
        const std::string u = upper(s);
        for (const char* k : ks)
            if (u.find(upper(k)) != std::string::npos) return true;
        return false;
    };
    for (const auto& hdr : ts) {
        if (!has(hdr.s, {"ПОЗ", "POZ", "POS"}) || hdr.s.find('@') != std::string::npos || normText(hdr.s).find('@') != std::string::npos) continue;
        const double tol = std::max(hdr.h, 1e-6) * 1.2;
        double xCount = 1e18, xMass = 1e18;
        bool sameRowHeader = false;
        for (const auto& o : ts) {
            if (std::abs(o.y - hdr.y) > tol) continue;
            if (has(o.s, {"КОЛ", "SONI", "СОНИ", "COUNT", "QTY", "ADET"})) { xCount = o.x; sameRowHeader = true; }
            if (has(o.s, {"МАСС", "MASSA", "MASS", "KÜTLE", "AĞIRLIK"})) xMass = o.x;
        }
        if (!sameRowHeader) continue;
        // qatorlar: sarlavhadan pastdagi matnlar Y bo'yicha guruhlanadi
        std::vector<T> below;
        for (const auto& o : ts)
            if (o.y < hdr.y - tol * 0.5) below.push_back(o);
        std::sort(below.begin(), below.end(), [](const T& a, const T& b) { return a.y > b.y; });
        std::vector<std::vector<T>> rows;
        for (const auto& o : below) {
            if (!rows.empty() && std::abs(rows.back().front().y - o.y) <= tol) rows.back().push_back(o);
            else rows.push_back({o});
        }
        double lastY = hdr.y;
        for (auto& r : rows) {
            if (lastY - r.front().y > hdr.h * 8) break;  // jadval tugadi
            std::sort(r.begin(), r.end(), [](const T& a, const T& b) { return a.x < b.x; });
            SpecRow row;
            // pozitsiya — sarlavha ustunidagi butun son
            double bestPos = 1e18, bestCnt = 1e18, bestMass = 1e18;
            std::string joined;
            for (const auto& c : r) {
                joined += c.s + " ";
                double v;
                std::size_t e = 0;
                const std::string n = normText(c.s);
                const bool pureNum = num(n, 0, v, &e) && n.find_first_not_of(" ", e) == std::string::npos;
                if (!pureNum) continue;
                if (std::abs(c.x - hdr.x) < bestPos && std::abs(c.x - hdr.x) < hdr.h * 6 && v == std::floor(v)) { bestPos = std::abs(c.x - hdr.x); row.pos = static_cast<int>(v); }
                if (std::abs(c.x - xCount) < bestCnt && std::abs(c.x - xCount) < hdr.h * 8) { bestCnt = std::abs(c.x - xCount); row.count = static_cast<int>(std::lround(v)); }
                if (std::abs(c.x - xMass) < bestMass && std::abs(c.x - xMass) < hdr.h * 8) { bestMass = std::abs(c.x - xMass); row.massEach = v; }
            }
            BarMark bm;
            if (row.pos <= 0 || !parseBarMark(joined, bm)) {
                if (row.pos > 0) continue;
                break;
            }
            row.d = bm.d;
            row.cls = bm.cls;
            row.length = bm.length;
            row.text = joined;
            if (row.massEach <= 0 && row.length > 0) row.massEach = row.length * massPerM(row.d);
            row.mass = row.massEach * row.count;
            out.push_back(row);
            lastY = r.front().y;
            if (region) {
                region->expand({hdr.x - hdr.h * 4, hdr.y + tol, 0});
                for (const auto& c : r) region->expand({c.x + c.h * 0.6 * static_cast<double>(c.s.size()), c.y - tol, 0});
            }
        }
        if (!out.empty()) break;  // bitta jadval yetarli
        if (region) *region = BBox{};
    }
    return out;
}

// ---------------------------------------------------------------- import
ImportedRebar importRebar(const std::vector<RebarSheetInput>& sheets, const BuildingModel& bm, double cover) {
    ImportedRebar R;
    auto issue = [&](bool e, const char* k, std::vector<std::string> a) { R.issues.push_back({e, k, std::move(a)}); };
    std::map<std::pair<int, int>, ScheduleRow> sched;  // (pos, level)
    for (const auto& sh : sheets) {
        if (!sh.doc) continue;
        // geometriya va yozuvlar
        struct Seg { std::vector<Vec3> pts; int face; bool used = false; };
        std::vector<Seg> segs;
        struct Lbl { Vec3 at; BarMark m; };
        std::vector<Lbl> lbls;
        std::function<void(const IEntity&)> visit = [&](const IEntity& e) {
            if (auto* ins = dynamic_cast<const InsertEntity*>(&e)) {
                for (const auto& c : ins->explode()) visit(*c);
                for (const auto& a : ins->attributes()) visit(a.text);
                return;
            }
            if (auto* t = dynamic_cast<const TextEntity*>(&e)) {
                BarMark m;
                if (parseBarMark(t->text(), m)) lbls.push_back({t->insertion(), m});
                return;
            }
            if (!isRebarLayer(e.layer())) return;
            std::vector<Vec3> pts;
            if (auto* l = dynamic_cast<const LineEntity*>(&e)) pts = {l->startPoint(), l->endPoint()};
            else if (auto* pl = dynamic_cast<const PolylineEntity*>(&e)) pts = pl->vertices();
            if (pts.size() >= 2) segs.push_back({pts, faceFromLayer(e.layer())});
        };
        sh.doc->forEach([&](const IEntity& e) { visit(e); });
        BBox table;
        auto spec = readSpecification(*sh.doc, &table);
        // spetsifikatsiya jadvalidagi matnlar markirovka emas
        std::vector<Lbl> marks;
        for (const auto& l : lbls)
            if (!table.valid() || !(l.at.x >= table.min.x && l.at.x <= table.max.x && l.at.y >= table.min.y && l.at.y <= table.max.y))
                marks.push_back(l);
        const int nFloors = std::max(1, sh.floorTo - sh.floorFrom + 1);
        for (auto r : spec) {
            r.count *= nFloors;
            r.mass *= nFloors;
            R.specMass += r.mass;
            R.spec.push_back(r);
        }
        auto dist = [](const Vec3& p, const std::vector<Vec3>& pts) {
            double best = 1e18;
            for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                const Vec3 a = pts[i], b = pts[i + 1];
                const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
                const double u = l2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0, 1.0) : 0;
                best = std::min(best, std::hypot(a.x + u * dx - p.x, a.y + u * dy - p.y));
            }
            return best;
        };
        auto dirOf = [](const std::vector<Vec3>& pts) {
            const double dx = pts.back().x - pts.front().x, dy = pts.back().y - pts.front().y, l = std::hypot(dx, dy);
            return l > 0 ? Vec3{dx / l, dy / l, 0} : Vec3{1, 0, 0};
        };
        const bool geom = sh.element == ElemKind::Slab || sh.element == ElemKind::Roof || sh.element == ElemKind::FoundationSlab;
        // 1-bosqich: har markirovkaga eng yaqin sterjen (yozuv 3 m ichida) band qilinadi
        std::vector<int> barOf(marks.size(), -1);
        for (std::size_t li = 0; li < marks.size(); ++li) {
            double bd = 3.0;
            const auto& mm = marks[li].m;
            for (std::size_t i = 0; i < segs.size(); ++i) {
                if (segs[i].used) continue;
                double d = dist(marks[li].at, segs[i].pts);
                if (d > 3.0) continue;
                // teng masofada: yuza (qatlam) va uzunlik mosligi hal qiladi
                if (mm.face && segs[i].face && mm.face != segs[i].face) d += 1.0;
                if (mm.length > 0) {
                    double len = 0;
                    for (std::size_t j = 0; j + 1 < segs[i].pts.size(); ++j) len += segs[i].pts[j].distanceTo(segs[i].pts[j + 1]);
                    d += 0.1 * std::abs(len - mm.length);
                }
                if (d < bd) { bd = d; barOf[li] = static_cast<int>(i); }
            }
            if (barOf[li] >= 0) segs[static_cast<std::size_t>(barOf[li])].used = true;
        }
        for (std::size_t li = 0; li < marks.size(); ++li) {
            const auto& mk = marks[li];
            ++R.marks;
            // markirovkaga to'liq ma'lumotni spetsifikatsiyadan to'ldirish
            BarMark m = mk.m;
            for (const auto& r : spec)
                if (m.pos > 0 && r.pos == m.pos) {
                    if (m.cls.empty()) m.cls = r.cls;
                    if (m.length == 0) m.length = r.length;
                    if (std::abs(r.d - m.d) > 1e-6) issue(true, "rbi.chk.dia", {std::to_string(m.pos), std::format("{:.0f}", m.d * 1000), std::format("{:.0f}", r.d * 1000)});
                }
            if (m.cls.empty()) m.cls = "A400";
            if (!geom) continue;
            // taqsimlash chizig'i — sterjenga perpendikulyar kesib o'tuvchi (band qilinmagan) chiziq
            const int best = barOf[li];
            if (best < 0) {
                issue(false, "rbi.chk.nobar", {m.pos ? std::to_string(m.pos) : "?", std::format("{:.2f}; {:.2f}", mk.at.x, mk.at.y)});
                continue;
            }
            Seg& bar = segs[static_cast<std::size_t>(best)];
            const Vec3 u = dirOf(bar.pts);
            int count = m.count > 0 ? m.count : 1;
            Vec3 a0 = bar.pts.front(), step{0, 0, 0};
            if (m.spacing > 0) {
                // taqsimlash chizig'i
                for (auto& s : segs) {
                    if (&s == &bar || s.used || s.pts.size() != 2) continue;
                    const Vec3 v = dirOf(s.pts);
                    if (std::abs(u.x * v.x + u.y * v.y) > 0.17) continue;  // ~80..100°
                    if (dist(s.pts.front(), bar.pts) > 0.5 && dist(s.pts.back(), bar.pts) > 0.5) {
                        // kesishishini tekshirish: sterjen chizig'i taqsimlash kesmasi ichidan o'tadimi
                        const double t0 = (s.pts.front().x - a0.x) * -u.y + (s.pts.front().y - a0.y) * u.x;
                        const double t1 = (s.pts.back().x - a0.x) * -u.y + (s.pts.back().y - a0.y) * u.x;
                        if (t0 * t1 > 0) continue;
                    }
                    const double len = std::hypot(s.pts.back().x - s.pts.front().x, s.pts.back().y - s.pts.front().y);
                    if (m.count == 0) count = static_cast<int>(std::floor(len / m.spacing + 1e-6)) + 1;
                    // nusxalar taqsimlash chizig'i boshidan
                    const Vec3 sv = dirOf(s.pts);
                    const double off0 = (s.pts.front().x - a0.x) * sv.x + (s.pts.front().y - a0.y) * sv.y;
                    a0 = a0 + sv * off0;
                    step = sv * m.spacing;
                    s.used = true;
                    break;
                }
                if (step.x == 0 && step.y == 0 && count > 1) step = Vec3{-u.y, u.x, 0} * m.spacing;
            }
            if (step.x == 0 && step.y == 0 && count > 1) issue(false, "rbi.chk.nozone", {std::to_string(m.pos)});
            const int face = m.face ? m.face : bar.face ? bar.face : 1;
            std::vector<Vec3> shape;
            for (const auto& p : bar.pts) shape.push_back(p - bar.pts.front());
            double drawnLen = 0;
            for (std::size_t i = 0; i + 1 < shape.size(); ++i) drawnLen += shape[i].distanceTo(shape[i + 1]);
            const double L = m.length > 0 ? m.length : drawnLen;
            for (int k = sh.floorFrom; k <= std::max(sh.floorFrom, sh.floorTo); ++k) {
                const int level = sh.element == ElemKind::FoundationSlab ? 0 : k;
                double z0 = 0, z1 = 0;
                bool found = false;
                for (const auto& pr : bm.prisms)
                    if (pr.level == level && (pr.kind == sh.element || (sh.element == ElemKind::Slab && pr.kind == ElemKind::Roof) ||
                                              (sh.element == ElemKind::Roof && pr.kind == ElemKind::Slab))) {
                        z0 = pr.z0;
                        z1 = pr.z1;
                        found = true;
                        break;
                    }
                if (!found) {
                    issue(false, "rbi.chk.nolevel", {std::to_string(k)});
                    continue;
                }
                const double z = face == 2 ? z1 - cover - m.d / 2 : z0 + cover + m.d / 2;
                for (int i = 0; i < count; ++i) {
                    Bar b{sh.element, level, face == 2 ? BarRole::TopX : BarRole::BottomX, m.d, m.cls, {}, L};
                    const Vec3 o = a0 + step * static_cast<double>(i);
                    for (const auto& q : shape) b.pts.push_back({o.x + q.x, o.y + q.y, z});
                    R.bars.push_back(std::move(b));
                }
                auto& row = sched[{m.pos, level}];
                row.pos = m.pos;
                row.kind = sh.element;
                row.level = level;
                row.role = face == 2 ? BarRole::TopX : BarRole::BottomX;
                row.d = m.d;
                row.cls = m.cls;
                row.length = L;
                row.count += count;
                row.totalLength += count * L;
                row.mass += count * L * massPerM(m.d);
            }
            // spetsifikatsiya bilan soni
            for (const auto& r : spec)
                if (m.pos > 0 && r.pos == m.pos && r.count > 0 && r.count != count)
                    issue(true, "rbi.chk.count", {std::to_string(m.pos), std::to_string(count), std::to_string(r.count)});
        }
        for (const auto& s : segs)
            if (!s.used && geom && s.pts.size() >= 2) ++R.unlabeled;
        for (const auto& r : spec) {
            bool drawn = false;
            for (const auto& mk : marks) drawn = drawn || mk.m.pos == r.pos;
            if (!drawn && geom) issue(false, "rbi.chk.notdrawn", {std::to_string(r.pos)});
        }
        if (spec.empty()) issue(false, "rbi.chk.nospec", {sh.tag});
    }
    if (R.unlabeled > 0) issue(false, "rbi.chk.unlabeled", {std::to_string(R.unlabeled)});
    for (auto& [k, r] : sched) {
        R.schedule.push_back(r);
        R.massByDia[std::format("\xC3\x98{:.0f} {}", r.d * 1000, r.cls)] += r.mass;
        R.totalMass += r.mass;
    }
    if (R.specMass > 0 && R.totalMass > 0 && std::abs(R.totalMass - R.specMass) / R.specMass > 0.03)
        issue(false, "rbi.chk.mass", {std::format("{:.1f}", R.totalMass), std::format("{:.1f}", R.specMass)});
    return R;
}

} // namespace asiacad::bim
