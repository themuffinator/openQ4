// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Which controller is in charge (PAD-01): the production roster policy and the
// battery notices, exercised without SDL or the engine. Two pads connect, use
// moves charge between them, an inactive and an active pad go away, a saved
// device keeps charge and takes it back when it returns.
#include "src/sys/ControllerRoster.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); std::exit(1); } } while (0)

using openq4::ControllerChange;
using openq4::ControllerRoster;

// Uses far enough apart that the active controller has always rested.
static int s_clock = 0;
static ControllerChange Use(ControllerRoster &roster, std::uint32_t id) {
	s_clock += 10 * openq4::CONTROLLER_TWIN_REST_MSEC;
	return roster.NoteUse(id, s_clock, false);
}

static void ConnectAndUse() {
	ControllerRoster roster;
	ControllerChange change;
	CHECK(roster.Active() == 0 && roster.Count() == 0 && roster.ActiveEntry() == nullptr);

	// the first pad to connect takes charge
	CHECK(roster.Add(11, true, "guid-xbox", change) && change == ControllerChange::Activated);
	CHECK(roster.Active() == 11);
	// a second gamepad is opened and waits: connecting is not using
	CHECK(roster.Add(12, true, "guid-dualsense", change) && change == ControllerChange::None);
	CHECK(roster.Active() == 11 && roster.Count() == 2 && roster.Has(12));

	// using the active pad changes nothing
	CHECK(Use(roster, 11) == ControllerChange::None);
	// using the other pad hands charge over, and back again
	CHECK(Use(roster, 12) == ControllerChange::HandedOver && roster.Active() == 12);
	CHECK(Use(roster, 12) == ControllerChange::None);
	CHECK(Use(roster, 11) == ControllerChange::HandedOver && roster.Active() == 11);
	// a device the roster never opened is nobody
	CHECK(Use(roster, 99) == ControllerChange::None && roster.Active() == 11);
	CHECK(Use(roster, 0) == ControllerChange::None);

	// duplicates and the zero id are refused
	CHECK(!roster.Add(11, true, "guid-xbox", change) && change == ControllerChange::None);
	CHECK(!roster.Add(0, true, "guid-zero", change));
	CHECK(roster.Count() == 2);
}

static void Loss() {
	ControllerRoster roster;
	ControllerChange change;
	roster.Add(11, true, "a", change);
	roster.Add(12, true, "b", change);
	roster.Add(13, false, "stick", change);

	// losing an inactive pad is silent
	CHECK(roster.Remove(12) == ControllerChange::None && roster.Active() == 11 && roster.Count() == 2);
	CHECK(roster.Remove(12) == ControllerChange::None);
	// losing the active pad hands over to what is left
	CHECK(roster.Remove(11) == ControllerChange::HandedOver && roster.Active() == 13);
	// losing the last one is a loss
	CHECK(roster.Remove(13) == ControllerChange::Lost && roster.Active() == 0 && roster.Count() == 0);
	// reconnecting takes charge again
	CHECK(roster.Add(21, true, "a", change) && change == ControllerChange::Activated && roster.Active() == 21);

	// a gamepad is preferred over a joystick as the successor, whatever the order
	ControllerRoster order;
	order.Add(1, true, "pad-one", change);
	order.Add(2, false, "stick", change);
	order.Add(3, true, "pad-two", change);
	CHECK(order.Remove(1) == ControllerChange::HandedOver && order.Active() == 3);
}

static void GamepadOverJoystick() {
	ControllerRoster roster;
	ControllerChange change;
	// a flight stick alone is the controller
	CHECK(roster.Add(5, false, "stick", change) && change == ControllerChange::Activated);
	// a gamepad that connects takes over from it, as it always has
	CHECK(roster.Add(6, true, "pad", change) && change == ControllerChange::HandedOver && roster.Active() == 6);
	// a joystick that connects does not take over from a gamepad
	CHECK(roster.Add(7, false, "stick-two", change) && change == ControllerChange::None && roster.Active() == 6);
	// but using it does
	CHECK(Use(roster, 7) == ControllerChange::HandedOver && roster.Active() == 7);
}

