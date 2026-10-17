#include <asiacad/io/xlsx.h>

#include <asiacad/core/error.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <sstream>

namespace asiacad::xlsx {

// ================================================================ ZIP
namespace zip {

unsigned crc32(const std::string& data) {
    static unsigned table[256];
    static bool init = false;
    if (!init) {
        for (unsigned i = 0; i < 256; ++i) {
            unsigned c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    unsigned c = 0xFFFFFFFFu;
    for (unsigned char b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

namespace {
void put16(std::string& o, unsigned v) { o += char(v & 0xFF); o += char((v >> 8) & 0xFF); }
void put32(std::string& o, unsigned v) { put16(o, v & 0xFFFF); put16(o, v >> 16); }
unsigned get16(const std::string& s, std::size_t p) {
    if (p + 2 > s.size()) throw Error("xlsx.err.read", {"zip"});
    return static_cast<unsigned char>(s[p]) | (static_cast<unsigned char>(s[p + 1]) << 8);
}
unsigned get32(const std::string& s, std::size_t p) { return get16(s, p) | (get16(s, p + 2) << 16); }

// ---- inflate (RFC 1951), tinf uslubida
struct Bits {
    const std::string& s;
    std::size_t pos = 0;
    unsigned bitbuf = 0;
    int bitcnt = 0;
    int bit() {
        if (bitcnt == 0) {
            if (pos >= s.size()) throw Error("xlsx.err.read", {"inflate"});
            bitbuf = static_cast<unsigned char>(s[pos++]);
            bitcnt = 8;
        }
        const int b = bitbuf & 1;
        bitbuf >>= 1;
        --bitcnt;
        return b;
    }
    unsigned bits(int n) {
        unsigned v = 0;
        for (int i = 0; i < n; ++i) v |= static_cast<unsigned>(bit()) << i;
        return v;
    }
};
struct Huff {
    std::uint16_t counts[16] = {};
    std::uint16_t symbols[320] = {};
    void build(const std::uint8_t* lengths, int n) {
        std::fill(std::begin(counts), std::end(counts), 0);
        for (int i = 0; i < n; ++i) counts[lengths[i]]++;
        counts[0] = 0;
        std::uint16_t offs[16];
        offs[1] = 0;
        for (int i = 1; i < 15; ++i) offs[i + 1] = offs[i] + counts[i];
        for (int i = 0; i < n; ++i)
            if (lengths[i]) symbols[offs[lengths[i]]++] = static_cast<std::uint16_t>(i);
    }
    int decode(Bits& b) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= b.bit();
            const int count = counts[len];
            if (code - count < first) return symbols[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw Error("xlsx.err.read", {"huffman"});
    }
};
const std::uint16_t kLenBase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const std::uint8_t kLenExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const std::uint16_t kDistBase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const std::uint8_t kDistExtra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void inflateBlock(Bits& b, std::string& out, const Huff& lt, const Huff& dt) {
    for (;;) {
        const int sym = lt.decode(b);
        if (sym < 256) { out += static_cast<char>(sym); continue; }
        if (sym == 256) return;
        const int li = sym - 257;
        if (li < 0 || li >= 29) throw Error("xlsx.err.read", {"len"});
        const unsigned len = kLenBase[li] + b.bits(kLenExtra[li]);
        const int di = dt.decode(b);
        if (di < 0 || di >= 30) throw Error("xlsx.err.read", {"dist"});
        const unsigned dist = kDistBase[di] + b.bits(kDistExtra[di]);
        if (dist > out.size()) throw Error("xlsx.err.read", {"dist"});
        const std::size_t from = out.size() - dist;
        for (unsigned i = 0; i < len; ++i) out += out[from + i];
    }
}
} // namespace

std::string inflate(const std::string& raw) {
    Bits b{raw};
    std::string out;
    int final = 0;
    do {
        final = b.bit();
        const unsigned type = b.bits(2);
        if (type == 0) {
            b.bitcnt = 0;  // bayt chegarasiga
            if (b.pos + 4 > raw.size()) throw Error("xlsx.err.read", {"stored"});
            const unsigned len = get16(raw, b.pos);
            b.pos += 4;
            if (b.pos + len > raw.size()) throw Error("xlsx.err.read", {"stored"});
            out.append(raw, b.pos, len);
            b.pos += len;
        } else if (type == 1) {
            std::uint8_t l[288];
            for (int i = 0; i < 144; ++i) l[i] = 8;
            for (int i = 144; i < 256; ++i) l[i] = 9;
            for (int i = 256; i < 280; ++i) l[i] = 7;
            for (int i = 280; i < 288; ++i) l[i] = 8;
            Huff lt, dt;
            lt.build(l, 288);
            std::uint8_t d[30];
            std::fill(std::begin(d), std::end(d), 5);
            dt.build(d, 30);
            inflateBlock(b, out, lt, dt);
        } else if (type == 2) {
            const unsigned hlit = b.bits(5) + 257, hdist = b.bits(5) + 1, hclen = b.bits(4) + 4;
            static const std::uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            std::uint8_t cl[19] = {};
            for (unsigned i = 0; i < hclen; ++i) cl[order[i]] = static_cast<std::uint8_t>(b.bits(3));
            Huff ct;
            ct.build(cl, 19);
            std::uint8_t lens[320] = {};
            for (unsigned n = 0; n < hlit + hdist;) {
                const int sym = ct.decode(b);
                if (sym < 16) lens[n++] = static_cast<std::uint8_t>(sym);
                else {
                    unsigned rep = 0;
                    std::uint8_t val = 0;
                    if (sym == 16) {
                        if (n == 0) throw Error("xlsx.err.read", {"rep"});
                        val = lens[n - 1];
                        rep = 3 + b.bits(2);
                    } else if (sym == 17) rep = 3 + b.bits(3);
                    else rep = 11 + b.bits(7);
                    if (n + rep > hlit + hdist) throw Error("xlsx.err.read", {"rep"});
                    while (rep--) lens[n++] = val;
                }
            }
            Huff lt, dt;
            lt.build(lens, static_cast<int>(hlit));
            dt.build(lens + hlit, static_cast<int>(hdist));
            inflateBlock(b, out, lt, dt);
        } else {
            throw Error("xlsx.err.read", {"block"});
        }
    } while (!final);
    return out;
}

std::string build(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, central;
    for (const auto& [name, data] : files) {
        const unsigned crc = crc32(data), off = static_cast<unsigned>(out.size());
        // local header
        put32(out, 0x04034b50); put16(out, 20); put16(out, 0x0800); put16(out, 0); put16(out, 0); put16(out, 0x21);
        put32(out, crc); put32(out, static_cast<unsigned>(data.size())); put32(out, static_cast<unsigned>(data.size()));
        put16(out, static_cast<unsigned>(name.size())); put16(out, 0);
        out += name;
        out += data;
        // central
        put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0x0800); put16(central, 0);
        put16(central, 0); put16(central, 0x21); put32(central, crc);
        put32(central, static_cast<unsigned>(data.size())); put32(central, static_cast<unsigned>(data.size()));
        put16(central, static_cast<unsigned>(name.size())); put16(central, 0); put16(central, 0); put16(central, 0); put16(central, 0);
        put32(central, 0); put32(central, off);
        central += name;
    }
    const unsigned cdOff = static_cast<unsigned>(out.size());
    out += central;
    put32(out, 0x06054b50); put16(out, 0); put16(out, 0);
    put16(out, static_cast<unsigned>(files.size())); put16(out, static_cast<unsigned>(files.size()));
    put32(out, static_cast<unsigned>(central.size())); put32(out, cdOff); put16(out, 0);
    return out;
}

std::map<std::string, std::string> extract(const std::string& s) {
    // markaziy katalog oxiri
    if (s.size() < 22) throw Error("xlsx.err.read", {"zip"});
    std::size_t eocd = std::string::npos;
    for (std::size_t p = s.size() - 22;; --p) {
        if (get32(s, p) == 0x06054b50) { eocd = p; break; }
        if (p == 0 || s.size() - p > 70000) break;
    }
    if (eocd == std::string::npos) throw Error("xlsx.err.read", {"zip"});
    const unsigned n = get16(s, eocd + 10);
    std::size_t p = get32(s, eocd + 16);
    std::map<std::string, std::string> out;
    for (unsigned i = 0; i < n; ++i) {
        if (get32(s, p) != 0x02014b50) throw Error("xlsx.err.read", {"zip"});
        const unsigned method = get16(s, p + 10), csize = get32(s, p + 20), nlen = get16(s, p + 28), xlen = get16(s, p + 30),
                       clen = get16(s, p + 32), loff = get32(s, p + 42);
        const std::string name = s.substr(p + 46, nlen);
        p += 46 + nlen + xlen + clen;
        if (get32(s, loff) != 0x04034b50) throw Error("xlsx.err.read", {"zip"});
        const std::size_t data = loff + 30 + get16(s, loff + 26) + get16(s, loff + 28);
        if (data + csize > s.size()) throw Error("xlsx.err.read", {"zip"});
        const std::string raw = s.substr(data, csize);
        if (method == 0) out[name] = raw;
        else if (method == 8) out[name] = inflate(raw);
        else throw Error("xlsx.err.read", {"method " + std::to_string(method)});
    }
    return out;
}

} // namespace zip

// ================================================================ yordamchi
std::string colName(int c) {
    std::string s;
    ++c;
    while (c > 0) {
        const int r = (c - 1) % 26;
        s.insert(s.begin(), static_cast<char>('A' + r));
        c = (c - 1) / 26;
    }
    return s;
}
std::string ref(int row, int col) { return colName(col) + std::to_string(row + 1); }

namespace {
std::string esc(const std::string& s) {
    std::string o;
    for (char ch : s) {
        switch (ch) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20 && ch != '\n' && ch != '\t') break;
                o += ch;
        }
    }
    return o;
}
std::string unesc(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { o += s[i]; continue; }
        const std::size_t e = s.find(';', i);
        if (e == std::string::npos) { o += s[i]; continue; }
        const std::string ent = s.substr(i + 1, e - i - 1);
        if (ent == "amp") o += '&';
        else if (ent == "lt") o += '<';
        else if (ent == "gt") o += '>';
        else if (ent == "quot") o += '"';
        else if (ent == "apos") o += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            unsigned long cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? std::stoul(ent.substr(2), nullptr, 16) : std::stoul(ent.substr(1));
            if (cp < 0x80) o += static_cast<char>(cp);
            else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) {
                o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F));
            }
        } else { o += s.substr(i, e - i + 1); }
        i = e;
    }
    return o;
}
std::string numStr(double v) {
    if (!std::isfinite(v)) return "0";
    std::string s = std::format("{:.10g}", v);
    return s;
}
// Sheet nomi: Excel cheklovlari ([]:*?/\ va 31 belgi)
std::string sheetName(const std::string& n, int i) {
    std::string o;
    for (char ch : n)
        if (!std::strchr("[]:*?/\\", ch)) o += ch;
    while (!o.empty() && o.front() == '\'') o.erase(o.begin());  // Excel: apostrof bilan boshlanmaydi/tugamaydi
    while (!o.empty() && o.back() == '\'') o.pop_back();
    if (o.empty()) o = "Sheet" + std::to_string(i + 1);
    // UTF-8 bo'yicha 31 belgigacha
    std::size_t chars = 0, k = 0;
    for (; k < o.size(); ++k) {
        if ((static_cast<unsigned char>(o[k]) & 0xC0) != 0x80) {
            if (chars == 31) break;
            ++chars;
        }
    }
    return o.substr(0, k);
}
} // namespace

