#pragma once
// Menyu ichki tarixi (Ctrl+Z / Ctrl+Y). Har menyu o'z modeli holatlarini (yo'l parametrlari, tarmoq, shahar ...) saqlaydi;
// menyu chizmaga yozgan amallar ham shu navbatda turadi — Ctrl+Z menyuda oxirgi amalni qaytaradi (model yoki chizma).
// Holat "o'zgarishdan keyin" yoziladi (kechiktirib): spinbox tez-tez o'zgarsa — bitta qadam.
#include <QObject>
#include <QTimer>

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace asiacad::gui {

class MenuUndo {
public:
    virtual ~MenuUndo() = default;
    enum Kind { None = 0, Model = 1, Drawing = 2 };
    struct Step {
        Kind kind = None;
        const void* cmd = nullptr;   // Drawing: chizma tarixida tepada bo'lishi kerak bo'lgan buyruq
    };
    virtual Step undoStep() = 0;      // Model — menyu o'zi tikladi; Drawing — MainWindow chizmada UNDO bajaradi
    virtual Step redoStep() = 0;
    virtual void noteDrawing(const void* cmd) = 0;   // menyu amali chizmaga yozildi (bitta undo qadami)
    virtual void dropRedoTop() = 0;   // chizma qadami qo'llanmadi (chizmada boshqa amal tepada) — tashlanadi
    virtual void dropUndoTop() = 0;
    virtual void flush() = 0;         // kutilayotgan o'zgarish darhol yoziladi
    virtual std::size_t undoCount() const = 0;
    virtual std::size_t redoCount() const = 0;
};

template <class S>
class MenuHistory final : public MenuUndo {
public:
    using Grab = std::function<S()>;
    using Key = std::function<std::string(const S&)>;
    using Apply = std::function<void(const S&)>;

    MenuHistory(QObject* owner, Grab grab, Key key, Apply apply, std::size_t limit = 80)
        : owner_(owner), grab_(std::move(grab)), key_(std::move(key)), apply_(std::move(apply)), limit_(limit) {
        timer_ = new QTimer(owner);
        timer_->setSingleShot(true);
        timer_->setInterval(350);
        QObject::connect(timer_, &QTimer::timeout, owner, [this] { flush(); });
    }

    // model o'zgardi — biroz kutib (bir nechta tez o'zgarish bitta qadam) holat yoziladi
    void note() {
        if (busy_) return;
        if (!has_) {   // birinchi holat — boshlang'ich (tarix shundan boshlanadi)
            sync();
            return;
        }
        timer_->start();
    }
    // tarix tozalanadi, joriy holat — boshlang'ich
    void reset() {
        timer_->stop();
        undo_.clear();
        redo_.clear();
        sync();
    }
    bool busy() const { return busy_; }

    void flush() override {
        timer_->stop();
        if (busy_ || !has_) return;
        S s = grab_();
        std::string k = key_(s);
        if (k == curKey_) return;
        E e;
        e.before = std::move(cur_);
        e.beforeKey = std::move(curKey_);
        e.after = s;
        e.afterKey = k;
        push(undo_, std::move(e));
        redo_.clear();
        cur_ = std::move(s);
        curKey_ = std::move(k);
    }
    void noteDrawing(const void* cmd) override {
        flush();
        if (!cmd) return;
        E e;
        e.drawing = true;
        e.cmd = cmd;
        push(undo_, std::move(e));
        redo_.clear();
    }
    Step undoStep() override {
        flush();
        if (undo_.empty()) return {};
        E e = std::move(undo_.back());
        undo_.pop_back();
        Step st{e.drawing ? Drawing : Model, e.cmd};
        if (!e.drawing) restore(e.before);
        redo_.push_back(std::move(e));
        return st;
    }
    Step redoStep() override {
        flush();
        if (redo_.empty()) return {};
        E e = std::move(redo_.back());
        redo_.pop_back();
        Step st{e.drawing ? Drawing : Model, e.cmd};
        if (!e.drawing) restore(e.after);
        push(undo_, std::move(e));
        return st;
    }
    void dropRedoTop() override { if (!redo_.empty()) redo_.pop_back(); }
    void dropUndoTop() override { if (!undo_.empty()) undo_.pop_back(); }
    std::size_t undoCount() const override { return undo_.size(); }
    std::size_t redoCount() const override { return redo_.size(); }

private:
    struct E {
        bool drawing = false;
        const void* cmd = nullptr;
        S before, after;
        std::string beforeKey, afterKey;
    };
    void push(std::deque<E>& q, E e) {
        q.push_back(std::move(e));
        while (q.size() > limit_) q.pop_front();
    }
    void sync() {
        cur_ = grab_();
        curKey_ = key_(cur_);
        has_ = true;
    }
    void restore(const S& s) {
        timer_->stop();
        busy_ = true;
        apply_(s);
        busy_ = false;
        sync();   // tiklashda hisoblangan qiymatlar (natijalar) — joriy holat shu
        // tiklashdan keyin kechikib bajariladigan hisob ham yangi qadam bo'lmasin
        QTimer::singleShot(0, owner_, [this] { if (!timer_->isActive() && !busy_) sync(); });
    }

    QObject* owner_;
    Grab grab_;
    Key key_;
    Apply apply_;
    std::size_t limit_;
    QTimer* timer_ = nullptr;
    std::deque<E> undo_, redo_;
    S cur_{};
    std::string curKey_;
    bool has_ = false, busy_ = false;
};

} // namespace asiacad::gui
