// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
//
// Virtual test controllers (PAD rule 5: asset-free tests first). Included by
// sdl3_backend.cpp.
//
// The cheat-gated virtualController command attaches SDL virtual joysticks
// inside this process, so the controller layer can be driven without hardware
// and without synthesising any operating-system input: the devices exist only
// in this game's SDL, and nothing reaches another window. They arrive through
// the same SDL events a real pad raises, so hotplug, handover, release, the
// shaped axes, gyro, the touchpad, rumble and lights all take the real path.
// What the game sends a virtual pad (rumble, trigger rumble, light, player
// number, effects, sensor switch) is recorded for `virtualController report`.
//
// A virtual pad cannot report a battery; in_joystickPowerProbe stands in.
//
// SDL drops controller input while the window has no keyboard focus. A test
// window is usually hidden, so while any virtual controller is attached the
// background-events hint is on, and it returns to what it was afterwards.

struct sdlVirtualController_t {
	bool			attached;
	SDL_JoystickID	id;
	SDL_Joystick *	joystick;		// our own reference, for the SDL_SetJoystickVirtual* calls
	char			profile[16];
	bool			sensors;		// has a gyro and an accelerometer
	// a rotation being held, sent as one gyro sample a frame until it runs out,
	// on top of a constant bias as an uncalibrated sensor reports; and what the
	// accelerometer reads (at rest, face up: +y)
	float			gyro[3];
	Uint64			gyroUntilNS;
	float			gyroBias[3];
	float			accel[3];
	// what the game sent
	int				rumbleCalls;
	Uint16			rumbleLow, rumbleHigh;
	int				triggerRumbleCalls;
	Uint16			triggerRumbleLeft, triggerRumbleRight;
	int				ledCalls;
	Uint8			led[3];
	int				playerIndex;
	int				effectCalls;
	int				effectBytes;
	unsigned int	effectChecksum;
	bool			sensorsEnabled;
};

static const int SDL3_MAX_VIRTUAL_CONTROLLERS = 4;
static sdlVirtualController_t s_virtualControllers[SDL3_MAX_VIRTUAL_CONTROLLERS];
static bool s_virtualBackgroundEvents = false;
static idStr s_virtualBackgroundEventsBefore;

static void SDL3_VirtualControllerBackgroundEvents(void) {
	bool any = false;
	for (int slot = 0; slot < SDL3_MAX_VIRTUAL_CONTROLLERS; ++slot) {
		any = any || s_virtualControllers[slot].attached;
	}
	if (any == s_virtualBackgroundEvents) {
		return;
	}
	if (any) {
		const char *before = SDL_GetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
		s_virtualBackgroundEventsBefore = before != NULL ? before : "0";
		(void)SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	} else {
		(void)SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, s_virtualBackgroundEventsBefore.c_str());
	}
	s_virtualBackgroundEvents = any;
}

static void SDLCALL SDL3_Virtual_SetPlayerIndex(void *userdata, int playerIndex) {
	static_cast<sdlVirtualController_t *>(userdata)->playerIndex = playerIndex;
}

static bool SDLCALL SDL3_Virtual_Rumble(void *userdata, Uint16 low, Uint16 high) {
	sdlVirtualController_t *pad = static_cast<sdlVirtualController_t *>(userdata);
	pad->rumbleCalls++;
	pad->rumbleLow = low;
	pad->rumbleHigh = high;
	return true;
}

static bool SDLCALL SDL3_Virtual_RumbleTriggers(void *userdata, Uint16 left, Uint16 right) {
	sdlVirtualController_t *pad = static_cast<sdlVirtualController_t *>(userdata);
	pad->triggerRumbleCalls++;
	pad->triggerRumbleLeft = left;
	pad->triggerRumbleRight = right;
	return true;
}

