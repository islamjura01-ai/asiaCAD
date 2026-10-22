#include "model3d_view.h"

#include <QContextMenuEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QSurfaceFormat>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace asiacad::gui {

namespace {
constexpr double kDeg = 3.14159265358979323846 / 180.0;

const char* kVS = R"(
attribute vec3 aPos;
attribute vec3 aNor;
attribute vec4 aCol;
attribute float aId;
uniform mat4 uMvp;
uniform mat3 uNorm;
uniform float uSel;
varying vec4 vCol;
varying vec3 vNor;
varying float vZ;
varying float vSel;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    vCol = aCol;
    vNor = uNorm * aNor;
    vZ = aPos.z;
    vSel = abs(aId - uSel) < 0.5 ? 1.0 : 0.0;
}
)";
const char* kFS = R"(
uniform float uClipZ;
uniform float uLit;
uniform float uAlphaMul;
uniform vec4 uOverride;
uniform vec3 uLight;
varying vec4 vCol;
varying vec3 vNor;
varying float vZ;
varying float vSel;
void main() {
    if (vZ > uClipZ) discard;
    vec4 c = vCol;
    if (uOverride.a > 0.0) c = uOverride;
    if (uLit > 0.5) {
        vec3 n = normalize(vNor);
        float d = abs(dot(n, uLight));
        float sky = 0.5 + 0.5 * abs(n.y);
        c.rgb = c.rgb * (0.30 + 0.55 * d + 0.15 * sky);
    }
    if (vSel > 0.5) c.rgb = mix(c.rgb, vec3(0.15, 0.62, 1.0), 0.55);
    c.a *= uAlphaMul;
    gl_FragColor = c;
}
)";

// Ear clipping (2D, CCW); teshiklar ko'prik bilan ulangan
std::vector<std::array<int, 3>> earclip(const std::vector<QPointF>& p) {
    std::vector<std::array<int, 3>> out;
    const int n = static_cast<int>(p.size());
    if (n < 3) return out;
    double area = 0;
    for (int i = 0; i < n; ++i) area += p[i].x() * p[(i + 1) % n].y() - p[(i + 1) % n].x() * p[i].y();
    std::vector<int> idx(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) idx[static_cast<std::size_t>(i)] = area >= 0 ? i : n - 1 - i;
    auto cross = [&](int a, int b, int c) {
        return (p[b].x() - p[a].x()) * (p[c].y() - p[a].y()) - (p[b].y() - p[a].y()) * (p[c].x() - p[a].x());
    };
    auto same = [&](int a, int b) { return std::abs(p[a].x() - p[b].x()) < 1e-12 && std::abs(p[a].y() - p[b].y()) < 1e-12; };
    auto inside = [&](int q, int a, int b, int c) {
        if (same(q, a) || same(q, b) || same(q, c)) return false;
        return cross(a, b, q) > 1e-14 && cross(b, c, q) > 1e-14 && cross(c, a, q) > 1e-14;
    };
    int guard = 0;
    while (idx.size() > 3 && guard++ < 100000) {
        bool cut = false;
        const int m = static_cast<int>(idx.size());
        for (int i = 0; i < m; ++i) {
            const int a = idx[static_cast<std::size_t>((i + m - 1) % m)], b = idx[static_cast<std::size_t>(i)], c = idx[static_cast<std::size_t>((i + 1) % m)];
            if (cross(a, b, c) <= 1e-14) continue;
            bool ok = true;
            for (int j : idx)
                if (j != a && j != b && j != c && inside(j, a, b, c)) { ok = false; break; }
            if (!ok) continue;
            out.push_back({a, b, c});
            idx.erase(idx.begin() + i);
            cut = true;
            break;
        }
        if (!cut) {   // degeneratsiya — eng "tekis" uchni olib tashlab davom etamiz
            idx.erase(idx.begin());
        }
    }
    if (idx.size() == 3 && cross(idx[0], idx[1], idx[2]) > 1e-14) out.push_back({idx[0], idx[1], idx[2]});
    return out;
}
QFont scaled(QFont f, double k) {
    if (f.pointSizeF() > 0) f.setPointSizeF(std::max(6.0, f.pointSizeF() * k));
    else if (f.pixelSize() > 0) f.setPixelSize(std::max(8, int(f.pixelSize() * k)));
    return f;
}
}  // namespace

Model3DView::Model3DView(QWidget* parent) : QOpenGLWidget(parent) {
    QSurfaceFormat f = format();
    f.setDepthBufferSize(24);
    f.setSamples(4);
    setFormat(f);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);
    buildUi();
}

Model3DView::~Model3DView() {
    makeCurrent();
    vOpaque_.destroy();
    vTrans_.destroy();
    vLine_.destroy();
    vEdge_.destroy();
    prog_.reset();
    doneCurrent();
}

void Model3DView::setTr(std::function<QString(const char*)> tr) {
    trf_ = std::move(tr);
    retranslateUi();
    update();
}

std::function<QString(const char*)>& Model3DView::globalTr() {
    static std::function<QString(const char*)> f;
    return f;
}
void Model3DView::setGlobalTr(std::function<QString(const char*)> tr) { globalTr() = std::move(tr); }

QString Model3DView::tr_(const char* k) const {
    if (trf_) return trf_(k);
    if (globalTr()) {
        const QString s = globalTr()(k);
        if (!s.isEmpty() && s != QString::fromLatin1(k)) return s;
    }
    static const std::map<std::string, const char*> def = {
        {"v3d.top", "Yuqori"}, {"v3d.bottom", "Past"}, {"v3d.front", "Old"}, {"v3d.back", "Orqa"}, {"v3d.left", "Chap"}, {"v3d.right", "O'ng"},
        {"v3d.fit", "Hammasi"}, {"v3d.persp", "Perspektiva"}, {"v3d.ortho", "Orto"}, {"v3d.style", "Uslub"}, {"v3d.cut", "Kesim"},
        {"v3d.vis", "Toifalar"}, {"v3d.shot", "Rasm"}, {"v3d.s.shaded", "Bo'yalgan"}, {"v3d.s.edges", "Bo'yalgan + qirralar"},
        {"v3d.s.wire", "Sim karkas"}, {"v3d.s.xray", "Rentgen"}, {"v3d.showall", "Hammasini ko'rsatish"}, {"v3d.isolate", "Ajratish"},
        {"v3d.hide", "Yashirish"}, {"v3d.center", "Shu yerga markazlash"}, {"v3d.hint", "Chap — aylantirish · o'rta/o'ng — surish · g'ildirak — yaqinlashtirish"},
        {"v3d.home", "Boshlang'ich ko'rinish"}};
    auto it = def.find(k);
    return it != def.end() ? QString::fromUtf8(it->second) : QString::fromLatin1(k);
}

void Model3DView::setDark(bool dark) {
    dark_ = dark;
    update();
}

