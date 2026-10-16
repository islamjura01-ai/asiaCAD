#include <asiacad/core/dimension.h>
#include <asiacad/core/entities.h>
#include <asiacad/i18n/json.h>
#include <asiacad/io/acad_file.h>

#include <asiacad/i18n/utf8.h>

#include <charconv>
#include <functional>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace asiacad {

namespace {

// ============================================================ yozish
void writeNum(std::ostream& os, double v) {
    if (!std::isfinite(v)) { os << "null"; return; }
    char buf[32];
    auto [p, ec] = std::to_chars(buf, buf + sizeof buf, v);  // eng qisqa aniq round-trip
    os.write(buf, p - buf);
}

void writeStr(std::ostream& os, std::string_view s) {
    os << '"';
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (ch == '"') os << "\\\"";
        else if (ch == '\\') os << "\\\\";
        else if (ch == '\n') os << "\\n";
        else if (ch == '\t') os << "\\t";
        else if (c < 0x20) {
            static const char* hx = "0123456789abcdef";
            os << "\\u00" << hx[c >> 4] << hx[c & 0xF];
        } else os << ch;
    }
    os << '"';
}

void writeVec(std::ostream& os, const Vec3& p) {
    os << '[';
    writeNum(os, p.x);
    os << ',';
    writeNum(os, p.y);
    os << ',';
    writeNum(os, p.z);
    os << ']';
}

void key(std::ostream& os, const char* k) { os << ",\"" << k << "\":"; }

const char* hName(TextHAlign h) { return h == TextHAlign::Center ? "C" : h == TextHAlign::Right ? "R" : "L"; }
const char* vName(TextVAlign v) {
    switch (v) {
        case TextVAlign::Bottom: return "Bot";
        case TextVAlign::Middle: return "M";
        case TextVAlign::Top: return "T";
        default: return "B";
    }
}

void writeTextFields(std::ostream& os, const TextEntity& t) {
    key(os, "ins"); writeVec(os, t.insertion());
    key(os, "h"); writeNum(os, t.height());
    key(os, "rot"); writeNum(os, t.rotation());
    key(os, "halign"); writeStr(os, hName(t.hAlign()));
    key(os, "valign"); writeStr(os, vName(t.vAlign()));
    key(os, "text"); writeStr(os, t.text());
}

void writeLoop(std::ostream& os, const PolylineEntity& p) {
    os << "\"pts\":[";
    for (std::size_t i = 0; i < p.vertices().size(); ++i) {
        if (i) os << ',';
        writeVec(os, p.vertices()[i]);
    }
    os << ']';
    bool anyBulge = false;
    for (double b : p.bulges()) anyBulge |= b != 0.0;
    if (anyBulge) {
        os << ",\"bulges\":[";
        for (std::size_t i = 0; i < p.bulges().size(); ++i) {
            if (i) os << ',';
            writeNum(os, p.bulges()[i]);
        }
        os << ']';
    }
}

