#include "pipe3d_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace asiacad::gui {

namespace {
constexpr double kPI = 3.14159265358979323846;
P3 add(P3 a, P3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
P3 sub(P3 a, P3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
P3 mul(P3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(P3 a, P3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
P3 cross(P3 a, P3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
P3 norm(P3 a) {
    const double l = std::sqrt(dot(a, a));
    return l < 1e-15 ? P3{0, 0, 1} : mul(a, 1 / l);
}
QColor rgb(unsigned v) { return QColor((v >> 16) & 255, (v >> 8) & 255, v & 255); }
} // namespace

Pipe3DView::Pipe3DView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(280, 220);
    setMouseTracking(false);
}

void Pipe3DView::resetView() {
    az_ = -35;
    el_ = 25;
    zoom_ = 1;
    pan_ = {};
    update();
}

// ---------------------------------------------------------------- geometriya
void Pipe3DView::cylinder(P3 a, P3 b, double r, int seg, const QColor& c, bool capA, bool capB, bool outline) {
    const P3 ax = norm(sub(b, a));
    const P3 ref = std::abs(ax.z) < 0.9 ? P3{0, 0, 1} : P3{1, 0, 0};
    const P3 u = norm(cross(ax, ref)), v = cross(ax, u);
    std::vector<P3> ra, rb;
    for (int i = 0; i < seg; ++i) {
        const double t = 2 * kPI * i / seg;
        const P3 off = add(mul(u, r * std::cos(t)), mul(v, r * std::sin(t)));
        ra.push_back(add(a, off));
        rb.push_back(add(b, off));
    }
    for (int i = 0; i < seg; ++i) {
        const int j = (i + 1) % seg;
        faces_.push_back({{ra[i], ra[j], rb[j], rb[i]}, c, outline});
    }
    if (capA) faces_.push_back({std::vector<P3>(ra.rbegin(), ra.rend()), c.darker(110), outline});
    if (capB) faces_.push_back({rb, c.darker(110), outline});
}

void Pipe3DView::ring(P3 a, P3 b, double ro, double ri, int seg, const QColor& c) {
    const P3 ax = norm(sub(b, a));
    const P3 ref = std::abs(ax.z) < 0.9 ? P3{0, 0, 1} : P3{1, 0, 0};
    const P3 u = norm(cross(ax, ref)), v = cross(ax, u);
    for (int i = 0; i < seg; ++i) {
        const double t0 = 2 * kPI * i / seg, t1 = 2 * kPI * (i + 1) / seg;
        auto pt = [&](double r, double t) { return add(a, add(mul(u, r * std::cos(t)), mul(v, r * std::sin(t)))); };
        faces_.push_back({{pt(ro, t0), pt(ro, t1), pt(ri, t1), pt(ri, t0)}, c, false});
    }
}

void Pipe3DView::showPipe(const civil::PipeSpec& s, const QString& title) {
    faces_.clear();
    labels_.clear();
    title_ = title;
    subtitle_ = QStringLiteral("Ø%1 × %2 mm").arg(s.od, 0, 'f', 1).arg(s.wall, 0, 'f', 1);
    const double ro = s.od / 2000.0, ri = s.id_mm() / 2000.0;
    const double L = std::max(0.9, s.od / 1000.0 * 3.2);
    const QColor c = rgb(civil::materialRgb(s.material));
    const QColor inner = s.material == civil::PipeMaterial::PVC ? QColor(214, 210, 200)   // PVX ichi och
                         : s.material == civil::PipeMaterial::DuctileIron ? QColor(170, 170, 165)  // sement-qum qoplama
                                                                          : c.lighter(125);
    const int seg = 48;
    const P3 a{-L / 2, 0, 0}, b{L / 2, 0, 0};
    if (s.material == civil::PipeMaterial::PP) {
        // gofra: qovurg'alar
        const int ribs = std::max(6, static_cast<int>(L / (s.od / 1000.0 * 0.22)));
        for (int i = 0; i < ribs; ++i) {
            const double x0 = -L / 2 + L * i / ribs, x1 = x0 + L / ribs;
            cylinder({x0, 0, 0}, {x0 + (x1 - x0) * 0.55, 0, 0}, ro, seg, c, false, false);
            cylinder({x0 + (x1 - x0) * 0.55, 0, 0}, {x1, 0, 0}, ro * 0.93, seg, c.darker(115), false, false);
        }
    } else {
        cylinder(a, b, ro, seg, c, false, false);
    }
    cylinder(b, a, ri, seg, inner, false, false);  // ichki sirt (teskari yo'nalishda — normal ichkariga)
    ring(b, add(b, {1, 0, 0}), ro, ri, seg, c.lighter(115));  // kesim: devor qalinligi
    if (s.socket) {
        const double rs = ro * 1.16 + 0.004, ls = std::max(0.08, s.od / 1000.0 * 0.6);
        cylinder({-L / 2 - ls, 0, 0}, a, rs, seg, c.darker(105), false, false);
        ring({-L / 2 - ls, 0, 0}, {-L / 2 - ls - 1, 0, 0}, rs, ro, seg, c.darker(120));
    } else {
        ring(a, {-L / 2 - 1, 0, 0}, ro, ri, seg, c.lighter(115));
    }
    if (s.material == civil::PipeMaterial::PE) {  // PE: rangli chiziq (kanalizatsiya — jigarrang)
        cylinder({-L / 2, 0, ro * 0.995}, {L / 2, 0, ro * 0.995}, ro * 0.035, 8, QColor(150, 90, 40), false, false);
    }
    labels_.push_back({{L / 2, 0, ro + ro * 0.35}, QStringLiteral("t = %1 mm").arg(s.wall, 0, 'f', 1)});
    labels_.push_back({{0, 0, -ro * 1.5}, QString::fromStdString(s.standard) + "  ·  " + QString::fromStdString(s.klass)});
    center_ = {0, 0, 0};
    fit();
    update();
}

void Pipe3DView::showNetwork(const civil::SewerNetwork& n, const civil::SewerResult& r) {
    faces_.clear();
    labels_.clear();
    title_ = QString::fromStdString(n.name);
    if (n.manholes.size() < 2 || r.manholes.size() != n.manholes.size()) {
        update();
        return;
    }
    // vertikal bo'rttirish: chuqurliklar ko'rinadigan bo'lsin
    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300, minZ = 1e300, maxZ = -1e300;
    for (std::size_t i = 0; i < n.manholes.size(); ++i) {
        const auto& m = n.manholes[i];
        minX = std::min(minX, m.x), maxX = std::max(maxX, m.x), minY = std::min(minY, m.y), maxY = std::max(maxY, m.y);
        minZ = std::min({minZ, r.manholes[i].invIn, r.manholes[i].invOut});
        maxZ = std::max(maxZ, m.rim);
    }
    const double horiz = std::max(maxX - minX, maxY - minY);
    const double ex = std::clamp(0.12 * horiz / std::max(0.5, maxZ - minZ), 1.0, 20.0);
    const double z0 = minZ;
    auto Z = [&](double el) { return (el - z0) * ex; };
    subtitle_ = QStringLiteral("Z × %1").arg(ex, 0, 'f', 1);
    const QColor concrete(170, 166, 156), lid(60, 62, 66);
    for (std::size_t i = 0; i < n.manholes.size(); ++i) {
        const auto& m = n.manholes[i];
        const double rr = std::max(r.manholes[i].diameter / 2 + 0.08, horiz / 70.0);
        const double bot = std::min(r.manholes[i].invIn, r.manholes[i].invOut) - 0.15;
        cylinder({m.x, m.y, Z(bot)}, {m.x, m.y, Z(m.rim) - 0.05 * ex}, rr, 20, concrete, true, false);
        cylinder({m.x, m.y, Z(m.rim) - 0.05 * ex}, {m.x, m.y, Z(m.rim)}, rr * 0.75, 20, lid, false, true);
        labels_.push_back({{m.x, m.y, Z(m.rim) + 0.6 * ex}, QString::fromStdString(m.name)});
    }
    for (std::size_t i = 0; i + 1 < n.manholes.size() && i < r.segments.size(); ++i) {
        const auto& s = r.segments[i];
        if (!s.spec) continue;
        const auto& a = n.manholes[i];
        const auto& b = n.manholes[i + 1];
        const double ro = s.spec->od / 2000.0;
        const double rs = std::max(ro, horiz / 140.0);  // juda ingichka bo'lib qolmasin
        const QColor c = rgb(civil::materialRgb(s.spec->material));
        cylinder({a.x, a.y, Z(s.invUp + ro)}, {b.x, b.y, Z(s.invDown + ro)}, rs, 16, c, false, false);
        labels_.push_back({{(a.x + b.x) / 2, (a.y + b.y) / 2, Z((s.invUp + s.invDown) / 2) - 0.8 * ex},
                           QStringLiteral("%1 Ø%2").arg(QString::fromLatin1(civil::materialKey(s.spec->material))).arg(s.spec->dn)});
    }
    // yer sathi lentasi (yarim shaffof)
    const double w = std::max(2.0, horiz * 0.04);
    for (std::size_t i = 0; i + 1 < n.manholes.size(); ++i) {
        const auto& a = n.manholes[i];
        const auto& b = n.manholes[i + 1];
        double dx = b.x - a.x, dy = b.y - a.y;
        const double l = std::hypot(dx, dy);
        if (l < 1e-9) continue;
        dx /= l, dy /= l;
        const P3 nn{-dy * w, dx * w, 0};
        Face f{{{a.x + nn.x, a.y + nn.y, Z(a.rim)}, {b.x + nn.x, b.y + nn.y, Z(b.rim)}, {b.x - nn.x, b.y - nn.y, Z(b.rim)},
                {a.x - nn.x, a.y - nn.y, Z(a.rim)}},
               QColor(120, 170, 90), false, 70};
        faces_.push_back(f);
    }
    center_ = {(minX + maxX) / 2, (minY + maxY) / 2, Z((minZ + maxZ) / 2)};
    fit();
    update();
}

// ---------------------------------------------------------------- proyeksiya va chizish
QPointF Pipe3DView::project(const P3& p0, double* depth) const {
    const P3 p = sub(p0, center_);
    const double a = az_ * kPI / 180, e = el_ * kPI / 180;
    const double x1 = p.x * std::cos(a) - p.y * std::sin(a);
    const double y1 = p.x * std::sin(a) + p.y * std::cos(a);
    const double up = p.z * std::cos(e) + y1 * std::sin(e);
    if (depth) *depth = y1 * std::cos(e) - p.z * std::sin(e);
    const double s = fitScale_ * zoom_;
    return {width() / 2.0 + pan_.x() + (x1 - fitMid_.x()) * s, height() / 2.0 + 10 + pan_.y() - (up + fitMid_.y()) * s};
}

void Pipe3DView::fit() {
    // proyeksiya qutisi bo'yicha (masshtab 1) — cho'ziq quvur ham oynani to'ldiradi
    const double keepS = fitScale_, keepZ = zoom_;
    const QPointF keepP = pan_;
    fitScale_ = 1;
    zoom_ = 1;
    pan_ = {};
    fitMid_ = {};
    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
    auto take = [&](const P3& q) {
        const QPointF s = project(q);
        minX = std::min(minX, s.x()), maxX = std::max(maxX, s.x()), minY = std::min(minY, s.y()), maxY = std::max(maxY, s.y());
    };
    for (const auto& f : faces_)
        for (const auto& q : f.pts) take(q);
    for (const auto& l : labels_) take(l.at);
    zoom_ = keepZ;
    pan_ = keepP;
    const double w = std::max(1e-9, maxX - minX), h = std::max(1e-9, maxY - minY);
    fitScale_ = std::min((width() - 40) / w, (height() - 90) / h);
    if (!std::isfinite(fitScale_) || fitScale_ <= 0) fitScale_ = keepS > 0 ? keepS : 1;
    // markaz — proyeksiya qutisi o'rtasi (masshtab 1 dagi siljish)
    fitMid_ = QPointF((minX + maxX) / 2 - width() / 2.0, (minY + maxY) / 2 - height() / 2.0 - 10);
}

void Pipe3DView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor bg = dark_ ? QColor(17, 22, 40) : QColor(250, 247, 240);
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0, bg.lighter(dark_ ? 135 : 100));
    g.setColorAt(1, bg);
    p.fillRect(rect(), g);
    if (faces_.empty()) return;
    fit();
    const double a = az_ * kPI / 180, e = el_ * kPI / 180;
    // yorug'lik — ko'rish fazosida yuqori-chapdan
    const P3 light = norm({-0.45, -0.55, 0.7});
    struct Item { double depth; std::size_t idx; double shade; };
    std::vector<Item> items;
    items.reserve(faces_.size());
    for (std::size_t i = 0; i < faces_.size(); ++i) {
        const auto& f = faces_[i];
        double d = 0;
        for (const auto& q : f.pts) {
            double dd;
            project(q, &dd);
            d += dd;
        }
        d /= static_cast<double>(f.pts.size());
        P3 nrm = f.pts.size() >= 3 ? norm(cross(sub(f.pts[1], f.pts[0]), sub(f.pts[2], f.pts[0]))) : P3{0, 0, 1};
        // ko'rish fazosiga
        const double x1 = nrm.x * std::cos(a) - nrm.y * std::sin(a);
        const double y1 = nrm.x * std::sin(a) + nrm.y * std::cos(a);
        const P3 nv{x1, -(y1 * std::cos(e) - nrm.z * std::sin(e)), nrm.z * std::cos(e) + y1 * std::sin(e)};
        const double lam = std::abs(dot(nv, light));
        items.push_back({d, i, 0.38 + 0.62 * lam});
    }
    std::sort(items.begin(), items.end(), [](const Item& x, const Item& y) { return x.depth > y.depth; });
    for (const auto& it : items) {
        const auto& f = faces_[it.idx];
        QPolygonF poly;
        for (const auto& q : f.pts) poly << project(q);
        QColor c = f.color;
        c.setRgbF(std::min(1.0, c.redF() * it.shade), std::min(1.0, c.greenF() * it.shade), std::min(1.0, c.blueF() * it.shade));
        c.setAlpha(f.alpha);
        p.setPen(f.outline ? QPen(c.darker(150), 0.6) : QPen(c, 0.6));
        p.setBrush(c);
        p.drawPolygon(poly);
    }
    // yozuvlar
    QFont fnt = font();
    fnt.setPixelSize(11);
    fnt.setBold(true);
    p.setFont(fnt);
    const QColor tc = dark_ ? QColor(232, 236, 248) : QColor(30, 30, 30);
    for (const auto& l : labels_) {
        const QPointF q = project(l.at);
        const QRectF br = p.fontMetrics().boundingRect(l.text).adjusted(-5, -2, 5, 2);
        QRectF box(q.x() - br.width() / 2, q.y() - br.height() / 2, br.width(), br.height());
        p.setPen(Qt::NoPen);
        p.setBrush(dark_ ? QColor(0, 0, 0, 140) : QColor(255, 255, 255, 190));
        p.drawRoundedRect(box, 5, 5);
        p.setPen(tc);
        p.drawText(box, Qt::AlignCenter, l.text);
    }
    fnt.setPixelSize(13);
    p.setFont(fnt);
    p.setPen(tc);
    p.drawText(QPointF(12, 22), title_);
    fnt.setBold(false);
    fnt.setPixelSize(11);
    p.setFont(fnt);
    p.drawText(QPointF(12, 40), subtitle_);
    // o'qlar belgisi
    const QPointF o(40, height() - 34);
    const struct { P3 d; QColor c; const char* t; } axes[] = {{{1, 0, 0}, QColor(220, 70, 90), "X"},
                                                            {{0, 1, 0}, QColor(40, 190, 170), "Y"},
                                                            {{0, 0, 1}, QColor(90, 140, 255), "Z"}};
    for (const auto& ax : axes) {
        const double x1 = ax.d.x * std::cos(a) - ax.d.y * std::sin(a);
        const double y1 = ax.d.x * std::sin(a) + ax.d.y * std::cos(a);
        const QPointF t = o + QPointF(x1 * 22, -(ax.d.z * std::cos(e) + y1 * std::sin(e)) * 22);
        p.setPen(QPen(ax.c, 2));
        p.drawLine(o, t);
        p.drawText(t + QPointF(3, 3), ax.t);
    }
}

void Pipe3DView::mousePressEvent(QMouseEvent* e) { last_ = e->pos(); }
void Pipe3DView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint d = e->pos() - last_;
    last_ = e->pos();
    if (e->buttons() & Qt::LeftButton) {
        az_ += d.x() * 0.5;
        el_ = std::clamp(el_ + d.y() * 0.4, -10.0, 89.0);
    } else if (e->buttons() & (Qt::RightButton | Qt::MiddleButton)) {
        pan_ += d;
    }
    update();
}
void Pipe3DView::wheelEvent(QWheelEvent* e) {
    zoom_ = std::clamp(zoom_ * std::pow(1.0015, e->angleDelta().y()), 0.1, 50.0);
    update();
}
void Pipe3DView::mouseDoubleClickEvent(QMouseEvent*) { resetView(); }

} // namespace asiacad::gui
