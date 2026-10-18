#include "draft_panels.h"

#include "theme.h"
#include "viewport.h"

#include <asiacad/bim/steel.h>
#include <asiacad/civil/symbols.h>
#include <asiacad/core/entities.h>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>

namespace asiacad::gui {

QPixmap symbolPixmap(const BlockDef& b, int size, bool dark) {
    QPixmap pm(size, size);
    pm.fill(dark ? QColor(34, 40, 58) : QColor(244, 242, 236));
    BBox bb;
    for (const auto& e : b.entities) bb.expand(e->bbox());
    if (!bb.valid()) return pm;
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const double w = std::max(1e-9, bb.max.x - bb.min.x), h = std::max(1e-9, bb.max.y - bb.min.y);
    const double k = (size - 8) / std::max(w, h);
    const double ox = (size - w * k) / 2, oy = (size - h * k) / 2;
    auto P = [&](const Vec3& v) { return QPointF(ox + (v.x - bb.min.x) * k, size - oy - (v.y - bb.min.y) * k); };
    auto col = [&](int c) { return aciColor(c, !dark); };
    for (const auto& e : b.entities) {
        const QColor c = col(e->color());
        if (auto* hh = dynamic_cast<const HatchEntity*>(e.get())) {
            QPainterPath path;
            path.setFillRule(Qt::OddEvenFill);
            for (const auto& lp : hh->loops()) {
                const auto v = lp.tessellated().vertices();
                for (std::size_t i = 0; i < v.size(); ++i) (i ? path.lineTo(P(v[i])) : path.moveTo(P(v[i])));
                path.closeSubpath();
            }
            p.fillPath(path, c);
            continue;
        }
        p.setPen(QPen(c, 1.0));
        p.setBrush(Qt::NoBrush);
        if (auto* l = dynamic_cast<const LineEntity*>(e.get())) p.drawLine(P(l->startPoint()), P(l->endPoint()));
        else if (auto* ci = dynamic_cast<const CircleEntity*>(e.get())) p.drawEllipse(P(ci->center()), ci->radius() * k, ci->radius() * k);
        else if (auto* pl = dynamic_cast<const PolylineEntity*>(e.get())) {
            const auto v = pl->tessellated().vertices();
            QPolygonF poly;
            for (const auto& q : v) poly << P(q);
            if (pl->isClosed()) p.drawPolygon(poly);
            else p.drawPolyline(poly);
        } else if (auto* t = dynamic_cast<const TextEntity*>(e.get())) {
            QFont f = p.font();
            f.setPixelSize(std::max(6, static_cast<int>(t->height() * k)));
            f.setBold(true);
            p.setFont(f);
            p.setPen(c);
            const QPointF a = P(t->insertion());
            p.drawText(QRectF(a.x() - size, a.y() - size, 2 * size, 2 * size), Qt::AlignCenter, QString::fromStdString(t->text()));
        }
    }
    return pm;
}

// ---------------------------------------------------------------- belgilar palitrasi
SymbolPalette::SymbolPalette(const i18n::Localizer& loc, bool dark, QWidget* parent) : QWidget(parent), loc_(loc), dark_(dark) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    list_ = new QListWidget(this);
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(54, 54));
    list_->setGridSize(QSize(86, 92));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWordWrap(true);
    list_->setSpacing(2);
    v->addWidget(list_, 1);
    auto* form = new QFormLayout;
    scale_ = new QDoubleSpinBox(this);
    scale_->setRange(0.01, 1000);
    scale_->setDecimals(2);
    scale_->setValue(1);
    rot_ = new QDoubleSpinBox(this);
    rot_->setRange(-360, 360);
    rot_->setDecimals(1);
    form->addRow(QString::fromStdString(loc_.tr("gui.sym.scale")), scale_);
    form->addRow(QString::fromStdString(loc_.tr("gui.sym.rot")), rot_);
    v->addLayout(form);
    auto* hint = new QLabel(QString::fromStdString(loc_.tr("gui.sym.hint")), this);
    hint->setWordWrap(true);
    hint->setObjectName("muted");
    v->addWidget(hint);
    fill();
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        const QString k = it->data(Qt::UserRole).toString();
        if (!k.isEmpty()) emit symbolChosen(k, scale_->value(), rot_->value());
    });
    for (auto* s : {scale_, rot_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] {
            if (auto* it = list_->currentItem(); it && !it->data(Qt::UserRole).toString().isEmpty())
                emit symbolChosen(it->data(Qt::UserRole).toString(), scale_->value(), rot_->value());
        });
}

void SymbolPalette::fill() {
    list_->clear();
    for (const auto& s : civil::symbolCatalog()) {
        auto b = civil::makeSymbol(s.key);
        if (!b) continue;
        auto* it = new QListWidgetItem(QIcon(symbolPixmap(*b, 64, dark_)), QString::fromStdString(loc_.tr(std::string("sym.") + s.key)));
        it->setData(Qt::UserRole, QString::fromLatin1(s.key));
        it->setToolTip(QString::fromStdString(loc_.tr(std::string("sym.cat.") + s.category)) + " · " + it->text());
        list_->addItem(it);
    }
}