void writeEntity(std::ostream& os, const IEntity& e, bool withId = true) {
    os << '{';
    if (withId) os << "\"id\":" << e.id() << ',';
    os << "\"type\":";
    writeStr(os, toString(e.type()));
    key(os, "layer");
    writeStr(os, e.layer());
    key(os, "color");
    os << e.color();
    switch (e.type()) {
        case EntityType::Point:
            key(os, "p"); writeVec(os, static_cast<const PointEntity&>(e).position());
            break;
        case EntityType::Line: {
            const auto& l = static_cast<const LineEntity&>(e);
            key(os, "a"); writeVec(os, l.startPoint());
            key(os, "b"); writeVec(os, l.endPoint());
            break;
        }
        case EntityType::Circle: {
            const auto& c = static_cast<const CircleEntity&>(e);
            key(os, "c"); writeVec(os, c.center());
            key(os, "r"); writeNum(os, c.radius());
            break;
        }
        case EntityType::Arc: {
            const auto& a = static_cast<const ArcEntity&>(e);
            key(os, "c"); writeVec(os, a.center());
            key(os, "r"); writeNum(os, a.radius());
            key(os, "start"); writeNum(os, a.startAngle());
            key(os, "end"); writeNum(os, a.endAngle());
            break;
        }
        case EntityType::Polyline: {
            const auto& p = static_cast<const PolylineEntity&>(e);
            key(os, "closed"); os << (p.isClosed() ? "true" : "false");
            os << ',';
            writeLoop(os, p);
            break;
        }
        case EntityType::Text:
            writeTextFields(os, static_cast<const TextEntity&>(e));
            break;
        case EntityType::Insert: {
            const auto& in = static_cast<const InsertEntity&>(e);
            const Transform& m = in.matrix();
            key(os, "block"); writeStr(os, in.block()->name);
            key(os, "m");
            os << '[';
            const double v[8] = {m.a, m.b, m.c, m.d, m.tx, m.ty, m.sz, m.tz};
            for (int k = 0; k < 8; ++k) {
                if (k) os << ',';
                writeNum(os, v[k]);
            }
            os << ']';
            if (!in.attributes().empty()) {
                key(os, "attribs");
                os << '[';
                bool first = true;
                for (const auto& a : in.attributes()) {
                    if (!first) os << ',';
                    first = false;
                    os << "{\"tag\":";
                    writeStr(os, a.tag);
                    key(os, "layer"); writeStr(os, a.text.layer());
                    key(os, "color"); os << a.text.color();
                    writeTextFields(os, a.text);
                    os << '}';
                }
                os << ']';
            }
            break;
        }
        case EntityType::Hatch: {
            const auto& h = static_cast<const HatchEntity&>(e);
            key(os, "pattern"); writeStr(os, h.pattern());
            key(os, "scale"); writeNum(os, h.patternScale());
            key(os, "angle"); writeNum(os, h.patternAngle());
            key(os, "loops");
            os << '[';
            for (std::size_t k = 0; k < h.loops().size(); ++k) {
                if (k) os << ',';
                os << '{';
                writeLoop(os, h.loops()[k]);
                os << '}';
            }
            os << ']';
            break;
        }
        case EntityType::Dimension: {
            const auto& d = static_cast<const DimensionEntity&>(e);
            key(os, "kind"); writeStr(os, toString(d.kind()));
            key(os, "p1"); writeVec(os, d.p1());
            key(os, "p2"); writeVec(os, d.p2());
            key(os, "loc"); writeVec(os, d.location());
            key(os, "h"); writeNum(os, d.textHeight());
            key(os, "dec"); os << d.decimals();
            key(os, "rot"); writeNum(os, d.rotation());
            break;
        }
    }
    os << '}';
}

// ============================================================ o'qish
class EntityReader {
public:
    EntityReader(const json::Value& o, std::string ref) : o_(o), ref_(std::move(ref)) {}

    [[noreturn]] void bad(const char* field) const { throw Error("acad.err.entity", {ref_, field}); }
    const json::Value& raw() const { return o_; }
    const std::string& ref() const { return ref_; }

    double num(const char* k) const {
        const json::Value* v = o_.find(k);
        if (!v || !v->isNumber() || !std::isfinite(v->num())) bad(k);
        return v->num();
    }
    double num(const char* k, double def) const {
        const json::Value* v = o_.find(k);
        return v && v->isNumber() ? v->num() : def;
    }
    bool boolean(const char* k, bool def) const {
        const json::Value* v = o_.find(k);
        return v && v->isBool() ? v->boolean() : def;
    }
    std::string str(const char* k, std::string def = {}) const {
        const json::Value* v = o_.find(k);
        return v && v->isString() ? v->str() : def;
    }
    static bool toVec(const json::Value& v, Vec3& out) {
        if (!v.isArray() || v.arr().size() < 2 || v.arr().size() > 3) return false;
        for (const auto& c : v.arr())
            if (!c.isNumber() || !std::isfinite(c.num())) return false;
        out = {v.arr()[0].num(), v.arr()[1].num(), v.arr().size() == 3 ? v.arr()[2].num() : 0.0};
        return true;
    }
    Vec3 vec(const char* k) const {
        const json::Value* v = o_.find(k);
        Vec3 p;
        if (!v || !toVec(*v, p)) bad(k);
        return p;
    }
    std::vector<Vec3> vecs(const char* k) const {
        const json::Value* v = o_.find(k);
        if (!v || !v->isArray()) bad(k);
        std::vector<Vec3> out;
        for (const auto& item : v->arr()) {
            Vec3 p;
            if (!toVec(item, p)) bad(k);
            out.push_back(p);
        }
        return out;
    }
    std::vector<double> nums(const char* k) const {
        std::vector<double> out;
        const json::Value* v = o_.find(k);
        if (!v) return out;
        if (!v->isArray()) bad(k);
        for (const auto& item : v->arr()) {
            if (!item.isNumber()) bad(k);
            out.push_back(item.num());
        }
        return out;
    }

private:
    const json::Value& o_;
    std::string ref_;
};

