#pragma once
// AsiaCAD GUI — uch interfeys rejimi va ularning mavzulari (dizayn: Samarqand, Ipak yo'li, Tungi Toshkent)
#include <QColor>
#include <QString>

namespace asiacad::gui {

enum class UiMode { Classic, Module, Focus };

struct Theme {
    UiMode mode = UiMode::Classic;
    QColor canvasBg, gridMinor, gridMajor, axisX, axisY;
    QColor selection, snapMarker, crosshair, rubber, windowSel, crossingSel;
    QColor accent, accentText;
    bool lightCanvas = false;  // ACI 7 yorug' fonda qora chiziladi
    QString styleSheet;
};

Theme themeFor(UiMode mode);
QString modeKey(UiMode mode);          // "classic" | "module" | "focus"
UiMode modeFromKey(const QString& k);  // noma'lum — Classic

// AutoCAD Color Index -> RGB (7 — fon rangiga qarab oq/qora)
QColor aciColor(int index, bool lightCanvas);

} // namespace asiacad::gui
