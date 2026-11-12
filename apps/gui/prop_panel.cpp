#include "prop_panel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPointer>
#include <QSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace asiacad::gui {

namespace {
QString num(double v, int dec) {
    if (std::abs(v) < 0.5 * std::pow(10.0, -dec)) v = 0;
    return QString::number(v, 'f', dec);
}
bool parseNum(QString s, double& v) {
    s = s.trimmed().replace(',', '.').replace(QChar(0x2212), '-');
    bool ok = false;
    v = s.toDouble(&ok);
    return ok && std::isfinite(v);
}
}  // namespace

PropPanel::PropPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("propPanel");
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    v->addWidget(scroll_);
    empty(QString());
}

void PropPanel::begin(const QString& title, const QString& subtitle) {
    setters_.clear();
    actions_.clear();
    getters_.clear();
    defaults_.clear();
    title_ = title;
    body_ = new QWidget;
    body_->setObjectName("propBody");
    lay_ = new QVBoxLayout(body_);
    lay_->setContentsMargins(8, 6, 8, 6);
    lay_->setSpacing(3);
    form_ = nullptr;
    auto* t = new QLabel(title, body_);
    t->setObjectName("title");
    t->setWordWrap(true);
    t->setStyleSheet(QStringLiteral("font-weight:700; font-size:14px;"));
    lay_->addWidget(t);
    if (!subtitle.isEmpty()) {
        auto* s = new QLabel(subtitle, body_);
        s->setObjectName("muted");
        s->setWordWrap(true);
        s->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lay_->addWidget(s);
    }
    scroll_->setWidget(body_);   // eskisi o'chiriladi
}

void PropPanel::section(const QString& title) {
    if (!lay_) begin(QString());
    auto* l = new QLabel(title, body_);
    l->setObjectName("propSection");
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("font-weight:700; padding-top:8px;"));
    lay_->addWidget(l);
    form_ = new QFormLayout;
    form_->setLabelAlignment(Qt::AlignLeft);
    form_->setHorizontalSpacing(8);
    form_->setVerticalSpacing(3);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    lay_->addLayout(form_);
}

void PropPanel::row(const QString& label, QWidget* w) {
    if (!form_) section(QString());
    auto* l = new QLabel(label, body_);
    l->setObjectName("muted");
    l->setWordWrap(true);
    form_->addRow(l, w);
}

void PropPanel::info(const QString& label, const QString& value, const QColor& color) {
    auto* v = new QLabel(value, body_);
    v->setWordWrap(true);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (color.isValid()) v->setStyleSheet(QStringLiteral("color:%1;").arg(color.name()));
    row(label, v);
}

void PropPanel::number(const QString& key, const QString& label, double value, int dec, std::function<void(double)> apply, const QString& unit) {
    auto* w = new QWidget(body_);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    auto* e = new QLineEdit(num(value, dec), w);
    e->setObjectName("p_" + key);
    e->setMinimumWidth(80);
    h->addWidget(e, 1);
    if (!unit.isEmpty()) {
        auto* u = new QLabel(unit, w);
        u->setObjectName("muted");
        h->addWidget(u);
    }
    row(label, w);
    const QString orig = e->text();
    auto set = [apply](const QString& s) {
        double v;
        if (parseNum(s, v)) apply(v);
    };
    setters_[key] = set;
    getters_[key] = [e] { return e->text(); };
    connect(e, &QLineEdit::editingFinished, this, [this, e, orig, set] {
        const QString s = e->text().trimmed();
        if (s == orig) return;
        double v;
        if (!parseNum(s, v)) {   // noto'g'ri qiymat — eskisi qaytadi
            e->setText(orig);
            return;
        }
        // panel qo'llashda qayta quriladi — signal manbasi (e) o'chmasligi uchun keyingi aylanishda
        QTimer::singleShot(0, this, [this, set, s] {
            set(s);
            emit applied();
        });
    });
}

void PropPanel::text(const QString& key, const QString& label, const QString& value, std::function<void(const QString&)> apply) {
    auto* e = new QLineEdit(value, body_);
    e->setObjectName("p_" + key);
    row(label, e);
    setters_[key] = apply;
    getters_[key] = [e] { return e->text(); };
    const QString orig = value;
    connect(e, &QLineEdit::editingFinished, this, [this, e, orig, apply] {
        const QString s = e->text().trimmed();
        if (s == orig) return;
        QTimer::singleShot(0, this, [this, apply, s] {
            apply(s);
            emit applied();
        });
    });
}

void PropPanel::choice(const QString& key, const QString& label, const QStringList& items, int current, std::function<void(int)> apply) {
    auto* c = new QComboBox(body_);
    c->setObjectName("p_" + key);
    c->addItems(items);
    c->setCurrentIndex(current);
    row(label, c);
    setters_[key] = [apply, items](const QString& s) {
        bool ok = false;
        const int i = s.toInt(&ok);
        if (ok && i >= 0 && i < items.size()) apply(i);
        else if (const int j = static_cast<int>(items.indexOf(s)); j >= 0) apply(j);
    };
    getters_[key] = [c] { return c->currentText(); };
    connect(c, &QComboBox::activated, this, [this, apply, current](int i) {
        if (i == current) return;
        QTimer::singleShot(0, this, [this, apply, i] {
            apply(i);
            emit applied();
        });
    });
}

