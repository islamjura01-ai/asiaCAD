#include "survey_dialog.h"

#include "icons.h"

#include <asiacad/core/error.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <format>

namespace asiacad::gui {

namespace {
constexpr civil::DevKind kKinds[] = {civil::DevKind::Plan, civil::DevKind::Vertical, civil::DevKind::Height, civil::DevKind::Axis, civil::DevKind::Point};
const char* kIcons[] = {"devplan", "devvert", "devheight", "devaxis", "devpoint"};
const QString kSel = QStringLiteral("\x01sel");   // combodagi "tanlov" bandi ma'lumoti
const QString kNone = QStringLiteral("\x01none");
}  // namespace

QString SurveyDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

SurveyDialog::SurveyDialog(const i18n::Localizer& loc, const Document& doc, std::function<std::vector<EntityId>()> selection, QWidget* parent)
    : QDialog(parent), loc_(loc), doc_(doc), selection_(std::move(selection)) {
    setWindowTitle(t("gui.dev.title"));
    resize(1080, 680);
    auto* root = new QHBoxLayout(this);
    auto* left = new QVBoxLayout;
    root->addLayout(left, 0);
    // ---- tahlil turi (katta tugmalar)
    auto* kb = new QGroupBox(t("gui.dev.kind"), this);
    auto* kg = new QGridLayout(kb);
    kinds_ = new QButtonGroup(this);
    for (int i = 0; i < 5; ++i) {
        auto* b = new QToolButton(kb);
        b->setCheckable(true);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIcon(makeIcon(kIcons[i], Qt::white, QColor("#0E9F8E")));
        b->setIconSize(QSize(34, 34));
        b->setText(QString::fromStdString(loc_.tr(std::string("dev.kind.") + civil::devKindKey(kKinds[i]))));
        b->setToolTip(t((std::string("gui.dev.tip.") + civil::devKindKey(kKinds[i])).c_str()));
        b->setMinimumSize(118, 74);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        b->setObjectName(QString("kind_") + civil::devKindKey(kKinds[i]));
        kinds_->addButton(b, i);
        kg->addWidget(b, i / 3, i % 3);
    }
    kinds_->button(0)->setChecked(true);
    connect(kinds_, &QButtonGroup::idClicked, this, [this](int i) {
        const auto k = kKinds[i];   // tur bo'yicha odatiy parametrlar
        const QSignalBlocker b1(clus_), b2(maxD_);
        clus_->setValue(k == civil::DevKind::Vertical ? 0.5 : 1.5);
        maxD_->setValue(k == civil::DevKind::Point ? 0.5 : k == civil::DevKind::Height ? 2.0 : 1.0);
        updateEnabled();
        ran_ = false;
    });
    left->addWidget(kb);
    // ---- nuqtalar va loyiha
    auto* src = new QGroupBox(t("gui.dev.data"), this);
    auto* sf = new QFormLayout(src);
    ptsSrc_ = new QComboBox(src);
    ptsSrc_->setObjectName("ptsSrc");
    refSrc_ = new QComboBox(src);
    refSrc_->setObjectName("refSrc");
    auto row = [&](QComboBox* c, const char* btnKey, const QString& name, std::function<void()> f) {
        auto* h = new QHBoxLayout;
        h->addWidget(c, 1);
        auto* b = new QPushButton(t(btnKey), src);
        b->setObjectName(name);
        b->setToolTip(t("gui.dev.takesel.tip"));
        connect(b, &QPushButton::clicked, this, std::move(f));
        h->addWidget(b);
        return h;
    };
    sf->addRow(t("gui.dev.points"), row(ptsSrc_, "gui.dev.takesel", "takePts", [this] { takePointsSelection(); }));
    sf->addRow(t("gui.dev.design"), row(refSrc_, "gui.dev.takesel", "takeRef", [this] { takeRefSelection(); }));
    ptsInfo_ = new QLabel(src);
    ptsInfo_->setObjectName("ptsInfo");
    sf->addRow(QString(), ptsInfo_);
    auto* io = new QHBoxLayout;
    auto* imp = new QPushButton(makeIcon("ptimport", QColor("#0E9F8E")), t("gui.dev.import"), src);
    auto* exp = new QPushButton(makeIcon("ptexport", QColor("#0E9F8E")), t("gui.dev.exportpts"), src);
    io->addWidget(imp);
    io->addWidget(exp);
    sf->addRow(io);
    geo_ = new QCheckBox(t("gui.dev.geo"), src);
    geo_->setChecked(true);
    sf->addRow(geo_);
    connect(imp, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, t("gui.dev.import"), QString(), "TXT / CSV / XLSX (*.txt *.csv *.xlsx *.xyz *.dat);;* (*)");
        if (!f.isEmpty()) emit commandReady(importCommand(f));
    });
    connect(exp, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, t("gui.dev.exportpts"), "points.txt", "TXT (*.txt);;CSV (*.csv);;Excel (*.xlsx)");
        if (f.isEmpty()) return;
        emit commandReady(QStringLiteral("PTEXPORT %1 \"%2\"%3").arg(selToken(ptsSrc_, ptsSel_), f, geo_->isChecked() ? "" : " XY"));
    });
    connect(ptsSrc_, &QComboBox::currentIndexChanged, this, [this] { ran_ = false; updateEnabled(); });
    connect(refSrc_, &QComboBox::currentIndexChanged, this, [this] { ran_ = false; });
    left->addWidget(src);
    // ---- parametrlar
    auto* pb = new QGroupBox(t("gui.dev.params"), this);
    auto* pf = new QFormLayout(pb);
    auto spin = [&](const char* key, double v, double lo, double hi, int dec, double step, const QString& name) {
        auto* s = new QDoubleSpinBox(pb);
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setSingleStep(step);
        s->setValue(v);
        s->setObjectName(name);
        pf->addRow(t(key), s);
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { ran_ = false; });
        return s;
    };
    tol_ = spin("gui.dev.tol", 10, 0.1, 1000, 1, 1, "tol");
    ratio_ = spin("gui.dev.ratio", 0, 0, 100000, 0, 100, "ratio");
    maxD_ = spin("gui.dev.maxd", 1.0, 0.01, 100, 2, 0.1, "maxd");
    clus_ = spin("gui.dev.cluster", 1.5, 0.05, 20, 2, 0.1, "cluster");
    auto* zrow = new QHBoxLayout;
    useZ_ = new QCheckBox(pb);
    zDes_ = new QDoubleSpinBox(pb);
    zDes_->setRange(-1000, 9000);
    zDes_->setDecimals(3);
    zDes_->setObjectName("zdes");
    zrow->addWidget(useZ_);
    zrow->addWidget(zDes_, 1);
    pf->addRow(t("gui.dev.zdes"), zrow);
    connect(useZ_, &QCheckBox::toggled, this, [this] { ran_ = false; updateEnabled(); });
    connect(zDes_, &QDoubleSpinBox::valueChanged, this, [this] { ran_ = false; });
    textH_ = spin("gui.dev.texth", 0.25, 0.01, 10, 2, 0.05, "texth");
    arrow_ = spin("gui.dev.arrow", 0.8, 0.05, 20, 2, 0.1, "arrow");
    left->addWidget(pb);
    left->addStretch();
    // ---- o'ng: natija
    auto* right = new QVBoxLayout;
    root->addLayout(right, 1);
    auto* top = new QHBoxLayout;
    auto* runB = new QPushButton(t("gui.dev.run"), this);
    runB->setObjectName("run");
    runB->setDefault(true);
    auto* drawB = new QPushButton(t("gui.dev.draw"), this);
    drawB->setObjectName("draw");
    auto* xlB = new QPushButton(makeIcon("excel", QColor("#1D6F42")), t("gui.dev.excel"), this);
    xlB->setObjectName("excel");
    auto* csvB = new QPushButton(t("gui.dev.csv"), this);
    auto* clrB = new QPushButton(t("gui.dev.clear"), this);
    for (auto* b : {runB, drawB, xlB, csvB, clrB}) top->addWidget(b);
    top->addStretch();
    right->addLayout(top);
    connect(runB, &QPushButton::clicked, this, [this] { run(); });
    connect(drawB, &QPushButton::clicked, this, [this] {
        if (run() && !res_.rows.empty()) emit commandReady(drawCommand());
    });
    connect(xlB, &QPushButton::clicked, this, [this] {
        if (!run()) return;
        QString f = QFileDialog::getSaveFileName(this, t("gui.dev.excel"), "ogish.xlsx", "Excel (*.xlsx)");
        if (f.isEmpty()) return;
        if (!f.endsWith(".xlsx", Qt::CaseInsensitive)) f += ".xlsx";
        exportExcel(f);
    });
    connect(csvB, &QPushButton::clicked, this, [this] {
        if (!run()) return;
        const QString f = QFileDialog::getSaveFileName(this, t("gui.dev.csv"), "ogish.csv", "CSV (*.csv)");
        if (!f.isEmpty()) exportCsv(f);
    });
    connect(clrB, &QPushButton::clicked, this, [this] { emit commandReady("DEVCLEAR"); });
    sum_ = new QLabel(this);
    sum_->setObjectName("summary");
    sum_->setTextFormat(Qt::RichText);
    sum_->setWordWrap(true);
    right->addWidget(sum_);
    table_ = new QTableWidget(this);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    right->addWidget(table_, 1);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        if (r >= 0 && r < static_cast<int>(res_.rows.size())) emit locateRequested(res_.rows[static_cast<std::size_t>(r)].p.x, res_.rows[static_cast<std::size_t>(r)].p.y);
    });
    auto* hint = new QLabel(t("gui.dev.hint"), this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #8a93a3;");
    right->addWidget(hint);
    refreshLayers();
    updateEnabled();
}

