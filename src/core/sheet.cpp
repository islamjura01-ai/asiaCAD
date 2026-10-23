#include <asiacad/core/sheet.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace asiacad {

const std::vector<std::string>& paperSizes() {
    static const std::vector<std::string> v{"A4", "A3", "A2", "A1", "A0"};
    return v;
}

bool paperSize(const std::string& size, bool landscape, double& w, double& h) {
    static const std::map<std::string, std::pair<double, double>> m{
        {"A4", {210, 297}}, {"A3", {297, 420}}, {"A2", {420, 594}}, {"A1", {594, 841}}, {"A0", {841, 1189}}};
    std::string u = size;
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto it = m.find(u);
    if (it == m.end()) return false;
    w = landscape ? it->second.second : it->second.first;
    h = landscape ? it->second.first : it->second.second;
    return true;
}

PaperRect sheetFrame(const Sheet& s) {
    double w = 420, h = 297;
    paperSize(s.size, s.landscape, w, h);
    return {20, 5, w - 5, h - 5};
}

PaperRect sheetStamp(const Sheet& s) {
    const PaperRect f = sheetFrame(s);
    return {f.x1 - 185, f.y0, f.x1, f.y0 + 55};
}

PaperRect sheetUsable(const Sheet& s) {
    const PaperRect f = sheetFrame(s);
    return {f.x0 + 5, f.y0 + 55 + 5, f.x1 - 5, f.y1 - 5};
}

PaperRect viewRect(const SheetView& v) { return {v.at.x - v.w / 2, v.at.y - v.h / 2, v.at.x + v.w / 2, v.at.y + v.h / 2}; }

const std::vector<double>& standardScales() {
    static const std::vector<double> v{1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 2500, 5000, 10000, 25000, 50000, 100000};
    return v;
}

double fitScale(double mw, double mh, double pw, double ph) {
    for (double s : standardScales())
        if (mw * 1000.0 / s <= pw + 1e-9 && mh * 1000.0 / s <= ph + 1e-9) return s;
    return standardScales().back();
}

namespace {
constexpr double kGap = 6.0, kTitle = 9.0;  // ko'rinishlar orasidagi oraliq va ostidagi nom joyi
PaperRect reserved(const SheetView& v) {
    const PaperRect r = viewRect(v);
    return {r.x0 - kGap, r.y0 - kTitle - kGap, r.x1 + kGap, r.y1 + kGap};
}
}  // namespace

bool placeView(const Sheet& s, SheetView& v) {
    const PaperRect u = sheetUsable(s);
    if (v.w > u.w() + 1e-9 || v.h + kTitle > u.h() + 1e-9) return false;
    for (double y = u.y1 - v.h / 2; y - v.h / 2 - kTitle >= u.y0 - 1e-9; y -= 5) {
        for (double x = u.x0 + v.w / 2; x + v.w / 2 <= u.x1 + 1e-9; x += 5) {
            SheetView c = v;
            c.at = {x, y, 0};
            const PaperRect r = viewRect(c);
            const PaperRect rr{r.x0, r.y0 - kTitle, r.x1, r.y1};
            bool ok = true;
            for (const auto& o : s.views)
                if (rr.overlaps(reserved(o))) { ok = false; break; }
            if (ok) { v.at = c.at; return true; }
        }
    }
    return false;
}

bool makeView(const Sheet& s, const Vec3& a, const Vec3& b, double scale, const std::string& title, SheetView& out) {
    const double mw = std::max(std::abs(b.x - a.x), 1e-6), mh = std::max(std::abs(b.y - a.y), 1e-6);
    SheetView v;
    v.center = {(a.x + b.x) / 2, (a.y + b.y) / 2, 0};
    v.title = title;
    const PaperRect u = sheetUsable(s);
    std::vector<double> tries;
    if (scale > 0) tries.push_back(scale);
    else {
        const double s0 = fitScale(mw, mh, u.w(), u.h() - kTitle);
        for (double sc : standardScales())
            if (sc >= s0) tries.push_back(sc);
    }
    for (double sc : tries) {
        v.scale = sc;
        v.w = mw * 1000.0 / sc;
        v.h = mh * 1000.0 / sc;
        if (placeView(s, v)) { out = v; return true; }
    }
    return false;
}

std::string scaleLabel(double scale) { return std::format("1:{:g}", scale); }

