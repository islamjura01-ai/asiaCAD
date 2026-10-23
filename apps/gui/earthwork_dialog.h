#pragma once
// AsiaCAD GUI — "Yer ishlari" (TopografGeodezist uslubida): yuza va gorizontallar, bo'ylama profil, ko'ndalang kesimlar
// (razrezlar, hajm bilan), kartogramma, hajm hisobi. Natija chizmaga AsiaCAD buyrug'i orqali (undo bilan), Excelga — XLSX=.
#include <asiacad/civil/terrain.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTabWidget;

namespace asiacad::gui {

class EarthworkDialog : public QDialog {
    Q_OBJECT
public:
    enum Tab { Surface = 0, Profile = 1, Sections = 2, Cartogram = 3, Volume = 4 };
    EarthworkDialog(const i18n::Localizer& loc, const Document& doc, std::function<std::vector<EntityId>()> selection, QWidget* parent = nullptr);
    void setTab(int t);
    int tab() const;
    void refreshLayers();
    void setGroundLayer(const QString& l);
    void setDesignMode(int m);                 // 0 yuza (qatlam), 1 gorizontal tekislik, 2 qiya tekislik, 3 balans
    void setDesignLayer(const QString& l);
    void setDesignZ(double z);
    void takePath();                           // tanlovdagi birinchi chiziq — o'q
    void takeBoundary();                       // tanlovdagi yopiq kontur — chegara
    void setPath(EntityId id) { path_ = id; updateInfo(); }
    void setBoundary(EntityId id) { bound_ = id; updateInfo(); }
    void setAt(const Vec3& p);
    QString command(int tab, const QString& xlsx = {}) const;   // joriy sahifa buyrug'i
    bool compute();                            // kartogramma / hajm — oynada hisob
    QLabel* result() const { return res_; }

signals:
    void commandReady(const QString& line);
    void pickRequested();

private:
    QString t(const char* key, const QStringList& args = {}) const;
    QString layerToken(const QComboBox* c) const;
    void updateInfo();
    void runTab(bool excel);
    QString commandBody(int tab, const QString& xlsx) const;

    const i18n::Localizer& loc_;
    const Document& doc_;
    std::function<std::vector<EntityId>()> selection_;
    EntityId path_ = kInvalidId, bound_ = kInvalidId;
    std::optional<Vec3> at_;
    QTabWidget* tabs_ = nullptr;
    QComboBox *eg_ = nullptr, *fgMode_ = nullptr, *fg_ = nullptr;
    QDoubleSpinBox *z_ = nullptr, *ix_ = nullptr, *iy_ = nullptr, *maxEdge_ = nullptr;
    QDoubleSpinBox *cStep_ = nullptr, *cMajor_ = nullptr;
    QDoubleSpinBox *pStep_ = nullptr, *pH_ = nullptr, *pV_ = nullptr, *pPk_ = nullptr;
    QDoubleSpinBox *sStep_ = nullptr, *sL_ = nullptr, *sR_ = nullptr, *sScale_ = nullptr;
    QSpinBox* sCols_ = nullptr;
    QDoubleSpinBox *kCell_ = nullptr, *vCell_ = nullptr;
    QCheckBox* dyn_ = nullptr;   // dinamik bog'lanish (Civil 3D kabi)
public:
    QCheckBox* dynamicBox() const { return dyn_; }
private:
    QLabel *pathInfo_ = nullptr, *boundInfo_ = nullptr, *atInfo_ = nullptr, *res_ = nullptr;
};

} // namespace asiacad::gui
