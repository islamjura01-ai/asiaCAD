#include "waterway_dialog.h"
#include "table_bulk.h"

#include <asiacad/civil/excel.h>
#include <asiacad/civil/road.h>

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace asiacad::gui {

using civil::WwLayer;
using civil::WwType;

namespace {
QString f2(double v) { return QString::number(v, 'f', 2); }
QString f1(double v) { return QString::number(v, 'f', 1); }
QString f3(double v) { return QString::number(v, 'f', 3); }
const QColor kConcrete(165, 168, 172), kEarth(150, 115, 75), kFlume(200, 200, 195), kWater(60, 150, 230), kGround(120, 110, 90), kPipe(90, 140, 210);
QColor typeColor(WwType t) {
    switch (t) {
        case WwType::PressurePipe: return QColor(31, 123, 224);
        case WwType::GravityPipe: return QColor(26, 160, 150);
        case WwType::ConcreteCanal: return QColor(140, 145, 155);
        case WwType::EarthDitch: return QColor(150, 115, 75);
        case WwType::Flume: return QColor(210, 160, 60);
    }
    return Qt::gray;
}
const WwType kTypes[5] = {WwType::ConcreteCanal, WwType::Flume, WwType::EarthDitch, WwType::GravityPipe, WwType::PressurePipe};
}  // namespace

// ---------------------------------------------------------------- profil
void WwProfileView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor bg = dark_ ? QColor(18, 22, 34) : QColor(250, 250, 252), fg = dark_ ? QColor(200, 205, 220) : QColor(40, 40, 50);
    p.fillRect(rect(), bg);
    if (!m_ || !p_ || m_->st.size() < 2) return;
    const double full = civil::wwFullDepth(p_->sec);
    double zmin = 1e18, zmax = -1e18;
    for (const auto& s : m_->st) {
        zmin = std::min({zmin, s.ground, s.bottom - p_->sec.t});
        zmax = std::max({zmax, s.ground, s.bottom + full, s.water});
    }
    zmin = std::floor(zmin - 0.5);
    zmax = std::ceil(zmax + 0.5);
    const double L = std::max(1e-6, m_->st.back().s);
    const QRectF r = QRectF(rect()).adjusted(60, 30, -16, -34);
    auto P = [&](double s, double z) { return QPointF(r.left() + s / L * r.width(), r.bottom() - (z - zmin) / (zmax - zmin) * r.height()); };
    p.setPen(QPen(fg.darker(180), 1));
    const double dz = std::max(0.5, std::round((zmax - zmin) / 6 * 2) / 2);
    for (double z = zmin; z <= zmax + 1e-9; z += dz) {
        p.drawLine(P(0, z), P(L, z));
        p.drawText(QRectF(0, P(0, z).y() - 8, 56, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(z, 'f', 1));
    }
    // o'zan (tubdan qirg'oqqacha) va suv
    QPolygonF body, water;
    for (const auto& s : m_->st) body << P(s.s, s.bottom + full);
    for (auto it = m_->st.rbegin(); it != m_->st.rend(); ++it) body << P(it->s, it->bottom);
    QColor bc = typeColor(p_->sec.type);
    bc.setAlpha(70);
    p.setPen(Qt::NoPen);
    p.setBrush(bc);
    p.drawPolygon(body);
    if (p_->sec.type != WwType::PressurePipe) {
        for (const auto& s : m_->st) water << P(s.s, std::max(s.bottom, s.water));
        for (auto it = m_->st.rbegin(); it != m_->st.rend(); ++it) water << P(it->s, it->bottom);
        QColor wc = kWater;
        wc.setAlpha(120);
        p.setBrush(wc);
        p.drawPolygon(water);
    }
    auto line = [&](auto get, QColor c, double w, Qt::PenStyle st = Qt::SolidLine) {
        QPainterPath path;
        for (std::size_t i = 0; i < m_->st.size(); ++i) {
            const auto& s = m_->st[i];
            if (i == 0) path.moveTo(P(s.s, get(s)));
            else path.lineTo(P(s.s, get(s)));
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c, w, st));
        p.drawPath(path);
    };
    line([](const civil::WwStation& s) { return s.ground; }, QColor(170, 120, 70), 2);
    line([](const civil::WwStation& s) { return s.bottom; }, QColor(235, 70, 70), 2.4);
    line([&](const civil::WwStation& s) { return s.bottom + full; }, fg, 1.2);
    line([](const civil::WwStation& s) { return s.water; }, kWater.lighter(120), 1.8,
         p_->sec.type == WwType::PressurePipe ? Qt::DashLine : Qt::SolidLine);
    p.setPen(fg);
    for (const auto& s : m_->st) {
        if (s.control) {
            p.setBrush(QColor(235, 70, 70));
            p.drawEllipse(P(s.s, s.bottom), 3.5, 3.5);
        }
        if (std::abs(std::fmod(s.s, 100.0)) < 0.01)
            p.drawText(QRectF(P(s.s, zmin).x() - 30, r.bottom() + 4, 60, 14), Qt::AlignCenter, QString::number(s.s / 100, 'f', 0));
    }
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    const QColor cs[4] = {QColor(170, 120, 70), QColor(235, 70, 70), kWater.lighter(120), fg};
    double x = r.left();
    for (int i = 0; i < lbl_.size() && i < 4; ++i) {
        p.setPen(cs[i]);
        p.drawText(QPointF(x, 18), "— " + lbl_[i]);
        x += p.fontMetrics().horizontalAdvance("— " + lbl_[i]) + 24;
    }
}

