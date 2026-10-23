// GUI tutun testi: oyna ko'rsatilmaydi (offscreen), lekin asboblar -> Interpreter -> Document zanjiri
// to'liq tekshiriladi. ctest orqali QT_QPA_PLATFORM=offscreen bilan ishga tushadi.
#include "main_window.h"
#include "sewer_dialog.h"
#include "building_dialog.h"
#include "rebar_dialog.h"
#include "excel_link.h"
#include "utility_dialog.h"
#include "road_dialog.h"
#include "waterway_dialog.h"
#include "corridor_dialog.h"
#include "generator_dialogs.h"
#include "templates_dialog.h"
#include "survey_dialog.h"
#include "earthwork_dialog.h"
#include "drive_dialog.h"
#include "draft_panels.h"

#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <asiacad/io/xlsx.h>

#include <QPushButton>
#include <QListWidget>
#include <QToolButton>
#include <QImage>
#include "sheet_view.h"
#include <QCheckBox>
#include "smart_panel.h"
#include <QLineEdit>
#include <asiacad/civil/sewer.h>
#include <QMenu>
#include <QKeyEvent>
#include <QTabWidget>
#include <QTableWidget>

#include <asiacad/core/dimension.h>
#include <asiacad/core/entities.h>

#include <QMouseEvent>

#include <QApplication>
#include <QFile>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>

#include <cstdio>
#include <string>

using namespace asiacad;
using namespace asiacad::gui;