// ---------------------------------------------------------------- tugmalar paneli
void Model3DView::buildUi() {
    bar_ = new QWidget(this);
    bar_->setObjectName("v3dBar");
    bar_->setAttribute(Qt::WA_StyledBackground, true);
    bar_->setStyleSheet("#v3dBar { background: rgba(20,24,32,170); border-radius: 6px; } "
                        "#v3dBar QToolButton { color: #e8ecf2; padding: 3px 7px; border: none; border-radius: 4px; } "
                        "#v3dBar QToolButton:hover { background: rgba(255,255,255,40); } "
                        "#v3dBar QToolButton:checked { background: rgba(30,140,255,140); }");
    auto* h = new QHBoxLayout(bar_);
    h->setContentsMargins(4, 3, 4, 3);
    h->setSpacing(2);
    auto mk = [&](const QString& name) {
        auto* b = new QToolButton(bar_);
        b->setObjectName(name);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        h->addWidget(b);
        return b;
    };
    bFit_ = mk("v3dFit");
    connect(bFit_, &QToolButton::clicked, this, [this] { fit(); });
    bProj_ = mk("v3dProj");
    bProj_->setCheckable(true);
    connect(bProj_, &QToolButton::toggled, this, [this](bool on) { setOrtho(on); });
    bStyle_ = mk("v3dStyle");
    bStyle_->setPopupMode(QToolButton::InstantPopup);
    auto* sm = new QMenu(bStyle_);
    bStyle_->setMenu(sm);
    connect(sm, &QMenu::aboutToShow, this, [this, sm] {
        sm->clear();
        const char* keys[4] = {"v3d.s.shaded", "v3d.s.edges", "v3d.s.wire", "v3d.s.xray"};
        for (int i = 0; i < 4; ++i) {
            auto* a = sm->addAction(tr_(keys[i]));
            a->setCheckable(true);
            a->setChecked(style_ == i);
            connect(a, &QAction::triggered, this, [this, i] { setStyle(static_cast<Style>(i)); });
        }
    });
    bVis_ = mk("v3dVis");
    bVis_->setPopupMode(QToolButton::InstantPopup);
    auto* vm = new QMenu(bVis_);
    bVis_->setMenu(vm);
    connect(vm, &QMenu::aboutToShow, this, [this, vm] {
        vm->clear();
        for (const auto& c : categories()) {
            auto* a = vm->addAction(c.isEmpty() ? QStringLiteral("—") : c);
            a->setCheckable(true);
            a->setChecked(!hiddenCats_.count(c));
            connect(a, &QAction::toggled, this, [this, c](bool on) { setCategoryVisible(c, on); });
        }
        vm->addSeparator();
        connect(vm->addAction(tr_("v3d.showall")), &QAction::triggered, this, [this] { showAll(); });
    });
    bCut_ = mk("v3dCut");
    bCut_->setCheckable(true);
    cut_ = new QSlider(Qt::Horizontal, bar_);
    cut_->setObjectName("v3dCutSlider");
    cut_->setRange(0, 1000);
    cut_->setValue(1000);
    cut_->setFixedWidth(120);
    cut_->setVisible(false);
    h->addWidget(cut_);
    connect(bCut_, &QToolButton::toggled, this, [this](bool on) {
        cut_->setVisible(on);
        bar_->adjustSize();
        setClip(on ? cut_->value() / 1000.0 : 1.0);
    });
    connect(cut_, &QSlider::valueChanged, this, [this](int v) { if (bCut_->isChecked()) setClip(v / 1000.0); });
    bShot_ = mk("v3dShot");
    connect(bShot_, &QToolButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr_("v3d.shot"), "model3d.png", "PNG (*.png)");
        if (!f.isEmpty()) snapshot().save(f);
    });
    retranslateUi();
    bar_->move(8, 8);
}

void Model3DView::retranslateUi() {
    if (!bar_) return;
    bFit_->setText(QString::fromUtf8("\xE2\xA4\xA2 ") + tr_("v3d.fit"));
    bProj_->setText(ortho_ ? tr_("v3d.ortho") : tr_("v3d.persp"));
    bStyle_->setText(tr_("v3d.style") + QString::fromUtf8(" \xE2\x96\xBE"));
    bVis_->setText(tr_("v3d.vis") + QString::fromUtf8(" \xE2\x96\xBE"));
    bCut_->setText(QString::fromUtf8("\xE2\x9C\x82 ") + tr_("v3d.cut"));
    bShot_->setText(tr_("v3d.shot"));
    bar_->adjustSize();
}

// ---------------------------------------------------------------- sahna
void Model3DView::beginScene(const QString& title, const QString& subtitle) {
    tri_.clear();
    line_.clear();
    edge_.clear();
    labels_.clear();
    objs_.clear();
    cur_ = -1;
    faces_ = 0;
    sel_ = -1;
    haveOrigin_ = false;
    title_ = title;
    subtitle_ = subtitle;
    dirty_ = true;
}

void Model3DView::beginObject(const QString& name, const QString& category, const QStringList& props) {
    Obj o;
    o.name = name;
    o.category = category;
    o.props = props;
    o.t0 = o.t1 = tri_.size();
    o.l0 = o.l1 = line_.size();
    o.e0 = o.e1 = edge_.size();
    objs_.push_back(o);
    cur_ = static_cast<int>(objs_.size()) - 1;
}

void Model3DView::endObject() {
    if (cur_ >= 0) {
        auto& o = objs_[static_cast<std::size_t>(cur_)];
        o.t1 = tri_.size();
        o.l1 = line_.size();
        o.e1 = edge_.size();
    }
    cur_ = -1;
}

int Model3DView::curObj() { return cur_; }

QVector3D Model3DView::L(const P3& p) const { return QVector3D(float(p.x - ox_), float(p.y - oy_), float(p.z - oz_)); }

void Model3DView::tri(const QVector3D& a, const QVector3D& b, const QVector3D& c, const QColor& col, int alpha) {
    QVector3D n = QVector3D::crossProduct(b - a, c - a);
    if (n.lengthSquared() < 1e-20f) return;
    n.normalize();
    const float id = float(curObj());
    const float r = float(col.redF()), g = float(col.greenF()), bl = float(col.blueF()), al = float(alpha) / 255.f;
    for (const QVector3D* v : {&a, &b, &c}) {
        tri_.insert(tri_.end(), {v->x(), v->y(), v->z(), n.x(), n.y(), n.z(), r, g, bl, al, id});
    }
}

void Model3DView::seg(std::vector<float>& buf, const QVector3D& a, const QVector3D& b, const QColor& col, int alpha) {
    const float id = float(curObj());
    const float r = float(col.redF()), g = float(col.greenF()), bl = float(col.blueF()), al = float(alpha) / 255.f;
    buf.insert(buf.end(), {a.x(), a.y(), a.z(), r, g, bl, al, id});
    buf.insert(buf.end(), {b.x(), b.y(), b.z(), r, g, bl, al, id});
}

