#include "theme.h"

#include <algorithm>
#include <array>

namespace asiacad::gui {

namespace {

// Umumiy uslub: shrift, tugmalar, panellar. Ranglar har rejimda o'rniga qo'yiladi.
QString baseSheet(const QString& win, const QString& panel, const QString& panel2, const QString& text,
                  const QString& muted, const QString& border, const QString& accent, const QString& accentText,
                  const QString& ribbon, const QString& ribbonText, const QString& input) {
    return QStringLiteral(R"(
QMainWindow, QDialog { background: %1; }
QWidget { color: %4; font-size: 13px; }
QDockWidget { color: %4; font-weight: 600; }
QDockWidget::title { background: %2; padding: 6px 10px; border-bottom: 1px solid %6; }
QDockWidget > QWidget { background: %2; }
QTreeWidget, QListWidget, QPlainTextEdit, QTextEdit { background: %2; border: 0; color: %4; }
QTreeWidget::item, QListWidget::item { padding: 4px 2px; border-radius: 6px; }
QTreeWidget::item:selected, QListWidget::item:selected { background: %3; color: %4; }
QHeaderView::section { background: %2; color: %5; border: 0; padding: 4px; font-weight: 600; }
QLineEdit, QComboBox { background: %11; color: %4; border: 1px solid %6; border-radius: 8px; padding: 5px 8px; }
QComboBox QAbstractItemView { background: %2; color: %4; selection-background-color: %3; }
QPushButton { background: %3; color: %4; border: 1px solid %6; border-radius: 8px; padding: 6px 12px; font-weight: 600; }
QPushButton:hover { border-color: %7; }
QPushButton#accent, QPushButton[accent="true"] { background: %7; color: %8; border: 0; }
QToolButton { color: %4; background: transparent; border: 1px solid transparent; border-radius: 8px; padding: 4px; }
QToolButton:hover { background: %3; border-color: %6; }
QToolButton:checked { background: %7; color: %8; border-color: %7; }
QToolButton[ribbonSmall="true"] { padding: 1px 4px; border-radius: 5px; }
QTabWidget#ribbon::pane { background: %9; border: 0; border-bottom: 1px solid %6; }
QWidget#ribbonPage { background: %9; }
QTabWidget#ribbon > QTabBar::tab { background: transparent; color: %10; padding: 8px 16px; font-weight: 600; border: 0; }
QTabWidget#ribbon > QTabBar::tab:selected { background: %9; color: %4; border-top-left-radius: 8px; border-top-right-radius: 8px; }
QFrame#ribbonGroup QLabel#groupTitle { color: %5; font-size: 11px; font-weight: 700; }
QFrame#ribbonSep { background: %6; }
QToolBar { background: %2; border: 0; spacing: 4px; padding: 4px; }
QToolBar#rail { background: %9; }
QStatusBar { background: %2; color: %5; border-top: 1px solid %6; }
QStatusBar QLabel { color: %5; font-family: 'DejaVu Sans Mono', monospace; }
QStatusBar QToolButton { font-size: 11px; font-weight: 700; padding: 2px 8px; border-radius: 7px; }
QLabel#muted { color: %5; }
QLabel#title { font-weight: 800; font-size: 14px; }
QTableWidget, QTableView { background: %2; color: %4; gridline-color: %6; border: 1px solid %6; border-radius: 6px;
  selection-background-color: %3; selection-color: %4; }
QTableCornerButton::section { background: %2; border: 0; }
QAbstractSpinBox { background: %11; color: %4; border: 1px solid %6; border-radius: 8px; padding: 4px 6px; }
QCheckBox { color: %4; spacing: 6px; }
QTabWidget::pane { border: 1px solid %6; border-radius: 6px; background: %2; top: -1px; }
QTabBar::tab { background: %3; color: %5; padding: 6px 12px; margin-right: 2px; border-top-left-radius: 8px; border-top-right-radius: 8px; font-weight: 600; }
QTabBar::tab:selected { background: %7; color: %8; }
QSplitter::handle { background: transparent; width: 6px; }
QScrollArea#sewerLeft, QWidget#sewerLeftBody { background: transparent; }
QGroupBox { color: %4; border: 1px solid %6; border-radius: 8px; margin-top: 14px; padding-top: 8px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar:horizontal { background: transparent; height: 10px; }
QScrollBar::handle:horizontal { background: %6; border-radius: 5px; min-width: 30px; }
QScrollBar::handle:vertical { background: %6; border-radius: 5px; min-height: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
)")
        .arg(win, panel, panel2, text, muted, border, accent, accentText, ribbon, ribbonText, input);
}

} // namespace

Theme themeFor(UiMode mode) {
    Theme t;
    t.mode = mode;
    switch (mode) {
    case UiMode::Classic:  // Samarqand / Lojuvard: to'q ko'k sirt + oltin urg'u (sariq — faqat faol va muhim amallar)
        t.canvasBg = QColor("#0B1530");
        t.gridMinor = QColor("#132049");
        t.gridMajor = QColor("#1C2C5E");
        t.axisX = QColor("#C8324B");
        t.axisY = QColor("#1FA3A0");
        t.selection = QColor("#E0A526");
        t.snapMarker = QColor("#F2C230");
        t.crosshair = QColor("#C9D4F0");
        t.rubber = QColor("#5BC2E7");
        t.windowSel = QColor(75, 140, 255);
        t.crossingSel = QColor(60, 200, 120);
        t.accent = QColor("#E0A526");
        t.accentText = QColor("#231800");
        t.lightCanvas = false;
        // Rang tokenlari (yagona tizim):
        //   bg-0  #08112A  sarlavha / varaq yorliqlari       surface #0F1D40  panellar
        //   bg-1  #13224A  lenta sahifasi                    hover   #1C3166  ustiga kelish, tanlangan qator
        //   line  #22386B  chegara, ajratgich                 input   #0A1633  maydonlar
        //   text  #E6ECFA  asosiy matn   muted #8FA2CF  ikkinchi darajali   faint #5F7099
        //   accent #E0A526 (hover #F1B93B), accent-text #231800
        t.styleSheet = baseSheet("#0B1736", "#0F1D40", "#1C3166", "#E6ECFA", "#8FA2CF", "#22386B", "#E0A526",
                                 "#231800", "#13224A", "#8FA2CF", "#0A1633");
        t.styleSheet += QStringLiteral(R"(
QWidget#topArea { background: #08112A; }
QWidget#appBar { background: #08112A; border-bottom: 1px solid #13224A; }
QWidget#appBar QLabel#brandName { color: #F2F5FD; }
QWidget#appBar QLabel#appTitle { color: #8FA2CF; font-size: 12px; font-weight: 600; }
QFrame#appSep { background: #22386B; }
QToolButton[qat="true"] { padding: 5px; border-radius: 6px; }
QToolButton[qat="true"]:hover { background: #1C3166; border-color: #22386B; }
QPushButton#searchBox { background: #0F1D40; color: #8FA2CF; border: 1px solid #22386B; border-radius: 8px;
  padding: 6px 10px; text-align: left; font-weight: 500; }
QPushButton#searchBox:hover { border-color: #E0A526; color: #E6ECFA; }
QTabWidget#ribbon > QTabBar { background: #08112A; }
QTabWidget#ribbon > QTabBar::tab { color: #8FA2CF; background: transparent; padding: 7px 15px 8px 15px; margin: 0 1px;
  border: 0; border-top: 2px solid transparent; font-weight: 600; }
QTabWidget#ribbon > QTabBar::tab:hover { color: #E6ECFA; }
QTabWidget#ribbon > QTabBar::tab:selected { color: #FFFFFF; background: #13224A; border-top: 2px solid #E0A526;
  border-top-left-radius: 0; border-top-right-radius: 0; }
QTabWidget#ribbon::pane { background: #13224A; border: 0; border-bottom: 1px solid #22386B; }
QWidget#ribbonPage { background: #13224A; }
QTabWidget#ribbon QToolButton { color: #DCE4F7; border-radius: 7px; }
QTabWidget#ribbon QToolButton[ribbonBtn="true"] { padding: 4px 6px 3px 6px; font-size: 12px; }
QTabWidget#ribbon QToolButton[ribbonSmall="true"] { font-size: 12px; }
QTabWidget#ribbon QToolButton:hover { background: #1C3166; border-color: #2A447E; }
QTabWidget#ribbon QToolButton:checked { background: #E0A526; color: #231800; border-color: #E0A526; }
QTabWidget#ribbon QToolButton[stateToggle="true"]:checked { background: #1C3166; color: #F1C35A; border-color: #2A447E; }
QTabWidget#ribbon QToolButton::menu-indicator { image: none; width: 0; }
QFrame#ribbonGroup QLabel#groupTitle { color: #6F82B0; font-size: 10px; font-weight: 700; letter-spacing: 0.4px; }
QFrame#ribbonSep { background: #22386B; margin: 6px 0 6px 0; }
QComboBox#ribbonLayer { background: #0A1633; }
QDockWidget { font-size: 12px; }
QDockWidget::title { background: #0C1836; color: #8FA2CF; padding: 6px 10px; border-bottom: 1px solid #1A2C5A; }
QLabel#panelHint { color: #6F82B0; font-size: 12px; padding: 6px 2px; }
QPushButton#layerAdd { background: transparent; color: #DCE4F7; border: 1px dashed #2A447E; }
QPushButton#layerAdd:hover { border-color: #E0A526; color: #F1B93B; }
QPushButton[accent="true"]:hover, QPushButton#accent:hover { background: #F1B93B; }
QStatusBar QToolButton { color: #8FA2CF; }
QStatusBar QToolButton:checked { background: #1C3166; color: #F1C35A; border: 1px solid #3A4F86; }
QTabBar#viewTabs { background: #0B1736; }
QTabBar#viewTabs::tab { background: transparent; color: #8FA2CF; padding: 6px 14px; margin: 4px 2px 0 2px; border-radius: 0;
  border-bottom: 2px solid transparent; }
QTabBar#viewTabs::tab:hover { color: #E6ECFA; }
QTabBar#viewTabs::tab:selected { background: transparent; color: #FFFFFF; border-bottom: 2px solid #E0A526; }
QFrame#emptyCard { background: rgba(15, 29, 64, 235); border: 1px solid #22386B; border-radius: 14px; }
QLabel#emptyTitle { color: #F2F5FD; font-size: 17px; font-weight: 800; }
QLabel#emptyText { color: #8FA2CF; font-size: 13px; }
QFrame#emptyCard QPushButton { padding: 8px 14px; text-align: left; }
QMenu { background: #0F1D40; color: #E6ECFA; border: 1px solid #22386B; padding: 4px; }
QMenu::item { padding: 6px 18px; border-radius: 5px; }
QMenu::item:selected { background: #1C3166; }
QMenu::separator { height: 1px; background: #22386B; margin: 4px 6px; }
QToolTip { background: #08112A; color: #E6ECFA; border: 1px solid #22386B; padding: 4px 6px; }
)");
        break;
    case UiMode::Module:  // Ipak yo'li: iliq yorug', zafaron
        t.canvasBg = QColor("#FFFDF8");
        t.gridMinor = QColor("#F3EADB");
        t.gridMajor = QColor("#E6D6BD");
        t.axisX = QColor("#D9466A");
        t.axisY = QColor("#0E9F8E");
        t.selection = QColor("#E7881B");
        t.snapMarker = QColor("#D9466A");
        t.crosshair = QColor("#7A6A58");
        t.rubber = QColor("#1F7BE0");
        t.windowSel = QColor(31, 123, 224);
        t.crossingSel = QColor(14, 159, 142);
        t.accent = QColor("#E7881B");
        t.accentText = QColor("#FFFFFF");
        t.lightCanvas = true;
        t.styleSheet = baseSheet("#FBF6EC", "#FFFFFF", "#FFF1DE", "#2B2118", "#7A6A58", "#EADFCB", "#E7881B",
                                 "#FFFFFF", "#2B2118", "#E9DFCC", "#FFFFFF");
        t.styleSheet += QStringLiteral(R"(
QToolBar#rail QToolButton { color: #E9DFCC; font-size: 10px; }
QToolBar#rail QToolButton:checked { background: #4A3A2A; color: #FFFFFF; border-color: #E7881B; }
QToolBar#context QToolButton { border: 1px solid #EADFCB; border-radius: 16px; padding: 5px 12px; background: #FFFFFF; font-weight: 600; }
QToolBar#context QToolButton:checked { background: #E7881B; color: #FFFFFF; border-color: #E7881B; }
QWidget#topArea, QWidget#appBar { background: #2B2118; }
QWidget#appBar QLabel#brandName { color: #FFFFFF; }
QWidget#appBar QLabel#appTitle { color: #E9DFCC; font-size: 12px; font-weight: 600; }
QFrame#appSep { background: #4A3A2A; }
QWidget#appBar QToolButton { color: #FFFFFF; }
QWidget#appBar QToolButton:hover { background: #4A3A2A; border-color: #4A3A2A; }
QPushButton#searchBox { background: #3A2D20; color: #E9DFCC; border: 1px solid #4A3A2A; text-align: left; font-weight: 500; }
QPushButton#searchBox:hover { border-color: #E7881B; }
QLabel#panelHint { color: #7A6A58; padding: 6px 2px; }
QFrame#emptyCard { background: rgba(255, 255, 255, 240); border: 1px solid #EADFCB; border-radius: 14px; }
QLabel#emptyTitle { font-size: 17px; font-weight: 800; }
QLabel#emptyText { color: #7A6A58; }
)");
        break;
    case UiMode::Focus:  // Tungi Toshkent: to'q, neon urg'u
        t.canvasBg = QColor("#0D1020");
        t.gridMinor = QColor("#151A30");
        t.gridMajor = QColor("#20274A");
        t.axisX = QColor("#F07ACF");
        t.axisY = QColor("#22D3EE");
        t.selection = QColor("#22D3EE");
        t.snapMarker = QColor("#FFB020");
        t.crosshair = QColor("#A3ABD1");
        t.rubber = QColor("#9BE15D");
        t.windowSel = QColor(34, 211, 238);
        t.crossingSel = QColor(155, 225, 93);
        t.accent = QColor("#E04BD8");
        t.accentText = QColor("#1E0420");
        t.lightCanvas = false;
        t.styleSheet = baseSheet("#0D1020", "#151A30", "#232A4D", "#E7EAF6", "#A3ABD1", "#2A3157", "#E04BD8",
                                 "#1E0420", "#151A30", "#A3ABD1", "#0D1020");
        t.styleSheet += QStringLiteral(R"(
QToolBar#floating { background: #151A30; border: 1px solid #2A3157; border-radius: 14px; padding: 6px; }
QWidget#topArea, QWidget#appBar { background: #0D1020; }
QWidget#appBar { border-bottom: 1px solid #2A3157; }
QWidget#appBar QLabel#appTitle { color: #A3ABD1; font-size: 12px; font-weight: 600; }
QFrame#appSep { background: #2A3157; }
QPushButton#searchBox { background: #151A30; color: #A3ABD1; border: 1px solid #2A3157; text-align: left; font-weight: 500; }
QPushButton#searchBox:hover { border-color: #E04BD8; }
QLabel#panelHint { color: #A3ABD1; padding: 6px 2px; }
QFrame#emptyCard { background: rgba(21, 26, 48, 235); border: 1px solid #2A3157; border-radius: 14px; }
QLabel#emptyTitle { font-size: 17px; font-weight: 800; }
QLabel#emptyText { color: #A3ABD1; }
)");
        break;
    }
    return t;
}

QString modeKey(UiMode mode) {
    switch (mode) {
    case UiMode::Classic: return QStringLiteral("classic");
    case UiMode::Module: return QStringLiteral("module");
    case UiMode::Focus: return QStringLiteral("focus");
    }
    return QStringLiteral("classic");
}

UiMode modeFromKey(const QString& k) {
    if (k == QLatin1String("module")) return UiMode::Module;
    if (k == QLatin1String("focus")) return UiMode::Focus;
    return UiMode::Classic;
}

QColor aciColor(int i, bool lightCanvas) {
    static const std::array<QColor, 10> basic = {
        QColor(0, 0, 0),       QColor(255, 0, 0),     QColor(255, 255, 0),   QColor(0, 255, 0),
        QColor(0, 255, 255),   QColor(0, 0, 255),     QColor(255, 0, 255),   QColor(255, 255, 255),
        QColor(128, 128, 128), QColor(192, 192, 192)};
    if (i == 7 || i <= 0 || i > 255) return lightCanvas ? QColor(20, 20, 20) : QColor(240, 240, 240);
    if (i < 10) {
        QColor c = basic[static_cast<std::size_t>(i)];
        if (lightCanvas && i == 2) c = QColor(200, 160, 0);   // sariq yorug' fonda ko'rinsin
        if (lightCanvas && i == 4) c = QColor(0, 150, 170);
        return c;
    }
    if (i >= 250) {
        const int g = 51 + (i - 250) * 40;
        return QColor(g, g, g);
    }
    // 10..249: 24 rang tusi × 10 to'yinganlik/yorqinlik varianti (AutoCAD jadvaliga yaqin)
    const int hueIdx = (i - 10) / 10, var = i % 10;
    static const double val[] = {1.0, 1.0, 0.8, 0.8, 0.6, 0.6, 0.5, 0.5, 0.3, 0.3};
    const double sat = (var % 2 == 0) ? 1.0 : 0.5;
    return QColor::fromHsvF(std::clamp(hueIdx * 15.0 / 360.0, 0.0, 1.0), sat, val[var]);
}

} // namespace asiacad::gui
