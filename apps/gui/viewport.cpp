#include "viewport.h"

#include <asiacad/core/dimension.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/grips.h>
#include <asiacad/core/draft.h>
#include <asiacad/core/modify.h>

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace asiacad::gui {

namespace {
constexpr double kMinScale = 1e-6, kMaxScale = 1e6;
constexpr std::size_t kMaxGripObjects = 200;  // AutoCAD GRIPOBJLIMIT kabi

QString num(double v) {
    QString s = QString::number(v, 'f', 6);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) s.chop(1);
    return s == "-0" ? QStringLiteral("0") : s;
}
} // namespace

Viewport::Viewport(Document& doc, const i18n::Localizer& loc, QWidget* parent)
    : QOpenGLWidget(parent), doc_(doc), loc_(loc), theme_(themeFor(UiMode::Classic)) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);
    setMinimumSize(320, 240);
}

void Viewport::setTheme(const Theme& t) {
    theme_ = t;
    update();
}

QString Viewport::fmtPoint(const Vec3& p) { return num(p.x) + "," + num(p.y); }

Vec3 Viewport::toWorld(const QPointF& s) const {
    return {center_.x + (s.x() - width() / 2.0) / scale_, center_.y - (s.y() - height() / 2.0) / scale_, 0};
}

QPointF Viewport::toScreen(const Vec3& w) const {
    return {width() / 2.0 + (w.x - center_.x) * scale_, height() / 2.0 - (w.y - center_.y) * scale_};
}

void Viewport::setView(const Vec3& c, double ppu) {
    center_ = c;
    scale_ = std::clamp(ppu, kMinScale, kMaxScale);
    update();
}

