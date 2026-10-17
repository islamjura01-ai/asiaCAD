#pragma once
// AsiaCAD — suv yo'li: bosimli va o'zi oqar quvurlar, beton kanallar, tuproq ariqlar, lotoklar (yig'ma novlar).
//  * Plan chizmasidan: o'q (KANAL/ARIQ/LOTOK/КАНАЛ/ЛОТОК/АРЫК/SUV_YOLI/ВОДОВОД ...), yer belgilari (rel'ef qatlami, 3D nuqtalar,
//    3D gorizontallar), loyiha belgilari o'q yonida: tub "▽485,20", "дно 485.20", "Л 485,20"; 3D o'q bo'lsa — Z.
//    Barcha balandliklar absolyut (Boltiq) — chizmadagi yozuvlardan; yer belgisi umuman bo'lmasa — xato.
//  * Kesim: to'g'ri to'rtburchak/trapetsiya (b, h, m), parabolik lotok (B, h), quvur (D). Qoplama qalinligi t.
//  * Gidravlika: Manning — normal chuqurlik, tezlik, Froud soni, o'tkazish qobiliyati (zaxira balandligi bilan);
//    bosimli quvur — Hazen-Williams bosim yo'qotishi, pyezometrik chiziq.
//  * Hajmlar: qazish/to'kma (kotlovan qiyaligi bilan), beton qoplama, lotok elementlari soni, quvur uzunligi.
//  * Tekshiruv (SNiP 2.06.03-85 "Мелиоративные системы и сооружения" va 2.04.02-84 asosida — tasdiqlanmagan):
//    sig'maslik, loyqa bosish (v < vmin), yuvilish (v > vmax), teskari nishablik, bosimli quvurda tezlik va manfiy bosim.
#include <asiacad/core/document.h>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace asiacad::civil {

enum class WwType { PressurePipe, GravityPipe, ConcreteCanal, EarthDitch, Flume };
const char* wwTypeKey(WwType t);  // "PPIPE", "GPIPE", "CANAL", "DITCH", "FLUME" (i18n: ww.type.<key>)
bool wwTypeFromKey(const std::string& s, WwType& out);
bool wwOpen(WwType t);            // ochiq o'zan (kanal, ariq, lotok)

struct WwSection {
    WwType type = WwType::ConcreteCanal;
    std::string name;             // "Beton kanal 1.5×1.2", "ЛР-6" ...
    double b = 1.0;               // tub kengligi (kanal/ariq), m
    double h = 1.0;               // kesim balandligi (qirg'oqqacha), m
    double m = 0.0;               // qiyalik koeffitsiyenti 1:m (0 — to'g'ri to'rtburchak)
    double B = 0;                 // parabolik lotok: yuqori kenglik (0 — trapetsiya/to'rtburchak)
    double d = 0.5;               // quvur ichki diametri, m
    double t = 0.12;              // devor / qoplama qalinligi, m (ariqda 0)
    double n = 0.014;             // g'adir-budurlik (Manning)
    double chw = 130;             // Hazen-Williams C (bosimli quvur)
    double freeboard = 0.15;      // zaxira balandlik (ochiq o'zan), m
    double pieceLen = 6.0;        // lotok elementi uzunligi, m
    double vmin = 0.4, vmax = 4.0;  // ruxsat etilgan tezliklar, m/s
    bool user = false;
};
std::vector<WwSection> builtinWwSections();   // shablonlar (lotok o'lchamlari taxminiy — katalogdan tekshirilsin)

enum class WwLayer { Axis, Ground, Design, Ignore };
const char* wwLayerKey(WwLayer k);
bool wwLayerFromKey(const std::string& s, WwLayer& out);
WwLayer guessWwLayer(const std::string& layer);
std::map<std::string, WwLayer> autoWwLayers(const Document& doc);

