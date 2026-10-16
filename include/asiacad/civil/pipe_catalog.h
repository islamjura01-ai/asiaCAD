#pragma once
// AsiaCAD — quvurlar katalogi va shablonlari (kanalizatsiya, suv, bosimli tarmoqlar).
// Har bir o'lcham: material, standart, sinf (SN/SDR/klass), DN, tashqi diametr, devor qalinligi,
// massa, Manning n, rang. `verified` — standart jadvali bilan solishtirilgan (false: namunaviy qiymat,
// loyihada ishlatishdan oldin standart bo'yicha tekshiring — UI buni ko'rsatadi).
#include <string>
#include <vector>

namespace asiacad::civil {

enum class PipeMaterial { PVC, PP, PE, AsbestosCement, Steel, DuctileIron, Concrete };

const char* materialKey(PipeMaterial m);           // "PVC", "PP", ... (i18n: pipe.mat.<key>)
bool materialFromKey(const std::string& k, PipeMaterial& out);
bool isMetal(PipeMaterial m);
double materialDensity(PipeMaterial m);            // t/m³ (massa berilmaganda hisoblash uchun)
int materialAci(PipeMaterial m);                   // chizmadagi rang (ACI)
unsigned materialRgb(PipeMaterial m);              // 3D ko'rinish rangi 0xRRGGBB

struct PipeSpec {
    std::string id;         // "PVC-SN8-200"
    PipeMaterial material = PipeMaterial::PVC;
    std::string standard;   // "EN 1401 / GOST R 54475"
    std::string klass;      // "SN8", "SDR17", "K9", "BNT", "TS"
    int dn = 0;             // shartli diametr, mm (plastikda — tashqi diametr)
    double od = 0;          // tashqi diametr, mm
    double wall = 0;        // devor qalinligi, mm
    double massKgM = 0;     // kg/m (0 — zichlikdan hisoblanadi)
    double manningN = 0.013;
    bool pressure = false;  // bosimli quvur (SDR/PN)
    bool socket = true;     // raструб (qo'ng'iroqli) ulanish
    bool verified = false;

    double id_mm() const { return od - 2 * wall; }   // ichki diametr
    double mass() const;                            // kg/m
    std::string label() const;                      // "PVC SN8 Ø200×5.9"
};

// Shablon — tayyor tanlov to'plami (tizim, quvur, nishablik, boshlang'ich chuqurlik, quduq)
struct PipeTemplate {
    std::string id;
    std::string name;       // i18n kaliti ("tpl.yard_pvc160") yoki foydalanuvchi nomi
    std::string system = "K1";  // K1 maishiy, K2 yomg'ir, K3 ishlab chiqarish, N bosimli
    std::string specId;
    double slope = 0.008;
    double startDepth = 1.5;    // birinchi quduqda quvur tubigacha, m
    double manholeD = 1.0;      // quduq ichki diametri, m (0 — avto)
    bool street = false;        // ko'cha tarmog'i (min Ø200)
    bool user = false;          // foydalanuvchi saqlagan
};

class PipeCatalog {
public:
    static const PipeCatalog& builtin();

    const std::vector<PipeSpec>& specs() const { return specs_; }
    const PipeSpec* find(const std::string& id) const;
    std::vector<const PipeSpec*> byMaterial(PipeMaterial m) const;
    std::vector<PipeMaterial> materials() const;

    std::vector<PipeTemplate> templates;  // o'rnatilgan + foydalanuvchi

    // Foydalanuvchi shablonlari (JSON massiv). Xato bo'lsa std::runtime_error.
    static std::vector<PipeTemplate> loadTemplates(const std::string& path);
    static void saveTemplates(const std::string& path, const std::vector<PipeTemplate>& t);
    static std::string templatesToJson(const std::vector<PipeTemplate>& t);
    static std::vector<PipeTemplate> templatesFromJson(const std::string& text);

    std::vector<PipeSpec> specs_;
};

} // namespace asiacad::civil
