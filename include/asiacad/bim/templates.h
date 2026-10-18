#pragma once
// AsiaCAD — konstruksiya shablonlari (0 dan, haqiqiy koordinata va dengiz sathi balandligida joylanadi):
//  * STEELFRAME — metall karkas: ko'p qavatli (o'qlar to'ri) yoki portal rama (angar): ustun, rigel, rafter, progon, bog'lamalar
//  * STAIR      — zina: ikki marshli (U) yoki bir marshli; beton yoki metall (kosour — shveller); 3D pog'onalar + plan belgilari
//  * RAMP       — yerto'laga rampa: avtomobil (o'tish qismlari bilan) yoki piyoda (maydonchalar bilan); yon devorlar, belgilar
//  * RWALL      — tirgak devor: L (uchburchak), T (konsol), gravitatsion; kesim chizmasi, 3D, beton hajmi, barqarorlik
// Joylash: lokal (0,0) — tayanch nuqta; burilish (gradus, X dan soat strelkasiga qarshi); z0 — tayanch nuqtaning absolyut belgisi.
// Geo-joylash: tanlangan obyektlarni 1–2 nuqta bo'yicha geodezik koordinata va belgiga o'tkazish (GEOPLACE).
#include <asiacad/core/document.h>

#include <memory>
#include <string>
#include <vector>

namespace asiacad::bim {

using Entities = std::vector<std::unique_ptr<IEntity>>;

struct Placement {
    double x = 0, y = 0, rotDeg = 0, z0 = 0;
    Transform transform() const;
};

struct TplIssue {
    bool error = false;
    std::string key;
    std::vector<std::string> args;
};
struct TplResult {
    Entities entities;
    std::vector<TplIssue> issues;
    std::vector<std::pair<std::string, std::string>> info;  // i18n kalit -> qiymat (xulosa jadvali)
    double concrete = 0;        // m³
    double steel = 0;           // t
};

// ---- metall karkas
struct SteelFrameSpec {
    int type = 1;                                // 0 — ko'p qavatli karkas, 1 — portal rama (angar)
    std::vector<double> dx{6, 6, 6}, dy{6, 6};   // 0: o'qlar oraliqlari
    int floors = 2;
    double floorH = 3.6;
    double span = 18, bay = 6, eave = 6, ridge = 7.5;  // 1: oraliq, rama qadami, karniz va tizma balandligi
    int bays = 6;
    double purlinStep = 1.5;
    std::string column = "I40", beam = "I30", beam2 = "I20", rafter = "I40", purlin = "[16", brace = "L75x6";
    bool bracing = true;
    Placement at;
};
TplResult makeSteelFrame(const SteelFrameSpec& s);

// ---- zina
struct StairSpec {
    int type = 0;                // 0 — ikki marshli (U), 1 — bir marshli to'g'ri
    double H = 3.0;              // qavat balandligi (pol — pol)
    double width = 1.2;          // marsh eni
    double gap = 0.10;           // marshlar orasi (U)
    double tread = 0.30, riserMax = 0.17;
    double landing = 1.5;        // maydoncha (U — oraliq, to'g'ri — yuqori va pastki)
    double waist = 0.15;         // marsh plitasi qalinligi
    bool steel = false;          // metall zina: kosour (stringer) — shveller
    std::string stringer = "[16";
    Placement at;
};
TplResult makeStair(const StairSpec& s);

// ---- rampa
struct RampSpec {
    int kind = 0;                // 0 — avtomobil, 1 — piyoda (MGN)
    double H = 3.3;              // chuqurlik (yer — yerto'la poli)
    double width = 3.5;
    double slope = 15;           // % (asosiy qism)
    double trans = 3.6;          // avtomobil: o'tish qismlari uzunligi (yarim nishablik bilan), m
    double landingEvery = 0.8;   // piyoda: shu balandlikdan keyin maydoncha, m
    double landingL = 1.5;
    double wallT = 0.25, parapet = 0.3;
    double floorL = 6.0;         // pastdagi gorizontal qism
    Placement at;
};
TplResult makeRamp(const RampSpec& s);

// ---- tirgak devor
struct RetainSpec {
    int type = 1;                // 0 — L (tovonsiz), 1 — T (konsol), 2 — gravitatsion (trapetsiya)
    double H = 3.0;              // devor balandligi (poydevor ustidan)
    double tTop = 0.25, tBot = 0.40;
    double B = 2.1;              // poydevor eni
    double toe = 0.5;            // old tovon (T)
    double hBase = 0.40;         // poydevor qalinligi
    double length = 20;          // devor uzunligi (to'g'ri)
    double phi = 30, gamma = 18, q = 10;  // grunt: ichki ishqalanish burchagi °, solishtirma og'irlik kN/m³, yuklama kPa
    double mu = 0.5, Rgr = 200;           // poydevor ishqalanishi, grunt hisobiy qarshiligi kPa
    Placement at;
};
TplResult makeRetainingWall(const RetainSpec& s);
// Barqarorlik (1 m uchun, Rankin): ag'darilish va siljish zaxira koeffitsientlari, poydevor ostidagi bosim
struct RetainCheck {
    double Pa = 0, W = 0, Mo = 0, Mr = 0, FSo = 0, FSs = 0, e = 0, qmax = 0, qmin = 0, area = 0;
};
RetainCheck retainCheck(const RetainSpec& s);
// Kesim konturi (lokal: x — devor ortidan tashqariga, 0 — old yuz pastki burchagi; z — yuqoriga)
std::vector<Vec3> retainSection(const RetainSpec& s);

// ---- JSON (buyruqlar uchun)
std::string steelFrameToJson(const SteelFrameSpec& s);
SteelFrameSpec steelFrameFromJson(const std::string& t);
std::string stairToJson(const StairSpec& s);
StairSpec stairFromJson(const std::string& t);
std::string rampToJson(const RampSpec& s);
RampSpec rampFromJson(const std::string& t);
std::string retainToJson(const RetainSpec& s);
RetainSpec retainFromJson(const std::string& t);

// ---- geo-joylash: lokal p1 -> dunyo P1 (+ p2 -> P2 burilish uchun). Masshtab 1 (allowScale bo'lsa — hisoblanadi).
struct GeoFit {
    Transform t;
    double rotDeg = 0, scale = 1, residual = 0;  // residual — p2 nuqtadagi farq (masshtab 1 da), m
};
GeoFit geoFit(const Vec3& p1, const Vec3& P1, const Vec3* p2, const Vec3* P2, bool allowScale);

} // namespace asiacad::bim