void Model3DView::polygonTris(const std::vector<QVector3D>& outer, const std::vector<std::vector<QVector3D>>& holes, const QColor& c, int alpha, bool flip) {
    if (outer.size() < 3) return;
    // Newell normali -> proyeksiya tekisligi
    QVector3D n(0, 0, 0);
    for (std::size_t i = 0; i < outer.size(); ++i) {
        const auto& a = outer[i];
        const auto& b = outer[(i + 1) % outer.size()];
        n += QVector3D((a.y() - b.y()) * (a.z() + b.z()), (a.z() - b.z()) * (a.x() + b.x()), (a.x() - b.x()) * (a.y() + b.y()));
    }
    const int ax = std::abs(n.x()) >= std::abs(n.y()) && std::abs(n.x()) >= std::abs(n.z()) ? 0 : (std::abs(n.y()) >= std::abs(n.z()) ? 1 : 2);
    auto P2 = [&](const QVector3D& v) { return ax == 0 ? QPointF(v.y(), v.z()) : ax == 1 ? QPointF(v.z(), v.x()) : QPointF(v.x(), v.y()); };
    std::vector<QVector3D> V = outer;
    // teshiklarni ko'prik bilan ulash (eng o'ngdagi teshik uchi -> eng yaqin tashqi uch)
    std::vector<std::vector<QVector3D>> hs = holes;
    std::sort(hs.begin(), hs.end(), [&](const auto& a, const auto& b) {
        auto mx = [&](const auto& h) { double m = -1e300; for (const auto& v : h) m = std::max(m, P2(v).x()); return m; };
        return mx(a) > mx(b);
    });
    for (const auto& h : hs) {
        if (h.size() < 3) continue;
        std::size_t hi = 0;
        for (std::size_t i = 1; i < h.size(); ++i)
            if (P2(h[i]).x() > P2(h[hi]).x()) hi = i;
        const QPointF hp = P2(h[hi]);
        std::size_t best = 0;
        double bd = 1e300;
        for (std::size_t i = 0; i < V.size(); ++i) {
            const QPointF q = P2(V[i]);
            if (q.x() < hp.x() - 1e-9) continue;
            const double d = std::hypot(q.x() - hp.x(), q.y() - hp.y());
            if (d < bd) { bd = d; best = i; }
        }
        if (bd > 1e299)
            for (std::size_t i = 0; i < V.size(); ++i) {
                const QPointF q = P2(V[i]);
                const double d = std::hypot(q.x() - hp.x(), q.y() - hp.y());
                if (d < bd) { bd = d; best = i; }
            }
        // teshik qarama-qarshi yo'nalishda bo'lishi kerak
        double ao = 0, ah = 0;
        for (std::size_t i = 0; i < V.size(); ++i) { const QPointF a = P2(V[i]), b = P2(V[(i + 1) % V.size()]); ao += a.x() * b.y() - b.x() * a.y(); }
        for (std::size_t i = 0; i < h.size(); ++i) { const QPointF a = P2(h[i]), b = P2(h[(i + 1) % h.size()]); ah += a.x() * b.y() - b.x() * a.y(); }
        std::vector<QVector3D> hh = h;
        if ((ao > 0) == (ah > 0)) {
            std::reverse(hh.begin(), hh.end());
            hi = hh.size() - 1 - hi;
        }
        std::vector<QVector3D> nv(V.begin(), V.begin() + static_cast<long>(best) + 1);
        for (std::size_t k = 0; k <= hh.size(); ++k) nv.push_back(hh[(hi + k) % hh.size()]);
        nv.push_back(V[best]);
        nv.insert(nv.end(), V.begin() + static_cast<long>(best) + 1, V.end());
        V.swap(nv);
    }
    std::vector<QPointF> p2;
    for (const auto& v : V) p2.push_back(P2(v));
    for (const auto& t : earclip(p2)) {
        if (flip) tri(V[t[0]], V[t[2]], V[t[1]], c, alpha);
        else tri(V[t[0]], V[t[1]], V[t[2]], c, alpha);
    }
}

void Model3DView::addPrism(const std::vector<P3>& o, const std::vector<std::vector<P3>>& holes, double z0, double z1, const QColor& c, int alpha) {
    if (o.size() < 3) return;
    if (!haveOrigin_) { ox_ = o[0].x; oy_ = o[0].y; oz_ = z0; haveOrigin_ = true; }
    const bool autoObj = cur_ < 0;
    if (autoObj) beginObject({}, {});
    auto ring = [&](const std::vector<P3>& r, double z) {
        std::vector<QVector3D> v;
        for (const auto& q : r) v.push_back(L({q.x, q.y, z}));
        return v;
    };
    const QColor edgeC = dark_ ? QColor(15, 18, 24) : QColor(40, 44, 52);
    const int ea = alpha < 255 ? std::max(40, alpha / 2) : 200;
    auto sides = [&](const std::vector<P3>& r, bool inner) {
        const auto b = ring(r, z0), t = ring(r, z1);
        const std::size_t n = r.size();
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t j = (i + 1) % n;
            const QColor cc = inner ? c.darker(112) : c;
            tri(b[i], b[j], t[j], cc, alpha);
            tri(b[i], t[j], t[i], cc, alpha);
            ++faces_;
            seg(edge_, b[i], b[j], edgeC, ea);
            seg(edge_, t[i], t[j], edgeC, ea);
            // vertikal qirra — faqat sezilarli burilishda (aylana ustunlar yo'l-yo'l bo'lmasin)
            const P3& pa = r[(i + n - 1) % n];
            const P3& pb = r[i];
            const P3& pc = r[j];
            const double ux = pb.x - pa.x, uy = pb.y - pa.y, vx = pc.x - pb.x, vy = pc.y - pb.y;
            const double cosang = (ux * vx + uy * vy) / std::max(1e-12, std::hypot(ux, uy) * std::hypot(vx, vy));
            if (cosang < 0.985) seg(edge_, b[i], t[i], edgeC, ea);
        }
    };
    sides(o, false);
    for (const auto& h : holes) sides(h, true);
    std::vector<std::vector<QVector3D>> hb, ht;
    for (const auto& h : holes) { hb.push_back(ring(h, z0)); ht.push_back(ring(h, z1)); }
    polygonTris(ring(o, z1), ht, c.lighter(108), alpha, false);
    polygonTris(ring(o, z0), hb, c.darker(110), alpha, true);
    faces_ += 2;
    if (autoObj) endObject();
}

void Model3DView::addFace(const std::vector<P3>& pts, const QColor& c, int alpha, bool outline) {
    if (pts.size() < 3) return;
    if (!haveOrigin_) { ox_ = pts[0].x; oy_ = pts[0].y; oz_ = pts[0].z; haveOrigin_ = true; }
    const bool autoObj = cur_ < 0;
    if (autoObj) beginObject({}, {});
    std::vector<QVector3D> v;
    for (const auto& q : pts) v.push_back(L(q));
    polygonTris(v, {}, c, alpha, false);
    if (outline) {
        const QColor ec = dark_ ? QColor(15, 18, 24) : QColor(40, 44, 52);
        for (std::size_t i = 0; i < v.size(); ++i) seg(edge_, v[i], v[(i + 1) % v.size()], ec, 160);
    }
    ++faces_;
    if (autoObj) endObject();
}

void Model3DView::addPolyline(const std::vector<P3>& pts, const QColor& c, double, bool closed) {
    if (pts.size() < 2) return;
    if (!haveOrigin_) { ox_ = pts[0].x; oy_ = pts[0].y; oz_ = pts[0].z; haveOrigin_ = true; }
    const bool autoObj = cur_ < 0;
    if (autoObj) beginObject({}, {});
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) seg(line_, L(pts[i]), L(pts[i + 1]), c, 255);
    if (closed && pts.size() > 2) seg(line_, L(pts.back()), L(pts.front()), c, 255);
    ++faces_;
    if (autoObj) endObject();
}

void Model3DView::addTube(const std::vector<P3>& pts0, double r, const QColor& c, bool closed, int sides) {
    if (pts0.size() < 2 || r <= 0) return;
    if (!haveOrigin_) { ox_ = pts0[0].x; oy_ = pts0[0].y; oz_ = pts0[0].z; haveOrigin_ = true; }
    const bool autoObj = cur_ < 0;
    if (autoObj) beginObject({}, {});
    std::vector<P3> pts = pts0;
    if (closed) pts.push_back(pts0.front());
    const float id = float(curObj());
    const float cr = float(c.redF()), cg = float(c.greenF()), cb = float(c.blueF());
    for (std::size_t s = 0; s + 1 < pts.size(); ++s) {
        const QVector3D a = L(pts[s]), b = L(pts[s + 1]);
        QVector3D ax = b - a;
        if (ax.lengthSquared() < 1e-14f) continue;
        ax.normalize();
        const QVector3D ref = std::abs(ax.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0);
        const QVector3D u = QVector3D::crossProduct(ax, ref).normalized(), v = QVector3D::crossProduct(ax, u);
        for (int k = 0; k < sides; ++k) {
            const double t0 = 2 * 3.14159265358979 * k / sides, t1 = 2 * 3.14159265358979 * (k + 1) / sides;
            const QVector3D n0 = u * float(std::cos(t0)) + v * float(std::sin(t0)), n1 = u * float(std::cos(t1)) + v * float(std::sin(t1));
            const QVector3D p00 = a + n0 * float(r), p01 = a + n1 * float(r), p10 = b + n0 * float(r), p11 = b + n1 * float(r);
            auto put = [&](const QVector3D& p, const QVector3D& n) { tri_.insert(tri_.end(), {p.x(), p.y(), p.z(), n.x(), n.y(), n.z(), cr, cg, cb, 1.f, id}); };
            put(p00, n0); put(p10, n0); put(p11, n1);
            put(p00, n0); put(p11, n1); put(p01, n1);
        }
    }
    ++faces_;
    if (autoObj) endObject();
}