static void ChosenDevice() {
	ControllerRoster roster;
	ControllerChange change;
	roster.Add(11, true, "guid-xbox", change);
	roster.Add(12, true, "guid-dualsense/serial-7", change);
	CHECK(roster.Active() == 11 && !roster.PreferenceConnected());

	// choosing a connected device moves charge to it at once
	CHECK(roster.SetPreference("guid-dualsense/serial-7") == ControllerChange::HandedOver && roster.Active() == 12);
	CHECK(roster.PreferenceConnected());
	// and use of another pad no longer takes it
	CHECK(Use(roster, 11) == ControllerChange::None && roster.Active() == 12);

	// while the chosen device is away the others work as Automatic
	CHECK(roster.Remove(12) == ControllerChange::HandedOver && roster.Active() == 11);
	CHECK(!roster.PreferenceConnected());
	roster.Add(13, true, "guid-switch", change);
	CHECK(Use(roster, 13) == ControllerChange::HandedOver && roster.Active() == 13);

	// reconnecting keeps the choice: it takes charge back under a new instance id
	CHECK(roster.Add(31, true, "guid-dualsense/serial-7", change) && change == ControllerChange::HandedOver);
	CHECK(roster.Active() == 31);
	CHECK(Use(roster, 13) == ControllerChange::None && roster.Active() == 31);

	// a chosen joystick keeps charge when a gamepad connects
	ControllerRoster stick;
	stick.Add(5, false, "hotas", change);
	stick.SetPreference("hotas");
	CHECK(stick.Add(6, true, "pad", change) && change == ControllerChange::None && stick.Active() == 5);

	// the chosen device is the successor when the active one goes
	ControllerRoster successor;
	successor.Add(1, true, "one", change);
	successor.Add(2, true, "two", change);
	successor.Add(3, true, "three", change);
	successor.SetPreference("absent");
	CHECK(successor.Active() == 1);
	CHECK(successor.SetPreference("three") == ControllerChange::HandedOver && successor.Active() == 3);

	// Automatic again: nothing moves until a pad is used
	CHECK(roster.SetPreference("") == ControllerChange::None && roster.Active() == 31);
	CHECK(Use(roster, 13) == ControllerChange::HandedOver);
	CHECK(roster.SetPreference(nullptr) == ControllerChange::None);

	// a choice made with nothing connected waits for the device
	ControllerRoster empty;
	CHECK(empty.SetPreference("later") == ControllerChange::None);
	CHECK(empty.Add(1, true, "first", change) && change == ControllerChange::Activated);
	CHECK(empty.Add(2, true, "later", change) && change == ControllerChange::HandedOver && empty.Active() == 2);
}

static void PutDownBeforeHandover() {
	ControllerRoster roster;
	ControllerChange change;
	roster.Add(11, true, "guid-xbox", change);
	roster.Add(12, true, "guid-dualsense", change);
	const int rest = openq4::CONTROLLER_HANDOVER_REST_MSEC;
	// the active pad is used; the other pad's use waits for it to rest
	CHECK(roster.NoteUse(11, 1000, false) == ControllerChange::None);
	CHECK(roster.NoteUse(12, 1000 + rest - 1, false) == ControllerChange::None && roster.Active() == 11);
	// nor does charge move while the active pad holds something, however long
	CHECK(roster.NoteUse(12, 1000 + 10 * rest, true) == ControllerChange::None && roster.Active() == 11);
	// put down and rested: use moves charge
	CHECK(roster.NoteUse(12, 1000 + 10 * rest + 300, false) == ControllerChange::HandedOver && roster.Active() == 12);
	// a pad never used since it connected has nothing to rest from
	ControllerRoster fresh;
	fresh.Add(1, true, "a", change);
	fresh.Add(2, true, "b", change);
	CHECK(fresh.NoteUse(2, 5, false) == ControllerChange::HandedOver && fresh.Active() == 2);
}

// One pair of hands seen as two devices (a remapper's virtual pad beside the
// physical one): both report every movement, a few milliseconds apart.
static void Twins() {
	ControllerRoster roster;
	ControllerChange change;
	roster.Add(11, true, "guid-dualshock", change);
	roster.Add(12, true, "guid-virtual-xbox", change);
	CHECK(roster.Active() == 11);

	// a movement held on both: the active pad reports first, every event, and
	// charge never moves (it used to move on every event)
	int now = 100;
	for (int i = 0; i < 200; ++i, now += 4) {
		CHECK(roster.NoteUse(11, now, true) == ControllerChange::None);
		CHECK(roster.NoteUse(12, now + 1, true) == ControllerChange::None);
	}
	CHECK(roster.Active() == 11 && roster.ActiveTwin() == 0);

	// after a rest the waiting device wins the race once: charge moves, and
	// the old device moving within the twin window marks the pair
	now += 2000;
	CHECK(roster.NoteUse(12, now, false) == ControllerChange::HandedOver && roster.Active() == 12);
	CHECK(roster.NoteUse(11, now + 3, true) == ControllerChange::None && roster.Active() == 12);
	CHECK(roster.ActiveTwin() == 11);

	// later races after ordinary rests do not move it back
	for (int race = 0; race < 5; ++race) {
		now += 2000;
		CHECK(roster.NoteUse(11, now, false) == ControllerChange::None);
		CHECK(roster.NoteUse(12, now + 2, true) == ControllerChange::None);
		CHECK(roster.Active() == 12);
	}

	// a long rest of the active device lets the twin take charge: a false
	// pairing is never permanent
	now += openq4::CONTROLLER_TWIN_REST_MSEC + 10;
	CHECK(roster.NoteUse(11, now, false) == ControllerChange::HandedOver && roster.Active() == 11);

	// the twin going away forgets the pair
	CHECK(roster.Remove(12) == ControllerChange::None && roster.ActiveTwin() == 0);

	// two separate pads used one after another more than the window apart
	// are not twins
	ControllerRoster pair;
	pair.Add(1, true, "a", change);
	pair.Add(2, true, "b", change);
	CHECK(pair.NoteUse(2, 10, false) == ControllerChange::HandedOver);
	CHECK(pair.NoteUse(1, 10 + openq4::CONTROLLER_TWIN_WINDOW_MSEC + 1, false) == ControllerChange::None);
	CHECK(pair.ActiveTwin() == 0 && pair.Active() == 2);
}

