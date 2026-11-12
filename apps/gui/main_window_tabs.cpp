// AsiaCAD GUI — varaqlar (AutoCAD fayl yorliqlari kabi): Start · ochiq chizmalar · menyular.
//   Har chizma o'z hujjati, undo/redo tarixi, joriy qatlami, ko'rinishi va IFC modellari bilan; varaq almashganda
//   hammasi almashtiriladi (Viewport, Interpreter, menyular bitta Document obyektiga bog'langan — mazmuni almashadi).
//   Menyular (yordamchi oynalar) ham varaq bo'lib ochiladi va ochilgan chizmasiga bog'lanadi.
#include "main_window.h"

#include "building_dialog.h"
#include "corridor_dialog.h"
#include "drive_dialog.h"
#include "earthwork_dialog.h"
#include "generator_dialogs.h"
#include "icons.h"
#include "menu_history.h"
#include "model3d_view.h"
#include "rebar_dialog.h"
#include "road_dialog.h"
#include "sewer_dialog.h"
#include "survey_dialog.h"
#include "templates_dialog.h"
#include "utility_dialog.h"
#include "waterway_dialog.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QDockWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace asiacad::gui {

namespace {

QColor mutedFor(const Theme& th) {
    if (th.mode == UiMode::Module) return QColor("#7A6A58");
    if (th.mode == UiMode::Focus) return QColor("#A3ABD1");
    return QColor("#8FA2CF");
}

// menyu varag'i nomi qisqa: "Avtomobil yo'li — chizmadan 3D koridor" -> "Avtomobil yo'li" (to'liq nomi — maslahatda)
QString shortTitle(const QString& s) {
    QString r = s.section(QStringLiteral(" — "), 0, 0).section(QStringLiteral(" - "), 0, 0).trimmed();
    if (r.isEmpty()) r = s;
    if (r.size() > 30) r = r.left(28) + QChar(0x2026);
    return r;
}

bool sameFile(const QString& a, const QString& b) {
    if (a.isEmpty() || b.isEmpty()) return false;
    const QString x = QFileInfo(a).absoluteFilePath(), y = QFileInfo(b).absoluteFilePath();
#ifdef Q_OS_WIN
    return x.compare(y, Qt::CaseInsensitive) == 0;
#else
    return x == y;
#endif
}

} // namespace

