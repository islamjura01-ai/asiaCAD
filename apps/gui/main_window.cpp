#include "main_window.h"

#include "command_palette.h"
#include "icons.h"

#include <asiacad/core/entities.h>

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
    col->addWidget(view_, 1);
    setCentralWidget(central);

    buildModuleBars();
    buildDocks();
    buildStatusBar();

    connect(view_, &Viewport::runCommand, this, [this](const QString& l) { runLine(l); });
    connect(view_, &Viewport::promptChanged, this, [this](const QString& s) { prompt_->setText(s); });
    connect(view_, &Viewport::cursorMoved, this, [this](double x, double y) {
        coords_->setText(QStringLiteral("X %1   Y %2").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3));
    });
    connect(view_, &Viewport::selectionChanged, this, &MainWindow::refreshProperties);
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
    appendHistory(QString::fromStdString(loc_.tr("app.banner", {"0.9"})), theme_.accent);
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
            {"offset", "#E0A526"}, {"trim", "#E0A526"}, {"extend", "#E0A526"}, {"fillet", "#E0A526"},
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
    // Asbob tugagach (Esc, Enter, buyruq bajarildi) — "Tanlash" tugmasi yonsin
    connect(view_, &Viewport::promptChanged, this, [this] {
        if (act_.contains(toolKey(view_->tool()))) act_[toolKey(view_->tool())]->setChecked(true);
    });
}

