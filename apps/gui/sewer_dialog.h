#pragma once
// AsiaCAD GUI — "Kanalizatsiya" menyusi: bitta oynada shablon, material/o'lcham tanlovi, quduqlar va
// quvur bo'laklari jadvali, jonli gidravlik hisob va norma tekshiruvi, hajmlar (PTO) va 3D ko'rinish.
// "Chizmaga chizish" tarmoqni .kan.json ga yozib, SEWER buyrug'ini Interpreter orqali bajaradi (undo bitta qadam).
#include "pipe3d_view.h"
#include "template_sidebar.h"

#include <asiacad/civil/sewer.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTabWidget;
class QTableWidget;

namespace asiacad::gui {

class SewerDialog : public QDialog {
    Q_OBJECT
public:
    // pickPolyline: chizmada tanlangan poliliniya uchlari (x, y, z=yer belgisi), bo'lmasa bo'sh
    SewerDialog(const i18n::Localizer& loc, std::function<std::vector<Vec3>()> pickPolyline, bool dark,
                QWidget* parent = nullptr);

    civil::SewerNetwork network() const;
    void setNetwork(const civil::SewerNetwork& n);
    const civil::SewerResult& result() const { return res_; }
    bool withProfile() const;
    QString templatesPath() const;
    TemplateSidebar* sidebar() const { return side_; }

    // sinov va skrinshot uchun
    void selectTemplate(int index);
    void showTab(int index);
    QTableWidget* segmentsTable() const { return segs_; }
    Pipe3DView* pipeView() const { return pipe3d_; }

signals:
    void drawRequested(const QString& jsonPath, bool profile);

private:
    QString t(const char* key, const QStringList& args = {}) const;
    void buildUi();
    void fillMaterials();
    void fillSizes();
    void fillTemplates();
    void onTemplate(int i);
    void onSpecChanged();
    void applySpecToAll();
    void addManhole();
    void removeManhole();
    void fromPolyline();
    void openFile();
    void saveFile();
    void saveTemplate();
    void draw();
    void syncSegmentsRows();
    void recompute();
    const civil::PipeSpec* currentSpec() const;
    QComboBox* specCombo(int row) const;

    const i18n::Localizer& loc_;
    std::function<std::vector<Vec3>()> pick_;
    std::vector<civil::PipeTemplate> templates_;
    civil::SewerResult res_;
    bool updating_ = false;

    QComboBox* tpl_ = nullptr;
    TemplateSidebar* side_ = nullptr;
    QLineEdit* name_ = nullptr;
    QComboBox* system_ = nullptr;
    QCheckBox* street_ = nullptr;
    QCheckBox* pressure_ = nullptr;
    QComboBox* material_ = nullptr;
    QComboBox* size_ = nullptr;
    QLabel* specInfo_ = nullptr;
    QDoubleSpinBox* slope_ = nullptr;
    QDoubleSpinBox* depth_ = nullptr;
    QDoubleSpinBox* frost_ = nullptr;
    QDoubleSpinBox* textH_ = nullptr;
    QTableWidget* mh_ = nullptr;
    QTableWidget* segs_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    Pipe3DView* pipe3d_ = nullptr;
    Pipe3DView* net3d_ = nullptr;
    QListWidget* checks_ = nullptr;
    QPlainTextEdit* qty_ = nullptr;
    QLabel* status_ = nullptr;
    QCheckBox* profile_ = nullptr;
    QGroupBox* storm_ = nullptr;
    QDoubleSpinBox *q20_ = nullptr, *nPar_ = nullptr, *mr_ = nullptr, *gamma_ = nullptr, *period_ = nullptr, *tcon_ = nullptr,
                   *tcan_ = nullptr;
    QDoubleSpinBox* share_[7] = {};
};

} // namespace asiacad::gui
