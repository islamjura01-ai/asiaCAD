// AsiaCAD CLI — listlar (Revit "Sheets"): SHEET NEW/VIEW/STAMP/SIZE/RENAME/DEL, argumentsiz — ro'yxat.
// Har bir o'zgarish SheetsCommand orqali (undo/redo). Model birligi — metr, qog'oz — mm.
#include "interpreter.h"

#include <asiacad/core/error.h>
#include <asiacad/core/sheet.h>
#include <asiacad/i18n/utf8.h>

#include <format>
#include <set>

namespace asiacad::cli {

bool Interpreter::dispatchSheet(const std::string& cmd, const Args& a) {
    if (cmd != "SHEET") return false;
    auto usage = [&]() { return Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))}); };
    auto sheets = doc_.sheets();
    auto idx = [&](const std::string& name) -> std::size_t {
        for (std::size_t i = 0; i < sheets.size(); ++i)
            if (utf8::toUpper(sheets[i].name) == utf8::toUpper(name)) return i;
        throw Error("sheet.err.notfound", {name});
    };
    auto commit = [&](const char* what) { stack_.execute(std::make_unique<SheetsCommand>(sheets, std::string("SHEET ") + what)); };
    auto sizeArg = [&](const std::string& t, Sheet& s) {
        double w, h;
        if (!paperSize(t, true, w, h)) return false;
        s.size = utf8::toUpper(t);
        return true;
    };
    auto orientArg = [&](const std::string& t, Sheet& s) {
        if (loc_.isKeyword(t, "PORTRAIT")) { s.landscape = false; return true; }
        if (loc_.isKeyword(t, "LANDSCAPE")) { s.landscape = true; return true; }
        return false;
    };
    auto sizeText = [&](const Sheet& s) { return s.size + " " + loc_.keywordName(s.landscape ? "LANDSCAPE" : "PORTRAIT"); };

    if (a.empty()) {
        if (sheets.empty()) { say("sheet.msg.none"); return true; }
        for (std::size_t i = 0; i < sheets.size(); ++i) {
            const auto& s = sheets[i];
            std::string sc;
            for (const auto& v : s.views) sc += (sc.empty() ? "" : ", ") + scaleLabel(v.scale);
            say("sheet.msg.item", {std::to_string(i + 1), s.name, sizeText(s), std::to_string(s.views.size()), sc.empty() ? "—" : sc});
        }
        return true;
    }
    const std::string sub = loc_.resolveKeyword(a[0]);
    if (sub == "NEW") {
        if (a.size() < 2) throw usage();
        for (const auto& s : sheets)
            if (utf8::toUpper(s.name) == utf8::toUpper(a[1])) throw Error("sheet.err.exists", {a[1]});
        Sheet s;
        s.name = a[1];
        for (std::size_t i = 2; i < a.size(); ++i)
            if (!sizeArg(a[i], s) && !orientArg(a[i], s)) throw Error("sheet.err.size", {a[i]});
        s.stamp["STAGE"] = "P";
        if (!sheets.empty()) {  // shtampning umumiy maydonlari oldingi listdan
            for (const char* k : {"CODE", "OBJECT", "BUILDING", "STAGE", "ORG", "DEV", "CHK", "NCTRL", "APPR", "DATE"})
                if (auto it = sheets.back().stamp.find(k); it != sheets.back().stamp.end()) s.stamp[k] = it->second;
        }
        sheets.push_back(s);
        commit("NEW");
        say("sheet.msg.new", {s.name, sizeText(s), std::to_string(sheets.size())});
        return true;
    }
    if (sub == "SIZE") {
        if (a.size() < 3) throw usage();
        Sheet& s = sheets[idx(a[1])];
        for (std::size_t i = 2; i < a.size(); ++i)
            if (!sizeArg(a[i], s) && !orientArg(a[i], s)) throw Error("sheet.err.size", {a[i]});
        // ko'rinishlar yangi maydonga qayta joylanadi
        auto views = std::move(s.views);
        s.views.clear();
        for (auto v : views) {
            if (!placeView(s, v)) throw Error("sheet.err.nospace", {s.name, scaleLabel(v.scale)});
            s.views.push_back(v);
        }
        commit("SIZE");
        say("sheet.msg.size", {s.name, sizeText(s)});
        return true;
    }
    if (sub == "RENAME") {
        if (a.size() < 3) throw usage();
        const std::size_t i = idx(a[1]);
        for (std::size_t j = 0; j < sheets.size(); ++j)
            if (j != i && utf8::toUpper(sheets[j].name) == utf8::toUpper(a[2])) throw Error("sheet.err.exists", {a[2]});
        const std::string old = sheets[i].name;
        sheets[i].name = a[2];
        commit("RENAME");
        say("sheet.msg.rename", {old, a[2]});
        return true;
    }
    if (sub == "DEL") {
        if (a.size() < 2) throw usage();
        const std::size_t i = idx(a[1]);
        if (a.size() >= 3) {  // n-ko'rinishni o'chirish
            const int n = static_cast<int>(parseNumber(a[2]));
            if (n < 1 || n > static_cast<int>(sheets[i].views.size())) throw usage();
            sheets[i].views.erase(sheets[i].views.begin() + (n - 1));
            commit("DEL");
            say("sheet.msg.delview", {sheets[i].name, std::to_string(n)});
            return true;
        }
        const std::string name = sheets[i].name;
        sheets.erase(sheets.begin() + static_cast<std::ptrdiff_t>(i));
        commit("DEL");
        say("sheet.msg.del", {name});
        return true;
    }
    if (sub == "STAMP") {
        if (a.size() < 3) throw usage();
        Sheet& s = sheets[idx(a[1])];
        static const std::set<std::string> keys{"CODE", "OBJECT", "BUILDING", "TITLE", "STAGE", "ORG", "DEV", "CHK", "NCTRL", "APPR", "DATE"};
        bool all = false;
        for (std::size_t i = 2; i < a.size(); ++i) {
            if (loc_.isKeyword(a[i], "ALL")) { all = true; continue; }
            const auto eq = a[i].find('=');
            if (eq == std::string::npos) throw usage();
            const std::string k = utf8::toUpper(a[i].substr(0, eq));
            if (!keys.count(k)) throw Error("sheet.err.key", {k, "CODE OBJECT BUILDING TITLE STAGE ORG DEV CHK NCTRL APPR DATE"});
            s.stamp[k] = a[i].substr(eq + 1);
        }
        if (all)  // TITLE dan boshqa maydonlar barcha listlarga
            for (auto& o : sheets)
                for (const auto& [k, v] : s.stamp)
                    if (k != "TITLE") o.stamp[k] = v;
        commit("STAMP");
        say("sheet.msg.stamp", {s.name});
        return true;
    }
    if (sub == "VIEW") {
        if (a.size() < 4) throw usage();
        Sheet& s = sheets[idx(a[1])];
        const Vec3 p1 = parsePoint(a[2]), p2 = parsePoint(a[3]);
        double scale = 0;
        std::string title;
        for (std::size_t i = 4; i < a.size(); ++i) {
            const auto eq = a[i].find('=');
            const std::string k = eq == std::string::npos ? std::string() : utf8::toUpper(a[i].substr(0, eq));
            std::string v = eq == std::string::npos ? std::string() : a[i].substr(eq + 1);
            if (k == "SCALE") {
                if (v.rfind("1:", 0) == 0) v = v.substr(2);
                scale = parseNumber(v);
                if (scale <= 0) throw usage();
            } else if (k == "TITLE") title = v;
            else throw usage();
        }
        SheetView view;
        if (!makeView(s, p1, p2, scale, title, view)) throw Error("sheet.err.nospace", {s.name, scale > 0 ? scaleLabel(scale) : "—"});
        s.views.push_back(view);
        commit("VIEW");
        say("sheet.msg.view", {s.name, std::to_string(s.views.size()), scaleLabel(view.scale), std::format("{:.0f}", view.w), std::format("{:.0f}", view.h)});
        return true;
    }
    throw usage();
}

} // namespace asiacad::cli
