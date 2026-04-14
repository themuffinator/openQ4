// Initial OpenQ4 multiplayer bot weapon configuration.

projectileinfo {
	name "bullet"
	model ""
	flags 0
	gravity 0
	damage 8
	radius 0
	visdamage 0
	damagetype 1
	healthinc 0
	push 0
	detonation 0
	bounce 0
	bouncefric 0
	bouncestop 0
}

projectileinfo {
	name "shotgun_pellet"
	model ""
	flags 0
	gravity 0
	damage 10
	radius 0
	visdamage 0
	damagetype 1
	healthinc 0
	push 0
	detonation 0
	bounce 0
	bouncefric 0
	bouncestop 0
}

projectileinfo {
	name "hyper"
	model ""
	flags 0
	gravity 0
	damage 20
	radius 0
	visdamage 0
	damagetype 1
	healthinc 0
	push 0
	detonation 0
	bounce 0
	bouncefric 0
	bouncestop 0
}

projectileinfo {
	name "rocket"
	model ""
	flags 0
	gravity 0
	damage 100
	radius 120
	visdamage 0
	damagetype 3
	healthinc 0
	push 0
	detonation 0
	bounce 0
	bouncefric 0
	bouncestop 0
}

weaponinfo {
	number 1
	name "weapon_machinegun"
	level 1
	model "weapon_machinegun_world"
	weaponindex 6
	flags 0
	projectile "bullet"
	numprojectiles 1
	hspread 1.5
	vspread 1.5
	speed 0
}

weaponinfo {
	number 2
	name "weapon_shotgun"
	level 2
	model "weapon_shotgun_world"
	weaponindex 5
	flags 0
	projectile "shotgun_pellet"
	numprojectiles 11
	hspread 12
	vspread 12
	speed 0
}

weaponinfo {
	number 3
	name "weapon_hyperblaster"
	level 3
	model "weapon_hyperblaster_world"
	weaponindex 11
	flags 0
	projectile "hyper"
	numprojectiles 1
	hspread 1
	vspread 1
	speed 1400
}

weaponinfo {
	number 6
	name "weapon_rocketlauncher"
	level 4
	model "weapon_rocketlauncher_world"
	weaponindex 8
	flags 0
	projectile "rocket"
	numprojectiles 1
	hspread 0
	vspread 0
	speed 900
}
