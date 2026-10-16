#include "template_sidebar.h"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace asiacad::gui {

namespace {
constexpr int kRoleKind = Qt::UserRole + 1;  // 0 — bo'lim, 1 — shablon
constexpr int kRoleData = Qt::UserRole + 2;
QIcon swatchIcon(const QColor& c, bool user) {
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(2, 2, 14, 14), 4, 4);
    if (user) {
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(13, 5), 3, 3);
    }
    return QIcon(pm);
}
} // namespace

TemplateSidebar::TemplateSidebar(const QString& title, const QString& searchHint, const QString& applyText, const QString& hint,
                                 QWidget* parent)
    : QWidget(parent) {
    setObjectName("templateSidebar");
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    auto* t = new QLabel(title, this);
    t->setObjectName("title");
    v->addWidget(t);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(searchHint);
    search_->setClearButtonEnabled(true);
    v->addWidget(search_);
    list_ = new QListWidget(this);
    list_->setObjectName("templateList");
    list_->setIconSize(QSize(18, 18));
    list_->setWordWrap(true);
    list_->setSpacing(2);
    v->addWidget(list_, 1);
    auto* h = new QLabel(hint, this);
    h->setObjectName("muted");
    h->setWordWrap(true);
    v->addWidget(h);
    auto* apply = new QPushButton(applyText, this);
    apply->setObjectName("accent");
    v->addWidget(apply);
    setMinimumWidth(240);

    connect(search_, &QLineEdit::textChanged, this, &TemplateSidebar::filter);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (it && it->data(kRoleKind).toInt() == 1) emit templateActivated(it->data(kRoleData));
    });
    connect(apply, &QPushButton::clicked, this, [this] {
        auto* it = list_->currentItem();
        if (it && it->data(kRoleKind).toInt() == 1) emit templateActivated(it->data(kRoleData));
    });
}

void TemplateSidebar::clear() { list_->clear(); }

void TemplateSidebar::addSection(const QString& title) {
    auto* it = new QListWidgetItem(title.toUpper());
    it->setFlags(Qt::ItemIsEnabled);
    it->setData(kRoleKind, 0);
    QFont f = it->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 0.85);
    it->setFont(f);
    it->setForeground(QColor(150, 160, 190));
    list_->addItem(it);
}

void TemplateSidebar::addTemplate(const QString& name, const QString& subtitle, const QVariant& data, const QColor& accent, bool user) {
    auto* it = new QListWidgetItem(swatchIcon(accent, user), subtitle.isEmpty() ? name : name + "\n" + subtitle);
    it->setData(kRoleKind, 1);
    it->setData(kRoleData, data);
    it->setToolTip(name);
    list_->addItem(it);
}

int TemplateSidebar::count() const {
    int n = 0;
    for (int i = 0; i < list_->count(); ++i) n += list_->item(i)->data(kRoleKind).toInt() == 1;
    return n;
}

bool TemplateSidebar::activate(int index) {
    int n = 0;
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(kRoleKind).toInt() == 1 && n++ == index) {
            list_->setCurrentRow(i);
            emit templateActivated(list_->item(i)->data(kRoleData));
            return true;
        }
    return false;
}

QVariant TemplateSidebar::currentData() const {
    auto* it = list_->currentItem();
    return it ? it->data(kRoleData) : QVariant();
}

void TemplateSidebar::filter(const QString& text) {
    const QString q = text.trimmed();
    for (int i = 0; i < list_->count(); ++i) {
        auto* it = list_->item(i);
        if (it->data(kRoleKind).toInt() == 0) it->setHidden(!q.isEmpty());
        else it->setHidden(!q.isEmpty() && !it->text().contains(q, Qt::CaseInsensitive));
    }
}

} // namespace asiacad::gui