// ---------------------------------------------------------------- dialog
WaterwayDialog::WaterwayDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent)
    : QDialog(parent), loc_(loc), doc_(doc) {
    setWindowTitle(t("gui.ww.title"));
    resize(1660, 920);
    p_ = civil::defaultWwParams();
    secs_ = civil::builtinWwSections();
    buildUi();
    view_->setDark(dark);
    profile_->setDark(dark);
    paramsToUi();
    fillLayers();
    reread();
}

QString WaterwayDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

void WaterwayDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    // ---- 1: kesim shablonlari
    side_ = new TemplateSidebar(t("gui.ww.side"), t("gui.side.search"), t("gui.side.apply"), t("gui.side.hint"), split);
    for (WwType ty : kTypes) {
        side_->addSection(t((std::string("ww.type.") + civil::wwTypeKey(ty)).c_str()));
        for (std::size_t i = 0; i < secs_.size(); ++i) {
            const auto& s = secs_[i];
            if (s.type != ty) continue;
            QString sub;
            if (ty == WwType::PressurePipe || ty == WwType::GravityPipe) sub = QStringLiteral("D=%1 m · n=%2").arg(s.d).arg(s.n);
            else if (s.B > 0) sub = QStringLiteral("B=%1 · h=%2 · t=%3 m").arg(s.B).arg(s.h).arg(s.t);
            else sub = QStringLiteral("b=%1 · h=%2 · 1:%3 · n=%4").arg(s.b).arg(s.h).arg(s.m).arg(s.n);
            side_->addTemplate(QString::fromStdString(s.name), sub, QVariant(static_cast<int>(i)), typeColor(ty));
        }
    }
    connect(side_, &TemplateSidebar::templateActivated, this, [this](const QVariant& v) {
        const int i = v.toInt();
        if (i >= 0 && i < static_cast<int>(secs_.size())) applySection(secs_[static_cast<std::size_t>(i)]);
    });

    // ---- 2: parametrlar
    auto* scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setObjectName("sewerLeft");
    scroll->setMinimumWidth(300);
    auto* left = new QWidget;
    left->setObjectName("sewerLeftBody");
    scroll->setWidget(left);
    auto* lv = new QVBoxLayout(left);
    auto title = [&](const char* k) {
        auto* l = new QLabel(t(k), left);
        l->setObjectName("title");
        lv->addWidget(l);
    };
    title("gui.rd.layers");
    layers_ = new QTableWidget(0, 2, left);
    layers_->setHorizontalHeaderLabels({t("gui.bim.col.layer"), t("gui.bim.col.kind")});
    layers_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layers_->verticalHeader()->setVisible(false);
    layers_->setMinimumHeight(140);
    layers_->setMaximumHeight(180);
    lv->addWidget(layers_);
    auto* rereadB = new QPushButton(t("gui.ut.reread"), left);
    lv->addWidget(rereadB);

    title("gui.ww.section");
    secName_ = new QLabel(left);
    secName_->setObjectName("muted");
    secName_->setWordWrap(true);
    lv->addWidget(secName_);
    auto* form = new QFormLayout;
    auto dspin = [&](double lo, double hi, double step, int dec) {
        auto* s = new QDoubleSpinBox(left);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        return s;
    };
    type_ = new QComboBox(left);
    for (WwType ty : kTypes) type_->addItem(t((std::string("ww.type.") + civil::wwTypeKey(ty)).c_str()), QString::fromLatin1(civil::wwTypeKey(ty)));
    b_ = dspin(0, 50, 0.1, 2);
    h_ = dspin(0.1, 20, 0.1, 2);
    m_s = dspin(0, 5, 0.25, 2);
    B_ = dspin(0, 10, 0.05, 2);
    d_ = dspin(0.05, 5, 0.05, 3);
    t_ = dspin(0, 1, 0.01, 3);
    n_ = dspin(0.008, 0.05, 0.001, 3);
    fb_ = dspin(0, 2, 0.05, 2);
    vmin_ = dspin(0, 3, 0.05, 2);
    vmax_ = dspin(0.2, 15, 0.1, 2);
    piece_ = dspin(0.5, 20, 0.5, 1);
    chw_ = dspin(60, 160, 5, 0);
    form->addRow(t("gui.ww.type"), type_);
    form->addRow(t("gui.ww.b"), b_);
    form->addRow(t("gui.ww.h"), h_);
    form->addRow(t("gui.ww.m"), m_s);
    form->addRow(t("gui.ww.B"), B_);
    form->addRow(t("gui.ww.d"), d_);
    form->addRow(t("gui.ww.t"), t_);
    form->addRow(t("gui.ww.n"), n_);
    form->addRow(t("gui.ww.fb"), fb_);
    form->addRow(t("gui.ww.vmin"), vmin_);
    form->addRow(t("gui.ww.vmax"), vmax_);
    form->addRow(t("gui.ww.piece"), piece_);
    form->addRow(t("gui.ww.chw"), chw_);
    lv->addLayout(form);
    title("gui.ww.hydro");
    auto* f2l = new QFormLayout;
    q_ = dspin(0.001, 1000, 0.05, 3);
    step_ = dspin(5, 200, 5, 0);
    depth_ = dspin(0, 10, 0.1, 2);
    slope_ = dspin(0.01, 100, 0.1, 2);
    head_ = dspin(0, 9000, 0.5, 2);
    ground_ = dspin(-500, 9000, 0.5, 2);
    f2l->addRow(t("gui.ww.q"), q_);
    f2l->addRow(t("gui.ww.step"), step_);
    f2l->addRow(t("gui.ww.depth"), depth_);
    f2l->addRow(t("gui.ww.slope"), slope_);
    f2l->addRow(t("gui.ww.head"), head_);
    f2l->addRow(t("gui.ut.ground"), ground_);
    lv->addLayout(f2l);
    auto* note = new QLabel(t("gui.ww.note"), left);
    note->setWordWrap(true);
    note->setObjectName("muted");
    lv->addWidget(note);
    lv->addStretch(1);

    // ---- 3: 3D va profil
    auto* mid = new QWidget(split);
    auto* mv = new QVBoxLayout(mid);
    mv->setContentsMargins(6, 0, 6, 0);
    midTabs_ = new QTabWidget(mid);
    auto* v3 = new QWidget(midTabs_);
    auto* v3l = new QVBoxLayout(v3);
    v3l->setContentsMargins(0, 0, 0, 0);
    view_ = new Pipe3DView(v3);
    v3l->addWidget(view_, 1);
    auto* vx = new QHBoxLayout;
    vx->addWidget(new QLabel(t("gui.ut.vexag"), v3));
    vexag_ = dspin(1, 20, 1, 0);
    vexag_->setValue(3);
    vx->addWidget(vexag_);
    vx->addStretch(1);
    v3l->addLayout(vx);
    profile_ = new WwProfileView(midTabs_);
    midTabs_->addTab(v3, "3D");
    midTabs_->addTab(profile_, t("gui.rd.profile"));
    mv->addWidget(midTabs_, 1);

    // ---- 4: jadvallar
    auto* tabs = new QTabWidget(split);
    st_ = new QTableWidget(0, 9, tabs);
    st_->setHorizontalHeaderLabels({t("xl.c.pk"), t("xl.c.ground"), t("ww.lbl.bottom"), t("xl.c.grade"), t("gui.ww.col.yn"), t("gui.ww.col.v"),
                                    t("ww.lbl.water"), t("xl.c.cutA"), t("xl.c.fillA")});
    st_->verticalHeader()->setVisible(false);
    st_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    quant_ = new QListWidget(tabs);
    checks_ = new QListWidget(tabs);
    checks_->setWordWrap(true);
    connect(checks_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        const QVariant v = it->data(Qt::UserRole);
        if (v.isValid()) emit locateRequested(v.toPointF().x(), v.toPointF().y());
    });
    tabs->addTab(st_, t("gui.rd.stations"));
    tabs->addTab(quant_, t("gui.rd.quant"));
    tabs->addTab(checks_, t("gui.bim.checks"));
    split->setStretchFactor(2, 3);
    split->setStretchFactor(3, 2);
    split->setSizes({240, 310, 600, 520});

    // ---- past
    auto* bottom = new QHBoxLayout;
    auto* scratch = new QPushButton(t("gui.gen.scratch"), this);
    scratch->setToolTip(t("gui.gen.scratch.tip"));
    bottom->addWidget(scratch);
    connect(scratch, &QPushButton::clicked, this, [this] { emit scratchRequested(); });
    summary_ = new QLabel(this);
    summary_->setObjectName("title");
    bottom->addWidget(summary_, 1);
    auto* xlStatus = new QLabel(this);
    xlStatus->setObjectName("muted");
    bottom->addWidget(xlStatus);
    const excel::Tr tr = [this](const std::string& k, const std::vector<std::string>& a) { return loc_.tr(k, a); };
    excel_ = new ExcelLink(
        loc_, this, "suv_yoli", [this, tr] { return excel::wwWorkbook(m_, p_, tr); },
        [this, tr](const xlsx::Workbook& wb) {
            excel::wwFromWorkbook(wb, m_, p_, tr);
            paramsToUi();
            fillTables();
            render();
        });
    excel_->setStatusLabel(xlStatus);
    excel_->addButtons(bottom);
    auto* draw = new QPushButton(t("gui.rd.draw"), this);
    draw->setObjectName("accent");
    auto* close = new QPushButton(QString::fromUtf8("✕"), this);
    close->setFixedWidth(44);
    bottom->addWidget(draw);
    bottom->addWidget(close);
    root->addLayout(bottom);

    connect(rereadB, &QPushButton::clicked, this, &WaterwayDialog::reread);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(draw, &QPushButton::clicked, this, [this] {
        const QString path = QDir::temp().filePath("asiacad-waterway.json");
        std::ofstream f(path.toStdString(), std::ios::binary | std::ios::trunc);
        f << civil::wwToJson(m_, p_);
        f.close();
        emit drawRequested(path);
    });
    for (auto* s : {b_, h_, m_s, B_, d_, t_, n_, fb_, vmin_, vmax_, piece_, chw_, q_, head_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) { readParams(); modelChanged(); } });
    // loyiha belgisi yo'q bo'lsa tub chuqurlik/nishablikdan quriladi — qayta o'qish
    for (auto* s : {step_, depth_, slope_, ground_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) reread(); });
    connect(type_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_) return;
        readParams();
        paramsToUi();
        reread();
    });
    connect(vexag_, &QDoubleSpinBox::valueChanged, this, [this] { render(); });
    connect(st_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (updating_ || it->column() != 2) return;
        bool ok = false;
        const double z = it->text().replace(',', '.').toDouble(&ok);
        if (ok) setBottom(it->row(), z);
    });
}

