#pragma once
// AsiaCAD — UTF-8 yordamchilari: dekodlash, katta harfga o'tkazish, buyruq kalitini normallashtirish.
// Qamrov: ASCII, Latin-1, Latin Extended-A (turk, o'zbek lotin), kirill + kengaytmasi (ўқғҳ, әғқңөұүһі, ңөү).
#include <string>
#include <string_view>

namespace asiacad::utf8 {

std::u32string decode(std::string_view s);  // buzilgan bayt -> U+FFFD
std::string encode(std::u32string_view s);
std::size_t length(std::string_view s);

char32_t toUpper(char32_t c);
std::string toUpper(std::string_view s);

// Buyruq va kalit so'zlarni solishtirish uchun kalit:
//  - katta harf;
//  - turkcha İ/ı/i -> I (tilga qarab "LİNE" ≠ "LINE" muammosi bo'lmasin);
//  - apostroflar (' ` ´ ʻ ʼ ‘ ’) olib tashlanadi: KOʻCHIR = KO'CHIR = KOCHIR.
std::string foldKey(std::string_view s);

} // namespace asiacad::utf8
