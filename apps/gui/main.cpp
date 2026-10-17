// AsiaCAD — grafik interfeys (Qt 6)
//   asiacad-gui [fayl.acad|fayl.dxf] [--lang uz] [--mode classic|module|focus] [--i18n papka]
// Sinov/hujjat uchun:
//   --screenshot out.png [--size 1440x900] [--select-all] [--tool line] [--point x,y]... [--cursor x,y]
//   [--cmd "LINE 0,0 10,10"]...
#include "main_window.h"
#include "sewer_dialog.h"
#include "building_dialog.h"
#include "rebar_dialog.h"
#include "utility_dialog.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QScreen>
#include <QSurfaceFormat>
#include <QFile>
#include <QTimer>

#include <cstdio>

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
fs::path findI18nDir(const QString& cli) {
    std::vector<fs::path> c;
    if (!cli.isEmpty()) c.emplace_back(cli.toStdString());
    c.emplace_back((QCoreApplication::applicationDirPath() + "/i18n").toStdString());
    c.emplace_back("i18n");
#ifdef ASIACAD_I18N_DIR
    c.emplace_back(ASIACAD_I18N_DIR);
#endif
    std::error_code ec;
    for (const auto& p : c)
        if (fs::is_directory(p, ec)) return p;
    return {};
}
} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setSamples(4);  // chiziqlar silliq
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication app(argc, argv);
    QApplication::setApplicationName("AsiaCAD");
    QApplication::setOrganizationName("AsiaCAD");
    QFont f = QApplication::font();
    f.setPointSizeF(10);
    QApplication::setFont(f);

    QString file, lang, mode, i18nDir, shot, size = "1440x900", tool, cursor, select, grip, value, sewerFile, sewerTab, sewerTpl, viewArg;
    bool sewer = false;
    int building = 0;
    bool rebar = false;
    bool util = false;
    QString rebarKind;
    bool ortho = false, polar = false;
    QStringList points;
    QStringList cmds;
    bool selectAll = false;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args[i];
        auto next = [&] { return i + 1 < args.size() ? args[++i] : QString(); };
        if (a == "--lang") lang = next();
        else if (a == "--mode") mode = next();
        else if (a == "--i18n") i18nDir = next();
        else if (a == "--screenshot") shot = next();
        else if (a == "--size") size = next();
        else if (a == "--tool") tool = next();
        else if (a == "--cmd") cmds << next();
        else if (a == "--select-all") selectAll = true;
        else if (a == "--point") points << next();
        else if (a == "--cursor") cursor = next();
        else if (a == "--select") select = next();
        else if (a == "--grip") grip = next();
        else if (a == "--value") value = next();  // OFFSET masofasi / FILLET radiusi
        else if (a == "--view") viewArg = next();  // cx,cy,piksel/birlik
        else if (a == "--rebar") rebar = true;
        else if (a == "--util") util = true;  // Yer osti tarmoqlari 3D  // Armatura 3D menyusi (bino --building N dan)
        else if (a == "--rebar-kind") rebarKind = next();  // FOUND/STRIP/COLUMN/WALL/SLAB/ROOF
        else if (a == "--building") building = next().toInt();  // Monolit bino menyusi (qavatlar)
        else if (a == "--sewer") sewer = true;                 // Kanalizatsiya menyusi (skrinshot uni oladi)
        else if (a == "--sewer-file") { sewer = true; sewerFile = next(); }
        else if (a == "--sewer-tab") sewerTab = next();
        else if (a == "--sewer-tpl") sewerTpl = next();
        else if (a == "--ortho") ortho = true;
        else if (a == "--polar") polar = true;
        else file = a;
    }
    if (lang.isEmpty()) lang = qEnvironmentVariable("ASIACAD_LANG", "uz");

    asiacad::i18n::Localizer loc;
    const fs::path dir = findI18nDir(i18nDir);
    if (dir.empty() || loc.loadDirectory(dir) == 0) {
        QMessageBox::critical(nullptr, "AsiaCAD", "i18n/*.json not found");
        return 1;
    }
    if (!loc.setLanguage(lang.toStdString())) loc.setLanguage("en");

    asiacad::gui::MainWindow w(loc);
    if (!mode.isEmpty()) w.setMode(asiacad::gui::modeFromKey(mode));
    const auto wh = size.split('x');
    if (wh.size() == 2) w.resize(wh[0].toInt(), wh[1].toInt());
    w.show();
    if (!file.isEmpty()) w.runLine(QStringLiteral("OPEN \"%1\" !").arg(QFileInfo(file).absoluteFilePath()));
    for (const auto& c : cmds) w.runLine(c);

    if (!shot.isEmpty()) {
        QTimer::singleShot(300, &w, [&] {
            w.viewport()->zoomExtents();
            if (const auto v = viewArg.split(','); v.size() == 3) w.viewport()->setView({v[0].toDouble(), v[1].toDouble(), 0}, v[2].toDouble());
            if (selectAll) w.viewport()->setSelection(w.document().ids());
            if (!select.isEmpty()) {
                std::vector<asiacad::EntityId> ids;
                for (const auto& t : select.split(',')) ids.push_back(t.toULongLong());
                w.viewport()->setSelection(ids);
            }
            if (ortho) w.viewport()->setOrtho(true);
            if (polar) w.viewport()->setPolar(true);
            if (const auto gi = grip.split(','); gi.size() == 2) w.viewport()->beginGrip(gi[0].toULongLong(), gi[1].toInt());
            if (!tool.isEmpty()) w.activateTool(tool);
            if (building > 0) w.openBuilding()->setFloors(building);
            if (util) {
                auto* ud = w.openUtility();
                if (!select.isEmpty()) ud->selectLine(select.toInt());
            }
            if (rebar) {
                auto* rd = w.openRebar();
                asiacad::bim::ElemKind k;
                if (!rebarKind.isEmpty() && asiacad::bim::kindFromKey(rebarKind.toStdString(), k)) rd->selectKind(k);
            }
            if (sewer) {
                auto* d = w.openSewer();
                if (!sewerTpl.isEmpty()) d->selectTemplate(sewerTpl.toInt());
                if (!sewerFile.isEmpty()) {
                    QFile f(sewerFile);
                    if (f.open(QIODevice::ReadOnly)) d->setNetwork(asiacad::civil::fromJson(f.readAll().toStdString()));
                }
                if (!sewerTab.isEmpty()) d->showTab(sewerTab.toInt());
            }
            if (!value.isEmpty()) {
                w.viewport()->setOffsetDistance(value.toDouble());
                w.viewport()->setFilletRadius(value.toDouble());
            }
            for (const auto& p : points) {
                const auto xy = p.split(',');
                if (xy.size() == 2) w.viewport()->feedPoint({xy[0].toDouble(), xy[1].toDouble(), 0});
            }
            if (!cursor.isEmpty()) {
                const auto xy = cursor.split(',');
                if (xy.size() == 2) w.viewport()->simulateCursor({xy[0].toDouble(), xy[1].toDouble(), 0});
            }
        });
        QTimer::singleShot(1200, &w, [&] {
            if (qEnvironmentVariableIsSet("ASIACAD_DEBUG_SIZES"))
                for (QWidget* c : w.findChildren<QWidget*>())
                    if (c->isVisible() && c->minimumSizeHint().width() > 500)
                        std::fprintf(stderr, "%s %s %d\n", c->metaObject()->className(), qPrintable(c->objectName()), c->minimumSizeHint().width());
            QWidget* target = &w;
            for (QWidget* tl : QApplication::topLevelWidgets())
                if ((sewer && tl->isVisible() && tl->inherits("asiacad::gui::SewerDialog")) ||
                    (building > 0 && !rebar && tl->isVisible() && tl->inherits("asiacad::gui::BuildingDialog")) ||
                    (rebar && tl->isVisible() && tl->inherits("asiacad::gui::RebarDialog")) ||
                    (util && tl->isVisible() && tl->inherits("asiacad::gui::UtilityDialog")))
                    target = tl;
            target->grab().save(shot);
            QApplication::exit(0);
        });
    }
    return app.exec();
}
