#pragma once
// AsiaCAD — yer osti muhandislik tarmoqlari: tayyor plan chizmasidan (DXF/DWG→DXF, .acad) 3D model.
//  * Qatlam -> tarmoq turi (suv, kanalizatsiya, yomg'ir, gaz, issiqlik, elektr, aloqa, drenaj, yer sathi)
//  * Chiziq/poliliniya — quvur o'qi; aylana — quduq/kamera
//  * Yozuvlar: diametr (Ø200, %%c200, d=110, DN300, Ду150), chuqurlik (h=1,80), belgi (98,45, ▽98.45, отм. 98.45)
//    eng yaqin chiziq/uchga biriktiriladi; 3D poliliniya Z si bo'lsa — o'sha olinadi
//  * Yer sathi: "yer"/"rel'ef" qatlamidagi belgi yozuvlari va 3D nuqtalar (IDW), bo'lmasa parametr
//  * Belgisiz uchlar — chiziq bo'ylab interpolyatsiya, umuman yo'q bo'lsa — tur bo'yicha standart chuqurlik
//  * Tekshiruv: diametr/belgi yo'q (standart olindi), uzilish (ulanmagan uchlar), kesishmalarda vertikal masofa,
//    quvur ustidagi qatlam, o'z oqimli tarmoqda teskari nishablik. Xatolar keyin qo'lda tuzatiladi (jadval/Excel).
#include <asiacad/core/document.h>

#include <map>
#include <string>
#include <vector>

namespace asiacad::civil {

enum class UtilKind { Water, Sewer, Storm, Gas, Heat, Power, Telecom, Drain, Other, Ground, Ignore };
const char* utilKey(UtilKind k);                 // "WATER", ... (i18n: util.kind.<key>)
bool utilFromKey(const std::string& s, UtilKind& out);
UtilKind guessUtil(const std::string& layer);
bool isGravity(UtilKind k);                      // kanalizatsiya, yomg'ir, drenaj — belgi quvur tubiga
bool isCable(UtilKind k);                        // elektr, aloqa
std::vector<UtilKind> utilKinds();               // tarmoq turlari (Ground/Ignore siz)

struct UtilParams {
    double ground = 100.0;        // yer sathi belgisi (yer nuqtalari bo'lmasa), m
    double labelRadius = 3.0;     // yozuvni chiziqqa biriktirish radiusi, m
    double gapTol = 1.0;          // shu masofadagi ulanmagan uchlar — uzilish, m
    double clearPipe = 0.2;       // kesishmada vertikal masofa (sirtlar orasi), m
    double clearCable = 0.5;      // kabel ishtirok etsa, m
    std::map<UtilKind, double> depth;     // standart chuqurlik (quvur ustigacha), m
    std::map<UtilKind, double> dn;        // standart diametr, mm
    std::map<UtilKind, double> minCover;  // eng kam qatlam (quvur ustigacha), m
};
UtilParams defaultUtilParams();

struct UtilVertex {
    double x = 0, y = 0, z = 0;   // z — quvur o'qi belgisi
    double ground = 0;            // shu nuqtadagi yer belgisi
    enum Src { FromZ, FromLabel, FromDepth, Interpolated, Default, Manual } src = Default;
};

struct UtilLine {
    int id = 0;
    UtilKind kind = UtilKind::Other;
    std::string layer;
    double dn = 0;                // mm
    bool dnFromLabel = false;
    bool dnManual = false;
    std::vector<UtilVertex> v;
    double length() const;        // 3D
};

struct UtilNode {                 // quduq/kamera (aylana)
    UtilKind kind = UtilKind::Other;
    std::string layer;
    double x = 0, y = 0, r = 0.5;
    double ground = 0, bottom = 0;
};

struct UtilCrossing {
    int a = -1, b = -1;           // chiziq indekslari
    double x = 0, y = 0;
    double za = 0, zb = 0;        // o'q belgilari
    double clear = 0, required = 0;
};

struct UtilIssue {
    bool error = false;
    std::string key;
    std::vector<std::string> args;
    int line = -1;
    double x = 0, y = 0;
};

struct UtilModel {
    std::vector<UtilLine> lines;
    std::vector<UtilNode> nodes;
    std::vector<UtilCrossing> crossings;
    std::vector<UtilIssue> issues;
    std::map<UtilKind, double> lengthByKind;
    int groundPoints = 0;
    int errors() const;
};

std::map<std::string, UtilKind> autoUtilLayers(const Document& doc);
UtilModel readUtilities(const Document& doc, const std::map<std::string, UtilKind>& layers, const UtilParams& p);

// Bir nechta chizma (masalan suv, kanalizatsiya, yomg'ir — alohida fayllar) bitta modelga: kesishmalar fayllar orasida
// ham tekshiriladi. force != Ignore — fayldagi barcha chiziqlar shu tur (yer sathi qatlamlaridan tashqari).
// Yozuvlar faqat o'z fayli chiziqlariga biriktiriladi; yer nuqtalari umumiy. tag — chiziq qatlami oldiga ("fayl · qatlam").
struct UtilSource {
    const Document* doc = nullptr;
    std::map<std::string, UtilKind> layers;
    UtilKind force = UtilKind::Ignore;
    std::string tag;
};
UtilModel readUtilities(const std::vector<UtilSource>& sources, const UtilParams& p);
// Qo'lda tuzatishdan keyin: kesishmalar, uzunliklar va tekshiruv qayta hisoblanadi (belgilar o'zgarmaydi)
void checkUtilities(UtilModel& m, const UtilParams& p);

// Yozuvni tahlil qilish (sinov uchun ochiq): 'd' — diametr mm, 'h' — chuqurlik m, 'z' — belgi m, 0 — yo'q
char parseUtilLabel(const std::string& text, double& value);

std::string utilToJson(const UtilModel& m, const UtilParams& p);
void utilFromJson(const std::string& text, UtilModel& m, UtilParams& p);

// Chizmaga: har tur o'z qatlamida 3D poliliniyalar (3D_<KEY>), quduqlar va kesishma belgilari (3D_KESISHMA)
std::vector<std::unique_ptr<IEntity>> drawUtilities3D(const UtilModel& m, double textHeight);

} // namespace asiacad::civil