void Model3DView::addTriangles(const civil::DriveScene& sc, double alpha) {
    if (sc.tris.empty() && sc.segs.empty()) return;
    if (!haveOrigin_) {
        const auto& p = sc.tris.empty() ? sc.segs.front().a : sc.tris.front().p[0];
        ox_ = p.x; oy_ = p.y; oz_ = p.z;
        haveOrigin_ = true;
    }
    const bool autoObj = cur_ < 0;
    if (autoObj) beginObject({}, {});
    const int a = std::clamp(int(alpha * 255), 10, 255);
    for (const auto& t : sc.tris) {
        const QColor c(int((t.rgb >> 16) & 255), int((t.rgb >> 8) & 255), int(t.rgb & 255));
        tri(L({t.p[0].x, t.p[0].y, t.p[0].z}), L({t.p[1].x, t.p[1].y, t.p[1].z}), L({t.p[2].x, t.p[2].y, t.p[2].z}), c, a);
    }
    for (const auto& s : sc.segs) {
        const QColor c(int((s.rgb >> 16) & 255), int((s.rgb >> 8) & 255), int(s.rgb & 255));
        seg(line_, L({s.a.x, s.a.y, s.a.z}), L({s.b.x, s.b.y, s.b.z}), c, 255);
    }
    faces_ += static_cast<int>(sc.tris.size() / 2 + 1);
    if (autoObj) endObject();
}

void Model3DView::addLabel(const P3& at, const QString& text) {
    if (!haveOrigin_) { ox_ = at.x; oy_ = at.y; oz_ = at.z; haveOrigin_ = true; }
    labels_.push_back({L(at), text});
}

void Model3DView::endScene(bool keepView) {
    if (cur_ != -1) endObject();
    // obyektlar chegaralari
    smin_ = QVector3D(1e30f, 1e30f, 1e30f);
    smax_ = QVector3D(-1e30f, -1e30f, -1e30f);
    for (auto& o : objs_) {
        auto grow = [&](const std::vector<float>& buf, std::size_t a, std::size_t b, int stride) {
            for (std::size_t i = a; i < b; i += static_cast<std::size_t>(stride)) {
                const QVector3D p(buf[i], buf[i + 1], buf[i + 2]);
                o.bmin = QVector3D(std::min(o.bmin.x(), p.x()), std::min(o.bmin.y(), p.y()), std::min(o.bmin.z(), p.z()));
                o.bmax = QVector3D(std::max(o.bmax.x(), p.x()), std::max(o.bmax.y(), p.y()), std::max(o.bmax.z(), p.z()));
            }
        };
        grow(tri_, o.t0, o.t1, kTriStride);
        grow(line_, o.l0, o.l1, kLineStride);
        grow(edge_, o.e0, o.e1, kLineStride);
        if (o.bmin.x() <= o.bmax.x()) {
            smin_ = QVector3D(std::min(smin_.x(), o.bmin.x()), std::min(smin_.y(), o.bmin.y()), std::min(smin_.z(), o.bmin.z()));
            smax_ = QVector3D(std::max(smax_.x(), o.bmax.x()), std::max(smax_.y(), o.bmax.y()), std::max(smax_.z(), o.bmax.z()));
        }
    }
    for (const auto& l : labels_) {
        smin_ = QVector3D(std::min(smin_.x(), l.at.x()), std::min(smin_.y(), l.at.y()), std::min(smin_.z(), l.at.z()));
        smax_ = QVector3D(std::max(smax_.x(), l.at.x()), std::max(smax_.y(), l.at.y()), std::max(smax_.z(), l.at.z()));
    }
    if (smin_.x() > smax_.x()) { smin_ = QVector3D(0, 0, 0); smax_ = QVector3D(1, 1, 1); }
    dirty_ = true;
    static bool first = true;
    (void)first;
    if (!keepView || !fitted_) resetView();
    if (!objs_.empty()) fitted_ = true;
    update();
}

// ---------------------------------------------------------------- ko'rinish
QVector3D Model3DView::dir() const {
    const double y = yaw_ * kDeg, p = pitch_ * kDeg;
    return QVector3D(float(std::sin(y) * std::cos(p)), float(std::cos(y) * std::cos(p)), float(-std::sin(p)));
}
QVector3D Model3DView::eye() const { return target_ - dir() * float(dist_); }
QVector3D Model3DView::cameraDir() const { return dir(); }

QMatrix4x4 Model3DView::viewMatrix() const {
    QMatrix4x4 m;
    m.lookAt(eye(), target_, QVector3D(0, 0, 1));
    return m;
}
QMatrix4x4 Model3DView::projMatrix() const {
    QMatrix4x4 m;
    const float a = float(std::max(1, width())) / float(std::max(1, height()));
    const float R = (smax_ - smin_).length();
    const float farP = float(dist_) + R * 4 + 10;
    if (ortho_) {
        const float h = float(dist_ * std::tan(fov_ * kDeg / 2));
        m.ortho(-h * a, h * a, -h, h, -farP, farP);
    } else {
        m.perspective(float(fov_), a, std::max(0.01f, float(dist_) * 0.002f), farP);
    }
    return m;
}

void Model3DView::resetView() {
    yaw_ = 45;
    pitch_ = 30;
    fit();
}

void Model3DView::fit() {
    // ko'rinadigan obyektlar chegarasi
    QVector3D a(1e30f, 1e30f, 1e30f), b(-1e30f, -1e30f, -1e30f);
    for (const auto& o : objs_) {
        if (o.hidden || hiddenCats_.count(o.category) || o.bmin.x() > o.bmax.x()) continue;
        a = QVector3D(std::min(a.x(), o.bmin.x()), std::min(a.y(), o.bmin.y()), std::min(a.z(), o.bmin.z()));
        b = QVector3D(std::max(b.x(), o.bmax.x()), std::max(b.y(), o.bmax.y()), std::max(b.z(), o.bmax.z()));
    }
    if (a.x() > b.x()) { a = smin_; b = smax_; }
    target_ = (a + b) * 0.5f;
    const double R = std::max(0.5, double((b - a).length()) / 2);
    const double asp = double(std::max(1, width())) / double(std::max(1, height()));
    const double half = std::atan(std::tan(fov_ * kDeg / 2) * std::min(1.0, asp));   // tor tomon bo'yicha
    dist_ = R / std::sin(half) * 1.05;
    update();
}

void Model3DView::setView(View v) {
    userMoved_ = v != Home;
    double y = yaw_, p = pitch_;
    switch (v) {
    case Home: y = 45; p = 30; break;
    case Top: y = 0; p = 89.9; break;
    case Bottom: y = 0; p = -89.9; break;
    case Front: y = 0; p = 0; break;
    case Back: y = 180; p = 0; break;
    case Left: y = 90; p = 0; break;
    case Right: y = 270; p = 0; break;
    case IsoSW: y = 45; p = 35.264; break;
    case IsoSE: y = 315; p = 35.264; break;
    case IsoNE: y = 225; p = 35.264; break;
    case IsoNW: y = 135; p = 35.264; break;
    }
    // qisqa animatsiya (eng qisqa yo'l bilan)
    const double y0 = yaw_, p0 = pitch_;
    double dy = std::fmod(y - y0 + 540.0, 360.0) - 180.0;
    auto* an = new QVariantAnimation(this);
    an->setDuration(isVisible() ? 260 : 0);
    an->setStartValue(0.0);
    an->setEndValue(1.0);
    connect(an, &QVariantAnimation::valueChanged, this, [this, y0, p0, dy, p](const QVariant& t) {
        const double k = t.toDouble();
        yaw_ = y0 + dy * k;
        pitch_ = p0 + (p - p0) * k;
        update();
    });
    connect(an, &QVariantAnimation::finished, this, [this, y0, dy, p] {
        yaw_ = std::fmod(y0 + dy + 360.0, 360.0);
        pitch_ = p;
        update();
    });
    if (!isVisible()) {
        yaw_ = std::fmod(y0 + dy + 360.0, 360.0);
        pitch_ = p;
        delete an;
    } else an->start(QAbstractAnimation::DeleteWhenStopped);
    if (v == Home) fit();
}