void WaterwayDialog::showTab(int i) { midTabs_->setCurrentIndex(i); }

void WaterwayDialog::fillLayers() {
    updating_ = true;
    layers_->setRowCount(0);
    for (const auto& [name, k] : civil::autoWwLayers(doc_)) {
        if (name.rfind("SUV_3D", 0) == 0 || name == "SUV_PROFIL") continue;
        const int r = layers_->rowCount();
        layers_->insertRow(r);
        auto* it = new QTableWidgetItem(QString::fromStdString(name));
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        layers_->setItem(r, 0, it);
        auto* cb = new QComboBox(layers_);
        for (WwLayer kk : {WwLayer::Ignore, WwLayer::Axis, WwLayer::Ground, WwLayer::Design})
            cb->addItem(t((std::string("ww.layer.") + civil::wwLayerKey(kk)).c_str()), QString::fromLatin1(civil::wwLayerKey(kk)));
        cb->setCurrentIndex(std::max(0, cb->findData(QString::fromLatin1(civil::wwLayerKey(k)))));
        connect(cb, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) reread(); });
        layers_->setCellWidget(r, 1, cb);
    }
    updating_ = false;
}

std::map<std::string, WwLayer> WaterwayDialog::layerMap() const {
    std::map<std::string, WwLayer> m;
    for (int r = 0; r < layers_->rowCount(); ++r) {
        WwLayer k = WwLayer::Ignore;
        if (auto* cb = qobject_cast<QComboBox*>(layers_->cellWidget(r, 1))) civil::wwLayerFromKey(cb->currentData().toString().toStdString(), k);
        m[layers_->item(r, 0)->text().toStdString()] = k;
    }
    return m;
}

