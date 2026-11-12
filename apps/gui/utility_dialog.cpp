#include "utility_dialog.h"
#include "table_bulk.h"

#include <asiacad/civil/excel.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
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

using civil::UtilKind;

QColor utilColor(UtilKind k) {
    switch (k) {
        case UtilKind::Water: return QColor(40, 120, 240);
        case UtilKind::Sewer: return QColor(150, 95, 50);
        case UtilKind::Storm: return QColor(40, 170, 150);
        case UtilKind::Gas: return QColor(240, 200, 30);
        case UtilKind::Heat: return QColor(235, 70, 60);
        case UtilKind::Power: return QColor(250, 130, 30);
        case UtilKind::Telecom: return QColor(170, 90, 220);
        case UtilKind::Drain: return QColor(110, 170, 70);
        default: return QColor(160, 160, 160);
    }
}

namespace {
struct Tpl { UtilKind k; const char* name; double dn, depth; };
const Tpl kTpl[] = {
    {UtilKind::Water, "PE100 SDR17 Ø63", 63, 1.5},     {UtilKind::Water, "PE100 SDR17 Ø110", 110, 1.5},
    {UtilKind::Water, "PE100 SDR17 Ø160", 160, 1.7},   {UtilKind::Water, "PE100 SDR17 Ø225", 225, 1.8},
    {UtilKind::Water, "VChShG Ø300", 300, 2.0},        {UtilKind::Sewer, "PVX SN4 Ø160", 160, 1.2},
    {UtilKind::Sewer, "PVX SN8 Ø200", 200, 1.5},       {UtilKind::Sewer, "PVX SN8 Ø250", 250, 1.8},
    {UtilKind::Sewer, "PP gofra SN8 Ø315", 315, 2.0},  {UtilKind::Storm, "PP gofra Ø300", 300, 1.0},
    {UtilKind::Storm, "Temir-beton Ø400", 400, 1.2},   {UtilKind::Storm, "Temir-beton Ø500", 500, 1.5},
    {UtilKind::Gas, "PE80 SDR11 Ø32", 32, 1.0},        {UtilKind::Gas, "PE80 SDR11 Ø63", 63, 1.0},
    {UtilKind::Gas, "PE100 SDR11 Ø110", 110, 1.2},     {UtilKind::Gas, "Poʻlat Ø159", 159, 1.0},
    {UtilKind::Heat, "2×Ø89 kanalda", 89, 0.8},        {UtilKind::Heat, "2×Ø159 kanalda", 159, 1.0},
    {UtilKind::Heat, "2×Ø273 kanalda", 273, 1.2},      {UtilKind::Power, "0,4 kV AVBbShv", 50, 0.7},
    {UtilKind::Power, "10 kV AAShv", 60, 0.7},         {UtilKind::Telecom, "Optik kabel PND Ø40", 40, 0.7},
    {UtilKind::Telecom, "Kabel kanalizatsiyasi Ø110", 110, 0.7}, {UtilKind::Drain, "Drenaj PVX Ø160", 160, 1.5}};
QString f2(double v) { return QString::number(v, 'f', 2); }
QString f3(double v) { return QString::number(v, 'f', 3); }
} // namespace

UtilityDialog::UtilityDialog(const i18n::Localizer& loc, const Document& doc, bool dark, QWidget* parent)
    : QDialog(parent), loc_(loc), doc_(doc) {
    setWindowTitle(t("gui.ut.title"));
    resize(1640, 900);
    p_ = civil::defaultUtilParams();
    buildUi();
    view_->setDark(dark);
    fillLayers();
    reread();
    // menyu tarixi: model (chiziqlar, Z, Ø, tur) va parametrlar — har o'zgarishdan keyin
    hist_ = std::make_unique<MenuHistory<State>>(
        this, [this] { return State{m_, p_}; }, [](const State& s) { return civil::utilToJson(s.m, s.p); },
        [this](const State& s) {
            m_ = s.m;
            p_ = s.p;
            updating_ = true;
            ground_->setValue(p_.ground);
            radius_->setValue(p_.labelRadius);
            gap_->setValue(p_.gapTol);
            clPipe_->setValue(p_.clearPipe);
            clCable_->setValue(p_.clearCable);
            updating_ = false;
            if (selected_ >= static_cast<int>(m_.lines.size())) selected_ = -1;
            fillTables();
            render();
        });
    hist_->reset();
}

QString UtilityDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

void UtilityDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    // ---- 1: shablonlar
    side_ = new TemplateSidebar(t("gui.side.utils"), t("gui.side.search"), t("gui.side.apply"), t("gui.ut.tpl_hint"), split);
    UtilKind last = UtilKind::Ignore;
    for (int i = 0; i < static_cast<int>(std::size(kTpl)); ++i) {
        const auto& x = kTpl[i];
        if (x.k != last) side_->addSection(t((std::string("util.kind.") + civil::utilKey(x.k)).c_str()));
        last = x.k;
        side_->addTemplate(QString::fromUtf8(x.name), QStringLiteral("Ø%1 · h=%2 m").arg(x.dn).arg(x.depth, 0, 'f', 1), i, utilColor(x.k));
    }
    connect(side_, &TemplateSidebar::templateActivated, this, &UtilityDialog::applyTemplate);

    // ---- 2: qatlamlar va parametrlar
    auto* scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setObjectName("sewerLeft");
    scroll->setMinimumWidth(320);
    auto* left = new QWidget;
    left->setObjectName("sewerLeftBody");
    scroll->setWidget(left);
    auto* lv = new QVBoxLayout(left);
    {
        std::vector<std::pair<QString, QString>> roles{{"AUTO", t("gui.ds.auto")}};
        for (UtilKind k : civil::utilKinds()) roles.push_back({QString::fromLatin1(civil::utilKey(k)), t((std::string("util.kind.") + civil::utilKey(k)).c_str())});
        roles.push_back({"GROUND", t("util.kind.GROUND")});
        set_ = new DrawingSetWidget(loc_, t("gui.ds.title_util"), roles, false, left);
        lv->addWidget(set_);
        useDoc_ = new QCheckBox(t("gui.ds.usedoc"), left);
        useDoc_->setChecked(true);
        lv->addWidget(useDoc_);
        connect(set_, &DrawingSetWidget::changed, this, [this] { reread(); });
        connect(useDoc_, &QCheckBox::toggled, this, [this] { reread(); });
    }
    auto* lt = new QLabel(t("gui.ut.layers"), left);
    lt->setObjectName("title");
    lv->addWidget(lt);
    layers_ = new QTableWidget(0, 2, left);
    layers_->setHorizontalHeaderLabels({t("gui.bim.col.layer"), t("gui.bim.col.kind")});
    layers_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layers_->verticalHeader()->setVisible(false);
    layers_->setMinimumHeight(220);
    lv->addWidget(layers_);
    auto* rereadB = new QPushButton(t("gui.ut.reread"), left);
    lv->addWidget(rereadB);
    auto* pt = new QLabel(t("gui.ut.params"), left);
    pt->setObjectName("title");
    lv->addWidget(pt);
    auto* form = new QFormLayout;
    auto dspin = [&](double lo, double hi, double step, int dec, double v) {
        auto* s = new QDoubleSpinBox(left);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        s->setValue(v);
        return s;
    };
    ground_ = dspin(-500, 5000, 0.1, 2, p_.ground);
    radius_ = dspin(0.2, 50, 0.5, 1, p_.labelRadius);
    gap_ = dspin(0.05, 20, 0.1, 2, p_.gapTol);
    clPipe_ = dspin(0, 3, 0.05, 2, p_.clearPipe);
    clCable_ = dspin(0, 3, 0.05, 2, p_.clearCable);
    vexag_ = dspin(1, 50, 1, 0, 5);
    vexag_->setMinimumWidth(64);
    form->addRow(t("gui.ut.ground"), ground_);
    form->addRow(t("gui.ut.radius"), radius_);
    form->addRow(t("gui.ut.gap"), gap_);
    form->addRow(t("gui.ut.clpipe"), clPipe_);
    form->addRow(t("gui.ut.clcable"), clCable_);
    form->addRow(t("gui.ut.vexag"), vexag_);
    lv->addLayout(form);
    auto* note = new QLabel(t("gui.ut.note"), left);
    note->setWordWrap(true);
    note->setObjectName("muted");
    lv->addWidget(note);
    lv->addStretch(1);

    // ---- 3: 3D
    auto* mid = new QWidget(split);
    auto* mv = new QVBoxLayout(mid);
    mv->setContentsMargins(6, 0, 6, 0);
    auto* fl = new QGridLayout;
    fl->setHorizontalSpacing(10);
    fl->setVerticalSpacing(2);
    const auto kinds = civil::utilKinds();
    for (std::size_t i = 0; i < kinds.size() && i < 9; ++i) {
        show_[i] = new QCheckBox(t((std::string("util.kind.") + civil::utilKey(kinds[i])).c_str()), mid);
        show_[i]->setChecked(true);
        QPalette pal = show_[i]->palette();
        pal.setColor(QPalette::WindowText, utilColor(kinds[i]));
        show_[i]->setPalette(pal);
        show_[i]->setStyleSheet(QStringLiteral("QCheckBox{color:%1;}").arg(utilColor(kinds[i]).name()));
        fl->addWidget(show_[i], static_cast<int>(i / 5), static_cast<int>(i % 5));
        connect(show_[i], &QCheckBox::toggled, this, [this] { render(); });
    }
    fl->setColumnStretch(5, 1);
    mv->addLayout(fl);
    view_ = new Model3DView(mid);
    mv->addWidget(view_, 1);
    connect(view_, &Model3DView::objectPicked, this, &UtilityDialog::onPicked);

    // ---- 4: jadvallar
    auto* tabs = new QTabWidget(split);
    lines_ = new QTableWidget(0, 7, tabs);
    lines_->setHorizontalHeaderLabels({"ID", t("gui.bim.col.layer"), t("gui.bim.col.kind"), "Ø, mm", "L, m", t("xl.c.covermin"), t("xl.c.status")});
    lines_->verticalHeader()->setVisible(false);
    lines_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    lines_->horizontalHeader()->setStretchLastSection(true);
    lines_->setSelectionBehavior(QAbstractItemView::SelectRows);
    points_ = new QTableWidget(0, 7, tabs);
    points_->setHorizontalHeaderLabels({"№", "X", "Y", t("xl.c.zaxis"), t("xl.c.ground"), t("xl.c.depthtop"), t("xl.c.source")});
    points_->verticalHeader()->setVisible(false);
    points_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    points_->horizontalHeader()->setStretchLastSection(true);
    cross_ = new QTableWidget(0, 6, tabs);
    cross_->setHorizontalHeaderLabels({t("xl.c.linea"), t("xl.c.lineb"), "X", "Y", t("xl.c.clear"), t("xl.c.required")});
    cross_->verticalHeader()->setVisible(false);
    cross_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    cross_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    checks_ = new QListWidget(tabs);
    checks_->setWordWrap(true);
    connect(checks_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        const QVariant v = it->data(Qt::UserRole);
        if (v.isValid()) emit locateRequested(v.toPointF().x(), v.toPointF().y());
    });
    tabs->addTab(lines_, t("gui.ut.lines"));
    tabs->addTab(points_, t("gui.ut.points"));
    tabs->addTab(cross_, t("gui.ut.cross"));
    tabs->addTab(checks_, t("gui.bim.checks"));
    props_ = new PropPanel(tabs);
    props_->empty(t("gui.xp.hint"));
    tabs->addTab(props_, t("gui.xp.tab"));
    tabs_ = tabs;
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 0);
    split->setStretchFactor(2, 3);
    split->setStretchFactor(3, 2);
    split->setSizes({230, 300, 560, 560});
    scroll->setMinimumWidth(280);

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
        loc_, this, "yer_osti_tarmoqlari", [this, tr] { return excel::utilityWorkbook(m_, p_, tr); },
        [this, tr](const xlsx::Workbook& wb) {
            excel::utilityFromWorkbook(wb, m_, p_, tr);
            fillTables();
            render();
        });
    excel_->setStatusLabel(xlStatus);
    excel_->addButtons(bottom);
    auto* draw = new QPushButton(t("gui.ut.draw"), this);
    draw->setObjectName("accent");
    drawB_ = draw;
    auto* close = new QPushButton(QString::fromUtf8("✕"), this);
    close->setFixedWidth(44);
    bottom->addWidget(draw);
    bottom->addWidget(close);
    root->addLayout(bottom);

    connect(rereadB, &QPushButton::clicked, this, &UtilityDialog::reread);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(draw, &QPushButton::clicked, this, [this] {
        const QString path = QDir::temp().filePath("asiacad-utilities.json");
        std::ofstream f(path.toStdString(), std::ios::binary | std::ios::trunc);
        f << civil::utilToJson(m_, p_);
        f.close();
        emit drawRequested(path);
    });
    for (auto* s : {ground_, radius_, gap_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) reread(); });
    for (auto* s : {clPipe_, clCable_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) { readParams(); modelChanged(); } });
    connect(vexag_, &QDoubleSpinBox::valueChanged, this, [this] { render(); });
    connect(lines_, &QTableWidget::itemSelectionChanged, this, [this] {
        if (updating_) return;
        const auto rows = lines_->selectionModel()->selectedRows();
        selected_ = rows.isEmpty() ? -1 : rows.front().row();
        fillPoints();
        render();
    });
    connect(lines_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (updating_ || it->column() != 3) return;
        bool ok = false;
        const double d = it->text().replace(',', '.').toDouble(&ok);
        auto& ln = m_.lines[static_cast<std::size_t>(it->row())];
        if (ok && d > 0) { ln.dn = d; ln.dnManual = true; }
        modelChanged();
    });
    connect(points_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (updating_ || it->column() != 3 || selected_ < 0) return;
        bool ok = false;
        const double z = it->text().replace(',', '.').toDouble(&ok);
        if (ok) setZ(selected_, it->row(), z);
    });
    connect(checks_, &QListWidget::currentRowChanged, this, [this](int r) {
        if (updating_ || r < 0 || r >= static_cast<int>(m_.issues.size())) return;
        if (m_.issues[static_cast<std::size_t>(r)].line >= 0) selectLine(m_.issues[static_cast<std::size_t>(r)].line);
    });
}

