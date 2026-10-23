#include "main_window.h"

#include "command_palette.h"
#include "sewer_dialog.h"
#include "building_dialog.h"
#include "rebar_dialog.h"
#include "utility_dialog.h"
#include "road_dialog.h"
#include "waterway_dialog.h"
#include "corridor_dialog.h"
#include "generator_dialogs.h"
#include "templates_dialog.h"
#include "survey_dialog.h"
#include "earthwork_dialog.h"
#include "model3d_view.h"
#include <QStackedWidget>
#include <QTabBar>
#include <asiacad/civil/terrain.h>
#include "drive_dialog.h"
#include "draft_panels.h"
#include "icons.h"

#include <asiacad/civil/drive.h>
#include <asiacad/core/entities.h>

#include <functional>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace asiacad::gui {

namespace {

struct ActDef {
    const char* key;
    const char* icon;
    const char* label;
    bool tool;
};

const ActDef kActs[] = {
    {"new", "new", "gui.act.new", false},         {"open", "open", "gui.act.open", false},
    {"save", "save", "gui.act.save", false},      {"saveas", "save", "gui.act.saveas", false},
    {"import", "open", "gui.act.import", false},   {"export", "save", "gui.act.export", false},
    {"undo", "undo", "gui.act.undo", false},      {"redo", "redo", "gui.act.redo", false},
    {"select", "select", "gui.act.select", true}, {"line", "line", "gui.act.line", true},
    {"pline", "pline", "gui.act.pline", true},    {"circle", "circle", "gui.act.circle", true},
    {"arc", "arc", "gui.act.arc", true},          {"point", "point", "gui.act.point", true},
    {"text", "text", "gui.act.text", true},       {"move", "move", "gui.act.move", true},
    {"copy", "copy", "gui.act.copy", true},       {"rotate", "rotate", "gui.act.rotate", true},
    {"erase", "erase", "gui.act.erase", false},   {"extents", "extents", "gui.act.extents", false},
    {"zoomin", "zoomin", "gui.act.zoomin", false}, {"zoomout", "zoomout", "gui.act.zoomout", false},
    {"grid", "grid", "gui.act.grid", false},      {"palette", "search", "gui.act.palette", false},
    {"dimlinear", "dimlinear", "gui.act.dimlinear", true},     {"dimaligned", "dimaligned", "gui.act.dimaligned", true},
    {"dimradius", "dimradius", "gui.act.dimradius", true},     {"dimdiameter", "dimdiameter", "gui.act.dimdiameter", true},
    {"offset", "offset", "gui.act.offset", true},  {"trim", "trim", "gui.act.trim", true},
    {"extend", "extend", "gui.act.extend", true},  {"fillet", "fillet", "gui.act.fillet", true},
    {"sewer", "pipes", "gui.act.sewer", false},
    {"building", "bim", "gui.act.building", false},
    {"rebar", "rebar", "gui.act.rebar", false},
    {"util", "util", "gui.act.util", false},
    {"road", "road", "gui.act.road", false},
    {"waterway", "waterway", "gui.act.waterway", false},
    {"corridor", "corridor", "gui.act.corridor", false},
    {"city", "city", "gui.act.city", false},
    {"drive", "drive", "gui.act.drive", false},
    // Draw / Modify / Layers / Annotation (AutoCAD "Home" lentasi kabi)
    {"rect", "rect", "gui.act.rect", true},         {"polygon", "polygon", "gui.act.polygon", true},
    {"ellipse", "ellipse", "gui.act.ellipse", true}, {"spline", "spline", "gui.act.spline", true},
    {"hatch", "hatch", "gui.act.hatch", false},      {"symbols", "symbols", "gui.act.symbols", false},
    {"stretch", "stretch", "gui.act.stretch", true}, {"scale", "scale", "gui.act.scale", true},
    {"mirror", "mirror", "gui.act.mirror", true},    {"chamfer", "chamfer", "gui.act.chamfer", true},
    {"array", "array", "gui.act.array", false},      {"explode", "explode", "gui.act.explode", false},
    {"join", "join", "gui.act.join", false},         {"break", "break", "gui.act.break", true},
    {"matchprop", "matchprop", "gui.act.matchprop", true},
    {"leader", "leader", "gui.act.leader", true},    {"dimangular", "dimangular", "gui.act.dimangular", true},
    {"layprops", "layers", "gui.act.layprops", false}, {"laymcur", "laymcur", "gui.act.laymcur", false},
    {"laymatch", "laymatch", "gui.act.laymatch", false}, {"layoff", "layoff", "gui.act.layoff", false},
    {"layfrz", "layfrz", "gui.act.layfrz", false},   {"laylck", "laylck", "gui.act.laylck", false},
    {"layulk", "layulk", "gui.act.layulk", false},   {"layiso", "layiso", "gui.act.layiso", false},
    {"layuniso", "layuniso", "gui.act.layuniso", false}, {"layon", "layon", "gui.act.layon", false},
    {"steel", "steel", "gui.act.steel", false},
    // Konstruksiya shablonlari va geo-joylash
    {"steelframe", "steelframe", "gui.act.steelframe", false}, {"stairtpl", "stair", "gui.act.stairtpl", false},
    {"ramptpl", "ramp", "gui.act.ramptpl", false},             {"rwalltpl", "rwall", "gui.act.rwalltpl", false},
    {"geoplace", "geoplace", "gui.act.geoplace", false},
    // Geodezik tahlil
    {"ptimport", "ptimport", "gui.act.ptimport", false}, {"ptexport", "ptexport", "gui.act.ptexport", false},
    {"devplan", "devplan", "gui.act.devplan", false},    {"devvert", "devvert", "gui.act.devvert", false},
    {"devheight", "devheight", "gui.act.devheight", false}, {"devaxis", "devaxis", "gui.act.devaxis", false},
    {"devpoint", "devpoint", "gui.act.devpoint", false}, {"devclear", "devclear", "gui.act.devclear", false},
    // Yer ishlari
    {"ewtin", "terrain", "gui.act.ewtin", false},           {"ewprofile", "profile", "gui.act.ewprofile", false},
    {"ewsections", "sections", "gui.act.ewsections", false}, {"ewcarto", "cartogram", "gui.act.ewcarto", false},
    {"ewvolume", "volume", "gui.act.ewvolume", false},
    {"view3d", "volume", "gui.act.view3d", false},
};

// Asbob -> Tool va kanonik buyruq
Tool toolOf(const QString& k) {
    if (k == "line") return Tool::Line;
    if (k == "pline") return Tool::Pline;
    if (k == "circle") return Tool::Circle;
    if (k == "arc") return Tool::Arc;
    if (k == "point") return Tool::Point;
    if (k == "text") return Tool::Text;
    if (k == "move") return Tool::Move;
    if (k == "copy") return Tool::Copy;
    if (k == "rotate") return Tool::Rotate;
    if (k == "dimlinear") return Tool::DimLinear;
    if (k == "dimaligned") return Tool::DimAligned;
    if (k == "dimradius") return Tool::DimRadius;
    if (k == "dimdiameter") return Tool::DimDiameter;
    if (k == "offset") return Tool::Offset;
    if (k == "trim") return Tool::Trim;
    if (k == "extend") return Tool::Extend;
    if (k == "fillet") return Tool::Fillet;
    if (k == "rect") return Tool::Rect;
    if (k == "polygon") return Tool::Polygon;
    if (k == "ellipse") return Tool::Ellipse;
    if (k == "spline") return Tool::Spline;
    if (k == "stretch") return Tool::Stretch;
    if (k == "scale") return Tool::Scale;
    if (k == "mirror") return Tool::Mirror;
    if (k == "chamfer") return Tool::Chamfer;
    if (k == "break") return Tool::Break;
    if (k == "matchprop") return Tool::MatchProp;
    if (k == "leader") return Tool::Leader;
    if (k == "dimangular") return Tool::DimAngular;
    return Tool::Select;
}

QString toolKey(Tool t) {
    switch (t) {
    case Tool::Line: return "line";
    case Tool::Pline: return "pline";
    case Tool::Circle: return "circle";
    case Tool::Arc: return "arc";
    case Tool::Point: return "point";
    case Tool::Text: return "text";
    case Tool::Move: return "move";
    case Tool::Copy: return "copy";
    case Tool::Rotate: return "rotate";
    case Tool::DimLinear: return "dimlinear";
    case Tool::DimAligned: return "dimaligned";
    case Tool::DimRadius: return "dimradius";
    case Tool::DimDiameter: return "dimdiameter";
    case Tool::Offset: return "offset";
    case Tool::Trim: return "trim";
    case Tool::Extend: return "extend";
    case Tool::Fillet: return "fillet";
    case Tool::Rect: return "rect";
    case Tool::Polygon: return "polygon";
    case Tool::Ellipse: return "ellipse";
    case Tool::Spline: return "spline";
    case Tool::Stretch: return "stretch";
    case Tool::Scale: return "scale";
    case Tool::Mirror: return "mirror";
    case Tool::Chamfer: return "chamfer";
    case Tool::Break: return "break";
    case Tool::MatchProp: return "matchprop";
    case Tool::Leader: return "leader";
    case Tool::DimAngular: return "dimangular";
    case Tool::Symbol:
    case Tool::LayPick:
    case Tool::SteelBeam:
    case Tool::SteelSec:
    case Tool::Pick:
    case Tool::Grip:
    case Tool::Select: return "select";
    }
    return "select";
}

QIcon swatch(const QColor& c) {
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(0, 0, 0, 60));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 4, 4);
    return QIcon(pm);
}

const struct { unsigned mode; const char* name; } kSnaps[] = {
    {SnapEnd, "END"}, {SnapMid, "MID"}, {SnapCen, "CEN"}, {SnapQua, "QUA"}, {SnapInt, "INT"},
    {SnapPer, "PER"}, {SnapTan, "TAN"}, {SnapNea, "NEA"}, {SnapNod, "NOD"}, {SnapIns, "INS"},
};

} // namespace

MainWindow::MainWindow(i18n::Localizer& loc, QWidget* parent)
    : QMainWindow(parent), loc_(loc), stack_(doc_), interp_(std::make_unique<cli::Interpreter>(doc_, stack_, loc_, out_)) {
    resize(1440, 900);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);

    view_ = new Viewport(doc_, loc_, this);
    palette_ = new CommandPalette(loc_, this);

    buildActions();

    auto* central = new QWidget(this);
    auto* col = new QVBoxLayout(central);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    buildRibbon();
    setMenuWidget(ribbon_);  // lenta butun kenglikda (panellar ustida) — AutoCAD kabi
    // 2D chizma / 3D model (Revit kabi ko'rinish yorliqlari)
    viewTabs_ = new QTabBar(central);
    viewTabs_->setObjectName("viewTabs");
    viewTabs_->setExpanding(false);
    viewTabs_->setDrawBase(false);
    viewTabs_->addTab(QString());
    viewTabs_->addTab(QString());
    col->addWidget(viewTabs_);
    viewStack_ = new QStackedWidget(central);
    view3d_ = new Model3DView(viewStack_);
    view3d_->setObjectName("view3d");
    Model3DView::setGlobalTr([this](const char* k) {
        const std::string key = std::string("gui.") + k;
        const std::string v = loc_.tr(key);
        return v == key ? QString() : QString::fromStdString(v);
    });
    viewStack_->addWidget(view_);
    viewStack_->addWidget(view3d_);
    col->addWidget(viewStack_, 1);
    connect(viewTabs_, &QTabBar::currentChanged, this, [this](int i) { show3D(i == 1); });
    setCentralWidget(central);

    buildModuleBars();
    buildDocks();
    buildStatusBar();

    connect(view_, &Viewport::runCommand, this, [this](const QString& l) { runLine(l); });
    connect(view_, &Viewport::pointsPicked, this, [this](const std::vector<Vec3>& pts) {
        if (auto cb = std::move(pickCb_)) { pickCb_ = {}; cb(pts); }
    });
    connect(view_, &Viewport::pickCancelled, this, [this] {
        if (auto cb = std::move(pickCb_)) { pickCb_ = {}; cb({}); }
    });
    connect(view_, &Viewport::promptChanged, this, [this](const QString& s) { prompt_->setText(s); });
    connect(view_, &Viewport::cursorMoved, this, [this](double x, double y) {
        coords_->setText(QStringLiteral("X %1   Y %2").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3));
    });
    connect(view_, &Viewport::selectionChanged, this, &MainWindow::refreshProperties);
    // kontekst: bitta chiziq (feature line) tanlansa — viewport ustida "▶ Haydash" (Civil 3D kontekst lentasi kabi)
    driveCtx_ = new QToolButton(view_);
    driveCtx_->setObjectName("accent");
    driveCtx_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    driveCtx_->setCursor(Qt::PointingHandCursor);
    driveCtx_->hide();
    connect(driveCtx_, &QToolButton::clicked, this, [this] { openDrive(); });
    connect(view_, &Viewport::selectionChanged, this, [this] {
        const auto& sel = view_->selection();
        bool curve = false;
        if (sel.size() == 1)
            if (const IEntity* e = doc_.find(sel.front())) curve = dynamic_cast<const ICurve*>(e) && !dynamic_cast<const CircleEntity*>(e);
        driveCtx_->setText(t("gui.drv.ctx"));
        driveCtx_->adjustSize();
        driveCtx_->move(view_->width() - driveCtx_->width() - 14, 12);
        driveCtx_->setVisible(curve);
        if (curve) driveCtx_->raise();
    });
    connect(view_, &Viewport::textRequested, this, [this](const Vec3& at) {
        QDialog dlg(this);
        dlg.setWindowTitle(t("gui.text.title"));
        auto* form = new QFormLayout(&dlg);
        auto* txt = new QLineEdit(&dlg);
        auto* h = new QDoubleSpinBox(&dlg);
        h->setDecimals(3);
        h->setRange(0.001, 1e6);
        h->setValue(std::max(0.1, std::round(24.0 / std::max(1e-9, view_->toScreen({1, 0, 0}).x() - view_->toScreen({0, 0, 0}).x()) * 10) / 10));
        form->addRow(t("gui.text.value"), txt);
        form->addRow(t("gui.text.height"), h);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() == QDialog::Accepted && !txt->text().trimmed().isEmpty()) {
            QString s = txt->text();
            s.remove('"');
            runLine(QStringLiteral("TEXT %1 %2 \"%3\"").arg(Viewport::fmtPoint(at)).arg(h->value()).arg(s));
        }
    });
    connect(palette_, &CommandPalette::commandChosen, this, &MainWindow::startCommand);
    auto* k = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
    connect(k, &QShortcut::activated, this, [this] { palette_->open(view_); });

    setMode(UiMode::Classic);
    appendHistory(QString::fromStdString(loc_.tr("app.banner", {"0.23"})), theme_.accent);
    retranslate();
    refreshLayers();
    refreshProperties();
    refreshTitle();
}

