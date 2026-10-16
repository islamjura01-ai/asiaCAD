#include <asiacad/core/dimension.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/geom2d.h>
#include <asiacad/core/snap.h>

#include <algorithm>

namespace asiacad {

namespace {
constexpr std::pair<SnapMode, std::string_view> kNames[] = {
    {SnapEnd, "END"}, {SnapMid, "MID"}, {SnapCen, "CEN"}, {SnapQua, "QUA"}, {SnapInt, "INT"},
    {SnapPer, "PER"}, {SnapTan, "TAN"}, {SnapNea, "NEA"}, {SnapNod, "NOD"}, {SnapIns, "INS"},
};

BBox around(const Vec3& p, double r) {
    BBox b;
    b.expand({p.x - r, p.y - r, p.z});
    b.expand({p.x + r, p.y + r, p.z});
    return b;
}

double dist2d(const Vec3& a, const Vec3& b) { return (a - b).length2d(); }

struct Collector {
    const Vec3& cursor;
    double aperture;
    unsigned modes;
    const Vec3* from;
    std::vector<SnapResult>& out;
    std::vector<std::pair<geom::Prim, EntityId>>& prims;  // INT uchun

    void add(SnapMode m, const Vec3& p, EntityId id, bool anyDistance = false) {
        if (!(modes & m)) return;
        const double d = dist2d(p, cursor);
        if (!anyDistance && d > aperture) return;
        out.push_back({m, p, id, d});
    }

