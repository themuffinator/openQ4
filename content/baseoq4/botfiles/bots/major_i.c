// Prototype OpenQ4 multiplayer bot item weights.

weight "weapon_machinegun" switch (6) {
	case 0: return 60;
	default: return 2;
}

weight "weapon_machinegun_mp" switch (6) {
	case 0: return 60;
	default: return 2;
}

weight "weapon_shotgun" switch (5) {
	case 0: return 85;
	default: return 5;
}

weight "weapon_shotgun_mp" switch (5) {
	case 0: return 85;
	default: return 5;
}

weight "weapon_hyperblaster" switch (11) {
	case 0: return 78;
	default: return 5;
}

weight "weapon_hyperblaster_mp" switch (11) {
	case 0: return 78;
	default: return 5;
}

weight "weapon_rocketlauncher" switch (8) {
	case 0: return 96;
	default: return 6;
}

weight "weapon_rocketlauncher_mp" switch (8) {
	case 0: return 96;
	default: return 6;
}

weight "ammo_machinegun" switch (19) {
	case 0: return 45;
	case 60: return 28;
	case 120: return 8;
	default: return 5;
}

weight "ammo_machinegun_mp" switch (19) {
	case 0: return 45;
	case 60: return 28;
	case 120: return 8;
	default: return 5;
}

weight "ammo_shotgun" switch (18) {
	case 0: return 55;
	case 10: return 40;
	case 25: return 12;
	default: return 6;
}

weight "ammo_shotgun_mp" switch (18) {
	case 0: return 55;
	case 10: return 40;
	case 25: return 12;
	default: return 6;
}

weight "ammo_hyperblaster" switch (21) {
	case 0: return 52;
	case 60: return 30;
	case 120: return 10;
	default: return 6;
}

weight "ammo_hyperblaster_mp" switch (21) {
	case 0: return 52;
	case 60: return 30;
	case 120: return 10;
	default: return 6;
}

weight "ammo_rocketlauncher" switch (23) {
	case 0: return 70;
	case 5: return 55;
	case 15: return 20;
	default: return 8;
}

weight "ammo_rocketlauncher_mp" switch (23) {
	case 0: return 70;
	case 5: return 55;
	case 15: return 20;
	default: return 8;
}

weight "item_health_small" switch (29) {
	case 0: return 75;
	case 30: return 55;
	case 75: return 10;
	default: return 4;
}

weight "item_health_small_mp" switch (29) {
	case 0: return 75;
	case 30: return 55;
	case 75: return 10;
	default: return 4;
}

weight "item_health_large" switch (29) {
	case 0: return 90;
	case 40: return 65;
	case 90: return 15;
	default: return 6;
}

weight "item_health_large_mp" switch (29) {
	case 0: return 90;
	case 40: return 65;
	case 90: return 15;
	default: return 6;
}

weight "item_health_mega" switch (29) {
	case 0: return 100;
	case 80: return 70;
	case 120: return 25;
	default: return 10;
}

weight "item_armor_small" switch (1) {
	case 0: return 55;
	case 50: return 30;
	case 100: return 12;
	default: return 6;
}

weight "item_armor_small_mp" switch (1) {
	case 0: return 55;
	case 50: return 30;
	case 100: return 12;
	default: return 6;
}

weight "item_armor_large" switch (1) {
	case 0: return 85;
	case 50: return 60;
	case 125: return 20;
	default: return 8;
}

weight "item_armor_large_mp" switch (1) {
	case 0: return 85;
	case 50: return 60;
	case 125: return 20;
	default: return 8;
}
