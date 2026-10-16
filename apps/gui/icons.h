#pragma once
// Vektor ikonkalar QPainter bilan chiziladi — resurs fayllarisiz, istalgan o'lcham va rangda.
#include <QColor>
#include <QIcon>
#include <QString>

namespace asiacad::gui {

// name: new open save undo redo select line pline circle arc point text move copy rotate erase
//       extents zoomin zoomout grid search layers drawing survey terrain road pipes bim logo
// bg yaroqsiz bo'lsa — fonsiz (faqat chiziq)
QIcon makeIcon(const QString& name, const QColor& fg, const QColor& bg = QColor(), int size = 64);

} // namespace asiacad::gui
