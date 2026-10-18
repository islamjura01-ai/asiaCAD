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
#include "road_dialog.h"
#include "waterway_dialog.h"
#include "corridor_dialog.h"
#include "generator_dialogs.h"
#include "drive_dialog.h"
#include "templates_dialog.h"
#include "survey_dialog.h"

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
    bool road = false;
    bool waterway = false;
    bool corridor = false;
    bool city = false;
    int cityTpl = -1;
    QString scratch;
    int tplPage = -1;
    int surveyKind = -1;
    QString surveyRef;
    bool geoDlg = false;
    bool drive = false;
    int driveMode = -1, driveSrc = -1;
    double driveAt = -1;
    int asmIndex = -1, station = -1;
    QStringList sheetArgs, kjArgs;
    QString rebarKind;
    bool noConcrete = false;
    int upTo = -1;
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
        else if (a == "--util") util = true;  // Yer osti tarmoqlari 3D
        else if (a == "--road") road = true;
        else if (a == "--waterway") waterway = true;
        else if (a == "--corridor") corridor = true;
        else if (a == "--drive") drive = true;                   // Haydash oynasi (tanlangan chiziq yoki koridor bo'ylab)
        else if (a == "--drive-mode") driveMode = next().toInt();
        else if (a == "--drive-src") driveSrc = next().toInt();
        else if (a == "--drive-at") driveAt = next().toDouble();
        else if (a == "--city") city = true;                     // Shahar mavzesi oynasi
        else if (a == "--city-tpl") cityTpl = next().toInt();    // shablon raqami (0 dan)
        else if (a == "--tpl") tplPage = next().toInt();          // konstruksiya shablonlari: 0 karkas, 1 zina, 2 rampa, 3 tirgak devor
        else if (a == "--geo") geoDlg = true;
        else if (a == "--survey") surveyKind = next().toInt();     // geodezik tahlil oynasi (0 reja, 1 balandlik, 2 vertikal, 3 qiya o'q, 4 nuqta)
        else if (a == "--survey-ref") surveyRef = next();          // loyiha qatlami                    // geo-joylash oynasi (tanlov bilan)
        else if (a == "--scratch") scratch = next();             // "0 dan" oynasi: building | road | waterway | util
        else if (a == "--asm") asmIndex = next().toInt();
        else if (a == "--station") station = next().toInt();
        else if (a == "--sheet") sheetArgs << next();  // ROL,yo'l[,dan,gacha] — yer osti (--util) yoki bino chizmalari
        else if (a == "--kj") kjArgs << next();        // ROL,yo'l,dan,gacha — armatura KJ chizmalari  // Yo'l 3D (--sewer-tab 1 — profil)  // Armatura 3D menyusi (bino --building N dan)
        else if (a == "--rebar-kind") rebarKind = next();
        else if (a == "--no-concrete") noConcrete = true;
        else if (a == "--upto") upTo = next().toInt();  // FOUND/STRIP/COLUMN/WALL/SLAB/ROOF
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
            auto addSheets = [](asiacad::gui::DrawingSetWidget* ds, const QStringList& list) {
                for (const auto& sa : list) {
                    const auto f = sa.split(',');
                    if (f.size() >= 2) ds->addFile(f[1], f[0], f.size() > 2 ? f[2].toInt() : 1, f.size() > 3 ? f[3].toInt() : 1);
                }
            };
            if (!sheetArgs.isEmpty() && !util && building > 0) addSheets(w.openBuilding()->drawings(), sheetArgs);
            if (road) {
                auto* rd = w.openRoad();
                if (!sewerTab.isEmpty()) rd->showTab(sewerTab.toInt());
            }
            if (corridor) {
                auto* cd = w.openCorridor();
                const auto all = asiacad::civil::builtinAssemblies();
                if (asmIndex >= 0 && asmIndex < static_cast<int>(all.size())) cd->applyAssembly(all[static_cast<std::size_t>(asmIndex)]);
                if (!sewerTab.isEmpty()) cd->showTab(sewerTab.toInt());
                if (station >= 0) cd->showStation(station);
            }
            if (city) {
                auto* cd = w.openCity();
                if (cityTpl >= 0) cd->sidebar()->activate(cityTpl);
            }
            if (!scratch.isEmpty()) w.openScratch(scratch);
            if (tplPage >= 0) w.openTemplates(tplPage);
            if (geoDlg) w.openGeoPlace();
            if (surveyKind >= 0) {
                auto* sd = w.openSurvey(surveyKind);
                if (!surveyRef.isEmpty()) sd->setRefLayer(surveyRef);
                sd->run();
            }
            if (drive) {
                auto* dd = w.openDrive();
                if (driveSrc >= 0) dd->setSource(driveSrc);
                if (driveMode >= 0) dd->setMode(driveMode);
                if (driveAt >= 0) dd->setStation(driveAt);
            }
            if (waterway) {
                auto* wd = w.openWaterway();
                if (!sewerTab.isEmpty()) wd->showTab(sewerTab.toInt());
            }
            if (util) {
                auto* ud = w.openUtility();
                if (!sheetArgs.isEmpty()) addSheets(ud->drawings(), sheetArgs);
                if (!select.isEmpty()) ud->selectLine(select.toInt());
            }
            if (rebar) {
                auto* rd = w.openRebar();
                if (!kjArgs.isEmpty()) addSheets(rd->drawings(), kjArgs);
                asiacad::bim::ElemKind k;
                if (!rebarKind.isEmpty() && asiacad::bim::kindFromKey(rebarKind.toStdString(), k)) rd->selectKind(k);
                if (noConcrete) rd->setShowConcrete(false);
                if (upTo >= 0) rd->setUpTo(upTo);
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
                    (building > 0 && !rebar && !drive && tl->isVisible() && tl->inherits("asiacad::gui::BuildingDialog")) ||
                    (rebar && tl->isVisible() && tl->inherits("asiacad::gui::RebarDialog")) ||
                    (util && tl->isVisible() && tl->inherits("asiacad::gui::UtilityDialog")) ||
                    (road && tl->isVisible() && tl->inherits("asiacad::gui::RoadDialog")) ||
                    (waterway && tl->isVisible() && tl->inherits("asiacad::gui::WaterwayDialog")) ||
                    (corridor && !drive && tl->isVisible() && tl->inherits("asiacad::gui::CorridorDialog")) ||
                    (city && tl->isVisible() && tl->inherits("asiacad::gui::CityDialog")) ||
                    (surveyKind >= 0 && tl->isVisible() && tl->inherits("asiacad::gui::SurveyDialog")) ||
                    (drive && tl->isVisible() && tl->inherits("asiacad::gui::DriveDialog")) ||
                    ((!scratch.isEmpty() || tplPage >= 0 || geoDlg) && tl->isVisible() && tl->inherits("asiacad::gui::GenBase")))
                    target = tl;
            target->grab().save(shot);
            QApplication::exit(0);
        });
    }
    return app.exec();
}
