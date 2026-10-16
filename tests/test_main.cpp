// AsiaCAD core testlari (tashqi kutubxonasiz)
#include <asiacad/bim/building.h>
#include <asiacad/bim/rebar.h>
#include <asiacad/civil/sewer.h>
#include <asiacad/core/commands.h>
#include <asiacad/core/dimension.h>
#include <asiacad/core/grips.h>
#include <asiacad/core/modify.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/core/geom2d.h>
#include <asiacad/core/snap.h>
#include <asiacad/i18n/json.h>
#include <asiacad/i18n/localizer.h>
#include <asiacad/i18n/utf8.h>
#include <asiacad/io/acad_file.h>
#include <asiacad/io/dxf_reader.h>
#include <asiacad/io/dxf_text.h>
#include <asiacad/io/dxf_writer.h>

#include "interpreter.h"

#include <algorithm>
#include <random>
#include <set>
#include <chrono>
#include <format>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace asiacad;

static int g_fail = 0, g_checks = 0;
static std::vector<std::pair<const char*, std::function<void()>>>& registry() {
    static std::vector<std::pair<const char*, std::function<void()>>> r;
    return r;
}
struct Reg { Reg(const char* n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #c "\n"; } } while (0)
#define NEAR(a, b) CHECK(std::abs((a) - (b)) < 1e-6)
#define VNEAR(p, q) CHECK((p).equals((q), 1e-6))

// ---------- math ----------
TEST(transform_rotation) {
    auto t = Transform::rotation({10, 10}, deg2rad(90));
    VNEAR(t.apply({20, 10}), Vec3(10, 20));
    NEAR(t.uniformScale(), 1.0);
    NEAR(rad2deg(t.rotationAngle()), 90.0);
}
TEST(transform_mirror) {
    auto t = Transform::mirror({0, 0}, {0, 10});  // Y o'qi
    VNEAR(t.apply({5, 3}), Vec3(-5, 3));
    CHECK(t.isMirroring());
    auto d = Transform::mirror({0, 0}, {1, 1});  // y=x
    VNEAR(d.apply({3, 1}), Vec3(1, 3));
}
TEST(transform_scale) {
    auto t = Transform::scaling({1, 1, 1}, 2);
    VNEAR(t.apply({2, 3, 4}), Vec3(3, 5, 7));
}

// ---------- entities ----------
TEST(line_basics) {
    LineEntity l({0, 0}, {3, 4});
    NEAR(l.length(), 5.0);
    VNEAR(l.pointAtDistance(2.5), Vec3(1.5, 2));
    VNEAR(l.closestPoint({10, 0}), Vec3(3, 4));
    VNEAR(l.closestPoint({0, 5}), Vec3(2.4, 3.2));
}
TEST(arc_three_points_ccw_and_cw) {
    auto a = ArcEntity::fromThreePoints({10, 0}, {0, 10}, {-10, 0});  // yuqori yarim aylana
    VNEAR(a.center(), Vec3(0, 0));
    NEAR(a.radius(), 10.0);
    NEAR(a.length(), kPi * 10);
    VNEAR(a.startPoint(), Vec3(10, 0));
    auto b = a.bbox();
    NEAR(b.max.y, 10.0);
    NEAR(b.min.y, 0.0);

    auto c = ArcEntity::fromThreePoints({-10, 0}, {0, 10}, {10, 0});  // CW kiritilgan — xuddi shu yoy
    NEAR(c.length(), kPi * 10);
    NEAR(c.bbox().max.y, 10.0);
}
TEST(arc_collinear_throws) {
    bool thrown = false;
    try { ArcEntity::fromThreePoints({0, 0}, {1, 1}, {2, 2}); } catch (const std::invalid_argument&) { thrown = true; }
    CHECK(thrown);
}
TEST(arc_mirror_keeps_shape) {
    auto a = ArcEntity::fromThreePoints({10, 0}, {0, 10}, {-10, 0});
    a.transform(Transform::mirror({0, 0}, {1, 0}));  // X o'qi bo'yicha — pastki yarim
    NEAR(a.length(), kPi * 10);
    NEAR(a.bbox().min.y, -10.0);
    NEAR(a.bbox().max.y, 0.0);
}
TEST(arc_closest_point) {
    ArcEntity a({0, 0}, 5, 0, kPi / 2);
    VNEAR(a.closestPoint({10, 10}), Vec3(5 / std::sqrt(2.0), 5 / std::sqrt(2.0)));
    VNEAR(a.closestPoint({0, -10}), Vec3(5, 0));
}
TEST(polyline_area_length) {
    PolylineEntity p({{0, 0}, {10, 0}, {10, 5}, {0, 5}}, true);
    NEAR(p.area(), 50.0);
    NEAR(p.length(), 30.0);
    VNEAR(p.pointAtDistance(12), Vec3(10, 2));
    PolylineEntity q({{0, 0, 0}, {3, 4, 12}});  // 3D: fazoviy 13, gorizontal 5
    NEAR(q.length(), 13.0);
    NEAR(q.length2d(), 5.0);
}
TEST(circle_scale) {
    CircleEntity c({1, 1}, 2);
    c.transform(Transform::scaling({0, 0}, 3));
    VNEAR(c.center(), Vec3(3, 3));
    NEAR(c.radius(), 6.0);
}
TEST(text_mirror_readable) {
    TextEntity t({0, 0}, 2.5, "Toshkent");
    t.transform(Transform::mirror({5, 0}, {5, 10}));
    NEAR(t.rotation(), 0.0);
    VNEAR(t.insertion(), Vec3(10, 0));
}

// ---------- document + commands ----------
TEST(undo_redo_add_delete) {
    Document doc;
    CommandStack st(doc);
    auto& c1 = static_cast<AddEntityCommand&>(st.execute(std::make_unique<AddEntityCommand>(std::make_unique<LineEntity>(Vec3{0, 0}, Vec3{1, 0}))));
    auto& c2 = static_cast<AddEntityCommand&>(st.execute(std::make_unique<AddEntityCommand>(std::make_unique<CircleEntity>(Vec3{0, 0}, 1))));
    const EntityId a = c1.entityId(), b = c2.entityId();
    CHECK(doc.size() == 2);
    st.execute(std::make_unique<DeleteCommand>(std::vector<EntityId>{a}));
    CHECK(doc.size() == 1 && !doc.contains(a));
    st.undo();
    CHECK(doc.size() == 2 && doc.ids().front() == a);  // tartib tiklandi
    st.undo(); st.undo();
    CHECK(doc.size() == 0);
    st.redo(); st.redo();
    CHECK(doc.contains(a) && doc.contains(b));
    CHECK(doc.ids() == (std::vector<EntityId>{a, b}));  // ID'lar saqlanadi
}
TEST(undo_transform) {
    Document doc;
    CommandStack st(doc);
    const EntityId id = doc.add(std::make_unique<LineEntity>(Vec3{0, 0}, Vec3{10, 0}));
    st.execute(std::make_unique<TransformCommand>(std::vector{id}, Transform::rotation({0, 0}, kPi / 2), "ROTATE"));
    VNEAR(static_cast<LineEntity*>(doc.find(id))->endPoint(), Vec3(0, 10));
    st.undo();
    VNEAR(static_cast<LineEntity*>(doc.find(id))->endPoint(), Vec3(10, 0));
    st.redo();
    VNEAR(static_cast<LineEntity*>(doc.find(id))->endPoint(), Vec3(0, 10));
}
TEST(copy_array_undo_redo) {
    Document doc;
    CommandStack st(doc);
    const EntityId id = doc.add(std::make_unique<CircleEntity>(Vec3{0, 0}, 1));
    std::vector<Transform> pl;
    for (int i = 1; i <= 4; ++i) pl.push_back(Transform::translation({i * 5.0, 0}));
    auto& c = static_cast<CopyCommand&>(st.execute(std::make_unique<CopyCommand>(std::vector{id}, pl)));
    const auto created = c.createdIds();
    CHECK(doc.size() == 5);
    st.undo();
    CHECK(doc.size() == 1);
    st.redo();
    CHECK(doc.size() == 5);
    CHECK(doc.ids() == (std::vector<EntityId>{id, created[0], created[1], created[2], created[3]}));
    NEAR(static_cast<CircleEntity*>(doc.find(created[3]))->center().x, 20.0);
}
TEST(locked_layer_blocks_edit) {
    Document doc;
    CommandStack st(doc);
    st.execute(std::make_unique<SetLayerCommand>(Layer{"QURILISH", 1, true, true}));
    auto e = std::make_unique<PointEntity>(Vec3{1, 1});
    e->setLayer("QURILISH");
    const EntityId id = doc.add(std::move(e));
    bool thrown = false;
    try { st.execute(std::make_unique<DeleteCommand>(std::vector{id})); } catch (const std::invalid_argument&) { thrown = true; }
    CHECK(thrown);
    CHECK(doc.contains(id));
    CHECK(st.undoCount() == 1);  // muvaffaqiyatsiz buyruq tarixga tushmaydi
}
TEST(composite_atomic) {
    Document doc;
    CommandStack st(doc);
    auto comp = std::make_unique<CompositeCommand>("TEST");
    comp->add(std::make_unique<AddEntityCommand>(std::make_unique<PointEntity>(Vec3{})));
    comp->add(std::make_unique<DeleteCommand>(std::vector<EntityId>{999}));  // xato
    bool thrown = false;
    try { st.execute(std::move(comp)); } catch (...) { thrown = true; }
    CHECK(thrown);
    CHECK(doc.size() == 0);
    CHECK(!st.canUndo());
}
TEST(layer_command_undo) {
    Document doc;
    CommandStack st(doc);
    st.execute(std::make_unique<SetLayerCommand>(Layer{"YOL", 3}));
    st.execute(std::make_unique<SetLayerCommand>(Layer{"YOL", 5}));
    CHECK(doc.layer("YOL")->color == 5);
    st.undo();
    CHECK(doc.layer("YOL")->color == 3);
    st.undo();
    CHECK(!doc.layer("YOL"));
}
TEST(dirty_tracking) {
    Document doc;
    CommandStack st(doc);
    CHECK(!st.isDirty());
    st.execute(std::make_unique<AddEntityCommand>(std::make_unique<PointEntity>(Vec3{})));
    CHECK(st.isDirty());
    st.markClean();
    CHECK(!st.isDirty());
    st.undo();
    CHECK(st.isDirty());
    st.redo();
    CHECK(!st.isDirty());
    st.undo();
    st.execute(std::make_unique<AddEntityCommand>(std::make_unique<PointEntity>(Vec3{})));
    CHECK(st.isDirty());  // toza holat endi erishib bo'lmaydi
}

// ---------- DXF ----------
TEST(dxf_text_encoding) {
    CHECK(DxfWriter::encodeText("Abc") == "Abc");
    CHECK(DxfWriter::encodeText("Ж") == "\\U+0416");
    CHECK(DxfWriter::encodeText("Oʻzbekiston") == "O\\U+02BBzbekiston");
    CHECK(DxfWriter::encodeText("Ğ") == "\\U+011E");
}
TEST(dxf_export_codepage) {
    auto exportOf = [](std::vector<std::string> texts) {
        Document doc;
        for (auto& t : texts) doc.add(std::make_unique<TextEntity>(Vec3{}, 1, t));
        std::ostringstream os;
        DxfWriter(os).writeDocument(doc);
        return os.str();
    };
    const auto ru = exportOf({"Ташкент", "Қарши"});
    CHECK(ru.find("ANSI_1251") != std::string::npos);
    CHECK(ru.find("\xD2\xE0\xF8\xEA\xE5\xED\xF2") != std::string::npos);  // cp1251 baytlari
    CHECK(ru.find("\\U+049A\xE0\xF0\xF8\xE8") != std::string::npos);      // Қ sig'maydi -> \U+
    CHECK(exportOf({"Çeşme Ğ ı"}).find("ANSI_1254") != std::string::npos);
    CHECK(exportOf({"Oʻzbekiston"}).find("ANSI_1252") != std::string::npos);
    // Qaytib o'qilganda asl UTF-8
    auto r = readDxf(ru);
    CHECK(r.entities.size() == 2 && static_cast<TextEntity&>(*r.entities[1]).text() == "Қарши");
}
TEST(dxf_structure) {
    Document doc;
    doc.addLayer(Layer{"YOL", 3});
    auto l = std::make_unique<LineEntity>(Vec3{4500000.125, 1200000.5}, Vec3{4500100, 1200000.5});
    l->setLayer("YOL");
    doc.add(std::move(l));
    doc.add(std::make_unique<PolylineEntity>(std::vector<Vec3>{{0, 0, 1}, {1, 0, 2}, {1, 1, 3}}, true));
    std::ostringstream os;
    DxfWriter(os).writeDocument(doc);
    const std::string s = os.str();
    CHECK(s.find("AC1009") != std::string::npos);
    CHECK(s.find("4500000.125\n") != std::string::npos);  // katta koordinatalarda aniqlik
    CHECK(s.find("SEQEND") != std::string::npos);
    CHECK(s.find("\n 70\n9\n") != std::string::npos);  // yopiq + 3D
    CHECK(s.rfind("EOF\n") == s.size() - 4);
}

// ---------- i18n: UTF-8 ----------
TEST(utf8_fold) {
    CHECK(utf8::foldKey("line") == "LINE");
    CHECK(utf8::foldKey("çizgi") == utf8::foldKey("ÇİZGİ"));       // turkcha i/İ
    CHECK(utf8::foldKey("ÇIZGI") == utf8::foldKey("ÇİZGİ"));       // nuqtasiz I bilan yozilsa ham
    CHECK(utf8::foldKey("çıkış") == "ÇIKIŞ");
    CHECK(utf8::foldKey("koʻchir") == "KOCHIR");                    // U+02BB
    CHECK(utf8::foldKey("ko'chir") == "KOCHIR");
    CHECK(utf8::foldKey("ko‘chir") == "KOCHIR");                    // U+2018
    CHECK(utf8::foldKey("кўчир") == "КЎЧИР");
    CHECK(utf8::foldKey("қатлам") == "ҚАТЛАМ");
    CHECK(utf8::foldKey("шеңбер") == "ШЕҢБЕР");
    CHECK(utf8::foldKey("мәтін") == "МӘТІН");
    CHECK(utf8::foldKey("бұру") == "БҰРУ");
    CHECK(utf8::foldKey("өчүрүү") == "ӨЧҮРҮҮ");
    CHECK(utf8::foldKey("ҳамма") == "ҲАММА");
    CHECK(utf8::foldKey("ёрдам") == "ЁРДАМ");
    CHECK(utf8::length("Ўзбек") == 5);
}

// ---------- i18n: JSON ----------
TEST(json_parse) {
    auto v = json::parse(R"({"a":[1,2.5,-3e2],"b":"Ж😀\n","c":true,"d":null,"e":{}})");
    CHECK(v.isObject());
    CHECK(v.find("a")->arr().size() == 3);
    NEAR(v.find("a")->arr()[2].num(), -300.0);
    CHECK(v.find("b")->str() == "Ж\xF0\x9F\x98\x80\n");
    CHECK(v.find("c")->boolean());
    CHECK(v.find("d")->isNull());
    CHECK(v.obj()[0].first == "a");  // tartib saqlanadi
    bool thrown = false;
    try { json::parse("{\"a\":\n  [1,}"); } catch (const json::ParseError& e) { thrown = e.line() == 2; }
    CHECK(thrown);
    CHECK(json::parse("\xEF\xBB\xBF{}").isObject());  // BOM
}

// ---------- i18n: Localizer ----------
static i18n::Localizer& loadedLocalizer() {
    static i18n::Localizer loc;
    static bool once = [] {
        std::vector<std::string> errs;
        loc.loadDirectory(ASIACAD_I18N_DIR, &errs);
        for (auto& e : errs) std::cerr << "  i18n: " << e << '\n';
        return true;
    }();
    (void)once;
    return loc;
}
TEST(i18n_all_languages_loaded) {
    auto& loc = loadedLocalizer();
    CHECK(loc.languages() == (std::vector<std::string>{"en", "kk", "ky", "ru", "tr", "uz"}));
}
TEST(i18n_dictionaries_complete) {
    auto& loc = loadedLocalizer();
    for (const auto& code : loc.languages()) {
        for (const auto& m : loc.missingKeys(code)) {
            ++g_fail;
            std::cerr << "  " << code << ": yo'q " << m << '\n';
        }
    }
    // Interpretator ishlatadigan har bir buyruqning sintaksisi va tavsifi bor
    for (const auto& c : loc.canonicalCommands()) {
        CHECK(loc.hasMessage("desc." + c));
        CHECK(std::string(cli::commandSyntax(c)).size() > 0 || c == "EXTENTS" || c == "HELP" || c == "EXIT");
    }
    CHECK(loc.canonicalCommands().size() == 48);
}
TEST(i18n_no_alias_conflicts) {
    for (const auto& c : loadedLocalizer().conflicts()) {
        ++g_fail;
        std::cerr << "  ziddiyat: " << c << '\n';
    }
}
TEST(i18n_resolve_any_language) {
    auto loc = loadedLocalizer();  // nusxa: faol tilni o'zgartiramiz
    loc.setLanguage("uz");
    for (const char* t : {"MOVE", "m", "KOʻCHIR", "kochir", "кўчир", "перенести", "жылжыту", "жылдыруу", "taşı", "TAŞI"}) {
        const bool ok = loc.resolveCommand(t) == "MOVE";
        if (!ok) std::cerr << "  topilmadi: " << t << '\n';
        CHECK(ok);
    }
    CHECK(loc.resolveCommand("sil") == "ERASE");
    CHECK(loc.resolveCommand("КРУГ") == "CIRCLE");
    CHECK(loc.resolveCommand("xyzzy").empty());
    CHECK(loc.resolveKeyword("hamma") == "ALL");
    CHECK(loc.resolveKeyword("всё") == "ALL");
    CHECK(loc.resolveKeyword("tümü") == "ALL");
    // "C": buyruq sifatida CIRCLE, kalit sifatida CLOSE — nomlar fazosi alohida
    CHECK(loc.resolveCommand("c") == "CIRCLE");
    CHECK(loc.resolveKeyword("c") == "CLOSE");
}
TEST(i18n_messages_and_fallback) {
    auto loc = loadedLocalizer();
    loc.setLanguage("ru");
    CHECK(loc.tr("msg.saved", {"a.dxf"}) == "Сохранено: a.dxf");
    CHECK(loc.commandName("LINE") == "ОТРЕЗОК");
    CHECK(loc.tr("no.such.key", {"x"}) == "no.such.key: x");
    loc.setLanguage("en");
    CHECK(loc.commandName("LINE") == "LINE");
    CHECK(i18n::Localizer::format("{1}-{0}-{5}", {"a", "b"}) == "b-a-");
    i18n::Localizer partial;  // en'da bor, faol tilda yo'q xabar en'dan olinadi
    CHECK(partial.loadLanguage(R"({"language":"en","messages":{"x":"X {0}"}})"));
    CHECK(partial.loadLanguage(R"({"language":"zz","messages":{}})"));
    partial.setLanguage("zz");
    CHECK(partial.tr("x", {"1"}) == "X 1");
    std::string err;
    CHECK(!partial.loadLanguage(R"({"name":"bad"})", &err) && !err.empty());
}

// ---------- CLI integratsiya ----------
static std::string runScript(const std::string& lang, const std::vector<std::string>& lines) {
    auto loc = loadedLocalizer();
    loc.setLanguage(lang);
    Document doc;
    CommandStack st(doc);
    std::ostringstream out;
    cli::Interpreter in(doc, st, loc, out);
    for (const auto& l : lines) in.run(l);
    return out.str();
}
TEST(cli_russian_session) {
    const auto s = runScript("ru", {"КРУГ 0,0 5", "отрезок 0,0 10,0 10,10", "ПЕРЕНЕСТИ ВСЕ 5,0", "ОТМЕНИТЬ", "площадь 1"});
    CHECK(s.find("Круг #1") != std::string::npos);
    CHECK(s.find("Создано отрезков: 2 (#2..#3)") != std::string::npos);
    CHECK(s.find("ПЕРЕНЕСТИ: выполнено") != std::string::npos);
    CHECK(s.find("Отменено: ПЕРЕНЕСТИ") != std::string::npos);
    CHECK(s.find("Площадь = 78.5398") != std::string::npos);
}
TEST(cli_mixed_language_input_uz_output) {
    // Interfeys o'zbekcha, foydalanuvchi turkcha/qozoqcha/inglizcha buyruq yozadi
    const auto s = runScript("uz", {"çokluçizgi 0,0 10,0 10,10 0,10 kapat", "ШЕҢБЕР 50,50 2", "area 1", "ERASE 99"});
    CHECK(s.find("Poliliniya #1") != std::string::npos);
    CHECK(s.find("Aylana #2") != std::string::npos);
    CHECK(s.find("Maydon = 100.0000") != std::string::npos);
    CHECK(s.find("Xato: Obyekt topilmadi: #99") != std::string::npos);
}
TEST(cli_errors_localized) {
    const auto kk = runScript("kk", {"шеңбер 0,0", "foo"});
    CHECK(kk.find("Қате: Қолдану: ШЕҢБЕР p r") != std::string::npos);
    CHECK(kk.find("Қате: Белгісіз пәрмен: foo (АНЫҚТАМА)") != std::string::npos);
    const auto tr = runScript("tr", {"YAY 0,0 1,1 2,2", "çember 0,0 -1"});
    CHECK(tr.find("Hata: Üç nokta aynı doğru üzerinde") != std::string::npos);
    CHECK(tr.find("Hata: Yarıçap pozitif olmalı") != std::string::npos);
}
TEST(cli_layer_keywords_localized) {
    const auto s = runScript("ky", {"КАТМАР ЖОЛ 3 КУЛПУЛА", "УЧУРДАГЫ ЖОЛ", "ЧЕКИТ 1,1", "өчүрүү акыркы", "катмар"});
    CHECK(s.find("Учурдагы катмар: ЖОЛ") != std::string::npos);
    CHECK(s.find("Ката: Объект кулпуланган же тоңдурулган катмарда: #1") != std::string::npos);
    CHECK(s.find("[кулпуланган]") != std::string::npos);
}
TEST(cli_lang_switch) {
    const auto s = runScript("uz", {"TIL ru", "КРУГ 0,0 1", "язык TR", "çember 0,0 1", "LANG xx"});
    CHECK(s.find("Язык: Русский (ru)") != std::string::npos);
    CHECK(s.find("Круг #1") != std::string::npos);
    CHECK(s.find("Çember #2") != std::string::npos);
    CHECK(s.find("Hata: Bilinmeyen dil: xx") != std::string::npos);
}

// ---------- DXF import ----------
static std::string dataPath(const char* f) { return std::string(ASIACAD_TEST_DATA) + "/" + f; }

template <class T>
static std::vector<const T*> ofType(const DxfImportResult& r) {
    std::vector<const T*> v;
    for (const auto& e : r.entities)
        if (auto* p = dynamic_cast<const T*>(e.get())) v.push_back(p);
    return v;
}
static const Layer* findLayer(const DxfImportResult& r, const std::string& n) {
    for (const auto& l : r.layers)
        if (l.name == n) return &l;
    return nullptr;
}
static bool hasWarning(const DxfImportResult& r, const std::string& key) {
    for (const auto& w : r.warnings)
        if (w.key == key) return true;
    return false;
}

TEST(dxf_text_codecs) {
    using namespace dxftext;
    CHECK(toUtf8("\xCF\xF0\xE8\xE2\xE5\xF2", Codepage::Cp1251) == "Привет");
    CHECK(toUtf8("\xD0\xDD\xDE\xF0\xFD\xFE", Codepage::Cp1254) == "ĞİŞğış");
    CHECK(toUtf8("\x8F\xE0\xA8\xA2\xA5\xE2", Codepage::Cp866) == "Привет");
    CHECK(decodeUnicodeEscapes("O\\U+02BBzbek \\U+049a") == "Oʻzbek Қ");
    CHECK(decodeTextControlCodes("%%c50 45%%d %%p0.05 100%%%") == "⌀50 45° ±0.05 100%");
    CHECK(decodeTextControlCodes("%%uTagi%%u") == "Tagi");
    auto p = mtextToParagraphs("{\\fArial|b1;Qalin} matn\\PIkkinchi\\~qator\\P\\S1^2; \\\\ \\{x\\}");
    CHECK(p.size() == 3);
    CHECK(p[0] == "Qalin matn");
    CHECK(p[1] == "Ikkinchi qator");
    CHECK(p[2] == "1/2 \\ {x}");
    bool ok = true;
    CHECK(fromDxfName("ansi_1251", &ok) == Codepage::Cp1251 && ok);
    fromDxfName("ANSI_932", &ok);
    CHECK(!ok);
}

TEST(dxf_import_r2018_mix) {
    DxfImportOptions opt;
    opt.explodeInserts = true;  // eski xulq: bloklar geometriya bo'lib keladi
    auto r = readDxfFile(dataPath("r2018_mix.dxf"), opt);
    CHECK(r.release == "R2018");
    CHECK(r.encoding == "UTF-8");
    CHECK(r.insUnits == 6);
    // Qatlamlar jadvali
    const Layer* ch = findLayer(r, "Чегара");
    CHECK(ch && ch->locked && ch->color == 1);
    const Layer* off = findLayer(r, "OFF");
    CHECK(off && !off->visible && off->color == 4);
    // Bulge'li LWPOLYLINE: 10x10 kvadrat + yarim aylana
    bool foundPoly = false;
    for (auto* p : ofType<PolylineEntity>(r))
        if (p->layer() == "Чегара") {
            foundPoly = true;
            NEAR(p->area(), 100 + 12.5 * kPi);
            NEAR(p->length(), 30 + 5 * kPi);
            CHECK(p->hasBulges());
        }
    CHECK(foundPoly);
    // Teskari extrusion bilan ARC
    auto arcs = ofType<ArcEntity>(r);
    CHECK(arcs.size() == 1);
    if (!arcs.empty()) {
        VNEAR(arcs[0]->center(), Vec3(-20, 0));
        NEAR(rad2deg(arcs[0]->startAngle()), 90.0);
        NEAR(rad2deg(arcs[0]->endAngle()), 180.0);
    }
    // TEXT (Middle-Center) va MTEXT (2 qator, Top-Left)
    int texts = 0;
    for (auto* t : ofType<TextEntity>(r)) {
        if (t->text() == "Тошкент") {
            ++texts;
            CHECK(t->hAlign() == TextHAlign::Center && t->vAlign() == TextVAlign::Middle);
            VNEAR(t->insertion(), Vec3(5, 5));
        } else if (t->text() == "Birinchi") {
            ++texts;
            VNEAR(t->insertion(), Vec3(0, 100));
            CHECK(t->vAlign() == TextVAlign::Top);
        } else if (t->text() == "Ikkinchi qalin °") {
            ++texts;
            VNEAR(t->insertion(), Vec3(0, 100 - 2 * 5.0 / 3.0));
        } else if (t->text() == "17") {
            ++texts;  // ATTRIB
            VNEAR(t->insertion(), Vec3(600.5, 0.5));
        }
    }
    CHECK(texts == 4);
    // INSERT: masshtab 2, burilish 90°, rang BYBLOCK -> 5, qatlam 0 -> YOL
    bool tree = false;
    int minsert = 0;
    for (auto* c : ofType<CircleEntity>(r)) {
        if (c->center().equals({200, 0})) {
            tree = true;
            NEAR(c->radius(), 2.0);
            CHECK(c->color() == 5);
            CHECK(c->layer() == "YOL");
        }
        if (c->center().x >= 399 && c->center().x <= 441) ++minsert;
    }
    CHECK(tree);
    CHECK(minsert == 6);  // MINSERT 2x3
    // Ichma-ich blok: FOREST(300,0) -> TREE (0,0) va (10,0)
    int forest = 0;
    for (auto* c : ofType<CircleEntity>(r))
        if (c->center().equals({300, 0}) || c->center().equals({310, 0})) ++forest;
    CHECK(forest == 2);
    // Notekis masshtab (1,3): aylana -> ellipsga yaqin poliliniya, maydon ≈ π·1·3
    bool ell = false;
    for (auto* p : ofType<PolylineEntity>(r))
        if (p->bbox().min.x > 498 && p->bbox().max.x < 502) {
            ell = true;
            CHECK(std::abs(p->area() - 3 * kPi) < 0.05);
        }
    CHECK(ell);
    // ELLIPSE (a=10, b=5) -> 72 uchli poliliniya
    bool ellipse = false;
    for (auto* p : ofType<PolylineEntity>(r))
        if (p->vertices().size() == 72 && p->bbox().min.x > 89 && p->bbox().max.x < 111) {
            ellipse = true;
            CHECK(std::abs(p->area() - 50 * kPi) < 0.3);
        }
    CHECK(ellipse);
    // Hisobot
    CHECK(r.skipped.empty());
    CHECK(r.imported["HATCH"] == 1);
    CHECK(hasWarning(r, "dxf.warn.paperspace"));
    CHECK(hasWarning(r, "dxf.warn.nonuniform"));
    CHECK(hasWarning(r, "dxf.warn.curves"));
    CHECK(r.imported["ARC"] == 1);
}

TEST(dxf_import_r12_cp1251) {
    auto r = readDxfFile(dataPath("r12_cp1251.dxf"));
    CHECK(r.release == "R12");
    CHECK(r.encoding == "ANSI_1251");
    CHECK(findLayer(r, "Слой1") != nullptr);
    auto t = ofType<TextEntity>(r);
    CHECK(t.size() == 1 && t[0]->text() == "Привет, Ташкент Қўғ");
    CHECK(t.size() == 1 && t[0]->layer() == "Слой1");
    bool bulged = false, poly3d = false;
    for (auto* p : ofType<PolylineEntity>(r)) {
        if (p->hasBulges()) { bulged = true; NEAR(p->length(), 10 + 5 * kPi); }
        if (p->is3d()) { poly3d = true; NEAR(p->length(), 13.0); NEAR(p->length2d(), 5.0); }
    }
    CHECK(bulged && poly3d);
}

TEST(dxf_import_headerless_utf8) {
    auto r = readDxfFile(dataPath("headerless_utf8.dxf"));
    CHECK(r.encoding == "UTF-8");
    CHECK(findLayer(r, "Qatlam-ʻ") != nullptr);
    auto t = ofType<TextEntity>(r);
    CHECK(t.size() == 1 && t[0]->text() == "Oʻzbekiston ⌀50");
}

TEST(dxf_roundtrip_own_writer) {
    Document doc;
    doc.addLayer(Layer{"YOʻL", 3, true, true});
    auto add = [&](std::unique_ptr<IEntity> e, const char* layer = "0") { e->setLayer(layer); doc.add(std::move(e)); };
    add(std::make_unique<LineEntity>(Vec3{4500000.125, 1200000.5, 1}, Vec3{4500100, 1200050, 2}), "YOʻL");
    add(std::make_unique<ArcEntity>(Vec3{0, 0}, 5, deg2rad(30), deg2rad(300)));
    add(std::make_unique<CircleEntity>(Vec3{1, 2, 3}, 4));
    add(std::make_unique<PolylineEntity>(std::vector<Vec3>{{0, 0}, {10, 0}, {10, 10}}, std::vector<double>{0, 0.5, 0}, true));
    add(std::make_unique<PolylineEntity>(std::vector<Vec3>{{0, 0, 0}, {1, 1, 5}}));
    add(std::make_unique<TextEntity>(Vec3{7, 8}, 2.5, "Қарши — Ğ", deg2rad(30), TextHAlign::Right, TextVAlign::Top));
    add(std::make_unique<PointEntity>(Vec3{9, 9, 9}));
    std::ostringstream os;
    DxfWriter(os).writeDocument(doc);
    auto r = readDxf(os.str());
    CHECK(r.entities.size() == doc.size());
    const Layer* l = findLayer(r, "YOʻL");
    CHECK(l && l->locked && l->color == 3);
    std::size_t i = 0;
    doc.forEach([&](const IEntity& e) {
        if (i >= r.entities.size()) return;
        const auto& g = *r.entities[i++];
        CHECK(g.type() == e.type());
        CHECK(g.layer() == e.layer());
        const BBox a = e.bbox(), b = g.bbox();
        VNEAR(a.min, b.min);
        VNEAR(a.max, b.max);
        if (auto* p = dynamic_cast<const PolylineEntity*>(&e)) NEAR(p->area(), static_cast<const PolylineEntity&>(g).area());
        if (auto* t = dynamic_cast<const TextEntity*>(&e)) {
            const auto& u = static_cast<const TextEntity&>(g);
            CHECK(u.text() == t->text());
            CHECK(u.hAlign() == t->hAlign() && u.vAlign() == t->vAlign());
            VNEAR(u.insertion(), t->insertion());
        }
    });
}

TEST(dxf_errors) {
    auto code = [](std::string_view s) {
        try { readDxf(s); } catch (const Error& e) { return e.key(); }
        return std::string("ok");
    };
    CHECK(code("AutoCAD Binary DXF\r\n\x1a") == "dxf.err.binary");
    CHECK(code(std::string("AC1032\0\0\0\0", 10)) == "dxf.err.dwg");
    CHECK(code("0\nSECTION\nabc\nENTITIES\n") == "dxf.err.syntax");
    CHECK(code("") == "ok");
    bool open = false;
    try { readDxfFile("/yoq/fayl.dxf"); } catch (const Error& e) { open = e.key() == "dxf.err.open"; }
    CHECK(open);
}

TEST(dxf_import_is_one_undo_step) {
    Document doc;
    CommandStack st(doc);
    auto r = readDxfFile(dataPath("r2018_mix.dxf"));
    const std::size_t n = r.entities.size();
    std::size_t newLayers = 0;
    st.execute(makeImportCommand(doc, r, &newLayers));
    CHECK(doc.size() == n);
    CHECK(newLayers == 4);  // Defpoints, YOL, Чегара, OFF ("0" bor edi)
    CHECK(doc.block("TREE") && doc.block("FOREST") && doc.block("PT"));
    CHECK(doc.layer("Чегара").has_value());
    st.undo();
    CHECK(doc.size() == 0);
    CHECK(!doc.layer("Чегара").has_value());
    CHECK(doc.blocks().empty());
    st.redo();
    CHECK(doc.size() == n);
}

TEST(polyline_bulge_geometry) {
    // Yarim aylana: (0,0)->(10,0), bulge=1 (CCW, pastga bo'rtadi)
    PolylineEntity p({{0, 0}, {10, 0}}, {1.0, 0.0}, false);
    NEAR(p.length(), 5 * kPi);
    VNEAR(p.pointAtDistance(2.5 * kPi), Vec3(5, -5));
    VNEAR(p.closestPoint({5, -20}), Vec3(5, -5));
    NEAR(p.bbox().min.y, -5.0);
    // Ko'zgu: bulge ishorasi o'zgaradi, geometriya ko'zgulanadi
    p.transform(Transform::mirror({0, 0}, {1, 0}));
    VNEAR(p.pointAtDistance(2.5 * kPi), Vec3(5, 5));
    // Notekis masshtab: yoylar bo'linadi
    PolylineEntity q({{0, 0}, {10, 0}}, {1.0, 0.0}, false);
    q.transform(Transform::scaling3(1, 2, 1));
    CHECK(!q.hasBulges());
    NEAR(q.bbox().min.y, -10.0);
    // Aylana = 2 ta bulge=1 segment
    PolylineEntity c({{1, 0}, {-1, 0}}, {1.0, 1.0}, true);
    NEAR(c.area(), kPi);
}

TEST(cli_import_localized) {
    const std::string cmd = "ИМПОРТ " + dataPath("r12_cp1251.dxf");
    const auto s = runScript("ru", {cmd, "СПИСОК ПОСЛЕДНИЙ", "ОТМЕНИТЬ", "импорт nofile.dxf"});
    CHECK(s.find("Импортировано из") != std::string::npos);
    CHECK(s.find("объектов 3") != std::string::npos);
    CHECK(s.find("Всего объектов: 3") != std::string::npos);
    CHECK(s.find("Отменено: ИМПОРТ") != std::string::npos);
    CHECK(s.find("Ошибка: Не удалось открыть файл: nofile.dxf") != std::string::npos);
}

// ---------- Native .acad ----------
static std::string acadText(const Document& d) {
    std::ostringstream os;
    writeAcad(d, os);
    return os.str();
}
static std::filesystem::path tempDir() {
    auto d = std::filesystem::temp_directory_path() / ("asiacad_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(d);
    return d;
}

TEST(acad_roundtrip_exact) {
    Document doc;
    doc.addLayer(Layer{"Yoʻl \"A\"", 3, false, true, false});
    doc.addLayer(Layer{"Чегара", 1, true, false, true});
    auto add = [&](std::unique_ptr<IEntity> e, const char* layer, int color = kColorByLayer) {
        e->setLayer(layer);
        e->setColor(color);
        return doc.add(std::move(e));
    };
    add(std::make_unique<PointEntity>(Vec3{0.1, 1e-17, -0.0}), "0", 1);
    const EntityId gone = add(std::make_unique<LineEntity>(Vec3{0, 0}, Vec3{1, 1}), "0");
    add(std::make_unique<LineEntity>(Vec3{4500000.123456789, 1234567.987654321, 412.3456}, Vec3{kPi, 1.0 / 3.0, 2e300}), "Чегара");
    add(std::make_unique<CircleEntity>(Vec3{1, 2, 3}, std::sqrt(2.0)), "0");
    add(std::make_unique<ArcEntity>(Vec3{0, 0}, 5, deg2rad(30), deg2rad(300)), "0");
    add(std::make_unique<PolylineEntity>(std::vector<Vec3>{{0, 0}, {10, 0}, {10, 10}}, std::vector<double>{0, -0.4142135623730951, 0}, true), "Yoʻl \"A\"");
    add(std::make_unique<PolylineEntity>(std::vector<Vec3>{{0, 0, 1}, {3, 4, 13}}), "0");
    add(std::make_unique<TextEntity>(Vec3{7, 8}, 2.5, "Qator \"1\"\n\\ ⌀50 — Қарши\t✓", deg2rad(30), TextHAlign::Right, TextVAlign::Top), "0", 5);
    doc.take(gone);  // ID'larda bo'shliq: #2 yo'q, nextId saqlanishi kerak
    const std::string a = acadText(doc);
    auto r = readAcad(a);
    CHECK(r.warnings.empty());
    CHECK(r.doc.ids() == doc.ids());
    CHECK(r.doc.peekNextId() == doc.peekNextId());
    CHECK(acadText(r.doc) == a);  // bit-bit bir xil
    auto l = r.doc.layer("Yoʻl \"A\"");
    CHECK(l && !l->visible && l->locked && l->color == 3);
    auto* t = dynamic_cast<const TextEntity*>(r.doc.find(doc.ids().back()));
    CHECK(t && t->text() == "Qator \"1\"\n\\ ⌀50 — Қарши\t✓" && t->hAlign() == TextHAlign::Right && t->color() == 5);
    // Har bir obyekt alohida qatorda
    std::size_t lines = 0;
    for (char c : a) lines += c == '\n';
    CHECK(lines >= doc.size() + doc.layers().size());
}

TEST(acad_save_atomic_backup) {
    namespace fs = std::filesystem;
    const auto dir = tempDir();
    const std::string path = (dir / "chizma.acad").string();
    Document d1;
    d1.add(std::make_unique<PointEntity>(Vec3{1, 1}));
    saveAcad(d1, path);
    Document d2 = loadAcad(path).doc;
    d2.add(std::make_unique<PointEntity>(Vec3{2, 2}));
    saveAcad(d2, path);
    CHECK(loadAcad(path).doc.size() == 2);
    CHECK(fs::exists(path + ".bak"));
    CHECK(loadAcad(path + ".bak").doc.size() == 1);
    CHECK(!fs::exists(path + ".tmp"));
    bool failed = false;
    try { saveAcad(d1, (dir / "yoq_papka" / "x.acad").string()); } catch (const Error& e) { failed = e.key() == "err.write_failed"; }
    CHECK(failed);
    fs::remove_all(dir);
}

TEST(acad_errors_and_compat) {
    auto code = [](std::string_view s) {
        try { readAcad(s); } catch (const Error& e) { return e.key(); }
        return std::string("ok");
    };
    CHECK(code("{bad json") == "acad.err.parse");
    CHECK(code(R"({"format":"dxf","version":1})") == "acad.err.format");
    CHECK(code(R"({"format":"asiacad","version":2})") == "acad.err.version");
    CHECK(code(R"({"format":"asiacad","version":1,"entities":[{"id":1,"type":"CIRCLE","c":[0,0],"r":-1}]})") == "acad.err.entity");
    CHECK(code(R"({"format":"asiacad","version":1,"entities":[{"id":1,"type":"LINE","a":[0,0]}]})") == "acad.err.entity");
    CHECK(code(R"({"format":"asiacad","version":1,"entities":[{"id":1.5,"type":"POINT","p":[0,0]}]})") == "acad.err.entity");
    CHECK(code(R"({"format":"asiacad","version":1,"entities":[{"id":1,"type":"POINT","p":[0,0]},{"id":1,"type":"POINT","p":[1,1]}]})") == "acad.err.duplicate_id");
    // Kelajak versiya obyektlari va qo'shimcha maydonlar — o'tkaziladi
    auto r = readAcad(R"({"format":"asiacad","version":1,"future":{"x":1},"entities":[
        {"id":5,"type":"MLEADER","style":"x"},{"id":7,"type":"POINT","p":[1,2],"layer":"YANGI","extra":true}]})");
    CHECK(r.doc.size() == 1);
    CHECK(r.doc.contains(7));
    CHECK(r.doc.layer("YANGI").has_value());
    CHECK(r.warnings.size() == 2);
    CHECK(r.doc.peekNextId() == 8);
    bool open = false;
    try { loadAcad("/yoq/fayl.acad"); } catch (const Error& e) { open = e.key() == "acad.err.open"; }
    CHECK(open);
}

TEST(cli_open_save_new) {
    namespace fs = std::filesystem;
    const auto dir = tempDir();
    const std::string base = (dir / "uchastka").string();
    const auto s = runScript("uz", {
        "SAQLA",                                 // fayl nomi yo'q
        "AYLANA 0,0 5",
        "SAQLA " + base,                         // .acad qo'shiladi
        "CHIZIQ 0,0 10,0",
        "YANGI",                                 // saqlanmagan o'zgarish -> rad
        "SAQLA",                                 // joriy faylga
        "SAQLA " + base + ".dxf",                // eksport
        "YANGI",
        "ROYXAT",
        "OCH " + base + ".acad",
        "ROYXAT HAMMA",
        "OCH " + dataPath("r12_cp1251.dxf"),
        "SAQLA",                                 // DXF ochilgan — nom yo'q
        "CHIQISH",                               // o'zgarish yo'q — chiqadi (run false)
    });
    CHECK(s.find("Xato: Fayl nomi kerak") != std::string::npos);
    CHECK(s.find("Saqlandi: " + base + ".acad") != std::string::npos);
    CHECK(s.find("Xato: Chizmada saqlanmagan oʻzgarishlar bor") != std::string::npos);
    CHECK(s.find("Eksport qilindi: " + base + ".dxf") != std::string::npos);
    CHECK(s.find("Yangi chizma") != std::string::npos);
    CHECK(s.find("Jami: 0 ta obyekt") != std::string::npos);
    CHECK(s.find(base + ".acad ochildi: 2 ta obyekt, 1 ta qatlam") != std::string::npos);
    CHECK(s.find("Jami: 2 ta obyekt") != std::string::npos);
    CHECK(s.find("r12_cp1251.dxf ochildi: 3 ta obyekt") != std::string::npos);
    CHECK(fs::exists(base + ".acad.bak"));
    // EXIT himoyasi
    const auto e = runScript("en", {"POINT 1,1", "EXIT", "EXIT !"});
    CHECK(e.find("Error: The drawing has unsaved changes. Save it or repeat with \"!\": EXIT !") != std::string::npos);
    fs::remove_all(dir);
}

// ---------- Bloklar va HATCH ----------
template <class T>
static std::vector<const T*> docOfType(const Document& d) {
    std::vector<const T*> v;
    d.forEach([&](const IEntity& e) { if (auto* p = dynamic_cast<const T*>(&e)) v.push_back(p); });
    return v;
}

TEST(dxf_import_blocks_as_inserts) {
    auto r = readDxfFile(dataPath("r2018_mix.dxf"));
    CHECK(r.imported["INSERT"] >= 10);
    CHECK(!hasWarning(r, "dxf.warn.inserts"));
    const InsertEntity* tree = nullptr;
    const InsertEntity* stretched = nullptr;
    const InsertEntity* pt = nullptr;
    int grid = 0;
    for (auto* in : ofType<InsertEntity>(r)) {
        const Vec3 p = in->insertionPoint();
        if (p.equals({200, 0})) tree = in;
        if (p.equals({500, 0})) stretched = in;
        if (in->block()->name == "PT") pt = in;
        if (in->block()->name == "TREE" && p.x >= 399 && p.x <= 441) ++grid;
    }
    CHECK(grid == 6);  // MINSERT 2x3 -> 6 ta INSERT
    CHECK(tree != nullptr);
    if (tree) {
        double sx, sy, rot;
        CHECK(tree->decompose(sx, sy, rot));
        NEAR(sx, 2.0);
        NEAR(sy, 2.0);
        NEAR(rad2deg(rot), 90.0);
        CHECK(tree->layer() == "YOL" && tree->color() == 5);
        // Blok ichida: qatlam "0", rang BYBLOCK saqlanadi
        bool byblock = false;
        for (const auto& e : tree->block()->entities)
            if (e->type() == EntityType::Circle) byblock = e->color() == kColorByBlock && e->layer() == "0";
        CHECK(byblock);
        // Explode: meros qoidalari
        bool circle = false;
        for (const auto& e : tree->explode())
            if (auto* c = dynamic_cast<const CircleEntity*>(e.get())) {
                circle = true;
                VNEAR(c->center(), Vec3(200, 0));
                NEAR(c->radius(), 2.0);
                CHECK(c->color() == 5 && c->layer() == "YOL");
            }
        CHECK(circle);
    }
    CHECK(stretched != nullptr);
    if (stretched) {
        double area = -1;
        for (const auto& e : stretched->explode())
            if (auto* p = dynamic_cast<const PolylineEntity*>(e.get())) area = p->area();
        CHECK(std::abs(area - 3 * kPi) < 0.05);  // notekis masshtab: aylana -> ellips
        NEAR(stretched->bbox().max.y, 6.0);
    }
    CHECK(pt && pt->attributes().size() == 1 && pt->attributes()[0].tag == "NUM" && pt->attributes()[0].text.text() == "17");
    // Ichma-ich: FOREST ichida 2 ta TREE
    auto forest = std::find_if(r.blocks.begin(), r.blocks.end(), [](auto& b) { return b->name == "FOREST"; });
    CHECK(forest != r.blocks.end() && (*forest)->entities.size() == 2);
    // Ichki bloklar avval
    std::size_t iTree = 99, iForest = 99;
    for (std::size_t i = 0; i < r.blocks.size(); ++i) {
        if (r.blocks[i]->name == "TREE") iTree = i;
        if (r.blocks[i]->name == "FOREST") iForest = i;
    }
    CHECK(iTree < iForest);
}

TEST(dxf_import_hatches) {
    auto r = readDxfFile(dataPath("hatch.dxf"));
    auto hs = ofType<HatchEntity>(r);
    CHECK(hs.size() == 5);
    if (hs.size() == 5) {
        NEAR(hs[0]->area(), 84.0);  // teshikli
        CHECK(hs[0]->pattern() == "ANSI31" && !hs[0]->isSolid());
        NEAR(hs[0]->patternScale(), 2.0);
        NEAR(rad2deg(hs[0]->patternAngle()), 45.0);
        CHECK(hs[0]->layer() == "SHTRIX");
        NEAR(hs[1]->area(), 12.5 * kPi);  // CCW yoy qirrasi
        NEAR(hs[2]->area(), 12.5 * kPi);  // CW yoy qirrasi
        CHECK(std::abs(hs[3]->area() - 8 * kPi) < 0.05);  // ellips qirrasi
        NEAR(hs[4]->area(), 12.5 * kPi);  // bulge'li yo'l
        CHECK(hs[1]->isSolid());
    }
}

TEST(hatch_geometry) {
    std::vector<PolylineEntity> loops;
    loops.emplace_back(std::vector<Vec3>{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true);
    loops.emplace_back(std::vector<Vec3>{{2, 2}, {4, 2}, {4, 4}, {2, 4}}, true);
    loops.emplace_back(std::vector<Vec3>{{6, 6}, {8, 6}, {8, 8}, {6, 8}}, true);
    loops.emplace_back(std::vector<Vec3>{{6.5, 6.5}, {7.5, 6.5}, {7.5, 7.5}, {6.5, 7.5}}, true);  // orol teshik ichida
    HatchEntity h(loops, "ANSI31");
    NEAR(h.area(), 100 - 4 - 4 + 1);
    h.transform(Transform::scaling({0, 0}, 2));
    NEAR(h.area(), 4 * 93.0);
    NEAR(h.patternScale(), 2.0);
    h.transform(Transform::rotation({0, 0}, kPi / 2));
    NEAR(rad2deg(h.patternAngle()), 90.0);
    NEAR(h.area(), 4 * 93.0);
}

TEST(insert_transform_and_dxf_roundtrip) {
    auto def = std::make_shared<BlockDef>();
    def->name = "BELGI";
    def->base = {1, 0};
    auto c = std::make_unique<CircleEntity>(Vec3{1, 0}, 1);
    c->setColor(kColorByBlock);
    def->entities.push_back(std::move(c));
    def->entities.push_back(std::make_unique<LineEntity>(Vec3{1, 0}, Vec3{3, 0}));
    Document doc;
    CHECK(doc.addBlock(def));
    CHECK(!doc.addBlock(def));
    CHECK(doc.block("belgi") == def);  // nom katta-kichik harfga sezgir emas
    auto in = std::make_unique<InsertEntity>(def, InsertEntity::placement(def->base, {100, 50}, 2, 2, 1, kPi / 2));
    in->setColor(3);
    in->addAttribute("NUM", TextEntity({100, 52}, 1, "Қ-17"));
    VNEAR(in->insertionPoint(), Vec3(100, 50));
    // Ko'zgu: manfiy masshtab bo'lib ajraladi
    in->transform(Transform::mirror({0, 0}, {0, 1}));
    double sx, sy, rot;
    CHECK(in->decompose(sx, sy, rot));
    NEAR(std::abs(sx), 2.0);
    NEAR(sx * sy, -4.0);
    VNEAR(in->insertionPoint(), Vec3(-100, 50));
    doc.add(std::move(in));
    CHECK(doc.blockUsage("BELGI") == 1);
    std::ostringstream os;
    DxfWriter(os).writeDocument(doc);
    auto r = readDxf(os.str());
    auto ins = ofType<InsertEntity>(r);
    CHECK(ins.size() == 1 && r.blocks.size() == 1);
    if (ins.size() == 1) {
        VNEAR(ins[0]->insertionPoint(), Vec3(-100, 50));
        const BBox a = doc.find(doc.ids()[0])->bbox(), b = ins[0]->bbox();
        VNEAR(a.min, b.min);
        VNEAR(a.max, b.max);
        CHECK(ins[0]->color() == 3);
        CHECK(ins[0]->attributes().size() == 1 && ins[0]->attributes()[0].text.text() == "Қ-17");
    }
    // Qiyshiq (skew) matritsa — DXF'da explode qilib yoziladi
    Document skew;
    skew.addBlock(def);
    Transform sk;
    sk.b = 0.5;  // x' = x + 0.5y
    skew.add(std::make_unique<InsertEntity>(def, sk));
    std::ostringstream os2;
    DxfWriter(os2).writeDocument(skew);
    auto r2 = readDxf(os2.str());
    CHECK(ofType<InsertEntity>(r2).empty() && r2.entities.size() == 2);
}

TEST(acad_roundtrip_blocks_hatch) {
    Document doc;
    CommandStack st(doc);
    auto r = readDxfFile(dataPath("r2018_mix.dxf"));
    st.execute(makeImportCommand(doc, r));
    auto h = readDxfFile(dataPath("hatch.dxf"));
    st.execute(makeImportCommand(doc, h));
    const std::string a = acadText(doc);
    auto back = readAcad(a);
    CHECK(back.warnings.empty());
    CHECK(back.doc.blocks().size() == doc.blocks().size());
    CHECK(acadText(back.doc) == a);
    CHECK(docOfType<HatchEntity>(back.doc).size() == 6);
    // Ichma-ich blok havolasi bir xil ob'ektga ishora qiladi
    const BlockPtr forest = back.doc.block("FOREST");
    CHECK(forest && !forest->entities.empty());
    if (forest && !forest->entities.empty()) {
        auto* inner = dynamic_cast<const InsertEntity*>(forest->entities[0].get());
        CHECK(inner && inner->block() == back.doc.block("TREE"));
    }
}

TEST(import_renames_conflicting_block) {
    Document doc;
    CommandStack st(doc);
    auto r1 = readDxfFile(dataPath("r2018_mix.dxf"));
    st.execute(makeImportCommand(doc, r1));
    auto r2 = readDxfFile(dataPath("r2018_mix.dxf"));
    st.execute(makeImportCommand(doc, r2));
    CHECK(doc.block("TREE") && doc.block("TREE$1"));
    CHECK(doc.blockUsage("TREE") == doc.blockUsage("TREE$1"));
    st.undo();
    CHECK(!doc.block("TREE$1"));
}

TEST(cli_block_insert_explode_hatch) {
    const auto s = runScript("uz", {
        "QATLAM A 1",
        "AYLANA 0,0 1",
        "CHIZIQ -1,0 1,0",
        "BLOK BELGI 0,0 1,2",
        "QOʻY BELGI 10,10 2 90",
        "BLOK",
        "MAYDON 3",
        "PORTLAT 4",
        "ROYXAT OXIRGI",
        "ORQAGA",
        "POLICHIZIQ 20,0 30,0 30,10 20,10 YOPIQ",
        "AYLANA 25,5 2",
        "SHTRIX 7,8 ANSI31 0.5 45",
        "MAYDON OXIRGI",
        "SHTRIX 3",
        "BLOK BELGI 0,0 1",
        "QOʻY YOQ 0,0",
    });
    CHECK(s.find("BELGI bloki 2 ta obyektdan yaratildi va #3 havola bilan almashtirildi") != std::string::npos);
    CHECK(s.find("Blok havolasi #4") != std::string::npos);
    CHECK(s.find("  BELGI: 2 ta obyekt, 2 marta ishlatilgan") != std::string::npos);
    CHECK(s.find("Xato: #3 uchun maydon hisoblanmaydi") != std::string::npos);
    CHECK(s.find("1 ta obyekt 2 ta qismga ajratildi") != std::string::npos);
    CHECK(s.find("CIRCLE c=(10.0000, 10.0000, 0.0000) r=2.0000") != std::string::npos);
    CHECK(s.find("Bekor qilindi: PORTLAT") != std::string::npos);
    CHECK(s.find("Shtrix #9: 2 ta kontur, maydon " + std::format("{:.4f}", 100 - 4 * kPi)) != std::string::npos);
    CHECK(s.find("Xato: #3 yopiq kontur emas") != std::string::npos);
    CHECK(s.find("Maydon = " + std::format("{:.4f}", 100 - 4 * kPi)) != std::string::npos);
    CHECK(s.find("Xato: Blok allaqachon mavjud: BELGI") != std::string::npos);
    CHECK(s.find("Xato: Blok topilmadi: YOQ") != std::string::npos);
}

// ---------- Geometriya, OSNAP, indeks, tanlash ----------
static bool hasPoint(const std::vector<Vec3>& v, const Vec3& p) {
    for (const auto& q : v)
        if (q.equals(p, 1e-6)) return true;
    return false;
}

TEST(geom_intersections) {
    using geom::Prim;
    std::vector<Vec3> h;
    geom::intersect(Prim::seg({0, 0}, {10, 10}), Prim::seg({0, 10}, {10, 0}), h);
    CHECK(h.size() == 1 && hasPoint(h, {5, 5}));
    h.clear();
    geom::intersect(Prim::seg({0, 0}, {4, 4}), Prim::seg({0, 10}, {10, 0}), h);  // kesmalar yetib bormaydi
    CHECK(h.empty());
    h.clear();
    geom::intersect(Prim::seg({-10, 0}, {10, 0}), Prim::arc({0, 0}, 5, 0, 2 * kPi), h);
    CHECK(h.size() == 2 && hasPoint(h, {5, 0}) && hasPoint(h, {-5, 0}));
    h.clear();
    geom::intersect(Prim::seg({-10, 0}, {10, 0}), Prim::arc({0, 0}, 5, 0, kPi / 2), h);  // yoy faqat I chorakda
    CHECK(h.size() == 1 && hasPoint(h, {5, 0}));
    h.clear();
    geom::intersect(Prim::arc({0, 0}, 5, 0, 2 * kPi), Prim::arc({6, 0}, 5, 0, 2 * kPi), h);
    CHECK(h.size() == 2 && hasPoint(h, {3, 4}) && hasPoint(h, {3, -4}));
    h.clear();
    geom::intersect(Prim::seg({-5, 5}, {5, 5}), Prim::arc({0, 0}, 5, 0, 2 * kPi), h);  // urinma
    CHECK(h.size() == 1 && hasPoint(h, {0, 5}));
    std::vector<Vec3> tg;
    geom::tangentPoints(Prim::arc({0, 0}, 5, 0, 2 * kPi), {10, 0}, tg);
    CHECK(tg.size() == 2 && hasPoint(tg, {2.5, 5 * std::sqrt(3.0) / 2}));
    Vec3 foot;
    CHECK(geom::perpendicularFoot(Prim::seg({0, 0}, {10, 0}), {20, 7}, foot));
    VNEAR(foot, Vec3(20, 0));  // cheksiz chiziqqa (AutoCAD PER)
    BBox box;
    box.expand({4, -1});
    box.expand({6, 1});
    CHECK(geom::hitsBox(Prim::seg({0, 0}, {10, 0}), box));
    CHECK(!geom::hitsBox(Prim::seg({0, 5}, {10, 5}), box));
    CHECK(geom::hitsBox(Prim::arc({5, -5}, 5, 0, kPi), box));
    CHECK(!geom::hitsBox(Prim::arc({5, 0}, 20, 0, 2 * kPi), box));  // quti aylana ichida, chegarasiga tegmaydi
}

TEST(osnap_modes) {
    Document doc;
    const EntityId line = doc.add(std::make_unique<LineEntity>(Vec3{0, 0}, Vec3{10, 0}));
    const EntityId circ = doc.add(std::make_unique<CircleEntity>(Vec3{20, 0}, 5));
    doc.add(std::make_unique<LineEntity>(Vec3{5, -5}, Vec3{5, 5}));
    doc.add(std::make_unique<PolylineEntity>(std::vector<Vec3>{{30, 0}, {40, 0}}, std::vector<double>{1.0, 0.0}, false));
    doc.add(std::make_unique<PointEntity>(Vec3{50, 50}));
    auto first = [&](Vec3 c, unsigned m, const Vec3* from = nullptr, double ap = 1.0) {
        auto r = findSnaps(doc, c, ap, m, from);
        return r.empty() ? Vec3{-999, -999} : r.front().point;
    };
    VNEAR(first({9.6, 0.3}, SnapEnd), Vec3(10, 0));
    VNEAR(first({2.4, 0.2}, SnapMid, nullptr, 3), Vec3(5, 0));
    VNEAR(first({24.8, 0.4}, SnapCen), Vec3(20, 0));  // markaz uzoqda, aylana kursor ostida
    VNEAR(first({20.3, 4.8}, SnapQua), Vec3(20, 5));
    VNEAR(first({5.2, 0.3}, SnapInt), Vec3(5, 0));
    VNEAR(first({15.2, 0.3}, SnapInt, nullptr, 1), Vec3(-999, -999));  // chiziq 10 da tugaydi — kesishish yo'q
    VNEAR(first({35, -4.7}, SnapMid), Vec3(35, -5));               // bulge yoy o'rtasi
    VNEAR(first({35, -4.7}, SnapCen), Vec3(35, 0));                // bulge yoy markazi
    VNEAR(first({50.2, 49.9}, SnapNod), Vec3(50, 50));
    VNEAR(first({3.3, 0.4}, SnapNea), Vec3(3.3, 0));
    const Vec3 from{3, 8};
    VNEAR(first({7.5, 0.3}, SnapPer, &from), Vec3(3, 0));
    const Vec3 ext{30, 0};
    const Vec3 t = first({22.5, 4.5}, SnapTan, &ext);
    NEAR((t - Vec3(20, 0)).length2d(), 5.0);
    NEAR((t - ext).dot(t - Vec3(20, 0)), 0.0);  // radius urinmaga perpendikulyar
    // Ustuvorlik: END yaqinroq NEA'dan ham ustun
    auto all = findSnaps(doc, {9.7, 0.1}, 1.0, SnapAll);
    CHECK(!all.empty() && all.front().mode == SnapEnd && all.front().id == line);
    CHECK(all.back().mode == SnapNea);
    // INSERT ichidagi geometriya
    auto def = std::make_shared<BlockDef>();
    def->name = "B";
    def->entities.push_back(std::make_unique<LineEntity>(Vec3{0, 0}, Vec3{2, 0}));
    doc.addBlock(def);
    doc.add(std::make_unique<InsertEntity>(def, InsertEntity::placement({0, 0}, {100, 100}, 3, 3, 1, kPi / 2)));
    VNEAR(first({100.2, 105.8}, SnapEnd), Vec3(100, 106));
    VNEAR(first({100.3, 100.2}, SnapIns), Vec3(100, 100));
    (void)circ;
}

TEST(spatial_index_matches_bruteforce) {
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> U(0, 1000), L(0.1, 30);
    Document doc;
    CommandStack st(doc);
    std::vector<EntityId> ids;
    for (int i = 0; i < 20000; ++i) {
        const Vec3 a{U(rng), U(rng)};
        ids.push_back(doc.add(std::make_unique<LineEntity>(a, a + Vec3{L(rng), L(rng)})));
    }
    auto brute = [&](const BBox& b) {
        std::vector<EntityId> v;
        doc.forEach([&](const IEntity& e) { if (e.bbox().intersects2d(b)) v.push_back(e.id()); });
        std::sort(v.begin(), v.end());
        return v;
    };
    auto fast = [&](const BBox& b) {
        auto v = doc.query(b);
        std::sort(v.begin(), v.end());
        return v;
    };
    auto randBox = [&] {
        BBox b;
        const Vec3 p{U(rng), U(rng)};
        b.expand(p);
        b.expand(p + Vec3{L(rng) * 3, L(rng) * 3});
        return b;
    };
    bool ok = true;
    for (int q = 0; q < 200; ++q) {
        const BBox b = randBox();
        ok = ok && fast(b) == brute(b);
    }
    CHECK(ok);
    // Inkremental o'zgarishlar: ko'chirish, o'chirish, qo'shish, undo
    st.execute(std::make_unique<TransformCommand>(std::vector<EntityId>(ids.begin(), ids.begin() + 300),
                                                  Transform::translation({500, -300}), "MOVE"));
    st.execute(std::make_unique<DeleteCommand>(std::vector<EntityId>(ids.begin() + 300, ids.begin() + 500)));
    for (int i = 0; i < 100; ++i) st.execute(std::make_unique<AddEntityCommand>(std::make_unique<CircleEntity>(Vec3{U(rng), U(rng)}, 5)));
    st.undo();
    ok = true;
    for (int q = 0; q < 200; ++q) {
        const BBox b = randBox();
        ok = ok && fast(b) == brute(b);
    }
    CHECK(ok);
    // Ko'p o'zgarish -> to'liq qayta qurish yo'li
    st.execute(std::make_unique<TransformCommand>(doc.ids(), Transform::rotation({500, 500}, 0.3), "ROTATE"));
    ok = true;
    for (int q = 0; q < 100; ++q) {
        const BBox b = randBox();
        ok = ok && fast(b) == brute(b);
    }
    CHECK(ok);
    // O'chiq qatlam so'rovdan chiqadi
    doc.addLayer(Layer{"YASHIRIN", 1, false});
    auto e = std::make_unique<PointEntity>(Vec3{-50, -50});
    e->setLayer("YASHIRIN");
    doc.add(std::move(e));
    BBox far;
    far.expand({-51, -51});
    far.expand({-49, -49});
    CHECK(doc.query(far).empty() && doc.query(far, false).size() == 1);
}

TEST(spatial_index_performance) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> U(0, 100000);
    Document doc;
    for (int i = 0; i < 120000; ++i) {
        const Vec3 a{U(rng) + 4.5e6, U(rng)};
        doc.add(std::make_unique<LineEntity>(a, a + Vec3{5, 5}));
    }
    const auto t0 = std::chrono::steady_clock::now();
    BBox b;
    b.expand({4.5e6 + 50000, 50000});
    b.expand({4.5e6 + 52000, 52000});
    std::size_t n = doc.query(b).size();  // birinchi so'rov daraxtni quradi
    const auto t1 = std::chrono::steady_clock::now();
    for (int k = 0; k < 1000; ++k) n += doc.query(b).size();
    const auto t2 = std::chrono::steady_clock::now();
    const double build = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double perQuery = std::chrono::duration<double, std::micro>(t2 - t1).count() / 1000;
    std::cout << "    120k: qurish " << build << " ms, so'rov " << perQuery << " µs\n";
    CHECK(build < 2000);
    CHECK(perQuery < 500);
    CHECK(n > 0);
}

TEST(select_window_crossing_pick) {
    Document doc;
    const EntityId inside = doc.add(std::make_unique<LineEntity>(Vec3{1, 1}, Vec3{2, 2}));
    const EntityId diag = doc.add(std::make_unique<LineEntity>(Vec3{-5, 9}, Vec3{9, -5}));   // qutini kesadi
    const EntityId corner = doc.add(std::make_unique<LineEntity>(Vec3{9, 13}, Vec3{13, 9}));  // bbox kesadi, chiziq emas
    const EntityId circ = doc.add(std::make_unique<CircleEntity>(Vec3{5, 5}, 20));             // quti aylana ichida
    const EntityId pt = doc.add(std::make_unique<PointEntity>(Vec3{3, 3}));
    std::vector<PolylineEntity> loops;
    loops.emplace_back(std::vector<Vec3>{{-100, -100}, {100, -100}, {100, 100}, {-100, 100}}, true);
    const EntityId hatch = doc.add(std::make_unique<HatchEntity>(loops, "SOLID"));
    BBox box;
    box.expand({0, 0});
    box.expand({10, 10});
    CHECK(selectWindow(doc, box, false) == (std::vector<EntityId>{inside, pt}));
    CHECK(selectWindow(doc, box, true) == (std::vector<EntityId>{inside, diag, pt, hatch}));
    (void)corner;
    (void)circ;
    CHECK(pickEntity(doc, {1.55, 1.45}, 0.2) == inside);
    CHECK(pickEntity(doc, {25, 5.1}, 0.2) == circ);
    CHECK(pickEntity(doc, {50, 50}, 0.2) == kInvalidId);  // shtrix ichi — chegaradan uzoq
}

TEST(cli_snap_and_select) {
    const auto s = runScript("uz", {
        "CHIZIQ 0,0 10,0",
        "AYLANA 20,0 5",
        "CHIZIQ OXIR:9.8,0.3 MARKAZ:24,1",   // (10,0) -> (20,0)
        "ROYXAT OXIRGI",
        "CHIZIQ 5,8 PERP:3,0.2",              // perpendikulyar asos (5,0)
        "ROYXAT OXIRGI",
        "BOGʻLA 19.5,4.6 2",
        "TANLOV OYNA:-1,-1,11,1",
        "TANLOV KESIB:4,-1,6,1",
        "TANLOV TANLA:20,5.1",
        "KOʻCHIR OXIRGI 0,10",
        "CHIZIQ OXIR:100,100 0,0",
        "TANLOV XATO:1,2",
    });
    CHECK(s.find("LINE (10.0000, 0.0000, 0.0000) -> (20.0000, 0.0000, 0.0000)") != std::string::npos);
    CHECK(s.find("LINE (5.0000, 8.0000, 0.0000) -> (5.0000, 0.0000, 0.0000)") != std::string::npos);
    CHECK(s.find("  CHORAK  (20.0000, 5.0000, 0.0000)  #2") != std::string::npos);
    CHECK(s.find("1 ta obyekt tanlandi: 1") != std::string::npos);
    CHECK(s.find("2 ta obyekt tanlandi: 1,4") != std::string::npos);
    CHECK(s.find("1 ta obyekt tanlandi: 2") != std::string::npos);
    CHECK(s.find("KOʻCHIR bajarildi") != std::string::npos);
    CHECK(s.find("Xato: OXIR: (100,100) atrofida 2.0000 radiusda nuqta topilmadi") != std::string::npos);
    CHECK(s.find("Xato: Notoʻgʻri tanlov: XATO:1,2") != std::string::npos);
}

// ---------- Tutqichlar (grips) ----------
TEST(grips_points_per_type) {
    CHECK(gripPoints(LineEntity({0, 0, 0}, {10, 0, 0})).size() == 3);
    CHECK(gripPoints(CircleEntity({0, 0, 0}, 5)).size() == 5);
    CHECK(gripPoints(ArcEntity::fromThreePoints({5, 0, 0}, {0, 5, 0}, {-5, 0, 0})).size() == 4);
    CHECK(gripPoints(PolylineEntity({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}})).size() == 3);
    CHECK(gripPoints(DimensionEntity(DimKind::Aligned, {0, 0, 0}, {10, 0, 0}, {5, 3, 0}, 2.5)).size() == 3);
}
TEST(grips_edit_geometry) {
    LineEntity l({0, 0, 0}, {10, 0, 0});
    l.setLayer("A");
    l.setColor(3);
    auto e = withGripMoved(l, 1, {10, 5, 0});
    auto* m = dynamic_cast<LineEntity*>(e.get());
    CHECK(m && m->endPoint().equals({10, 5, 0}) && m->startPoint().equals({0, 0, 0}));
    CHECK(e->layer() == "A" && e->color() == 3);
    auto mid = withGripMoved(l, 2, {5, 2, 0});  // o'rtasi — butun chiziq siljiydi
    VNEAR(static_cast<LineEntity&>(*mid).startPoint(), Vec3(0, 2, 0));
    CircleEntity c({0, 0, 0}, 5);
    auto c2 = withGripMoved(c, 2, {0, 8, 0});
    NEAR(static_cast<CircleEntity&>(*c2).radius(), 8.0);
    auto c3 = withGripMoved(c, 0, {3, 4, 0});
    VNEAR(static_cast<CircleEntity&>(*c3).center(), Vec3(3, 4, 0));
    const ArcEntity a = ArcEntity::fromThreePoints({5, 0, 0}, {0, 5, 0}, {-5, 0, 0});
    auto a2 = withGripMoved(a, 3, {0, -5, 0});
    NEAR(static_cast<ArcEntity&>(*a2).sweep(), 1.5 * kPi);
    bool bad = false, badIdx = false;
    try { withGripMoved(c, 1, {0, 0, 0}); } catch (const Error& ex) { bad = ex.key() == "core.grip_invalid"; }
    try { withGripMoved(c, 9, {0, 0, 0}); } catch (const Error& ex) { badIdx = ex.key() == "core.grip_index"; }
    CHECK(bad && badIdx);
    PolylineEntity pl({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}}, {0.0, 0.5, 0.0}, true);
    auto pl2 = withGripMoved(pl, 1, {12, 0, 0});
    auto& q = static_cast<PolylineEntity&>(*pl2);
    VNEAR(q.vertices()[1], Vec3(12, 0, 0));
    NEAR(q.bulges()[1], 0.5);
    CHECK(q.isClosed());
}
TEST(grips_replace_command_undo_keeps_id_and_order) {
    Document doc;
    CommandStack st(doc);
    const EntityId a = doc.add(std::make_unique<LineEntity>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    const EntityId b = doc.add(std::make_unique<CircleEntity>(Vec3{0, 0, 0}, 1));
    st.execute(std::make_unique<ReplaceEntityCommand>(a, withGripMoved(*doc.find(a), 0, {-5, 0, 0})));
    CHECK(doc.ids() == std::vector<EntityId>({a, b}));
    NEAR(static_cast<const LineEntity*>(doc.find(a))->length(), 15.0);
    CHECK(doc.query(BBox{{-6, -1, 0}, {-4, 1, 0}}).size() == 1);  // indeks yangilangan
    st.undo();
    NEAR(static_cast<const LineEntity*>(doc.find(a))->length(), 10.0);
    st.redo();
    NEAR(static_cast<const LineEntity*>(doc.find(a))->length(), 15.0);
}

// ---------- O'lchamlar ----------
TEST(dimension_linear_orientation_and_text) {
    auto h = DimensionEntity::linear({0, 0, 0}, {12.5, 4, 0}, {6, -5, 0}, 2.5);
    NEAR(h.rotation(), 0.0);
    NEAR(h.measurement(), 12.5);
    CHECK(h.text() == "12,50");
    auto v = DimensionEntity::linear({0, 0, 0}, {12.5, 4, 0}, {20, 2, 0}, 2.5);
    NEAR(v.rotation(), kPi / 2);
    CHECK(v.text() == "4");
    DimensionEntity al(DimKind::Aligned, {0, 0, 0}, {3, 4, 0}, {0, 5, 0}, 2.5, 0);
    CHECK(al.text() == "5");
    DimensionEntity r(DimKind::Radius, {0, 0, 0}, {25, 0, 0}, {40, 0, 0}, 2.5);
    CHECK(r.text() == "R25");
    DimensionEntity d(DimKind::Diameter, {-25, 0, 0}, {25, 0, 0}, {10, 0, 0}, 2.5);
    CHECK(d.text() == "\xC3\x98" "50");
}
TEST(dimension_explode_gost) {
    DimensionEntity d(DimKind::Rotated, {0, 0, 0}, {100, 0, 0}, {50, 20, 0}, 5, 0, 0);
    d.setLayer("OLCHAM");
    const auto parts = d.explode();
    int lines = 0, texts = 0;
    for (const auto& p : parts) {
        CHECK(p->layer() == "OLCHAM");
        if (auto* l = dynamic_cast<const LineEntity*>(p.get())) {
            ++lines;
            if (std::abs(l->startPoint().y - 20) < 1e-9 && std::abs(l->endPoint().y - 20) < 1e-9) {  // o'lcham chizig'i
                NEAR(l->startPoint().x, -3.0);
                NEAR(l->endPoint().x, 103.0);
            }
        }
        if (auto* t = dynamic_cast<const TextEntity*>(p.get())) {
            ++texts;
            CHECK(t->text() == "100");
            CHECK(t->insertion().y > 20);
        }
    }
    CHECK(lines == 5);  // 2 chiqarish + o'lcham + 2 ta 45° serif
    CHECK(texts == 1);
    DimensionEntity r(DimKind::Radius, {0, 0, 0}, {10, 0, 0}, {5, 0, 0}, 2);
    int hatches = 0;
    for (const auto& p : r.explode()) hatches += p->type() == EntityType::Hatch;
    CHECK(hatches == 1);  // strelka
}
TEST(dimension_transform_and_roundtrip) {
    Document doc;
    CommandStack st(doc);
    auto d = std::make_unique<DimensionEntity>(DimensionEntity::linear({0, 0, 0}, {10, 0, 0}, {5, 4, 0}, 2.5));
    const EntityId id = doc.add(std::move(d));
    st.execute(std::make_unique<TransformCommand>(std::vector<EntityId>{id}, Transform::rotation({0, 0, 0}, kPi / 2), "ROTATE"));
    auto* r = static_cast<const DimensionEntity*>(doc.find(id));
    NEAR(r->measurement(), 10.0);
    NEAR(r->rotation(), kPi / 2);
    st.execute(std::make_unique<TransformCommand>(std::vector<EntityId>{id}, Transform::scaling({0, 0, 0}, 2), "SCALE"));
    NEAR(static_cast<const DimensionEntity*>(doc.find(id))->textHeight(), 5.0);
    NEAR(static_cast<const DimensionEntity*>(doc.find(id))->measurement(), 20.0);
    const std::string a = acadText(doc);
    auto back = readAcad(a);
    CHECK(back.warnings.empty());
    CHECK(acadText(back.doc) == a);
    CHECK(docOfType<DimensionEntity>(back.doc).size() == 1);
}
TEST(cli_grip_and_dimensions) {
    const auto s = runScript("uz", {"CHIZIQ 0,0 100,0", "AYLANA 50,50 10", "TUTQICH 1 1 120,0", "ROʻYXAT 1",
                                    "OʻLCHAM 0,0 120,0 60,-10", "OʻLCHAMPAR 0,0 30,40 0,10 3.5", "OʻLCHAMRAD 2 70,50",
                                    "OʻLCHAMDIA 2 50,70 2.5 1", "OʻLCHAMRAD 1 0,0", "TUTQICH 2 7 0,0", "ORQAGA 5", "ROʻYXAT 1"});
    CHECK(s.find("Obyekt #1: 1-tutqich koʻchirildi") != std::string::npos);
    CHECK(s.find("L=120.0000") != std::string::npos);
    CHECK(s.find("Oʻlcham #3: 120") != std::string::npos);
    CHECK(s.find("Oʻlcham #4: 50") != std::string::npos);
    CHECK(s.find("Oʻlcham #5: R10") != std::string::npos);
    CHECK(s.find("Oʻlcham #6: \xC3\x98" "20,0") != std::string::npos);
    CHECK(s.find("Xato: Aylana yoki yoy tanlang") != std::string::npos);
    CHECK(s.find("Xato: Tutqich raqami notoʻgʻri: 7 (obyektda 5 ta)") != std::string::npos);
    CHECK(s.find("L=100.0000") != std::string::npos);  // undo tutqichni ham qaytardi
    const auto ru = runScript("ru", {"РЗМЛИНЕЙНЫЙ 0,0 0,0 5,5"});
    CHECK(ru.find("Ошибка: Точки размера совпадают") != std::string::npos);
}

// ---------- OFFSET / TRIM / EXTEND / FILLET ----------
TEST(offset_line_circle_arc) {
    auto l = offsetEntity(LineEntity({0, 0, 0}, {10, 0, 0}), 2, {5, -7, 0});
    auto& ol = static_cast<LineEntity&>(*l);
    VNEAR(ol.startPoint(), Vec3(0, -2, 0));
    VNEAR(ol.endPoint(), Vec3(10, -2, 0));
    NEAR(static_cast<CircleEntity&>(*offsetEntity(CircleEntity({0, 0, 0}, 5), 2, {1, 0, 0})).radius(), 3.0);
    NEAR(static_cast<CircleEntity&>(*offsetEntity(CircleEntity({0, 0, 0}, 5), 2, {9, 0, 0})).radius(), 7.0);
    bool thrown = false;
    try { offsetEntity(CircleEntity({0, 0, 0}, 5), 6, {1, 0, 0}); } catch (const Error& e) { thrown = e.key() == "err.offset_radius"; }
    CHECK(thrown);
    const ArcEntity a = ArcEntity::fromThreePoints({5, 0, 0}, {0, 5, 0}, {-5, 0, 0});
    auto oa = offsetEntity(a, 1, {0, 10, 0});
    NEAR(static_cast<ArcEntity&>(*oa).radius(), 6.0);
    NEAR(static_cast<ArcEntity&>(*oa).sweep(), kPi);
}
TEST(offset_polyline_miter_and_bulge) {
    // Kvadrat ichkariga 1 ga: 8x8, maydon 64
    PolylineEntity sq({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}, true);
    auto in = offsetEntity(sq, 1, {5, 5, 0});
    auto& p = static_cast<PolylineEntity&>(*in);
    CHECK(p.isClosed() && p.vertices().size() == 4);
    NEAR(p.area(), 64.0);
    auto out = offsetEntity(sq, 1, {20, 5, 0});
    NEAR(static_cast<PolylineEntity&>(*out).area(), 144.0);
    // Ochiq L shakl, CW tartibda ham to'g'ri tomon
    PolylineEntity L({{0, 10, 0}, {0, 0, 0}, {10, 0, 0}});
    auto ol = offsetEntity(L, 2, {5, 5, 0});
    auto& q = static_cast<PolylineEntity&>(*ol);
    CHECK(q.vertices().size() == 3);
    VNEAR(q.vertices()[1], Vec3(2, 2, 0));
    VNEAR(q.vertices()[0], Vec3(2, 10, 0));
    // Yarim doira uchli "stadion" (to'g'ri + yoy urinma): ichkariga — uchlar soni saqlanadi, yoylar konsentrik
    PolylineEntity st({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}, {0.0, 1.0, 0.0, 1.0}, true);
    auto os = offsetEntity(st, 1, {5, 5, 0});
    auto& r = static_cast<PolylineEntity&>(*os);
    CHECK(r.vertices().size() == 4);
    NEAR(r.bulges()[1], 1.0);
    NEAR(r.length(), st.length() - 2 * kPi * 1.0);
}
TEST(trim_line_arc_circle_polyline) {
    const std::vector<geom::Prim> edges = {geom::Prim::seg({3, -5, 0}, {3, 5, 0}), geom::Prim::seg({7, -5, 0}, {7, 5, 0})};
    // O'rtasini qirqish -> 2 bo'lak
    auto mid = trimEntity(LineEntity({0, 0, 0}, {10, 0, 0}), edges, {5, 0, 0});
    CHECK(mid.size() == 2);
    NEAR(static_cast<LineEntity&>(*mid[0]).length(), 3.0);
    VNEAR(static_cast<LineEntity&>(*mid[1]).startPoint(), Vec3(7, 0, 0));
    // Uchini qirqish -> 1 bo'lak
    auto end = trimEntity(LineEntity({0, 0, 0}, {10, 0, 0}), edges, {9, 0, 0});
    CHECK(end.size() == 1);
    VNEAR(static_cast<LineEntity&>(*end[0]).endPoint(), Vec3(7, 0, 0));
    bool none = false;
    try { trimEntity(LineEntity({0, 10, 0}, {10, 10, 0}), edges, {5, 10, 0}); } catch (const Error& e) { none = e.key() == "err.trim_nothing"; }
    CHECK(none);
    // Aylana ikki chiziq bilan: o'ng qismi olib tashlanadi -> yoy
    auto c = trimEntity(CircleEntity({5, 0, 0}, 3), {geom::Prim::seg({5, -9, 0}, {5, 9, 0})}, {8, 0, 0});
    CHECK(c.size() == 1 && c[0]->type() == EntityType::Arc);
    auto& arc = static_cast<ArcEntity&>(*c[0]);
    NEAR(arc.sweep(), kPi);
    NEAR(arc.pointAtAngle(arc.startAngle() + arc.sweep() / 2).x, 2.0);  // chap yarmi qoldi
    // Ochiq poliliniya: o'rta qism, tip saqlanadi
    PolylineEntity pl({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}});
    auto pp = trimEntity(pl, {geom::Prim::seg({5, -1, 0}, {5, 1, 0}), geom::Prim::seg({9, 5, 0}, {11, 5, 0})}, {10, 1, 0});
    CHECK(pp.size() == 2 && pp[0]->type() == EntityType::Polyline);
    NEAR(static_cast<PolylineEntity&>(*pp[0]).length(), 5.0);
    NEAR(static_cast<PolylineEntity&>(*pp[1]).length(), 5.0);
}
TEST(extend_line_arc) {
    const std::vector<geom::Prim> wall = {geom::Prim::seg({20, -5, 0}, {20, 5, 0})};
    auto l = extendEntity(LineEntity({0, 0, 0}, {10, 0, 0}), wall, {9, 0, 0});
    VNEAR(static_cast<LineEntity&>(*l).endPoint(), Vec3(20, 0, 0));
    auto l2 = extendEntity(LineEntity({0, 0, 0}, {10, 0, 0}), {geom::Prim::seg({-4, -5, 0}, {-4, 5, 0})}, {1, 0, 0});
    VNEAR(static_cast<LineEntity&>(*l2).startPoint(), Vec3(-4, 0, 0));
    bool none = false;
    try { extendEntity(LineEntity({0, 10, 0}, {10, 10, 0}), wall, {9, 10, 0}); } catch (const Error& e) { none = e.key() == "err.extend_nothing"; }
    CHECK(none);
    // Yoy (0..90°) gorizontal chiziqqa (y = -5 ... radius 10) — oxiri 180° dagi x o'qigacha
    ArcEntity a({0, 0, 0}, 10, 0, kPi / 2);
    auto ea = extendEntity(a, {geom::Prim::seg({-20, 0, 0}, {-5, 0, 0})}, {0, 10, 0});
    NEAR(static_cast<ArcEntity&>(*ea).sweep(), kPi);
}
TEST(fillet_lines_radius_and_corner) {
    LineEntity h({0, 0, 0}, {10, 0, 0}), v({12, 2, 0}, {12, 10, 0});
    auto r0 = filletLines(h, {5, 0, 0}, v, {12, 5, 0}, 0);
    VNEAR(static_cast<LineEntity&>(*r0.first).endPoint(), Vec3(12, 0, 0));
    VNEAR(static_cast<LineEntity&>(*r0.second).startPoint(), Vec3(12, 0, 0));
    CHECK(!r0.arc);
    auto r2 = filletLines(h, {5, 0, 0}, v, {12, 5, 0}, 2);
    VNEAR(static_cast<LineEntity&>(*r2.first).endPoint(), Vec3(10, 0, 0));
    VNEAR(static_cast<LineEntity&>(*r2.second).startPoint(), Vec3(12, 2, 0));
    CHECK(r2.arc != nullptr);
    if (r2.arc) {
        auto& a = static_cast<ArcEntity&>(*r2.arc);
        NEAR(a.radius(), 2.0);
        VNEAR(a.center(), Vec3(10, 2, 0));
        NEAR(a.sweep(), kPi / 2);
    }
    bool big = false, par = false;
    try { filletLines(h, {5, 0, 0}, v, {12, 5, 0}, 50); } catch (const Error& e) { big = e.key() == "err.fillet_radius"; }
    try { filletLines(h, {5, 0, 0}, LineEntity({0, 3, 0}, {9, 3, 0}), {5, 3, 0}, 1); } catch (const Error& e) { par = e.key() == "err.fillet_parallel"; }
    CHECK(big && par);
    // Kesishgan chiziqlar: tanlangan tomonlar qoladi (X shakldan burchak)
    auto x = filletLines(LineEntity({-5, 0, 0}, {5, 0, 0}), {3, 0, 0}, LineEntity({0, -5, 0}, {0, 5, 0}), {0, 3, 0}, 0);
    VNEAR(static_cast<LineEntity&>(*x.first).startPoint(), Vec3(0, 0, 0));
    VNEAR(static_cast<LineEntity&>(*x.second).startPoint(), Vec3(0, 0, 0));
    NEAR(static_cast<LineEntity&>(*x.first).length(), 5.0);
}
TEST(cli_modify_commands_with_undo) {
    const auto s = runScript("uz", {
        "CHIZIQ 0,0 20,0", "CHIZIQ 10,-5 10,5", "QIRQ HAMMA 15,0", "ROʻYXAT 1",
        "PARALLEL 3 1 5,5", "ROʻYXAT 3",
        "CHIZIQ 0,2 5,2", "UZAYTIR 2 4,2", "ROʻYXAT 4",
        "CHIZIQ 30,0 40,0", "CHIZIQ 42,2 42,10", "TUTASHTIR 35,0 42,5 2", "ROʻYXAT 6",
        "AYLANA 100,0 5", "CHIZIQ 100,-10 100,10", "QIRQ 9 105,0", "ROʻYXAT 8",
        "TUTASHTIR 100,1 95,0", "QIRQ HAMMA 500,500", "ORQAGA 2", "ROʻYXAT 8"});
    CHECK(s.find("#1 qirqildi: 1 ta boʻlak qoldi") != std::string::npos);
    CHECK(s.find("LINE (0.0000, 0.0000, 0.0000) -> (10.0000, 0.0000, 0.0000)") != std::string::npos);
    CHECK(s.find("Parallel nusxa: 1 ta (#3…)") != std::string::npos);
    CHECK(s.find("LINE (0.0000, 3.0000, 0.0000) -> (10.0000, 3.0000, 0.0000)") != std::string::npos);
    CHECK(s.find("#4 uzaytirildi") != std::string::npos);
    CHECK(s.find("LINE (0.0000, 2.0000, 0.0000) -> (10.0000, 2.0000, 0.0000)") != std::string::npos);
    CHECK(s.find("#5 va #6 tutashtirildi (R=2.0000)") != std::string::npos);
    CHECK(s.find("LINE (42.0000, 2.0000, 0.0000) -> (42.0000, 10.0000, 0.0000)") != std::string::npos);
    CHECK(s.find("ARC") != std::string::npos);
    CHECK(s.find("Xato: Ikki xil chiziq (LINE) tanlang") != std::string::npos);
    CHECK(s.find("Xato: Nuqtada obyekt yoʻq: 500,500") != std::string::npos);
    CHECK(s.find("CIRCLE c=(100.0000, 0.0000, 0.0000) r=5.0000") != std::string::npos);  // undo aylanani qaytardi
}