void SymbolPalette::retranslate() { fill(); }

bool SymbolPalette::choose(const QString& key) {
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(Qt::UserRole).toString() == key) {
            list_->setCurrentRow(i);
            emit symbolChosen(key, scale_->value(), rot_->value());
            return true;
        }
    return false;
}

// ---------------------------------------------------------------- metall (КМ)
SteelPanel::SteelPanel(const i18n::Localizer& loc, bool dark, QWidget* parent) : QWidget(parent), loc_(loc), dark_(dark) {
    auto T = [&](const std::string& k) { return QString::fromStdString(loc_.tr(k)); };
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    family_ = new QComboBox(this);
    for (auto f : {bim::SteelFamily::IBeam, bim::SteelFamily::Channel, bim::SteelFamily::Angle, bim::SteelFamily::Pipe, bim::SteelFamily::SquareTube})
        family_->addItem(T(std::string("km.fam.") + bim::steelFamilyKey(f)), static_cast<int>(f));
    size_ = new QComboBox(this);
    v->addWidget(family_);
    v->addWidget(size_);
    preview_ = new QLabel(this);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumHeight(170);
    v->addWidget(preview_);
    props_ = new QLabel(this);
    props_->setWordWrap(true);
    props_->setTextFormat(Qt::RichText);
    v->addWidget(props_);
    auto* member = new QPushButton(T("gui.km.member"), this);
    member->setObjectName("accent");
    auto* sec = new QPushButton(T("gui.km.section"), this);
    auto* spec = new QPushButton(T("gui.km.spec"), this);
    v->addWidget(member);
    auto* h = new QHBoxLayout;
    h->addWidget(sec);
    h->addWidget(spec);
    v->addLayout(h);
    auto* hint = new QLabel(T("gui.km.hint"), this);
    hint->setWordWrap(true);
    hint->setObjectName("muted");
    v->addWidget(hint);
    v->addStretch(1);
    connect(family_, &QComboBox::currentIndexChanged, this, [this] { fillSizes(); });
    connect(size_, &QComboBox::currentIndexChanged, this, [this] { update_(); });
    connect(member, &QPushButton::clicked, this, [this] { emit drawMember(profile()); });
    connect(sec, &QPushButton::clicked, this, [this] { emit drawSection(profile()); });
    connect(spec, &QPushButton::clicked, this, [this] { emit specRequested(); });
    fillSizes();
}

QString SteelPanel::profile() const { return size_->currentData().toString(); }

bool SteelPanel::select(const QString& name) {
    const auto p = bim::findSteel(name.toStdString());
    if (!p) return false;
    family_->setCurrentIndex(family_->findData(static_cast<int>(p->family)));
    const int i = size_->findData(QString::fromStdString(p->name));
    if (i < 0) return false;
    size_->setCurrentIndex(i);
    return true;
}

void SteelPanel::fillSizes() {
    const auto f = static_cast<bim::SteelFamily>(family_->currentData().toInt());
    size_->clear();
    for (const auto& p : bim::steelCatalog())
        if (p.family == f) size_->addItem(QString::fromStdString(p.name) + QString::fromUtf8("  ·  %1 kg/m").arg(p.mass, 0, 'f', 2), QString::fromStdString(p.name));
    size_->setCurrentIndex(std::min(5, size_->count() - 1));
    update_();
}

void SteelPanel::update_() {
    const auto p = bim::findSteel(profile().toStdString());
    if (!p) return;
    // kesim rasmi (masshtab bilan, o'lcham yozuvlari)
    const int W = 240, H = 170;
    QPixmap pm(W, H);
    pm.fill(dark_ ? QColor(24, 30, 48) : QColor(246, 244, 238));
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    const auto loops = bim::steelSection(*p);
    const double k = std::min((W - 60) / (p->b / 1000), (H - 40) / (p->h / 1000));
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (const auto& lp : loops) {
        for (std::size_t i = 0; i < lp.size(); ++i) {
            const QPointF q(W / 2.0 + lp[i].x * k, H / 2.0 - lp[i].y * k);
            i ? path.lineTo(q) : path.moveTo(q);
        }
        path.closeSubpath();
    }
    g.fillPath(path, QColor(110, 140, 170));
    g.setPen(QPen(dark_ ? QColor(220, 228, 240) : QColor(30, 40, 60), 1.2));
    g.drawPath(path);
    QFont f = g.font();
    f.setPixelSize(11);
    g.setFont(f);
    g.drawText(QRectF(0, H - 18, W, 16), Qt::AlignCenter, QStringLiteral("b = %1").arg(p->b));
    g.save();
    g.translate(14, H / 2.0);
    g.rotate(-90);
    g.drawText(QRectF(-60, -8, 120, 16), Qt::AlignCenter, QStringLiteral("h = %1").arg(p->h));
    g.restore();
    g.end();
    preview_->setPixmap(pm);
    props_->setText(QStringLiteral("<b>%1</b> · %2<br>A = %3 sm² · <b>%4 kg/m</b><br>s = %5 · t = %6 mm%7")
                        .arg(QString::fromStdString(p->name), QString::fromLatin1(p->standard))
                        .arg(p->A, 0, 'f', 2)
                        .arg(p->mass, 0, 'f', 2)
                        .arg(p->s)
                        .arg(p->t)
                        .arg(p->Ix > 0 ? QStringLiteral("<br>Ix = %1 sm⁴ · Wx = %2 sm³").arg(p->Ix, 0, 'f', 0).arg(p->Wx, 0, 'f', 1) : QString()));
}

