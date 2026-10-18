#pragma once
// AsiaCAD GUI — "Geodezik tahlil" (ijroiy syomka): GeoUzPro / Geo_Deviation uslubida.
//  Nuqtalar: TXT/CSV/XLSX import-eksport (geodezik tartib X — shimol), qatlamdan yoki tanlovdan.
//  Tahlil turlari: reja/fasad, balandlik, vertikallik (ustun, devor, qoziq), qiya o'q (anker, qiya qoziq), loyiha nuqtasi.
//  Natija: jadval (ikki marta bosish — chizmada ko'rsatish), xulosa, chizmaga strelka/qiymat/B-P, Excel va CSV.
#include <asiacad/civil/survey.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

#include <functional>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

namespace asiacad::gui {

class SurveyDialog : public QDialog {
    Q_OBJECT
public:
    SurveyDialog(const i18n::Localizer& loc, const Document& doc, std::function<std::vector<EntityId>()> selection, QWidget* parent = nullptr);
    void setKind(civil::DevKind k);
    civil::DevKind kind() const;
    civil::DevParams params() const;
    void refreshLayers();                     // chizma o'zgarganda
    void setPointsLayer(const QString& l);
    void setRefLayer(const QString& l);       // "" — yo'q
    void takePointsSelection();
    void takeRefSelection();
    bool run();                               // tahlil (ko'rinish) — jadval va xulosa
    const civil::DevResult& result() const { return res_; }
    QString drawCommand() const;              // "DEVIATION …"
    QString importCommand(const QString& path) const;
    bool exportExcel(const QString& path);
    bool exportCsv(const QString& path);
    QTableWidget* table() const { return table_; }
    QLabel* summary() const { return sum_; }

signals:
    void commandReady(const QString& line);
    void locateRequested(double x, double y);

private:
    QString t(const char* key, const QStringList& args = {}) const;
    QString selToken(const QComboBox* c, const std::vector<EntityId>& sel) const;
    std::vector<EntityId> resolve(const QComboBox* c, const std::vector<EntityId>& sel) const;
    void updateEnabled();
    void fillTable();

    const i18n::Localizer& loc_;
    const Document& doc_;
    std::function<std::vector<EntityId>()> selection_;
    std::vector<EntityId> ptsSel_, refSel_;
    QButtonGroup* kinds_ = nullptr;
    QComboBox *ptsSrc_ = nullptr, *refSrc_ = nullptr;
    QDoubleSpinBox *tol_ = nullptr, *maxD_ = nullptr, *clus_ = nullptr, *zDes_ = nullptr, *ratio_ = nullptr, *textH_ = nullptr, *arrow_ = nullptr;
    QCheckBox *useZ_ = nullptr, *geo_ = nullptr;
    QLabel *ptsInfo_ = nullptr, *sum_ = nullptr;
    QTableWidget* table_ = nullptr;
    civil::DevResult res_;
    bool ran_ = false;
};

} // namespace asiacad::gui
