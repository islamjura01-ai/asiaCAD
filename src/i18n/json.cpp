#include <asiacad/i18n/json.h>
#include <asiacad/i18n/utf8.h>

#include <charconv>
#include <cstdlib>

namespace asiacad::json {

const Value* Value::find(std::string_view key) const {
    if (!isObject()) return nullptr;
    for (const auto& [k, val] : obj())
        if (k == key) return &val;
    return nullptr;
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view t) : t_(t) {
        // UTF-8 BOM (Windows Notepad) ni o'tkazib yuborish
        if (t_.size() >= 3 && t_.substr(0, 3) == "\xEF\xBB\xBF") i_ = 3;
    }

    Value parseRoot() {
        Value v = parseValue(0);
        skipWs();
        if (i_ != t_.size()) fail("Ortiqcha belgilar");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& m) const {
        std::size_t line = 1, col = 1;
        for (std::size_t k = 0; k < i_ && k < t_.size(); ++k) {
            if (t_[k] == '\n') { ++line; col = 1; } else ++col;
        }
        throw ParseError(m, line, col);
    }
    void skipWs() {
        while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\t' || t_[i_] == '\n' || t_[i_] == '\r')) ++i_;
    }
    bool consume(std::string_view lit) {
        if (t_.substr(i_, lit.size()) == lit) { i_ += lit.size(); return true; }
        return false;
    }

    Value parseValue(int depth) {
        if (depth > 256) fail("Juda chuqur ichma-ichlik");
        skipWs();
        if (i_ >= t_.size()) fail("Kutilmagan fayl oxiri");
        const char c = t_[i_];
        Value v;
        if (c == '{') v.v = parseObject(depth);
        else if (c == '[') v.v = parseArray(depth);
        else if (c == '"') v.v = parseString();
        else if (consume("true")) v.v = true;
        else if (consume("false")) v.v = false;
        else if (consume("null")) v.v = nullptr;
        else if (c == '-' || (c >= '0' && c <= '9')) v.v = parseNumber();
        else fail(std::string("Kutilmagan belgi '") + c + "'");
        return v;
    }

    Object parseObject(int depth) {
        Object o;
        ++i_;
        skipWs();
        if (i_ < t_.size() && t_[i_] == '}') { ++i_; return o; }
        for (;;) {
            skipWs();
            if (i_ >= t_.size() || t_[i_] != '"') fail("Kalit (\"...\") kutilgan");
            std::string k = parseString();
            skipWs();
            if (i_ >= t_.size() || t_[i_] != ':') fail("':' kutilgan");
            ++i_;
            o.emplace_back(std::move(k), parseValue(depth + 1));
            skipWs();
            if (i_ < t_.size() && t_[i_] == ',') { ++i_; continue; }
            if (i_ < t_.size() && t_[i_] == '}') { ++i_; return o; }
            fail("',' yoki '}' kutilgan");
        }
    }

    Array parseArray(int depth) {
        Array a;
        ++i_;
        skipWs();
        if (i_ < t_.size() && t_[i_] == ']') { ++i_; return a; }
        for (;;) {
            a.push_back(parseValue(depth + 1));
            skipWs();
            if (i_ < t_.size() && t_[i_] == ',') { ++i_; continue; }
            if (i_ < t_.size() && t_[i_] == ']') { ++i_; return a; }
            fail("',' yoki ']' kutilgan");
        }
    }

    unsigned hex4() {
        if (i_ + 4 > t_.size()) fail("\\u dan keyin 4 ta hex raqam kerak");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = t_[i_++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') v |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= static_cast<unsigned>(h - 'A' + 10);
            else fail("Noto'g'ri hex raqam");
        }
        return v;
    }

    std::string parseString() {
        ++i_;  // "
        std::string out;
        for (;;) {
            if (i_ >= t_.size()) fail("Satr yopilmagan");
            const char c = t_[i_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("Satr ichida boshqaruv belgisi");
            if (c != '\\') { out.push_back(c); continue; }
            if (i_ >= t_.size()) fail("Satr yopilmagan");
            switch (const char e = t_[i_++]) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    char32_t cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF) {  // surrogate juftligi
                        if (!consume("\\u")) fail("Juft surrogate kutilgan");
                        const unsigned lo = hex4();
                        if (lo < 0xDC00 || lo > 0xDFFF) fail("Noto'g'ri past surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        fail("Yolg'iz past surrogate");
                    }
                    out += utf8::encode(std::u32string(1, cp));
                    break;
                }
                default: fail(std::string("Noma'lum escape \\") + e);
            }
        }
    }

    double parseNumber() {
        const std::size_t start = i_;
        if (t_[i_] == '-') ++i_;
        auto digits = [&] {
            const std::size_t s = i_;
            while (i_ < t_.size() && t_[i_] >= '0' && t_[i_] <= '9') ++i_;
            return i_ - s;
        };
        if (digits() == 0) fail("Raqam kutilgan");
        if (i_ < t_.size() && t_[i_] == '.') { ++i_; if (digits() == 0) fail("Nuqtadan keyin raqam kutilgan"); }
        if (i_ < t_.size() && (t_[i_] == 'e' || t_[i_] == 'E')) {
            ++i_;
            if (i_ < t_.size() && (t_[i_] == '+' || t_[i_] == '-')) ++i_;
            if (digits() == 0) fail("Daraja ko'rsatkichi kutilgan");
        }
        // from_chars — tizim lokaliga bog'liq emas (ru_RU da ham "1.5" to'g'ri o'qiladi)
        const std::string num(t_.substr(start, i_ - start));
        double d = 0;
        auto [p, ec] = std::from_chars(num.data(), num.data() + num.size(), d);
        if (ec != std::errc{}) fail("Son diapazondan tashqarida");
        return d;
    }

    std::string_view t_;
    std::size_t i_ = 0;
};

} // namespace

Value parse(std::string_view text) { return Parser(text).parseRoot(); }

} // namespace asiacad::json
