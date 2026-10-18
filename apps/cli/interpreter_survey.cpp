// AsiaCAD CLI — geodezik (ijroiy) tahlil: nuqtalar TXT/CSV/XLSX import-eksport, og'ish tahlili (reja/fasad, balandlik,
// vertikallik, qiya o'q — anker/qoziq, loyiha nuqtasi), chizmaga belgilar va Excel/CSV hisobot.
#include "interpreter.h"

#include <asiacad/civil/survey.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/utf8.h>
#include <asiacad/io/xlsx.h>

#include <format>
#include <fstream>
#include <sstream>

namespace asiacad::cli {

namespace {
std::string ext(const std::string& path) {
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) return {};
    return utf8::toUpper(path.substr(dot + 1));
}
std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error("acad.err.open", {path});
    std::stringstream b;
    b << f.rdbuf();
    return b.str();
}
void writeAll(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw Error("dev.err.write", {path});
    f << data;
}
}  // namespace

bool Interpreter::dispatchSurvey(const std::string& cmd, const Args& a) {
    auto usage = [&]() { return Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))}); };
    auto up = [](const std::string& s) { return utf8::toUpper(s); };
    auto tr = [this](const std::string& k) { return loc_.tr(k); };

    if (cmd == "PTIMPORT") {
        // PTIMPORT fayl [XY] [H=h] — fayl: N X Y Z [kod]; odatda geodezik (X shimol), XY — chizma tartibi
        if (a.empty()) throw usage();
        civil::PointFileFormat f;
        double h = 0.25;
        for (std::size_t k = 1; k < a.size(); ++k) {
            const std::string u = up(a[k]);
            if (u == "XY") f.geodetic = false;
            else if (u.rfind("H=", 0) == 0) h = parseNumber(a[k].substr(2));
            else throw usage();
        }
        std::string text;
        if (ext(a[0]) == "XLSX") {   // Excel: birinchi varaq qatorlari
            const auto wb = xlsx::load(a[0]);
            if (wb.sheets.empty()) throw Error("dev.err.nopoints", {a[0]});
            const auto& sh = wb.sheets.front();
            for (std::size_t r = 0; r < sh.rows.size(); ++r) {
                for (std::size_t c = 0; c < sh.rows[r].size(); ++c) text += (c ? "\t" : "") + sh.str(r, c);
                text += "\n";
            }
        } else text = readAll(a[0]);
        std::vector<std::string> skipped;
        const auto pts = civil::readPoints(text, f, &skipped);
        if (pts.empty()) throw Error("dev.err.nopoints", {a[0]});
        auto es = civil::pointEntities(pts, h);
        addLayered(cmd, std::move(es));
        BBox b;
        for (const auto& p : pts) b.expand(p.p);
        say("dev.msg.imported", {std::to_string(pts.size()), std::format("{:.3f}", b.min.z), std::format("{:.3f}", b.max.z),
                                 f.geodetic ? loc_.tr("dev.order.geo") : loc_.tr("dev.order.xy")});
        if (!skipped.empty()) out_ << "  " << loc_.tr("dev.msg.skipped", {std::to_string(skipped.size()), skipped.front()}) << '\n';
    } else if (cmd == "PTEXPORT") {
        // PTEXPORT sel fayl [XY] — TXT (bo'sh joy), CSV (vergul), XLSX
        need(a, 2, cmd);
        civil::PointFileFormat f;
        for (std::size_t k = 2; k < a.size(); ++k) {
            if (up(a[k]) == "XY") f.geodetic = false;
            else throw usage();
        }
        const auto pts = civil::collectPoints(doc_, parseSelection(a[0]));
        if (pts.empty()) throw Error("dev.err.nopoints", {a[0]});
        const std::string e = ext(a[1]);
        if (e == "XLSX") {
            xlsx::Workbook wb;
            auto& s = wb.add(loc_.tr("dev.sheet.points").substr(0, 31));
            s.add({xlsx::Cell::s(tr("dev.col.name"), 1), xlsx::Cell::s(tr(f.geodetic ? "dev.col.xn" : "dev.col.x"), 1),
                   xlsx::Cell::s(tr(f.geodetic ? "dev.col.ye" : "dev.col.y"), 1), xlsx::Cell::s(tr("dev.col.z"), 1), xlsx::Cell::s(tr("dev.col.code"), 1)});
            s.widths = {10, 14, 14, 10, 16};
            for (const auto& p : pts)
                s.add({xlsx::Cell::s(p.name), xlsx::Cell::n(f.geodetic ? p.p.y : p.p.x, 3), xlsx::Cell::n(f.geodetic ? p.p.x : p.p.y, 3),
                       xlsx::Cell::n(p.p.z, 3), xlsx::Cell::s(p.code)});
            xlsx::save(wb, a[1]);
        } else {
            if (e == "CSV") f.delim = ',';
            writeAll(a[1], civil::writePoints(pts, f));
        }
        say("dev.msg.exported", {std::to_string(pts.size()), a[1]});
    } else if (cmd == "DEVIATION") {
        // DEVIATION tur nuqtalar loyiha|- [TOL=mm] [MAX=m] [R=m] [Z=m] [K=n] [H=h] [A=m] [XY] [NODRAW] [XLSX=f] [CSV=f]
        need(a, 3, cmd);
        civil::DevParams p;
        if (!civil::devKindFrom(a[0], p.kind)) throw Error("dev.err.kind", {a[0]});
        if (p.kind == civil::DevKind::Vertical) p.cluster = 0.5;
        if (p.kind == civil::DevKind::Point) p.maxDist = 0.5;
        if (p.kind == civil::DevKind::Height) p.maxDist = 2.0;
        bool geo = true, draw = true;
        std::string xl, csv;
        for (std::size_t k = 3; k < a.size(); ++k) {
            const std::string u = up(a[k]);
            const auto eq = a[k].find('=');
            const std::string key = eq == std::string::npos ? u : u.substr(0, eq), val = eq == std::string::npos ? "" : a[k].substr(eq + 1);
            if (key == "XY") geo = false;
            else if (key == "NODRAW") draw = false;
            else if (key == "TOL") p.tol = parseNumber(val) / 1000;
            else if (key == "MAX") p.maxDist = parseNumber(val);
            else if (key == "R") p.cluster = parseNumber(val);
            else if (key == "Z") { p.designZ = parseNumber(val); p.useDesignZ = true; }
            else if (key == "K") p.tolRatio = parseNumber(val);
            else if (key == "H") p.textH = parseNumber(val);
            else if (key == "A") p.arrowLen = parseNumber(val);
            else if (key == "XLSX") xl = val;
            else if (key == "CSV") csv = val;
            else throw usage();
        }
        if (p.tol <= 0 || p.maxDist <= 0 || p.cluster <= 0 || p.textH <= 0) throw usage();
        const auto pts = civil::collectPoints(doc_, parseSelection(a[1]));
        if (pts.empty()) throw Error("dev.err.nopoints", {a[1]});
        std::vector<civil::DesignRef> refs;
        if (a[2] != "-") refs = civil::collectRefs(doc_, parseSelection(a[2]));
        if (refs.empty() && p.kind != civil::DevKind::Vertical && !(p.kind == civil::DevKind::Height && p.useDesignZ)) throw Error("dev.err.noref", {});
        const auto R = civil::analyze(pts, refs, p);
        if (draw && !R.rows.empty()) addLayered(cmd, civil::annotate(R, p));
        const std::string kind = loc_.tr(std::string("dev.kind.") + civil::devKindKey(p.kind));
        say("dev.msg.result", {kind, std::to_string(R.rows.size()), std::to_string(R.nOk), std::to_string(R.rows.size() - R.nOk),
                               std::format("{:.1f}", R.maxAbs * 1000), std::format("{:.1f}", R.mean * 1000), std::format("{:.1f}", R.rms * 1000),
                               std::format("{:g}", p.tol * 1000)});
        int shown = 0;
        for (const auto& r : R.rows)
            if (!r.ok && shown++ < 25)
                out_ << "  " << loc_.tr("sewer.lvl.warning") << ": "
                     << loc_.tr("dev.msg.bad", {r.name, std::format("{:+.1f}", r.d * 1000), std::format("{:.1f}", r.allow * 1000), r.ref}) << '\n';
        for (const auto& el : R.elements)
            if (p.kind == civil::DevKind::Axis)
                out_ << "  " << loc_.tr("dev.msg.axis", {el.ref, std::format("{:.2f}", el.measIncl), std::format("{:.2f}", el.designIncl),
                                                         std::format("{:.2f}", el.measAz), std::format("{:.2f}", el.designAz), std::format("{:.1f}", el.maxDev * 1000)})
                     << '\n';
        if (!R.unmatched.empty()) {
            std::string u;
            for (std::size_t i = 0; i < R.unmatched.size() && i < 20; ++i) u += (i ? ", " : "") + R.unmatched[i];
            if (R.unmatched.size() > 20) u += ", …";
            out_ << "  " << loc_.tr("dev.msg.unmatched", {std::to_string(R.unmatched.size()), u}) << '\n';
        }
        if (!xl.empty()) {
            xlsx::save(civil::toWorkbook(R, p, tr, geo), xl);
            say("dev.msg.saved", {xl});
        }
        if (!csv.empty()) {
            writeAll(csv, civil::toCsv(R, tr, geo));
            say("dev.msg.saved", {csv});
        }
    } else if (cmd == "DEVCLEAR") {
        std::vector<EntityId> ids;
        for (const char* l : {"DEV_STRELKA", "DEV_QIYMAT", "DEV_BP"})
            for (EntityId id : doc_.idsOnLayer(l)) ids.push_back(id);
        if (ids.empty()) {
            say("dev.msg.cleared", {"0"});
            return true;
        }
        stack_.execute(std::make_unique<DeleteCommand>(ids));
        say("dev.msg.cleared", {std::to_string(ids.size())});
    } else {
        return false;
    }
    return true;
}

} // namespace asiacad::cli
