// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cstdint>

namespace openq4 {

// Which connected controller is in charge (PAD-01). The platform layer opens
// every gamepad and joystick and reports what happens to them here; this
// decides the active one and says when a handover or a loss took place. It
// holds no device handles and reads no clock or setting, so the same policy
// drives real pads, virtual test pads and, later, MP-25's seats.
//
// Rules:
//  - Automatic: the active controller is the last one used, where "used" is a
//    button press or a stick or trigger moved past its dead zone. A gamepad
//    that connects takes over from a plain joystick, as it always has, but
//    never from another gamepad.
//  - Chosen device: while the saved device is connected only it can be
//    active. While it is away the rules above apply, and it takes charge
//    again when it returns.
//  - When the active controller goes away, another connected one takes over
//    (the chosen device, else a gamepad, else a joystick). With none left the
//    roster reports a loss.

constexpr int CONTROLLER_ROSTER_MAX = 16;
constexpr int CONTROLLER_IDENTITY_CHARS = 96;

enum class ControllerChange {
	None,		// the active controller is the one it was
	Activated,	// a controller took charge where none was active
	HandedOver,	// charge moved from one connected controller to another
	Lost		// the active controller went away and none took over
};

struct ControllerEntry {
	std::uint32_t id = 0;		// the platform's instance id; never 0 for a connected device
	bool gamepad = false;		// a mapped gamepad, not a plain joystick
	char identity[CONTROLLER_IDENTITY_CHARS] = {};	// stable across reconnects: GUID, and serial where reported
};

class ControllerRoster {
public:
	int Count() const { return count; }
	const ControllerEntry &Entry(int index) const { return entries[index]; }
	std::uint32_t Active() const { return active; }
	bool Has(std::uint32_t id) const { return Find(id) >= 0; }
	const char *Preference() const { return preference; }

	const ControllerEntry *ActiveEntry() const {
		const int index = Find(active);
		return index >= 0 ? &entries[index] : nullptr;
	}

	// Whether the saved device is connected, so use cannot move charge elsewhere.
	bool PreferenceConnected() const { return FindPreferred() >= 0; }

	// The saved device choice; empty is Automatic. Charge moves to the device
	// at once when it is connected.
	ControllerChange SetPreference(const char *identity) {
		Copy(preference, identity);
		const int preferred = FindPreferred();
		if (preferred >= 0) {
			return Activate(entries[preferred].id);
		}
		return ControllerChange::None;
	}

	// A device connected and was opened. False when the roster is full or the
	// id is already present; the caller then leaves the device closed.
	bool Add(std::uint32_t id, bool gamepad, const char *identity, ControllerChange &change) {
		change = ControllerChange::None;
		if (id == 0 || count >= CONTROLLER_ROSTER_MAX || Find(id) >= 0) {
			return false;
		}
		ControllerEntry &entry = entries[count++];
		entry.id = id;
		entry.gamepad = gamepad;
		Copy(entry.identity, identity);

		const int current = Find(active);
		if (current < 0) {
			change = Activate(id);
		} else if (Matches(entry)) {
			change = Activate(id);
		} else if (gamepad && !entries[current].gamepad && !Matches(entries[current])) {
			change = Activate(id);
		}
		return true;
	}

	// A device went away.
	ControllerChange Remove(std::uint32_t id) {
		const int index = Find(id);
		if (index < 0) {
			return ControllerChange::None;
		}
		for (int i = index; i + 1 < count; ++i) {
			entries[i] = entries[i + 1];
		}
		entries[--count] = ControllerEntry();
		if (id != active) {
			return ControllerChange::None;
		}
		active = 0;
		int successor = FindPreferred();
		for (int i = 0; successor < 0 && i < count; ++i) {
			if (entries[i].gamepad) {
				successor = i;
			}
		}
		if (successor < 0 && count > 0) {
			successor = 0;
		}
		if (successor < 0) {
			return ControllerChange::Lost;
		}
		active = entries[successor].id;
		return ControllerChange::HandedOver;
	}