const Sheet* Workbook::find(const std::string& name) const {
    for (const auto& s : sheets)
        if (s.name == name) return &s;
    return nullptr;
}

std::string Sheet::str(std::size_t r, std::size_t c) const {
    const Cell* x = at(r, c);
    if (!x) return {};
    if (x->kind == Cell::Text) return x->text;
    if (x->kind == Cell::Number || x->kind == Cell::Formula) return numStr(x->num);
    return {};
}
std::optional<double> Sheet::num(std::size_t r, std::size_t c) const {
    const Cell* x = at(r, c);
    if (!x) return std::nullopt;
    if (x->kind == Cell::Number || x->kind == Cell::Formula) return x->num;
    if (x->kind == Cell::Text) {
        std::string t = x->text;
        t.erase(std::remove_if(t.begin(), t.end(), [](char ch) { return ch == ' ' || ch == '\xA0'; }), t.end());
        std::replace(t.begin(), t.end(), ',', '.');
        if (t.empty()) return std::nullopt;
        try {
            std::size_t used = 0;
            const double v = std::stod(t, &used);
            if (used == t.size()) return v;
        } catch (...) {
        }
    }
    return std::nullopt;
}
int Sheet::column(const std::string& header) const {
    if (rows.empty()) return -1;
    for (std::size_t c = 0; c < rows[0].size(); ++c)
        if (str(0, c) == header) return static_cast<int>(c);
    return -1;
}