// ---------------------------------------------------------------- varaqlar paneli
QWidget* MainWindow::buildDocBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("docBar");
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(6, 0, 6, 0);
    h->setSpacing(2);
    docTabs_ = new QTabBar(bar);
    docTabs_->setObjectName("docTabs");
    docTabs_->setExpanding(false);
    docTabs_->setDrawBase(false);
    docTabs_->setMovable(true);
    docTabs_->setUsesScrollButtons(true);
    docTabs_->setElideMode(Qt::ElideMiddle);
    docTabs_->setIconSize(QSize(15, 15));
    docTabs_->setContextMenuPolicy(Qt::CustomContextMenu);
    docTabs_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    docTabs_->installEventFilter(this);
    h->addWidget(docTabs_, 0, Qt::AlignBottom);
    auto mk = [&](const char* name) {
        auto* b = new QToolButton(bar);
        b->setObjectName(QString::fromLatin1(name));
        b->setProperty("docBtn", true);
        b->setAutoRaise(true);
        b->setIconSize(QSize(14, 14));
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    auto* plus = mk("docNew");
    connect(plus, &QToolButton::clicked, this, [this] { newDrawing(); });
    h->addWidget(plus, 0, Qt::AlignVCenter);
    h->addStretch(1);
    // o'ng tomonda (AutoCAD kabi): ▾ ochiq varaqlar · _ yig'ish · ▭ butun ekran · ✕ joriy varaqni yopish
    auto* list = mk("tabList");
    list->setPopupMode(QToolButton::InstantPopup);
    auto* lm = new QMenu(list);
    list->setMenu(lm);
    connect(lm, &QMenu::aboutToShow, this, [this, lm] {
        lm->clear();
        for (int i = 0; i < docTabs_->count(); ++i) {
            QAction* a = lm->addAction(docTabs_->tabIcon(i), docTabs_->tabText(i));
            a->setCheckable(true);
            a->setChecked(i == docTabs_->currentIndex());
            connect(a, &QAction::triggered, this, [this, i] { if (i < docTabs_->count()) docTabs_->setCurrentIndex(i); });
        }
        lm->addSeparator();
        lm->addAction(act_["new"]);
        lm->addAction(act_["open"]);
    });
    auto* mn = mk("winMin");
    connect(mn, &QToolButton::clicked, this, [this] { showMinimized(); });
    tabMax_ = mk("winMax");
    connect(tabMax_, &QToolButton::clicked, this, &MainWindow::toggleFullScreen);
    auto* cl = mk("winClose");   // dasturni butunlay yopish (saqlanmagan chizmalar so'raladi); varaq — o'z × tugmasi bilan
    connect(cl, &QToolButton::clicked, this, [this] { close(); });
    for (auto* b : {list, mn, tabMax_, cl}) h->addWidget(b, 0, Qt::AlignVCenter);

    connect(docTabs_, &QTabBar::customContextMenuRequested, this, [this](const QPoint& p) {
        const int i = docTabs_->tabAt(p);
        if (i < 0) return;
        QMenu m(this);
        const bool start = !menuAt(i) && docTabs_->tabData(i).toInt() == 0;
        if (!start) m.addAction(makeIcon("x", mutedFor(theme_)), t("gui.tab.close"), this, [this, i] { closeTabAt(i); });
        m.addAction(t("gui.tab.closeothers"), this, [this, i] {
            QObject* keepM = menuAt(i);
            const int keepD = keepM ? -1 : docTabs_->tabData(i).toInt();
            for (int k = docTabs_->count() - 1; k >= 0; --k) {
                QWidget* mk = menuAt(k);
                const int id = mk ? -1 : docTabs_->tabData(k).toInt();
                if ((mk && mk == keepM) || (!mk && (id == 0 || id == keepD))) continue;
                if (!closeTabAt(k)) break;   // "Bekor qilish" — to'xtatiladi
            }
        });
        if (!start && !menuAt(i)) {
            m.addSeparator();
            m.addAction(icon("save"), t("gui.act.save"), this, [this, i] {
                docTabs_->setCurrentIndex(i);
                saveFile(false);
            });
        }
        m.exec(docTabs_->mapToGlobal(p));
    });

    // klaviatura: Ctrl+F4 / Ctrl+W — varaqni yopish, Ctrl+Tab — keyingi varaq, Ctrl+0 — butun ekran
    for (const QKeySequence& ks : {QKeySequence(Qt::CTRL | Qt::Key_F4), QKeySequence(Qt::CTRL | Qt::Key_W)}) {
        auto* sc = new QShortcut(ks, this);
        connect(sc, &QShortcut::activated, this, [this] { closeTabAt(docTabs_->currentIndex()); });
    }
    auto* nextT = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    connect(nextT, &QShortcut::activated, this, [this] {
        if (docTabs_->count() > 1) docTabs_->setCurrentIndex((docTabs_->currentIndex() + 1) % docTabs_->count());
    });
    auto* prevT = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
    connect(prevT, &QShortcut::activated, this, [this] {
        if (docTabs_->count() > 1) docTabs_->setCurrentIndex((docTabs_->currentIndex() + docTabs_->count() - 1) % docTabs_->count());
    });
    auto* full = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this);
    connect(full, &QShortcut::activated, this, &MainWindow::toggleFullScreen);
    return bar;
}

void MainWindow::addTabClose(int index) {
    auto* b = new QToolButton(docTabs_);
    b->setObjectName("tabX");
    b->setAutoRaise(true);
    b->setIconSize(QSize(10, 10));
    b->setFixedSize(18, 18);
    b->setIcon(makeIcon("x", mutedFor(theme_)));
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(t("gui.tab.close"));
    connect(b, &QToolButton::clicked, this, [this, b] {
        for (int i = 0; i < docTabs_->count(); ++i)
            if (docTabs_->tabButton(i, QTabBar::RightSide) == b) {
                closeTabAt(i);
                return;
            }
    });
    docTabs_->setTabButton(index, QTabBar::RightSide, b);
}

void MainWindow::initTabs() {
    DocSlot s;
    s.id = ++docSeq_;
    s.untitled = ++untitledN_;
    activeDoc_ = s.id;
    docs_.push_back(std::move(s));
    {
        const QSignalBlocker b(docTabs_);
        const int st = docTabs_->addTab(QString());
        docTabs_->setTabData(st, 0);
        const int d = docTabs_->addTab(QString());
        docTabs_->setTabData(d, activeDoc_);
        addTabClose(d);
        docTabs_->setCurrentIndex(d);
    }
    pages_->setCurrentWidget(drawPage_);
    connect(docTabs_, &QTabBar::currentChanged, this, &MainWindow::onDocTabChanged);
    retranslateTabs();
}

