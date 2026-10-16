#pragma once
// AsiaCAD — minimal JSON (RFC 8259) parser. Lug'atlar va sozlamalar uchun.
// Obyekt kalitlari fayldagi tartibni saqlaydi (help va UI ro'yxatlari shu tartibda chiqadi).
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace asiacad::json {

struct Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

struct Value {
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v = nullptr;

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(v); }
    bool isString() const { return std::holds_alternative<std::string>(v); }
    bool isArray() const { return std::holds_alternative<Array>(v); }
    bool isObject() const { return std::holds_alternative<Object>(v); }
    bool isNumber() const { return std::holds_alternative<double>(v); }
    bool isBool() const { return std::holds_alternative<bool>(v); }

    const std::string& str() const { return std::get<std::string>(v); }
    const Array& arr() const { return std::get<Array>(v); }
    const Object& obj() const { return std::get<Object>(v); }
    double num() const { return std::get<double>(v); }
    bool boolean() const { return std::get<bool>(v); }

    // Obyektdan kalit bo'yicha (topilmasa nullptr)
    const Value* find(std::string_view key) const;
};

class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& msg, std::size_t line, std::size_t col)
        : std::runtime_error(msg + " (" + std::to_string(line) + ":" + std::to_string(col) + ")"), line_(line), col_(col) {}
    std::size_t line() const { return line_; }
    std::size_t column() const { return col_; }

private:
    std::size_t line_, col_;
};

Value parse(std::string_view text);

} // namespace asiacad::json