// Haqiqiy sichqoncha hodisalari (bog'lanish/orto/qutb shu yo'l orqali ishlaydi)
static void mouse(QWidget* w, QEvent::Type t, const QPointF& pos, Qt::MouseButtons held) {
    QMouseEvent e(t, pos, w->mapToGlobal(pos), t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, held, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}
static void click(Viewport* v, const Vec3& p) {
    const QPointF s = v->toScreen(p);
    mouse(v, QEvent::MouseMove, s, Qt::NoButton);
    mouse(v, QEvent::MouseButtonPress, s, Qt::LeftButton);
    mouse(v, QEvent::MouseButtonRelease, s, Qt::NoButton);
}
static const LineEntity* lastLine(Document& doc) {
    const LineEntity* r = nullptr;
    doc.forEach([&](const IEntity& e) { if (e.type() == EntityType::Line) r = static_cast<const LineEntity*>(&e); });
    return r;
}

static int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    asiacad::gui::ExcelLink::openExternally = false;
    i18n::Localizer loc;
    CHECK(loc.loadDirectory(ASIACAD_I18N_DIR) == 6);
    loc.setLanguage("uz");
    MainWindow w(loc);
    Document& doc = w.document();
    Viewport* v = w.viewport();
    v->resize(800, 600);
    v->setView({50, 50, 0}, 5.0);

    // 1. Chiziq asbobi: 3 nuqta -> 2 ta LINE, har biri alohida undo qadami
    w.activateTool("line");
    CHECK(v->tool() == Tool::Line);
    v->feedPoint({0, 0, 0});
    v->feedPoint({10, 0, 0});
    v->feedPoint({10, 10, 0});
    v->finishTool();
    CHECK(doc.size() == 2);
    CHECK(v->tool() == Tool::Select);
    w.runLine("UNDO");
    CHECK(doc.size() == 1);
    w.runLine("REDO");
    CHECK(doc.size() == 2);

    // 2. Aylana: markaz + radius nuqtasi
    w.activateTool("circle");
    v->feedPoint({30, 30, 0});
    v->feedPoint({35, 30, 0});
    CHECK(doc.size() == 3);
    bool hasCircle = false;
    doc.forEach([&](const IEntity& e) {
        if (e.type() == EntityType::Circle) hasCircle = std::abs(static_cast<const CircleEntity&>(e).radius() - 5.0) < 1e-9;
    });
    CHECK(hasCircle);

    // 3. Poliliniya: birinchi nuqtaga qaytish -> yopiq
    w.activateTool("pline");
    v->feedPoint({0, 40, 0});
    v->feedPoint({20, 40, 0});
    v->feedPoint({20, 60, 0});
    v->feedPoint({0, 40, 0});  // yopish
    bool closedPl = false;
    doc.forEach([&](const IEntity& e) {
        if (e.type() == EntityType::Polyline) closedPl = static_cast<const PolylineEntity&>(e).isClosed();
    });
    CHECK(closedPl);
    CHECK(doc.size() == 4);

    // 4. Yoy: 3 nuqta
    w.activateTool("arc");
    v->feedPoint({50, 0, 0});
    v->feedPoint({60, 10, 0});
    v->feedPoint({70, 0, 0});
    CHECK(doc.size() == 5);

    // 5. Tanlash va ko'chirish
    v->setSelection(doc.ids());
    w.activateTool("move");
    CHECK(v->tool() == Tool::Move);
    v->feedPoint({0, 0, 0});
    v->feedPoint({100, 0, 0});
    BBox b = doc.extents();
    CHECK(b.min.x >= 99.999);
    w.runLine("UNDO");
    b = doc.extents();
    CHECK(b.min.x < 1e-9);

    // 6. Tanlovsiz ko'chirish — asbob yoqilmaydi
    v->setSelection({});
    w.activateTool("move");
    CHECK(v->tool() == Tool::Select);

    // 7. O'chirish: tanlangan obyektlar va tanlov tozalanishi
    v->setSelection({doc.ids().front()});
    w.runLine("ERASE " + QString::number(doc.ids().front()));
    CHECK(doc.size() == 4);
    CHECK(v->selection().empty());

    // 8. Buyruq boshqa tilda: rus tilida aylana
    w.runLine("КРУГ 0,0 3");
    CHECK(doc.size() == 5);

    // 9. Til almashtirish: interfeys va Interpreter birga
    w.setLanguage("ru");
    CHECK(loc.language() == "ru");
    w.setLanguage("tr");
    CHECK(loc.language() == "tr");

    // 10. Rejimlar
    w.setMode(UiMode::Module);
    CHECK(w.mode() == UiMode::Module);
    w.setMode(UiMode::Focus);
    CHECK(w.mode() == UiMode::Focus);
    w.setMode(UiMode::Classic);

    // 11. Nuqta formati: katta UTM koordinatalari aniqligi yo'qolmaydi
    CHECK(Viewport::fmtPoint({4512398.123456, 1234612.5, 0}) == "4512398.123456,1234612.5");

    // 12. Ekran <-> dunyo almashuvi
    const Vec3 wp{12.5, -3.25, 0};
    const Vec3 back = v->toWorld(v->toScreen(wp));
    CHECK(std::abs(back.x - wp.x) < 1e-9 && std::abs(back.y - wp.y) < 1e-9);

    // 13. ORTO: kursor biroz qiya bo'lsa ham chiziq gorizontal
    w.runLine("NEW !");
    v->setSnapModes(0);
    v->setView({0, 0, 0}, 5.0);
    v->setOrtho(true);
    CHECK(v->ortho() && !v->polar());
    w.activateTool("line");
    click(v, {0, 0, 0});
    click(v, {30, 3, 0});
    v->finishTool();
    const LineEntity* ol = lastLine(doc);
    CHECK(ol && std::abs(ol->endPoint().x - 30) < 1e-6 && std::abs(ol->endPoint().y) < 1e-9);

    // 14. QUTB: 44° atrofidagi kursor 45° nurga tortiladi; orto avtomatik o'chadi
    v->setPolar(true);
    CHECK(v->polar() && !v->ortho());
    w.activateTool("line");
    click(v, {-40, -40, 0});
    click(v, {-30, -40 + 10 * std::tan(44.0 * M_PI / 180.0), 0});
    v->finishTool();
    const LineEntity* pl = lastLine(doc);
    CHECK(pl && std::abs((pl->endPoint().x + 40) - (pl->endPoint().y + 40)) < 1e-6);

    // 15. To'g'ridan-to'g'ri masofa: qutb nuri bo'yicha 25 birlik
    w.activateTool("line");
    click(v, {0, -20, 0});
    mouse(v, QEvent::MouseMove, v->toScreen({10, -19.9, 0}), Qt::NoButton);
    CHECK(v->feedDistance(25));
    v->finishTool();
    const LineEntity* dl = lastLine(doc);
    CHECK(dl && std::abs(dl->endPoint().x - 25) < 1e-9 && std::abs(dl->endPoint().y + 20) < 1e-9);

    // 16. Tutqich: chiziq oxirini sudrab ko'chirish -> GRIP buyrug'i, undo bilan qaytadi
    const EntityId lineId = doc.ids().front();
    v->setSelection({lineId});
    const QPointF gp = v->toScreen({30, 0, 0});
    const auto g = v->gripAt(gp);
    CHECK(g && g->first == lineId && g->second == 1);
    mouse(v, QEvent::MouseMove, gp, Qt::NoButton);
    mouse(v, QEvent::MouseButtonPress, gp, Qt::LeftButton);
    CHECK(v->tool() == Tool::Grip);
    mouse(v, QEvent::MouseMove, v->toScreen({30, 20, 0}), Qt::LeftButton);
    mouse(v, QEvent::MouseButtonRelease, v->toScreen({30, 20, 0}), Qt::NoButton);
    CHECK(v->tool() == Tool::Select);
    auto* gl = static_cast<const LineEntity*>(doc.find(lineId));
    CHECK(gl && std::abs(gl->endPoint().y - 20) < 1e-6 && std::abs(gl->endPoint().x - 30) < 1e-6);
    CHECK(v->selection().size() == 1);  // tanlov saqlanadi (ID o'zgarmaydi)
    w.runLine("UNDO");
    gl = static_cast<const LineEntity*>(doc.find(lineId));
    CHECK(gl && std::abs(gl->endPoint().y) < 1e-9);
    // Esc — tutqich bekor
    v->beginGrip(lineId, 0);
    CHECK(v->tool() == Tool::Grip);
    v->cancelTool();
    CHECK(v->tool() == Tool::Select);
    v->setPolar(false);

    // 17. O'lcham asboblari
    const auto before = doc.size();
    w.activateTool("dimlinear");
    CHECK(v->tool() == Tool::DimLinear);
    v->feedPoint({0, 0, 0});
    v->feedPoint({30, 0, 0});
    v->feedPoint({15, -8, 0});
    CHECK(doc.size() == before + 1);
    CHECK(v->tool() == Tool::DimLinear);  // asbob keyingi o'lcham uchun faol qoladi
    w.runLine("CIRCLE 50,50 10");
    w.activateTool("dimradius");
    v->setView({50, 50, 0}, 5.0);
    v->feedPoint({60, 50, 0});  // aylanani tanlash
    v->feedPoint({75, 55, 0});  // joy
    w.activateTool("dimdiameter");
    v->feedPoint({50, 60, 0});
    v->feedPoint({52, 52, 0});
    std::vector<std::string> dims;
    doc.forEach([&](const IEntity& e) {
        if (e.type() == EntityType::Dimension) dims.push_back(static_cast<const DimensionEntity&>(e).text());
    });
    CHECK(dims.size() == 3);
    CHECK(dims.size() == 3 && dims[0] == "30" && dims[1] == "R10" && dims[2] == "\xC3\x98" "20");
    v->cancelTool();

    // 18. Tahrir asboblari: OFFSET, TRIM, EXTEND, FILLET (sichqoncha bilan; nuqta obyekt ustiga tushiriladi)
    w.runLine("NEW !");
    v->setView({0, 0, 0}, 10.0);
    w.runLine("LINE -20,0 20,0");          // 1
    w.runLine("LINE 0,-10 0,10");          // 2
    w.runLine("LINE -20,15 -5,15");        // 3
    w.runLine("LINE 10,5 10,20");          // 4
    w.activateTool("offset");
    v->setOffsetDistance(4);
    click(v, {5, 0.3, 0});                 // obyekt (0.3 — apertura ichida)
    click(v, {5, 8, 0});                   // tomon
    CHECK(doc.size() == 5);
    auto* off = dynamic_cast<const LineEntity*>(doc.find(5));
    CHECK(off && std::abs(off->startPoint().y - 4) < 1e-9);
    v->cancelTool();
    w.activateTool("trim");
    click(v, {15, 0.2, 0});                // 1-chiziqning o'ng qismi (x>0) olib tashlanadi... lekin 4-chiziq ham kesmaydi
    auto* tr = dynamic_cast<const LineEntity*>(doc.find(1));
    CHECK(tr && std::abs(tr->endPoint().x) < 1e-9);
    v->cancelTool();
    w.activateTool("extend");
    click(v, {-6, 15.2, 0});               // 3-chiziq o'ng uchi -> 2-chiziq (x=0)? 2-chiziq y<=10, demak 4-chiziq x=10
    auto* ex = dynamic_cast<const LineEntity*>(doc.find(3));
    CHECK(ex && std::abs(ex->endPoint().x - 10) < 1e-9);
    v->cancelTool();
    w.activateTool("fillet");
    v->setFilletRadius(2);
    click(v, {-10, 15, 0});
    click(v, {10, 8, 0});
    bool hasFilletArc = false;
    doc.forEach([&](const IEntity& e) {
        if (auto* a = dynamic_cast<const ArcEntity*>(&e)) hasFilletArc = std::abs(a->radius() - 2) < 1e-9;
    });
    CHECK(hasFilletArc);
    w.runLine("UNDO");
    hasFilletArc = false;
    doc.forEach([&](const IEntity& e) { if (e.type() == EntityType::Arc) hasFilletArc = true; });
    CHECK(!hasFilletArc);
    v->cancelTool();

    // 19. Kanalizatsiya menyusi: shablon, poliliniyadan quduqlar, jonli tekshiruv, chizish va undo
    w.runLine("NEW !");
    w.runLine("PLINE 0,0,100 40,0,99.8 85,0,99.6 120,10,99.4");
    v->setSelection(doc.ids());
    SewerDialog* sd = w.openSewer();
    CHECK(sd->sidebar()->count() >= 8);
    {   // Excel: eksport -> tashqi tahrir (fayl saqlanadi) -> avtomatik qayta o'qish
        const QString xp = QDir::temp().filePath("asiacad-gui-test.xlsx");
        QFile::remove(xp);
        const auto before = sd->network();
        CHECK(sd->excel()->exportTo(xp, false));
        CHECK(QFile::exists(xp));
        QElapsedTimer tm;
        tm.start();
        while (tm.elapsed() < 1700) { QApplication::processEvents(); QThread::msleep(20); }  // o'z yozuvimiz o'tib ketsin
        auto wb = asiacad::xlsx::load(xp.toStdString());
        wb.sheets[0].rows[1][3] = asiacad::xlsx::Cell::n(before.manholes[0].rim + 1.5);
        asiacad::xlsx::save(wb, xp.toStdString());
        bool reloaded = false;
        QObject::connect(sd->excel(), &asiacad::gui::ExcelLink::reloaded, [&] { reloaded = true; });
        tm.restart();
        while (!reloaded && tm.elapsed() < 5000) { QApplication::processEvents(); QThread::msleep(20); }
        CHECK(reloaded);
        CHECK(std::abs(sd->network().manholes[0].rim - (before.manholes[0].rim + 1.5)) < 1e-6);
        sd->setNetwork(before);
    }
    CHECK(sd != nullptr);
    sd->selectTemplate(1);  // ko'cha tarmog'i PVX SN8 Ø200
    auto clickBtn = [&](const char* key) {
        const QString txt = QString::fromStdString(loc.tr(key));
        for (auto* b : sd->findChildren<QPushButton*>())
            if (b->text() == txt) { b->click(); return true; }
        return false;
    };
    CHECK(clickBtn("gui.sew.frompline"));
    auto net = sd->network();
    CHECK(net.manholes.size() == 4 && std::abs(net.manholes[3].rim - 99.4) < 1e-9);
    CHECK(net.segments.size() == 3 && net.segments[0].specId == "PVC-SN8-200");
    CHECK(sd->segmentsTable()->rowCount() == 3);
    CHECK(sd->result().errors() == 0);
    CHECK(sd->pipeView()->faceCount() > 100);
    const auto docBefore = doc.size();
    CHECK(clickBtn("gui.sew.draw"));
    CHECK(doc.size() > docBefore + 50);
    CHECK(doc.layer("K1_PROFIL").has_value());
    w.runLine("UNDO");
    CHECK(doc.size() == docBefore);
    // normadan chiqish -> xato soni
    net.segments[1].slope = 0.002;
    sd->setNetwork(net);
    CHECK(sd->result().errors() >= 1);
    sd->close();

    // 20. Monolit bino menyusi: qatlamlar avtomatik, 3D, PTO jadvali chizmaga va undo
    w.runLine("NEW !");
    w.runLine("LAYER USTUN");
    w.runLine("CLAYER USTUN");
    w.runLine("PLINE 0,0 0.4,0 0.4,0.4 0,0.4 " + QString::fromStdString(loc.keywordName("CLOSE")));
    w.runLine("COPY LAST 6,0 2");
    w.runLine("LAYER PLITA");
    w.runLine("CLAYER PLITA");
    w.runLine("PLINE -0.3,-0.3 12.7,-0.3 12.7,0.7 -0.3,0.7 " + QString::fromStdString(loc.keywordName("CLOSE")));
    BuildingDialog* bd = w.openBuilding();
    bd->setFloors(4);
    CHECK(bd->model().prisms.size() == 2 + 4 * (3 + 1));
    CHECK(bd->model().totalVolume > 0);
    CHECK(bd->view()->faceCount() > 50);
    const auto nb = doc.size();
    for (auto* b : bd->findChildren<QPushButton*>())
        if (b->text() == QString::fromStdString(loc.tr("gui.bim.totable"))) b->click();
    CHECK(doc.size() > nb + 30);
    CHECK(doc.layer("PTO").has_value());
    w.runLine("UNDO");
    CHECK(doc.size() == nb);
    // yon shablonlar paneli — bino tiplari
    CHECK(bd->sidebar()->count() == 5);
    CHECK(bd->sidebar()->activate(3));  // kottej 2 qavat
    CHECK(bd->params().floors == 2 && bd->params().stripFoundation);
    bd->setFloors(4);

    // 21. Armatura 3D: bino modelidan fundamentdan tomgacha, yon shablonlar, ведомость chizmaga va undo
    RebarDialog* rd = nullptr;
    for (auto* b : bd->findChildren<QPushButton*>())
        if (b->text() == QString::fromStdString(loc.tr("gui.bim.rebar"))) b->click();
    rd = w.findChild<RebarDialog*>();
    CHECK(rd != nullptr);
    if (rd) {
        CHECK(rd->sidebar()->count() >= 12);
        const double m0 = rd->model().totalMass;
        CHECK(m0 > 0);
        CHECK(!rd->model().bars.empty());
        CHECK(rd->view()->faceCount() > 100);
        // ustun shablonini 12Ø25 ga almashtirish — massa oshadi
        int colIdx = -1, n = 0;
        for (const auto& tp : asiacad::bim::builtinRebarTemplates()) {
            if (tp.id == "col_12x25") colIdx = n;
            ++n;
        }
        CHECK(rd->sidebar()->activate(colIdx));
        CHECK(rd->settings().byKind.at(asiacad::bim::ElemKind::Column).nMain == 12);
        CHECK(rd->model().totalMass > m0);
        const auto nr = doc.size();
        for (auto* b : rd->findChildren<QPushButton*>())
            if (b->text() == QString::fromStdString(loc.tr("gui.rb.todwg"))) b->click();
        CHECK(doc.size() > nr + 20);
        CHECK(doc.layer("ARM_VEDOMOST").has_value());
        w.runLine("UNDO");
        CHECK(doc.size() == nr);
        {   // armatura: Excel'da ustun 12 sterjen -> import
            const QString xp = QDir::temp().filePath("asiacad-gui-rebar.xlsx");
            CHECK(rd->excel()->exportTo(xp, false));
            auto wb = asiacad::xlsx::load(xp.toStdString());
            for (auto& sh : wb.sheets)
                for (auto& row : sh.rows)
                    if (row.size() > 18 && row[0].kind == asiacad::xlsx::Cell::Text && row[0].text == "COLUMN") row[18] = asiacad::xlsx::Cell::n(12);
            const QString xp2 = QDir::temp().filePath("asiacad-gui-rebar2.xlsx");
            asiacad::xlsx::save(wb, xp2.toStdString());
            CHECK(rd->excel()->importFrom(xp2));
            CHECK(rd->settings().byKind.at(asiacad::bim::ElemKind::Column).nMain == 12);
        }
        rd->close();
    }
    bd->close();

    // 22. Yer osti tarmoqlari: chizmadan 3D, xatolar, qo'lda tuzatish, chizmaga
    w.runLine(QStringLiteral("OPEN \"%1/yer_osti.acad\" !").arg(ASIACAD_EXAMPLES_DIR));
    {
        UtilityDialog* ud = w.openUtility();
        CHECK(ud->model().lines.size() == 6);
        CHECK(ud->sidebar()->count() >= 20);
        CHECK(ud->view()->faceCount() > 20);
        const int e0 = ud->model().errors();
        CHECK(e0 >= 3);
        // kabelni (4-chiziq) suv quvuri ostidan o'tkazish: ikkala uchini 2,4 m chuqurlikka
        int cable = -1;
        for (std::size_t i = 0; i < ud->model().lines.size(); ++i)
            if (ud->model().lines[i].kind == asiacad::civil::UtilKind::Power) cable = static_cast<int>(i);
        CHECK(cable >= 0);
        if (cable >= 0) {
            const auto& ln = ud->model().lines[static_cast<std::size_t>(cable)];
            ud->setZ(cable, 0, ln.v[0].ground - 2.4);
            ud->setZ(cable, 1, ud->model().lines[static_cast<std::size_t>(cable)].v[1].ground - 2.4);
            CHECK(ud->model().errors() < e0);
            CHECK(ud->model().lines[static_cast<std::size_t>(cable)].v[0].src == asiacad::civil::UtilVertex::Manual);
        }
        const auto n0 = doc.size();
        for (auto* b : ud->findChildren<QPushButton*>())
            if (b->text() == QString::fromStdString(loc.tr("gui.ut.draw"))) b->click();
        CHECK(doc.size() > n0 + 6);
        CHECK(doc.layer("3D_WATER").has_value());
        w.runLine("UNDO");
        CHECK(doc.size() == n0);
        ud->close();
    }

    // 23. Yo'l 3D: chizmadan koridor, toifa shabloni, qo'lda tuzatish, chizmaga
    w.runLine(QStringLiteral("OPEN \"%1/yol.acad\" !").arg(ASIACAD_EXAMPLES_DIR));
    {
        RoadDialog* rd = w.openRoad();
        CHECK(std::abs(rd->model().length - 600) < 1e-6);
        CHECK(rd->sidebar()->count() == 9);
        CHECK(rd->view()->faceCount() > 100);
        const int e0 = rd->model().errors();
        CHECK(e0 >= 2);
        rd->applyCategory("IV");  // IV toifa: 60 ‰ — nishablik xatosi yo'qoladi
        CHECK(rd->params().cat.laneWidth == 3.0);
        CHECK(rd->model().errors() < e0);
        const auto n0 = doc.size();
        for (auto* b : rd->findChildren<QPushButton*>())
            if (b->text() == QString::fromStdString(loc.tr("gui.rd.draw"))) b->click();
        CHECK(doc.size() > n0 + 30);
        CHECK(doc.layer("YOL_3D_OQ").has_value() && doc.layer("YOL_PROFIL").has_value());
        w.runLine("UNDO");
        CHECK(doc.size() == n0);
        rd->close();
    }

    // 23a. Koridor: palitra, yig'ma muharriri, qism qo'shish/olib tashlash, chizmaga
    w.runLine(QStringLiteral("OPEN \"%1/yol.acad\" !").arg(ASIACAD_EXAMPLES_DIR));
    {
        CorridorDialog* cd = w.openCorridor();
        CHECK(cd->palette()->count() == 11);
        cd->applyAssembly(asiacad::civil::builtinAssemblies()[1]);
        CHECK(cd->model().sec.size() > 20);
        CHECK(cd->view()->faceCount() > 100);
        const auto n0 = cd->assembly().right.size();
        const double cut0 = cd->model().cutExact;
        cd->addSub(asiacad::civil::SubKind::Curb, 0);
        CHECK(cd->assembly().right.size() == n0 + 1);
        CHECK(cd->assembly().right[n0 - 1].kind == asiacad::civil::SubKind::Curb);  // qiyalikdan oldin
        CHECK(cd->assembly().right.back().kind == asiacad::civil::SubKind::DaylightDitch);
        cd->removeSub(1, static_cast<int>(n0 - 1));
        CHECK(cd->assembly().right.size() == n0);
        CHECK(std::abs(cd->model().cutExact - cut0) < 1e-6);
        cd->setParam(1, 0, "w", 3.75);
        CHECK(std::abs(cd->assembly().right[0].get("w") - 3.75) < 1e-12);
        cd->showStation(5);
        const auto d0 = doc.size();
        for (auto* b : cd->findChildren<QPushButton*>())
            if (b->text() == QString::fromStdString(loc.tr("gui.cor.draw"))) b->click();
        CHECK(doc.size() > d0 + 20);
        CHECK(doc.layer("KOR_KESIM").has_value() && doc.layer("KOR_DAYLIGHT").has_value());
        w.runLine("UNDO");
        CHECK(doc.size() == d0);
        cd->close();
    }

    // 23b. Suv yo'li: beton kanal chizmadan, lotok shabloni, qo'lda tuzatish, chizmaga (absolyut belgilar)
    w.runLine(QStringLiteral("OPEN \"%1/suv_yoli.acad\" !").arg(ASIACAD_EXAMPLES_DIR));
    {
        WaterwayDialog* wd = w.openWaterway();
        CHECK(wd->model().designPoints == 4);
        CHECK(std::abs(wd->model().st.front().bottom - 413.2) < 1e-9);
        CHECK(wd->sidebar()->count() >= 15);
        CHECK(wd->view()->faceCount() > 100);
        const int e0 = wd->model().errors();
        CHECK(e0 >= 1);  // teskari nishablik
        int ri = -1;
        for (std::size_t i = 0; i < wd->model().st.size(); ++i)
            if (std::abs(wd->model().st[i].bottom - 412.95) < 1e-9) ri = static_cast<int>(i);
        CHECK(ri > 0);
        wd->setBottom(ri, 412.50);
        CHECK(wd->model().errors() < e0);
        wd->applySection(asiacad::civil::builtinWwSections()[9]);
        CHECK(wd->model().pieces > 100);
        const auto n0 = doc.size();
        for (auto* b : wd->findChildren<QPushButton*>())
            if (b->text() == QString::fromStdString(loc.tr("gui.rd.draw"))) b->click();
        CHECK(doc.size() > n0 + 30);
        CHECK(doc.layer("SUV_3D_TUB").has_value() && doc.layer("SUV_PROFIL").has_value());
        w.runLine("UNDO");
        CHECK(doc.size() == n0);
        wd->close();
    }

    // 24. Alohida chizmalar: tarmoqlar (har biri o'z faylida), qavat qolip chizmalari, KJ armatura
    {
        const QString dir = QStringLiteral(ASIACAD_EXAMPLES_DIR);
        w.runLine("NEW !");
        UtilityDialog* ud = w.openUtility();
        auto* ds = ud->drawings();
        ds->addFile(dir + "/tarmoqlar/topo.dxf", "GROUND");
        ds->addFile(dir + "/tarmoqlar/suv.dxf", "WATER");
        ds->addFile(dir + "/tarmoqlar/kanal.dxf", "SEWER");
        ds->addFile(dir + "/tarmoqlar/yomgir.dxf", "STORM");
        CHECK(ud->model().lines.size() == 3);
        CHECK(ud->model().groundPoints == 45);
        bool clash = false;
        for (const auto& i : ud->model().issues) clash |= i.key == "util.chk.clash" || i.key == "util.chk.clear";
        CHECK(clash);
        ud->close();

        BuildingDialog* bdlg = w.openBuilding();
        bdlg->drawings()->addFile(dir + "/bino_qolip/fundament.dxf", "FOUND");
        bdlg->drawings()->addFile(dir + "/bino_qolip/qavat_1_2.dxf", "PLAN", 1, 2);
        bdlg->drawings()->addFile(dir + "/bino_qolip/qavat_3_5.dxf", "PLAN", 3, 5);
        CHECK(bdlg->params().floors == 5);
        CHECK(bdlg->floorPlans().floors.size() == 5);
        bool unsup = false;
        for (const auto& i : bdlg->model().issues) unsup |= i.key == "bim.chk.unsupported";
        CHECK(unsup);
        double fv = 0;
        for (const auto& r : bdlg->model().pto)
            if (r.kind == asiacad::bim::ElemKind::FoundationSlab) fv += r.volume;
        CHECK(std::abs(fv - 20 * 14 * 0.8) < 1e-6);

        RebarDialog* rdk = w.openRebar();
        rdk->drawings()->addFile(dir + "/bino_qolip/kj_plita_1_2.dxf", "SLAB", 1, 2);
        CHECK(rdk->imported().marks == 3);
        CHECK(rdk->model().bars.size() == 2 * (61 + 91 + 13));
        bool cnt = false;
        for (const auto& i : rdk->model().issues) cnt |= i.key == "rbi.chk.count";
        CHECK(cnt);
        CHECK(rdk->imported().specMass > 0);
        CHECK(!rdk->projectMode());
        rdk->setSourceKj(false);
        CHECK(rdk->model().bars.size() > 1000);  // shablon bo'yicha butun bino
        rdk->close();

        // bitta faylda KJ loyiha: rol ko'rsatilmasa ham (SLAB) — o'q pufakchalari bo'yicha avto taniladi
        RebarDialog* rdp = w.openRebar();
        rdp->drawings()->addFile(dir + "/kj_loyiha/kj_loyiha.dxf", "SLAB", 1, 1);
        CHECK(rdp->projectMode());
        CHECK(rdp->project().views.size() == 5);
        const auto nb = rdp->model().bars.size();
        CHECK(nb > 600);
        double conc = 0;
        for (const auto& pr : rdp->project().bm.pto) conc += pr.volume;
        CHECK(conc > 80);
        // fundament qalinligi parametri — beton qayta hisoblanadi
        rdp->kjParams().foundT = 0.6;
        rdp->recomputeNow();
        double fv2 = 0;
        for (const auto& r : rdp->project().bm.pto)
            if (r.kind == asiacad::bim::ElemKind::FoundationSlab) fv2 += r.volume;
        CHECK(std::abs(fv2 - 14 * 8 * 0.6) < 1e-6);
        rdp->setShowConcrete(false);
        rdp->setUpTo(0);
        CHECK(rdp->view()->faceCount() >= 0);
        rdp->close();
        bdlg->close();
    }

    // 25. "0 dan chizish" — har menyuda: kiritish → chizmaga (undo bilan) → modul o'qiydi → tuzatish (tekshiruvdan joyga)
    {
        w.runLine("NEW !");
        // bino: o'qlar to'ri
        auto* gd = qobject_cast<GridInputDialog*>(w.openScratch("building"));
        CHECK(gd != nullptr);
        gd->setSpacing("6x5,4.5", "6x3");
        CHECK(gd->preview()->count() > 50);
        gd->setSpacing("6x", "6x3");
        CHECK(!gd->valid());
        gd->setSpacing("6x5,4.5", "6x3");
        const auto n0 = doc.size();
        gd->accept();
        CHECK(doc.size() > n0 + 50);
        BuildingDialog* bd = w.openBuilding();
        int cols = 0;
        for (const auto& pr : bd->model().prisms) cols += pr.kind == asiacad::bim::ElemKind::Column && pr.level == 1;
        CHECK(cols == 28);
        CHECK(bd->params().floors == 5);
        bd->close();
        w.runLine("UNDO");
        CHECK(doc.size() == n0);

        // yo'l o'qi: burilish nuqtalari va profil
        auto* ad = qobject_cast<AlignInputDialog*>(w.openScratch("road"));
        CHECK(ad != nullptr && ad->pi()->rowCount() == 3);
        ad->accept();
        RoadDialog* rd = w.openRoad();
        CHECK(rd->model().curves.size() == 1);
        CHECK(rd->model().designPoints == 3);
        rd->close();

        // kanal o'qi — suv yo'li moduli taniydigan qatlamda
        w.runLine("NEW !");
        auto* ac = qobject_cast<AlignInputDialog*>(w.openScratch("waterway"));
        ac->accept();
        CHECK(doc.layer("CANAL_O'Q").has_value());
        WaterwayDialog* wwd = w.openWaterway();
        CHECK(wwd->model().length > 500);
        wwd->close();

        // yer osti tarmog'i
        w.runLine("NEW !");
        auto* nd = qobject_cast<NetInputDialog*>(w.openScratch("util"));
        CHECK(nd != nullptr && nd->nodes()->rowCount() == 4);
        nd->chain();
        CHECK(nd->pipes()->rowCount() == 3);
        nd->accept();
        UtilityDialog* ud = w.openUtility();
        CHECK(ud->model().nodes.size() == 4);
        CHECK(ud->model().lines.size() == 3);
        // tuzatish: tekshiruv bandi bosilsa chizmada joy ko'rsatiladi
        w.locate(40, 0);
        ud->close();

        // shahar mavzesi: shablon, 3D, chizmaga
        w.runLine("NEW !");
        CityDialog* cd = w.openCity();
        CHECK(cd->sidebar()->count() == 6);
        cd->sidebar()->activate(2);
        CHECK(cd->result().buildings.size() > 10);
        CHECK(cd->view()->faceCount() > 100);
        cd->sidebar()->activate(5);   // 300 kvartal
        CHECK(cd->result().buildings.size() >= 600);
        QElapsedTimer tm;
        tm.start();
        const auto c0 = doc.size();
        for (auto* b : cd->findChildren<QPushButton*>())
            if (b->text() == QString::fromStdString(loc.tr("gui.gen.draw"))) b->click();
        CHECK(doc.size() > c0 + 3000);
        CHECK(tm.elapsed() < 15000);
        CHECK(doc.layer("KOCHA_O'Q").has_value() && doc.layer("SHAHAR_3D").has_value());
        cd->close();
        // ko'cha o'qi koridor uchun ham o'q (eng uzuni olinadi)
        CorridorDialog* cr = w.openCorridor();
        CHECK(cr->model().length > 1000);
        if (cr->model().length <= 1000) std::printf("  koridor L=%.1f, kesim %zu\n", cr->model().length, cr->model().sec.size());
        cr->close();
    }

    // 26. Haydash (Drive): koridor bo'ylab, feature line tanlab, shahar va bino ichida
    {
        w.runLine(QStringLiteral("OPEN \"%1/yol.acad\" !").arg(ASIACAD_EXAMPLES_DIR));
        CorridorDialog* cd = w.openCorridor();
        cd->applyAssembly(asiacad::civil::builtinAssemblies()[1]);
        w.viewport()->setSelection({});
        DriveDialog* dd = w.openDrive();
        CHECK(std::abs(dd->path().length() - 600) < 1);
        CHECK(dd->scene().tris.size() > 1000);
        CHECK(!dd->sight().issues.empty());             // PK3 dagi o'tkir qavariq sinish
        dd->view()->resize(900, 560);
        dd->view()->renderNow();
        CHECK(dd->view()->drawnTriangles() > 100);
        dd->setStation(100);
        dd->step(1.0);
        CHECK(std::abs(dd->station() - (100 + 60 / 3.6)) < 1e-6);
        dd->checks()->item(0)->setSelected(true);
        emit dd->checks()->itemClicked(dd->checks()->item(0));
        CHECK(std::abs(dd->station() - std::max(0.0, dd->sight().issues.front().s0 - 20)) < 1e-6);
        // feature line tanlansa — kontekst tugmasi va u bo'ylab
        w.viewport()->setSelection({121});
        auto* ctx = w.viewport()->findChild<QToolButton*>();
        CHECK(ctx && !ctx->isHidden());
        dd = w.openDrive();
        CHECK(std::abs(dd->path().length() - 600) < 1);
        dd->close();
        cd->close();

        // shahar: ko'cha o'qi bo'ylab (yer yo'q — tekis maydon), tezlik
        w.runLine("NEW !");
        w.runLine("CITY 4 3 9-16");
        w.viewport()->setSelection({1});
        dd = w.openDrive();
        CHECK(dd->path().length() > 300);
        CHECK(dd->scene().tris.size() > 1000);
        dd->setStation(120);
        dd->view()->resize(1000, 600);
        QElapsedTimer tm;
        tm.start();
        dd->view()->renderNow();
        CHECK(tm.elapsed() < 1500);
        CHECK(dd->view()->drawnTriangles() > 50);
        dd->close();

        // bino ichida piyoda: o'qlar to'ri, bino modeli, 2D yo'l
        w.runLine("NEW !");
        w.runLine("GRID 6x5,4.5 6x3");
        BuildingDialog* bd = w.openBuilding();
        w.runLine("PLINE 3,9 27,9 27,15");
        const auto pid = w.document().ids().back();
        w.viewport()->setSelection({pid});
        dd = w.openDrive();
        CHECK(std::abs(dd->path().length() - 30) < 1e-6);
        CHECK(std::abs(dd->path().zAt(10) - bd->model().prisms.front().z0) < 50);  // qavat sathida
        dd->view()->renderNow();
        CHECK(dd->view()->drawnTriangles() > 50);
        dd->close();
        bd->close();
    }

    // 27. Asosiy lenta (AutoCAD Home kabi): Draw / Modify / Layers asboblari, belgilar palitrasi
    {
        w.runLine("NEW !");
        auto* vp = w.viewport();
        auto countType = [&](EntityType t) {
            int n = 0;
            for (EntityId id : doc.ids()) n += doc.find(id)->type() == t;
            return n;
        };
        w.activateTool("rect");
        vp->feedPoint({0, 0, 0});
        vp->feedPoint({10, 5, 0});
        CHECK(countType(EntityType::Polyline) == 1);
        const EntityId rectId = doc.ids().back();
        CHECK(std::abs(static_cast<const PolylineEntity*>(doc.find(rectId))->area() - 50) < 1e-6);
        w.activateTool("polygon");
        vp->setPolygonSides(5);
        vp->feedPoint({30, 0, 0});
        vp->feedPoint({34, 0, 0});
        CHECK(static_cast<const PolylineEntity*>(doc.find(doc.ids().back()))->vertices().size() == 5);
        w.activateTool("ellipse");
        vp->feedPoint({50, 0, 0});
        vp->feedPoint({56, 0, 0});
        vp->feedPoint({50, 3, 0});
        CHECK(std::abs(static_cast<const PolylineEntity*>(doc.find(doc.ids().back()))->area() - 3.14159265 * 18) < 0.1);
        w.activateTool("spline");
        vp->feedPoint({0, 20, 0});
        vp->feedPoint({10, 25, 0});
        vp->feedPoint({20, 18, 0});
        vp->finishTool();
        CHECK(static_cast<const PolylineEntity*>(doc.find(doc.ids().back()))->vertices().size() > 20);
        // STRETCH: to'rtburchakning o'ng tomoni
        w.activateTool("stretch");
        vp->feedPoint({8, -1, 0});
        vp->feedPoint({12, 6, 0});
        vp->feedPoint({0, 0, 0});
        vp->feedPoint({5, 0, 0});
        CHECK(std::abs(static_cast<const PolylineEntity*>(doc.find(rectId))->area() - 75) < 1e-6);
        // MIRROR (nusxa bilan)
        const auto n0 = doc.size();
        vp->setSelection({rectId});
        w.activateTool("mirror");
        vp->feedPoint({0, -2, 0});
        vp->feedPoint({20, -2, 0});
        CHECK(doc.size() == n0 + 1);
        // SCALE: masofa = koeffitsient
        vp->setSelection({rectId});
        w.activateTool("scale");
        vp->feedPoint({0, 0, 0});
        vp->feedPoint({2, 0, 0});
        CHECK(std::abs(static_cast<const PolylineEntity*>(doc.find(rectId))->area() - 300) < 1e-6);
        // belgilar palitrasi: daraxtni ikki joyga
        CHECK(w.symbolPalette()->list()->count() >= 20);
        CHECK(w.symbolPalette()->choose("TREE"));
        CHECK(vp->tool() == Tool::Symbol);
        vp->feedPoint({100, 100, 0});
        vp->feedPoint({110, 100, 0});
        CHECK(countType(EntityType::Insert) == 2);
        CHECK(doc.block("SYM_TREE") != nullptr);
        vp->cancelTool();
        // qatlam amallari: tanlov bo'yicha muzlatish, keyin hammasini yoqish
        w.runLine("LAYER DAN 3");
        w.runLine(QStringLiteral("CHPROP %1 LAYER=DAN").arg(rectId));
        vp->setSelection({rectId});
        w.activateTool("layfrz");
        CHECK(doc.layer("DAN")->frozen);
        w.activateTool("layon");
        CHECK(!doc.layer("DAN")->frozen);
        // qatlam tanlash asbobi (tanlovsiz): obyektni bosish — joriy qatlam
        vp->setSelection({});
        w.activateTool("laymcur");
        CHECK(vp->tool() == Tool::LayPick);
        vp->feedPoint({15, 0, 0});  // cho'zilgan to'rtburchakning pastki tomoni (x 0..30 masshtabdan keyin)
        // massiv dialogi buyrug'i
        ArrayDialog ad(loc, "5", {0, 0, 0}, kInvalidId);
        CHECK(ad.command() == "ARRAY 5 R 3 4 6 6");
        ad.tabs()->setCurrentIndex(1);
        CHECK(ad.command().startsWith("ARRAY 5 P 0,0 8 360"));
    }

    // 28. PTO: zina va lift; piyoda zina orqali 2-qavatga chiqish
    {
        w.runLine("NEW !");
        w.runLine("GRID 6x5,4.5 6.6x3");
        BuildingDialog* bd = w.openBuilding();
        CHECK(!bd->model().stairs.empty());
        CHECK(!bd->model().glass.empty() && bd->model().doors == 1 && bd->model().masonry > 0);   // fasad: derazalar, eshik, kladka
        CHECK(!bd->findChild<QPushButton*>("sample")->property("accent").toBool());              // bino bor — oddiy tugma
        w.runLine("PLINE 20.3,1.0 20.3,5.5 18.9,5.5 18.9,1.0 18.9,-3");
        w.viewport()->setSelection({w.document().ids().back()});
        DriveDialog* dd = w.openDrive();
        const auto& path = dd->path();
        CHECK(path.length() > 10);
        const double z0 = path.p.front().z, z1 = path.p.back().z;
        CHECK(std::abs((z1 - z0) - 3.3) < 1e-6);   // fundament ustidan (−0.30) 2-qavat poliga (+3.00)
        if (std::abs((z1 - z0) - 3.3) > 1e-6) {
            for (std::size_t i = 0; i < path.p.size(); i += 4) std::printf("  %.2f %.2f %.3f\n", path.p[i].x, path.p[i].y, path.p[i].z);
            for (const auto& st : bd->model().stairs) std::printf(" stair L%d n=%d h=%.3f w=%.2f fl=%.2f\n", st.level, st.risers, st.h, st.width, st.flight);
        }
        double zmid = 0;
        for (const auto& q : path.p)
            if (std::abs(q.y - 5.5) < 0.01 && q.x > 19.5) zmid = q.z;
        CHECK(std::abs((zmid - z0) - 10 * 0.165) < 1e-6);  // oraliq maydoncha
        dd->setStation(path.length() * 0.4);
        dd->view()->renderNow();
        CHECK(dd->view()->drawnTriangles() > 50);
        dd->close();
        bd->close();
    }

    // 29. Metall (KM) paneli: profil, element asbobi, vedomost
    {
        w.runLine("NEW !");
        auto* sp = w.steelPanel();
        CHECK(sp->select("I30") && sp->profile() == "I30");
        CHECK(sp->select("[16") && sp->profile() == "[16");
        sp->select("I20");
        emit sp->drawMember(sp->profile());
        CHECK(w.viewport()->tool() == Tool::SteelBeam);
        w.viewport()->feedPoint({0, 0, 0});
        w.viewport()->feedPoint({6, 0, 0});
        w.viewport()->feedPoint({6, 6, 0});
        w.viewport()->finishTool();
        CHECK(doc.layer("KM_I20").has_value());
        emit sp->drawSection("I20");
        w.viewport()->feedPoint({20, 0, 0});
        w.viewport()->cancelTool();
        const auto n0 = doc.size();
        emit sp->specRequested();
        CHECK(doc.size() > n0 + 10);
        CHECK(doc.layer("KM-SPEC").has_value());
    }

    // 30. Konstruksiya shablonlari: zina / rampa / karkas / tirgak devor; tayanch chizmadan; geo-joylash
    {
        w.runLine("NEW !");
        auto* td = w.openTemplates(TemplatesDialog::Stair);
        CHECK(td->isVisible() && td->page() == TemplatesDialog::Stair && td->valid() && td->previewCount() > 40);
        // tayanch va yo'nalish chizmadan: oyna yashirinadi, 2 nuqtadan keyin qaytadi
        td->findChild<QPushButton*>("pick")->click();
        CHECK(!td->isVisible() && w.viewport()->tool() == Tool::Pick);
        w.viewport()->feedPoint({100, 50, 0});
        w.viewport()->feedPoint({100, 60, 0});
        CHECK(td->isVisible() && w.viewport()->tool() == Tool::Select);
        CHECK(std::abs(td->stairSpec().at.rotDeg - 90) < 1e-9 && td->stairSpec().at.x == 100);
        td->findChild<QDoubleSpinBox*>("z")->setValue(412.5);
        const auto n0 = doc.size();
        td->findChild<QPushButton*>("draw")->click();
        CHECK(doc.size() > n0 + 40 && doc.layer("ZINA_3D").has_value());
        double zmax = -1e9;
        for (EntityId id : doc.ids()) zmax = std::max(zmax, doc.find(id)->bbox().max.z);
        CHECK(std::abs(zmax - 415.8) < 1e-6);
        for (int pg : {0, 2, 3}) {
            td->setPage(pg);
            CHECK(td->valid() && td->previewCount() > 10);
            const auto n1 = doc.size();
            td->findChild<QPushButton*>("draw")->click();
            CHECK(doc.size() > n1);
        }
        CHECK(doc.layer("RAMPA_3D").has_value() && doc.layer("TIRGAK_DEVOR_3D").has_value() && doc.layer("KM_I40").has_value());
        CHECK(td->findChild<QLabel*>("info")->text().contains(QString::fromStdString(loc.tr("tpl.i.fso"))));
        // Esc — tanlash bekor, oyna qaytadi
        td->findChild<QPushButton*>("pick")->click();
        w.viewport()->cancelTool();
        CHECK(td->isVisible());
        td->close();
        // geo-joylash: chiziq lokal 0,0 -> geodezik X/Y + belgi, 2-nuqta bo'yicha burilish
        w.runLine("NEW !");
        w.runLine("LINE 0,0 10,0");
        w.viewport()->setSelection({doc.ids().back()});
        auto* gd = w.openGeoPlace();
        gd->findChild<QPushButton*>("pick1")->click();
        w.viewport()->feedPoint({0, 0, 0});
        CHECK(gd->isVisible());
        gd->setWorld(1, 4612345.12, 512300.5, 412.3);
        gd->setPicked(2, {10, 0, 0});
        gd->setWorld(2, 4612355.12, 512300.5, 0);
        CHECK(gd->valid() && gd->resultText().contains("90"));
        CHECK(gd->command().endsWith(" GEO"));
        gd->findChild<QPushButton*>("apply")->click();
        auto* ln = dynamic_cast<const LineEntity*>(doc.find(doc.ids().back()));
        CHECK(ln && ln->startPoint().equals({512300.5, 4612345.12, 412.3}, 1e-6) && ln->endPoint().equals({512300.5, 4612355.12, 412.3}, 1e-6));
    }

    // 31. Geodezik tahlil: import, reja/fasad, vertikallik, Excel, chizmaga, tozalash
    {
        w.runLine("NEW !");
        const QString dir = QDir::tempPath();
        {
            QFile f(dir + "/asiacad_gui_pts.txt");
            f.open(QIODevice::WriteOnly);
            f.write("1 -0.012 3.000 10.0\n2 0.004 3.000 13.0\n3 2.000 10.008 10.0\n4 2.006 10.004 13.0\n");
        }
        w.runLine("PLINE 0,0 10,0 10,5 0,5 C");
        auto* sd = w.openSurvey(0);
        CHECK(sd->isVisible() && sd->kind() == asiacad::civil::DevKind::Plan);
        w.runLine(sd->importCommand(dir + "/asiacad_gui_pts.txt"));
        sd->refreshLayers();
        sd->setPointsLayer("SYOMKA");
        sd->setRefLayer("0");
        CHECK(sd->run());
        CHECK(sd->result().rows.size() == 4 && sd->result().nOk == 3 && sd->table()->rowCount() == 4);
        CHECK(sd->summary()->text().contains("75.0%"));
        CHECK(sd->drawCommand().startsWith("DEVIATION PLAN "));
        CHECK(sd->exportExcel(dir + "/asiacad_gui_dev.xlsx") && QFile::exists(dir + "/asiacad_gui_dev.xlsx"));
        const auto n0 = doc.size();
        sd->findChild<QPushButton*>("draw")->click();
        CHECK(doc.size() > n0 + 8 && doc.layer("DEV_STRELKA").has_value() && doc.layer("DEV_BP").has_value());
        bool atZ = true;
        for (EntityId id : doc.idsOnLayer("DEV_QIYMAT")) {
            const double z = doc.find(id)->bbox().min.z;
            atZ = atZ && (std::abs(z - 10) < 1e-9 || std::abs(z - 13) < 1e-9);
        }
        CHECK(atZ);
        // vertikallik: 2 juft nuqta — 2 qator
        sd->setKind(asiacad::civil::DevKind::Vertical);
        CHECK(sd->run() && sd->result().rows.size() == 4);   // loyiha yuzi bilan: har nuqta devor yuziga nisbatan
        sd->setRefLayer("");
        CHECK(sd->run() && sd->result().rows.size() == 2);   // loyihasiz: pastki nuqtaga nisbatan
        // tanlovdan: faqat 2 nuqta
        auto sy = doc.idsOnLayer("SYOMKA");
        w.viewport()->setSelection({sy[0], sy[1]});
        sd->takePointsSelection();
        CHECK(sd->run() && sd->result().rows.size() == 1);
        // tozalash
        w.runLine("DEVCLEAR");
        CHECK(doc.idsOnLayer("DEV_STRELKA").empty());
        sd->close();
    }

    // 32. Yer ishlari: yuza, gorizontal, profil, kesimlar, kartogramma, hajm
    {
        w.runLine("NEW !");
        for (int x = 0; x <= 100; x += 10)
            for (int y = 0; y <= 60; y += 10) w.runLine(QStringLiteral("POINT %1,%2,%3").arg(x).arg(y).arg(100 + 0.01 * x));
        w.runLine("PLINE 0,30 100,30");
        const EntityId axis = doc.ids().back();
        w.runLine("PLINE 0,0 100,0 100,60 0,60 C");
        const EntityId bnd = doc.ids().back();
        auto* ed = w.openEarthwork(3);
        CHECK(ed->isVisible() && ed->tab() == EarthworkDialog::Cartogram);
        ed->setGroundLayer("0");
        ed->setDesignMode(1);
        ed->setDesignZ(100.5);
        ed->setPath(axis);
        ed->setBoundary(bnd);
        CHECK(ed->compute() && ed->result()->text().contains("+750.0"));
        ed->setDesignMode(3);
        CHECK(ed->compute() && ed->result()->text().contains("100.500"));
        ed->setDesignMode(1);
        auto n0 = doc.size();
        ed->findChild<QPushButton*>("draw3")->click();
        CHECK(doc.size() > n0 + 50 && doc.layer("KARTOGRAMMA_HAJM").has_value());
        ed->setTab(EarthworkDialog::Profile);
        n0 = doc.size();
        ed->findChild<QPushButton*>("draw1")->click();
        CHECK(doc.size() > n0 + 30 && doc.layer("PROFIL_YER").has_value());
        ed->setTab(EarthworkDialog::Sections);
        ed->setDesignZ(101);
        CHECK(ed->compute() && ed->result()->text().contains("2000.0"));
        n0 = doc.size();
        ed->findChild<QPushButton*>("draw2")->click();
        CHECK(doc.size() > n0 + 30 && doc.layer("RAZREZ_IZ").has_value());
        ed->setTab(EarthworkDialog::Surface);
        n0 = doc.size();
        ed->findChild<QPushButton*>("drawContour")->click();
        CHECK(doc.layer("GORIZONTAL").has_value() || doc.layer("GORIZONTAL_ASOSIY").has_value());
        ed->findChild<QPushButton*>("drawTin")->click();
        CHECK(doc.idsOnLayer("YUZA_TIN").size() >= 100);
        // chizma joyi — chizmadan
        ed->findChild<QPushButton*>("pickAt")->click();
        CHECK(!ed->isVisible() && w.viewport()->tool() == Tool::Pick);
        w.viewport()->feedPoint({0, -200, 0});
        CHECK(ed->isVisible() && ed->command(EarthworkDialog::Profile).contains("AT=0,-200"));
        // dinamik bog'lanish: sukut bo'yicha yoqilgan; o'chirilsa — STATIC
        CHECK(ed->dynamicBox()->isChecked() && !ed->command(EarthworkDialog::Profile).contains("STATIC"));
        CHECK(!doc.links().empty());
        ed->dynamicBox()->setChecked(false);
        CHECK(ed->command(EarthworkDialog::Sections).endsWith(" STATIC") && !ed->command(EarthworkDialog::Volume).contains("STATIC"));
        ed->dynamicBox()->setChecked(true);
        ed->close();
    }


    // 30. AutoCAD uslubi: holat satri, QADAM, OTRACK, bosish-bosish ramkasi, g'ildirak 2× — Extents, kompas, takrorlash
    {
        MainWindow w(loc);
        w.resize(1400, 900);
        w.show();
        QApplication::processEvents();
        Viewport* v = w.viewport();
        Document& doc = w.document();
        for (const char* n : {"grid", "snap", "ortho", "polar", "osnap", "otrack", "dyn"}) CHECK(w.statusButton(n) != nullptr);
        CHECK(w.statusButton("osnap")->isChecked() && v->osnap() && v->otrack() && v->dyn() && !v->gridSnap());
        CHECK(w.statusButton("osnap")->menu() && w.statusButton("osnap")->menu()->actions().size() == 10);
        w.statusButton("osnap")->toggle();
        CHECK(!v->osnap());
        w.statusButton("snap")->toggle();
        CHECK(v->gridSnap());
        v->setView({100, 200, 0}, 20);
        // QADAM: 1 m to'rga tortiladi
        w.activateTool("line");
        click(v, {100.3, 200.4, 0});
        click(v, {105.6, 200.2, 0});
        v->finishTool();
        const LineEntity* gl = lastLine(doc);
        CHECK(gl && std::abs(gl->startPoint().x - 100) < 1e-9 && std::abs(gl->startPoint().y - 200) < 1e-9 && std::abs(gl->endPoint().x - 106) < 1e-9);
        w.statusButton("snap")->toggle();
        // OTRACK: olingan nuqta (110,190) — kursor x≈110 bo'lsa vertikal nurga tushadi
        w.activateTool("line");
        click(v, {100, 205, 0});
        v->addTrackPoint({110, 190, 0});
        CHECK(v->trackPoints().size() == 1);
        click(v, {110.15, 208.37, 0});
        v->finishTool();
        gl = lastLine(doc);
        CHECK(gl && std::abs(gl->endPoint().x - 110) < 1e-9 && std::abs(gl->endPoint().y - 208.37) < 0.06);
        CHECK(v->trackPoints().empty());
        w.statusButton("osnap")->toggle();
        CHECK(v->osnap());
        // takrorlash: bo'sh holatda Enter — oxirgi asbob
        CHECK(v->tool() == Tool::Select && v->lastTool() == Tool::Line);
        QKeyEvent ret(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(v, &ret);
        CHECK(v->tool() == Tool::Line);
        v->cancelTool();
        // bosish-bosish ramkasi (PICKDRAG=0): bo'sh joy — ramka boshlanadi, ikkinchi bosish yakunlaydi
        v->setSelection({});
        click(v, {98, 210, 0});
        CHECK(v->windowPending() && v->selection().empty());
        click(v, {112, 199, 0});
        CHECK(!v->windowPending() && v->selection().size() == 2);
        // kontekst menyu: tanlov bilan — tahrir amallari
        QMenu* cm = v->contextMenu(nullptr);
        CHECK(cm->actions().size() >= 8);
        delete cm;
        v->setSelection({});
        cm = v->contextMenu(nullptr);
        CHECK(cm->actions().size() >= 5);
        delete cm;
        // g'ildirakni ikki marta bosish — ZOOM Extents
        v->setView({5000, 5000, 0}, 0.5);
        const QPointF mid(v->width() / 2.0, v->height() / 2.0);
        QMouseEvent dbl(QEvent::MouseButtonDblClick, mid, v->mapToGlobal(mid), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(v, &dbl);
        const BBox ext = doc.extents(true);
        CHECK(QLineF(v->toScreen({(ext.min.x + ext.max.x) / 2, (ext.min.y + ext.max.y) / 2, 0}), mid).length() < 2);
        // kompas: TOP ikki marta — 3D
        const QPointF tc = v->compassTopRect().center();
        QMouseEvent dbl2(QEvent::MouseButtonDblClick, tc, v->mapToGlobal(tc), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(v, &dbl2);
        CHECK(w.is3D());
        w.show3D(false);
        // izoh masshtabi 1:100 — o'lcham matni 0,25 m
        auto* ab = w.findChild<QComboBox*>("annoScale");
        CHECK(ab && ab->itemText(0) == QString::fromStdString(loc.tr("gui.status.annoauto")));
        v->setAnnoScale(100);
        CHECK(std::abs(v->dimTextHeight() - 0.25) < 1e-12);
        v->setAnnoScale(0);
        v->setCrosshairSize(100);
        CHECK(v->crosshairSize() == 100);
        v->setCrosshairSize(5);
    }

    // 31. Listlar (Revit Sheets): yangi list, ko'rinish, sahifa, PDF, ko'rinishni ikki marta bosish — chizmada
    {
        MainWindow w(loc);
        w.resize(1400, 900);
        w.show();
        QApplication::processEvents();
        w.runLine("PLINE 0,0 80,0 80,50 0,50 C");
        w.runLine("CIRCLE 40,25 10");
        w.viewport()->zoomExtents();
        CHECK(w.document().sheets().empty());
        w.showSheets();
        CHECK(w.isSheets() && w.sheetView()->isVisible());
        CHECK(!w.findChild<QPushButton*>("sheetAddView")->isEnabled());
        CHECK(w.newSheet("L-1", "A3", false) && w.document().sheets().size() == 1);
        CHECK(w.findChild<QPushButton*>("sheetAddView")->isEnabled());
        CHECK(w.addSheetView(MainWindow::SrcAll, 0, "Reja"));
        CHECK(w.isSheets() && w.document().sheets()[0].views.size() == 1 && w.document().sheets()[0].views[0].scale == 500);
        CHECK(w.addSheetView(MainWindow::SrcScreen, 1000, QString()));
        CHECK(w.document().sheets()[0].views.size() == 2);
        CHECK(w.newSheet("L-2", "A4", true) && w.currentSheet() == 1);
        auto* box = w.findChild<QComboBox*>("sheetBox");
        CHECK(box && box->count() == 2);
        box->setCurrentIndex(0);
        CHECK(w.currentSheet() == 0);
        // rasm chiziladi (oq qog'oz + qora chiziqlar)
        QImage img = w.sheetView()->grab().toImage();
        int dark = 0, white = 0;
        for (int y = 0; y < img.height(); y += 3)
            for (int x = 0; x < img.width(); x += 3) {
                const QColor c = img.pixelColor(x, y);
                if (c.lightness() > 245) ++white; else if (c.lightness() < 60) ++dark;
            }
        CHECK(white > 2000 && dark > 50);
        // PDF
        const QString pdf = QDir::temp().filePath("asiacad_sheets_test.pdf");
        QFile::remove(pdf);
        CHECK(w.exportSheetsPdf(pdf, true));
        QFile pf(pdf);
        CHECK(pf.open(QIODevice::ReadOnly) && pf.read(5) == "%PDF-" && pf.size() > 3000);
        pf.close();
        QFile::remove(pdf);
        // ko'rinishni ikki marta bosish — 2D chizmada o'sha joy
        const auto& v0 = w.document().sheets()[0].views[0];
        SheetCanvas* sv = w.sheetView();
        QPointF hit;
        for (int y = 5; y < sv->height() && hit.isNull(); y += 4)
            for (int x = 5; x < sv->width(); x += 4)
                if (sv->viewAt(QPointF(x, y)) == 0) { hit = QPointF(x, y); break; }
        CHECK(!hit.isNull());
        QMouseEvent dbl(QEvent::MouseButtonDblClick, hit, sv->mapToGlobal(hit), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(sv, &dbl);
        CHECK(!w.isSheets() && !w.is3D());
        CHECK(QLineF(w.viewport()->toScreen(v0.center), QPointF(w.viewport()->width() / 2.0, w.viewport()->height() / 2.0)).length() < 2);
        // undo — ro'yxat yangilanadi
        w.runLine("UNDO");
        CHECK(w.document().sheets().size() == 1 && box->count() == 1);
    }

    // 32. Tuzatish paneli: quvur/quduq (aqlli obyekt) va oddiy obyekt xossalari
    {
        MainWindow w(loc);
        w.resize(1400, 900);
        w.show();
        QApplication::processEvents();
        w.runLine(QStringLiteral("SEWER \"%1/k1_kocha.kan.json\"").arg(ASIACAD_EXAMPLES_DIR));
        SmartPanel* sp = w.smartPanel();
        CHECK(sp && w.selectTag("sewer:K1:seg:1"));
        CHECK(sp->title().contains("K1-2") && sp->title().contains("K1-3"));
        QLineEdit* slope = sp->field("SLOPE");
        CHECK(slope && !slope->isReadOnly() && sp->field("INSUP") && sp->field("INVDOWN"));
        slope->setText("15");
        emit slope->editingFinished();
        QApplication::processEvents();
        QApplication::processEvents();
        auto net = asiacad::civil::fromJson(w.document().dictGet("sewer:K1"));
        CHECK(std::abs(net.segments[1].slope - 0.015) < 1e-12);
        CHECK(w.viewport()->selection().size() == 1 && w.document().find(w.viewport()->selection()[0])->xget("seg") == "1");  // o'sha quvur tanlangan qoladi
        CHECK(sp->field("SLOPE") && sp->field("SLOPE")->text() == "15.00");
        QLineEdit* ins = sp->field("INSDOWN");
        ins->setText("6");
        emit ins->editingFinished();
        QApplication::processEvents();
        QApplication::processEvents();
        net = asiacad::civil::fromJson(w.document().dictGet("sewer:K1"));
        CHECK(std::abs(net.segments[1].insDown - 0.06) < 1e-12);
        // navigatsiya: pastki quduq
        auto* down = sp->findChild<QPushButton*>("mhdown");
        CHECK(down);
        down->click();
        QApplication::processEvents();
        CHECK(sp->title().contains("K1-3") && sp->field("RIM") && sp->field("D"));
        QLineEdit* rim = sp->field("RIM");
        rim->setText("245.5");
        emit rim->editingFinished();
        QApplication::processEvents();
        QApplication::processEvents();
        net = asiacad::civil::fromJson(w.document().dictGet("sewer:K1"));
        CHECK(net.manholes[2].rim == 245.5);
        w.runLine("UNDO");
        net = asiacad::civil::fromJson(w.document().dictGet("sewer:K1"));
        CHECK(net.manholes[2].rim != 245.5);
        // oddiy chiziq: uzunlik
        w.runLine("LINE 0,0 10,0");
        const EntityId lid = w.document().ids().back();
        w.viewport()->setSelection({lid});
        QApplication::processEvents();
        QLineEdit* len = sp->field("LENGTH");
        CHECK(len && len->text() == "10.000");
        len->setText("30");
        emit len->editingFinished();
        QApplication::processEvents();
        QApplication::processEvents();
        const auto* ln = static_cast<const LineEntity*>(w.document().find(lid));
        CHECK(ln && std::abs(ln->endPoint().x - 30) < 1e-9);
        CHECK(w.viewport()->selection().size() == 1);
    }
    std::printf("\nGUI: %d tekshiruv, %d xato\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