static bool SDLCALL SDL3_Virtual_SetLED(void *userdata, Uint8 red, Uint8 green, Uint8 blue) {
	sdlVirtualController_t *pad = static_cast<sdlVirtualController_t *>(userdata);
	pad->ledCalls++;
	pad->led[0] = red;
	pad->led[1] = green;
	pad->led[2] = blue;
	return true;
}

static bool SDLCALL SDL3_Virtual_SendEffect(void *userdata, const void *data, int size) {
	sdlVirtualController_t *pad = static_cast<sdlVirtualController_t *>(userdata);
	pad->effectCalls++;
	pad->effectBytes = size;
	unsigned int sum = 0;
	for (int i = 0; data != NULL && i < size; ++i) {
		sum = sum * 31u + static_cast<const unsigned char *>(data)[i];
	}
	pad->effectChecksum = sum;
	return true;
}

static bool SDLCALL SDL3_Virtual_SetSensorsEnabled(void *userdata, bool enabled) {
	static_cast<sdlVirtualController_t *>(userdata)->sensorsEnabled = enabled;
	return true;
}

// Whether an event came from one of the virtual pads. They exist only inside
// this process, so the rule that drops user input while another window has
// the desktop's focus does not apply to them.
static bool SDL3_IsVirtualControllerEvent(const SDL_Event &event) {
	SDL_JoystickID id = 0;
	switch (event.type) {
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			id = event.gbutton.which;
			break;
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			id = event.gaxis.which;
			break;
		case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
		case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
		case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
			id = event.gtouchpad.which;
			break;
		case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
			id = event.gsensor.which;
			break;
		case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
		case SDL_EVENT_JOYSTICK_BUTTON_UP:
			id = event.jbutton.which;
			break;
		case SDL_EVENT_JOYSTICK_HAT_MOTION:
			id = event.jhat.which;
			break;
		case SDL_EVENT_JOYSTICK_AXIS_MOTION:
			id = event.jaxis.which;
			break;
		default:
			return false;
	}
	for (int slot = 0; id != 0 && slot < SDL3_MAX_VIRTUAL_CONTROLLERS; ++slot) {
		if (s_virtualControllers[slot].attached && s_virtualControllers[slot].id == id) {
			return true;
		}
	}
	return false;
}

static sdlVirtualController_t *SDL3_VirtualControllerSlot(const idCmdArgs &args, int arg) {
	const int slot = args.Argc() > arg ? atoi(args.Argv(arg)) : -1;
	if (slot < 0 || slot >= SDL3_MAX_VIRTUAL_CONTROLLERS || !s_virtualControllers[slot].attached) {
		common->Printf("virtualController: no virtual controller in slot '%s'\n", args.Argc() > arg ? args.Argv(arg) : "");
		return NULL;
	}
	return &s_virtualControllers[slot];
}

static void SDL3_VirtualControllerReport(int slot) {
	const sdlVirtualController_t &pad = s_virtualControllers[slot];
	common->Printf("VIRTUAL_CONTROLLER slot=%d id=%u profile=%s active=%d rumble=%d:%u:%u triggerRumble=%d:%u:%u "
		"led=%d:%u,%u,%u player=%d effect=%d:%d:%u sensors=%d\n",
		slot, static_cast<unsigned int>(pad.id), pad.profile,
		static_cast<std::uint32_t>(pad.id) == s_controllerRoster.Active() ? 1 : 0,
		pad.rumbleCalls, pad.rumbleLow, pad.rumbleHigh,
		pad.triggerRumbleCalls, pad.triggerRumbleLeft, pad.triggerRumbleRight,
		pad.ledCalls, pad.led[0], pad.led[1], pad.led[2], pad.playerIndex,
		pad.effectCalls, pad.effectBytes, pad.effectChecksum, pad.sensorsEnabled ? 1 : 0);
}