void Viewport::zoomExtents() {
    const BBox b = doc_.extents(true);
    if (!b.valid()) { setView({0, 0, 0}, 10.0); return; }
    const double w = std::max(b.max.x - b.min.x, 1e-6), h = std::max(b.max.y - b.min.y, 1e-6);
    const double s = std::min((width() - 60.0) / w, (height() - 60.0) / h);
    setView({(b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, 0}, s > 0 ? s : 1.0);
}

void Viewport::zoomBy(double f) { setView(center_, scale_ * f); }

std::optional<Vec3> Viewport::lastPoint() const {
    if (pts_.empty()) return std::nullopt;
    return pts_.back();
}

// ---------------------------------------------------------------- asboblar

void Viewport::setTool(Tool t) {
    if ((t == Tool::Move || t == Tool::Copy || t == Tool::Rotate || t == Tool::Scale || t == Tool::Mirror) && sel_.empty()) {
        emit promptChanged(QString::fromStdString(loc_.tr("gui.prompt.selectfirst")));
        tool_ = Tool::Select;
        pts_.clear();
        update();
        return;
    }
    tool_ = t;
    pts_.clear();
    snap_.reset();
    track_.reset();
    dimTarget_ = kInvalidId;
    modTarget_ = kInvalidId;
    dimH_ = 0;
    if (t == Tool::DimLinear || t == Tool::DimAligned || t == Tool::DimRadius || t == Tool::DimDiameter) {
        for (EntityId id : doc_.ids()) {  // oxirgi o'lcham balandligi
            const IEntity* e = doc_.find(id);
            if (e && e->type() == EntityType::Dimension) dimH_ = static_cast<const DimensionEntity*>(e)->textHeight();
        }
    }
    gripDrag_ = false;
    emitPrompt();
    update();
}

void Viewport::setOrtho(bool on) {
    ortho_ = on;
    if (on) polar_ = false;
    emit orthoPolarChanged(ortho_, polar_);
    updateSnap(mouse_);
    update();
}

void Viewport::setPolar(bool on) {
    polar_ = on;
    if (on) ortho_ = false;
    emit orthoPolarChanged(ortho_, polar_);
    updateSnap(mouse_);
    update();
}

double Viewport::dimTextHeight() const {
    if (dimH_ > 0) return dimH_;  // chizmadagi mavjud o'lchamlar bilan bir xil
    // ekranda ~13 px; 1-2-2.5-5·10^k qatoridan
    const double raw = 13.0 / scale_;
    const double e = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (e * m >= raw * 0.8) return e * m;
    return e * 10;
}

std::optional<Vec3> Viewport::trackBase() const {
    if (pts_.empty()) return std::nullopt;
    if (tool_ == Tool::DimRadius || tool_ == Tool::DimDiameter) return std::nullopt;
    return pts_.back();
}

bool Viewport::feedDistance(double d) {
    if (tool_ == Tool::Select) return false;
    const auto base = trackBase();
    if (!base) return false;
    Vec3 dir;
    if (track_) dir = {std::cos(deg2rad(track_->angleDeg)), std::sin(deg2rad(track_->angleDeg)), 0};
    else {
        const Vec3 v = toWorld(mouse_) - *base;
        const double l = v.length2d();
        if (l < 1e-12) return false;
        dir = {v.x / l, v.y / l, 0};
    }
    clickPoint(*base + dir * d);
    return true;
}

std::optional<std::pair<EntityId, int>> Viewport::gripAt(const QPointF& sp) const {
    if (tool_ != Tool::Select || sel_.empty() || sel_.size() > kMaxGripObjects) return std::nullopt;
    std::optional<std::pair<EntityId, int>> best;
    double bestD = 7.0;
    for (EntityId id : sel_) {
        const IEntity* e = doc_.find(id);
        if (!e || !doc_.isEditable(id)) continue;
        const auto g = gripPoints(*e);
        for (std::size_t i = 0; i < g.size(); ++i) {
            const QPointF q = toScreen(g[i]);
            const double d = std::max(std::abs(q.x() - sp.x()), std::abs(q.y() - sp.y()));
            if (d <= bestD) { bestD = d; best = std::pair{id, static_cast<int>(i)}; }
        }
    }
    return best;
}

void Viewport::beginGrip(EntityId id, int index) {
    const IEntity* e = doc_.find(id);
    if (!e) return;
    const auto g = gripPoints(*e);
    if (index < 0 || static_cast<std::size_t>(index) >= g.size()) return;
    tool_ = Tool::Grip;
    gripId_ = id;
    gripIdx_ = index;
    pts_ = {g[static_cast<std::size_t>(index)]};
    hoverGrip_.reset();
    emitPrompt();
    updateSnap(mouse_);
    update();
}

void Viewport::cancelTool() {
    if ((tool_ == Tool::Pline || tool_ == Tool::Spline || tool_ == Tool::Leader) && pts_.size() >= 2) { finishTool(); return; }
    tool_ = Tool::Select;
    pts_.clear();
    snap_.reset();
    track_.reset();
    gripDrag_ = false;
    dimTarget_ = kInvalidId;
    modTarget_ = kInvalidId;
    emitPrompt();
    update();
}

void Viewport::finishTool() {
    if (tool_ == Tool::Pline && pts_.size() >= 2) {
        QStringList parts{QStringLiteral("PLINE")};
        for (const auto& p : pts_) parts << fmtPoint(p);
        emit runCommand(parts.join(' '));
    }
    if (tool_ == Tool::Spline && pts_.size() >= 2) {
        QStringList parts{QStringLiteral("SPLINE")};
        for (const auto& p : pts_) parts << fmtPoint(p);
        emit runCommand(parts.join(' '));
    }
    if (tool_ == Tool::Leader && pts_.size() >= 2) {
        QStringList parts;
        for (const auto& p : pts_) parts << fmtPoint(p);
        emit leaderRequested(parts.join(' '));
    }
    tool_ = Tool::Select;
    pts_.clear();
    track_.reset();
    gripDrag_ = false;
    dimTarget_ = kInvalidId;
    emitPrompt();
    update();
}

QString Viewport::selArg() const {
    QStringList ids;
    for (EntityId id : sel_) ids << QString::number(id);
    return ids.join(',');
}

void Viewport::emitPrompt() {
    const char* key = "gui.prompt.idle";
    switch (tool_) {
    case Tool::Select: key = "gui.prompt.idle"; break;
    case Tool::Line: case Tool::Pline: key = pts_.empty() ? "gui.prompt.first" : "gui.prompt.next"; break;
    case Tool::Circle: key = pts_.empty() ? "gui.prompt.center" : "gui.prompt.radius"; break;
    case Tool::Arc: key = pts_.empty() ? "gui.prompt.first" : (pts_.size() == 1 ? "gui.prompt.arc2" : "gui.prompt.arc3"); break;
    case Tool::Point: key = "gui.prompt.point"; break;
    case Tool::Text: key = "gui.prompt.textpos"; break;
    case Tool::Move: case Tool::Copy: key = pts_.empty() ? "gui.prompt.base" : "gui.prompt.target"; break;
    case Tool::Rotate: key = pts_.empty() ? "gui.prompt.base" : "gui.prompt.angle"; break;
    case Tool::DimLinear:
    case Tool::DimAligned:
        key = pts_.empty() ? "gui.prompt.first" : (pts_.size() == 1 ? "gui.prompt.dim2" : "gui.prompt.dimloc");
        break;
    case Tool::DimRadius:
    case Tool::DimDiameter: key = dimTarget_ == kInvalidId ? "gui.prompt.dimsel" : "gui.prompt.dimloc"; break;
    case Tool::Grip: key = "gui.prompt.grip"; break;
    case Tool::Offset: key = modTarget_ == kInvalidId ? "gui.prompt.offsetsel" : "gui.prompt.offsetside"; break;
    case Tool::Trim: key = "gui.prompt.trim"; break;
    case Tool::Extend: key = "gui.prompt.extend"; break;
    case Tool::Fillet: key = modTarget_ == kInvalidId ? "gui.prompt.fillet1" : "gui.prompt.fillet2"; break;
    case Tool::Rect: key = pts_.empty() ? "gui.prompt.first" : "gui.prompt.corner"; break;
    case Tool::Polygon: key = pts_.empty() ? "gui.prompt.center" : "gui.prompt.polyvertex"; break;
    case Tool::Ellipse: key = pts_.empty() ? "gui.prompt.center" : (pts_.size() == 1 ? "gui.prompt.axis1" : "gui.prompt.axis2"); break;
    case Tool::Spline: key = pts_.empty() ? "gui.prompt.first" : "gui.prompt.splnext"; break;
    case Tool::Stretch: key = pts_.size() < 2 ? "gui.prompt.window" : (pts_.size() == 2 ? "gui.prompt.base" : "gui.prompt.target"); break;
    case Tool::Scale: key = pts_.empty() ? "gui.prompt.base" : "gui.prompt.factor"; break;
    case Tool::Mirror: key = pts_.empty() ? "gui.prompt.mirror1" : "gui.prompt.mirror2"; break;
    case Tool::Chamfer: key = modTarget_ == kInvalidId ? "gui.prompt.chamfer1" : "gui.prompt.chamfer2"; break;
    case Tool::Break: key = modTarget_ == kInvalidId ? "gui.prompt.break1" : "gui.prompt.break2"; break;
    case Tool::Leader: key = pts_.empty() ? "gui.prompt.leader1" : "gui.prompt.splnext"; break;
    case Tool::DimAngular: key = pts_.empty() ? "gui.prompt.vertex" : (pts_.size() < 3 ? "gui.prompt.ray" : "gui.prompt.dimloc"); break;
    case Tool::Symbol: key = "gui.prompt.symbol"; break;
    case Tool::MatchProp: key = modTarget_ == kInvalidId ? "gui.prompt.matchsrc" : "gui.prompt.matchdst"; break;
    case Tool::LayPick: key = "gui.prompt.laypick"; break;
    }
    std::vector<std::string> args;
    if (tool_ == Tool::Offset) args = {num(offsetDist_).toStdString()};
    if (tool_ == Tool::Fillet) args = {num(filletR_).toStdString()};
    if (tool_ == Tool::Chamfer) args = {num(chamferD_).toStdString()};
    if (tool_ == Tool::Polygon) args = {std::to_string(polySides_)};
    emit promptChanged(QString::fromStdString(loc_.tr(key, args)));
}

bool Viewport::feedPoint(const Vec3& w) {
    if (tool_ == Tool::Select) return false;
    clickPoint(w);
    return true;
}

void Viewport::clickPoint(const Vec3& p) {
    switch (tool_) {
    case Tool::Select: return;
    case Tool::Line:
        if (!pts_.empty()) emit runCommand("LINE " + fmtPoint(pts_.back()) + " " + fmtPoint(p));
        pts_.push_back(p);
        break;
    case Tool::Pline:
        if (pts_.size() >= 2 && QLineF(toScreen(p), toScreen(pts_.front())).length() < 8) {
            QStringList parts{QStringLiteral("PLINE")};
            for (const auto& q : pts_) parts << fmtPoint(q);
            parts << QString::fromStdString(loc_.keywordName("CLOSE"));
            emit runCommand(parts.join(' '));
            pts_.clear();
            tool_ = Tool::Select;
        } else {
            pts_.push_back(p);
        }
        break;
    case Tool::Circle:
        if (pts_.empty()) { pts_.push_back(p); break; }
        if (const double r = pts_[0].distanceTo(p); r > 0) emit runCommand("CIRCLE " + fmtPoint(pts_[0]) + " " + num(r));
        pts_.clear();
        break;
    case Tool::Arc:
        pts_.push_back(p);
        if (pts_.size() == 3) {
            emit runCommand("ARC " + fmtPoint(pts_[0]) + " " + fmtPoint(pts_[1]) + " " + fmtPoint(pts_[2]));
            pts_.clear();
        }
        break;
    case Tool::Point: emit runCommand("POINT " + fmtPoint(p)); break;
    case Tool::Text: emit textRequested(p); break;
    case Tool::Move:
    case Tool::Copy:
        if (pts_.empty()) { pts_.push_back(p); break; }
        if (tool_ == Tool::Move) emit runCommand("MOVE " + selArg() + " " + fmtPoint(pts_[0]) + " " + fmtPoint(p));
        else emit runCommand("COPY " + selArg() + " " + num(p.x - pts_[0].x) + "," + num(p.y - pts_[0].y));
        pts_.clear();
        tool_ = Tool::Select;
        break;
    case Tool::Rotate: {
        if (pts_.empty()) { pts_.push_back(p); break; }
        const double deg = std::atan2(p.y - pts_[0].y, p.x - pts_[0].x) * 180.0 / M_PI;
        emit runCommand("ROTATE " + selArg() + " " + fmtPoint(pts_[0]) + " " + num(deg));
        pts_.clear();
        tool_ = Tool::Select;
        break;
    }
    case Tool::DimLinear:
    case Tool::DimAligned:
        if (pts_.size() == 1 && pts_[0].distanceTo(p) < 1e-9) break;  // ustma-ust nuqta
        pts_.push_back(p);
        if (pts_.size() == 3) {
            emit runCommand(QString(tool_ == Tool::DimLinear ? "DIMLINEAR " : "DIMALIGNED ") + fmtPoint(pts_[0]) + " " +
                            fmtPoint(pts_[1]) + " " + fmtPoint(pts_[2]) + " " + num(dimTextHeight()));
            pts_.clear();  // asbob faol qoladi — keyingi o'lcham
        }
        break;
    case Tool::DimRadius:
    case Tool::DimDiameter:
        if (dimTarget_ == kInvalidId) {
            const EntityId id = pickEntity(doc_, p, aperture());
            const IEntity* e = doc_.find(id);
            if (e && (e->type() == EntityType::Circle || e->type() == EntityType::Arc)) dimTarget_ = id;
            break;
        }
        emit runCommand(QString(tool_ == Tool::DimRadius ? "DIMRADIUS " : "DIMDIAMETER ") + QString::number(dimTarget_) +
                        " " + fmtPoint(p) + " " + num(dimTextHeight()));
        dimTarget_ = kInvalidId;
        break;
    case Tool::Offset: {
        if (modTarget_ == kInvalidId) {
            const EntityId id = pickAt(p, nullptr);
            const IEntity* e = doc_.find(id);
            if (e && (e->type() == EntityType::Line || e->type() == EntityType::Circle || e->type() == EntityType::Arc ||
                      e->type() == EntityType::Polyline))
                modTarget_ = id;
            break;
        }
        emit runCommand("OFFSET " + num(offsetDist_) + " " + QString::number(modTarget_) + " " + fmtPoint(p));
        modTarget_ = kInvalidId;
        break;
    }
    case Tool::Trim:
    case Tool::Extend: {
        Vec3 on;
        if (pickAt(p, &on) == kInvalidId) break;
        emit runCommand(QString(tool_ == Tool::Trim ? "TRIM " : "EXTEND ") + QString::fromStdString(loc_.keywordName("ALL")) +
                        " " + fmtPoint(on));
        break;
    }
    case Tool::Fillet: {
        Vec3 on;
        const EntityId id = pickAt(p, &on);
        const IEntity* e = doc_.find(id);
        if (!e || e->type() != EntityType::Line) break;
        if (modTarget_ == kInvalidId) { modTarget_ = id; modPick_ = on; break; }
        if (id == modTarget_) break;
        emit runCommand("FILLET " + fmtPoint(modPick_) + " " + fmtPoint(on) + " " + num(filletR_));
        modTarget_ = kInvalidId;
        break;
    }
    case Tool::Rect:
        if (pts_.empty()) { pts_.push_back(p); break; }
        if (std::abs(p.x - pts_[0].x) > 1e-9 && std::abs(p.y - pts_[0].y) > 1e-9) emit runCommand("RECTANG " + fmtPoint(pts_[0]) + " " + fmtPoint(p));
        pts_.clear();
        break;
    case Tool::Polygon:
        if (pts_.empty()) { pts_.push_back(p); break; }
        if (pts_[0].distanceTo(p) > 1e-9) emit runCommand("POLYGON " + QString::number(polySides_) + " " + fmtPoint(pts_[0]) + " " + fmtPoint(p));
        pts_.clear();
        break;
    case Tool::Ellipse:
        pts_.push_back(p);
        if (pts_.size() == 3) {
            const Vec3 c = pts_[0], a = pts_[1];
            const Vec3 u = Vec3{a.x - c.x, a.y - c.y, 0} / std::max(1e-12, (a - c).length2d());
            const double b = std::abs((p.x - c.x) * -u.y + (p.y - c.y) * u.x);
            if (b > 1e-9 && (a - c).length2d() > 1e-9) emit runCommand("ELLIPSE " + fmtPoint(c) + " " + fmtPoint(a) + " " + num(b));
            pts_.clear();
        }
        break;
    case Tool::Spline:
    case Tool::Leader:
        if (pts_.empty() || pts_.back().distanceTo(p) > 1e-9) pts_.push_back(p);
        break;
    case Tool::Stretch:
        pts_.push_back(p);
        if (pts_.size() == 4) {
            emit runCommand("STRETCH " + fmtPoint(pts_[0]) + " " + fmtPoint(pts_[1]) + " " + fmtPoint(pts_[2]) + " " + fmtPoint(pts_[3]));
            pts_.clear();
            tool_ = Tool::Select;
        }
        break;
    case Tool::Scale: {
        if (pts_.empty()) { pts_.push_back(p); break; }
        const double k = pts_[0].distanceTo(p);
        if (k > 1e-9) emit runCommand("SCALE " + selArg() + " " + fmtPoint(pts_[0]) + " " + num(k));
        pts_.clear();
        tool_ = Tool::Select;
        break;
    }
    case Tool::Mirror:
        if (pts_.empty()) { pts_.push_back(p); break; }
        if (pts_[0].distanceTo(p) > 1e-9)
            emit runCommand("MIRROR " + selArg() + " " + fmtPoint(pts_[0]) + " " + fmtPoint(p) + " " + QString::fromStdString(loc_.keywordName("COPY")));
        pts_.clear();
        tool_ = Tool::Select;
        break;
    case Tool::Chamfer: {
        Vec3 on;
        const EntityId id = pickAt(p, &on);
        const IEntity* e = doc_.find(id);
        if (!e || e->type() != EntityType::Line) break;
        if (modTarget_ == kInvalidId) { modTarget_ = id; modPick_ = on; break; }
        if (id == modTarget_) break;
        emit runCommand("CHAMFER " + fmtPoint(modPick_) + " " + fmtPoint(on) + " " + num(chamferD_));
        modTarget_ = kInvalidId;
        break;
    }
    case Tool::Break: {
        if (modTarget_ == kInvalidId) {
            Vec3 on;
            const EntityId id = pickAt(p, &on);
            if (id == kInvalidId) break;
            modTarget_ = id;
            modPick_ = on;
            break;
        }
        emit runCommand("BREAK " + QString::number(modTarget_) + " " + fmtPoint(modPick_) + " " + fmtPoint(p));
        modTarget_ = kInvalidId;
        break;
    }
    case Tool::DimAngular:
        pts_.push_back(p);
        if (pts_.size() == 4) {
            emit runCommand("DIMANGULAR " + fmtPoint(pts_[0]) + " " + fmtPoint(pts_[1]) + " " + fmtPoint(pts_[2]) + " " + fmtPoint(pts_[3]) + " " +
                            num(dimTextHeight()));
            pts_.clear();
        }
        break;
    case Tool::Symbol:
        emit runCommand("SYMBOL " + symKey_ + " " + fmtPoint(p) + " " + num(symScale_) + " " + num(symRot_));
        break;
    case Tool::MatchProp: {
        const EntityId id = pickAt(p, nullptr, false);
        if (id == kInvalidId) break;
        if (modTarget_ == kInvalidId) { modTarget_ = id; break; }
        if (id != modTarget_) emit runCommand("MATCHPROP " + QString::number(modTarget_) + " " + QString::number(id));
        break;
    }
    case Tool::LayPick: {
        const EntityId id = pickAt(p, nullptr, false);
        if (id == kInvalidId) break;
        emit runCommand(layCmd_ + " " + QString::number(id));
        tool_ = Tool::Select;
        break;
    }
    case Tool::Grip:
        emit runCommand("GRIP " + QString::number(gripId_) + " " + QString::number(gripIdx_) + " " + fmtPoint(p));
        pts_.clear();
        tool_ = Tool::Select;
        gripDrag_ = false;
        track_.reset();
        snap_.reset();
        break;
    }
    emitPrompt();
    update();
}

void Viewport::setSelection(std::vector<EntityId> ids) {
    sel_ = std::move(ids);
    emit selectionChanged();
    update();
}

void Viewport::documentChanged() {
    const auto before = sel_.size();
    sel_.erase(std::remove_if(sel_.begin(), sel_.end(), [&](EntityId id) { return !doc_.contains(id); }), sel_.end());
    if (sel_.size() != before) emit selectionChanged();
    update();
}

void Viewport::simulateCursor(const Vec3& w) {
    mouse_ = toScreen(w);
    mouseInside_ = true;
    updateSnap(mouse_);
    const Vec3 a = acquirePoint();
    emit cursorMoved(a.x, a.y);
    update();
}

// ---------------------------------------------------------------- bog'lanish

void Viewport::updateSnap(const QPointF& s) {
    snap_.reset();
    track_.reset();
    const Vec3 c = toWorld(s);
    acquired_ = c;
    if (tool_ == Tool::Select || isModifyTool()) return;  // tahrir asboblari obyekt tanlaydi — bog'lanishsiz
    if (snapModes_ != 0) {
        const Vec3* from = pts_.empty() ? nullptr : &pts_.back();
        const auto res = findSnaps(doc_, c, aperture(), snapModes_, from);
        if (!res.empty()) {
            snap_ = res.front();
            acquired_ = snap_->point;
            return;  // OSNAP ustun
        }
    }
    const auto base = trackBase();
    if (!base || (!ortho_ && !polar_)) return;
    const Vec3 v = c - *base;
    if (v.length2d() * scale_ < 2) return;
    if (ortho_) {
        const bool horiz = std::abs(v.x) >= std::abs(v.y);
        acquired_ = horiz ? Vec3{c.x, base->y, 0} : Vec3{base->x, c.y, 0};
        const double ang = horiz ? (v.x >= 0 ? 0.0 : 180.0) : (v.y >= 0 ? 90.0 : 270.0);
        track_ = Track{*base, ang, true};
        return;
    }
    // qutb: eng yaqin qadam; burchak farqi ekranda ~10 px ichida bo'lsa tortiladi
    const double ang = rad2deg(std::atan2(v.y, v.x));
    const double snapped = std::round(ang / polarStepDeg_) * polarStepDeg_;
    const double dist = v.length2d();
    const double lateral = std::abs(std::sin(deg2rad(ang - snapped))) * dist * scale_;
    if (lateral > 10.0 && std::abs(ang - snapped) > 3.0) return;
    const Vec3 u{std::cos(deg2rad(snapped)), std::sin(deg2rad(snapped)), 0};
    const double along = v.dot(u);
    if (along <= 0) return;
    acquired_ = *base + u * along;
    double a = std::fmod(snapped, 360.0);
    if (a < 0) a += 360.0;
    track_ = Track{*base, a, false};
}

// ---------------------------------------------------------------- sichqoncha va klaviatura

void Viewport::mousePressEvent(QMouseEvent* e) {
    setFocus();
    mouse_ = e->position();
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && spaceDown_)) {
        panning_ = true;
        panStart_ = e->position();
        panCenter_ = center_;
        return;
    }
    if (e->button() == Qt::RightButton) {  // AutoCAD odati: o'ng tugma — Enter
        if (tool_ != Tool::Select) finishTool();
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (tool_ != Tool::Select) {
        clickPoint(acquirePoint());
        return;
    }
    if (const auto g = gripAt(e->position())) {  // tutqich: bosish-bosish yoki sudrash
        beginGrip(g->first, g->second);
        gripDrag_ = true;
        gripPress_ = e->position();
        return;
    }
    boxing_ = true;
    boxStart_ = e->position();
}