void WaterwayDialog::paramsToUi() {
    updating_ = true;
    const auto& s = p_.sec;
    type_->setCurrentIndex(std::max(0, type_->findData(QString::fromLatin1(civil::wwTypeKey(s.type)))));
    b_->setValue(s.b);
    h_->setValue(s.h);
    m_s->setValue(s.m);
    B_->setValue(s.B);
    d_->setValue(s.d);
    t_->setValue(s.t);
    n_->setValue(s.n);
    fb_->setValue(s.freeboard);
    vmin_->setValue(s.vmin);
    vmax_->setValue(s.vmax);
    piece_->setValue(s.pieceLen);
    chw_->setValue(s.chw);
    q_->setValue(p_.q);
    step_->setValue(p_.step);
    depth_->setValue(p_.depth);
    slope_->setValue(p_.slope * 1000);
    head_->setValue(p_.headStart);
    ground_->setValue(p_.ground);
    const bool pipe = !civil::wwOpen(s.type);
    for (QWidget* w : {static_cast<QWidget*>(b_), static_cast<QWidget*>(m_s), static_cast<QWidget*>(fb_)}) w->setEnabled(!pipe && s.B <= 0);
    fb_->setEnabled(!pipe);
    B_->setEnabled(s.type == WwType::Flume);
    piece_->setEnabled(s.type == WwType::Flume);
    h_->setEnabled(!pipe);
    d_->setEnabled(pipe);
    chw_->setEnabled(s.type == WwType::PressurePipe);
    head_->setEnabled(s.type == WwType::PressurePipe);
    depth_->setEnabled(pipe);
    secName_->setText(QString::fromStdString(s.name));
    updating_ = false;
}

