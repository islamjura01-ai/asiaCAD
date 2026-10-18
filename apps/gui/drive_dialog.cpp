#include "drive_dialog.h"

#include <asiacad/civil/road.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <cstdio>

namespace asiacad::gui {

namespace {
QString f1(double v) { return QString::number(v, 'f', 1); }
QString f2(double v) { return QString::number(v, 'f', 2); }
}  // namespace

// ---------------------------------------------------------------- profil va ko'rinish grafigi
class DriveProfile : public QWidget {
public:
    explicit DriveProfile(QWidget* parent) : QWidget(parent) { setMinimumHeight(130); }
    const civil::DrivePath* path = nullptr;
    const civil::SightResult* sight = nullptr;
    double s = 0, maxD = 400;
    QString title;
    std::function<void(double)> seek;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(20, 26, 44));
        if (!path || path->length() <= 0) return;
        const double L = path->length();
        double zmin = 1e18, zmax = -1e18;
        for (const auto& q : path->p) { zmin = std::min(zmin, q.z); zmax = std::max(zmax, q.z); }
        if (zmax - zmin < 1) { zmin -= 0.5; zmax += 0.5; }
        const QRectF R(46, 18, width() - 56, height() - 34);
        auto X = [&](double st) { return R.left() + st / L * R.width(); };
        auto Yz = [&](double z) { return R.bottom() - (z - zmin) / (zmax - zmin) * R.height() * 0.55 - R.height() * 0.4; };
        auto Yd = [&](double d) { return R.bottom() - std::min(d, maxD) / maxD * R.height() * 0.38; };
        p.setPen(QColor(150, 160, 185));
        QFont fn = font();
        fn.setPixelSize(10);
        p.setFont(fn);
        p.drawText(QPointF(6, 12), title);
        // ko'rinish masofasi (maydon) va talab
        if (sight && !sight->samples.empty()) {
            QPainterPath a;
            a.moveTo(X(sight->samples.front().s), R.bottom());
            for (const auto& sm : sight->samples) a.lineTo(X(sm.s), Yd(sm.total));
            a.lineTo(X(sight->samples.back().s), R.bottom());
            a.closeSubpath();
            p.fillPath(a, QColor(80, 160, 230, 70));
            for (const auto& is : sight->issues)
                p.fillRect(QRectF(X(is.s0) - 1, R.top(), std::max(2.0, X(is.s1) - X(is.s0) + 2), R.height()), QColor(232, 70, 80, 60));
            p.setPen(QPen(QColor(232, 70, 80), 1, Qt::DashLine));
            p.drawLine(QPointF(R.left(), Yd(sight->required)), QPointF(R.right(), Yd(sight->required)));
            p.drawText(QPointF(R.left() - 42, Yd(sight->required) + 3), f1(sight->required));
        }
        // profil
        QPolygonF pl;
        const std::size_t stp = std::max<std::size_t>(1, path->p.size() / 1500);
        for (std::size_t i = 0; i < path->p.size(); i += stp) pl << QPointF(X(path->s[i]), Yz(path->p[i].z));
        p.setPen(QPen(QColor(240, 190, 80), 1.6));
        p.drawPolyline(pl);
        p.setPen(QColor(150, 160, 185));
        p.drawText(QPointF(4, Yz(zmax) + 4), f2(zmax));
        p.drawText(QPointF(4, Yz(zmin) + 4), f2(zmin));
        // joriy joy
        p.setPen(QPen(QColor(255, 255, 255), 1.4));
        p.drawLine(QPointF(X(s), R.top()), QPointF(X(s), R.bottom()));
        p.setBrush(QColor(255, 255, 255));
        p.drawEllipse(QPointF(X(s), Yz(path->zAt(s))), 3.5, 3.5);
        p.drawText(QPointF(X(s) + 5, R.top() + 10), QString::fromStdString(civil::pk(s)));
    }
    void mousePressEvent(QMouseEvent* e) override { emitSeek(e->position().x()); }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) emitSeek(e->position().x());
    }