void PropPanel::action(const QString& key, const QString& label, std::function<void()> fn, bool danger) {
    if (!form_) section(QString());
    auto* b = new QPushButton(label, body_);
    b->setObjectName("p_" + key);
    b->setCursor(Qt::PointingHandCursor);
    if (danger) b->setStyleSheet(QStringLiteral("QPushButton { color: #FF8A8F; }"));
    form_->addRow(b);
    actions_[key] = [fn](const QList<double>&) { fn(); };
    connect(b, &QPushButton::clicked, this, [this, fn] {
        QTimer::singleShot(0, this, [this, fn] {
            fn();
            emit applied();
        });
    });
}

void PropPanel::action(const QString& key, const QString& label, const QList<In>& inputs, std::function<void(const QList<double>&)> fn) {
    if (!form_) section(QString());
    auto* w = new QWidget(body_);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 4, 0, 2);
    v->setSpacing(2);
    auto* h = new QHBoxLayout;
    h->setSpacing(4);
    QList<QLineEdit*> eds;
    for (const auto& in : inputs) {
        auto* l = new QLabel(in.label, w);
        l->setObjectName("muted");
        auto* e = new QLineEdit(num(in.value, in.dec), w);
        e->setObjectName(QStringLiteral("p_%1_%2").arg(key).arg(eds.size()));
        e->setMinimumWidth(46);
        e->setToolTip(in.label);
        h->addWidget(l);
        h->addWidget(e, 1);
        eds << e;
    }
    auto* b = new QPushButton(label, w);
    b->setObjectName("p_" + key);
    b->setCursor(Qt::PointingHandCursor);
    v->addLayout(h);
    v->addWidget(b);
    form_->addRow(w);
    actions_[key] = fn;
    for (const auto& in : inputs) defaults_[key] << in.value;
    const QList<In> defs = inputs;
    connect(b, &QPushButton::clicked, this, [this, eds, defs, fn] {
        QList<double> vals;
        for (int i = 0; i < eds.size(); ++i) {
            double x;
            vals << (parseNum(eds[i]->text(), x) ? x : defs[i].value);
        }
        QTimer::singleShot(0, this, [this, fn, vals] {
            fn(vals);
            emit applied();
        });
    });
}

void PropPanel::bind(const QString& key, const QString& label, QDoubleSpinBox* sb, const QString& unit) {
    if (!sb) return;
    QPointer<QDoubleSpinBox> p(sb);
    number(key, label, sb->value(), sb->decimals(), [p](double v) { if (p) p->setValue(v); }, unit.isEmpty() ? sb->suffix().trimmed() : unit);
}

void PropPanel::bind(const QString& key, const QString& label, QSpinBox* sb, const QString& unit) {
    if (!sb) return;
    QPointer<QSpinBox> p(sb);
    number(key, label, sb->value(), 0, [p](double v) { if (p) p->setValue(static_cast<int>(std::lround(v))); }, unit.isEmpty() ? sb->suffix().trimmed() : unit);
}

void PropPanel::note(const QString& text) {
    if (!lay_) begin(QString());
    auto* l = new QLabel(text, body_);
    l->setObjectName("muted");
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:#6FA8DC; padding-top:4px;"));
    lay_->addWidget(l);
}

void PropPanel::end() {
    if (lay_) lay_->addStretch(1);
}

void PropPanel::empty(const QString& hint) {
    begin(QString());
    title_.clear();
    if (!hint.isEmpty()) {
        auto* l = new QLabel(hint, body_);
        l->setObjectName("panelHint");
        l->setWordWrap(true);
        l->setAlignment(Qt::AlignCenter);
        lay_->addStretch(1);
        lay_->addWidget(l);
    }
    end();
}

bool PropPanel::edit(const QString& key, const QString& value) {
    const auto it = setters_.find(key);
    if (it == setters_.end()) return false;
    const auto fn = it->second;   // qo'llashda panel qayta quriladi — nusxa
    fn(value);
    emit applied();
    return true;
}

bool PropPanel::trigger(const QString& key, const QList<double>& inputs) {
    const auto it = actions_.find(key);
    if (it == actions_.end()) return false;
    const auto fn = it->second;
    QList<double> in = inputs;   // berilmaganlari — boshlang'ich qiymatlar
    if (const auto d = defaults_.find(key); d != defaults_.end())
        for (int i = static_cast<int>(in.size()); i < d->second.size(); ++i) in << d->second[i];
    fn(in);
    emit applied();
    return true;
}

bool PropPanel::has(const QString& key) const { return setters_.count(key) || actions_.count(key); }

QString PropPanel::value(const QString& key) const {
    const auto it = getters_.find(key);
    return it == getters_.end() ? QString() : it->second();
}

QStringList PropPanel::keys() const {
    QStringList k;
    for (const auto& [n, f] : setters_) k << n;
    for (const auto& [n, f] : actions_) k << n;
    return k;
}

}  // namespace asiacad::gui
