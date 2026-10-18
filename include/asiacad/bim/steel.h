#pragma once
// AsiaCAD — metall konstruksiyalar (КМ): prokat profillari katalogi, kesim konturi, element (o'q chizig'i) va
// metall prokat vedomosti (profil bo'yicha uzunlik va massa).
// Element chizmada — "KM_<profil>" qatlamidagi chiziq/poliliniya (masalan "KM_I20", "KM_[16", "KM_L75x6", "KM_TR159x6").
// Qiymatlar GOST 8239-89 (qo'shtavr), GOST 8240-97 (shveller, У seriya), GOST 8509-93 (teng tomonli burchak) bo'yicha
// eslab kiritilgan — tasdiqlanmagan; truba (GOST 10704) va kvadrat truba (GOST 30245) — formuladan.
#include <asiacad/core/document.h>

#include <optional>
#include <string>
#include <vector>

namespace asiacad::bim {

enum class SteelFamily { IBeam, Channel, Angle, Pipe, SquareTube };

struct SteelProfile {
    std::string name;          // "I20", "[16", "L75x6", "TR159x6", "KV100x5"
    SteelFamily family = SteelFamily::IBeam;
    double h = 0, b = 0, s = 0, t = 0;  // mm: balandlik, kenglik (tokcha), devor, tokcha qalinligi (truba: h=D, t=devor)
    double A = 0;              // kesim yuzasi, sm²
    double mass = 0;           // kg/m
    double Ix = 0, Wx = 0;     // sm⁴, sm³ (0 — ma'lum emas)
    const char* standard = "";
};

const char* steelFamilyKey(SteelFamily f);    // "IBEAM", ... (i18n: km.fam.<key>)
const std::vector<SteelProfile>& steelCatalog();
// Nom bo'yicha (katalog; truba/kv. truba — "TR159x6", "KV100x5" formuladan). Kirill "Д"/"Двутавр", "Ш"/"[" ham tushuniladi
std::optional<SteelProfile> findSteel(const std::string& name);

// Kesim konturlari (metrda, markaz — og'irlik markazi emas, kesim gabariti o'rtasi), ichki konturlar — teshik
std::vector<std::vector<Vec3>> steelSection(const SteelProfile& p);

struct SteelMember {
    EntityId id = kInvalidId;
    SteelProfile profile;
    Vec3 a, b;                 // o'q uchlari (3D)
    double length() const { return a.distanceTo(b); }
};
// "KM_<profil>" qatlamidagi chiziqlar va poliliniyalar (har segment — element)
std::vector<SteelMember> readSteel(const Document& doc, std::vector<std::string>* unknownLayers = nullptr);
std::string steelLayer(const SteelProfile& p);   // "KM_I20"

struct SteelSpecRow {
    std::string profile, standard;
    SteelFamily family;
    int count = 0;
    double length = 0;         // m
    double mass = 0;           // t
};
struct SteelSpec {
    std::vector<SteelSpecRow> rows;   // oila, so'ng profil balandligi bo'yicha
    double total = 0;                 // t
    double reserve = 0;               // t (qirqim va payvandga zahira, foiz bilan)
};
SteelSpec steelSpec(const std::vector<SteelMember>& ms, double reservePct = 2.0);

} // namespace asiacad::bim