void UtilityDialog::fillLayers() {
    updating_ = true;
    const auto lm = civil::autoUtilLayers(doc_);
    layers_->setRowCount(0);
    for (const auto& [name, k] : lm) {
        if (name.rfind("3D_", 0) == 0) continue;
        const int r = layers_->rowCount();
        layers_->insertRow(r);
        auto* it = new QTableWidgetItem(QString::fromStdString(name));
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        layers_->setItem(r, 0, it);
        auto* cb = new QComboBox(layers_);
        for (UtilKind kk : {UtilKind::Ignore, UtilKind::Water, UtilKind::Sewer, UtilKind::Storm, UtilKind::Gas, UtilKind::Heat, UtilKind::Power,
                            UtilKind::Telecom, UtilKind::Drain, UtilKind::Other, UtilKind::Ground})
            cb->addItem(t((std::string("util.kind.") + civil::utilKey(kk)).c_str()), QString::fromLatin1(civil::utilKey(kk)));
        cb->setCurrentIndex(std::max(0, cb->findData(QString::fromLatin1(civil::utilKey(k)))));
        connect(cb, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) reread(); });
        layers_->setCellWidget(r, 1, cb);
    }
    updating_ = false;
}

std::map<std::string, UtilKind> UtilityDialog::layerMap() const {
    std::map<std::string, UtilKind> m;
    for (int r = 0; r < layers_->rowCount(); ++r) {
        UtilKind k = UtilKind::Ignore;
        if (auto* cb = qobject_cast<QComboBox*>(layers_->cellWidget(r, 1))) civil::utilFromKey(cb->currentData().toString().toStdString(), k);
        m[layers_->item(r, 0)->text().toStdString()] = k;
    }
    return m;
}

void UtilityDialog::readParams() {
    p_.ground = ground_->value();
    p_.labelRadius = radius_->value();
    p_.gapTol = gap_->value();
    p_.clearPipe = clPipe_->value();
    p_.clearCable = clCable_->value();
}