MainWindow::~MainWindow() = default;

QString MainWindow::t(const char* key, const QStringList& args) const {
    std::vector<std::string> a;
    for (const auto& s : args) a.push_back(s.toStdString());
    return QString::fromStdString(loc_.tr(key, a));
}

QIcon MainWindow::icon(const QString& name) const {
    switch (mode_) {
    case UiMode::Classic: {
        // lenta: rangli plitka ikonkalar (Samarqand palitrasi)
        static const QHash<QString, QString> tint = {
            {"line", "#1FA3A0"}, {"pline", "#1FA3A0"}, {"circle", "#1FA3A0"}, {"arc", "#1FA3A0"}, {"point", "#1FA3A0"}, {"text", "#1FA3A0"},
            {"move", "#E0A526"}, {"copy", "#E0A526"}, {"rotate", "#E0A526"}, {"select", "#E0A526"}, {"erase", "#C8324B"},
            {"pipes", "#1F7BE0"}, {"bim", "#D9466A"}, {"rebar", "#D9466A"}, {"util", "#1F7BE0"}, {"road", "#1F7BE0"}, {"waterway", "#1F9BD0"}, {"corridor", "#E7881B"}, {"city", "#8E5BD6"}, {"drive", "#2BA84A"}, {"offset", "#E0A526"}, {"trim", "#E0A526"}, {"extend", "#E0A526"}, {"fillet", "#E0A526"}, {"stretch", "#E0A526"}, {"scale", "#E0A526"}, {"mirror", "#E0A526"}, {"chamfer", "#E0A526"}, {"array", "#E0A526"}, {"explode", "#E0A526"}, {"join", "#E0A526"}, {"break", "#E0A526"}, {"matchprop", "#E0A526"},
            {"rect", "#1C8C6E"}, {"polygon", "#1C8C6E"}, {"ellipse", "#1C8C6E"}, {"spline", "#1C8C6E"}, {"hatch", "#1C8C6E"}, {"symbols", "#3E9B3A"}, {"steel", "#4F6D8A"},
            {"steelframe", "#4F6D8A"}, {"stairtpl", "#D9466A"}, {"ramptpl", "#E7881B"}, {"rwalltpl", "#8A7B5C"}, {"geoplace", "#0E9F8E"},
            {"ptimport", "#0E9F8E"}, {"ptexport", "#0E9F8E"}, {"devplan", "#0E9F8E"}, {"devvert", "#0E9F8E"}, {"devheight", "#0E9F8E"},
            {"devaxis", "#0E9F8E"}, {"devpoint", "#0E9F8E"}, {"devclear", "#E0A526"},
            {"ewtin", "#3E9B3A"}, {"ewprofile", "#3E9B3A"}, {"ewsections", "#3E9B3A"}, {"ewcarto", "#3E9B3A"}, {"ewvolume", "#3E9B3A"}, {"view3d", "#7A3FC8"},
            {"leader", "#C8324B"}, {"dimangular", "#C8324B"},
            {"layers", "#D99A1E"}, {"laymcur", "#2BA84A"}, {"laymatch", "#1C8C9E"}, {"layoff", "#6B7280"}, {"layfrz", "#2F7FD8"}, {"laylck", "#C2410C"}, {"layulk", "#E58A3A"}, {"layiso", "#7A3FC8"}, {"layuniso", "#A27BDB"}, {"layon", "#3E9B3A"},
            {"dimlinear", "#C8324B"}, {"dimaligned", "#C8324B"}, {"dimradius", "#C8324B"}, {"dimdiameter", "#C8324B"},
            {"new", "#2F5DA8"}, {"open", "#2F5DA8"}, {"save", "#2F5DA8"}, {"undo", "#5B6B95"}, {"redo", "#5B6B95"},
            {"extents", "#7A3FC8"}, {"zoomin", "#7A3FC8"}, {"zoomout", "#7A3FC8"}, {"grid", "#7A3FC8"}, {"search", "#C8324B"}};
        return makeIcon(name, Qt::white, QColor(tint.value(name, "#2F5DA8")));
    }
    case UiMode::Module: return makeIcon(name, QColor("#B4600A"));
    case UiMode::Focus: return makeIcon(name, QColor("#E7EAF6"));
    }
    return makeIcon(name, Qt::white);
}

void MainWindow::buildActions() {
    auto* tools = new QActionGroup(this);
    tools->setExclusive(true);
    for (const auto& d : kActs) {
        auto* a = new QAction(this);
        a->setData(QString::fromLatin1(d.icon));
        act_.insert(d.key, a);
        if (d.tool) {
            a->setCheckable(true);
            tools->addAction(a);
            const QString key = d.key;
            connect(a, &QAction::triggered, this, [this, key] {
                view_->setTool(toolOf(key));
                if (act_.contains(toolKey(view_->tool()))) act_[toolKey(view_->tool())]->setChecked(true);
                view_->setFocus();
            });
        }
    }
    act_["select"]->setChecked(true);
    act_["new"]->setShortcut(QKeySequence::New);
    act_["open"]->setShortcut(QKeySequence::Open);
    act_["save"]->setShortcut(QKeySequence::Save);
    act_["undo"]->setShortcut(QKeySequence::Undo);
    act_["redo"]->setShortcut(QKeySequence::Redo);
    act_["grid"]->setCheckable(true);
    act_["grid"]->setChecked(true);
    act_["grid"]->setShortcut(QKeySequence(Qt::Key_F7));
    for (auto* a : act_) addAction(a);  // yorliqlar lenta yashirin bo'lsa ham ishlasin

    connect(act_["new"], &QAction::triggered, this, [this] { if (maybeSave()) runLine("NEW !"); });
    connect(act_["open"], &QAction::triggered, this, &MainWindow::openFile);
    connect(act_["save"], &QAction::triggered, this, [this] { saveFile(false); });
    connect(act_["saveas"], &QAction::triggered, this, [this] { saveFile(true); });
    connect(act_["import"], &QAction::triggered, this, &MainWindow::importDxf);
    connect(act_["export"], &QAction::triggered, this, &MainWindow::exportDxf);
    connect(act_["undo"], &QAction::triggered, this, [this] { runLine("UNDO"); });
    connect(act_["redo"], &QAction::triggered, this, [this] { runLine("REDO"); });
    connect(act_["erase"], &QAction::triggered, this, [this] { startCommand("ERASE"); });
    connect(act_["extents"], &QAction::triggered, this, [this] { view_->zoomExtents(); });
    connect(act_["zoomin"], &QAction::triggered, this, [this] { view_->zoomBy(1.5); });
    connect(act_["zoomout"], &QAction::triggered, this, [this] { view_->zoomBy(1 / 1.5); });
    connect(act_["grid"], &QAction::toggled, this, [this](bool on) { view_->setGridVisible(on); });
    connect(act_["palette"], &QAction::triggered, this, [this] { palette_->open(view_); });
    connect(act_["sewer"], &QAction::triggered, this, [this] { openSewer(); });
    connect(act_["building"], &QAction::triggered, this, [this] { openBuilding(); });
    connect(act_["rebar"], &QAction::triggered, this, [this] { openRebar(); });
    connect(act_["util"], &QAction::triggered, this, [this] { openUtility(); });
    connect(act_["road"], &QAction::triggered, this, [this] { openRoad(); });
    connect(act_["waterway"], &QAction::triggered, this, [this] { openWaterway(); });
    connect(act_["corridor"], &QAction::triggered, this, [this] { openCorridor(); });
    connect(act_["city"], &QAction::triggered, this, [this] { openCity(); });
    connect(act_["steelframe"], &QAction::triggered, this, [this] { openTemplates(0); });
    connect(act_["stairtpl"], &QAction::triggered, this, [this] { openTemplates(1); });
    connect(act_["ramptpl"], &QAction::triggered, this, [this] { openTemplates(2); });
    connect(act_["rwalltpl"], &QAction::triggered, this, [this] { openTemplates(3); });
    connect(act_["geoplace"], &QAction::triggered, this, [this] { openGeoPlace(); });
    connect(act_["ptimport"], &QAction::triggered, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, t("gui.act.ptimport"), QString(), "TXT / CSV / XLSX (*.txt *.csv *.xlsx *.xyz *.dat);;* (*)");
        if (f.isEmpty()) return;
        runLine(openSurvey()->importCommand(f));
        view_->zoomExtents();
    });
    connect(act_["ptexport"], &QAction::triggered, this, [this] { openSurvey(); });
    connect(act_["devplan"], &QAction::triggered, this, [this] { openSurvey(0); });
    connect(act_["devheight"], &QAction::triggered, this, [this] { openSurvey(1); });
    connect(act_["devvert"], &QAction::triggered, this, [this] { openSurvey(2); });
    connect(act_["devaxis"], &QAction::triggered, this, [this] { openSurvey(3); });
    connect(act_["devpoint"], &QAction::triggered, this, [this] { openSurvey(4); });
    connect(act_["devclear"], &QAction::triggered, this, [this] { runLine("DEVCLEAR"); });
    connect(act_["ewtin"], &QAction::triggered, this, [this] { openEarthwork(0); });
    act_["view3d"]->setShortcut(QKeySequence(Qt::Key_F4));
    connect(act_["view3d"], &QAction::triggered, this, [this] { show3D(!is3D()); });
    connect(act_["ewprofile"], &QAction::triggered, this, [this] { openEarthwork(1); });
    connect(act_["ewsections"], &QAction::triggered, this, [this] { openEarthwork(2); });
    connect(act_["ewcarto"], &QAction::triggered, this, [this] { openEarthwork(3); });
    connect(act_["ewvolume"], &QAction::triggered, this, [this] { openEarthwork(4); });
    connect(act_["drive"], &QAction::triggered, this, [this] { openDrive(); });
    // ---- tanlov bilan bajariladigan amallar
    auto withSel = [this](const QString& canon) {
        if (view_->selection().empty()) { prompt_->setText(t("gui.prompt.selectfirst")); return; }
        QStringList ids;
        for (EntityId id : view_->selection()) ids << QString::number(id);
        runLine(canon + " " + ids.join(','));
    };
    connect(act_["explode"], &QAction::triggered, this, [withSel] { withSel("EXPLODE"); });
    connect(act_["join"], &QAction::triggered, this, [withSel] { withSel("JOIN"); });
    connect(act_["hatch"], &QAction::triggered, this, [this] {
        if (view_->selection().empty()) { prompt_->setText(t("gui.prompt.hatchsel")); return; }
        QStringList ids;
        for (EntityId id : view_->selection()) ids << QString::number(id);
        bool ok = false;
        const QStringList pats{"SOLID", "ANSI31", "ANSI37", "AR-CONC", "EARTH", "GRAVEL"};
        const QString pat = QInputDialog::getItem(this, t("gui.act.hatch"), t("gui.hatch.pattern"), pats, 1, false, &ok);
        if (ok) runLine("HATCH " + ids.join(',') + " " + pat);
    });
    connect(act_["steel"], &QAction::triggered, this, [this] {
        steelDock_->show();
        steelDock_->raise();
    });
    connect(act_["symbols"], &QAction::triggered, this, [this] {
        symDock_->show();
        symDock_->raise();
    });
    connect(act_["layprops"], &QAction::triggered, this, [this] {
        layersDock_->setVisible(!layersDock_->isVisible() || layersDock_->visibleRegion().isEmpty());
        layersDock_->raise();
    });
    connect(act_["layon"], &QAction::triggered, this, [this] { runLine("LAYON"); });
    connect(act_["layuniso"], &QAction::triggered, this, [this] { runLine("LAYUNISO"); });
    // qatlam amallari: tanlov bo'lsa — darhol, bo'lmasa — obyektni bosib tanlash
    for (const auto& [key, canon] : std::vector<std::pair<QString, QString>>{{"laymcur", "LAYMCUR"}, {"layoff", "LAYOFF"}, {"layfrz", "LAYFRZ"},
                                                                              {"laylck", "LAYLCK"}, {"layulk", "LAYULK"}, {"layiso", "LAYISO"}}) {
        const QString c = canon;
        connect(act_[key], &QAction::triggered, this, [this, c] {
            if (!view_->selection().empty()) {
                QStringList ids;
                for (EntityId id : view_->selection()) ids << QString::number(id);
                runLine(c + " " + (c == "LAYMCUR" ? ids.front() : ids.join(',')));
                return;
            }
            view_->setLayerPickCommand(c);
            view_->setTool(Tool::LayPick);
            view_->setFocus();
        });
    }
    connect(act_["laymatch"], &QAction::triggered, this, [this] {
        if (view_->selection().empty()) { prompt_->setText(t("gui.prompt.selectfirst")); return; }
        QStringList ids;
        for (EntityId id : view_->selection()) ids << QString::number(id);
        view_->setLayerPickCommand("LAYMATCH " + ids.join(','));
        view_->setTool(Tool::LayPick);
        view_->setFocus();
    });
    connect(act_["array"], &QAction::triggered, this, [this] {
        const auto& sel = view_->selection();
        if (sel.empty()) { prompt_->setText(t("gui.prompt.selectfirst")); return; }
        // tanlovda eng uzun ochiq egri — yo'l (PATH) bo'lishi mumkin
        EntityId path = kInvalidId;
        double best = 0;
        if (sel.size() >= 2)
            for (EntityId id : sel)
                if (auto* c = dynamic_cast<const ICurve*>(doc_.find(id)); c && !c->isClosed() && c->length() > best) { best = c->length(); path = id; }
        QStringList ids;
        BBox b;
        for (EntityId id : sel)
            if (id != path) {
                ids << QString::number(id);
                if (const IEntity* e = doc_.find(id)) b.expand(e->bbox());
            }
        if (ids.isEmpty()) { ids << QString::number(path); path = kInvalidId; }
        ArrayDialog dlg(loc_, ids.join(','), b.valid() ? (b.min + b.max) * 0.5 : Vec3{}, path, this);
        dlg.setStyleSheet(theme_.styleSheet);
        if (dlg.exec() == QDialog::Accepted) runLine(dlg.command());
    });
    connect(view_, &Viewport::leaderRequested, this, [this](const QString& pts) {
        bool ok = false;
        const QString txt = QInputDialog::getText(this, t("gui.act.leader"), t("gui.text.value"), QLineEdit::Normal, QString(), &ok);
        if (!ok) return;
        QString esc = txt;
        esc.replace('"', '\'');
        runLine("LEADER " + pts + " \"" + esc + "\" " + Viewport::fmtPoint({view_->dimTextHeight(), 0, 0}).section(',', 0, 0));
    });
    // Asbob tugagach (Esc, Enter, buyruq bajarildi) — "Tanlash" tugmasi yonsin
    connect(view_, &Viewport::promptChanged, this, [this] {
        if (act_.contains(toolKey(view_->tool()))) act_[toolKey(view_->tool())]->setChecked(true);
    });
}

