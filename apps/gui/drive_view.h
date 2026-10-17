#pragma once
// AsiaCAD GUI — birinchi shaxs ko'rinishi (perspektiva) uchun dasturiy rasterlovchi: z-bufer (to'g'ri ko'rinish — bino ichida
// ham), yaqin tekislik bo'yicha qirqish, quyosh yoritilishi, tuman, osmon. OpenGL talab qilmaydi (offscreen/skrinshotda ham ishlaydi).
#include <asiacad/civil/drive.h>

#include <QImage>
#include <QWidget>

#include <functional>

namespace asiacad::gui {

class DriveView : public QWidget {
    Q_OBJECT
public:
    explicit DriveView(QWidget* parent = nullptr);
    void setScene(const civil::DriveScene* s) { scene_ = s; update(); }
    // Kamera: ko'z va qarash yo'nalishi (yaw/pitch qo'shimcha burilish, gradus)
    void setCamera(const Vec3& eye, const Vec3& target);
    void setFov(double deg) { fov_ = deg; update(); }
    void setLook(double yawDeg, double pitchDeg) { yaw_ = yawDeg; pitch_ = pitchDeg; update(); }
    double yaw() const { return yaw_; }
    double pitch() const { return pitch_; }
    void setRenderScale(double k) { scale_ = k; }
    void setDrawDistance(double d) { far_ = d; }
    void setOverlay(std::function<void(QPainter&)> f) { overlay_ = std::move(f); }
    // So'nggi kadr statistikasi (sinov uchun)
    int drawnTriangles() const { return drawn_; }
    QImage frame() const { return img_; }
    void renderNow();

signals:
    void lookChanged();
    void keyPressed(int key);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void render(int W, int H);
    const civil::DriveScene* scene_ = nullptr;
    Vec3 eye_{0, 0, 2}, target_{10, 0, 2};
    double fov_ = 70, yaw_ = 0, pitch_ = 0, scale_ = 0.75, far_ = 2500;
    QImage img_;
    std::vector<float> depth_;
    int drawn_ = 0;
    QPoint last_;
    std::function<void(QPainter&)> overlay_;
};

} // namespace asiacad::gui
