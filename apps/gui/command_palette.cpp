#include "command_palette.h"

#include <asiacad/i18n/utf8.h>

#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace asiacad::gui {

CommandPalette::CommandPalette(const i18n::Localizer& loc, QWidget* parent) : QDialog(parent), loc_(loc) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setObjectName("palette");
    setMinimumWidth(600);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 8);
    lay->setSpacing(8);
    edit_ = new QLineEdit(this);
    edit_->setObjectName("paletteEdit");
    edit_->setClearButtonEnabled(true);
    list_ = new QListWidget(this);
    list_->setObjectName("paletteList");
    list_->setMinimumHeight(340);
    list_->setIconSize(QSize(18, 18));
    list_->setUniformItemSizes(true);
    footer_ = new QLabel(this);
    footer_->setObjectName("muted");
    lay->addWidget(edit_);
    lay->addWidget(list_);
    lay->addWidget(footer_);
    edit_->installEventFilter(this);
    connect(edit_, &QLineEdit::textChanged, this, &CommandPalette::refill);
    connect(list_, &QListWidget::itemActivated, this, [this] { accept(); });
}

void CommandPalette::open(QWidget* anchor) {
    edit_->setPlaceholderText(QString::fromStdString(loc_.tr("gui.palette.hint")));
    footer_->setText(QString::fromStdString(loc_.tr("gui.palette.footer")));
    entries_ = provider_ ? provider_() : QList<Entry>{};
    edit_->clear();
    refill({});
    if (anchor) {
        const QPoint top = anchor->mapToGlobal(QPoint((anchor->width() - width()) / 2, 40));
        move(top);
    }
    show();
    edit_->setFocus();
}

void CommandPalette::setFilter(const QString& f) {
    if (entries_.isEmpty() && provider_) entries_ = provider_();
    edit_->setText(f);
    refill(f);
}

int CommandPalette::resultCount() const { return list_->count(); }

void CommandPalette::refill(const QString& filter) {
    list_->clear();
    const std::string f = asiacad::utf8::foldKey(filter.trimmed().toStdString());
    std::set<std::string> seenCanon;
    // 1) lenta amallari — joyi va tezkor tugmasi bilan (lentani o'rgatadi).
    //    Tartib: nomi so'rov bilan boshlanadi > nomida bor > buyruq/kalit so'zda > faqat joyida (varaq/guruh)
    struct Hit { int score; int order; const Entry* e; };
    std::vector<Hit> hits;
    for (int i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        int score = 0;
        if (!f.empty()) {
            const std::string lab = asiacad::utf8::foldKey(e.label.toStdString());
            const std::string kw = asiacad::utf8::foldKey(e.keywords.toStdString());
            const std::string pth = asiacad::utf8::foldKey(e.path.toStdString());
            if (lab.rfind(f, 0) == 0) score = 4;
            else if (lab.find(f) != std::string::npos) score = 3;
            else if (kw.find(f) != std::string::npos) score = 2;
            else if (pth.find(f) != std::string::npos) score = 1;
            else continue;
        }
        hits.push_back({score, i, &e});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    for (const auto& h : hits) {
        const Entry& e = *h.e;
        QString text = e.label;
        if (!e.path.isEmpty()) text += QStringLiteral("    ·    ") + e.path;
        if (!e.shortcut.isEmpty()) text += QStringLiteral("    [") + e.shortcut + ']';
        auto* it = new QListWidgetItem(e.icon, text, list_);
        it->setData(Qt::UserRole, e.id);
        it->setToolTip(e.keywords);
    }
    // 2) buyruqlar (CLI) — istalgan tildagi nom va aliaslar bilan
    const std::string fr = filter.trimmed().toStdString();
    const std::string aliasCanon = fr.empty() ? std::string{} : loc_.resolveCommand(fr);
    for (const auto& canon : loc_.canonicalCommands()) {
        const std::string name = loc_.commandName(canon);
        const std::string desc = loc_.hasMessage("desc." + canon) ? loc_.tr("desc." + canon) : std::string{};
        if (!f.empty()) {
            const std::string hay = asiacad::utf8::foldKey(canon + " " + name + " " + desc);
            if (hay.find(f) == std::string::npos && aliasCanon != canon) continue;
        }
        auto* it = new QListWidgetItem(QString::fromStdString(name + "   ·   " + desc), list_);
        it->setData(Qt::UserRole, QString::fromStdString(canon));
        // aniq alias (masalan "L", "C", "ЛИНИЯ") — ro'yxat boshiga
        if (!aliasCanon.empty() && aliasCanon == canon) list_->insertItem(0, list_->takeItem(list_->row(it)));
    }
    if (list_->count()) list_->setCurrentRow(0);
}

bool CommandPalette::eventFilter(QObject* o, QEvent* e) {
    if (o == edit_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Down || k->key() == Qt::Key_Up) {
            const int r = list_->currentRow() + (k->key() == Qt::Key_Down ? 1 : -1);
            if (r >= 0 && r < list_->count()) list_->setCurrentRow(r);
            return true;
        }
        if (k->key() == Qt::Key_PageDown || k->key() == Qt::Key_PageUp) {
            const int r = std::clamp(list_->currentRow() + (k->key() == Qt::Key_PageDown ? 10 : -10), 0, std::max(0, list_->count() - 1));
            list_->setCurrentRow(r);
            return true;
        }
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) { accept(); return true; }
        if (k->key() == Qt::Key_Escape) { reject(); return true; }
    }
    return QDialog::eventFilter(o, e);
}

void CommandPalette::accept() {
    const QString id = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString();
    QDialog::accept();
    if (!id.isEmpty()) emit commandChosen(id);
}

} // namespace asiacad::gui