void MainWindow::buildRibbon() {
    ribbon_ = new QTabWidget(this);
    ribbon_->setObjectName("ribbon");
    ribbon_->setDocumentMode(true);
    ribbon_->setFixedHeight(134);
    {   // brend: o'ng yuqori burchakda katta logotip va nom
        auto* brand = new QWidget(ribbon_);
        brand->setObjectName("brand");
        brand->setStyleSheet("#brand { background: transparent; }");
        brand->setAttribute(Qt::WA_TranslucentBackground);
        auto* bl = new QHBoxLayout(brand);
        bl->setContentsMargins(8, 2, 14, 2);
        bl->setSpacing(8);
        auto* ic = new QLabel(brand);
        ic->setPixmap(makeIcon("logo", QColor("#E0A526"), QColor(), 64).pixmap(30, 30));
        auto* nm = new QLabel(QStringLiteral("<span style='font-size:21px; font-weight:800; letter-spacing:0.5px;'>Asia<span style='color:#E0A526'>CAD</span></span>"
                                             "<span style='font-size:11px; color:#8a93a3;'>&nbsp;0.22<nbsp;0.23</span>"),
                              brand);
        nm->setObjectName("brandName");
        nm->setTextFormat(Qt::RichText);
        bl->addWidget(ic);
        bl->addWidget(nm);
        ribbon_->setCornerWidget(brand, Qt::TopRightCorner);
        setWindowIcon(makeIcon("logo", QColor("#E0A526"), QColor("#14213D"), 256));
    }
    // Har bir sahifa: guruhlar (tugmalar + pastda guruh nomi)
    struct Group { const char* title; std::vector<const char*> keys; std::vector<const char*> small = {}; bool iconsOnly = false; int rows = 3; };
    struct Page { const char* title; std::vector<Group> groups; };
    const std::vector<Page> pages = {
        {"gui.tab.home", {{"gui.grp.draw", {"line", "pline", "circle", "arc"}, {"rect", "polygon", "ellipse", "spline", "point", "hatch", "text", "symbols", "steel"}, true},
                          {"gui.grp.modify", {"select"}, {"move", "copy", "stretch", "rotate", "mirror", "scale", "trim", "extend", "fillet",
                                                          "chamfer", "array", "offset", "erase", "explode", "join", "break", "matchprop"}},
                          {"gui.grp.layers", {}, {"layprops", "laymcur", "laymatch", "layoff", "layfrz", "laylck", "layulk", "layiso", "layuniso", "layon"}, true, 2},
                          {"gui.grp.history", {"undo", "redo"}}}},
        {"gui.grp.dim", {{"gui.grp.dim", {"dimlinear", "dimaligned", "dimangular", "dimradius", "dimdiameter"}, {"leader", "text"}},
                         {"gui.grp.history", {"undo", "redo"}}}},
        {"gui.grp.utilities", {{"gui.grp.utilities", {"sewer", "util", "road", "waterway", "corridor", "building", "rebar", "city"}}, {"gui.grp.analyze", {"drive"}}, {"gui.grp.history", {"undo", "redo"}}}},
        {"gui.tab.survey", {{"gui.grp.points", {"ptimport", "ptexport"}}, {"gui.grp.earth", {"ewtin", "ewprofile", "ewsections", "ewcarto", "ewvolume"}},
                            {"gui.grp.deviation", {"devplan", "devvert", "devheight", "devaxis", "devpoint"}},
                            {"gui.grp.clean", {"devclear"}}, {"gui.grp.geo", {"geoplace"}}, {"gui.grp.history", {"undo", "redo"}}}},
        {"gui.tab.struct", {{"gui.grp.templates", {"steelframe", "stairtpl", "ramptpl", "rwalltpl"}}, {"gui.grp.km", {"steel"}},
                            {"gui.grp.geo", {"geoplace"}}, {"gui.grp.analyze", {"view3d", "drive"}}, {"gui.grp.history", {"undo", "redo"}}}},
        {"gui.grp.file", {{"gui.grp.file", {"new", "open", "save", "saveas", "import", "export"}}}},
        {"gui.tab.view", {{"gui.grp.model", {"view3d"}}, {"gui.grp.zoom", {"extents", "zoomin", "zoomout", "grid"}},
                          {"gui.grp.interface", {"palette"}}}},
    };
    for (const auto& pg : pages) {
        auto* page = new QWidget(ribbon_);
        page->setObjectName("ribbonPage");
        page->setAttribute(Qt::WA_StyledBackground, true);
        page->setProperty("i18n", pg.title);
        auto* row = new QHBoxLayout(page);
        row->setContentsMargins(10, 6, 10, 4);
        row->setSpacing(6);
        bool first = true;
        for (const auto& g : pg.groups) {
            if (!first) {
                auto* sep = new QFrame(page);
                sep->setObjectName("ribbonSep");
                sep->setFixedWidth(1);
                row->addWidget(sep);
            }
            first = false;
            auto* box = new QFrame(page);
            box->setObjectName("ribbonGroup");
            auto* v = new QVBoxLayout(box);
            v->setContentsMargins(2, 0, 2, 0);
            v->setSpacing(2);
            if (std::string(g.title) == "gui.grp.layers") {  // joriy qatlam ro'yxati (AutoCAD Layers paneli kabi)
                ribbonLayer_ = new QComboBox(box);
                ribbonLayer_->setMinimumWidth(190);
                ribbonLayer_->setObjectName("ribbonLayer");
                v->addWidget(ribbonLayer_);
                connect(ribbonLayer_, &QComboBox::activated, this, [this](int i) {
                    if (!updatingUi_ && i >= 0) runLine(QStringLiteral("CLAYER \"%1\"").arg(ribbonLayer_->itemData(i).toString()));
                });
            }
            auto* btns = new QHBoxLayout;
            btns->setSpacing(2);
            for (const char* k : g.keys) {
                auto* b = new QToolButton(box);
                b->setDefaultAction(act_[k]);
                b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
                b->setIconSize(QSize(30, 30));
                b->setMinimumWidth(56);
                b->setProperty("ribbonBtn", true);
                btns->addWidget(b);
            }
            if (!g.small.empty()) {  // kichik tugmalar: 2 qator, ikona yonida matn (AutoCAD lentasi kabi)
                auto* grid = new QGridLayout;
                grid->setSpacing(1);
                for (std::size_t i = 0; i < g.small.size(); ++i) {
                    auto* b = new QToolButton(box);
                    b->setDefaultAction(act_[g.small[i]]);
                    b->setToolButtonStyle(g.iconsOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
                    b->setIconSize(g.iconsOnly ? QSize(22, 22) : QSize(18, 18));
                    b->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
                    b->setProperty("ribbonSmall", true);
                    if (!g.iconsOnly) {  // matn qisqartirilmasin (retranslate dan keyin ham)
                        b->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
                        b->setMinimumWidth(118);
                    }
                    grid->addWidget(b, static_cast<int>(i) % g.rows, static_cast<int>(i) / g.rows);  // 3 qator — AutoCAD kabi
                }
                btns->addLayout(grid);
            }
            v->addLayout(btns);
            auto* title = new QLabel(box);
            title->setObjectName("groupTitle");
            title->setAlignment(Qt::AlignCenter);
            title->setProperty("i18n", g.title);
            v->addWidget(title);
            row->addWidget(box);
        }
        row->addStretch(1);
        ribbon_->addTab(page, QString());
    }
}

void MainWindow::buildModuleBars() {
    // Modul rejimi: chapda modullar paneli
    rail_ = new QToolBar(this);
    rail_->setObjectName("rail");
    rail_->setMovable(false);
    rail_->setOrientation(Qt::Vertical);
    rail_->setIconSize(QSize(40, 40));
    rail_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    addToolBar(Qt::LeftToolBarArea, rail_);
    const struct { const char* key; const char* icon; const char* color; } mods[] = {
        {"gui.module.drawing", "drawing", "#4B4FD1"}, {"gui.module.survey", "survey", "#0E9F8E"},
        {"gui.module.terrain", "terrain", "#3E9B3A"}, {"gui.module.road", "road", "#E7881B"},
        {"gui.module.pipes", "pipes", "#1F7BE0"},     {"gui.module.bim", "bim", "#D9466A"},
        {"gui.module.city", "city", "#8E5BD6"}};
    auto* grp = new QActionGroup(this);
    for (const auto& m : mods) {
        auto* a = rail_->addAction(makeIcon(m.icon, Qt::white, QColor(m.color)), QString());
        a->setCheckable(true);
        a->setProperty("i18n", m.key);
        grp->addAction(a);
        const QString key = m.key;
        connect(a, &QAction::triggered, this, [this, key, a] {
            if (key == "gui.module.bim") {
                openBuilding();
                rail_->actions().first()->setChecked(true);
            } else if (key == "gui.module.pipes") {
                openSewer();
                rail_->actions().first()->setChecked(true);
            } else if (key == "gui.module.road") {
                openCorridor();
                rail_->actions().first()->setChecked(true);
            } else if (key == "gui.module.terrain") {
                openEarthwork(0);
                rail_->actions().first()->setChecked(true);
            } else if (key == "gui.module.survey") {
                openSurvey();
                rail_->actions().first()->setChecked(true);
            } else if (key == "gui.module.city") {
                openCity();
                rail_->actions().first()->setChecked(true);
            } else if (key != "gui.module.drawing") {
                statusBar()->showMessage(t("gui.module.soon", {QString::fromStdString(loc_.tr(key.toStdString()))}), 4000);
                rail_->actions().first()->setChecked(true);
            }
            Q_UNUSED(a);
        });
    }
    rail_->actions().first()->setChecked(true);

    // Modul rejimi: yuqorida kontekst asboblari (bo'limning o'z buyruqlari)
    context_ = new QToolBar(this);
    context_->setObjectName("context");
    context_->setMovable(false);
    context_->setIconSize(QSize(18, 18));
    context_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    for (const char* k : {"select", "line", "pline", "circle", "arc", "point", "text", "move", "copy", "rotate", "offset", "trim", "fillet", "erase", "dimlinear", "dimradius", "undo", "redo", "extents"})
        context_->addAction(act_[k]);
    addToolBar(Qt::TopToolBarArea, context_);

    // Fokus rejimi: chizma ustida suzuvchi vertikal panel
    floating_ = new QToolBar(view_);
    floating_->setObjectName("floating");
    floating_->setOrientation(Qt::Vertical);
    floating_->setIconSize(QSize(22, 22));
    for (const char* k : {"select", "line", "pline", "circle", "arc", "text", "move", "trim", "dimlinear", "erase", "extents", "palette"})
        floating_->addAction(act_[k]);
    floating_->move(16, 16);
    floating_->adjustSize();
}

void MainWindow::buildDocks() {
    // Qatlamlar
    layersDock_ = new QDockWidget(this);
    layersDock_->setObjectName("layersDock");
    layersDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    auto* lw = new QWidget(layersDock_);
    auto* lv = new QVBoxLayout(lw);
    lv->setContentsMargins(8, 8, 8, 8);
    layers_ = new QTreeWidget(lw);
    layers_->setColumnCount(5);  // nom · ko'rinish · muzlatish · qulf · rang (AutoCAD Layer Properties kabi)
    layers_->setHeaderHidden(false);
    layers_->setRootIsDecorated(false);
    layers_->header()->setStretchLastSection(false);
    layers_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layers_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layers_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layers_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layers_->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    lv->addWidget(layers_);
    auto* addLayer = new QPushButton(lw);
    addLayer->setObjectName("accent");
    addLayer->setProperty("i18n", "gui.layers.new");
    lv->addWidget(addLayer);
    layersDock_->setWidget(lw);
    layersDock_->setMinimumWidth(240);
    addDockWidget(Qt::LeftDockWidgetArea, layersDock_);
    connect(layers_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
        if (updatingUi_) return;
        const QString name = it->data(0, Qt::UserRole).toString();
        QString kw;
        if (col == 1) kw = QString::fromStdString(loc_.keywordName(it->checkState(1) == Qt::Checked ? "ON" : "OFF"));
        if (col == 2) kw = QString::fromStdString(loc_.keywordName(it->checkState(2) == Qt::Checked ? "FREEZE" : "THAW"));
        if (col == 3) kw = QString::fromStdString(loc_.keywordName(it->checkState(3) == Qt::Checked ? "LOCK" : "UNLOCK"));
        if (!kw.isEmpty()) runLine(QStringLiteral("LAYER \"%1\" %2").arg(name, kw));
    });
    connect(layers_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int col) {
        const QString name = it->data(0, Qt::UserRole).toString();
        if (col == 4) {  // rang (ACI 1..255)
            bool ok = false;
            const auto L = doc_.layer(name.toStdString());
            const int c = QInputDialog::getInt(this, t("gui.layers.color"), name, L ? L->color : 7, 1, 255, 1, &ok);
            if (ok) runLine(QStringLiteral("LAYER \"%1\" %2").arg(name).arg(c));
            return;
        }
        runLine(QStringLiteral("CLAYER \"%1\"").arg(name));
    });
    connect(addLayer, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, t("gui.layers.new"), t("gui.layers.name"), QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !n.isEmpty()) runLine(QStringLiteral("LAYER \"%1\"").arg(QString(n).remove('"')));
    });

    // Xossalar
    propsDock_ = new QDockWidget(this);
    propsDock_->setObjectName("propsDock");
    propsDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    propsBox_ = new QWidget(propsDock_);
    auto* pv = new QVBoxLayout(propsBox_);
    pv->setContentsMargins(10, 10, 10, 10);
    pv->setSpacing(8);
    propsCount_ = new QLabel(propsBox_);
    propsCount_->setObjectName("title");
    pv->addWidget(propsCount_);
    auto* form = new QFormLayout;
    propsLayer_ = new QComboBox(propsBox_);
    propsColor_ = new QComboBox(propsBox_);
    auto* l1 = new QLabel(propsBox_); l1->setProperty("i18n", "gui.props.layer"); l1->setObjectName("muted");
    auto* l2 = new QLabel(propsBox_); l2->setProperty("i18n", "gui.props.color"); l2->setObjectName("muted");
    form->addRow(l1, propsLayer_);
    form->addRow(l2, propsColor_);
    pv->addLayout(form);
    propsList_ = new QPlainTextEdit(propsBox_);
    propsList_->setReadOnly(true);
    propsList_->setFont(QFont(QStringLiteral("DejaVu Sans Mono"), 9));
    pv->addWidget(propsList_, 1);
    propsDock_->setWidget(propsBox_);
    propsDock_->setMinimumWidth(270);
    addDockWidget(Qt::RightDockWidgetArea, propsDock_);

    // Genplan belgilari palitrasi (rangli): bosilsa — joylash asbobi
    symDock_ = new QDockWidget(this);
    symDock_->setObjectName("symbolsDock");
    symDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    symPal_ = new SymbolPalette(loc_, !theme_.lightCanvas, symDock_);
    symDock_->setWidget(symPal_);
    addDockWidget(Qt::RightDockWidgetArea, symDock_);
    tabifyDockWidget(propsDock_, symDock_);
    propsDock_->raise();
    // Metall konstruksiyalar (КМ)
    steelDock_ = new QDockWidget(this);
    steelDock_->setObjectName("steelDock");
    steelDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    steelPal_ = new SteelPanel(loc_, !theme_.lightCanvas, steelDock_);
    steelDock_->setWidget(steelPal_);
    addDockWidget(Qt::RightDockWidgetArea, steelDock_);
    tabifyDockWidget(symDock_, steelDock_);
    propsDock_->raise();
    connect(steelPal_, &SteelPanel::drawMember, this, [this](const QString& p) {
        view_->setSteelProfile(p);
        view_->setTool(Tool::SteelBeam);
        view_->setFocus();
    });
    connect(steelPal_, &SteelPanel::drawSection, this, [this](const QString& p) {
        view_->setSteelProfile(p);
        view_->setTool(Tool::SteelSec);
        view_->setFocus();
    });
    connect(steelPal_, &SteelPanel::specRequested, this, [this] {
        const BBox b = doc_.extents(true);
        const double x = b.valid() ? b.max.x + 5 : 0, y = b.valid() ? b.max.y : 0;
        runLine(QStringLiteral("STEELSPEC %1,%2").arg(x).arg(y));
        view_->zoomExtents();
    });
    connect(symPal_, &SymbolPalette::symbolChosen, this, [this](const QString& key, double k, double rot) {
        view_->setSymbol(key, k, rot);
        view_->setTool(Tool::Symbol);
        view_->setFocus();
    });
    connect(propsLayer_, &QComboBox::activated, this, [this](int i) {
        if (updatingUi_ || view_->selection().empty()) return;
        QStringList ids;
        for (EntityId id : view_->selection()) ids << QString::number(id);
        runLine(QStringLiteral("CHPROP %1 %2=\"%3\"").arg(ids.join(','), QString::fromStdString(loc_.keywordName("LAYER")), propsLayer_->itemText(i)));
    });
    connect(propsColor_, &QComboBox::activated, this, [this](int i) {
        if (updatingUi_ || view_->selection().empty()) return;
        QStringList ids;
        for (EntityId id : view_->selection()) ids << QString::number(id);
        const int c = propsColor_->itemData(i).toInt();
        const QString val = c == kColorByLayer ? QString::fromStdString(loc_.keywordName("BYLAYER")) : QString::number(c);
        runLine(QStringLiteral("CHPROP %1 %2=%3").arg(ids.join(','), QString::fromStdString(loc_.keywordName("COLOR")), val));
    });

    // Buyruq satri
    cmdDock_ = new QDockWidget(this);
    cmdDock_->setObjectName("cmdDock");
    cmdDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    auto* cw = new QWidget(cmdDock_);
    auto* cv = new QVBoxLayout(cw);
    cv->setContentsMargins(8, 4, 8, 8);
    cv->setSpacing(4);
    history_ = new QPlainTextEdit(cw);
    history_->setReadOnly(true);
    history_->setMaximumBlockCount(2000);
    history_->setFont(QFont(QStringLiteral("DejaVu Sans Mono"), 10));
    history_->setMinimumHeight(70);
    cv->addWidget(history_, 1);
    auto* row = new QHBoxLayout;
    prompt_ = new QLabel(cw);
    prompt_->setObjectName("muted");
    cmdEdit_ = new QLineEdit(cw);
    cmdEdit_->setFont(QFont(QStringLiteral("DejaVu Sans Mono"), 10));
    cmdEdit_->installEventFilter(this);
    row->addWidget(prompt_);
    row->addWidget(cmdEdit_, 1);
    cv->addLayout(row);
    cmdDock_->setWidget(cw);
    addDockWidget(Qt::BottomDockWidgetArea, cmdDock_);
    connect(cmdEdit_, &QLineEdit::returnPressed, this, &MainWindow::submitCommandLine);
    // Viewport ichida harf terilsa — buyruq satriga o'tadi (AutoCAD odati)
    view_->installEventFilter(this);
    resizeDocks({layersDock_, propsDock_}, {250, 290}, Qt::Horizontal);
    resizeDocks({cmdDock_}, {150}, Qt::Vertical);
}

