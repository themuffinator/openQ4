// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Interaction.h"
#include <memory>
namespace Rml { class Element; class ElementDocument; }
namespace openq4::ui {
// Derived layout only. Authored vector parts and canonical ancestry remain intact.
class ScrollbarView {
public:
    struct PointerResult {std::string control;std::optional<double> fraction;bool thumb=false,invalidProjection=false;};
    ScrollbarView();~ScrollbarView();
    void Reset();
    bool Initialize(const DocumentModel&,Rml::ElementDocument&,std::string& error);
    bool HasScrollbars() const noexcept;
    bool OwnsAxis(const Rml::Element* viewport,bool vertical) const noexcept;
    void PreserveDensity(double nextRatio);
    // deferRefresh requires the caller to publish matching bounds immediately.
    bool Sync(Interaction&,double ratio,bool freshLayout,std::string& error,bool deferRefresh = false);
    bool ApplyCommands(Interaction&);
    std::map<std::string,double> CaptureOffsets(const Interaction&) const;
    PointerResult PointerPart(Rml::Element* hit,float x,float y,const Interaction&) const;
    std::string WheelTarget(Rml::Element* hit,const Interaction&) const;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
