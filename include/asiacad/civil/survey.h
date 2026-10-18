#pragma once
// AsiaCAD — geodezik (ijroiy) tahlil: o'lchangan nuqtalarning loyihadan og'ishi (GeoUzPro / Geo_Deviation uslubida).
//  * PLAN   — reja / fasad: nuqtadan loyiha chizig'igacha (devor yuzi, o'q, fasad izi) perpendikulyar; B/P (yuqori/past) —
//             mahalliy guruhda (≈1.5 m) Z medianasiga nisbatan; strelka doim chiziqdan tashqariga (nuqta tomonga)
//  * HEIGHT — balandlik: Z − loyiha belgisi (doimiy yoki eng yaqin loyiha 3D chizig'i / nuqtasidan interpolyatsiya)
//  * VERT   — vertikallik (ustun, devor qirrasi, qoziq, fasad): bir vertikaldagi nuqtalar guruhi — pastki nuqtaga
//             (yoki loyiha o'qiga) nisbatan dX, dY, og'ish va H; ruxsat — mm va/yoki H/k
//  * AXIS   — qiya / vertikal / gorizontal 3D o'q (anker, qiya qoziq, tirgak, to'sin): o'qqa 3D perpendikulyar, uning
//             yon (reja) va tik tashkil etuvchilari, o'q bo'yicha masofa; har element uchun o'lchangan o'q burchagi va azimut farqi
//  * POINT  — loyiha nuqtasi (qoziq / anker boshi, ustun markazi): eng yaqin o'lchangan nuqta — dX, dY, dZ, reja og'ishi
// Belgilar (strelka, qiymat, B/P) nuqta qaysi balandlikda bo'lsa, o'sha balandlikda chiziladi.
// Nuqtalar fayli: TXT/CSV (bo'sh joy, tab, vergul, nuqta-vergul), tartib — N X Y Z [kod]; geodezik tartib (X — shimol).
#include <asiacad/core/document.h>
#include <asiacad/io/xlsx.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace asiacad::civil {

struct SurveyPoint {
    std::string name;
    Vec3 p;
    std::string code;
};

// ---- nuqtalar fayli
struct PointFileFormat {
    bool geodetic = true;   // faylda X — shimol (chizma y), Y — sharq (chizma x)
    bool hasName = true;    // birinchi ustun — nuqta nomi (o'qishda avtomatik ham aniqlanadi)
    char delim = 0;         // yozishda: 0 — bo'sh joy (TXT), ',' / ';' / '\t'
    int decimals = 3;
};
std::vector<SurveyPoint> readPoints(const std::string& text, const PointFileFormat& f, std::vector<std::string>* skipped = nullptr);
std::string writePoints(const std::vector<SurveyPoint>& pts, const PointFileFormat& f);

// Chizmaga: nuqta (SYOMKA) + nom va belgi yozuvlari (SYOMKA_NOMI, SYOMKA_BELGI) — nuqtaning o'z balandligida
std::vector<std::unique_ptr<IEntity>> pointEntities(const std::vector<SurveyPoint>& pts, double textH);
// Chizmadan: nuqtalar (POINT, INSERT asosi), nomi — yonidagi SYOMKA_NOMI yozuvidan (bo'lmasa "#id")
std::vector<SurveyPoint> collectPoints(const Document& doc, const std::vector<EntityId>& ids);

// ---- loyiha elementlari
struct DesignRef {
    std::string name;
    std::vector<Vec3> pts;   // chiziq / poliliniya (3D) yoki bitta nuqta
    bool closed = false;
};
std::vector<DesignRef> collectRefs(const Document& doc, const std::vector<EntityId>& ids);

enum class DevKind { Plan, Height, Vertical, Axis, Point };
const char* devKindKey(DevKind k);              // "PLAN", "HEIGHT", ...
bool devKindFrom(const std::string& s, DevKind& k);

struct DevParams {
    DevKind kind = DevKind::Plan;
    double tol = 0.010;          // ruxsat, m
    double tolRatio = 0;         // VERT: qo'shimcha H/k (0 — yo'q); masalan 1000 -> H/1000
    double maxDist = 1.0;        // PLAN/HEIGHT/AXIS: undan uzoq nuqtalar loyihaga tegishli emas; POINT: moslash radiusi
    double cluster = 1.5;        // PLAN: B/P guruhi; VERT: bir vertikal radiusi
    double designZ = 0;          // HEIGHT: loyiha elementi bo'lmasa
    bool useDesignZ = false;
    double textH = 0.25;
    double arrowLen = 0.8;       // strelka uzunligi (chizma birligida)
};

struct DevRow {
    std::string name, ref;
    Vec3 p, foot;                 // o'lchangan nuqta va loyihadagi mos nuqta
    double dx = 0, dy = 0, dz = 0;
    double d = 0;                 // asosiy og'ish (ishorali: PLAN — tashqari/chap +, HEIGHT — yuqori +), m
    double h = 0;                 // VERT: pastki nuqtadan balandlik; AXIS: o'q bo'yicha masofa
    double lat = 0, vert = 0;     // AXIS: yon (reja) va tik tashkil etuvchilar
    double allow = 0;             // shu nuqta uchun ruxsat
    char cls = 0;                 // PLAN: 'B' (yuqori) / 'P' (past)
    bool ok = true;
};
struct DevElement {               // AXIS: element bo'yicha o'q burchaklari; VERT: guruh
    std::string ref;
    int n = 0;
    double designIncl = 0, measIncl = 0, designAz = 0, measAz = 0;   // ° (qiyalik — gorizontdan)
    double maxDev = 0;
};
struct DevResult {
    DevKind kind = DevKind::Plan;
    std::vector<DevRow> rows;
    std::vector<DevElement> elements;
    std::vector<std::string> unmatched;   // loyihaga tegishli bo'lmagan nuqtalar / o'lchanmagan loyiha nuqtalari
    int nOk = 0;
    double maxAbs = 0, mean = 0, rms = 0;
};
DevResult analyze(const std::vector<SurveyPoint>& pts, const std::vector<DesignRef>& refs, const DevParams& prm);

// Chizmaga: strelka (loyihadan nuqtaga), qiymat (mm, gorizontal), B/P harfi; ranglar: yashil — ruxsatda, qizil — oshgan,
// B — yashil, P — ko'k. Qatlamlar: DEV_STRELKA, DEV_QIYMAT, DEV_BP
std::vector<std::unique_ptr<IEntity>> annotate(const DevResult& r, const DevParams& prm);

// Excel / CSV. tr — sarlavhalar tarjimasi (kalit -> matn), geodetic — X ustuni shimol bo'lsin
xlsx::Workbook toWorkbook(const DevResult& r, const DevParams& prm, const std::function<std::string(const std::string&)>& tr, bool geodetic);
std::string toCsv(const DevResult& r, const std::function<std::string(const std::string&)>& tr, bool geodetic, char delim = ';');

} // namespace asiacad::civil