void WaterwayDialog::readParams() {
    auto& s = p_.sec;
    civil::wwTypeFromKey(type_->currentData().toString().toStdString(), s.type);
    s.b = b_->value();
    s.h = h_->value();
    s.m = m_s->value();
    s.B = s.type == WwType::Flume ? B_->value() : 0;
    s.d = d_->value();
    if (!civil::wwOpen(s.type)) s.h = s.d;
    s.t = t_->value();
    s.n = n_->value();
    s.freeboard = fb_->value();
    s.vmin = vmin_->value();
    s.vmax = vmax_->value();
    s.pieceLen = piece_->value();
    s.chw = chw_->value();
    p_.q = q_->value();
    p_.step = step_->value();
    p_.depth = depth_->value();
    p_.slope = slope_->value() / 1000;
    p_.headStart = head_->value();
    p_.ground = ground_->value();
}

void WaterwayDialog::applySection(const civil::WwSection& s) {
    p_.sec = s;
    paramsToUi();
    if (m_.designPoints == 0) reread();  // tub chuqurlikdan quriladi — kesimga bog'liq
    else modelChanged();
}

void WaterwayDialog::setFlow(double q) {
    p_.q = q;
    paramsToUi();
    modelChanged();
}

void WaterwayDialog::reread() {
    if (layers_->rowCount() == 0) fillLayers();
    readParams();
    m_ = civil::buildWaterway(doc_, layerMap(), p_);
    fillTables();
    render();
}

void WaterwayDialog::modelChanged() {
    civil::recomputeWaterway(m_, p_);
    fillTables();
    render();
}

void WaterwayDialog::setBottom(int i, double z) {
    if (i < 0 || i >= static_cast<int>(m_.st.size())) return;
    m_.st[static_cast<std::size_t>(i)].bottom = z;
    m_.st[static_cast<std::size_t>(i)].control = true;
    modelChanged();
}