using BlockMap = std::map<std::string, std::shared_ptr<BlockDef>>;  // kalit — katta harfdagi nom

void applyProps(IEntity& e, const EntityReader& r) {
    std::string layer = r.str("layer", "0");
    e.setLayer(layer.empty() ? "0" : layer);
    int color = static_cast<int>(r.num("color", kColorByLayer));
    if (color < 0 || color > 256) color = kColorByLayer;
    e.setColor(color);
}

std::unique_ptr<IEntity> makeEntity(const std::string& type, const EntityReader& r, const BlockMap& blocks);

std::unique_ptr<TextEntity> makeText(const EntityReader& r) {
    const std::string h = r.str("halign", "L"), v = r.str("valign", "B");
    const TextHAlign ha = h == "C" ? TextHAlign::Center : h == "R" ? TextHAlign::Right : TextHAlign::Left;
    const TextVAlign va = v == "Bot" ? TextVAlign::Bottom : v == "M" ? TextVAlign::Middle : v == "T" ? TextVAlign::Top
                                                                                                     : TextVAlign::Baseline;
    const double height = r.num("h");
    if (height <= 0) r.bad("h");
    return std::make_unique<TextEntity>(r.vec("ins"), height, r.str("text"), r.num("rot", 0.0), ha, va);
}

std::unique_ptr<IEntity> makeEntity(const std::string& type, const EntityReader& r, const BlockMap& blocks) {
    if (type == "INSERT") {
        auto it = blocks.find(utf8::toUpper(r.str("block")));
        if (it == blocks.end()) r.bad("block");
        const json::Value* mv = r.raw().find("m");
        if (!mv || !mv->isArray() || mv->arr().size() != 8) r.bad("m");
        double v[8];
        for (int k = 0; k < 8; ++k) {
            const auto& x = mv->arr()[static_cast<std::size_t>(k)];
            if (!x.isNumber() || !std::isfinite(x.num())) r.bad("m");
            v[k] = x.num();
        }
        Transform m;
        m.a = v[0]; m.b = v[1]; m.c = v[2]; m.d = v[3]; m.tx = v[4]; m.ty = v[5]; m.sz = v[6]; m.tz = v[7];
        auto ins = std::make_unique<InsertEntity>(it->second, m);
        if (const json::Value* av = r.raw().find("attribs"); av && av->isArray())
            for (const auto& a : av->arr()) {
                if (!a.isObject()) r.bad("attribs");
                EntityReader ar(a, r.ref());
                auto t = makeText(ar);
                applyProps(*t, ar);
                ins->addAttribute(ar.str("tag"), *t);
            }
        return ins;
    }
    if (type == "HATCH") {
        const json::Value* lv = r.raw().find("loops");
        if (!lv || !lv->isArray() || lv->arr().empty()) r.bad("loops");
        std::vector<PolylineEntity> loops;
        for (const auto& l : lv->arr()) {
            if (!l.isObject()) r.bad("loops");
            EntityReader lr(l, r.ref());
            auto pts = lr.vecs("pts");
            if (pts.size() < 2) r.bad("loops");
            auto bul = lr.nums("bulges");
            if (!bul.empty() && bul.size() != pts.size()) r.bad("bulges");
            loops.emplace_back(std::move(pts), std::move(bul), true);
        }
        return std::make_unique<HatchEntity>(std::move(loops), r.str("pattern", "SOLID"), r.num("scale", 1.0), r.num("angle", 0.0));
    }
    if (type == "DIMENSION") {
        DimKind k;
        if (!dimKindFromString(r.str("kind"), k)) r.bad("kind");
        const double h = r.num("h");
        if (h <= 0) r.bad("h");
        const Vec3 p1 = r.vec("p1"), p2 = r.vec("p2");
        if (p1.distanceTo(p2) < 1e-12) r.bad("p2");
        return std::make_unique<DimensionEntity>(k, p1, p2, r.vec("loc"), h, static_cast<int>(r.num("dec", 0.0)),
                                                 r.num("rot", 0.0));
    }
    if (type == "POINT") return std::make_unique<PointEntity>(r.vec("p"));
    if (type == "LINE") return std::make_unique<LineEntity>(r.vec("a"), r.vec("b"));
    if (type == "CIRCLE") {
        const double rad = r.num("r");
        if (rad <= 0) r.bad("r");
        return std::make_unique<CircleEntity>(r.vec("c"), rad);
    }
    if (type == "ARC") {
        const double rad = r.num("r");
        if (rad <= 0) r.bad("r");
        return std::make_unique<ArcEntity>(r.vec("c"), rad, r.num("start"), r.num("end"));
    }
    if (type == "POLYLINE") {
        auto pts = r.vecs("pts");
        if (pts.empty()) r.bad("pts");
        auto bulges = r.nums("bulges");
        if (!bulges.empty() && bulges.size() != pts.size()) r.bad("bulges");
        return std::make_unique<PolylineEntity>(std::move(pts), std::move(bulges), r.boolean("closed", false));
    }
    if (type == "TEXT") return makeText(r);
    return nullptr;
}

} // namespace