private:
    void emitSeek(double x) {
        if (!path || !seek) return;
        const double w = width() - 56;
        seek(std::clamp((x - 46) / w, 0.0, 1.0) * path->length());
    }
};

// ---------------------------------------------------------------- oyna
DriveDialog::DriveDialog(const i18n::Localizer& loc, QWidget* parent) : QDialog(parent), loc_(loc) {
    setWindowTitle(t("gui.drv.title"));
    resize(1400, 820);
    auto* root = new QHBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split);
    auto* left = new QWidget(split);
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    view_ = new DriveView(left);
    lv->addWidget(view_, 1);
    prof_ = new DriveProfile(left);
    prof_->title = t("gui.drv.profile");
    lv->addWidget(prof_);
    pos_ = new QSlider(Qt::Horizontal, left);
    pos_->setRange(0, 10000);
    lv->addWidget(pos_);

    auto* right = new QWidget(split);
    right->setMinimumWidth(320);
    auto* rv = new QVBoxLayout(right);
    auto* form = new QFormLayout;
    auto dspin = [&](double lo, double hi, double step, int dec, double v) {
        auto* s = new QDoubleSpinBox(right);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        s->setValue(v);
        return s;
    };
    mode_ = new QComboBox(right);
    mode_->addItems({t("gui.drv.m.drive"), t("gui.drv.m.truck"), t("gui.drv.m.walk"), t("gui.drv.m.drone")});
    zsrc_ = new QComboBox(right);
    zconst_ = dspin(-500, 9000, 0.1, 2, 0);
    speed_ = dspin(1, 200, 5, 0, 60);
    design_ = dspin(10, 150, 10, 0, 60);
    eye_ = dspin(0.3, 300, 0.1, 2, 1.2);
    obj_ = dspin(0, 5, 0.05, 2, 0.2);
    ahead_ = dspin(1, 500, 5, 0, 40);
    offset_ = dspin(-50, 50, 0.25, 2, 0);
    fov_ = dspin(30, 120, 5, 0, 70);
    reverse_ = new QCheckBox(t("gui.drv.reverse"), right);
    obst_ = new QCheckBox(t("gui.drv.obst"), right);
    obst_->setChecked(true);
    form->addRow(t("gui.drv.mode"), mode_);
    form->addRow(t("gui.drv.zsrc"), zsrc_);
    form->addRow(t("gui.drv.zconst"), zconst_);
    form->addRow(t("gui.drv.speed"), speed_);
    form->addRow(t("gui.drv.design"), design_);
    form->addRow(t("gui.drv.eye"), eye_);
    form->addRow(t("gui.drv.obj"), obj_);
    form->addRow(t("gui.drv.ahead"), ahead_);
    form->addRow(t("gui.drv.offset"), offset_);
    form->addRow(t("gui.drv.fov"), fov_);
    form->addRow(reverse_);
    form->addRow(obst_);
    rv->addLayout(form);
    auto* btns = new QHBoxLayout;
    auto* startB = new QPushButton(t("gui.drv.start"), right);
    startB->setFixedWidth(44);
    playB_ = new QPushButton(t("gui.drv.play"), right);
    playB_->setObjectName("accent");
    btns->addWidget(startB);
    btns->addWidget(playB_, 1);
    rv->addLayout(btns);
    summary_ = new QLabel(right);
    summary_->setWordWrap(true);
    summary_->setObjectName("title");
    rv->addWidget(summary_);
    rv->addWidget(new QLabel("<b>" + t("gui.drv.checks") + "</b>", right));
    checks_ = new QListWidget(right);
    checks_->setWordWrap(true);
    rv->addWidget(checks_, 1);
    sceneLbl_ = new QLabel(right);
    sceneLbl_->setObjectName("muted");
    rv->addWidget(sceneLbl_);
    auto* hint = new QLabel(t("gui.drv.hint"), right);
    hint->setWordWrap(true);
    hint->setObjectName("muted");
    rv->addWidget(hint);
    split->setStretchFactor(0, 1);
    split->setSizes({1060, 340});

    timer_ = new QTimer(this);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const double dt = lastTick_ ? std::min(0.2, (now - lastTick_) / 1000.0) : 0.033;
        lastTick_ = now;
        step(dt);
    });
    connect(playB_, &QPushButton::clicked, this, [this] { play(!playing()); });
    connect(startB, &QPushButton::clicked, this, [this] { setStation(0); });
    connect(pos_, &QSlider::valueChanged, this, [this](int v) {
        if (!updating_) setStation(v / 10000.0 * path_.length());
    });
    prof_->seek = [this](double st) { setStation(st); };
    connect(mode_, &QComboBox::currentIndexChanged, this, [this](int m) { setMode(m); });
    connect(zsrc_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_) return;
        rebuildPath();
    });
    connect(zconst_, &QDoubleSpinBox::valueChanged, this, [this] {
        if (!updating_ && zsrc_->currentIndex() >= 0 && src_[static_cast<std::size_t>(zsrc_->currentIndex())].constant) rebuildPath();
    });
    connect(reverse_, &QCheckBox::toggled, this, [this] { rebuildPath(); });
    for (auto* s : {design_, obj_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) recomputeSight(); });
    connect(eye_, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) { recomputeSight(); updateCamera(); } });
    connect(obst_, &QCheckBox::toggled, this, [this] { recomputeSight(); });
    for (auto* s : {ahead_, offset_}) connect(s, &QDoubleSpinBox::valueChanged, this, [this] { updateCamera(); });
    connect(fov_, &QDoubleSpinBox::valueChanged, this, [this](double v) { view_->setFov(v); });
    connect(checks_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        const QVariant v = it->data(Qt::UserRole);
        if (v.isValid()) setStation(std::max(0.0, v.toDouble()));
    });
    connect(view_, &DriveView::keyPressed, this, [this](int k) {
        if (k == Qt::Key_Space) play(!playing());
        else if (k == Qt::Key_Right) setStation(s_ + 10);
        else if (k == Qt::Key_Left) setStation(s_ - 10);
        else if (k == Qt::Key_W || k == Qt::Key_Up) speed_->setValue(speed_->value() * 1.25);
        else if (k == Qt::Key_S || k == Qt::Key_Down) speed_->setValue(speed_->value() / 1.25);
    });
    view_->setOverlay([this](QPainter& p) { drawOverlay(p); });
}

