#include <asiacad/civil/drawset.h>

#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/json.h>
#include <asiacad/io/acad_file.h>
#include <asiacad/io/dxf_reader.h>

#include <format>

namespace asiacad::civil {

std::string fileName(const std::string& path) {
    const auto p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

double autoScale(int insUnits, const BBox& ext, double medianTextHeight) {
    // $INSUNITS ko'pincha noto'g'ri (shablondan "m" qolgan, chizma esa mm da): matn balandligi 20 dan katta bo'lsa — mm
    if (medianTextHeight > 20 && insUnits != 4) return 0.001;
    switch (insUnits) {
        case 4: return 0.001;  // mm
        case 5: return 0.01;   // sm
        case 6: return 1.0;    // m
        case 14: return 0.1;   // dm
        default: break;
    }
    if (!ext.valid()) return 1.0;
    const double w = std::max(ext.max.x - ext.min.x, ext.max.y - ext.min.y);
    return w > 2000 ? 0.001 : 1.0;  // qurilish chizmalari ko'pincha mm da
}

LoadedSheet loadSheet(const SheetFile& f) {
    LoadedSheet ls;
    ls.file = f;
    ls.doc = std::make_shared<Document>();
    int ins = 0;
    double textH = 0;
    try {
        std::string ext = f.path.size() >= 5 ? f.path.substr(f.path.size() - 5) : f.path;
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".acad") {
            auto r = loadAcad(f.path);
            *ls.doc = std::move(r.doc);
            ins = 6;
        } else {
            DxfImportOptions opt;
            opt.explodeInserts = true;
            auto r = readDxfFile(f.path, opt);
            ins = r.insUnits;
            textH = r.medianTextHeight;
            ls.dims = std::move(r.dims);
            for (const auto& l : r.layers) ls.doc->addLayer(l);
            for (auto& e : r.entities) {
                if (!ls.doc->layer(e->layer())) ls.doc->addLayer(Layer{e->layer()});
                ls.doc->add(std::move(e));
            }
        }
    } catch (const Error& e) {
        ls.error = e.key();
        for (const auto& a : e.args()) ls.error += " " + a;
        return ls;
    } catch (const std::exception& e) {
        ls.error = e.what();
        return ls;
    }
    const double s = f.scale > 0 ? f.scale : (ins == 6 && f.path.ends_with(".acad") ? 1.0 : autoScale(ins, ls.doc->extents(false), textH));
    ls.scaleUsed = s;
    if (std::abs(s - 1) > 1e-12 || f.dx != 0 || f.dy != 0) {
        const Transform sc = Transform::scaling({0, 0, 0}, s), tr = Transform::translation({f.dx, f.dy, 0});
        for (EntityId id : ls.doc->ids()) {
            IEntity* e = ls.doc->find(id);
            e->transform(sc);
            e->transform(tr);
            ls.doc->noteChanged(id);
        }
        for (auto& d : ls.dims) {
            d.p1 = tr.apply(sc.apply(d.p1));
            d.p2 = tr.apply(sc.apply(d.p2));
            d.loc = tr.apply(sc.apply(d.loc));
        }
    }
    return ls;
}

std::vector<LoadedSheet> loadSheets(const std::vector<SheetFile>& files) {
    std::vector<LoadedSheet> out;
    for (const auto& f : files)
        if (f.enabled) out.push_back(loadSheet(f));
    return out;
}

bim::FloorPlans floorPlansFrom(const std::vector<LoadedSheet>& sheets) {
    bim::FloorPlans fp;
    for (const auto& sh : sheets) {
        if (!sh.error.empty() || !sh.doc) continue;
        const auto plan = bim::readPlan(*sh.doc, bim::autoLayerMap(*sh.doc));
        if (sh.file.role == "FOUND") {
            fp.foundation = plan;
            continue;
        }
        if (sh.file.role != "PLAN" && sh.file.role != "AUTO") continue;
        for (int k = std::max(1, sh.file.floorFrom); k <= std::max(sh.file.floorFrom, sh.file.floorTo); ++k) {
            auto pl = plan;
            if (k != sh.file.floorFrom) pl.levelTop.reset();
            fp.floors[k] = pl;
        }
    }
    return fp;
}

std::string sheetsToJson(const std::vector<SheetFile>& s) {
    std::string o = "[";
    for (std::size_t i = 0; i < s.size(); ++i) {
        std::string p;
        for (char c : s[i].path) { if (c == '"' || c == '\\') p += '\\'; p += c; }
        o += std::format("{}{{\"path\":\"{}\",\"role\":\"{}\",\"from\":{},\"to\":{},\"scale\":{},\"dx\":{},\"dy\":{},\"enabled\":{}}}", i ? "," : "", p,
                         s[i].role, s[i].floorFrom, s[i].floorTo, s[i].scale, s[i].dx, s[i].dy, s[i].enabled);
    }
    return o + "]";
}

std::vector<SheetFile> sheetsFromJson(const std::string& text) {
    std::vector<SheetFile> out;
    json::Value v;
    try { v = json::parse(text); } catch (...) { return out; }
    const json::Value* arr = v.isArray() ? &v : v.find("sheets");
    if (!arr || !arr->isArray()) return out;
    for (const auto& x : arr->arr()) {
        if (!x.isObject()) continue;
        SheetFile f;
        if (auto* p = x.find("path"); p && p->isString()) f.path = p->str();
        if (auto* p = x.find("role"); p && p->isString()) f.role = p->str();
        if (auto* p = x.find("from"); p && p->isNumber()) f.floorFrom = static_cast<int>(p->num());
        if (auto* p = x.find("to"); p && p->isNumber()) f.floorTo = static_cast<int>(p->num());
        if (auto* p = x.find("scale"); p && p->isNumber()) f.scale = p->num();
        if (auto* p = x.find("dx"); p && p->isNumber()) f.dx = p->num();
        if (auto* p = x.find("dy"); p && p->isNumber()) f.dy = p->num();
        if (auto* p = x.find("enabled"); p && p->isBool()) f.enabled = p->boolean();
        if (!f.path.empty()) out.push_back(f);
    }
    return out;
}

} // namespace asiacad::civil
