#pragma once
// AsiaCAD GUI — "Monolit bino" menyusi: chizma qatlamlarini element turlariga bog'lash, qavat/fundament/padbetonka
// parametrlari, beton sinflari, jonli 3D model (qavatlar bo'yicha ko'rsatish), PTO jadvali va tekshiruv.
// "PTO jadvalini chizmaga" — parametrlar JSON'ga yoziladi va BUILDING buyrug'i Interpreter orqali bajariladi.
#include "bim_props.h"
#include "model3d_view.h"
#include "prop_panel.h"
#include "template_sidebar.h"
#include "drawing_set_widget.h"
#include "excel_link.h"

#include <asiacad/bim/building.h>
#include <asiacad/bim/rebar.h>
#include <asiacad/bim/norms.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

class QPushButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QSlider;
class QToolButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace asiacad::gui {

class BuildingDialog : public QDialog {
    Q_OBJECT
public:
    BuildingDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent = nullptr);

    bim::BuildingParams params() const;
    std::map<std::string, bim::ElemKind> layerMap() const;
    const bim::BuildingModel& model() const { return model_; }
    void reread();  // chizmadan qayta o'qish (qatlamlar ro'yxati yangilanadi)
    // Chizma o'zgardi (Xossalar panelidan buyruq bajarildi): yangi qatlamlar qo'shiladi, bog'lashlar saqlanadi, model qayta quriladi
    void documentChanged();
    PropPanel* props() const { return props_; }
    EntityId prismSource(int i) const { return i >= 0 && i < static_cast<int>(prismSrc_.size()) ? prismSrc_[static_cast<std::size_t>(i)] : 0; }
    void setFloors(int n);
    Model3DView* view() const { return view_; }
    TemplateSidebar* sidebar() const { return side_; }
    ExcelLink* excel() const { return excel_; }
    DrawingSetWidget* drawings() const { return set_; }
    const bim::FloorPlans& floorPlans() const { return fp_; }
    bim::PlanInput typicalPlan() const;
    void setParams(const bim::BuildingParams& p, const std::map<std::string, bim::ElemKind>& layers);
    void applyTypology(const QString& id);  // btpl.* — bino tiplari

signals:
    void scratchRequested();                    // "0 dan" — generator oynasi (MainWindow ochadi)
    void locateRequested(double x, double y);   // tuzatish: tekshiruvdagi joyni chizmada ko'rsatish
    void tableRequested(const QString& jsonPath);
    void rebarRequested();
    void sampleRequested();                     // namuna bino (o'qlar to'ri) — MainWindow GRID bajaradi
    void open3DRequested();                     // asosiy oynadagi 3D ko'rinish
    void commandsRequested(const QStringList& lines);   // Xossalar: chizma obyektini tuzatish (MainWindow bajaradi, undo bilan)

private:
    // 3D qatlamlari (ko'rinish): element turlari (beton va armaturaga birdek) + beton, armatura qatlamlari
    enum ShowGroup { GFound, GColumn, GWall, GSlab, GStair, GInfill, GGlass, GConcrete, GRebar, GCount };
    QToolButton* showChip_[GCount] = {};
    static int groupOf(bim::ElemKind k);
    bool rebarDirty_ = true;
    bim::RebarModel rebar_;
    void applyPreset(int preset);   // 0 — hammasi, 1 — karkas (beton), 2 — faqat armatura
public:
    void setGroupVisible(int g, bool on);   // sinov va menyu uchun
    bool groupVisible(int g) const;
    // ShNQ: elementlar va armatura shu qoidalar bilan tekshiriladi; on — 3D da ranglash (xato — qizil, ogohlantirish — sariq)
    void setNormRules(const std::vector<bim::NormRule>* rules);
    void runNormCheck(bool on);
    const bim::NormReport& normReport() const { return normRep_; }
    bool normOn() const { return normOn_; }
private:
    const std::vector<bim::NormRule>* normRules_ = nullptr;
    QPushButton* normBtn_ = nullptr;
    bool normOn_ = false, normDirty_ = true;
    bim::NormReport normRep_;
    std::vector<bim::NormRule> normRulesUsed_;
    std::map<int, int> normPrism_, normBar_;   // prizma / sterjen -> eng og'ir daraja
    void computeNorms();
private:
    QPushButton* sample_ = nullptr;
    bool autoFloors_ = true;   // keyingi recompute: chizma / varaqlar bo'yicha qavatlar soni oshirilsin
    QString t(const char* key, const QStringList& args = {}) const;
    void buildUi();
    void fillLayers(bool keep = false);
    void recompute();
    void render();
    void onPicked(const QString& key);   // 3D da tanlov — Xossalar paneli

    const i18n::Localizer& loc_;
    const Document& doc_;
    bim::BuildingModel model_;
    bool updating_ = false;

    TemplateSidebar* side_ = nullptr;
    ExcelLink* excel_ = nullptr;
    DrawingSetWidget* set_ = nullptr;
    bim::FloorPlans fp_;
    QTableWidget* layers_ = nullptr;
    QSpinBox* floors_ = nullptr;
    QDoubleSpinBox *height_ = nullptr, *slab_ = nullptr, *roof_ = nullptr, *ftop_ = nullptr, *ft_ = nullptr, *fover_ = nullptr,
                   *stripW_ = nullptr, *lean_ = nullptr, *wallT_ = nullptr, *zeroAbs_ = nullptr;
    QCheckBox* zeroAuto_ = nullptr;
    QCheckBox* strip_ = nullptr;
    QComboBox *clsLean_ = nullptr, *clsFound_ = nullptr, *clsFrame_ = nullptr, *clsSlab_ = nullptr;
    QSlider* showUpTo_ = nullptr;
    QLabel* showLbl_ = nullptr;
    Model3DView* view_ = nullptr;
    QTableWidget* pto_ = nullptr;
    QListWidget* checks_ = nullptr;
    QLabel* summary_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    PropPanel* props_ = nullptr;
    BimSources src_;                       // chizmadagi manba obyektlar (yopiq poliliniyalar)
    std::vector<EntityId> prismSrc_;       // model_.prisms bilan parallel
    std::map<QString, int> keyPrism_;      // 3D kaliti -> prizma
};

} // namespace asiacad::gui
