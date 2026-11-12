#pragma once
// AsiaCAD GUI — "Armatura 3D" menyusi. Yon tomonda shablonlar paneli (element turlari bo'yicha), o'rtada element turi
// parametrlari va 3D karkas (turlar va qavatlar bo'yicha filtr, beton shaffof), o'ngda ведомость, jami va tekshiruv.
// Bino modeli "Monolit bino" parametrlaridan (padbetonka -> tom) olinadi; "Ведомостni chizmaga" — REBAR buyrug'i.
#include "bim_props.h"
#include "model3d_view.h"
#include "prop_panel.h"
#include "template_sidebar.h"
#include "drawing_set_widget.h"
#include "excel_link.h"
#include "menu_history.h"

#include <asiacad/bim/rebar.h>
#include <asiacad/bim/rebar_import.h>
#include <asiacad/bim/kj_project.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

class QCheckBox;
class QPushButton;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace asiacad::gui {

class RebarDialog : public QDialog {
    Q_OBJECT
public:
    RebarDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent = nullptr);

    void setBuilding(const bim::BuildingParams& p, const std::map<std::string, bim::ElemKind>& layers, const bim::FloorPlans& fp = {});
    DrawingSetWidget* drawings() const { return set_; }
    void setSourceKj(bool kj);                       // false — shablonlar (avto), true — KJ chizmalardan
    const bim::ImportedRebar& imported() const { return imp_; }
    const bim::KjProject& project() const { return kj_; }   // "KJ loyiha" (bitta fayl) natijasi
    bool projectMode() const { return projectMode_; }
    bim::KjParams& kjParams() { return kp_; }
    void recomputeNow() { recompute(); }
    void setShowConcrete(bool on);
    void setUpTo(int level);
    const bim::RebarModel& model() const { return rm_; }
    const bim::BuildingModel& buildingModel() const { return bm_; }   // beton (shablon rejimi — chizmadan, KJ — loyihadan)
    const bim::RebarSettings& settings() const { return rs_; }
    TemplateSidebar* sidebar() const { return side_; }
    ExcelLink* excel() const { return excel_; }
    Model3DView* view() const { return view_; }
    void selectKind(bim::ElemKind k);
    QString templatesPath() const;
    PropPanel* props() const { return props_; }
    void documentChanged() { recompute(); }   // chizma o'zgardi (Xossalar buyrug'i) — model qayta quriladi
    MenuUndo* menuHistory() const { return hist_.get(); }   // Ctrl+Z / Ctrl+Y (menyu varag'ida)

signals:
    void scheduleRequested(const QString& jsonPath);
    void sampleRequested();   // bino yo'q — namuna bino yaratish (MainWindow)
    void commandsRequested(const QStringList& lines);   // Xossalar: chizma obyektini tuzatish (MainWindow bajaradi, undo bilan)

private:
    QPushButton* sample_ = nullptr;
    QString t(const char* key, const QStringList& args = {}) const;
    void buildUi();
    void fillSidebar();
    void loadEditor();     // joriy tur shablonini maydonlarga
    void storeEditor();    // maydonlardan shablonga
    void recompute();
    void render();
    void applyTemplate(const QVariant& data);
    void saveTemplate();

    const i18n::Localizer& loc_;
    const Document& doc_;
    bim::BuildingParams bp_;
    std::map<std::string, bim::ElemKind> layers_;
    bim::BuildingModel bm_;
    bim::RebarSettings rs_;
    bim::RebarModel rm_;
    std::vector<bim::RebarTemplate> templates_;
    bool updating_ = false;

    TemplateSidebar* side_ = nullptr;
    ExcelLink* excel_ = nullptr;
    DrawingSetWidget* set_ = nullptr;
    QComboBox* source_ = nullptr;
    bim::FloorPlans fp_;
    bim::ImportedRebar imp_;
    bim::KjProject kj_;
    bim::KjParams kp_;
    struct State { bim::RebarSettings rs; bim::KjParams kp; };
    std::unique_ptr<MenuHistory<State>> hist_;
    bool projectMode_ = false;
    QWidget* kjBox_ = nullptr;
    QDoubleSpinBox *kjDatum_ = nullptr, *kjX0_ = nullptr, *kjY0_ = nullptr, *kjRot_ = nullptr;
    QCheckBox *kjDatumAuto_ = nullptr, *kjFTopAuto_ = nullptr;
    QDoubleSpinBox *kjFTop_ = nullptr, *kjFT_ = nullptr, *kjCover_ = nullptr, *kjIn_ = nullptr, *kjOut_ = nullptr;
    QComboBox* kind_ = nullptr;
    QFormLayout* form_ = nullptr;
    QLabel* legend_ = nullptr;
    QDoubleSpinBox *cover_ = nullptr, *lap_ = nullptr, *dStir_ = nullptr, *sStir_ = nullptr, *sStirEnd_ = nullptr, *chairs_ = nullptr,
                   *ties_ = nullptr;
    QDoubleSpinBox* dMain_ = nullptr;
    QWidget* rowW_[12] = {};  // forma qatorlari (tur bo'yicha yashiriladi)
    QDoubleSpinBox* d_[4] = {};
    QDoubleSpinBox* s_[4] = {};
    QSpinBox* n_ = nullptr;
    QCheckBox* top_ = nullptr;
    QComboBox *clsMain_ = nullptr, *clsStir_ = nullptr;
    QCheckBox* showKind_[6] = {};
    QCheckBox* concrete_ = nullptr;
    QSlider* upTo_ = nullptr;
    QLabel* upToLbl_ = nullptr;
    Model3DView* view_ = nullptr;
    QTableWidget* sched_ = nullptr;
    QListWidget* totals_ = nullptr;
    QListWidget* checks_ = nullptr;
    QLabel* summary_ = nullptr;
    // Xossalar
    QTabWidget* tabs_ = nullptr;
    PropPanel* props_ = nullptr;
    BimSources src_;
    std::vector<EntityId> prismSrc_;    // bm_.prisms bilan parallel (shablon rejimi: chizmadagi manba)
    std::map<QString, int> keyPrism_;   // 3D kaliti -> bm_.prisms
    std::map<QString, QString> keySlab_;   // KJ: 3D kaliti -> plita turi (kj_.slabs[].tag)
    void onPicked(const QString& key);
    void fillTemplate(bim::ElemKind k);       // shu tur armatura shabloni maydonlari (hamma elementlarga)
    void fillKjSlab(const bim::Prism& pr, const QString& tag);
    bim::KjSlabEdit& slabEdit(const QString& tag);
    Vec3 kjLocal(const Vec3& p) const;        // KJ: absolyut (geodezik bog'langan) -> o'qlar bo'yicha lokal
};

} // namespace asiacad::gui
