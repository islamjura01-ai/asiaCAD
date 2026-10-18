// AsiaCAD CLI — chizmachilik buyruqlari (AutoCAD "Home" lentasi: Draw / Modify / Layers / Annotation) va genplan belgilari.
#include "interpreter.h"

#include <asiacad/bim/steel.h>
#include <asiacad/bim/templates.h>
#include <asiacad/civil/symbols.h>
#include <asiacad/core/draft.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/core/grips.h>
#include <asiacad/core/snap.h>
#include <asiacad/i18n/utf8.h>

#include <format>
#include <fstream>
#include <sstream>
#include <optional>
#include <set>

namespace asiacad::cli {

namespace {
std::string n2(double v) { return std::format("{:.3f}", v); }
bool looksPoint(const std::string& t) { return t.find(',') != std::string::npos; }
}  // namespace

bool Interpreter::dispatchDraft(const std::string& cmd, const Args& a) {
    auto usage = [&]() { return Error("err.usage", {loc_.commandName(cmd) + " " + localizeSyntax(loc_, commandSyntax(cmd))}); };
    auto up = [](const std::string& s) { return utf8::toUpper(s); };
    auto replaceMany = [&](std::vector<std::pair<EntityId, std::unique_ptr<IEntity>>> reps, EntityList adds, std::vector<EntityId> dels = {}) {
        auto comp = std::make_unique<CompositeCommand>(cmd);
        std::vector<AddEntityCommand*> ac;
        for (auto& [id, e] : reps) comp->add(std::make_unique<ReplaceEntityCommand>(id, std::move(e), cmd));
        for (auto& e : adds) {
            auto c = std::make_unique<AddEntityCommand>(std::move(e), cmd);
            ac.push_back(c.get());
            comp->add(std::move(c));
        }
        if (!dels.empty()) comp->add(std::make_unique<DeleteCommand>(dels));
        stack_.execute(std::move(comp));
        last_.clear();
        for (auto& [id, e] : reps) last_.push_back(id);
        for (auto* c : ac) last_.push_back(c->entityId());
    };
    auto layerLocked = [&](const std::string& l) {
        const auto L = doc_.layer(l);
        return L && (L->locked || L->frozen || !L->visible);
    };

    // ---------------------------------------------------------------- Draw
    if (cmd == "RECTANG") {
        need(a, 2, cmd);
        double r = 0, ch = 0;
        for (std::size_t k = 2; k < a.size(); ++k) {
            const std::string u = up(a[k]);
            if (u.rfind("R=", 0) == 0) r = parseNumber(a[k].substr(2));
            else if (u.rfind("C=", 0) == 0) ch = parseNumber(a[k].substr(2));
            else throw usage();
        }
        addAndReport(cmd, makeRectangle(parsePoint(a[0]), parsePoint(a[1]), r, ch), "msg.created");
    } else if (cmd == "POLYGON") {
        need(a, 3, cmd);
        const int n = static_cast<int>(parseNumber(a[0]));
        const Vec3 c = parsePoint(a[1]);
        bool inscribed = true;
        double rot = 0, rad = 0;
        if (looksPoint(a[2])) {  // radius nuqta bilan: yo'nalishi — birinchi uch
            const Vec3 p = parsePoint(a[2]);
            rad = (p - c).length2d();
            rot = std::atan2(p.y - c.y, p.x - c.x);
        } else rad = parseNumber(a[2]);
        for (std::size_t k = 3; k < a.size(); ++k) {
            const std::string u = up(a[k]);
            if (u == "I") inscribed = true;
            else if (u == "C") inscribed = false;
            else rot = deg2rad(parseNumber(a[k]));
        }
        addAndReport(cmd, makePolygon(c, n, rad, inscribed, rot), "msg.created");
    } else if (cmd == "ELLIPSE") {
        need(a, 3, cmd);
        const Vec3 c = parsePoint(a[0]);
        double ra, rb, rot = 0;
        if (looksPoint(a[1])) {  // birinchi o'q oxiri nuqta bilan
            const Vec3 p = parsePoint(a[1]);
            ra = (p - c).length2d();
            rot = std::atan2(p.y - c.y, p.x - c.x);
        } else ra = parseNumber(a[1]);
        rb = parseNumber(a[2]);
        if (a.size() > 3) rot = deg2rad(parseNumber(a[3]));
        addAndReport(cmd, makeEllipse(c, ra, rb, rot), "msg.created");
    } else if (cmd == "SPLINE") {
        need(a, 2, cmd);
        std::vector<Vec3> pts;
        bool closed = false;
        for (const auto& t : a) {
            if (loc_.isKeyword(t, "CLOSE")) closed = true;
            else pts.push_back(parsePoint(t));
        }
        addAndReport(cmd, makeSpline(pts, closed), "msg.created");
    }
    // ---------------------------------------------------------------- Modify
    else if (cmd == "STRETCH") {
        need(a, 4, cmd);
        const Vec3 w1 = parsePoint(a[0]), w2 = parsePoint(a[1]);
        const Vec3 d = parsePoint(a[3]) - parsePoint(a[2]);
        std::vector<std::pair<EntityId, std::unique_ptr<IEntity>>> reps;
        for (EntityId id : doc_.ids()) {
            const IEntity* e = doc_.find(id);
            if (!e || layerLocked(e->layer())) continue;
            if (auto s = stretchEntity(*e, w1, w2, d)) reps.push_back({id, std::move(s)});
        }
        if (reps.empty()) throw Error("err.stretch_none");
        const std::size_t n = reps.size();
        replaceMany(std::move(reps), {});
        say("msg.stretched", {std::to_string(n)});
    } else if (cmd == "ARRAY") {
        need(a, 2, cmd);
        const auto sel = parseSelection(a[0]);
        if (sel.empty()) throw Error("err.empty_selection");
        const std::string mode = up(a[1]);
        std::vector<Transform> tr;
        if (mode == "R") {  // ARRAY sel R qatorlar ustunlar dx dy
            need(a, 6, cmd);
            tr = arrayRect(static_cast<int>(parseNumber(a[2])), static_cast<int>(parseNumber(a[3])), parseNumber(a[4]), parseNumber(a[5]));
        } else if (mode == "P") {  // ARRAY sel P markaz soni [to'ldirish°] [NOROT]
            need(a, 4, cmd);
            double fill = 360;
            bool rot = true;
            for (std::size_t k = 4; k < a.size(); ++k) {
                if (up(a[k]) == "NOROT") rot = false;
                else fill = parseNumber(a[k]);
            }
            BBox b;
            for (EntityId id : sel)
                if (const IEntity* e = doc_.find(id)) b.expand(e->bbox());
            const Vec3 base = b.valid() ? (b.min + b.max) * 0.5 : Vec3{};
            tr = arrayPolar(parsePoint(a[2]), static_cast<int>(parseNumber(a[3])), fill, rot, base);
        } else if (mode == "PATH") {  // ARRAY sel PATH yo'l_id soni|S=qadam [NOALIGN]
            need(a, 4, cmd);
            const auto pid = parseSelection(a[2]);
            const IEntity* path = pid.empty() ? nullptr : doc_.find(pid.front());
            if (!path) throw Error("core.not_found", {a[2]});
            int count = 0;
            double spacing = 0;
            bool align = true;
            for (std::size_t k = 3; k < a.size(); ++k) {
                const std::string u = up(a[k]);
                if (u == "NOALIGN") align = false;
                else if (u.rfind("S=", 0) == 0) spacing = parseNumber(a[k].substr(2));
                else count = static_cast<int>(parseNumber(a[k]));
            }
            BBox b;
            for (EntityId id : sel)
                if (const IEntity* e = doc_.find(id)) b.expand(e->bbox());
            const Vec3 base = b.valid() ? (b.min + b.max) * 0.5 : Vec3{};
            tr = arrayPath(*path, base, count, spacing, align);
        } else throw usage();
        auto& c = static_cast<CopyCommand&>(stack_.execute(std::make_unique<CopyCommand>(sel, tr)));
        last_ = c.createdIds();
        say("msg.copies_created", {std::to_string(last_.size())});
    } else if (cmd == "CHAMFER") {
        need(a, 3, cmd);
        const Vec3 p1 = parsePoint(a[0]), p2 = parsePoint(a[1]);
        const double d1 = parseNumber(a[2]), d2 = a.size() > 3 ? parseNumber(a[3]) : d1;
        const EntityId i1 = pickEntity(doc_, p1, aperture_), i2 = pickEntity(doc_, p2, aperture_);
        auto* l1 = dynamic_cast<const LineEntity*>(doc_.find(i1));
        auto* l2 = dynamic_cast<const LineEntity*>(doc_.find(i2));
        if (!l1 || !l2 || i1 == i2) throw Error("err.fillet_lines");
        auto r = chamferLines(*l1, p1, *l2, p2, d1, d2);
        std::vector<std::pair<EntityId, std::unique_ptr<IEntity>>> reps;
        reps.push_back({i1, std::move(r.first)});
        reps.push_back({i2, std::move(r.second)});
        EntityList adds;
        if (r.bevel) adds.push_back(std::move(r.bevel));
        replaceMany(std::move(reps), std::move(adds));
        say("msg.done", {loc_.commandName(cmd)});
    } else if (cmd == "JOIN") {
        need(a, 1, cmd);
        const auto sel = parseSelection(a[0]);
        std::vector<const IEntity*> parts;
        std::vector<EntityId> ids;
        for (EntityId id : sel)
            if (const IEntity* e = doc_.find(id); e && (e->type() == EntityType::Line || e->type() == EntityType::Arc ||
                                                        (e->type() == EntityType::Polyline && !static_cast<const PolylineEntity*>(e)->isClosed()))) {
                parts.push_back(e);
                ids.push_back(id);
            }
        if (parts.size() < 2) throw Error("err.join_need");
        auto j = joinCurves(parts, std::max(1e-6, aperture_ * 0.01));
        if (!j) throw Error("err.join_gap");
        const bool closed = j->isClosed();
        std::vector<std::pair<EntityId, std::unique_ptr<IEntity>>> reps;
        reps.push_back({ids.front(), std::move(j)});
        replaceMany(std::move(reps), {}, std::vector<EntityId>(ids.begin() + 1, ids.end()));
        say("msg.joined", {std::to_string(ids.size()), loc_.tr(closed ? "msg.closed" : "msg.open")});
    } else if (cmd == "BREAK") {
        need(a, 2, cmd);
        const auto sel = parseSelection(a[0]);
        const IEntity* e = sel.empty() ? nullptr : doc_.find(sel.front());
        if (!e) throw Error("core.not_found", {a[0]});
        const Vec3 p1 = parsePoint(a[1]), p2 = a.size() > 2 ? parsePoint(a[2]) : p1;
        auto parts = breakCurve(*e, p1, p2);
        if (parts.empty()) {
            stack_.execute(std::make_unique<DeleteCommand>(std::vector<EntityId>{sel.front()}));
            last_.clear();
        } else {
            std::vector<std::pair<EntityId, std::unique_ptr<IEntity>>> reps;
            reps.push_back({sel.front(), std::move(parts.front())});
            EntityList rest;
            for (std::size_t i = 1; i < parts.size(); ++i) rest.push_back(std::move(parts[i]));
            replaceMany(std::move(reps), std::move(rest));
        }
        say("msg.broken", {std::to_string(last_.size())});
    } else if (cmd == "LENGTHEN") {
        need(a, 3, cmd);
        const auto sel = parseSelection(a[0]);
        const IEntity* e = sel.empty() ? nullptr : doc_.find(sel.front());
        if (!e) throw Error("core.not_found", {a[0]});
        std::vector<std::pair<EntityId, std::unique_ptr<IEntity>>> reps;
        reps.push_back({sel.front(), lengthenCurve(*e, parsePoint(a[1]), parseNumber(a[2]))});
        replaceMany(std::move(reps), {});
        say("msg.done", {loc_.commandName(cmd)});
    } else if (cmd == "MATCHPROP") {
        need(a, 2, cmd);
        const auto src = parseSelection(a[0]);
        const IEntity* s = src.empty() ? nullptr : doc_.find(src.front());
        if (!s) throw Error("core.not_found", {a[0]});
        auto dst = parseSelection(a[1]);
        std::erase(dst, src.front());
        stack_.execute(std::make_unique<ChangePropsCommand>(dst, s->layer(), s->color()));
        say("msg.matched", {std::to_string(dst.size()), s->layer()});
    }
    // ---------------------------------------------------------------- Layers
    else if (cmd == "LAYMCUR") {
        need(a, 1, cmd);
        const auto sel = parseSelection(a[0]);
        const IEntity* e = sel.empty() ? nullptr : doc_.find(sel.front());
        if (!e) throw Error("core.not_found", {a[0]});
        layer_ = e->layer();
        say("msg.layer_current", {layer_});
    } else if (cmd == "LAYMATCH") {  // LAYMATCH sel namuna — tanlov namunaning qatlamiga o'tadi
        need(a, 2, cmd);
        const auto ref = parseSelection(a[1]);
        const IEntity* r = ref.empty() ? nullptr : doc_.find(ref.front());
        if (!r) throw Error("core.not_found", {a[1]});
        const auto sel = parseSelection(a[0]);
        stack_.execute(std::make_unique<ChangePropsCommand>(sel, r->layer(), std::nullopt));
        say("msg.matched", {std::to_string(sel.size()), r->layer()});
    } else if (cmd == "LAYOFF" || cmd == "LAYFRZ" || cmd == "LAYLCK" || cmd == "LAYULK") {
        need(a, 1, cmd);
        std::set<std::string> names;
        for (EntityId id : parseSelection(a[0]))
            if (const IEntity* e = doc_.find(id)) names.insert(e->layer());
        if (names.empty()) throw Error("err.empty_selection");
        auto comp = std::make_unique<CompositeCommand>(cmd);
        for (const auto& n : names) {
            Layer L = doc_.layer(n).value_or(Layer{n});
            if (cmd == "LAYOFF") {
                if (n == layer_) throw Error("err.layer_current_off", {n});
                L.visible = false;
            } else if (cmd == "LAYFRZ") {
                if (n == layer_) throw Error("err.layer_current_off", {n});
                L.frozen = true;
            } else if (cmd == "LAYLCK") L.locked = true;
            else L.locked = false;
            comp->add(std::make_unique<SetLayerCommand>(L));
        }
        stack_.execute(std::move(comp));
        std::string list;
        for (const auto& n : names) list += (list.empty() ? "" : ", ") + n;
        say("msg.layers_changed", {loc_.commandName(cmd), list});
    } else if (cmd == "LAYON") {  // barcha qatlamlar yoqiladi va eritiladi
        auto comp = std::make_unique<CompositeCommand>(cmd);
        int n = 0;
        for (const auto& [name, l] : doc_.layers())
            if (!l.visible || l.frozen) {
                Layer L = l;
                L.visible = true;
                L.frozen = false;
                comp->add(std::make_unique<SetLayerCommand>(L));
                ++n;
            }
        if (n) stack_.execute(std::move(comp));
        isoSaved_.clear();
        say("msg.layers_on", {std::to_string(n)});
    } else if (cmd == "LAYISO") {  // tanlov qatlamlaridan boshqasi o'chiriladi (LAYUNISO — qaytarish)
        need(a, 1, cmd);
        std::set<std::string> keep;
        for (EntityId id : parseSelection(a[0]))
            if (const IEntity* e = doc_.find(id)) keep.insert(e->layer());
        if (keep.empty()) throw Error("err.empty_selection");
        auto comp = std::make_unique<CompositeCommand>(cmd);
        isoSaved_.clear();
        for (const auto& [name, l] : doc_.layers()) {
            if (keep.count(name) || !l.visible) continue;
            isoSaved_.push_back(l);
            Layer L = l;
            L.visible = false;
            comp->add(std::make_unique<SetLayerCommand>(L));
        }
        if (!keep.count(layer_)) layer_ = *keep.begin();
        stack_.execute(std::move(comp));
        say("msg.layiso", {std::to_string(keep.size()), std::to_string(isoSaved_.size())});
    } else if (cmd == "LAYUNISO") {
        if (isoSaved_.empty()) throw Error("err.layuniso");
        auto comp = std::make_unique<CompositeCommand>(cmd);
        for (const auto& l : isoSaved_) comp->add(std::make_unique<SetLayerCommand>(l));
        stack_.execute(std::move(comp));
        say("msg.layers_on", {std::to_string(isoSaved_.size())});
        isoSaved_.clear();
    }
    // ---------------------------------------------------------------- Annotation
    else if (cmd == "LEADER") {
        need(a, 3, cmd);
        std::vector<Vec3> pts;
        std::size_t k = 0;
        for (; k < a.size() && looksPoint(a[k]); ++k) pts.push_back(parsePoint(a[k]));
        if (pts.size() < 2 || k >= a.size()) throw usage();
        const std::string text = a[k++];
        const double h = k < a.size() ? parseNumber(a[k]) : dimHeight_;
        auto es = makeLeader(pts, text, h);
        addMany(cmd.c_str(), std::move(es));
        say("msg.created_n", {std::to_string(last_.size())});
    } else if (cmd == "DIMANGULAR") {
        need(a, 4, cmd);
        double h = dimHeight_;
        bool dms = false;
        for (std::size_t k = 4; k < a.size(); ++k) {
            if (up(a[k]) == "DMS") dms = true;
            else h = parseNumber(a[k]);
        }
        auto es = makeAngularDim(parsePoint(a[0]), parsePoint(a[1]), parsePoint(a[2]), parsePoint(a[3]), h, dms);
        std::string txt;
        for (const auto& e : es)
            if (auto* t = dynamic_cast<const TextEntity*>(e.get())) txt = t->text();
        addMany(cmd.c_str(), std::move(es));
        say("msg.dim_angle", {txt});
    } else if (cmd == "ID") {
        need(a, 1, cmd);
        const Vec3 p = parsePoint(a[0]);
        out_ << "X=" << n2(p.x) << "  Y=" << n2(p.y) << "  Z=" << n2(p.z) << '\n';
    }
    // ---------------------------------------------------------------- belgilar
    else if (cmd == "SYMBOL") {
        if (a.empty()) {  // ro'yxat
            std::string cat;
            for (const auto& s : civil::symbolCatalog()) {
                if (cat != s.category) {
                    cat = s.category;
                    out_ << loc_.tr(std::string("sym.cat.") + cat) << ":\n";
                }
                out_ << "  " << s.key << " — " << loc_.tr(std::string("sym.") + s.key) << '\n';
            }
            return true;
        }
        need(a, 2, cmd);
        std::string key = up(a[0]);
        // mahalliy nom bilan ham ("daraxt")
        for (const auto& s : civil::symbolCatalog())
            if (up(loc_.tr(std::string("sym.") + s.key)) == key) key = s.key;
        const std::string bn = civil::symbolBlockName(key);
        BlockPtr def = doc_.block(bn);
        auto comp = std::make_unique<CompositeCommand>(cmd);
        if (!def) {
            def = civil::makeSymbol(key);
            if (!def) throw Error("err.symbol_unknown", {a[0]});
            comp->add(std::make_unique<AddBlockCommand>(def));
        }
        const double k = a.size() > 2 ? parseNumber(a[2]) : 1.0;
        if (k <= 0) throw Error("err.factor_positive");
        const double rot = a.size() > 3 ? deg2rad(parseNumber(a[3])) : 0.0;
        // bir nechta nuqta: "x1,y1;x2,y2" — bir buyruqda
        std::vector<AddEntityCommand*> ac;
        std::string pts = a[1];
        for (std::size_t i = 0; i <= pts.size();) {
            std::size_t j = pts.find(';', i);
            if (j == std::string::npos) j = pts.size();
            const Vec3 p = parsePoint(pts.substr(i, j - i));
            auto ins = std::make_unique<InsertEntity>(def, InsertEntity::placement(def->base, p, k, k, k, rot));
            ins->setLayer(layer_);
            ins->setColor(color_);
            auto c = std::make_unique<AddEntityCommand>(std::move(ins), cmd);
            ac.push_back(c.get());
            comp->add(std::move(c));
            i = j + 1;
        }
        stack_.execute(std::move(comp));
        last_.clear();
        for (auto* c : ac) last_.push_back(c->entityId());
        say("msg.symbol_placed", {loc_.tr("sym." + key), std::to_string(last_.size())});
    }
    // ---------------------------------------------------------------- metall konstruksiyalar (КМ)
    else if (cmd == "STEEL") {
        if (a.empty()) {
            std::string fam;
            for (const auto& p : bim::steelCatalog()) {
                if (fam != bim::steelFamilyKey(p.family)) {
                    fam = bim::steelFamilyKey(p.family);
                    out_ << '\n' << loc_.tr("km.fam." + fam) << " (" << p.standard << "):\n ";
                }
                out_ << ' ' << p.name;
            }
            out_ << '\n';
            return true;
        }
        std::string nm;
        for (const auto& t : a) nm += t;  // "Двутавр 30" ham
        const auto p = bim::findSteel(nm);
        if (!p) throw Error("km.err.unknown", {nm});
        say("km.msg.info", {p->name, loc_.tr(std::string("km.fam.") + bim::steelFamilyKey(p->family)), p->standard, std::format("{:g}", p->h),
                            std::format("{:g}", p->b), std::format("{:g}", p->s), std::format("{:g}", p->t), std::format("{:.2f}", p->A),
                            std::format("{:.2f}", p->mass),
                            (p->Ix > 0 ? std::format("{:.0f}", p->Ix) : "—") + " / " + (p->Wx > 0 ? std::format("{:.1f}", p->Wx) : "—")});
    } else if (cmd == "STEELSEC") {
        need(a, 2, cmd);
        const auto p = bim::findSteel(a[0]);
        if (!p) throw Error("km.err.unknown", {a[0]});
        const Vec3 o = parsePoint(a[1]);
        const double k = a.size() > 2 ? parseNumber(a[2]) : 1.0;
        if (k <= 0) throw Error("err.factor_positive");
        EntityList es;
        std::vector<PolylineEntity> loops;
        for (const auto& lp : bim::steelSection(*p)) {
            std::vector<Vec3> v;
            for (const auto& q : lp) v.push_back(o + q * k);
            loops.emplace_back(v, true);
            auto pl = std::make_unique<PolylineEntity>(v, true);
            pl->setLayer("KM-KESIM");
            es.push_back(std::move(pl));
        }
        auto h = std::make_unique<HatchEntity>(loops, "ANSI31", std::max(0.002, p->h / 1000 * k / 12));
        h->setLayer("KM-KESIM");
        es.push_back(std::move(h));
        const double th = std::max(p->h, p->b) / 1000 * k * 0.12;
        auto t = std::make_unique<TextEntity>(o + Vec3{0, -p->h / 2000 * k - th * 1.6, 0}, th, p->name + "  " + p->standard, 0, TextHAlign::Center);
        t->setLayer("KM-YOZUV");
        es.push_back(std::move(t));
        addLayered(cmd, std::move(es));
        say("km.msg.section", {p->name});
    } else if (cmd == "STEELBEAM") {
        need(a, 3, cmd);
        const auto p = bim::findSteel(a[0]);
        if (!p) throw Error("km.err.unknown", {a[0]});
        std::vector<Vec3> pts;
        for (std::size_t k = 1; k < a.size(); ++k) pts.push_back(parsePoint(a[k]));
        EntityList es;
        std::unique_ptr<IEntity> axis;
        if (pts.size() == 2) axis = std::make_unique<LineEntity>(pts[0], pts[1]);
        else axis = std::make_unique<PolylineEntity>(pts, false);
        axis->setLayer(bim::steelLayer(*p));
        es.push_back(std::move(axis));
        double L = 0;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            L += pts[i].distanceTo(pts[i + 1]);
            const Vec3 m = (pts[i] + pts[i + 1]) * 0.5;
            const double ang = std::atan2(pts[i + 1].y - pts[i].y, pts[i + 1].x - pts[i].x);
            double rot = ang;
            if (rot > kPi / 2) rot -= kPi;
            if (rot < -kPi / 2) rot += kPi;
            const double th = std::clamp(p->h / 1000 * 1.2, 0.15, 0.5);
            auto t = std::make_unique<TextEntity>(Vec3{m.x - std::sin(rot) * th * 0.6, m.y + std::cos(rot) * th * 0.6, m.z}, th, p->name, rot, TextHAlign::Center);
            t->setLayer("KM-YOZUV");
            es.push_back(std::move(t));
        }
        addLayered(cmd, std::move(es));
        say("km.msg.member", {p->name, std::format("{:.3f}", L), std::format("{:.1f}", L * p->mass)});
    } else if (cmd == "STEELSPEC") {
        double reserve = 2.0;
        std::optional<Vec3> at;
        for (const auto& t : a) {
            if (looksPoint(t)) at = parsePoint(t);
            else reserve = parseNumber(t);
        }
        std::vector<std::string> unknown;
        const auto ms = bim::readSteel(doc_, &unknown);
        if (ms.empty()) throw Error("km.err.none");
        const auto sp = bim::steelSpec(ms, reserve);
        for (const auto& r : sp.rows)
            say("km.msg.row", {r.profile, r.standard, std::to_string(r.count), std::format("{:.2f}", r.length), std::format("{:.3f}", r.mass)});
        say("km.msg.total", {std::format("{:.3f}", sp.total), std::format("{:g}", reserve), std::format("{:.3f}", sp.reserve), std::format("{:.3f}", sp.total + sp.reserve)});
        for (const auto& u : unknown) out_ << "  " << loc_.tr("sewer.lvl.warning") << ": " << loc_.tr("km.warn.layer", {u}) << '\n';
        if (at) {  // vedomost jadvali chizmaga (GOST 21.502 uslubida: profil, GOST, soni, uzunlik, massa)
            EntityList es;
            const double hgt = 2.5, rh = 8, cw[5] = {35, 45, 20, 30, 30};
            const char* hdr[5] = {"km.col.profile", "km.col.standard", "km.col.count", "km.col.length", "km.col.mass"};
            const std::size_t nrows = sp.rows.size() + 3;
            double W = 0;
            for (double c : cw) W += c;
            auto line = [&](Vec3 p0, Vec3 p1) {
                auto l = std::make_unique<LineEntity>(p0, p1);
                l->setLayer("KM-SPEC");
                es.push_back(std::move(l));
            };
            auto txt = [&](double x, double y, const std::string& v) {
                auto t = std::make_unique<TextEntity>(Vec3{x, y, 0}, hgt, v, 0, TextHAlign::Left, TextVAlign::Middle);
                t->setLayer("KM-SPEC");
                es.push_back(std::move(t));
            };
            const Vec3 o = *at;
            auto T = std::make_unique<TextEntity>(o + Vec3{0, hgt * 2, 0}, hgt * 1.4, loc_.tr("km.spec.title"));
            T->setLayer("KM-SPEC");
            es.push_back(std::move(T));
            for (std::size_t r = 0; r <= nrows; ++r) line(o + Vec3{0, -rh * static_cast<double>(r), 0}, o + Vec3{W, -rh * static_cast<double>(r), 0});
            double x = 0;
            for (int c = 0; c <= 5; ++c) {
                line(o + Vec3{x, 0, 0}, o + Vec3{x, -rh * static_cast<double>(nrows), 0});
                if (c < 5) x += cw[c];
            }
            auto row = [&](std::size_t r, std::vector<std::string> v) {
                double xx = 0;
                for (int c = 0; c < 5; ++c) {
                    if (c < static_cast<int>(v.size())) txt(o.x + xx + 2, o.y - rh * (static_cast<double>(r) + 0.5), v[static_cast<std::size_t>(c)]);
                    xx += cw[c];
                }
            };
            std::vector<std::string> h;
            for (const char* k : hdr) h.push_back(loc_.tr(k));
            row(0, h);
            std::size_t r = 1;
            for (const auto& s2 : sp.rows)
                row(r++, {s2.profile, s2.standard, std::to_string(s2.count), std::format("{:.2f}", s2.length), std::format("{:.3f}", s2.mass)});
            row(r++, {loc_.tr("km.spec.reserve", {std::format("{:g}", reserve)}), "", "", "", std::format("{:.3f}", sp.reserve)});
            row(r++, {loc_.tr("km.spec.total"), "", "", "", std::format("{:.3f}", sp.total + sp.reserve)});
            addLayered(cmd, std::move(es));
        }
    }
    // ---------------------------------------------------------------- konstruksiya shablonlari (haqiqiy koordinata va belgida)
    else if (cmd == "STEELFRAME" || cmd == "STAIR" || cmd == "RAMP" || cmd == "RWALL") {
        // file.json | kalit=qiymat ... [x,y[,z]]  (z — tayanch nuqtaning absolyut belgisi; ro'yxat: dx=6;6;6)
        std::string js;
        if (a.size() == 1 && a[0].size() > 5 && up(a[0].substr(a[0].size() - 5)) == ".JSON") {
            std::ifstream f(a[0], std::ios::binary);
            if (!f) throw Error("acad.err.open", {a[0]});
            std::stringstream b;
            b << f.rdbuf();
            js = b.str();
        } else {
            js = "{";
            bool first = true;
            auto add = [&](const std::string& k, const std::string& v) {
                js += (first ? "\"" : ",\"") + k + "\":" + v;
                first = false;
            };
            for (const auto& t : a) {
                const auto eq = t.find('=');
                if (eq == std::string::npos) {
                    if (!looksPoint(t)) throw usage();
                    const Vec3 p = parsePoint(t);
                    add("x", std::format("{}", p.x));
                    add("y", std::format("{}", p.y));
                    if (std::count(t.begin(), t.end(), ',') >= 2) add("z0", std::format("{}", p.z));
                    continue;
                }
                const std::string k = t.substr(0, eq), v = t.substr(eq + 1);
                if (k.empty() || v.empty()) throw usage();
                if (v.find(';') != std::string::npos) {
                    std::string arr = "[";
                    std::stringstream ss(v);
                    std::string part;
                    for (bool f1 = true; std::getline(ss, part, ';'); f1 = false) arr += (f1 ? "" : ",") + std::format("{}", parseNumber(part));
                    add(k, arr + "]");
                } else if (up(v) == "TRUE" || up(v) == "FALSE") add(k, up(v) == "TRUE" ? "true" : "false");
                else {
                    bool isNum = true;
                    try { (void)parseNumber(v); } catch (const Error&) { isNum = false; }
                    if (isNum) add(k, std::format("{}", parseNumber(v)));
                    else {
                        std::string q;
                        for (char c : v) { if (c == '"' || c == '\\') q += '\\'; q += c; }
                        add(k, "\"" + q + "\"");
                    }
                }
            }
            js += "}";
            // mantiqiy kalitlar 0/1 bilan ham
            for (const char* bk : {"\"steel\":", "\"bracing\":"})
                for (const char* d : {"0", "1"}) {
                    const std::string from = std::string(bk) + d;
                    if (const auto q = js.find(from); q != std::string::npos && (q + from.size() == js.size() - 1 || js[q + from.size()] == ','))
                        js.replace(q, from.size(), std::string(bk) + (d[0] == '1' ? "true" : "false"));
                }
        }
        bim::TplResult r;
        if (cmd == "STEELFRAME") r = bim::makeSteelFrame(bim::steelFrameFromJson(js));
        else if (cmd == "STAIR") r = bim::makeStair(bim::stairFromJson(js));
        else if (cmd == "RAMP") r = bim::makeRamp(bim::rampFromJson(js));
        else r = bim::makeRetainingWall(bim::retainFromJson(js));
        const std::size_t n = r.entities.size();
        addLayered(cmd, std::move(r.entities));
        say("tpl.msg.done", {loc_.commandName(cmd), std::to_string(n)});
        for (const auto& [k, v] : r.info) out_ << "  " << loc_.tr(k) << ": " << v << '\n';
        for (const auto& is : r.issues)
            out_ << "  " << loc_.tr(is.error ? "sewer.lvl.error" : "sewer.lvl.warning") << ": " << loc_.tr(is.key, is.args) << '\n';
    } else if (cmd == "GEOPLACE") {
        // GEOPLACE sel p1 P1 [p2 P2] [GEO] [SCALE] [Z=H] — lokal nuqtalar geodezik koordinata/belgiga
        need(a, 3, cmd);
        const auto ids = parseSelection(a[0]);
        if (ids.empty()) throw Error("err.empty_selection");
        bool geo = false, scale = false;
        std::optional<double> zAbs;
        std::vector<std::string> pts;
        for (std::size_t k = 1; k < a.size(); ++k) {
            const std::string u = up(a[k]);
            if (u == "GEO") geo = true;
            else if (u == "SCALE") scale = true;
            else if (u.rfind("Z=", 0) == 0) zAbs = parseNumber(a[k].substr(2));
            else pts.push_back(a[k]);
        }
        if (pts.size() != 2 && pts.size() != 4) throw usage();
        auto world = [&](const std::string& t, const Vec3& local) {
            Vec3 P = parseRawPoint(t);
            if (geo) std::swap(P.x, P.y);  // geodeziya: X — shimol, Y — sharq
            if (std::count(t.begin(), t.end(), ',') < 2) P.z = local.z;  // belgi berilmagan — saqlanadi
            return P;
        };
        const Vec3 p1 = parsePoint(pts[0]);
        Vec3 P1 = world(pts[1], p1);
        if (zAbs) P1.z = *zAbs;
        std::optional<Vec3> p2, P2;
        if (pts.size() == 4) {
            p2 = parsePoint(pts[2]);
            P2 = world(pts[3], *p2);
        }
        const auto g = bim::geoFit(p1, P1, p2 ? &*p2 : nullptr, P2 ? &*P2 : nullptr, scale);
        stack_.execute(std::make_unique<TransformCommand>(ids, g.t, cmd));
        last_ = ids;
        say("tpl.msg.geo", {std::to_string(ids.size()), std::format("{:.3f}", geo ? P1.y : P1.x), std::format("{:.3f}", geo ? P1.x : P1.y), std::format("{:+.3f}", P1.z - p1.z),
                            std::format("{:.4f}", g.rotDeg)});
        if (p2) say("tpl.msg.geo2", {std::format("{:.6f}", g.scale), std::format("{:.3f}", g.residual), scale ? loc_.tr("tpl.geo.scaled") : loc_.tr("tpl.geo.rigid")});
    } else {
        return false;
    }
    return true;
}

} // namespace asiacad::cli