// ---------- Quvurlar katalogi va kanalizatsiya ----------
TEST(pipe_catalog_contents) {
    const auto& c = civil::PipeCatalog::builtin();
    CHECK(c.materials().size() == 7);
    auto* ac = c.find("AC-BNT-200");  // GOST 1839-80 jadvali
    CHECK(ac && ac->od == 211 && ac->wall == 11 && ac->mass() == 13.2 && ac->verified);
    auto* pvc = c.find("PVC-SN8-200");  // SDR34
    CHECK(pvc && std::abs(pvc->wall - 5.9) < 1e-9 && std::abs(pvc->id_mm() - 188.2) < 1e-9);
    NEAR(pvc->mass(), 3.14159265358979 * (0.1 * 0.1 - 0.0941 * 0.0941) * 1400);  // zichlikdan
    auto* di = c.find("DI-K9-300");  // e = 9(0.5+0.001·300) = 7.2
    CHECK(di && std::abs(di->wall - 7.2) < 1e-9);
    for (const auto& t : c.templates) CHECK(c.find(t.specId) != nullptr);
    // foydalanuvchi shablonlari JSON orqali
    civil::PipeTemplate u;
    u.id = "u1";
    u.name = "Mening \"shablonim\"";
    u.specId = "PE-SDR17-160";
    u.slope = 0.01;
    const auto back = civil::PipeCatalog::templatesFromJson(civil::PipeCatalog::templatesToJson({u}));
    CHECK(back.size() == 1 && back[0].name == u.name && back[0].user && std::abs(back[0].slope - 0.01) < 1e-12);
}
TEST(sewer_hydraulics_manning) {
    // to'liq kesim: v = R^(2/3)·√i / n, R = D/4
    double v = 0;
    const double q = civil::partialFlow(0.2, 1.0, 0.005, 0.013, &v);
    NEAR(v, std::pow(0.05, 2.0 / 3.0) * std::sqrt(0.005) / 0.013);
    NEAR(q, v * 3.14159265358979 * 0.01);
    // yarim to'lishda R = D/4 ham — tezlik bir xil, sarf yarmi
    double vh = 0;
    const double qh = civil::partialFlow(0.2, 0.5, 0.005, 0.013, &vh);
    CHECK(std::abs(vh - v) < 1e-9 && std::abs(qh - q / 2) < 1e-9);
    const double f = civil::fillForFlow(0.2, qh, 0.005, 0.013);
    CHECK(std::abs(f - 0.5) < 1e-6);
    CHECK(civil::fillForFlow(0.2, q * 2, 0.005, 0.013) > 1.0);
}
static civil::SewerNetwork demoNet() {
    civil::SewerNetwork n;
    n.startDepth = 1.8;
    n.manholes = {{"K1-1", 0, 0, 100.0, std::nullopt, 0}, {"K1-2", 40, 0, 99.8, std::nullopt, 0}, {"K1-3", 85, 0, 99.6, std::nullopt, 0}};
    n.segments = {{"PVC-SN8-200", 0.007, 3.0}, {"PVC-SN8-250", 0.006, 6.0}};
    return n;
}
TEST(sewer_compute_inverts_and_checks) {
    auto n = demoNet();
    auto r = civil::compute(n);
    CHECK(r.errors() == 0);
    NEAR(r.segments[0].invUp, 98.2);
    NEAR(r.segments[0].invDown, 98.2 - 0.28);
    // K1-2 da Ø200 -> Ø250: ustki yasovchi bo'yicha (ichki 188.2 -> 235.4)
    NEAR(r.manholes[1].invOut, 98.2 - 0.28 - (0.2354 - 0.1882));
    NEAR(r.segments[1].invUp, r.manholes[1].invOut);
    NEAR(r.totalLength, 85.0);
    CHECK(r.manholes[0].diameter == 1.0 && r.manholeCount[1.0] == 3);
    CHECK(r.trenchVolume > 0 && r.backfillVolume > 0 && r.backfillVolume < r.trenchVolume);
    auto has = [&](const civil::SewerResult& rr, const std::string& key) {
        for (const auto& i : rr.issues) if (i.key == key) return true;
        return false;
    };
    // normadan chiqishlar: kichik nishablik, katta oraliq, sayoz, diametr kamayishi, hovli quvuri ko'chada
    auto bad = demoNet();
    bad.segments[0].slope = 0.004;
    bad.manholes[1].x = 60;  // 60 m > 50 m (Ø200)
    bad.segments[1].specId = "PVC-SN8-160";
    bad.startDepth = 0.6;
    auto rb = civil::compute(bad);
    CHECK(has(rb, "sewer.chk.min_slope"));
    CHECK(has(rb, "sewer.chk.spacing"));
    CHECK(has(rb, "sewer.chk.cover"));
    CHECK(has(rb, "sewer.chk.dn_decrease"));
    CHECK(has(rb, "sewer.chk.min_dn"));
    CHECK(rb.errors() >= 5);
    // sig'imdan oshgan sarf
    auto over = demoNet();
    over.segments[0].flowLs = 500;
    CHECK(has(civil::compute(over), "sewer.chk.capacity"));
    // kichik quduq
    auto mh = demoNet();
    mh.manholes[1].diameter = 0.7;
    CHECK(has(civil::compute(mh), "sewer.chk.manhole_d"));
}
TEST(sewer_json_roundtrip_and_drawing) {
    auto n = demoNet();
    n.manholes[2].invert = 97.0;
    const auto back = civil::fromJson(civil::toJson(n));
    CHECK(back.manholes.size() == 3 && back.segments.size() == 2);
    CHECK(back.manholes[2].invert && std::abs(*back.manholes[2].invert - 97.0) < 1e-12);
    CHECK(back.segments[1].specId == "PVC-SN8-250");
    const auto r = civil::compute(n);
    const auto plan = civil::drawPlan(n, r);
    int circles = 0, lines = 0, texts = 0;
    for (const auto& e : plan) {
        circles += e->type() == EntityType::Circle;
        lines += e->type() == EntityType::Line;
        texts += e->type() == EntityType::Text;
        CHECK(e->layer().rfind("K1_", 0) == 0);
    }
    CHECK(circles == 3 && lines == 2 && texts == 3 * 2 + 2 * 2);
    const auto prof = civil::drawProfile(n, r, {500, 0, 0}, civil::ProfileLabels{});
    CHECK(prof.size() > 40);
    for (const auto& e : prof) CHECK(e->layer() == "K1_PROFIL");
}
TEST(cli_sewer_command) {
    const std::string path = (std::filesystem::temp_directory_path() / "asiacad_test.kan.json").string();
    {
        std::ofstream f(path);
        f << civil::toJson(demoNet());
    }
    const auto s = runScript("ru", {"КАНАЛИЗАЦИЯ " + path, "ОТМЕНИТЬ", "ТРУБЫ AC", "SEWER nofile.kan.json"});
    CHECK(s.find("Сеть K1: колодцев 3, L=85.0000 м; ошибок 0") != std::string::npos);
    CHECK(s.find("PVC SN8 Ø200×5.9") != std::string::npos);
    CHECK(s.find("AC-BNT-400") != std::string::npos);
    CHECK(s.find("Ошибка:") != std::string::npos);
    std::filesystem::remove(path);
}

