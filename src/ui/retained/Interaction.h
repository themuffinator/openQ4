// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Document.h"

namespace openq4::ui {
enum class MenuInput { Next, Previous, Up, Down, Left, Right, Accept, Back };
struct ControlBounds { float x = 0, y = 0, width = 0, height = 0; bool visible = false; };
struct ControlAction {
	enum class Kind { Activate, Back } kind = Kind::Activate;
	std::string document, node, action, event;
};
struct ControlFeedback { std::string node, timeline; ControlState state = ControlState::Default; };
struct InteractionSnapshot {
	struct Modal { std::string root, restore; };
	std::string focus;
	std::vector<Modal> modals;
	std::map<std::string,bool> enabled;
	std::map<std::string,ControlState> presented;
};

// Pure semantic interaction shared by the game and editor. The layout adapter
// supplies current projected boxes and hit IDs; this class never reads a device.
class Interaction {
public:
	void Reset(const DocumentModel& model);
	void SetBounds(const std::map<std::string,ControlBounds>& bounds);
	bool SetEnabled(const std::string& id, bool enabled);
	bool Focus(const std::string& id);
	void Hover(const std::string& id);
	void Pointer(bool down);
	void Input(MenuInput input, bool down);
	// Cancel arms without forgetting held inputs. Their eventual releases must
	// not activate a different control after replacement, focus loss or a modal.
	void Cancel();
	// After Cancel/Restore and adapter source quarantine, release logical held
	// latches without changing focus, arms, feedback, actions or presentation.
	void ReleaseInputSources();
	bool PushModal(const std::string& root);
	bool PopModal();
	std::string Focused() const { return focused; }
	std::string Modal() const { return modals.empty() ? std::string{} : modals.back().root; }
	std::optional<ControlState> State(const std::string& id) const;
	std::vector<ControlFeedback> TakeFeedback();
	std::vector<ControlAction> TakeActions();
	bool CanActivate(const std::string& id) const { return Eligible(id); }
	bool Overflowed() const { return overflowed; }
	// Capture persistent semantics only. Restore cancels queued actions/arms and
	// hover, preserves receiving-instance held latches, and awaits fresh bounds.
	InteractionSnapshot Capture() const;
	bool Restore(const InteractionSnapshot& snapshot, std::string& error);
private:
	struct Item { Control control; ControlBounds bounds; ControlState state = ControlState::Default; bool known = false; };
	struct ModalScope { std::string root, restore; };
	bool Within(const std::string& id, const std::string& root) const;
	bool Eligible(const std::string& id) const;
	std::string First() const;
	void Navigate(MenuInput input);
	void Refresh();
	void Queue(ControlAction action);
	std::string document, focused, hovered, armed;
	bool pointerHeld = false, acceptHeld = false, backHeld = false, pointerArm = false, overflowed = false;
	std::map<std::string,Item> items;
	std::vector<std::string> order;
	std::map<std::string,std::string> parents;
	std::vector<ModalScope> modals;
	std::vector<ControlFeedback> feedback;
	std::vector<ControlAction> actions;
};
} // namespace openq4::ui
