#pragma once
// AsiaCAD GUI — IFC modellar oynasi: yuklangan modellar (ko'rinish), qavatlar filtri, tarkib (PTO) jadvali, Excelga eksport.
// Modellar MainWindow da saqlanadi (3D sahnaga rebuild3D qo'shadi); oyna faqat ko'rsatadi va boshqaradi.
#include <asiacad/bim/ifc.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

#include <memory>
#include <set>
#include <vector>

class QLabel;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace asiacad::gui {

struct IfcLoaded {
    QString path;
    std::shared_ptr<bim::IfcModel> model;
    bool visible = true;
    std::set<int> hiddenStoreys;   // -1 — qavatsiz elementlar
};

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

signals:
    void addRequested();
    void visibilityChanged();
    void removeRequested(int index);
    void show3DRequested();

private:
    QString t(const char* key, const QStringList& args = {}) const;
    void fillDetails();
    void onItemChanged(QTreeWidgetItem* it, int col);
    QString storeyLabel(const bim::IfcModel& m, int storey) const;

    const i18n::Localizer& loc_;
    std::vector<IfcLoaded>& models_;
    QTreeWidget* tree_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* info_ = nullptr;
    QLabel* warn_ = nullptr;
    QLabel* sumTitle_ = nullptr;
    QTableWidget* sum_ = nullptr;
    QPushButton *add_ = nullptr, *remove_ = nullptr, *export_ = nullptr, *show3d_ = nullptr, *close_ = nullptr;
    bool filling_ = false;
};

// Element nomi 3D va jadvallar uchun: nomi bo'lmasa — tur nomi
QString ifcElementName(const i18n::Localizer& loc, const bim::IfcElement& e);

} // namespace asiacad::gui