    // Obyekt va (INSERT bo'lsa) uning ichidagi geometriya
    void entity(const IEntity& e, EntityId id) {
        switch (e.type()) {
            case EntityType::Hatch: return;  // AutoCAD'dagi kabi: shtrix chegarasiga bog'lanilmaydi (OSNAPHATCH=0)
            case EntityType::Point: add(SnapNod, static_cast<const PointEntity&>(e).position(), id); return;
            case EntityType::Text: add(SnapIns, static_cast<const TextEntity&>(e).insertion(), id); return;
            case EntityType::Dimension: {  // faqat tayanch nuqtalar (DIMLINEAR/ALIGNED o'lchangan nuqtalar)
                const auto& d = static_cast<const DimensionEntity&>(e);
                add(SnapNod, d.p1(), id);
                add(SnapNod, d.p2(), id);
                return;
            }
            case EntityType::Insert: {
                const auto& in = static_cast<const InsertEntity&>(e);
                add(SnapIns, in.insertionPoint(), id);
                for (const auto& part : in.explode()) entity(*part, id);
                return;
            }
            default: break;
        }
        const auto ps = geom::primitivesOf(e);
        double near = 1e300;
        for (const auto& p : ps) near = std::min(near, geom::distance2d(p, cursor));
        const bool underCursor = near <= aperture;
        for (const auto& p : ps) {
            if (p.bbox().intersects2d(around(cursor, aperture))) prims.push_back({p, id});
            if (!p.isFullCircle()) {
                add(SnapEnd, p.a, id);
                add(SnapEnd, p.b, id);
                add(SnapMid, p.mid(), id);
            }
            if (p.kind == geom::Prim::Kind::Arc) {
                // Markaz kursordan uzoq bo'lishi mumkin: AutoCAD'dagi kabi egri apertura ichida bo'lsa yetarli
                if (underCursor) add(SnapCen, p.c, id, true);
                for (int q = 0; q < 4; ++q)
                    if (p.containsAngle(q * kPi / 2)) add(SnapQua, {p.c.x + p.r * std::cos(q * kPi / 2), p.c.y + p.r * std::sin(q * kPi / 2), p.c.z}, id);
            }
            if (underCursor && from) {
                Vec3 foot;
                if (geom::perpendicularFoot(p, *from, foot)) add(SnapPer, foot, id, true);
                std::vector<Vec3> tg;
                geom::tangentPoints(p, *from, tg);
                for (const auto& t : tg) add(SnapTan, t, id, true);
            }
        }
        if (underCursor) {
            Vec3 best;
            double bd = 1e300;
            for (const auto& p : ps) {
                const Vec3 c = geom::closestPoint(p, cursor);
                if (dist2d(c, cursor) < bd) { bd = dist2d(c, cursor); best = c; }
            }
            add(SnapNea, best, id);
        }
    }
};
} // namespace

std::string_view snapName(SnapMode m) {
    for (const auto& [k, n] : kNames)
        if (k == m) return n;
    return "?";
}

SnapMode snapFromName(std::string_view s) {
    for (const auto& [k, n] : kNames)
        if (n == s) return k;
    return static_cast<SnapMode>(0);
}

std::vector<SnapResult> findSnaps(const Document& doc, const Vec3& cursor, double aperture, unsigned modes, const Vec3* from) {
    std::vector<SnapResult> out;
    std::vector<std::pair<geom::Prim, EntityId>> prims;
    Collector col{cursor, aperture, modes, from, out, prims};
    for (EntityId id : doc.query(around(cursor, aperture)))
        if (const IEntity* e = doc.find(id)) col.entity(*e, id);
    // PER/TAN uchun obyekt kursorga yaqin bo'lishi kifoya, nuqta esa uzoqroq bo'lishi mumkin —
    // shuning uchun ular yuqorida masofa cheklovisiz qo'shildi.
    if (modes & SnapInt) {
        std::vector<Vec3> hits;
        for (std::size_t i = 0; i < prims.size(); ++i)
            for (std::size_t j = i + 1; j < prims.size(); ++j) {
                hits.clear();
                geom::intersect(prims[i].first, prims[j].first, hits);
                for (const auto& h : hits) col.add(SnapInt, h, prims[i].second);
            }
    }
    std::stable_sort(out.begin(), out.end(), [](const SnapResult& a, const SnapResult& b) {
        const bool an = a.mode == SnapNea, bn = b.mode == SnapNea;
        if (an != bn) return bn;  // NEA — eng past ustuvorlik
        return a.distance < b.distance;
    });
    // Takrorlar (masalan, ikki kesmaning umumiy uchi) — bitta qoladi
    std::vector<SnapResult> uniq;
    for (const auto& r : out) {
        bool dup = false;
        for (const auto& u : uniq)
            if (u.mode == r.mode && u.point.equals(r.point, 1e-9)) { dup = true; break; }
        if (!dup) uniq.push_back(r);
    }
    return uniq;
}

std::vector<EntityId> selectWindow(const Document& doc, const BBox& box, bool crossing) {
    std::vector<EntityId> out;
    auto insideBox = [&](const BBox& b) {
        return b.min.x >= box.min.x && b.max.x <= box.max.x && b.min.y >= box.min.y && b.max.y <= box.max.y;
    };
    for (EntityId id : doc.query(box)) {
        const IEntity* e = doc.find(id);
        if (!e) continue;
        const BBox b = e->bbox();
        if (insideBox(b)) { out.push_back(id); continue; }
        if (!crossing) continue;
        bool hit = false;
        switch (e->type()) {
            case EntityType::Point:
                break;  // nuqta faqat ichida bo'lsa (yuqorida)
            case EntityType::Text:
                hit = true;  // qutisi kesishadi (taxminiy metrika)
                break;
            default: {
                for (const auto& p : geom::primitivesOf(*e))
                    if (geom::hitsBox(p, box)) { hit = true; break; }
                if (!hit && e->type() == EntityType::Hatch) {  // to'rtburchak to'liq shtrix ichida
                    const Vec3 c{(box.min.x + box.max.x) / 2, (box.min.y + box.max.y) / 2, 0};
                    int depth = 0;
                    for (const auto& l : static_cast<const HatchEntity*>(e)->loops()) depth += l.containsPoint2d(c) ? 1 : 0;
                    hit = depth % 2 == 1;
                }
            }
        }
        if (hit) out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

EntityId pickEntity(const Document& doc, const Vec3& p, double tol) {
    EntityId best = kInvalidId;
    double bestD = tol;
    for (EntityId id : doc.query(around(p, tol))) {
        const IEntity* e = doc.find(id);
        if (!e) continue;
        double d = 1e300;
        if (auto* pt = dynamic_cast<const PointEntity*>(e)) {
            d = dist2d(pt->position(), p);
        } else if (e->type() == EntityType::Text) {
            const BBox b = e->bbox();
            d = (p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y) ? 0.0 : 1e300;
        } else {
            for (const auto& pr : geom::primitivesOf(*e)) d = std::min(d, geom::distance2d(pr, p));
            std::vector<std::unique_ptr<IEntity>> parts;
            if (auto* in = dynamic_cast<const InsertEntity*>(e)) parts = in->explode();
            else if (auto* dm = dynamic_cast<const DimensionEntity*>(e)) parts = dm->explode();
            for (const auto& part : parts)
                if (part->type() == EntityType::Text) {
                    const BBox b = part->bbox();
                    if (p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y) d = 0;
                }
        }
        if (d < bestD || (d == bestD && (best == kInvalidId || id > best))) {  // teng bo'lsa — ustidagisi (keyingisi)
            bestD = d;
            best = id;
        }
    }
    return best;
}

} // namespace asiacad