TEST(storm_flow_shnq_2_04_22) {
    civil::StormInput st;
    st.enabled = true;  // q20=50, n=0.55, mr=60, γ=1.54, P=1
    const double A = 50 * std::pow(20.0, 0.55);  // P=1 -> lgP=0
    NEAR(civil::stormA(st), A);
    NEAR(civil::stormZImpervious(250, 0.55), 0.32);    // 6-jadval, 300 dan kichik
    NEAR(civil::stormZImpervious(450, 0.70), 0.305);   // 400..500 orasida, n ≥ 0.65
    NEAR(civil::stormBeta(0.55), 0.725);               // 7-jadval interpolyatsiya
    NEAR(civil::stormAreaK(1000), 0.90);               // 4-jadval
    NEAR(civil::stormShortK(5), 0.8);                  // 63-band
    const double q = 0.725 * 0.32 * std::pow(A, 1.2) * 2.0 / std::pow(10.0, 1.2 * 0.55 - 0.1);
    NEAR(civil::stormFlow(st, 2.0, 10.0), q);
    st.share = {0.5, 0, 0, 0, 0, 0, 0.5};              // yarmi gazon
    NEAR(civil::stormZmid(st), (0.32 + 0.038) / 2);
    // tarmoqda: sarf pastga qarab o'sadi, vaqt ortadi
    civil::SewerNetwork n;
    n.system = "K2";
    n.storm.enabled = true;
    n.startDepth = 2.0;
    n.manholes = {{"D-1", 0, 0, 100, std::nullopt, 0, 1.5}, {"D-2", 50, 0, 99.8, std::nullopt, 0, 2.0}, {"D-3", 100, 0, 99.6, std::nullopt, 0, 0}};
    n.segments = {{"RC-TS-500", 0.003, 0}, {"RC-TS-600", 0.003, 0}};
    const auto r = civil::compute(n);
    CHECK(r.segments[0].stormF == 1.5 && r.segments[1].stormF == 3.5);
    CHECK(r.segments[1].stormT > r.segments[0].stormT);
    CHECK(r.segments[1].flowLs > r.segments[0].flowLs && r.segments[0].flowLs > 0);
    CHECK(r.segments[0].maxFill == 1.0);  // yomg'ir tarmog'i to'liq kesimga
    const auto back = civil::fromJson(civil::toJson(n));
    CHECK(back.storm.enabled && back.manholes[1].areaHa == 2.0);
}

