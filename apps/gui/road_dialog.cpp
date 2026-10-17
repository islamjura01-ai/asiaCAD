#include "road_dialog.h"
#include "table_bulk.h"

#include <asiacad/civil/excel.h>

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
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace asiacad::gui {

using civil::RoadLayer;

namespace {
QString f2(double v) { return QString::number(v, 'f', 2); }
QString f1(double v) { return QString::number(v, 'f', 1); }
const QColor kAsphalt(70, 72, 78), kShoulder(150, 150, 140), kFill(120, 160, 80), kCut(170, 120, 70), kGround(120, 110, 90);
} // namespace

// ---------------------------------------------------------------- profil
void RoadProfileView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor bg = dark_ ? QColor(18, 22, 34) : QColor(250, 250, 252), fg = dark_ ? QColor(200, 205, 220) : QColor(40, 40, 50);
    p.fillRect(rect(), bg);
    if (!m_ || m_->st.size() < 2) return;
    double zmin = 1e18, zmax = -1e18;
    for (const auto& s : m_->st) {
        zmin = std::min({zmin, s.ground, s.design});
        zmax = std::max({zmax, s.ground, s.design});
    }
    zmin = std::floor(zmin - 1);
    zmax = std::ceil(zmax + 1);
    const double L = m_->st.back().s;
    const QRectF r = QRectF(rect()).adjusted(56, 30, -16, -34);
    auto P = [&](double s, double z) { return QPointF(r.left() + s / L * r.width(), r.bottom() - (z - zmin) / (zmax - zmin) * r.height()); };
    p.setPen(QPen(fg.darker(180), 1));
    for (double z = zmin; z <= zmax + 1e-9; z += std::max(1.0, std::round((zmax - zmin) / 6))) {
        p.drawLine(P(0, z), P(L, z));
        p.drawText(QRectF(0, P(0, z).y() - 8, 52, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(z, 'f', 0));
    }
    // kesma/to'kma soyasi
    for (std::size_t i = 0; i + 1 < m_->st.size(); ++i) {
        const auto& a = m_->st[i];
        const auto& b = m_->st[i + 1];
        QPolygonF poly{P(a.s, a.ground), P(b.s, b.ground), P(b.s, b.design), P(a.s, a.design)};
        QColor c = (a.design + b.design) / 2 > (a.ground + b.ground) / 2 ? kFill : kCut;
        c.setAlpha(90);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPolygon(poly);
    }
    QPainterPath g, d;
    for (std::size_t i = 0; i < m_->st.size(); ++i) {
        const auto& s = m_->st[i];
        if (i == 0) { g.moveTo(P(s.s, s.ground)); d.moveTo(P(s.s, s.design)); }
        else { g.lineTo(P(s.s, s.ground)); d.lineTo(P(s.s, s.design)); }
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(170, 120, 70), 2));
    p.drawPath(g);
    p.setPen(QPen(QColor(235, 70, 70), 2.4));
    p.drawPath(d);
    p.setPen(fg);
    for (const auto& s : m_->st) {
        if (s.control) {
            p.setBrush(QColor(235, 70, 70));
            p.drawEllipse(P(s.s, s.design), 3.5, 3.5);
        }
        const bool hundred = std::abs(std::fmod(s.s, 100.0)) < 0.01;
        if (hundred) p.drawText(QRectF(P(s.s, zmin).x() - 30, r.bottom() + 4, 60, 14), Qt::AlignCenter, QString::number(s.s / 100, 'f', 0));
    }
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(170, 120, 70));
    p.drawText(QPointF(r.left(), 18), "— " + g_);
    p.setPen(QColor(235, 70, 70));
    p.drawText(QPointF(r.left() + 160, 18), "— " + d_);
}

// ---------------------------------------------------------------- dialog
RoadDialog::RoadDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent)
    : QDialog(parent), loc_(loc), doc_(doc) {
    setWindowTitle(t("gui.rd.title"));
    resize(1640, 920);
    p_ = civil::defaultRoadParams();
    buildUi();
    view_->setDark(dark);
    profile_->setDark(dark);
    paramsToUi();
    fillLayers();
    reread();
}

QString RoadDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

void RoadDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    side_ = new TemplateSidebar(t("gui.side.roads"), t("gui.side.search"), t("gui.side.apply"), t("gui.side.hint"), split);
    side_->addSection(t("gui.rd.sec_cat"));
    for (const auto& c : civil::roadCategories()) {
        const QString nm = t(("road.cat." + c.id).c_str());
        side_->addTemplate(nm.section(" (", 0, 0), QStringLiteral("%1×%2 m · %3 m · ≤%4 ‰ · R≥%5")
                                                       .arg(c.lanes).arg(c.laneWidth).arg(c.shoulder).arg(c.maxGrade * 1000).arg(c.minRadius),
                           QString("cat:") + QString::fromStdString(c.id), QColor(70, 72, 78));
    }
    side_->addSection(t("gui.rd.sec_pav"));
    side_->addTemplate(t("gui.rd.pav_cap"), "5+7+8 / 20 / 30 cm", QString("pav:cap"), QColor(40, 40, 46));
    side_->addTemplate(t("gui.rd.pav_light"), "5+7 / 20 / 30 cm", QString("pav:light"), QColor(90, 90, 96));
    side_->addTemplate(t("gui.rd.pav_gravel"), "15 / 25 cm", QString("pav:gravel"), QColor(150, 140, 120));
    connect(side_, &TemplateSidebar::templateActivated, this, [this](const QVariant& v) {
        const QString s = v.toString();
        if (s.startsWith("cat:")) applyCategory(s.mid(4));
        else {
            if (s == "pav:cap")
                p_.pavement = {{"road.pav.ab_fine", 0.05, true}, {"road.pav.ab_coarse", 0.07, true}, {"road.pav.ab_coarse", 0.08, true},
                               {"road.pav.crushed", 0.20, false}, {"road.pav.sandgravel", 0.30, false}};
            else if (s == "pav:light")
                p_.pavement = {{"road.pav.ab_fine", 0.05, true}, {"road.pav.ab_coarse", 0.07, true}, {"road.pav.crushed", 0.20, false},
                               {"road.pav.sandgravel", 0.30, false}};
            else
                p_.pavement = {{"road.pav.crushed", 0.15, false}, {"road.pav.sandgravel", 0.25, false}};
            fillPavement();
            modelChanged();
        }
    });

    auto* scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setObjectName("sewerLeft");
    scroll->setMinimumWidth(290);
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
    layers_->setMinimumHeight(150);
    layers_->setMaximumHeight(190);
    lv->addWidget(layers_);
    auto* rereadB = new QPushButton(t("gui.ut.reread"), left);
    lv->addWidget(rereadB);
    title("gui.rd.section");
    catLbl_ = new QLabel(left);
    catLbl_->setObjectName("muted");
    lv->addWidget(catLbl_);
    auto* form = new QFormLayout;
    auto dspin = [&](double lo, double hi, double step, int dec) {
        auto* s = new QDoubleSpinBox(left);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        return s;
    };
    lanes_ = new QSpinBox(left);
    lanes_->setRange(1, 8);
    laneW_ = dspin(2.5, 4.5, 0.25, 2);
    shoulder_ = dspin(0, 5, 0.25, 2);
    strength_ = dspin(0, 2, 0.25, 2);
    crossfall_ = dspin(0, 6, 0.5, 1);
    shfall_ = dspin(0, 8, 0.5, 1);
    slopeF_ = dspin(1, 4, 0.25, 2);
    slopeC_ = dspin(1, 4, 0.25, 2);
    step_ = dspin(5, 100, 5, 0);
    raise_ = dspin(0, 5, 0.1, 2);
    ground_ = dspin(-500, 5000, 0.5, 2);
    maxGrade_ = dspin(5, 150, 5, 0);
    minR_ = dspin(10, 5000, 50, 0);
    form->addRow(t("gui.rd.lanes"), lanes_);
    form->addRow(t("gui.rd.lanew"), laneW_);
    form->addRow(t("gui.rd.shoulder"), shoulder_);
    form->addRow(t("gui.rd.strength"), strength_);
    form->addRow(t("gui.rd.crossfall"), crossfall_);
    form->addRow(t("gui.rd.shfall"), shfall_);
    auto* slopes = new QWidget(left);
    auto* sh = new QHBoxLayout(slopes);
    sh->setContentsMargins(0, 0, 0, 0);
    sh->addWidget(slopeF_);
    sh->addWidget(slopeC_);
    form->addRow(t("gui.rd.slope"), slopes);
    form->addRow(t("gui.rd.maxgrade"), maxGrade_);
    form->addRow(t("gui.rd.minradius"), minR_);
    form->addRow(t("gui.rd.step"), step_);
    form->addRow(t("gui.rd.raise"), raise_);
    form->addRow(t("gui.ut.ground"), ground_);
    lv->addLayout(form);
    title("gui.rd.pavement");
    pav_ = new QTableWidget(0, 2, left);
    pav_->setHorizontalHeaderLabels({t("xl.c.name"), t("xl.c.thick")});
    pav_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    pav_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    pav_->verticalHeader()->setVisible(false);
    pav_->setMinimumHeight(150);
    lv->addWidget(pav_);
    auto* note = new QLabel(t("gui.rd.note"), left);
    note->setWordWrap(true);
    note->setObjectName("muted");
    lv->addWidget(note);
    lv->addStretch(1);

    // ---- o'rta: 3D va profil
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
    profile_ = new RoadProfileView(midTabs_);
    midTabs_->addTab(v3, "3D");
    midTabs_->addTab(profile_, t("gui.rd.profile"));
    mv->addWidget(midTabs_, 1);

    // ---- o'ng
    auto* tabs = new QTabWidget(split);
    st_ = new QTableWidget(0, 7, tabs);
    st_->setHorizontalHeaderLabels({t("xl.c.pk"), t("xl.c.ground"), t("xl.c.design"), t("xl.c.workz"), t("xl.c.grade"), t("xl.c.fillA"), t("xl.c.cutA")});
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
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 0);
    split->setStretchFactor(2, 3);
    split->setStretchFactor(3, 2);
    split->setSizes({230, 300, 600, 520});

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
        loc_, this, "yol_piketlar", [this, tr] { return excel::roadWorkbook(m_, p_, tr); },
        [this, tr](const xlsx::Workbook& wb) {
            excel::roadFromWorkbook(wb, m_, p_, tr);
            paramsToUi();
            fillPavement();
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

    connect(rereadB, &QPushButton::clicked, this, &RoadDialog::reread);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(draw, &QPushButton::clicked, this, [this] {
        const QString path = QDir::temp().filePath("asiacad-road.json");
        std::ofstream f(path.toStdString(), std::ios::binary | std::ios::trunc);
        f << civil::roadToJson(m_, p_);
        f.close();
        emit drawRequested(path);
    });
    for (auto* s : {laneW_, shoulder_, strength_, crossfall_, shfall_, slopeF_, slopeC_, maxGrade_, minR_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) { readParams(); modelChanged(); } });
    connect(lanes_, &QSpinBox::valueChanged, this, [this] { if (!updating_) { readParams(); modelChanged(); } });
    for (auto* s : {step_, raise_, ground_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) reread(); });
    connect(vexag_, &QDoubleSpinBox::valueChanged, this, [this] { render(); });
    connect(st_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (updating_ || it->column() != 2) return;
        bool ok = false;
        const double z = it->text().replace(',', '.').toDouble(&ok);
        if (ok) setDesign(it->row(), z);
    });
    connect(pav_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (updating_ || it->column() != 1 || it->row() >= static_cast<int>(p_.pavement.size())) return;
        bool ok = false;
        const double v = it->text().replace(',', '.').toDouble(&ok);
        if (ok && v >= 0) {
            p_.pavement[static_cast<std::size_t>(it->row())].thickness = v;
            modelChanged();
        }
    });
}