static void Capacity() {
	ControllerRoster roster;
	ControllerChange change;
	for (int i = 0; i < openq4::CONTROLLER_ROSTER_MAX; ++i) {
		CHECK(roster.Add(100 + i, true, "pad", change));
	}
	CHECK(!roster.Add(999, true, "pad", change) && roster.Count() == openq4::CONTROLLER_ROSTER_MAX);
	CHECK(roster.Active() == 100);
	roster.Clear();
	CHECK(roster.Count() == 0 && roster.Active() == 0);
	// an over-long identity is cut, not overrun
	char longName[400];
	for (int i = 0; i < 399; ++i) {
		longName[i] = 'x';
	}
	longName[399] = '\0';
	CHECK(roster.Add(1, true, longName, change));
	CHECK(std::strlen(roster.Entry(0).identity) == openq4::CONTROLLER_IDENTITY_CHARS - 1);
}

static void Battery() {
	using openq4::ControllerBatteryNotice;
	using openq4::ControllerPower;
	openq4::ControllerBatteryState state;

	// plenty of charge: nothing
	CHECK(openq4::UpdateControllerBattery(state, 7, ControllerPower::OnBattery, 80, 15) == ControllerBatteryNotice::None);
	CHECK(openq4::UpdateControllerBattery(state, 7, ControllerPower::OnBattery, 16, 15) == ControllerBatteryNotice::None);
	// reaching the threshold: one notice
	CHECK(openq4::UpdateControllerBattery(state, 7, ControllerPower::OnBattery, 15, 15) == ControllerBatteryNotice::Low);
	// and only one, however the reading wanders
	CHECK(openq4::UpdateControllerBattery(state, 7, ControllerPower::OnBattery, 14, 15) == ControllerBatteryNotice::None);
	CHECK(openq4::UpdateControllerBattery(state, 7, ControllerPower::OnBattery, 17, 15) == ControllerBatteryNotice::None);
	CHECK(openq4::UpdateControllerBattery(state, 7, ControllerPower::OnBattery, 12, 15) == ControllerBatteryNotice::None);
	// plugging in: the second notice, once
	CHECK(openq4::UpdateControllerBattery(state, 7, ControllerPower::Charging, 12, 15) == ControllerBatteryNotice::Charging);
	CHECK(openq4::UpdateControllerBattery(state, 7, ControllerPower::Charging, 13, 15) == ControllerBatteryNotice::None);
	CHECK(openq4::UpdateControllerBattery(state, 7, ControllerPower::Charged, 100, 15) == ControllerBatteryNotice::None);
	// unplugged and run down again: it may say so again
	CHECK(openq4::UpdateControllerBattery(state, 7, ControllerPower::OnBattery, 60, 15) == ControllerBatteryNotice::None);
	CHECK(openq4::UpdateControllerBattery(state, 7, ControllerPower::OnBattery, 10, 15) == ControllerBatteryNotice::Low);

	// an unknown reading, or the notice turned off, says nothing
	openq4::ControllerBatteryState quiet;
	CHECK(openq4::UpdateControllerBattery(quiet, 3, ControllerPower::OnBattery, -1, 15) == ControllerBatteryNotice::None);
	CHECK(openq4::UpdateControllerBattery(quiet, 3, ControllerPower::Unknown, 5, 15) == ControllerBatteryNotice::None);
	CHECK(openq4::UpdateControllerBattery(quiet, 3, ControllerPower::OnBattery, 5, 0) == ControllerBatteryNotice::None);
	CHECK(openq4::UpdateControllerBattery(quiet, 3, ControllerPower::Wired, 5, 15) == ControllerBatteryNotice::None);

	// another controller takes over already low: its own notice, and a
	// charging pad arriving is not "started charging"
	openq4::ControllerBatteryState handover;
	CHECK(openq4::UpdateControllerBattery(handover, 1, ControllerPower::OnBattery, 90, 15) == ControllerBatteryNotice::None);
	CHECK(openq4::UpdateControllerBattery(handover, 2, ControllerPower::OnBattery, 9, 15) == ControllerBatteryNotice::Low);
	CHECK(openq4::UpdateControllerBattery(handover, 3, ControllerPower::Charging, 40, 15) == ControllerBatteryNotice::None);
	CHECK(openq4::UpdateControllerBattery(handover, 0, ControllerPower::Unknown, -1, 15) == ControllerBatteryNotice::None);
	CHECK(openq4::UpdateControllerBattery(handover, 0, ControllerPower::Unknown, -1, 15) == ControllerBatteryNotice::None);
}

int main() {
	ConnectAndUse();
	Loss();
	GamepadOverJoystick();
	ChosenDevice();
	PutDownBeforeHandover();
	Twins();
	Capacity();
	Battery();
	std::puts("controller roster: ok");
	return 0;
}
