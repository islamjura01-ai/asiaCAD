#include <asiacad/core/props.h>

#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>

#include <cmath>
#include <cstdlib>

namespace asiacad {

namespace {
// Yopiq 4 uchli to'g'ri to'rtburchak (ustun, plita): tomonlar a = |v1-v0|, b = |v2-v1|
bool isRect(const PolylineEntity& p, double& a, double& b) {
    const auto& v = p.vertices();
    if (!p.isClosed() || v.size() != 4 || p.hasBulges()) return false;
    for (int i = 0; i < 4; ++i) {
        const Vec3 d1 = v[(i + 1) % 4] - v[i], d2 = v[(i + 2) % 4] - v[(i + 1) % 4];
        const double l1 = std::hypot(d1.x, d1.y), l2 = std::hypot(d2.x, d2.y);
        if (l1 < 1e-9 || l2 < 1e-9 || std::abs(d1.x * d2.x + d1.y * d2.y) > 1e-6 * l1 * l2) return false;
    }
    a = std::hypot(v[1].x - v[0].x, v[1].y - v[0].y);
    b = std::hypot(v[2].x - v[1].x, v[2].y - v[1].y);
    return true;
}
PropField num(const char* k, double v, const char* unit = "m", bool ed = true, int dec = 3) {
    PropField f;
    f.key = k;
    f.value = v;
    f.unit = unit;
    f.editable = ed;
    f.decimals = dec;
    return f;
}
double deg(double r) {
    double d = r * 180.0 / kPi;
    d = std::fmod(d, 360.0);
    return d < 0 ? d + 360.0 : d;
}
double parse(const std::string& s) {
    std::string t = s;
    for (auto& c : t) if (c == ',') c = '.';
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (t.empty() || end == t.c_str() || *end != '\0' || !std::isfinite(v)) throw Error("prop.err.value", {s});
    return v;
}
std::unique_ptr<IEntity> keep(const IEntity& src, std::unique_ptr<IEntity> dst) {
    dst->setLayer(src.layer());
    dst->setColor(src.color());
    for (const auto& [k, v] : src.xdata()) dst->xset(k, v);
    return dst;
}
}  // namespace

std::vector<PropField> entityProps(const IEntity& e) {
    std::vector<PropField> f;
    switch (e.type()) {
    case EntityType::Point: {
        const Vec3 p = static_cast<const PointEntity&>(e).position();
        f = {num("X", p.x), num("Y", p.y), num("Z", p.z)};
        break;
    }
    case EntityType::Line: {
        const auto& l = static_cast<const LineEntity&>(e);
        const Vec3 a = l.startPoint(), b = l.endPoint();
        const double l2 = std::hypot(b.x - a.x, b.y - a.y);
        f = {num("X1", a.x), num("Y1", a.y), num("Z1", a.z), num("X2", b.x), num("Y2", b.y), num("Z2", b.z),
             num("LENGTH", l2), num("ANGLE", deg(std::atan2(b.y - a.y, b.x - a.x)), "°", true, 4), num("DZ", b.z - a.z, "m", false),
             num("SLOPE", l2 > 1e-12 ? (b.z - a.z) / l2 * 1000.0 : 0.0, "‰", false, 2), num("LENGTH3D", a.distanceTo(b), "m", false)};
        break;
    }
    case EntityType::Circle: {
        const auto& c = static_cast<const CircleEntity&>(e);
        f = {num("CX", c.center().x), num("CY", c.center().y), num("CZ", c.center().z), num("R", c.radius()), num("D", 2 * c.radius()),
             num("AREA", c.area(), "m²"), num("CIRC", c.length(), "m", false)};
        break;
    }
    case EntityType::Arc: {
        const auto& a = static_cast<const ArcEntity&>(e);
        f = {num("CX", a.center().x), num("CY", a.center().y), num("R", a.radius()), num("START", deg(a.startAngle()), "°", true, 4),
             num("END", deg(a.endAngle()), "°", true, 4), num("LENGTH", a.length(), "m", false)};
        break;
    }
    case EntityType::Polyline: {
        const auto& p = static_cast<const PolylineEntity&>(e);
        f = {num("VERTS", static_cast<double>(p.vertices().size()), "", false, 0), num("LENGTH", p.length2d(), "m", false),
             num("LENGTH3D", p.length(), "m", false)};
        if (p.isClosed()) f.push_back(num("AREA", p.area(), "m²", false));
        if (double a = 0, b = 0; isRect(p, a, b)) {  // to'rtburchak: kesim o'lchamlari (markaz va yo'nalish saqlanadi)
            f.push_back(num("RECTW", a));
            f.push_back(num("RECTH", b));
        }
        f.push_back(num("CLOSED", p.isClosed() ? 1 : 0, "", true, 0));
        if (!p.is3d()) f.push_back(num("ELEV", p.vertices().empty() ? 0.0 : p.vertices().front().z));
        break;
    }
    case EntityType::Text: {
        const auto& t = static_cast<const TextEntity&>(e);
        PropField tx;
        tx.key = "TEXT";
        tx.text = t.text();
        tx.isText = true;
        f = {tx, num("H", t.height()), num("ROT", deg(t.rotation()), "°", true, 2), num("X", t.insertion().x), num("Y", t.insertion().y),
             num("Z", t.insertion().z)};
        break;
    }
    case EntityType::Insert: {
        const auto& i = static_cast<const InsertEntity&>(e);
        PropField b;
        b.key = "BLOCK";
        b.text = i.block() ? i.block()->name : std::string();
        b.isText = true;
        b.editable = false;
        const Vec3 p = i.insertionPoint();
        f = {b, num("X", p.x, "m", false), num("Y", p.y, "m", false), num("Z", p.z, "m", false)};
        break;
    }
    default: break;
    }
    return f;
}

std::unique_ptr<IEntity> withProp(const IEntity& e, const std::string& key, const std::string& value) {
    switch (e.type()) {
    case EntityType::Point: {
        Vec3 p = static_cast<const PointEntity&>(e).position();
        const double v = parse(value);
        if (key == "X") p.x = v; else if (key == "Y") p.y = v; else if (key == "Z") p.z = v; else return nullptr;
        return keep(e, std::make_unique<PointEntity>(p));
    }
    case EntityType::Line: {
        const auto& l = static_cast<const LineEntity&>(e);
        Vec3 a = l.startPoint(), b = l.endPoint();
        const double v = parse(value);
        const double l2 = std::hypot(b.x - a.x, b.y - a.y), ang = std::atan2(b.y - a.y, b.x - a.x);
        if (key == "X1") a.x = v; else if (key == "Y1") a.y = v; else if (key == "Z1") a.z = v;
        else if (key == "X2") b.x = v; else if (key == "Y2") b.y = v; else if (key == "Z2") b.z = v;
        else if (key == "LENGTH") {  // boshi joyida, yo'nalish saqlanadi
            if (v <= 0) throw Error("prop.err.value", {value});
            b = {a.x + v * std::cos(ang), a.y + v * std::sin(ang), b.z};
        } else if (key == "ANGLE") {
            const double r = v * kPi / 180.0;
            b = {a.x + l2 * std::cos(r), a.y + l2 * std::sin(r), b.z};
        } else return nullptr;
        if (a.distanceTo(b) < 1e-12) throw Error("prop.err.value", {value});
        return keep(e, std::make_unique<LineEntity>(a, b));
    }
    case EntityType::Circle: {
        const auto& c = static_cast<const CircleEntity&>(e);
        Vec3 o = c.center();
        double r = c.radius();
        const double v = parse(value);
        if (key == "CX") o.x = v; else if (key == "CY") o.y = v; else if (key == "CZ") o.z = v;
        else if (key == "R") r = v; else if (key == "D") r = v / 2;
        else if (key == "AREA") r = v > 0 ? std::sqrt(v / kPi) : -1;
        else return nullptr;
        if (r <= 0) throw Error("prop.err.value", {value});
        return keep(e, std::make_unique<CircleEntity>(o, r));
    }
    case EntityType::Arc: {
        const auto& a = static_cast<const ArcEntity&>(e);
        Vec3 o = a.center();
        double r = a.radius(), s = a.startAngle(), en = a.endAngle();
        const double v = parse(value);
        if (key == "CX") o.x = v; else if (key == "CY") o.y = v; else if (key == "R") r = v;
        else if (key == "START") s = v * kPi / 180.0; else if (key == "END") en = v * kPi / 180.0;
        else return nullptr;
        if (r <= 0) throw Error("prop.err.value", {value});
        return keep(e, std::make_unique<ArcEntity>(o, r, s, en));
    }
    case EntityType::Polyline: {
        const auto& p = static_cast<const PolylineEntity&>(e);
        auto pts = p.vertices();
        bool closed = p.isClosed();
        const double v = parse(value);
        double ra = 0, rb = 0;
        if (key == "CLOSED") closed = v != 0;
        else if (key == "ELEV") for (auto& q : pts) q.z = v;
        else if ((key == "RECTW" || key == "RECTH") && isRect(p, ra, rb)) {
            if (v <= 0) throw Error("prop.err.value", {value});
            const Vec3 c = (pts[0] + pts[2]) * 0.5;
            Vec3 u = pts[1] - pts[0], w = pts[2] - pts[1];
            u = u * (1.0 / ra);
            w = w * (1.0 / rb);
            const double na = key == "RECTW" ? v : ra, nb = key == "RECTH" ? v : rb;
            const double z = pts[0].z;
            pts = {c - u * (na / 2) - w * (nb / 2), c + u * (na / 2) - w * (nb / 2), c + u * (na / 2) + w * (nb / 2), c - u * (na / 2) + w * (nb / 2)};
            for (auto& q : pts) q.z = z;
        } else return nullptr;
        return keep(e, std::make_unique<PolylineEntity>(pts, p.bulges(), closed));
    }
    case EntityType::Text: {
        const auto& t = static_cast<const TextEntity&>(e);
        Vec3 ins = t.insertion();
        double h = t.height(), rot = t.rotation();
        std::string tx = t.text();
        if (key == "TEXT") {
            if (value.empty()) throw Error("prop.err.value", {value});
            tx = value;
        } else {
            const double v = parse(value);
            if (key == "H") { if (v <= 0) throw Error("prop.err.value", {value}); h = v; }
            else if (key == "ROT") rot = v * kPi / 180.0;
            else if (key == "X") ins.x = v; else if (key == "Y") ins.y = v; else if (key == "Z") ins.z = v;
            else return nullptr;
        }
        return keep(e, std::make_unique<TextEntity>(ins, h, tx, rot, t.hAlign(), t.vAlign()));
    }
    default: return nullptr;
    }
}

} // namespace asiacad