void RoadDialog::showTab(int i) { midTabs_->setCurrentIndex(i); }

void RoadDialog::fillLayers() {
    updating_ = true;
    layers_->setRowCount(0);
    for (const auto& [name, k] : civil::autoRoadLayers(doc_)) {
        if (name.rfind("YOL_3D", 0) == 0 || name == "YOL_PROFIL") continue;
        const int r = layers_->rowCount();
        layers_->insertRow(r);
        auto* it = new QTableWidgetItem(QString::fromStdString(name));
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        layers_->setItem(r, 0, it);
        auto* cb = new QComboBox(layers_);
        for (RoadLayer kk : {RoadLayer::Ignore, RoadLayer::Axis, RoadLayer::Ground, RoadLayer::Design})
            cb->addItem(t((std::string("road.layer.") + civil::roadLayerKey(kk)).c_str()), QString::fromLatin1(civil::roadLayerKey(kk)));
        cb->setCurrentIndex(std::max(0, cb->findData(QString::fromLatin1(civil::roadLayerKey(k)))));
        connect(cb, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) reread(); });
        layers_->setCellWidget(r, 1, cb);
    }
    updating_ = false;
}

std::map<std::string, RoadLayer> RoadDialog::layerMap() const {
    std::map<std::string, RoadLayer> m;
    for (int r = 0; r < layers_->rowCount(); ++r) {
        RoadLayer k = RoadLayer::Ignore;
        if (auto* cb = qobject_cast<QComboBox*>(layers_->cellWidget(r, 1))) civil::roadLayerFromKey(cb->currentData().toString().toStdString(), k);
        m[layers_->item(r, 0)->text().toStdString()] = k;
    }
    return m;
}

