#include <asiacad/core/commands.h>

#include <asiacad/core/error.h>

#include <stdexcept>

namespace asiacad {

namespace {
void requireEditable(const Document& doc, const std::vector<EntityId>& ids) {
    if (ids.empty()) throw Error("core.no_selection");
    for (EntityId id : ids) {
        if (!doc.contains(id)) throw Error("core.not_found", {std::to_string(id)});
        if (!doc.isEditable(id)) throw Error("core.locked", {std::to_string(id)});
    }
}
} // namespace

// ---------------- Add ----------------
void AddEntityCommand::execute(Document& doc) {
    if (!pending_) throw std::logic_error("AddEntityCommand: entity yo'q");
    if (id_ == kInvalidId) id_ = doc.add(std::move(pending_));
    else doc.restore(std::move(pending_), index_);
}
void AddEntityCommand::undo(Document& doc) { pending_ = doc.take(id_, &index_); }

// ---------------- Delete ----------------
void DeleteCommand::execute(Document& doc) {
    requireEditable(doc, ids_);
    removed_.clear();
    for (EntityId id : ids_) {
        std::size_t idx = 0;
        auto e = doc.take(id, &idx);
        removed_.emplace_back(std::move(e), idx);
    }
}
void DeleteCommand::undo(Document& doc) {
    for (auto it = removed_.rbegin(); it != removed_.rend(); ++it) doc.restore(std::move(it->first), it->second);
    removed_.clear();
}

// ---------------- Transform ----------------
void TransformCommand::execute(Document& doc) {
    requireEditable(doc, ids_);
    snapshots_.clear();
    for (EntityId id : ids_) snapshots_.push_back(doc.find(id)->clone());
    for (EntityId id : ids_) {
        doc.find(id)->transform(t_);
        doc.noteChanged(id);
    }
}
void TransformCommand::undo(Document& doc) {
    for (auto& snap : snapshots_) {
        std::size_t idx = 0;
        doc.take(snap->id(), &idx);
        doc.restore(std::move(snap), idx);
    }
    snapshots_.clear();
}

// ---------------- Copy ----------------
void CopyCommand::execute(Document& doc) {
    if (!parked_.empty()) {  // redo
        for (auto it = parked_.rbegin(); it != parked_.rend(); ++it) doc.restore(std::move(it->first), it->second);
        parked_.clear();
        return;
    }
    for (EntityId id : ids_)
        if (!doc.contains(id)) throw Error("core.not_found", {std::to_string(id)});
    if (placements_.empty()) throw Error("core.copy_no_placement");
    created_.clear();
    for (const auto& t : placements_)
        for (EntityId id : ids_) {
            auto c = doc.find(id)->clone();
            c->transform(t);
            created_.push_back(doc.add(std::move(c)));
        }
}
void CopyCommand::undo(Document& doc) {
    parked_.clear();
    for (auto it = created_.rbegin(); it != created_.rend(); ++it) {
        std::size_t idx = 0;
        auto e = doc.take(*it, &idx);
        parked_.emplace_back(std::move(e), idx);
    }
}

// ---------------- Layer ----------------
void SetLayerCommand::execute(Document& doc) {
    if (new_.name.empty()) throw Error("core.layer_empty_name");
    old_ = doc.layer(new_.name);
    if (old_) doc.setLayer(new_);
    else doc.addLayer(new_);
}
void SetLayerCommand::undo(Document& doc) {
    if (old_) doc.setLayer(*old_);
    else doc.removeLayer(new_.name);
}

// ---------------- ChangeProps ----------------
void ChangePropsCommand::execute(Document& doc) {
    requireEditable(doc, ids_);
    if (layer_ && !doc.layer(*layer_)) throw Error("core.layer_missing", {*layer_});
    old_.clear();
    for (EntityId id : ids_) {
        IEntity* e = doc.find(id);
        old_.emplace_back(e->layer(), e->color());
        if (layer_) e->setLayer(*layer_);
        if (color_) e->setColor(*color_);
    }
}
void ChangePropsCommand::undo(Document& doc) {
    for (std::size_t i = 0; i < ids_.size(); ++i) {
        IEntity* e = doc.find(ids_[i]);
        e->setLayer(old_[i].first);
        e->setColor(old_[i].second);
    }
}

// ---------------- Block ----------------
void AddBlockCommand::execute(Document& doc) {
    if (!doc.addBlock(b_)) throw Error("err.block_exists", {b_->name});
}

// ---------------- Composite ----------------
void CompositeCommand::execute(Document& doc) {
    std::size_t done = 0;
    try {
        for (; done < children_.size(); ++done) children_[done]->execute(doc);
    } catch (...) {
        while (done > 0) children_[--done]->undo(doc);  // atomarlik
        throw;
    }
}
void CompositeCommand::undo(Document& doc) {
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) (*it)->undo(doc);
}

// ---------------- Stack ----------------
ICommand& CommandStack::execute(std::unique_ptr<ICommand> cmd) {
    cmd->execute(doc_);
    ++pushes_;
    if (cleanIndex_ > undo_.size()) cleanLost_ = true;  // toza holat redo shoxida edi
    redo_.clear();
    undo_.push_back(std::move(cmd));
    if (undo_.size() > limit_) {
        undo_.pop_front();
        if (cleanIndex_ == 0) cleanLost_ = true;
        else --cleanIndex_;
    }
    return *undo_.back();
}
void CommandStack::squash(std::size_t n) {
    if (n < 2 || n > undo_.size()) return;
    const std::size_t start = undo_.size() - n, oldSize = undo_.size();
    auto comp = std::make_unique<CompositeCommand>(undo_[start]->name());
    for (std::size_t i = start; i < oldSize; ++i) comp->add(std::move(undo_[i]));
    undo_.resize(start);
    undo_.push_back(std::move(comp));
    if (cleanIndex_ > start && cleanIndex_ <= oldSize) {
        if (cleanIndex_ == oldSize) cleanIndex_ = undo_.size(); else cleanLost_ = true;
    }
}

bool CommandStack::undo() {
    if (undo_.empty()) return false;
    auto cmd = std::move(undo_.back());
    undo_.pop_back();
    cmd->undo(doc_);
    redo_.push_back(std::move(cmd));
    return true;
}
bool CommandStack::redo() {
    if (redo_.empty()) return false;
    auto cmd = std::move(redo_.back());
    redo_.pop_back();
    cmd->execute(doc_);
    undo_.push_back(std::move(cmd));
    return true;
}

} // namespace asiacad
