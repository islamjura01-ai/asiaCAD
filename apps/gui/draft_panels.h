#pragma once
// AsiaCAD GUI — chizmachilik panellari:
//  * SymbolPalette — genplan belgilari (rangli ikonalar, bo'limlar, masshtab va burilish); bosilsa — joylash asbobi
//  * ArrayDialog   — massiv: to'g'ri burchakli / qutbiy / yo'l bo'ylab (ARRAY buyrug'ini tuzadi)
#include <asiacad/core/block.h>
#include <asiacad/i18n/localizer.h>

#include <QDialog>
#include <QPixmap>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QListWidget;
class QSpinBox;
class QTabWidget;

namespace asiacad::gui {

QPixmap symbolPixmap(const BlockDef& b, int size, bool dark);

class SymbolPalette : public QWidget {
    Q_OBJECT
public:
    SymbolPalette(const i18n::Localizer& loc, bool dark, QWidget* parent = nullptr);
    void retranslate();
    QListWidget* list() const { return list_; }
    bool choose(const QString& key);   // sinov uchun

signals:
    void symbolChosen(const QString& key, double scale, double rotDeg);

private:
    void fill();
    const i18n::Localizer& loc_;
    bool dark_;
    QListWidget* list_ = nullptr;
    QDoubleSpinBox *scale_ = nullptr, *rot_ = nullptr;
};

class ArrayDialog : public QDialog {
    Q_OBJECT
public:
    ArrayDialog(const i18n::Localizer& loc, const QString& selArg, const Vec3& selCenter, EntityId pathId, QWidget* parent = nullptr);
    QString command() const;
    QTabWidget* tabs() const { return tabs_; }

private:
    QString sel_;
    QTabWidget* tabs_ = nullptr;
    QSpinBox *rows_ = nullptr, *cols_ = nullptr, *pn_ = nullptr, *pathN_ = nullptr, *pathId_ = nullptr;
    QDoubleSpinBox *dx_ = nullptr, *dy_ = nullptr, *cx_ = nullptr, *cy_ = nullptr, *fill_ = nullptr, *spacing_ = nullptr;
    QCheckBox *rotate_ = nullptr, *align_ = nullptr;
};

} // namespace asiacad::gui