struct WwParams {
    WwSection sec;
    double q = 0.5;               // hisobiy sarf, m³/s
    double step = 20;             // piket oralig'i, m
    double ground = 0;            // yer nuqtalari yo'q bo'lsa (xato beriladi)
    double depth = 1.0;           // loyiha belgisi yo'q: tub = yer − (kesim + depth·[quvur]) ; ochiq o'zanda tub = yer − h
    double slope = 0.001;         // loyiha belgisi yo'q bo'lsa — nishablik (boshidan oxiriga pasayadi)
    double headStart = 0;         // bosimli quvur: boshlang'ich pyezometrik belgi (0 — tub + 10 m)
    double labelRadius = 6.0;     // yozuvni o'qqa biriktirish, m
    double cutSlope = 1.0;        // kotlovan qiyaligi 1:m
    double trenchExtra = 0.3;     // kotlovan tubi kesim tashqi kengligidan har tomonga, m
};
WwParams defaultWwParams();

struct WwStation {
    double s = 0, x = 0, y = 0, dx = 1, dy = 0;
    double ground = 0;            // yer (absolyut)
    double bottom = 0;            // o'zan tubi / quvur lotogi (absolyut)
    bool control = false;
    double slope = 0;             // keyingi piketgacha (pasayish +)
    double depthN = 0;            // normal chuqurlik (ochiq o'zan / o'zi oqar quvur), m
    double v = 0, fr = 0;         // tezlik, Froud
    double water = 0;             // suv sathi (ochiq/o'zi oqar) yoki pyezometrik belgi (bosimli)
    double cut = 0, fill = 0;     // ko'ndalang maydon, m²
};

struct WwIssue {
    bool error = false;
    std::string key;
    std::vector<std::string> args;
    double s = 0;
};

struct WwModel {
    std::vector<WwStation> st;
    std::vector<WwIssue> issues;
    std::vector<std::array<double, 3>> groundPts;
    int groundPoints = 0, designPoints = 0;
    double length = 0;
    double cutVolume = 0, fillVolume = 0, concrete = 0;   // m³
    int pieces = 0;               // lotok elementlari
    double capacity = 0;          // eng kichik o'tkazish qobiliyati (zaxira bilan), m³/s
    double headLoss = 0;          // bosimli quvur, m
    int errors() const;
};

// Kesim gidravlikasi (sinov uchun ochiq): y — suv chuqurligi
double wwArea(const WwSection& s, double y);
double wwPerimeter(const WwSection& s, double y);
double wwTopWidth(const WwSection& s, double y);
double wwFlow(const WwSection& s, double y, double slope);           // Manning, m³/s
double wwNormalDepth(const WwSection& s, double q, double slope);    // -1 — sig'maydi
double wwFullDepth(const WwSection& s);                              // kesim balandligi (quvurda D)
// Belgi yozuvi: 'b' — tub ("дно 485.20", "Л 485,20", "tub", "taban"), 'g' — yer ("земля", "yer", "GL"), 'w' — suv sathi
// ("УВ", "ГВ"), 'z' — turi aytilmagan ("▽485,20", "485.20" — qatlamiga qarab), 0 — belgi emas
char wwLabel(const std::string& text, double& v);

WwModel buildWaterway(const Document& doc, const std::map<std::string, WwLayer>& layers, const WwParams& p);
void recomputeWaterway(WwModel& m, const WwParams& p);   // qo'lda tuzatilgan tub belgilaridan keyin

std::string wwToJson(const WwModel& m, const WwParams& p);
void wwFromJson(const std::string& text, WwModel& m, WwParams& p);

// Chizmaga 3D (absolyut belgilar): o'zan qirralari, tub, suv sathi, har piketda kesim; quvur — o'q va halqalar
std::vector<std::unique_ptr<IEntity>> drawWaterway3D(const WwModel& m, const WwParams& p);
// Bo'ylama profil: yer, tub, qirg'oq (kesim usti), suv sathi; origin — chap yuqori burchak
std::vector<std::unique_ptr<IEntity>> drawWaterwayProfile(const WwModel& m, const WwParams& p, const Vec3& origin, double vscale, double h,
                                                          const std::string& groundLbl, const std::string& bottomLbl,
                                                          const std::string& waterLbl, const std::string& pkLbl);

} // namespace asiacad::civil
