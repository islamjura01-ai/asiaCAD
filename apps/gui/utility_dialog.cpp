#include "utility_dialog.h"

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
    view_ = new Pipe3DView(mid);
    mv->addWidget(view_, 1);

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
    tabs->addTab(lines_, t("gui.ut.lines"));
    tabs->addTab(points_, t("gui.ut.points"));
    tabs->addTab(cross_, t("gui.ut.cross"));
    tabs->addTab(checks_, t("gui.bim.checks"));
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 0);
    split->setStretchFactor(2, 3);
    split->setStretchFactor(3, 2);
    split->setSizes({230, 300, 560, 560});
    scroll->setMinimumWidth(280);

    // ---- past
    auto* bottom = new QHBoxLayout;
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
        view_->addPolyline(axis, sel ? QColor(255, 255, 255) : c, std::clamp(ln.dn / 40.0, 2.0, 9.0) + (sel ? 2 : 0));
    }
    for (const auto& n : m_.nodes) {
        if (!visible(n.kind)) continue;
        std::vector<P3> ring;
        for (int s = 0; s < 12; ++s) ring.push_back({n.x + n.r * std::cos(s * 3.14159265 / 6), n.y + n.r * std::sin(s * 3.14159265 / 6), 0});
        QColor c = utilColor(n.kind);
        view_->addPrism(ring, {}, Z(n.bottom), Z(n.ground), c.lighter(130), 110);
    }
    for (const auto& c : m_.crossings) {
        if (c.clear + 1e-9 >= c.required) continue;
        view_->addPolyline({{c.x, c.y, Z(std::min(c.za, c.zb)) - 0.5}, {c.x, c.y, Z(std::max(c.za, c.zb)) + 1.5}}, QColor(255, 60, 60), 3);
        view_->addLabel({c.x, c.y, Z(std::max(c.za, c.zb)) + 1.8}, QStringLiteral("⚠ %1 < %2").arg(f2(c.clear), f2(c.required)));
    }
    view_->endScene(true);
}

} // namespace asiacad::gui