void Viewport::mouseMoveEvent(QMouseEvent* e) {
    mouse_ = e->position();
    mouseInside_ = true;
    if (panning_) {
        const QPointF d = e->position() - panStart_;
        center_ = {panCenter_.x - d.x() / scale_, panCenter_.y + d.y() / scale_, 0};
    } else {
        updateSnap(mouse_);
        const auto hg = tool_ == Tool::Select && !boxing_ ? gripAt(mouse_) : std::nullopt;
        if (hg != hoverGrip_) hoverGrip_ = hg;
    }
    const Vec3 w = acquirePoint();
    emit cursorMoved(w.x, w.y);
    update();
}

void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    if (panning_ && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
        panning_ = false;
        return;
    }
    if (e->button() == Qt::LeftButton && gripDrag_) {
        gripDrag_ = false;
        if (tool_ == Tool::Grip && QLineF(gripPress_, e->position()).length() > 6) clickPoint(acquirePoint());
        return;
    }
    if (e->button() != Qt::LeftButton || !boxing_) return;
    boxing_ = false;
    const bool additive = e->modifiers() & Qt::ShiftModifier;
    std::vector<EntityId> hit;
    if (QLineF(boxStart_, e->position()).length() < 4) {
        const EntityId id = pickEntity(doc_, toWorld(e->position()), aperture());
        if (id != kInvalidId) hit.push_back(id);
    } else {
        BBox b;
        b.expand(toWorld(boxStart_));
        b.expand(toWorld(e->position()));
        hit = selectWindow(doc_, b, e->position().x() < boxStart_.x());
    }
    if (!additive) {
        sel_ = hit;
    } else {
        for (EntityId id : hit) {
            auto it = std::find(sel_.begin(), sel_.end(), id);
            if (it == sel_.end()) sel_.push_back(id); else sel_.erase(it);
        }
    }
    emit selectionChanged();
    update();
}

