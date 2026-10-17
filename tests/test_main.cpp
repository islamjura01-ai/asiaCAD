// AsiaCAD core testlari (tashqi kutubxonasiz)
#include <asiacad/bim/building.h>
#include <asiacad/bim/rebar.h>
#include <asiacad/bim/rebar_import.h>
#include <asiacad/bim/kj_project.h>
#include <asiacad/civil/excel.h>
#include <asiacad/civil/drawset.h>
#include <asiacad/civil/road.h>
#include <asiacad/civil/sewer.h>
#include <asiacad/civil/utility.h>
#include <asiacad/io/xlsx.h>
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
    CHECK(loc.canonicalCommands().size() == 52);
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
TEST(sewer_snip_2_04_03_85) {
    auto has = [&](const civil::SewerResult& rr, const std::string& key) {
        for (const auto& i : rr.issues) if (i.key == key) return true;
        return false;
    };
    auto ref = [&](const civil::SewerResult& rr, const std::string& key) {
        for (const auto& i : rr.issues) if (i.key == key) return i.ref;
        return std::string();
    };
    // 2.41: Ø200 0,006 — faqat asoslangan holda (ogohlantirish), 0,004 — xato
    auto a = demoNet();
    a.segments[0].slope = 0.006;
    const auto ra = civil::compute(a);
    CHECK(has(ra, "sewer.chk.slope_just") && !has(ra, "sewer.chk.min_slope"));
    CHECK(ref(ra, "sewer.chk.slope_just") == "SNiP 2.04.03-85, 2.41");
    // 2.33: yomg'ir ko'cha tarmog'i Ø200 < Ø250
    auto st = demoNet();
    st.system = "K2";
    st.street = true;
    CHECK(has(civil::compute(st), "sewer.chk.min_dn"));
    // 4.5: burilish 120° (quvurlar orasidagi burchak 60° < 90°)
    auto an = demoNet();
    an.manholes[2].x = 40 - 22.5;
    an.manholes[2].y = 39;
    const auto rn = civil::compute(an);
    CHECK(has(rn, "sewer.chk.angle"));
    CHECK(!has(civil::compute(demoNet()), "sewer.chk.angle"));
    // SP 32.13330 6.3.1: Ø800 quduq — 800 + 500 = 1,3 m
    auto big = demoNet();
    big.segments = {{"RC-TS-800", 0.002, 300.0}, {"RC-TS-800", 0.002, 300.0}};
    const auto rbg = civil::compute(big);
    bool any = false;
    for (const auto& m : rbg.manholes) any |= std::abs(m.required - 1.3) < 1e-9;
    CHECK(any && !has(rbg, "sewer.chk.spec"));
    CHECK(ref(rbg, "sewer.chk.vmin").empty() || ref(rbg, "sewer.chk.vmin") == "SNiP 2.04.03-85, 2.34, 16-jadval");
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

// ---------- Yer osti tarmoqlari ----------
TEST(util_labels_and_layers) {
    double v = 0;
    CHECK(civil::parseUtilLabel("K1 Ø200", v) == 'd' && v == 200);
    CHECK(civil::parseUtilLabel("%%c110x6,6", v) == 'd' && v == 110);
    CHECK(civil::parseUtilLabel("Ду150", v) == 'd' && v == 150);
    CHECK(civil::parseUtilLabel("DN 300", v) == 'd' && v == 300);
    CHECK(civil::parseUtilLabel("h=1,80", v) == 'h' && std::abs(v - 1.8) < 1e-12);
    CHECK(civil::parseUtilLabel("Н=2.1", v) == 'h' && std::abs(v - 2.1) < 1e-12);
    CHECK(civil::parseUtilLabel("98,45", v) == 'z' && std::abs(v - 98.45) < 1e-12);
    CHECK(civil::parseUtilLabel("▽ 101.20", v) == 'z' && std::abs(v - 101.2) < 1e-12);
    CHECK(civil::parseUtilLabel("отм. 99.5", v) == 'z');
    CHECK(civil::parseUtilLabel("АВБбШв 4x120", v) == 0);
    CHECK(civil::parseUtilLabel("L=45", v) == 'l' && std::abs(v - 45) < 1e-12);  // quvur uzunligi
    CHECK(civil::guessUtil("V1_SUV") == civil::UtilKind::Water);
    CHECK(civil::guessUtil("К1") == civil::UtilKind::Sewer);
    CHECK(civil::guessUtil("K2_YOMGIR") == civil::UtilKind::Storm);
    CHECK(civil::guessUtil("ГАЗ_НД") == civil::UtilKind::Gas);
    CHECK(civil::guessUtil("KABEL_0.4KV") == civil::UtilKind::Power);
    CHECK(civil::guessUtil("СВЯЗЬ") == civil::UtilKind::Telecom);
    CHECK(civil::guessUtil("YER_SATHI") == civil::UtilKind::Ground);
    CHECK(civil::guessUtil("YER_OSTI_SUV") == civil::UtilKind::Water);
    CHECK(civil::guessUtil("KONTUR") == civil::UtilKind::Ignore);
}

TEST(util_example_drawing_to_3d) {
    const auto r = loadAcad(std::string(ASIACAD_EXAMPLES_DIR) + "/yer_osti.acad");
    const auto p = civil::defaultUtilParams();
    auto m = civil::readUtilities(r.doc, civil::autoUtilLayers(r.doc), p);
    CHECK(m.lines.size() == 6);
    CHECK(m.nodes.size() == 3);
    CHECK(m.groundPoints == 6);
    auto byLayer = [&](const std::string& l) -> const civil::UtilLine* {
        for (const auto& x : m.lines) if (x.layer == l) return &x;
        return nullptr;
    };
    const auto* w = byLayer("V1_SUV");
    const auto* k = byLayer("K1_KANAL");
    const auto* g = byLayer("GAZ");
    CHECK(w && w->dn == 110 && w->dnFromLabel);
    CHECK(k && k->dn == 200);
    if (k) {
        NEAR(k->v[0].z, 98.30 + 0.1);  // tub belgisi + radius
        CHECK(k->v[1].src == civil::UtilVertex::FromLabel);
    }
    if (w) {
        CHECK(w->v[0].src == civil::UtilVertex::FromDepth && w->v[1].src == civil::UtilVertex::Interpolated);
        NEAR(w->v[0].ground - w->v[0].z, 1.5 + 0.055);
    }
    CHECK(g && !g->dnFromLabel && g->dn == 63);
    auto has = [&](const char* key) {
        for (const auto& i : m.issues) if (i.key == key) return true;
        return false;
    };
    CHECK(has("util.chk.no_dn"));   // gaz
    CHECK(has("util.chk.gap"));     // aloqa 0,5 m
    CHECK(has("util.chk.clear"));   // kabel suv ustidan yaqin
    CHECK(m.crossings.size() >= 6);
    // qo'lda tuzatish: kabelni 0,5 m pastga tushirish -> suv bilan kesishma tuzaladi? (kabel endi suv ostida)
    // JSON orqali saqlash/o'qish
    civil::UtilModel m2;
    auto p2 = p;
    civil::utilFromJson(civil::utilToJson(m, p), m2, p2);
    CHECK(m2.lines.size() == m.lines.size() && m2.issues.size() == m.issues.size());
    NEAR(m2.lines[0].v[0].z, m.lines[0].v[0].z);
    CHECK(!civil::drawUtilities3D(m, 0.5).empty());
}

TEST(util_force_kind_keeps_known_layers_only) {
    // NetCAD fayli: tarmoq qatlami nomidan tanilsa (ET1_ASU_Borular), "SEWER" roli faqat shunday qatlamlarga —
    // genplan/yo'l chiziqlari ("0", "YOL_KENAR") quvurga aylanmaydi
    Document d;
    d.addLayer(Layer{"ET1_ASU_Borular_3"});
    d.addLayer(Layer{"YOL_KENAR"});
    d.add(std::make_unique<LineEntity>(Vec3{0, 0, 0}, Vec3{40, 0, 0}));
    d.find(d.ids().back())->setLayer("ET1_ASU_Borular_3");
    d.add(std::make_unique<LineEntity>(Vec3{0, 10, 0}, Vec3{40, 10, 0}));
    d.find(d.ids().back())->setLayer("YOL_KENAR");
    d.add(std::make_unique<LineEntity>(Vec3{0, 20, 0}, Vec3{40, 20, 0}));
    civil::UtilSource src{&d, {}, civil::UtilKind::Sewer, "a"};
    const auto m = civil::readUtilities(std::vector<civil::UtilSource>{src}, civil::defaultUtilParams());
    CHECK(m.lines.size() == 1);
    // tanilgan qatlam yo'q — eski xulq: barcha chiziqlar shu tur
    Document e;
    e.add(std::make_unique<LineEntity>(Vec3{0, 0, 0}, Vec3{40, 0, 0}));
    civil::UtilSource s2{&e, {}, civil::UtilKind::Water, "b"};
    CHECK(civil::readUtilities(std::vector<civil::UtilSource>{s2}, civil::defaultUtilParams()).lines.size() == 1);
}

TEST(util_separate_files_clash) {
    // suv, kanalizatsiya, yomg'ir — alohida DXF (mm da, qatlam nomlari umumiy "TRUBA"), rel'ef — alohida fayl
    const std::string dir = std::string(ASIACAD_EXAMPLES_DIR) + "/tarmoqlar/";
    std::vector<civil::SheetFile> files{{dir + "topo.dxf", "AUTO"}, {dir + "suv.dxf", "WATER"}, {dir + "kanal.dxf", "SEWER"}, {dir + "yomgir.dxf", "STORM"}};
    const auto sheets = civil::loadSheets(files);
    CHECK(sheets.size() == 4);
    for (const auto& sh : sheets) {
        CHECK(sh.error.empty());
        NEAR(sh.scaleUsed, 0.001);  // avto: mm
    }
    std::vector<civil::UtilSource> src;
    for (const auto& sh : sheets) {
        civil::UtilKind k = civil::UtilKind::Ignore;
        civil::utilFromKey(sh.file.role, k);
        src.push_back({sh.doc.get(), civil::autoUtilLayers(*sh.doc), k, civil::fileName(sh.file.path)});
    }
    const auto m = civil::readUtilities(src, civil::defaultUtilParams());
    CHECK(m.lines.size() == 3);
    CHECK(m.groundPoints == 45);
    CHECK(m.nodes.size() == 2);
    const civil::UtilLine *w = nullptr, *k = nullptr, *st = nullptr;
    for (const auto& l : m.lines) {
        if (l.kind == civil::UtilKind::Water) w = &l;
        if (l.kind == civil::UtilKind::Sewer) k = &l;
        if (l.kind == civil::UtilKind::Storm) st = &l;
    }
    CHECK(w && k && st);
    if (w && k && st) {
        CHECK(w->dn == 160 && k->dn == 200 && st->dn == 400);  // har yozuv o'z faylidagi chiziqqa
        NEAR(k->v[0].z, 98.20 + 0.1);
        CHECK(w->layer.starts_with("suv.dxf"));
    }
    bool clashStormWater = false, sewerWaterOk = true;
    for (const auto& c : m.crossings) {
        const auto ka = m.lines[static_cast<std::size_t>(c.a)].kind, kb = m.lines[static_cast<std::size_t>(c.b)].kind;
        auto is = [&](civil::UtilKind x, civil::UtilKind y) { return (ka == x && kb == y) || (ka == y && kb == x); };
        if (is(civil::UtilKind::Water, civil::UtilKind::Storm) && c.clear < c.required) clashStormWater = true;
        if (is(civil::UtilKind::Water, civil::UtilKind::Sewer) && c.clear < c.required) sewerWaterOk = false;
    }
    CHECK(clashStormWater);
    CHECK(sewerWaterOk);
    CHECK(m.crossings.size() == 2);
}

TEST(building_per_floor_formwork_drawings) {
    const std::string dir = std::string(ASIACAD_EXAMPLES_DIR) + "/bino_qolip/";
    civil::SheetFile f0{dir + "fundament.dxf", "FOUND"}, f1{dir + "qavat_1_2.dxf", "PLAN", 1, 2}, f3{dir + "qavat_3_5.dxf", "PLAN", 3, 5};
    const auto sheets = civil::loadSheets({f0, f1, f3});
    for (const auto& sh : sheets) CHECK(sh.error.empty());
    const auto fp = civil::floorPlansFrom(sheets);
    CHECK(fp.foundation.has_value() && fp.floors.size() == 5);
    NEAR(fp.floors.at(1).slabT, 0.2);
    NEAR(fp.floors.at(3).slabT, 0.18);
    CHECK(fp.floors.at(1).levelTop && std::abs(*fp.floors.at(1).levelTop - 3.3) < 1e-9);
    CHECK(!fp.floors.at(2).levelTop);
    bim::BuildingParams p;
    p.floors = 5;
    p.floorHeight = 3.3;
    const auto m = bim::buildModel(fp.floors.at(1), p, fp);
    auto vol = [&](bim::ElemKind k, int lv) {
        for (const auto& r : m.pto) if (r.kind == k && r.level == lv) return r.volume;
        return -1.0;
    };
    NEAR(vol(bim::ElemKind::FoundationSlab, 0), 20 * 14 * 0.8);   // aniq kontur, h=800
    NEAR(vol(bim::ElemKind::Column, 1), 12 * 0.16 * (3.3 - 0.2 + 0.3));  // -0.300 dan +3.100 gacha
    NEAR(vol(bim::ElemKind::Column, 3), 12 * 0.25 * (9.9 - 0.18 - 6.6));
    NEAR(m.heightTop, 9.9 + 2 * 3.3);
    bool unsupported = false;
    for (const auto& is : m.issues) unsupported |= is.key == "bim.chk.unsupported";
    CHECK(unsupported);  // 3-qavatda ko'chirilgan ustun ostida tayanch yo'q
}

TEST(rebar_marks_parse) {
    bim::BarMark m;
    CHECK(bim::parseBarMark("Поз.1 Ø12 A500C шаг 200", m) && m.pos == 1 && std::abs(m.d - 0.012) < 1e-12 && m.cls == "A500C" &&
          std::abs(m.spacing - 0.2) < 1e-12 && m.count == 0);
    CHECK(bim::parseBarMark("4Ø16 А-III L=3450", m) && m.count == 4 && m.cls == "A400" && std::abs(m.length - 3.45) < 1e-9);
    CHECK(bim::parseBarMark("%%c10 AI @150 верх", m) && m.cls == "A240" && std::abs(m.spacing - 0.15) < 1e-12 && m.face == 2);
    CHECK(bim::parseBarMark("⌀14 A400 s=200 pastki", m) && m.face == 1);
    CHECK(!bim::parseBarMark("Плита П-1 h=200", m));
    CHECK(bim::normRebarClass("А500С") == "A500C");
    CHECK(bim::isRebarLayer("ARM_NIZ") && bim::isRebarLayer("Арматура верх") && !bim::isRebarLayer("PLITA"));
    CHECK(bim::faceFromLayer("ARM_VERX") == 2 && bim::faceFromLayer("ARM_NIZ") == 1);
}

TEST(rebar_import_from_kj_drawing) {
    const std::string dir = std::string(ASIACAD_EXAMPLES_DIR) + "/bino_qolip/";
    const auto sheets = civil::loadSheets({{dir + "fundament.dxf", "FOUND"}, {dir + "qavat_1_2.dxf", "PLAN", 1, 2}, {dir + "qavat_3_5.dxf", "PLAN", 3, 5}});
    const auto fp = civil::floorPlansFrom(sheets);
    bim::BuildingParams p;
    p.floors = 5;
    p.floorHeight = 3.3;
    const auto bm = bim::buildModel(fp.floors.at(1), p, fp);
    const auto kj = civil::loadSheet({dir + "kj_plita_1_2.dxf", "SLAB", 1, 2});
    CHECK(kj.error.empty());
    const auto spec = bim::readSpecification(*kj.doc);
    CHECK(spec.size() == 3);
    if (spec.size() == 3) {
        CHECK(spec[0].pos == 1 && spec[0].count == 61 && std::abs(spec[0].length - 18) < 1e-9);
        CHECK(spec[1].count == 95 && std::abs(spec[2].d - 0.016) < 1e-12);
    }
    const auto r = bim::importRebar({{kj.doc.get(), bim::ElemKind::Slab, 1, 2, "kj"}}, bm);
    CHECK(r.marks == 3);
    // 2 qavat × (61 + 91 + 13)
    CHECK(r.bars.size() == 2 * (61 + 91 + 13));
    int n1 = 0, n3 = 0;
    for (const auto& b : r.schedule) {
        if (b.pos == 1 && b.level == 1) n1 = b.count;
        if (b.pos == 3 && b.level == 2) n3 = b.count;
    }
    CHECK(n1 == 61 && n3 == 13);
    // z: 1-qavat plitasi +3.100..+3.300 — pastki to'r 25 mm himoya bilan, yuqori — ustidan
    double zb = 0, zt = 0;
    for (const auto& b : r.bars) {
        if (b.level == 1 && b.role == bim::BarRole::BottomX) zb = b.pts[0].z;
        if (b.level == 1 && b.role == bim::BarRole::TopX) zt = b.pts[0].z;
    }
    NEAR(zb, 3.1 + 0.025 + 0.006);
    NEAR(zt, 3.3 - 0.025 - 0.008);
    bool countErr = false;
    for (const auto& i : r.issues) countErr |= i.key == "rbi.chk.count" && i.args[0] == "2";
    CHECK(countErr);   // spetsifikatsiyada 95, chizmada 91
    CHECK(r.specMass > 0 && r.totalMass > 0);
    NEAR(r.specMass, 2 * (61 * 15.98 + 95 * 10.66 + 13 * 3.79));
}

// ---------- Yo'l ----------
static excel::Tr testTr();
TEST(road_example_corridor) {
    CHECK(civil::pk(0) == "PK0+00,00");
    CHECK(civil::pk(301.5) == "PK3+01,50");
    CHECK(civil::pk(1250) == "PK12+50,00");
    CHECK(civil::guessRoadLayer("YOL_OQ") == civil::RoadLayer::Axis);
    CHECK(civil::guessRoadLayer("ОСЬ_ДОРОГИ") == civil::RoadLayer::Axis);
    CHECK(civil::guessRoadLayer("LOYIHA") == civil::RoadLayer::Design);
    CHECK(civil::guessRoadLayer("РЕЛЬЕФ") == civil::RoadLayer::Ground);
    const auto r = loadAcad(std::string(ASIACAD_EXAMPLES_DIR) + "/yol.acad");
    const auto p = civil::defaultRoadParams();
    auto m = civil::buildRoad(r.doc, civil::autoRoadLayers(r.doc), p);
    NEAR(m.length, 600.0);
    CHECK(m.groundPoints > 50);
    CHECK(m.designPoints == 5);
    CHECK(m.st.size() >= 31);
    // loyiha belgilari: PK0 — 101, PK3 — 104
    NEAR(m.st.front().design, 101.0);
    bool at300 = false;
    for (const auto& s : m.st)
        if (std::abs(s.s - 300) < 0.01) { at300 = true; NEAR(s.design, 104.0); }
    CHECK(at300);
    // ko'ndalang kesim: 7 nuqta, qatnov qismi chetidan 2% pastga
    const auto& s0 = m.st.front();
    CHECK(s0.section.size() == 7);
    NEAR(s0.section[2].first, 3.5);
    NEAR(s0.section[2].second, s0.design - 3.5 * 0.02);
    CHECK(m.fillVolume > 0 && m.cutVolume > 0);
    CHECK(!m.pavementVolumes.empty());
    NEAR(m.pavementVolumes[0].second, 0.05 * 2 * (3.5 + 0.5) * 600);
    auto has = [&](const civil::RoadModel& mm, const char* key) {
        for (const auto& i : mm.issues) if (i.key == key) return true;
        return false;
    };
    CHECK(has(m, "road.chk.grade"));
    CHECK(has(m, "road.chk.nocurve"));
    // qo'lda tuzatish: PK4+50 belgisini 101,5 ga ko'tarish -> nishablik normada
    for (auto& s : m.st)
        if (std::abs(s.s - 450) < 0.01) s.design = 101.5;
    // oraliq piketlarni chiziqli qayta taqsimlash
    double z300 = 104, z450 = 101.5;
    for (auto& s : m.st)
        if (s.s > 300 && s.s < 450) s.design = z300 + (z450 - z300) * (s.s - 300) / 150;
        else if (s.s > 450) s.design = z450 + (98.0 - z450) * (s.s - 450) / 150;
    civil::recomputeRoad(m, p);
    CHECK(!has(m, "road.chk.grade"));
    // JSON
    civil::RoadModel m2;
    auto p2 = p;
    civil::roadFromJson(civil::roadToJson(m, p), m2, p2);
    CHECK(m2.st.size() == m.st.size());
    NEAR(m2.cutVolume, m.cutVolume);
    CHECK(!civil::drawRoad3D(m).empty());
    CHECK(!civil::drawRoadProfile(m, {0, -100, 0}, 10, 2, "g", "d", "pk").empty());
}

TEST(excel_road_roundtrip) {
    const auto tr = testTr();
    const auto r = loadAcad(std::string(ASIACAD_EXAMPLES_DIR) + "/yol.acad");
    auto p = civil::defaultRoadParams();
    auto m = civil::buildRoad(r.doc, civil::autoRoadLayers(r.doc), p);
    auto wb = xlsx::fromBytes(xlsx::toBytes(excel::roadWorkbook(m, p, tr)));
    CHECK(wb.sheets.size() == 4);
    // Excel'da: 2-piket loyiha belgisini o'zgartirish, polosa enini 3,75
    wb.sheets[0].rows[2][5] = xlsx::Cell::n(m.st[1].design + 0.4);
    for (auto& row : wb.sheets[2].rows)
        if (!row.empty() && row[0].text == "laneWidth") row[1] = xlsx::Cell::n(3.75);
    const double z1 = m.st[1].design;
    excel::roadFromWorkbook(wb, m, p, tr);
    NEAR(m.st[1].design, z1 + 0.4);
    CHECK(m.st[1].control);
    NEAR(p.cat.laneWidth, 3.75);
    NEAR(m.st[0].section[2].first, 3.75);
}

// ---------- Excel (.xlsx) ----------
TEST(xlsx_zip_inflate_roundtrip) {
    using namespace asiacad::xlsx;
    CHECK(colName(0) == "A" && colName(25) == "Z" && colName(26) == "AA" && colName(701) == "ZZ" && colName(702) == "AAA");
    CHECK(ref(2, 1) == "B3");
    CHECK(zip::crc32("123456789") == 0xCBF43926u);
    // qo'lda yasalgan "fixed Huffman" deflate: "abc" (python zlib.compress(b'abc')[2:-4])
    const std::string raw("\x4b\x4c\x4a\x06\x00", 5);
    CHECK(zip::inflate(raw) == "abc");
    Workbook wb;
    auto& s = wb.add("Sinov 'A' x");
    s.add({Cell::s("Nomi", 1), Cell::s("Qiymat", 1)});
    s.add({Cell::s("a & <b> ʻ"), Cell::n(1.25)});
    s.add({Cell::s("jami"), Cell::f("SUM(B2:B2)", 1.25)});
    const auto back = fromBytes(toBytes(wb));
    CHECK(back.sheets.size() == 1 && back.sheets[0].name == "Sinov 'A' x");
    CHECK(back.sheets[0].str(1, 0) == "a & <b> ʻ");
    CHECK(back.sheets[0].num(1, 1) && *back.sheets[0].num(1, 1) == 1.25);
    CHECK(back.sheets[0].at(2, 1)->kind == Cell::Formula && back.sheets[0].at(2, 1)->text == "SUM(B2:B2)");
    CHECK(back.sheets[0].column("Qiymat") == 1);
    // matnli son ("1,5") ham son sifatida o'qiladi
    Sheet t;
    t.add({Cell::s("1,5")});
    CHECK(t.num(0, 0) && std::abs(*t.num(0, 0) - 1.5) < 1e-12);
}

static excel::Tr testTr() {
    auto loc = std::make_shared<i18n::Localizer>(loadedLocalizer());
    loc->setLanguage("uz");
    return [loc](const std::string& k, const std::vector<std::string>& a) { return loc->tr(k, a); };
}

TEST(excel_sewer_roundtrip) {
    const auto tr = testTr();
    auto n = demoNet();
    n.segments[0].flowLs = 4;
    const auto r = civil::compute(n);
    auto wb = xlsx::fromBytes(xlsx::toBytes(excel::sewerWorkbook(n, r, tr)));
    CHECK(wb.sheets.size() == 5);
    CHECK(wb.sheets[0].name == tr("xl.sh.manholes", {}));
    // Excel'da tahrirlash: 2-quduq yer belgisi, 2-bo'lak nishabligi, yangi quduq qo'shish
    auto& m = wb.sheets[0];
    m.rows[2][3] = xlsx::Cell::n(100.1);
    m.rows.push_back({xlsx::Cell::s("K1-4"), xlsx::Cell::n(130), xlsx::Cell::n(0), xlsx::Cell::n(99.4)});
    wb.sheets[1].rows[2][4] = xlsx::Cell::n(0.009);
    civil::SewerNetwork n2 = n;
    excel::sewerFromWorkbook(wb, n2, tr);
    CHECK(n2.manholes.size() == 4 && n2.segments.size() == 3);
    NEAR(n2.manholes[1].rim, 100.1);
    NEAR(n2.segments[1].slope, 0.009);
    CHECK(n2.segments[2].specId == n2.segments[1].specId);  // yangi bo'lak — oxirgi quvur
    NEAR(n2.segments[0].flowLs, 4);
    // uzunlik formulasi Quduqlar varag'iga ishora qiladi
    CHECK(wb.sheets[1].at(1, 6)->kind == xlsx::Cell::Formula && wb.sheets[1].at(1, 6)->text.find("Quduqlar") != std::string::npos);
}

TEST(excel_rebar_roundtrip) {
    const auto tr = testTr();
    Document doc;
    auto e = std::make_unique<PolylineEntity>(std::vector<Vec3>{{0, 0, 0}, {0.4, 0, 0}, {0.4, 0.4, 0}, {0, 0.4, 0}}, true);
    e->setLayer("USTUN");
    doc.add(std::move(e));
    bim::BuildingParams p;
    p.floors = 3;
    auto layers = bim::autoLayerMap(doc);
    const auto bm = bim::buildModel(bim::readPlan(doc, layers), p);
    auto rs = bim::defaultRebarSettings();
    const auto rm = bim::buildRebar(bm, p, rs);
    auto wb = xlsx::fromBytes(xlsx::toBytes(excel::rebarWorkbook(p, layers, bm, rs, rm, tr)));
    const auto* v = wb.find(tr("xl.sh.sched", {}));
    CHECK(v && v->rows.size() == rm.schedule.size() + 2);
    // massa formulasi
    CHECK(v && v->at(1, 10)->kind == xlsx::Cell::Formula);
    // shablon: ustun 8 sterjen, qavatlar 7
    auto* t = const_cast<xlsx::Sheet*>(wb.find(tr("xl.sh.rtpl", {})));
    CHECK(t != nullptr);
    if (t)
        for (auto& row : t->rows)
            if (!row.empty() && row[0].kind == xlsx::Cell::Text && row[0].text == "COLUMN") row[18] = xlsx::Cell::n(8);
    auto* ps = const_cast<xlsx::Sheet*>(wb.find(tr("xl.sh.params", {})));
    if (ps)
        for (auto& row : ps->rows)
            if (!row.empty() && row[0].text == "floors") row[1] = xlsx::Cell::n(7);
    bim::BuildingParams p2 = p;
    auto layers2 = layers;
    auto rs2 = rs;
    excel::rebarFromWorkbook(wb, p2, layers2, rs2, tr);
    CHECK(rs2.byKind[bim::ElemKind::Column].nMain == 8);
    CHECK(p2.floors == 7);
    CHECK(layers2["USTUN"] == bim::ElemKind::Column);
    NEAR(rs2.byKind[bim::ElemKind::Slab].s1, rs.byKind[bim::ElemKind::Slab].s1);
}

TEST(kj_single_file_project) {
    // bitta faylda 5 ko'rinish: fundament (qolip+to'r), vypusklar, ustunlar, plita, plita qo'shimcha yuqori
    civil::SheetFile f;
    f.path = std::string(ASIACAD_EXAMPLES_DIR) + "/kj_loyiha/kj_loyiha.dxf";
    const auto ls = civil::loadSheet(f);
    CHECK(ls.error.empty());
    NEAR(ls.scaleUsed, 0.001);           // $INSUNITS = m, lekin matnlar 300 — mm
    CHECK(!ls.dims.empty());
    NEAR(ls.dims.front().lfac, 2.0);      // M200 uslubi
    const auto views = bim::detectKjViews(*ls.doc, ls.dims);
    CHECK(views.size() == 5);
    std::set<bim::KjViewKind> kinds;
    for (const auto& v : views) {
        kinds.insert(v.kind);
        CHECK(v.aligned);
        NEAR(v.scale, 2.0);
        CHECK(v.axesX.size() == 3 && v.axesY.size() == 2);
        if (v.axesX.count("3")) NEAR(v.axesX.at("3"), 12.0);   // chizmada 6000 mm × 2
    }
    CHECK(kinds.count(bim::KjViewKind::FoundMain) && kinds.count(bim::KjViewKind::Outlets) && kinds.count(bim::KjViewKind::Columns) &&
          kinds.count(bim::KjViewKind::SlabMain) && kinds.count(bim::KjViewKind::SlabExtraTop));
    bim::KjParams kp;
    const auto r = bim::buildKjProject(*ls.doc, ls.dims, kp);
    NEAR(r.slabTop, 3.0);
    NEAR(r.slabT, 0.2);
    CHECK(r.concrete == "B30");
    auto vol = [&](bim::ElemKind k) {
        for (const auto& row : r.bm.pto) if (row.kind == k) return row.volume;
        return -1.0;
    };
    NEAR(vol(bim::ElemKind::FoundationSlab), 14 * 8 * 0.4);
    NEAR(vol(bim::ElemKind::Slab), 14 * 8 * 0.2);
    NEAR(vol(bim::ElemKind::Column), 6 * 0.25 * (2.8 + 0.15));
    NEAR(vol(bim::ElemKind::Wall), 0.4 * 4 * 2.95);
    std::map<std::pair<int, long>, int> n;   // (rol, Ø mm) -> soni
    for (const auto& b : r.bars) n[{static_cast<int>(b.role) * 1000 + static_cast<int>(b.kind), std::lround(b.d * 1000)}]++;
    auto cnt = [&](bim::ElemKind k, bim::BarRole ro, long d) { return n[{static_cast<int>(ro) * 1000 + static_cast<int>(k), d}]; };
    CHECK(cnt(bim::ElemKind::FoundationSlab, bim::BarRole::BottomX, 16) == 40);   // 8 m / 0.2
    CHECK(cnt(bim::ElemKind::FoundationSlab, bim::BarRole::TopY, 16) == 70);      // 14 m / 0.2
    CHECK(cnt(bim::ElemKind::Slab, bim::BarRole::BottomX, 12) == 40);
    CHECK(cnt(bim::ElemKind::Slab, bim::BarRole::TopX, 16) == 60);                // 6 zona × 2 m / 0.2
    CHECK(cnt(bim::ElemKind::Slab, bim::BarRole::TopY, 16) == 60);
    // vypusklar fundament ko'rinishidan, ustunlar boshqa ko'rinishdan — o'qlar bo'yicha ustma-ust tushadi
    int outlets = 0, inCols = 0;
    for (const auto& b : r.bars)
        if (b.kind == bim::ElemKind::Column && b.role == bim::BarRole::Main) (b.level == 0 ? outlets : inCols)++;
    CHECK(outlets == 24 && inCols == 24);
    for (const auto& b : r.bars)
        if (b.kind == bim::ElemKind::Column && b.role == bim::BarRole::Main && b.level == 0) {
            NEAR(b.pts[0].z, kp.foundTop - kp.outletIn);
            NEAR(b.pts[1].z, kp.foundTop + kp.outletOut);
        }
    int stir = 0;
    for (const auto& b : r.bars) stir += b.role == bim::BarRole::Stirrup;
    CHECK(stir > 6 * 10);                    // "100/200" — uchlarida zichroq
    bool err = false;
    for (const auto& i : r.issues) err |= i.error;
    CHECK(!err);
    CHECK(r.totalMass > 1000);
}

TEST(kj_auto_scale_text_height) {
    BBox b;
    b.expand({0, 0, 0});
    b.expand({100, 100, 0});
    NEAR(civil::autoScale(6, b, 300), 0.001);   // $INSUNITS = m, lekin matn 300 — mm
    NEAR(civil::autoScale(6, b, 2.5), 1.0);
    NEAR(civil::autoScale(4, b, 2.5), 0.001);
    bim::BarMark m;
    CHECK(bim::parseBarMark("\xC3\x98" "16AIII, \xD1\x88.200", m));
    NEAR(m.spacing, 0.2);
    CHECK(m.cls == "A400");
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
