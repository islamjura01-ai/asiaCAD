#pragma once
// AsiaCAD — Koridor: yig'ma (assembly) + qismlar (subassembly) + o'q + loyiha profili + yer -> 3D model va hajmlar.
// Civil 3D Corridor g'oyasi, lekin soddaroq va aniqroq:
//  * Qismlar oddiy parametrlar bilan (kenglik, nishablik %, qalinlik, qiyalik 1:m); har qism nuqtalar (kod bilan),
//    ustki chiziq (TOP), asos chizig'i (DATUM) va material shakllarini (ASFALT, ASOS, BETON, ...) quradi.
//  * Qiyalik (daylight) yer sathini aniq kesishma bilan topadi (bisection), kesma/to'kma o'zi tanlanadi.
//  * Hajmlar: o'rtacha maydon (Civil 3D usuli) va aniq — prizmoidal (o'rta kesim bilan, Simpson) + egri chiziqda
//    Pappus tuzatmasi (og'irlik markazi ekssentrisiteti). Ikkalasi ham ko'rsatiladi.
//  * Barcha balandliklar absolyut: loyiha belgilari va yer — chizmadagi yozuvlardan; yer yo'q bo'lsa — xato.
//  * Kanal/lotok markaziy qismlari Manning bo'yicha suv sathini ham chizadi (Q berilsa).
#include <asiacad/civil/waterway.h>
#include <asiacad/core/document.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace asiacad::civil {

enum class SubKind {
    Lane, Shoulder, Curb, Sidewalk, Median, GenericLink,          // yo'l
    Daylight, DaylightDitch, DaylightBench,                         // qiyaliklar (yergacha)
    Ditch, ChannelHalf, FlumeHalf, PipeTrenchHalf,                  // kanal, lotok, transheya (markaziylar — o'qdan)
    RetainingWall
};
const char* subKey(SubKind k);                   // "LANE", ... (i18n: cor.sub.<key>)
bool subFromKey(const std::string& s, SubKind& out);
bool subIsCenter(SubKind k);                     // o'qdan boshlanadi (median, kanal, lotok, transheya)
bool subIsDaylight(SubKind k);                   // yergacha — undan keyin qism qo'shilmaydi

struct SubParam {
    std::string key;                             // i18n: cor.p.<key>
    double value = 0;
};
struct Subassembly {
    SubKind kind = SubKind::Lane;
    std::vector<SubParam> p;
    double get(const std::string& k, double def = 0) const;
    void set(const std::string& k, double v);
};
// Palitra: tur bo'yicha standart parametrlar va bo'lim (Civil 3D tool palette yorlig'i kabi)
Subassembly defaultSub(SubKind k);
const char* subCategory(SubKind k);              // "BASIC", "LANES", "SHOULDERS", "MEDIANS", "CURBS", "DAYLIGHT", "CHANNELS", "TRENCH", "WALLS"
std::vector<SubKind> subKinds();

struct Assembly {
    std::string name;
    std::string group;                           // "ROAD", "CANAL", "TRENCH" (i18n: cor.grp.<key>)
    std::vector<Subassembly> right, left;        // o'qdan tashqariga tartibda
    bool symmetric = true;                       // chap = o'ngning ko'zgu nusxasi
    double q = 0;                                // suv yo'li uchun sarf (m³/s) — 0: suv sathi hisoblanmaydi
    double n = 0.015;                            // g'adir-budurlik
    const std::vector<Subassembly>& side(int s) const { return s > 0 || symmetric ? right : left; }
};
std::vector<Assembly> builtinAssemblies();

// ---- kesim
struct SecPoint {
    double o = 0, z = 0;                         // offset (o'ngga +), balandlik (absolyut)
    std::string code;                            // "CROWN", "ETW", "ETS", "DAYLIGHT", "CH_BOTTOM", ...
    int side = 1;
};
struct SecShape {
    std::string material;                        // "ASPHALT", "BASE", "SUBBASE", "SHOULDER", "CONCRETE", "SIDEWALK", "BEDDING", "PIPE", "WATER"
    std::vector<std::pair<double, double>> poly;
    double area() const;
    double centroidO() const;
};
struct Section {
    double s = 0, x = 0, y = 0, dx = 1, dy = 0;
    double design = 0, ground = 0;
    int region = 0;
    std::vector<SecPoint> pts;                   // chapdan o'ngga ustki chiziq (TOP)
    std::vector<std::pair<double, double>> datum;  // asos chizig'i (chapdan o'ngga)
    std::vector<std::pair<double, double>> groundLine;
    std::vector<SecShape> shapes;
    double cut = 0, fill = 0;                    // m²
    double cutCentroid = 0, fillCentroid = 0;    // offset
    bool daylightL = true, daylightR = true;     // qiyalik yerga yetdimi
    double water = 0;                            // suv sathi (0 — yo'q)
};

struct CorRegion {
    double s0 = 0, s1 = 1e18;
    int assembly = 0;
};

struct CorridorParams {
    std::vector<Assembly> assemblies;            // ishlatiladigan yig'malar
    std::vector<CorRegion> regions;              // bo'sh — butun uzunlik 0-yig'ma
    double step = 20;                            // piket oralig'i, m
    double ground = 0;                           // yer nuqtalari yo'q bo'lsa (xato)
    double maxDaylight = 150;                    // qiyalik qidirish masofasi, m
    bool designIsInvert = false;                 // profil — kanal/quvur tubi (aks holda — yo'l o'qi ustki belgisi)
    std::map<std::string, WwLayer> layers;       // bo'sh — avto (yo'l va suv yo'li qatlamlari)
};
CorridorParams defaultCorridorParams();

struct CorIssue {
    bool error = false;
    std::string key;
    std::vector<std::string> args;
    double s = 0;
};

struct Corridor {
    std::vector<Section> sec;
    std::vector<std::array<double, 3>> groundPts;
    int groundPoints = 0, designPoints = 0;
    double length = 0;
    // hajmlar (m³): material -> (o'rtacha maydon, aniq)
    std::map<std::string, std::pair<double, double>> volumes;
    double cutAvg = 0, fillAvg = 0, cutExact = 0, fillExact = 0;
    std::vector<double> vertexS;
    std::vector<CorIssue> issues;
    int errors() const;
};

// O'q, yer va loyiha belgilarini o'qib, koridor quradi
Corridor buildCorridor(const Document& doc, const CorridorParams& p);
// Qo'lda tuzatilgan loyiha belgilari (sec[i].design) yoki yig'ma parametrlari o'zgargach — kesimlar va hajmlar qayta
void recomputeCorridor(Corridor& c, const CorridorParams& p);
// Bitta kesimni qurish (yig'ma oldindan ko'rishi uchun ham): ground(o) — offsetdagi yer belgisi
Section buildSection(const Assembly& a, double design, const std::function<double(double)>& ground, double maxDaylight,
                     double waterSlope = 0);

std::string assemblyToJson(const Assembly& a);
Assembly assemblyFromJson(const std::string& text);
std::string corridorToJson(const Corridor& c, const CorridorParams& p);
void corridorFromJson(const std::string& text, Corridor& c, CorridorParams& p);

// Chizmaga (absolyut): kod bo'yicha 3D chiziqlar (KOR_<KOD>), har piketda kesim (KOR_KESIM), material chegaralari
std::vector<std::unique_ptr<IEntity>> drawCorridor3D(const Corridor& c);

} // namespace asiacad::civil
