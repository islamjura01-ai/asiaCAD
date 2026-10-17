#pragma once
// AsiaCAD GUI — "Armatura 3D" menyusi. Yon tomonda shablonlar paneli (element turlari bo'yicha), o'rtada element turi
// parametrlari va 3D karkas (turlar va qavatlar bo'yicha filtr, beton shaffof), o'ngda ведомость, jami va tekshiruv.
// Bino modeli "Monolit bino" parametrlaridan (padbetonka -> tom) olinadi; "Ведомостni chizmaga" — REBAR buyrug'i.
#include "pipe3d_view.h"
#include "template_sidebar.h"
#include "drawing_set_widget.h"
#include "excel_link.h"

#include <asiacad/bim/rebar.h>
#include <asiacad/bim/rebar_import.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QSlider;
class QSpinBox;
class QTableWidget;

namespace asiacad::gui {

class RebarDialog : public QDialog {
    Q_OBJECT
public:
    RebarDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent = nullptr);

    void setBuilding(const bim::BuildingParams& p, const std::map<std::string, bim::ElemKind>& layers, const bim::FloorPlans& fp = {});
    DrawingSetWidget* drawings() const { return set_; }
    void setSourceKj(bool kj);                       // false — shablonlar (avto), true — KJ chizmalardan
    const bim::ImportedRebar& imported() const { return imp_; }
    const bim::RebarModel& model() const { return rm_; }
    const bim::RebarSettings& settings() const { return rs_; }
    TemplateSidebar* sidebar() const { return side_; }
    ExcelLink* excel() const { return excel_; }
    Pipe3DView* view() const { return view_; }
    void selectKind(bim::ElemKind k);
    QString templatesPath() const;

signals:
    void scheduleRequested(const QString& jsonPath);

private:
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
    Pipe3DView* view_ = nullptr;
    QTableWidget* sched_ = nullptr;
    QListWidget* totals_ = nullptr;
    QListWidget* checks_ = nullptr;
    QLabel* summary_ = nullptr;
};

} // namespace asiacad::gui
