#pragma once
// AsiaCAD GUI — "Suv yo'li": bosimli/o'zi oqar quvur, beton kanal, tuproq ariq, lotok — plan chizmasidan 3D (absolyut belgilar).
//  chap — kesim shablonlari (tur bo'yicha), qatlam vazifalari, kesim va gidravlika parametrlari
//  o'rta — 3D (vertikal masshtab) va bo'ylama profil (yer, tub, qirg'oq, suv sathi / pyezometrik chiziq)
//  o'ng — Piketlar (tub belgisi qo'lda tuzatiladi), Hajmlar, Tekshiruv; past — Excel (bog'langan), "3D va profilni chizmaga"
#include "excel_link.h"
#include "pipe3d_view.h"
#include "template_sidebar.h"

#include <asiacad/civil/waterway.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QTableWidget;
class QTabWidget;

namespace asiacad::gui {

class WwProfileView : public QWidget {
public:
    explicit WwProfileView(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(200); }
    void setModel(const civil::WwModel* m, const civil::WwParams* p, const QStringList& labels) {
        m_ = m;
        p_ = p;
        lbl_ = labels;
        update();
    }
    void setDark(bool d) { dark_ = d; update(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    const civil::WwModel* m_ = nullptr;
    const civil::WwParams* p_ = nullptr;
    QStringList lbl_;
    bool dark_ = true;
};

class WaterwayDialog : public QDialog {
    Q_OBJECT
public:
    WaterwayDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent = nullptr);

    void reread();
    const civil::WwModel& model() const { return m_; }
    const civil::WwParams& params() const { return p_; }
    TemplateSidebar* sidebar() const { return side_; }
    ExcelLink* excel() const { return excel_; }
    Pipe3DView* view() const { return view_; }
    void setBottom(int station, double z);      // qo'lda tuzatish
    void applySection(const civil::WwSection& s);
    void setFlow(double q);
    void showTab(int i);

signals:
    void drawRequested(const QString& jsonPath);

private:
    QString t(const char* key, const QStringList& args = {}) const;
    void buildUi();
    void fillLayers();
    std::map<std::string, civil::WwLayer> layerMap() const;
    void paramsToUi();
    void readParams();
    void modelChanged();
    void fillTables();
    void render();

    const i18n::Localizer& loc_;
    const Document& doc_;
    civil::WwParams p_;
    civil::WwModel m_;
    std::vector<civil::WwSection> secs_;
    bool updating_ = false;

    TemplateSidebar* side_ = nullptr;
    QTableWidget* layers_ = nullptr;
    QComboBox* type_ = nullptr;
    QLabel* secName_ = nullptr;
    QDoubleSpinBox *b_ = nullptr, *h_ = nullptr, *m_s = nullptr, *B_ = nullptr, *d_ = nullptr, *t_ = nullptr, *n_ = nullptr, *fb_ = nullptr,
                   *vmin_ = nullptr, *vmax_ = nullptr, *piece_ = nullptr, *chw_ = nullptr, *q_ = nullptr, *step_ = nullptr, *depth_ = nullptr,
                   *slope_ = nullptr, *head_ = nullptr, *ground_ = nullptr, *vexag_ = nullptr;
    QTabWidget* midTabs_ = nullptr;
    Pipe3DView* view_ = nullptr;
    WwProfileView* profile_ = nullptr;
    QTableWidget* st_ = nullptr;
    QListWidget* quant_ = nullptr;
    QListWidget* checks_ = nullptr;
    QLabel* summary_ = nullptr;
    ExcelLink* excel_ = nullptr;
};

} // namespace asiacad::gui