void Model3DView::setStyle(Style s) {
    style_ = s;
    dirty_ = true;
    update();
}
void Model3DView::setOrtho(bool on) {
    ortho_ = on;
    if (bProj_ && bProj_->isChecked() != on) { const QSignalBlocker b(bProj_); bProj_->setChecked(on); }
    retranslateUi();
    update();
}
void Model3DView::setClip(double f) {
    clip_ = std::clamp(f, 0.0, 1.0);
    update();
}
QStringList Model3DView::categories() const {
    QStringList out;
    std::set<QString> seen;
    for (const auto& o : objs_)
        if (seen.insert(o.category).second) out << o.category;
    std::sort(out.begin(), out.end());
    return out;
}
void Model3DView::setCategoryVisible(const QString& c, bool on) {
    if (on) hiddenCats_.erase(c);
    else hiddenCats_.insert(c);
    dirty_ = true;
    update();
}
void Model3DView::isolateSelected() {
    if (sel_ < 0) return;
    for (int i = 0; i < static_cast<int>(objs_.size()); ++i) objs_[static_cast<std::size_t>(i)].hidden = i != sel_;
    dirty_ = true;
    fit();
}
void Model3DView::hideSelected() {
    if (sel_ < 0) return;
    objs_[static_cast<std::size_t>(sel_)].hidden = true;
    sel_ = -1;
    dirty_ = true;
    update();
}
void Model3DView::showAll() {
    for (auto& o : objs_) o.hidden = false;
    hiddenCats_.clear();
    dirty_ = true;
    update();
}

QString Model3DView::selectionInfo() const {
    if (sel_ < 0 || sel_ >= static_cast<int>(objs_.size())) return {};
    const auto& o = objs_[static_cast<std::size_t>(sel_)];
    QStringList l;
    if (!o.name.isEmpty()) l << o.name;
    if (!o.category.isEmpty() && o.category != o.name) l << o.category;
    l << o.props;
    return l.join('\n');
}

// ---------------------------------------------------------------- GPU
void Model3DView::initializeGL() {
    initializeOpenGLFunctions();
    prog_ = std::make_unique<QOpenGLShaderProgram>();
    const bool es = context()->isOpenGLES();
    const QByteArray vh = es ? "#version 100\n" : "#version 120\n";
    const QByteArray fh = es ? "#version 100\nprecision mediump float;\n" : "#version 120\n";
    prog_->addShaderFromSourceCode(QOpenGLShader::Vertex, vh + kVS);
    prog_->addShaderFromSourceCode(QOpenGLShader::Fragment, fh + kFS);
    prog_->bindAttributeLocation("aPos", 0);
    prog_->bindAttributeLocation("aNor", 1);
    prog_->bindAttributeLocation("aCol", 2);
    prog_->bindAttributeLocation("aId", 3);
    glReady_ = prog_->link();
    for (auto* b : {&vOpaque_, &vTrans_, &vLine_, &vEdge_}) {
        b->create();
        b->setUsagePattern(QOpenGLBuffer::StaticDraw);
    }
    dirty_ = true;
}

void Model3DView::resizeGL(int, int) {
    if (!userMoved_ && !objs_.empty()) fit();   // oyna o'lchami o'zgardi, foydalanuvchi hali aylantirmagan
}

void Model3DView::upload() {
    std::vector<float> op, tr, ln, ed;
    std::vector<char> vis(objs_.size(), 1);
    for (std::size_t i = 0; i < objs_.size(); ++i) vis[i] = !(objs_[i].hidden || hiddenCats_.count(objs_[i].category));
    auto visible = [&](float id) {
        const auto k = static_cast<std::size_t>(id);
        return k < vis.size() && vis[k];
    };
    for (std::size_t i = 0; i + kTriStride * 3 <= tri_.size(); i += kTriStride * 3) {
        if (!visible(tri_[i + 10])) continue;
        const bool transp = tri_[i + 9] < 0.999f || tri_[i + kTriStride + 9] < 0.999f;
        auto& dst = (transp || style_ == XRay) ? tr : op;
        dst.insert(dst.end(), tri_.begin() + static_cast<long>(i), tri_.begin() + static_cast<long>(i + kTriStride * 3));
    }
    for (std::size_t i = 0; i + kLineStride <= line_.size(); i += kLineStride)
        if (visible(line_[i + 7])) ln.insert(ln.end(), line_.begin() + static_cast<long>(i), line_.begin() + static_cast<long>(i + kLineStride));
    for (std::size_t i = 0; i + kLineStride <= edge_.size(); i += kLineStride)
        if (visible(edge_[i + 7])) ed.insert(ed.end(), edge_.begin() + static_cast<long>(i), edge_.begin() + static_cast<long>(i + kLineStride));
    auto put = [](QOpenGLBuffer& b, const std::vector<float>& v) {
        b.bind();
        b.allocate(v.empty() ? nullptr : v.data(), static_cast<int>(v.size() * sizeof(float)));
        b.release();
    };
    put(vOpaque_, op);
    put(vTrans_, tr);
    put(vLine_, ln);
    put(vEdge_, ed);
    nOpaque_ = static_cast<int>(op.size() / kTriStride);
    nTrans_ = static_cast<int>(tr.size() / kTriStride);
    nLine_ = static_cast<int>(ln.size() / kLineStride);
    nEdge_ = static_cast<int>(ed.size() / kLineStride);
    dirty_ = false;
}

void Model3DView::paintGL() {
    const QColor bg = dark_ ? QColor(27, 31, 39) : QColor(236, 239, 243);
    glClearColor(float(bg.redF()), float(bg.greenF()), float(bg.blueF()), 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (glReady_ && !objs_.empty()) {
        if (dirty_) upload();
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        prog_->bind();
        const QMatrix4x4 V = viewMatrix(), P = projMatrix();
        prog_->setUniformValue("uMvp", P * V);
        prog_->setUniformValue("uNorm", V.normalMatrix());
        prog_->setUniformValue("uSel", float(sel_));
        prog_->setUniformValue("uLight", QVector3D(-0.35f, 0.55f, 0.76f).normalized());
        const double zr = double(smax_.z() - smin_.z());
        prog_->setUniformValue("uClipZ", clip_ >= 0.999 ? 1e30f : float(double(smin_.z()) + zr * clip_ + 1e-4));
        prog_->setUniformValue("uOverride", QVector4D(0, 0, 0, 0));
        auto drawTris = [&](QOpenGLBuffer& b, int n, float alphaMul) {
            if (n <= 0) return;
            b.bind();
            prog_->enableAttributeArray(0);
            prog_->enableAttributeArray(1);
            prog_->enableAttributeArray(2);
            prog_->enableAttributeArray(3);
            prog_->setAttributeBuffer(0, GL_FLOAT, 0, 3, kTriStride * sizeof(float));
            prog_->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, kTriStride * sizeof(float));
            prog_->setAttributeBuffer(2, GL_FLOAT, 6 * sizeof(float), 4, kTriStride * sizeof(float));
            prog_->setAttributeBuffer(3, GL_FLOAT, 10 * sizeof(float), 1, kTriStride * sizeof(float));
            prog_->setUniformValue("uLit", 1.f);
            prog_->setUniformValue("uAlphaMul", alphaMul);
            glDrawArrays(GL_TRIANGLES, 0, n);
            prog_->disableAttributeArray(1);
            b.release();
        };
        auto drawLines = [&](QOpenGLBuffer& b, int n, float alphaMul) {
            if (n <= 0) return;
            b.bind();
            prog_->enableAttributeArray(0);
            prog_->enableAttributeArray(2);
            prog_->enableAttributeArray(3);
            prog_->disableAttributeArray(1);
            glVertexAttrib3f(1, 0.f, 0.f, 1.f);
            prog_->setAttributeBuffer(0, GL_FLOAT, 0, 3, kLineStride * sizeof(float));
            prog_->setAttributeBuffer(2, GL_FLOAT, 3 * sizeof(float), 4, kLineStride * sizeof(float));
            prog_->setAttributeBuffer(3, GL_FLOAT, 7 * sizeof(float), 1, kLineStride * sizeof(float));
            prog_->setUniformValue("uLit", 0.f);
            prog_->setUniformValue("uAlphaMul", alphaMul);
            glDrawArrays(GL_LINES, 0, n);
            b.release();
        };
        if (style_ != Wireframe) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.f, 1.f);
            drawTris(vOpaque_, nOpaque_, 1.f);
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        if (style_ != Shaded) drawLines(vEdge_, nEdge_, style_ == Wireframe ? 1.f : 0.85f);
        drawLines(vLine_, nLine_, 1.f);
        if (style_ != Wireframe && nTrans_ > 0) {
            glDepthMask(GL_FALSE);
            drawTris(vTrans_, nTrans_, style_ == XRay ? 0.28f : 1.f);
            glDepthMask(GL_TRUE);
        }
        if (style_ == Wireframe && nOpaque_ + nTrans_ > 0 && nEdge_ == 0) {   // qirrasiz yuzalar (yer) — yengil soya
            glDepthMask(GL_FALSE);
            drawTris(vOpaque_, nOpaque_, 0.15f);
            glDepthMask(GL_TRUE);
        }
        for (int a = 0; a < 4; ++a) prog_->disableAttributeArray(a);
        prog_->release();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    drawOverlay(p);
}

