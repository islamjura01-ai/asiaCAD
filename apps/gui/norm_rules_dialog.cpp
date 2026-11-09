#include "norm_rules_dialog.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

#include <set>

namespace asiacad::gui {

namespace {
enum Col { CEnabled, CId, CTitle, CCats, CConcrete, CWhen, CParam, CMin, CMax, CUnit, CSev, CNorm, CClause, CVerified, CNote, CCount };

QString join(const std::vector<std::string>& v) {
    QStringList l;
    for (const auto& s : v) l << QString::fromStdString(s);
    return l.join(", ");
}
} // namespace

NormRulesDialog::NormRulesDialog(const i18n::Localizer& loc, std::vector<bim::NormRule> rules, QWidget* parent)
    : QDialog(parent), loc_(loc), rules_(std::move(rules)) {
    setObjectName("normRulesDialog");
    setWindowTitle(t("gui.norm.rules.title"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(1180, 620);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);
    auto* title = new QLabel(t("gui.norm.rules.title"), this);
    title->setObjectName("dialogTitle");
    root->addWidget(title);
    help_ = new QLabel(t("gui.norm.rules.help"), this);
    help_->setObjectName("muted");
    help_->setWordWrap(true);
    help_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(help_);
    table_ = new QTableWidget(this);
    table_->setObjectName("normRulesTable");
    table_->setColumnCount(CCount);
    table_->setHorizontalHeaderLabels({QStringLiteral("✓"), QStringLiteral("ID"), t("gui.norm.col.rule"), t("gui.norm.r.cats"), t("gui.norm.r.concrete"),
                                       t("gui.norm.r.when"), t("gui.norm.r.param"), QStringLiteral("min"), QStringLiteral("max"), t("gui.norm.r.unit"),
                                       t("gui.norm.col.sev"), t("gui.norm.col.norm"), t("gui.norm.col.clause"), t("gui.norm.col.verified"),
                                       t("gui.norm.col.note")});
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    root->addWidget(table_, 1);

    auto* bar = new QHBoxLayout;
    auto* add = new QPushButton(t("gui.norm.r.add"), this);
    auto* dup = new QPushButton(t("gui.norm.r.dup"), this);
    auto* del = new QPushButton(t("gui.norm.r.del"), this);
    auto* defs = new QPushButton(t("gui.norm.r.defaults"), this);
    auto* imp = new QPushButton(t("gui.norm.r.import"), this);
    auto* exp = new QPushButton(t("gui.norm.r.export"), this);
    auto* save = new QPushButton(t("gui.norm.r.save"), this);
    save->setObjectName("normRulesSave");
    save->setProperty("accent", true);
    auto* cancel = new QPushButton(t("gui.btn.cancel"), this);
    for (auto* b : {add, dup, del, defs, imp, exp}) bar->addWidget(b);
    bar->addStretch(1);
    bar->addWidget(save);
    bar->addWidget(cancel);
    root->addLayout(bar);

    connect(add, &QPushButton::clicked, this, [this] {
        bim::NormRule r;
        r.id = "user." + std::to_string(table_->rowCount() + 1);
        r.cats = {"WALL"};
        r.param = "thickness";
        r.min = "120mm";
        r.severity = 1;
        r.title[loc_.language()] = t("gui.norm.r.newtitle").toStdString();
        addRow(r);
        table_->scrollToBottom();
        table_->setCurrentCell(table_->rowCount() - 1, CTitle);
    });
    connect(dup, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row < 0) return;
        if (!collect().isEmpty()) return;   // xato bo'lsa — avval tuzatish
        bim::NormRule r = rules_[static_cast<std::size_t>(row)];
        r.id += ".copy";
        addRow(r);
        table_->scrollToBottom();
    });
    connect(del, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row >= 0) table_->removeRow(row);
    });
    connect(defs, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, windowTitle(), t("gui.norm.r.defaults.q")) != QMessageBox::Yes) return;
        rules_ = bim::defaultNormRules();
        fill();
    });
    connect(imp, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, t("gui.norm.r.import"), {}, QStringLiteral("JSON (*.json)"));
        if (f.isEmpty()) return;
        QFile file(f);
        if (!file.open(QIODevice::ReadOnly)) return;
        try {
            rules_ = bim::normRulesFromJson(file.readAll().toStdString());
            fill();
        } catch (const std::exception& ex) {
            QMessageBox::warning(this, windowTitle(), t("norm.err.rules", {QDir::toNativeSeparators(f), QString::fromUtf8(ex.what())}));
        }
    });
    connect(exp, &QPushButton::clicked, this, [this] {
        const QString err = collect();
        if (!err.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), err);
            return;
        }
        QString f = QFileDialog::getSaveFileName(this, t("gui.norm.r.export"), QStringLiteral("shnq_qoidalar.json"), QStringLiteral("JSON (*.json)"));
        if (f.isEmpty()) return;
        if (!f.endsWith(".json", Qt::CaseInsensitive)) f += ".json";
        QFile file(f);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(QByteArray::fromStdString(bim::normRulesToJson(rules_)));
    });
    connect(save, &QPushButton::clicked, this, [this] {
        const QString err = collect();
        if (!err.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), err);
            return;
        }
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    fill();
}

