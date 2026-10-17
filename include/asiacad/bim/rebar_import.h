#pragma once
// AsiaCAD — KJ (temir-beton, armaturalash) chizmalaridan armaturani o'qish.
//  * Markirovka: "Поз.1 Ø12 A500C L=3450 шаг 200", "4Ø16 A400", "⌀10 A-III s=150 верх", "%%c12 AIII @200 низ"
//    -> pozitsiya, soni, Ø, sinf (A-I/A-III -> A240/A400), uzunlik, qadam, yuza (pastki/yuqori)
//  * Sterjenlar: armatura qatlamlaridagi chiziq/poliliniyalar (ARM, АРМ, REBAR, STERJEN...), yozuv eng yaqin sterjenga
//  * Taqsimlash chizig'i: sterjenga perpendikulyar chiziq — soni = uzunlik / qadam + 1, nusxalar shu chiziq bo'ylab
//  * Spetsifikatsiya jadvali ("Поз." | "Наименование" | "Кол." | "Масса") — matnlar qatorlarga yig'iladi
//  * Z: bino modelidagi shu qavat plitasi (pastki — tagidan, yuqori — ustidan himoya qatlami bilan)
//  * Taqqoslash: chizmadagi soni ↔ spetsifikatsiya soni, Ø/sinf mosligi, umumiy massa
#include <asiacad/bim/rebar.h>

namespace asiacad::bim {

struct BarMark {
    int pos = 0, count = 0;
    double d = 0;              // m
    std::string cls;
    double length = 0;         // m
    double spacing = 0;        // m
    int face = 0;              // 0 — noma'lum, 1 — pastki, 2 — yuqori
};
bool parseBarMark(const std::string& text, BarMark& out);   // Ø topilsa true
std::string normRebarClass(const std::string& raw);        // "A-III" -> "A400", "AIII" -> "A400", "А500С" -> "A500C"
bool isRebarLayer(const std::string& layer);
int faceFromLayer(const std::string& layer);               // ВЕРХ/YUQORI/TOP -> 2, НИЗ/PASTKI/BOT -> 1

struct SpecRow {
    int pos = 0;
    double d = 0;
    std::string cls;
    double length = 0;         // m
    int count = 0;
    double massEach = 0, mass = 0;  // kg
    std::string text;
};
std::vector<SpecRow> readSpecification(const Document& doc, BBox* region = nullptr);  // region — jadval chegarasi

struct RebarSheetInput {
    const Document* doc = nullptr;
    ElemKind element = ElemKind::Slab;   // Slab/Roof/FoundationSlab — 3D; Wall/Column — faqat spetsifikatsiya
    int floorFrom = 1, floorTo = 1;
    std::string tag;
};

struct ImportedRebar {
    std::vector<Bar> bars;                     // 3D
    std::vector<SpecRow> spec;                 // barcha varaqlar (qavatlar soniga ko'paytirilgan)
    std::vector<ScheduleRow> schedule;         // chizmadan (pozitsiya × qavat)
    std::map<std::string, double> massByDia;
    double totalMass = 0, specMass = 0;
    int marks = 0, unlabeled = 0;
    std::vector<RebarIssue> issues;
};

ImportedRebar importRebar(const std::vector<RebarSheetInput>& sheets, const BuildingModel& bm, double cover = 0.025);

} // namespace asiacad::bim
