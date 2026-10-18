#pragma once
// AsiaCAD CLI — matnli buyruq interpretatori (AutoCAD command line uslubida).
// Buyruq nomlari, kalit so'zlar va barcha xabarlar Localizer orqali (i18n/*.json).
#include <asiacad/core/commands.h>
#include <asiacad/core/dimension.h>
#include <asiacad/i18n/localizer.h>

#include <ostream>
#include <string>
#include <vector>

namespace asiacad::cli {

class Interpreter {
public:
    Interpreter(Document& doc, CommandStack& stack, i18n::Localizer& loc, std::ostream& out)
        : doc_(doc), stack_(stack), loc_(loc), out_(out) {}

    // Bitta qatorni bajaradi. EXIT bo'lsa false qaytaradi.
    bool run(const std::string& line);
    const std::string& currentLayer() const { return layer_; }
    const std::string& currentFile() const { return currentFile_; }

private:
    using Args = std::vector<std::string>;
    void dispatch(const std::string& canon, const Args& a);
    bool dispatchDraft(const std::string& canon, const Args& a);  // interpreter_draft.cpp: Draw/Modify/Layers/Annotation, belgilar

    Vec3 parsePoint(const std::string& tok) const;
    Vec3 parseRawPoint(const std::string& tok) const;  // x,y[,z] yoki @dx,dy — bog'lanishsiz
    std::vector<double> parseNumbers(const std::string& csv) const;
    double parseNumber(const std::string& tok) const;
    std::vector<EntityId> parseSelection(const std::string& tok) const;
    void need(const Args& a, std::size_t n, const std::string& canon) const;
    void addAndReport(const std::string& canon, std::unique_ptr<IEntity> e, const char* msgKey);
    void addMany(const char* name, std::vector<std::unique_ptr<IEntity>> es);
    void addLayered(const std::string& name, std::vector<std::unique_ptr<IEntity>> es);  // qatlamlari saqlanadi (yo'qlari yaratiladi)
    void addDimension(const std::string& canon, std::unique_ptr<DimensionEntity> d);
    void say(const std::string& key, const std::vector<std::string>& args = {});
    void help();

    Document& doc_;
    CommandStack& stack_;
    i18n::Localizer& loc_;
    std::ostream& out_;
    std::string layer_ = "0";
    int color_ = kColorByLayer;
    std::vector<EntityId> last_;  // oxirgi yaratilgan obyektlar (LAST tanlovi)
    mutable Vec3 lastPt_;         // @dx,dy nisbiy kiritish uchun
    std::string currentFile_;     // OPEN/SAVE qilingan .acad fayl
    double filletR_ = 0.0;        // FILLET radiusi (AutoCAD FILLETRAD kabi eslab qolinadi)
    double dimHeight_ = 2.5;      // o'lcham matni balandligi (DIM* buyruqlarida [h] berilmasa)
    double aperture_ = 1.0;
    std::vector<Layer> isoSaved_;   // LAYISO dan oldingi holat (LAYUNISO uchun)       // OSNAP va PICK radiusi (chizma birligida)
};

// Buyruq sintaksisi (tilga bog'liq emas): p = nuqta, sel = tanlov, r = radius, h = balandlik
const char* commandSyntax(const std::string& canonical);
std::string localizeSyntax(const i18n::Localizer& loc, std::string s);

} // namespace asiacad::cli
