# AsiaCAD — Phase 1: Drawing Engine (v0.24: Qt 6 grafik interfeys, 6 til, DXF, .acad, bloklar, HATCH, OSNAP, R-tree)

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

## Alohida chizmalarni import qilish (v0.14) — `civil/drawset`, `bim/rebar_import`

Har bir menyuda **"Chizmalar"** paneli: bir nechta DXF/.acad fayl, har biriga vazifa, qavat oralig'i va birlik
(mm/sm/m — avto: $INSUNITS yoki chizma o'lchami bo'yicha).

- **Yer osti tarmoqlari:** suv, kanalizatsiya, yomg'ir, gaz... — har biri o'z faylida, qatlam nomlari ixtiyoriy
  (vazifa fayl bo'yicha beriladi), rel'ef alohida faylda. Kesishmalar va to'qnashuvlar **fayllar orasida** tekshiriladi;
  yozuvlar faqat o'z fayli chiziqlariga biriktiriladi. Namuna: `examples/tarmoqlar/`.
- **Monolit bino:** fundament plani + har qavat (yoki qavatlar oralig'i) uchun qolip chizmasi. Plita qalinligi
  "h=200"/"δ=200" yozuvidan, plita usti belgisi "+3.300" dan; fundament aniq konturi va belgisi o'z planidan.
  Tekshiruv: yuqori qavat ustuni ostida tayanch yo'q, qavat belgilari. Namuna: `examples/bino_qolip/`.
- **Armatura (KJ):** "Armatura manbai — KJ chizmalaridan". Markirovka ("Поз.1 Ø12 A500C шаг 200", "4Ø16 А-III L=3450",
  "верх/низ"), armatura qatlamidagi sterjen, taqsimlash chizig'i (soni = uzunlik / qadam + 1), spetsifikatsiya jadvali
  ("Поз." | "Наименование" | "Кол." | "Масса"). 3D sterjenlar shu qavat plitasida (pastki/yuqori, himoya qatlami bilan).
  Taqqoslash: chizmadagi soni ↔ spetsifikatsiya, Ø mosligi, umumiy massa. Namuna: `examples/bino_qolip/kj_plita_1_2.dxf`.

## Koridor: yig'malar va qismlar (Civil 3D Corridor kabi) — `civil/corridor`, `CORRIDOR`

Bitta oynada: chapda palitra (vertikal yorliqlar: Yig'malar, Asosiy, Polosalar, Yo'l yoqasi, Ajratuvchi, Bordyur,
Qiyalik, Kanal/lotok, Transheya, Devor), yonida yig'ma muharriri (chap/o'ng ro'yxat, ↑ ↓ ✕, parametrlar jadvali, kesma/
to'kmada jonli ko'rinish), o'rtada 3D | Kesimlar (piket bo'yicha, materiallar rangda) | Profil, o'ngda Hajmlar.
- Qismlar: polosa (3 qatlam qoplama), yo'l yoqasi, bordyur, trotuar, ajratuvchi, oddiy chiziq, qiyalik yergacha,
  kyuvet+qiyalik, bermali qiyalik, ariq, kanal (trapetsiya/to'rtburchak, qoplama, qirg'oq), parabolik lotok (yostiq bilan),
  quvur transheyasi (to'shama, qayta ko'mish), tirgak devor.
- 13 tayyor yig'ma: yo'llar (II, III, IV toifa, magistral, shahar ko'chasi, tirgak devorli), kanallar (trapetsiya,
  to'rtburchak, ariq, lotok, damba ichida), transheyalar. Qism bosilsa — qiyalikdan oldin qo'shiladi.
- Aniqlik: qiyalik yer bilan bisection'da kesishadi; kesma/to'kma maydoni tanaffus nuqtalari bo'yicha aniq integral;
  hajmlar ikki usulda — o'rtacha maydon (Civil 3D) va aniq: prizmoidal (o'rta kesim, Simpson) + egri chiziqda Pappus
  tuzatmasi. Sinov: R=100 m yoyda aniq usul 0.5% ichida, o'rtacha maydon ~5% kam.
- Kanal/lotok: Q berilsa Manning bo'yicha suv sathi; suv yo'lida teskari nishablik — xato. Balandliklar absolyut.
- CLI: `CORRIDOR [fayl.json] [ASM=n] [+]`, `EXCEL CORRIDOR`. Excel: piketlar (loyiha belgisi), hajmlar, yig'ma
  parametrlari ("R1.LANE.w") — tahrirlab qaytarish mumkin.