void Viewport::wheelEvent(QWheelEvent* e) {
    const double f = std::pow(1.0015, e->angleDelta().y());
    const Vec3 before = toWorld(e->position());
    scale_ = std::clamp(scale_ * f, kMinScale, kMaxScale);
    const Vec3 after = toWorld(e->position());
    center_ = {center_.x + before.x - after.x, center_.y + before.y - after.y, 0};
    update();
}

void Viewport::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Escape:
        if (tool_ != Tool::Select) cancelTool();
        else if (!sel_.empty()) setSelection({});
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter: finishTool(); return;
    case Qt::Key_Delete:
        if (!sel_.empty()) emit runCommand("ERASE " + selArg());
        return;
    case Qt::Key_Space:
        if (!e->isAutoRepeat()) spaceDown_ = true;
        return;
    default: QOpenGLWidget::keyPressEvent(e);
    }
}

void Viewport::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) spaceDown_ = false;
    QOpenGLWidget::keyReleaseEvent(e);
}

void Viewport::leaveEvent(QEvent*) {
    mouseInside_ = false;
    snap_.reset();
    update();
}

void Viewport::resizeGL(int, int) {}

// ---------------------------------------------------------------- chizish

QColor Viewport::colorOf(const IEntity& e, const QColor& inherit) const {
    int c = e.color();
    if (c == kColorByBlock) return inherit.isValid() ? inherit : aciColor(7, theme_.lightCanvas);
    if (c == kColorByLayer) {
        if (e.layer() == "0" && inherit.isValid()) return inherit;
        const auto l = doc_.layer(e.layer());
        c = l ? l->color : 7;
    }
    return aciColor(c, theme_.lightCanvas);
}

