#include <asiacad/i18n/json.h>
#include <asiacad/i18n/localizer.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace asiacad::i18n {

namespace {
bool readAliases(const json::Value* node, const char* section, std::vector<std::pair<std::string, std::vector<std::string>>>& out,
                 std::string* error) {
    if (!node) return true;  // bo'lim ixtiyoriy
    if (!node->isObject()) {
        if (error) *error = std::string(section) + " obyekt bo'lishi kerak";
        return false;
    }
    for (const auto& [canon, list] : node->obj()) {
        std::vector<std::string> aliases;
        if (list.isString()) {
            aliases.push_back(list.str());
        } else if (list.isArray()) {
            for (const auto& a : list.arr()) {
                if (!a.isString()) {
                    if (error) *error = std::string(section) + "." + canon + ": satrlar massivi kutilgan";
                    return false;
                }
                aliases.push_back(a.str());
            }
        } else {
            if (error) *error = std::string(section) + "." + canon + ": satr yoki massiv kutilgan";
            return false;
        }
        out.emplace_back(utf8::foldKey(canon), std::move(aliases));
    }
    return true;
}
} // namespace

bool Localizer::loadLanguage(std::string_view text, std::string* error) {
    json::Value root;
    try {
        root = json::parse(text);
    } catch (const json::ParseError& e) {
        if (error) *error = e.what();
        return false;
    }
    if (!root.isObject()) {
        if (error) *error = "Ildiz obyekt bo'lishi kerak";
        return false;
    }
    const json::Value* code = root.find("language");
    if (!code || !code->isString() || code->str().empty()) {
        if (error) *error = "\"language\" maydoni yo'q";
        return false;
    }
    Lang l;
    l.code = code->str();
    const json::Value* name = root.find("name");
    l.name = name && name->isString() ? name->str() : l.code;

    if (!readAliases(root.find("commands"), "commands", l.commands, error)) return false;
    if (!readAliases(root.find("keywords"), "keywords", l.keywords, error)) return false;

    if (const json::Value* msgs = root.find("messages")) {
        if (!msgs->isObject()) {
            if (error) *error = "messages obyekt bo'lishi kerak";
            return false;
        }
        for (const auto& [k, v] : msgs->obj()) {
            if (!v.isString()) {
                if (error) *error = "messages." + k + ": satr kutilgan";
                return false;
            }
            l.messages[k] = v.str();
        }
    }

    for (const auto& [canon, aliases] : l.commands) {
        canonCommands_.insert(canon);
        for (const auto& a : aliases) l.cmdIndex.emplace(utf8::foldKey(a), canon);
    }
    for (const auto& [canon, aliases] : l.keywords) {
        canonKeywords_.insert(canon);
        for (const auto& a : aliases) l.kwIndex.emplace(utf8::foldKey(a), canon);
    }

    // Buyruqlar tartibi: en.json tartibi ustun, qolganlari oxiriga
    if (l.code == "en") {
        std::vector<std::string> order;
        for (const auto& [canon, _] : l.commands) order.push_back(canon);
        for (const auto& c : commandOrder_)
            if (std::find(order.begin(), order.end(), c) == order.end()) order.push_back(c);
        commandOrder_ = std::move(order);
    } else {
        for (const auto& [canon, _] : l.commands)
            if (std::find(commandOrder_.begin(), commandOrder_.end(), canon) == commandOrder_.end())
                commandOrder_.push_back(canon);
    }

    const std::string c = l.code;
    langs_[c] = std::move(l);
    if (active_.empty()) active_ = c;
    return true;
}

std::size_t Localizer::loadDirectory(const std::filesystem::path& dir, std::vector<std::string>* errors) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        if (errors) errors->push_back("Papka topilmadi: " + dir.string());
        return 0;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    std::size_t n = 0;
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string err;
        if (loadLanguage(ss.str(), &err)) ++n;
        else if (errors) errors->push_back(f.filename().string() + ": " + err);
    }
    return n;
}

bool Localizer::setLanguage(std::string_view code) {
    if (!hasLanguage(code)) return false;
    active_ = std::string(code);
    return true;
}

std::string Localizer::languageName(std::string_view code) const {
    auto it = langs_.find(std::string(code));
    return it == langs_.end() ? std::string(code) : it->second.name;
}

std::vector<std::string> Localizer::languages() const {
    std::vector<std::string> v;
    for (const auto& [c, _] : langs_) v.push_back(c);
    return v;
}

const Localizer::Lang* Localizer::active() const {
    auto it = langs_.find(active_);
    return it == langs_.end() ? nullptr : &it->second;
}

