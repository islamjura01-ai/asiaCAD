#pragma once
// AsiaCAD — avtomobil yo'li: plan chizmasidan (o'q chizig'i + yer belgilari + loyiha belgilari) 3D koridor.
//  * Qatlamlar: o'q (O'Q/ОСЬ/AXIS/CL), yer sathi (YER_SATHI/РЕЛЬЕФ/GORIZONTAL — belgi yozuvlari, 3D nuqtalar, 3D gorizontallar),
//    loyiha belgilari (LOYIHA/ПРОЕКТ/DESIGN — o'q yonidagi ▽101,20 yozuvlari) yoki o'qning o'zi 3D bo'lsa — Z
//  * Ko'ndalang profil: qatnov qismi (polosalar, ikki tomonga nishab), yo'l yoqasi, qiyalik (to'kma/kesma 1:m) yergacha
//  * Natija: piketlar (PK), loyiha/yer belgilari, ishchi belgi, nishablik ‰, to'kma/kesma maydon va hajm,
//    qoplama qatlamlari hajmi, 3D sirtlar
//  * Tekshiruv (SNiP 2.05.02-85 / QMQ 2.05.02-95 asosida — tasdiqlanmagan): bo'ylama nishablik, egri radiusi,
//    burchak nuqtada egri yo'qligi, yer/loyiha ma'lumoti yo'qligi
#include <asiacad/core/document.h>

#include <array>
#include <map>
#include <string>
#include <vector>

namespace asiacad::civil {

enum class RoadLayer { Axis, Ground, Design, Ignore };
const char* roadLayerKey(RoadLayer k);
bool roadLayerFromKey(const std::string& s, RoadLayer& out);
RoadLayer guessRoadLayer(const std::string& layer);

struct PavementLayer {
    std::string name;
    double thickness = 0.05;   // m
    bool asphalt = false;      // m² hisobi uchun
};

struct RoadCategory {
    std::string id;            // "I-a", "II", ... (i18n: road.cat.<id>)
    int speed = 100;           // hisobiy tezlik, km/soat
    int lanes = 2;
    double laneWidth = 3.5, shoulder = 2.5, strengthened = 0.5;
    double maxGrade = 0.05;    // bo'ylama, ulush
    double minRadius = 600;    // m
};
std::vector<RoadCategory> roadCategories();    // SNiP 2.05.02-85 4, 10-jadvallar asosida (tasdiqlanmagan)

struct RoadParams {
    RoadCategory cat;
    double crossfall = 0.02;       // qatnov qismi
    double shoulderFall = 0.04;    // yo'l yoqasi
    double slopeFill = 1.5, slopeCut = 1.5;   // 1:m
    double step = 20;              // piket oralig'i, m
    double ground = 100.0;         // yer nuqtalari bo'lmasa
    double raise = 0.5;            // loyiha ma'lumoti bo'lmasa: yer + raise
    double labelRadius = 6.0;      // loyiha yozuvini o'qqa biriktirish, m
    std::vector<PavementLayer> pavement;
};
RoadParams defaultRoadParams();

struct RoadStation {
    double s = 0;                  // piket masofasi
    double x = 0, y = 0;           // o'qda
    double dx = 1, dy = 0;         // birlik yo'nalish
    double ground = 0, design = 0; // o'qdagi yer va loyiha belgisi
    bool control = false;          // loyiha nazorat nuqtasi (yozuv/Z/qo'lda)
    double grade = 0;              // keyingi piketgacha nishablik
    // ko'ndalang profil: chapdan o'ngga nuqtalar (offset, z); [0] — chap qiyalik oxiri (yerda)
    std::vector<std::pair<double, double>> section;
    double cutArea = 0, fillArea = 0;  // m²
};

struct RoadIssue {
    bool error = false;
    std::string key;
    std::vector<std::string> args;
    double s = 0;
};

struct RoadModel {
    std::vector<RoadStation> st;
    std::vector<RoadIssue> issues;
    double length = 0;
    double cutVolume = 0, fillVolume = 0;          // m³
    double asphaltArea = 0;                         // m²
    std::vector<std::pair<std::string, double>> pavementVolumes;  // qatlam -> m³
    int groundPoints = 0, designPoints = 0;
    std::vector<double> vertexS;                    // o'q burilish nuqtalari (PK)
    std::vector<std::array<double, 3>> groundPts;   // yer nuqtalari (qayta hisob uchun)
    struct Curve { double s0, s1, radius; };
    std::vector<Curve> curves;                      // o'qdagi yoylar
    int errors() const;
};

std::map<std::string, RoadLayer> autoRoadLayers(const Document& doc);
// O'qni o'qish (eng uzun zanjir), yer va loyiha nuqtalari, piketlar, kesimlar, hajmlar, tekshiruv
RoadModel buildRoad(const Document& doc, const std::map<std::string, RoadLayer>& layers, const RoadParams& p);
// Qo'lda tuzatilgan loyiha belgilaridan keyin (st[i].design, control) — kesimlar/hajmlar/tekshiruv qayta
void recomputeRoad(RoadModel& m, const RoadParams& p);

std::string pk(double s);          // 120.5 -> "PK1+20,50"

std::string roadToJson(const RoadModel& m, const RoadParams& p);
void roadFromJson(const std::string& text, RoadModel& m, RoadParams& p);

// Chizmaga: 3D chiziqlar (o'q, qatnov qismi chetlari, yo'l yoqasi chetlari, qiyalik oxirlari), har piketda ko'ndalang kesim
std::vector<std::unique_ptr<IEntity>> drawRoad3D(const RoadModel& m);
// Bo'ylama profil (yer va loyiha, piketlar, belgilar jadvali); origin — chap yuqori burchak (pastga chiziladi)
std::vector<std::unique_ptr<IEntity>> drawRoadProfile(const RoadModel& m, const Vec3& origin, double vscale, double h,
                                                      const std::string& groundLbl, const std::string& designLbl, const std::string& pkLbl);

} // namespace asiacad::civil