	// A connected device was used: a button went down, or a stick or trigger
	// passed its dead zone.
	ControllerChange NoteUse(std::uint32_t id) {
		const int index = Find(id);
		if (index < 0 || id == active) {
			return ControllerChange::None;
		}
		const int preferred = FindPreferred();
		if (preferred >= 0 && preferred != index) {
			return ControllerChange::None;
		}
		return Activate(id);
	}

	void Clear() {
		for (int i = 0; i < count; ++i) {
			entries[i] = ControllerEntry();
		}
		count = 0;
		active = 0;
	}

private:
	static void Copy(char (&out)[CONTROLLER_IDENTITY_CHARS], const char *text) {
		// plain loops: the engine redefines the C string functions
		int length = 0;
		for (; text != nullptr && text[length] != '\0' && length < CONTROLLER_IDENTITY_CHARS - 1; ++length) {
			out[length] = text[length];
		}
		for (; length < CONTROLLER_IDENTITY_CHARS; ++length) {
			out[length] = '\0';
		}
	}

	bool Matches(const ControllerEntry &entry) const {
		if (preference[0] == '\0') {
			return false;
		}
		for (int i = 0; i < CONTROLLER_IDENTITY_CHARS; ++i) {
			if (preference[i] != entry.identity[i]) {
				return false;
			}
			if (preference[i] == '\0') {
				break;
			}
		}
		return true;
	}

	int Find(std::uint32_t id) const {
		for (int i = 0; id != 0 && i < count; ++i) {
			if (entries[i].id == id) {
				return i;
			}
		}
		return -1;
	}

	int FindPreferred() const {
		for (int i = 0; i < count; ++i) {
			if (Matches(entries[i])) {
				return i;
			}
		}
		return -1;
	}

	ControllerChange Activate(std::uint32_t id) {
		if (id == active) {
			return ControllerChange::None;
		}
		const bool had = Find(active) >= 0;
		active = id;
		return had ? ControllerChange::HandedOver : ControllerChange::Activated;
	}

	ControllerEntry entries[CONTROLLER_ROSTER_MAX];
	int count = 0;
	std::uint32_t active = 0;
	char preference[CONTROLLER_IDENTITY_CHARS] = {};
};

// The battery notices (PAD-01): one when the active controller's charge
// reaches the threshold on battery, one when it starts charging. Charge
// wandering around the threshold does not repeat the first; it shows again
// only after the controller has charged or another controller took over.
enum class ControllerPower { Unknown, OnBattery, Charging, Charged, Wired };

enum class ControllerBatteryNotice { None, Low, Charging };

struct ControllerBatteryState {
	std::uint32_t device = 0;
	bool lowShown = false;
	ControllerPower power = ControllerPower::Unknown;
};

inline ControllerBatteryNotice UpdateControllerBattery(ControllerBatteryState &state, std::uint32_t device,
	ControllerPower power, int percent, int lowPercent) {
	if (device != state.device) {
		// another controller: start again, and say nothing about how it arrived
		state.device = device;
		state.lowShown = false;
		state.power = device != 0 ? power : ControllerPower::Unknown;
		if (device != 0 && power == ControllerPower::OnBattery && lowPercent > 0 && percent >= 0 && percent <= lowPercent) {
			state.lowShown = true;
			return ControllerBatteryNotice::Low;
		}
		return ControllerBatteryNotice::None;
	}
	if (device == 0) {
		return ControllerBatteryNotice::None;
	}
	const ControllerPower previous = state.power;
	state.power = power;
	if (power == ControllerPower::Charging || power == ControllerPower::Charged || power == ControllerPower::Wired) {
		state.lowShown = false;
		if (power == ControllerPower::Charging && previous == ControllerPower::OnBattery) {
			return ControllerBatteryNotice::Charging;
		}
		return ControllerBatteryNotice::None;
	}
	if (power == ControllerPower::OnBattery && lowPercent > 0 && percent >= 0 && percent <= lowPercent && !state.lowShown) {
		state.lowShown = true;
		return ControllerBatteryNotice::Low;
	}
	return ControllerBatteryNotice::None;
}

} // namespace openq4
