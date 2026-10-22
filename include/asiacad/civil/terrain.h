#pragma once
// AsiaCAD — yer yuzasi va yer ishlari (TopografGeodezist uslubida):
//  * Tin        — nuqtalardan uchburchak yuza (Delaunay): syomka nuqtalari, belgi yozuvlari, gorizontallar (3D chiziqlar);
//                 chegara (yopiq kontur) va eng uzun qirra bilan kesish; gorizontallar
//  * Bo'ylama profil — o'q bo'ylab yer va loyiha chiziqlari, jadval (loyiha, yer, ishchi belgi, masofa, PK)
//  * Ko'ndalang kesimlar (razrezlar) — har N m da, chap/o'ng; to'kma/kesma maydoni; hajm — o'rtacha maydonlar usulida
//  * Kartogramma — kvadratlar to'ri: tugunda ishchi belgi, loyiha va yer belgisi; kvadrat hajmlari (uchburchakli prizmalar),
//                 nol chizig'i, ustunlar bo'yicha yig'indi; loyiha — yuza, gorizontal / qiya tekislik yoki balans tekisligi
//  * Hajm — ikki yuza (yoki yuza va tekislik) orasidagi kesma / to'kma
#include <asiacad/core/document.h>
#include <asiacad/io/xlsx.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace asiacad::civil {

using Entities = std::vector<std::unique_ptr<IEntity>>;

class Tin {
public:
    // pts — (x, y, z); takror nuqtalar birlashtiriladi; boundary — yopiq kontur (rejada); maxEdge > 0 — uzun qirrali chet uchburchaklar olib tashlanadi
    static Tin build(std::vector<Vec3> pts, const std::vector<Vec3>* boundary = nullptr, double maxEdge = 0);
    bool z(double x, double y, double& out) const;
    bool empty() const { return tri_.empty(); }
    const std::vector<Vec3>& points() const { return pts_; }
    const std::vector<std::array<int, 3>>& triangles() const { return tri_; }
    BBox bounds() const { return box_; }
    double area() const;                                  // rejadagi maydon
    std::vector<std::vector<Vec3>> contours(double level) const;   // gorizontal (zanjirlar)

private:
    void index();
    std::vector<Vec3> pts_;
    std::vector<std::array<int, 3>> tri_;
    BBox box_;
    double cell_ = 1;
    int gx_ = 0, gy_ = 0;
    std::vector<std::vector<int>> grid_;
};

// Yer nuqtalari: POINT, belgi yozuvlari ("412.35", "▽412,35", "Z=412.35"), 3D chiziq/poliliniya uchlari (z ≠ 0), blok asoslari
std::vector<Vec3> terrainPoints(const Document& doc, const std::vector<EntityId>& ids);
// Chiziq / poliliniya / yoy — rejadagi zanjir (yoylar bo'laklangan)
std::vector<Vec3> pathOf(const IEntity& e);

// Loyiha yuzasi: TIN, tekislik (z0 + ix·(x−bx) + iy·(y−by)) yoki yo'q
struct Design {
    const Tin* tin = nullptr;
    bool plane = false;
    double z0 = 0, ix = 0, iy = 0;   // ix, iy — nishablik (ulush, 0.002 = 2‰)
    Vec3 base;
    bool z(double x, double y, double& out) const;
    bool valid() const { return tin || plane; }
};

// ---------------------------------------------------------------- bo'ylama profil
struct ProfileSpec {
    double step = 20;            // piket / jadval ustunlari qadami, m
    double hScale = 1000, vScale = 100;   // 1:1000 / 1:100
    double pk0 = 0;              // boshlang'ich piket (m)
    Vec3 at;                     // chizma joyi (chap pastki burchak)
    double textH = 2.5;          // qog'ozda, mm
    double datum = std::nan(""); // shartli gorizont (NaN — avtomatik)
};
struct ProfilePoint {
    double s = 0;
    bool hasG = false, hasD = false;
    double zg = 0, zd = 0;
    bool station = false;        // jadval ustuni (piket yoki burilish)
};
std::vector<ProfilePoint> sampleProfile(const std::vector<Vec3>& path, const Tin& eg, const Design& fg, const ProfileSpec& p);
Entities drawProfile(const std::vector<ProfilePoint>& pr, const ProfileSpec& p, const std::function<std::string(const std::string&)>& tr);

