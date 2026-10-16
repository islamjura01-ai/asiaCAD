# AsiaCAD — Phase 1: Drawing Engine (v0.9: Qt 6 grafik interfeys, 6 til, DXF, .acad, bloklar, HATCH, OSNAP, R-tree)

C++20, tashqi bog'liqliksiz. CMake 3.20+, GCC 13 / Clang 16 / MSVC 2022.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build          # yoki ./build/asiacad_tests
./build/asiacad                 # interaktiv
./build/asiacad examples/demo.acs -o demo.dxf
./build/asiacad --lang ru        # uz | ru | kk | ky | tr | en  (yoki ASIACAD_LANG)
./build/asiacad-gui examples/uchastka.acad --mode classic   # grafik interfeys: classic | module | focus
# buyruq satrida:  OPEN chizma.acad | SAVE [fayl] | NEW | IMPORT chizma.dxf
#                  BLOCK nom p sel | INSERT nom p [k] [°] | EXPLODE sel | HATCH sel [naqsh] [k] [°]
#                  LINE END:9.8,0.2 CEN:24,1 | OSNAP p [rejimlar] [r] | SELECT WINDOW:x1,y1,x2,y2
```

## Tuzilma

```
include/asiacad/core/math.h       Vec3, BBox, Transform (move/rotate/scale/mirror)
include/asiacad/core/entity.h     IEntity, ICurve
include/asiacad/core/entities.h   Point, Line, Circle, Arc (3 nuqta), Polyline (2D/3D, bulge), Text (tekislash)
include/asiacad/core/entities.h   + InsertEntity (affin matritsa, atributlar, explode), HatchEntity (teshikli konturlar)
include/asiacad/core/block.h      BlockDef — o'zgarmas, shared_ptr orqali ulashiladi
include/asiacad/core/document.h   Document, Layer (on/off, lock, freeze), blok jadvali
include/asiacad/core/commands.h   ICommand, Add/Delete/Transform/Copy/SetLayer/ChangeProps,
                                  CompositeCommand (atomar), CommandStack (undo/redo, dirty)
