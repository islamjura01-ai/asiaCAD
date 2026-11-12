#pragma once
// AsiaCAD GUI — menyular uchun umumiy "Xossalar" paneli (Civil 3D Properties kabi): 3D da tanlangan element maydonlari,
// tuzatish (qiymat kiritilib Enter / boshqa joy bosilsa — darhol qo'llanadi) va qo'shimcha detallar tugmalari
// (teshik, quduq, nusxa, armatura zonasi …). Panel elementni bilmaydi: menyu begin()/number()/action() bilan to'ldiradi,
// har maydonning qo'llash funksiyasi — menyuniki (chizma obyekti buyruq bilan yoki menyu modeli o'zgaradi).
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <map>

class QDoubleSpinBox;
class QFormLayout;
class QSpinBox;
class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace asiacad::gui {

class PropPanel : public QWidget {
    Q_OBJECT
public:
    struct In {   // amal oldidagi kichik maydon (masalan teshik o'lchami): nomi va boshlang'ich qiymati
        QString label;
        double value = 0;
        int dec = 2;
    };

    explicit PropPanel(QWidget* parent = nullptr);

    void begin(const QString& title, const QString& subtitle = {});
    void section(const QString& title);
    void info(const QString& label, const QString& value, const QColor& color = {});
    void number(const QString& key, const QString& label, double value, int dec, std::function<void(double)> apply, const QString& unit = {});
    void text(const QString& key, const QString& label, const QString& value, std::function<void(const QString&)> apply);
    void choice(const QString& key, const QString& label, const QStringList& items, int current, std::function<void(int)> apply);
    void action(const QString& key, const QString& label, std::function<void()> fn, bool danger = false);
    void action(const QString& key, const QString& label, const QList<In>& inputs, std::function<void(const QList<double>&)> fn);
    // menyu boshqaruvi bilan bog'langan maydon: qiymat spinboxga yoziladi — menyu odatdagidek qayta hisoblaydi
    void bind(const QString& key, const QString& label, QDoubleSpinBox* sb, const QString& unit = {});
    void bind(const QString& key, const QString& label, QSpinBox* sb, const QString& unit = {});
    void note(const QString& text);
    void end();
    void empty(const QString& hint);   // hech narsa tanlanmagan

    // sinov va avtomatlashtirish: maydonga qiymat (qo'llanadi), amalni bajarish (kirish qiymatlari bilan)
    bool edit(const QString& key, const QString& value);
    bool trigger(const QString& key, const QList<double>& inputs = {});
    bool has(const QString& key) const;
    QString value(const QString& key) const;
    QString title() const { return title_; }
    QStringList keys() const;

signals:
    void applied();   // qiymat qo'llandi yoki amal bajarildi

private:
    void row(const QString& label, QWidget* w);
    QWidget* body_ = nullptr;
    QVBoxLayout* lay_ = nullptr;
    QFormLayout* form_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QString title_;
    std::map<QString, std::function<void(const QString&)>> setters_;
    std::map<QString, std::function<void(const QList<double>&)>> actions_;
    std::map<QString, std::function<QString()>> getters_;
    std::map<QString, QList<double>> defaults_;   // amal kirish maydonlarining boshlang'ich qiymatlari
};

}  // namespace asiacad::gui