// ---------------------------------------------------------------- overlay: sarlavha, yozuvlar, ViewCube, o'qlar, tanlov
void Model3DView::drawOverlay(QPainter& p) {
    const QColor fg = dark_ ? QColor(225, 230, 238) : QColor(30, 34, 42);
    const QColor dim = dark_ ? QColor(140, 150, 165) : QColor(110, 118, 130);
    if (objs_.empty() && labels_.empty()) {
        p.setPen(dim);
        p.setFont(scaled(p.font(), 1.15));
        p.drawText(rect().adjusted(40, 40, -40, -40), Qt::AlignCenter | Qt::TextWordWrap, empty_);
        return;
    }
    // yozuvlar
    const QMatrix4x4 M = projMatrix() * viewMatrix();
    if (labels_.size() <= 400) {
        p.setPen(fg);
        for (const auto& l : labels_) {
            const QVector4D c = M * QVector4D(l.at, 1.f);
            if (c.w() <= 0) continue;
            const QPointF s((c.x() / c.w() * 0.5 + 0.5) * width(), (0.5 - c.y() / c.w() * 0.5) * height());
            p.drawText(s + QPointF(4, -4), l.text);
        }
    }
    // sarlavha
    QFont f = p.font();
    p.setPen(fg);
    if (!title_.isEmpty()) {
        QFont b = f;
        b.setBold(true);
        p.setFont(b);
        p.drawText(QPointF(12, bar_->height() + 28), title_);
        p.setFont(f);
    }
    if (!subtitle_.isEmpty()) {
        p.setPen(dim);
        p.drawText(QRectF(12, bar_->height() + 34, width() * 0.6, 40), Qt::TextWordWrap, subtitle_);
    }
    // o'q uchligi (chap past)
    {
        QMatrix4x4 R = viewMatrix();
        R.setColumn(3, QVector4D(0, 0, 0, 1));
        const QPointF o(36, height() - 36);
        const struct { QVector3D v; QColor c; const char* t; } ax[3] = {
            {{1, 0, 0}, QColor(230, 80, 80), "X"}, {{0, 1, 0}, QColor(90, 200, 110), "Y"}, {{0, 0, 1}, QColor(80, 140, 240), "Z"}};
        for (const auto& a : ax) {
            const QVector3D v = R.map(a.v);
            const QPointF e = o + QPointF(v.x(), -v.y()) * 24;
            p.setPen(QPen(a.c, 2));
            p.drawLine(o, e);
            p.drawText(e + QPointF(3, 4), QString::fromLatin1(a.t));
        }
    }
    // tanlangan obyekt ma'lumoti (chap past, o'qlar ustida)
    const QString info = selectionInfo();
    if (!info.isEmpty()) {
        const QStringList lines = info.split('\n');
        const QFontMetrics fm(f);
        int w = 0;
        for (const auto& s : lines) w = std::max(w, fm.horizontalAdvance(s));
        const QRect box(70, height() - 18 - fm.height() * int(lines.size()) - 12, w + 20, fm.height() * int(lines.size()) + 12);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(15, 110, 220, 200));
        p.drawRoundedRect(box, 6, 6);
        p.setPen(Qt::white);
        for (int i = 0; i < lines.size(); ++i) p.drawText(QPointF(box.left() + 10, box.top() + 6 + fm.ascent() + i * fm.height()), lines[i]);
    }
    // yo'riqnoma (pastda o'rtada)
    p.setPen(dim);
    p.setFont(scaled(f, 0.85));
    p.drawText(QRectF(0, height() - 22, width() - 10, 20), Qt::AlignRight | Qt::AlignVCenter, tr_("v3d.hint"));
    p.setFont(f);
    drawCube(p);
}