include/asiacad/core/geom2d.h     Kesma/yoy primitivlari: kesishish, eng yaqin, perpendikulyar, urinma, quti bilan kesishish
include/asiacad/core/spatial_index.h  R-tree (STR), Document ichida inkremental sinxronlanadi
include/asiacad/core/snap.h       OSNAP (10 rejim), Window/Crossing/Pick tanlash
include/asiacad/core/error.h      Error(kalit, argumentlar) — core matn chiqarmaydi
include/asiacad/i18n/             utf8 (case folding), json parser, Localizer
i18n/*.json                       uz, ru, kk, ky, tr, en — buyruqlar, kalit so'zlar, xabarlar
include/asiacad/io/dxf_writer.h   DXF R12 eksport (kod sahifasi avtomatik, sig'maganlari \U+XXXX)
include/asiacad/io/dxf_reader.h   DXF import R12–R2018, bitta undo qadami
include/asiacad/io/acad_file.h    Native .acad (JSON v1): atomar saqlash + .bak, versiya nazorati
include/asiacad/io/dxf_text.h     ANSI_1251/1252/1254/1250, DOS866, \U+, %%-kodlar, MTEXT formatlash
apps/cli/                         buyruq interpretatori (6 til, Localizer orqali)
tests/                            72 test + GUI tutun testi; tests/data — ezdxf bilan yaratilgan DXF fixture'lar
```

## Dizayn qarorlari

- **Barcha o'zgarishlar ICommand orqali.** Document'ni to'g'ridan-to'g'ri o'zgartiradigan UI kodi bo'lmasligi kerak — aks holda undo buziladi.
- **Undo snapshot asosida** (TransformCommand entity nusxasini saqlaydi). Teskari matritsa ishlatilmaydi: suzuvchi nuqta xatosi to'planmaydi.
- **ID barqaror**: undo/redo'dan keyin entity o'sha ID va chizilish tartibida qaytadi.
- **Qulflangan/muzlatilgan qatlam** tahrirni bloklaydi; muvaffaqiyatsiz buyruq tarixga tushmaydi.
- **DXF R12** — handle talab qilmaydi, AutoCAD/Civil 3D/BricsCAD/QGIS o'qiydi. Koordinatalar 10 kasr xonagacha (UTM/GSK-2011 kattaliklarida aniqlik yo'qolmaydi).
- **Polyline** 3D uchlarni saqlaydi; `length()` fazoviy, `length2d()` gorizontal.
- **DIST** geodezik azimut beradi (shimoldan soat yo'nalishida).

## Tillar (i18n)

- Lug'at = bitta JSON fayl. Yangi til qo'shish uchun kod o'zgarmaydi — `i18n/xx.json` qo'shiladi.
- **Buyruq istalgan tilda yoziladi**, javob faol tilda chiqadi: interfeys o'zbekcha bo'lsa ham `КРУГ`, `çember`, `ШЕҢБЕР`, `C` ishlaydi.
- Solishtirish `foldKey` orqali: katta-kichik harf, turkcha I/İ/ı, apostrof (oʻ/o'/o‘) farqi yo'q.
- Buyruq va kalit so'zlar alohida nomlar fazosida (`C` buyruq sifatida CIRCLE, kalit sifatida CLOSE).
- Testlar har bir lug'atda kalitlar to'liqligini va tillararo taxallus ziddiyatlarini tekshiradi.
- `--i18n papka` bo'lmasa: dastur yonidagi `i18n/` → joriy papka → manba papkasi.

## DXF import

| Qo'llab-quvvatlanadi | Izoh |
|---|---|
| LINE, POINT, CIRCLE, ARC | OCS: ±Z aniq; boshqa normal — 3D poliliniya |
| LWPOLYLINE, POLYLINE 2D/3D | bulge (yoy segmentlar) saqlanadi, maydon/uzunlik aniq |
| TEXT, ATTRIB | tekislash (72/73/74), %%c %%d %%p |
| MTEXT | formatlash tozalanadi, har paragraf alohida TEXT |
| ELLIPSE, SPLINE (NURBS, vaznli) | poliliniyaga bo'linadi (≤5°); ratio=1 ellips — aylana/yoy |
| BLOCK, INSERT, MINSERT | blok ta'riflari saqlanadi (ichma-ich), INSERT — to'liq matritsa, ATTRIB — atribut; `explodeInserts` opsiyasi |
| HATCH | poliliniya va qirra yo'llari (chiziq, CW/CCW yoy, ellips, splayn), teshiklar, naqsh nomi/masshtab/burchak |
| DIMENSION | o'lcham bloki geometriyasi |
| 3DFACE, SOLID, TRACE | yopiq poliliniya |
| Kodlash | R2007+ UTF-8; oldin $DWGCODEPAGE; HEADER'siz fayl — UTF-8 avtoaniqlash |

Hozircha yo'q (hisobotda "o'tkazib yuborildi"): LEADER/MLEADER, mesh/polyface, IMAGE, 3DSOLID, binar DXF, DWG.
Notekis masshtabli blokdagi aylana/yoy — poliliniya (ogohlantirish bilan). Paper space — o'tkazib yuboriladi.
21 MB / 120 ming obyektli fayl ≈ 0.3 s.

## Native format (.acad)

- JSON, `"version": 1`; bitta obyekt — bitta qator: git diff va qo'lda tekshirish qulay, Python/JS darhol o'qiydi.
- Sonlar `std::to_chars` bilan eng qisqa aniq ko'rinishda: saqlash→ochish bit-bit bir xil (UTM koordinatalari ham).
- ID'lar va `nextId` saqlanadi — keyingi modullar (Survey, Road) obyektlarga ID orqali havola qila oladi.
- Atomar saqlash: `fayl.tmp` → almashtirish, eski versiya `fayl.bak`. Uzilish faylni buzmaydi.
- Moslik: noma'lum maydonlar e'tiborsiz; noma'lum obyekt turi — ogohlantirish bilan o'tkaziladi; yangiroq `version` — xato.
- CLI: `SAVE` (.acad — asosiy, .dxf — eksport, nomsiz — joriy fayl), `OPEN` (.acad yoki .dxf yangi chizma sifatida),
  `NEW`; saqlanmagan o'zgarish bo'lsa OPEN/NEW/EXIT rad etadi — oxiriga `!` qo'shilsa majburan.
- 120 ming obyekt: saqlash ~0.1 s, ochish ~0.3 s, 18 MB.

## Bloklar va HATCH

- **BlockDef** yaratilgandan keyin o'zgarmaydi; barcha INSERT'lar bitta ta'rifga `shared_ptr` orqali ishora qiladi — 10 ming daraxt belgisi xotirada bitta.
- **INSERT joylashuvi — to'liq affin matritsa.** Burilish, ko'zgu (manfiy masshtab), notekis masshtab va ichma-ich transformlar aniq saqlanadi.
  DXF'ga `R·S` ko'rinishiga ajratib yoziladi; qiyshiq (skew) bo'lsa — explode qilib.
- **Meros:** blok ichidagi "0" qatlam va BYBLOCK rang INSERT'nikini oladi (AutoCAD qoidasi) — explode va chizishda.
- **Notekis masshtabli blokdagi aylana** explode'da ellipsga yaqin poliliniya bo'ladi; INSERT o'zi aniq qoladi.
- **HATCH maydoni** even-odd qoidasi bilan: teshik ichidagi orol yana qo'shiladi. Naqsh chizish — viewport bosqichida.
- DXF R12'da HATCH yo'q — eksportda konturlar yopiq POLYLINE bo'lib yoziladi; `.acad`da to'liq saqlanadi.
- Import qilinayotgan blok nomi hujjatdagisi bilan to'qnashsa — `NOM$1` deb qayta nomlanadi.

## OSNAP va tanlash

- **10 rejim:** END, MID, CEN, QUA, INT, PER, TAN, NEA, NOD, INS — blok (INSERT) ichidagi geometriyada ham ishlaydi.
  Ustuvorlik AutoCAD'dagi kabi: aniq nuqtalar masofa bo'yicha, NEA eng oxirida. PER/TAN oldingi kiritilgan nuqtadan hisoblanadi.
- Barcha egri chiziqlar ikki primitivga keltiriladi — kesma va yoy (bulge segmentlar ham), shuning uchun kesishish va bog'lanish hamma obyekt juftligida bir xil ishlaydi.
- **CLI'da nuqta kiritish:** `REJIM:x,y` — masalan `CHIZIQ OXIR:9.8,0.3 MARKAZ:24,1` (rejim nomlari 6 tilda). Radius `OSNAP p r` bilan o'rnatiladi.
- **Tanlash:** `WINDOW:x1,y1,x2,y2` — to'liq ichidagilar; `CROSSING:...` — geometriyasi tegadiganlar (faqat bbox emas: qiyshiq chiziq aniq tekshiriladi);
  `PICK:x,y[,r]` — eng yaqin obyekt. `SELECT` natijasi `LAST` bo'ladi — keyingi buyruqda ishlatiladi.
- **R-tree:** 120 ming obyekt — qurish ~30 ms, so'rov ~1.5 µs. Tahrirlardan keyin to'liq qayta qurilmaydi:
  o'zgargan obyektlar alohida ro'yxatda turadi, ular ko'payganda (n/8 dan oshsa) daraxt qayta quriladi. Testlar natijani brute-force bilan solishtiradi.

## Keyingi qadamlar

1. Chop etish (PLOT, A4–A0, masshtab), qatlam chiziq turlari, CHAMFER, poliliniya FILLET, yoyli uchni EXTEND
2. Phase 2: Survey/Terrain — nuqtalar importi (CSV/GSI/SDR), TIN, gorizontallar, hajmlar

## Grafik interfeys (Qt 6) — `asiacad-gui`

Qt 6.4+ topilsa avtomatik yig'iladi (`-DASIACAD_BUILD_GUI=OFF` — o'chirish). Ubuntu: `apt install qt6-base-dev`,
Windows: Qt Online Installer (MSVC 2022 64-bit) va `cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.x/msvc2022_64`.

```
apps/gui/main_window.*     asosiy oyna: lenta, modullar paneli, suzuvchi panel, qatlamlar, xossalar, buyruq satri
apps/gui/viewport.*        chizma oynasi (QOpenGLWidget + QPainter): zoom/pan, OSNAP markerlari, tanlash, asboblar
apps/gui/theme.*           uch rejim mavzusi + ACI ranglar jadvali
apps/gui/icons.*           vektor ikonkalar (resurs fayllarisiz)
apps/gui/command_palette.* Ctrl+K — buyruqni istalgan tilda qidirish
tests/gui_smoke.cpp        ekransiz tutun testi (ctest: gui)
```

- **Uch rejim bitta dasturda** (status satrida tanlanadi): Klassik (Samarqand lentasi), Modul (Ipak yo'li — chapda modullar,
  yuqorida kontekst asboblari), Fokus (Tungi Toshkent — chizma butun ekranda, suzuvchi panel, Ctrl+K).
- **Yagona yo'l:** har bir GUI amali buyruq satri bilan bir xil buyruqqa aylanadi (`LINE 0,0 10,10`) va `Interpreter`
  orqali bajariladi — undo/redo, qatlam qulfi va xato xabarlari CLI bilan bir xil. Buyruq tarixi pastda ko'rinadi.
- **Sichqoncha:** g'ildirak — kursor atrofida zoom; o'rta tugma yoki Space+chap — pan; o'ng tugma — Enter.
  Tanlash: bosish (Shift — qo'shish/olib tashlash), ramka chapdan o'ngga — WINDOW, o'ngdan chapga — CROSSING.
- **Asboblar:** chiziq, poliliniya (birinchi nuqtaga bosish — yopish), aylana, 3 nuqtali yoy, nuqta, matn,
  ko'chirish, nusxa, burish, o'chirish (Delete). Nuqta OSNAP bilan olinadi; 10 rejim status satrida yoqiladi.
- **Klaviatura:** chizmada harf terilsa buyruq satriga o'tadi; asbob faol bo'lsa `x,y` yoki `@dx,dy` nuqta sifatida
  qabul qilinadi; ↑/↓ — buyruqlar tarixi; Esc — bekor qilish; F7 — to'r; Ctrl+Z/Y, Ctrl+N/O/S.
- **Til** status satrida almashadi — interfeys, buyruq nomlari va xabarlar birga o'zgaradi.
- **Tutqichlar (grips, v0.8):** tanlangan obyekt uchlarida ko'k kvadratlar (ustida — yashil, faol — qizil). Tutqichni bosib
  qo'yib yuborish yoki bosish → yangi nuqtani bosish. Chiziq: uchlar + o'rta (siljitish); aylana: markaz + 4 kvadrant (radius);
  yoy: markaz, boshi, o'rtasi, oxiri; poliliniya/shtrix: uchlar (bulge saqlanadi); o'lcham: p1, p2, joyi.
  Har bir tahrir `GRIP id n x,y` buyrug'i — obyekt ID'si va chizish tartibi o'zgarmaydi, undo bitta qadam.
- **ORTO (F8) va QUTB (F10):** orto — faqat gorizontal/vertikal; qutb — 15° qadamli nurga tortadi (ekranda ~10 px).
  Nuqtali kuzatuv nuri va `QUTB: 14.142 < 45°` yorlig'i chiqadi. OSNAP doim ustun. Ikkalasi bir vaqtda yoqilmaydi.
  **To'g'ridan-to'g'ri masofa:** asbob faol bo'lsa buyruq satriga bitta son (`25`) — nur yo'nalishida shu masofadagi nuqta.
- **O'lchamlar (GOST 2.307 / SPDS):** chiziqli (joyiga qarab gorizontal/vertikal), parallel, radius (R), diametr (Ø).
  45° serif, chiqarish chiziqlari o'lcham chizig'idan o'tadi, matn chiziq ustida va o'qiladigan tomonga buriladi;
  radius/diametrda strelka. O'nli ajratgich — vergul; butun qiymat kasrsiz. Buyruqlar:
  `DIMLINEAR p1 p2 joy [h] [kasr]`, `DIMALIGNED ...`, `DIMRADIUS sel joy [h]`, `DIMDIAMETER sel joy [h]`
  (uz: OʻLCHAM, OʻLCHAMPAR, OʻLCHAMRAD, OʻLCHAMDIA; ru: РЗМЛИНЕЙНЫЙ, РЗМПАРАЛ, РЗМРАДИУС, РЗМДИАМЕТР).
  .acad'da `DIMENSION` sifatida saqlanadi (tahrirlanadi), DXF R12'ga tayyor chiziq/matn/strelka bo'lib yoziladi.
  GUI matn balandligini chizmadagi oxirgi o'lchamdan oladi (bo'lmasa — joriy masshtabdan).
- **Tahrir (v0.9):** `OFFSET d sel p` — parallel nusxa (poliliniya: to'g'ri burchaklar kesishguncha, yoylar konsentrik;
  radiusdan katta bo'lsa xato); `TRIM chegaralar p ...` — eng yaqin ikki kesishish orasini olib tashlaydi
  (aylana → yoy, o'rtasidan — 2 bo'lak, ID saqlanadi); `EXTEND chegaralar p ...` — yaqin uchni birinchi chegaragacha
  (chiziq, yoy, ochiq poliliniyaning to'g'ri uchi); `FILLET p1 p2 [r]` — ikki chiziq, r=0 — o'tkir burchak, radius eslab qolinadi.
  Har biri bitta undo qadami. GUI: TRIM/EXTEND "tezkor" rejimda (barcha ko'rinadigan obyektlar chegara), olib tashlanadigan
  qism qizil punktir, OFFSET/EXTEND/FILLET natijasi oldindan ko'rinadi; masofa/radius — buyruq satriga son yozish.
  Lenta "Tahrir" guruhida kichik tugmalar; o'lchamlar alohida "O'lcham" varag'ida; lenta butun oyna kengligida.
- Skrinshot/sinov rejimi: `asiacad-gui fayl.acad --mode focus --lang ru --tool line --point 0,0 --cursor 10,5 --screenshot out.png`
  (`--select 8 --grip 8,3 --polar --ortho --value 3` ham bor).
