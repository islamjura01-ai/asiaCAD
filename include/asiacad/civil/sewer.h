#pragma once
// AsiaCAD — o'z oqimli (va bosimli) kanalizatsiya tarmog'i: quduqlar zanjiri, quvur bo'laklari,
// gidravlik hisob (Manning, qisman to'lish), normativ tekshiruv, plan va bo'ylama profil chizmasi.
//
// Tarmoq normalari: SNiP 2.04.03-85 "Kanalizatsiya. Tashqi tarmoqlar va inshootlar" (eski norma, matn bilan
// solishtirilgan; quduqlar oralig'i/o'lchami — SP 32.13330.2018 6.3.1, qiymatlar bir xil),
// har bir xabarda band raqami ko'rsatiladi.
// Yomg'ir (K2) sarfi: SHNQ 2.04.22-25, 62–70-bandlar — chegaraviy intensivlik usuli (8–13-formulalar, 3–7-jadvallar).
#include <array>
#include <asiacad/civil/pipe_catalog.h>
#include <asiacad/core/entity.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace asiacad::civil {

struct Manhole {
    std::string name;
    double x = 0, y = 0;
    double rim = 0;                       // yer (lyuk) belgisi, m
    std::optional<double> invert;         // chiqish quvuri tubi belgisi (berilmasa — hisoblanadi)
    double diameter = 0;                  // ichki diametr, m (0 — normaga ko'ra avto)
    double areaHa = 0;                    // shu quduqqa qo'shiladigan oqim maydoni, ga (yomg'ir tarmog'i)
};

// Yomg'ir suvlari hisobi (SHNQ 2.04.22-25). q20, n, mr, γ — hudud bo'yicha 1-rasm va 2-ilovadan kiritiladi.
struct StormInput {
    bool enabled = false;
    double q20 = 50;       // 20 min, P=1 yil intensivligi, l/s·ga (1-rasm)
    double n = 0.55;       // daraja ko'rsatkichi (2-ilova)
    double mr = 60;        // yildagi o'rtacha yomg'irlar soni (2-ilova)
    double gamma = 1.54;   // γ (2-ilova)
    double P = 1.0;        // hisobiy intensivlikdan bir marta oshish davri, yil (3-jadval)
    double tcon = 5;       // yuza konsentratsiya vaqti, min (69-band: 3–5 yoki 5–10)
    double tcan = 0;       // ko'cha novlari bo'ylab oqish vaqti, min (12-formula)
    // yuzalar ulushi (5-jadval tartibida): tom/asfalt, bruschatka/shag'al, xarsangtosh, ishlov berilmagan shag'al,
    // shag'alli yo'lak, grunt, gazon — yig'indisi 1
    std::array<double, 7> share{1, 0, 0, 0, 0, 0, 0};
};

struct Segment {
    std::string specId;
    double slope = 0;                     // 0 — tarmoqning standart nishabligi
    double flowLs = 0;                    // hisobiy sarf, l/s (0 — faqat to'liq kesim sig'imi)
};

struct SewerNetwork {
    std::string name = "K1";
    std::string system = "K1";            // K1 maishiy, K2 yomg'ir, K3 ishlab chiqarish, N bosimli
    bool street = true;                   // ko'cha tarmog'i (min Ø200); false — hovli (min Ø150)
    bool pressure = false;
    double startDepth = 1.5;              // birinchi quduqda quvur tubigacha, m
    double defaultSlope = 0.008;
    double frostDepth = 0.8;              // grunt muzlash chuqurligi, m (hudud bo'yicha kiriting)
    double textHeight = 1.0;              // chizma matni, m
    double vScale = 10;                   // profil: vertikal masshtab koeffitsienti
    StormInput storm;                     // system == "K2" va enabled bo'lsa — sarf avtomatik
    std::vector<Manhole> manholes;        // oqim yo'nalishida
    std::vector<Segment> segments;        // manholes.size() - 1
};

