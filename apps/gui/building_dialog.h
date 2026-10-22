#pragma once
// AsiaCAD GUI — "Monolit bino" menyusi: chizma qatlamlarini element turlariga bog'lash, qavat/fundament/padbetonka
// parametrlari, beton sinflari, jonli 3D model (qavatlar bo'yicha ko'rsatish), PTO jadvali va tekshiruv.
// "PTO jadvalini chizmaga" — parametrlar JSON'ga yoziladi va BUILDING buyrug'i Interpreter orqali bajariladi.
#include "model3d_view.h"
#include "template_sidebar.h"
#include "drawing_set_widget.h"
#include "excel_link.h"

#include <asiacad/bim/building.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

class QPushButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QSlider;
class QSpinBox;
class QTableWidget;

namespace asiacad::gui {

class BuildingDialog : public QDialog {
    Q_OBJECT
public:
    BuildingDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent = nullptr);

    bim::BuildingParams params() const;
    std::map<std::string, bim::ElemKind> layerMap() const;
    const bim::BuildingModel& model() const { return model_; }
    void reread();  // chizmadan qayta o'qish (qatlamlar ro'yxati yangilanadi)
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

private:
    QPushButton* sample_ = nullptr;
    QString t(const char* key, const QStringList& args = {}) const;
    void buildUi();
    void fillLayers();
    void recompute();
    void render();

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
};

} // namespace asiacad::gui
