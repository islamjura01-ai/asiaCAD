#pragma once
// AsiaCAD — modullar va Excel o'rtasida ikki tomonlama almashuv.
//  Eksport: kiritiladigan jadvallar (tahrirlanadi) + natijalar + jonli formulalar (uzunlik, massa, jami, SUMIF) + tekshiruv.
//  Import: tahrirlangan kirish ustunlari va parametrlar qaytib o'qiladi, model qayta hisoblanadi.
// Varaq nomlari UI tilida; import avval shu tildagi nom, keyin tartib raqami bo'yicha qidiradi.
// Parametr varaqlarida A ustuni — mashina kaliti (tilga bog'liq emas).
#include <asiacad/bim/rebar.h>
#include <asiacad/civil/road.h>
#include <asiacad/civil/waterway.h>
#include <asiacad/civil/sewer.h>
#include <asiacad/civil/utility.h>
#include <asiacad/io/xlsx.h>

#include <functional>

namespace asiacad::excel {

using Tr = std::function<std::string(const std::string& key, const std::vector<std::string>& args)>;

xlsx::Workbook sewerWorkbook(const civil::SewerNetwork& n, const civil::SewerResult& r, const Tr& tr);
void sewerFromWorkbook(const xlsx::Workbook& wb, civil::SewerNetwork& n, const Tr& tr);

xlsx::Workbook buildingWorkbook(const bim::BuildingParams& p, const std::map<std::string, bim::ElemKind>& layers,
                                const bim::BuildingModel& m, const Tr& tr);
void buildingFromWorkbook(const xlsx::Workbook& wb, bim::BuildingParams& p, std::map<std::string, bim::ElemKind>& layers, const Tr& tr);

// Armatura kitobi bino varaqlarini ham o'z ichiga oladi — bitta fayl ikkala menyuni boshqaradi
xlsx::Workbook rebarWorkbook(const bim::BuildingParams& p, const std::map<std::string, bim::ElemKind>& layers,
                             const bim::BuildingModel& m, const bim::RebarSettings& rs, const bim::RebarModel& rm, const Tr& tr);
void rebarFromWorkbook(const xlsx::Workbook& wb, bim::BuildingParams& p, std::map<std::string, bim::ElemKind>& layers,
                       bim::RebarSettings& rs, const Tr& tr);

// Yer osti tarmoqlari: Tarmoqlar (tur, Ø — tahrirlanadi), Nuqtalar (Z — tahrirlanadi), Kesishmalar, Hajmlar, Tekshiruv
xlsx::Workbook utilityWorkbook(const civil::UtilModel& m, const civil::UtilParams& p, const Tr& tr);
void utilityFromWorkbook(const xlsx::Workbook& wb, civil::UtilModel& m, const civil::UtilParams& p, const Tr& tr);

// Yo'l: Piketlar (loyiha belgisi tahrirlanadi; ishchi belgi, nishablik, hajmlar — formulalar), Hajmlar, Parametrlar, Tekshiruv
xlsx::Workbook roadWorkbook(const civil::RoadModel& m, const civil::RoadParams& p, const Tr& tr);
void roadFromWorkbook(const xlsx::Workbook& wb, civil::RoadModel& m, civil::RoadParams& p, const Tr& tr);
// Suv yo'li: piketlar (tub belgisi tahrirlanadi), hajmlar, parametrlar (kesim, sarf), tekshiruv
xlsx::Workbook wwWorkbook(const civil::WwModel& m, const civil::WwParams& p, const Tr& tr);
void wwFromWorkbook(const xlsx::Workbook& wb, civil::WwModel& m, civil::WwParams& p, const Tr& tr);

} // namespace asiacad::excel
