# AsiaCAD — ishni davom ettirish uchun qo'llanma (Claude / dasturchi)

C++20 / Qt 6 CAD-BIM-infratuzilma platformasi: chizma yadrosi (DXF/DWG, .acad), monolit bino va armatura 3D,
KJ loyihasi (DWG varaqlardan hajm va armatura), yo'l, suv yo'li, koridor, yer osti tarmoqlari, kanalizatsiya,
shahar mavzesi, geodeziya, yer ishlari, IFC, ShNQ tekshiruvi. Interfeys 6 tilda: uz, en, ru, kk, ky, tr.
Modullar, buyruqlar va versiyalar bo'yicha batafsil — `README.md`.

## Holat

- Ish tarmog'i: **`ui-v0.31-lenta`** (main dan oldinda, hali birlashtirilmagan). Repo: https://github.com/islamjura01-ai/asiaCAD
- Oxirgi ishlar (v0.32):
  - Xossalar paneli har menyuda: 3D da element tanlansa — xossalar, tuzatish, qo'shimcha detal (`apps/gui/prop_panel.*`).
  - Varaqlar (AutoCAD kabi): Boshlash · bir nechta chizma · menyular varaq bo'lib (`apps/gui/main_window_tabs.cpp`).
  - Menyularda Ctrl+Z / Ctrl+Y (`apps/gui/menu_history.h`).
- Testlar: yadro — `157 test, 2513 tekshiruv, 0 xato` (Windows CI da ham 0 xato); GUI — `602 tekshiruv, 0 xato`.

## Yig'ish va testlar (Linux)

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/asiacad_tests                       # yadro (CLI buyruqlari, BIM, civil, IO)
Xvfb :97 -screen 0 1920x1080x24 &           # GUI testlari X server talab qiladi
DISPLAY=:97 QT_QPA_PLATFORM=xcb ./build/asiacad_gui_tests
```

- `ctest` GUI testini `offscreen` bilan ishga tushiradi — konteynerda QOpenGLWidget qulaydi; yuqoridagi xcb usulidan foydalaning.
- Qt 6 (Widgets, OpenGLWidgets) topilmasa faqat yadro va CLI (`asiacad`) yig'iladi.
- Ko'rib tekshirish (skrinshot rejimi):
  `./build/asiacad-gui [fayl] [--road|--util|--waterway|--corridor|--city|--sewer|--building N|--rebar|--start|--tabs N]
  [--pick KALIT|PREFIKS*] --screenshot out.png --size 1600x900` (barcha bayroqlar — `apps/gui/main.cpp`).

## Windows

- GitHub Actions `Windows build` (`.github/workflows/windows.yml`): main ga push, `v*` teg yoki qo'lda
  (`gh workflow run windows.yml --ref ui-v0.31-lenta`). Natija — `AsiaCAD-Windows` artefakti (zip):
  `AsiaCAD.exe`, `asiacad-cli.exe`, `i18n/`, `examples/`, Qt DLL lari. Teg bo'lsa — Release ga ham qo'shiladi.
- Yadro testlari CI jurnalida ko'rinadi (test nomi oldindan chiqariladi — qulasa joyi ma'lum).

## Tuzilma (asosiy joylar)

```
include/asiacad/, src/   core (Document, entity, CommandStack), io (DXF/DWG/acad), i18n, bim (bino, armatura,
                         KJ loyihasi, IFC, ShNQ), civil (yo'l, suv yo'li, koridor, tarmoqlar, kanalizatsiya, shahar)
apps/cli/                Interpreter — barcha buyruqlar (GUI ham chizmani faqat shu orqali o'zgartiradi)
apps/gui/                main_window.cpp (lenta, panellar, buyruq satri), main_window_tabs.cpp (varaqlar),
                         *_dialog.cpp (menyular), prop_panel (Xossalar), model3d_view / pipe3d_view (3D),
                         menu_history.h (menyu tarixi), theme.cpp (3 rejim uslubi), icons.cpp (vektor ikonkalar)
i18n/*.json              6 til lug'atlari
tests/                   test_main.cpp (yadro), gui_smoke.cpp (GUI), data/ (test fayllari)
examples/                namuna chizmalar (DXF/acad — sintetik, mijoz fayli emas)
```

## Qoidalar

- **Mijoz chizmalari va foydalanuvchi fayllari o'zgartirilmaydi** — faqat vaqtinchalik nusxa bilan ishlanadi, repoga qo'shilmaydi.
- Barcha loyihalar ShNQ ga mos; me'yordan chetlanish xato sifatida ko'rsatiladi.
- UI matni faqat i18n kaliti orqali va **6 tilning hammasida** (`i18n_gui_keys_complete` testi tekshiradi).
  O'zbekchada ʻ (U+02BB) va ʼ (U+02BC). Kalitlar oilasi: `gui.<modul>.*`; Xossalar — `gui.xp.*`; varaqlar — `gui.tab.*`, `gui.start.*`.
- Kod izohlari, commit xabarlari — o'zbekcha, qisqa.
- Har yangi xususiyatga test (yadro yoki GUI) va README da bo'lim.

## Muhim ichki tafsilotlar

- **Chizmaga yozish** faqat `MainWindow::runLine` (Interpreter buyrug'i) orqali — har biri undo qilinadi.
  Menyu nomidan — `menuRun(menu, lines)`: bir nechta buyruq bitta undo qadamiga birlashadi (`CommandStack::squash`)
  va menyu tarixiga belgi qo'yiladi.
- **Varaqlar:** `doc_` obyekti bitta (Viewport, Interpreter, menyular unga havola qiladi); varaq almashganda mazmuni
  almashtiriladi — `DocSlot` (Document, `CommandStack::History`, `Interpreter::DocState`, IFC, ko'rinish).
- **Menyular** — `QDialog`; `showMenu()` uni `QScrollArea` ichida varaqqa joylaydi va joriy chizmaga bog'laydi
  (`menuDoc_`). Kichik suzuvchi oynalarga `windowButtons()` (yig'ish / butun ekran / yopish).
- **Menyu tarixi:** `MenuHistory<State>` — holat "o'zgarishdan keyin" yoziladi (350 ms kechikish, JSON kaliti bilan
  solishtiriladi); `undoRedo()` avval faol menyu tarixini, bo'sh bo'lsa chizmani qaytaradi.
- **Xossalar kalitlari** (3D obyekt kaliti → PropPanel): bino/armatura shabloni `e:<id>:<daraja>:<TUR>` yoki `p:...`;
  armatura `r:i`, KJ plita `kj:<teg>:<daraja>:<tartib>`, KJ boshqa `c:<TUR>:<daraja>:<tartib>`; tarmoqlar `line:i`,
  `node:k`; yo'l / suv yo'li `st:i`; koridor `sec:i`; kanalizatsiya `mh:i`, `seg:i`; shahar `b:<indeks>`, `x:<qo'shimcha>`;
  asosiy 3D `bim:i`.
