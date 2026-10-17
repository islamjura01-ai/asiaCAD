#pragma once
// AsiaCAD GUI — menyuni Excel fayli bilan bog'lash: "Excelga" (saqlash + tizimdagi Excel'da ochish + kuzatish),
// "Exceldan" (fayl tanlab o'qish). Bog'langan fayl saqlansa (Excel/LibreOffice) — reloadRequested avtomatik chiqadi.
#include <asiacad/i18n/localizer.h>
#include <asiacad/io/xlsx.h>

#include <QObject>
#include <QString>

#include <functional>

class QFileSystemWatcher;
class QLabel;
class QPushButton;
class QTimer;
class QWidget;
class QHBoxLayout;

namespace asiacad::gui {

class ExcelLink : public QObject {
    Q_OBJECT
public:
    // build — joriy holatdan kitob; apply — o'qilgan kitobni menyuga qo'llash (xato bo'lsa Error tashlaydi)
    ExcelLink(const i18n::Localizer& loc, QWidget* owner, const QString& baseName, std::function<xlsx::Workbook()> build,
              std::function<void(const xlsx::Workbook&)> apply);

    void addButtons(QHBoxLayout* layout);       // "Excelga ↗" va "Exceldan ↙"
    void setStatusLabel(QLabel* l) { status_ = l; }
    // dasturiy (sinov va CLI'siz ishlatish uchun)
    bool exportTo(const QString& path, bool openInApp);
    bool importFrom(const QString& path);
    QString linkedPath() const { return path_; }
    static bool openExternally;                 // sinovlarda false

signals:
    void reloaded(const QString& path);

private:
    void watch(const QString& path);
    void onChanged();
    void report(const QString& text, bool error);

    const i18n::Localizer& loc_;
    QWidget* owner_;
    QString base_, path_;
    std::function<xlsx::Workbook()> build_;
    std::function<void(const xlsx::Workbook&)> apply_;
    QFileSystemWatcher* watcher_ = nullptr;
    QTimer* debounce_ = nullptr;
    QLabel* status_ = nullptr;
    qint64 selfWrite_ = 0;  // o'zimiz yozgan vaqt (o'z yozuvimizni qayta o'qimaslik uchun)
};

} // namespace asiacad::gui