## Suv yo'li: quvurlar, beton kanal, ariq, lotok (v0.15) — `civil/waterway`, `WATERWAY`

Plan chizmasidan (o'q — KANAL/ARIQ/LOTOK/ВОДОВОД qatlami; yer — rel'ef qatlami, 3D nuqtalar/gorizontallar; tub belgilari
o'q yonida: "дно 485.20", "Л 485,20", "▽485,20", "tub", "taban") 3D model **absolyut belgilarda**.
- Turlar: bosimli quvur (Hazen-Williams, pyezometrik chiziq, manfiy bosim), o'zi oqar quvur (Manning, to'lish),
  beton kanal (to'g'ri to'rtburchak/trapetsiya), tuproq ariq, parabolik lotok (ЛР — o'lchamlar taxminiy, katalogdan tekshiring).
- Normal chuqurlik, tezlik, Froud, o'tkazish qobiliyati (zaxira bilan); qazish, damba, beton qoplama, lotok elementlari.
- Tekshiruv (SNiP 2.06.03-85 asosida, tasdiqlanmagan): sig'maslik, zaxira, loyqa bosish/yuvilish tezliklari, teskari
  nishablik, kritik rejim, quvur ustidagi qatlam; yer belgilari yo'q bo'lsa — xato.
- GUI "Suv yo'li": kesim shablonlari, 3D, bo'ylama profil, piketlar (tub qo'lda tuzatiladi — oraliqlar qayta
  interpolyatsiya), Excel bilan bog'langan. CLI: `WATERWAY [fayl.json] [Q=0.5] [SEC=n] [+] [x,y]`, `EXCEL WATERWAY`.
  Namuna: `examples/suv_yoli.acs` (ataylab teskari nishablik bilan).

## Absolyut balandliklar (Boltiq) — barcha 3D modellarda

Syomka tahlili bilan bir tizimda ishlash uchun 3D model balandliklari **absolyut**:
- **Bino / KJ:** chizmadagi izohdan "За относительную отметку ±0,000 по ГП 484,20 принят уровень верха фундаментной плиты"
  (shuningdek "±0.000 = 484.200 абс.", "0,000 соответствует абс. 101,35", turkcha "kotu") — ±0.000 ning absolyut belgisi va
  u nimaga tengligi (fundament usti / toza pol). Prizmalar va sterjenlar z = nisbiy + datum. Topilmasa — **xato**
  ("balandliklar nisbiy"), qiymat qo'lda kiritiladi; chizmadagidan farq qilsa — ogohlantirish.
- **KJ plan XY:** o'qlar bo'yicha lokal; geodezik bog'lash — "1"/"А" kesishmasi X, Y va burilish burchagi.
- **Yer osti tarmoqlari / yo'l:** yer nuqtalari, quduq belgilari yoki 3D Z bo'lmasa — **xato** (shartli yer sathi
  ishlatilganini aniq ko'rsatadi).

## Turkiya/NetCAD kanalizatsiya loyihasi (v0.15)

"ATIKSU UYGULAMA" kabi NetCAD planlari to'g'ridan-to'g'ri o'qiladi: `ET1_ASU_Borular` (quvur, bir nechta parallel chiziq →
bitta o'q), `Ana_Baca`/`Ara_Baca` (quduq aylanalari), `BACA ETİKETLERİ` (qopqoq / nom "070A" / tub belgilari, yo'naltiruvchi
chiziq bo'ylab), `Boru Etiketleri` ("48.1m", "1:365", "Ø500", "MBB"/"PE-BA"). Yozuvsiz quduqlarda tub belgisi tarmoq bo'yicha
baholanadi; yozuvdagi nishablik ↔ quduq belgilaridan hisoblangan nishablik solishtiriladi. Fayl roli (SEWER) faqat nomidan
tarmoq ekani tanilgan qatlamlarga qo'llanadi — genplan/yo'l xreflari quvurga aylanmaydi. Katta jadvallar bir martada
to'ldiriladi (600+ quvur — soniyalarda).

## Bitta faylda KJ loyiha: qolip + armatura (v0.15) — `bim/kj_project`

Haqiqiy KJ fayllarda (masalan "1-43B-9 КЖ ТИП 3") barcha varaqlar model fazosida yonma-yon, har biri o'z masshtabida.
AsiaCAD faylni o'zi tahlil qiladi — rol berish shart emas ("Armatura 3D" → KJ chizmalaridan → faylni qo'shish):

