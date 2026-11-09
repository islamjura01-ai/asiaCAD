#include <asiacad/civil/drawset.h>
#include <asiacad/io/dwg_bridge.h>

#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/json.h>
#include <asiacad/io/acad_file.h>
#include <asiacad/io/dxf_reader.h>

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <set>

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
            // DWG (KJ, qolip planlari ko'pincha DWG da) — ODA File Converter / LibreDWG orqali vaqtinchalik DXF ga
            std::string src = f.path;
            if (ext.size() == 5 && ext.substr(1) == ".dwg") src = dwg::toDxf(f.path);
            auto r = readDxfFile(src, opt);
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

bim::FloorPlans floorPlansFrom(const std::vector<LoadedSheet>& sheets, double floorHeight, std::vector<SheetFloors>* placed) {
    bim::FloorPlans fp;
    struct Item {
        const LoadedSheet* sh;
        bim::PlanInput plan;
        std::vector<double> lv;
        SheetFloors info;
    };
    std::vector<Item> items;
    for (const auto& sh : sheets) {
        if (!sh.error.empty() || !sh.doc) continue;
        auto plan = bim::readPlan(*sh.doc, bim::autoLayerMap(*sh.doc));
        if (sh.file.role == "FOUND") {
            fp.foundation = plan;
            continue;
        }
        if (sh.file.role != "PLAN" && sh.file.role != "AUTO") continue;
        Item it{&sh, std::move(plan), {}, {}};
        it.info.file = fileName(sh.file.path);
        it.info.autoMode = sh.file.floorFrom <= 0;
        it.info.range = it.plan.levelSet.range;
        items.push_back(std::move(it));
    }
    // oraliqlarni qavatlarga bo'lish: qo'shni varaqlar belgilari qavat balandligini beradi
    std::vector<double> known;
    for (const auto& it : items) {
        if (!it.plan.levelSet.empty() && !it.plan.levelSet.range) known.insert(known.end(), it.plan.levelSet.levels.begin(), it.plan.levelSet.levels.end());
        else if (it.plan.levelSet.empty() && it.plan.levelTop) known.push_back(*it.plan.levelTop);
    }
    for (auto& it : items) {
        std::vector<double> hints;
        if (it.plan.levelSet.range) {
            const double lo = it.plan.levelSet.levels.front(), hi = it.plan.levelSet.levels.back();
            double below = -1e18, above = 1e18;
            for (double k : known) {
                if (k < lo - 1.0) below = std::max(below, k);
                if (k > hi + 1.0) above = std::min(above, k);
            }
            if (below > -1e17) hints.push_back(lo - below);
            if (above < 1e17) hints.push_back(above - hi);
        }
        it.lv = bim::planLevels(it.plan, floorHeight, hints);
    }
    auto put = [&](Item& it, int k, std::optional<double> lv) {
        auto pl = it.plan;
        pl.levelTop = lv;
        pl.levelSet = {};
        fp.floors[k] = std::move(pl);
        it.info.floors.push_back(k);
        if (lv) it.info.levels.push_back(*lv);
    };
    // qo'lda kiritilgan qavatlar
    for (auto& it : items) {
        const auto& f = it.sh->file;
        if (f.floorFrom <= 0) continue;
        const int from = f.floorFrom;
        int to = std::max(from, f.floorTo);
        if (it.lv.size() >= 2 && to == from) to = from + static_cast<int>(it.lv.size()) - 1;   // tipik: chizmadagi qavatlar soni
        for (int k = from; k <= to; ++k) {
            const auto i = static_cast<std::size_t>(k - from);
            put(it, k, i < it.lv.size() ? std::optional<double>(it.lv[i]) : (i == 0 ? it.plan.levelTop : std::nullopt));
        }
    }
    // avto: belgilar tartibi bo'yicha
    std::vector<std::pair<double, Item*>> autoLv;
    for (auto& it : items)
        if (it.sh->file.floorFrom <= 0)
            for (double l : it.lv) autoLv.push_back({l, &it});
    if (!autoLv.empty()) {
        std::sort(autoLv.begin(), autoLv.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        // qavat qadami: avto belgilar oralig'ining medianasi (bo'lmasa — parametr)
        double step = floorHeight > 1.5 ? floorHeight : 3.0;
        {
            std::vector<double> gaps;
            for (std::size_t i = 1; i < autoLv.size(); ++i)
                if (const double g = autoLv[i].first - autoLv[i - 1].first; g > 1.5 && g < 8) gaps.push_back(g);
            if (!gaps.empty()) {
                std::nth_element(gaps.begin(), gaps.begin() + static_cast<std::ptrdiff_t>(gaps.size() / 2), gaps.end());
                step = gaps[gaps.size() / 2];
            }
        }
        // qo'lda kiritilgan qavatlar — tayanch: belgisi bor bo'lsa o'zi, yo'q bo'lsa qo'shnilari orasida / qadam bilan
        std::map<int, double> anchor;
        for (const auto& [k, pl] : fp.floors)
            if (pl.levelTop) anchor[k] = *pl.levelTop;
        const std::map<int, double> knownA = anchor;
        for (const auto& [k, pl] : fp.floors) {
            if (pl.levelTop) continue;
            auto nx = knownA.upper_bound(k);
            auto pv = nx == knownA.begin() ? knownA.end() : std::prev(nx);
            if (pv != knownA.end() && nx != knownA.end())
                anchor[k] = pv->second + (nx->second - pv->second) * (k - pv->first) / (nx->first - pv->first);
            else if (pv != knownA.end()) anchor[k] = pv->second + (k - pv->first) * step;
            else if (nx != knownA.end()) anchor[k] = nx->second - (nx->first - k) * step;
            else anchor[k] = k * step;
        }
        std::vector<double> all;
        for (const auto& [l, it] : autoLv) all.push_back(l);
        for (const auto& [k, l] : anchor) all.push_back(l);
        std::sort(all.begin(), all.end());
        std::vector<double> U;   // takrorsiz (2 sm ichida — bitta qavat)
        for (double l : all)
            if (U.empty() || l - U.back() > 0.02) U.push_back(l);
        auto rank = [&](double l) { return static_cast<int>(std::upper_bound(U.begin(), U.end(), l + 0.02) - U.begin()); };
        // eng pastki belgidan pastdagi qavatlar (±0.000 — 1-qavat poli): belgi / qadam - 1
        const int below = std::max(0, static_cast<int>(std::lround(U.front() / step)) - 1);
        std::set<int> taken;   // qo'lda kiritilgan qavatlar o'zgarmaydi; bir belgili ikki varaq — birinchisi
        for (const auto& [k, pl] : fp.floors) taken.insert(k);
        for (const auto& [l, it] : autoLv) {
            int k = below + rank(l);
            for (const auto& [ak, al] : anchor)   // eng yaqin pastdagi qo'lda kiritilgan qavatga nisbatan
                if (al < l - 0.02) k = ak + rank(l) - rank(al);
            if (!taken.insert(k).second) continue;
            put(*it, k, l);
        }
    }
    // belgisiz avto varaq — 1-qavat (band bo'lmasa)
    for (auto& it : items)
        if (it.sh->file.floorFrom <= 0 && it.lv.empty() && !fp.floors.count(1)) put(it, 1, it.plan.levelTop);
    if (placed) {
        placed->clear();
        for (auto& it : items) placed->push_back(std::move(it.info));
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
