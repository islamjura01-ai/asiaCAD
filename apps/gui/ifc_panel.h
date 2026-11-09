#pragma once
// AsiaCAD GUI — IFC modellar oynasi: yuklangan modellar (ko'rinish), qavatlar filtri, tarkib (PTO) jadvali, Excelga eksport.
// Modellar MainWindow da saqlanadi (3D sahnaga rebuild3D qo'shadi); oyna faqat ko'rsatadi va boshqaradi.
#include <asiacad/bim/ifc.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

#include <memory>
#include <set>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTabWidget;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace asiacad::gui {

struct IfcLoaded {
    QString path;
    std::shared_ptr<bim::IfcModel> model;
    bool visible = true;
    bool georef = true;            // IfcMapConversion qo'llanadimi (xarita koordinatalari)
    std::set<int> hiddenStoreys;   // -1 — qavatsiz elementlar
};

// IFC -> 2D chizma (reja kesimi, o'qlar, razbivka nuqtalari)
struct IfcPlanOptions {
    int storey = -1;               // -1 — mutlaq balandlik (z)
    double z = 1.0;                // kesim balandligi (mutlaq), m
    bool cut = true, axes = true, points = true, labels = true;
};

// Razbivka uchun elementlar: ustun, qoziq, fundament
bool ifcIsStakeoutElement(const bim::IfcElement& e);

class IfcPanel : public QDialog {
    Q_OBJECT
public:
    IfcPanel(const i18n::Localizer& loc, std::vector<IfcLoaded>& models, QWidget* parent = nullptr);
    void refresh();          // modellar ro'yxati o'zgardi
    void retranslate();
    int current() const;     // tanlangan model (-1 — yo'q)
    void setCurrent(int i);
    // sinov uchun
    QTableWidget* summaryTable() const { return sum_; }
    QTreeWidget* tree() const { return tree_; }
    bool exportXlsx(const QString& path) const;
    bool exportStakeout(const QString& path) const;    // razbivka jadvali (Excel)
    QTableWidget* elementTable() const { return elems_; }
    QLineEdit* searchEdit() const { return search_; }
    QTabWidget* tabs() const { return tabs_; }
    void highlightGuid(const QString& guid);           // 3D da tanlangan element — ro'yxatda
    IfcPlanOptions planOptions() const;

signals:
    void addRequested();
    void visibilityChanged();
    void removeRequested(int index);
    void show3DRequested();
    void elementActivated(int model, const QString& guid);   // ro'yxatdan — 3D da topish
    void planRequested(int model, const asiacad::gui::IfcPlanOptions& opt);
    void reloadRequested(int model, bool georef);

private:
    QString t(const char* key, const QStringList& args = {}) const;
    void fillDetails();
    void onItemChanged(QTreeWidgetItem* it, int col);
    QString storeyLabel(const bim::IfcModel& m, int storey) const;
    void fillElements();
    void fillPlan();
    void updatePlanZ();

    const i18n::Localizer& loc_;
    std::vector<IfcLoaded>& models_;
    QTreeWidget* tree_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* info_ = nullptr;
    QLabel* warn_ = nullptr;
    QLabel* sumTitle_ = nullptr;
    QTableWidget* sum_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QCheckBox* georef_ = nullptr;
    // Elementlar
    QLineEdit* search_ = nullptr;
    QComboBox* catFilter_ = nullptr;
    QTableWidget* elems_ = nullptr;
    QLabel* elemCount_ = nullptr;
    // Reja va razbivka
    QComboBox* planStorey_ = nullptr;
    QDoubleSpinBox* cutH_ = nullptr;
    QLabel* planZ_ = nullptr;
    QLabel* planHint_ = nullptr;
    QCheckBox *chkCut_ = nullptr, *chkAxes_ = nullptr, *chkPoints_ = nullptr, *chkLabels_ = nullptr;
    QPushButton *planBtn_ = nullptr, *stakeBtn_ = nullptr;
    QLabel *lblStorey_ = nullptr, *lblCut_ = nullptr;
    QPushButton *add_ = nullptr, *remove_ = nullptr, *export_ = nullptr, *show3d_ = nullptr, *close_ = nullptr;
    bool filling_ = false;
};

// Element nomi 3D va jadvallar uchun: nomi bo'lmasa — tur nomi
QString ifcElementName(const i18n::Localizer& loc, const bim::IfcElement& e);

} // namespace asiacad::gui