// ---------- Monolit bino ----------
TEST(bim_guess_layers) {
    CHECK(bim::guessKind("USTUN") == bim::ElemKind::Column);
    CHECK(bim::guessKind("А-КОЛОННЫ") == bim::ElemKind::Column);
    CHECK(bim::guessKind("S-WALL") == bim::ElemKind::Wall);
    CHECK(bim::guessKind("Плита перекрытия") == bim::ElemKind::Slab);
    CHECK(bim::guessKind("PADBETONKA") == bim::ElemKind::Lean);
    CHECK(bim::guessKind("Ленточный фундамент") == bim::ElemKind::StripFooting);
    CHECK(bim::guessKind("YOZUV") == bim::ElemKind::Ignore);
}
TEST(bim_model_volumes) {
    Document doc;
    auto add = [&](const char* layer, std::vector<Vec3> pts) {
        auto e = std::make_unique<PolylineEntity>(std::move(pts), true);
        e->setLayer(layer);
        doc.add(std::move(e));
    };
    add("KONTUR", {{0, 0, 0}, {12, 0, 0}, {12, 6, 0}, {0, 6, 0}});
    for (double x : {0.0, 6.0, 12.0})
        for (double y : {0.0, 6.0}) add("USTUN", {{x - 0.2, y - 0.2, 0}, {x + 0.2, y - 0.2, 0}, {x + 0.2, y + 0.2, 0}, {x - 0.2, y + 0.2, 0}});
    add("TESHIK", {{5, 2, 0}, {7, 2, 0}, {7, 4, 0}, {5, 4, 0}});
    auto line = std::make_unique<LineEntity>(Vec3{0, 3, 0}, Vec3{4, 3, 0});
    line->setLayer("DEVOR");
    doc.add(std::move(line));
    bim::BuildingParams p;
    p.floors = 3;
    p.floorHeight = 3.0;
    const auto m = bim::buildModel(bim::readPlan(doc, bim::autoLayerMap(doc)), p);
    auto vol = [&](bim::ElemKind k, int lv) {
        for (const auto& r : m.pto) if (r.kind == k && r.level == lv) return r.volume;
        return -1.0;
    };
    NEAR(vol(bim::ElemKind::Column, 2), 6 * 0.16 * 2.8);
    NEAR(vol(bim::ElemKind::Column, 1), 6 * 0.16 * (2.8 + 0.3));    // fundament ustidan
    NEAR(vol(bim::ElemKind::Slab, 1), (72 - 4) * 0.2);              // teshik ayirildi
    NEAR(vol(bim::ElemKind::Roof, 3), (72 - 4) * 0.22);
    NEAR(vol(bim::ElemKind::Wall, 1), 4 * 0.2 * 3.1);               // chiziq -> 0.2 m devor
    NEAR(vol(bim::ElemKind::FoundationSlab, 0), 13 * 7 * 0.6);      // +0.5 m chiqish
    NEAR(vol(bim::ElemKind::Lean, 0), 13.2 * 7.2 * 0.1);
    NEAR(m.heightTop, 9.0);
    CHECK(m.byConcrete.at("B7.5") > 0 && m.byConcrete.at("B25") > 0);
    // lenta fundament
    p.stripFoundation = true;
    const auto ms = bim::buildModel(bim::readPlan(doc, bim::autoLayerMap(doc)), p);
    CHECK(vol(bim::ElemKind::FoundationSlab, 0) > 0);
    bool strip = false;
    for (const auto& r : ms.pto) strip = strip || (r.kind == bim::ElemKind::StripFooting && r.count == 7);
    CHECK(strip);
    // JSON
    std::map<std::string, bim::ElemKind> lm{{"USTUN", bim::ElemKind::Column}, {"X", bim::ElemKind::Wall}};
    bim::BuildingParams q;
    std::map<std::string, bim::ElemKind> lm2;
    bim::paramsFromJson(bim::paramsToJson(p, lm), q, lm2);
    CHECK(q.floors == 3 && q.stripFoundation && lm2 == lm);
    // PTO jadvali
    bim::PtoLabels L{"T", "a", "b", "c", "d", "e", "f", "TOTAL", nullptr, nullptr};
    const auto tbl = bim::drawPtoTable(m, {0, 0, 0}, 0.25, L);
    CHECK(tbl.size() > m.pto.size() * 6);
}
TEST(cli_building_command) {
    const auto s = runScript("ru", {"ПОЛИЛИНИЯ 0,0 6,0 6,6 0,6 ЗАМКНУТЬ", "ЗДАНИЕ 2 3"});
    CHECK(s.find("Ошибка") != std::string::npos || s.find("Предупреждение") != std::string::npos);  // слои не назначены
    const auto u = runScript("uz", {"QATLAM USTUN", "JORIY USTUN", "POLICHIZIQ 0,0 0.4,0 0.4,0.4 0,0.4 YOPIQ", "BINO 2 3 0.2 20,10"});
    CHECK(u.find("1-qavat · Ustun: 1 dona") != std::string::npos);
    CHECK(u.find("PTO jadvali chizildi") != std::string::npos);
}