- **Birlik:** $INSUNITS = m bo'lsa ham matn balandligi (300…500) bo'yicha mm deb aniqlanadi.
- **Ko'rinishlar:** o'q pufakchalari (aylana ichida "1", "11Р", "А", "Б/1") qator/ustunlar bo'yicha guruhlanadi;
  kesim va tugunlardagi pufakchalar chetga qoldiriladi. Sarlavha — yaqin yirik matn ("Опалубочный план ... фундаментной
  плиты", "Схема расположения выпусков", "... плиты перекрытия в нижней/верхней зоне", "колонн и монолитных стен").
- **Masshtab:** ko'rinishdagi o'lchamlar uslubining DIMLFAC'i (M200 → 2: chizmada 3000, haqiqatda 6000).
- **Tekislash:** "1" o'qi X, "А" o'qi Y — barcha ko'rinishlar bitta koordinatada (vypusklar ustunlar ichiga tushadi).
- **Beton:** fundament konturi, ustun/devor to'rtburchaklari ("Приямок" yozuvlilari chiqariladi), plita konturi;
  plita belgilari jufti (+3.050 / +2.850 → 0.20 m), beton sinfi "Бетон кл.В30" dan. Fundament usti va qalinligi —
  parametr (kesimdan tekshiring).
- **Armatura:** asosiy to'r "Ø16AIII, ш.200" (yuqori + pastki, X + Y, kontur bo'yicha kesilgan, 11.7 m dan uzunlarga
  nahlyostka); "d 20"/"d22" qatlamlaridagi chiziqlar — zona to'rtburchagi bo'ylab qadam bilan qo'shimcha sterjenlar;
  aylanalar — vertikal sterjenlar (fundamentda shpilkalar, vypusklar 640 + 960); ustun karkaslari — vypusklar joyida
  asosiy sterjenlar va xomutlar ("100/200").
- CLI: `KJ fayl.dxf [fundament usti] [qalinligi]`. Namuna: `examples/kj_loyiha/` (`make_kj.py` — generator).

## Har menyuda uch yo'l: 0 dan chizish · import · tuzatish (v0.16) — `civil/generators`

Har bir modul (Bino, Yo'l, Suv yo'li, Koridor, Yer osti) oynasining pastida **✚ 0 dan** tugmasi bor. Kiritish oynasi
jonli plan bilan ochiladi; "Chizmaga" bosilsa generator buyrug'i chizmaga AsiaCAD importi taniydigan obyektlarni yozadi
(undo bilan), modul shu chizmani xuddi import qilingan fayl kabi o'qiydi. Shuning uchun 0 dan chizilgan, boshqa dasturdan
import qilingan va AsiaCAD tayyorlagan chizma — bitta yo'ldan o'tadi va bir xil tuzatiladi.

| Buyruq | Nima chizadi | Keyin o'qiydi |
|---|---|---|
| `GRID 6x5,4.5 6x3 [0.5x0.5] [x,y]` | o'qlar to'ri (1, 2… / А, Б…), ustunlar, tashqi devorlar, plita konturi | `BUILDING` |
| `ALIGN file.json` | o'q: burilish nuqtalari (X, Y, R — yoy), piketlar, loyiha belgilari | `ROAD`, `WATERWAY`, `CORRIDOR` |
| `UTILNET file.json` | quduqlar (nom, qopqoq, tub), quvurlar (Ø, i=, uzunlik yozuvlari) | `UTIL` |
| `CITY [nx ny] [5-16] [x,y]` | ko'chalar (o'q + qatnov qismi), kvartallar, binolar, 3D massiv | `ROAD`/`CORRIDOR` (ko'cha o'qlari) |

* **Tuzatish**: Yo'l, Suv yo'li, Koridor, Yer osti oynalarida "Tekshiruv" bandini bossangiz — chizmada o'sha joy ko'rsatiladi.
  Element chizmada o'zgartiriladi, modul qayta o'qiydi; jadvallarda belgilar qo'lda tuzatiladi.
