#pragma once
// AsiaCAD — o'z oqimli (va bosimli) kanalizatsiya tarmog'i: quduqlar zanjiri, quvur bo'laklari,
// gidravlik hisob (Manning, qisman to'lish), normativ tekshiruv, plan va bo'ylama profil chizmasi.
//
// Normativ qiymatlar SNiP 2.04.03-85 / KMK 2.04.03 asosida (sewer.cpp boshidagi jadvallar).
// ShNQ (2024+) matni bilan solishtirilmagan — har bir tekshiruv `ref` maydonida bandni ko'rsatadi,
// UI ularni "tasdiqlanmagan" deb belgilaydi.
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