void MainWindow::buildRibbon() {
    ribbon_ = new QTabWidget(this);
    ribbon_->setObjectName("ribbon");
    ribbon_->setDocumentMode(true);
    ribbon_->setFixedHeight(122);
    // Har bir sahifa: guruhlar (tugmalar + pastda guruh nomi)
    struct Group { const char* title; std::vector<const char*> keys; std::vector<const char*> small = {}; };
    struct Page { const char* title; std::vector<Group> groups; };
    const std::vector<Page> pages = {
        {"gui.tab.home", {{"gui.grp.draw", {"line", "pline", "circle", "arc", "point", "text"}},
                          {"gui.grp.modify", {"select", "move", "copy", "rotate", "erase"}, {"offset", "trim", "extend", "fillet"}},
                          {"gui.grp.history", {"undo", "redo"}}}},
        {"gui.grp.dim", {{"gui.grp.dim", {"dimlinear", "dimaligned", "dimradius", "dimdiameter"}},
                         {"gui.grp.history", {"undo", "redo"}}}},
        {"gui.grp.file", {{"gui.grp.file", {"new", "open", "save", "saveas", "import", "export"}}}},
        {"gui.tab.view", {{"gui.grp.zoom", {"extents", "zoomin", "zoomout", "grid"}},
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
                    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                    b->setIconSize(QSize(18, 18));
                    b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
                    grid->addWidget(b, static_cast<int>(i % 2), static_cast<int>(i / 2));
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
        {"gui.module.pipes", "pipes", "#1F7BE0"},     {"gui.module.bim", "bim", "#D9466A"}};
    auto* grp = new QActionGroup(this);
    for (const auto& m : mods) {
        auto* a = rail_->addAction(makeIcon(m.icon, Qt::white, QColor(m.color)), QString());
        a->setCheckable(true);
        a->setProperty("i18n", m.key);
        grp->addAction(a);
        const QString key = m.key;
        connect(a, &QAction::triggered, this, [this, key, a] {
            if (key != "gui.module.drawing") {
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
    layers_->setColumnCount(3);
    layers_->setHeaderHidden(true);
    layers_->setRootIsDecorated(false);
    layers_->header()->setStretchLastSection(false);
    layers_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layers_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layers_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
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
        if (col == 2) kw = QString::fromStdString(loc_.keywordName(it->checkState(2) == Qt::Checked ? "LOCK" : "UNLOCK"));
        if (!kw.isEmpty()) runLine(QStringLiteral("LAYER \"%1\" %2").arg(name, kw));
    });
    connect(layers_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        runLine(QStringLiteral("CLAYER \"%1\"").arg(it->data(0, Qt::UserRole).toString()));
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
    auto* sb = statusBar();
    coords_ = new QLabel(sb);
    coords_->setMinimumWidth(260);
    sb->addWidget(coords_);
    for (const auto& s : kSnaps) {
        auto* b = new QToolButton(sb);
        b->setCheckable(true);
        b->setChecked(view_->snapModes() & s.mode);
        const unsigned m = s.mode;
        connect(b, &QToolButton::toggled, this, [this, m](bool on) {
            view_->setSnapModes(on ? (view_->snapModes() | m) : (view_->snapModes() & ~m));
        });
        snapBtns_.insert(s.mode, b);
        sb->addPermanentWidget(b);
    }
    auto addToggle = [&](QToolButton*& b, Qt::Key key, auto setter) {
        b = new QToolButton(sb);
        b->setCheckable(true);
        sb->addPermanentWidget(b);
        auto* a = new QAction(this);
        a->setShortcut(QKeySequence(key));
        a->setShortcutContext(Qt::ApplicationShortcut);
        addAction(a);
        connect(a, &QAction::triggered, b, &QToolButton::toggle);
        connect(b, &QToolButton::toggled, this, [this, setter](bool on) {
            if (!updatingUi_) (view_->*setter)(on);
        });
    };
    auto* sep = new QFrame(sb);
    sep->setFrameShape(QFrame::VLine);
    sb->addPermanentWidget(sep);
    addToggle(orthoBtn_, Qt::Key_F8, &Viewport::setOrtho);
    addToggle(polarBtn_, Qt::Key_F10, &Viewport::setPolar);
    connect(view_, &Viewport::orthoPolarChanged, this, [this](bool o, bool p) {
        updatingUi_ = true;
        orthoBtn_->setChecked(o);
        polarBtn_->setChecked(p);
        updatingUi_ = false;
    });
    modeBox_ = new QComboBox(sb);
    langBox_ = new QComboBox(sb);
    sb->addPermanentWidget(modeBox_);
    sb->addPermanentWidget(langBox_);
    connect(modeBox_, &QComboBox::activated, this, [this](int i) { setMode(static_cast<UiMode>(modeBox_->itemData(i).toInt())); });
    connect(langBox_, &QComboBox::activated, this, [this](int i) { setLanguage(langBox_->itemData(i).toString()); });
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
    propsDock_->setWindowTitle(t("gui.panel.props"));
    cmdDock_->setWindowTitle(t("gui.panel.command"));
    for (const auto& s : kSnaps) {
        // tor holat bayrog'i: uzun nomlar (ТОЧКАВСТАВКИ) 5 harfgacha, to'liq nomi — maslahatda
        const QString full = QString::fromStdString(loc_.keywordName(s.name));
        snapBtns_[s.mode]->setText(full.size() > 6 ? full.left(5) + "." : full);
        snapBtns_[s.mode]->setToolTip(full);
    }
    orthoBtn_->setText(t("gui.status.ortho"));
    orthoBtn_->setToolTip(t("gui.status.ortho") + " (F8)");
    polarBtn_->setText(t("gui.status.polar"));
    polarBtn_->setToolTip(t("gui.status.polar") + QStringLiteral(" (F10) — %1°").arg(view_->polarStep()));
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
    layers_->clear();
    const std::string cur = interp_->currentLayer();
    for (const auto& [name, l] : doc_.layers()) {
        auto* it = new QTreeWidgetItem(layers_);
        const QString qn = QString::fromStdString(name);
        it->setText(0, name == cur ? qn + "   · " + t("gui.layers.current") : qn);
        it->setData(0, Qt::UserRole, qn);
        it->setIcon(0, swatch(aciColor(l.color, theme_.lightCanvas)));
        QFont f = it->font(0);
        f.setBold(name == cur);
        it->setFont(0, f);
        it->setCheckState(1, l.visible ? Qt::Checked : Qt::Unchecked);
        it->setCheckState(2, l.locked ? Qt::Checked : Qt::Unchecked);
        it->setToolTip(1, QString::fromStdString(loc_.keywordName("ON")) + " / " + QString::fromStdString(loc_.keywordName("OFF")));
        it->setToolTip(2, QString::fromStdString(loc_.keywordName("LOCK")));
        if (name == cur) layers_->setCurrentItem(it);
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
    if (!cont) close();
    return cont;
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
    if (text.isEmpty()) { view_->finishTool(); view_->setFocus(); return; }
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
        if (ok && (view_->tool() == Tool::Offset || view_->tool() == Tool::Fillet)) {  // masofa / radius
            appendHistory("› " + text, theme_.accent);
            if (view_->tool() == Tool::Offset) view_->setOffsetDistance(d);
            else view_->setFilletRadius(d);
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
                    "DIMLINEAR", "DIMALIGNED", "DIMRADIUS", "DIMDIAMETER", "OFFSET", "TRIM", "EXTEND", "FILLET"}.contains(canon)) {
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
