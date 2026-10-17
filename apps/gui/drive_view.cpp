#include "drive_view.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace asiacad::gui {

namespace {
constexpr double kPI = 3.14159265358979323846;
Vec3 crossV(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 unit(const Vec3& v) {
    const double l = v.length();
    return l < 1e-15 ? Vec3{1, 0, 0} : v / l;
}
struct CV { double x, y, z; };  // kamera fazosi (z — oldinga)
}  // namespace

DriveView::DriveView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 300);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void DriveView::setCamera(const Vec3& eye, const Vec3& target) {
    eye_ = eye;
    target_ = target;
    update();
}

void DriveView::renderNow() { render(std::max(16, static_cast<int>(width() * scale_)), std::max(16, static_cast<int>(height() * scale_))); }

void DriveView::render(int W, int H) {
    if (img_.width() != W || img_.height() != H) img_ = QImage(W, H, QImage::Format_RGB32);
    depth_.assign(static_cast<std::size_t>(W) * H, 0.0f);
    // osmon: gradient (ufqda och)
    const QColor skyTop(92, 146, 214), skyHor(206, 224, 240), fogC(198, 214, 228);
    // kamera bazisi
    Vec3 f = unit(target_ - eye_);
    {
        const double hz = std::atan2(f.y, f.x) + yaw_ * kPI / 180;
        const double el = std::asin(std::clamp(f.z, -1.0, 1.0)) + pitch_ * kPI / 180;
        const double ce = std::cos(std::clamp(el, -1.45, 1.45));
        f = {std::cos(hz) * ce, std::sin(hz) * ce, std::sin(std::clamp(el, -1.45, 1.45))};
    }
    const Vec3 r = unit(crossV(f, {0, 0, 1}));
    const Vec3 u = crossV(r, f);
    const double fpx = (W / 2.0) / std::tan(fov_ * kPI / 360);
    // ufq chizig'i (pitch hisobga olingan)
    const double horizonY = H / 2.0 + fpx * std::tan(std::asin(std::clamp(f.z, -1.0, 1.0)));
    for (int y = 0; y < H; ++y) {
        const double t = std::clamp((y - (horizonY - H * 0.6)) / (H * 0.6), 0.0, 1.0);
        const QColor c = y < horizonY ? QColor::fromRgbF(skyTop.redF() + (skyHor.redF() - skyTop.redF()) * t,
                                                         skyTop.greenF() + (skyHor.greenF() - skyTop.greenF()) * t,
                                                         skyTop.blueF() + (skyHor.blueF() - skyTop.blueF()) * t)
                                      : fogC;
        auto* line = reinterpret_cast<QRgb*>(img_.scanLine(y));
        std::fill(line, line + W, c.rgb());
    }
    drawn_ = 0;
    if (!scene_) return;
    const double nearZ = 0.08, farZ = far_;
    const double fogD = far_ * 0.85;
    const Vec3 sun = unit({-0.35, -0.55, 0.76});
    auto toCam = [&](const Vec3& p) {
        const Vec3 d = p - eye_;
        return CV{d.dot(r), d.dot(u), d.dot(f)};
    };
    // qirqish: z >= near
    auto clip = [&](const CV* in, int n, CV* out) {
        int m = 0;
        for (int i = 0; i < n; ++i) {
            const CV& a = in[i];
            const CV& b = in[(i + 1) % n];
            const bool ia = a.z >= nearZ, ib = b.z >= nearZ;
            if (ia) out[m++] = a;
            if (ia != ib) {
                const double t = (nearZ - a.z) / (b.z - a.z);
                out[m++] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, nearZ};
            }
        }
        return m;
    };
    auto* px = reinterpret_cast<QRgb*>(img_.bits());
    const int stride = img_.bytesPerLine() / 4;
    auto shadePix = [&](double baseR, double baseG, double baseB, float invz) {
        const double dist = 1.0 / invz;
        const double fg = std::clamp(dist / fogD, 0.0, 1.0);
        const double k = fg * fg;
        return qRgb(static_cast<int>(baseR + (fogC.red() - baseR) * k), static_cast<int>(baseG + (fogC.green() - baseG) * k),
                    static_cast<int>(baseB + (fogC.blue() - baseB) * k));
    };
    // uchburchak rasterlash (ekran koordinatalari, invz chiziqli)
    auto raster = [&](const double* sx, const double* sy, const float* iz, double cr, double cg, double cb) {
        const double minX = std::max(0.0, std::floor(std::min({sx[0], sx[1], sx[2]})));
        const double maxX = std::min(W - 1.0, std::ceil(std::max({sx[0], sx[1], sx[2]})));
        const double minY = std::max(0.0, std::floor(std::min({sy[0], sy[1], sy[2]})));
        const double maxY = std::min(H - 1.0, std::ceil(std::max({sy[0], sy[1], sy[2]})));
        if (minX > maxX || minY > maxY) return;
        const double area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (std::abs(area) < 1e-12) return;
        const double ia = 1 / area;
        for (int y = static_cast<int>(minY); y <= static_cast<int>(maxY); ++y) {
            const double py = y + 0.5;
            // satr uchun chegaralar: chiziqli tenglamalar
            const double e0y = (sx[2] - sx[1]) * (py - sy[1]), e1y = (sx[0] - sx[2]) * (py - sy[2]), e2y = (sx[1] - sx[0]) * (py - sy[0]);
            const double a0 = -(sy[2] - sy[1]), a1 = -(sy[0] - sy[2]), a2 = -(sy[1] - sy[0]);
            QRgb* row = px + static_cast<std::ptrdiff_t>(y) * stride;
            float* drow = depth_.data() + static_cast<std::ptrdiff_t>(y) * W;
            for (int x = static_cast<int>(minX); x <= static_cast<int>(maxX); ++x) {
                const double pxx = x + 0.5;
                double w0 = (e0y + a0 * (pxx - sx[1])) * ia, w1 = (e1y + a1 * (pxx - sx[2])) * ia, w2 = (e2y + a2 * (pxx - sx[0])) * ia;
                if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
                const float z = static_cast<float>(w0 * iz[0] + w1 * iz[1] + w2 * iz[2]);
                if (z <= drow[x]) continue;
                drow[x] = z;
                row[x] = shadePix(cr, cg, cb, z);
            }
        }
    };
    for (const auto& t : scene_->tris) {
        CV c[3] = {toCam(t.p[0]), toCam(t.p[1]), toCam(t.p[2])};
        if (c[0].z < nearZ && c[1].z < nearZ && c[2].z < nearZ) continue;
        if (c[0].z > farZ && c[1].z > farZ && c[2].z > farZ) continue;
        // ko'rish piramidasi tashqarisida (bir tomonda) — tashlab ketiladi
        auto outside = [&](auto pred) { return pred(c[0]) && pred(c[1]) && pred(c[2]); };
        const double tx = W / 2.0 / fpx * 1.05, ty = H / 2.0 / fpx * 1.05;
        if (outside([&](const CV& q) { return q.x > q.z * tx + 0.5; }) || outside([&](const CV& q) { return q.x < -q.z * tx - 0.5; }) ||
            outside([&](const CV& q) { return q.y > q.z * ty + 0.5; }) || outside([&](const CV& q) { return q.y < -q.z * ty - 0.5; }))
            continue;
        CV poly[4];
        const int n = clip(c, 3, poly);
        if (n < 3) continue;
        const Vec3 nw = unit(crossV(t.p[1] - t.p[0], t.p[2] - t.p[0]));
        const double lam = std::abs(nw.dot(sun));
        const double sh = 0.42 + 0.58 * lam;
        const double cr = std::min(255.0, ((t.rgb >> 16) & 255) * sh), cg = std::min(255.0, ((t.rgb >> 8) & 255) * sh),
                     cb = std::min(255.0, (t.rgb & 255) * sh);
        double sx[4], sy[4];
        float iz[4];
        for (int i = 0; i < n; ++i) {
            sx[i] = W / 2.0 + poly[i].x / poly[i].z * fpx;
            sy[i] = H / 2.0 - poly[i].y / poly[i].z * fpx;
            iz[i] = static_cast<float>(1.0 / poly[i].z);
        }
        for (int i = 1; i + 1 < n; ++i) {
            const double xs[3] = {sx[0], sx[i], sx[i + 1]}, ys[3] = {sy[0], sy[i], sy[i + 1]};
            const float zs[3] = {iz[0], iz[i], iz[i + 1]};
            raster(xs, ys, zs, cr, cg, cb);
        }
        ++drawn_;
    }
    // chiziqlar (chuqurlik tekshiruvi bilan, kichik siljish)
    for (const auto& s : scene_->segs) {
        CV a = toCam(s.a), b = toCam(s.b);
        if (a.z < nearZ && b.z < nearZ) continue;
        if (a.z > farZ && b.z > farZ) continue;
        if (a.z < nearZ) { const double t = (nearZ - a.z) / (b.z - a.z); a = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, nearZ}; }
        if (b.z < nearZ) { const double t = (nearZ - b.z) / (a.z - b.z); b = {b.x + (a.x - b.x) * t, b.y + (a.y - b.y) * t, nearZ}; }
        const double x0 = W / 2.0 + a.x / a.z * fpx, y0 = H / 2.0 - a.y / a.z * fpx;
        const double x1 = W / 2.0 + b.x / b.z * fpx, y1 = H / 2.0 - b.y / b.z * fpx;
        const double steps = std::ceil(std::max(std::abs(x1 - x0), std::abs(y1 - y0)));
        if (steps > 20000) continue;
        const float iza = static_cast<float>(1 / a.z), izb = static_cast<float>(1 / b.z);
        const double cr = (s.rgb >> 16) & 255, cg = (s.rgb >> 8) & 255, cb = s.rgb & 255;
        for (int k = 0; k <= static_cast<int>(steps); ++k) {
            const double t = steps > 0 ? k / steps : 0;
            const int x = static_cast<int>(x0 + (x1 - x0) * t), y = static_cast<int>(y0 + (y1 - y0) * t);
            if (x < 0 || y < 0 || x >= W || y >= H) continue;
            const float z = iza + (izb - iza) * static_cast<float>(t);
            float& d = depth_[static_cast<std::size_t>(y) * W + x];
            if (z * 1.004f + 1e-5f < d) continue;
            d = std::max(d, z);
            px[static_cast<std::ptrdiff_t>(y) * stride + x] = shadePix(cr, cg, cb, z);
        }
    }
}

void DriveView::paintEvent(QPaintEvent*) {
    renderNow();
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(rect(), img_);
    p.setRenderHint(QPainter::Antialiasing);
    if (overlay_) overlay_(p);
}

void DriveView::mousePressEvent(QMouseEvent* e) {
    last_ = e->pos();
    setFocus();
}
void DriveView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint d = e->pos() - last_;
    last_ = e->pos();
    if (!(e->buttons() & (Qt::LeftButton | Qt::RightButton))) return;
    yaw_ = std::fmod(yaw_ - d.x() * 0.25, 360.0);
    pitch_ = std::clamp(pitch_ - d.y() * 0.2, -80.0, 80.0);
    emit lookChanged();
    update();
}
void DriveView::mouseDoubleClickEvent(QMouseEvent*) {
    yaw_ = pitch_ = 0;
    emit lookChanged();
    update();
}
void DriveView::keyPressEvent(QKeyEvent* e) { emit keyPressed(e->key()); }

} // namespace asiacad::gui