void Model3DView::drawCube(QPainter& p) {
    const int S = 92;
    int top = 12;
    if (bar_ && bar_->geometry().right() > width() - S - 24) top = bar_->geometry().bottom() + 10;   // tor oynada — panel ostida
    cubeRect_ = QRect(width() - S - 14, top, S, S);
    homeRect_ = QRect(cubeRect_.left() - 6, cubeRect_.top() - 2, 22, 22);
    QMatrix4x4 R = viewMatrix();
    R.setColumn(3, QVector4D(0, 0, 0, 1));
    const QPointF c = cubeRect_.center();
    const double k = S * 0.27;
    auto pr = [&](const QVector3D& v, double* depth = nullptr) {
        const QVector3D w = R.map(v);
        if (depth) *depth = w.z();
        return c + QPointF(w.x() * k, -w.y() * k);
    };
    // yuzlar: 0 yuqori(+Z) 1 past(-Z) 2 old(-Y, janub) 3 orqa(+Y) 4 chap(-X) 5 o'ng(+X)
    const QVector3D nrm[6] = {{0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}};
    const char* lab[6] = {"v3d.top", "v3d.bottom", "v3d.front", "v3d.back", "v3d.left", "v3d.right"};
    struct F { double d; int i; };
    std::vector<F> fs;
    for (int i = 0; i < 6; ++i) {
        const QVector3D n = R.map(nrm[i]);
        if (n.z() > 0.02f) fs.push_back({n.z(), i});
    }
    std::sort(fs.begin(), fs.end(), [](const F& a, const F& b) { return a.d < b.d; });
    cubeFaces_.clear();
    // kompas halqasi
    p.setPen(QPen(dark_ ? QColor(90, 100, 118) : QColor(160, 168, 180), 1.4));
    p.setBrush(Qt::NoBrush);
    {
        QPainterPath ring;
        for (int i = 0; i <= 48; ++i) {
            const double a = i * 2 * 3.14159265358979 / 48;
            const QPointF q = pr(QVector3D(float(1.7 * std::cos(a)), float(1.7 * std::sin(a)), -1.f));
            if (i == 0) ring.moveTo(q); else ring.lineTo(q);
        }
        p.drawPath(ring);
        p.setPen(dark_ ? QColor(200, 210, 225) : QColor(60, 66, 78));
        const QPointF n = pr(QVector3D(0, 2.05f, -1.f));
        p.drawText(QRectF(n.x() - 8, n.y() - 8, 16, 16), Qt::AlignCenter, "N");
    }
    for (const auto& fc : fs) {
        const QVector3D n = nrm[fc.i];
        QVector3D u, v;
        if (std::abs(n.z()) > 0.5f) { u = QVector3D(1, 0, 0); v = QVector3D(0, 1, 0); }
        else if (std::abs(n.y()) > 0.5f) { u = QVector3D(1, 0, 0); v = QVector3D(0, 0, 1); }
        else { u = QVector3D(0, 1, 0); v = QVector3D(0, 0, 1); }
        std::vector<QPointF> poly{pr(n - u - v), pr(n + u - v), pr(n + u + v), pr(n - u + v)};
        const double shade = 0.55 + 0.45 * fc.d;
        const QColor base = dark_ ? QColor(int(70 * shade + 30), int(80 * shade + 30), int(98 * shade + 30)) : QColor(int(200 * shade + 40), int(206 * shade + 40), int(214 * shade + 40));
        p.setBrush(base);
        p.setPen(QPen(dark_ ? QColor(150, 165, 185) : QColor(120, 128, 140), 1));
        p.drawPolygon(poly.data(), int(poly.size()));
        if (fc.d > 0.35) {
            p.setPen(dark_ ? QColor(235, 240, 248) : QColor(30, 34, 42));
            const QFont of = p.font();
            QFont f = scaled(of, 0.78);
            f.setBold(true);
            p.setFont(f);
            const QPointF cc = pr(n);
            p.drawText(QRectF(cc.x() - 30, cc.y() - 9, 60, 18), Qt::AlignCenter, tr_(lab[fc.i]));
            p.setFont(of);
        }
        cubeFaces_.push_back({poly, fc.i});
    }
    // yuqori burchaklar (izometriya) — nuqtalar
    const QVector3D cor[4] = {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
    for (int i = 0; i < 4; ++i) {
        double d;
        const QPointF q = pr(cor[i], &d);
        if (d < -0.2) continue;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 140, 255, 150));
        p.drawEllipse(q, 4, 4);
        cubeFaces_.push_back({{q}, 6 + i});
    }
    // uy tugmasi
    p.setPen(dark_ ? QColor(200, 210, 225) : QColor(60, 66, 78));
    p.setBrush(Qt::NoBrush);
    QPainterPath h;
    const QRectF hr = homeRect_;
    h.moveTo(hr.left() + 4, hr.center().y());
    h.lineTo(hr.center().x(), hr.top() + 4);
    h.lineTo(hr.right() - 4, hr.center().y());
    h.moveTo(hr.left() + 7, hr.center().y() - 1);
    h.lineTo(hr.left() + 7, hr.bottom() - 4);
    h.lineTo(hr.right() - 7, hr.bottom() - 4);
    h.lineTo(hr.right() - 7, hr.center().y() - 1);
    p.drawPath(h);
}

int Model3DView::cubeHit(const QPoint& px) const {
    if (homeRect_.contains(px)) return 10;
    for (auto it = cubeFaces_.rbegin(); it != cubeFaces_.rend(); ++it) {
        if (it->second >= 6) {
            const QPointF q = it->first.front();
            if (std::hypot(q.x() - px.x(), q.y() - px.y()) <= 8) return it->second;
        }
    }
    for (auto it = cubeFaces_.rbegin(); it != cubeFaces_.rend(); ++it) {
        if (it->second >= 6) continue;
        QPolygonF poly;
        for (const auto& q : it->first) poly << q;
        if (poly.containsPoint(px, Qt::OddEvenFill)) return it->second;
    }
    return -1;
}

// ---------------------------------------------------------------- nur va tanlash
void Model3DView::rayFrom(const QPoint& px, QVector3D& o, QVector3D& d) const {
    const QMatrix4x4 inv = (projMatrix() * viewMatrix()).inverted();
    const float x = 2.f * float(px.x()) / float(std::max(1, width())) - 1.f, y = 1.f - 2.f * float(px.y()) / float(std::max(1, height()));
    const QVector4D n = inv * QVector4D(x, y, -1, 1), f = inv * QVector4D(x, y, 1, 1);
    const QVector3D a = n.toVector3DAffine(), b = f.toVector3DAffine();
    o = a;
    d = (b - a).normalized();
}

bool Model3DView::rayHit(const QPoint& px, QVector3D& hit, int* objOut) const {
    QVector3D o, d;
    rayFrom(px, o, d);
    const double zr = double(smax_.z() - smin_.z());
    const float clipZ = clip_ >= 0.999 ? 1e30f : float(double(smin_.z()) + zr * clip_ + 1e-4);
    float best = 1e30f;
    int bestObj = -1;
    for (int k = 0; k < static_cast<int>(objs_.size()); ++k) {
        const auto& ob = objs_[static_cast<std::size_t>(k)];
        if (ob.hidden || hiddenCats_.count(ob.category) || ob.t0 == ob.t1) continue;
        // AABB
        float t0 = 0, t1 = best;
        bool miss = false;
        for (int a = 0; a < 3 && !miss; ++a) {
            const float inv = std::abs(d[a]) < 1e-12f ? 1e12f : 1.f / d[a];
            float ta = (ob.bmin[a] - 1e-3f - o[a]) * inv, tb = (ob.bmax[a] + 1e-3f - o[a]) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) miss = true;
        }
        if (miss) continue;
        for (std::size_t i = ob.t0; i + kTriStride * 3 <= ob.t1; i += kTriStride * 3) {
            const QVector3D A(tri_[i], tri_[i + 1], tri_[i + 2]), B(tri_[i + kTriStride], tri_[i + kTriStride + 1], tri_[i + kTriStride + 2]),
                C(tri_[i + 2 * kTriStride], tri_[i + 2 * kTriStride + 1], tri_[i + 2 * kTriStride + 2]);
            const QVector3D e1 = B - A, e2 = C - A, pv = QVector3D::crossProduct(d, e2);
            const float det = QVector3D::dotProduct(e1, pv);
            if (std::abs(det) < 1e-12f) continue;
            const float inv = 1.f / det;
            const QVector3D tv = o - A;
            const float u = QVector3D::dotProduct(tv, pv) * inv;
            if (u < 0 || u > 1) continue;
            const QVector3D qv = QVector3D::crossProduct(tv, e1);
            const float v = QVector3D::dotProduct(d, qv) * inv;
            if (v < 0 || u + v > 1) continue;
            const float t = QVector3D::dotProduct(e2, qv) * inv;
            if (t > 1e-5f && t < best && (o + d * t).z() <= clipZ) {
                best = t;
                bestObj = k;
            }
        }
    }
    if (bestObj < 0) return false;
    hit = o + d * best;
    if (objOut) *objOut = bestObj;
    return true;
}

bool Model3DView::selectAt(const QPoint& px) {
    QVector3D h;
    int k = -1;
    const bool ok = rayHit(px, h, &k);
    sel_ = ok ? k : -1;
    update();
    if (ok) emit objectSelected(objs_[static_cast<std::size_t>(k)].name, objs_[static_cast<std::size_t>(k)].props);
    return ok;
}

QImage Model3DView::snapshot() { return grabFramebuffer(); }

