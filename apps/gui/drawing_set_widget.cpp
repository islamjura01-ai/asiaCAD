#include "drawing_set_widget.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <format>

namespace asiacad::gui {

namespace {
enum Col { CFile = 0, CRole, CFrom, CTo, CScale };
}

DrawingSetWidget::DrawingSetWidget(const i18n::Localizer& loc, const QString& title, std::vector<std::pair<QString, QString>> roles, bool floors,
                                   QWidget* parent)
    : QWidget(parent), loc_(loc), roles_(std::move(roles)), floors_(floors) {
    auto t = [this](const char* k) { return QString::fromStdString(loc_.tr(k)); };
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    auto* tl = new QLabel(title, this);
    tl->setObjectName("title");
    v->addWidget(tl);
    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels({t("gui.ds.file"), t("gui.ds.role"), t("gui.ds.from"), t("gui.ds.to"), t("gui.ds.scale")});
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(CFile, QHeaderView::Stretch);
    table_->setColumnHidden(CFrom, !floors_);
    table_->setColumnHidden(CTo, !floors_);
    table_->setMinimumHeight(110);
    table_->setMaximumHeight(180);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->addWidget(table_);
    auto* h = new QHBoxLayout;
    auto* add = new QPushButton(t("gui.ds.add"), this);
    auto* rem = new QPushButton(t("gui.ds.remove"), this);
    h->addWidget(add);
    h->addWidget(rem);
    v->addLayout(h);
    status_ = new QLabel(this);
    status_->setObjectName("muted");
    status_->setWordWrap(true);
    v->addWidget(status_);
    refreshStatus();
    connect(add, &QPushButton::clicked, this, [this, t] {
        const QStringList files = QFileDialog::getOpenFileNames(this, t("gui.ds.add"), QString(), t("gui.ds.filter"));
        for (const auto& f : files) addFile(f, roles_.empty() ? "AUTO" : roles_.front().first);
    });
    connect(rem, &QPushButton::clicked, this, [this] {
        const auto rows = table_->selectionModel()->selectedRows();
        if (rows.isEmpty()) return;
        table_->removeRow(rows.front().row());
        refreshStatus();
        emit changed();
    });
    connect(table_, &QTableWidget::itemChanged, this, [this] {
        if (!updating_) { refreshStatus(); emit changed(); }
    });
}

void DrawingSetWidget::addRow(const civil::SheetFile& f) {
    updating_ = true;
    const int r = table_->rowCount();
    table_->insertRow(r);
    auto* it = new QTableWidgetItem(QString::fromStdString(civil::fileName(f.path)));
    it->setData(Qt::UserRole, QString::fromStdString(f.path));
    it->setToolTip(QString::fromStdString(f.path));
    it->setFlags((it->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    it->setCheckState(f.enabled ? Qt::Checked : Qt::Unchecked);
    table_->setItem(r, CFile, it);
    auto* role = new QComboBox(table_);
    for (const auto& [k, label] : roles_) role->addItem(label, k);
    role->setCurrentIndex(std::max(0, role->findData(QString::fromStdString(f.role))));
    connect(role, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) emit changed(); });
    table_->setCellWidget(r, CRole, role);
    for (int c : {CFrom, CTo}) {
        auto* sp = new QSpinBox(table_);
        sp->setRange(0, 200);
        sp->setValue(c == CFrom ? f.floorFrom : f.floorTo);
        connect(sp, &QSpinBox::valueChanged, this, [this] { if (!updating_) emit changed(); });
        table_->setCellWidget(r, c, sp);
    }
    auto* sc = new QComboBox(table_);
    sc->addItem(QString::fromStdString(loc_.tr("gui.ds.auto")), 0.0);
    sc->addItem("mm", 0.001);
    sc->addItem("sm", 0.01);
    sc->addItem("m", 1.0);
    sc->setCurrentIndex(std::max(0, sc->findData(f.scale)));
    connect(sc, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) { refreshStatus(); emit changed(); } });
    table_->setCellWidget(r, CScale, sc);
    updating_ = false;
}

void DrawingSetWidget::addFile(const QString& path, const QString& role, int from, int to) {
    civil::SheetFile f;
    f.path = path.toStdString();
    f.role = role.toStdString();
    f.floorFrom = from;
    f.floorTo = std::max(from, to);
    addRow(f);
    refreshStatus();
    emit changed();
}

std::vector<civil::SheetFile> DrawingSetWidget::sheets() const {
    std::vector<civil::SheetFile> out;
    for (int r = 0; r < table_->rowCount(); ++r) {
        civil::SheetFile f;
        f.path = table_->item(r, CFile)->data(Qt::UserRole).toString().toStdString();
        f.enabled = table_->item(r, CFile)->checkState() == Qt::Checked;
        if (auto* c = qobject_cast<QComboBox*>(table_->cellWidget(r, CRole))) f.role = c->currentData().toString().toStdString();
        if (auto* s = qobject_cast<QSpinBox*>(table_->cellWidget(r, CFrom))) f.floorFrom = s->value();
        if (auto* s = qobject_cast<QSpinBox*>(table_->cellWidget(r, CTo))) f.floorTo = std::max(f.floorFrom, s->value());
        if (auto* c = qobject_cast<QComboBox*>(table_->cellWidget(r, CScale))) f.scale = c->currentData().toDouble();
        out.push_back(f);
    }
    return out;
}

void DrawingSetWidget::setSheets(const std::vector<civil::SheetFile>& s) {
    table_->setRowCount(0);
    for (const auto& f : s) addRow(f);
    refreshStatus();
    emit changed();
}

int DrawingSetWidget::count() const { return table_->rowCount(); }

std::vector<civil::LoadedSheet> DrawingSetWidget::loaded() {
    std::vector<civil::LoadedSheet> out;
    for (const auto& f : sheets()) {
        if (!f.enabled) continue;
        const std::string key = f.path + std::format("|{}|{}|{}", f.scale, f.dx, f.dy);
        auto it = cache_.find(key);
        if (it == cache_.end()) it = cache_.emplace(key, civil::loadSheet(f)).first;
        civil::LoadedSheet ls = it->second;
        ls.file = f;
        out.push_back(ls);
    }
    return out;
}

void DrawingSetWidget::refreshStatus() {
    int n = 0, bad = 0;
    QString errs;
    for (const auto& ls : loaded()) {
        ++n;
        if (!ls.error.empty()) {
            ++bad;
            errs += "\n" + QString::fromStdString(civil::fileName(ls.file.path) + ": " + ls.error);
        }
    }
    status_->setText(n == 0 ? QString::fromStdString(loc_.tr("gui.ds.none"))
                            : QString::fromStdString(loc_.tr("gui.ds.status", {std::to_string(n), std::to_string(bad)})) + errs);
}

} // namespace asiacad::gui
