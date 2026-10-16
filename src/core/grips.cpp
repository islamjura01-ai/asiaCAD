#include <asiacad/core/grips.h>

#include <asiacad/core/dimension.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>

#include <stdexcept>

namespace asiacad {

std::vector<Vec3> gripPoints(const IEntity& e) {
    switch (e.type()) {
        case EntityType::Point: return {static_cast<const PointEntity&>(e).position()};
        case EntityType::Line: {
            const auto& l = static_cast<const LineEntity&>(e);
            return {l.startPoint(), l.endPoint(), (l.startPoint() + l.endPoint()) * 0.5};
        }
        case EntityType::Circle: {
            const auto& c = static_cast<const CircleEntity&>(e);
            const Vec3 o = c.center();
            const double r = c.radius();
            return {o, o + Vec3{r, 0, 0}, o + Vec3{0, r, 0}, o - Vec3{r, 0, 0}, o - Vec3{0, r, 0}};
        }
        case EntityType::Arc: {
            const auto& a = static_cast<const ArcEntity&>(e);
            return {a.center(), a.startPoint(), a.pointAtAngle(a.startAngle() + a.sweep() / 2), a.endPoint()};
        }
        case EntityType::Polyline: return static_cast<const PolylineEntity&>(e).vertices();
        case EntityType::Text: return {static_cast<const TextEntity&>(e).insertion()};
        case EntityType::Insert: return {static_cast<const InsertEntity&>(e).insertionPoint()};
        case EntityType::Hatch: {
            std::vector<Vec3> out;
            for (const auto& l : static_cast<const HatchEntity&>(e).loops())
                out.insert(out.end(), l.vertices().begin(), l.vertices().end());
            return out;
        }
        case EntityType::Dimension: {
            const auto& d = static_cast<const DimensionEntity&>(e);
            return {d.p1(), d.p2(), d.location()};
        }
    }
    return {};
}

namespace {
std::unique_ptr<IEntity> moved(const IEntity& e, const Vec3& from, const Vec3& to) {
    auto c = e.clone();
    c->transform(Transform::translation(to - from));
    return c;
}
} // namespace

std::unique_ptr<IEntity> withGripMoved(const IEntity& e, std::size_t i, const Vec3& to) {
    const auto g = gripPoints(e);
    if (i >= g.size()) throw Error("core.grip_index", {std::to_string(i), std::to_string(g.size())});
    std::unique_ptr<IEntity> out;
    try {
        switch (e.type()) {
            case EntityType::Line:
                if (i == 2) out = moved(e, g[2], to);
                else out = std::make_unique<LineEntity>(i == 0 ? to : g[0], i == 1 ? to : g[1]);
                break;
            case EntityType::Circle: {
                const auto& c = static_cast<const CircleEntity&>(e);
                if (i == 0) { out = moved(e, g[0], to); break; }
                const double r = (to - c.center()).length2d();
                if (r < 1e-12) throw Error("core.grip_invalid");
                out = std::make_unique<CircleEntity>(c.center(), r);
                break;
            }
            case EntityType::Arc: {
                if (i == 0) { out = moved(e, g[0], to); break; }
                Vec3 p[3] = {g[1], g[2], g[3]};
                p[i - 1] = to;
                out = std::make_unique<ArcEntity>(ArcEntity::fromThreePoints(p[0], p[1], p[2]));
                break;
            }
            case EntityType::Polyline: {
                const auto& pl = static_cast<const PolylineEntity&>(e);
                auto pts = pl.vertices();
                pts[i] = to;
                out = std::make_unique<PolylineEntity>(std::move(pts), pl.bulges(), pl.isClosed());
                break;
            }
            case EntityType::Hatch: {
                const auto& h = static_cast<const HatchEntity&>(e);
                std::vector<PolylineEntity> loops;
                std::size_t k = i;
                for (const auto& l : h.loops()) {
                    auto pts = l.vertices();
                    if (k < pts.size()) pts[k] = to;
                    k = k < pts.size() ? static_cast<std::size_t>(-1) / 2 : k - pts.size();
                    loops.emplace_back(std::move(pts), l.bulges(), true);
                }
                out = std::make_unique<HatchEntity>(std::move(loops), h.pattern(), h.patternScale(), h.patternAngle());
                break;
            }
            case EntityType::Dimension: {
                const auto& d = static_cast<const DimensionEntity&>(e);
                Vec3 p[3] = {d.p1(), d.p2(), d.location()};
                p[i] = to;
                out = std::make_unique<DimensionEntity>(d.withPoints(p[0], p[1], p[2]));
                break;
            }
            case EntityType::Point:
            case EntityType::Text:
            case EntityType::Insert: out = moved(e, g[0], to); break;
        }
    } catch (const Error& ex) {
        if (ex.key() == "core.grip_index") throw;
        throw Error("core.grip_invalid");
    } catch (const std::invalid_argument&) {
        throw Error("core.grip_invalid");
    }
    out->setLayer(e.layer());
    out->setColor(e.color());
    return out;
}

void ReplaceEntityCommand::execute(Document& doc) {
    if (!doc.contains(id_)) throw Error("core.not_found", {std::to_string(id_)});
    if (!doc.isEditable(id_)) throw Error("core.locked", {std::to_string(id_)});
    other_ = doc.replace(id_, std::move(other_));
}

void ReplaceEntityCommand::undo(Document& doc) { other_ = doc.replace(id_, std::move(other_)); }

} // namespace asiacad
