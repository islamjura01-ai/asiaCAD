# AsiaCAD — Phase 1: Drawing Engine (v0.13: Qt 6 grafik interfeys, 6 til, DXF, .acad, bloklar, HATCH, OSNAP, R-tree)

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
tests/                            81 test + GUI tutun testi; tests/data — ezdxf bilan yaratilgan DXF fixture'lar
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

## Quvurlar va kanalizatsiya (v0.10) — `src/civil/`

- **Katalog** (`PIPES [material]`): PVX SN4/SN8 (EN 1401, devor = OD/SDR), PP gofra SN8, PE100 SDR17/SDR11 (GOST 18599),
  asbest-sement BNT (GOST 1839-80 jadvali), po'lat (GOST 10704), cho'yan VChShG K9 (GOST ISO 2531, e = 9·(0.5+0.001·DN)),
  temir-beton TS (GOST 6482). Har bir o'lcham: tashqi Ø, devor, ichki Ø, massa kg/m, Manning n.
  `*` — namunaviy qiymat (PP, po'lat, temir-beton): loyihadan oldin standart/ishlab chiqaruvchi jadvali bilan solishtiring.
- **Shablonlar:** hovli PVX Ø160, ko'cha PVX Ø200, kollektor PVX Ø315 / PP Ø300, asbest-sement ta'mir, yomg'ir K2 temir-beton Ø500,
  bosimli PE100 Ø160, yo'l ostidan po'lat g'ilof. O'z shablonlaringiz `pipe_templates.json` ga saqlanadi (menyudan).
- **Hisob:** quduqlar zanjiri, quvur tubi belgilari (quduqda ustki yasovchi bo'yicha ulanadi), Manning bo'yicha to'liq va qisman
  to'lish (h/d, v), xandaq/qum to'shama/ko'mish hajmlari (taxminiy), quvur uzunligi va massasi, quduqlar soni.
- **Yomg'ir (K2) sarfi — SHNQ 2.04.22-25, 62–70-bandlar:** chegaraviy intensivlik usuli
  q_r = z_mid·A^1.2·F / t^(1.2n−0.1), A = q20·20^n·(1 + lgP/lg mr)^γ, q_cal = β·q_r; z — 5/6-jadval (yuzalar ulushi bo'yicha),
  β — 7-jadval, K — 4-jadval (F ≥ 500 ga), t < 10 min tuzatmasi (63-band), t = tcon + tcan + 0.017·Σl/v (11–13-formulalar).
  q20, n, mr, γ hudud bo'yicha 1-rasm va 2-ilovadan qo'lda kiritiladi (dasturda xarita yo'q). Quduqlarda F (ga) — qo'shiladigan maydon.
  Namuna: `examples/k2_yomgir.kan.json`.
- **Norma tekshiruvi** — SNiP 2.04.03-85 (eski norma, matn bilan solishtirilgan; har bir xabarda band):
  min diametr 2.33 (maishiy: ko'cha 200, hovli 150; yomg'ir: ko'cha 250, hovli 200),
  min nishablik 2.41 (Ø150 — 0,008, Ø200 — 0,007; asoslangan holda 0,007 / 0,005 — ogohlantirish; kattalari 1/D — amaliy qoida),
  max to'lish va min tezlik 2.34 / 16-jadval (yomg'ir P ≤ 0,33 yil — 0,6 m/s), max tezlik 2.36 (8/4, yomg'ir 10/7 m/s),
  ulanish burchagi ≥ 90° 4.5, shelyga bo'yicha ulash 4.7, chuqurlik 4.8 (quvur ustigacha ≥ 0,7 m, muzlash −0,3/−0,5 m),
  quduqlar oralig'i va o'lchami — SP 32.13330.2018 6.3.1 (35/50/75/100/150/200 m; D ≤ 600 — 1000 mm, D ≥ 700 — D+500).
  Xato — qizil qator.
- **Chizma** (`SEWER fayl.kan.json [x,y | -]`): plan (quduqlar, quvurlar material rangida, oqim strelkasi, yozuvlar) va bo'ylama
  profil (yer, quvur tubi/usti, quduqlar, jadval: belgilar, chuqurlik, nishablik ‰, masofa, quvur) — qatlamlar `K1_QUVUR`,
  `K1_QUDUQ`, `K1_YOZUV`, `K1_PROFIL`; bitta undo qadami.
- **GUI menyusi** ("Tarmoqlar" → "Kanalizatsiya", yoki Modul rejimida "Quvurlar"): bitta oynada shablon, tizim (K1/K2/K3/N),
  material va o'lcham, quduqlar jadvali (tanlangan 3D poliliniyadan — Z = yer belgisi), bo'laklar jadvali (har birida quvur
  tanlovi, nishablik, sarf), jonli tekshiruv, hajmlar, 3D: quvur (devor kesimi, raструб, gofra) va 3D: tarmoq.
  Namuna: `examples/k1_kocha.kan.json`.

## Monolit bino (v0.11) — `src/bim/building.*`

- Tayyor plan (DXF import yoki .acad) qatlamlari element turiga bog'lanadi; nomidan avtomatik taniladi:
  USTUN/КОЛОННА/COLUMN, DEVOR/СТЕНА/WALL, PLITA/ПЛИТА/SLAB, TOM/ПОКРЫТИЕ, TESHIK/ПРОЕМ, KONTUR, FUNDAMENT, LENTA, PADBETONKA.
  Ustun — yopiq poliliniya yoki aylana; devor — yopiq kontur yoki chiziq (qalinlik parametri); plita — kontur + teshiklar.
- Padbetonka → fundament plitasi (yoki lenta + ustun ostida yostiq) → har qavat ustun, devor, plita → tom plitasi.
  Ustun/devorlar plita ostigacha (ikki marta hisoblanmaydi).
- PTO: tur × qavat bo'yicha soni, beton m³, qolip m²; beton sinflari bo'yicha jami. `BUILDING [n] [H] [t] [x,y]` —
  jadvalni chizmaga (qatlam PTO). GUI: "Tarmoqlar → Monolit bino" (Modul rejimida "BIM"): qatlam xaritasi, parametrlar,
  3D model (qavatlar bo'yicha ko'rsatish), PTO, tekshiruv. Namuna: `examples/bino_plan.acs`.

## Armatura 3D (v0.12) — `src/bim/rebar.*`

- Bino modelidan fundamentdan tomgacha avtomatik karkas: fundament plitasi/lenta — pastki va yuqori to'r + stullar;
  ustun — bo'ylama sterjenlar + xomutlar (uchlarida zich), keyingi qavatga nahlyostka; devor — ikki yuza + shpilkalar;
  plita/tom — to'rlar kontur va teshiklar bo'yicha kesiladi. Savdo uzunligi 11,7 m dan oshsa nahlyostka qo'shiladi.
- Ведомость: pozitsiya, Ø, sinf, uzunlik, soni, massa; Ø bo'yicha jami; bog'lash simi, stul, shpilka.
- Tekshiruv — ShNQ 2.03.01-24 (22.07.2024 dan, QMQ 2.03.01-96 o'rniga), 8-bob:
  himoya qatlami (376, 378-b.), ustun Ø 12…40 (409, 411), sterjenlar orasi ≤ 400 (412), qo'shimcha xomut (428),
  xomut qadami Rsc bo'yicha 500/15d yoki 400/12d (423), nahlyostkada ≤ 10d (425), xomut Ø ≥ 0,25d va 5 mm (434),
  plita qadami 200 / 1,5h / 400 (415, 418), nahlyostka uzunligi 36-jadval + Rb (12-jadval) + Rs (21-jadval),
  minimal armaturalash 37-jadval × 2 (406, λ = H/i). A500C ShNQ'da yo'q — ogohlantirish, SP 63 qiymatlari; standart sinf A400.
- `REBAR [file.json] [x,y]` (uz ARMATURA, ru АРМАТУРА) — vedomostni chizmaga (qatlam ARM_VEDOMOST).
- GUI: "Tarmoqlar → Armatura" yoki "Monolit bino → Armatura (3D)": yon tomonda shablonlar paneli (12 ta tayyor +
  o'zingiz saqlaganlar), element turi parametrlari, 3D karkas (Ø bo'yicha rang, tur/qavat filtri, shaffof beton).

## Yon shablonlar paneli (v0.12)

Har bir katta menyuda chapda shablonlar paneli turadi (qidiruv, bo'limlar, ikki marta bosish — qo'llash):
kanalizatsiya — quvur shablonlari material bo'yicha; monolit bino — bino tiplari (karkas 9/16 qavat, ofis, kottej, maktab);
armatura — element turlari bo'yicha. Foydalanuvchi shablonlari AppData'da saqlanadi.

## Excel bilan hamnafas (v0.13) — `io/xlsx`, `civil/excel`

- O'z .xlsx yozuvchi/o'quvchimiz (tashqi kutubxonasiz: ZIP + inflate). Excel, LibreOffice, Google Sheets fayllari o'qiladi.
- Har bir menyuda **Excelga ↗ / Exceldan ↙**: jadvallar jonli formulalar bilan (uzunlik, massa, SUMIF/SUMIFS, hajmlar),
  sariq kataklar — tahrirlanadi. Bog'langan faylni Excelda saqlasangiz, menyu avtomatik yangilanadi.
- Kanalizatsiya (quduqlar, bo'laklar, hajmlar, parametrlar), bino (parametrlar, qatlamlar, PTO), armatura (vedomost,
  diametrlar, shablonlar), yer osti tarmoqlari (tarmoqlar, nuqtalar Z, kesishmalar), yo'l (piketlar, hajmlar).
- `EXCEL SEWER|BUILDING|REBAR|UTIL|ROAD kirish chiqish` — .xlsx chiqishi eksport, .xlsx kirishi import.

## Yer osti tarmoqlari 3D (v0.13) — `civil/utility`

- Plan chizmasidan: qatlam -> tur (suv, kanalizatsiya, yomg'ir, gaz, issiqlik, elektr, aloqa, drenaj, yer sathi).
- Yozuvlar: Ø200, %%c110, DN300, Ду150 — diametr; h=1,80 — quvur ustigacha chuqurlik; 98,45 / ▽98.45 / отм. 98.45 — belgi
  (o'z oqimli — quvur tubi, boshqalar — o'q); 3D poliliniya Z si; yer — belgi yozuvlari va 3D nuqtalar (IDW).
- Xatolar: diametr/belgi yo'q (standart olindi), uzilish, kesishmalarda vertikal oraliq (0,2 / kabel 0,5 m),
  quvur ustidagi qatlam, teskari nishablik. Qo'lda tuzatish: jadvalda (tur, Ø, har uch Z) yoki Excelda.
- `UTIL` — tahlil, `UTIL +` — 3D_* qatlamlarga chizish. Namuna: `examples/yer_osti.acs`.

## Yo'l 3D (v0.13) — `civil/road`

- O'q (poliliniya, yoylar bilan) + yer belgilari + loyiha belgilari (▽101,20) -> piketlar, ko'ndalang profil
  (polosalar, yo'l yoqasi, qiyalik 1:m yergacha), 3D koridor, bo'ylama profil.
- Toifa shablonlari I-a…V, yo'l to'shamasi shablonlari; to'kma/kesma hajmlari, qoplama qatlamlari, asfalt m².
- Tekshiruv (SNiP 2.05.02-85 asosida, tasdiqlanmagan): bo'ylama nishablik, egri radiusi, egrisiz burilish, baland to'kma.
- `ROAD` — tahlil, `ROAD + x,y` — 3D va profil. Namuna: `examples/yol.acs`.

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