TEST(rebar_build_model) {
    CHECK(std::abs(bim::barMassPerM(0.012) - 0.888) < 1e-3);
    CHECK(std::abs(bim::barMassPerM(0.016) - 1.578) < 1e-3);
    Document doc;
    auto add = [&](const char* layer, std::vector<Vec3> pts) {
        auto e = std::make_unique<PolylineEntity>(std::move(pts), true);
        e->setLayer(layer);
        doc.add(std::move(e));
    };
    add("USTUN", {{0, 0, 0}, {0.4, 0, 0}, {0.4, 0.4, 0}, {0, 0.4, 0}});
    add("PLITA", {{-3, -3, 0}, {3, -3, 0}, {3, 3, 0}, {-3, 3, 0}});
    bim::BuildingParams p;
    p.floors = 2;
    const auto bm = bim::buildModel(bim::readPlan(doc, bim::autoLayerMap(doc)), p);
    auto rs = bim::defaultRebarSettings();
    CHECK(rs.byKind.count(bim::ElemKind::Column) && rs.byKind.count(bim::ElemKind::Slab) && rs.byKind.count(bim::ElemKind::FoundationSlab));
    const auto rm = bim::buildRebar(bm, p, rs);
    CHECK(rm.totalMass > 0);
    double sum = 0;
    for (const auto& r : rm.schedule) sum += r.mass;
    NEAR(sum, rm.totalMass);
    int mainL1 = 0, stir = 0, slabBX = 0;
    bool found = false, roof = false;
    for (const auto& b : rm.bars) {
        if (b.kind == bim::ElemKind::Column && b.level == 1 && b.role == bim::BarRole::Main) ++mainL1;
        if (b.kind == bim::ElemKind::Column && b.role == bim::BarRole::Stirrup) ++stir;
        if (b.kind == bim::ElemKind::Slab && b.level == 1 && b.role == bim::BarRole::BottomX) ++slabBX;
        found |= b.kind == bim::ElemKind::FoundationSlab;
        roof |= b.kind == bim::ElemKind::Roof;
    }
    CHECK(mainL1 == rs.byKind[bim::ElemKind::Column].nMain);
    CHECK(stir > 10);
    CHECK(slabBX >= 28 && slabBX <= 32);  // 6 m / 0,2 (teshik yo'q)
    CHECK(found && roof);                  // fundamentdan tomgacha
    CHECK(rm.nodes > 0 && rm.wireKg > 0 && rm.chairs > 0);
    // himoya qatlami va ustun Ø tekshiruvi
    auto bad = rs;
    bad.byKind[bim::ElemKind::Column].cover = 0.010;  // 10 + Ø8 = 18 < 20 (376b)
    bad.byKind[bim::ElemKind::Column].d1 = 0.010;
    const auto rb = bim::buildRebar(bm, p, bad);
    bool cover = false, cold = false;
    for (const auto& is : rb.issues) {
        cover |= is.key == "rebar.chk.cover";
        cold |= is.key == "rebar.chk.col_d";
    }
    CHECK(cover && cold);
}

