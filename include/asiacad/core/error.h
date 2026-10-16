#pragma once
// AsiaCAD — tilga bog'liq bo'lmagan xato: kalit + argumentlar.
// Core hech qachon tayyor matn chiqarmaydi; tarjimani UI qatlami (Localizer) qiladi.
#include <stdexcept>
#include <string>
#include <vector>

namespace asiacad {

// Tarjima qilinadigan xabar (ogohlantirishlar va hisobotlar uchun)
struct Message {
    std::string key;
    std::vector<std::string> args;
};

class Error : public std::invalid_argument {
public:
    explicit Error(std::string key, std::vector<std::string> args = {})
        : std::invalid_argument(compose(key, args)), key_(std::move(key)), args_(std::move(args)) {}

    const std::string& key() const { return key_; }
    const std::vector<std::string>& args() const { return args_; }

private:
    static std::string compose(const std::string& k, const std::vector<std::string>& a) {
        std::string s = k;
        for (std::size_t i = 0; i < a.size(); ++i) s += (i == 0 ? ": " : ", ") + a[i];
        return s;
    }
    std::string key_;
    std::vector<std::string> args_;
};

} // namespace asiacad