void WaterwayDialog::fillTables() {
    updating_ = true;
    BulkFill bulk(st_);
    st_->setRowCount(0);
    for (std::size_t i = 0; i < m_.st.size(); ++i) {
        const auto& s = m_.st[i];
        const int r = st_->rowCount();
        st_->insertRow(r);
        const QStringList cells{QString::fromStdString(civil::pk(s.s)), f2(s.ground), f2(s.bottom), f2(s.slope * 1000),
                                s.depthN >= 0 ? f3(s.depthN) : QStringLiteral("—"), f2(s.v), f2(s.water), f2(s.cut), f2(s.fill)};
        for (int k = 0; k < cells.size(); ++k) {
            auto* it = new QTableWidgetItem(cells[k]);
            if (k != 2) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            else it->setBackground(QColor(255, 242, 204, s.control ? 110 : 40));
            if (k == 3 && p_.sec.type != WwType::PressurePipe && s.slope <= 0) it->setForeground(QColor(232, 70, 80));
            if (k == 5 && (s.v > p_.sec.vmax || (s.v > 0 && s.v < p_.sec.vmin))) it->setForeground(QColor(232, 70, 80));
            st_->setItem(r, k, it);
        }
    }
    quant_->clear();
    quant_->addItem(t("gui.ww.q.len") + ": " + f2(m_.length) + " m");
    quant_->addItem(t("xl.c.cutV") + ": " + f1(m_.cutVolume) + " m³");
    quant_->addItem(t("xl.c.fillV") + ": " + f1(m_.fillVolume) + " m³");
    if (m_.concrete > 0) quant_->addItem(t("gui.ww.q.concrete") + ": " + f1(m_.concrete) + " m³");
    if (p_.sec.type == WwType::Flume) quant_->addItem(t("gui.ww.q.pieces") + ": " + QString::number(m_.pieces));
    if (p_.sec.type != WwType::PressurePipe) quant_->addItem(t("gui.ww.q.capacity") + ": " + f3(m_.capacity) + " m³/s");
    else quant_->addItem(t("gui.ww.q.headloss") + ": " + f2(m_.headLoss) + " m");
    if (!m_.st.empty())
        quant_->addItem(t("ww.msg.levels", {f2(m_.st.front().bottom), f2(m_.st.back().bottom), f2(m_.st.front().ground), f2(m_.st.back().ground)}));
    checks_->clear();
    for (const auto& is : m_.issues) {
        auto* it = new QListWidgetItem(QString::fromStdString(loc_.tr(is.error ? "sewer.lvl.error" : "sewer.lvl.warning") + ": " + loc_.tr(is.key, is.args)));
        it->setForeground(is.error ? QColor(232, 70, 80) : QColor(230, 160, 30));
        if (!m_.st.empty()) {
            const auto& q = *std::min_element(m_.st.begin(), m_.st.end(), [&](const auto& a, const auto& b) { return std::abs(a.s - is.s) < std::abs(b.s - is.s); });
            it->setData(Qt::UserRole, QPointF(q.x, q.y));
            it->setToolTip(t("gui.gen.locate"));
        }
        checks_->addItem(it);
    }
    if (m_.issues.empty()) checks_->addItem(t("gui.bim.ok"));
    summary_->setText(t("gui.ww.summary", {t((std::string("ww.type.") + civil::wwTypeKey(p_.sec.type)).c_str()), f1(m_.length), f3(p_.q),
                                           f3(m_.capacity), QString::number(m_.cutVolume, 'f', 0), f1(m_.concrete),
                                           QString::number(m_.errors())}));
    profile_->setModel(&m_, &p_, {t("road.lbl.ground"), t("ww.lbl.bottom"), t("ww.lbl.water"), t("gui.ww.lbl.top")});
    updating_ = false;
}