// ---------------------------------------------------------------- ko'ndalang kesimlar
struct SectionSpec {
    double step = 20, left = 20, right = 20;
    double sample = 0.5;         // kesim bo'ylab qadam, m
    double scale = 200;          // 1:200 (gorizontal va vertikal)
    int cols = 4;
    double pk0 = 0;
    Vec3 at;
    double textH = 2.5;          // mm
};
struct XSection {
    double s = 0;
    Vec3 c, n;                   // markaz (rejada) va chap normal
    std::vector<std::pair<double, double>> g, d;   // (masofa: chap −, o'ng +; belgi)
    double cut = 0, fill = 0;    // m²
};
struct SectionVolumes {
    std::vector<double> cutV, fillV;   // har oraliq uchun (i -> i+1), m³
    double cut = 0, fill = 0;
};
std::vector<XSection> sampleSections(const std::vector<Vec3>& path, const Tin& eg, const Design& fg, const SectionSpec& p);
SectionVolumes sectionVolumes(const std::vector<XSection>& xs);
Entities drawSections(const std::vector<XSection>& xs, const SectionVolumes& v, const SectionSpec& p,
                      const std::function<std::string(const std::string&)>& tr);
Entities drawSectionMarks(const std::vector<XSection>& xs, const SectionSpec& p);   // rejada kesim chiziqlari

// ---------------------------------------------------------------- kartogramma va hajm
struct CartoSpec {
    double cell = 20;
    std::optional<Vec3> origin;            // to'r boshi (bo'lmasa — chegara/yuza chap pastki burchagi)
    std::vector<Vec3> boundary;            // bo'sh — butun yuza
    bool balance = false;                  // loyiha — balans tekisligi (kesma = to'kma)
    double textH = 0;                      // chizmada, m (0 — avtomatik: cell/12)
};
struct CartoNode {
    bool ok = false;
    Vec3 p;
    double zg = 0, zd = 0, h = 0;
};
struct CartoCell {
    bool ok = false;
    double fill = 0, cut = 0, area = 0;    // m³, m³, m²
};
struct CartoResult {
    int nx = 0, ny = 0;                    // kvadratlar soni
    Vec3 origin;
    double cell = 20;
    std::vector<CartoNode> nodes;          // (nx+1)·(ny+1), i + j·(nx+1)
    std::vector<CartoCell> cells;          // nx·ny
    std::vector<double> colFill, colCut;
    double fill = 0, cut = 0, area = 0;
    double balanceZ = std::nan("");
    std::vector<std::vector<Vec3>> zero;   // nol chizig'i
    const CartoNode& node(int i, int j) const { return nodes[static_cast<std::size_t>(i + j * (nx + 1))]; }
    const CartoCell& at(int i, int j) const { return cells[static_cast<std::size_t>(i + j * nx)]; }
};
CartoResult cartogram(const Tin& eg, Design fg, const CartoSpec& s);
Entities drawCartogram(const CartoResult& r, const CartoSpec& s, const std::function<std::string(const std::string&)>& tr);

// Yuzalar orasidagi hajm (mayda to'r bilan)
struct VolumeResult {
    double fill = 0, cut = 0, area = 0, cell = 0, balanceZ = std::nan("");
};
VolumeResult volumeBetween(const Tin& eg, Design fg, const std::vector<Vec3>& boundary, double cell = 0, bool balance = false);

// Excel
xlsx::Workbook profileWorkbook(const std::vector<ProfilePoint>& pr, const ProfileSpec& p, const std::function<std::string(const std::string&)>& tr);
xlsx::Workbook sectionsWorkbook(const std::vector<XSection>& xs, const SectionVolumes& v, const SectionSpec& p,
                                const std::function<std::string(const std::string&)>& tr);
xlsx::Workbook cartogramWorkbook(const CartoResult& r, const std::function<std::string(const std::string&)>& tr);


} // namespace asiacad::civil
