#pragma once
// AsiaCAD GUI — jadvalni ko'p qator bilan to'ldirish: ResizeToContents har insertRow'da barcha qatorlarni qayta o'lchaydi
// (O(n²): 600 quvurli tarmoqda daqiqalab). To'ldirish paytida Interactive, oxirida bir marta asl rejim.
#include <QHeaderView>
#include <QTableWidget>

#include <vector>

namespace asiacad::gui {

class BulkFill {
public:
    explicit BulkFill(QTableWidget* t) : t_(t) {
        auto* h = t_->horizontalHeader();
        for (int c = 0; c < h->count(); ++c) {
            modes_.push_back(h->sectionResizeMode(c));
            h->setSectionResizeMode(c, QHeaderView::Interactive);
        }
        t_->setUpdatesEnabled(false);
    }
    ~BulkFill() {
        auto* h = t_->horizontalHeader();
        for (int c = 0; c < static_cast<int>(modes_.size()) && c < h->count(); ++c) h->setSectionResizeMode(c, modes_[static_cast<std::size_t>(c)]);
        t_->setUpdatesEnabled(true);
    }
    BulkFill(const BulkFill&) = delete;
    BulkFill& operator=(const BulkFill&) = delete;

private:
    QTableWidget* t_;
    std::vector<QHeaderView::ResizeMode> modes_;
};

} // namespace asiacad::gui