void MainWindow::buildStatusBar() {
    // AutoCAD holat satri: MODEL | koordinata ... TO'R QADAM ORTO QUTB OSNAP OTRACK DYN | 1:100 | ⚙
    auto* sb = statusBar();
    modelLbl_ = new QLabel(sb);
    modelLbl_->setObjectName("modelTab");
    modelLbl_->setStyleSheet(QStringLiteral("QLabel#modelTab{font-weight:700;padding:1px 8px;border:1px solid palette(mid);border-radius:3px;}"));
    sb->addWidget(modelLbl_);
    coords_ = new QLabel(sb);
    coords_->setMinimumWidth(240);
    coords_->setFont(QFont(QStringLiteral("DejaVu Sans Mono"), 9));
    sb->addWidget(coords_);

    auto addToggle = [&](QToolButton*& b, const char* name, Qt::Key key, auto setter, bool on) {
        b = new QToolButton(sb);
        b->setObjectName(QString::fromLatin1(name));
        b->setCheckable(true);
        b->setChecked(on);
        b->setAutoRaise(true);
        sb->addPermanentWidget(b);
        auto* a = new QAction(this);
        a->setShortcut(QKeySequence(key));
        a->setShortcutContext(Qt::ApplicationShortcut);
        addAction(a);
        connect(a, &QAction::triggered, b, &QToolButton::toggle);
        if (setter)
            connect(b, &QToolButton::toggled, this, [this, setter](bool v) {
                if (!updatingUi_) (view_->*setter)(v);
            });
    };
    // TO'R (F7) — mavjud "grid" amali bilan bir xil holatda
    gridBtn_ = new QToolButton(sb);
    gridBtn_->setObjectName("grid");
    gridBtn_->setCheckable(true);
    gridBtn_->setAutoRaise(true);
    gridBtn_->setChecked(view_->gridVisible());
    sb->addPermanentWidget(gridBtn_);
    connect(gridBtn_, &QToolButton::toggled, act_["grid"], &QAction::setChecked);
    connect(act_["grid"], &QAction::toggled, gridBtn_, &QToolButton::setChecked);
    // QADAM (F9) + qadam menyusi
    addToggle(snapBtn_, "snap", Qt::Key_F9, &Viewport::setGridSnap, view_->gridSnap());
    stepMenu_ = new QMenu(snapBtn_);
    auto* stepGrp = new QActionGroup(stepMenu_);
    for (double st : {0.01, 0.05, 0.1, 0.5, 1.0, 5.0, 10.0}) {
        auto* a = stepMenu_->addAction(QString::number(st));
        a->setCheckable(true);
        a->setChecked(std::abs(st - view_->snapUnit()) < 1e-12);
        stepGrp->addAction(a);
        connect(a, &QAction::triggered, this, [this, st] {
            view_->setSnapUnit(st);
            if (!snapBtn_->isChecked()) snapBtn_->setChecked(true);
            retranslateStatus();
        });
    }
    snapBtn_->setMenu(stepMenu_);
    snapBtn_->setPopupMode(QToolButton::MenuButtonPopup);
    auto* sep = new QFrame(sb);
    sep->setFrameShape(QFrame::VLine);
    sb->addPermanentWidget(sep);
    // ORTO (F8, 90°) va QUTB (F10) + burchak menyusi
    addToggle(orthoBtn_, "ortho", Qt::Key_F8, &Viewport::setOrtho, view_->ortho());
    addToggle(polarBtn_, "polar", Qt::Key_F10, &Viewport::setPolar, view_->polar());
    polarMenu_ = new QMenu(polarBtn_);
    auto* angGrp = new QActionGroup(polarMenu_);
    for (double ang : {90.0, 45.0, 30.0, 22.5, 18.0, 15.0, 10.0, 5.0}) {
        auto* a = polarMenu_->addAction(QStringLiteral("%1°").arg(ang));
        a->setCheckable(true);
        a->setChecked(std::abs(ang - view_->polarStep()) < 1e-9);
        angGrp->addAction(a);
        connect(a, &QAction::triggered, this, [this, ang] {
            view_->setPolarStep(ang);
            if (!polarBtn_->isChecked()) polarBtn_->setChecked(true);
            retranslateStatus();
        });
    }
    polarBtn_->setMenu(polarMenu_);
    polarBtn_->setPopupMode(QToolButton::MenuButtonPopup);
    connect(view_, &Viewport::orthoPolarChanged, this, [this](bool o, bool p) {
        updatingUi_ = true;
        orthoBtn_->setChecked(o);
        polarBtn_->setChecked(p);
        updatingUi_ = false;
    });
    // OSNAP (F3) — umumiy kalit, menyuda rejimlar (magnitlar)
    addToggle(osnapBtn_, "osnap", Qt::Key_F3, &Viewport::setOsnap, view_->osnap());
    osnapMenu_ = new QMenu(osnapBtn_);
    for (const auto& sn : kSnaps) {
        auto* a = osnapMenu_->addAction(QString());
        a->setCheckable(true);
        a->setChecked(view_->snapModes() & sn.mode);
        const unsigned m = sn.mode;
        connect(a, &QAction::toggled, this, [this, m](bool on) {
            view_->setSnapModes(on ? (view_->snapModes() | m) : (view_->snapModes() & ~m));
            if (on && !osnapBtn_->isChecked()) osnapBtn_->setChecked(true);
        });
        snapActs_.insert(sn.mode, a);
    }
    osnapBtn_->setMenu(osnapMenu_);
    osnapBtn_->setPopupMode(QToolButton::MenuButtonPopup);
    addToggle(otrackBtn_, "otrack", Qt::Key_F11, &Viewport::setOtrack, view_->otrack());
    addToggle(dynBtn_, "dyn", Qt::Key_F12, &Viewport::setDyn, view_->dyn());
    auto* sep2 = new QFrame(sb);
    sep2->setFrameShape(QFrame::VLine);
    sb->addPermanentWidget(sep2);
    // Izoh masshtabi: o'lcham/matn balandligi = 2,5 mm × masshtab
    annoBox_ = new QComboBox(sb);
    annoBox_->setObjectName("annoScale");
    annoBox_->addItem(QString(), 0.0);
    for (int d : {1, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000}) annoBox_->addItem(QStringLiteral("1:%1").arg(d), double(d));
    sb->addPermanentWidget(annoBox_);
    connect(annoBox_, &QComboBox::activated, this, [this](int i) { view_->setAnnoScale(annoBox_->itemData(i).toDouble()); });
    // ⚙ sozlamalar: nishon o'lchami
    gearBtn_ = new QToolButton(sb);
    gearBtn_->setObjectName("gear");
    gearBtn_->setAutoRaise(true);
    gearBtn_->setText(QStringLiteral("⚙"));
    gearBtn_->setPopupMode(QToolButton::InstantPopup);
    gearMenu_ = new QMenu(gearBtn_);
    gearBtn_->setMenu(gearMenu_);
    sb->addPermanentWidget(gearBtn_);
    connect(gearMenu_, &QMenu::aboutToShow, this, [this] {
        gearMenu_->clear();
        auto* cm = gearMenu_->addMenu(t("gui.status.cursor"));
        auto* g = new QActionGroup(cm);
        for (int pct : {3, 5, 10, 25, 50, 100}) {
            auto* a = cm->addAction(QStringLiteral("%1%").arg(pct));
            a->setCheckable(true);
            a->setChecked(view_->crosshairSize() == pct);
            g->addAction(a);
            connect(a, &QAction::triggered, this, [this, pct] { view_->setCrosshairSize(pct); });
        }
        gearMenu_->addAction(t("gui.act.view3d"), this, [this] { show3D(true); });
    });

    modeBox_ = new QComboBox(sb);
    langBox_ = new QComboBox(sb);
    sb->addPermanentWidget(modeBox_);
    sb->addPermanentWidget(langBox_);
    connect(modeBox_, &QComboBox::activated, this, [this](int i) { setMode(static_cast<UiMode>(modeBox_->itemData(i).toInt())); });
    connect(langBox_, &QComboBox::activated, this, [this](int i) { setLanguage(langBox_->itemData(i).toString()); });
    connect(view_, &Viewport::request3D, this, [this] { show3D(true); });
    connect(view_, &Viewport::repeatRequested, this, [this] {
        if (cmdHistory_.isEmpty()) return;  // oxirgi buyruq satrga qo'yiladi — Enter bilan bajariladi
        cmdEdit_->setText(cmdHistory_.last());
        cmdEdit_->setFocus();
    });
}

