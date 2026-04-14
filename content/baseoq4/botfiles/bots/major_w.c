// Prototype OpenQ4 multiplayer bot weapon weights.

weight "weapon_machinegun" switch (6) {
	case 0: return 0;
	default: {
		switch (19) {
			case 0: return 5;
			case 40: return 35;
			case 120: return 55;
			default: return 45;
		}
	}
}

weight "weapon_shotgun" switch (5) {
	case 0: return 0;
	default: {
		switch (18) {
			case 0: return 0;
			case 4: return 35;
			case 12: return 80;
			default: return 70;
		}
	}
}

weight "weapon_hyperblaster" switch (11) {
	case 0: return 0;
	default: {
		switch (21) {
			case 0: return 0;
			case 30: return 40;
			case 80: return 72;
			default: return 64;
		}
	}
}

weight "weapon_rocketlauncher" switch (8) {
	case 0: return 0;
	default: {
		switch (23) {
			case 0: return 0;
			case 3: return 45;
			case 8: return 88;
			default: return 78;
		}
	}
}
