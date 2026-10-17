#pragma once
// AsiaCAD — Excel (.xlsx, Office Open XML) o'qish va yozish, tashqi kutubxonalarsiz.
//  * Yozish: ZIP (stored), inline satrlar, sonlar, formulalar (keshlangan qiymat bilan — Excel ochganda qayta hisoblaydi),
//    qalin sarlavha, ustun kengliklari, sarlavha qatorini qotirish, avtofiltr, raqam formatlari.
//  * O'qish: ZIP (stored + deflate — o'z inflate'imiz), sharedStrings, inlineStr, sonlar, formulalar natijasi.
//    Excel / LibreOffice / Google Sheets saqlagan fayllar o'qiladi.
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace asiacad::xlsx {

struct Cell {
    enum Kind { Empty, Number, Text, Formula } kind = Empty;
    double num = 0;
    std::string text;     // Text — matn; Formula — formula ("=" siz)
    int style = 0;        // 0 oddiy, 1 sarlavha (qalin, fon), 2 "0.00", 3 "0.000", 4 jami (qalin, "0.00"), 5 "0", 6 xato (qizil), 7 ogohlantirish
    static Cell n(double v, int st = 2) { Cell c; c.kind = Number; c.num = v; c.style = st; return c; }
    static Cell s(std::string v, int st = 0) { Cell c; c.kind = Text; c.text = std::move(v); c.style = st; return c; }
    static Cell f(std::string formula, double cached, int st = 2) {
        Cell c; c.kind = Formula; c.text = std::move(formula); c.num = cached; c.style = st; return c;
    }
};

struct Sheet {
    std::string name;
    std::vector<std::vector<Cell>> rows;
    std::vector<double> widths;   // ustun kengligi (belgi), 0 — avto
    bool freezeHeader = true;
    bool autoFilter = true;
    void add(std::vector<Cell> r) { rows.push_back(std::move(r)); }
    // o'qishda
    const Cell* at(std::size_t r, std::size_t c) const { return r < rows.size() && c < rows[r].size() ? &rows[r][c] : nullptr; }
    std::string str(std::size_t r, std::size_t c) const;     // matn yoki son matn ko'rinishida
    std::optional<double> num(std::size_t r, std::size_t c) const;  // son, formula natijasi yoki "1,25" kabi matn
    int column(const std::string& header) const;              // 1-qatordagi sarlavha bo'yicha ustun (-1 — yo'q)
};

struct Workbook {
    std::vector<Sheet> sheets;
    Sheet& add(const std::string& name) { sheets.push_back({}); sheets.back().name = name; return sheets.back(); }
    const Sheet* find(const std::string& name) const;
};

std::string colName(int c);                        // 0 -> "A", 26 -> "AA"
std::string ref(int row, int col);                 // 0-asosli -> "B3"

void save(const Workbook& wb, const std::string& path);    // Error("xlsx.err.write") tashlaydi
std::string toBytes(const Workbook& wb);
Workbook load(const std::string& path);                     // Error("xlsx.err.read") tashlaydi
Workbook fromBytes(const std::string& bytes);

// ZIP (ichki, testlar uchun ochiq)
namespace zip {
std::string build(const std::vector<std::pair<std::string, std::string>>& files);   // stored
std::map<std::string, std::string> extract(const std::string& bytes);
std::string inflate(const std::string& raw);                // raw deflate (RFC 1951)
unsigned crc32(const std::string& data);
} // namespace zip

} // namespace asiacad::xlsx