void MainWindow::retranslateTabs() {
    if (!docTabs_) return;
    const QColor mu = mutedFor(theme_);
    if (auto* b = findChild<QToolButton*>("docNew")) { b->setIcon(makeIcon("plus", mu)); b->setToolTip(t("gui.tab.new")); }
    if (auto* b = findChild<QToolButton*>("tabList")) { b->setIcon(makeIcon("chev", mu)); b->setToolTip(t("gui.tab.list")); }
    if (auto* b = findChild<QToolButton*>("winMin")) { b->setIcon(makeIcon("min", mu)); b->setToolTip(t("gui.win.min")); }
    if (auto* b = findChild<QToolButton*>("winClose")) { b->setIcon(makeIcon("x", mu)); b->setToolTip(t("gui.win.close") + QStringLiteral("  (Alt+F4)")); }
    if (tabMax_) {
        tabMax_->setIcon(makeIcon(isFullScreen() ? "unmax" : "max", mu));
        tabMax_->setToolTip((isFullScreen() ? t("gui.win.normal") : t("gui.win.full")) + QStringLiteral("  (Ctrl+0)"));
    }
    for (int i = 0; i < docTabs_->count(); ++i) {
        if (auto* b = qobject_cast<QToolButton*>(docTabs_->tabButton(i, QTabBar::RightSide))) {
            b->setIcon(makeIcon("x", mu));
            b->setToolTip(t("gui.tab.close") + QStringLiteral("  (Ctrl+F4)"));
        }
        QWidget* m = menuAt(i);
        const int id = m ? -1 : docTabs_->tabData(i).toInt();
        docTabs_->setTabIcon(i, makeIcon(m ? m->property("tabIcon").toString() : id == 0 ? QStringLiteral("home") : QStringLiteral("drawing"), mu));
    }
    for (const auto& [l, k] : startLabels_) l->setText(t(k.constData()));
    refreshDocTabs();
    refreshStartPage();
}

