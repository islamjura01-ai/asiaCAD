#pragma once
// AsiaCAD GUI — ShNQ qoidalari jadvali: yoqish/o'chirish, chegaralar (ifoda), norma va band, daraja, "tasdiqlangan" belgisi.
// Saqlash — foydalanuvchi fayli (AppData/shnq_rules.json); standart qoidalarga qaytarish, JSON import/eksport.
#include <asiacad/bim/norms.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>

#include <vector>

class QLabel;
class QPushButton;
class QTableWidget;

namespace asiacad::gui {

class NormRulesDialog : public QDialog {
    Q_OBJECT
public:
    NormRulesDialog(const i18n::Localizer& loc, std::vector<bim::NormRule> rules, QWidget* parent = nullptr);
    std::vector<bim::NormRule> rules() const { return rules_; }
    QTableWidget* table() const { return table_; }
    // jadvaldan o'qib tekshirish; xato bo'lsa — matn (bo'sh — hammasi to'g'ri)
    QString collect();

private:
    QString t(const char* key, const QStringList& args = {}) const;
    void fill();
    void addRow(const bim::NormRule& r);

    const i18n::Localizer& loc_;
    std::vector<bim::NormRule> rules_;
    QTableWidget* table_ = nullptr;
    QLabel* help_ = nullptr;
};

} // namespace asiacad::gui
