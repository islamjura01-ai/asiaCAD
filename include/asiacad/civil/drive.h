#pragma once
// AsiaCAD — Drive tahlili (Civil 3D "Analyze → Drive" kabi, lekin ko'rinish masofasi hisobi bilan):
//  * Yo'l chizig'i (feature line / 3D poliliniya / o'q + loyiha profili) bo'ylab kamera: ko'z balandligi, oldinga qarash
//  * Sahna: chizmadagi 3D chiziqlar, ikki sathli yopiq konturlar (bino massivlari), koridor yuzasi, bino prizmalari, yer
//  * Ko'rinish masofasi: haydovchi ko'zidan (h_ko'z) yo'l ustidagi to'siqqacha (h_to'siq) — profil (qavariq egri) va
//    3D to'siqlar (kesma qiyaligi, binolar, devorlar) bo'yicha; talab qilinadigan to'xtash masofasi bilan solishtiriladi
// Barcha balandliklar absolyut.
#include <asiacad/civil/corridor.h>
#include <asiacad/core/document.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace asiacad::civil {

struct DriveScene {
    struct Tri {
        std::array<Vec3, 3> p;
        std::uint32_t rgb = 0x909090;
        bool obstacle = true;          // ko'rinishni to'sadimi (yo'l yuzasi ham to'sadi — qavariq egrida)
    };
    struct Seg {
        Vec3 a, b;
        std::uint32_t rgb = 0xffffff;
    };
    std::vector<Tri> tris;
    std::vector<Seg> segs;
    void addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, std::uint32_t rgb, bool obstacle = true);
    // vertikal prizma (kontur XY, z0..z1): yon devorlar va ikki qopqoq (qavariq bo'lmagan kontur — quloq kesish)
    void addPrism(const std::vector<Vec3>& outline, double z0, double z1, std::uint32_t rgb);
    void append(const DriveScene& o);
    BBox bounds() const;
};

// Chizmadan: 3D chiziqlar (z ≠ 0), bir xil XY konturli yopiq poliliniyalar juftligi (z har xil) — prizma
DriveScene sceneFromDocument(const Document& doc);
// Koridor: kesimlar orasida ustki yuza (material bo'yicha rang: asfalt, yo'lak, beton, suv, kesma/to'kma qiyaligi),
// chiziqlar: o'q (uzuq), qatnov qismi chetlari
DriveScene sceneFromCorridor(const Corridor& c);
// Koridor ustki yuzasi belgisi (x, y nuqtada): eng yaqin kesimlar orasida offset bo'yicha; tashqarida — false
bool corridorSurfaceZ(const Corridor& c, double x, double y, double& z);
// Yer yuzasi: yo'l atrofida (±halfWidth) to'r, groundAt — chizmadagi syomkadan
DriveScene groundAround(const std::vector<Vec3>& path, const std::function<double(double, double)>& groundAt, double halfWidth,
                        double step);

// Chizmadagi 2D chiziqlar (z = 0) yo'l atrofida (radius) — sirtga yotqizilgan chiziqlar; "YUZA/ASFALT/TROTUAR" qatlamlaridagi
// yopiq konturlar — yuzalar (masalan shahar ko'chalari qatnov qismi)
DriveScene drapeLinework(const Document& doc, const std::vector<Vec3>& path, double radius, const std::function<double(double, double)>& z);

// ---- yo'l
struct DrivePath {
    std::vector<Vec3> p;               // zich nuqtalar (absolyut z)
    std::vector<double> s;             // gorizontal masofa (piket)
    double length() const { return s.empty() ? 0 : s.back(); }
    Vec3 at(double st) const;
    Vec3 dir(double st, double ds = 2.0) const;   // birlik yo'nalish (3D)
    double grade(double st) const;                 // bo'ylama nishablik (m/m)
    double zAt(double st) const { return at(st).z; }
};
// Egri chiziqdan (poliliniya yoylari bo'linadi); zFn bo'lsa — z shu funksiyadan (2D chiziqni yuzaga "yotqizish")
DrivePath makeDrivePath(const IEntity& curve, double step = 1.0, const std::function<double(double, double)>& zFn = {});
DrivePath makeDrivePath(const std::vector<Vec3>& pts, double step = 1.0);
bool curveHasZ(const IEntity& curve);

struct DriveCamera {
    Vec3 eye, target;
};
DriveCamera driveCamera(const DrivePath& p, double st, double eyeH, double lookAhead);

// ---- ko'rinish masofasi
struct SightParams {
    double eyeH = 1.2;                 // haydovchi ko'zi, m (SNiP 2.05.02-85 — tasdiqlanmagan)
    double objH = 0.2;                 // to'siq balandligi, m
    double maxD = 400;                 // qidirish chegarasi, m
    double step = 10;                  // piketlar oralig'i, m
    double speedKmh = 60;              // hisobiy tezlik
    bool obstacles3d = true;           // sahnadagi to'siqlar (kesma qiyaligi, binolar) ham
};
struct SightSample {
    double s = 0;
    double profile = 0;                // faqat bo'ylama profil bo'yicha, m
    double total = 0;                  // profil + 3D to'siqlar, m
    double grade = 0;
};
struct SightIssue {
    double s0 = 0, s1 = 0, minD = 0;
};
struct SightResult {
    std::vector<SightSample> samples;
    double required = 0;               // talab qilinadigan to'xtash masofasi
    double minD = 0, minAt = 0, maxGrade = 0;
    std::vector<SightIssue> issues;    // total < required bo'lgan oraliqlar
};
// To'xtash uchun eng kam ko'rinish masofasi (SNiP 2.05.02-85, 10-jadval — tasdiqlanmagan), oraliq — chiziqli
double requiredStoppingSight(double speedKmh);
SightResult sightAnalysis(const DrivePath& path, const DriveScene& scene, const SightParams& sp);

} // namespace asiacad::civil