void MainWindow::refreshDocTabs() {
    if (!docTabs_) return;
    for (int i = 0; i < docTabs_->count(); ++i) {
        if (QWidget* m = menuAt(i)) {
            docTabs_->setTabText(i, shortTitle(m->windowTitle()));
            const int o = menuOwner(m);
            docTabs_->setTabToolTip(i, o ? t("gui.tab.owner", {m->windowTitle(), drawingName(o)}) : m->windowTitle());
            continue;
        }
        const int id = docTabs_->tabData(i).toInt();
        if (id == 0) {
            docTabs_->setTabText(i, t("gui.tab.start"));
            docTabs_->setTabToolTip(i, t("gui.start.sub"));
            continue;
        }
        const DocSlot* s = slotOf(id);
        if (!s) continue;
        docTabs_->setTabText(i, drawingName(id) + (drawingDirty(id) ? QStringLiteral("*") : QString()));
        const std::string& cf = id == activeDoc_ ? interp_->currentFile() : s->ist.currentFile;
        const QString path = !cf.empty() ? QString::fromStdString(cf) : s->source;
        docTabs_->setTabToolTip(i, path.isEmpty() ? drawingName(id) : QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
    }
}

// ---------------------------------------------------------------- chizmalar
MainWindow::DocSlot* MainWindow::slotOf(int id) {
    for (auto& s : docs_)
        if (s.id == id) return &s;
    return nullptr;
}

const MainWindow::DocSlot* MainWindow::slotOf(int id) const {
    for (const auto& s : docs_)
        if (s.id == id) return &s;
    return nullptr;
}

QWidget* MainWindow::menuAt(int index) const {
    if (!docTabs_ || index < 0 || index >= docTabs_->count()) return nullptr;
    const QVariant d = docTabs_->tabData(index);
    if (d.metaType() != QMetaType::fromType<QObject*>()) return nullptr;
    return qobject_cast<QWidget*>(d.value<QObject*>());
}

int MainWindow::tabOfDoc(int id) const {
    if (!docTabs_) return -1;
    for (int i = 0; i < docTabs_->count(); ++i)
        if (!menuAt(i) && docTabs_->tabData(i).toInt() == id) return i;
    return -1;
}

int MainWindow::tabOfMenu(const QWidget* m) const {
    if (!docTabs_ || !m) return -1;
    for (int i = 0; i < docTabs_->count(); ++i)
        if (menuAt(i) == m) return i;
    return -1;
}

QString MainWindow::drawingName(int id) const {
    const DocSlot* s = slotOf(id);
    if (!s) return {};
    const std::string& cf = id == activeDoc_ ? interp_->currentFile() : s->ist.currentFile;
    if (!cf.empty()) return QFileInfo(QString::fromStdString(cf)).fileName();
    if (!s->source.isEmpty()) return QFileInfo(s->source).fileName();
    return t("gui.tab.drawing", {QString::number(s->untitled)});
}

bool MainWindow::drawingDirty(int id) const {
    if (id == activeDoc_) return stack_.isDirty();
    const DocSlot* s = slotOf(id);
    return s && (s->hist.undo.size() != s->hist.cleanIndex || s->hist.cleanLost);
}

bool MainWindow::activateDrawing(int id) {
    DocSlot* to = slotOf(id);
    if (!to) return false;
    if (id == activeDoc_) return true;
    // joriy asbob tugatiladi; chizmadan nuqta kutayotgan menyu natija olmaydi (varaq almashdi)
    pickCb_ = {};
    pickReturn_ = nullptr;
    view_->cancelTool();
    view_->cancelTool();
    if (act_.contains("select")) act_["select"]->setChecked(true);
    if (DocSlot* from = slotOf(activeDoc_)) {
        from->center = view_->viewCenter();
        from->scale = view_->viewScale();
        from->hasView = true;
        from->viewMode = isSheets() ? 2 : is3D() ? 1 : 0;
        from->doc = std::make_unique<Document>();
        std::swap(*from->doc, doc_);
        stack_.swapHistory(from->hist);
        interp_->swapDocState(from->ist);
        std::swap(ifc_, from->ifc);
        std::swap(ifcDocFile_, from->ifcDocFile);
    }
    to = slotOf(id);
    if (to->doc) std::swap(*to->doc, doc_);
    to->doc.reset();
    stack_.swapHistory(to->hist);
    interp_->swapDocState(to->ist);
    std::swap(ifc_, to->ifc);
    std::swap(ifcDocFile_, to->ifcDocFile);
    activeDoc_ = id;
    // ko'rinish: shu chizmaning oxirgi holati (2D/3D/listlar, markaz va masshtab)
    view_->setSelection({});
    view_->documentChanged();
    if (to->hasView) view_->setView(to->center, to->scale);
    else view_->zoomExtents();
    const int mode = std::clamp(to->viewMode, 0, 2);
    {
        const QSignalBlocker b(viewTabs_);
        viewTabs_->setCurrentIndex(mode);
    }
    viewStack_->setCurrentWidget(mode == 1 ? static_cast<QWidget*>(view3d_) : mode == 2 ? sheetPage_ : static_cast<QWidget*>(view_));
    stale3d_ = true;
    refreshLayers();
    refreshProperties();
    refreshSheets();
    if (ifcPanel_) ifcPanel_->refresh();
    refreshTitle();
    if (pages_ && pages_->currentWidget() == drawPage_ && mode == 1) {
        stale3d_ = false;
        rebuild3D(false);
    }
    return true;
}

int MainWindow::newDrawing() {
    DocSlot s;
    s.id = ++docSeq_;
    s.untitled = ++untitledN_;
    s.doc = std::make_unique<Document>();
    const int id = s.id;
    docs_.push_back(std::move(s));
    int t = 0;
    {
        const QSignalBlocker b(docTabs_);
        t = docTabs_->addTab(makeIcon("drawing", mutedFor(theme_)), QString());
        docTabs_->setTabData(t, id);
        addTabClose(t);
    }
    refreshDocTabs();
    if (docTabs_->currentIndex() != t) docTabs_->setCurrentIndex(t);
    else onDocTabChanged(t);
    return id;
}

bool MainWindow::openPath(const QString& file) {
    if (file.isEmpty()) return false;
    const QString abs = QFileInfo(file).absoluteFilePath();
    // allaqachon ochiq — o'sha varaqqa o'tiladi
    for (const auto& s : docs_) {
        const std::string& cf = s.id == activeDoc_ ? interp_->currentFile() : s.ist.currentFile;
        if (sameFile(QString::fromStdString(cf), abs) || sameFile(s.source, abs)) {
            if (const int t = tabOfDoc(s.id); t >= 0) docTabs_->setCurrentIndex(t);
            return true;
        }
    }
    const int prev = activeDoc_;
    const DocSlot* cur = slotOf(activeDoc_);
    // joriy chizma bo'sh va tegilmagan (yangi "Chizma1") — fayl shu varaqda ochiladi
    const bool pristine = cur && cur->source.isEmpty() && interp_->currentFile().empty() && doc_.size() == 0 &&
                          !stack_.isDirty() && !stack_.canUndo() && !stack_.canRedo();
    if (!pristine) newDrawing();
    else if (const int t = tabOfDoc(activeDoc_); t >= 0 && docTabs_->currentIndex() != t) docTabs_->setCurrentIndex(t);
    const int id = activeDoc_;
    runLine(QStringLiteral("OPEN \"%1\" !").arg(abs));
    if (!interp_->lastOk()) {
        if (!pristine) {   // ochilmadi — yangi varaq yopiladi, oldingisiga qaytiladi
            const int t = tabOfDoc(id);
            if (const int pt = tabOfDoc(prev); pt >= 0) docTabs_->setCurrentIndex(pt);
            if (t >= 0 && activeDoc_ != id) {
                const QSignalBlocker b(docTabs_);
                docTabs_->removeTab(t);
                docs_.erase(std::remove_if(docs_.begin(), docs_.end(), [id](const DocSlot& s) { return s.id == id; }), docs_.end());
            }
            refreshDocTabs();
        }
        return false;
    }
    if (DocSlot* s = slotOf(id)) s->source = abs;
    noteRecent(abs);
    refreshTitle();
    return true;
}

bool MainWindow::closeTabAt(int index) {
    if (!docTabs_ || index < 0 || index >= docTabs_->count()) return false;
    if (QWidget* m = menuAt(index)) {
        m->close();
        removeMenuTab(m);
        return true;
    }
    const int id = docTabs_->tabData(index).toInt();
    if (id <= 0 || !slotOf(id)) return false;   // Start yopilmaydi
    if (drawingDirty(id)) {
        if (docTabs_->currentIndex() != index) docTabs_->setCurrentIndex(index);
        if (!maybeSave()) return false;
    }
    // shu chizmaga bog'langan menyular ham yopiladi
    std::vector<QWidget*> owned;
    for (auto& [m, owner] : menuDoc_)
        if (owner == id) {
            owned.push_back(const_cast<QWidget*>(m));
            owner = 0;
        }
    for (QWidget* m : owned)
        if (tabOfMenu(m) >= 0) {
            m->close();
            removeMenuTab(m);
        }
    if (docs_.size() == 1) newDrawing();   // oxirgi chizma — o'rniga yangi bo'sh chizma
    if (id == activeDoc_) {
        // qo'shni chizma varag'iga o'tiladi
        const int t = tabOfDoc(id);
        int target = -1;
        for (int k = t + 1; k < docTabs_->count() && target < 0; ++k)
            if (!menuAt(k) && docTabs_->tabData(k).toInt() > 0) target = k;
        for (int k = t - 1; k >= 0 && target < 0; --k)
            if (!menuAt(k) && docTabs_->tabData(k).toInt() > 0) target = k;
        if (target >= 0) docTabs_->setCurrentIndex(target);
    }
    if (id == activeDoc_) return false;
    {
        const QSignalBlocker b(docTabs_);
        const int t = tabOfDoc(id);
        const bool cur = docTabs_->currentIndex() == t;
        docTabs_->removeTab(t);
        if (cur) docTabs_->setCurrentIndex(std::max(0, tabOfDoc(activeDoc_)));
    }
    docs_.erase(std::remove_if(docs_.begin(), docs_.end(), [id](const DocSlot& s) { return s.id == id; }), docs_.end());
    onDocTabChanged(docTabs_->currentIndex());
    refreshDocTabs();
    return true;
}

void MainWindow::showDrawing(int id) {
    const int t = tabOfDoc(id);
    if (t < 0) return;
    if (docTabs_->currentIndex() != t) docTabs_->setCurrentIndex(t);
    else onDocTabChanged(t);
}

void MainWindow::showStart() {
    for (int i = 0; i < docTabs_->count(); ++i)
        if (!menuAt(i) && docTabs_->tabData(i).toInt() == 0) {
            if (docTabs_->currentIndex() != i) docTabs_->setCurrentIndex(i);
            else onDocTabChanged(i);
            return;
        }
}

bool MainWindow::isStart() const { return pages_ && pages_->currentWidget() == startPage_; }

void MainWindow::onDocTabChanged(int index) {
    if (tabSwitching_ || index < 0 || !pages_) return;
    tabSwitching_ = true;
    if (QWidget* m = menuAt(index)) {
        if (view_->tool() == Tool::Pick) {   // chizmadan nuqta kutilayotgan edi — bekor (natija kutilmaydi)
            pickCb_ = {};
            pickReturn_ = nullptr;
            view_->cancelTool();
        }
        if (const int o = menuOwner(m); o > 0) activateDrawing(o);
        pages_->setCurrentWidget(menuPage(m));
        setMenuChrome(true);
        m->setFocus();
    } else if (docTabs_->tabData(index).toInt() == 0) {
        if (view_->tool() == Tool::Pick) {
            pickCb_ = {};
            pickReturn_ = nullptr;
            view_->cancelTool();
        }
        refreshStartPage();
        pages_->setCurrentWidget(startPage_);
        setMenuChrome(true);
    } else {
        activateDrawing(docTabs_->tabData(index).toInt());
        pages_->setCurrentWidget(drawPage_);
        setMenuChrome(false);
        if (is3D()) {
            if (stale3d_) {
                stale3d_ = false;
                rebuild3D(false);
            }
            view3d_->setFocus();
        } else {
            view_->setFocus();
        }
    }
    tabSwitching_ = false;
    refreshTitle();
}

// Start / menyu varag'ida: lenta, asboblar va chizma panellari yashiriladi (menyu butun maydonni egallaydi)
void MainWindow::setMenuChrome(bool on) {
    if (on == menuChrome_) return;
    menuChrome_ = on;
    if (on) {
        chromeHidden_.clear();
        for (QWidget* w : std::initializer_list<QWidget*>{ribbon_, rail_, context_, floating_, layersDock_, propsDock_, symDock_, steelDock_, cmdDock_})
            if (w && !w->isHidden()) chromeHidden_ << w;
        for (const auto& w : chromeHidden_) w->hide();
    } else {
        for (const auto& w : chromeHidden_)
            if (w) w->show();
        chromeHidden_.clear();
        updateContextDocks();
    }
}

void MainWindow::toggleFullScreen() {
    if (isFullScreen()) {
        if (wasMaximized_) showMaximized();
        else showNormal();
    } else {
        wasMaximized_ = isMaximized();
        showFullScreen();
    }
    retranslateTabs();
}

// ---------------------------------------------------------------- menyular varaq bo'lib
QWidget* MainWindow::menuPage(QWidget* m) {
    if (auto it = menuWrap_.find(m); it != menuWrap_.end()) return it->second;
    auto* sc = new QScrollArea(pages_);
    sc->setObjectName("menuPage");
    sc->setFrameShape(QFrame::NoFrame);
    sc->setWidgetResizable(true);   // kichik ekranda menyu siqilmaydi — aylantiriladi
    m->setAttribute(Qt::WA_Moved, true);   // QDialog o'zini ota oyna markaziga surmasin
    sc->setWidget(m);
    m->installEventFilter(this);
    if (m->property("tabIcon").toString().isEmpty()) {
        QString ic = QStringLiteral("menu");
        if (m == sewer_) ic = "pipes";
        else if (m == building_) ic = "bim";
        else if (m == rebar_) ic = "rebar";
        else if (m == util_) ic = "util";
        else if (m == road_) ic = "road";
        else if (m == waterway_) ic = "waterway";
        else if (m == corridor_) ic = "corridor";
        else if (m == city_) ic = "city";
        else if (m == tpl_) ic = "steelframe";
        else if (m == survey_) ic = "devplan";
        else if (m == earth_) ic = "volume";
        else if (m == drive_) ic = "drive";
        m->setProperty("tabIcon", ic);
    }
    menuWrap_[m] = sc;
    return sc;
}

void MainWindow::showMenu(QWidget* m) {
    if (!m || !pages_) return;
    QWidget* page = menuPage(m);
    menuDoc_[m] = activeDoc_;   // menyu joriy chizmaga bog'lanadi (Start/menyu varag'ida — oxirgi faol chizma)
    int t = tabOfMenu(m);
    if (t < 0) {
        if (pages_->indexOf(page) < 0) pages_->addWidget(page);
        const QSignalBlocker b(docTabs_);
        // menyu varag'i — o'z chizmasidan keyin (shu chizmaning boshqa menyularidan keyin)
        int at = tabOfDoc(activeDoc_);
        if (at < 0) at = docTabs_->count() - 1;
        while (at + 1 < docTabs_->count() && menuAt(at + 1) && menuOwner(menuAt(at + 1)) == activeDoc_) ++at;
        t = docTabs_->insertTab(at + 1, makeIcon(m->property("tabIcon").toString(), mutedFor(theme_)), shortTitle(m->windowTitle()));
        docTabs_->setTabData(t, QVariant::fromValue<QObject*>(m));
        addTabClose(t);
    }
    m->setAttribute(Qt::WA_Moved, true);
    if (m->isHidden()) m->show();
    refreshDocTabs();
    if (docTabs_->currentIndex() != t) docTabs_->setCurrentIndex(t);
    else onDocTabChanged(t);
    if (isMinimized()) showNormal();
    raise();
    activateWindow();
}

void MainWindow::removeMenuTab(QWidget* m) {
    const int t = tabOfMenu(m);
    if (t < 0) return;
    const bool cur = docTabs_->currentIndex() == t;
    {
        const QSignalBlocker b(docTabs_);
        docTabs_->removeTab(t);
    }
    if (auto it = menuWrap_.find(m); it != menuWrap_.end()) pages_->removeWidget(it->second);
    if (pickReturn_ == m) pickReturn_ = nullptr;
    if (cur) {
        int target = tabOfDoc(menuOwner(m));
        if (target < 0) target = std::clamp(t, 0, docTabs_->count() - 1);
        {
            const QSignalBlocker b(docTabs_);
            docTabs_->setCurrentIndex(target);
        }
        onDocTabChanged(docTabs_->currentIndex());
    }
}

QWidget* MainWindow::currentMenu() const { return docTabs_ ? menuAt(docTabs_->currentIndex()) : nullptr; }

bool MainWindow::menuOpen(const QWidget* m) const { return tabOfMenu(m) >= 0; }

int MainWindow::menuOwner(const QWidget* m) const {
    const auto it = menuDoc_.find(m);
    return it == menuDoc_.end() ? 0 : it->second;
}

// chizmadan nuqta olish: menyu o'z chizmasi varag'iga o'tadi, tugagach qaytadi
void MainWindow::menuAway(QWidget* m) {
    const int t = tabOfDoc(menuOwner(m));
    if (t >= 0 && docTabs_->currentIndex() != t) docTabs_->setCurrentIndex(t);
    if (is3D() || isSheets()) show3D(false);
    pickReturn_ = m;
}

void MainWindow::menuBack(QWidget* m) {
    if (!m || pickReturn_ != m) return;
    pickReturn_ = nullptr;
    if (menuOpen(m)) showMenu(m);
}

// ---------------------------------------------------------------- Ctrl+Z / Ctrl+Y
MenuUndo* MainWindow::historyOf(QWidget* m) const {
    if (!m) return nullptr;
    if (m == util_) return util_->menuHistory();
    if (m == road_) return road_->menuHistory();
    if (m == waterway_) return waterway_->menuHistory();
    if (m == corridor_) return corridor_->menuHistory();
    if (m == sewer_) return sewer_->menuHistory();
    if (m == city_) return city_->menuHistory();
    if (m == rebar_) return rebar_->menuHistory();
    if (m == building_) return building_->menuHistory();
    return nullptr;
}

void MainWindow::menuDocumentChanged(QWidget* m) {
    // chizmadan o'qiydigan menyular yangilanadi (shu chizmaga bog'langan va ochiq bo'lsa)
    if (building_ && ownedHere(building_) && (m == building_ || menuOpen(building_))) building_->documentChanged();
    if (rebar_ && ownedHere(rebar_) && (m == rebar_ || menuOpen(rebar_))) rebar_->documentChanged();
    if (m && m == earth_) earth_->refreshLayers();
    if (m && m == survey_) survey_->refreshLayers();
}

void MainWindow::undoRedo(bool undo) {
    QWidget* m = currentMenu();
    if (MenuUndo* h = historyOf(m)) {
        for (int guard = 0; guard < 500; ++guard) {
            const MenuUndo::Step st = undo ? h->undoStep() : h->redoStep();
            if (st.kind == MenuUndo::None) break;
            if (st.kind == MenuUndo::Model) {
                statusBar()->showMessage(t(undo ? "gui.undo.menu" : "gui.redo.menu", {m->windowTitle()}), 3000);
                return;
            }
            // menyu chizmaga yozgan amal — hali chizma tarixida tepada bo'lsa qaytariladi
            if (st.cmd == (undo ? static_cast<const void*>(stack_.undoTop()) : static_cast<const void*>(stack_.redoTop()))) {
                runLine(undo ? QStringLiteral("UNDO") : QStringLiteral("REDO"));
                menuDocumentChanged(m);
                return;
            }
            if (undo) h->dropRedoTop();
            else h->dropUndoTop();
        }
    }
    runLine(undo ? QStringLiteral("UNDO") : QStringLiteral("REDO"));
    menuDocumentChanged(m);
}

bool MainWindow::menuRun(QWidget* m, const QStringList& lines) {
    const std::size_t before = stack_.undoCount(), pushes = stack_.pushCount();
    for (const auto& l : lines)
        if (!runLine(l)) return false;
    if (stack_.pushCount() == pushes) return true;   // chizma o'zgarmadi (xato)
    if (const std::size_t after = stack_.undoCount(); after > before + 1) stack_.squash(after - before);   // menyu amali — bitta qadam
    if (MenuUndo* h = historyOf(m)) h->noteDrawing(stack_.undoTop());
    refreshTitle();
    return true;
}

// ---------------------------------------------------------------- Start sahifasi
QWidget* MainWindow::buildStartPage() {
    auto* page = new QWidget(this);
    page->setObjectName("startPage");
    page->setAttribute(Qt::WA_StyledBackground, true);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* sc = new QScrollArea(page);
    sc->setObjectName("startScroll");
    sc->setFrameShape(QFrame::NoFrame);
    sc->setWidgetResizable(true);
    outer->addWidget(sc);
    auto* body = new QWidget(sc);
    body->setObjectName("startBody");
    sc->setWidget(body);
    auto* col = new QVBoxLayout(body);
    col->setContentsMargins(40, 28, 40, 28);
    col->setSpacing(18);
    auto lbl = [&](const char* name, const char* key, QWidget* parent) {
        auto* l = new QLabel(t(key), parent);
        l->setObjectName(QString::fromLatin1(name));
        startLabels_.append({l, QByteArray(key)});
        return l;
    };
    {   // sarlavha
        auto* head = new QHBoxLayout;
        head->setSpacing(14);
        auto* logo = new QLabel(body);
        logo->setPixmap(makeIcon("logo", QColor("#E0A526"), QColor(), 128).pixmap(52, 52));
        head->addWidget(logo);
        auto* ht = new QVBoxLayout;
        ht->setSpacing(2);
        auto* title = new QLabel(QStringLiteral("<span style='font-size:26px; font-weight:800;'>Asia<span style='color:#E0A526'>CAD</span></span>"
                                                "<span style='font-size:13px; color:#7F92BF;'>&nbsp;&nbsp;0.31</span>"), body);
        title->setObjectName("startTitle");
        title->setTextFormat(Qt::RichText);
        ht->addWidget(title);
        ht->addWidget(lbl("startSub", "gui.start.sub", body));
        head->addLayout(ht);
        head->addStretch(1);
        col->addLayout(head);
    }
    auto card = [&](const char* headKey) {
        auto* f = new QFrame(body);
        f->setObjectName("startCard");
        auto* v = new QVBoxLayout(f);
        v->setContentsMargins(18, 16, 18, 16);
        v->setSpacing(10);
        v->addWidget(lbl("startHead", headKey, f));
        return std::make_pair(f, v);
    };
    auto* row = new QHBoxLayout;
    row->setSpacing(16);
    {   // boshlash: yangi / ochish + menyular
        auto [f, v] = card("gui.start.begin");
        for (const char* k : {"new", "open"}) {
            auto* b = new QToolButton(f);
            b->setDefaultAction(act_[k]);
            b->setObjectName("startBig");
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            b->setIconSize(QSize(22, 22));
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setCursor(Qt::PointingHandCursor);
            v->addWidget(b);
        }
        v->addSpacing(6);
        v->addWidget(lbl("startHead", "gui.start.menus", f));
        auto* grid = new QGridLayout;
        grid->setSpacing(6);
        int i = 0;
        for (const char* k : {"building", "rebar", "sewer", "util", "road", "waterway", "corridor", "city", "steelframe", "devplan", "ewvolume", "ifcimport"}) {
            auto* b = new QToolButton(f);
            b->setDefaultAction(act_[k]);
            b->setObjectName("startMenu");
            b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            b->setIconSize(QSize(26, 26));
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            b->setMinimumWidth(96);
            b->setCursor(Qt::PointingHandCursor);
            grid->addWidget(b, i / 3, i % 3);
            ++i;
        }
        v->addLayout(grid);
        v->addStretch(1);
        row->addWidget(f, 5);
    }
    auto list = [&](const char* name) {
        auto* l = new QListWidget(body);
        l->setObjectName(QString::fromLatin1(name));
        l->setIconSize(QSize(20, 20));
        l->setFrameShape(QFrame::NoFrame);
        l->setCursor(Qt::PointingHandCursor);
        l->setMinimumHeight(220);
        auto open = [this](QListWidgetItem* it) {
            if (!it) return;
            const QString f = it->data(Qt::UserRole).toString();
            if (f.isEmpty()) return;
            if (f.endsWith(".ifc", Qt::CaseInsensitive) || f.endsWith(".ifczip", Qt::CaseInsensitive)) importIfc({f});
            else openPath(f);
        };
        connect(l, &QListWidget::itemClicked, this, open);
        connect(l, &QListWidget::itemActivated, this, open);
        return l;
    };
    {
        auto [f, v] = card("gui.start.recent");
        startRecentNone_ = lbl("startNone", "gui.start.none", f);
        v->addWidget(startRecentNone_);
        startRecent_ = list("startRecent");
        v->addWidget(startRecent_, 1);
        row->addWidget(f, 6);
    }
    {
        auto [f, v] = card("gui.start.samples");
        startSamples_ = list("startSamples");
        v->addWidget(startSamples_, 1);
        row->addWidget(f, 5);
    }
    col->addLayout(row, 1);
    auto* hint = lbl("startHint", "gui.start.hint", body);
    hint->setWordWrap(true);
    col->addWidget(hint);
    startPage_ = page;
    return page;
}

void MainWindow::refreshStartPage() {
    if (!startRecent_ || !startSamples_) return;
    const QColor mu = mutedFor(theme_);
    startRecent_->clear();
    for (const QString& f : recent_) {
        const QFileInfo fi(f);
        if (!fi.exists()) continue;
        const bool ifc = f.endsWith(".ifc", Qt::CaseInsensitive) || f.endsWith(".ifczip", Qt::CaseInsensitive);
        auto* it = new QListWidgetItem(makeIcon(ifc ? "ifc" : "drawing", mu), fi.fileName() + QStringLiteral("\n") + QDir::toNativeSeparators(fi.absolutePath()));
        it->setData(Qt::UserRole, fi.absoluteFilePath());
        it->setToolTip(QDir::toNativeSeparators(fi.absoluteFilePath()));
        startRecent_->addItem(it);
    }
    if (startRecentNone_) startRecentNone_->setVisible(startRecent_->count() == 0);
    startSamples_->clear();
    QStringList dirs = {QCoreApplication::applicationDirPath() + "/examples", QCoreApplication::applicationDirPath() + "/../examples",
                        QCoreApplication::applicationDirPath() + "/../../examples"};
    for (const QString& d : dirs) {
        const QDir dir(d);
        if (!dir.exists()) continue;
        for (const QFileInfo& fi : dir.entryInfoList({"*.acad", "*.dxf"}, QDir::Files, QDir::Name)) {
            auto* it = new QListWidgetItem(makeIcon("drawing", mu), fi.completeBaseName());
            it->setData(Qt::UserRole, fi.absoluteFilePath());
            it->setToolTip(QDir::toNativeSeparators(fi.absoluteFilePath()));
            startSamples_->addItem(it);
        }
        break;
    }
}

} // namespace asiacad::gui
