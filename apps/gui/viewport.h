#pragma once
// AsiaCAD GUI — chizma oynasi (OpenGL sirt ustida QPainter).
//  * Zoom — g'ildirak (kursor atrofida), Pan — o'rta tugma yoki Space+chap tugma
//  * Tanlash — bosish (Shift — qo'shish/olib tashlash), ramka: chapdan o'ngga — WINDOW, o'ngdan chapga — CROSSING
//  * Asboblar nuqtani bog'lanish (OSNAP) bilan oladi va tayyor buyruq satrini runCommand() orqali yuboradi —
//    shu tufayli har bir amal Interpreter -> CommandStack orqali o'tadi va undo/redo buzilmaydi.
#include "theme.h"

#include <asiacad/core/document.h>
#include <asiacad/core/geom2d.h>
#include <asiacad/core/snap.h>
#include <asiacad/i18n/localizer.h>

#include <QOpenGLWidget>
#include <QPointF>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

class QPainter;
class QMenu;
class QTimer;

namespace asiacad::gui {

enum class Tool { Select, Line, Pline, Circle, Arc, Point, Text, Move, Copy, Rotate,
                  DimLinear, DimAligned, DimRadius, DimDiameter, Grip,
                  Offset, Trim, Extend, Fillet,
                  Rect, Polygon, Ellipse, Spline, Stretch, Scale, Mirror, Chamfer, Break, Leader, DimAngular,
                  Symbol, MatchProp, LayPick, SteelBeam, SteelSec, Pick, PipeDraw };

class Viewport : public QOpenGLWidget {
    Q_OBJECT
public:
    Viewport(Document& doc, const i18n::Localizer& loc, QWidget* parent = nullptr);

    void setTheme(const Theme& t);
    void setSnapModes(unsigned m) { snapModes_ = m; }
    unsigned snapModes() const { return snapModes_; }
    void setGridVisible(bool on) { grid_ = on; update(); }
    // ORTO (F8) — faqat gorizontal/vertikal; QUTB (F10) — polarStep burchak qadamlariga tortiladi.
    // Ikkalasi bir vaqtda yoqilmaydi (AutoCAD kabi). OSNAP ustunlik qiladi.
    void setOrtho(bool on);
    void setPolar(bool on);
    bool ortho() const { return ortho_; }
    bool polar() const { return polar_; }
    void setPolarStep(double deg) { polarStepDeg_ = deg > 0 ? deg : 15.0; }
    double polarStep() const { return polarStepDeg_; }
    // AutoCAD holat satri yordamchilari: OSNAP (F3) — umumiy kalit, QADAM (F9) — to'r qadamiga tortish,
    // OTRACK (F11) — bog'lanish nuqtasidan gorizontal/vertikal kuzatuv, DYN (F12) — kursor yonida koordinata
    void setOsnap(bool on) { osnapOn_ = on; updateSnap(mouse_); update(); }
    bool osnap() const { return osnapOn_; }
    void setGridSnap(bool on) { gridSnap_ = on; updateSnap(mouse_); update(); }
    bool gridSnap() const { return gridSnap_; }
    void setSnapUnit(double u) { if (u > 0) snapUnit_ = u; }
    double snapUnit() const { return snapUnit_; }
    void setOtrack(bool on) { otrack_ = on; if (!on) tracked_.clear(); update(); }
    bool otrack() const { return otrack_; }
    void addTrackPoint(const Vec3& p);  // OTRACK nuqtasi (sinov uchun; odatda kursor 0,35 s turganda)
    const std::vector<Vec3>& trackPoints() const { return tracked_; }
    void setDyn(bool on) { dyn_ = on; update(); }
    bool dyn() const { return dyn_; }
    void setCrosshairSize(int pct) { cursorPct_ = std::clamp(pct, 1, 100); update(); }
    int crosshairSize() const { return cursorPct_; }
    void setAnnoScale(double denom) { annoScale_ = denom > 0 ? denom : 0; }  // 0 — avtomatik
    double annoScale() const { return annoScale_; }
    void setSnapOverride(unsigned mode) { snapOverride_ = mode; updateSnap(mouse_); update(); }  // bir martalik (Shift+o'ng)
    unsigned snapOverride() const { return snapOverride_; }
    Tool lastTool() const { return lastTool_; }
    bool repeatLast();                         // bo'sh holatda Enter/Space — oxirgi asbob
    QMenu* contextMenu(QWidget* parent);        // o'ng tugma menyusi (holatga qarab)
    QMenu* snapMenu(QWidget* parent);           // Shift+o'ng: bir martalik bog'lanish
    bool windowPending() const { return boxPending_; }
    // Kompas / UCS belgisi geometriyasi (ekran)
    QRectF compassTopRect() const;
    QPointF compassCenter() const;
    // To'g'ridan-to'g'ri masofa: kuzatuv yo'nalishi (yoki kursor tomoni) bo'yicha d masofadagi nuqta
    bool feedDistance(double d);
    double dimTextHeight() const;
    void setOffsetDistance(double d) { if (d > 0) offsetDist_ = d; emitPrompt(); update(); }
    double offsetDistance() const { return offsetDist_; }
    void setFilletRadius(double r) { if (r >= 0) filletR_ = r; emitPrompt(); update(); }
    double filletRadius() const { return filletR_; }
    void setChamferDistance(double d) { if (d >= 0) chamferD_ = d; emitPrompt(); update(); }
    void setPolygonSides(int n) { if (n >= 3) polySides_ = n; emitPrompt(); }
    int polygonSides() const { return polySides_; }
    void setSymbol(const QString& key, double scale, double rotDeg) { symKey_ = key; symScale_ = scale; symRot_ = rotDeg; }
    void setLayerPickCommand(const QString& canon) { layCmd_ = canon; }
    void setSteelProfile(const QString& name) { steel_ = name; }
    void setPipeArgs(const QString& args) { pipeArgs_ = args; }   // PIPEDRAW buyrug'i: tarmoq nomi va sozlamalari
    const QString& pipeArgs() const { return pipeArgs_; }
    // nuqta(lar) olish (oynalar uchun): n ta nuqta — pointsPicked; Esc — pickCancelled. prompts — har nuqta uchun i18n kalit
    void startPick(int n, const QStringList& prompts);  // LAYMCUR, LAYOFF, LAYFRZ, LAYLCK, LAYULK, LAYISO, LAYMATCH  // joriy masshtabda o'qiladigan "chiroyli" balandlik
    // Tutqichlar: tanlangan obyektning tutqichi ustida bo'lsa (id, indeks)
    std::optional<std::pair<EntityId, int>> gripAt(const QPointF& screen) const;
    void beginGrip(EntityId id, int index);  // sinov va skriptlar uchun
    bool gridVisible() const { return grid_; }