QToolButton* MainWindow::statusButton(const char* name) const {
    return statusBar()->findChild<QToolButton*>(QString::fromLatin1(name));
}

void MainWindow::retranslateStatus() {
    auto set = [this](QToolButton* b, const char* key, const QString& keyName, const QString& extra = {}) {
        b->setText(t(key));
        b->setToolTip(t(key) + " (" + keyName + ")" + extra);
    };
    modelLbl_->setText(t("gui.status.model"));
    set(gridBtn_, "gui.status.grid", "F7");
    set(snapBtn_, "gui.status.snap", "F9", QStringLiteral(" — %1").arg(view_->snapUnit()));
    set(orthoBtn_, "gui.status.ortho", "F8", QStringLiteral(" — 90°"));
    set(polarBtn_, "gui.status.polar", "F10", QStringLiteral(" — %1°").arg(view_->polarStep()));
    set(osnapBtn_, "gui.status.osnap", "F3");
    set(otrackBtn_, "gui.status.otrack", "F11");
    set(dynBtn_, "gui.status.dyn", "F12");
    for (const auto& s : kSnaps) snapActs_[s.mode]->setText(QString::fromStdString(loc_.keywordName(s.name)));
    annoBox_->setItemText(0, t("gui.status.annoauto"));
    annoBox_->setToolTip(t("gui.status.anno"));
    gearBtn_->setToolTip(t("gui.status.settings"));
}

void MainWindow::setMode(UiMode m) {
    mode_ = m;
    theme_ = themeFor(m);
    ribbon_->setVisible(m == UiMode::Classic);
    rail_->setVisible(m == UiMode::Module);
    context_->setVisible(m == UiMode::Module);
    floating_->setVisible(m == UiMode::Focus);
    layersDock_->setVisible(m != UiMode::Focus);
    propsDock_->setVisible(m != UiMode::Focus);
    cmdDock_->setVisible(true);
    applyTheme();
    if (modeBox_) {
        updatingUi_ = true;
        modeBox_->setCurrentIndex(modeBox_->findData(static_cast<int>(m)));
        updatingUi_ = false;
    }
}

void MainWindow::applyTheme() {
    qApp->setStyleSheet(theme_.styleSheet);
    view_->setTheme(theme_);
    for (auto* a : act_) a->setIcon(icon(a->data().toString()));
    floating_->adjustSize();
    refreshLayers();
}

void MainWindow::setLanguage(const QString& code) {
    runLine("LANG " + code, false);
}

void MainWindow::retranslate() {
    if (viewTabs_) {
        viewTabs_->setTabText(0, t("gui.view.2d"));
        viewTabs_->setTabText(1, t("gui.view.3d"));
    }
    updatingUi_ = true;
    for (const auto& d : kActs) {
        act_[d.key]->setText(t(d.label));
        act_[d.key]->setToolTip(t(d.label));
    }
    // ribbon_ sahifa nomlari va guruh sarlavhalari
    for (int i = 0; i < ribbon_->count(); ++i) {
        QWidget* page = ribbon_->widget(i);
        ribbon_->setTabText(i, t(page->property("i18n").toByteArray().constData()));
        for (auto* lbl : page->findChildren<QLabel*>()) lbl->setText(t(lbl->property("i18n").toByteArray().constData()));
    }
    for (auto* b : ribbon_->findChildren<QToolButton*>())
        if (b->property("ribbonBtn").toBool())
            b->setMinimumWidth(std::max(56, static_cast<int>(b->fontMetrics().horizontalAdvance(b->text())) + 14));
    for (auto* a : rail_->actions()) a->setText(t(a->property("i18n").toByteArray().constData()));
    for (QWidget* w : findChildren<QWidget*>())
        if (auto* b = qobject_cast<QPushButton*>(w); b && b->property("i18n").isValid()) b->setText(t(b->property("i18n").toByteArray().constData()));
    for (QLabel* l : propsBox_->findChildren<QLabel*>())
        if (l->property("i18n").isValid()) l->setText(t(l->property("i18n").toByteArray().constData()));
    layersDock_->setWindowTitle(t("gui.panel.layers"));
    if (symDock_) symDock_->setWindowTitle(t("gui.panel.symbols"));
    if (steelDock_) steelDock_->setWindowTitle(t("gui.panel.steel"));
    if (symPal_) symPal_->retranslate();
    propsDock_->setWindowTitle(t("gui.panel.props"));
    cmdDock_->setWindowTitle(t("gui.panel.command"));
    retranslateStatus();
    modeBox_->clear();
    modeBox_->addItem(t("gui.mode.classic"), static_cast<int>(UiMode::Classic));
    modeBox_->addItem(t("gui.mode.module"), static_cast<int>(UiMode::Module));
    modeBox_->addItem(t("gui.mode.focus"), static_cast<int>(UiMode::Focus));
    modeBox_->setCurrentIndex(modeBox_->findData(static_cast<int>(mode_)));
    langBox_->clear();
    for (const auto& code : loc_.languages()) langBox_->addItem(QString::fromStdString(loc_.languageName(code)), QString::fromStdString(code));
    langBox_->setCurrentIndex(langBox_->findData(QString::fromStdString(loc_.language())));
    cmdEdit_->setPlaceholderText(t("gui.palette.hint"));
    updatingUi_ = false;
    prompt_->setText(t("gui.prompt.idle"));
    refreshLayers();
    refreshProperties();
    refreshTitle();
}

void MainWindow::refreshLayers() {
    if (!layers_) return;
    updatingUi_ = true;
    layers_->setHeaderLabels({t("gui.layers.h.name"), QString::fromUtf8("👁"), QString::fromUtf8("❄"), QString::fromUtf8("🔒"), t("gui.layers.h.color")});
    layers_->clear();
    const std::string cur = interp_->currentLayer();
    for (const auto& [name, l] : doc_.layers()) {
        auto* it = new QTreeWidgetItem(layers_);
        const QString qn = QString::fromStdString(name);
        it->setText(0, name == cur ? QString::fromUtf8("● ") + qn : qn);
        if (name == cur) it->setToolTip(0, t("gui.layers.current"));
        it->setData(0, Qt::UserRole, qn);
        it->setIcon(0, swatch(aciColor(l.color, theme_.lightCanvas)));
        QFont f = it->font(0);
        f.setBold(name == cur);
        it->setFont(0, f);
        it->setCheckState(1, l.visible ? Qt::Checked : Qt::Unchecked);
        it->setCheckState(2, l.frozen ? Qt::Checked : Qt::Unchecked);
        it->setCheckState(3, l.locked ? Qt::Checked : Qt::Unchecked);
        it->setIcon(4, swatch(aciColor(l.color, theme_.lightCanvas)));
        it->setText(4, QString::number(l.color));
        it->setToolTip(1, QString::fromStdString(loc_.keywordName("ON")) + " / " + QString::fromStdString(loc_.keywordName("OFF")));
        it->setToolTip(2, QString::fromStdString(loc_.keywordName("FREEZE")));
        it->setToolTip(3, QString::fromStdString(loc_.keywordName("LOCK")));
        it->setToolTip(4, t("gui.layers.color"));
        if (name == cur) layers_->setCurrentItem(it);
    }
    if (ribbonLayer_) {
        ribbonLayer_->clear();
        for (const auto& [name, l] : doc_.layers()) {
            ribbonLayer_->addItem(swatch(aciColor(l.color, theme_.lightCanvas)),
                                  QString::fromStdString(name) + (l.visible ? "" : "  (" + QString::fromStdString(loc_.keywordName("OFF")) + ")") +
                                      (l.frozen ? "  ❄" : "") + (l.locked ? "  🔒" : ""),
                                  QString::fromStdString(name));
            if (name == cur) ribbonLayer_->setCurrentIndex(ribbonLayer_->count() - 1);
        }
    }
    updatingUi_ = false;
}

void MainWindow::refreshProperties() {
    if (!propsCount_) return;
    updatingUi_ = true;
    const auto& sel = view_->selection();
    propsLayer_->clear();
    propsColor_->clear();
    propsList_->clear();
    if (sel.empty()) {
        propsCount_->setText(t("gui.props.none"));
        propsLayer_->setEnabled(false);
        propsColor_->setEnabled(false);
        updatingUi_ = false;
        return;
    }
    propsCount_->setText(t("gui.props.count", {QString::number(sel.size())}));
    propsLayer_->setEnabled(true);
    propsColor_->setEnabled(true);
    std::set<std::string> layersUsed;
    std::set<int> colorsUsed;
    QStringList lines;
    for (EntityId id : sel) {
        const IEntity* e = doc_.find(id);
        if (!e) continue;
        layersUsed.insert(e->layer());
        colorsUsed.insert(e->color());
        if (lines.size() < 60) lines << QString("#%1  %2").arg(id).arg(QString::fromStdString(e->describe()));
    }
    for (const auto& [name, l] : doc_.layers())
        propsLayer_->addItem(swatch(aciColor(l.color, theme_.lightCanvas)), QString::fromStdString(name));
    propsLayer_->setCurrentIndex(layersUsed.size() == 1 ? propsLayer_->findText(QString::fromStdString(*layersUsed.begin())) : -1);
    propsColor_->addItem(t("gui.props.bylayer"), kColorByLayer);
    for (int c = 1; c <= 9; ++c) propsColor_->addItem(swatch(aciColor(c, theme_.lightCanvas)), QString::number(c), c);
    if (colorsUsed.size() == 1) {
        const int idx = propsColor_->findData(*colorsUsed.begin());
        propsColor_->setCurrentIndex(idx);
        if (idx < 0) { propsColor_->addItem(swatch(aciColor(*colorsUsed.begin(), theme_.lightCanvas)), QString::number(*colorsUsed.begin()), *colorsUsed.begin()); propsColor_->setCurrentIndex(propsColor_->count() - 1); }
    } else {
        propsColor_->setCurrentIndex(-1);
    }
    propsList_->setPlainText(lines.join('\n'));
    updatingUi_ = false;
}

void MainWindow::refreshTitle() {
    const QString file = QString::fromStdString(interp_->currentFile());
    const QString name = file.isEmpty() ? t("gui.untitled") : QFileInfo(file).fileName();
    setWindowTitle(QStringLiteral("%1%2 — AsiaCAD").arg(name, stack_.isDirty() ? " *" : ""));
}

void MainWindow::appendHistory(const QString& text, const QColor& color) {
    for (const QString& line : text.split('\n', Qt::SkipEmptyParts)) {
        history_->appendHtml(QStringLiteral("<span style='color:%1'>%2</span>").arg(color.name(), line.toHtmlEscaped()));
    }
}

bool MainWindow::runLine(const QString& line, bool echo) {
    if (echo) appendHistory("› " + line, theme_.accent);
    const QString langBefore = QString::fromStdString(loc_.language());
    out_.str({});
    out_.clear();
    const bool cont = interp_->run(line.toStdString());
    const QString out = QString::fromStdString(out_.str());
    if (!out.isEmpty()) {
        const QString errPrefix = QString::fromStdString(loc_.tr("err.prefix", {""})).trimmed();
        for (const QString& l : out.split('\n', Qt::SkipEmptyParts))
            appendHistory(l, (!errPrefix.isEmpty() && l.startsWith(errPrefix)) ? QColor("#FF6B81") : palette().color(QPalette::WindowText));
    }
    const auto toks = line.trimmed().split(QRegularExpression("\\s+"));
    const std::string canon = toks.isEmpty() ? std::string{} : loc_.resolveCommand(toks.first().toStdString());
    view_->documentChanged();
    if (canon == "OPEN" || canon == "NEW" || canon == "IMPORT") { view_->setSelection({}); view_->zoomExtents(); }
    if (QString::fromStdString(loc_.language()) != langBefore) retranslate();
    refreshLayers();
    refreshProperties();
    refreshTitle();
    if (is3D()) rebuild3D(true);
    if (!cont) close();
    return cont;
}

SewerDialog* MainWindow::openSewer() {
    if (!sewer_) {
        auto pick = [this]() {
            std::vector<Vec3> v;
            for (EntityId id : view_->selection())
                if (auto* pl = dynamic_cast<const PolylineEntity*>(doc_.find(id))) {
                    v = pl->vertices();
                    if (pl->isClosed() && !v.empty()) v.push_back(v.front());
                    break;
                }
            return v;
        };
        sewer_ = new SewerDialog(loc_, pick, !theme_.lightCanvas, this);
        sewer_->setStyleSheet(theme_.styleSheet);
        connect(sewer_, &SewerDialog::drawRequested, this, [this](const QString& path, bool profile) {
            runLine(QStringLiteral("SEWER \"%1\"%2").arg(path, profile ? QString() : QStringLiteral(" -")));
            view_->zoomExtents();
        });
    }
    sewer_->show();
    sewer_->raise();
    sewer_->activateWindow();
    return sewer_;
}