std::vector<SheetPrim> sheetPrims(const Sheet& s, int number, int total, const SheetTr& tr) {
    std::vector<SheetPrim> out;
    auto line = [&](double x0, double y0, double x1, double y1, double w) {
        SheetPrim p;
        p.a = {x0, y0, 0};
        p.b = {x1, y1, 0};
        p.width = w;
        out.push_back(p);
    };
    auto rect = [&](const PaperRect& r, double w) {
        line(r.x0, r.y0, r.x1, r.y0, w);
        line(r.x1, r.y0, r.x1, r.y1, w);
        line(r.x1, r.y1, r.x0, r.y1, w);
        line(r.x0, r.y1, r.x0, r.y0, w);
    };
    auto text = [&](double x, double y, const std::string& t, double h, int align = 1) {
        if (t.empty()) return;
        SheetPrim p;
        p.kind = SheetPrim::Text;
        p.a = {x, y, 0};
        p.text = t;
        p.h = h;
        p.align = align;
        out.push_back(p);
    };
    auto field = [&](const char* k) -> std::string {
        auto it = s.stamp.find(k);
        return it == s.stamp.end() ? std::string() : it->second;
    };
    double W = 420, H = 297;
    paperSize(s.size, s.landscape, W, H);
    rect({0, 0, W, H}, 0.18);              // qirqish chizig'i
    const PaperRect f = sheetFrame(s);
    rect(f, 0.7);                          // ramka
    // Asosiy yozuv
    const PaperRect st = sheetStamp(s);
    const double x0 = st.x0, top = st.y1, T = 0.7, t = 0.25;
    rect(st, T);
    const double cols[] = {0, 10, 20, 30, 40, 55, 65};
    for (int r = 1; r <= 10; ++r) line(x0, top - 5 * r, x0 + 65, top - 5 * r, (r == 5 || r == 6) ? T : t);
    for (int c = 1; c <= 6; ++c) {
        const double x = x0 + cols[c];
        const bool full = c == 2 || c == 4 || c == 5 || c == 6;   // pastki 5 qatorda ham davom etadigan
        line(x, top, x, full ? st.y0 : top - 30, T);
    }
    const char* hdr[] = {"sheet.st.izm", "sheet.st.kol", "sheet.st.list", "sheet.st.ndoc", "sheet.st.sign", "sheet.st.date"};
    for (int c = 0; c < 6; ++c) text(x0 + (cols[c] + cols[c + 1]) / 2, top - 27.5, tr(hdr[c]), 1.8);
    const char* roles[] = {"sheet.st.dev", "sheet.st.chk", "", "sheet.st.nctrl", "sheet.st.appr"};
    const char* names[] = {"DEV", "CHK", "", "NCTRL", "APPR"};
    for (int r = 0; r < 5; ++r) {
        const double y = top - 30 - 5 * r - 2.5;
        if (*roles[r]) text(x0 + 1, y, tr(roles[r]), 1.8, 0);
        const std::string n = *names[r] ? field(names[r]) : std::string();
        text(x0 + 21, y, n, 2.2, 0);
        if (!n.empty()) text(x0 + 60, y, field("DATE"), 1.6);
    }
    // o'ng qism (120 mm)
    const double xr = x0 + 65;
    line(xr, top - 15, st.x1, top - 15, T);
    line(xr, top - 30, st.x1, top - 30, T);
    line(xr, top - 45, st.x1, top - 45, T);
    line(xr + 70, top - 30, xr + 70, st.y0, T);
    line(xr + 70, top - 35, st.x1, top - 35, T);
    line(xr + 85, top - 30, xr + 85, top - 45, T);
    line(xr + 100, top - 30, xr + 100, top - 45, T);
    text(xr + 60, top - 7.5, field("CODE"), 5);
    text(xr + 60, top - 22.5, field("OBJECT"), 3.5);
    text(xr + 35, top - 37.5, field("BUILDING"), 3);
    text(xr + 77.5, top - 32.5, tr("sheet.st.stage"), 1.8);
    text(xr + 92.5, top - 32.5, tr("sheet.st.list"), 1.8);
    text(xr + 110, top - 32.5, tr("sheet.st.lists"), 1.8);
    text(xr + 77.5, top - 40, field("STAGE"), 3.5);
    text(xr + 92.5, top - 40, std::to_string(number), 3.5);
    text(xr + 110, top - 40, std::to_string(total), 3.5);
    text(xr + 35, st.y0 + 5, field("TITLE").empty() ? s.name : field("TITLE"), 3);
    text(xr + 95, st.y0 + 5, field("ORG"), 3);
    // ko'rinishlar nomi: "NOM  M 1:500" va tagiga chiziq
    for (const auto& v : s.views) {
        const PaperRect r = viewRect(v);
        const std::string lbl = (v.title.empty() ? std::string() : v.title + "   ") + tr("sheet.st.m") + " " + scaleLabel(v.scale);
        text(r.x0, r.y0 - 4.5, lbl, 3.5, 0);
        line(r.x0, r.y0 - 7, r.x0 + std::min(r.w(), 3.5 * 0.68 * static_cast<double>(std::count_if(lbl.begin(), lbl.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }))), r.y0 - 7, 0.5);
    }
    return out;
}

} // namespace asiacad