    void zoomExtents();
    void zoomBy(double factor);  // markaz atrofida
    void setView(const Vec3& center, double pixelsPerUnit);

    Tool tool() const { return tool_; }
    void setTool(Tool t);
    void cancelTool();
    void finishTool();                 // Enter: poliliniyani yakunlash va h.k.
    bool feedPoint(const Vec3& world); // klaviaturadan kiritilgan nuqta (asbob faol bo'lsa)
    std::optional<Vec3> lastPoint() const;

    const std::vector<EntityId>& selection() const { return sel_; }
    void setSelection(std::vector<EntityId> ids);
    void documentChanged();            // buyruqdan keyin: o'chgan obyektlarni tanlovdan olib tashlash

    void simulateCursor(const Vec3& world);  // sinov: kursorni dunyo nuqtasiga qo'yish

    // Modelni boshqa sirtga chizish (list ko'rinishi, PDF): target — qurilma to'rtburchagi, ppu — piksel/m,
    // qog'oz ranglari (7-rang qora), penMul — chiziq qalinligi koeffitsienti
    void renderModel(QPainter& p, const QRectF& target, const Vec3& center, double ppu, double penMul = 1.0);

    Vec3 toWorld(const QPointF& s) const;
    QPointF toScreen(const Vec3& w) const;
    static QString fmtPoint(const Vec3& p);

signals:
    void runCommand(const QString& line);
    void cursorMoved(double x, double y);
    void promptChanged(const QString& text);
    void selectionChanged();
    void orthoPolarChanged(bool ortho, bool polar);
    void textRequested(const Vec3& at);  // matn asbobi: oyna dialog ochadi
    void leaderRequested(const QString& points);
    void pointsPicked(const std::vector<Vec3>& pts);
    void pickCancelled();
    void request3D();                       // kompas TOP — ikki marta bosish
    void repeatRequested();                 // oxirgi asbob yo'q — oxirgi buyruq satrini takrorlash  // vynoska: nuqtalar tayyor — matn so'raladi

protected:
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    void drawGrid(QPainter& p);
    void drawEntity(QPainter& p, const IEntity& e, const QColor& inheritColor, bool selected, int depth = 0);
    void drawOverlay(QPainter& p);
    void drawSnapMarker(QPainter& p, const SnapResult& s);
    QColor colorOf(const IEntity& e, const QColor& inherit) const;
    void updateSnap(const QPointF& screen);
    Vec3 acquirePoint() const { return acquired_; }  // bog'lanish > orto/qutb > kursor
    std::optional<Vec3> trackBase() const;
    void drawGrips(QPainter& p);
    void drawTracking(QPainter& p);
    void drawUcsIcon(QPainter& p);
    void drawCompass(QPainter& p);
    void drawOtrack(QPainter& p);
    void drawDyn(QPainter& p);
    bool overCompass(const QPointF& s) const;
    void finishBox(const QPointF& end, bool additive);
    void drawPreviewEntity(QPainter& p, const IEntity& e);
    std::unique_ptr<IEntity> previewEntity(const Vec3& cur) const;
    void tip(QPainter& p, const QPointF& at, const QString& text, const QColor& bg);
    void clickPoint(const Vec3& p);
    void emitPrompt();
    int pickN_ = 1;
    QStringList pickPrompts_;
    QString selArg() const;
    double aperture() const { return 12.0 / scale_; }

