// AsiaCAD CLI — ijro syomkasi (as-built): syomka nuqtalarini loyiha bilan solishtirish.
//   ASBUILT <tarmoq> pts [REF=INV|TOP] [TOL=sm] [TOLP=sm] [MAXD=m] [XLSX=f] [NODRAW]
//     kanalizatsiya tarmog'i: nuqta eng yaqin quvur o'qiga proyeksiyalanadi; loyiha — lotok (INV) yoki quvur usti (TOP)
//   ASBUILT ROAD pts …  — yo'l: piket va o'qdan masofa bo'yicha loyiha yuzasi (ko'ndalang kesimlar orasida chiziqli)
// Natija: har nuqtada Δh (sm) yozuvi (yashil — me'yorda, qizil — oshgan), plandagi siljish, jadval (Excel), statistika.
#include "interpreter.h"

#include <asiacad/civil/road.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/utf8.h>
#include <asiacad/io/xlsx.h>

#include <cmath>
#include <format>
#include <set>

namespace asiacad::cli {

namespace {
struct AbRow {
    std::string name, elem;
    Vec3 p;
    double design = 0, dz = 0, off = 0, pk = 0;
    bool ok = true;
};
// z ni ko'ndalang kesimdan (offset, z) chiziqli; tashqarida — false
bool sectionZ(const std::vector<std::pair<double, double>>& sec, double o, double& z) {
    for (std::size_t k = 0; k + 1 < sec.size(); ++k) {
        const double a = sec[k].first, b = sec[k + 1].first;
        if ((o >= a - 1e-9 && o <= b + 1e-9) || (o <= a + 1e-9 && o >= b - 1e-9)) {
            const double t = std::abs(b - a) < 1e-12 ? 0 : (o - a) / (b - a);
            z = sec[k].second + (sec[k + 1].second - sec[k].second) * t;
            return true;
        }
    }
    return false;
}
}  // namespace

bool Interpreter::dispatchAsBuilt(const std::string& cmd, const Args& a) {
    if (cmd != "ASBUILT") return false;
    auto usage = [&]() { return Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))}); };
    if (a.size() < 2) throw usage();
    const std::string obj = a[0];
    std::vector<EntityId> sel = parseSelection(a[1]);
    bool top = false, draw = true;
    double tol = 5, tolP = 10, maxD = 2.0;
    std::string xl;
    for (std::size_t i = 2; i < a.size(); ++i) {
        const std::string u = utf8::toUpper(a[i]);
        if (u == "NODRAW") { draw = false; continue; }
        const auto eq = a[i].find('=');
        if (eq == std::string::npos) throw usage();
        const std::string k = utf8::toUpper(a[i].substr(0, eq)), v = a[i].substr(eq + 1);
        if (k == "REF") top = utf8::toUpper(v) == "TOP";
        else if (k == "TOL") tol = parseNumber(v);
        else if (k == "TOLP") tolP = parseNumber(v);
        else if (k == "MAXD") maxD = parseNumber(v);
        else if (k == "XLSX") xl = v;
        else throw usage();
    }
    // syomka nuqtalari va ularning nomlari (PTIMPORT: SYOMKA_NOMI qatlamidagi yozuv)
    std::vector<std::pair<Vec3, EntityId>> pts;
    for (EntityId id : sel)
        if (const auto* p = dynamic_cast<const PointEntity*>(doc_.find(id))) pts.push_back({p->position(), id});
    if (pts.empty()) throw Error("ab.err.points", {a[1]});
    std::vector<const TextEntity*> names;
    for (EntityId id : doc_.idsOnLayer("SYOMKA_NOMI"))
        if (const auto* t = dynamic_cast<const TextEntity*>(doc_.find(id))) names.push_back(t);
    auto nameOf = [&](const Vec3& p, EntityId id) {
        const TextEntity* best = nullptr;
        double bd = 1.0;
        for (const auto* t : names)
            if (const double d = std::hypot(t->insertion().x - p.x, t->insertion().y - p.y); d < bd) { bd = d; best = t; }
        return best ? best->text() : "#" + std::to_string(id);
    };

    std::vector<AbRow> rows;
    std::string objLabel;
    if (utf8::toUpper(obj) == "ROAD" || loc_.resolveCommand(obj) == "ROAD") {
        const std::string js = doc_.dictGet("road:main");
        if (js.empty()) throw Error("obj.err.none", {"ROAD"});
        civil::RoadModel m;
        auto p = civil::defaultRoadParams();
        civil::roadFromJson(js, m, p);
        objLabel = loc_.tr("obj.kind.road");
        for (const auto& [q, id] : pts) {
            // eng yaqin o'q bo'lagi (piketlar orasida)
            double bestD = 1e300, bs = 0, bo = 0;
            std::size_t bi = 0;
            for (std::size_t i = 0; i + 1 < m.st.size(); ++i) {
                const auto &A = m.st[i], &B = m.st[i + 1];
                const double vx = B.x - A.x, vy = B.y - A.y, L2 = vx * vx + vy * vy;
                if (L2 < 1e-12) continue;
                const double t = std::clamp(((q.x - A.x) * vx + (q.y - A.y) * vy) / L2, 0.0, 1.0);
                const double px = A.x + vx * t, py = A.y + vy * t;
                const double d = std::hypot(q.x - px, q.y - py);
                if (d < bestD) {
                    bestD = d;
                    bi = i;
                    bs = t;
                    bo = ((q.x - A.x) * -vy + (q.y - A.y) * vx) / std::sqrt(L2);  // chap +
                }
            }
            if (bestD > 1e299) continue;
            const auto &A = m.st[bi], &B = m.st[bi + 1];
            double za = 0, zb = 0;
            if (!sectionZ(A.section, bo, za) || !sectionZ(B.section, bo, zb)) continue;  // yo'l kesimidan tashqarida
            AbRow r;
            r.p = q;
            r.name = nameOf(q, id);
            r.pk = A.s + (B.s - A.s) * bs;
            r.elem = civil::pk(r.pk);
            r.design = za + (zb - za) * bs;
            r.dz = q.z - r.design;
            r.off = bo;
            r.ok = std::abs(r.dz) * 100 <= tol + 1e-9;
            rows.push_back(r);
        }
    } else {
        const std::string js = doc_.dictGet("sewer:" + obj);
        if (js.empty()) throw Error("pipe.err.net", {obj});
        const auto net = civil::fromJson(js);
        const auto res = civil::compute(net);
        objLabel = obj;
        std::vector<double> ch(net.manholes.size(), 0);
        for (std::size_t i = 1; i < net.manholes.size() && i - 1 < res.segments.size(); ++i) ch[i] = ch[i - 1] + res.segments[i - 1].length;
        for (const auto& [q, id] : pts) {
            double bestD = 1e300, bt = 0, bo = 0;
            int bi = -1;
            for (std::size_t i = 0; i + 1 < net.manholes.size() && i < res.segments.size(); ++i) {
                if (!res.segments[i].spec) continue;
                const auto &A = net.manholes[i], &B = net.manholes[i + 1];
                const double vx = B.x - A.x, vy = B.y - A.y, L2 = vx * vx + vy * vy;
                if (L2 < 1e-12) continue;
                const double t = std::clamp(((q.x - A.x) * vx + (q.y - A.y) * vy) / L2, 0.0, 1.0);
                const double d = std::hypot(q.x - (A.x + vx * t), q.y - (A.y + vy * t));
                if (d < bestD) {
                    bestD = d;
                    bi = static_cast<int>(i);
                    bt = t;
                    bo = ((q.x - A.x) * -vy + (q.y - A.y) * vx) / std::sqrt(L2);
                }
            }
            if (bi < 0 || bestD > maxD) continue;
            const auto& s = res.segments[static_cast<std::size_t>(bi)];
            AbRow r;
            r.p = q;
            r.name = nameOf(q, id);
            r.elem = net.manholes[static_cast<std::size_t>(bi)].name + "→" + net.manholes[static_cast<std::size_t>(bi) + 1].name;
            r.pk = ch[static_cast<std::size_t>(bi)] + s.length * bt;
            r.design = s.invUp + (s.invDown - s.invUp) * bt + (top ? s.spec->od / 1000.0 : 0.0);
            r.dz = q.z - r.design;
            r.off = bo;
            r.ok = std::abs(r.dz) * 100 <= tol + 1e-9 && std::abs(r.off) * 100 <= tolP + 1e-9;
            rows.push_back(r);
        }
    }
    if (rows.empty()) throw Error("ab.err.none", {objLabel});
    int okN = 0;
    double maxAbs = 0, sum = 0, sum2 = 0, maxOff = 0;
    for (const auto& r : rows) {
        okN += r.ok;
        maxAbs = std::max(maxAbs, std::abs(r.dz));
        maxOff = std::max(maxOff, std::abs(r.off));
        sum += r.dz;
        sum2 += r.dz * r.dz;
    }
    const double n = static_cast<double>(rows.size());
    say("ab.msg.summary", {objLabel, std::to_string(rows.size()), std::to_string(okN), std::format("{:g}", tol), std::format("{:+.1f}", sum / n * 100),
                           std::format("{:.1f}", std::sqrt(sum2 / n) * 100), std::format("{:.1f}", maxAbs * 100), std::format("{:.1f}", maxOff * 100)});
    int shown = 0;
    for (const auto& r : rows)
        if (!r.ok && shown++ < 20)
            say("ab.msg.bad", {r.name, r.elem, std::format("{:.3f}", r.p.z), std::format("{:.3f}", r.design), std::format("{:+.1f}", r.dz * 100),
                               std::format("{:+.1f}", r.off * 100)});
    if (!xl.empty()) {
        xlsx::Workbook wb;
        auto& sh = wb.add(loc_.tr("ab.xl.sheet"));
        sh.add({xlsx::Cell::s(loc_.tr("xl.c.name"), 1), xlsx::Cell::s("X", 1), xlsx::Cell::s("Y", 1), xlsx::Cell::s(loc_.tr("ab.xl.meas"), 1),
                xlsx::Cell::s(loc_.tr("ab.xl.design"), 1), xlsx::Cell::s(loc_.tr("ab.xl.dz"), 1), xlsx::Cell::s(loc_.tr("ab.xl.off"), 1),
                xlsx::Cell::s(loc_.tr("ab.xl.elem"), 1), xlsx::Cell::s(loc_.tr("ab.xl.pk"), 1), xlsx::Cell::s(loc_.tr("xl.c.status"), 1)});
        sh.widths = {10, 13, 13, 11, 11, 10, 11, 18, 10, 14};
        for (const auto& r : rows) {
            const int row = static_cast<int>(sh.rows.size()) + 1;
            sh.add({xlsx::Cell::s(r.name), xlsx::Cell::n(r.p.x, 3), xlsx::Cell::n(r.p.y, 3), xlsx::Cell::n(r.p.z, 3), xlsx::Cell::n(r.design, 3),
                    xlsx::Cell::f(std::format("(D{0}-E{0})*100", row), r.dz * 100, 2), xlsx::Cell::n(r.off * 100, 2), xlsx::Cell::s(r.elem),
                    xlsx::Cell::n(r.pk, 2), xlsx::Cell::s(loc_.tr(r.ok ? "ab.xl.ok" : "ab.xl.bad"), r.ok ? 0 : 6)});
        }
        const int last = static_cast<int>(sh.rows.size());
        sh.add({});
        sh.add({xlsx::Cell::s(loc_.tr("ab.xl.mean"), 4), {}, {}, {}, {}, xlsx::Cell::f(std::format("AVERAGE(F2:F{})", last), sum / n * 100, 4)});
        sh.add({xlsx::Cell::s(loc_.tr("ab.xl.rms"), 4), {}, {}, {}, {}, xlsx::Cell::f(std::format("SQRT(SUMSQ(F2:F{0})/COUNT(F2:F{0}))", last), std::sqrt(sum2 / n) * 100, 4)});
        sh.add({xlsx::Cell::s(loc_.tr("ab.xl.max"), 4), {}, {}, {}, {}, xlsx::Cell::f(std::format("MAX(ABS(MAX(F2:F{0})),ABS(MIN(F2:F{0})))", last), maxAbs * 100, 4)});
        xlsx::save(wb, xl);
        say("dev.msg.saved", {xl});
    }
    if (draw) {
        auto comp = std::make_unique<CompositeCommand>(cmd);
        std::vector<EntityId> old;
        doc_.forEach([&](const IEntity& e) { if (e.xget("asbuilt") == obj) old.push_back(e.id()); });
        if (!old.empty()) comp->add(std::make_unique<DeleteCommand>(old));
        if (!doc_.layer("IJRO_SYOMKA")) comp->add(std::make_unique<SetLayerCommand>(Layer{"IJRO_SYOMKA"}));
        const double h = 0.6;
        std::vector<AddEntityCommand*> adds;
        for (const auto& r : rows) {
            auto t = std::make_unique<TextEntity>(Vec3{r.p.x + 0.15, r.p.y - 0.15, r.p.z}, h, std::format("{:+.1f}", r.dz * 100), 0.0, TextHAlign::Left,
                                                  TextVAlign::Top);
            t->setLayer("IJRO_SYOMKA");
            t->setColor(r.ok ? 3 : 1);
            t->xset("asbuilt", obj);
            auto c = std::make_unique<AddEntityCommand>(std::move(t), cmd);
            adds.push_back(c.get());
            comp->add(std::move(c));
        }
        stack_.execute(std::move(comp));
        last_.clear();
        for (auto* c : adds) last_.push_back(c->entityId());
    }
    return true;
}

} // namespace asiacad::cli