void writeAcad(const Document& doc, std::ostream& os) {
    os << "{\n\"format\":\"asiacad\",\n\"version\":" << kAcadFormatVersion << ",\n\"generator\":\"AsiaCAD 0.9\",\n";
    os << "\"nextId\":" << doc.peekNextId() << ",\n\"layers\":[\n";
    bool first = true;
    for (const auto& [name, l] : doc.layers()) {
        if (!first) os << ",\n";
        first = false;
        os << "{\"name\":";
        writeStr(os, name);
        os << ",\"color\":" << l.color << ",\"visible\":" << (l.visible ? "true" : "false")
           << ",\"locked\":" << (l.locked ? "true" : "false") << ",\"frozen\":" << (l.frozen ? "true" : "false") << '}';
    }
    os << "\n],\n\"blocks\":[\n";
    // Bog'liqlik tartibida: ichki bloklar avval
    std::vector<const BlockDef*> order;
    std::set<const BlockDef*> seen;
    std::function<void(const BlockDef*)> visit = [&](const BlockDef* b) {
        if (!seen.insert(b).second) return;
        for (const auto& e : b->entities)
            if (auto* in = dynamic_cast<const InsertEntity*>(e.get())) visit(in->block().get());
        order.push_back(b);
    };
    for (const auto& [k, b] : doc.blocks()) visit(b.get());
    first = true;
    for (const BlockDef* b : order) {
        if (!first) os << ",\n";
        first = false;
        os << "{\"name\":";
        writeStr(os, b->name);
        os << ",\"base\":";
        writeVec(os, b->base);
        os << ",\"entities\":[";
        for (std::size_t k = 0; k < b->entities.size(); ++k) {
            os << (k ? ",\n  " : "\n  ");
            writeEntity(os, *b->entities[k], false);
        }
        os << "]}";
    }
    os << "\n],\n\"entities\":[\n";
    first = true;
    doc.forEach([&](const IEntity& e) {
        if (!first) os << ",\n";
        first = false;
        writeEntity(os, e);
    });
    os << "\n]\n}\n";
}

void saveAcad(const Document& doc, const std::string& path, bool keepBackup) {
    namespace fs = std::filesystem;
    const fs::path target(path), tmp(path + ".tmp"), bak(path + ".bak");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw Error("err.write_failed", {path});
        writeAcad(doc, f);
        f.flush();
        if (!f) {
            f.close();
            std::error_code ec;
            fs::remove(tmp, ec);
            throw Error("err.write_failed", {path});
        }
    }
    std::error_code ec;
    if (fs::exists(target, ec)) {
        if (keepBackup) {
            fs::remove(bak, ec);
            fs::rename(target, bak, ec);
        }
        if (ec) ec.clear();
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        throw Error("err.write_failed", {path});
    }
}

