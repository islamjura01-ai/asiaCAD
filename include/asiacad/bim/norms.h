#pragma once
// AsiaCAD — ShNQ (shaharsozlik normalari va qoidalari) bo'yicha normativ tekshiruv.
//
// * Qoidalar — tahrirlanadigan jadval (JSON): toifa + shart -> o'lchanadigan kattalik [min, max] oralig'ida bo'lishi kerak.
//   Har qoidada norma, band, daraja (xato / ogohlantirish) va "tasdiqlangan" belgisi (qiymat normaning matni bilan
//   solishtirilganmi) bor. Chegaralar — ifoda: "max(15mm, d)", "if(lean, 35mm, 70mm)".
// * O'lchamlar IFC geometriyasidan: devor qalinligi (gorizontal kesim — "polosa eni"), plita qalinligi (2V / gorizontal
//   yuzalar), ustun kesimi, zina marshi (atributlar, Pset yoki pog'onalar geometriyasi), eshik / deraza, to'siq, xona.
// * Armatura himoya qatlami: har sterjen bo'ylab nuqtalarda beton birlashmasining tashqi sirtigacha eng qisqa masofa
//   (ichki tutashuv yuzalari — masalan ustun usti va plita osti — hisobga olinmaydi) minus radius.
//   Joylashgan element (host): nuqtalarning ko'pi qaysi beton elementda bo'lsa.
// * "Monolit bino" modeli (prizmalar, zinapoya, armatura) ham IfcModel ko'rinishiga o'tkazilib, shu qoidalar bilan tekshiriladi.
#include <asiacad/bim/ifc.h>

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace asiacad::bim {

struct BuildingModel;
struct BuildingParams;
struct RebarModel;

struct NormRule {
    std::string id;
    bool enabled = true;
    std::string norm, clause;                 // "ShNQ 2.03.01-24", "376a"
    std::vector<std::string> cats;            // ifcCategory kalitlari ("REBAR", "SLAB"...); bo'sh — hammasi
    bool concreteOnly = false;                // faqat beton elementlar (material nomi bo'yicha)
    std::string when;                         // shart ifodasi (bo'sh — hammasi)
    std::string param;                        // kattalik ifodasi: cover, d, thickness, width...
    std::string min, max;                     // chegaralar (ifoda; bo'sh — yo'q)
    int severity = 2;                         // 2 — xato, 1 — ogohlantirish
    bool verified = false;                    // qiymat normaning matni bilan solishtirilgan
    std::string unit = "mm";                  // ko'rsatish: "mm", "m", "" (o'lchamsiz)
    std::map<std::string, std::string> title; // til ("uz", "ru", "en"...) -> nomi
    std::string note;
    std::string titleFor(const std::string& lang) const;
};

std::vector<NormRule> defaultNormRules();
std::string normRulesToJson(const std::vector<NormRule>& rules);
// Xato bo'lsa — std::runtime_error (JSON sintaksisi yoki tuzilma)
std::vector<NormRule> normRulesFromJson(const std::string& text);

// ---- ifodalar: sonlar (15mm, 0.2, 1.5m), satrlar ("SLAB"), o'zgaruvchilar, + - * / < <= > >= == != && || ! ?:,
//      funksiyalar: min max abs sqrt round floor ceil if(c,a,b) prop("Pset.X") has("Pset.X")
using NormValue = std::variant<double, std::string>;
using NormVars = std::map<std::string, NormValue>;
using NormProps = std::vector<std::pair<std::string, std::string>>;
// Natija: qiymat; o'zgaruvchi topilmasa — std::nullopt (qoida bu elementga qo'llanmaydi). Sintaksis xatosi — err to'ldiriladi.
std::optional<NormValue> normEval(const std::string& expr, const NormVars& vars, const NormProps* props = nullptr, std::string* err = nullptr);
bool normSyntaxOk(const std::string& expr, std::string* err = nullptr);

// Tekshiriladigan obyekt (element yoki elementdagi sterjenlar guruhi)
struct NormSubject {
    int element = -1;                         // IfcModel::elements indeksi
    std::string cat;                          // ifcCategory
    NormVars vars;
    Vec3 at;                                  // belgi joyi (eng yomon nuqta yoki element usti)
    std::string where;                        // izoh: "3 sterjen", "z=+2.150"
};

struct NormViolation {
    int subject = -1, element = -1, rule = -1;
    int severity = 2;
    double value = 0, limit = 0;
    bool belowMin = true;                     // true — min dan kichik, false — max dan katta
    Vec3 at;
    std::string where;
};

struct NormReport {
    std::vector<NormViolation> items;         // daraja, keyin element bo'yicha tartiblangan
    std::vector<int> applicable, failed;      // qoida bo'yicha: tekshirilgan / buzilgan obyektlar
    int subjects = 0, errors = 0, warnings = 0;
    int rebarSamples = 0;                     // armatura tahlilidagi nuqtalar
    std::vector<std::string> ruleErrors;      // "id: xato matni" — ifodasi buzilgan qoidalar (o'tkazib yuboriladi)
    double seconds = 0;
    // element -> eng og'ir daraja (0 — buzilish yo'q)
    std::vector<int> worstByElement(std::size_t elements) const;
};

std::vector<NormSubject> ifcNormSubjects(const IfcModel& m, int* rebarSamples = nullptr);
NormReport normCheck(const std::vector<NormSubject>& subjects, const std::vector<NormRule>& rules, const IfcModel* m = nullptr);
NormReport normCheck(const IfcModel& m, const std::vector<NormRule>& rules);   // o'lchash + tekshirish

// "Monolit bino" -> IfcModel: prizmalar (beton), zinapoya (har qavat — marsh: pog'ona h, b, eni), armatura (sterjenlar;
// tayanch va bog'lovchilar emas). prismsOf / barOf: element -> prizmalar / sterjen indekslari (GUI ranglash uchun).
// Nomlar bo'sh — GUI tarjima qiladi (objectType = kindKey, storey = qavat).
IfcModel buildingAsIfc(const BuildingModel& bm, const BuildingParams& bp, const RebarModel* rebar,
                       std::vector<std::vector<int>>* prismsOf = nullptr, std::vector<int>* barOf = nullptr);

// Qiymat birlik bilan: 0.0185, "mm" -> "18.5 mm"; 1.05, "m" -> "1.050 m"
std::string normFormat(double v, const std::string& unit);
// Material nomi bo'yicha beton (Beton, бетон, concrete, B25, железобетон...)
bool isConcreteMaterial(const std::string& material);

} // namespace asiacad::bim
