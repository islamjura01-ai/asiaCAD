#pragma once
// AsiaCAD — monolit karkas bino modeli: 2D plan (DXF/.acad qatlamlari) -> 3D beton elementlar.
// Padbetonka -> fundament (plita yoki lenta) -> har qavat: ustunlar, devorlar, plita -> tom plitasi.
// Har bir element — vertikal prizma (kontur + teshiklar, z0..z1). Hajm (m³), qolip (m²), qavat va tur bo'yicha PTO.
//
// Vertikal sxema (±0.000 — 1-qavat toza poli):
//   padbetonka:        fBot - leanT .. fBot          (fBot = foundTop - foundT)
//   fundament plitasi: fBot .. foundTop             (kontur + chiqish foundOverhang)
//   k-qavat ustun/devor: (k==1 ? foundTop : (k-1)·H) .. k·H - slabT
//   k-qavat plitasi:   k·H - slabT .. k·H           (oxirgi qavat — tom plitasi, roofT)
#include <asiacad/core/document.h>

#include <functional>
#include <map>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace asiacad::bim {

// Stair — chizmada zinapoya katagi konturi (ZINA/ЛЕСТН/STAIR), modelda — marshlar va maydonchalar;
// Shaft — lift shaxtasi ichki konturi (LIFT/ЛИФТ/SHAXTA), modelda — shaxta devorlari (plitada teshik)
enum class ElemKind { Lean, FoundationSlab, StripFooting, Column, Wall, Slab, Roof, Opening, Outline, Stair, Shaft, Ignore };

const char* kindKey(ElemKind k);  // "LEAN", "FOUND", ... (i18n: bim.kind.<key>)
bool kindFromKey(const std::string& s, ElemKind& out);
// Qatlam nomidan taxminiy tur (USTUN/КОЛОННА/COLUMN, DEVOR/СТЕНА/WALL, PLITA/ПЛИТА/SLAB, ...)
ElemKind guessKind(const std::string& layerName);

struct BuildingParams {
    int floors = 5;
    double floorHeight = 3.0;     // qavat balandligi (plita ustidan plita ustigacha), m
    double slabT = 0.20;          // qavatlararo plita, m
    double roofT = 0.22;          // tom plitasi, m
    double foundTop = -0.30;      // fundament usti belgisi, m
    double foundT = 0.60;         // fundament plitasi qalinligi, m
    double foundOverhang = 0.50;  // plita kontur tashqarisiga chiqishi, m
    bool stripFoundation = false; // true — lenta fundament (devorlar ostida) + ustun ostida stakan o'rniga plita
    double stripWidth = 0.8;      // lenta kengligi, m
    double leanT = 0.10;          // padbetonka qalinligi, m
    double leanOverhang = 0.10;   // padbetonka fundamentdan chiqishi, m
    double wallT = 0.20;          // chiziq bilan berilgan devor qalinligi, m
    // ±0.000 ning absolyut belgisi (Boltiq tizimi). zeroAbsAuto — chizmadagi yozuvdan ("±0,000 по ГП 484,20"),
    // topilmasa zeroAbs. 3D model, chizmaga chiqarish va syomka bilan solishtirish — absolyut belgilarda.
    double zeroAbs = 0;
    bool zeroAbsAuto = true;
    std::string classLean = "B7.5", classFound = "B25", classFrame = "B25", classSlab = "B25";
    // zinapoya va lift (KMK 2.08.01 / SP 54 — taxminiy chegaralar, tasdiqlanmagan)
    double stepTread = 0.30;      // pog'ona eni b, m
    double riserMax = 0.18;       // pog'ona balandligi h eng ko'pi, m
    double stairWidthMin = 1.05;  // marsh eni eng kami, m
    double stairT = 0.15;         // marsh plitasi (qiya) qalinligi, m
    double shaftWallT = 0.20;     // lift shaxtasi devori, m
    double shaftMin = 1.50;       // shaxta ichki o'lchami eng kami, m
};

struct Prism {
    ElemKind kind = ElemKind::Slab;
    int level = 0;                          // 0 — fundament, 1..N — qavatlar
    std::vector<Vec3> outline;              // XY, yopiq (oxirgi = birinchi emas)
    std::vector<std::vector<Vec3>> holes;
    double z0 = 0, z1 = 0;
    std::string concrete;
    double area() const;                    // kontur − teshiklar, m²
    double perimeter() const;               // tashqi + teshiklar, m
    double volume() const { return area() * (z1 - z0); }
    double formwork() const;                // qolip, m² (tur bo'yicha qoida)
};

