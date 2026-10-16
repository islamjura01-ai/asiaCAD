#include <asiacad/core/document.h>
#include <asiacad/core/entities.h>
#include <asiacad/io/dxf_writer.h>

#include <format>
#include <fstream>

namespace asiacad {

namespace {
std::string formatReal(double v) {
    if (almostEqual(v, 0.0, 1e-12)) return "0.0";
    std::string s = std::format("{:.10f}", v);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (s.back() == '.') s.push_back('0');
    return s;
}
} // namespace

void DxfWriter::group(int code, std::string_view value) {
    os_ << std::format("{:>3}\n", code) << value << '\n';
}
void DxfWriter::group(int code, int value) { group(code, std::to_string(value)); }
void DxfWriter::group(int code, double value) { group(code, formatReal(value)); }

void DxfWriter::point(int baseCode, const Vec3& p) {
    group(baseCode, p.x);
    group(baseCode + 10, p.y);
    group(baseCode + 20, p.z);
}

void DxfWriter::beginEntity(std::string_view dxfType, const IEntity& e) {
    group(0, dxfType);
    group(8, encode(e.layer()));
    if (e.color() != kColorByLayer) group(62, e.color());
}

std::string DxfWriter::encodeText(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        int len = (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > s.size()) {  // buzilgan UTF-8
            out.push_back('?');
            ++i;
            continue;
        }
        unsigned cp = c & (0xFF >> (len + 1));
        for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out += cp <= 0xFFFF ? std::format("\\U+{:04X}", cp) : std::string("?");
        i += static_cast<std::size_t>(len);
    }
    return out;
}

void DxfWriter::writeDocument(const Document& doc) {
    // HEADER
    group(0, "SECTION");
    group(2, "HEADER");
    group(9, "$ACADVER");
    group(1, "AC1009");
    std::vector<std::string> texts;
    for (const auto& [name, l] : doc.layers()) texts.push_back(name);
    auto collect = [&](const IEntity& e) {
        if (auto* t = dynamic_cast<const TextEntity*>(&e)) texts.push_back(t->text());
        if (auto* i = dynamic_cast<const InsertEntity*>(&e))
            for (const auto& a : i->attributes()) { texts.push_back(a.tag); texts.push_back(a.text.text()); }
    };
    doc.forEach(collect);
    for (const auto& [key, b] : doc.blocks()) {
        texts.push_back(b->name);
        for (const auto& e : b->entities) collect(*e);
    }
    cp_ = dxftext::chooseCodepage(texts);
    group(9, "$DWGCODEPAGE");
    group(3, dxftext::name(cp_));
    const BBox ext = doc.extents(false);
    group(9, "$EXTMIN");
    point(10, ext.valid() ? ext.min : Vec3{});
    group(9, "$EXTMAX");
    point(10, ext.valid() ? ext.max : Vec3{});
    group(0, "ENDSEC");

    // TABLES
    group(0, "SECTION");
    group(2, "TABLES");
    group(0, "TABLE");
    group(2, "LTYPE");
    group(70, 1);
    group(0, "LTYPE");
    group(2, "CONTINUOUS");
    group(70, 0);
    group(3, "Solid line");
    group(72, 65);
    group(73, 0);
    group(40, 0.0);
    group(0, "ENDTAB");

    group(0, "TABLE");
    group(2, "LAYER");
    group(70, static_cast<int>(doc.layers().size()));
    for (const auto& [name, l] : doc.layers()) {
        group(0, "LAYER");
        group(2, encode(name));
        group(70, (l.frozen ? 1 : 0) | (l.locked ? 4 : 0));
        group(62, l.visible ? l.color : -l.color);  // manfiy rang = o'chirilgan qatlam
        group(6, "CONTINUOUS");
    }
    group(0, "ENDTAB");
    group(0, "ENDSEC");

    // BLOCKS
    group(0, "SECTION");
    group(2, "BLOCKS");
    for (const auto& [key, b] : doc.blocks()) {
        group(0, "BLOCK");
        group(8, "0");
        group(2, encode(b->name));
        group(70, b->isAnonymous() ? 1 : 0);
        point(10, b->base);
        group(3, encode(b->name));
        for (const auto& e : b->entities) e->writeDxf(*this);
        group(0, "ENDBLK");
        group(8, "0");
    }
    group(0, "ENDSEC");

    // ENTITIES
    group(0, "SECTION");
    group(2, "ENTITIES");
    doc.forEach([this](const IEntity& e) { e.writeDxf(*this); });
    group(0, "ENDSEC");
    group(0, "EOF");
}

bool saveDxf(const Document& doc, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    DxfWriter w(f);
    w.writeDocument(doc);
    return static_cast<bool>(f);
}

} // namespace asiacad
