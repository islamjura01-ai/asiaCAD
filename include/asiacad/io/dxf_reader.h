#pragma once
// AsiaCAD — ASCII DXF import (R12 ... R2018).
//
// Qo'llab-quvvatlanadi:
//   LINE, POINT, CIRCLE, ARC, LWPOLYLINE (bulge), POLYLINE 2D/3D (bulge), TEXT (tekislash), MTEXT,
//   ELLIPSE, SPLINE (NURBS), 3DFACE, SOLID/TRACE, INSERT/MINSERT (ichma-ich, ATTRIB), DIMENSION (blok geometriyasi).
//   OCS (extrusion) — ±Z aniq; boshqa normallar 3D poliliniyaga aylanadi.
//   Kodlash: R2007+ UTF-8, oldingilar $DWGCODEPAGE (ANSI_1251/1252/1254/1250, DOS866), \U+XXXX.
//   HATCH (poliliniya va qirra yo'llari: chiziq, yoy, ellips, splayn), blok ta'riflari va INSERT (explode'siz).
// Hozircha yo'q: LEADER/MLEADER, mesh/polyface, IMAGE, 3DSOLID — hisobotda "skipped".
//
// Import bitta undo qadami: makeImportCommand() natijasini CommandStack orqali bajaring.
#include <asiacad/core/commands.h>
#include <asiacad/core/error.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace asiacad {

using DxfMessage = Message;  // lug'at kaliti: "dxf.warn.*"

struct DxfImportOptions {
    bool includePaperSpace = false;
    double maxArcStep = deg2rad(5.0);  // ellips/splayn/yoyni bo'lishdagi maksimal burchak qadami
    int maxBlockDepth = 16;
    bool explodeInserts = false;  // true — bloklar ta'rif sifatida emas, geometriya bo'lib keladi
};

// Model fazosidagi o'lcham (DIMENSION) ma'lumoti: o'lchangan nuqtalar, uslub va DIMLFAC (chizma masshtabi bo'yicha
// ko'rsatiladigan qiymat = o'lchangan × lfac). KJ varaqlarida har ko'rinish o'z masshtabida chiziladi (M200 — lfac 2).
struct DxfDimInfo {
    Vec3 p1, p2, loc;      // 13, 14, 10 (WCS)
    std::string style;     // 3
    std::string text;      // 1 (bo'sh yoki "<>" — o'lchov)
    double lfac = 1.0;     // uslubdan yoki XDATA DSTYLE (144) dan
    int kind = 0;          // 70 & 7: 0 — chiziqli, 1 — tekislangan ...
};

struct DxfImportResult {
    std::string version;   // "AC1015"
    std::string release;   // "R2000"
    std::string encoding;  // "UTF-8", "ANSI_1251", ...
    int insUnits = 0;      // $INSUNITS: 0 — birliksiz, 4 — mm, 6 — m ...
    std::vector<Layer> layers;
    std::vector<std::unique_ptr<IEntity>> entities;
    std::vector<std::shared_ptr<BlockDef>> blocks;  // ishlatilgan blok ta'riflari (ichkilari avval)
    std::map<std::string, std::size_t> imported;  // AsiaCAD turi -> soni
    std::map<std::string, std::size_t> skipped;   // DXF turi -> soni
    std::vector<DxfMessage> warnings;
    std::vector<DxfDimInfo> dims;                    // model fazosidagi o'lchamlar
    std::map<std::string, double> dimStyleLfac;      // UPPER(uslub) -> DIMLFAC
    double medianTextHeight = 0;                     // birlikni aniqlash uchun (mm chizmada ~ 250..500)
};

// Xatoda asiacad::Error ("dxf.err.*") tashlaydi
DxfImportResult readDxf(std::string_view content, const DxfImportOptions& opt = {});
DxfImportResult readDxfFile(const std::string& path, const DxfImportOptions& opt = {});

// Hujjatda yo'q qatlamlar + bloklar + barcha obyektlar -> bitta CompositeCommand. result.entities bo'shatiladi.
// Hujjatda shu nomli blok bo'lsa, import qilinayotgani "NOM$1" kabi qayta nomlanadi.
std::unique_ptr<CompositeCommand> makeImportCommand(const Document& doc, DxfImportResult& result,
                                                    std::size_t* newLayers = nullptr);

std::string_view dxfReleaseName(std::string_view acadver);
std::string_view insUnitsName(int code);

} // namespace asiacad
