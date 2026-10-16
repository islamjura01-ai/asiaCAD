#pragma once
// AsiaCAD — native .acad format (JSON, v1).
//
// {
//   "format": "asiacad", "version": 1, "generator": "AsiaCAD 0.10", "nextId": 42,
//   "layers":   [ {"name":"0","color":7,"visible":true,"locked":false,"frozen":false}, ... ],
//   "blocks":   [ {"name":"TREE","base":[1,1,0],"entities":[ ...obyektlar, id'siz... ]}, ... ],  // ichkilari avval
//   "entities": [
//     {"id":1,"type":"LINE","layer":"0","color":256,"a":[0,0,0],"b":[10,0,0]},
//     ...                                   // bitta obyekt — bitta qator (git diff uchun qulay)
//   ]
// }
//
// Geometriya: POINT p | LINE a b | CIRCLE c r | ARC c r start end (radian, CCW)
//             POLYLINE pts [bulges] closed | TEXT ins h rot text halign(L|C|R) valign(B|Bot|M|T)
//             INSERT block m[a,b,c,d,tx,ty,sz,tz] [attribs: {tag, layer, color, TEXT maydonlari}]
//             HATCH pattern scale angle loops[{pts, [bulges]}]
// Sonlar eng qisqa aniq ko'rinishda (std::to_chars) — yozib-o'qishda bit-bit bir xil.
// Moslik: noma'lum maydonlar e'tiborsiz qoldiriladi; noma'lum tur — ogohlantirish bilan o'tkaziladi;
//         "version" kattaroq bo'lsa — xato (eski dastur yangi faylni buzmasin).
#include <asiacad/core/document.h>
#include <asiacad/core/error.h>

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace asiacad {

inline constexpr int kAcadFormatVersion = 1;

struct AcadLoadResult {
    Document doc;
    std::vector<Message> warnings;
};

void writeAcad(const Document& doc, std::ostream& os);
// Atomar: avval <fayl>.tmp ga yoziladi, so'ng almashtiriladi; eski fayl <fayl>.bak bo'lib qoladi.
void saveAcad(const Document& doc, const std::string& path, bool keepBackup = true);

// Xatoda Error ("acad.err.*") tashlaydi
AcadLoadResult readAcad(std::string_view json);
AcadLoadResult loadAcad(const std::string& path);

} // namespace asiacad
