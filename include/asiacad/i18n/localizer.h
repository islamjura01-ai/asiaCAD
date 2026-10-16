#pragma once
// AsiaCAD — Localizer: buyruq taxalluslari, kalit so'zlar va xabarlarni JSON lug'atlardan yuklaydi.
//
// Lug'at fayli (i18n/<til>.json):
// {
//   "language": "uz", "name": "Oʻzbekcha",
//   "commands": { "LINE": ["CHIZIQ", "ЧИЗИҚ"], ... },   // birinchi taxallus — ko'rsatiladigan nom
//   "keywords": { "ALL": ["HAMMA"], ... },
//   "messages": { "msg.saved": "Saqlandi: {0}", ... }   // {0}..{9} — argumentlar
// }
//
// Buyruqni aniqlash tartibi: faol til -> kanonik (inglizcha) nom -> qolgan tillar.
// Ya'ni foydalanuvchi interfeys tilidan qat'i nazar istalgan tilda buyruq yoza oladi.
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace asiacad::i18n {

class Localizer {
public:
    // Papkadagi barcha *.json fayllarni yuklaydi. Yuklangan tillar sonini qaytaradi; xatolar errors ga.
    std::size_t loadDirectory(const std::filesystem::path& dir, std::vector<std::string>* errors = nullptr);
    // Bitta lug'at matnini yuklaydi (testlar va o'rnatilgan resurslar uchun)
    bool loadLanguage(std::string_view jsonText, std::string* error = nullptr);

    bool setLanguage(std::string_view code);
    const std::string& language() const { return active_; }
    std::string languageName(std::string_view code) const;
    std::vector<std::string> languages() const;
    bool hasLanguage(std::string_view code) const { return langs_.count(std::string(code)) != 0; }

    // Token -> kanonik nom ("LINE", "ALL"...). Topilmasa bo'sh satr.
    std::string resolveCommand(std::string_view token) const;
    std::string resolveKeyword(std::string_view token) const;
    bool isKeyword(std::string_view token, std::string_view canonical) const {
        return resolveKeyword(token) == canonical;
    }

    // Faol tildagi ko'rsatiladigan nom (yo'q bo'lsa — kanonik)
    std::string commandName(std::string_view canonical) const;
    std::string keywordName(std::string_view canonical) const;
    // Lug'atlarda e'lon qilingan barcha kanonik buyruqlar (en.json tartibida)
    const std::vector<std::string>& canonicalCommands() const { return commandOrder_; }

    // Xabar: faol til -> en -> kalitning o'zi
    std::string tr(std::string_view key, const std::vector<std::string>& args = {}) const;
    bool hasMessage(std::string_view key) const;

    // Sifat nazorati
    std::vector<std::string> conflicts() const;                 // turli tillarda bir xil taxallus, turli ma'no
    std::vector<std::string> missingKeys(std::string_view code, std::string_view reference = "en") const;

    static std::string format(std::string_view pattern, const std::vector<std::string>& args);

private:
    using AliasList = std::vector<std::pair<std::string, std::vector<std::string>>>;
    struct Lang {
        std::string code, name;
        AliasList commands, keywords;
        std::unordered_map<std::string, std::string> cmdIndex, kwIndex;  // foldKey(taxallus) -> kanonik
        std::unordered_map<std::string, std::string> messages;
    };
    const Lang* active() const;
    std::string resolve(std::string_view token, bool command) const;
    static std::string displayName(const Lang* l, std::string_view canonical, bool command);

    std::map<std::string, Lang> langs_;
    std::string active_;
    std::set<std::string> canonCommands_, canonKeywords_;
    std::vector<std::string> commandOrder_;
};

} // namespace asiacad::i18n
