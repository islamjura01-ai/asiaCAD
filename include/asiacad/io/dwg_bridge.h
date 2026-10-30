#pragma once
// AsiaCAD — DWG ochish/saqlash tashqi konverter orqali (DWG formati yopiq; FreeCAD/QGIS kabi):
//   1) ODA File Converter (bepul, opendesign.com) — "ODAFileConverter" (Windows: C:\Program Files\ODA\ODAFileConverter*\)
//   2) LibreDWG — "dwg2dxf" / "dxf2dwg"
// Yo'lni ASIACAD_DWG_CONVERTER muhit o'zgaruvchisi bilan aniq berish mumkin.
// DWG -> vaqtinchalik DXF -> AsiaCAD DXF o'quvchisi; saqlash: DXF yoziladi -> DWG ga aylantiriladi.
#include <string>

namespace asiacad::dwg {

struct Converter {
    enum Kind { None, Oda, LibreDwg } kind = None;
    std::string exe;      // ODAFileConverter yoki dwg2dxf
    std::string exe2;     // LibreDWG: dxf2dwg
};

Converter find();
// DWG -> DXF (vaqtinchalik fayl yo'li). Xatolar: Error("dwg.err.noconv"), Error("dwg.err.convert", {path})
std::string toDxf(const std::string& dwgPath);
// DXF -> DWG (version: ACAD2018, ACAD2013, ACAD2010, ACAD2007, ACAD2004, ACAD2000)
void fromDxf(const std::string& dxfPath, const std::string& dwgPath, const std::string& version = "ACAD2018");
std::string converterName(const Converter& c);   // "ODA File Converter" | "LibreDWG" | ""

} // namespace asiacad::dwg
