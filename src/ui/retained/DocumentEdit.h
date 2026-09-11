// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Document.h"
#include <cstdint>
#include <limits>
#include <span>

namespace openq4::ui {
struct DocumentEditIdentity {
    std::uint64_t document=0, revision=0;
    bool operator==(const DocumentEditIdentity&) const = default;
};
struct InsertDocumentNode { std::string parent; std::size_t index=0; std::string source; };
struct RemoveDocumentNode { std::string node; };
// Destination index is evaluated after removal. Root cannot be moved/removed.
struct MoveDocumentNode { std::string node,parent; std::size_t index=0; };
// Empty node selects the whole document; otherwise pointer is node-relative.
// Replaces an existing JSON value. Final document ID must remain unchanged.
struct ReplaceDocumentValue { std::string node,pointer,source; };
using DocumentEditOperation=std::variant<InsertDocumentNode,RemoveDocumentNode,MoveDocumentNode,ReplaceDocumentValue>;
struct DocumentEditLimits {
    std::size_t sourceBytes=16*1024*1024, historyBytes=64*1024*1024;
    std::size_t workingSourceBytes=128*1024*1024, states=128, steps=128;
    std::uint64_t revision=(std::numeric_limits<std::uint64_t>::max)();
};
struct DocumentEditReceipt {
    DocumentEditIdentity before,after;
    bool sourceChanged=false;
    std::size_t undo=0,redo=0,historySourceBytes=0;
};
// Serialized caller ownership; no host, renderer, filesystem or foreign callbacks.
// History shares immutable validated documents, not native handles or gestures.
// Returned false preserves identity/source/history and the receipt; diagnostics
// may change. Allocator/JsonCpp process-termination limitations are not hidden.
class DocumentEdit final {
public:
    class Prepared final {
    public:
        ~Prepared();
        Prepared(const Prepared&)=delete;
        Prepared& operator=(const Prepared&)=delete;
        const Document& Target() const noexcept;
        const DocumentEditReceipt& Receipt() const noexcept;
        bool OwnerCurrent() const noexcept;
    private:
        struct Data;
        explicit Prepared(std::shared_ptr<Data>);
        const Document& Origin() const noexcept;
        std::shared_ptr<Data> data;
        friend class DocumentEdit;
        friend class Runtime;
    };
    DocumentEdit();
    ~DocumentEdit();
    DocumentEdit(const DocumentEdit&)=delete;
    DocumentEdit& operator=(const DocumentEdit&)=delete;
    // Opens once. Use a new owner to replace a document/lifetime.
    bool Open(const std::string&,DocumentEditLimits,std::vector<Diagnostic>&) noexcept;
    DocumentEditIdentity Identity() const noexcept;
    // Borrowed until the next mutating call or destruction; never writable.
    const Document* Current() const noexcept;
    std::size_t UndoCount() const noexcept;
    std::size_t RedoCount() const noexcept;
    std::size_t HistorySourceBytes() const noexcept;
    // At most one retained preparation per owner. Its destruction is safe after
    // this owner dies. No callbacks, mutable target or independent authority.
    std::unique_ptr<Prepared> PrepareEdit(DocumentEditIdentity,std::span<const DocumentEditOperation>,std::vector<Diagnostic>&) noexcept;
    std::unique_ptr<Prepared> PrepareUndo(DocumentEditIdentity,bool redo,std::vector<Diagnostic>&) noexcept;
    bool CanPublish(const Prepared&) const noexcept;
    bool Publish(Prepared&,DocumentEditReceipt&) noexcept;
    bool Apply(DocumentEditIdentity,std::span<const DocumentEditOperation>,DocumentEditReceipt&,std::vector<Diagnostic>&) noexcept;
    bool Undo(DocumentEditIdentity,DocumentEditReceipt&,std::vector<Diagnostic>&) noexcept;
    bool Redo(DocumentEditIdentity,DocumentEditReceipt&,std::vector<Diagnostic>&) noexcept;
private:
    bool Travel(DocumentEditIdentity,bool,DocumentEditReceipt&,std::vector<Diagnostic>&) noexcept;
    void PublishPrepared(Prepared&,DocumentEditReceipt&) noexcept;
    struct Impl;
    std::shared_ptr<Impl> impl;
    friend class Runtime;
};
} // namespace openq4::ui
