#pragma once
#include <functional>
// AsiaCAD GUI — asosiy oyna. Uch interfeys rejimi bitta oynada:
//   Klassik (Samarqand) — lenta (ribbon), qatlamlar va xossalar paneli
//   Modul (Ipak yo'li)  — chapda modullar paneli, yuqorida kontekst asboblari
//   Fokus (Tungi Toshkent) — chizma butun ekranda, suzuvchi asboblar, Ctrl+K
// Barcha o'zgarishlar Interpreter orqali (buyruq satri bilan bir xil yo'l) — undo/redo yagona.
#include "theme.h"
#include "viewport.h"

#include <asiacad/core/commands.h>
#include <asiacad/core/document.h>
#include <asiacad/i18n/localizer.h>
#include "interpreter.h"

#include <QHash>
#include <QMainWindow>

#include <memory>
#include <sstream>

class QAction;
class QComboBox;
class QDialog;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;
class QToolBar;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace asiacad::gui {

class CommandPalette;
class SewerDialog;
class BuildingDialog;
class RebarDialog;
class UtilityDialog;
class RoadDialog;
class WaterwayDialog;
class CorridorDialog;
class CityDialog;
class TemplatesDialog;
class GeoPlaceDialog;
class DriveDialog;
class SymbolPalette;
class SteelPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(i18n::Localizer& loc, QWidget* parent = nullptr);
    ~MainWindow() override;

    void setMode(UiMode m);
    UiMode mode() const { return mode_; }
    void setLanguage(const QString& code);
    bool runLine(const QString& line, bool echo = true);  // false — EXIT
    Viewport* viewport() const { return view_; }
    SymbolPalette* symbolPalette() const { return symPal_; }
    SteelPanel* steelPanel() const { return steelPal_; }
    Document& document() { return doc_; }
    void activateTool(const QString& key);  // "line", "circle", ... (sinov va skriptlar uchun)
    SewerDialog* openSewer();               // "Kanalizatsiya" menyusi (bitta nusxa, modal emas)
    BuildingDialog* openBuilding();         // "Monolit bino" menyusi
    RebarDialog* openRebar();               // "Armatura 3D" menyusi
    UtilityDialog* openUtility();           // "Yer osti tarmoqlari 3D" menyusi
    RoadDialog* openRoad();                 // "Yo'l 3D" menyusi
    WaterwayDialog* openWaterway();         // "Suv yo'li" menyusi (quvur, kanal, ariq, lotok)
    CorridorDialog* openCorridor();         // "Koridor": yig'malar + qismlar palitrasi (Civil 3D Corridor kabi)
    CityDialog* openCity();                 // "Shahar mavzesi" — 0 dan: ko'chalar, kvartallar, binolar
    DriveDialog* openDrive();               // "Haydash" (Drive): tanlangan chiziq bo'ylab 3D ichida yurish va ko'rinish tahlili
    TemplatesDialog* openTemplates(int page);  // konstruksiya shablonlari: 0 metall karkas, 1 zina, 2 rampa, 3 tirgak devor
    GeoPlaceDialog* openGeoPlace();          // tanlovni haqiqiy koordinata / belgiga o'tkazish
    QDialog* openScratch(const QString& module);  // "0 dan chizish": building | road | waterway | corridor | util
    void locate(double x, double y);        // tuzatish: chizmada joyni ko'rsatish

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    QString t(const char* key, const QStringList& args = {}) const;
    void buildActions();
    void buildRibbon();
    void buildModuleBars();
    void buildDocks();
    void buildStatusBar();
    void retranslate();
    void applyTheme();
    void refreshLayers();
    void refreshProperties();
    void refreshTitle();
    void startCommand(const QString& canonical);  // palitra / buyruq satri: GUI asbobi bo'lsa — asbob
    void submitCommandLine();
    void appendHistory(const QString& text, const QColor& color);
    bool maybeSave();
    void openFile();
    void saveFile(bool saveAs);
    void importDxf();
    void exportDxf();
    QIcon icon(const QString& name) const;

    i18n::Localizer& loc_;
    Document doc_;
    CommandStack stack_;
    std::ostringstream out_;
    std::unique_ptr<cli::Interpreter> interp_;

    UiMode mode_ = UiMode::Classic;
    Theme theme_;
    Viewport* view_ = nullptr;
    CommandPalette* palette_ = nullptr;
    SewerDialog* sewer_ = nullptr;
    BuildingDialog* building_ = nullptr;
    RebarDialog* rebar_ = nullptr;
    UtilityDialog* util_ = nullptr;
    RoadDialog* road_ = nullptr;
    WaterwayDialog* waterway_ = nullptr;
    CorridorDialog* corridor_ = nullptr;
    CityDialog* city_ = nullptr;
    TemplatesDialog* tpl_ = nullptr;
    GeoPlaceDialog* geoDlg_ = nullptr;
    std::function<void(const std::vector<Vec3>&)> pickCb_;   // oyna chizmadan nuqta kutmoqda
    DriveDialog* drive_ = nullptr;
    QToolButton* driveCtx_ = nullptr;  // chiziq tanlanganda viewport ustida "▶ Haydash"

    QHash<QString, QAction*> act_;  // "line" -> QAction
    QTabWidget* ribbon_ = nullptr;
    QToolBar* rail_ = nullptr;
    QToolBar* context_ = nullptr;
    QToolBar* floating_ = nullptr;
    QDockWidget* layersDock_ = nullptr;
    QDockWidget* propsDock_ = nullptr;
    QDockWidget* symDock_ = nullptr;
    SymbolPalette* symPal_ = nullptr;
    QDockWidget* steelDock_ = nullptr;
    SteelPanel* steelPal_ = nullptr;
    QDockWidget* cmdDock_ = nullptr;
    QTreeWidget* layers_ = nullptr;
    QWidget* propsBox_ = nullptr;
    QLabel* propsCount_ = nullptr;
    QComboBox* propsLayer_ = nullptr;
    QComboBox* ribbonLayer_ = nullptr;
    QComboBox* propsColor_ = nullptr;
    QPlainTextEdit* propsList_ = nullptr;
    QPlainTextEdit* history_ = nullptr;
    QLineEdit* cmdEdit_ = nullptr;
    QLabel* prompt_ = nullptr;
    QLabel* coords_ = nullptr;
    QComboBox* modeBox_ = nullptr;
    QComboBox* langBox_ = nullptr;
    QHash<unsigned, QToolButton*> snapBtns_;
    QToolButton* orthoBtn_ = nullptr;
    QToolButton* polarBtn_ = nullptr;
    QStringList cmdHistory_;
    int cmdHistoryPos_ = 0;
    bool updatingUi_ = false;
};

} // namespace asiacad::gui