    Document& doc_;
    const i18n::Localizer& loc_;
    Theme theme_;
    Vec3 center_{0, 0, 0};
    double scale_ = 10.0;  // piksel / birlik
    bool grid_ = true;
    unsigned snapModes_ = SnapEnd | SnapMid | SnapCen | SnapInt | SnapPer | SnapNod | SnapIns;

    Tool tool_ = Tool::Select;
    std::vector<Vec3> pts_;  // asbob uchun olingan nuqtalar
    std::vector<EntityId> sel_;

    QPointF mouse_;
    bool mouseInside_ = false;
    std::optional<SnapResult> snap_;
    Vec3 acquired_;
    struct Track { Vec3 base; double angleDeg; bool ortho; };
    std::optional<Track> track_;
    bool ortho_ = false, polar_ = false;
    double polarStepDeg_ = 15.0;
    EntityId gripId_ = kInvalidId;
    int gripIdx_ = -1;
    std::optional<std::pair<EntityId, int>> hoverGrip_;
    bool gripDrag_ = false;  // tutqichdan bosib sudrash (qo'yib yuborganda yakunlanadi)
    QPointF gripPress_;
    EntityId dimTarget_ = kInvalidId;
    double dimH_ = 0;  // 0 — masshtabdan  // radius/diametr: tanlangan aylana/yoy
    QColor previewColor_;
    double offsetDist_ = 1.0, filletR_ = 0.0;
    EntityId modTarget_ = kInvalidId;  // OFFSET obyekti / FILLET birinchi chizig'i
    Vec3 modPick_;                     // FILLET birinchi tanlov nuqtasi (chiziq ustida)
    bool isModifyTool() const {
        return tool_ == Tool::Offset || tool_ == Tool::Trim || tool_ == Tool::Extend || tool_ == Tool::Fillet || tool_ == Tool::Chamfer ||
               (tool_ == Tool::Break && modTarget_ == kInvalidId) || tool_ == Tool::MatchProp || tool_ == Tool::LayPick;
    }
    double chamferD_ = 1.0;
    int polySides_ = 6;
    QString symKey_ = "TREE", layCmd_ = "LAYMCUR", steel_ = "I20", pipeArgs_ = "K1";
    double symScale_ = 1.0, symRot_ = 0.0;
    // Ekran apertura bilan obyekt tanlash; nuqta obyekt ustiga tushiriladi (buyruq satri aperturasi kichik bo'lsa ham)
    EntityId pickAt(const Vec3& p, Vec3* onCurve, bool editableCurvesOnly = true) const;
    std::vector<geom::Prim> edgesAround(EntityId self, bool everything) const;
    bool panning_ = false, spaceDown_ = false, spacePanned_ = false;
    bool osnapOn_ = true, gridSnap_ = false, otrack_ = true, dyn_ = true;
    double snapUnit_ = 1.0, annoScale_ = 0;
    int cursorPct_ = 5;
    unsigned snapOverride_ = 0;
    Tool lastTool_ = Tool::Select;
    bool boxPending_ = false;           // bosish-bosish ramkasi: birinchi burchak olingan
    std::vector<Vec3> tracked_;         // OTRACK olingan nuqtalar
    struct OtrackHit { Vec3 pt; std::vector<Vec3> from; };
    std::optional<OtrackHit> otHit_;
    QTimer* hoverTimer_ = nullptr;
    std::optional<Vec3> hoverSnap_;
    bool shiftHeld_ = false;
    std::optional<QRectF> target_;       // renderModel: tashqi sirt
    double penMul_ = 1.0;
    QPointF viewMid() const { return target_ ? target_->center() : QPointF(width() / 2.0, height() / 2.0); }            // Shift — ORTO vaqtincha teskari (AutoCAD)
    QPointF panStart_;
    Vec3 panCenter_;
    bool boxing_ = false;
    QPointF boxStart_;
};

} // namespace asiacad::gui