// ================================================================ yozish
std::string toBytes(const Workbook& wb) {
    std::vector<std::pair<std::string, std::string>> f;
    std::string ct = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/><Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>)";
    for (std::size_t i = 0; i < wb.sheets.size(); ++i)
        ct += std::format(R"(<Override PartName="/xl/worksheets/sheet{}.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)", i + 1);
    ct += "</Types>";
    f.push_back({"[Content_Types].xml", ct});
    f.push_back({"_rels/.rels", R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/></Relationships>)"});
    f.push_back({"docProps/app.xml", R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties"><Application>AsiaCAD</Application></Properties>)"});
    std::string wbx = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><calcPr calcId="191029" fullCalcOnLoad="1"/><sheets>)";
    std::string rels = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)";
    std::string defined;
    for (std::size_t i = 0; i < wb.sheets.size(); ++i) {
        const std::string nm = sheetName(wb.sheets[i].name, static_cast<int>(i));
        wbx += std::format(R"(<sheet name="{}" sheetId="{}" r:id="rId{}"/>)", esc(nm), i + 1, i + 1);
        rels += std::format(R"(<Relationship Id="rId{}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet{}.xml"/>)", i + 1, i + 1);
        const Sheet& sh = wb.sheets[i];
        if (sh.autoFilter && !sh.rows.empty() && !sh.rows[0].empty())
        {
            std::string q;
            for (char ch : nm) { q += ch; if (ch == '\'') q += '\''; }
            defined += std::format(R"(<definedName name="_xlnm._FilterDatabase" localSheetId="{}" hidden="1">'{}'!$A$1:${}${}</definedName>)", i,
                                   esc(q), colName(static_cast<int>(sh.rows[0].size()) - 1), sh.rows.size());
        }
    }
    wbx += "</sheets>";
    if (!defined.empty()) wbx += "<definedNames>" + defined + "</definedNames>";
    wbx += "</workbook>";
    // calcPr sheets'dan keyin bo'lishi kerak (sxema tartibi)
    {
        const std::string calc = R"(<calcPr calcId="191029" fullCalcOnLoad="1"/>)";
        wbx.erase(wbx.find(calc), calc.size());
        wbx.insert(wbx.find("</workbook>"), calc);
    }
    rels += std::format(R"(<Relationship Id="rId{}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>)",
                        wb.sheets.size() + 1);
    f.push_back({"xl/workbook.xml", wbx});
    f.push_back({"xl/_rels/workbook.xml.rels", rels});
    f.push_back({"xl/styles.xml", R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><numFmts count="2"><numFmt numFmtId="164" formatCode="0.000"/><numFmt numFmtId="165" formatCode="0.0000"/></numFmts><fonts count="4"><font><sz val="11"/><name val="Calibri"/></font><font><b/><sz val="11"/><color rgb="FFFFFFFF"/><name val="Calibri"/></font><font><b/><sz val="11"/><name val="Calibri"/></font><font><sz val="11"/><color rgb="FFC00000"/><name val="Calibri"/></font></fonts><fills count="5"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill><fill><patternFill patternType="solid"><fgColor rgb="FF1F4E79"/><bgColor indexed="64"/></patternFill></fill><fill><patternFill patternType="solid"><fgColor rgb="FFDDEBF7"/><bgColor indexed="64"/></patternFill></fill><fill><patternFill patternType="solid"><fgColor rgb="FFFFF2CC"/><bgColor indexed="64"/></patternFill></fill></fills><borders count="2"><border><left/><right/><top/><bottom/><diagonal/></border><border><left style="thin"><color rgb="FFBFBFBF"/></left><right style="thin"><color rgb="FFBFBFBF"/></right><top style="thin"><color rgb="FFBFBFBF"/></top><bottom style="thin"><color rgb="FFBFBFBF"/></bottom><diagonal/></border></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs><cellXfs count="8"><xf numFmtId="0" fontId="0" fillId="0" borderId="1" xfId="0" applyBorder="1"/><xf numFmtId="0" fontId="1" fillId="2" borderId="1" xfId="0" applyFont="1" applyFill="1" applyBorder="1" applyAlignment="1"><alignment horizontal="center" vertical="center" wrapText="1"/></xf><xf numFmtId="2" fontId="0" fillId="0" borderId="1" xfId="0" applyNumberFormat="1" applyBorder="1"/><xf numFmtId="164" fontId="0" fillId="0" borderId="1" xfId="0" applyNumberFormat="1" applyBorder="1"/><xf numFmtId="2" fontId="2" fillId="3" borderId="1" xfId="0" applyNumberFormat="1" applyFont="1" applyFill="1" applyBorder="1"/><xf numFmtId="1" fontId="0" fillId="0" borderId="1" xfId="0" applyNumberFormat="1" applyBorder="1"/><xf numFmtId="0" fontId="3" fillId="0" borderId="1" xfId="0" applyFont="1" applyBorder="1"/><xf numFmtId="0" fontId="0" fillId="4" borderId="1" xfId="0" applyFill="1" applyBorder="1"/></cellXfs><cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles></styleSheet>)"});
    for (std::size_t i = 0; i < wb.sheets.size(); ++i) {
        const Sheet& sh = wb.sheets[i];
        std::size_t ncol = 0;
        for (const auto& r : sh.rows) ncol = std::max(ncol, r.size());
        std::string x = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)";
        if (!sh.rows.empty() && ncol > 0) x += std::format(R"(<dimension ref="A1:{}"/>)", ref(static_cast<int>(sh.rows.size()) - 1, static_cast<int>(ncol) - 1));
        x += "<sheetViews><sheetView workbookViewId=\"0\"";
        x += i == 0 ? " tabSelected=\"1\">" : ">";
        if (sh.freezeHeader && sh.rows.size() > 1) x += R"(<pane ySplit="1" topLeftCell="A2" activePane="bottomLeft" state="frozen"/>)";
        x += "</sheetView></sheetViews><sheetFormatPr defaultRowHeight=\"15\"/>";
        if (ncol > 0) {
            x += "<cols>";
            for (std::size_t c = 0; c < ncol; ++c) {
                double w = c < sh.widths.size() ? sh.widths[c] : 0;
                if (w <= 0) {  // avto: eng uzun matn
                    std::size_t mx = 6;
                    for (const auto& r : sh.rows)
                        if (c < r.size()) {
                            std::size_t len = 0;
                            const std::string t = r[c].kind == Cell::Text ? r[c].text : numStr(r[c].num);
                            for (unsigned char ch : t) len += (ch & 0xC0) != 0x80;
                            mx = std::max(mx, len);
                        }
                    w = std::min<double>(60, static_cast<double>(mx) + 2);
                }
                x += std::format(R"(<col min="{}" max="{}" width="{}" customWidth="1"/>)", c + 1, c + 1, w);
            }
            x += "</cols>";
        }
        x += "<sheetData>";
        for (std::size_t r = 0; r < sh.rows.size(); ++r) {
            x += std::format(R"(<row r="{}">)", r + 1);
            for (std::size_t c = 0; c < sh.rows[r].size(); ++c) {
                const Cell& cell = sh.rows[r][c];
                const std::string rf = ref(static_cast<int>(r), static_cast<int>(c));
                switch (cell.kind) {
                    case Cell::Empty:
                        if (cell.style) x += std::format(R"(<c r="{}" s="{}"/>)", rf, cell.style);
                        break;
                    case Cell::Number: x += std::format(R"(<c r="{}" s="{}"><v>{}</v></c>)", rf, cell.style, numStr(cell.num)); break;
                    case Cell::Text:
                        x += std::format(R"(<c r="{}" s="{}" t="inlineStr"><is><t xml:space="preserve">{}</t></is></c>)", rf, cell.style, esc(cell.text));
                        break;
                    case Cell::Formula:
                        x += std::format(R"(<c r="{}" s="{}"><f>{}</f><v>{}</v></c>)", rf, cell.style, esc(cell.text), numStr(cell.num));
                        break;
                }
            }
            x += "</row>";
        }
        x += "</sheetData>";
        if (sh.autoFilter && !sh.rows.empty() && ncol > 0)
            x += std::format(R"(<autoFilter ref="A1:{}"/>)", ref(static_cast<int>(sh.rows.size()) - 1, static_cast<int>(ncol) - 1));
        x += R"(<pageMargins left="0.5" right="0.5" top="0.6" bottom="0.6" header="0.3" footer="0.3"/><pageSetup orientation="landscape" paperSize="9"/></worksheet>)";
        f.push_back({std::format("xl/worksheets/sheet{}.xml", i + 1), x});
    }
    return zip::build(f);
}

