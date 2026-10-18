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
#include "drive_dialog.h"
#include "draft_panels.h"

#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <asiacad/io/xlsx.h>

#include <QPushButton>
#include <QListWidget>
#include <QToolButton>
#include <QTabWidget>
#include <QTableWidget>

#include <asiacad/core/dimension.h>
#include <asiacad/core/entities.h>

#include <QMouseEvent>

#include <QApplication>
#include <QComboBox>

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

    std::printf("\nGUI: %d tekshiruv, %d xato\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
