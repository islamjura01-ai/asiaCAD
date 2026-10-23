#pragma once
// AsiaCAD GUI — listlar oynasi (Revit "Sheets"): qog'oz, GOST ramka va shtamp, modeldan masshtabli ko'rinishlar.
// G'ildirak — zoom, o'rta tugma — siljitish, ko'rinishni ikki marta bosish — chizmada ochiladi. PDF — QPdfWriter.
#include <asiacad/core/document.h>
#include <asiacad/i18n/localizer.h>

#include <QWidget>

namespace asiacad::gui {

class Viewport;

class SheetCanvas : public QWidget {
    Q_OBJECT
public:
    SheetCanvas(const Document& doc, const i18n::Localizer& loc, Viewport* renderer, QWidget* parent = nullptr);
    void setCurrent(int i);
    int current() const { return cur_; }
    void fit();
    // Bir listni chizish: o — qog'oz (0,0) ning qurilmadagi o'rni (chap past), k — piksel/mm
    static void paintSheet(QPainter& p, const Sheet& s, int number, int total, const QPointF& o, double k, Viewport* r,
                           const i18n::Localizer& loc, double penMul);
    bool exportPdf(const QString& path, const std::vector<int>& which, int dpi = 300) const;
    int viewAt(const QPointF& pos) const;  // kursor ostidagi ko'rinish (-1)

signals:
    void openView(int index);  // joriy listning ko'rinishi

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    const Sheet* sheet() const;
    QPointF origin() const;
    const Document& doc_;
    const i18n::Localizer& loc_;
    Viewport* r_;
    int cur_ = 0;
    double k_ = 2.0;        // piksel / mm
    QPointF pan_;           // qog'oz markazining vidjet markazidan siljishi
    bool panning_ = false, fitted_ = false;
    QPointF panStart_, panOrig_;
};

} // namespace asiacad::gui
