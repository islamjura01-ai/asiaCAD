#pragma once
// AsiaCAD — genplan va shahar belgilari kutubxonasi (rangli, haqiqiy o'lchamda, metrda; tayanch — markaz).
// Har belgi "SYM_<KALIT>" nomli blok; chizmaga INSERT bilan qo'yiladi (SYMBOL buyrug'i, Belgilar palitrasi).
// Haydash (Drive) sahnasida 3D o'rinbosar bilan ko'rinadi (daraxt, avtomobil, chiroq, bekat, ...).
#include <asiacad/core/block.h>

#include <string>
#include <vector>

namespace asiacad::civil {

struct SymbolInfo {
    const char* key;          // "TREE"; i18n: sym.<key>
    const char* category;     // "GREEN", "TRANSPORT", "STREET", "PEOPLE", "SIGNS"; i18n: sym.cat.<cat>
};
const std::vector<SymbolInfo>& symbolCatalog();
// Blok ta'rifi (yangi nusxa). Noma'lum kalit — nullptr
BlockPtr makeSymbol(const std::string& key);
std::string symbolBlockName(const std::string& key);   // "SYM_TREE"
// Blok nomidan kalit ("SYM_TREE" -> "TREE"); belgi bo'lmasa bo'sh
std::string symbolKeyOf(const std::string& blockName);

} // namespace asiacad::civil
