#include <asiacad/i18n/utf8.h>
#include <asiacad/io/dxf_text.h>

#include <cctype>

namespace asiacad::dxftext {

namespace {
#include "codepage_tables.inc"

const char16_t* table(Codepage cp) {
    switch (cp) {
        case Codepage::Cp1252: return kCp1252;
        case Codepage::Cp1251: return kCp1251;
        case Codepage::Cp1254: return kCp1254;
        case Codepage::Cp1250: return kCp1250;
        case Codepage::Cp866: return kCp866;
        case Codepage::Utf8: break;
    }
    return nullptr;
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// s[i] == '\\' bo'lsa va keyin "U+XXXX" kelsa — kod nuqtasi; aks holda 0
char32_t parseUEscape(std::string_view s, std::size_t i) {
    if (i + 6 >= s.size()) return 0;  // \U+XXXX = 7 belgi: s[i..i+6]
    if (s[i] != '\\' || (s[i + 1] != 'U' && s[i + 1] != 'u') || s[i + 2] != '+') return 0;
    char32_t cp = 0;
    for (int k = 3; k < 7; ++k) {
        const int h = hexVal(s[i + static_cast<std::size_t>(k)]);
        if (h < 0) return 0;
        cp = (cp << 4) | static_cast<char32_t>(h);
    }
    return cp;
}

void append(std::string& out, char32_t cp) { out += utf8::encode(std::u32string(1, cp)); }
} // namespace

Codepage fromDxfName(std::string_view n, bool* ok) {
    std::string u;
    for (char c : n) u.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if (ok) *ok = true;
    if (u == "ANSI_1252" || u.empty()) return Codepage::Cp1252;
    if (u == "ANSI_1251") return Codepage::Cp1251;
    if (u == "ANSI_1254") return Codepage::Cp1254;
    if (u == "ANSI_1250") return Codepage::Cp1250;
    if (u == "DOS866" || u == "DOS_866" || u == "ANSI_866") return Codepage::Cp866;
    if (u == "UTF8" || u == "UTF-8") return Codepage::Utf8;
    if (ok) *ok = false;
    return Codepage::Cp1252;
}

std::string_view name(Codepage cp) {
    switch (cp) {
        case Codepage::Utf8: return "UTF-8";
        case Codepage::Cp1252: return "ANSI_1252";
        case Codepage::Cp1251: return "ANSI_1251";
        case Codepage::Cp1254: return "ANSI_1254";
        case Codepage::Cp1250: return "ANSI_1250";
        case Codepage::Cp866: return "DOS866";
    }
    return "?";
}

std::string toUtf8(std::string_view raw, Codepage cp) {
    const char16_t* t = table(cp);
    if (!t) return std::string(raw);
    std::string out;
    out.reserve(raw.size() + raw.size() / 2);
    for (unsigned char c : raw) {
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else append(out, t[c - 0x80]);
    }
    return out;
}

bool encodable(char32_t c, Codepage cp) {
    if (c < 0x80 || cp == Codepage::Utf8) return true;
    const char16_t* t = table(cp);
    for (int i = 0; i < 128; ++i)
        if (t[i] == c && c != 0xFFFD) return true;
    return false;
}

std::string fromUtf8(std::string_view s, Codepage cp) {
    if (cp == Codepage::Utf8) return std::string(s);
    const char16_t* t = table(cp);
    std::string out;
    out.reserve(s.size());
    for (char32_t c : utf8::decode(s)) {
        if (c < 0x80) { out.push_back(static_cast<char>(c)); continue; }
        int hit = -1;
        for (int i = 0; i < 128 && hit < 0; ++i)
            if (t[i] == c && c != 0xFFFD) hit = i;
        if (hit >= 0) {
            out.push_back(static_cast<char>(0x80 + hit));
        } else if (c <= 0xFFFF) {
            static const char* hx = "0123456789ABCDEF";
            out += "\\U+";
            for (int k = 3; k >= 0; --k) out.push_back(hx[(c >> (4 * k)) & 0xF]);
        } else {
            out.push_back('?');
        }
    }
    return out;
}

Codepage chooseCodepage(const std::vector<std::string>& texts) {
    const Codepage cands[] = {Codepage::Cp1252, Codepage::Cp1251, Codepage::Cp1254};
    std::size_t score[3] = {0, 0, 0};
    for (const auto& s : texts)
        for (char32_t c : utf8::decode(s)) {
            if (c < 0x80) continue;
            for (int k = 0; k < 3; ++k)
                if (encodable(c, cands[k])) ++score[k];
        }
    int best = 0;
    for (int k = 1; k < 3; ++k)
        if (score[k] > score[best]) best = k;
    return cands[best];
}

std::string decodeUnicodeEscapes(std::string_view s) {
    if (s.find('\\') == std::string_view::npos) return std::string(s);
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (const char32_t cp = parseUEscape(s, i)) {
            append(out, cp);
            i += 7;
        } else {
            out.push_back(s[i++]);
        }
    }
    return out;
}

std::string decodeTextControlCodes(std::string_view in) {
    const std::string s = decodeUnicodeEscapes(in);
    if (s.find("%%") == std::string::npos) return s;
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '%' && i + 2 < s.size() && s[i + 1] == '%') {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + 2])));
            switch (c) {
                case 'c': append(out, 0x2300); i += 3; continue;  // ⌀ diametr
                case 'd': append(out, 0x00B0); i += 3; continue;  // °
                case 'p': append(out, 0x00B1); i += 3; continue;  // ±
                case '%': out.push_back('%'); i += 3; continue;
                case 'u': case 'o': case 'k': i += 3; continue;   // tagiga/ustiga chizish — ko'rsatilmaydi
                default: break;
            }
            if (i + 4 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 2])) &&
                std::isdigit(static_cast<unsigned char>(s[i + 3])) && std::isdigit(static_cast<unsigned char>(s[i + 4]))) {
                const int code = (s[i + 2] - '0') * 100 + (s[i + 3] - '0') * 10 + (s[i + 4] - '0');
                if (code > 0 && code < 256) append(out, static_cast<char32_t>(code));
                i += 5;
                continue;
            }
        }
        out.push_back(s[i++]);
    }
    return out;
}