BuildingDialog* MainWindow::openBuilding() {
    if (!building_) {
        building_ = new BuildingDialog(loc_, doc_, !theme_.lightCanvas, this);
        building_->setStyleSheet(theme_.styleSheet);
        connect(building_, &BuildingDialog::scratchRequested, this, [this] { openScratch("building"); });
        connect(building_, &BuildingDialog::locateRequested, this, &MainWindow::locate);
        connect(building_, &BuildingDialog::rebarRequested, this, [this] { openRebar(); });
        connect(building_, &BuildingDialog::sampleRequested, this, [this] { makeSampleBuilding(); });
        connect(building_, &BuildingDialog::open3DRequested, this, [this] {
            building_->hide();
            show3D(true);
        });
        connect(building_, &BuildingDialog::tableRequested, this, [this](const QString& path) {
            const BBox b = doc_.extents(true);
            const double x = b.valid() ? b.max.x + 3 : 0, y = b.valid() ? b.max.y : 0;
            runLine(QStringLiteral("BUILDING \"%1\" %2,%3").arg(path, Viewport::fmtPoint({x, 0, 0}).section(',', 0, 0),
                                                                 Viewport::fmtPoint({y, 0, 0}).section(',', 0, 0)));
            view_->zoomExtents();
        });
    } else {
        building_->reread();
    }
    building_->show();
    building_->raise();
    building_->activateWindow();
    return building_;
}

RebarDialog* MainWindow::openRebar() {
    if (!rebar_) {
        rebar_ = new RebarDialog(loc_, doc_, !theme_.lightCanvas, this);
        rebar_->setStyleSheet(theme_.styleSheet);
        connect(rebar_, &RebarDialog::sampleRequested, this, [this] { makeSampleBuilding(); });
        connect(rebar_, &RebarDialog::scheduleRequested, this, [this](const QString& path) {
            const BBox b = doc_.extents(true);
            const double x = b.valid() ? b.max.x + 3 : 0, y = b.valid() ? b.max.y : 0;
            runLine(QStringLiteral("REBAR \"%1\" %2,%3").arg(path, Viewport::fmtPoint({x, 0, 0}).section(',', 0, 0),
                                                              Viewport::fmtPoint({y, 0, 0}).section(',', 0, 0)));
            view_->zoomExtents();
        });
    }
    if (building_) rebar_->setBuilding(building_->params(), building_->layerMap(), building_->floorPlans());
    else rebar_->setBuilding(bim::BuildingParams{}, bim::autoLayerMap(doc_));
    rebar_->show();
    rebar_->raise();
    rebar_->activateWindow();
    return rebar_;
}

UtilityDialog* MainWindow::openUtility() {
    if (!util_) {
        util_ = new UtilityDialog(loc_, doc_, !theme_.lightCanvas, this);
        util_->setStyleSheet(theme_.styleSheet);
        connect(util_, &UtilityDialog::scratchRequested, this, [this] { openScratch("util"); });
        connect(util_, &UtilityDialog::locateRequested, this, &MainWindow::locate);
        connect(util_, &UtilityDialog::drawRequested, this, [this](const QString& path) {
            runLine(QStringLiteral("UTIL \"%1\"").arg(path));
            view_->zoomExtents();
        });
    } else {
        util_->reread();
    }
    util_->show();
    util_->raise();
    util_->activateWindow();
    return util_;
}

RoadDialog* MainWindow::openRoad() {
    if (!road_) {
        road_ = new RoadDialog(loc_, doc_, !theme_.lightCanvas, this);
        road_->setStyleSheet(theme_.styleSheet);
        connect(road_, &RoadDialog::scratchRequested, this, [this] { openScratch("road"); });
        connect(road_, &RoadDialog::locateRequested, this, &MainWindow::locate);
        connect(road_, &RoadDialog::drawRequested, this, [this](const QString& path) {
            const BBox b = doc_.extents(true);
            const double x = b.valid() ? b.min.x : 0, y = b.valid() ? b.min.y - 30 : -30;
            runLine(QStringLiteral("ROAD \"%1\" %2,%3").arg(path, Viewport::fmtPoint({x, 0, 0}).section(',', 0, 0),
                                                              Viewport::fmtPoint({y, 0, 0}).section(',', 0, 0)));
            view_->zoomExtents();
        });
    } else {
        road_->reread();
    }
    road_->show();
    road_->raise();
    road_->activateWindow();
    return road_;
}

CorridorDialog* MainWindow::openCorridor() {
    if (!corridor_) {
        corridor_ = new CorridorDialog(loc_, doc_, !theme_.lightCanvas, this);
        corridor_->setStyleSheet(theme_.styleSheet);
        connect(corridor_, &CorridorDialog::scratchRequested, this, [this] { openScratch("corridor"); });
        connect(corridor_, &CorridorDialog::locateRequested, this, &MainWindow::locate);
        connect(corridor_, &CorridorDialog::drawRequested, this, [this](const QString& path) {
            runLine(QStringLiteral("CORRIDOR \"%1\" +").arg(path));
            view_->zoomExtents();
        });
    } else {
        corridor_->reread();
    }
    corridor_->show();
    corridor_->raise();
    corridor_->activateWindow();
    return corridor_;
}

WaterwayDialog* MainWindow::openWaterway() {
    if (!waterway_) {
        waterway_ = new WaterwayDialog(loc_, doc_, !theme_.lightCanvas, this);
        waterway_->setStyleSheet(theme_.styleSheet);
        connect(waterway_, &WaterwayDialog::scratchRequested, this, [this] { openScratch("waterway"); });
        connect(waterway_, &WaterwayDialog::locateRequested, this, &MainWindow::locate);
        connect(waterway_, &WaterwayDialog::drawRequested, this, [this](const QString& path) {
            const BBox b = doc_.extents(true);
            const double x = b.valid() ? b.min.x : 0, y = b.valid() ? b.min.y - 30 : -30;
            runLine(QStringLiteral("WATERWAY \"%1\" %2,%3").arg(path, Viewport::fmtPoint({x, 0, 0}).section(',', 0, 0),
                                                                  Viewport::fmtPoint({y, 0, 0}).section(',', 0, 0)));
            view_->zoomExtents();
        });
    } else {
        waterway_->reread();
    }
    waterway_->show();
    waterway_->raise();
    waterway_->activateWindow();
    return waterway_;
}

CityDialog* MainWindow::openCity() {
    if (!city_) {
        city_ = new CityDialog(loc_, doc_, !theme_.lightCanvas, this);
        city_->setStyleSheet(theme_.styleSheet);
        connect(city_, &CityDialog::drawRequested, this, [this](const QString& path) {
            runLine(QStringLiteral("CITY \"%1\"").arg(path));
            view_->zoomExtents();
        });
    } else {
        city_->recompute();
    }
    city_->show();
    city_->raise();
    city_->activateWindow();
    return city_;
}

// Konstruksiya shablonlari: bitta nusxa (modal emas). "Chizmadan" — oyna yashirinadi, chizmada tayanch va yo'nalish olinadi.
TemplatesDialog* MainWindow::openTemplates(int page) {
    if (!tpl_) {
        tpl_ = new TemplatesDialog(loc_, !theme_.lightCanvas, this);
        tpl_->setStyleSheet(theme_.styleSheet);
        connect(tpl_, &TemplatesDialog::commandReady, this, [this](const QString& line) {
            if (runLine(line)) view_->zoomExtents();
        });
        connect(tpl_, &TemplatesDialog::pickRequested, this, [this](int n, const QStringList& prompts) {
            tpl_->hide();
            pickCb_ = [this](const std::vector<Vec3>& pts) {
                tpl_->setPicked(pts);
                tpl_->show();
                tpl_->raise();
            };
            view_->startPick(n, prompts);
        });
    }
    tpl_->setPage(page);
    tpl_->show();
    tpl_->raise();
    tpl_->activateWindow();
    return tpl_;
}

// ---------------------------------------------------------------- 3D model (asosiy oyna)
bool MainWindow::is3D() const { return viewStack_ && viewStack_->currentWidget() == view3d_; }

void MainWindow::show3D(bool on) {
    if (!viewStack_) return;
    if (viewTabs_->currentIndex() != (on ? 1 : 0)) {
        const QSignalBlocker b(viewTabs_);
        viewTabs_->setCurrentIndex(on ? 1 : 0);
    }
    if (on) {
        viewStack_->setCurrentWidget(view3d_);
        rebuild3D(view3d_->objectCount() > 0);
        view3d_->setFocus();
    } else {
        viewStack_->setCurrentWidget(view_);
        view_->setFocus();
    }
    raise();
    activateWindow();
}

void MainWindow::rebuild3D(bool keepView) {
    if (!view3d_) return;
    view3d_->setDark(!theme_.lightCanvas);
    view3d_->beginScene(QString(), QString());
    // chizma: qatlam bo'yicha (3D jismlar, yuzalar, 3D chiziqlar + 2D chiziqlar rejada)
    std::map<std::string, std::vector<EntityId>> byLayer;
    for (EntityId id : doc_.ids())
        if (const IEntity* e = doc_.find(id)) byLayer[e->layer()].push_back(id);
    std::size_t budget2d = 300000;
    for (const auto& [name, ids] : byLayer) {
        const auto L = doc_.layer(name);
        if (L && (!L->visible || L->frozen)) continue;
        if (name.rfind("SYOMKA_", 0) == 0) continue;   // nuqta yozuvlari — 3D da kerak emas
        Document tmp;
        std::vector<const IEntity*> flat;
        for (EntityId id : ids) {
            const IEntity* e = doc_.find(id);
            tmp.add(e->clone());
            const BBox b = e->bbox();
            if (std::abs(b.min.z) < 1e-9 && std::abs(b.max.z) < 1e-9) flat.push_back(e);
        }
        const auto sc = civil::sceneFromDocument(tmp);
        const QColor lc = aciColor(L ? L->color : 7, theme_.lightCanvas);
        const QString qn = QString::fromStdString(name);
        view3d_->beginObject(qn, qn, {t("gui.v3d.p.count", {QString::number(ids.size())})});
        view3d_->addTriangles(sc, 1.0);
        // 2D chiziqlar (z = 0): reja ko'rinishi
        for (const IEntity* e : flat) {
            if (budget2d == 0) break;
            std::vector<Vec3> pts;
            if (auto* c = dynamic_cast<const CircleEntity*>(e)) {
                for (int i = 0; i <= 48; ++i) pts.push_back(c->center() + Vec3{std::cos(i * kPi / 24), std::sin(i * kPi / 24), 0} * c->radius());
            } else pts = civil::pathOf(*e);
            if (pts.size() < 2) continue;
            std::vector<P3> pp;
            for (const auto& q : pts) pp.push_back({q.x, q.y, q.z});
            budget2d -= std::min(budget2d, pp.size());
            view3d_->addPolyline(pp, lc, 1.0);
        }
        view3d_->endObject();
    }
    // yer yuzasi (syomka nuqtalaridan TIN, balandlik bo'yicha ranglar)
    {
        const auto ids = doc_.idsOnLayer("SYOMKA");
        if (ids.size() >= 3 && ids.size() <= 60000) {
            const auto t3 = civil::Tin::build(civil::terrainPoints(doc_, ids));
            if (!t3.empty()) {
                double lo = 1e300, hi = -1e300;
                for (const auto& q : t3.points()) { lo = std::min(lo, q.z); hi = std::max(hi, q.z); }
                civil::DriveScene sc;
                auto col = [&](double z) {
                    const double k = hi > lo ? (z - lo) / (hi - lo) : 0.5;
                    const QColor c = QColor::fromHsvF(std::clamp(0.30 - 0.27 * k, 0.0, 1.0), 0.45, 0.62 + 0.25 * k);
                    return std::uint32_t((c.red() << 16) | (c.green() << 8) | c.blue());
                };
                for (const auto& tr3 : t3.triangles()) {
                    const auto &a = t3.points()[tr3[0]], &b = t3.points()[tr3[1]], &c = t3.points()[tr3[2]];
                    sc.tris.push_back({{a, b, c}, col((a.z + b.z + c.z) / 3), true});
                }
                view3d_->beginObject(t("gui.v3d.terrain"), t("gui.v3d.terrain"),
                                     {t("gui.v3d.p.count", {QString::number(t3.points().size())}), QStringLiteral("Z: %1 … %2").arg(lo, 0, 'f', 2).arg(hi, 0, 'f', 2)});
                view3d_->addTriangles(sc, 1.0);
                view3d_->endObject();
            }
        }
    }
    // bino modeli
    if (building_ && !building_->model().prisms.empty()) {
        for (const auto& pr : building_->model().prisms) {
            std::vector<P3> o;
            for (const auto& q : pr.outline) o.push_back({q.x, q.y, 0});
            std::vector<std::vector<P3>> hs;
            for (const auto& h : pr.holes) {
                std::vector<P3> hh;
                for (const auto& q : h) hh.push_back({q.x, q.y, 0});
                hs.push_back(hh);
            }
            const QString kind = QString::fromStdString(loc_.tr(std::string("bim.kind.") + bim::kindKey(pr.kind)));
            std::uint32_t c = 0xd6d1c6;
            if (pr.kind == bim::ElemKind::Column) c = 0xb3aca0;
            else if (pr.kind == bim::ElemKind::Slab || pr.kind == bim::ElemKind::Roof) c = 0x9fb4cc;
            else if (pr.kind == bim::ElemKind::FoundationSlab || pr.kind == bim::ElemKind::Lean || pr.kind == bim::ElemKind::StripFooting) c = 0x958f85;
            else if (pr.kind == bim::ElemKind::Stair) c = 0xd9c9a3;
            else if (pr.kind == bim::ElemKind::Shaft) c = 0xa9b2c8;
            else if (pr.kind == bim::ElemKind::Infill) c = 0xe4d6be;
            view3d_->beginObject(kind + " · " + (pr.level == 0 ? t("gui.bim.found") : t("gui.bim.level", {QString::number(pr.level)})), kind,
                                 {t("gui.v3d.p.volume", {QString::number(pr.volume(), 'f', 3)}),
                                  t("gui.v3d.p.levels", {QString::number(pr.z0, 'f', 3), QString::number(pr.z1, 'f', 3)}),
                                  t("gui.v3d.p.concrete", {QString::fromStdString(pr.concrete)})});
            view3d_->addPrism(o, hs, pr.z0, pr.z1, QColor(int(c >> 16), int((c >> 8) & 255), int(c & 255)));
            view3d_->endObject();
        }
        for (const auto& g : building_->model().glass) {
            std::vector<P3> q;
            for (const auto& v : g.quad) q.push_back({v.x, v.y, v.z});
            const QString nm = t(g.door ? "gui.v3d.door" : "gui.v3d.window");
            view3d_->beginObject(nm + " · " + t("gui.bim.level", {QString::number(g.level)}), nm, {});
            view3d_->addFace(q, g.door ? QColor(70, 110, 150) : QColor(120, 170, 215), g.door ? 170 : 120, true);
            view3d_->endObject();
        }
    }
    // armatura
    if (rebar_ && !rebar_->model().bars.empty()) {
        std::size_t nseg = 0;
        for (const auto& b : rebar_->model().bars) nseg += b.pts.size();
        const bool tubes = nseg < 150000;
        for (const auto& b : rebar_->model().bars) {
            std::vector<P3> pts;
            for (const auto& q : b.pts) pts.push_back({q.x, q.y, q.z});
            const QString dia = QString::fromUtf8("\xC3\x98") + QString::number(std::lround(b.d * 1000)) + " " + QString::fromStdString(b.cls);
            const int mm = int(std::lround(b.d * 1000));
            const QColor c = QColor::fromHsv((mm * 23) % 360, 150, 220);
            view3d_->beginObject(dia, t("gui.v3d.cat.rebar", {dia}), {t("gui.v3d.p.length", {QString::number(b.length, 'f', 2)})});
            if (tubes) view3d_->addTube(pts, std::max(0.003, b.d / 2), c, b.role == bim::BarRole::Stirrup, b.role == bim::BarRole::Stirrup ? 4 : 6);
            else view3d_->addPolyline(pts, c, 1.0, b.role == bim::BarRole::Stirrup);
            view3d_->endObject();
        }
    }
    // koridor
    if (corridor_ && corridor_->model().sec.size() >= 2) {
        view3d_->beginObject(t("gui.act.corridor"), t("gui.act.corridor"));
        view3d_->addTriangles(civil::sceneFromCorridor(corridor_->model()), 1.0);
        view3d_->endObject();
    }
    view3d_->setEmptyText(t("gui.v3d.empty"));
    view3d_->endScene(keepView);
}

