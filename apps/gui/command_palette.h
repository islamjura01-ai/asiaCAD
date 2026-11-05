#pragma once
// Buyruq qidirish (Ctrl+K): lenta amallari (ikonka, joyi "Varaq › Guruh", tezkor tugma) va CLI buyruqlari
#include <asiacad/i18n/localizer.h>

#include <QDialog>
#include <QIcon>
#include <QList>

#include <functional>

class QLineEdit;
class QListWidget;
class QLabel;

namespace asiacad::gui {

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    struct Entry {
        QString id;        // "act:line" — lenta amali
        QString label;     // ko'rinadigan nom
        QString path;      // "Chizish › Chizish"
        QString shortcut;  // "Ctrl+S"
        QString keywords;  // qo'shimcha qidiruv so'zlari (buyruq nomi, aliaslar)
        QIcon icon;
    };
    CommandPalette(const i18n::Localizer& loc, QWidget* parent);
    void open(QWidget* anchor);
    void setEntries(std::function<QList<Entry>()> provider) { provider_ = std::move(provider); }
    int resultCount() const;          // sinov uchun
    void setFilter(const QString& f); // sinov uchun

signals:
    void commandChosen(const QString& canonical);   // CLI kanonik nomi yoki "act:<kalit>"

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void refill(const QString& filter);
    void accept() override;

    const i18n::Localizer& loc_;
    QLineEdit* edit_;
    QListWidget* list_;
    QLabel* footer_;
    std::function<QList<Entry>()> provider_;
    QList<Entry> entries_;
};

} // namespace asiacad::gui
