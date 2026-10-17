#pragma once
// AsiaCAD — chizmalar to'plami: bir nechta DXF/.acad faylni bitta loyiha sifatida yuklash.
//  Har fayl: vazifasi (role — modulga bog'liq kalit), qavat oralig'i, masshtab (mm/sm/m — avto), siljitish (dx, dy).
//  Yer osti tarmoqlari: har tarmoq alohida chizmada (role = WATER/SEWER/... yoki AUTO).
//  Bino: qavat planlari / qolip chizmalari (role = PLAN, FOUND), qavat oralig'i bilan.
//  Armatura: KJ chizmalari (role = SLAB/WALL/COLUMN/FOUND), qavat oralig'i bilan.
#include <asiacad/bim/building.h>
#include <asiacad/core/document.h>

#include <memory>
#include <string>
#include <vector>

namespace asiacad::civil {

struct SheetFile {
    std::string path;
    std::string role = "AUTO";
    int floorFrom = 1, floorTo = 1;
    double scale = 0;            // 0 — avto ($INSUNITS yoki o'lcham bo'yicha), 0.001 — mm, 1 — m
    double dx = 0, dy = 0;       // metrda, masshtabdan keyin
    bool enabled = true;
};

struct LoadedSheet {
    SheetFile file;
    std::shared_ptr<Document> doc;  // metrga keltirilgan, bloklar explode qilingan
    double scaleUsed = 1;
    std::string error;              // bo'sh — muvaffaqiyatli
};

// Faylni yuklash (xato bo'lsa error to'ldiriladi, doc bo'sh hujjat)
LoadedSheet loadSheet(const SheetFile& f);
std::vector<LoadedSheet> loadSheets(const std::vector<SheetFile>& files);
// Avto masshtab: $INSUNITS (4 mm, 5 sm, 6 m), aks holda chizma kengligi > 2000 birlik — mm
double autoScale(int insUnits, const BBox& extents);
std::string fileName(const std::string& path);

// Bino: role "FOUND" — fundament plani, "PLAN" — qavat plani/qolip chizmasi (floorFrom..floorTo).
// Belgi ("+3.300") faqat oraliqning birinchi qavatiga qo'llanadi; qavat qatlamlari nomidan avto taniladi.
bim::FloorPlans floorPlansFrom(const std::vector<LoadedSheet>& sheets);

std::string sheetsToJson(const std::vector<SheetFile>& s);
std::vector<SheetFile> sheetsFromJson(const std::string& text);

} // namespace asiacad::civil
