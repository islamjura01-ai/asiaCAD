#include "excel_link.h"

#include <asiacad/core/error.h>

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace asiacad::gui {

bool ExcelLink::openExternally = true;

ExcelLink::ExcelLink(const i18n::Localizer& loc, QWidget* owner, const QString& baseName, std::function<xlsx::Workbook()> build,
                     std::function<void(const xlsx::Workbook&)> apply)
    : QObject(owner), loc_(loc), owner_(owner), base_(baseName), build_(std::move(build)), apply_(std::move(apply)) {
    watcher_ = new QFileSystemWatcher(this);
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(700);  // Excel faylni bir necha bosqichda yozadi
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, [this] { debounce_->start(); });
    connect(debounce_, &QTimer::timeout, this, &ExcelLink::onChanged);
}

void ExcelLink::addButtons(QHBoxLayout* layout) {
    auto tr = [this](const char* k) { return QString::fromStdString(loc_.tr(k)); };
    auto* ex = new QPushButton(tr("gui.xl.export"), owner_);
    auto* im = new QPushButton(tr("gui.xl.import"), owner_);
    ex->setObjectName("excelExport");
    im->setObjectName("excelImport");
    ex->setToolTip(tr("gui.xl.filter"));
    layout->addWidget(ex);
    layout->addWidget(im);
    connect(ex, &QPushButton::clicked, this, [this, tr] {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/AsiaCAD";
        QDir().mkpath(dir);
        const QString suggested = path_.isEmpty() ? dir + "/" + base_ + ".xlsx" : path_;
        const QString f = QFileDialog::getSaveFileName(owner_, tr("gui.xl.export"), suggested, tr("gui.xl.filter"));
        if (!f.isEmpty()) exportTo(f.endsWith(".xlsx", Qt::CaseInsensitive) ? f : f + ".xlsx", openExternally);
    });
    connect(im, &QPushButton::clicked, this, [this, tr] {
        const QString f = QFileDialog::getOpenFileName(owner_, tr("gui.xl.import"), path_, tr("gui.xl.filter"));
        if (!f.isEmpty() && importFrom(f)) watch(f);
    });
}

bool ExcelLink::exportTo(const QString& path, bool openInApp) {
    try {
        selfWrite_ = QDateTime::currentMSecsSinceEpoch();
        xlsx::save(build_(), path.toStdString());
    } catch (const Error& e) {
        report(QString::fromStdString(loc_.tr(e.key(), e.args())), true);
        return false;
    }
    watch(path);
    report(QString::fromStdString(loc_.tr("gui.xl.linked", {QFileInfo(path).fileName().toStdString()})), false);
    if (openInApp) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    return true;
}

bool ExcelLink::importFrom(const QString& path) {
    try {
        apply_(xlsx::load(path.toStdString()));
    } catch (const Error& e) {
        report(QString::fromStdString(loc_.tr(e.key(), e.args())), true);
        return false;
    } catch (const std::exception& e) {
        report(QString::fromUtf8(e.what()), true);
        return false;
    }
    report(QString::fromStdString(loc_.tr("gui.xl.reloaded", {QFileInfo(path).fileName().toStdString() + "  " +
                                                                QTime::currentTime().toString("HH:mm:ss").toStdString()})),
           false);
    emit reloaded(path);
    return true;
}

void ExcelLink::watch(const QString& path) {
    if (!path_.isEmpty()) watcher_->removePath(path_);
    path_ = path;
    watcher_->addPath(path);
}

void ExcelLink::onChanged() {
    if (QDateTime::currentMSecsSinceEpoch() - selfWrite_ < 1500) {  // o'zimiz yozdik
        if (!watcher_->files().contains(path_) && QFileInfo::exists(path_)) watcher_->addPath(path_);
        return;
    }
    if (!QFileInfo::exists(path_)) {  // Excel vaqtinchalik nomlash bilan saqlaydi — qayta urinish
        debounce_->start();
        return;
    }
    importFrom(path_);
    if (!watcher_->files().contains(path_)) watcher_->addPath(path_);  // almashtirilgan fayl kuzatuvdan chiqadi
}

void ExcelLink::report(const QString& text, bool error) {
    if (status_) {
        status_->setText(text);
        status_->setStyleSheet(error ? "color:#E84650;" : QString());
    } else if (error && owner_ && owner_->isVisible()) {
        QMessageBox::warning(owner_, owner_->windowTitle(), text);
    }
}

} // namespace asiacad::gui
