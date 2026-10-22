#pragma once
// AsiaCAD GUI — professional 3D ko'rinish (OpenGL): Revit / SketchUp / Civil 3D uslubidagi navigatsiya.
//  * Sichqoncha: chap tugma — aylantirish (bosilgan nuqta atrofida), o'rta / o'ng tugma yoki Shift+chap — surish,
//    g'ildirak — kursorga qarab yaqinlashtirish, ikki marta bosish — hammasi; chap bosish — obyektni tanlash,
//    o'ng bosish — menyu (ajratish, yashirish, markazlash)
//  * ViewCube (o'ng yuqori): yuzlar — Yuqori/Old/O'ng/Orqa/Chap/Past, burchaklar — izometriya; uy — boshlang'ich
//  * Ko'rinish uslubi: bo'yalgan, bo'yalgan + qirralar, sim karkas, rentgen; perspektiva / ortografik
//  * Kesim (Z bo'yicha — qavat planini ko'rish), toifalar ko'rinishi (Revit "Visibility/Graphics"), PNG rasm
//  * Katta koordinatalar (UTM) — mahalliy boshlang'ich nuqta bilan (float aniqligi yo'qolmaydi)
// API Pipe3DView bilan mos: beginScene / addPrism / addFace / addPolyline / addLabel / endScene.
#include "pipe3d_view.h"

#include <asiacad/civil/drive.h>

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>

#include <functional>
#include <map>
#include <memory>
#include <set>

class QToolButton;
class QSlider;
class QLabel;

namespace asiacad::gui {

class Model3DView : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    enum Style { Shaded = 0, ShadedEdges = 1, Wireframe = 2, XRay = 3 };
    enum View { Home, Top, Bottom, Front, Back, Left, Right, IsoSW, IsoSE, IsoNE, IsoNW };

    explicit Model3DView(QWidget* parent = nullptr);
    ~Model3DView() override;

    // tarjima (ViewCube yozuvlari, tugmalar)
    void setTr(std::function<QString(const char*)> tr);
    static void setGlobalTr(std::function<QString(const char*)> tr);   // hamma 3D oynalar uchun (til almashganda)
    void setDark(bool dark);

    // ---- sahna (Pipe3DView bilan mos)
    void beginScene(const QString& title, const QString& subtitle);
    void beginObject(const QString& name, const QString& category, const QStringList& props = {});
    void endObject();
    void addPrism(const std::vector<P3>& outline, const std::vector<std::vector<P3>>& holes, double z0, double z1, const QColor& c, int alpha = 255);
    void addFace(const std::vector<P3>& pts, const QColor& c, int alpha = 255, bool outline = false);
    void addPolyline(const std::vector<P3>& pts, const QColor& c, double widthPx, bool closed = false);
    void addTube(const std::vector<P3>& pts, double radius, const QColor& c, bool closed = false, int sides = 6);
    void addTriangles(const civil::DriveScene& sc, double alpha = 1.0);   // Haydash sahnasi (yuzalar + chiziqlar)
    void addLabel(const P3& at, const QString& text);
    void endScene(bool keepView = false);
    void setEmptyText(const QString& t) { empty_ = t; update(); }

    // ---- ko'rinish
    void resetView();
    void fit();
    void setView(View v);
    void setStyle(Style s);
    Style style() const { return style_; }
    void setOrtho(bool on);
    bool ortho() const { return ortho_; }
    void setClip(double frac);          // 0..1 — balandlik bo'yicha kesim (1 — kesimsiz)
    void setCategoryVisible(const QString& cat, bool on);
    QStringList categories() const;
    void isolateSelected();
    void hideSelected();
    void showAll();

