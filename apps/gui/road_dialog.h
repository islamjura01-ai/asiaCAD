#pragma once
// AsiaCAD GUI — "Yo'l 3D": plan chizmasidan (o'q + yer + loyiha belgilari) 3D koridor.
//  chap — shablonlar (texnik toifalar, yo'l to'shamasi), qatlam vazifalari, ko'ndalang profil parametrlari, qatlamlar
//  o'rta — 3D koridor (vertikal masshtab) va bo'ylama profil
//  o'ng — Piketlar (loyiha belgisi qo'lda tuzatiladi), Hajmlar, Tekshiruv
//  past — Excelga/Exceldan, "3D va profilni chizmaga" (ROAD fayl.json + x,y)
#include "excel_link.h"
#include "pipe3d_view.h"
#include "template_sidebar.h"

#include <asiacad/civil/road.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace asiacad::gui {

class RoadProfileView : public QWidget {
public:
    explicit RoadProfileView(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(200); }
    void setModel(const civil::RoadModel* m, const QString& groundLbl, const QString& designLbl) {
        m_ = m;
        g_ = groundLbl;
        d_ = designLbl;
        update();
    }
    void setDark(bool d) { dark_ = d; update(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    const civil::RoadModel* m_ = nullptr;
    QString g_, d_;
    bool dark_ = true;
};

class RoadDialog : public QDialog {
    Q_OBJECT
public:
    RoadDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent = nullptr);

    void reread();
    const civil::RoadModel& model() const { return m_; }
    const civil::RoadParams& params() const { return p_; }
    TemplateSidebar* sidebar() const { return side_; }
    ExcelLink* excel() const { return excel_; }
    Pipe3DView* view() const { return view_; }
    void setDesign(int station, double z);      // qo'lda tuzatish
    void applyCategory(const QString& id);
    void showTab(int i);

signals:
    void scratchRequested();                    // "0 dan" — generator oynasi (MainWindow ochadi)
    void locateRequested(double x, double y);   // tuzatish: tekshiruvdagi joyni chizmada ko'rsatish
    void drawRequested(const QString& jsonPath);

private:
    QString t(const char* key, const QStringList& args = {}) const;
    void buildUi();
    void fillLayers();
    std::map<std::string, civil::RoadLayer> layerMap() const;
    void paramsToUi();
    void readParams();
    void fillPavement();
    void modelChanged();
    void fillTables();
    void render();

    const i18n::Localizer& loc_;
    const Document& doc_;
    civil::RoadParams p_;
    civil::RoadModel m_;
    bool updating_ = false;

    TemplateSidebar* side_ = nullptr;
    QTableWidget* layers_ = nullptr;
    QSpinBox* lanes_ = nullptr;
    QDoubleSpinBox *laneW_ = nullptr, *shoulder_ = nullptr, *strength_ = nullptr, *crossfall_ = nullptr, *shfall_ = nullptr, *slopeF_ = nullptr,
                   *slopeC_ = nullptr, *step_ = nullptr, *raise_ = nullptr, *ground_ = nullptr, *maxGrade_ = nullptr, *minR_ = nullptr,
                   *vexag_ = nullptr;
    QLabel* catLbl_ = nullptr;
    QTableWidget* pav_ = nullptr;
    QTabWidget* midTabs_ = nullptr;
    Pipe3DView* view_ = nullptr;
    RoadProfileView* profile_ = nullptr;
    QTableWidget* st_ = nullptr;
    QListWidget* quant_ = nullptr;
    QListWidget* checks_ = nullptr;
    QLabel* summary_ = nullptr;
    ExcelLink* excel_ = nullptr;
};

} // namespace asiacad::gui
