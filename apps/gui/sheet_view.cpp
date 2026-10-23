#include "sheet_view.h"

#include "viewport.h"

#include <QMouseEvent>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace asiacad::gui {

SheetCanvas::SheetCanvas(const Document& doc, const i18n::Localizer& loc, Viewport* renderer, QWidget* parent)
    : QWidget(parent), doc_(doc), loc_(loc), r_(renderer) {
    setMouseTracking(true);
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
}

const Sheet* SheetCanvas::sheet() const {
    const auto& ss = doc_.sheets();
    if (ss.empty()) return nullptr;
    return &ss[static_cast<std::size_t>(std::clamp(cur_, 0, static_cast<int>(ss.size()) - 1))];
}

void SheetCanvas::setCurrent(int i) {
    const bool changed = i != cur_;
    cur_ = i;
    if (changed || !fitted_) fit();
    update();
}

void SheetCanvas::fit() {
    const Sheet* s = sheet();
    if (!s || width() < 10) return;
    double w = 420, h = 297;
    paperSize(s->size, s->landscape, w, h);
    k_ = std::min((width() - 40.0) / w, (height() - 40.0) / h);
    pan_ = {};
    fitted_ = true;
    update();
}

QPointF SheetCanvas::origin() const {
    const Sheet* s = sheet();
    double w = 420, h = 297;
    if (s) paperSize(s->size, s->landscape, w, h);
    return QPointF(width() / 2.0, height() / 2.0) + pan_ + QPointF(-w / 2 * k_, h / 2 * k_);
}

void SheetCanvas::paintSheet(QPainter& p, const Sheet& s, int number, int total, const QPointF& o, double k, Viewport* r,
                           const i18n::Localizer& loc, double penMul) {
    double W = 420, H = 297;
    paperSize(s.size, s.landscape, W, H);
    auto dev = [&](double x, double y) { return QPointF(o.x() + x * k, o.y() - y * k); };
    p.fillRect(QRectF(dev(0, H), dev(W, 0)), Qt::white);
    for (const auto& v : s.views) {
        const PaperRect pr = viewRect(v);
        if (r) r->renderModel(p, QRectF(dev(pr.x0, pr.y1), dev(pr.x1, pr.y0)), v.center, k * 1000.0 / v.scale, penMul);
    }
    const auto prims = sheetPrims(s, number, total, [&](const std::string& key) { return loc.tr(key); });
    p.setRenderHint(QPainter::Antialiasing);
    for (const auto& pr : prims) {
        if (pr.kind == SheetPrim::Line) {
            QPen pen(Qt::black, std::max(0.6, pr.width * k));
            pen.setCapStyle(Qt::SquareCap);
            p.setPen(pen);
            p.drawLine(dev(pr.a.x, pr.a.y), dev(pr.b.x, pr.b.y));
            continue;
        }
        const double hpx = pr.h * k;
        if (hpx < 1.5) continue;
        QFont f(QStringLiteral("DejaVu Sans"));
        f.setPixelSize(std::max(1, static_cast<int>(std::lround(hpx * 1.35))));
        f.setItalic(true);  // GOST 2.304 shrift (qiya)
        p.setFont(f);
        p.setPen(Qt::black);
        const QString t = QString::fromStdString(pr.text);
        const QPointF a = dev(pr.a.x, pr.a.y);
        const double big = 4000;
        QRectF box(a.x() - (pr.align == 1 ? big / 2 : pr.align == 2 ? big : 0), a.y() - hpx, big, hpx * 2);
        const Qt::Alignment al = Qt::AlignVCenter | (pr.align == 0 ? Qt::AlignLeft : pr.align == 2 ? Qt::AlignRight : Qt::AlignHCenter);
        p.drawText(box, al, t);
    }
}

void SheetCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(118, 124, 134));
    const Sheet* s = sheet();
    if (!s) {
        p.setPen(QColor(240, 240, 240));
        QFont f = font();
        f.setPixelSize(15);
        p.setFont(f);
        p.drawText(rect().adjusted(40, 40, -40, -40), Qt::AlignCenter | Qt::TextWordWrap, QString::fromStdString(loc_.tr("gui.sheet.empty")));
        return;
    }
    if (!fitted_) fit();
    double W = 420, H = 297;
    paperSize(s->size, s->landscape, W, H);
    const QPointF o = origin();
    p.fillRect(QRectF(o.x() + 6, o.y() - H * k_ + 6, W * k_, H * k_), QColor(0, 0, 0, 70));  // soya
    paintSheet(p, *s, cur_ + 1, static_cast<int>(doc_.sheets().size()), o, k_, r_, loc_, 1.0);
    // ko'rinish ustida — ko'k ramka (faqat ekranda)
    const int hv = viewAt(mapFromGlobal(QCursor::pos()));
    if (hv >= 0) {
        const PaperRect pr = viewRect(s->views[static_cast<std::size_t>(hv)]);
        p.setPen(QPen(QColor(40, 120, 230), 1.5, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(QPointF(o.x() + pr.x0 * k_, o.y() - pr.y1 * k_), QPointF(o.x() + pr.x1 * k_, o.y() - pr.y0 * k_)));
    }
    if (s->views.empty()) {
        p.setPen(QColor(120, 120, 120));
        const PaperRect u = sheetUsable(*s);
        p.drawText(QRectF(QPointF(o.x() + u.x0 * k_, o.y() - u.y1 * k_), QPointF(o.x() + u.x1 * k_, o.y() - u.y0 * k_)), Qt::AlignCenter,
                   QString::fromStdString(loc_.tr("gui.sheet.noview")));
    }
}

int SheetCanvas::viewAt(const QPointF& pos) const {
    const Sheet* s = sheet();
    if (!s) return -1;
    const QPointF o = origin();
    const double x = (pos.x() - o.x()) / k_, y = (o.y() - pos.y()) / k_;
    for (std::size_t i = 0; i < s->views.size(); ++i) {
        const PaperRect r = viewRect(s->views[i]);
        if (x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1) return static_cast<int>(i);
    }
    return -1;
}

void SheetCanvas::wheelEvent(QWheelEvent* e) {
    const double f = std::pow(1.0015, e->angleDelta().y());
    const QPointF c = QPointF(width() / 2.0, height() / 2.0) + pan_;  // qog'oz markazi
    const QPointF m = e->position();
    pan_ = m - (m - c) * f - QPointF(width() / 2.0, height() / 2.0);
    k_ = std::clamp(k_ * f, 0.05, 200.0);
    update();
}

void SheetCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus();
    if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
        panning_ = true;
        panStart_ = e->position();
        panOrig_ = pan_;
        setCursor(Qt::ClosedHandCursor);
    }
}

void SheetCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) pan_ = panOrig_ + (e->position() - panStart_);
    update();
}

void SheetCanvas::mouseReleaseEvent(QMouseEvent*) {
    panning_ = false;
    unsetCursor();
}

void SheetCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) { fit(); return; }
    const int i = viewAt(e->position());
    if (i < 0) return;
    emit openView(i);
}

void SheetCanvas::resizeEvent(QResizeEvent*) {
    if (fitted_ && pan_.isNull()) fit();
}

bool SheetCanvas::exportPdf(const QString& path, const std::vector<int>& which, int dpi) const {
    const auto& ss = doc_.sheets();
    if (which.empty()) return false;
    QPdfWriter pdf(path);
    pdf.setResolution(dpi);
    pdf.setCreator(QStringLiteral("AsiaCAD"));
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter* p = nullptr;
    bool first = true;
    for (int i : which) {
        if (i < 0 || i >= static_cast<int>(ss.size())) continue;
        const Sheet& s = ss[static_cast<std::size_t>(i)];
        double W = 420, H = 297;
        paperSize(s.size, s.landscape, W, H);
        pdf.setPageSize(QPageSize(QSizeF(W, H), QPageSize::Millimeter));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        if (first) {
            p = new QPainter(&pdf);
            if (!p->isActive()) { delete p; return false; }
        } else {
            pdf.newPage();
        }
        first = false;
        const double k = dpi / 25.4;
        // 0,25 mm asosiy chiziq: kosmetik 1,25 px × penMul
        paintSheet(*p, s, i + 1, static_cast<int>(ss.size()), QPointF(0, H * k), k, r_, loc_, 0.25 * k / 1.25);
    }
    if (!p) return false;
    p->end();
    delete p;
    return true;
}

} // namespace asiacad::gui