QString DriveDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

void DriveDialog::setPath(const std::vector<Vec3>& pts, const QString& title) {
    raw_ = pts;
    setWindowTitle(t("gui.drv.title") + (title.isEmpty() ? QString() : " — " + title));
    rebuildPath();
}

void DriveDialog::setSources(const std::vector<DriveSource>& s, int def) {
    updating_ = true;
    src_ = s;
    zsrc_->clear();
    for (const auto& x : src_) zsrc_->addItem(x.name);
    zsrc_->setCurrentIndex(std::clamp(def, 0, static_cast<int>(src_.size()) - 1));
    updating_ = false;
    if (def >= 0 && def < static_cast<int>(src_.size()) && src_[static_cast<std::size_t>(def)].walk) setMode(2);
    rebuildPath();
}

void DriveDialog::setSource(int i) { zsrc_->setCurrentIndex(i); }

void DriveDialog::setScene(civil::DriveScene sc) {
    scene_ = std::move(sc);
    view_->setScene(&scene_);
    sceneLbl_->setText(t("gui.drv.scene", {QString::number(scene_.tris.size()), QString::number(scene_.segs.size())}));
    recomputeSight();
}

void DriveDialog::setMode(int m) {
    struct P { double speed, design, eye, ahead, fov, pitch; bool obst; };
    static const P ps[] = {{60, 60, 1.2, 40, 70, 0, true}, {50, 60, 2.3, 50, 65, 0, true}, {5, 5, 1.7, 6, 80, 0, false}, {40, 60, 40, 120, 75, -18, false}};
    m = std::clamp(m, 0, 3);
    updating_ = true;
    if (mode_->currentIndex() != m) mode_->setCurrentIndex(m);
    const auto& p = ps[m];
    speed_->setValue(p.speed);
    design_->setValue(p.design);
    eye_->setValue(p.eye);
    ahead_->setValue(p.ahead);
    fov_->setValue(p.fov);
    obst_->setChecked(p.obst);
    updating_ = false;
    view_->setFov(p.fov);
    view_->setLook(0, p.pitch);
    recomputeSight();
    updateCamera();
}

