#pragma once
// AsiaCAD — listlar (Revit "Sheets" / AutoCAD "Layout"): qog'oz formati, GOST ramkasi va asosiy yozuv (shtamp),
// modeldan masshtabli ko'rinishlar. Qog'oz koordinatalari — mm, chap pastki burchak (0,0); model birligi — metr.
#include <asiacad/core/math.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace asiacad {

struct SheetView {
    Vec3 center;          // model markazi (m)
    double scale = 100;   // maxraj: 1:scale
    Vec3 at;              // qog'ozdagi markaz (mm)
    double w = 100, h = 100;  // qog'ozdagi o'lcham (mm)
    std::string title;
    double modelW() const { return w * scale / 1000.0; }
    double modelH() const { return h * scale / 1000.0; }
};

struct Sheet {
    std::string name;
    std::string size = "A3";   // A0..A4
    bool landscape = true;
    std::map<std::string, std::string> stamp;  // CODE OBJECT BUILDING TITLE STAGE ORG DEV CHK NCTRL APPR DATE
    std::vector<SheetView> views;
};

struct PaperRect { double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    double w() const { return x1 - x0; } double h() const { return y1 - y0; }
    bool overlaps(const PaperRect& o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; } };

// Qog'oz o'lchami (mm). Noma'lum format — false
bool paperSize(const std::string& size, bool landscape, double& w, double& h);
const std::vector<std::string>& paperSizes();
PaperRect sheetFrame(const Sheet& s);    // ichki ramka: chapda 20, qolganida 5 mm (GOST 2.301)
PaperRect sheetStamp(const Sheet& s);    // asosiy yozuv 185×55 (GOST 21.101 sh.3)
PaperRect sheetUsable(const Sheet& s);   // ko'rinishlar uchun maydon (shtamp ustida)
PaperRect viewRect(const SheetView& v);  // ko'rinish to'rtburchagi + nom uchun joy emas

const std::vector<double>& standardScales();       // 1, 2, 5, 10, 20, 25, 50, 100, ... 100000
double fitScale(double modelW, double modelH, double paperW, double paperH);  // eng yirik sig'adigan standart masshtab
// Ko'rinishni bo'sh joyga qo'yadi (yuqori chapdan). Sig'masa false.
bool placeView(const Sheet& s, SheetView& v);
// Model oynasi (m) — ko'rinish: scale<=0 bo'lsa avtomatik, joy topilmaguncha masshtab kichraytiriladi
bool makeView(const Sheet& s, const Vec3& a, const Vec3& b, double scale, const std::string& title, SheetView& out);

// Chizish primitivlari (mm): chiziq yoki matn
struct SheetPrim {
    enum Kind { Line, Text } kind = Line;
    Vec3 a, b;
    double width = 0.25;   // chiziq qalinligi, mm
    std::string text;
    double h = 2.5;        // matn balandligi, mm
    int align = 1;         // 0 chap, 1 markaz, 2 o'ng (vertikal — o'rta)
};
using SheetTr = std::function<std::string(const std::string&)>;
std::vector<SheetPrim> sheetPrims(const Sheet& s, int number, int total, const SheetTr& tr);
std::string scaleLabel(double scale);  // "1:500"

} // namespace asiacad