void SurveyDialog::refreshLayers() {
    for (auto* c : {ptsSrc_, refSrc_}) {
        const QString cur = c->currentData().toString();
        const QSignalBlocker b(c);
        c->clear();
        if (c == refSrc_) c->addItem(t("gui.dev.none"), kNone);
        const auto& sel = c == ptsSrc_ ? ptsSel_ : refSel_;
        c->addItem(t("gui.dev.selection", {QString::number(sel.size())}), kSel);
        for (const auto& [name, L] : doc_.layers()) {
            const auto n = doc_.idsOnLayer(name).size();
            if (n == 0) continue;
            c->addItem(QStringLiteral("%1 (%2)").arg(QString::fromStdString(name)).arg(n), QString::fromStdString(name));
        }
        int i = c->findData(cur);
        if (i < 0 && c == ptsSrc_) i = c->findData("SYOMKA");
        if (i < 0 && c == refSrc_) i = 0;
        if (i < 0) i = 0;
        c->setCurrentIndex(i);
    }
    updateEnabled();
}

void SurveyDialog::setPointsLayer(const QString& l) {
    refreshLayers();
    const int i = ptsSrc_->findData(l);
    if (i >= 0) ptsSrc_->setCurrentIndex(i);
}
void SurveyDialog::setRefLayer(const QString& l) {
    refreshLayers();
    const int i = l.isEmpty() ? 0 : refSrc_->findData(l);
    if (i >= 0) refSrc_->setCurrentIndex(i);
}
void SurveyDialog::takePointsSelection() {
    ptsSel_ = selection_ ? selection_() : std::vector<EntityId>{};
    refreshLayers();
    ptsSrc_->setCurrentIndex(ptsSrc_->findData(kSel));
}
void SurveyDialog::takeRefSelection() {
    refSel_ = selection_ ? selection_() : std::vector<EntityId>{};
    refreshLayers();
    refSrc_->setCurrentIndex(refSrc_->findData(kSel));
}