QString NormRulesDialog::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

void NormRulesDialog::fill() {
    table_->setRowCount(0);
    for (const auto& r : rules_) addRow(r);
    table_->resizeColumnsToContents();
    for (int c : {CTitle, CWhen, CNote})
        if (table_->columnWidth(c) > 300) table_->setColumnWidth(c, 300);
}

void NormRulesDialog::addRow(const bim::NormRule& r) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    auto check = [&](int col, bool on) {
        auto* it = new QTableWidgetItem;
        it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        table_->setItem(row, col, it);
    };
    auto text = [&](int col, const std::string& s) { table_->setItem(row, col, new QTableWidgetItem(QString::fromStdString(s))); };
    check(CEnabled, r.enabled);
    text(CId, r.id);
    auto* title = new QTableWidgetItem(QString::fromStdString(r.titleFor(loc_.language())));
    // boshqa tillardagi nomlar saqlanadi (faqat joriy til tahrirlanadi)
    QStringList others;
    for (const auto& [l, s] : r.title) others << QString::fromStdString(l + "=" + s);
    title->setData(Qt::UserRole, others.join(QChar(0x1F)));
    table_->setItem(row, CTitle, title);
    table_->setItem(row, CCats, new QTableWidgetItem(join(r.cats)));
    check(CConcrete, r.concreteOnly);
    text(CWhen, r.when);
    text(CParam, r.param);
    text(CMin, r.min);
    text(CMax, r.max);
    text(CUnit, r.unit);
    auto* sev = new QComboBox(table_);
    sev->addItems({t("gui.norm.sev.error"), t("gui.norm.sev.warning")});
    sev->setCurrentIndex(r.severity >= 2 ? 0 : 1);
    table_->setCellWidget(row, CSev, sev);
    text(CNorm, r.norm);
    text(CClause, r.clause);
    check(CVerified, r.verified);
    text(CNote, r.note);
}

QString NormRulesDialog::collect() {
    std::vector<bim::NormRule> out;
    QStringList errs;
    std::set<std::string> ids;
    const std::string lang = loc_.language();
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto txt = [&](int c) { return table_->item(row, c) ? table_->item(row, c)->text().trimmed().toStdString() : std::string(); };
        auto on = [&](int c) { return table_->item(row, c) && table_->item(row, c)->checkState() == Qt::Checked; };
        bim::NormRule r;
        r.enabled = on(CEnabled);
        r.id = txt(CId);
        if (r.id.empty()) r.id = "rule" + std::to_string(row + 1);
        if (!ids.insert(r.id).second) errs << t("gui.norm.r.dupid", {QString::number(row + 1), QString::fromStdString(r.id)});
        if (auto* it = table_->item(row, CTitle)) {
            for (const auto& kv : it->data(Qt::UserRole).toString().split(QChar(0x1F), Qt::SkipEmptyParts)) {
                const int eq = kv.indexOf('=');
                if (eq > 0) r.title[kv.left(eq).toStdString()] = kv.mid(eq + 1).toStdString();
            }
            r.title[lang] = it->text().trimmed().toStdString();
        }
        for (const auto& c : QString::fromStdString(txt(CCats)).split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts))
            r.cats.push_back(c.toUpper().toStdString());
        r.concreteOnly = on(CConcrete);
        r.when = txt(CWhen);
        r.param = txt(CParam);
        r.min = txt(CMin);
        r.max = txt(CMax);
        r.unit = txt(CUnit);
        if (auto* cb = qobject_cast<QComboBox*>(table_->cellWidget(row, CSev))) r.severity = cb->currentIndex() == 0 ? 2 : 1;
        r.norm = txt(CNorm);
        r.clause = txt(CClause);
        r.verified = on(CVerified);
        r.note = txt(CNote);
        if (r.param.empty()) errs << t("gui.norm.r.noparam", {QString::number(row + 1)});
        if (r.min.empty() && r.max.empty()) errs << t("gui.norm.r.nolimit", {QString::number(row + 1)});
        const std::pair<int, const std::string*> exprs[] = {{CWhen, &r.when}, {CParam, &r.param}, {CMin, &r.min}, {CMax, &r.max}};
        for (const auto& [col, ex] : exprs) {
            std::string e;
            const bool ok = ex->empty() || bim::normSyntaxOk(*ex, &e);
            if (auto* it = table_->item(row, col)) it->setBackground(ok ? QBrush() : QBrush(QColor(229, 72, 77, 90)));
            if (!ok) errs << t("gui.norm.r.syntax", {QString::number(row + 1), QString::fromStdString(*ex), QString::fromStdString(e)});
        }
        out.push_back(std::move(r));
    }
    if (errs.isEmpty()) rules_ = std::move(out);
    return errs.join('\n');
}

} // namespace asiacad::gui