void UtilityDialog::reread() {
    if (layers_->rowCount() == 0) fillLayers();
    readParams();
    std::vector<civil::UtilSource> src;
    if (useDoc_->isChecked()) src.push_back({&doc_, layerMap(), UtilKind::Ignore, ""});
    const auto sheets = set_->loaded();
    for (const auto& sh : sheets) {
        if (!sh.error.empty() || !sh.doc) continue;
        UtilKind k = UtilKind::Ignore;
        civil::utilFromKey(sh.file.role, k);
        src.push_back({sh.doc.get(), civil::autoUtilLayers(*sh.doc), k == UtilKind::Ground ? UtilKind::Ignore : k, civil::fileName(sh.file.path)});
        if (k == UtilKind::Ground)  // butun fayl — yer sathi
            for (const auto& [name, l] : sh.doc->layers()) src.back().layers[name] = UtilKind::Ground;
    }
    m_ = civil::readUtilities(src, p_);
    selected_ = -1;
    fillTables();
    render();
}

void UtilityDialog::modelChanged() {
    civil::checkUtilities(m_, p_);
    fillTables();
    render();
}

void UtilityDialog::setZ(int line, int vertex, double z) {
    if (line < 0 || line >= static_cast<int>(m_.lines.size())) return;
    auto& ln = m_.lines[static_cast<std::size_t>(line)];
    if (vertex < 0 || vertex >= static_cast<int>(ln.v.size())) return;
    ln.v[static_cast<std::size_t>(vertex)].z = z;
    ln.v[static_cast<std::size_t>(vertex)].src = civil::UtilVertex::Manual;
    modelChanged();
}

void UtilityDialog::selectLine(int index) {
    if (index < 0 || index >= lines_->rowCount()) return;
    lines_->selectRow(index);
    selected_ = index;
    fillPoints();
    render();
}

void UtilityDialog::applyTemplate(const QVariant& data) {
    const int i = data.toInt();
    if (i < 0 || i >= static_cast<int>(std::size(kTpl))) return;
    const auto& x = kTpl[i];
    std::vector<int> rows;
    for (const auto& r : lines_->selectionModel()->selectedRows()) rows.push_back(r.row());
    if (rows.empty() && selected_ >= 0) rows.push_back(selected_);
    for (int r : rows) {
        auto& ln = m_.lines[static_cast<std::size_t>(r)];
        ln.kind = x.k;
        ln.dn = x.dn;
        ln.dnManual = true;
        for (auto& v : ln.v) {
            v.z = isGravity(x.k) && v.src == civil::UtilVertex::FromLabel ? v.z : v.ground - x.depth - x.dn / 2000.0;
            if (v.src != civil::UtilVertex::FromLabel) v.src = civil::UtilVertex::Manual;
        }
    }
    modelChanged();
    if (!rows.empty()) selectLine(rows.front());
}

void UtilityDialog::fillTables() {
    updating_ = true;
    const int keep = selected_;
    BulkFill bulkLines(lines_), bulkCross(cross_);
    lines_->setRowCount(0);
    double total = 0;
    for (std::size_t i = 0; i < m_.lines.size(); ++i) {
        const auto& ln = m_.lines[i];
        const int r = lines_->rowCount();
        lines_->insertRow(r);
        total += ln.length();
        auto ro = [](const QString& s) {
            auto* it = new QTableWidgetItem(s);
            it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            return it;
        };
        lines_->setItem(r, 0, ro(QString::number(ln.id)));
        lines_->setItem(r, 1, ro(QString::fromStdString(ln.layer)));
        auto* cb = new QComboBox(lines_);
        for (UtilKind kk : civil::utilKinds()) cb->addItem(t((std::string("util.kind.") + civil::utilKey(kk)).c_str()), static_cast<int>(kk));
        cb->setCurrentIndex(std::max(0, cb->findData(static_cast<int>(ln.kind))));
        connect(cb, &QComboBox::currentIndexChanged, this, [this, i, cb] {
            if (updating_) return;
            m_.lines[i].kind = static_cast<UtilKind>(cb->currentData().toInt());
            modelChanged();
        });
        lines_->setCellWidget(r, 2, cb);
        lines_->setItem(r, 3, new QTableWidgetItem(QString::number(ln.dn)));
        lines_->setItem(r, 4, ro(f2(ln.length())));
        double cover = 1e9;
        for (const auto& v : ln.v) cover = std::min(cover, v.ground - v.z - ln.dn / 2000.0);
        lines_->setItem(r, 5, ro(f2(cover)));
        QString status;
        int lvl = 0;
        for (const auto& is : m_.issues)
            if (is.line == static_cast<int>(i)) {
                if (!status.isEmpty()) status += "; ";
                status += QString::fromStdString(loc_.tr(is.key, is.args));
                lvl = std::max(lvl, is.error ? 2 : 1);
            }
        auto* st = ro(status.isEmpty() ? "OK" : status);
        if (lvl) st->setForeground(lvl == 2 ? QColor(232, 70, 80) : QColor(230, 160, 30));
        lines_->setItem(r, 6, st);
        lines_->item(r, 0)->setForeground(utilColor(ln.kind));
    }
    cross_->setRowCount(0);
    for (const auto& c : m_.crossings) {
        const int r = cross_->rowCount();
        cross_->insertRow(r);
        const auto& A = m_.lines[static_cast<std::size_t>(c.a)];
        const auto& B = m_.lines[static_cast<std::size_t>(c.b)];
        const QStringList cells{QString::number(A.id) + " · " + t((std::string("util.kind.") + civil::utilKey(A.kind)).c_str()),
                                QString::number(B.id) + " · " + t((std::string("util.kind.") + civil::utilKey(B.kind)).c_str()),
                                f2(c.x), f2(c.y), f2(c.clear), f2(c.required)};
        for (int k = 0; k < cells.size(); ++k) {
            auto* it = new QTableWidgetItem(cells[k]);
            if (c.clear + 1e-9 < c.required) it->setForeground(QColor(232, 70, 80));
            cross_->setItem(r, k, it);
        }
    }
    checks_->clear();
    for (const auto& is : m_.issues) {
        auto* it = new QListWidgetItem(QString::fromStdString(loc_.tr(is.error ? "sewer.lvl.error" : "sewer.lvl.warning") + ": " + loc_.tr(is.key, is.args)));
        it->setForeground(is.error ? QColor(232, 70, 80) : QColor(230, 160, 30));
        if (is.x != 0 || is.y != 0) {
            it->setData(Qt::UserRole, QPointF(is.x, is.y));
            it->setToolTip(t("gui.gen.locate"));
        }
        checks_->addItem(it);
    }
    if (m_.issues.empty()) checks_->addItem(t("gui.bim.ok"));
    summary_->setText(t("gui.ut.summary", {QString::number(m_.lines.size()), QString::number(total, 'f', 1), QString::number(m_.crossings.size()),
                                           QString::number(m_.errors())}));
    if (keep >= 0 && keep < lines_->rowCount()) lines_->selectRow(keep);
    selected_ = keep;
    updating_ = false;
    fillPoints();
}