void MainWindow::makeSampleBuilding() {
    civil::GridSpec g;
    g.dx = {6, 6, 6, 6};
    g.dy = {6, 6, 6};
    if (building_) {
        g.floors = building_->params().floors;
        g.floorH = building_->params().floorHeight;
    }
    const BBox b = doc_.extents(true);
    if (b.valid()) { g.x0 = b.max.x + 20; g.y0 = b.min.y; }
    const QString path = QDir::temp().filePath("asiacad_sample_grid.json");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const std::string js = civil::gridToJson(g);
        f.write(js.data(), static_cast<qint64>(js.size()));
    }
    f.close();
    runLine(QStringLiteral("GRID \"%1\"").arg(path));
    view_->zoomExtents();
    auto* bd = openBuilding();
    auto p = bd->params();
    p.floors = g.floors;
    p.floorHeight = g.floorH;
    bd->setParams(p, bim::autoLayerMap(doc_));
    if (rebar_ && rebar_->isVisible()) openRebar();
}

EarthworkDialog* MainWindow::openEarthwork(int tab) {
    if (!earth_) {
        earth_ = new EarthworkDialog(loc_, doc_, [this] { return view_->selection(); }, this);
        earth_->setStyleSheet(theme_.styleSheet);
        connect(earth_, &EarthworkDialog::commandReady, this, [this](const QString& line) {
            runLine(line);
            view_->zoomExtents();
            earth_->refreshLayers();
        });
        connect(earth_, &EarthworkDialog::pickRequested, this, [this] {
            earth_->hide();
            pickCb_ = [this](const std::vector<Vec3>& pts) {
                if (!pts.empty()) earth_->setAt(pts.front());
                earth_->show();
                earth_->raise();
            };
            view_->startPick(1, {"gui.prompt.ewat"});
        });
    } else earth_->refreshLayers();
    if (tab >= 0) earth_->setTab(tab);
    earth_->show();
    earth_->raise();
    earth_->activateWindow();
    return earth_;
}

SurveyDialog* MainWindow::openSurvey(int kind) {
    if (!survey_) {
        survey_ = new SurveyDialog(loc_, doc_, [this] { return view_->selection(); }, this);
        survey_->setStyleSheet(theme_.styleSheet);
        connect(survey_, &SurveyDialog::commandReady, this, [this](const QString& line) {
            runLine(line);
            if (line.startsWith("PTIMPORT")) view_->zoomExtents();
            survey_->refreshLayers();
        });
        connect(survey_, &SurveyDialog::locateRequested, this, &MainWindow::locate);
    } else survey_->refreshLayers();
    if (kind >= 0 && kind <= 4) survey_->setKind(static_cast<civil::DevKind>(kind));
    survey_->show();
    survey_->raise();
    survey_->activateWindow();
    return survey_;
}

GeoPlaceDialog* MainWindow::openGeoPlace() {
    QStringList ids;
    for (EntityId id : view_->selection()) ids << QString::number(id);
    if (geoDlg_) geoDlg_->deleteLater();
    geoDlg_ = new GeoPlaceDialog(loc_, !theme_.lightCanvas, ids.join(','), static_cast<int>(ids.size()), this);
    geoDlg_->setStyleSheet(theme_.styleSheet);
    connect(geoDlg_, &GeoPlaceDialog::commandReady, this, [this](const QString& line) {
        if (runLine(line)) {
            view_->zoomExtents();
            geoDlg_->accept();
        }
    });
    connect(geoDlg_, &GeoPlaceDialog::pickRequested, this, [this](int which) {
        geoDlg_->hide();
        pickCb_ = [this, which](const std::vector<Vec3>& pts) {
            if (!pts.empty()) geoDlg_->setPicked(which, pts.front());
            geoDlg_->show();
            geoDlg_->raise();
        };
        view_->startPick(1, {which == 1 ? "gui.prompt.geo1" : "gui.prompt.geo2"});
    });
    geoDlg_->show();
    geoDlg_->raise();
    return geoDlg_;
}

// "0 dan chizish": modulga mos kiritish oynasi; tasdiqlansa — generator buyrug'i (undo bilan), keyin modul chizmani qayta o'qiydi.
// Shunday qilib 0 dan chizilgan va import qilingan chizma bitta yo'ldan o'tadi va bir xil tuzatiladi.
QDialog* MainWindow::openScratch(const QString& module) {
    const bool dark = !theme_.lightCanvas;
    QDialog* d = nullptr;
    std::function<QString()> cmd;
    std::function<void()> after;
    if (module == "building") {
        auto* g = new GridInputDialog(loc_, dark, this);
        d = g;
        cmd = [g] { return g->command(); };
        after = [this, g] {
            const auto spec = g->spec();
            auto* b = openBuilding();
            if (b->drawings()->count()) b->drawings()->setSheets({});  // alohida fayllar emas — joriy chizma o'qiladi
            auto p = b->params();
            p.floors = spec.floors;
            p.floorHeight = spec.floorH;
            b->setParams(p, b->layerMap());
        };
    } else if (module == "util") {
        auto* n = new NetInputDialog(loc_, dark, this);
        d = n;
        cmd = [n] { return n->command(); };
        after = [this] {
            auto* u = openUtility();
            if (u->drawings()->count()) u->drawings()->setSheets({});
        };
    } else {
        auto* a = new AlignInputDialog(loc_, dark, module == "waterway", this);
        d = a;
        cmd = [a] { return a->command(); };
        after = [this, module] {
            if (module == "waterway") openWaterway();
            else if (module == "corridor") openCorridor();
            else openRoad();
        };
    }
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setStyleSheet(theme_.styleSheet);
    connect(d, &QDialog::accepted, this, [this, cmd, after] {
        runLine(cmd());
        view_->zoomExtents();
        after();
    });
    d->show();
    d->raise();
    return d;
}