struct SegmentResult {
    double length = 0, slope = 0;
    double invUp = 0, invDown = 0;        // quvur tubi belgilari
    double depthUp = 0, depthDown = 0;    // yer sathidan quvur tubigacha
    double coverMin = 0;                  // quvur ustigacha eng kam qatlam
    double qFull = 0, vFull = 0;          // to'liq kesim: l/s, m/s
    double fill = 0, velocity = 0;        // hisobiy sarfda h/d va m/s (sarf berilsa)
    double maxFill = 0, vMin = 0, vMax = 0, maxSpacing = 0, minSlope = 0;
    double flowLs = 0;                    // hisobga olingan sarf (berilgan yoki yomg'ir hisobidan), l/s
    double stormF = 0, stormT = 0, stormZ = 0;  // yomg'ir: maydon ga, davomiylik min, z_mid
    const PipeSpec* spec = nullptr;
};

struct ManholeResult {
    double invIn = 0, invOut = 0;         // kirish/chiqish quvuri tubi
    double depth = 0;                     // yer sathidan quduq tubigacha
    double diameter = 0, required = 0;    // tanlangan / normadagi eng kichik, m
};

struct Issue {
    enum Level { Info, Warning, Error } level = Error;
    std::string key;                      // i18n: sewer.chk.<...>
    std::vector<std::string> args;
    std::string ref;                      // norma bandi
    int segment = -1, manhole = -1;
};

struct QuantityRow {
    std::string specId;
    double length = 0, massKg = 0;
};

struct SewerResult {
    std::vector<ManholeResult> manholes;
    std::vector<SegmentResult> segments;
    std::vector<Issue> issues;
    std::vector<QuantityRow> pipes;       // material/diametr bo'yicha
    std::map<double, int> manholeCount;   // diametr (m) -> soni
    double totalLength = 0;
    double trenchVolume = 0;              // m³, vertikal devorli xandaq (taxminiy)
    double beddingVolume = 0;             // qum to'shama 0.1 m, m³
    double backfillVolume = 0;            // m³
    int errors() const;
    int warnings() const;
};

SewerResult compute(const SewerNetwork& n, const PipeCatalog& cat = PipeCatalog::builtin());

// Qisman to'lgan doiraviy quvur (Manning): sarf (m³/s) va tezlik (m/s); D — ichki diametr, m
double partialFlow(double d, double fill, double slope, double n, double* velocity = nullptr);
// Berilgan sarf uchun to'lish darajasi h/d (0..1); sig'imdan oshsa 1.0 dan katta (−1 — hisoblab bo'lmaydi)
double fillForFlow(double d, double q, double slope, double n);

// Yomg'ir hisobi qismlari (sinov va UI uchun)
double stormA(const StormInput& s);                    // 10-formula
double stormZImpervious(double A, double n);           // 6-jadval (chiziqli interpolyatsiya)
double stormZmid(const StormInput& s);                 // 5-jadval bo'yicha o'rtacha z
double stormBeta(double n);                            // 7-jadval
double stormAreaK(double F);                           // 4-jadval (F ≥ 500 ga)
double stormShortK(double t);                          // 63-band (t < 10 min)
// Hisobiy sarf q_cal = β·q_r, l/s (8–9-formulalar); F — ga, t — min
double stormFlow(const StormInput& s, double F, double t);

// Profil jadvali sarlavhalari (UI tilida beriladi)
struct ProfileLabels {
    std::string ground = "Ground, m", invert = "Invert, m", depth = "Depth, m", slope = "Slope / length",
                distance = "Distance, m", pipe = "Pipe", manhole = "Manhole", datum = "Datum";
};

// Chizma: plan (quduqlar haqiqiy joyida) va profil (origin — chap pastki burchak)
std::vector<std::unique_ptr<IEntity>> drawPlan(const SewerNetwork& n, const SewerResult& r);
std::vector<std::unique_ptr<IEntity>> drawProfile(const SewerNetwork& n, const SewerResult& r, const Vec3& origin,
                                                  const ProfileLabels& labels);
std::vector<std::string> sewerLayers(const SewerNetwork& n);  // <name>_QUVUR, _QUDUQ, _YOZUV, _PROFIL

// JSON (".kan.json") — Error/ParseError bilan
std::string toJson(const SewerNetwork& n);
SewerNetwork fromJson(const std::string& text);

} // namespace asiacad::civil
