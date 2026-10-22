// AsiaCAD CLI — yer yuzasi va yer ishlari: TIN, gorizontallar, bo'ylama profil, ko'ndalang kesimlar (hajm bilan),
// kartogramma, yuzalar orasidagi hajm.
#include "interpreter.h"

#include <asiacad/civil/road.h>
#include <asiacad/civil/terrain.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/utf8.h>
#include <asiacad/io/xlsx.h>

#include <format>
#include <map>
#include <set>

namespace asiacad::cli {

namespace {
double nz(double v) { return std::abs(v) < 0.05 ? 0.0 : v; }   // "-0.0" bo'lmasin
}  // namespace

bool Interpreter::dispatchTerrain(const std::string& cmd, const Args& a) {
    static const std::set<std::string> mine{"TIN", "CONTOUR", "PROFILE", "XSECTIONS", "CARTOGRAM", "VOLUME"};
    if (!mine.count(cmd)) return false;
    auto usage = [&]() { return Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))}); };
    auto tr = [this](const std::string& k) { return loc_.tr(k); };
    // kalit=qiymat va pozitsion argumentlar
    std::map<std::string, std::string> kv;
    std::vector<std::string> pos;
    for (const auto& t : a) {
        const auto eq = t.find('=');
        if (eq != std::string::npos && eq > 0) kv[utf8::toUpper(t.substr(0, eq))] = t.substr(eq + 1);
        else if (utf8::toUpper(t) == "BALANCE" || utf8::toUpper(t) == "NODRAW" || utf8::toUpper(t) == "NOMARKS") kv[utf8::toUpper(t)] = "1";
        else pos.push_back(t);
    }
    auto num = [&](const char* k, double d) { return kv.count(k) ? parseNumber(kv[k]) : d; };
    auto has = [&](const char* k) { return kv.count(k) > 0; };
    auto point = [&](const char* k) { return parsePoint(kv[k]); };
    auto boundary = [&]() -> std::vector<Vec3> {
        if (!has("BOUND")) return {};
        for (EntityId id : parseSelection(kv["BOUND"]))
            if (const IEntity* e = doc_.find(id)) {
                auto v = civil::pathOf(*e);
                if (v.size() >= 3) return v;
            }
        throw Error("terr.err.bound", {});
    };
    const auto bound = boundary();
    auto tinFrom = [&](const std::string& sel) {
        const auto pts = civil::terrainPoints(doc_, parseSelection(sel));
        if (pts.size() < 3) throw Error("terr.err.points", {sel, std::to_string(pts.size())});
        const bool clip = (cmd == "TIN" || cmd == "CONTOUR") && !bound.empty();   // hajmda chegara — faqat maydon uchun
        auto t = civil::Tin::build(pts, clip ? &bound : nullptr, num("MAXEDGE", 0));
        if (t.empty()) throw Error("terr.err.points", {sel, std::to_string(pts.size())});
        return t;
    };
    auto pathArg = [&]() {
        if (pos.empty()) throw usage();
        for (EntityId id : parseSelection(pos[0]))
            if (const IEntity* e = doc_.find(id)) {
                auto v = civil::pathOf(*e);
                if (v.size() >= 2) return v;
            }
        throw Error("terr.err.path", {});
    };
    // loyiha: FG=tanlov (TIN) | Z=belgi [IX=‰ IY=‰ BASE=x,y] | BALANCE
    civil::Tin fgTin;
    auto design = [&]() {
        civil::Design d;
        if (has("FG")) {
            const auto pts = civil::terrainPoints(doc_, parseSelection(kv["FG"]));
            if (pts.size() < 3) throw Error("terr.err.points", {kv["FG"], std::to_string(pts.size())});
            fgTin = civil::Tin::build(pts, nullptr, num("MAXEDGE", 0));
            d.tin = &fgTin;
        } else if (has("Z") || has("IX") || has("IY")) {
            d.plane = true;
            d.z0 = num("Z", 0);
            d.ix = num("IX", 0) / 1000;
            d.iy = num("IY", 0) / 1000;
            if (has("BASE")) d.base = point("BASE");
        }
        return d;
    };
    auto say2 = [&](const std::string& path) {
        if (!path.empty()) say("dev.msg.saved", {path});
    };
    BBox ext = doc_.extents(true);
    if (!ext.valid()) ext.expand({0, 0, 0});

    if (cmd == "TIN" || cmd == "CONTOUR") {
        if (pos.empty()) throw usage();
        const auto t = tinFrom(pos[0]);
        double lo = 1e300, hi = -1e300;
        for (const auto& p : t.points()) { lo = std::min(lo, p.z); hi = std::max(hi, p.z); }
        civil::Entities es;
        if (cmd == "TIN") {
            for (const auto& tr3 : t.triangles()) {
                auto pl = std::make_unique<PolylineEntity>(std::vector<Vec3>{t.points()[tr3[0]], t.points()[tr3[1]], t.points()[tr3[2]]}, true);
                pl->setLayer("YUZA_TIN");
                pl->setColor(8);
                es.push_back(std::move(pl));
            }
            addLayered(cmd, std::move(es));
            say("terr.msg.tin", {std::to_string(t.points().size()), std::to_string(t.triangles().size()), std::format("{:.1f}", t.area()),
                                 std::format("{:.3f}", lo), std::format("{:.3f}", hi)});
        } else {
            const double step = num("STEP", 0.5), major = num("MAJOR", step * 5);
            if (step <= 0) throw usage();
            const double th = num("TH", std::max(0.2, step));
            int n = 0;
            for (double z = std::ceil(lo / step - 1e-9) * step; z <= hi + 1e-9; z += step) {
                const bool isMajor = major > 0 && std::abs(z / major - std::round(z / major)) < 1e-6;
                for (auto& c : t.contours(z)) {
                    const Vec3 mid = c[c.size() / 2];
                    const Vec3 dir = c[std::min(c.size() - 1, c.size() / 2 + 1)] - c[c.size() / 2 > 0 ? c.size() / 2 - 1 : 0];
                    auto pl = std::make_unique<PolylineEntity>(std::move(c), false);
                    pl->setLayer(isMajor ? "GORIZONTAL_ASOSIY" : "GORIZONTAL");
                    pl->setColor(isMajor ? 32 : 34);
                    es.push_back(std::move(pl));
                    ++n;
                    if (isMajor) {
                        double rot = std::atan2(dir.y, dir.x);
                        if (rot > kPi / 2) rot -= kPi;
                        if (rot < -kPi / 2) rot += kPi;
                        auto tx = std::make_unique<TextEntity>(mid, th, std::format("{:.2f}", z), rot, TextHAlign::Center, TextVAlign::Middle);
                        tx->setLayer("GORIZONTAL_YOZUV");
                        es.push_back(std::move(tx));
                    }
                }
            }
            addLayered(cmd, std::move(es));
            say("terr.msg.contour", {std::to_string(n), std::format("{:g}", step), std::format("{:.2f}", lo), std::format("{:.2f}", hi)});
        }
        return true;
    }
    if (!has("EG")) throw Error("terr.err.noeg", {});
    const auto eg = tinFrom(kv["EG"]);
    if (cmd == "PROFILE") {
        const auto path = pathArg();
        civil::ProfileSpec p;
        p.step = num("STEP", 20);
        p.hScale = num("H", 1000);
        p.vScale = num("V", 100);
        p.pk0 = num("PK0", 0);
        p.textH = num("TH", 2.5);
        if (has("DATUM")) p.datum = num("DATUM", 0);
        if (p.step <= 0 || p.hScale <= 0 || p.vScale <= 0) throw usage();
        const double k = p.hScale / 1000;
        p.at = has("AT") ? point("AT") : Vec3{ext.min.x, ext.min.y - 160 * k, 0};
        const auto fg = design();
        const auto pr = civil::sampleProfile(path, eg, fg, p);
        addLayered(cmd, civil::drawProfile(pr, p, tr));
        double L = 0, zmin = 1e300, zmax = -1e300;
        int st = 0;
        for (const auto& q : pr) {
            L = q.s;
            st += q.station;
            if (q.hasG) { zmin = std::min(zmin, q.zg); zmax = std::max(zmax, q.zg); }
        }
        say("terr.msg.profile", {std::format("{:.2f}", L), std::to_string(st), std::format("{:.2f}", zmin), std::format("{:.2f}", zmax),
                                 std::format("{:g}", p.hScale), std::format("{:g}", p.vScale)});
        if (has("XLSX")) { xlsx::save(civil::profileWorkbook(pr, p, tr), kv["XLSX"]); say2(kv["XLSX"]); }
    } else if (cmd == "XSECTIONS") {
        const auto path = pathArg();
        civil::SectionSpec p;
        p.step = num("STEP", 20);
        p.left = num("L", 20);
        p.right = num("R", 20);
        p.sample = num("SAMPLE", 0.5);
        p.scale = num("SCALE", 200);
        p.cols = static_cast<int>(num("COLS", 4));
        p.pk0 = num("PK0", 0);
        p.textH = num("TH", 2.5);
        p.at = has("AT") ? point("AT") : Vec3{ext.max.x + 20 * p.scale / 1000, ext.max.y, 0};
        const auto fg = design();
        if (!fg.valid()) throw Error("terr.err.nofg", {});
        const auto xs = civil::sampleSections(path, eg, fg, p);
        const auto v = civil::sectionVolumes(xs);
        civil::Entities es = civil::drawSections(xs, v, p, tr);
        if (!has("NOMARKS"))
            for (auto& e : civil::drawSectionMarks(xs, p)) es.push_back(std::move(e));
        addLayered(cmd, std::move(es));
        say("terr.msg.sections", {std::to_string(xs.size()), std::format("{:g}", p.step), std::format("{:.1f}", v.fill), std::format("{:.1f}", v.cut),
                                  std::format("{:+.1f}", nz(v.fill - v.cut))});
        if (has("XLSX")) { xlsx::save(civil::sectionsWorkbook(xs, v, p, tr), kv["XLSX"]); say2(kv["XLSX"]); }
    } else if (cmd == "CARTOGRAM") {
        civil::CartoSpec s;
        s.cell = num("CELL", 20);
        s.boundary = bound;
        s.balance = has("BALANCE");
        s.textH = num("TH", 0);
        if (has("ORIGIN")) s.origin = point("ORIGIN");
        const auto fg = design();
        if (!fg.valid() && !s.balance) throw Error("terr.err.nofg", {});
        const auto r = civil::cartogram(eg, fg, s);
        if (!has("NODRAW")) addLayered(cmd, civil::drawCartogram(r, s, tr));
        int nc = 0;
        for (const auto& c : r.cells) nc += c.ok;
        say("terr.msg.carto", {std::to_string(nc), std::format("{:g}", s.cell), std::format("{:.1f}", r.area), std::format("{:.1f}", r.fill),
                               std::format("{:.1f}", -r.cut), std::format("{:+.1f}", nz(r.fill + r.cut))});
        if (!std::isnan(r.balanceZ)) say("terr.msg.h0", {std::format("{:.3f}", r.balanceZ)});
        if (has("XLSX")) { xlsx::save(civil::cartogramWorkbook(r, tr), kv["XLSX"]); say2(kv["XLSX"]); }
    } else {   // VOLUME
        const auto fg = design();
        if (!fg.valid() && !has("BALANCE")) throw Error("terr.err.nofg", {});
        const auto v = civil::volumeBetween(eg, fg, bound, num("CELL", 0), has("BALANCE"));
        say("terr.msg.volume", {std::format("{:.1f}", v.area), std::format("{:.1f}", v.fill), std::format("{:.1f}", -v.cut), std::format("{:+.1f}", nz(v.fill + v.cut)),
                                std::format("{:.2f}", v.cell)});
        if (!std::isnan(v.balanceZ)) say("terr.msg.h0", {std::format("{:.3f}", v.balanceZ)});
    }
    return true;
}

} // namespace asiacad::cli