void SurveyDialog::setKind(civil::DevKind k) {
    for (int i = 0; i < 5; ++i)
        if (kKinds[i] == k) {
            kinds_->button(i)->setChecked(true);
            emit kinds_->idClicked(i);
        }
}
civil::DevKind SurveyDialog::kind() const { return kKinds[std::max(0, kinds_->checkedId())]; }

civil::DevParams SurveyDialog::params() const {
    civil::DevParams p;
    p.kind = kind();
    p.tol = tol_->value() / 1000;
    p.tolRatio = ratio_->value();
    p.maxDist = maxD_->value();
    p.cluster = clus_->value();
    p.useDesignZ = useZ_->isChecked();
    p.designZ = zDes_->value();
    p.textH = textH_->value();
    p.arrowLen = arrow_->value();
    return p;
}

void SurveyDialog::updateEnabled() {
    const auto k = kind();
    ratio_->setEnabled(k == civil::DevKind::Vertical);
    useZ_->setEnabled(k == civil::DevKind::Height || k == civil::DevKind::Point);
    zDes_->setEnabled(useZ_->isEnabled() && useZ_->isChecked());
    clus_->setEnabled(k == civil::DevKind::Plan || k == civil::DevKind::Vertical);
    maxD_->setEnabled(k != civil::DevKind::Vertical);
    const auto ids = resolve(ptsSrc_, ptsSel_);
    ptsInfo_->setText(t("gui.dev.ptscount", {QString::number(civil::collectPoints(doc_, ids).size())}));
}