void WaterwayDialog::render() {
    view_->beginScene(t("gui.ww.title"), summary_->text() + QStringLiteral("  ·  ×%1").arg(vexag_->value(), 0, 'f', 0));
    if (m_.st.size() >= 2) {
        double zs = 0;
        for (const auto& s : m_.st) zs += s.ground;
        const double z0 = zs / m_.st.size(), k = vexag_->value();
        auto W = [&](const civil::WwStation& s, double o, double z) { return P3{s.x - s.dy * o, s.y + s.dx * o, z0 + (z - z0) * k}; };
        const auto& sec = p_.sec;
        const bool pipe = !civil::wwOpen(sec.type);
        if (pipe) {
            std::vector<P3> crown, inv;
            for (const auto& s : m_.st) {
                crown.push_back(W(s, 0, s.bottom + sec.d));
                inv.push_back(W(s, 0, s.bottom));
                std::vector<P3> ring;
                for (int i = 0; i <= 16; ++i) {
                    const double a = 2 * 3.14159265358979 * i / 16;
                    ring.push_back(W(s, sec.d / 2 * std::cos(a), s.bottom + sec.d / 2 + sec.d / 2 * std::sin(a)));
                }
                view_->addPolyline(ring, typeColor(sec.type), 1.6);
            }
            for (std::size_t i = 0; i + 1 < m_.st.size(); ++i) {
                const auto& a = m_.st[i];
                const auto& b = m_.st[i + 1];
                for (int side : {-1, 1})
                    view_->addFace({W(a, side * sec.d / 2, a.bottom + sec.d / 2), W(a, 0, a.bottom + sec.d), W(b, 0, b.bottom + sec.d),
                                    W(b, side * sec.d / 2, b.bottom + sec.d / 2)},
                                   typeColor(sec.type), 230, false);
                view_->addFace({W(a, -6, a.ground), W(a, 6, a.ground), W(b, 6, b.ground), W(b, -6, b.ground)}, kGround, 60, false);
            }
            view_->addPolyline(crown, typeColor(sec.type).lighter(130), 2.4);
            view_->addPolyline(inv, typeColor(sec.type).darker(130), 2.4);
        } else {
            // ichki yuza (tub va devorlar)
            std::vector<std::pair<double, double>> shape;
            if (sec.B > 0) {
                for (int i = -6; i <= 6; ++i) {
                    const double x = sec.B / 2 * i / 6.0;
                    shape.push_back({x, sec.h * (x / (sec.B / 2)) * (x / (sec.B / 2))});
                }
            } else {
                const double T = sec.b + 2 * sec.m * sec.h;
                shape = {{-T / 2, sec.h}, {-sec.b / 2, 0}, {sec.b / 2, 0}, {T / 2, sec.h}};
            }
            const QColor wall = sec.type == WwType::EarthDitch ? kEarth : sec.type == WwType::Flume ? kFlume : kConcrete;
            const double topT = sec.b + 2 * sec.m * sec.h;
            const double halfTop = sec.B > 0 ? sec.B / 2 : topT / 2;
            for (std::size_t i = 0; i + 1 < m_.st.size(); ++i) {
                const auto& a = m_.st[i];
                const auto& b = m_.st[i + 1];
                for (std::size_t j = 0; j + 1 < shape.size(); ++j)
                    view_->addFace({W(a, shape[j].first, a.bottom + shape[j].second), W(a, shape[j + 1].first, a.bottom + shape[j + 1].second),
                                    W(b, shape[j + 1].first, b.bottom + shape[j + 1].second), W(b, shape[j].first, b.bottom + shape[j].second)},
                                   wall, 255, false);
                if (a.depthN > 0 && b.depthN > 0) {
                    const double ta = civil::wwTopWidth(sec, a.depthN) / 2, tb = civil::wwTopWidth(sec, b.depthN) / 2;
                    view_->addFace({W(a, -ta, a.water), W(a, ta, a.water), W(b, tb, b.water), W(b, -tb, b.water)}, kWater, 170, false);
                }
                for (int side : {-1, 1})  // qirg'oqdan tashqarida yer
                    view_->addFace({W(a, side * (halfTop + sec.t), a.bottom + sec.h), W(a, side * (halfTop + 10), a.ground),
                                    W(b, side * (halfTop + 10), b.ground), W(b, side * (halfTop + sec.t), b.bottom + sec.h)},
                                   kGround, 90, false);
            }
        }
        for (const auto& is : m_.issues) {
            if (!is.error) continue;
            for (const auto& s : m_.st)
                if (std::abs(s.s - is.s) < 0.01) {
                    view_->addPolyline({W(s, 0, s.bottom), W(s, 0, s.bottom + 4)}, QColor(255, 60, 60), 3);
                    view_->addLabel(W(s, 0, s.bottom + 4.5), QStringLiteral("⚠ ") + QString::fromStdString(civil::pk(s.s)));
                    break;
                }
        }
    }
    view_->endScene(true);
}

} // namespace asiacad::gui
