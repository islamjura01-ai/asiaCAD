#include "icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <cmath>

namespace asiacad::gui {

namespace {

// 24×24 koordinatalarida chiziladi (Feather/Lucide uslubi), keyin masshtablanadi
void glyph(QPainter& p, const QString& n) {
    auto L = [&](double x1, double y1, double x2, double y2) { p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); };
    auto C = [&](double cx, double cy, double r) { p.drawEllipse(QPointF(cx, cy), r, r); };
    auto R = [&](double x, double y, double w, double h, double rr = 1.5) { p.drawRoundedRect(QRectF(x, y, w, h), rr, rr); };
    auto P = [&](std::initializer_list<QPointF> pts, bool close = false) {
        QPainterPath path;
        bool first = true;
        for (const auto& q : pts) { if (first) path.moveTo(q); else path.lineTo(q); first = false; }
        if (close) path.closeSubpath();
        p.drawPath(path);
    };
    auto dot = [&](double x, double y, double r = 1.6) {
        p.save(); p.setBrush(p.pen().color()); C(x, y, r); p.restore();
    };
    if (n == "new") { P({{6, 3}, {14, 3}, {19, 8}, {19, 21}, {6, 21}}, true); P({{14, 3}, {14, 8}, {19, 8}}); L(12.5, 11, 12.5, 18); L(9, 14.5, 16, 14.5); }
    else if (n == "open") { P({{3, 7}, {9, 7}, {11, 9}, {21, 9}, {21, 19}, {3, 19}}, true); L(3, 12, 21, 12); }
    else if (n == "save") { P({{4, 4}, {17, 4}, {20, 7}, {20, 20}, {4, 20}}, true); R(8, 4, 8, 5, 0.5); R(7, 13, 10, 7, 0.5); }
    else if (n == "undo") { QPainterPath a; a.moveTo(9, 14); a.lineTo(4, 9); a.lineTo(9, 4); p.drawPath(a); QPainterPath b; b.moveTo(4, 9); b.lineTo(14, 9); b.cubicTo(18, 9, 20, 12, 20, 15); b.cubicTo(20, 18, 18, 20, 14, 20); b.lineTo(11, 20); p.drawPath(b); }
    else if (n == "redo") { QPainterPath a; a.moveTo(15, 14); a.lineTo(20, 9); a.lineTo(15, 4); p.drawPath(a); QPainterPath b; b.moveTo(20, 9); b.lineTo(10, 9); b.cubicTo(6, 9, 4, 12, 4, 15); b.cubicTo(4, 18, 6, 20, 10, 20); b.lineTo(13, 20); p.drawPath(b); }
    else if (n == "select") { P({{5, 3}, {5, 19}, {9.5, 15}, {12.5, 21}, {15, 20}, {12, 14}, {18, 14}}, true); }
    else if (n == "line") { L(5, 19, 19, 5); dot(5, 19); dot(19, 5); }
    else if (n == "pline") { P({{3, 18}, {9, 9}, {14, 14}, {21, 5}}); dot(3, 18); dot(9, 9); dot(14, 14); dot(21, 5); }
    else if (n == "circle") { C(12, 12, 8); dot(12, 12, 1.3); }
    else if (n == "arc") { QPainterPath a; a.moveTo(4, 18); a.cubicTo(4, 9, 10, 4, 20, 5); p.drawPath(a); dot(4, 18); dot(20, 5); dot(8.2, 9.2, 1.3); }
    else if (n == "point") { L(12, 5, 12, 19); L(5, 12, 19, 12); C(12, 12, 3); }
    else if (n == "text") { L(5, 6, 19, 6); L(12, 6, 12, 19); L(9, 19, 15, 19); L(5, 6, 5, 8); L(19, 6, 19, 8); }
    else if (n == "move") { L(12, 3, 12, 21); L(3, 12, 21, 12); P({{9, 6}, {12, 3}, {15, 6}}); P({{9, 18}, {12, 21}, {15, 18}}); P({{6, 9}, {3, 12}, {6, 15}}); P({{18, 9}, {21, 12}, {18, 15}}); }
    else if (n == "copy") { R(9, 9, 11, 11); P({{6, 15}, {4, 15}, {4, 4}, {15, 4}, {15, 6}}); }
    else if (n == "rotate") { QPainterPath a; a.moveTo(20, 12); a.arcTo(QRectF(4, 4, 16, 16), 0, 290); p.drawPath(a); P({{20, 6}, {20, 12}, {14, 12}}); }
    else if (n == "erase") { P({{4, 7}, {20, 7}}); P({{9, 7}, {9, 4}, {15, 4}, {15, 7}}); P({{6, 7}, {7, 20}, {17, 20}, {18, 7}}); L(10, 11, 10, 16); L(14, 11, 14, 16); }
    else if (n == "extents") { P({{3, 9}, {3, 3}, {9, 3}}); P({{15, 3}, {21, 3}, {21, 9}}); P({{21, 15}, {21, 21}, {15, 21}}); P({{9, 21}, {3, 21}, {3, 15}}); R(8, 8, 8, 8, 1); }
    else if (n == "zoomin") { C(10.5, 10.5, 6.5); L(15.5, 15.5, 21, 21); L(10.5, 7.5, 10.5, 13.5); L(7.5, 10.5, 13.5, 10.5); }
    else if (n == "zoomout") { C(10.5, 10.5, 6.5); L(15.5, 15.5, 21, 21); L(7.5, 10.5, 13.5, 10.5); }
    else if (n == "grid") { for (int i = 0; i < 4; ++i) { L(4 + i * 5.33, 4, 4 + i * 5.33, 20); L(4, 4 + i * 5.33, 20, 4 + i * 5.33); } }
    else if (n == "dimlinear") { L(5, 8, 5, 20); L(19, 8, 19, 20); L(3, 12, 21, 12); L(3.5, 13.5, 6.5, 10.5); L(17.5, 13.5, 20.5, 10.5); L(9, 6, 15, 6); }
    else if (n == "dimaligned") { L(4, 15, 13, 6); L(11, 21, 20, 12); L(5, 12, 16, 23); L(8, 9, 19, 20); L(6.5, 14.5, 9.5, 14.5); L(15.5, 17.5, 18.5, 17.5); }
    else if (n == "dimradius") { QPainterPath a; a.moveTo(20, 12); a.arcTo(QRectF(4, 4, 16, 16), 0, 300); p.drawPath(a); L(12, 12, 18, 6.5); dot(12, 12, 1.3); P({{15, 7}, {18, 6.5}, {17.4, 9.6}}); }
    else if (n == "dimdiameter") { C(12, 12, 8); L(6.3, 17.7, 17.7, 6.3); P({{14.6, 6.8}, {17.7, 6.3}, {17.2, 9.4}}); P({{9.4, 17.2}, {6.3, 17.7}, {6.8, 14.6}}); }
    else if (n == "offset") { P({{3, 20}, {3, 9}, {14, 9}}); P({{8, 20}, {8, 14}, {20, 14}}); P({{17, 4}, {20, 7}, {17, 10}}, false); }
    else if (n == "trim") { L(12, 3, 12, 21); L(3, 12, 10, 12); p.save(); QPen d = p.pen(); d.setStyle(Qt::DotLine); p.setPen(d); L(14, 12, 21, 12); p.restore(); L(17, 9, 20, 15); L(20, 9, 17, 15); }
    else if (n == "extend") { L(20, 3, 20, 21); L(3, 12, 11, 12); p.save(); QPen d = p.pen(); d.setStyle(Qt::DotLine); p.setPen(d); L(12, 12, 19, 12); p.restore(); P({{15, 9}, {18.5, 12}, {15, 15}}); }
    else if (n == "fillet") { QPainterPath a; a.moveTo(4, 4); a.lineTo(4, 12); a.quadTo(4, 20, 12, 20); a.lineTo(20, 20); p.drawPath(a); dot(4, 4); dot(20, 20); }
    else if (n == "search") { C(10.5, 10.5, 6.5); L(15.5, 15.5, 21, 21); }
    else if (n == "layers") { P({{12, 3}, {21, 8}, {12, 13}, {3, 8}}, true); P({{3, 12.5}, {12, 17.5}, {21, 12.5}}); P({{3, 16.5}, {12, 21.5}, {21, 16.5}}); }
    else if (n == "drawing") { P({{3, 17}, {9, 9}, {13, 13}, {21, 5}}); }
    else if (n == "survey") { C(12, 6, 2.5); L(12, 8.5, 6, 21); L(12, 8.5, 18, 21); L(12, 8.5, 12, 21); }
    else if (n == "terrain") { P({{2, 19}, {8, 10}, {12, 15}, {16, 8}, {22, 19}}, true); }
    else if (n == "road") { L(8, 3, 4, 21); L(16, 3, 20, 21); L(12, 4, 12, 7); L(12, 10.5, 12, 13.5); L(12, 17, 12, 20); }
    else if (n == "corridor") {  // yig'ma: o'q, polosalar, qiyaliklar
        P({{2, 19}, {6, 13}, {10, 12}, {14, 12}, {18, 13}, {22, 19}}); L(12, 4, 12, 12); L(10, 6, 14, 6); dot(6, 13); dot(18, 13);
    }
    else if (n == "rect") { R(3, 6, 18, 12, 0.5); dot(3, 6); dot(21, 18); }
    else if (n == "polygon") { P({{12, 3}, {20, 8}, {20, 16}, {12, 21}, {4, 16}, {4, 8}}, true); }
    else if (n == "ellipse") { p.drawEllipse(QPointF(12, 12), 9, 5.5); dot(12, 12, 1.2); }
    else if (n == "spline") { QPainterPath a; a.moveTo(3, 17); a.cubicTo(7, 3, 12, 22, 21, 6); p.drawPath(a); dot(3, 17); dot(21, 6); }
    else if (n == "hatch") { R(3, 3, 18, 18, 0.5); L(3, 11, 11, 3); L(3, 19, 19, 3); L(9, 21, 21, 9); L(17, 21, 21, 17); }
    else if (n == "symbols") { C(8, 8, 4.5); L(8, 12.5, 8, 15); R(13, 13, 8, 5, 1.5); dot(14.5, 19.5, 1); dot(19.5, 19.5, 1); P({{4, 21}, {8, 16}, {12, 21}}, true); }
    else if (n == "stretch") { R(3, 7, 9, 10, 0.5); p.save(); QPen d = p.pen(); d.setStyle(Qt::DashLine); p.setPen(d); R(9, 4, 12, 16, 0.5); p.restore(); L(13, 12, 20, 12); P({{17, 9}, {20, 12}, {17, 15}}); }
    else if (n == "scale") { R(3, 11, 10, 10, 0.5); R(3, 3, 18, 18, 0.5); L(13, 11, 19, 5); P({{15, 5}, {19, 5}, {19, 9}}); }
    else if (n == "mirror") { p.save(); QPen d = p.pen(); d.setStyle(Qt::DashLine); p.setPen(d); L(12, 2, 12, 22); p.restore(); P({{10, 6}, {3, 18}, {10, 18}}, true); P({{14, 6}, {21, 18}, {14, 18}}, true); }
    else if (n == "chamfer") { P({{4, 4}, {4, 13}, {11, 20}, {20, 20}}); dot(4, 13); dot(11, 20); }
    else if (n == "array") { for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) R(3 + i * 7, 3 + j * 7, 4.5, 4.5, 0.8); }
    else if (n == "explode") { R(8, 8, 8, 8, 0.5); L(4, 4, 7, 7); L(20, 4, 17, 7); L(4, 20, 7, 17); L(20, 20, 17, 17); }
    else if (n == "join") { L(3, 12, 10, 12); L(14, 12, 21, 12); P({{8, 8}, {12, 12}, {8, 16}}); P({{16, 8}, {12, 12}, {16, 16}}); }
    else if (n == "break") { L(3, 12, 9, 12); L(15, 12, 21, 12); L(10, 8, 8, 16); L(16, 8, 14, 16); }
    else if (n == "matchprop") { R(3, 14, 9, 7, 1); L(12, 17, 17, 12); P({{15, 4}, {21, 10}, {17, 14}, {11, 8}}, true); }
    else if (n == "leader") { P({{4, 18}, {12, 8}, {21, 8}}); p.save(); p.setBrush(p.pen().color()); P({{4, 18}, {5, 13}, {8, 16}}, true); p.restore(); L(13, 5, 20, 5); }
    else if (n == "dimangular") { L(4, 20, 21, 20); L(4, 20, 16, 5); QPainterPath a; a.moveTo(14, 20); a.arcTo(QRectF(-6, 10, 20, 20), 0, 52); p.drawPath(a); }
    else if (n.startsWith("lay")) {  // qatlam belgisi + holat
        P({{12, 4}, {21, 9}, {12, 14}, {3, 9}}, true);
        P({{3, 13}, {12, 18}, {21, 13}});
        if (n == "laymcur") { dot(18, 19, 2.6); }
        else if (n == "laymatch") { L(15, 21, 21, 15); P({{17, 15}, {21, 15}, {21, 19}}); }
        else if (n == "layoff") { L(15, 15, 22, 22); L(22, 15, 15, 22); }
        else if (n == "layfrz") { L(18.5, 14, 18.5, 23); L(14.5, 16.5, 22.5, 20.5); L(14.5, 20.5, 22.5, 16.5); }
        else if (n == "laylck") { R(15, 18, 7, 5, 1); QPainterPath a; a.moveTo(16.5, 18); a.arcTo(QRectF(16.5, 14, 4, 8), 180, -180); p.drawPath(a); }
        else if (n == "layulk") { R(15, 18, 7, 5, 1); QPainterPath a; a.moveTo(16.5, 18); a.arcTo(QRectF(16.5, 14, 4, 8), 180, -120); p.drawPath(a); }
        else if (n == "layiso") { C(18.5, 18.5, 3.5); dot(18.5, 18.5, 1.2); }
        else if (n == "layuniso") { P({{22, 18}, {18, 22}, {15, 18}}); }
        else if (n == "layon") { C(18.5, 18.5, 3.5); L(18.5, 13, 18.5, 14.5); L(13, 18.5, 14.5, 18.5); }
    }
    else if (n == "drive") {  // rul
        C(12, 12, 9); C(12, 12, 2.5); L(3.5, 10.5, 9.6, 11.2); L(14.4, 11.2, 20.5, 10.5); L(12, 14.5, 12, 21);
    }
    else if (n == "city") {  // kvartal binolari: har xil balandlik
        R(3, 11, 5, 10, 0.5); R(10, 4, 5, 17, 0.5); R(17, 8, 4, 13, 0.5); L(2, 21, 22, 21);
    }
    else if (n == "waterway") {  // trapetsiya kanal + to'lqin
        P({{2, 6}, {7, 19}, {17, 19}, {22, 6}});
        QPainterPath w; w.moveTo(6, 12); w.cubicTo(8, 10, 10, 14, 12, 12); w.cubicTo(14, 10, 16, 14, 18, 12); p.drawPath(w);
    }
    else if (n == "pipes") { P({{3, 8}, {13, 8}}); QPainterPath a; a.moveTo(13, 8); a.cubicTo(17, 8, 18, 10, 18, 14); a.lineTo(18, 21); p.drawPath(a); P({{3, 14}, {11, 14}}); }
    else if (n == "util") { L(2, 7, 22, 7); p.save(); QPen d = p.pen(); d.setStyle(Qt::DotLine); p.setPen(d); L(2, 10, 22, 10); p.restore(); L(4, 14, 20, 14); L(4, 19, 20, 19); L(8, 12, 8, 21); C(16, 16.5, 2.5); }
    else if (n == "rebar") { L(7, 3, 7, 21); L(17, 3, 17, 21); L(10, 5, 10, 19); L(14, 5, 14, 19); for (int i = 0; i < 4; ++i) R(5, 5 + i * 4.3, 14, 1.6, 0.4); }
    else if (n == "bim") { P({{3, 21}, {3, 9}, {12, 3}, {21, 9}, {21, 21}}, true); R(9, 14, 6, 7, 0.5); }
    else if (n == "logo") {  // sakkiz qirrali yulduz — Samarqand naqshi
        QPainterPath a;
        for (int i = 0; i < 16; ++i) {
            const double ang = i * M_PI / 8 - M_PI / 2, r = (i % 2 == 0) ? 9.5 : 5.2;
            const QPointF q(12 + r * std::cos(ang), 12 + r * std::sin(ang));
            if (i == 0) a.moveTo(q); else a.lineTo(q);
        }
        a.closeSubpath();
        p.save(); p.setBrush(p.pen().color()); p.drawPath(a); p.restore();
    }
    else { C(12, 12, 6); }
}

} // namespace

QIcon makeIcon(const QString& name, const QColor& fg, const QColor& bg, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (bg.isValid()) {
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(QRectF(0, 0, size, size), size * 0.26, size * 0.26);
    }
    const double inset = bg.isValid() ? size * 0.2 : size * 0.06;
    p.translate(inset, inset);
    p.scale((size - 2 * inset) / 24.0, (size - 2 * inset) / 24.0);
    QPen pen(fg, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    glyph(p, name);
    p.end();
    return QIcon(pm);
}

} // namespace asiacad::gui
