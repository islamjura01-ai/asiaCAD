#pragma once
// AsiaCAD — DXF matn kodlashlari va formatlash kodlari.
//  * R2007+ (AC1021+) — UTF-8; undan oldingilari $DWGCODEPAGE bo'yicha (MDH'da odatda ANSI_1251).
//  * Istalgan versiyada \U+XXXX ketma-ketligi.
//  * TEXT: %%c %%d %%p %%% %%u %%o; MTEXT: \P, {\f...;}, \S...; va hokazo.
#include <string>
#include <string_view>
#include <vector>

namespace asiacad::dxftext {

enum class Codepage { Utf8, Cp1252, Cp1251, Cp1254, Cp1250, Cp866 };

// "ANSI_1251", "dos866" ... ; noma'lum bo'lsa ok=false va Cp1252
Codepage fromDxfName(std::string_view name, bool* ok = nullptr);
std::string_view name(Codepage cp);

std::string toUtf8(std::string_view raw, Codepage cp);

// UTF-8 -> kod sahifasi; sahifada yo'q belgilar \U+XXXX bo'lib yoziladi (AutoCAD xulqi)
std::string fromUtf8(std::string_view utf8, Codepage cp);
bool encodable(char32_t c, Codepage cp);
// Matnlardagi ASCII'dan tashqari belgilarni eng ko'p qamraydigan sahifa (1251 / 1254 / 1252)
Codepage chooseCodepage(const std::vector<std::string>& texts);

// \U+XXXX ni ochadi (barcha satrlar uchun: qatlam nomlari, blok nomlari, matn)
std::string decodeUnicodeEscapes(std::string_view utf8);
// TEXT/ATTRIB uchun: \U+ va %% kodlari
std::string decodeTextControlCodes(std::string_view utf8);
// MTEXT: formatlashni olib tashlaydi, paragraflarga ajratadi
std::vector<std::string> mtextToParagraphs(std::string_view utf8);

} // namespace asiacad::dxftext
