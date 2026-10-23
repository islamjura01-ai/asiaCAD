// AsiaCAD CLI — dinamik bog'lanishlar (Civil 3D "dynamic model"): TIN, CONTOUR, PROFILE, XSECTIONS, CARTOGRAM natijasi
// manbasiga bog'lanadi (nuqtalar, o'q, chegara, loyiha yuzasi). Manba o'zgarsa (ko'chirish, tutqich, o'chirish, yangi nuqta
// qatlamga qo'shilishi) natija o'sha buyruq bilan qayta quriladi va foydalanuvchi amali bilan bitta undo qadamiga birlashadi.
#include "interpreter.h"

#include <asiacad/civil/terrain.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/utf8.h>
#include <asiacad/io/acad_file.h>

#include <algorithm>
#include <format>
#include <set>

namespace asiacad::cli {

namespace {
bool isFlag(const std::string& t) {
    const std::string u = utf8::toUpper(t);
    return u == "BALANCE" || u == "NODRAW" || u == "NOMARKS" || u == "STATIC";
}
std::string quoteTok(const std::string& t) { return t.find(' ') != std::string::npos ? "\"" + t + "\"" : t; }
}  // namespace

std::vector<EntityId> Interpreter::notLinkOutput(std::vector<EntityId> ids, const std::vector<EntityId>* extra) const {
    std::set<EntityId> out;
    for (const auto& l : doc_.links()) out.insert(l.outputs.begin(), l.outputs.end());
    if (extra) out.insert(extra->begin(), extra->end());
    if (out.empty()) return ids;
    ids.erase(std::remove_if(ids.begin(), ids.end(), [&](EntityId id) { return out.count(id) > 0; }), ids.end());
    return ids;
}

std::vector<EntityId> Interpreter::linkSources(const std::string& line, const std::vector<EntityId>* extra) const {
    const auto toks = tokenizeLine(line);
    std::vector<EntityId> ids;
    bool posTaken = false;
    auto addSel = [&](const std::string& sel) {
        try {
            for (EntityId id : parseSelection(sel)) ids.push_back(id);
        } catch (...) {
        }
    };
    for (std::size_t i = 1; i < toks.size(); ++i) {
        const auto& t = toks[i];
        const auto eq = t.find('=');
        if (eq != std::string::npos && eq > 0) {
            const std::string k = utf8::toUpper(t.substr(0, eq));
            if (k == "EG" || k == "FG" || k == "BOUND") addSel(t.substr(eq + 1));
        } else if (!posTaken && !isFlag(t)) {
            addSel(t);
            posTaken = true;
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return notLinkOutput(std::move(ids), extra);
}

std::string Interpreter::fingerprint(const std::vector<EntityId>& ids) const {
    std::uint64_t h = 1469598103934665603ULL;  // FNV-1a
    auto mix = [&](const std::string& s) {
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    };
    for (EntityId id : ids)
        if (const IEntity* e = doc_.find(id)) {
            mix(std::to_string(id));
            mix(entityJson(*e));
        }
    return std::format("{}:{:016x}", ids.size(), h);
}

std::string Interpreter::linkFingerprint(const std::string& line, const std::vector<EntityId>* extra) const {
    const auto toks = tokenizeLine(line);
    if (toks.empty()) return {};
    const bool posSurface = toks[0] == "TIN" || toks[0] == "CONTOUR";
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&](const std::string& s) {
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    };
    auto sel = [&](const std::string& t) {
        try { return notLinkOutput(parseSelection(t), extra); } catch (...) { return std::vector<EntityId>{}; }
    };
    auto surface = [&](const std::string& t) {  // faqat yuzaga ta'sir qiladigan nuqtalar (2D chiziq qo'shilsa — o'zgarish emas)
        const auto pts = civil::terrainPoints(doc_, sel(t));
        mix(std::to_string(pts.size()) + "|");
        for (const auto& p : pts) mix(std::format("{:.6f},{:.6f},{:.6f};", p.x, p.y, p.z));
    };
    auto geom = [&](const std::string& t) { mix(fingerprint(sel(t))); };
    bool posTaken = false;
    for (std::size_t i = 1; i < toks.size(); ++i) {
        const auto& t = toks[i];
        const auto eq = t.find('=');
        if (eq != std::string::npos && eq > 0) {
            const std::string k = utf8::toUpper(t.substr(0, eq));
            if (k == "EG" || k == "FG") { mix(k); surface(t.substr(eq + 1)); }
            else if (k == "BOUND") { mix(k); geom(t.substr(eq + 1)); }
        } else if (!posTaken && !isFlag(t)) {
            posTaken = true;
            if (posSurface) surface(t); else geom(t);
        }
    }
    return std::format("{:016x}", h);
}

void Interpreter::makeLink(const std::string& canon, const Args& a, const std::vector<std::string>& extra, const std::vector<EntityId>& prevLast) {
    std::string line = canon;
    bool posTaken = false;
    auto selTok = [&](const std::string& t) {  // LAST — aniq ID lar (keyin o'zgaradi)
        if (loc_.resolveKeyword(t) != "LAST") return t;
        std::string s;
        for (EntityId id : prevLast) s += (s.empty() ? "" : ",") + std::to_string(id);
        return s.empty() ? std::string("0") : s;
    };
    for (const auto& t : a) {
        const auto eq = t.find('=');
        const std::string k = eq == std::string::npos ? std::string() : utf8::toUpper(t.substr(0, eq));
        if (k == "XLSX" || utf8::toUpper(t) == "STATIC") continue;  // Excel qayta yozilmaydi
        std::string tok = t;
        if (k == "EG" || k == "FG" || k == "BOUND") tok = t.substr(0, eq + 1) + selTok(t.substr(eq + 1));
        else if (k.empty() && !posTaken && !isFlag(t)) { tok = selTok(t); posTaken = true; }
        line += " " + quoteTok(tok);
    }
    for (const auto& x : extra) line += " " + quoteTok(x);
    auto links = doc_.links();
    DynLink l;
    for (const auto& o : links) l.id = std::max(l.id, o.id);
    ++l.id;
    l.line = line;
    l.outputs = last_;
    l.fp = linkFingerprint(line, &l.outputs);
    links.push_back(l);
    stack_.execute(std::make_unique<LinksCommand>(links, canon));
    stack_.squash(2);
    say("link.msg.created", {std::to_string(l.id), std::to_string(linkSources(line, &l.outputs).size())});
}

int Interpreter::updateLinks(bool force, int only, bool merge) {
    int updated = 0;
    for (std::size_t i = 0; i < doc_.links().size(); ++i) {
        const DynLink L = doc_.links()[i];
        if (only && L.id != only) continue;
        if (!L.on && !force) continue;
        if (!force && linkFingerprint(L.line) == L.fp) continue;
        int k = 0;
        std::vector<EntityId> old;
        for (EntityId id : L.outputs)
            if (doc_.contains(id)) old.push_back(id);
        if (!old.empty()) { stack_.execute(std::make_unique<DeleteCommand>(old)); ++k; }
        const auto toks = tokenizeLine(L.line);
        const Args args(toks.begin() + 1, toks.end());
        const std::size_t p0 = stack_.pushCount();
        std::string err;
        regen_ = quiet_ = true;
        last_.clear();
        try {
            dispatch(toks.at(0), args);
        } catch (const Error& e) {
            err = loc_.tr(e.key(), e.args());
        } catch (const std::exception& e) {
            err = e.what();
        }
        regen_ = quiet_ = false;
        auto links = doc_.links();
        DynLink& M = links[i];
        if (err.empty()) {
            if (stack_.pushCount() > p0) ++k;
            M.outputs = last_;
            M.on = true;
        } else {  // manba yaroqsiz (masalan, nuqtalar o'chirildi): eski natija qoladi, bog'lanish to'xtatiladi
            if (!old.empty()) { stack_.undo(); --k; }
            M.on = false;
        }
        M.fp = linkFingerprint(M.line, &M.outputs);
        stack_.execute(std::make_unique<LinksCommand>(links, toks.at(0)));
        ++k;
        stack_.squash(static_cast<std::size_t>(k) + (merge ? 1 : 0));
        merge = true;
        if (err.empty()) {
            ++updated;
            say("link.msg.updated", {std::to_string(L.id), loc_.commandName(toks.at(0)), std::to_string(M.outputs.size())});
        } else {
            say("link.msg.broken", {std::to_string(L.id), err});
        }
    }
    return updated;
}

bool Interpreter::dispatchLinks(const std::string& cmd, const Args& a) {
    if (cmd != "LINKS") return false;
    auto usage = [&]() { return Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))}); };
    if (a.empty()) {
        if (doc_.links().empty()) { say("link.msg.none"); return true; }
        for (const auto& l : doc_.links()) {
            const auto toks = tokenizeLine(l.line);
            std::size_t alive = 0;
            for (EntityId id : l.outputs) alive += doc_.contains(id);
            say("link.msg.item", {std::to_string(l.id), loc_.keywordName(l.on ? "ON" : "OFF"), loc_.commandName(toks.at(0)),
                                  std::to_string(linkSources(l.line).size()), std::to_string(alive)});
        }
        return true;
    }
    const std::string sub = loc_.resolveKeyword(a[0]);
    const int n = a.size() > 1 ? static_cast<int>(parseNumber(a[1])) : 0;
    auto find = [&]() -> std::size_t {
        for (std::size_t i = 0; i < doc_.links().size(); ++i)
            if (doc_.links()[i].id == n) return i;
        throw Error("link.err.notfound", {std::to_string(n)});
    };
    if (sub == "UPDATE") {
        if (n) find();
        const int u = updateLinks(true, n, false);
        if (u == 0) say("link.msg.none");
        return true;
    }
    if (sub == "ON" || sub == "OFF" || sub == "DEL") {
        if (a.size() < 2) throw usage();
        auto links = doc_.links();
        const std::size_t i = find();
        if (sub == "DEL") links.erase(links.begin() + static_cast<std::ptrdiff_t>(i));
        else links[i].on = sub == "ON";
        stack_.execute(std::make_unique<LinksCommand>(links, "LINKS"));
        say(sub == "DEL" ? "link.msg.del" : "link.msg.state", {std::to_string(n), loc_.keywordName(sub == "DEL" ? "OFF" : sub)});
        if (sub == "ON") updateLinks(false, n, true);
        return true;
    }
    throw usage();
}

} // namespace asiacad::cli
