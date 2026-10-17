#pragma once
// AsiaCAD GUI — "Yer osti tarmoqlari 3D": plan chizmasini o'qib 3D model, xatolar ro'yxati va qo'lda tuzatish.
//  chap — tarmoq shablonlari (tanlangan chiziqlarga tur/Ø/chuqurlik), qatlam → tur jadvali, parametrlar
//  o'rta — 3D (tur filtri, vertikal masshtab, yer sathi proyeksiyasi, quduqlar, xato kesishmalar qizil)
//  o'ng — Tarmoqlar (tur, Ø tahrirlanadi), Nuqtalar (Z tahrirlanadi), Kesishmalar, Tekshiruv (bosilsa — chiziq tanlanadi)
//  past — Excelga/Exceldan (bog'langan), "3D ni chizmaga" (UTIL fayl.json)
#include "drawing_set_widget.h"
#include "excel_link.h"
#include "pipe3d_view.h"
#include "template_sidebar.h"

#include <asiacad/civil/utility.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QTableWidget;

namespace asiacad::gui {

class UtilityDialog : public QDialog {
    Q_OBJECT
public:
    UtilityDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent = nullptr);

    void reread();
    const civil::UtilModel& model() const { return m_; }
    civil::UtilModel& modelRef() { return m_; }
    const civil::UtilParams& params() const { return p_; }
    TemplateSidebar* sidebar() const { return side_; }
    ExcelLink* excel() const { return excel_; }
    Pipe3DView* view() const { return view_; }
    DrawingSetWidget* drawings() const { return set_; }
    QTableWidget* linesTable() const { return lines_; }
    void selectLine(int index);
    void setZ(int line, int vertex, double z);      // qo'lda tuzatish (sinov uchun ham)
    void modelChanged();                             // tekshiruv + jadvallar + 3D

signals:
    void scratchRequested();                    // "0 dan" — generator oynasi (MainWindow ochadi)
    void locateRequested(double x, double y);   // tuzatish: tekshiruvdagi joyni chizmada ko'rsatish
    void drawRequested(const QString& jsonPath);

private:
    QString t(const char* key, const QStringList& args = {}) const;
    void buildUi();
    void fillLayers();
    std::map<std::string, civil::UtilKind> layerMap() const;
    void readParams();
    void fillTables();
    void fillPoints();
    void render();
    void applyTemplate(const QVariant& data);

    const i18n::Localizer& loc_;
    const Document& doc_;
    civil::UtilParams p_;
    civil::UtilModel m_;
    bool updating_ = false;
    int selected_ = -1;

    TemplateSidebar* side_ = nullptr;
    DrawingSetWidget* set_ = nullptr;
    QCheckBox* useDoc_ = nullptr;
    QTableWidget* layers_ = nullptr;
    QDoubleSpinBox *ground_ = nullptr, *radius_ = nullptr, *gap_ = nullptr, *clPipe_ = nullptr, *clCable_ = nullptr, *vexag_ = nullptr;
    QCheckBox* show_[9] = {};
    Pipe3DView* view_ = nullptr;
    QTableWidget* lines_ = nullptr;
    QTableWidget* points_ = nullptr;
    QTableWidget* cross_ = nullptr;
    QListWidget* checks_ = nullptr;
    QLabel* summary_ = nullptr;
    ExcelLink* excel_ = nullptr;
};

QColor utilColor(civil::UtilKind k);

} // namespace asiacad::gui