std::string Localizer::resolve(std::string_view token, bool command) const {
    const std::string key = utf8::foldKey(token);
    if (key.empty()) return {};
    auto lookup = [&](const Lang& l) -> std::string {
        const auto& idx = command ? l.cmdIndex : l.kwIndex;
        auto it = idx.find(key);
        return it == idx.end() ? std::string{} : it->second;
    };
    if (const Lang* a = active())
        if (auto r = lookup(*a); !r.empty()) return r;
    if ((command ? canonCommands_ : canonKeywords_).count(key)) return key;
    if (auto it = langs_.find("en"); it != langs_.end())
        if (auto r = lookup(it->second); !r.empty()) return r;
    for (const auto& [code, l] : langs_)
        if (auto r = lookup(l); !r.empty()) return r;
    return {};
}

std::string Localizer::resolveCommand(std::string_view token) const { return resolve(token, true); }
std::string Localizer::resolveKeyword(std::string_view token) const { return resolve(token, false); }

std::string Localizer::displayName(const Lang* l, std::string_view canonical, bool command) {
    if (l && l->code != "en") {
        for (const auto& [c, aliases] : command ? l->commands : l->keywords)
            if (c == canonical && !aliases.empty()) return aliases.front();
    }
    return std::string(canonical);
}
std::string Localizer::commandName(std::string_view c) const { return displayName(active(), c, true); }
std::string Localizer::keywordName(std::string_view c) const { return displayName(active(), c, false); }

std::string Localizer::format(std::string_view p, const std::vector<std::string>& args) {
    std::string out;
    out.reserve(p.size() + 16);
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '{' && i + 2 < p.size() && p[i + 1] >= '0' && p[i + 1] <= '9' && p[i + 2] == '}') {
            const std::size_t idx = static_cast<std::size_t>(p[i + 1] - '0');
            if (idx < args.size()) out += args[idx];
            i += 2;
            continue;
        }
        out.push_back(p[i]);
    }
    return out;
}

bool Localizer::hasMessage(std::string_view key) const {
    const std::string k(key);
    if (const Lang* a = active(); a && a->messages.count(k)) return true;
    auto en = langs_.find("en");
    return en != langs_.end() && en->second.messages.count(k);
}

std::string Localizer::tr(std::string_view key, const std::vector<std::string>& args) const {
    const std::string k(key);
    if (const Lang* a = active())
        if (auto it = a->messages.find(k); it != a->messages.end()) return format(it->second, args);
    if (auto en = langs_.find("en"); en != langs_.end())
        if (auto it = en->second.messages.find(k); it != en->second.messages.end()) return format(it->second, args);
    std::string s = k;  // lug'at yo'q — kamida kalit va argumentlar ko'rinsin
    for (std::size_t i = 0; i < args.size(); ++i) s += (i == 0 ? ": " : ", ") + args[i];
    return s;
}

std::vector<std::string> Localizer::conflicts() const {
    std::vector<std::string> out;
    auto check = [&](bool command) {
        std::map<std::string, std::pair<std::string, std::string>> seen;  // fold -> (kanonik, til)
        for (const auto& c : command ? canonCommands_ : canonKeywords_) seen[c] = {c, "*"};
        for (const auto& [code, l] : langs_) {
            for (const auto& [fold, canon] : command ? l.cmdIndex : l.kwIndex) {
                auto [it, inserted] = seen.emplace(fold, std::make_pair(canon, code));
                if (!inserted && it->second.first != canon)
                    out.push_back(std::string(command ? "buyruq " : "kalit ") + fold + ": " + it->second.second + "=" +
                                  it->second.first + ", " + code + "=" + canon);
            }
        }
    };
    check(true);
    check(false);
    return out;
}

std::vector<std::string> Localizer::missingKeys(std::string_view code, std::string_view reference) const {
    std::vector<std::string> out;
    auto l = langs_.find(std::string(code));
    auto r = langs_.find(std::string(reference));
    if (l == langs_.end() || r == langs_.end()) return out;
    for (const auto& [k, _] : r->second.messages)
        if (!l->second.messages.count(k)) out.push_back("messages." + k);
    auto has = [](const AliasList& list, const std::string& c) {
        return std::any_of(list.begin(), list.end(), [&](const auto& p) { return p.first == c && !p.second.empty(); });
    };
    if (code != "en") {  // en uchun kanonik nomning o'zi yetarli
        for (const auto& [c, _] : r->second.commands)
            if (!has(l->second.commands, c)) out.push_back("commands." + c);
        for (const auto& [c, _] : r->second.keywords)
            if (!has(l->second.keywords, c)) out.push_back("keywords." + c);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace asiacad::i18n
