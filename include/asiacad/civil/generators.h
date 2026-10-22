#pragma once
// AsiaCAD — "0 dan chizish" generatorlari. Har biri chizmaga AsiaCAD'ning o'z importi taniydigan obyektlarni yozadi,
// shuning uchun bitta yo'l: 0 dan chizilgan ham, boshqa dasturdan import qilingan ham — bir xil o'qiladi va tuzatiladi.
//  * GRID    — bino: o'qlar to'ri (har xil qadamlar "6x5, 4.5"), ustunlar, tashqi devorlar, plita konturi
//  * ALIGN   — yo'l / kanal / koridor o'qi: burilish nuqtalari (X, Y, R — yoy) + loyiha profili (PK, belgi)
//  * UTILNET — yer osti tarmog'i: quduqlar (nom, X, Y, qopqoq, tub) va quvurlar (tur, Ø) — NetCAD uslubidagi yozuvlar
//  * CITY    — shahar mavzesi: ko'chalar to'ri (o'q + chetlar), kvartallar, binolar (qavatlar), 3D massiv, ko'rsatkichlar
// Barcha balandliklar absolyut; CITY bino asosini chizmadagi yer belgilaridan oladi (bo'lsa).
#include <asiacad/core/document.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace asiacad::civil {

using Entities = std::vector<std::unique_ptr<IEntity>>;

// "6x5, 4.5, 3*2" -> {6,6,6,6,6,4.5,3,3}; xato bo'lsa bo'sh
std::vector<double> parseSpacing(const std::string& s);

// ---- bino o'qlari to'ri
struct GridSpec {
    std::vector<double> dx, dy;        // o'qlar orasidagi masofalar, m
    double x0 = 0, y0 = 0;             // "1"/"A" o'qlari kesishmasi
    double colB = 0.4, colH = 0.4;     // ustun kesimi, m
    bool walls = true;                 // tashqi perimetr bo'ylab devor (ustunlar orasida)
    bool infill = true;                // true — to'siq devor (gazoblok, derazali: TOSIQ_DEVOR), false — monolit beton (DEVOR)
    double wallT = 0.30;
    bool slab = true;                  // plita konturi (tashqi o'qlardan overhang ga)
    double overhang = 0.2;
    int floors = 5;                    // BINO dialogiga uzatiladi (qavatlar chizmada emas, parametrda)
    double floorH = 3.0;
    bool core = true;                  // zinapoya katagi (2+ qavat) va lift shaxtasi (5+ qavat) — o'rta oraliqda, 1-qatorda
};
// O'q nomlari: X bo'yicha 1, 2, 3 ...; Y bo'yicha А, Б, В ... (GOST 21.101: Ё З Й О Х Ц Ч Щ Ъ Ы Ь ishlatilmaydi)
std::string gridLetter(int i);
Entities generateGrid(const GridSpec& g);
std::string gridToJson(const GridSpec& g);
GridSpec gridFromJson(const std::string& text);

// ---- o'q va profil
struct AlignPI {
    double x = 0, y = 0, r = 0;        // r — burilish yoyi radiusi (0 — o'tkir burchak)
};
struct AlignPVI {
    double s = 0, z = 0;               // piket (m) va loyiha belgisi (absolyut)
};
struct AlignSpec {
    std::vector<AlignPI> pi;
    std::vector<AlignPVI> pvi;
    std::string axisLayer = "O'Q";
    std::string designLayer = "LOYIHA";
    double labelOffset = 2.0;          // yozuv o'qdan chapga, m
};
Entities generateAlignment(const AlignSpec& a, std::vector<std::string>* warnings = nullptr);
std::string alignToJson(const AlignSpec& a);
AlignSpec alignFromJson(const std::string& text);

// ---- yer osti tarmog'i
struct NetNode {
    std::string name;
    double x = 0, y = 0, ground = 0, invert = 0;   // qopqoq va tub (absolyut)
};
struct NetPipe {
    int from = 0, to = 0;              // tugun indekslari
    std::string kind = "SEWER";        // utilKey: WATER, SEWER, STORM, GAS, ...
    double dn = 300;                   // mm
};
struct NetSpec {
    std::vector<NetNode> nodes;
    std::vector<NetPipe> pipes;
};
Entities generateNetwork(const NetSpec& n);
std::string netToJson(const NetSpec& n);
NetSpec netFromJson(const std::string& text);

// ---- shahar mavzesi
struct CitySpec {
    int nx = 4, ny = 3;                // kvartallar soni
    double blockW = 120, blockD = 90;  // kvartal o'lchami (qizil chiziqlar orasida), m
    double street = 24;                // ko'cha kengligi (qizil chiziqlar orasi), m
    double carriage = 14;              // qatnov qismi kengligi, m
    double setback = 6;                // binoning qizil chiziqdan chekinishi, m
    double bldW = 60, bldD = 15;       // bino o'lchami, m
    double gap = 20;                   // binolar orasidagi oraliq (yong'in/insolyatsiya), m
    int floorsMin = 5, floorsMax = 12;
    double floorH = 3.0;
    double x0 = 0, y0 = 0;
    double ground = 0;                 // yer belgisi (chizmada yer nuqtalari bo'lmasa)
    unsigned seed = 7;
};
struct CityBuilding {
    std::vector<std::array<double, 2>> footprint;
    int floors = 1;
    double base = 0, height = 0;       // asos (absolyut) va balandlik
};
struct CityResult {
    Entities entities;
    std::vector<CityBuilding> buildings;
    double siteArea = 0, blockArea = 0, footprint = 0, gfa = 0, streetLength = 0;
    double far() const { return blockArea > 0 ? gfa / blockArea : 0; }   // zichlik (QQA / kvartal maydoni)
};
// Chizmadagi yer belgilari (yer sathi qatlamlaridagi yozuvlar va nuqtalar) bo'yicha IDW; nuqta yo'q bo'lsa — bo'sh funksiya
std::function<double(double, double)> groundFromDoc(const Document& doc, int* count = nullptr);
// groundAt(x, y) — yer belgisi (chizmadagi syomkadan); bo'sh funksiya — spec.ground
CityResult generateCity(const CitySpec& c, const std::function<double(double, double)>& groundAt = {});
std::string cityToJson(const CitySpec& c);
CitySpec cityFromJson(const std::string& text);

} // namespace asiacad::civil
