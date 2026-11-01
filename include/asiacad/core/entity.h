#pragma once
// AsiaCAD — IEntity ierarxiyasining asosi
#include <asiacad/core/math.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace asiacad {

using EntityId = std::uint64_t;
inline constexpr EntityId kInvalidId = 0;

enum class EntityType { Point, Line, Circle, Arc, Polyline, Text, Insert, Hatch, Dimension, Mesh };

std::string_view toString(EntityType t);

class DxfWriter;

// Rang: AutoCAD Color Index (ACI). 256 = ByLayer, 0 = ByBlock.
inline constexpr int kColorByLayer = 256;
inline constexpr int kColorByBlock = 0;  // faqat blok ichidagi obyektlar uchun: INSERT rangini oladi

class IEntity {
public:
    virtual ~IEntity() = default;

    virtual EntityType type() const = 0;
    virtual std::unique_ptr<IEntity> clone() const = 0;
    virtual BBox bbox() const = 0;
    virtual void transform(const Transform& t) = 0;
    virtual void writeDxf(DxfWriter& w) const = 0;
    // Odam o'qiy oladigan qisqa tavsif (LIST buyrug'i uchun)
    virtual std::string describe() const = 0;

    EntityId id() const { return id_; }
    const std::string& layer() const { return layer_; }
    void setLayer(std::string l) { layer_ = std::move(l); }
    int color() const { return color_; }
    void setColor(int c) { color_ = c; }
    // Kengaytirilgan ma'lumot (AutoCAD XDATA / Civil 3D "aqlli obyekt" bog'i): masalan net=K1, seg=3.
    // Nusxalanganda saqlanadi, .acad faylida "x":{...}
    const std::map<std::string, std::string>& xdata() const { return x_; }
    std::string xget(const std::string& k) const { auto it = x_.find(k); return it == x_.end() ? std::string() : it->second; }
    void xset(const std::string& k, std::string v) { if (v.empty()) x_.erase(k); else x_[k] = std::move(v); }

protected:
    IEntity() = default;
    IEntity(const IEntity&) = default;
    IEntity& operator=(const IEntity&) = default;

private:
    friend class Document;  // ID faqat Document tomonidan beriladi
    EntityId id_ = kInvalidId;
    std::string layer_ = "0";
    int color_ = kColorByLayer;
    std::map<std::string, std::string> x_;
};

// Egri chiziqlar uchun umumiy interfeys (SRS: ICurve)
class ICurve : public IEntity {
public:
    virtual double length() const = 0;
    virtual Vec3 startPoint() const = 0;
    virtual Vec3 endPoint() const = 0;
    virtual bool isClosed() const { return false; }
    // t ∈ [0, length()] bo'yicha nuqta
    virtual Vec3 pointAtDistance(double s) const = 0;
    // Eng yaqin nuqta (snap, OSNAP NEAREST uchun)
    virtual Vec3 closestPoint(const Vec3& p) const = 0;
};

} // namespace asiacad