TEST(rebar_shnq_2_03_01_24) {
    // 12-jadval
    NEAR(bim::concreteRbShnq("B25"), 14.5);
    NEAR(bim::concreteRbShnq("B7.5"), 4.5);
    NEAR(bim::concreteRbShnq("B15"), 8.5);
    CHECK(bim::concreteRbShnq("X") == 0);
    // 36-jadval: A400 Ø16, B25 — cho'zilgan (0,9·365/14,5 + 11)·d, siqilgan (0,65·365/14,5 + 8)·d
    CHECK(std::abs(bim::requiredLapShnq("A400", 0.016, "B25", true) - (0.9 * 365 / 14.5 + 11) * 0.016) < 1e-9);
    CHECK(std::abs(bim::requiredLapShnq("A400", 0.016, "B25", false) - (0.65 * 365 / 14.5 + 8) * 0.016) < 1e-9);
    CHECK(std::abs(bim::requiredLapShnq("A400", 0.006, "B25", true) - 0.25) < 1e-9);  // lmin 250 mm
    // standart shablonlar B25 da xatosiz; B15 da plita nahlyostkasi 40d yetmaydi; A500C — ogohlantirish
    Document doc;
    auto add = [&](const char* layer, std::vector<Vec3> pts) {
        auto e = std::make_unique<PolylineEntity>(std::move(pts), true);
        e->setLayer(layer);
        doc.add(std::move(e));
    };
    add("USTUN", {{0, 0, 0}, {0.4, 0, 0}, {0.4, 0.4, 0}, {0, 0.4, 0}});
    add("PLITA", {{-3, -3, 0}, {3, -3, 0}, {3, 3, 0}, {-3, 3, 0}});
    bim::BuildingParams p;
    p.floors = 2;
    const auto bm = bim::buildModel(bim::readPlan(doc, bim::autoLayerMap(doc)), p);
    auto rs = bim::defaultRebarSettings();
    auto has = [](const bim::RebarModel& m, const char* key) {
        for (const auto& is : m.issues) if (is.key == key) return true;
        return false;
    };
    const auto ok = bim::buildRebar(bm, p, rs);
    bool anyErr = false;
    for (const auto& is : ok.issues) anyErr |= is.error;
    CHECK(!anyErr);
    auto p15 = p;
    p15.classSlab = "B15";
    CHECK(has(bim::buildRebar(bim::buildModel(bim::readPlan(doc, bim::autoLayerMap(doc)), p15), p15, rs), "rebar.chk.lap"));
    auto r5 = rs;
    r5.byKind[bim::ElemKind::Column].clsMain = "A500C";
    CHECK(has(bim::buildRebar(bm, p, r5), "rebar.chk.cls"));
    // 425: nahlyostka zonasida xomut <= 10d; 412: ustun yuzida sterjenlar orasi <= 400 mm
    auto rl = rs;
    rl.byKind[bim::ElemKind::Column].sStirEnd = 0.2;
    CHECK(has(bim::buildRebar(bm, p, rl), "rebar.chk.stir_lap"));
    Document big;
    auto e = std::make_unique<PolylineEntity>(std::vector<Vec3>{{0, 0, 0}, {0.6, 0, 0}, {0.6, 0.6, 0}, {0, 0.6, 0}}, true);
    e->setLayer("USTUN");
    big.add(std::move(e));
    const auto bmb = bim::buildModel(bim::readPlan(big, bim::autoLayerMap(big)), p);
    CHECK(has(bim::buildRebar(bmb, p, rs), "rebar.chk.col_face"));  // 4Ø16, 0,6 m: ~0,5 m oraliq
    // 37-jadval: 0,6×0,6 da 4Ø16 = 0,22% ≥ 0,20% (λ≈17) — OK; 12Ø25 da yo'q
    CHECK(!has(bim::buildRebar(bm, p, rs), "rebar.chk.mu"));
}

