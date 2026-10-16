#include "command_palette.h"

#include <asiacad/i18n/utf8.h>

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace asiacad::gui {

CommandPalette::CommandPalette(const i18n::Localizer& loc, QWidget* parent) : QDialog(parent), loc_(loc) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setMinimumWidth(520);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);
    edit_ = new QLineEdit(this);
    list_ = new QListWidget(this);
    list_->setMinimumHeight(300);
    lay->addWidget(edit_);
    lay->addWidget(list_);
    edit_->installEventFilter(this);
    connect(edit_, &QLineEdit::textChanged, this, &CommandPalette::refill);
    connect(list_, &QListWidget::itemActivated, this, [this] { accept(); });
}

void CommandPalette::open(QWidget* anchor) {
    edit_->setPlaceholderText(QString::fromStdString(loc_.tr("gui.palette.hint")));
    edit_->clear();
    refill({});
    if (anchor) {
        const QPoint top = anchor->mapToGlobal(QPoint((anchor->width() - width()) / 2, 40));
        move(top);
    }
    show();
    edit_->setFocus();
}

void CommandPalette::refill(const QString& filter) {
    list_->clear();
    const std::string f = asiacad::utf8::foldKey(filter.trimmed().toStdString());
    for (const auto& canon : loc_.canonicalCommands()) {
        const std::string name = loc_.commandName(canon);
        const std::string desc = loc_.hasMessage("desc." + canon) ? loc_.tr("desc." + canon) : std::string{};
        if (!f.empty()) {
            const std::string hay = asiacad::utf8::foldKey(canon + " " + name + " " + desc);
            const bool alias = !loc_.resolveCommand(filter.trimmed().toStdString()).empty() &&
                               loc_.resolveCommand(filter.trimmed().toStdString()) == canon;
            if (hay.find(f) == std::string::npos && !alias) continue;
        }
        auto* it = new QListWidgetItem(QString::fromStdString(name + "   ·   " + desc), list_);
        it->setData(Qt::UserRole, QString::fromStdString(canon));
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
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) { accept(); return true; }
        if (k->key() == Qt::Key_Escape) { reject(); return true; }
    }
    return QDialog::eventFilter(o, e);
}

void CommandPalette::accept() {
    if (auto* it = list_->currentItem()) emit commandChosen(it->data(Qt::UserRole).toString());
    QDialog::accept();
}

} // namespace asiacad::gui