* **Shahar mavzesi** (yangi menyu): 6 shablon (kam qavatli → 300 kvartalli yangi shahar), plan va 3D, ko'rsatkichlar
  (hudud, qurilish izi, umumiy maydon, zichlik, ko'chalar km). Bino asosi chizmadagi yer belgilaridan (absolyut).
  900 kvartal / 1800 bino < 2 s.
* Burilish yoyi sig'masa radius kamaytiriladi va ogohlantiriladi; o'zi oqar quvurda teskari nishablik va sayoz quduq — ogohlantirish.
* Binolar orasidagi oraliq balandlikdan kichik bo'lsa — ogohlantirish (insolyatsiya/yong'in me'yorlari bilan tekshirilmagan).

## PTO: zinapoya va lift shaxtasi (v0.18) — `bim/building`

Qatlamlar: **ZINA / ЛЕСТНИЦА / STAIR** — zinapoya katagi konturi, **LIFT / ЛИФТ / SHAXTA** — lift shaxtasi ichki konturi.
* Har qavatda ikki marshli zina quriladi: pog'onalar soni qavat balandligidan (h ≤ 0.18 m, juft), eni b = 0.30 m,
  oraliq maydoncha, plitada teshik; marsh betoni (pog'ona + qiya plita) PTO jadvalida.
* Shaxta: devor halqasi (0.20 m) har qavatda, plitada teshik (tomdan tashqari).
* Tekshiruvlar: 2h + b = 600…650 mm, marsh eni ≥ 1.05 m, katak uzunligi (marsh + ikki maydoncha), marshda ≤ 18 pog'ona,
  shaxta ichki o'lchami ≥ 1.5 m, qavat planlarida shaxta bir joyda, 2+ qavatda zina yo'q, 6+ qavatda lift yo'q
  (chegaralar taxminiy — tasdiqlanmagan).
* `GRID` 0 dan: "Zinapoya va lift" (o'rta oraliqda).
* **Haydash → "bino bo'ylab (zina orqali)"**: 2D yo'l chizing — piyoda pog'onalar bo'ylab keyingi qavatga chiqadi.

## Metall konstruksiyalar (КМ) — `bim/steel`, `STEEL*`

Katalog: qo'shtavr GOST 8239-89 (I10…I60), shveller GOST 8240-97 (5…40), teng tomonli burchak GOST 8509-93,
truba GOST 10704 va kvadrat truba GOST 30245 (formuladan) — jadval qiymatlari eslab kiritilgan, tasdiqlanmagan.
* Element — `KM_<profil>` qatlamidagi o'q chizig'i (`STEELBEAM I20 p1 p2 …`, panelda "Element chizish"); 3D (Z) bilan
  Haydash sahnasida profil kesimi bilan ko'rinadi.
* `STEELSEC` — kesim haqiqiy o'lchamda (shtrix bilan), `STEEL I20` — xossalar (A, kg/m, Ix, Wx).
* `STEELSPEC [x,y] [%]` — metall prokat vedomosti: profil, GOST, soni, uzunlik, massa, zahira; jadval chizmaga.

## Listlar — Revit "Sheets" / AutoCAD "Layout" (v0.24) — `core/sheet`, `SHEET`
- **Ko'rinish → Listlar** yorlig'i (2D chizma | 3D model | Listlar): oq qog'oz A4…A0 (tik/yotiq), GOST 2.301 ramkasi (chapda 20 mm), asosiy yozuv 185×55 (GOST 21.101 sh.3).
- **+ Ko'rinish**: ekrandagi ko'rinish, butun chizma yoki chizmada 2 nuqtali oyna; masshtab — standart (1:50…1:50000) yoki **Avto** (sig'adigan eng yirigi);
  bo'sh joyga avtomatik joylanadi, ostida «Nom  M 1:500». Ko'rinishni ikki marta bosish — 2D chizmada o'sha joy ochiladi.
- **Shtamp…**: shifr, obyekt, bino, chizma nomi, bosqich, tashkilot, Ishlab chiqdi / Tekshirdi / N.nazorat / Tasdiqladi, sana; «barcha listlarga» bilan;
  yangi list umumiy maydonlarni oldingisidan oladi. List raqami va listlar soni avtomatik.
- **PDF / Barchasi PDF**: haqiqiy o'lchamli sahifalar (300 dpi, chiziq 0,25 mm, ramka 0,7 mm) — chop etishga tayyor.
- Listlar `.acad` faylida saqlanadi; har o'zgarish undo/redo bilan.
- Buyruq satri: `VARAQ` (ru `ЛИСТ`, en `SHEET`/`LAYOUT`):
  `SHEET NEW L-1 A1 [PORTRAIT]`, `SHEET VIEW L-1 x1,y1 x2,y2 [SCALE=500] ["TITLE=Bosh reja"]`, `SHEET STAMP L-1 CODE=… "OBJECT=…" DEV=… [ALL]`,
  `SHEET SIZE L-1 A2`, `SHEET RENAME L-1 GP-1`, `SHEET DEL L-1 [n]`, `SHEET` — ro'yxat. Model birligi — metr.

## AutoCAD uslubidagi sichqoncha va yordamchilar (v0.23) — `apps/gui/viewport`
- **UCS belgisi** pastki chap burchakda: X (qizil) va Y (yashil) o'qlar harflari bilan, kvadrat — WCS.
- **Kompas (2D ViewCube)** yuqori o'ngda: N/E/S/W (ru: С/В/Ю/З), markazda TEPA; bosish — hammasini ko'rsatish, ikki marta — 3D model.
- **Sichqoncha**: g'ildirak — kursor atrofida zoom; o'rta tugma — siljitish; o'rta tugmani **ikki marta** — Hammasi (ZOOM E);
  o'ng tugma — buyruqda Enter, bo'sh holatda kontekst menyu (Ko'chirish, Nusxa, Burish, Masshtab, Ko'zgu, O'chirish / Takrorlash, Bekor, Qaytarish, Hammasini tanlash);
  **Shift+o'ng** — bir martalik bog'lanish (END, MID, CEN, …); bo'sh joyni bosish — ramka, ikkinchi bosish yakunlaydi (chapdan o'ngga WINDOW, o'ngdan chapga CROSSING).
- **Klaviatura**: Space/Enter — buyruqni yakunlash yoki oxirgisini takrorlash; Space ushlab sudrash — siljitish; Shift ushlansa ORTO vaqtincha teskari.
- **Holat satri**: MODEL · koordinata · TO'R (F7) · QADAM (F9, qadam menyusi 0,01…10) · ORTO (F8, 90°) · QUTB (F10, 90/45/30/22,5/18/15/10/5°) ·
  OSNAP (F3, menyuda magnit rejimlari) · OTRACK (F11 — bog'lanish nuqtasida 0,35 s turing, yashil "+" chiqadi; undan gorizontal/vertikal nurlar) ·
  DYN (F12 — kursor yonida X/Y, keyin uzunlik < burchak) · izoh masshtabi (1:100 → o'lcham matni 0,25 m) · ⚙ nishon o'lchami (3…100 %).

## Bino fasadi va 3D boshqaruv (v0.22)

* **To'siq devorlar** (`TOSIQ_DEVOR` / `КЛАДКА` / `ГАЗОБЛОК`): har qavatda derazalar (eni 1.5, balandligi 1.5, tokcha 0.9 m, qadam ≈3 m),
  1-qavatda kirish eshigi, tomda uzluksiz parapet; kladka hajmi betonga kirmaydi (PTO'da alohida qator, `D500`).
  `GRID` endi tashqi devorni to'siq devor sifatida chizadi (`infill:false` — monolit beton devor).
* Bo'sh chizmada BIM / Armatura oynalari nima qilishni aytadi; **Namuna bino** tugmasi ajratib ko'rsatiladi.
* 3D: **Shift + o'rta tugma** — aylantirish (AutoCAD/Revit), strelkalar, `+`/`−`, **W/A/S/D/Q/E — bino ichida yurish**; **F4** yoki
  **Ko'rinish → 3D model** — asosiy oynada 2D ⇄ 3D.
* Yo'qolgan 60 ta interfeys tarjimasi tiklandi; test endi har bir GUI kaliti 6 tilda borligini tekshiradi.

## Yer ishlari: yuza, profil, kesimlar, kartogramma, hajm (v0.21) — `civil/terrain`

Lenta → **Geodeziya → Yer ishlari** (chap panelda "Relyef"). Yer — syomka nuqtalari (`SYOMKA`), belgi yozuvlari ("412,35"),
3D gorizontallar qatlami; loyiha — boshqa yuza (qatlam), gorizontal yoki qiya tekislik (Z, ix‰, iy‰) yoki **balans tekisligi**.

| Buyruq | Natija |
|---|---|
| `TIN sel [MAXEDGE=m] [BOUND=id]` | Delaunay uchburchaklar yuzasi (`YUZA_TIN`, 3D) |
| `CONTOUR sel STEP=0.5 MAJOR=2.5` | gorizontallar 3D, asosiylari yozuvli |
| `PROFILE o'q EG=… [FG=…\|Z=… IX IY] STEP=20 H=1000 V=100 [PK0] [AT] [XLSX=]` | bo'ylama profil: yer (jigarrang), loyiha (qizil), shartli gorizont, jadval — loyiha / yer / ishchi belgi / masofa / PK |
| `XSECTIONS o'q EG=… FG=…\|Z=… STEP=20 L=20 R=20 SCALE=200 COLS=4 [XLSX=]` | har N m da ko'ndalang kesimlar (to'kma — qizil, kesma — ko'k shtrix, maydonlar), rejada kesim izlari, **hajmlar vedomosti** (o'rtacha maydonlar usuli); Excelda formulalar bilan |
| `CARTOGRAM EG=… Z=…\|FG=…\|BALANCE CELL=20 [BOUND=id] [XLSX=]` | kartogramma: kvadratlar to'ri, tugunda ishchi (±), loyiha va yer belgisi; kvadrat hajmi (to'rt uchburchakli prizma — nol chizig'i kvadrat ichida ham aniq), **nol chizig'i**, ustunlar yig'indisi, jami va balans; balans tekisligi belgisi H0 |
| `VOLUME EG=… FG=…\|Z=…\|BALANCE [BOUND=id] [CELL=m]` | yuzalar orasidagi kesma / to'kma (mayda to'r) |

Excel: profil (piketlar), kesimlar (vedomost + kesim nuqtalari), kartogramma (tugunlar, kvadratlar, ustunlar, xulosa).

## Geodezik tahlil — og'ishlar (v0.20) — `civil/survey`

Lenta → **Geodeziya** (chap panelda "Syomka"): GeoUzPro / Geo_Deviation uslubidagi ijroiy tahlil.

| Tur | Nima o'lchanadi |
|---|---|
| **Reja / fasad** (`DEVIATION PLAN`) | nuqtadan loyiha chizig'igacha (devor yuzi, o'q, fasad izi, kontur); yopiq kontur — tashqari "+", ochiq — chap "+"; **B/P** — mahalliy guruhda (≈1.5 m) Z medianasidan yuqori/past |
| **Vertikallik** (`VERT`) | bir vertikaldagi nuqtalar (ustun, devor qirrasi, qoziq, fasad): pastki nuqtaga yoki loyiha o'qiga nisbatan dX, dY, og'ish, H; ruxsat — mm + H/k |
| **Balandlik** (`HEIGHT`) | Z − loyiha belgisi (doimiy yoki loyiha 3D chizig'i / nuqtasidan interpolyatsiya) |
| **Qiya o'q** (`AXIS`) | anker, qiya qoziq, tirgak, to'sin: 3D o'qqa perpendikulyar — yon (rejada) va tik tashkil etuvchilar, o'q bo'yicha masofa; element uchun qiyalik va azimut (loyiha / o'lchangan) |
| **Loyiha nuqtasi** (`POINT`) | qoziq / anker boshi, ustun markazi (aylana, nuqta, blok): eng yaqin o'lchangan nuqta bilan juftlash — dX, dY, dZ; o'lchanmagan qoziqlar ro'yxati |

* Chizmaga: strelka **doim loyihadan tashqariga** (nuqta tomonga), qiymat mm — **doim gorizontal**, yashil — ruxsatda,
  qizil — oshgan; B — yashil, P — ko'k. Barcha belgilar **nuqta qaysi balandlikda bo'lsa, o'sha balandlikda** (`DEV_STRELKA`, `DEV_QIYMAT`, `DEV_BP`);
  ustma-ust (yuqori/past) nuqtalarning yozuvlari qatorlanadi. `DEVCLEAR` — tozalash.
* **Excel** (`XLSX=`): og'ishlar varag'i (geodezik X — shimol), xulosa (soni, ruxsatda %, max, o'rtacha, RMS, tegishli emaslar), elementlar (qiya o'q).
  **CSV** (`CSV=`, `;` va o'nlik vergul — Excel uchun).
* Nuqtalar: `PTIMPORT fayl.txt|csv|xlsx [XY]` — N X Y Z [kod], ajratuvchi avtomatik (bo'sh joy, tab, `,`, `;`, o'nlik vergul), sarlavha qatorlari o'tkaziladi;
  nuqta — `SYOMKA`, nom/belgi/kod yozuvlari nuqtaning o'z Z sida. `PTEXPORT sel fayl.txt|csv|xlsx [XY]`.
* Oynada: tur tugmalari, nuqtalar/loyiha — qatlamdan yoki tanlovdan, parametrlar, jadval (ikki marta bosish — chizmada ko'rsatish), Excel/CSV.

## Konstruksiya shablonlari va geo-joylash (v0.19) — `bim/templates`

Lenta → **Konstruksiya**: Metall karkas · Zina · Rampa · Tirgak devor · Geo-joylash. Oynada parametrlar, jonli **3D** va plan,
xulosa (o'lcham, belgilar, beton m³, metall t) va tekshiruvlar. Har shablon **haqiqiy koordinata va dengiz sathidan belgida** joylanadi:
X (sharq), Y (shimol), burilish, ▽ absolyut belgi; "Chizmadan" — tayanch nuqta va yo'nalishni chizmada bosish.

| Buyruq | Nima chiziladi |
|---|---|
| `STEELFRAME` | ko'p qavatli karkas (o'qlar `dx=6;6;6 dy=6;6`, qavatlar) yoki portal rama/angar (oraliq, qadam, karniz, tizma): ustun, rigel, rafter, progonlar, tik va tom bog'lamalari — `KM_<profil>` 3D o'qlari, profillar bo'yicha massa |
| `STAIR` | ikki marshli (U) yoki to'g'ri; beton yoki metall (kosour `[16`); 3D pog'onalar (`ZINA_3D`), plan (`ZINA_PLAN`: qirralar, strelka, `20×165×300 (+412.500)`); 2h+b, marsh eni, pog'ona soni, maydoncha |
| `RAMP` | yerto'laga: avtomobil (o'tish qismlari yarim nishablikda) yoki piyoda (har 0.8 m da maydoncha); yo'l yuzasi (`RAMPA_3D`), yon devorlar + parapet, plan: nishablik strelkalari `i=15%`, ▽ belgilar |
| `RWALL` | tirgak devor L / T / gravitatsion: 3D (`TIRGAK_DEVOR_3D`), plan, kesim chizmasi (H, B, q, φ); Rankin bo'yicha Ea, ag'darilish va siljish zaxirasi, poydevor ostidagi bosim |
| `GEOPLACE sel p1 P1 [p2 P2] [GEO] [SCALE] [Z=H]` | tanlovni 1–2 nuqta bo'yicha geodezik koordinata va belgiga o'tkazish; `GEO` — X shimol, Y sharq; burilish, masofa nisbati va 2-nuqtadagi farq ko'rsatiladi |

Buyruq satrida: `STAIR H=3.3 width=1.2 steel=1 1000,2000,412.5` (oxirgi nuqta — tayanch X,Y,▽) yoki `STAIR "fayl.json"`.
Haydash (Drive) sahnasi endi har qanday tekis 3D yuzani (rampa, devor, 3DFACE) uchburchaklaydi — rampa bo'ylab haydash mumkin.
Me'yor chegaralari (rampa 15/18 %, piyoda 8 %, zaxira 1.5 va h.k.) eslab kiritilgan — tasdiqlanmagan; hisob loyiha hisobini almashtirmaydi.

## Asosiy lenta — AutoCAD "Home" kabi (v0.17) — `core/draft`, `civil/symbols`

| Guruh | Asboblar (GUI + buyruq) |
|---|---|
| Chizish | Chiziq, Poliliniya, Aylana, Yoy, **To'rtburchak** (R=/C= burchak), **Ko'pburchak** (ichki/tashqi), **Ellips**, **Splayn**, Nuqta, **Shtrix**, Matn, **Belgilar** |
| Tahrir | Ko'chirish, Nusxa, **Cho'zish** (kesuvchi oyna), Burish, **Ko'zgu**, **Masshtab**, Qirqish, Uzaytirish, Tutashtirish, **Faska**, **Massiv** (to'g'ri burchakli / qutbiy / yo'l bo'ylab), Parallel, O'chirish, **Portlatish**, **Birlashtirish**, **Uzish**, `LENGTHEN`, **Xossa nusxa** |
| Qatlamlar | Joriy qatlam ro'yxati, Qatlam xossalari (ko'rinish, muzlatish, qulf, rang), **Joriy qilish**, **Qatlamga moslash**, **O'chirish/Muzlatish/Qulflash/Qulfni ochish** (obyektni bosib), **Ajratish / bekor qilish**, **Hammasi yoqilsin** |
| O'lcham | chiziqli, parallel, **burchak** (° yoki ° ' "), radius, diametr, **vynoska** (strelka + matn) |

**Genplan belgilari** (rangli, haqiqiy o'lchamda): daraxt, igna bargli, buta, gulzor, maysazor, yengil avtomobil, yuk mashinasi,
avtobus, velosiped, odam, ko'cha chirog'i (yoritish doirasi), svetofor, bekat, skameyka, axlat qutisi, gidrant, quduq qopqog'i,
favvora, avtoturargoh, piyoda o'tish joyi, yo'l strelkasi, shimol strelkasi. Palitradan bosib qo'yiladi; `SYMBOL TREE 0,0;12,0;24,0`;
ko'cha bo'ylab — `ARRAY sel PATH yo'l_id S=12`. **Haydash** sahnasida 3D ko'rinadi (daraxt, mashina, chiroq, bekat, ...).

## Tonnel, metro, galereya — koridor yig'malari

`Tonnel` qismi (palitrada "Tonnel" yorlig'i): aylana (TBM, tyubing), taqasimon (NATM), to'rtburchak (ochiq usul, kollektor).
Loyiha belgisi — yo'l usti; hajmlar: **qazish**, **qoplama**, **yo'l asosi** (o'rtacha va aniq). Tekshiruv: ustki qatlam
(yer — syomkadan), metro nishabligi 3‰…40‰ (SP 120 — tasdiqlanmagan). Shablonlar: metro TBM Ø5.5 va Ø10, avtomobil NATM,
metro ochiq usul 9.0×5.5, kollektor 3.0×2.5. **Haydash** tonnel ichidan yuradi (ichki yuza, ko'rinish masofasi).

## Haydash (Drive) tahlili — `civil/drive`, `DRIVE`

Civil 3D "Analyze → Drive" kabi: chiziq (feature line, 3D poliliniya, o'q) tanlansa viewport ustida **▶ Haydash** chiqadi
(lentada: Tahlil → Haydash). 3D model ichida birinchi shaxs ko'rinishida yuriladi — yo'lda, shahar ko'chasida, bino ichida.

* Rejimlar: avtomobil (ko'z 1.2 m), yuk mashinasi (2.3 m), piyoda — bino ichi (1.7 m, 5 km/soat), dron.
* Balandlik manbai: chiziq Z, koridor yuzasi (har qanday nuqtada — polosa o'qi bo'ylab ham), yer (syomka), bino qavati, doimiy belgi.
* Sahna: chizmadagi 3D chiziqlar, ikki sathli konturlar (bino massivlari), koridor yuzasi (asfalt, yo'lak, kesma/to'kma qiyaligi,
  suv), bino modeli (ustun, devor, plita), yer yuzasi, yerga yotqizilgan 2D chiziqlar.
* Dasturiy z-bufer rasterlovchi (perspektiva, yaqin tekislik qirqimi, tuman) — bino ichida ham to'g'ri ko'rinish, OpenGL shart emas.
* **Ko'rinish masofasi**: ko'zdan yo'l ustidagi to'siqqacha (0.2 m) — bo'ylama profil (qavariq egri) va 3D to'siqlar
  (kesma qiyaligi, binolar, devorlar; XY to'r bilan nur kesishuvi). Talab — SNiP 2.05.02-85 10-jadval bo'yicha
  (tasdiqlanmagan). Kamchilik oraliqlari ro'yxatda, kichik xaritada va profil grafigida qizil; bosilsa o'sha joyga o'tiladi.
  Tekshiruv: qavariq egri R=2000 da nazariy √(2R)(√h1+√h2) ≈ 97.6 m, egri ichidagi devor — 2R·acos((R−m)/R).
* Boshqaruv: sichqoncha — atrofga qarash, Bo'sh joy — yurish/to'xtash, ← → — siljish, W/S — tezlik, yon siljish — o'ng polosa.
* CLI: `DRIVE id [V=80] [EYE=1.2] [OBJ=0.2] [STEP=10]` — ko'rinish masofasi hisoboti.

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
