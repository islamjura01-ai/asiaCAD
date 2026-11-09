#pragma once
// AsiaCAD — IFC (ISO 16739, STEP fizik fayli ISO 10303-21) o'quvchi: IFC2X3, IFC4, IFC4X3.
// Revit, ArchiCAD, Tekla, SketchUp, Allplan, Renga eksportlari. Tashqi kutubxonasiz.
//
// O'qiladi:
//  * fazoviy tuzilma: loyiha, uchastka, bino, qavatlar (IfcBuildingStorey, belgisi)
//  * qurilish elementlari (IfcProduct avlodlari): devor, plita, ustun, to'sin, fundament, zina, deraza, eshik,
//    tom, armatura, quvurlar... — nomi, GUID, qavati, materiali, rangi, xossalar to'plami (Pset/Qto)
//  * geometriya: IfcExtrudedAreaSolid (to'rtburchak, doira, I/L/U/T/C profillar, ixtiyoriy kontur + teshiklar),
//    IfcFacetedBrep, IfcShellBasedSurfaceModel, IfcFaceBasedSurfaceModel, IfcTriangulatedFaceSet,
//    IfcPolygonalFaceSet, IfcMappedItem (tip geometriyasi), IfcBooleanResult (birinchi operand),
//    IfcSweptDiskSolid (armatura sterjenlari), joylashuv zanjiri IfcLocalPlacement
//  * birliklar: mm / m / fut (IfcSIUnit, IfcConversionBasedUnit) — natija doim metrda
//  * teshiklar (IfcRelVoidsElement): qavariq element (oddiy devor, plita) minus qavariq teshik — aniq ayiriladi
// O'qilmaydi (keyingi bosqich): qavariq bo'lmagan elementlardagi teshiklar, NURBS sirtlar (qirralar bo'yicha taxminiy).
#include <asiacad/core/math.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace asiacad::bim {

// Bir xil rangli bo'lak (deraza: rom + shisha)
struct IfcPart {
    std::uint32_t rgb = 0;            // 0 — rang berilmagan (material yoki tur bo'yicha standart rang)
    double alpha = 1.0;               // 1 — shaffof emas
    std::vector<std::array<Vec3, 3>> tris;              // dunyo koordinatalari, m
};

struct IfcElement {
    int id = 0;                       // STEP #raqami
    std::string guid, type, name;     // type — "IFCWALL" (katta harf), name — UTF-8
    std::string tag, objectType;
    int storey = -1;                  // IfcModel::storeys indeksi (-1 — qavatsiz)
    std::string material;             // materiallar nomi ("Beton B25; Armatura")
    std::vector<IfcPart> parts;       // ranglari hal qilingan (o'qishdan keyin rgb doim != 0)
    std::vector<std::pair<std::vector<Vec3>, double>> bars;   // armatura o'qi (IfcSweptDiskSolid) va radiusi, m
    std::vector<std::pair<std::string, std::string>> props;   // "Pset_WallCommon.IsExternal" -> "TRUE"
    BBox box;
    Vec3 origin;                      // joylashuv nuqtasi (ObjectPlacement boshi) — ustun/qoziq markazi, razbivka
    double angle = 0;                 // rejadagi burilish (lokal X o'qi), radian
    double volume = 0;                // yopiq jism hajmi, m³ (geometriyadan)
    double barLength = 0;             // armatura o'qlari uzunligi, m
    double barMass = 0;               // armatura massasi, kg (7850 kg/m³)
    std::size_t triangleCount() const;
};

struct IfcStorey {
    int id = 0;
    std::string name;
    double elevation = 0;             // m
    int elements = 0;
};

// Koordinata o'qi (IfcGrid / IfcGridAxis): "A", "1"...
struct IfcGridAxis {
    std::string tag;
    std::vector<Vec3> pts;            // dunyo koordinatalari, m (odatda 2 nuqta)
};

struct IfcModel {
    std::string schema;               // "IFC2X3", "IFC4"...
    std::string application;          // eksport qilgan dastur (FILE_NAME)
    std::string project, site, building;
    double unitScale = 1.0;           // fayl birligi -> m
    std::vector<IfcStorey> storeys;   // belgisi bo'yicha tartiblangan
    std::vector<IfcElement> elements;
    std::vector<IfcGridAxis> axes;
    int entityCount = 0;              // fayldagi STEP yozuvlari
    int skipped = 0;                  // geometriyasi o'qilmagan elementlar
    int openings = 0;                 // teshiklar (IfcOpeningElement) jami
    int openingsCut = 0;              // ulardan ayirilgani (qavariq element va teshik)
    BBox box;
    bool georef = false;              // IfcMapConversion qo'llangan (koordinatalar xarita tizimida)
    bool hasMapConversion = false;    // faylda IfcMapConversion bor (qo'llanmagan bo'lsa ham)
    std::vector<std::string> warnings;

    std::size_t triangleCount() const;
};

// Fayl o'qish. Xato bo'lsa — false va err (i18n kaliti: "ifc.err.open" / "ifc.err.format" / "ifc.err.empty").
// georef — IfcMapConversion bo'lsa xarita koordinatalariga (UTM / mahalliy tizim) o'tkazish.
bool readIfc(const std::string& path, IfcModel& out, std::string& err, bool georef = true);
// Matndan (sinov uchun)
bool parseIfc(const std::string& text, IfcModel& out, std::string& err, bool georef = true);

// Tur bo'yicha guruh kaliti (i18n: ifc.cat.<key>): "WALL", "SLAB", "COLUMN", "BEAM", "FOOTING", "STAIR", "WINDOW",
// "DOOR", "ROOF", "RAILING", "COVERING", "PLATE", "MEMBER", "CURTAIN", "REBAR", "PILE", "PIPE", "DUCT", "FURNITURE",
// "SPACE", "SITE", "MEP", "PROXY", "OTHER"
const char* ifcCategory(const std::string& type);
// Tur bo'yicha standart rang (rang berilmaganda)
std::uint32_t ifcDefaultColor(const std::string& type);

// Tur bo'yicha jamlama (PTO): soni, hajmi, yuzasi
struct IfcSummaryRow {
    std::string category;
    int count = 0;
    double volume = 0;                // m³
    double barLength = 0, barMass = 0;   // armatura: m, kg
};
std::vector<IfcSummaryRow> ifcSummary(const IfcModel& m);

// Gorizontal kesim (reja): z balandlikdagi tekislik bilan kesish -> elementlar konturlari.
// only — faqat shu indeksli elementlar (bo'sh — hammasi); skipCats — o'tkazib yuboriladigan toifalar ("SPACE"...)
struct IfcSectionLoop {
    int element = -1;                 // IfcModel::elements indeksi
    std::vector<Vec3> pts;            // z = kesim balandligi
    bool closed = false;
};
std::vector<IfcSectionLoop> ifcSection(const IfcModel& m, double z, const std::vector<int>& only = {},
                                       const std::vector<std::string>& skipCats = {"SPACE", "SITE"});

// STEP satridagi \X2\...\X0\, \X\hh, \S\c kodlarini UTF-8 ga
std::string ifcDecodeString(const std::string& s);

} // namespace asiacad::bim
