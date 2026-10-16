#pragma once
// AsiaCAD GUI — har bir katta menyuning yon tomonidagi shablonlar paneli.
// Bo'limlar (sarlavha) + kartalar (nomi, izoh, rang belgisi), qidiruv, ikki marta bosish yoki "Qo'llash" — templateActivated.
#include <QColor>
#include <QVariant>
#include <QWidget>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QLabel;

namespace asiacad::gui {

class TemplateSidebar : public QWidget {
    Q_OBJECT
public:
    TemplateSidebar(const QString& title, const QString& searchHint, const QString& applyText, const QString& hint,
                    QWidget* parent = nullptr);
    void clear();
    void addSection(const QString& title);
    void addTemplate(const QString& name, const QString& subtitle, const QVariant& data, const QColor& accent, bool user = false);
    int count() const;                          // shablonlar (bo'limlarsiz)
    bool activate(int index);                   // sinov uchun: index-shablonni qo'llash
    QVariant currentData() const;

signals:
    void templateActivated(const QVariant& data);

private:
    void filter(const QString& text);
    QListWidget* list_ = nullptr;
    QLineEdit* search_ = nullptr;
};

} // namespace asiacad::gui