std::vector<EntityId> SurveyDialog::resolve(const QComboBox* c, const std::vector<EntityId>& sel) const {
    const QString d = c->currentData().toString();
    if (d == kSel) return sel;
    if (d == kNone || d.isEmpty()) return {};
    return doc_.idsOnLayer(d.toStdString());
}

QString SurveyDialog::selToken(const QComboBox* c, const std::vector<EntityId>& sel) const {
    const QString d = c->currentData().toString();
    if (d == kNone || (d == kSel && sel.empty())) return "-";
    if (d == kSel) {
        QStringList ids;
        for (EntityId id : sel) ids << QString::number(id);
        return ids.join(',');
    }
    // qatlam: bo'sh joy bo'lsa — qo'shtirnoq ichida (tokenizer)
    return "\"" + QString::fromStdString(loc_.keywordName("LAYER")) + ":" + d + "\"";
}

bool SurveyDialog::run() {
    const auto p = params();
    const auto pts = civil::collectPoints(doc_, resolve(ptsSrc_, ptsSel_));
    const auto refs = civil::collectRefs(doc_, resolve(refSrc_, refSel_));
    res_ = {};
    ran_ = false;
    QString err;
    if (pts.empty()) err = t("gui.dev.err.nopts");
    else {
        try {
            if (refs.empty() && p.kind != civil::DevKind::Vertical && !(p.kind == civil::DevKind::Height && p.useDesignZ)) throw Error("dev.err.noref", {});
            res_ = civil::analyze(pts, refs, p);
            ran_ = true;
        } catch (const Error& e) {
            err = QString::fromStdString(loc_.tr(e.key(), e.args()));
        }
    }
    if (!ran_) {
        sum_->setText("<span style='color:#e05a50'>" + err.toHtmlEscaped() + "</span>");
        table_->setRowCount(0);
        return false;
    }
    fillTable();
    const int n = static_cast<int>(res_.rows.size()), bad = n - res_.nOk;
    QString html = QString("<b>%1</b> — ").arg(QString::fromStdString(loc_.tr(std::string("dev.kind.") + civil::devKindKey(p.kind))).toHtmlEscaped());
    html += t("gui.dev.sum", {QString::number(n), QString::number(res_.nOk), QString::number(bad), QString::number(res_.maxAbs * 1000, 'f', 1),
                              QString::number(res_.mean * 1000, 'f', 1), QString::number(res_.rms * 1000, 'f', 1)})
                .toHtmlEscaped();
    html += QString(" &nbsp; <span style='color:%1'>%2 %3%</span>")
                .arg(bad ? "#e05a50" : "#3fae5a", bad ? "✖" : "✔", QString::number(n ? 100.0 * res_.nOk / n : 0, 'f', 1));
    for (const auto& el : res_.elements)
        if (p.kind == civil::DevKind::Axis)
            html += "<br>" + QString::fromStdString(loc_.tr("dev.msg.axis", {el.ref, std::format("{:.2f}", el.measIncl), std::format("{:.2f}", el.designIncl),
                                                                              std::format("{:.2f}", el.measAz), std::format("{:.2f}", el.designAz),
                                                                              std::format("{:.1f}", el.maxDev * 1000)}))
                                .toHtmlEscaped();
    if (!res_.unmatched.empty()) {
        QStringList u;
        for (const auto& s : res_.unmatched) u << QString::fromStdString(s);
        html += "<br><span style='color:#e0a526'>" + t("gui.dev.unmatched", {QString::number(u.size()), u.mid(0, 15).join(", ")}).toHtmlEscaped() + "</span>";
    }
    sum_->setText(html);
    return true;
}

