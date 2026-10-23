#pragma once
// AsiaCAD — obyekt xossalari (AutoCAD "Properties" / Civil 3D panel kabi): har tur uchun maydonlar ro'yxati va
// bitta maydonni o'zgartirilgan nusxa. Kalitlar til-mustaqil (X1, LENGTH, ...), nomlari — i18n "prop.<KEY>".
#include <asiacad/core/entity.h>

#include <memory>
#include <string>
#include <vector>

namespace asiacad {

struct PropField {
    std::string key;
    double value = 0;
    std::string text;        // matnli maydon (TEXT, BLOCK)
    bool isText = false;
    bool editable = true;
    int decimals = 3;
    std::string unit;        // "m", "°", "‰", "m²"
};

std::vector<PropField> entityProps(const IEntity& e);
// Bitta maydon o'zgargan nusxa (qatlam, rang, xdata saqlanadi). Noma'lum kalit — nullptr; noto'g'ri qiymat — Error("prop.err.value")
std::unique_ptr<IEntity> withProp(const IEntity& e, const std::string& key, const std::string& value);

} // namespace asiacad