// ---------------------------------------------------------------- massiv
ArrayDialog::ArrayDialog(const i18n::Localizer& loc, const QString& selArg, const Vec3& c, EntityId pathId, QWidget* parent)
    : QDialog(parent), sel_(selArg) {
    auto T = [&](const char* k) { return QString::fromStdString(loc.tr(k)); };
    setWindowTitle(T("gui.arr.title"));
    auto* v = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);
    v->addWidget(tabs_);
    auto dspin = [&](QWidget* p, double lo, double hi, double val, int dec = 3) {
        auto* s = new QDoubleSpinBox(p);
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setValue(val);
        return s;
    };
    auto ispin = [&](QWidget* p, int lo, int hi, int val) {
        auto* s = new QSpinBox(p);
        s->setRange(lo, hi);
        s->setValue(val);
        return s;
    };
    {
        auto* w = new QWidget(tabs_);
        auto* f = new QFormLayout(w);
        rows_ = ispin(w, 1, 1000, 3);
        cols_ = ispin(w, 1, 1000, 4);
        dx_ = dspin(w, -1e6, 1e6, 6);
        dy_ = dspin(w, -1e6, 1e6, 6);
        f->addRow(T("gui.arr.rows"), rows_);
        f->addRow(T("gui.arr.cols"), cols_);
        f->addRow(T("gui.arr.dx"), dx_);
        f->addRow(T("gui.arr.dy"), dy_);
        tabs_->addTab(w, T("gui.arr.rect"));
    }
    {
        auto* w = new QWidget(tabs_);
        auto* f = new QFormLayout(w);
        cx_ = dspin(w, -1e9, 1e9, c.x);
        cy_ = dspin(w, -1e9, 1e9, c.y);
        pn_ = ispin(w, 2, 10000, 8);
        fill_ = dspin(w, -360, 360, 360, 1);
        rotate_ = new QCheckBox(T("gui.arr.rotate"), w);
        rotate_->setChecked(true);
        f->addRow(T("gui.arr.cx"), cx_);
        f->addRow(T("gui.arr.cy"), cy_);
        f->addRow(T("gui.arr.count"), pn_);
        f->addRow(T("gui.arr.fill"), fill_);
        f->addRow(rotate_);
        tabs_->addTab(w, T("gui.arr.polar"));
    }
    {
        auto* w = new QWidget(tabs_);
        auto* f = new QFormLayout(w);
        pathId_ = ispin(w, 0, 2000000000, static_cast<int>(pathId));
        pathN_ = ispin(w, 0, 100000, 10);
        spacing_ = dspin(w, 0, 1e6, 0);
        align_ = new QCheckBox(T("gui.arr.align"), w);
        align_->setChecked(true);
        f->addRow(T("gui.arr.path"), pathId_);
        f->addRow(T("gui.arr.count"), pathN_);
        f->addRow(T("gui.arr.spacing"), spacing_);
        f->addRow(align_);
        auto* h = new QLabel(T("gui.arr.pathhint"), w);
        h->setWordWrap(true);
        h->setObjectName("muted");
        f->addRow(h);
        tabs_->addTab(w, T("gui.arr.pathtab"));
    }
    if (pathId != kInvalidId) tabs_->setCurrentIndex(2);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(T("gui.arr.ok"));
    bb->button(QDialogButtonBox::Cancel)->setText(T("gui.gen.cancel"));
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString ArrayDialog::command() const {
    auto n = [](double v) { return QString::number(v, 'g', 12); };
    switch (tabs_->currentIndex()) {
    case 0: return QStringLiteral("ARRAY %1 R %2 %3 %4 %5").arg(sel_).arg(rows_->value()).arg(cols_->value()).arg(n(dx_->value()), n(dy_->value()));
    case 1:
        return QStringLiteral("ARRAY %1 P %2,%3 %4 %5%6").arg(sel_, n(cx_->value()), n(cy_->value())).arg(pn_->value()).arg(n(fill_->value()))
            .arg(rotate_->isChecked() ? "" : " NOROT");
    default:
        return QStringLiteral("ARRAY %1 PATH %2 %3%4").arg(sel_).arg(pathId_->value())
            .arg(spacing_->value() > 0 ? "S=" + n(spacing_->value()) : QString::number(pathN_->value()))
            .arg(align_->isChecked() ? "" : " NOALIGN");
    }
}

} // namespace asiacad::gui