void DriveDialog::rebuildPath() {
    std::vector<Vec3> pts = raw_;
    if (reverse_->isChecked()) std::reverse(pts.begin(), pts.end());
    const int si = zsrc_->currentIndex();
    if (si >= 0 && si < static_cast<int>(src_.size())) {
        const auto& src = src_[static_cast<std::size_t>(si)];
        if (src.constant)
            for (auto& q : pts) q.z = zconst_->value();
        else if (src.z) {
            if (src.reset) src.reset();
            for (auto& q : pts) {
                const double z = src.z(q.x, q.y);
                if (std::isfinite(z)) q.z = z;
            }
        }
    }
    path_ = civil::makeDrivePath(pts, 1.0);
    s_ = std::clamp(s_, 0.0, path_.length());
    prof_->path = &path_;
    recomputeSight();
    setStation(s_);
}

void DriveDialog::recomputeSight() {
    if (path_.length() <= 0) {
        sight_ = {};
        summary_->setText(t("gui.drv.nopath"));
        checks_->clear();
        return;
    }
    civil::SightParams sp;
    sp.eyeH = std::min(eye_->value(), 3.0);  // dron balandligida emas — haydovchi/piyoda ko'zi
    sp.objH = obj_->value();
    sp.speedKmh = design_->value();
    sp.obstacles3d = obst_->isChecked();
    sp.step = std::clamp(path_.length() / 150, 5.0, 20.0);
    sight_ = civil::sightAnalysis(path_, scene_, sp);
    prof_->sight = &sight_;
    prof_->maxD = sp.maxD;
    checks_->clear();
    for (const auto& is : sight_.issues) {
        auto* it = new QListWidgetItem(t("drive.chk.sight", {QString::fromStdString(civil::pk(is.s0)), QString::fromStdString(civil::pk(is.s1)),
                                                             f1(is.minD), QString::number(sight_.required, 'f', 0)}));
        it->setForeground(QColor(232, 70, 80));
        it->setData(Qt::UserRole, is.s0 - 20);
        checks_->addItem(it);
    }
    if (sight_.issues.empty()) checks_->addItem(t("gui.drv.ok"));
    summary_->setText(t("gui.drv.summary", {f1(path_.length()), f1(sight_.minD), QString::fromStdString(civil::pk(sight_.minAt)),
                                            QString::number(sight_.required, 'f', 0), QString::number(sight_.issues.size())}));
    prof_->update();
}

void DriveDialog::setStation(double st) {
    s_ = std::clamp(st, 0.0, path_.length());
    updating_ = true;
    pos_->setValue(path_.length() > 0 ? static_cast<int>(s_ / path_.length() * 10000) : 0);
    updating_ = false;
    prof_->s = s_;
    prof_->update();
    updateCamera();
}

void DriveDialog::updateCamera() {
    if (path_.length() <= 0) return;
    auto cam = civil::driveCamera(path_, s_, eye_->value(), ahead_->value());
    const Vec3 d = path_.dir(s_);
    const double h = std::hypot(d.x, d.y);
    if (h > 1e-9 && std::abs(offset_->value()) > 1e-9) {
        const Vec3 rgt{d.y / h * offset_->value(), -d.x / h * offset_->value(), 0};
        cam.eye += rgt;
        cam.target += rgt;
    }
    view_->setCamera(cam.eye, cam.target);
}

void DriveDialog::play(bool on) {
    if (on && s_ >= path_.length() - 1e-6) setStation(0);
    lastTick_ = 0;
    if (on) timer_->start();
    else timer_->stop();
    view_->setRenderScale(on ? 0.6 : 1.0);
    playB_->setText(t(on ? "gui.drv.pause" : "gui.drv.play"));
    view_->update();
}

