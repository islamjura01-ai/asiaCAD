#pragma once
// AsiaCAD GUI — konstruksiya shablonlari va geo-joylash:
//  * TemplatesDialog — Metall karkas / Zina / Rampa / Tirgak devor: parametrlar, jonli plan va 3D, xulosa va tekshiruvlar;
//    joylash — X, Y, burilish, absolyut belgi (dengiz sathidan); "Chizmadan" — tayanch nuqta va yo'nalishni chizmada ko'rsatish
//  * GeoPlaceDialog  — tanlangan obyektlarni 1–2 nuqta bo'yicha geodezik koordinata va belgiga o'tkazish (GEOPLACE)
#include "drive_view.h"
#include "generator_dialogs.h"

#include <asiacad/bim/templates.h>

#include <memory>

class QFormLayout;
class QStackedWidget;
class QTabWidget;

namespace asiacad::gui {

class TemplatesDialog : public GenBase {
    Q_OBJECT
public:
    enum Page { SteelFrame = 0, Stair = 1, Ramp = 2, RetainingWall = 3 };
    TemplatesDialog(const i18n::Localizer& loc, bool dark, QWidget* parent = nullptr);
    void setPage(int p);
    int page() const;
    QString command() const;      // "STAIR \"…json\"" (joriy sahifa)
    bool valid() const { return valid_; }
    std::size_t previewCount() const;
    QTabWidget* tabs() const { return tabs_; }
    void setPicked(const std::vector<Vec3>& pts);   // tayanch (+ yo'nalish) chizmadan
    void setPlacement(double x, double y, double rotDeg, double z0);
    bim::SteelFrameSpec steelSpec() const;
    bim::StairSpec stairSpec() const;
    bim::RampSpec rampSpec() const;
    bim::RetainSpec retainSpec() const;

signals:
    void commandReady(const QString& line);
    void pickRequested(int n, const QStringList& prompts);

private:
    void update_();
    bim::Placement placement() const;
    QDoubleSpinBox* dbl(QFormLayout* f, const char* key, double v, double lo, double hi, double step, int dec);
    QTabWidget* tabs_ = nullptr;
    QTabWidget* views_ = nullptr;
    // metall karkas
    QComboBox *sfType_ = nullptr, *pCol_ = nullptr, *pBeam_ = nullptr, *pBeam2_ = nullptr, *pRaf_ = nullptr, *pPur_ = nullptr, *pBr_ = nullptr;
    QStackedWidget* sfStack_ = nullptr;
    QLineEdit *sfDx_ = nullptr, *sfDy_ = nullptr;
    QSpinBox *sfFloors_ = nullptr, *sfBays_ = nullptr;
    QDoubleSpinBox *sfFloorH_ = nullptr, *sfSpan_ = nullptr, *sfBay_ = nullptr, *sfEave_ = nullptr, *sfRidge_ = nullptr, *sfPurlin_ = nullptr;
    QCheckBox* sfBracing_ = nullptr;
    // zina
    QComboBox *stType_ = nullptr, *stStringer_ = nullptr;
    QDoubleSpinBox *stH_ = nullptr, *stW_ = nullptr, *stGap_ = nullptr, *stTread_ = nullptr, *stRiser_ = nullptr, *stLanding_ = nullptr, *stWaist_ = nullptr;
    QCheckBox* stSteel_ = nullptr;
    // rampa
    QComboBox* rpKind_ = nullptr;
    QDoubleSpinBox *rpH_ = nullptr, *rpW_ = nullptr, *rpSlope_ = nullptr, *rpTrans_ = nullptr, *rpEvery_ = nullptr, *rpLand_ = nullptr, *rpWall_ = nullptr,
                   *rpPar_ = nullptr, *rpFloor_ = nullptr;
    // tirgak devor
    QComboBox* rwType_ = nullptr;
    QDoubleSpinBox *rwH_ = nullptr, *rwTop_ = nullptr, *rwBot_ = nullptr, *rwB_ = nullptr, *rwToe_ = nullptr, *rwBase_ = nullptr, *rwLen_ = nullptr,
                   *rwPhi_ = nullptr, *rwGamma_ = nullptr, *rwQ_ = nullptr, *rwMu_ = nullptr, *rwR_ = nullptr;
    // joylash
    QDoubleSpinBox *atX_ = nullptr, *atY_ = nullptr, *atRot_ = nullptr, *atZ_ = nullptr;
    QLabel* info_ = nullptr;
    PlanPreview* plan_ = nullptr;
    DriveView* view3d_ = nullptr;
    civil::DriveScene scene_;
    double orbitYaw_ = -50, orbitPitch_ = 28, orbitR_ = 30;
    Vec3 center_;
    bool valid_ = false;
};

class GeoPlaceDialog : public GenBase {
    Q_OBJECT
public:
    GeoPlaceDialog(const i18n::Localizer& loc, bool dark, const QString& selArg, int selCount, QWidget* parent = nullptr);
    QString command() const;
    bool valid() const { return valid_; }
    void setPicked(int which, const Vec3& p);   // 1 yoki 2 — lokal nuqta chizmadan
    void setWorld(int which, double X, double Y, double H);
    void setSecond(bool on);
    QString resultText() const;

signals:
    void commandReady(const QString& line);
    void pickRequested(int which);

private:
    void update_();
    QString sel_;
    QDoubleSpinBox *lx1_ = nullptr, *ly1_ = nullptr, *lz1_ = nullptr, *wx1_ = nullptr, *wy1_ = nullptr, *wh1_ = nullptr;
    QDoubleSpinBox *lx2_ = nullptr, *ly2_ = nullptr, *wx2_ = nullptr, *wy2_ = nullptr;
    QCheckBox *second_ = nullptr, *geo_ = nullptr, *scale_ = nullptr, *keepZ_ = nullptr;
    QWidget* p2box_ = nullptr;
    QLabel* res_ = nullptr;
    bool valid_ = false;
};

} // namespace asiacad::gui
