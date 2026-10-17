#pragma once
// AsiaCAD GUI — "Haydash" (Drive): tanlangan chiziq bo'ylab 3D model ichida birinchi shaxs ko'rinishida yurish.
//  * Rejimlar: avtomobil, yuk mashinasi, piyoda (bino ichi), dron
//  * Balandlik manbai: chiziq Z, koridor yuzasi, yer (syomka), bino qavati, doimiy belgi
//  * Ko'rinish masofasi (profil + 3D to'siqlar) va talab; kamchiliklar ro'yxati — bosilsa o'sha joyga
//  * Pastda — profil va ko'rinish grafigi (bosib siljitish), yuqorida — HUD va kichik xarita
#include "drive_view.h"

#include <asiacad/civil/drive.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QTimer;

namespace asiacad::gui {

struct DriveSource {
    QString name;
    std::function<double(double, double)> z;   // bo'sh — chiziqning o'z Z i; NaN — shu nuqtada yuza yo'q (chiziq Z qoladi)
    bool constant = false;                     // doimiy belgi (spin)
    bool walk = false;                         // piyoda rejimi uchun (bino qavati)
};

class DriveProfile;

class DriveDialog : public QDialog {
    Q_OBJECT
public:
    DriveDialog(const i18n::Localizer& loc, QWidget* parent = nullptr);

    void setPath(const std::vector<Vec3>& pts, const QString& title);
    void setSources(const std::vector<DriveSource>& s, int def);
    void setScene(civil::DriveScene sc);
    void setMode(int m);                 // 0 avtomobil, 1 yuk, 2 piyoda, 3 dron
    void setStation(double s);
    void setSource(int i);
    void play(bool on);
    bool playing() const;
    void step(double dtSec);             // animatsiya qadami (sinov uchun ham)

    double station() const { return s_; }
    const civil::DrivePath& path() const { return path_; }
    const civil::SightResult& sight() const { return sight_; }
    const civil::DriveScene& scene() const { return scene_; }
    DriveView* view() const { return view_; }
    QListWidget* checks() const { return checks_; }

private:
    QString t(const char* key, const QStringList& args = {}) const;
    void rebuildPath();
    void recomputeSight();
    void updateCamera();
    void drawOverlay(QPainter& p);

    const i18n::Localizer& loc_;
    std::vector<Vec3> raw_;
    std::vector<DriveSource> src_;
    civil::DrivePath path_;
    civil::DriveScene scene_;
    civil::SightResult sight_;
    double s_ = 0;
    bool updating_ = false;

    DriveView* view_ = nullptr;
    DriveProfile* prof_ = nullptr;
    QComboBox *mode_ = nullptr, *zsrc_ = nullptr;
    QDoubleSpinBox *speed_ = nullptr, *design_ = nullptr, *eye_ = nullptr, *obj_ = nullptr, *ahead_ = nullptr, *offset_ = nullptr, *fov_ = nullptr,
                   *zconst_ = nullptr;
    QCheckBox *reverse_ = nullptr, *obst_ = nullptr;
    QPushButton* playB_ = nullptr;
    QSlider* pos_ = nullptr;
    QListWidget* checks_ = nullptr;
    QLabel *summary_ = nullptr, *sceneLbl_ = nullptr;
    QTimer* timer_ = nullptr;
    qint64 lastTick_ = 0;
};

} // namespace asiacad::gui
