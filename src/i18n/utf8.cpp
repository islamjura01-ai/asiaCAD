#include <asiacad/i18n/utf8.h>

namespace asiacad::utf8 {

std::u32string decode(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + static_cast<std::size_t>(len) > s.size()) {
            out.push_back(U'�');
            ++i;
            continue;
        }
        char32_t cp = len == 1 ? c : (c & (0xFF >> (len + 1)));
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) {
            out.push_back(U'�');
            ++i;
            continue;
        }
        out.push_back(cp);
        i += static_cast<std::size_t>(len);
    }
    return out;
}

std::string encode(std::u32string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char32_t c : s) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::size_t length(std::string_view s) {
    std::size_t n = 0;
    for (unsigned char ch : s) if ((ch & 0xC0) != 0x80) ++n;
    return n;
}

char32_t toUpper(char32_t c) {
    if (c < 0x80) return (c >= U'a' && c <= U'z') ? c - 0x20 : c;
    // Latin-1
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 0x20;
    if (c == 0xFF) return 0x178;
    // Latin Extended-A (ç ğ ş ı ...)
    if (c == 0x131) return U'I';  // turkcha nuqtasiz ı
    if (c == 0x17F) return U'S';
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return (c & 1) ? c - 1 : c;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c : c - 1;
    // Kirill
    if (c >= 0x430 && c <= 0x44F) return c - 0x20;
    if (c >= 0x450 && c <= 0x45F) return c - 0x50;  // ё ў і ...
    if ((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF)) return (c & 1) ? c - 1 : c;  // ғ қ ң ү ұ ҳ һ
    if (c == 0x4CF) return 0x4C0;
    if (c >= 0x4C1 && c <= 0x4CE) return (c & 1) ? c : c - 1;
    if (c >= 0x4D0 && c <= 0x52F) return (c & 1) ? c - 1 : c;  // ә ө
    return c;
}

std::string toUpper(std::string_view s) {
    auto u = decode(s);
    for (auto& c : u) c = toUpper(c);
    return encode(u);
}

std::string foldKey(std::string_view s) {
    std::u32string out;
    for (char32_t c : decode(s)) {
        switch (c) {
            case U'\'': case U'`': case 0xB4: case 0x2BB: case 0x2BC: case 0x2018: case 0x2019:
                continue;  // apostroflar
            default: break;
        }
        c = toUpper(c);
        if (c == 0x130) c = U'I';  // İ
        out.push_back(c);
    }
    return encode(out);
}

} // namespace asiacad::utf8