void SurveyDialog::fillTable() {
    // ustunlar — Excel bilan bir xil (sarlavhalar tarjima qilingan)
    const bool geo = geo_->isChecked();
    const auto wb = civil::toWorkbook(res_, params(), [this](const std::string& k) { return loc_.tr(k); }, geo);
    const auto& sh = wb.sheets.front();
    table_->clear();
    table_->setRowCount(sh.rows.empty() ? 0 : static_cast<int>(sh.rows.size()) - 1);
    table_->setColumnCount(sh.rows.empty() ? 0 : static_cast<int>(sh.rows[0].size()));
    QStringList hdr;
    for (std::size_t c = 0; !sh.rows.empty() && c < sh.rows[0].size(); ++c) hdr << QString::fromStdString(sh.str(0, c));
    table_->setHorizontalHeaderLabels(hdr);
    const QString devHdr = QString::fromStdString(loc_.tr("dev.col.dev"));
    for (std::size_t r = 1; r < sh.rows.size(); ++r) {
        const bool ok = res_.rows[r - 1].ok;
        for (std::size_t c = 0; c < sh.rows[r].size(); ++c) {
            const auto& cell = sh.rows[r][c];
            QString v = QString::fromStdString(cell.text);
            if (cell.kind == xlsx::Cell::Number) {
                const bool integer = std::abs(cell.num - std::round(cell.num)) < 1e-9;
                v = QString::number(cell.num, 'f', cell.style == 3 ? 3 : (c == 0 && integer ? 0 : 1));
                const bool signedKind = res_.kind == civil::DevKind::Plan || res_.kind == civil::DevKind::Height;
                if (signedKind && hdr.value(static_cast<int>(c)) == devHdr && cell.num > 0) v.prepend('+');
            }
            auto* it = new QTableWidgetItem(v);
            if (cell.kind == xlsx::Cell::Number) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (!ok) it->setForeground(QColor("#e05a50"));
            table_->setItem(static_cast<int>(r) - 1, static_cast<int>(c), it);
        }
    }
    table_->resizeColumnsToContents();
}

QString SurveyDialog::drawCommand() const {
    const auto p = params();
    QString c = QStringLiteral("DEVIATION %1 %2 %3 TOL=%4 MAX=%5 R=%6 H=%7 A=%8")
                    .arg(civil::devKindKey(p.kind), selToken(ptsSrc_, ptsSel_), selToken(refSrc_, refSel_))
                    .arg(p.tol * 1000)
                    .arg(p.maxDist)
                    .arg(p.cluster)
                    .arg(p.textH)
                    .arg(p.arrowLen);
    if (p.kind == civil::DevKind::Vertical && p.tolRatio > 0) c += QStringLiteral(" K=%1").arg(p.tolRatio);
    if (useZ_->isEnabled() && p.useDesignZ) c += QStringLiteral(" Z=%1").arg(p.designZ, 0, 'f', 3);
    if (!geo_->isChecked()) c += " XY";
    return c;
}

QString SurveyDialog::importCommand(const QString& path) const {
    return QStringLiteral("PTIMPORT \"%1\"%2 H=%3").arg(path, geo_->isChecked() ? "" : " XY").arg(textH_->value());
}

bool SurveyDialog::exportExcel(const QString& path) {
    if (!ran_ && !run()) return false;
    try {
        xlsx::save(civil::toWorkbook(res_, params(), [this](const std::string& k) { return loc_.tr(k); }, geo_->isChecked()), path.toStdString());
        sum_->setText(sum_->text() + "<br>" + t("dev.msg.saved", {path}).toHtmlEscaped());
        return true;
    } catch (const Error& e) {
        sum_->setText(QString::fromStdString(loc_.tr(e.key(), e.args())));
        return false;
    }
}

bool SurveyDialog::exportCsv(const QString& path) {
    if (!ran_ && !run()) return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const std::string s = civil::toCsv(res_, [this](const std::string& k) { return loc_.tr(k); }, geo_->isChecked());
    f.write(s.data(), static_cast<qint64>(s.size()));
    sum_->setText(sum_->text() + "<br>" + t("dev.msg.saved", {path}).toHtmlEscaped());
    return true;
}

} // namespace asiacad::gui
