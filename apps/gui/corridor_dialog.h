#pragma once
// AsiaCAD GUI — "Koridor": Civil 3D Corridor kabi, lekin bitta oynada va soddaroq.
//  chap — palitra (vertikal yorliqlar): Yig'malar | Asosiy | Polosalar | Yo'l yoqasi | Ajratuvchi | Bordyur | Qiyalik |
//          Kanal/lotok | Transheya | Devor — bosilsa yig'maga qo'shiladi (tanlangan tomonga)
//  ikkinchi ustun — yig'ma muharriri: chap/o'ng qismlar ro'yxati (↑ ↓ ✕), tanlangan qism parametrlari, jonli oldindan ko'rish
//          (kesma/to'kma holatida), koridor parametrlari
//  o'rta — 3D | Kesimlar (piket bo'yicha, material ranglari, kesma/to'kma) | Profil
//  o'ng — Hajmlar (o'rtacha maydon va aniq: prizmoidal + egrilik), Piketlar (loyiha belgisi tahrirlanadi), Tekshiruv, Qatlamlar
#include "excel_link.h"
#include "model3d_view.h"
#include "template_sidebar.h"

#include <asiacad/civil/corridor.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QSlider;
class QTableWidget;
class QTabWidget;

namespace asiacad::gui {

// Bitta kesim chizmasi (yig'ma oldindan ko'rishi va piket kesimlari uchun)
class SectionView : public QWidget {
public:
    explicit SectionView(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(160); }
    void setSection(const civil::Section* s, const QString& title) {
        s_ = s;
        title_ = title;
        update();
    }
    void setDark(bool d) { dark_ = d; update(); }
    void setMaterialNames(std::map<std::string, QString> n) { names_ = std::move(n); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    const civil::Section* s_ = nullptr;
    QString title_;
    bool dark_ = true;
    std::map<std::string, QString> names_;
};

QColor materialColor(const std::string& m);

class CorridorDialog : public QDialog {
    Q_OBJECT
public:
    CorridorDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent = nullptr);

    void reread();
    const civil::Corridor& model() const { return c_; }
    const civil::CorridorParams& params() const { return p_; }
    const civil::Assembly& assembly() const { return p_.assemblies.front(); }
    void applyAssembly(const civil::Assembly& a);
    void addSub(civil::SubKind k, int side);       // side: 1 — o'ng, -1 — chap, 0 — ikkala (simmetrik)
    void removeSub(int side, int index);
    void setParam(int side, int index, const std::string& key, double v);
    void setDesign(int station, double z);
    void showStation(int i);
    void showTab(int i);
    QTabWidget* palette() const { return palette_; }
    ExcelLink* excel() const { return excel_; }
    Model3DView* view() const { return view_; }

signals:
    void scratchRequested();                    // "0 dan" — generator oynasi (MainWindow ochadi)
    void locateRequested(double x, double y);   // tuzatish: tekshiruvdagi joyni chizmada ko'rsatish
    void drawRequested(const QString& jsonPath);

private:
    QString t(const char* key, const QStringList& args = {}) const;
    QString subName(civil::SubKind k) const;
    void buildUi();
    void fillPalette();
    void fillLayers();
    void fillAssembly();
    void fillParams();
    void updatePreview();
    void modelChanged();       // yig'ma o'zgardi -> koridor qayta
    void fillTables();
    void render();
    std::map<std::string, civil::WwLayer> layerMap() const;

    const i18n::Localizer& loc_;
    const Document& doc_;
    civil::CorridorParams p_;
    civil::Corridor c_;
    std::vector<civil::Assembly> asms_;
    civil::Section preview_;
    bool updating_ = false;
    int selSide_ = 1, selIndex_ = -1;

    QTabWidget* palette_ = nullptr;
    QComboBox* addSide_ = nullptr;
    QLabel* asmName_ = nullptr;
    QListWidget *leftList_ = nullptr, *rightList_ = nullptr;
    QCheckBox* symmetric_ = nullptr;
    QTableWidget* params_ = nullptr;
    QDoubleSpinBox *q_ = nullptr, *n_ = nullptr, *step_ = nullptr, *maxDl_ = nullptr, *ground_ = nullptr, *vexag_ = nullptr;
    QComboBox* previewCase_ = nullptr;
    SectionView* preview_view_ = nullptr;
    QTabWidget* midTabs_ = nullptr;
    Model3DView* view_ = nullptr;
    SectionView* secView_ = nullptr;
    QSlider* secSlider_ = nullptr;
    QLabel* secLbl_ = nullptr;
    QWidget* profile_ = nullptr;
    QTableWidget* vol_ = nullptr;
    QTableWidget* st_ = nullptr;
    QListWidget* checks_ = nullptr;
    QTableWidget* layers_ = nullptr;
    QLabel* summary_ = nullptr;
    ExcelLink* excel_ = nullptr;
};

} // namespace asiacad::gui