static void SDL3_VirtualControllerDetach(int slot) {
	sdlVirtualController_t &pad = s_virtualControllers[slot];
	if (!pad.attached) {
		return;
	}
	if (pad.joystick != NULL) {
		SDL_CloseJoystick(pad.joystick);
	}
	(void)SDL_DetachVirtualJoystick(pad.id);
	common->Printf("VIRTUAL_CONTROLLER detached slot=%d id=%u\n", slot, static_cast<unsigned int>(pad.id));
	memset(&pad, 0, sizeof(pad));
	SDL3_VirtualControllerBackgroundEvents();
}

// The controller keys the engine holds down, as bindings and menus see them:
// what a handover or a loss must have released.
static void SDL3_VirtualControllerKeysDown(void) {
	idStr keys;
	for (int i = 0; i < 32 + 16; ++i) {
		const int key = SDL3_JoyKeyFromOrdinal(i);
		if (key != 0 && idKeyInput::IsDown(key)) {
			keys += keys.Length() > 0 ? "," : "";
			keys += i < 32 ? va("JOY%d", i + 1) : va("AUX%d", i - 31);
		}
	}
	int axes[MAX_JOYSTICK_AXIS];
	Sys_EnterCriticalSection(CRITICAL_SECTION_ONE);
	for (int axis = 0; axis < MAX_JOYSTICK_AXIS; ++axis) {
		axes[axis] = s_joystickAxisState[axis];
	}
	Sys_LeaveCriticalSection(CRITICAL_SECTION_ONE);
	common->Printf("VIRTUAL_CONTROLLER keysDown=%s look=%d,%d move=%d,%d family=%s instance=%d count=%d time=%d\n",
		keys.Length() > 0 ? keys.c_str() : "none", axes[AXIS_SIDE], axes[AXIS_FORWARD], axes[AXIS_YAW], axes[AXIS_PITCH],
		in_gamepadFamily.GetString(), in_gamepadInstance.GetInteger(), in_gamepadCount.GetInteger(), Sys_Milliseconds());
}

// before the joystick subsystem goes away
static void SDL3_DetachVirtualControllers(void) {
	for (int slot = 0; slot < SDL3_MAX_VIRTUAL_CONTROLLERS; ++slot) {
		SDL3_VirtualControllerDetach(slot);
	}
}