TEST(rebar_json_roundtrip) {
    auto rs = bim::defaultRebarSettings();
    rs.byKind[bim::ElemKind::Column].nMain = 8;
    rs.byKind[bim::ElemKind::Slab].s1 = 0.15;
    rs.stockLength = 12;
    bim::RebarSettings back;
    bim::rebarFromJson(bim::rebarToJson(rs), back);
    CHECK(back.byKind[bim::ElemKind::Column].nMain == 8);
    NEAR(back.byKind[bim::ElemKind::Slab].s1, 0.15);
    NEAR(back.stockLength, 12);
    // bino JSON ichidagi "rebar" bloki
    bim::RebarSettings b2;
    bim::rebarFromJson("{\"format\":\"asiacad-building\",\"rebar\":" + bim::rebarToJson(rs) + "}", b2);
    CHECK(b2.byKind[bim::ElemKind::Column].nMain == 8);
    auto tpls = bim::builtinRebarTemplates();
    CHECK(tpls.size() == 12);
    const auto t2 = bim::templatesFromJson(bim::templatesToJson(tpls));
    CHECK(t2.size() == tpls.size() && t2[3].id == tpls[3].id && t2[3].nMain == tpls[3].nMain);
}

TEST(cli_rebar_command) {
    const auto u = runScript("uz", {"QATLAM USTUN", "JORIY USTUN", "POLICHIZIQ 0,0 0.4,0 0.4,0.4 0,0.4 YOPIQ", "ARMATURA 20,10"});
    CHECK(u.find("Armatura jami") != std::string::npos);
    CHECK(u.find("Armatura vedomosti chizildi") != std::string::npos);
}

int main() {
    for (auto& [name, fn] : registry()) {
        const int before = g_fail;
        fn();
        std::cout << (g_fail == before ? "[ OK ] " : "[FAIL] ") << name << '\n';
    }
    std::cout << '\n' << registry().size() << " test, " << g_checks << " tekshiruv, " << g_fail << " xato\n";
    return g_fail == 0 ? 0 : 1;
}
