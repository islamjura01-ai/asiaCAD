#pragma once
// AsiaCAD — bitta KJ faylidan (qolip + armatura chizmalari bitta model fazosida) 3D beton va armatura.
//
// Haqiqiy loyihalarda (masalan "1-43B-9 КЖ") barcha varaqlar model fazosida yonma-yon, har ko'rinish o'z masshtabida:
//  * Ko'rinish = o'q pufakchalari to'plami (aylana ichida "1", "11Р", "А", "Б/1" ...); sarlavha — yaqin matn
//    ("Опалубочный план ... фундаментной плиты", "Схема расположения выпусков", "... плиты перекрытия в нижней зоне").
//  * Masshtab — ko'rinishdagi o'lchamlar uslubi DIMLFAC (M200 → 2: chizmada 3000, haqiqatda 6000).
//  * Tekislash — "1" o'qi X, "А" o'qi Y: barcha ko'rinishlar bitta koordinatada ustma-ust tushadi.
//  * Qolip: eng katta yopiq (yoki deyarli yopiq) kontur — plita; kichik to'rtburchaklar — ustun, cho'ziqlari — devor.
//  * Armatura: "Ø16AIII, ш.200" — asosiy to'r (yuqori va pastki, X va Y); "d 20" kabi qatlamlardagi chiziqlar — qo'shimcha
//    armatura zonasi (zona to'rtburchagi bo'ylab qadam bilan); aylanalar — vertikal sterjenlar (vypusk, shpilka).
#include <asiacad/bim/rebar.h>
#include <asiacad/bim/rebar_import.h>
#include <asiacad/io/dxf_reader.h>

namespace asiacad::bim {

enum class KjViewKind {
    FoundMain,         // fundament plitasi: qolip + asosiy to'r
    FoundExtraBottom,  // fundament: qo'shimcha pastki
    FoundExtraTop,     // fundament: qo'shimcha yuqori
    FoundVertical,     // fundament: qo'shimcha vertikal (shpilkalar)
    FoundSupports,     // tayanch karkaslar (3D ga kirmaydi)
    Outlets,           // vypusklar (ustun/devorga)
    Columns,           // ustun va devorlar sxemasi
    SlabMain,          // qavat plitasi: qolip + asosiy to'r
    SlabExtraBottom,
    SlabExtraTop,
    Other
};
const char* kjViewKey(KjViewKind k);  // i18n: kj.view.<key>

struct KjBarSet {          // bir xil sterjenlar guruhi (ведомость qatori)
    KjViewKind view;
    BarRole role;
    double d = 0;          // m
    std::string cls;
    int count = 0;
    double length = 0;     // jami, m
};

struct KjView {
    KjViewKind kind = KjViewKind::Other;
    std::string title;
    BBox box;                      // chizma koordinatasida (o'qlar to'plami chegarasi)
    double scale = 1;              // chizma birligi -> m (lfac × fayl masshtabi)
    Vec3 origin;                   // "1" va "А" o'qlari kesishmasi (chizmada)
    bool aligned = false;          // ikkala o'q topildi
    std::map<std::string, double> axesX, axesY;   // haqiqiy (m), origin'dan
    int columns = 0, walls = 0, dots = 0, zones = 0, lines = 0, bars = 0;
    std::vector<double> levels;    // matndagi belgilar
    double outlineArea = 0;        // m²
    std::string mainMark;          // "Ø16 A400 s200"
    bool use = true;
};

struct KjParams {
    double foundTop = -0.15;       // fundament plitasi usti (pol pirogi ostida), m
    double foundT = 0.40;          // fundament qalinligi, m
    double leanT = 0.10;           // beton tayyorlov (B7.5)
    double cover = 0.04;           // himoya qatlami (sterjen o'qigacha emas — sirtigacha), m
    double outletIn = 0.64, outletOut = 0.96;  // vypusk: plitada / plitadan yuqori
    double slabTop = 0;            // 0 — chizmadan (+3.050)
    double slabT = 0;              // 0 — chizmadan (+3.050 / +2.850 → 0.20)
    double meshStep = 0.2;         // belgi topilmasa
    bool columnCages = true;       // ustun karkaslari: vypusklar joyida asosiy sterjenlar + xomutlar
    double stirD = 0.010, stirS = 0.2, stirSEnd = 0.1;  // xomut ("Ø10 A-I шаг 100/200")
    std::string concrete = "";     // bo'sh — chizmadan ("Бетон кл.В30"), aks holda B25
};

struct KjProject {
    std::vector<KjView> views;
    BuildingModel bm;              // beton prizmalar + PTO
    std::vector<Bar> bars;         // 3D
    std::vector<ScheduleRow> schedule;
    std::map<std::string, double> massByDia;
    double totalMass = 0;
    std::vector<SpecRow> spec;     // chizmadagi spetsifikatsiya jadvallari
    double specMass = 0;
    double slabTop = 0, slabT = 0; // qo'llanilgan
    std::string concrete;
    std::vector<RebarIssue> issues;
};

// Faqat ko'rinishlarni topish (tez): o'q pufakchalari, sarlavha, masshtab, tekislash
std::vector<KjView> detectKjViews(const Document& doc, const std::vector<DxfDimInfo>& dims);
// To'liq: ko'rinishlar + beton + armatura. `views` berilsa (GUI'da "use" belgilari bilan) — qayta topilmaydi.
KjProject buildKjProject(const Document& doc, const std::vector<DxfDimInfo>& dims, const KjParams& p,
                         const std::vector<KjView>* views = nullptr);

} // namespace asiacad::bim
