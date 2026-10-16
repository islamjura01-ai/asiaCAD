#pragma once
// AsiaCAD GUI — quvur va kanalizatsiya tarmog'ining 3D ko'rinishi (QPainter, ortografik, rassom algoritmi).
//  * Bitta quvur: tashqi/ichki sirt, devor kesimi (halqa), raструб, gofra qovurg'alari, o'lcham yozuvlari
//  * Tarmoq: quduqlar (beton halqa + lyuk), quvurlar haqiqiy belgilarda, yer sathi lentasi;
//    vertikal masshtab avtomatik oshiriladi (yozuvda ko'rsatiladi)
// Sichqoncha: chap — aylantirish, o'ng/o'rta — surish, g'ildirak — yaqinlashtirish, ikki marta bosish — boshlang'ich ko'rinish.
#include <asiacad/civil/sewer.h>

#include <QColor>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <optional>
#include <vector>

namespace asiacad::gui {

struct P3 { double x = 0, y = 0, z = 0; };

class Pipe3DView : public QWidget {
    Q_OBJECT
public:
    explicit Pipe3DView(QWidget* parent = nullptr);

    void showPipe(const civil::PipeSpec& spec, const QString& title);
    void showNetwork(const civil::SewerNetwork& net, const civil::SewerResult& res);
    void setDark(bool dark) { dark_ = dark; update(); }
    // Umumiy sahna (bino modeli va boshqalar): vertikal prizmalar (teshikli), yozuvlar
    void beginScene(const QString& title, const QString& subtitle);
    void addPrism(const std::vector<P3>& outline, const std::vector<std::vector<P3>>& holes, double z0, double z1, const QColor& c,
                  int alpha = 255);
    void addLabel(const P3& at, const QString& text);
    void endScene(bool keepView = false);
    void resetView();
    QSize sizeHint() const override { return {420, 320}; }
    int faceCount() const { return static_cast<int>(faces_.size()); }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    struct Face {
        std::vector<P3> pts;
        QColor color;
        bool outline = false;
        int alpha = 255;
        std::vector<std::vector<P3>> holes;  // teshiklar (OddEven)
        int group = -1;                      // bitta jism (prizma) — chuqurlik bo'yicha birga saralanadi
    };
    int groups_ = 0;
    struct Label {
        P3 at;
        QString text;
    };
    void cylinder(P3 a, P3 b, double r, int seg, const QColor& c, bool capA, bool capB, bool outline = false);
    void ring(P3 a, P3 b, double ro, double ri, int seg, const QColor& c);  // devor kesimi (halqa) a nuqtada, o'q a→b
    void fit();
    QPointF project(const P3& p, double* depth = nullptr) const;

    std::vector<Face> faces_;
    std::vector<Label> labels_;
    QString title_, subtitle_;
    double az_ = -35, el_ = 25, zoom_ = 1, fitScale_ = 1;
    QPointF pan_, fitMid_;
    P3 center_;
    QPoint last_;
    bool dark_ = true;
};

} // namespace asiacad::gui
