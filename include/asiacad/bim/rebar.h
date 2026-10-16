#pragma once
// AsiaCAD — armatura: bino modelidagi har bir beton elementga shablon bo'yicha 3D sterjenlar.
//   Fundament plitasi / lenta / plita / tom — pastki va yuqori to'r (kontur va teshiklar bo'yicha kesiladi),
//                                             fiksator (stul) soni m² bo'yicha
//   Ustun   — bo'ylama sterjenlar (burchak + oraliq), xomutlar (uchlarida zichlashtirilgan), keyingi qavatga nahlyostka
//   Devor   — vertikal va gorizontal sterjenlar (ikki yuzada), shpilkalar m² bo'yicha
// Natija: 3D sterjenlar, ведомость (pozitsiya, Ø, sinf, uzunlik, soni, massa), Ø va sinf bo'yicha jami,
// bog'lash simi (tugunlar × sim uzunligi), normativ tekshiruv — ShNQ 2.03.01-24, 8-bob (376–434-bandlar, 21, 36, 37-jadvallar).
#include <asiacad/bim/building.h>

#include <map>
#include <string>
#include <vector>

namespace asiacad::bim {

struct RebarTemplate {
    std::string id, name;   // name — i18n kaliti ("rtpl....") yoki foydalanuvchi nomi
    ElemKind kind = ElemKind::Slab;
    // umumiy
    double cover = 0.025;   // himoya qatlami (sterjen sirtigacha), m
    double lapD = 40;       // nahlyostka uzunligi, Ø lar soni
    std::string clsMain = "A400", clsStir = "A240";  // ShNQ 2.03.01-24, 21-jadval
    // to'r (plita/fundament/lenta) va devor: d1/s1 — pastki X yoki vertikal; d2/s2 — pastki Y yoki gorizontal
    double d1 = 0.012, s1 = 0.2, d2 = 0.012, s2 = 0.2;
    bool topMesh = true;    // yuqori to'r (plita/fundament) yoki ikkinchi yuza (devor)
    double d3 = 0.012, s3 = 0.2, d4 = 0.012, s4 = 0.2;  // yuqori to'r X/Y
    double chairsPerM2 = 1.0, tiesPerM2 = 4.0;
    // ustun
    int nMain = 4;
    double dStir = 0.008, sStir = 0.2, sStirEnd = 0.1;
    bool user = false;
};

enum class BarRole { Main, Stirrup, BottomX, BottomY, TopX, TopY, Vertical, Horizontal, Tie, Chair };
const char* roleKey(BarRole r);  // i18n: rebar.role.<key>

struct Bar {
    ElemKind kind;
    int level;
    BarRole role;
    double d;               // m
    std::string cls;
    std::vector<Vec3> pts;  // 3D o'q chizig'i (xomut — yopiq)
    double length;          // nahlyostka va ilgaklar bilan, m
};

struct ScheduleRow {
    int pos = 0;
    ElemKind kind;
    int level;
    BarRole role;
    double d;
    std::string cls;
    double length;          // bitta sterjen, m
    int count = 0;
    double totalLength = 0, mass = 0;  // m, kg
};

struct RebarIssue {
    bool error = false;
    std::string key;
    std::vector<std::string> args;
};

struct RebarModel {
    std::vector<Bar> bars;                      // 3D ko'rinish uchun (shpilka va stullar kirmaydi)
    std::vector<ScheduleRow> schedule;
    std::map<std::string, double> massByDia;    // "Ø12 A400" -> kg
    double totalMass = 0;                       // kg
    double wireKg = 0;                          // bog'lash simi
    long nodes = 0;                             // bog'lanadigan tugunlar
    int chairs = 0, ties = 0;
    std::vector<RebarIssue> issues;
};

struct RebarSettings {
    std::map<ElemKind, RebarTemplate> byKind;   // Column, Wall, Slab, Roof, FoundationSlab, StripFooting
    double stockLength = 11.7;                  // sterjen savdo uzunligi (nahlyostkalar soni uchun), m
    double wirePerNode = 0.30;                  // bitta tugunga sim, m
    double wireKgPerM = 0.00888;                // Ø1.2 mm termik ishlov berilgan sim, kg/m
};

double barMassPerM(double d);
// ShNQ 2.03.01-24: 12-jadval Rb (og'ir beton, MPa; noma'lum sinf — 0) va 451-band/36-jadval nahlyostka uzunligi, m
double concreteRbShnq(const std::string& cls);
double requiredLapShnq(const std::string& steel, double d, const std::string& concrete, bool tension);                   // kg/m (7850 kg/m³)
std::vector<RebarTemplate> builtinRebarTemplates();
RebarSettings defaultRebarSettings();           // har bir tur uchun birinchi mos shablon

RebarModel buildRebar(const BuildingModel& bm, const BuildingParams& bp, const RebarSettings& rs);

std::string rebarToJson(const RebarSettings& rs);
void rebarFromJson(const std::string& text, RebarSettings& rs);   // bino JSON ichidagi "rebar" bloki ham qabul qilinadi
std::string templatesToJson(const std::vector<RebarTemplate>& t);
std::vector<RebarTemplate> templatesFromJson(const std::string& text);

struct ScheduleLabels {
    std::string title, pos, element, d, cls, length, count, mass, total, wire;
    std::function<std::string(int)> levelName;
    std::function<std::string(ElemKind)> kindName;
    std::function<std::string(BarRole)> roleName;
};
std::vector<std::unique_ptr<IEntity>> drawSchedule(const RebarModel& m, const Vec3& origin, double h, const ScheduleLabels& L);

} // namespace asiacad::bim
