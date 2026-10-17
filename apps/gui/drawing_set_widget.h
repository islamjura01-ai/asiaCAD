#pragma once
// AsiaCAD GUI — chizmalar to'plami paneli: bir nechta DXF/.acad fayl, har biriga vazifa (rol), qavat oralig'i, masshtab.
// Yer osti tarmoqlari (har tarmoq alohida fayl), bino (qavat qolip chizmalari, fundament plani), armatura (KJ chizmalar).
#include <asiacad/civil/drawset.h>
#include <asiacad/i18n/localizer.h>

#include <QWidget>

#include <map>
#include <utility>
#include <vector>

class QTableWidget;
class QLabel;

namespace asiacad::gui {

class DrawingSetWidget : public QWidget {
    Q_OBJECT
public:
    DrawingSetWidget(const i18n::Localizer& loc, const QString& title, std::vector<std::pair<QString, QString>> roles, bool floors,
                     QWidget* parent = nullptr);
    std::vector<civil::SheetFile> sheets() const;
    void setSheets(const std::vector<civil::SheetFile>& s);
    void addFile(const QString& path, const QString& role, int from = 1, int to = 1);
    // yoqilgan fayllar, yuklangan (kesh: yo'l + masshtab), rol va qavatlar joriy jadvaldan
    std::vector<civil::LoadedSheet> loaded();
    int count() const;

signals:
    void changed();

private:
    void addRow(const civil::SheetFile& f);
    void refreshStatus();
    const i18n::Localizer& loc_;
    std::vector<std::pair<QString, QString>> roles_;
    bool floors_;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
    std::map<std::string, civil::LoadedSheet> cache_;
    bool updating_ = false;
};

} // namespace asiacad::gui