static int SDL3_VirtualControllerButton(const char *name) {
	static const struct { const char *name; int button; } names[] = {
		{ "south", SDL_GAMEPAD_BUTTON_SOUTH }, { "east", SDL_GAMEPAD_BUTTON_EAST },
		{ "west", SDL_GAMEPAD_BUTTON_WEST }, { "north", SDL_GAMEPAD_BUTTON_NORTH },
		{ "back", SDL_GAMEPAD_BUTTON_BACK }, { "guide", SDL_GAMEPAD_BUTTON_GUIDE }, { "start", SDL_GAMEPAD_BUTTON_START },
		{ "leftstick", SDL_GAMEPAD_BUTTON_LEFT_STICK }, { "rightstick", SDL_GAMEPAD_BUTTON_RIGHT_STICK },
		{ "leftshoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER }, { "rightshoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
		{ "dpup", SDL_GAMEPAD_BUTTON_DPAD_UP }, { "dpdown", SDL_GAMEPAD_BUTTON_DPAD_DOWN },
		{ "dpleft", SDL_GAMEPAD_BUTTON_DPAD_LEFT }, { "dpright", SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
		{ "misc1", SDL_GAMEPAD_BUTTON_MISC1 },
		{ "rightpaddle1", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 }, { "leftpaddle1", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1 },
		{ "rightpaddle2", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2 }, { "leftpaddle2", SDL_GAMEPAD_BUTTON_LEFT_PADDLE2 },
		{ "touchpad", SDL_GAMEPAD_BUTTON_TOUCHPAD },
	};
	for (int i = 0; i < static_cast<int>(sizeof(names) / sizeof(names[0])); ++i) {
		if (!idStr::Icmp(name, names[i].name)) {
			return names[i].button;
		}
	}
	return (name[0] >= '0' && name[0] <= '9') ? atoi(name) : -1;
}

static int SDL3_VirtualControllerAxis(const char *name) {
	static const struct { const char *name; int axis; } names[] = {
		{ "leftx", SDL_GAMEPAD_AXIS_LEFTX }, { "lefty", SDL_GAMEPAD_AXIS_LEFTY },
		{ "rightx", SDL_GAMEPAD_AXIS_RIGHTX }, { "righty", SDL_GAMEPAD_AXIS_RIGHTY },
		{ "lefttrigger", SDL_GAMEPAD_AXIS_LEFT_TRIGGER }, { "righttrigger", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER },
	};
	for (int i = 0; i < static_cast<int>(sizeof(names) / sizeof(names[0])); ++i) {
		if (!idStr::Icmp(name, names[i].name)) {
			return names[i].axis;
		}
	}
	return (name[0] >= '0' && name[0] <= '9') ? atoi(name) : -1;
}

static void SDL3_VirtualControllerAttach(const char *profile) {
	if (!s_sdlJoystickSubsystemActive) {
		common->Printf("virtualController: controllers are off (in_joystick 0)\n");
		return;
	}
	int slot = 0;
	while (slot < SDL3_MAX_VIRTUAL_CONTROLLERS && s_virtualControllers[slot].attached) {
		++slot;
	}
	if (slot == SDL3_MAX_VIRTUAL_CONTROLLERS) {
		common->Printf("virtualController: all %d slots are in use\n", SDL3_MAX_VIRTUAL_CONTROLLERS);
		return;
	}

	static const SDL_VirtualJoystickSensorDesc sensors[2] = { { SDL_SENSOR_GYRO, 250.0f }, { SDL_SENSOR_ACCEL, 250.0f } };
	static const SDL_VirtualJoystickTouchpadDesc touchpads[1] = { { 2, { 0, 0, 0 } } };
	SDL_VirtualJoystickDesc desc;
	SDL_INIT_INTERFACE(&desc);
	desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
	desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
	bool hasSensors = false;
	if (!idStr::Icmp(profile, "xbox")) {
		desc.vendor_id = 0x045e;	// an Xbox Series pad
		desc.product_id = 0x0b12;
		desc.name = "openQ4 Virtual Xbox Controller";
	} else if (!idStr::Icmp(profile, "playstation")) {
		desc.vendor_id = 0x054c;	// a DualSense
		desc.product_id = 0x0ce6;
		desc.name = "openQ4 Virtual DualSense";
		desc.nsensors = 2;
		desc.sensors = sensors;
		desc.ntouchpads = 1;
		desc.touchpads = touchpads;
		hasSensors = true;
	} else if (!idStr::Icmp(profile, "switch")) {
		desc.vendor_id = 0x057e;	// a Switch Pro Controller
		desc.product_id = 0x2009;
		desc.name = "openQ4 Virtual Switch Pro Controller";
		desc.nsensors = 2;
		desc.sensors = sensors;
		hasSensors = true;
	} else if (!idStr::Icmp(profile, "generic")) {
		desc.name = "openQ4 Virtual Gamepad";
	} else if (!idStr::Icmp(profile, "stick")) {
		// a device SDL cannot map as a gamepad: the raw-joystick path
		desc.type = SDL_JOYSTICK_TYPE_FLIGHT_STICK;
		desc.naxes = 4;
		desc.nbuttons = 12;
		desc.nhats = 1;
		desc.name = "openQ4 Virtual Flight Stick";
	} else {
		common->Printf("virtualController: unknown profile '%s' (xbox, playstation, switch, generic, stick)\n", profile);
		return;
	}

	sdlVirtualController_t &pad = s_virtualControllers[slot];
	memset(&pad, 0, sizeof(pad));
	pad.playerIndex = -1;
	desc.userdata = &pad;
	desc.SetPlayerIndex = SDL3_Virtual_SetPlayerIndex;
	desc.Rumble = SDL3_Virtual_Rumble;
	desc.RumbleTriggers = SDL3_Virtual_RumbleTriggers;
	desc.SetLED = SDL3_Virtual_SetLED;
	desc.SendEffect = SDL3_Virtual_SendEffect;
	desc.SetSensorsEnabled = SDL3_Virtual_SetSensorsEnabled;

	const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
	if (id == 0) {
		common->Printf("virtualController: SDL could not attach a virtual joystick: %s\n", SDL_GetError());
		return;
	}
	pad.attached = true;
	pad.id = id;
	pad.sensors = hasSensors;
	pad.accel[1] = SDL_STANDARD_GRAVITY;	// lying flat, face up
	idStr::Copynz(pad.profile, profile, sizeof(pad.profile));
	pad.joystick = SDL_OpenJoystick(id);
	SDL3_VirtualControllerBackgroundEvents();
	common->Printf("VIRTUAL_CONTROLLER attached slot=%d id=%u profile=%s\n", slot, static_cast<unsigned int>(id), pad.profile);
}

// The sensors of a virtual pad report every frame, as real ones do: the
// accelerometer, then the gyro's bias plus any rotation being held. Samples
// carry the real clock, so the game integrates a held rate over the time it
// was really held; the sample that ends a hold is stamped with its end.
static void SDL3_VirtualControllerFrame(void) {
	const Uint64 now = SDL_GetTicksNS();
	for (int slot = 0; slot < SDL3_MAX_VIRTUAL_CONTROLLERS; ++slot) {
		sdlVirtualController_t &pad = s_virtualControllers[slot];
		if (!pad.attached || pad.joystick == NULL || !pad.sensors || !pad.sensorsEnabled) {
			continue;
		}
		(void)SDL_SendJoystickVirtualSensorData(pad.joystick, SDL_SENSOR_ACCEL, now, pad.accel, 3);
		float data[3] = { pad.gyroBias[0], pad.gyroBias[1], pad.gyroBias[2] };
		if (pad.gyroUntilNS != 0) {
			const float held[3] = { data[0] + pad.gyro[0], data[1] + pad.gyro[1], data[2] + pad.gyro[2] };
			if (now >= pad.gyroUntilNS) {
				(void)SDL_SendJoystickVirtualSensorData(pad.joystick, SDL_SENSOR_GYRO, pad.gyroUntilNS, held, 3);
				pad.gyroUntilNS = 0;
				common->Printf("VIRTUAL_CONTROLLER gyro done slot=%d\n", slot);
				if (now > pad.gyroUntilNS) {
					(void)SDL_SendJoystickVirtualSensorData(pad.joystick, SDL_SENSOR_GYRO, now, data, 3);
				}
			} else {
				(void)SDL_SendJoystickVirtualSensorData(pad.joystick, SDL_SENSOR_GYRO, now, held, 3);
			}
			continue;
		}
		(void)SDL_SendJoystickVirtualSensorData(pad.joystick, SDL_SENSOR_GYRO, now, data, 3);
	}
}

static void SDL3_VirtualController_f(const idCmdArgs &args) {
	const char *verb = args.Argv(1);
	if (!idStr::Icmp(verb, "attach")) {
		SDL3_VirtualControllerAttach(args.Argc() > 2 ? args.Argv(2) : "xbox");
		return;
	}
	if (!idStr::Icmp(verb, "report")) {
		for (int slot = 0; slot < SDL3_MAX_VIRTUAL_CONTROLLERS; ++slot) {
			if (s_virtualControllers[slot].attached && (args.Argc() < 3 || atoi(args.Argv(2)) == slot)) {
				SDL3_VirtualControllerReport(slot);
			}
		}
		SDL3_VirtualControllerKeysDown();
		return;
	}
	if (!idStr::Icmp(verb, "context")) {
		// what a controller button does right now: its context action
		// (PAD-06) if one is open, else its binding
		const int key = idKeyInput::StringToKeyNum(args.Argv(2));
		const char *context = idKeyInput::ContextBinding(key);
		common->Printf("VIRTUAL_CONTROLLER context key=%s action=%s binding=%s prompt=%s\n", args.Argv(2),
			context != NULL ? va("[%s]", context) : "none", va("[%s]", idKeyInput::GetBinding(key)),
			args.Argc() > 3 ? idKeyInput::KeysFromBinding(args.Argv(3)) : "");
		return;
	}
	if (!idStr::Icmp(verb, "choose")) {
		// the saved device choice: this slot's device, or automatic
		if (!idStr::Icmp(args.Argv(2), "auto")) {
			in_joystickDevice.SetString("");
		} else if (sdlVirtualController_t *pad = SDL3_VirtualControllerSlot(args, 2)) {
			const sdlController_t *controller = SDL3_FindController(pad->id);
			if (controller != NULL) {
				char identity[openq4::CONTROLLER_IDENTITY_CHARS];
				SDL3_ControllerIdentity(*controller, identity, sizeof(identity));
				in_joystickDevice.SetString(identity);
			}
		}
		return;
	}
	if (!idStr::Icmp(verb, "detach")) {
		if (!idStr::Icmp(args.Argv(2), "all")) {
			SDL3_DetachVirtualControllers();
		} else if (sdlVirtualController_t *pad = SDL3_VirtualControllerSlot(args, 2)) {
			SDL3_VirtualControllerDetach(static_cast<int>(pad - s_virtualControllers));
		}
		return;
	}

	const bool known = !idStr::Icmp(verb, "button") || !idStr::Icmp(verb, "axis") || !idStr::Icmp(verb, "hat") ||
		!idStr::Icmp(verb, "gyro") || !idStr::Icmp(verb, "gyrobias") || !idStr::Icmp(verb, "accel") || !idStr::Icmp(verb, "touch");
	if (!known) {
		common->Printf("usage: virtualController attach <xbox|playstation|switch|generic|stick>\n"
			"       virtualController detach <slot|all>\n"
			"       virtualController button <slot> <name|index> <0|1>\n"
			"       virtualController axis <slot> <leftx|lefty|rightx|righty|lefttrigger|righttrigger|index> <-1..1>\n"
			"       virtualController hat <slot> <center|up|down|left|right>\n"
			"       virtualController gyro <slot> <pitch> <yaw> <roll> <seconds>   (radians per second)\n"
			"       virtualController gyrobias <slot> <pitch> <yaw> <roll>         (a constant sensor bias)\n"
			"       virtualController accel <slot> <x> <y> <z>                     (metres per second squared)\n"
			"       virtualController touch <slot> <finger> <0|1> <x> <y>          (0..1)\n"
			"       virtualController choose <slot|auto>                           (the saved device choice)\n"
			"       virtualController context <key> [command]                      (a button's context action; a command's prompt)\n"
			"       virtualController report [slot]\n");
		return;
	}
	sdlVirtualController_t *pad = SDL3_VirtualControllerSlot(args, 2);
	if (pad == NULL || pad->joystick == NULL) {
		return;
	}

	if (!idStr::Icmp(verb, "button")) {
		const int button = SDL3_VirtualControllerButton(args.Argv(3));
		if (button < 0 || !SDL_SetJoystickVirtualButton(pad->joystick, button, atoi(args.Argv(4)) != 0)) {
			common->Printf("virtualController: could not set button '%s'\n", args.Argv(3));
		}
	} else if (!idStr::Icmp(verb, "axis")) {
		const int axis = SDL3_VirtualControllerAxis(args.Argv(3));
		const float value = SDL3_ClampRange(static_cast<float>(atof(args.Argv(4))), -1.0f, 1.0f);
		// a trigger rests at the bottom of its axis and reaches the top fully pressed
		const bool trigger = idStr::Icmp(pad->profile, "stick") != 0 &&
			(axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
		const float raw = trigger ? (SDL3_ClampUnit(value) * 2.0f - 1.0f) : value;
		const Sint16 scaled = static_cast<Sint16>(idMath::ClampInt(-32768, 32767, static_cast<int>(roundf(raw * 32767.0f)) - (raw <= -1.0f ? 1 : 0)));
		if (axis < 0 || !SDL_SetJoystickVirtualAxis(pad->joystick, axis, scaled)) {
			common->Printf("virtualController: could not set axis '%s'\n", args.Argv(3));
		}
	} else if (!idStr::Icmp(verb, "hat")) {
		Uint8 hat = SDL_HAT_CENTERED;
		if (!idStr::Icmp(args.Argv(3), "up")) {
			hat = SDL_HAT_UP;
		} else if (!idStr::Icmp(args.Argv(3), "down")) {
			hat = SDL_HAT_DOWN;
		} else if (!idStr::Icmp(args.Argv(3), "left")) {
			hat = SDL_HAT_LEFT;
		} else if (!idStr::Icmp(args.Argv(3), "right")) {
			hat = SDL_HAT_RIGHT;
		}
		if (!SDL_SetJoystickVirtualHat(pad->joystick, 0, hat)) {
			common->Printf("virtualController: could not set the hat\n");
		}
	} else if (!idStr::Icmp(verb, "gyro")) {
		if (!pad->sensors) {
			common->Printf("virtualController: the %s profile has no gyro\n", pad->profile);
			return;
		}
		pad->gyro[0] = static_cast<float>(atof(args.Argv(3)));
		pad->gyro[1] = static_cast<float>(atof(args.Argv(4)));
		pad->gyro[2] = static_cast<float>(atof(args.Argv(5)));
		const float seconds = SDL3_ClampRange(static_cast<float>(atof(args.Argv(6))), 0.0f, 60.0f);
		const Uint64 now = SDL_GetTicksNS();
		// open the interval now, so the hold integrates from this moment
		const float opening[3] = { pad->gyroBias[0] + pad->gyro[0], pad->gyroBias[1] + pad->gyro[1], pad->gyroBias[2] + pad->gyro[2] };
		if (pad->sensorsEnabled && seconds > 0.0f) {
			(void)SDL_SendJoystickVirtualSensorData(pad->joystick, SDL_SENSOR_GYRO, now, opening, 3);
		}
		pad->gyroUntilNS = seconds > 0.0f ? now + static_cast<Uint64>(static_cast<double>(seconds) * 1000000000.0) : 0;
	} else if (!idStr::Icmp(verb, "gyrobias")) {
		pad->gyroBias[0] = static_cast<float>(atof(args.Argv(3)));
		pad->gyroBias[1] = static_cast<float>(atof(args.Argv(4)));
		pad->gyroBias[2] = static_cast<float>(atof(args.Argv(5)));
	} else if (!idStr::Icmp(verb, "accel")) {
		if (!pad->sensors) {
			common->Printf("virtualController: the %s profile has no accelerometer\n", pad->profile);
			return;
		}
		pad->accel[0] = static_cast<float>(atof(args.Argv(3)));
		pad->accel[1] = static_cast<float>(atof(args.Argv(4)));
		pad->accel[2] = static_cast<float>(atof(args.Argv(5)));
	} else {
		if (!SDL_SetJoystickVirtualTouchpad(pad->joystick, 0, atoi(args.Argv(3)), atoi(args.Argv(4)) != 0,
				static_cast<float>(atof(args.Argv(5))), static_cast<float>(atof(args.Argv(6))), atoi(args.Argv(4)) != 0 ? 1.0f : 0.0f)) {
			common->Printf("virtualController: the %s profile has no touchpad\n", pad->profile);
		}
	}
}