void RoadDialog::paramsToUi() {
    updating_ = true;
    lanes_->setValue(p_.cat.lanes);
    laneW_->setValue(p_.cat.laneWidth);
    shoulder_->setValue(p_.cat.shoulder);
    strength_->setValue(p_.cat.strengthened);
    crossfall_->setValue(p_.crossfall * 100);
    shfall_->setValue(p_.shoulderFall * 100);
    slopeF_->setValue(p_.slopeFill);
    slopeC_->setValue(p_.slopeCut);
    maxGrade_->setValue(p_.cat.maxGrade * 1000);
    minR_->setValue(p_.cat.minRadius);
    step_->setValue(p_.step);
    raise_->setValue(p_.raise);
    ground_->setValue(p_.ground);
    catLbl_->setText(t(("road.cat." + p_.cat.id).c_str()));
    updating_ = false;
    fillPavement();
}

void RoadDialog::readParams() {
    p_.cat.lanes = lanes_->value();
    p_.cat.laneWidth = laneW_->value();
    p_.cat.shoulder = shoulder_->value();
    p_.cat.strengthened = strength_->value();
    p_.crossfall = crossfall_->value() / 100;
    p_.shoulderFall = shfall_->value() / 100;
    p_.slopeFill = slopeF_->value();
    p_.slopeCut = slopeC_->value();
    p_.cat.maxGrade = maxGrade_->value() / 1000;
    p_.cat.minRadius = minR_->value();
    p_.step = step_->value();
    p_.raise = raise_->value();
    p_.ground = ground_->value();
}

void RoadDialog::fillPavement() {
    const bool was = updating_;
    updating_ = true;
    pav_->setRowCount(0);
    for (const auto& l : p_.pavement) {
        const int r = pav_->rowCount();
        pav_->insertRow(r);
        auto* n = new QTableWidgetItem(l.name.rfind("road.", 0) == 0 ? t(l.name.c_str()) : QString::fromStdString(l.name));
        n->setFlags(n->flags() & ~Qt::ItemIsEditable);
        pav_->setItem(r, 0, n);
        pav_->setItem(r, 1, new QTableWidgetItem(QString::number(l.thickness, 'f', 2)));
    }
    updating_ = was;
}

void RoadDialog::applyCategory(const QString& id) {
    for (const auto& c : civil::roadCategories())
        if (QString::fromStdString(c.id) == id) p_.cat = c;
    paramsToUi();
    modelChanged();
}

void RoadDialog::reread() {
    if (layers_->rowCount() == 0) fillLayers();
    readParams();
    m_ = civil::buildRoad(doc_, layerMap(), p_);
    fillTables();
    render();
}

void RoadDialog::modelChanged() {
    civil::recomputeRoad(m_, p_);
    fillTables();
    render();
}

void RoadDialog::setDesign(int i, double z) {
    if (i < 0 || i >= static_cast<int>(m_.st.size())) return;
    m_.st[static_cast<std::size_t>(i)].design = z;
    m_.st[static_cast<std::size_t>(i)].control = true;
    modelChanged();
}

void RoadDialog::fillTables() {
    updating_ = true;
    BulkFill bulk(st_);
    st_->setRowCount(0);
    std::map<double, int> bad;  // piket -> 2 xato / 1 ogohlantirish
    for (const auto& is : m_.issues) bad[is.s] = std::max(bad[is.s], is.error ? 2 : 1);
    for (std::size_t i = 0; i < m_.st.size(); ++i) {
        const auto& s = m_.st[i];
        const int r = st_->rowCount();
        st_->insertRow(r);
        const QStringList cells{QString::fromStdString(civil::pk(s.s)), f2(s.ground), f2(s.design), f2(s.design - s.ground), f1(s.grade * 1000),
                                f2(s.fillArea), f2(s.cutArea)};
        for (int k = 0; k < cells.size(); ++k) {
            auto* it = new QTableWidgetItem(cells[k]);
            if (k != 2) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            else it->setBackground(QColor(255, 242, 204, s.control ? 110 : 40));
            if (k == 4 && std::abs(s.grade) > p_.cat.maxGrade + 1e-9) it->setForeground(QColor(232, 70, 80));
            st_->setItem(r, k, it);
        }
    }
    quant_->clear();
    quant_->addItem(QStringLiteral("L = %1 m").arg(f2(m_.length)));
    quant_->addItem(t("xl.c.fillV") + ": " + f1(m_.fillVolume));
    quant_->addItem(t("xl.c.cutV") + ": " + f1(m_.cutVolume));
    quant_->addItem(t("xl.asphalt") + ": " + f1(m_.asphaltArea));
    for (const auto& [name, v] : m_.pavementVolumes)
        quant_->addItem((name.rfind("road.", 0) == 0 ? t(name.c_str()) : QString::fromStdString(name)) + ": " + f1(v) + " m³");
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
    summary_->setText(t("gui.rd.summary", {f1(m_.length), QString::number(m_.st.size()), QString::number(m_.fillVolume, 'f', 0),
                                           QString::number(m_.cutVolume, 'f', 0), QString::number(m_.errors())}));
    profile_->setModel(&m_, t("road.lbl.ground"), t("road.lbl.design"));
    updating_ = false;
}