// ---------------------------------------------------------------- navigatsiya
void Model3DView::orbit(double dYaw, double dPitch) {
    userMoved_ = true;
    const QVector3D P = havePivot_ ? pivot_ : target_;
    auto frame = [](double y, double p) {
        const QVector3D f(float(std::sin(y * kDeg) * std::cos(p * kDeg)), float(std::cos(y * kDeg) * std::cos(p * kDeg)), float(-std::sin(p * kDeg)));
        QVector3D r = QVector3D::crossProduct(f, QVector3D(0, 0, 1));
        if (r.lengthSquared() < 1e-12f) r = QVector3D(float(std::cos(y * kDeg)), float(-std::sin(y * kDeg)), 0);
        r.normalize();
        const QVector3D u = QVector3D::crossProduct(r, f);
        QMatrix4x4 m;   // ustunlar: r, f, u
        m.setColumn(0, QVector4D(r, 0));
        m.setColumn(1, QVector4D(f, 0));
        m.setColumn(2, QVector4D(u, 0));
        m.setColumn(3, QVector4D(0, 0, 0, 1));
        return m;
    };
    const double ny = yaw_ + dYaw, np = std::clamp(pitch_ + dPitch, -89.9, 89.9);
    const QMatrix4x4 Rm = frame(ny, np) * frame(yaw_, pitch_).transposed();
    const QVector3D e = eye();
    const QVector3D ne = P + Rm.map(e - P), nt = P + Rm.map(target_ - P);
    target_ = nt;
    yaw_ = std::fmod(ny + 360.0, 360.0);
    pitch_ = np;
    dist_ = (nt - ne).length();
    update();
}

void Model3DView::pan(const QPoint& dpx) {
    userMoved_ = true;
    const double h = 2 * dist_ * std::tan(fov_ * kDeg / 2);
    const double s = h / std::max(1, height());
    const QVector3D f = dir();
    QVector3D r = QVector3D::crossProduct(f, QVector3D(0, 0, 1));
    if (r.lengthSquared() < 1e-10f) r = QVector3D(float(std::cos(yaw_ * kDeg)), float(-std::sin(yaw_ * kDeg)), 0);
    r.normalize();
    const QVector3D u = QVector3D::crossProduct(r, f).normalized();
    target_ += (-r * float(dpx.x()) + u * float(dpx.y())) * float(s);
    update();
}

void Model3DView::zoomAt(const QPoint& px, double factor) {
    userMoved_ = true;
    QVector3D P;
    if (!rayHit(px, P)) {
        QVector3D o, d;
        rayFrom(px, o, d);
        // kamera yo'nalishiga perpendikulyar, nishondan o'tuvchi tekislik bilan kesishish
        const QVector3D f = dir();
        const float den = QVector3D::dotProduct(d, f);
        const float t = std::abs(den) > 1e-6f ? QVector3D::dotProduct(target_ - o, f) / den : float(dist_);
        P = o + d * std::max(0.f, t);
    }
    const double nd = std::clamp(dist_ * factor, 0.05, 1e7);
    const float k = float(nd / dist_);
    const QVector3D e = eye();
    const QVector3D ne = P + (e - P) * k;
    target_ = P + (target_ - P) * k;
    dist_ = (target_ - ne).length();
    update();
}

void Model3DView::mousePressEvent(QMouseEvent* e) {
    setFocus();
    press_ = last_ = e->position().toPoint();
    btn_ = e->button();
    dragged_ = false;
    // AutoCAD / Revit odati: Shift + o'rta — aylantirish; o'rta / o'ng / Shift + chap — surish
    const bool shiftMid = btn_ == Qt::MiddleButton && (e->modifiers() & Qt::ShiftModifier);
    panMode_ = !shiftMid && (btn_ == Qt::MiddleButton || btn_ == Qt::RightButton || (btn_ == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier)));
    havePivot_ = false;
    if ((btn_ == Qt::LeftButton || shiftMid) && !panMode_) {
        const int h = btn_ == Qt::LeftButton ? cubeHit(press_) : -1;
        if (h >= 0) {
            static const View fv[6] = {Top, Bottom, Front, Back, Left, Right};
            static const View cv[4] = {IsoSW, IsoSE, IsoNE, IsoNW};
            setView(h == 10 ? Home : (h < 6 ? fv[h] : cv[h - 6]));
            btn_ = Qt::NoButton;
            return;
        }
        QVector3D p;
        if (rayHit(press_, p)) { pivot_ = p; havePivot_ = true; }
    }
}

void Model3DView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    if (btn_ == Qt::NoButton) return;
    const QPoint d = pos - last_;
    if ((pos - press_).manhattanLength() > 3) dragged_ = true;
    if (!dragged_) return;
    if (panMode_) pan(d);
    else orbit(d.x() * 0.4, d.y() * 0.3);
    last_ = pos;
}

void Model3DView::mouseReleaseEvent(QMouseEvent* e) {
    const Qt::MouseButton b = btn_;
    btn_ = Qt::NoButton;
    if (!dragged_ && b == Qt::LeftButton && !panMode_) selectAt(e->position().toPoint());
}

void Model3DView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (cubeHit(e->position().toPoint()) >= 0) return;
    fit();
}

void Model3DView::wheelEvent(QWheelEvent* e) {
    const double steps = e->angleDelta().y() / 120.0;
    zoomAt(e->position().toPoint(), std::pow(0.85, steps));
}

void Model3DView::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_F: fit(); return;
    case Qt::Key_Escape: sel_ = -1; update(); return;
    case Qt::Key_H: hideSelected(); return;
    case Qt::Key_I: isolateSelected(); return;
    // klaviatura: strelkalar — aylantirish, +/- — yaqinlashtirish, W/A/S/D — yurish (bino ichida), Q/E — pastga/yuqoriga
    case Qt::Key_Left: orbit(-15, 0); return;
    case Qt::Key_Right: orbit(15, 0); return;
    case Qt::Key_Up: orbit(0, -10); return;
    case Qt::Key_Down: orbit(0, 10); return;
    case Qt::Key_Plus:
    case Qt::Key_Equal: zoomAt(rect().center(), 0.8); return;
    case Qt::Key_Minus: zoomAt(rect().center(), 1.25); return;
    case Qt::Key_W:
    case Qt::Key_S:
    case Qt::Key_A:
    case Qt::Key_D:
    case Qt::Key_Q:
    case Qt::Key_E: {
        const float step = float((e->modifiers() & Qt::ShiftModifier) ? 2.0 : 0.5);
        QVector3D f = dir();
        f.setZ(0);
        if (f.lengthSquared() < 1e-8f) f = QVector3D(float(std::sin(yaw_ * kDeg)), float(std::cos(yaw_ * kDeg)), 0);
        f.normalize();
        const QVector3D r = QVector3D::crossProduct(f, QVector3D(0, 0, 1)).normalized();
        QVector3D mv;
        switch (e->key()) {
        case Qt::Key_W: mv = f; break;
        case Qt::Key_S: mv = -f; break;
        case Qt::Key_A: mv = -r; break;
        case Qt::Key_D: mv = r; break;
        case Qt::Key_Q: mv = QVector3D(0, 0, -1); break;
        default: mv = QVector3D(0, 0, 1); break;
        }
        target_ += mv * step;   // ko'z va nishon birga siljiydi

        userMoved_ = true;
        update();
        return;
    }
    default: QOpenGLWidget::keyPressEvent(e);
    }
}

void Model3DView::contextMenuEvent(QContextMenuEvent* e) {
    if (dragged_) return;   // o'ng tugma bilan surildi
    selectAt(e->pos());
    QMenu m(this);
    QVector3D hit;
    const bool onObj = rayHit(e->pos(), hit);
    auto* iso = m.addAction(tr_("v3d.isolate"));
    auto* hide = m.addAction(tr_("v3d.hide"));
    iso->setEnabled(sel_ >= 0);
    hide->setEnabled(sel_ >= 0);
    auto* all = m.addAction(tr_("v3d.showall"));
    m.addSeparator();
    auto* ctr = m.addAction(tr_("v3d.center"));
    ctr->setEnabled(onObj);
    auto* fitA = m.addAction(tr_("v3d.fit"));
    QAction* r = m.exec(e->globalPos());
    if (r == iso) isolateSelected();
    else if (r == hide) hideSelected();
    else if (r == all) showAll();
    else if (r == ctr) { target_ = hit; update(); }
    else if (r == fitA) fit();
}

} // namespace asiacad::gui