void save(const Workbook& wb, const std::string& path) {
    const std::string bytes = toBytes(wb);
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) throw Error("xlsx.err.write", {path});
    o.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!o) throw Error("xlsx.err.write", {path});
}

// ================================================================ o'qish
namespace {
// juda oddiy XML skaneri: <tag ...> ... </tag>
struct Tag {
    std::string name;
    std::map<std::string, std::string> attrs;
    bool selfClose = false, closing = false;
};
bool nextTag(const std::string& s, std::size_t& p, Tag& t, std::size_t* start = nullptr) {
    for (;;) {
        p = s.find('<', p);
        if (p == std::string::npos) return false;
        if (s.compare(p, 4, "<!--") == 0) { p = s.find("-->", p); if (p == std::string::npos) return false; continue; }
        if (s.compare(p, 2, "<?") == 0 || s.compare(p, 2, "<!") == 0) { p = s.find('>', p); if (p == std::string::npos) return false; continue; }
        break;
    }
    if (start) *start = p;
    const std::size_t e = s.find('>', p);
    if (e == std::string::npos) return false;
    std::string body = s.substr(p + 1, e - p - 1);
    p = e + 1;
    t = {};
    if (!body.empty() && body[0] == '/') { t.closing = true; body.erase(0, 1); }
    if (!body.empty() && body.back() == '/') { t.selfClose = true; body.pop_back(); }
    std::size_t i = 0;
    while (i < body.size() && !std::isspace(static_cast<unsigned char>(body[i]))) ++i;
    t.name = body.substr(0, i);
    if (const auto c = t.name.find(':'); c != std::string::npos) t.name = t.name.substr(c + 1);  // nomlar fazosi prefiksi
    while (i < body.size()) {
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i;
        const std::size_t eq = body.find('=', i);
        if (eq == std::string::npos) break;
        std::string k = body.substr(i, eq - i);
        while (!k.empty() && std::isspace(static_cast<unsigned char>(k.back()))) k.pop_back();
        std::size_t q = eq + 1;
        while (q < body.size() && std::isspace(static_cast<unsigned char>(body[q]))) ++q;
        if (q >= body.size()) break;
        const char quote = body[q];
        const std::size_t qe = body.find(quote, q + 1);
        if (qe == std::string::npos) break;
        if (const auto c = k.find(':'); c != std::string::npos && k.substr(0, c) != "r") k = k.substr(c + 1);
        t.attrs[k] = unesc(body.substr(q + 1, qe - q - 1));
        i = qe + 1;
    }
    return true;
}
std::string textUntil(const std::string& s, std::size_t& p, const std::string& closeName) {
    // <t> ichidagi matn (ichma-ich <r><t> lar ham yig'iladi)
    std::string out;
    Tag t;
    std::size_t st = 0;
    std::size_t last = p;
    bool inT = false;
    while (nextTag(s, p, t, &st)) {
        if (inT) out += unesc(s.substr(last, st - last));
        if (t.name == "t" && !t.closing && !t.selfClose) inT = true;
        else if (t.name == "t" && t.closing) inT = false;
        else if (t.name == "rPh" && !t.closing && !t.selfClose) {  // fonetik — o'tkazib yuborish
            const auto e = s.find("</", p);
            p = s.find("rPh>", e);
            p = p == std::string::npos ? s.size() : p + 4;
        }
        if (t.name == closeName && t.closing) break;
        last = p;
    }
    return out;
}
int colIndex(const std::string& r) {
    int c = 0;
    for (char ch : r) {
        if (ch >= 'A' && ch <= 'Z') c = c * 26 + (ch - 'A' + 1);
        else break;
    }
    return c - 1;
}
int rowIndex(const std::string& r) {
    std::size_t i = 0;
    while (i < r.size() && std::isalpha(static_cast<unsigned char>(r[i]))) ++i;
    try { return std::stoi(r.substr(i)) - 1; } catch (...) { return -1; }
}
std::string resolve(const std::string& base, const std::string& target) {
    if (!target.empty() && target[0] == '/') return target.substr(1);
    std::string dir = base.substr(0, base.rfind('/') + 1);
    std::string t = target;
    while (t.rfind("../", 0) == 0) {
        t.erase(0, 3);
        if (!dir.empty()) dir.pop_back();
        dir = dir.substr(0, dir.rfind('/') + 1);
    }
    return dir + t;
}
} // namespace