void Viewport::paintGL() {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme_.canvasBg);
    if (grid_) drawGrid(p);

    BBox view;
    view.expand(toWorld(QPointF(0, 0)));
    view.expand(toWorld(QPointF(width(), height())));
    const auto ids = doc_.query(view, true);
    std::vector<char> isSel;
    for (EntityId id : ids) {
        const IEntity* e = doc_.find(id);
        if (!e) continue;
        const bool selected = std::find(sel_.begin(), sel_.end(), id) != sel_.end();
        drawEntity(p, *e, QColor(), selected);
    }
    drawOverlay(p);
}

void Viewport::drawGrid(QPainter& p) {
    // qadam 1-2-5·10^k: ekranda kamida 14 px
    double step = std::pow(10.0, std::floor(std::log10(14.0 / scale_)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (step * m * scale_ >= 14.0) { step *= m; break; }
    const Vec3 a = toWorld(QPointF(0, height())), b = toWorld(QPointF(width(), 0));
    const long long i0 = static_cast<long long>(std::floor(a.x / step)), i1 = static_cast<long long>(std::ceil(b.x / step));
    const long long j0 = static_cast<long long>(std::floor(a.y / step)), j1 = static_cast<long long>(std::ceil(b.y / step));
    if ((i1 - i0) > 2000 || (j1 - j0) > 2000) return;
    QPen minor(theme_.gridMinor, 1), major(theme_.gridMajor, 1);
    for (long long i = i0; i <= i1; ++i) {
        p.setPen(i % 5 == 0 ? major : minor);
        const double x = toScreen({i * step, 0, 0}).x();
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
    }
    for (long long j = j0; j <= j1; ++j) {
        p.setPen(j % 5 == 0 ? major : minor);
        const double y = toScreen({0, j * step, 0}).y();
        p.drawLine(QPointF(0, y), QPointF(width(), y));
    }
    const QPointF o = toScreen({0, 0, 0});
    p.setPen(QPen(theme_.axisX, 1.4));
    p.drawLine(QPointF(o.x(), o.y()), QPointF(o.x() + 60, o.y()));
    p.setPen(QPen(theme_.axisY, 1.4));
    p.drawLine(QPointF(o.x(), o.y()), QPointF(o.x(), o.y() - 60));
}

void Viewport::drawEntity(QPainter& p, const IEntity& e, const QColor& inherit, bool selected, int depth) {
    const QColor col = previewColor_.isValid() ? previewColor_ : colorOf(e, inherit);
    QPen pen(selected && !previewColor_.isValid() ? theme_.selection : col, selected ? 2.4 : 1.25);
    pen.setCosmetic(true);
    if (selected) pen.setStyle(Qt::DashLine);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    auto arcPath = [&](const Vec3& c, double r, double a0, double sweep, QPainterPath& path, bool move) {
        const double rpx = r * scale_;
        const int n = std::clamp(static_cast<int>(std::abs(sweep) * std::sqrt(std::max(rpx, 1.0)) * 1.2), 2, 720);
        for (int i = 0; i <= n; ++i) {
            const double a = a0 + sweep * i / n;
            const QPointF q = toScreen({c.x + r * std::cos(a), c.y + r * std::sin(a), 0});
            if (i == 0 && move) path.moveTo(q); else path.lineTo(q);
        }
    };
    auto polyPath = [&](const PolylineEntity& pl) {
        QPainterPath path;
        const PolylineEntity t = pl.hasBulges() ? pl.tessellated(deg2rad(3.0)) : pl;
        const auto& v = t.vertices();
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i == 0) path.moveTo(toScreen(v[i])); else path.lineTo(toScreen(v[i]));
        }
        if (t.isClosed()) path.closeSubpath();
        return path;
    };

    switch (e.type()) {
    case EntityType::Point: {
        const QPointF q = toScreen(static_cast<const PointEntity&>(e).position());
        p.drawLine(q + QPointF(-4, -4), q + QPointF(4, 4));
        p.drawLine(q + QPointF(-4, 4), q + QPointF(4, -4));
        break;
    }
    case EntityType::Line: {
        const auto& l = static_cast<const LineEntity&>(e);
        p.drawLine(toScreen(l.startPoint()), toScreen(l.endPoint()));
        break;
    }
    case EntityType::Circle: {
        const auto& c = static_cast<const CircleEntity&>(e);
        const double r = c.radius() * scale_;
        if (r < 0.6) p.drawPoint(toScreen(c.center()));
        else p.drawEllipse(toScreen(c.center()), r, r);
        break;
    }
    case EntityType::Arc: {
        const auto& a = static_cast<const ArcEntity&>(e);
        QPainterPath path;
        arcPath(a.center(), a.radius(), a.startAngle(), a.sweep(), path, true);
        p.drawPath(path);
        break;
    }
    case EntityType::Polyline: p.drawPath(polyPath(static_cast<const PolylineEntity&>(e))); break;
    case EntityType::Text: {
        const auto& t = static_cast<const TextEntity&>(e);
        const double hpx = t.height() * scale_;
        const QPointF ins = toScreen(t.insertion());
        if (hpx < 3) {  // juda mayda: matn o'rniga chiziq
            p.drawLine(ins, ins + QPointF(std::max(2.0, hpx * 0.6 * t.text().size()), 0));
            break;
        }
        QFont f = font();
        f.setPixelSize(std::max(1, static_cast<int>(std::lround(hpx * 1.35))));
        p.save();
        p.setFont(f);
        p.translate(ins);
        p.rotate(-t.rotation() * 180.0 / M_PI);
        const QString s = QString::fromStdString(t.text());
        const QFontMetricsF fm(f);
        const double w = fm.horizontalAdvance(s);
        double dx = 0, dy = 0;
        if (t.hAlign() == TextHAlign::Center) dx = -w / 2; else if (t.hAlign() == TextHAlign::Right) dx = -w;
        switch (t.vAlign()) {
        case TextVAlign::Baseline: dy = 0; break;
        case TextVAlign::Bottom: dy = -fm.descent(); break;
        case TextVAlign::Middle: dy = hpx / 2; break;
        case TextVAlign::Top: dy = hpx; break;
        }
        p.drawText(QPointF(dx, dy), s);
        p.restore();
        break;
    }
    case EntityType::Insert: {
        const auto& ins = static_cast<const InsertEntity&>(e);
        const BBox b = ins.bbox();
        const double spx = std::max(b.max.x - b.min.x, b.max.y - b.min.y) * scale_;
        if (spx < 3 || depth > 8) {
            p.drawRect(QRectF(toScreen(b.min), toScreen(b.max)).normalized());
            break;
        }
        for (const auto& child : ins.explode()) drawEntity(p, *child, col, selected, depth + 1);
        break;
    }
    case EntityType::Hatch: {
        const auto& h = static_cast<const HatchEntity&>(e);
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        for (const auto& loop : h.loops()) path.addPath(polyPath(loop));
        QColor fill = selected ? theme_.selection : col;
        QString pat = QString::fromStdString(h.pattern()).toUpper();
        fill.setAlpha(pat == "SOLID" ? 150 : 60);
        p.fillPath(path, fill);
        if (pat != "SOLID") {  // naqsh: 45° shtrix, ekranga mos zichlikda
            p.save();
            p.setClipPath(path);
            QPen hp(selected ? theme_.selection : col, 1);
            hp.setCosmetic(true);
            p.setPen(hp);
            const QRectF r = path.boundingRect();
            const double gap = std::max(6.0, h.patternScale() * scale_ * 0.25);
            for (double x = r.left() - r.height(); x < r.right(); x += gap)
                p.drawLine(QPointF(x, r.bottom()), QPointF(x + r.height(), r.top()));
            p.restore();
        }
        p.setPen(pen);
        p.drawPath(path);
        break;
    }
    case EntityType::Dimension:
        for (const auto& child : static_cast<const DimensionEntity&>(e).explode()) drawEntity(p, *child, col, selected, depth + 1);
        break;
    }
}