AcadLoadResult readAcad(std::string_view text) {
    json::Value root;
    try {
        root = json::parse(text);
    } catch (const json::ParseError& e) {
        throw Error("acad.err.parse", {e.what()});
    }
    const json::Value* fmt = root.find("format");
    if (!fmt || !fmt->isString() || fmt->str() != "asiacad") throw Error("acad.err.format");
    const json::Value* ver = root.find("version");
    if (!ver || !ver->isNumber()) throw Error("acad.err.format");
    if (ver->num() > kAcadFormatVersion) throw Error("acad.err.version", {std::to_string(static_cast<int>(ver->num()))});

    AcadLoadResult res;
    std::set<std::string> declared{"0"};
    if (const json::Value* layers = root.find("layers"); layers && layers->isArray()) {
        for (const auto& lv : layers->arr()) {
            if (!lv.isObject()) continue;
            EntityReader r(lv, "layer");
            Layer l;
            l.name = r.str("name");
            if (l.name.empty()) continue;
            l.color = static_cast<int>(r.num("color", 7));
            if (l.color < 1 || l.color > 255) l.color = 7;
            l.visible = r.boolean("visible", true);
            l.locked = r.boolean("locked", false);
            l.frozen = r.boolean("frozen", false);
            if (!res.doc.setLayer(l)) res.doc.addLayer(l);
            declared.insert(l.name);
        }
    }

    std::map<std::string, std::size_t> unknown;
    std::set<std::string> missing;
    auto checkLayer = [&](const std::string& layer) {
        if (!declared.count(layer) && missing.insert(layer).second) res.warnings.push_back({"acad.warn.missing_layer", {layer}});
    };

    // Bloklar: 1) bo'sh ta'riflar (nom bo'yicha havola uchun), 2) ichini to'ldirish
    BlockMap blocks;
    std::vector<std::shared_ptr<BlockDef>> blockOrder;
    const json::Value* bv = root.find("blocks");
    if (bv && bv->isArray()) {
        for (const auto& b : bv->arr()) {
            if (!b.isObject()) throw Error("acad.err.format");
            EntityReader br(b, "block");
            auto def = std::make_shared<BlockDef>();
            def->name = br.str("name");
            if (def->name.empty()) br.bad("name");
            if (const json::Value* base = b.find("base")) EntityReader::toVec(*base, def->base);
            if (!blocks.emplace(utf8::toUpper(def->name), def).second) throw Error("acad.err.duplicate_id", {def->name});
            blockOrder.push_back(def);
        }
        std::size_t bi = 0;
        for (const auto& b : bv->arr()) {
            auto& def = blockOrder[bi++];
            const json::Value* ev = b.find("entities");
            if (!ev || !ev->isArray()) continue;
            std::size_t k = 0;
            for (const auto& e : ev->arr()) {
                ++k;
                const std::string ref = def->name + "/" + std::to_string(k);
                if (!e.isObject()) throw Error("acad.err.entity", {ref, "{}"});
                EntityReader r(e, ref);
                const std::string type = r.str("type");
                auto ent = makeEntity(type, r, blocks);
                if (!ent) { ++unknown[type.empty() ? "?" : type]; continue; }
                applyProps(*ent, r);
                checkLayer(ent->layer());
                def->entities.push_back(std::move(ent));
            }
        }
        for (const auto& def : blockOrder) res.doc.addBlock(def);
    }
    const json::Value* ents = root.find("entities");
    if (ents && !ents->isArray()) throw Error("acad.err.format");
    if (ents) {
        std::size_t index = 0;
        for (const auto& ev : ents->arr()) {
            ++index;
            if (!ev.isObject()) throw Error("acad.err.entity", {std::to_string(index), "{}"});
            const json::Value* idv = ev.find("id");
            const std::string ref = idv && idv->isNumber() ? std::to_string(static_cast<long long>(idv->num())) : "?" + std::to_string(index);
            EntityReader r(ev, ref);
            const double idn = r.num("id");
            if (idn < 1 || idn != std::floor(idn) || idn > 9007199254740992.0) r.bad("id");
            const std::string type = r.str("type");
            auto e = makeEntity(type, r, blocks);
            if (!e) {
                ++unknown[type.empty() ? "?" : type];
                continue;
            }
            applyProps(*e, r);
            checkLayer(e->layer());
            res.doc.addWithId(std::move(e), static_cast<EntityId>(idn));
        }
    }
    for (const auto& [t, n] : unknown) res.warnings.push_back({"acad.warn.unknown_type", {t, std::to_string(n)}});
    if (const json::Value* next = root.find("nextId"); next && next->isNumber() && next->num() >= 1)
        res.doc.reserveIds(static_cast<EntityId>(next->num()));
    return res;
}

AcadLoadResult loadAcad(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error("acad.err.open", {path});
    std::stringstream ss;
    ss << f.rdbuf();
    return readAcad(ss.str());
}

} // namespace asiacad