bool DriveDialog::playing() const { return timer_->isActive(); }

void DriveDialog::step(double dt) {
    const double ns = s_ + speed_->value() / 3.6 * dt;
    setStation(ns);
    if (ns >= path_.length()) play(false);
}

void DriveDialog::drawOverlay(QPainter& p) {
    if (path_.length() <= 0) {
        p.setPen(Qt::white);
        p.drawText(view_->rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap, t("gui.drv.nopath"));
        return;
    }
    // HUD
    double cur = sight_.samples.empty() ? 0 : sight_.samples.front().total;
    for (const auto& sm : sight_.samples)
        if (sm.s <= s_ + 1e-6) cur = sm.total;
    const bool bad = !sight_.samples.empty() && cur < sight_.required && path_.length() - s_ >= sight_.required;
    const QString l1 = t("gui.drv.hud", {QString::fromStdString(civil::pk(s_)), f2(path_.zAt(s_) ), QString::number(path_.grade(s_) * 1000, 'f', 1),
                                         QString::number(speed_->value(), 'f', 0)});
    const QString l2 = t("gui.drv.hudsight", {f1(cur), QString::number(sight_.required, 'f', 0)});
    QFont fn = p.font();
    fn.setPixelSize(13);
    fn.setBold(true);
    p.setFont(fn);
    const int w = std::max(p.fontMetrics().horizontalAdvance(l1), p.fontMetrics().horizontalAdvance(l2)) + 24;
    const QRectF box(12, 12, w, 52);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(10, 14, 28, 170));
    p.drawRoundedRect(box, 8, 8);
    p.setPen(QColor(240, 244, 255));
    p.drawText(QPointF(box.left() + 12, box.top() + 21), l1);
    p.setPen(bad ? QColor(255, 105, 110) : QColor(120, 220, 140));
    p.drawText(QPointF(box.left() + 12, box.top() + 41), l2);
    // kichik xarita
    const QRectF mm(view_->width() - 192, 12, 180, 180);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(10, 14, 28, 160));
    p.drawRoundedRect(mm, 8, 8);
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const auto& q : path_.p) { x0 = std::min(x0, q.x); y0 = std::min(y0, q.y); x1 = std::max(x1, q.x); y1 = std::max(y1, q.y); }
    const double k = std::min((mm.width() - 24) / std::max(1.0, x1 - x0), (mm.height() - 24) / std::max(1.0, y1 - y0));
    const double ox = mm.center().x() - (x0 + x1) / 2 * k, oy = mm.center().y() + (y0 + y1) / 2 * k;
    auto M = [&](const Vec3& q) { return QPointF(ox + q.x * k, oy - q.y * k); };
    QPolygonF pl;
    const std::size_t stp = std::max<std::size_t>(1, path_.p.size() / 600);
    for (std::size_t i = 0; i < path_.p.size(); i += stp) pl << M(path_.p[i]);
    pl << M(path_.p.back());
    p.setPen(QPen(QColor(240, 190, 80), 2));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(pl);
    p.setPen(QPen(QColor(255, 90, 95), 3));
    for (const auto& is : sight_.issues) {
        QPolygonF b;
        for (double st = is.s0; st <= is.s1 + 1e-6; st += std::max(1.0, (is.s1 - is.s0) / 20)) b << M(path_.at(st));
        if (b.size() == 1) b << b.front();
        p.drawPolyline(b);
    }
    const Vec3 c = path_.at(s_), d = path_.dir(s_);
    const double ya = view_->yaw() * 3.14159265358979 / 180;
    const double hx = d.x * std::cos(ya) - d.y * std::sin(ya), hy = d.x * std::sin(ya) + d.y * std::cos(ya);
    const QPointF pc = M(c);
    p.setPen(QPen(Qt::white, 2));
    p.drawLine(pc, pc + QPointF(hx, -hy) * 14);
    p.setBrush(Qt::white);
    p.drawEllipse(pc, 4, 4);
}

} // namespace asiacad::gui
