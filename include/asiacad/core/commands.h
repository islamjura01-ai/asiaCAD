#pragma once
// AsiaCAD — Command Pattern: barcha hujjat o'zgarishlari shu orqali o'tadi (Undo/Redo)
#include <asiacad/core/document.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace asiacad {

class ICommand {
public:
    virtual ~ICommand() = default;
    // Birinchi bajarish ham, redo ham shu metod orqali. Xato bo'lsa hujjatni o'zgartirmasdan throw qiladi.
    virtual void execute(Document& doc) = 0;
    virtual void undo(Document& doc) = 0;
    virtual std::string name() const = 0;
};

// Listlar to'plamini almashtirish (yangi, o'chirish, shtamp, ko'rinish — barchasi bitta undo qadami)
class SheetsCommand final : public ICommand {
public:
    SheetsCommand(std::vector<Sheet> after, std::string name) : after_(std::move(after)), name_(std::move(name)) {}
    void execute(Document& doc) override { before_ = doc.sheets(); doc.setSheets(after_); }
    void undo(Document& doc) override { doc.setSheets(before_); }
    std::string name() const override { return name_; }

private:
    std::vector<Sheet> after_, before_;
    std::string name_;
};

// Yangi entity qo'shish
class AddEntityCommand final : public ICommand {
public:
    explicit AddEntityCommand(std::unique_ptr<IEntity> e, std::string name = "ADD")
        : pending_(std::move(e)), name_(std::move(name)) {}
    void execute(Document& doc) override;
    void undo(Document& doc) override;
    std::string name() const override { return name_; }
    EntityId entityId() const { return id_; }

private:
    std::unique_ptr<IEntity> pending_;  // hujjatda bo'lmagan paytdagi egalik
    std::string name_;
    EntityId id_ = kInvalidId;
    std::size_t index_ = 0;
};

class DeleteCommand final : public ICommand {
public:
    explicit DeleteCommand(std::vector<EntityId> ids) : ids_(std::move(ids)) {}
    void execute(Document& doc) override;
    void undo(Document& doc) override;
    std::string name() const override { return "ERASE"; }

private:
    std::vector<EntityId> ids_;
    std::vector<std::pair<std::unique_ptr<IEntity>, std::size_t>> removed_;
};

// MOVE / ROTATE / SCALE / MIRROR — barchasi Transform orqali
class TransformCommand final : public ICommand {
public:
    TransformCommand(std::vector<EntityId> ids, Transform t, std::string name)
        : ids_(std::move(ids)), t_(t), name_(std::move(name)) {}
    void execute(Document& doc) override;
    void undo(Document& doc) override;
    std::string name() const override { return name_; }

private:
    std::vector<EntityId> ids_;
    Transform t_;
    std::string name_;
    std::vector<std::unique_ptr<IEntity>> snapshots_;
};

// COPY (va kelajakda ARRAY): nusxalarga transform qo'llab qo'shadi
class CopyCommand final : public ICommand {
public:
    CopyCommand(std::vector<EntityId> ids, std::vector<Transform> placements)
        : ids_(std::move(ids)), placements_(std::move(placements)) {}
    void execute(Document& doc) override;
    void undo(Document& doc) override;
    std::string name() const override { return "COPY"; }
    const std::vector<EntityId>& createdIds() const { return created_; }

private:
    std::vector<EntityId> ids_;
    std::vector<Transform> placements_;
    std::vector<EntityId> created_;
    std::vector<std::pair<std::unique_ptr<IEntity>, std::size_t>> parked_;  // undo'dan keyin
};

// Qatlam yaratish yoki xossalarini o'zgartirish
class SetLayerCommand final : public ICommand {
public:
    explicit SetLayerCommand(Layer l) : new_(std::move(l)) {}
    void execute(Document& doc) override;
    void undo(Document& doc) override;
    std::string name() const override { return "LAYER"; }

private:
    Layer new_;
    std::optional<Layer> old_;
};

// Entity'larni boshqa qatlamga o'tkazish / rangini o'zgartirish
class ChangePropsCommand final : public ICommand {
public:
    ChangePropsCommand(std::vector<EntityId> ids, std::optional<std::string> layer, std::optional<int> color)
        : ids_(std::move(ids)), layer_(std::move(layer)), color_(color) {}
    void execute(Document& doc) override;
    void undo(Document& doc) override;
    std::string name() const override { return "CHPROP"; }

private:
    std::vector<EntityId> ids_;
    std::optional<std::string> layer_;
    std::optional<int> color_;
    std::vector<std::pair<std::string, int>> old_;
};

// Blok ta'rifini qo'shish (undo — olib tashlash)
class AddBlockCommand final : public ICommand {
public:
    explicit AddBlockCommand(BlockPtr b) : b_(std::move(b)) {}
    void execute(Document& doc) override;
    void undo(Document& doc) override { doc.removeBlock(b_->name); }
    std::string name() const override { return "BLOCK"; }

private:
    BlockPtr b_;
};

// Bir nechta buyruqni bitta undo qadami sifatida
class CompositeCommand final : public ICommand {
public:
    explicit CompositeCommand(std::string name) : name_(std::move(name)) {}
    void add(std::unique_ptr<ICommand> c) { children_.push_back(std::move(c)); }
    void execute(Document& doc) override;
    void undo(Document& doc) override;
    std::string name() const override { return name_; }
    bool empty() const { return children_.empty(); }

private:
    std::string name_;
    std::vector<std::unique_ptr<ICommand>> children_;
};

class CommandStack {
public:
    explicit CommandStack(Document& doc, std::size_t limit = 1000) : doc_(doc), limit_(limit) {}

    // Bajaradi va undo tarixiga qo'shadi. Xato bo'lsa throw, tarix o'zgarmaydi.
    ICommand& execute(std::unique_ptr<ICommand> cmd);
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    std::string undoName() const { return canUndo() ? undo_.back()->name() : std::string{}; }
    std::string redoName() const { return canRedo() ? redo_.back()->name() : std::string{}; }
    std::size_t undoCount() const { return undo_.size(); }

    // Saqlangandan keyin o'zgarish bo'lganmi
    bool isDirty() const { return undo_.size() != cleanIndex_ || cleanLost_; }
    void markClean() { cleanIndex_ = undo_.size(); cleanLost_ = false; }
    // Yangi hujjat ochilganda: tarix tozalanadi, holat "saqlangan"
    void clear() {
        undo_.clear();
        redo_.clear();
        cleanIndex_ = 0;
        cleanLost_ = false;
    }

private:
    Document& doc_;
    std::size_t limit_;
    std::deque<std::unique_ptr<ICommand>> undo_;
    std::vector<std::unique_ptr<ICommand>> redo_;
    std::size_t cleanIndex_ = 0;
    bool cleanLost_ = false;
};

} // namespace asiacad
