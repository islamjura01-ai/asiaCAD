#pragma once
// AsiaCAD GUI — "0 dan chizish": har menyudagi kiritish oynalari. Natija AsiaCAD buyrug'i (GRID / ALIGN / UTILNET / CITY) orqali
// chizmaga yoziladi — undo bilan, buyruq satri bilan bir xil yo'l; keyin modul o'sha chizmani import kabi o'qiydi va tuzatiladi.
//  * GridInputDialog  — bino o'qlari to'ri (qadamlar "6x5,4.5"), ustun, devor, plita, qavatlar; jonli plan
//  * AlignInputDialog — yo'l/kanal/koridor o'qi: burilish nuqtalari (X, Y, R) va loyiha profili (PK, belgi); jonli plan
//  * NetInputDialog   — yer osti tarmog'i: quduqlar va quvurlar jadvali; jonli plan
//  * CityDialog       — shahar mavzesi: shablonlar, parametrlar, plan va 3D massiv, ko'rsatkichlar
#include "pipe3d_view.h"
#include "template_sidebar.h"

#include <asiacad/civil/generators.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace asiacad::gui {

// Generator natijasining 2D ko'rinishi (chiziq, poliliniya, aylana; matnlar — kichik)
class PlanPreview : public QWidget {
public:
    explicit PlanPreview(QWidget* parent = nullptr) : QWidget(parent) { setMinimumSize(260, 200); }
    void setEntities(civil::Entities es) { es_ = std::move(es); update(); }
    void setDark(bool d) { dark_ = d; update(); }
    std::size_t count() const { return es_.size(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    civil::Entities es_;
    bool dark_ = true;
};

class GenBase : public QDialog {
    Q_OBJECT
public:
    GenBase(const i18n::Localizer& loc, bool dark, QWidget* parent);
    // fayl (vaqtinchalik) — generator JSON'i; MainWindow buyruqni ishga tushiradi
    QString writeTemp(const std::string& json, const QString& name) const;

protected:
    QString t(const char* key, const QStringList& args = {}) const;
    const i18n::Localizer& loc_;
    bool dark_;
};

class GridInputDialog : public GenBase {
    Q_OBJECT
public:
    GridInputDialog(const i18n::Localizer& loc, bool dark, QWidget* parent = nullptr);
    civil::GridSpec spec() const;
    QString command() const;           // "GRID \"file.json\""
    void setSpacing(const QString& dx, const QString& dy);
    PlanPreview* preview() const { return prev_; }
    bool valid() const;

private:
    void update_();
    QLineEdit *dx_ = nullptr, *dy_ = nullptr;
    QDoubleSpinBox *colB_ = nullptr, *colH_ = nullptr, *wallT_ = nullptr, *floorH_ = nullptr, *x0_ = nullptr, *y0_ = nullptr;
    QCheckBox *walls_ = nullptr, *slab_ = nullptr, *core_ = nullptr;
    QSpinBox* floors_ = nullptr;
    QLabel* info_ = nullptr;
    PlanPreview* prev_ = nullptr;
};

class AlignInputDialog : public GenBase {
    Q_OBJECT
public:
    // canal = true: o'q qatlami suv yo'li moduli taniydigan nom bilan ("CANAL_O'Q")
    AlignInputDialog(const i18n::Localizer& loc, bool dark, bool canal, QWidget* parent = nullptr);
    civil::AlignSpec spec() const;
    QString command() const;
    void setSpec(const civil::AlignSpec& a);
    PlanPreview* preview() const { return prev_; }
    QTableWidget* pi() const { return pi_; }
    QTableWidget* pvi() const { return pvi_; }

private:
    void update_();
    bool canal_;
    QTableWidget *pi_ = nullptr, *pvi_ = nullptr;
    QLabel* info_ = nullptr;
    PlanPreview* prev_ = nullptr;
};

class NetInputDialog : public GenBase {
    Q_OBJECT
public:
    NetInputDialog(const i18n::Localizer& loc, bool dark, QWidget* parent = nullptr);
    civil::NetSpec spec() const;
    QString command() const;
    void setSpec(const civil::NetSpec& n);
    void chain();                      // ketma-ket quduqlarni quvur bilan ulash
    PlanPreview* preview() const { return prev_; }
    QTableWidget* nodes() const { return nodes_; }
    QTableWidget* pipes() const { return pipes_; }

private:
    void update_();
    void addPipeRow(int from, int to, const std::string& kind, double dn);
    QTableWidget *nodes_ = nullptr, *pipes_ = nullptr;
    QLabel* info_ = nullptr;
    PlanPreview* prev_ = nullptr;
    bool updating_ = false;
};

class CityDialog : public GenBase {
    Q_OBJECT
public:
    CityDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent = nullptr);
    civil::CitySpec spec() const;
    void setSpec(const civil::CitySpec& c);
    const civil::CityResult& result() const { return r_; }
    TemplateSidebar* sidebar() const { return side_; }
    Pipe3DView* view() const { return view_; }
    PlanPreview* preview() const { return prev_; }
    void recompute();

signals:
    void drawRequested(const QString& jsonPath);

private:
    const Document& doc_;
    civil::CityResult r_;
    bool updating_ = false;
    TemplateSidebar* side_ = nullptr;
    QSpinBox *nx_ = nullptr, *ny_ = nullptr, *fmin_ = nullptr, *fmax_ = nullptr, *seed_ = nullptr;
    QDoubleSpinBox *blockW_ = nullptr, *blockD_ = nullptr, *street_ = nullptr, *carriage_ = nullptr, *setback_ = nullptr, *bldW_ = nullptr,
                   *bldD_ = nullptr, *gap_ = nullptr, *floorH_ = nullptr, *x0_ = nullptr, *y0_ = nullptr, *ground_ = nullptr;
    QLabel* stats_ = nullptr;
    PlanPreview* prev_ = nullptr;
    Pipe3DView* view_ = nullptr;
};

} // namespace asiacad::gui
