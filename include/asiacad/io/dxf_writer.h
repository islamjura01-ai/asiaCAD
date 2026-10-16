#pragma once
// AsiaCAD — DXF R12 (AC1009) ASCII eksport.
// R12 tanlandi: handle talab qilmaydi, AutoCAD/Civil 3D/BricsCAD/QGIS hammasi o'qiydi.
// Kod sahifasi matnga qarab tanlanadi (ANSI_1251 / 1254 / 1252), sahifaga sig'maydigan
// belgilar (masalan, o'zbek қ ғ ҳ) \U+XXXX bo'lib yoziladi — AutoCAD shunday qiladi.
#include <asiacad/core/entity.h>
#include <asiacad/io/dxf_text.h>

#include <ostream>
#include <string>
#include <string_view>

namespace asiacad {

class Document;

class DxfWriter {
public:
    explicit DxfWriter(std::ostream& os) : os_(os) {}

    void writeDocument(const Document& doc);

    // Entity'lar foydalanadigan past darajadagi API
    void group(int code, std::string_view value);
    void group(int code, int value);
    void group(int code, double value);
    void point(int baseCode, const Vec3& p);  // 10/20/30, 11/21/31 ...
    void beginEntity(std::string_view dxfType, const IEntity& e);

    // Faol kod sahifasida (writeDocument tanlaydi)
    std::string encode(std::string_view utf8) const { return dxftext::fromUtf8(utf8, cp_); }
    void setCodepage(dxftext::Codepage cp) { cp_ = cp; }
    dxftext::Codepage codepage() const { return cp_; }
    // Faqat ASCII + \U+XXXX (kod sahifasidan mustaqil)
    static std::string encodeText(std::string_view utf8);

private:
    std::ostream& os_;
    dxftext::Codepage cp_ = dxftext::Codepage::Cp1252;
};

bool saveDxf(const Document& doc, const std::string& path);

} // namespace asiacad