void UtilityDialog::fillPoints() {
    const bool was = updating_;
    updating_ = true;
    BulkFill bulk(points_);
    points_->setRowCount(0);
    if (selected_ >= 0 && selected_ < static_cast<int>(m_.lines.size())) {
        const auto& ln = m_.lines[static_cast<std::size_t>(selected_)];
        static const char* srcKey[] = {"util.src.z", "util.src.label", "util.src.depth", "util.src.interp", "util.src.default", "util.src.manual"};
        for (std::size_t j = 0; j < ln.v.size(); ++j) {
            const auto& v = ln.v[j];
            const int r = points_->rowCount();
            points_->insertRow(r);
            const QStringList cells{QString::number(j + 1), f3(v.x), f3(v.y), f3(v.z), f3(v.ground), f2(v.ground - v.z - ln.dn / 2000.0),
                                    t(srcKey[static_cast<int>(v.src)])};
            for (int k = 0; k < cells.size(); ++k) {
                auto* it = new QTableWidgetItem(cells[k]);
                if (k != 3) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
                else it->setBackground(QColor(255, 242, 204, 60));
                points_->setItem(r, k, it);
            }
        }
    }
    updating_ = was;
}

void UtilityDialog::render() {
    const auto kinds = civil::utilKinds();
    auto visible = [&](UtilKind k) {
        for (std::size_t i = 0; i < kinds.size() && i < 9; ++i)
            if (kinds[i] == k) return show_[i]->isChecked();
        return true;
    };
    // vertikal masshtab: yer sathining o'rtachasi atrofida
    double gsum = 0;
    int gn = 0;
    for (const auto& ln : m_.lines)
        for (const auto& v : ln.v) { gsum += v.ground; ++gn; }
    const double g0 = gn ? gsum / gn : p_.ground, k = vexag_->value();
    auto Z = [&](double z) { return g0 + (z - g0) * k; };
    view_->beginScene(t("gui.ut.title"), summary_->text() + QStringLiteral("  ·  ×%1").arg(k, 0, 'f', 0));
    for (std::size_t i = 0; i < m_.lines.size(); ++i) {
        const auto& ln = m_.lines[i];
        if (!visible(ln.kind)) continue;
        std::vector<P3> axis, surf;
        for (const auto& v : ln.v) {
            axis.push_back({v.x, v.y, Z(v.z)});
            surf.push_back({v.x, v.y, Z(v.ground)});
        }
        const QColor c = utilColor(ln.kind);
        QColor faint = c;
        faint.setAlpha(70);
        view_->addPolyline(surf, faint, 1.0);  // plandagi izi (yer sathida)
        for (std::size_t j = 0; j < ln.v.size(); ++j)
            view_->addPolyline({surf[j], axis[j]}, faint, 0.8);
        const bool sel = static_cast<int>(i) == selected_;
        const QString kn = t((std::string("util.kind.") + civil::utilKey(ln.kind)).c_str());
        view_->beginObject(kn + QStringLiteral(" #%1").arg(ln.id), kn, {QString::fromStdString(ln.layer), QStringLiteral("Ø%1").arg(ln.dn)},
                           QStringLiteral("line:%1").arg(i));
        view_->addPolyline(axis, sel ? QColor(255, 255, 255) : c, std::clamp(ln.dn / 40.0, 2.0, 9.0) + (sel ? 2 : 0));
        view_->endObject();
    }
    for (std::size_t ni = 0; ni < m_.nodes.size(); ++ni) {
        const auto& n = m_.nodes[ni];
        if (!visible(n.kind)) continue;
        std::vector<P3> ring;
        for (int s = 0; s < 12; ++s) ring.push_back({n.x + n.r * std::cos(s * 3.14159265 / 6), n.y + n.r * std::sin(s * 3.14159265 / 6), 0});
        QColor c = utilColor(n.kind);
        const QString kn = t((std::string("util.kind.") + civil::utilKey(n.kind)).c_str());
        view_->beginObject(n.name.empty() ? kn : QString::fromStdString(n.name), kn, {QString::fromStdString(n.layer)}, QStringLiteral("node:%1").arg(ni));
        view_->addPrism(ring, {}, Z(n.bottom), Z(n.ground), c.lighter(130), 110);
        view_->endObject();
    }
    for (const auto& c : m_.crossings) {
        if (c.clear + 1e-9 >= c.required) continue;
        view_->addPolyline({{c.x, c.y, Z(std::min(c.za, c.zb)) - 0.5}, {c.x, c.y, Z(std::max(c.za, c.zb)) + 1.5}}, QColor(255, 60, 60), 3);
        view_->addLabel({c.x, c.y, Z(std::max(c.za, c.zb)) + 1.8}, QStringLiteral("⚠ %1 < %2").arg(f2(c.clear), f2(c.required)));
    }
    view_->endScene(true);
    if (hist_) hist_->note();   // menyu tarixi: o'zgarish bo'lsa — yangi qadam (kechiktirib)
}

