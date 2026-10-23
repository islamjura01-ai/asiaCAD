#pragma once
// AsiaCAD GUI — tuzatish paneli (Civil 3D "Properties" / kontekst paneli kabi): tanlangan obyekt turiga qarab
//  * kanalizatsiya quvuri — material/Ø, nishablik, lotok boshi/oxiri, quduqqa kirishi (sm), sarf; uzunliklar,
//    chuqurlik, ko'mish, sig'im, tezlik, normativ tekshiruv;
//  * quduq — nomi, X/Y, yer belgisi, chiqish lotogi, diametr; kirish/chiqish lotogi, perepad, chuqurlik;
//  * dinamik bog'lanish natijasi — qaysi buyruq, yangilash;
//  * har qanday obyekt — geometriya (X1…Z2, uzunlik, burchak, radius, matn, balandlik …).
// Har bir o'zgarish buyruq qatori (PIPEEDIT / SETPROP) orqali — undo bilan.
#include <asiacad/core/document.h>
#include <asiacad/i18n/localizer.h>

#include <QWidget>

#include <functional>

class QFormLayout;
class QLineEdit;
class QVBoxLayout;

namespace asiacad::gui {

class SmartPanel : public QWidget {
    Q_OBJECT
public:
    SmartPanel(const Document& doc, const i18n::Localizer& loc, QWidget* parent = nullptr);
    void rebuild(const std::vector<EntityId>& sel);
    QLineEdit* field(const QString& key) const;   // "f_<KEY>"
    QString title() const { return title_; }

signals:
    // line — bajariladigan buyruq; reselect — bajarilgandan keyin tanlanadigan obyekt belgisi ("sewer:K1:seg:2")
    void edit(const QString& line, const QString& reselect);
    void select(const QString& tag);              // "sewer:K1:mh:3" — shu obyektni tanlash

private:
    void addSection(const QString& title);
    QLineEdit* addField(const QString& key, const QString& label, const QString& value, bool editable,
                        std::function<void(const QString&)> onEdit);
    void addInfo(const QString& label, const QString& value, const QColor& color = QColor());
    bool buildSewer(EntityId id);
    void buildLink(EntityId id);
    void buildGeneric(const std::vector<EntityId>& sel);
    QString t(const char* key, const QStringList& args = {}) const;

    const Document& doc_;
    const i18n::Localizer& loc_;
    QWidget* body_ = nullptr;
    QVBoxLayout* lay_ = nullptr;
    QFormLayout* form_ = nullptr;
    QString title_;
};

} // namespace asiacad::gui