std::vector<std::string> mtextToParagraphs(std::string_view s) {
    std::vector<std::string> paras(1);
    auto skipTo = [&](std::size_t i, char end) {
        while (i < s.size() && s[i] != end) ++i;
        return i < s.size() ? i + 1 : i;
    };
    for (std::size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (c == '{' || c == '}') { ++i; continue; }
        if (c != '\\' || i + 1 >= s.size()) { paras.back().push_back(c); ++i; continue; }
        const char k = s[i + 1];
        switch (k) {
            case 'P': case 'N': paras.emplace_back(); i += 2; break;
            case '~': paras.back().push_back(' '); i += 2; break;
            case '\\': case '{': case '}': paras.back().push_back(k); i += 2; break;
            case 'U': case 'u':
                if (const char32_t cp = parseUEscape(s, i)) { append(paras.back(), cp); i += 7; }
                else { paras.back().push_back(k); i += 2; }
                break;
            case 'M': case 'm':  // \M+nXXXX — DBCS (CJK); bizning bozorlar uchun dolzarb emas
                paras.back().push_back('?');
                i = std::min(s.size(), i + 8);
                break;
            case 'S': {  // kasr: \S1^2; -> 1/2
                const std::size_t semi = s.find(';', i + 2);
                const std::size_t end = semi == std::string_view::npos ? s.size() : semi;
                std::string frac(s.substr(i + 2, end - (i + 2)));
                for (auto& ch : frac) if (ch == '^' || ch == '#') ch = '/';
                paras.back() += frac;
                i = semi == std::string_view::npos ? s.size() : semi + 1;
                break;
            }
            case 'A': case 'C': case 'c': case 'f': case 'F': case 'H': case 'h': case 'Q': case 'q':
            case 'T': case 't': case 'W': case 'w': case 'p': case 'X':
                i = skipTo(i + 2, ';');
                break;
            case 'L': case 'l': case 'O': case 'o': case 'K': case 'k':
                i += 2;
                break;
            default:
                paras.back().push_back(k);
                i += 2;
        }
    }
    for (auto& p : paras) p = decodeTextControlCodes(p);
    return paras;
}

} // namespace asiacad::dxftext