void Viewport::drawSnapMarker(QPainter& p, const SnapResult& s) {
    const QPointF q = toScreen(s.point);
    QPen pen(theme_.snapMarker, 2.2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const double r = 7;
    switch (s.mode) {
    case SnapEnd: p.drawRect(QRectF(q.x() - r, q.y() - r, 2 * r, 2 * r)); break;
    case SnapMid: {
        QPolygonF t{q + QPointF(0, -r - 1), q + QPointF(r + 1, r), q + QPointF(-r - 1, r)};
        p.drawPolygon(t);
        break;
    }
    case SnapCen: p.drawEllipse(q, r, r); break;
    case SnapQua: {
        QPolygonF d{q + QPointF(0, -r), q + QPointF(r, 0), q + QPointF(0, r), q + QPointF(-r, 0)};
        p.drawPolygon(d);
        break;
    }
    case SnapInt: p.drawLine(q + QPointF(-r, -r), q + QPointF(r, r)); p.drawLine(q + QPointF(-r, r), q + QPointF(r, -r)); break;
    case SnapPer: p.drawLine(q + QPointF(-r, r), q + QPointF(r, r)); p.drawLine(q + QPointF(-r, r), q + QPointF(-r, -r)); p.drawLine(q + QPointF(-r, 0), q + QPointF(0, 0)); p.drawLine(q + QPointF(0, 0), q + QPointF(0, r)); break;
    case SnapTan: p.drawEllipse(q, r * 0.8, r * 0.8); p.drawLine(q + QPointF(-r, -r), q + QPointF(r, -r)); break;
    case SnapNea: p.drawLine(q + QPointF(-r, -r), q + QPointF(r, -r)); p.drawLine(q + QPointF(r, -r), q + QPointF(-r, r)); p.drawLine(q + QPointF(-r, r), q + QPointF(r, r)); break;
    default: p.drawEllipse(q, r * 0.6, r * 0.6); p.drawLine(q + QPointF(-r, -r), q + QPointF(r, r)); break;
    }
    // yorliq
    const QString name = QString::fromStdString(loc_.keywordName(std::string(snapName(s.mode))));
    QFont f = font();
    f.setPixelSize(11);
    f.setBold(true);
    p.setFont(f);
    const QFontMetricsF fm(f);
    const QRectF box(q.x() + 12, q.y() + 10, fm.horizontalAdvance(name) + 12, 18);
    p.setPen(Qt::NoPen);
    p.setBrush(theme_.snapMarker);
    p.drawRoundedRect(box, 6, 6);
    p.setPen(QColor(20, 20, 20));
    p.drawText(box, Qt::AlignCenter, name);
}

EntityId Viewport::pickAt(const Vec3& p, Vec3* onCurve, bool curvesOnly) const {
    // Eng yaqin tahrirlanadigan egri (LINE/ARC/CIRCLE/POLYLINE); shtrix, o'lcham, blok — o'tkazib yuboriladi
    const double tol = aperture();
    BBox box;
    box.expand(p - Vec3{tol, tol, 0});
    box.expand(p + Vec3{tol, tol, 0});
    EntityId best = kInvalidId;
    double bestD = tol;
    Vec3 bestQ = p;
    for (EntityId id : doc_.query(box, true)) {
        const IEntity* e = doc_.find(id);
        if (!e || !doc_.isEditable(id)) continue;
        const auto t = e->type();
        if (curvesOnly && t != EntityType::Line && t != EntityType::Arc && t != EntityType::Circle && t != EntityType::Polyline) continue;
        for (const auto& pr : geom::primitivesOf(*e)) {
            const Vec3 q = geom::closestPoint(pr, p);
            const double d = (q - p).length2d();
            if (d < bestD || (d == bestD && id > best)) { bestD = d; best = id; bestQ = q; }
        }
    }
    if (onCurve) *onCurve = bestQ;
    return best;
}

std::vector<geom::Prim> Viewport::edgesAround(EntityId self, bool everything) const {
    std::vector<geom::Prim> out;
    BBox box = everything ? doc_.extents(true) : doc_.find(self)->bbox();
    for (EntityId id : doc_.query(box, true)) {
        if (id == self) continue;
        auto pr = geom::primitivesOf(*doc_.find(id));
        out.insert(out.end(), pr.begin(), pr.end());
    }
    return out;
}

std::unique_ptr<IEntity> Viewport::previewEntity(const Vec3& cur) const {
    try {
        const double h = dimTextHeight();
        switch (tool_) {
        case Tool::Grip:
            if (const IEntity* e = doc_.find(gripId_)) return withGripMoved(*e, static_cast<std::size_t>(gripIdx_), cur);
            break;
        case Tool::DimLinear:
            if (pts_.size() == 2) return std::make_unique<DimensionEntity>(DimensionEntity::linear(pts_[0], pts_[1], cur, h));
            break;
        case Tool::DimAligned:
            if (pts_.size() == 2) return std::make_unique<DimensionEntity>(DimKind::Aligned, pts_[0], pts_[1], cur, h);
            break;
        case Tool::Offset:
            if (const IEntity* e = doc_.find(modTarget_)) return offsetEntity(*e, offsetDist_, cur);
            break;
        case Tool::Extend: {
            Vec3 on;
            const EntityId id = pickAt(cur, &on);
            if (const IEntity* e = doc_.find(id)) return extendEntity(*e, edgesAround(id, true), on);
            break;
        }
        case Tool::Fillet: {
            Vec3 on;
            const EntityId id = pickAt(cur, &on);
            auto* l1 = dynamic_cast<const LineEntity*>(doc_.find(modTarget_));
            auto* l2 = dynamic_cast<const LineEntity*>(doc_.find(id));
            if (!l1 || !l2 || id == modTarget_) break;
            auto r = filletLines(*l1, modPick_, *l2, on, filletR_);
            if (!r.arc) return std::move(r.first);
            return std::move(r.arc);  // yoy ko'rinishi (chiziqlar quyida alohida)
        }
        case Tool::DimRadius:
        case Tool::DimDiameter: {
            const IEntity* e = doc_.find(dimTarget_);
            Vec3 c;
            double r = 0;
            if (auto* ci = dynamic_cast<const CircleEntity*>(e)) { c = ci->center(); r = ci->radius(); }
            else if (auto* ar = dynamic_cast<const ArcEntity*>(e)) { c = ar->center(); r = ar->radius(); }
            else break;
            Vec3 dir{1, 0, 0};
            if (const Vec3 v = cur - c; v.length2d() > 1e-12) dir = Vec3{v.x, v.y, 0} / v.length2d();
            if (tool_ == Tool::DimRadius) return std::make_unique<DimensionEntity>(DimKind::Radius, c, c + dir * r, cur, h);
            return std::make_unique<DimensionEntity>(DimKind::Diameter, c - dir * r, c + dir * r, cur, h);
        }
        default: break;
        }
    } catch (...) {
    }
    return nullptr;
}

void Viewport::drawPreviewEntity(QPainter& p, const IEntity& e) {
    previewColor_ = theme_.rubber;
    drawEntity(p, e, QColor(), false);
    previewColor_ = QColor();
}

void Viewport::tip(QPainter& p, const QPointF& at, const QString& text, const QColor& bg) {
    QFont f = font();
    f.setPixelSize(11);
    f.setBold(true);
    p.setFont(f);
    const QFontMetricsF fm(f);
    QRectF box(at.x() + 16, at.y() + 18, fm.horizontalAdvance(text) + 14, 20);
    if (box.right() > width() - 4) box.moveRight(at.x() - 16);
    if (box.bottom() > height() - 4) box.moveBottom(at.y() - 16);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(box, 6, 6);
    p.setPen(bg.lightness() > 140 ? QColor(20, 20, 20) : QColor(245, 245, 245));
    p.drawText(box, Qt::AlignCenter, text);
}

void Viewport::drawTracking(QPainter& p) {
    const auto base = trackBase();
    if (!base) return;
    const Vec3 cur = acquirePoint();
    const double len = cur.distanceTo(*base);
    if (track_) {  // nuqtali kuzatuv nuri ekran chetigacha
        const QPointF a = toScreen(track_->base);
        const double ar = deg2rad(track_->angleDeg);
        const QPointF far = a + QPointF(std::cos(ar), -std::sin(ar)) * (width() + height()) * 2.0;
        QPen tp(theme_.snapMarker, 1.0, Qt::DotLine);
        tp.setCosmetic(true);
        p.setPen(tp);
        p.drawLine(a, far);
        const QString name = QString::fromStdString(loc_.tr(track_->ortho ? "gui.status.ortho" : "gui.status.polar"));
        tip(p, toScreen(cur), QStringLiteral("%1: %2 < %3°").arg(name, QString::number(len, 'f', 3),
                                                              QString::number(track_->angleDeg, 'f', 0)),
            theme_.snapMarker);
    } else if (!snap_ && len > 0) {
        double ang = rad2deg(std::atan2(cur.y - base->y, cur.x - base->x));
        if (ang < 0) ang += 360.0;
        QColor bg = theme_.crosshair;
        bg.setAlpha(220);
        tip(p, toScreen(cur), QStringLiteral("%1 < %2°").arg(QString::number(len, 'f', 3), QString::number(ang, 'f', 1)), bg);
    }
}

void Viewport::drawGrips(QPainter& p) {
    if (sel_.empty() || sel_.size() > kMaxGripObjects) return;
    const QColor cold(36, 110, 255), hot(232, 60, 60), hover(40, 190, 90);
    p.setPen(QPen(QColor(10, 20, 40), 1));
    for (EntityId id : sel_) {
        const IEntity* e = doc_.find(id);
        if (!e || !doc_.isEditable(id)) continue;
        const auto g = gripPoints(*e);
        for (std::size_t i = 0; i < g.size(); ++i) {
            const bool isHover = hoverGrip_ && hoverGrip_->first == id && hoverGrip_->second == static_cast<int>(i);
            p.setBrush(isHover ? hover : cold);
            const QPointF q = toScreen(g[i]);
            p.drawRect(QRectF(q.x() - 4.5, q.y() - 4.5, 9, 9));
        }
    }
    if (tool_ == Tool::Grip) {
        p.setBrush(hot);
        const QPointF q = toScreen(pts_.empty() ? Vec3{} : pts_[0]);
        p.drawRect(QRectF(q.x() - 4.5, q.y() - 4.5, 9, 9));
    }
}

void Viewport::drawOverlay(QPainter& p) {
    // asbob oldindan ko'rinishi (rezina chiziq)
    if (tool_ != Tool::Select && mouseInside_) {
        const Vec3 cur = acquirePoint();
        QPen rp(theme_.rubber, 1.4, Qt::DashLine);
        rp.setCosmetic(true);
        p.setPen(rp);
        p.setBrush(Qt::NoBrush);
        const QPointF c = toScreen(cur);
        if (!pts_.empty()) {
            const QPointF a = toScreen(pts_.back());
            switch (tool_) {
            case Tool::Line:
            case Tool::Move:
            case Tool::Copy:
            case Tool::Rotate: p.drawLine(a, c); break;
            case Tool::Grip:
            case Tool::DimLinear:
            case Tool::DimAligned:
                if (auto pe = previewEntity(cur)) drawPreviewEntity(p, *pe);
                else p.drawLine(a, c);
                p.setPen(rp);
                if (tool_ == Tool::Grip) p.drawLine(a, c);
                break;
            case Tool::Rect: {
                const QPointF q = toScreen(pts_[0]);
                p.drawRect(QRectF(q, c).normalized());
                break;
            }
            case Tool::Stretch:
                if (pts_.size() == 1) {  // kesuvchi oyna — yashil, uzuq
                    QPen wp(QColor(70, 200, 110), 1.2, Qt::DashLine);
                    wp.setCosmetic(true);
                    p.setPen(wp);
                    p.setBrush(QColor(70, 200, 110, 30));
                    p.drawRect(QRectF(toScreen(pts_[0]), c).normalized());
                } else {
                    p.drawRect(QRectF(toScreen(pts_[0]), toScreen(pts_[1])).normalized());
                    if (pts_.size() == 3) p.drawLine(toScreen(pts_[2]), c);
                }
                break;
            case Tool::Polygon:
                try {
                    auto pg = makePolygon(pts_[0], polySides_, std::max(1e-9, (cur - pts_[0]).length2d()), true,
                                          std::atan2(cur.y - pts_[0].y, cur.x - pts_[0].x));
                    QPolygonF poly;
                    for (const auto& v : pg->vertices()) poly << toScreen(v);
                    p.drawPolygon(poly);
                } catch (...) {}
                break;
            case Tool::Ellipse:
                if (pts_.size() == 1) p.drawLine(a, c);
                else {
                    const Vec3 cc = pts_[0], ax = pts_[1];
                    const double ra = (ax - cc).length2d();
                    const Vec3 u = Vec3{ax.x - cc.x, ax.y - cc.y, 0} / std::max(1e-12, ra);
                    const double rb = std::abs((cur.x - cc.x) * -u.y + (cur.y - cc.y) * u.x);
                    if (ra > 1e-9 && rb > 1e-9) {
                        auto el = makeEllipse(cc, ra, rb, std::atan2(u.y, u.x), 64);
                        QPolygonF poly;
                        for (const auto& v : el->vertices()) poly << toScreen(v);
                        p.drawPolygon(poly);
                    }
                }
                break;
            case Tool::Spline:
                try {
                    auto pts = pts_;
                    pts.push_back(cur);
                    auto sp = makeSpline(pts, false, 10);
                    QPolygonF poly;
                    for (const auto& v : sp->vertices()) poly << toScreen(v);
                    p.drawPolyline(poly);
                } catch (...) {}
                break;
            case Tool::Leader:
            case Tool::Scale:
            case Tool::Mirror:
            case Tool::DimAngular: {
                QPainterPath path(toScreen(pts_[0]));
                for (std::size_t i = 1; i < pts_.size(); ++i) path.lineTo(toScreen(pts_[i]));
                path.lineTo(c);
                p.drawPath(path);
                break;
            }
            case Tool::Pline: {
                QPainterPath path(toScreen(pts_[0]));
                for (std::size_t i = 1; i < pts_.size(); ++i) path.lineTo(toScreen(pts_[i]));
                path.lineTo(c);
                p.drawPath(path);
                break;
            }
            case Tool::Circle: {
                const double r = QLineF(toScreen(pts_[0]), c).length();
                p.drawEllipse(toScreen(pts_[0]), r, r);
                p.drawLine(toScreen(pts_[0]), c);
                break;
            }
            case Tool::Arc:
                if (pts_.size() == 1) { p.drawLine(a, c); }
                else {
                    try {
                        const ArcEntity arc = ArcEntity::fromThreePoints(pts_[0], pts_[1], cur);
                        QPainterPath path;
                        const int n = 96;
                        for (int i = 0; i <= n; ++i) {
                            const double ang = arc.startAngle() + arc.sweep() * i / n;
                            const QPointF q = toScreen({arc.center().x + arc.radius() * std::cos(ang), arc.center().y + arc.radius() * std::sin(ang), 0});
                            if (i == 0) path.moveTo(q); else path.lineTo(q);
                        }
                        p.drawPath(path);
                    } catch (...) { p.drawLine(a, c); }
                }
                break;
            default: break;
            }
            // olingan nuqtalar
            p.setPen(Qt::NoPen);
            p.setBrush(theme_.rubber);
            for (const auto& q : pts_) p.drawEllipse(toScreen(q), 3, 3);
        } else if (dimTarget_ != kInvalidId || tool_ == Tool::Offset || tool_ == Tool::Extend || tool_ == Tool::Fillet) {
            if (auto pe = previewEntity(cur)) drawPreviewEntity(p, *pe);
            if (tool_ == Tool::Fillet && modTarget_ != kInvalidId) {  // ikkala chiziqning yangi holati
                Vec3 on;
                const EntityId id = pickAt(cur, &on);
                auto* l1 = dynamic_cast<const LineEntity*>(doc_.find(modTarget_));
                auto* l2 = dynamic_cast<const LineEntity*>(doc_.find(id));
                if (l1 && l2 && id != modTarget_) {
                    try {
                        auto r = filletLines(*l1, modPick_, *l2, on, filletR_);
                        drawPreviewEntity(p, *r.first);
                        drawPreviewEntity(p, *r.second);
                    } catch (...) {
                    }
                }
            }
        }
        if (tool_ == Tool::Trim) {  // olib tashlanadigan qism qizil
            Vec3 on;
            const EntityId id = pickAt(cur, &on);
            if (const IEntity* e = doc_.find(id)) {
                try {
                    auto keep = trimEntity(*e, edgesAround(id, false), on);
                    previewColor_ = QColor(255, 40, 70);
                    drawEntity(p, *e, QColor(), true);  // olib tashlanadigan qism — qalin qizil punktir
                    for (const auto& k : keep) {
                        previewColor_ = colorOf(*e, QColor());
                        drawEntity(p, *k, QColor(), false);
                    }
                    previewColor_ = QColor();
                } catch (...) {
                    previewColor_ = QColor();
                }
            }
        }
        if (isModifyTool()) {  // tanlov ostidagi obyekt / tanlangan birinchi obyekt
            if (const IEntity* e = doc_.find(modTarget_)) {
                previewColor_ = theme_.selection;
                drawEntity(p, *e, QColor(), true);
                previewColor_ = QColor();
            }
        }
        drawTracking(p);
    }
    if (tool_ == Tool::Select || tool_ == Tool::Grip) drawGrips(p);
    // tanlash ramkasi
    if (boxing_ && QLineF(boxStart_, mouse_).length() >= 4) {
        const bool crossing = mouse_.x() < boxStart_.x();
        QColor c = crossing ? theme_.crossingSel : theme_.windowSel;
        QPen bp(c, 1.2, crossing ? Qt::DashLine : Qt::SolidLine);
        p.setPen(bp);
        c.setAlpha(40);
        p.setBrush(c);
        p.drawRect(QRectF(boxStart_, mouse_).normalized());
    }
    if (snap_) drawSnapMarker(p, *snap_);
    // kursor: nishon + qidiruv kvadrati
    if (mouseInside_ && !panning_) {
        QPen cp(theme_.crosshair, 1);
        p.setPen(cp);
        p.setBrush(Qt::NoBrush);
        const QPointF m = mouse_;
        const double arm = tool_ == Tool::Select ? 22 : 40;
        p.drawLine(QPointF(m.x() - arm, m.y()), QPointF(m.x() + arm, m.y()));
        p.drawLine(QPointF(m.x(), m.y() - arm), QPointF(m.x(), m.y() + arm));
        if (tool_ == Tool::Select) p.drawRect(QRectF(m.x() - 5, m.y() - 5, 10, 10));
    }
}

} // namespace asiacad::gui