// "Haydash": tanlangan chiziq (feature line) bo'ylab — bo'lmasa ochiq Koridor/Yo'l o'qi bo'ylab. Sahna: chizmadagi 3D, koridor yuzasi,
// bino modeli, yer. Balandlik manbalari: chiziq Z, koridor yuzasi, yer, bino qavatlari, doimiy belgi.
DriveDialog* MainWindow::openDrive() {
    std::vector<Vec3> pts;
    QString title;
    bool lineZ = false;
    for (EntityId id : view_->selection())
        if (const IEntity* e = doc_.find(id); e && dynamic_cast<const ICurve*>(e) && !dynamic_cast<const CircleEntity*>(e)) {
            const auto dp = civil::makeDrivePath(*e, 0.25);  // zich — pog'onalarni ham his qilsin
            pts = dp.p;
            lineZ = civil::curveHasZ(*e);
            title = QString::fromStdString(e->layer());
            break;
        }
    if (pts.empty() && corridor_ && corridor_->model().sec.size() >= 2) {
        for (const auto& sc : corridor_->model().sec) pts.push_back({sc.x, sc.y, sc.design});
        lineZ = true;
        title = t("gui.act.corridor");
    }
    if (pts.empty() && road_ && road_->model().st.size() >= 2) {
        for (const auto& st : road_->model().st) pts.push_back({st.x, st.y, st.design});
        lineZ = true;
        title = t("gui.act.road");
    }
    if (!drive_) {
        drive_ = new DriveDialog(loc_, this);
        drive_->setStyleSheet(theme_.styleSheet);
    }
    // balandlik manbalari
    std::vector<DriveSource> src;
    int def = -1;
    if (lineZ) { src.push_back({t("drive.z.line"), {}}); def = 0; }
    if (corridor_ && corridor_->model().sec.size() >= 2) {
        const civil::Corridor* c = &corridor_->model();
        src.push_back({t("drive.z.corridor"), [c](double x, double y) {
                           double z;
                           return civil::corridorSurfaceZ(*c, x, y, z) ? z : std::nan("");
                       }});
        if (def < 0) def = static_cast<int>(src.size()) - 1;
    }
    const auto ground = civil::groundFromDoc(doc_);
    if (ground) {
        src.push_back({t("drive.z.ground"), ground});
        if (def < 0) def = static_cast<int>(src.size()) - 1;
    }
    if (building_) {
        const auto& m = building_->model();
        std::map<int, double> top;  // daraja -> yuqori sirt
        for (const auto& pr : m.prisms)
            if (pr.kind == bim::ElemKind::Slab || pr.kind == bim::ElemKind::FoundationSlab)
                top[pr.level] = top.count(pr.level) ? std::max(top[pr.level], pr.z1) : pr.z1;
        // piyoda bino bo'ylab: oyoq ostidagi eng yuqori yuza (plita, zina pog'onasi, maydoncha), bir qadamda 0.6 m dan ko'p ko'tarilmaydi —
        // 2D yo'l zina katagiga kirsa, pog'onalar bo'ylab keyingi qavatga chiqiladi
        if (!top.empty()) {
            auto walk = std::make_shared<std::vector<bim::Prism>>();
            for (const auto& pr : m.prisms)
                if (pr.kind == bim::ElemKind::Slab || pr.kind == bim::ElemKind::FoundationSlab || pr.kind == bim::ElemKind::Stair) walk->push_back(pr);
            {   // 1-qavat poli (gruntdagi pol modelda yo'q): 1-qavat vertikal elementlari asosida, 1-qavat plitasi konturida
                double zf = 1e18;
                for (const auto& pr : m.prisms)
                    if (pr.level == 1 && (pr.kind == bim::ElemKind::Column || pr.kind == bim::ElemKind::Wall))
                        zf = std::min(zf, pr.z0);
                if (zf < 1e17)
                    for (const auto& pr : m.prisms)
                        if (pr.level == 1 && pr.kind == bim::ElemKind::Slab) walk->push_back({bim::ElemKind::Slab, 0, pr.outline, {}, zf - 0.1, zf, {}});
            }
            auto state = std::make_shared<double>(std::nan(""));
            auto inPoly = [](const std::vector<Vec3>& poly, double x, double y) {
                bool in = false;
                for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
                    if ((poly[i].y > y) != (poly[j].y > y) && x < (poly[j].x - poly[i].x) * (y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
                return in;
            };
            DriveSource d{t("drive.z.walk"), [walk, state, inPoly](double x, double y) {
                              if (std::isnan(*state)) {  // boshlanish: oyoq ostidagi eng past yuza (1-qavat poli)
                                  double lo = 1e18;
                                  for (const auto& pr : *walk) {
                                      if (pr.z1 >= lo || !inPoly(pr.outline, x, y)) continue;
                                      bool hole = false;
                                      for (const auto& h : pr.holes) hole = hole || inPoly(h, x, y);
                                      if (!hole) lo = pr.z1;
                                  }
                                  *state = lo < 1e17 ? lo : 0.0;
                              }
                              double best = -1e18;
                              for (const auto& pr : *walk) {
                                  if (pr.z1 > *state + 0.6 || pr.z1 < best) continue;
                                  if (!inPoly(pr.outline, x, y)) continue;
                                  bool hole = false;
                                  for (const auto& h : pr.holes) hole = hole || inPoly(h, x, y);
                                  if (!hole) best = pr.z1;
                              }
                              if (best > -1e17) *state = best;
                              return *state;
                          }};
            d.walk = true;
            d.reset = [state] { *state = std::nan(""); };
            src.push_back(d);
            // 2D yo'l bino ichida boshlansa — boshqa yuzalardan (koridor, yer) ustun
            bool inBuilding = false;
            if (!lineZ && !pts.empty())
                for (const auto& pr : *walk) inBuilding = inBuilding || inPoly(pr.outline, pts.front().x, pts.front().y);
            if ((def < 0 && !lineZ) || inBuilding) def = static_cast<int>(src.size()) - 1;
        }
        for (const auto& [lvl, z] : top) {
            const double zz = z;
            DriveSource d{t("drive.z.floor", {QString::number(lvl + 1), QString::number(z, 'f', 2)}), [zz](double, double) { return zz; }};
            d.walk = true;
            src.push_back(d);
            if (def < 0 && !lineZ) def = static_cast<int>(src.size()) - 1;
        }
    }
    {   // shablon yuzalari (rampa yo'li, zina pog'onalari): oyoq/g'ildirak ostidagi yuqoriga qaragan uchburchak; bir qadamda ≤ 0.6 m ko'tarilish
        Document tdoc;
        doc_.forEach([&](const IEntity& e) {
            const std::string& L = e.layer();
            if (L == "RAMPA_3D" || L == "ZINA_3D" || L == "ZINA_3D_METALL") tdoc.add(e.clone());
        });
        auto tris = std::make_shared<std::vector<std::array<Vec3, 3>>>();
        if (tdoc.size())
            for (const auto& tr : civil::sceneFromDocument(tdoc).tris) {
                const Vec3 a = tr.p[1] - tr.p[0], b = tr.p[2] - tr.p[0];
                const double nz = a.x * b.y - a.y * b.x, nl = std::sqrt(std::pow(a.y * b.z - a.z * b.y, 2) + std::pow(a.z * b.x - a.x * b.z, 2) + nz * nz);
                if (nl > 1e-12 && std::abs(nz) / nl > 0.5) tris->push_back(tr.p);
            }
        if (!tris->empty()) {
            auto zAt = [](const std::array<Vec3, 3>& p, double x, double y, double& z) {
                const double d = (p[1].y - p[2].y) * (p[0].x - p[2].x) + (p[2].x - p[1].x) * (p[0].y - p[2].y);
                if (std::abs(d) < 1e-12) return false;
                const double l1 = ((p[1].y - p[2].y) * (x - p[2].x) + (p[2].x - p[1].x) * (y - p[2].y)) / d;
                const double l2 = ((p[2].y - p[0].y) * (x - p[2].x) + (p[0].x - p[2].x) * (y - p[2].y)) / d;
                const double l3 = 1 - l1 - l2;
                if (l1 < -1e-9 || l2 < -1e-9 || l3 < -1e-9) return false;
                z = l1 * p[0].z + l2 * p[1].z + l3 * p[2].z;
                return true;
            };
            auto state = std::make_shared<double>(std::nan(""));
            DriveSource d{t("drive.z.tpl"), [tris, state, zAt](double x, double y) {
                              double lo = 1e18, best = -1e18, z;
                              for (const auto& p : *tris)
                                  if (zAt(p, x, y, z)) {
                                      lo = std::min(lo, z);
                                      if ((std::isnan(*state) || z <= *state + 0.6) && z > best) best = z;
                                  }
                              if (lo > 1e17) return std::nan("");
                              if (std::isnan(*state)) best = lo;
                              if (best > -1e17) *state = best;
                              return *state;
                          }};
            d.reset = [state] { *state = std::nan(""); };
            src.push_back(d);
            if (!lineZ && !pts.empty()) {
                double z;
                bool on = false;
                for (const auto& p : *tris) on = on || zAt(p, pts.front().x, pts.front().y, z);
                if (on) def = static_cast<int>(src.size()) - 1;
            }
        }
    }
    src.push_back({t("drive.z.const"), {}, true});
    if (def < 0) def = static_cast<int>(src.size()) - 1;
    // sahna
    civil::DriveScene sc = civil::sceneFromDocument(doc_);
    if (corridor_) sc.append(civil::sceneFromCorridor(corridor_->model()));
    if (building_)
        for (const auto& pr : building_->model().prisms) {
            std::uint32_t col = 0xd6d1c6;
            if (pr.kind == bim::ElemKind::Column) col = 0xb3aca0;
            else if (pr.kind == bim::ElemKind::Slab || pr.kind == bim::ElemKind::Roof) col = 0xc2beb6;
            else if (pr.kind == bim::ElemKind::FoundationSlab || pr.kind == bim::ElemKind::Lean) col = 0x958f85;
            else if (pr.kind == bim::ElemKind::Stair) col = 0xd9c9a3;
            else if (pr.kind == bim::ElemKind::Shaft) col = 0xa9b2c8;
            sc.addPrism(pr.outline, pr.z0, pr.z1, col);
        }
    if (!pts.empty()) {
        // yer: syomkadan; bo'lmasa — sahnaning eng past sathida tekis maydon (shahar, bino)
        std::function<double(double, double)> gz = ground;
        if (!gz) {
            double z0 = 1e18;
            for (const auto& q : pts) z0 = std::min(z0, q.z);
            if (!sc.tris.empty()) z0 = sc.bounds().min.z;
            gz = [z0](double, double) { return z0; };
        }
        sc.append(civil::groundAround(pts, gz, 300, 8));
        sc.append(civil::drapeLinework(doc_, pts, 300, gz));
    }
    drive_->setScene(std::move(sc));
    drive_->setSources(src, def);
    drive_->setPath(pts, title);
    drive_->show();
    drive_->raise();
    drive_->activateWindow();
    if (pts.empty()) statusBar()->showMessage(t("gui.drv.nopath"), 6000);
    return drive_;
}

void MainWindow::locate(double x, double y) {
    const double ppu = std::max(1.0, std::min(view_->width(), view_->height()) / 60.0);  // ~60 m ko'rinadi
    view_->setView({x, y, 0}, ppu);
    view_->update();
    raise();
    activateWindow();
}

void MainWindow::activateTool(const QString& key) {
    if (act_.contains(key)) act_[key]->trigger();
}

void MainWindow::startCommand(const QString& canonical) {
    static const QHash<QString, QString> tools = {
        {"LINE", "line"}, {"PLINE", "pline"}, {"CIRCLE", "circle"}, {"ARC", "arc"}, {"POINT", "point"},
        {"TEXT", "text"}, {"MOVE", "move"}, {"COPY", "copy"}, {"ROTATE", "rotate"},
        {"OFFSET", "offset"}, {"TRIM", "trim"}, {"EXTEND", "extend"}, {"FILLET", "fillet"},
        {"DIMLINEAR", "dimlinear"}, {"DIMALIGNED", "dimaligned"}, {"DIMRADIUS", "dimradius"}, {"DIMDIAMETER", "dimdiameter"}};
    if (tools.contains(canonical)) { act_[tools[canonical]]->trigger(); return; }
    if (canonical == "ERASE") {
        if (view_->selection().empty()) { prompt_->setText(t("gui.prompt.selectfirst")); return; }
        QStringList ids;
        for (EntityId id : view_->selection()) ids << QString::number(id);
        runLine("ERASE " + ids.join(','));
        return;
    }
    if (canonical == "UNDO" || canonical == "REDO" || canonical == "HELP" || canonical == "EXTENTS") {
        runLine(canonical);
        if (canonical == "EXTENTS") view_->zoomExtents();
        return;
    }
    if (canonical == "SEWER") { openSewer(); return; }
    if (canonical == "BUILDING") { openBuilding(); return; }
    if (canonical == "CITY") { openCity(); return; }
    if (canonical == "DRIVE") { openDrive(); return; }
    if (canonical == "GRID") { openScratch("building"); return; }
    if (canonical == "ALIGN") { openScratch("road"); return; }
    if (canonical == "UTILNET") { openScratch("util"); return; }
    if (canonical == "STEELFRAME") { openTemplates(0); return; }
    if (canonical == "STAIR") { openTemplates(1); return; }
    if (canonical == "RAMP") { openTemplates(2); return; }
    if (canonical == "RWALL") { openTemplates(3); return; }
    if (canonical == "GEOPLACE") { openGeoPlace(); return; }
    if (canonical == "DEVIATION" || canonical == "PTEXPORT") { openSurvey(); return; }
    if (canonical == "PTIMPORT") { act_["ptimport"]->trigger(); return; }
    if (canonical == "DEVCLEAR") { runLine("DEVCLEAR"); return; }
    if (canonical == "TIN" || canonical == "CONTOUR") { openEarthwork(0); return; }
    if (canonical == "PROFILE") { openEarthwork(1); return; }
    if (canonical == "XSECTIONS") { openEarthwork(2); return; }
    if (canonical == "CARTOGRAM") { openEarthwork(3); return; }
    if (canonical == "VOLUME") { openEarthwork(4); return; }
    if (canonical == "OPEN") { openFile(); return; }
    if (canonical == "SAVE") { saveFile(false); return; }
    if (canonical == "IMPORT") { importDxf(); return; }
    if (canonical == "NEW") { act_["new"]->trigger(); return; }
    // Qolganlari argument talab qiladi — buyruq satriga nomi yoziladi
    cmdEdit_->setText(QString::fromStdString(loc_.commandName(canonical.toStdString())) + " ");
    cmdEdit_->setFocus();
}

void MainWindow::submitCommandLine() {
    const QString text = cmdEdit_->text().trimmed();
    cmdEdit_->clear();
    if (text.isEmpty()) {  // bo'sh Enter: asbob faol — yakunlash, aks holda oxirgisini takrorlash (AutoCAD)
        if (view_->tool() != Tool::Select) view_->finishTool(); else view_->repeatLast();
        view_->setFocus();
        return;
    }
    cmdHistory_ << text;
    cmdHistoryPos_ = cmdHistory_.size();
    // Asbob faol bo'lsa: "x,y" yoki "@dx,dy" — nuqta sifatida
    static const QRegularExpression pt(R"(^(@)?\s*(-?[0-9.]+)\s*,\s*(-?[0-9.]+)$)");
    if (view_->tool() != Tool::Select) {
        if (const auto m = pt.match(text); m.hasMatch()) {
            Vec3 p{m.captured(2).toDouble(), m.captured(3).toDouble(), 0};
            if (!m.captured(1).isEmpty()) {
                const auto last = view_->lastPoint();
                if (last) p = {last->x + p.x, last->y + p.y, 0};
            }
            appendHistory("› " + text, theme_.accent);
            view_->feedPoint(p);
            return;
        }
        // Bitta son — to'g'ridan-to'g'ri masofa (orto/qutb yo'nalishi yoki kursor tomoni)
        bool ok = false;
        const double d = text.toDouble(&ok);
        if (ok && (view_->tool() == Tool::Offset || view_->tool() == Tool::Fillet || view_->tool() == Tool::Chamfer ||
                   (view_->tool() == Tool::Polygon && !view_->lastPoint()))) {  // masofa / radius / faska / tomonlar soni
            appendHistory("› " + text, theme_.accent);
            if (view_->tool() == Tool::Offset) view_->setOffsetDistance(d);
            else if (view_->tool() == Tool::Fillet) view_->setFilletRadius(d);
            else if (view_->tool() == Tool::Chamfer) view_->setChamferDistance(d);
            else view_->setPolygonSides(static_cast<int>(d));
            return;
        }
        if (ok && d > 0 && view_->feedDistance(d)) {
            appendHistory("› " + text, theme_.accent);
            return;
        }
    }
    // Faqat buyruq nomi (argumentsiz) va GUI asbobi bor bo'lsa — asbobni ishga tushirish
    const auto toks = text.split(QRegularExpression("\\s+"));
    const QString canon = QString::fromStdString(loc_.resolveCommand(toks.first().toStdString()));
    if (toks.size() == 1 && !canon.isEmpty() &&
        QStringList{"LINE", "PLINE", "CIRCLE", "ARC", "POINT", "TEXT", "MOVE", "COPY", "ROTATE", "ERASE", "OPEN", "SAVE", "IMPORT",
                    "DIMLINEAR", "DIMALIGNED", "DIMRADIUS", "DIMDIAMETER", "OFFSET", "TRIM", "EXTEND", "FILLET", "SEWER", "BUILDING"}.contains(canon)) {
        appendHistory("› " + text, theme_.accent);
        startCommand(canon);
        return;
    }
    runLine(text);
}

bool MainWindow::maybeSave() {
    if (!stack_.isDirty()) return true;
    const auto r = QMessageBox::question(this, t("gui.unsaved.title"), t("gui.unsaved.text"),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) saveFile(false);
    return !stack_.isDirty() || r == QMessageBox::Discard;
}

void MainWindow::openFile() {
    if (!maybeSave()) return;
    const QString f = QFileDialog::getOpenFileName(this, t("gui.act.open"), {}, t("gui.filter.drawings"));
    if (!f.isEmpty()) runLine(QStringLiteral("OPEN \"%1\" !").arg(f));
}

void MainWindow::saveFile(bool saveAs) {
    QString f = QString::fromStdString(interp_->currentFile());
    if (saveAs || f.isEmpty()) {
        f = QFileDialog::getSaveFileName(this, t("gui.act.saveas"), f, QStringLiteral("AsiaCAD (*.acad)"));
        if (f.isEmpty()) return;
        if (!f.endsWith(".acad", Qt::CaseInsensitive)) f += ".acad";
        runLine(QStringLiteral("SAVE \"%1\"").arg(f));
    } else {
        runLine(QStringLiteral("SAVE"));
    }
}

void MainWindow::importDxf() {
    const QString f = QFileDialog::getOpenFileName(this, t("gui.act.import"), {}, QStringLiteral("DXF (*.dxf)"));
    if (!f.isEmpty()) runLine(QStringLiteral("IMPORT \"%1\"").arg(f));
}

void MainWindow::exportDxf() {
    QString f = QFileDialog::getSaveFileName(this, t("gui.act.export"), {}, QStringLiteral("DXF (*.dxf)"));
    if (f.isEmpty()) return;
    if (!f.endsWith(".dxf", Qt::CaseInsensitive)) f += ".dxf";
    runLine(QStringLiteral("SAVE \"%1\"").arg(f));
}

bool MainWindow::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (o == cmdEdit_) {
            if (k->key() == Qt::Key_Up || k->key() == Qt::Key_Down) {
                if (cmdHistory_.isEmpty()) return true;
                cmdHistoryPos_ = std::clamp(cmdHistoryPos_ + (k->key() == Qt::Key_Up ? -1 : 1), 0, static_cast<int>(cmdHistory_.size()));
                cmdEdit_->setText(cmdHistoryPos_ < cmdHistory_.size() ? cmdHistory_[cmdHistoryPos_] : QString());
                return true;
            }
            if (k->key() == Qt::Key_Escape) { cmdEdit_->clear(); view_->cancelTool(); view_->setFocus(); return true; }
        } else if (o == view_) {
            // Chizmada harf yoki raqam terilsa — buyruq satriga yo'naltiriladi
            const QString txt = k->text();
            const bool plain = !(k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
            if (plain && !txt.isEmpty() && txt.at(0).isPrint() && k->key() != Qt::Key_Space) {
                cmdEdit_->setFocus();
                cmdEdit_->insert(txt);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (maybeSave()) e->accept(); else e->ignore();
}

} // namespace asiacad::gui