void RoadDialog::render() {
    view_->beginScene(t("gui.rd.title"), summary_->text() + QStringLiteral("  ·  ×%1").arg(vexag_->value(), 0, 'f', 0));
    if (m_.st.size() >= 2) {
        double zs = 0;
        for (const auto& s : m_.st) zs += s.ground;
        const double z0 = zs / m_.st.size(), k = vexag_->value();
        auto W = [&](const civil::RoadStation& s, double o, double z) { return P3{s.x - s.dy * o, s.y + s.dx * o, z0 + (z - z0) * k}; };
        const std::size_t n = m_.st.front().section.size();
        for (std::size_t i = 0; i + 1 < m_.st.size(); ++i) {
            const auto& a = m_.st[i];
            const auto& b = m_.st[i + 1];
            if (a.section.size() != n || b.section.size() != n) continue;
            for (std::size_t j = 0; j + 1 < n; ++j) {
                const bool carriage = j == n / 2 - 1 || j == n / 2;
                const bool shoulder = j == 1 || j + 2 == n;
                QColor c = carriage ? kAsphalt : shoulder ? kShoulder : kFill;
                if (!carriage && !shoulder) {
                    // qiyalik: oxiri yuqorida bo'lsa — kesma
                    const auto& toe = j == 0 ? a.section[0] : a.section[n - 1];
                    const auto& edge = j == 0 ? a.section[1] : a.section[n - 2];
                    c = toe.second > edge.second ? kCut : kFill;
                }
                view_->addFace({W(a, a.section[j].first, a.section[j].second), W(a, a.section[j + 1].first, a.section[j + 1].second),
                                W(b, b.section[j + 1].first, b.section[j + 1].second), W(b, b.section[j].first, b.section[j].second)},
                               c, 255, false);
            }
            // yer yuzasi (qiyalik oxiridan 15 m tashqariga)
            for (int side : {0, 1}) {
                const auto& ta = side == 0 ? a.section.front() : a.section.back();
                const auto& tb = side == 0 ? b.section.front() : b.section.back();
                const double sgn = side == 0 ? 1 : -1;
                view_->addFace({W(a, ta.first, ta.second), W(a, ta.first + sgn * 15, a.ground), W(b, tb.first + sgn * 15, b.ground),
                                W(b, tb.first, tb.second)},
                               kGround, 90, false);
            }
        }
        // chiziqlar: o'q (oq, uzuq) va qatnov chetlari
        std::vector<P3> axis, le, re;
        for (const auto& s : m_.st) {
            if (s.section.size() != n) continue;
            axis.push_back(W(s, 0, s.design + 0.01));
            le.push_back(W(s, s.section[n / 2 - 1].first, s.section[n / 2 - 1].second + 0.01));
            re.push_back(W(s, s.section[n / 2 + 1].first, s.section[n / 2 + 1].second + 0.01));
        }
        for (std::size_t i = 0; i + 1 < axis.size(); i += 2) view_->addPolyline({axis[i], axis[i + 1]}, QColor(245, 245, 245), 1.5);
        view_->addPolyline(le, QColor(245, 245, 245), 1.2);
        view_->addPolyline(re, QColor(245, 245, 245), 1.2);
        for (const auto& is : m_.issues) {
            if (!is.error) continue;
            for (const auto& s : m_.st)
                if (std::abs(s.s - is.s) < 0.01) {
                    view_->addPolyline({W(s, 0, s.design), W(s, 0, s.design + 6)}, QColor(255, 60, 60), 3);
                    view_->addLabel(W(s, 0, s.design + 6.5), QStringLiteral("⚠ ") + QString::fromStdString(civil::pk(s.s)));
                    break;
                }
        }
    }
    view_->endScene(true);
}

} // namespace asiacad::gui