struct PlanInput {
    std::vector<std::vector<Vec3>> columns, walls, slabs, openings, outlines;
    std::vector<std::vector<Vec3>> stairs, shafts;  // zinapoya kataklari, lift shaxtalari (ichki kontur)
    std::vector<std::vector<Vec3>> foundations, strips, leans;   // fundament plani (aniq konturlar)
    std::vector<std::pair<Vec3, Vec3>> wallLines;  // markaz chiziqlari (wallT bilan)
    double slabT = 0;                       // qolip chizmasidagi "h=200" / "δ=200" (0 — parametrdan)
    std::optional<double> levelTop;         // qolip chizmasidagi plita usti belgisi "+3.000"
    std::optional<double> zeroAbs;          // "±0,000 = 484,20 абс." — nisbiy nolning absolyut (Boltiq) belgisi
    bool empty() const {
        return columns.empty() && walls.empty() && wallLines.empty() && slabs.empty() && foundations.empty() && strips.empty() && stairs.empty() &&
               shafts.empty();
    }
};

// Qavatlar bo'yicha alohida planlar (qolip chizmalari): qavat -> plan; fundament plani alohida.
// Berilmagan qavatlar uchun tipik plan ishlatiladi.
struct FloorPlans {
    std::map<int, PlanInput> floors;
    std::optional<PlanInput> foundation;
};

// Hujjatdan: qatlam -> tur xaritasi bo'yicha (bo'sh bo'lsa guessKind). Bloklar ichidagi geometriya ham olinadi.
PlanInput readPlan(const Document& doc, const std::map<std::string, ElemKind>& layerMap);
std::map<std::string, ElemKind> autoLayerMap(const Document& doc);

struct BuildingIssue {
    bool error = false;
    std::string key;
    std::vector<std::string> args;
};

struct PtoRow {
    ElemKind kind;
    int level;
    int count = 0;
    double volume = 0, formwork = 0;
    std::string concrete;
};

// Zinapoya hisobi (har qavat): ikki marshli
struct StairInfo {
    int level = 0, risers = 0;
    double h = 0, b = 0, width = 0, flight = 0, landing = 0, cellL = 0;
    std::vector<Vec3> cell;
};

struct BuildingModel {
    std::vector<Prism> prisms;
    std::vector<StairInfo> stairs;
    std::vector<PtoRow> pto;                // tur × qavat
    std::map<std::string, double> byConcrete;  // sinf -> m³
    double totalVolume = 0, totalFormwork = 0;
    double heightTop = 0;                   // tom usti belgisi (absolyut)
    double zeroAbs = 0;                     // qo'llanilgan ±0.000 absolyut belgisi (prizmalar z — absolyut)
    bool zeroAbsKnown = false;              // chizmadan yoki foydalanuvchidan; false — balandliklar nisbiy
    std::vector<BuildingIssue> issues;
};

BuildingModel buildModel(const PlanInput& plan, const BuildingParams& p);
BuildingModel buildModel(const PlanInput& typical, const BuildingParams& p, const FloorPlans& fp);
// prizmalar bo'yicha PTO, beton sinflari va jami (buildModel oxirida ham chaqiriladi)
void computePto(BuildingModel& m);
// "+3.050", "-0,150", "±0.000" (uch kasrli belgi)
bool parseLevelText(const std::string& s, double& v);
// Nisbiy nolning absolyut belgisi: "За относительную отметку ±0,000 по ГП 484,20 принят уровень верха фундаментной плиты",
// "±0.000 = 484.200 абс.", "±0.00 = 484.20 kotu", "0.000 соответствует абсолютной отметке 484.20".
// ref: 1 — ±0.000 = fundament plitasi usti, 2 — toza pol, 0 — aytilmagan
struct DatumInfo {
    double abs = 0;
    int ref = 0;
    std::string text;
};
bool parseDatumText(const std::string& text, DatumInfo& d);
std::optional<DatumInfo> findDatum(const Document& doc);

// Parametrlar + qatlam xaritasi JSON'da (GUI -> BUILDING buyrug'i). Xato bo'lsa Error.
std::string paramsToJson(const BuildingParams& p, const std::map<std::string, ElemKind>& layerMap);
void paramsFromJson(const std::string& text, BuildingParams& p, std::map<std::string, ElemKind>& layerMap);

// PTO jadvali chizma obyektlari sifatida (origin — chap yuqori burchak), qatlam "PTO"
struct PtoLabels {
    std::string title, level, element, count, volume, formwork, klass, total;
    std::function<std::string(int)> levelName;
    std::function<std::string(ElemKind)> kindName;
};
std::vector<std::unique_ptr<IEntity>> drawPtoTable(const BuildingModel& m, const Vec3& origin, double h, const PtoLabels& L);

} // namespace asiacad::bim