    // ---- holat (sinov uchun)
    int faceCount() const { return faces_; }
    int objectCount() const { return static_cast<int>(objs_.size()); }
    int triangleCount() const { return static_cast<int>(tri_.size() / kTriStride / 3); }
    int selected() const { return sel_; }
    bool selectAt(const QPoint& px);    // ekran nuqtasi bo'yicha tanlash
    QString selectionInfo() const;
    QVector3D cameraDir() const;
    QImage snapshot();

signals:
    void objectSelected(const QString& name, const QStringList& props);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    static constexpr int kTriStride = 11;   // pos3 nor3 col4 id1
    static constexpr int kLineStride = 8;   // pos3 col4 id1
    struct Obj {
        QString name, category;
        QStringList props;
        std::size_t t0 = 0, t1 = 0, l0 = 0, l1 = 0, e0 = 0, e1 = 0;   // vertex oraliqlari (tri, line, edge)
        QVector3D bmin{1e30f, 1e30f, 1e30f}, bmax{-1e30f, -1e30f, -1e30f};
        bool hidden = false;
    };
    struct Label { QVector3D at; QString text; };

    QString tr_(const char* k) const;
    int curObj();
    QVector3D L(const P3& p) const;            // dunyo -> mahalliy
    void tri(const QVector3D& a, const QVector3D& b, const QVector3D& c, const QColor& col, int alpha);
    void seg(std::vector<float>& buf, const QVector3D& a, const QVector3D& b, const QColor& col, int alpha);
    void polygonTris(const std::vector<QVector3D>& outer, const std::vector<std::vector<QVector3D>>& holes, const QColor& c, int alpha, bool flip);
    void upload();
    void buildUi();
    void retranslateUi();
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projMatrix() const;
    QVector3D eye() const;
    QVector3D dir() const;
    bool rayHit(const QPoint& px, QVector3D& hit, int* obj = nullptr) const;
    void rayFrom(const QPoint& px, QVector3D& o, QVector3D& d) const;
    void orbit(double dYaw, double dPitch);
    void pan(const QPoint& delta);
    void zoomAt(const QPoint& px, double factor);
    void drawOverlay(QPainter& p);
    void drawCube(QPainter& p);
    int cubeHit(const QPoint& px) const;   // -1 yo'q, 0..5 yuzlar, 6..9 burchaklar, 10 uy

    std::function<QString(const char*)> trf_;
    static std::function<QString(const char*)>& globalTr();
    bool dark_ = true;
    // sahna
    std::vector<float> tri_, line_, edge_;    // barcha (yashirinlarini ham) — CPU nusxa
    std::vector<Obj> objs_;
    std::vector<Label> labels_;
    int cur_ = -1;
    int faces_ = 0;
    bool haveOrigin_ = false;
    double ox_ = 0, oy_ = 0, oz_ = 0;         // mahalliy boshlang'ich nuqta
    QVector3D smin_{0, 0, 0}, smax_{1, 1, 1};
    QString title_, subtitle_, empty_;
    std::set<QString> hiddenCats_;
    // GPU
    bool glReady_ = false, dirty_ = true;
    std::unique_ptr<QOpenGLShaderProgram> prog_;
    QOpenGLBuffer vOpaque_{QOpenGLBuffer::VertexBuffer}, vTrans_{QOpenGLBuffer::VertexBuffer}, vLine_{QOpenGLBuffer::VertexBuffer},
        vEdge_{QOpenGLBuffer::VertexBuffer};
    int nOpaque_ = 0, nTrans_ = 0, nLine_ = 0, nEdge_ = 0;
    // kamera
    QVector3D target_{0, 0, 0};
    double dist_ = 50, yaw_ = 35, pitch_ = 30, fov_ = 40;
    bool ortho_ = false;
    bool fitted_ = false;
    bool userMoved_ = false;
    Style style_ = ShadedEdges;
    double clip_ = 1.0;
    int sel_ = -1;
    // sichqoncha
    QPoint press_, last_;
    Qt::MouseButton btn_ = Qt::NoButton;
    bool dragged_ = false, panMode_ = false;
    QVector3D pivot_;
    bool havePivot_ = false;
    // tugmalar
    QWidget* bar_ = nullptr;
    QToolButton *bFit_ = nullptr, *bProj_ = nullptr, *bStyle_ = nullptr, *bCut_ = nullptr, *bVis_ = nullptr, *bShot_ = nullptr;
    QSlider* cut_ = nullptr;
    QWidget* cutBox_ = nullptr;
    mutable std::vector<std::pair<std::vector<QPointF>, int>> cubeFaces_;   // ekran ko'pburchagi, yuz raqami
    QRect cubeRect_, homeRect_;
};

} // namespace asiacad::gui
