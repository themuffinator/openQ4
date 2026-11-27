// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
// The interface variant (docs/dev/ui/interface-variants.md): one runtime choice
// between the desktop, touch and VR presentations of the retained interface,
// with the device class that sets its defaults (docs/dev/ui-visual-design.md
// sections 13.2 and 13.6). Engine-free: the platform layer observes the
// signals through SDL3, the engine adds the VR session's state and the
// player's override, and documents read the result through view sources.
#include <cstdint>
#include <string_view>

namespace openq4::ui {

enum class InterfaceVariant : std::uint8_t { Desktop, Touch, Vr };
// Section 13.2's viewing profiles, plus the headset VR presents in.
enum class InterfaceDeviceClass : std::uint8_t { Desk, Couch, Handheld, Phone, Tablet, Headset };
// The pointer the player used last: none yet, a mouse (or trackpad), or a finger.
enum class InterfacePointer : std::uint8_t { None, Mouse, Touch };
// ui_interfaceVariant: auto, or a forced variant. Forcing vr outside a VR
// session lays the documents out for VR; it starts no headset.
enum class InterfaceVariantRequest : std::uint8_t { Auto, Desktop, Touch, Vr };

struct InterfaceSignals {
	bool vrActive = false;     // the VR session presents (idVRSystem::IsActive)
	bool mobileBuild = false;  // an Android or iOS/iPadOS build
	bool sdlTablet = false;    // SDL_IsTablet
	bool sdlTV = false;        // SDL_IsTV
	bool steamDeck = false;    // the steamdeck platform profile or its host signal
	bool couch = false;        // the couch platform profile (a television)
	bool touchscreen = false;  // a direct touch device is present
	bool keyboard = false;     // a keyboard is attached, or was typed on
	bool mouse = false;        // a mouse is attached, or was moved
	bool pointerHeld = false;  // a finger or mouse button is down
	InterfacePointer pointer = InterfacePointer::None;
	// The window's shorter side in dp (physical pixels over the display
	// scale); 0 when unknown. 600 dp and above is a tablet, as on Android.
	float smallestWidthDp = 0;
	bool operator==(const InterfaceSignals&) const = default;
};

inline constexpr float TabletSmallestWidthDp = 600;
// Section 13.6: touch hit regions are at least 48 dp across.
inline constexpr float TouchTargetDp = 48;

inline const char* InterfaceVariantName(InterfaceVariant variant) {
	switch (variant) {
	case InterfaceVariant::Touch: return "touch";
	case InterfaceVariant::Vr: return "vr";
	default: return "desktop";
	}
}

inline const char* InterfaceDeviceClassName(InterfaceDeviceClass device) {
	switch (device) {
	case InterfaceDeviceClass::Couch: return "couch";
	case InterfaceDeviceClass::Handheld: return "handheld";
	case InterfaceDeviceClass::Phone: return "phone";
	case InterfaceDeviceClass::Tablet: return "tablet";
	case InterfaceDeviceClass::Headset: return "headset";
	default: return "desk";
	}
}

inline const char* InterfacePointerName(InterfacePointer pointer) {
	return pointer == InterfacePointer::Mouse ? "mouse" : pointer == InterfacePointer::Touch ? "touch" : "none";
}

// Case-insensitive; empty is auto. False for anything else.
inline bool ParseInterfaceVariantRequest(std::string_view text, InterfaceVariantRequest& out) {
	const auto is = [&](std::string_view name) {
		if (text.size() != name.size()) return false;
		for (std::size_t i = 0; i < name.size(); ++i) {
			char c = text[i];
			if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
			if (c != name[i]) return false;
		}
		return true;
	};
	if (text.empty() || is("auto")) out = InterfaceVariantRequest::Auto;
	else if (is("desktop")) out = InterfaceVariantRequest::Desktop;
	else if (is("touch")) out = InterfaceVariantRequest::Touch;
	else if (is("vr")) out = InterfaceVariantRequest::Vr;
	else return false;
	return true;
}

// The device the interface is on. A VR session wins; then the platform
// profiles; a phone or tablet build splits at 600 dp (or SDL's own tablet
// test); any other machine is a desk, touchscreen or not.
inline InterfaceDeviceClass ResolveInterfaceDeviceClass(const InterfaceSignals& signals) {
	if (signals.vrActive) return InterfaceDeviceClass::Headset;
	if (signals.couch || (signals.sdlTV && !signals.mobileBuild)) return InterfaceDeviceClass::Couch;
	if (signals.steamDeck) return InterfaceDeviceClass::Handheld;
	if (signals.mobileBuild || signals.sdlTablet) {
		if (signals.sdlTV) return InterfaceDeviceClass::Couch; // Android TV
		const bool tablet = signals.sdlTablet || signals.smallestWidthDp >= TabletSmallestWidthDp;
		return tablet ? InterfaceDeviceClass::Tablet : InterfaceDeviceClass::Phone;
	}
	return InterfaceDeviceClass::Desk;
}

// The variant: VR only while the VR session presents; otherwise the pointer
// used last (section 13.4's modality: a finger takes the touch presentation,
// a mouse the desktop one), and before either the device's own: touch on a
// phone, tablet or handheld, the desktop elsewhere.
inline InterfaceVariant ResolveInterfaceVariant(const InterfaceSignals& signals, InterfaceVariantRequest request) {
	switch (request) {
	case InterfaceVariantRequest::Desktop: return InterfaceVariant::Desktop;
	case InterfaceVariantRequest::Touch: return InterfaceVariant::Touch;
	case InterfaceVariantRequest::Vr: return InterfaceVariant::Vr;
	default: break;
	}
	if (signals.vrActive) return InterfaceVariant::Vr;
	if (signals.pointer == InterfacePointer::Touch) return InterfaceVariant::Touch;
	if (signals.pointer == InterfacePointer::Mouse) return InterfaceVariant::Desktop;
	switch (ResolveInterfaceDeviceClass(signals)) {
	case InterfaceDeviceClass::Phone: case InterfaceDeviceClass::Tablet: case InterfaceDeviceClass::Handheld:
		return InterfaceVariant::Touch;
	default:
		return InterfaceVariant::Desktop;
	}
}

// Holds the applied variant. A change waits while a finger or mouse button
// is down, so a layout never moves under a press: a desktop touchscreen's
// first tap completes on the layout it landed on, and the touch layout
// follows. VR starting or stopping, a forced change and the device class
// apply at once.
class InterfaceVariantTracker {
public:
	// True on the first update and whenever the applied variant or device
	// class changed.
	bool Update(const InterfaceSignals& signals, InterfaceVariantRequest request) {
		const auto wanted = ResolveInterfaceVariant(signals, request);
		const auto device = ResolveInterfaceDeviceClass(signals);
		const bool immediate = !initialized || request != appliedRequest || signals.vrActive != appliedVr ||
			wanted == InterfaceVariant::Vr || variant == InterfaceVariant::Vr;
		bool changed = !initialized || device != deviceClass;
		deviceClass = device;
		if (wanted != variant && (immediate || !signals.pointerHeld)) { variant = wanted; changed = true; }
		initialized = true; appliedRequest = request; appliedVr = signals.vrActive;
		return changed;
	}
	InterfaceVariant Variant() const { return variant; }
	InterfaceDeviceClass DeviceClass() const { return deviceClass; }
	bool Initialized() const { return initialized; }

private:
	InterfaceVariant variant = InterfaceVariant::Desktop;
	InterfaceDeviceClass deviceClass = InterfaceDeviceClass::Desk;
	InterfaceVariantRequest appliedRequest = InterfaceVariantRequest::Auto;
	bool appliedVr = false, initialized = false;
};

} // namespace openq4::ui
