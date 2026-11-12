#pragma once
// AsiaCAD GUI — bino elementi (ustun, devor, plita, fundament, zina …) xossalari: "Monolit bino" va "Armatura 3D"
// menyularida umumiy. Element chizmadagi manba obyekti (yopiq poliliniya) bilan bog'lanadi; o'lcham, joy, tur (qatlam),
// nusxa, teshik, o'chirish — chizmaga buyruqlar (SETPROP, MOVE, COPY, CHPROP, RECTANG, ERASE) orqali, undo bilan.
// Manbasi topilmagan element (avtomatik — masalan fundament konturdan, yoki boshqa varaqdan) — faqat ma'lumot.
#include "prop_panel.h"

#include <asiacad/bim/building.h>
#include <asiacad/core/document.h>
#include <asiacad/i18n/localizer.h>

#include <QStringList>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace asiacad::gui {

// Chizmadagi bino elementlari: yopiq poliliniyalar (bino qatlamlarida) — kontur imzosi va "ichida" bo'yicha qidiruv
class BimSources {
public:
    void build(const Document& doc, const std::map<std::string, bim::ElemKind>& layerMap);
    EntityId find(const bim::Prism& pr) const;   // 0 — topilmadi
    std::size_t size() const { return rings_.size(); }

private:
    struct Ring {
        EntityId id = 0;
        bim::ElemKind kind = bim::ElemKind::Ignore;
        std::vector<Vec3> pts;
        double area = 0;
    };
    std::map<std::string, EntityId> sig_;
    std::vector<Ring> rings_;
};

// Bino elementi kaliti: manbasi bo'lsa "e:<id>:<qavat>:<tur>", bo'lmasa "p:<tur>:<qavat>:<tartib>"
QString bimPrismKey(const bim::Prism& pr, EntityId src, int ordinal);

// Panelni element bilan to'ldirish (end() — chaqiruvchida: menyu o'z bo'limlarini qo'shadi). run — buyruqlar (MainWindow
// bajaradi, undo bilan; keyin menyu yangilanadi)
void fillBimElement(PropPanel& panel, const i18n::Localizer& loc, const Document& doc, const std::map<std::string, bim::ElemKind>& layerMap,
                    const bim::Prism& pr, EntityId src, const std::function<void(const QStringList&)>& run);

// Ikki nuqta / son — buyruq satri uchun (nuqta — o'nlik ajratgich, vergul — koordinatalar)
QString cmdNum(double v);
QString cmdPt(double x, double y);

}  // namespace asiacad::gui