Workbook fromBytes(const std::string& bytes) {
    const auto files = zip::extract(bytes);
    auto get = [&](const std::string& n) -> const std::string* {
        auto it = files.find(n);
        return it == files.end() ? nullptr : &it->second;
    };
    const std::string* wbx = get("xl/workbook.xml");
    if (!wbx) throw Error("xlsx.err.read", {"workbook.xml"});
    // rels
    std::map<std::string, std::string> rid;
    if (const std::string* rels = get("xl/_rels/workbook.xml.rels")) {
        std::size_t p = 0;
        Tag t;
        while (nextTag(*rels, p, t))
            if (t.name == "Relationship") rid[t.attrs["Id"]] = resolve("xl/workbook.xml", t.attrs["Target"]);
    }
    // shared strings
    std::vector<std::string> shared;
    if (const std::string* ss = get("xl/sharedStrings.xml")) {
        std::size_t p = 0;
        Tag t;
        while (nextTag(*ss, p, t))
            if (t.name == "si" && !t.closing && !t.selfClose) shared.push_back(textUntil(*ss, p, "si"));
            else if (t.name == "si" && t.selfClose) shared.emplace_back();
    }
    Workbook wb;
    std::size_t p = 0;
    Tag t;
    while (nextTag(*wbx, p, t)) {
        if (t.name != "sheet" || t.closing) continue;
        Sheet sh;
        sh.name = t.attrs["name"];
        std::string path = rid.count(t.attrs["r:id"]) ? rid[t.attrs["r:id"]] : rid[t.attrs["id"]];
        const std::string* x = get(path);
        if (!x) { wb.sheets.push_back(sh); continue; }
        std::size_t q = 0;
        Tag c;
        int curRow = -1;
        while (nextTag(*x, q, c)) {
            if (c.name == "row" && !c.closing) {
                curRow = c.attrs.count("r") ? std::stoi(c.attrs["r"]) - 1 : curRow + 1;
                if (c.selfClose) continue;
            } else if (c.name == "c" && !c.closing) {
                const std::string r = c.attrs["r"];
                const int ci = r.empty() ? (curRow >= 0 && static_cast<std::size_t>(curRow) < sh.rows.size() ? static_cast<int>(sh.rows[static_cast<std::size_t>(curRow)].size()) : 0) : colIndex(r);
                const int ri = r.empty() ? curRow : rowIndex(r);
                if (ri < 0 || ci < 0) continue;
                Cell cell;
                const std::string type = c.attrs.count("t") ? c.attrs["t"] : "n";
                if (!c.selfClose) {
                    std::string v, f, inl;
                    bool hasV = false;
                    Tag k;
                    std::size_t st = 0;
                    while (nextTag(*x, q, k, &st)) {
                        if (k.name == "c" && k.closing) break;
                        if (k.name == "v" && !k.closing && !k.selfClose) {
                            const std::size_t e = x->find("</", q);
                            v = unesc(x->substr(q, e - q));
                            hasV = true;
                        } else if (k.name == "f" && !k.closing && !k.selfClose) {
                            const std::size_t e = x->find("</", q);
                            f = unesc(x->substr(q, e - q));
                        } else if (k.name == "is" && !k.closing) {
                            inl = textUntil(*x, q, "is");
                        }
                    }
                    if (type == "s" && hasV) {
                        const std::size_t idx = static_cast<std::size_t>(std::stoul(v));
                        cell = Cell::s(idx < shared.size() ? shared[idx] : std::string());
                    } else if (type == "inlineStr") {
                        cell = Cell::s(inl);
                    } else if (type == "str" || type == "e") {
                        cell = Cell::s(v);
                    } else if (type == "b") {
                        cell = Cell::n(v == "1" ? 1 : 0);
                    } else if (hasV) {
                        try { cell = Cell::n(std::stod(v)); } catch (...) { cell = Cell::s(v); }
                    }
                    if (!f.empty() && (cell.kind == Cell::Number || cell.kind == Cell::Empty)) {
                        const double cached = cell.num;
                        cell = Cell::f(f, cached);
                    }
                }
                cell.style = 0;
                auto& rows = sh.rows;
                if (rows.size() <= static_cast<std::size_t>(ri)) rows.resize(static_cast<std::size_t>(ri) + 1);
                auto& row = rows[static_cast<std::size_t>(ri)];
                if (row.size() <= static_cast<std::size_t>(ci)) row.resize(static_cast<std::size_t>(ci) + 1);
                row[static_cast<std::size_t>(ci)] = cell;
            }
        }
        wb.sheets.push_back(std::move(sh));
    }
    return wb;
}

Workbook load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error("xlsx.err.read", {path});
    std::stringstream ss;
    ss << f.rdbuf();
    return fromBytes(ss.str());
}

} // namespace asiacad::xlsx
