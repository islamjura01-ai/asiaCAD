#pragma once
// Ctrl+K — buyruq qidirish (Fokus rejimining asosiy kirish nuqtasi, boshqa rejimlarda ham ishlaydi).
// Qidiruv istalgan tilda: kanonik nom, faol tildagi nom va tavsif bo'yicha.
#include <asiacad/i18n/localizer.h>

#include <QDialog>

class QLineEdit;
class QListWidget;

namespace asiacad::gui {

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    CommandPalette(const i18n::Localizer& loc, QWidget* parent);
    void open(QWidget* anchor);

signals:
    void commandChosen(const QString& canonical);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void refill(const QString& filter);
    void accept() override;

    const i18n::Localizer& loc_;
    QLineEdit* edit_;
    QListWidget* list_;
};

} // namespace asiacad::gui