// ---------------------------------------------------------------- Xossalar
namespace {
const QColor kErr(232, 70, 80), kWarn(230, 160, 30);
const char* kSrcKey[] = {"util.src.z", "util.src.label", "util.src.depth", "util.src.interp", "util.src.default", "util.src.manual"};
}  // namespace

void UtilityDialog::onPicked(const QString& key) {
    const int i = key.section(':', 1).toInt();
    if (key.startsWith("line:") && i >= 0 && i < static_cast<int>(m_.lines.size())) {
        if (selected_ != i) {   // jadvalda ham tanlanadi; 3D qayta chiziladi (oq rang) — u yana shu yerga qaytadi
            updating_ = true;
            if (i < lines_->rowCount()) lines_->selectRow(i);
            updating_ = false;
            selected_ = i;
            fillPoints();
            render();
            return;
        }
        fillLineProps(i);
    } else if (key.startsWith("node:") && i >= 0 && i < static_cast<int>(m_.nodes.size())) {
        fillNodeProps(i);
    } else {
        props_->empty(t("gui.xp.hint"));
        return;
    }
    if (tabs_ && tabs_->currentWidget() != props_) tabs_->setCurrentWidget(props_);
}

void UtilityDialog::fillLineProps(int i) {
    const auto& ln = m_.lines[static_cast<std::size_t>(i)];
    const auto kinds = civil::utilKinds();
    auto kname = [this](UtilKind k) { return t((std::string("util.kind.") + civil::utilKey(k)).c_str()); };
    props_->begin(t("gui.xp.uline", {kname(ln.kind), QString::number(ln.id)}), QString::fromStdString(ln.layer));
    props_->section(t("gui.xp.network"));
    QStringList kn;
    int cur = 0;
    for (std::size_t k = 0; k < kinds.size(); ++k) {
        kn << kname(kinds[k]);
        if (kinds[k] == ln.kind) cur = static_cast<int>(k);
    }
    props_->choice("kind", t("gui.xp.kind"), kn, cur, [this, i, kinds](int k) {
        if (i >= static_cast<int>(m_.lines.size()) || k < 0 || k >= static_cast<int>(kinds.size())) return;
        m_.lines[static_cast<std::size_t>(i)].kind = kinds[static_cast<std::size_t>(k)];
        modelChanged();
    });
    props_->number("dn", QString::fromUtf8("Ø"), ln.dn, 0, [this, i](double v) {
        if (i >= static_cast<int>(m_.lines.size()) || v <= 0 || v > 4000) return;
        auto& l = m_.lines[static_cast<std::size_t>(i)];
        l.dn = v;
        l.dnManual = true;
        modelChanged();
    }, "mm");
    double cover = 1e9;
    for (const auto& v : ln.v) cover = std::min(cover, v.ground - v.z - ln.dn / 2000.0);
    if (ln.v.empty()) cover = 0;
    props_->number("cover", t("gui.xp.ucover"), cover, 2, [this, i](double c) {   // hamma uchlarda bir xil chuqurlik (quvur ustigacha)
        if (i >= static_cast<int>(m_.lines.size()) || c < 0 || c > 50) return;
        auto& l = m_.lines[static_cast<std::size_t>(i)];
        for (auto& v : l.v) {
            v.z = v.ground - c - l.dn / 2000.0;
            v.src = civil::UtilVertex::Manual;
        }
        modelChanged();
    }, "m");
    if (!ln.v.empty()) {
        props_->section(t("gui.xp.uz"));
        const std::size_t n = std::min<std::size_t>(ln.v.size(), 30);
        for (std::size_t j = 0; j < n; ++j) {
            const auto& v = ln.v[j];
            props_->number(QStringLiteral("z%1").arg(j + 1), QStringLiteral("%1 · %2").arg(j + 1).arg(t(kSrcKey[static_cast<int>(v.src)])), v.z, 3,
                           [this, i, j](double z) { setZ(i, static_cast<int>(j), z); }, "m");
        }
        if (ln.v.size() > n) props_->note(t("gui.xp.more", {QString::number(ln.v.size() - n)}));
    }
    props_->section(t("gui.xp.calc"));
    double l2 = 0;
    for (std::size_t j = 0; j + 1 < ln.v.size(); ++j) l2 += std::hypot(ln.v[j + 1].x - ln.v[j].x, ln.v[j + 1].y - ln.v[j].y);
    props_->info(t("gui.xp.length"), f2(l2) + " / " + f2(ln.length()) + " m");
    if (!ln.v.empty()) {
        props_->info(t("gui.xp.uground"), f3(ln.v.front().ground) + QString::fromUtf8(" → ") + f3(ln.v.back().ground));
        props_->info(t("gui.xp.uaxis"), f3(ln.v.front().z) + QString::fromUtf8(" → ") + f3(ln.v.back().z));
        const auto rq = p_.minCover.find(ln.kind);
        const double req = rq == p_.minCover.end() ? 0 : rq->second;
        props_->info(t("gui.xp.ucovermin"), f2(cover) + (req > 0 ? QString::fromUtf8(" (≥ %1)").arg(f2(req)) : QString()),
                     req > 0 && cover + 1e-9 < req ? kErr : QColor());
        if (l2 > 1e-6) props_->info(t("gui.xp.slope"), QString::number((ln.v.front().z - ln.v.back().z) / l2 * 1000, 'f', 2) + QString::fromUtf8(" ‰"));
    }
    if (!ln.material.empty()) props_->info(t("gui.xp.material"), QString::fromStdString(ln.material));
    for (const auto& c : m_.crossings) {
        if (c.a != i && c.b != i) continue;
        const auto& o = m_.lines[static_cast<std::size_t>(c.a == i ? c.b : c.a)];
        props_->info(t("gui.xp.ucross", {QString::number(o.id), kname(o.kind)}), f2(c.clear) + " / " + f2(c.required) + " m",
                     c.clear + 1e-9 < c.required ? kErr : QColor());
    }
    for (const auto& is : m_.issues)
        if (is.line == i) props_->info(QString::fromUtf8(is.error ? "⚠" : "!"), QString::fromStdString(loc_.tr(is.key, is.args)), is.error ? kErr : kWarn);
    // qo'shimcha detallar
    props_->section(t("gui.xp.details"));
    props_->action("node", t("gui.xp.addnode"), {{t("gui.xp.vertex"), 1, 0}, {"D", 1.0, 2}}, [this, i](const QList<double>& in) {
        if (i >= static_cast<int>(m_.lines.size())) return;
        const auto& l = m_.lines[static_cast<std::size_t>(i)];
        if (l.v.empty()) return;
        const int j = std::clamp(static_cast<int>(std::lround(in.value(0))) - 1, 0, static_cast<int>(l.v.size()) - 1);
        const auto& v = l.v[static_cast<std::size_t>(j)];
        civil::UtilNode n;
        n.kind = l.kind;
        n.layer = l.layer;
        n.x = v.x;
        n.y = v.y;
        n.r = std::clamp(in.value(1, 1.0), 0.3, 6.0) / 2;
        n.ground = v.ground;
        n.bottom = v.z - l.dn / 2000.0;
        n.estimated = true;
        n.name = "Q" + std::to_string(m_.nodes.size() + 1);
        m_.nodes.push_back(n);
        modelChanged();
        view_->selectKey(QStringLiteral("node:%1").arg(m_.nodes.size() - 1));
    });
    if (ln.v.size() > 2)
        props_->action("split", t("gui.xp.split"), {{t("gui.xp.vertex"), static_cast<double>(ln.v.size() / 2 + 1), 0}}, [this, i](const QList<double>& in) {
            if (i >= static_cast<int>(m_.lines.size())) return;
            auto& l = m_.lines[static_cast<std::size_t>(i)];
            const int j = static_cast<int>(std::lround(in.value(0))) - 1;
            if (j <= 0 || j >= static_cast<int>(l.v.size()) - 1) return;
            civil::UtilLine b = l;
            b.v.erase(b.v.begin(), b.v.begin() + j);
            l.v.resize(static_cast<std::size_t>(j) + 1);
            int mx = 0;
            for (const auto& x : m_.lines) mx = std::max(mx, x.id);
            b.id = mx + 1;
            b.from = -1;
            l.to = -1;
            b.lengthLabel = l.lengthLabel = 0;
            m_.lines.insert(m_.lines.begin() + i + 1, b);
            modelChanged();
        });
    props_->action("parallel", t("gui.xp.parallel"), {{t("gui.xp.offset"), 1.5, 2}}, [this, i](const QList<double>& in) {
        if (i >= static_cast<int>(m_.lines.size())) return;
        const double off = in.value(0, 1.5);
        civil::UtilLine b = m_.lines[static_cast<std::size_t>(i)];
        if (b.v.size() < 2 || std::abs(off) < 0.01) return;
        auto nrm = [&](std::size_t a, std::size_t c, double& nx, double& ny) {
            const double dx = b.v[c].x - b.v[a].x, dy = b.v[c].y - b.v[a].y, L = std::hypot(dx, dy);
            if (L < 1e-9) return false;
            nx = -dy / L;
            ny = dx / L;
            return true;
        };
        auto nv = b.v;
        for (std::size_t j = 0; j < b.v.size(); ++j) {   // burchakda — bissektrisa bo'ylab (masofa saqlanadi)
            double ax = 0, ay = 0, cx = 0, cy = 0;
            const bool A = j > 0 && nrm(j - 1, j, ax, ay), Cc = j + 1 < b.v.size() && nrm(j, j + 1, cx, cy);
            double nx = (A ? ax : 0) + (Cc ? cx : 0), ny = (A ? ay : 0) + (Cc ? cy : 0);
            const double L = std::hypot(nx, ny);
            if (L < 1e-9) continue;
            nx /= L;
            ny /= L;
            const double cs = A ? nx * ax + ny * ay : 1;
            const double k = cs > 0.25 ? 1 / cs : 4;
            nv[j].x += nx * off * k;
            nv[j].y += ny * off * k;
            nv[j].src = civil::UtilVertex::Manual;
        }
        b.v = nv;
        int mx = 0;
        for (const auto& x : m_.lines) mx = std::max(mx, x.id);
        b.id = mx + 1;
        b.from = b.to = -1;
        b.dnManual = true;
        m_.lines.push_back(b);
        modelChanged();
        view_->selectKey(QStringLiteral("line:%1").arg(m_.lines.size() - 1));
    });
    props_->action("locate", t("gui.xp.locate"), [this, i] {
        if (i >= static_cast<int>(m_.lines.size()) || m_.lines[static_cast<std::size_t>(i)].v.empty()) return;
        const auto& l = m_.lines[static_cast<std::size_t>(i)];
        const auto& v = l.v[l.v.size() / 2];
        emit locateRequested(v.x, v.y);
    });
    props_->action("todwg", t("gui.xp.todwg"), [this] { if (drawB_) drawB_->click(); });
    props_->action("erase", t("gui.xp.erase"), [this, i] {
        if (i >= static_cast<int>(m_.lines.size())) return;
        view_->clearSelection();   // indekslar suriladi
        m_.lines.erase(m_.lines.begin() + i);
        selected_ = -1;
        modelChanged();
    }, true);
    if (i > 0) props_->action("prev", QString::fromUtf8("◀ ") + t("gui.xp.prev"), [this, i] { view_->selectKey(QStringLiteral("line:%1").arg(i - 1), true); });
    if (i + 1 < static_cast<int>(m_.lines.size()))
        props_->action("next", t("gui.xp.next") + QString::fromUtf8(" ▶"), [this, i] { view_->selectKey(QStringLiteral("line:%1").arg(i + 1), true); });
    props_->note(t("gui.xp.unote"));
    props_->end();
}

void UtilityDialog::fillNodeProps(int k) {
    const auto& n = m_.nodes[static_cast<std::size_t>(k)];
    const QString kn = t((std::string("util.kind.") + civil::utilKey(n.kind)).c_str());
    props_->begin(t("gui.xp.unode", {n.name.empty() ? QString::number(k + 1) : QString::fromStdString(n.name)}),
                  kn + (n.layer.empty() ? QString() : " · " + QString::fromStdString(n.layer)));
    props_->section(t("gui.xp.network"));
    props_->text("name", t("gui.xp.name"), QString::fromStdString(n.name), [this, k](const QString& s) {
        if (k >= static_cast<int>(m_.nodes.size())) return;
        m_.nodes[static_cast<std::size_t>(k)].name = s.trimmed().toStdString();
        modelChanged();
    });
    props_->number("ground", t("gui.xp.rim"), n.ground, 3, [this, k](double v) {
        if (k >= static_cast<int>(m_.nodes.size())) return;
        m_.nodes[static_cast<std::size_t>(k)].ground = v;
        modelChanged();
    }, "m");
    props_->number("bottom", t("gui.xp.ubottom"), n.bottom, 3, [this, k](double v) {
        if (k >= static_cast<int>(m_.nodes.size())) return;
        m_.nodes[static_cast<std::size_t>(k)].bottom = v;
        m_.nodes[static_cast<std::size_t>(k)].estimated = false;
        modelChanged();
    }, "m");
    props_->number("d", t("gui.xp.diam"), 2 * n.r, 2, [this, k](double v) {
        if (k >= static_cast<int>(m_.nodes.size()) || v < 0.2 || v > 12) return;
        m_.nodes[static_cast<std::size_t>(k)].r = v / 2;
        modelChanged();
    }, "m");
    props_->section(t("gui.xp.calc"));
    props_->info(t("gui.xp.depth"), f2(n.ground - n.bottom) + " m", n.ground - n.bottom < 0 ? kErr : QColor());
    if (!n.inverts.empty()) {
        QString s;
        for (double z : n.inverts) s += (s.isEmpty() ? QString() : QStringLiteral("; ")) + f3(z);
        props_->info(t("gui.xp.inverts"), s);
    }
    if (n.estimated) props_->info("!", t("gui.xp.estimated"), kWarn);
    for (const auto& l : m_.lines)
        if (l.from == k || l.to == k)
            props_->info(t("gui.xp.uline", {t((std::string("util.kind.") + civil::utilKey(l.kind)).c_str()), QString::number(l.id)}),
                         l.from == k ? t("gui.xp.start") : t("gui.xp.end"));
    props_->section(t("gui.xp.details"));
    props_->action("locate", t("gui.xp.locate"), [this, k] {
        if (k < static_cast<int>(m_.nodes.size())) emit locateRequested(m_.nodes[static_cast<std::size_t>(k)].x, m_.nodes[static_cast<std::size_t>(k)].y);
    });
    props_->action("todwg", t("gui.xp.todwg"), [this] { if (drawB_) drawB_->click(); });
    props_->action("erase", t("gui.xp.erase"), [this, k] {
        if (k >= static_cast<int>(m_.nodes.size())) return;
        view_->clearSelection();
        m_.nodes.erase(m_.nodes.begin() + k);
        for (auto& l : m_.lines) {   // chiziq uchlaridagi quduq indekslari
            for (int* e : {&l.from, &l.to}) {
                if (*e == k) *e = -1;
                else if (*e > k) --*e;
            }
        }
        modelChanged();
    }, true);
    if (k > 0) props_->action("prev", QString::fromUtf8("◀ ") + t("gui.xp.prev"), [this, k] { view_->selectKey(QStringLiteral("node:%1").arg(k - 1), true); });
    if (k + 1 < static_cast<int>(m_.nodes.size()))
        props_->action("next", t("gui.xp.next") + QString::fromUtf8(" ▶"), [this, k] { view_->selectKey(QStringLiteral("node:%1").arg(k + 1), true); });
    props_->note(t("gui.xp.unote"));
    props_->end();
}

} // namespace asiacad::gui
