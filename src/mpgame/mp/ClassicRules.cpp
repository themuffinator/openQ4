//----------------------------------------------------------------
// ClassicRules.cpp
//
// openQ4 Classic multiplayer ruleset (si_ruleset 1).  See ClassicRules.h.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "ClassicRules.h"

/*
===============================================================================

	Entity definition overlay.

	Quake 4 keeps its weapon, ammo and item numbers in entity definitions, and
	the game copies a definition's dictionary when it spawns the entity.  The
	Classic ruleset writes its numbers into those dictionaries when a Classic
	map loads, before anything spawns, and puts the shipped values back when
	the map unloads or the game module shuts down.  Server and clients apply
	the same table from the same server info, so predicted weapon timing and
	spread agree on both ends.

	Names are resolved the way the game resolves them (FindEntityDef tries the
	"_mp" twin first), so a row names the definition the game will actually
	read.  A NULL value deletes the key.

	The numbers lean on Quake 2's deathmatch values without copying them
	slavishly; where Quake 4's content or engine pulls the other way the row
	says so.

===============================================================================
*/

typedef struct classicDefPatch_s {
	const char *	def;
	const char *	key;
	const char *	value;
} classicDefPatch_t;

static const classicDefPatch_t classicDefPatches[] = {
	// ---- Player: spawn with the blaster alone, 100 health, Quake 2 ammo limits ----
	// The blaster takes the gauntlet's slot, which is where Quake 4's own
	// single-player definition keeps it.
	{ "player_marine_mp",			"def_weapon0",					"weapon_blaster" },
	{ "player_marine_mp",			"weapon",						"weapon_blaster" },
	{ "player_marine_mp",			"current_weapon",				"0" },
	{ "player_marine_mp",			"health",						"100" },
	{ "player_marine_mp",			"ammo_machinegun",				"0" },
	{ "player_marine_mp",			"max_ammo_machinegun",			"200" },	// bullets, shared with the nailgun
	{ "player_marine_mp",			"max_ammo_shotgun",				"100" },	// shells
	{ "player_marine_mp",			"max_ammo_hyperblaster",		"200" },	// cells, shared with the lightning gun and DMG
	{ "player_marine_mp",			"max_ammo_grenadelauncher",		"50" },
	{ "player_marine_mp",			"max_ammo_rocketlauncher",		"50" },
	{ "player_marine_mp",			"max_ammo_railgun",				"50" },		// slugs

	// ---- Blaster: a 15 damage bolt twice a second, fired on the press ----
	{ "weapon_blaster",				"fireRate",						"0.5" },
	{ "weapon_blaster",				"fire_on_press",				"1" },
	{ "weapon_blaster",				"def_projectile",				"projectile_blaster" },
	{ "weapon_blaster",				"def_hitscan",					NULL },
	{ "weapon_blaster",				"spread",						"0" },
	{ "projectile_blaster",			"speed",						"1000" },
	{ "damage_blaster",				"damage",						"15" },
	{ "damage_blaster",				"knockback",					"8" },
	{ "damage_blaster",				"classic_energy",				"1" },

	// ---- Machinegun: 8 damage, ten rounds a second, wider spray ----
	{ "weapon_machinegun_mp",		"spread",						"350" },
	{ "weapon_machinegun_mp",		"inv_start_ammo_machinegun",	NULL },
	{ "weapon_machinegun_mp",		"inv_ammo_machinegun",			"50" },
	{ "weapon_machinegun_mp",		"respawn",						"30" },
	{ "weapon_machinegun_mp",		"respawn_Team DM",				NULL },
	{ "weapon_machinegun_mp",		"respawn_Tourney",				NULL },
	{ "damage_machinegun_mp",		"damage",						"8" },
	{ "damage_machinegun_mp",		"damage_flesh",					"8" },
	{ "damage_machinegun_mp",		"damage_team",					NULL },

	// ---- Shotgun plays the super shotgun: 20 pellets of 6, two shells a blast ----
	{ "weapon_shotgun_mp",			"hitscans",						"20" },
	{ "weapon_shotgun_mp",			"spread",						"1150" },
	{ "weapon_shotgun_mp",			"ammoRequired",					"2" },
	{ "weapon_shotgun_mp",			"fireRate",						"1.0" },
	{ "weapon_shotgun_mp",			"inv_start_ammo_shotgun",		NULL },
	{ "weapon_shotgun_mp",			"inv_ammo_shotgun",				"10" },
	{ "weapon_shotgun_mp",			"respawn",						"30" },
	{ "weapon_shotgun_mp",			"respawn_Team DM",				NULL },
	{ "weapon_shotgun_mp",			"respawn_Tourney",				NULL },
	{ "damage_pellet",				"damage",						"6" },
	{ "damage_pellet",				"knockback",					"6" },

	// ---- Hyperblaster: 15 damage bolts at ten a second, no splash ----
	{ "weapon_hyperblaster_mp",		"fireRate",						"0.1" },
	{ "weapon_hyperblaster_mp",		"inv_start_ammo_hyperblaster",	NULL },
	{ "weapon_hyperblaster_mp",		"inv_ammo_hyperblaster",		"50" },
	{ "weapon_hyperblaster_mp",		"respawn",						"30" },
	{ "weapon_hyperblaster_mp",		"respawn_Team DM",				NULL },
	{ "weapon_hyperblaster_mp",		"respawn_Tourney",				NULL },
	{ "projectile_hyperblaster",	"speed",						"1000" },
	{ "projectile_hyperblaster",	"def_splash_damage",			NULL },
	{ "damage_hyperblaster",		"damage",						"15" },
	{ "damage_hyperblaster",		"classic_energy",				"1" },

	// ---- Grenade launcher: 120 damage over 160 units, 2.5 second fuse ----
	{ "weapon_grenadelauncher_mp",	"fireRate",						"1.0" },
	{ "weapon_grenadelauncher_mp",	"inv_start_ammo_grenadelauncher", NULL },
	{ "weapon_grenadelauncher_mp",	"inv_ammo_grenadelauncher",		"5" },
	{ "weapon_grenadelauncher_mp",	"respawn",						"30" },
	{ "weapon_grenadelauncher_mp",	"respawn_Team DM",				NULL },
	{ "weapon_grenadelauncher_mp",	"respawn_Tourney",				NULL },
	{ "projectile_grenade",			"speed",						"600" },
	{ "projectile_grenade",			"fuse",							"2.5" },
	{ "damage_grenadeDirect",		"damage",						"120" },
	{ "damage_grenadeSplash",		"damage",						"120" },
	{ "damage_grenadeSplash",		"radius",						"160" },

	// ---- Nailgun plays the chaingun: fast, light, and fed from the bullet box ----
	// It stays a nail launcher (Quake 4's model and effects), firing quicker
	// and lighter so it takes the chaingun's place in the arsenal.
	{ "weapon_nailgun_mp",			"fireRate",						"0.08" },
	{ "weapon_nailgun_mp",			"ammoType",						"ammo_machinegun" },
	{ "weapon_nailgun_mp",			"inv_start_ammo_nailgun",		NULL },
	{ "weapon_nailgun_mp",			"inv_ammo_machinegun",			"50" },
	{ "weapon_nailgun_mp",			"respawn",						"30" },
	{ "weapon_nailgun_mp",			"respawn_Team DM",				NULL },
	{ "weapon_nailgun_mp",			"respawn_Tourney",				NULL },
	{ "projectile_nail",			"speed",						"2000" },
	{ "projectile_nail",			"def_splash_damage",			NULL },
	{ "damage_nailDirect",			"damage",						"10" },
	{ "damage_nailDirect",			"knockback",					"10" },

	// ---- Rocket launcher: 650 units a second, 110 on a hit, 120 over 120 units ----
	{ "weapon_rocketlauncher_mp",	"fireRate",						"0.8" },
	{ "weapon_rocketlauncher_mp",	"inv_start_ammo_rocketlauncher", NULL },
	{ "weapon_rocketlauncher_mp",	"inv_ammo_rocketlauncher",		"5" },
	{ "weapon_rocketlauncher_mp",	"respawn",						"30" },
	{ "weapon_rocketlauncher_mp",	"respawn_Team DM",				NULL },
	{ "weapon_rocketlauncher_mp",	"respawn_Tourney",				NULL },
	{ "projectile_rocket",			"speed",						"650" },
	{ "damage_rocketDirect",		"damage",						"110" },	// Quake 2 rolls 100-120
	{ "damage_rocketSplash",		"damage",						"120" },
	{ "damage_rocketSplash",		"radius",						"120" },

	// ---- Railgun: 100 damage through everyone in the line, every 1.5 seconds ----
	{ "weapon_railgun_mp",			"fireRate",						"1.5" },
	{ "weapon_railgun_mp",			"inv_start_ammo_railgun",		NULL },
	{ "weapon_railgun_mp",			"inv_ammo_railgun",				"10" },
	{ "weapon_railgun_mp",			"respawn",						"30" },
	{ "weapon_railgun_mp",			"respawn_Team DM",				NULL },
	{ "weapon_railgun_mp",			"respawn_Tourney",				NULL },
	{ "damage_railgun",				"damage",						"100" },

	// ---- Lightning gun: no Quake 2 twin; keeps its numbers, runs on cells ----
	{ "weapon_lightninggun_mp",		"ammoType",						"ammo_hyperblaster" },
	{ "weapon_lightninggun_mp",		"inv_start_ammo_lightninggun",	NULL },
	{ "weapon_lightninggun_mp",		"inv_ammo_hyperblaster",		"50" },
	{ "weapon_lightninggun_mp",		"respawn",						"30" },
	{ "weapon_lightninggun_mp",		"respawn_Team DM",				NULL },
	{ "weapon_lightninggun_mp",		"respawn_Tourney",				NULL },
	{ "damage_lightninggun",		"classic_energy",				"1" },

	// ---- Dark Matter Gun plays the BFG: 50 cells a shot, 200 damage over a huge radius ----
	{ "weapon_dmg_mp",				"ammoType",						"ammo_hyperblaster" },
	{ "weapon_dmg_mp",				"ammoRequired",					"50" },
	{ "weapon_dmg_mp",				"fireRate",						"2.5" },
	{ "weapon_dmg_mp",				"inv_start_ammo_dmg",			NULL },
	{ "weapon_dmg_mp",				"inv_ammo_hyperblaster",		"50" },
	{ "weapon_dmg_mp",				"respawn",						"30" },
	{ "weapon_dmg_mp",				"respawn_Team DM",				NULL },
	{ "weapon_dmg_mp",				"respawn_Tourney",				NULL },
	{ "projectile_dmg",				"speed",						"400" },
	{ "damage_dmg",					"damage",						"200" },
	{ "damage_dmg",					"classic_energy",				"1" },
	{ "damage_dmg_splash",			"damage",						"200" },
	{ "damage_dmg_splash",			"radius",						"1000" },
	{ "damage_dmg_splash",			"classic_energy",				"1" },

	// ---- Ammo: Quake 2 box sizes, 30 second respawn ----
	{ "ammo_machinegun_mp",			"inv_ammo_machinegun",			"50" },
	{ "ammo_machinegun_mp",			"respawn",						"30" },
	{ "ammo_nailgun_mp",			"inv_ammo_nailgun",				NULL },
	{ "ammo_nailgun_mp",			"inv_ammo_machinegun",			"50" },
	{ "ammo_nailgun_mp",			"respawn",						"30" },
	{ "ammo_shotgun_mp",			"inv_ammo_shotgun",				"10" },
	{ "ammo_shotgun_mp",			"respawn",						"30" },
	{ "ammo_hyperblaster_mp",		"inv_ammo_hyperblaster",		"50" },
	{ "ammo_hyperblaster_mp",		"respawn",						"30" },
	{ "ammo_lightninggun_mp",		"inv_ammo_lightninggun",		NULL },
	{ "ammo_lightninggun_mp",		"inv_ammo_hyperblaster",		"50" },
	{ "ammo_lightninggun_mp",		"respawn",						"30" },
	{ "ammo_dmg_mp",				"inv_ammo_dmg",					NULL },
	{ "ammo_dmg_mp",				"inv_ammo_hyperblaster",		"50" },
	{ "ammo_dmg_mp",				"respawn",						"30" },
	{ "ammo_grenadelauncher_mp",	"inv_ammo_grenadelauncher",		"5" },
	{ "ammo_grenadelauncher_mp",	"respawn",						"30" },
	{ "ammo_rocketlauncher_mp",		"inv_ammo_rocketlauncher",		"5" },
	{ "ammo_rocketlauncher_mp",		"respawn",						"30" },
	{ "ammo_railgun_mp",			"inv_ammo_railgun",				"10" },
	{ "ammo_railgun_mp",			"respawn",						"30" },

	// ---- Health: stimpack +2 past the limit, +10, +25, timed mega health ----
	{ "item_health_shard_mp",		"inv_bonushealth",				"2" },
	{ "item_health_shard_mp",		"respawn",						"30" },
	{ "item_health_small_mp",		"inv_health",					"10" },
	{ "item_health_small_mp",		"respawn",						"30" },
	{ "item_health_large_mp",		"inv_health",					"25" },
	{ "item_health_large_mp",		"respawn",						"30" },
	{ "item_health_mega",			"inv_bonushealth",				NULL },
	{ "item_health_mega",			"inv_megahealth",				"100" },
	{ "item_health_mega",			"respawn",						"20" },		// after the holder is back to 100

	// ---- Armour: shards, combat (yellow) and body (red) armour ----
	// Quake 4 has two armour pickups and Quake 2 three; the small one plays
	// combat armour and the large one body armour.  Jacket armour is what a
	// shard gives a player who has none.
	{ "item_armor_shard_mp",		"inv_armor",					NULL },
	{ "item_armor_shard_mp",		"inv_armor_tier",				"shard" },
	{ "item_armor_shard_mp",		"respawn",						"20" },
	{ "item_armor_small_mp",		"inv_armor",					NULL },
	{ "item_armor_small_mp",		"inv_armor_tier",				"combat" },
	{ "item_armor_small_mp",		"respawn",						"20" },
	{ "item_armor_large_mp",		"inv_armor",					NULL },
	{ "item_armor_large_mp",		"inv_armor_tier",				"body" },
	{ "item_armor_large_mp",		"respawn",						"20" },

	// ---- Quad damage comes back every minute ----
	{ "powerup_quad_damage",		"respawn",						"60" },
};

typedef struct classicAppliedPatch_s {
	idStr				key;
	idStr				applied;
	bool				deleted;
} classicAppliedPatch_t;

// One definition the overlay rewrote: its shipped dictionary, flattened to the
// text the engine parses it back from, and the rows written into it.
typedef struct classicAppliedDecl_s {
	idDeclEntityDef *				decl;
	idStr							original;
	idList<classicAppliedPatch_t>	rows;
} classicAppliedDecl_t;

static idList<classicAppliedDecl_t>		classicApplied;
static int								classicAppliedRows = 0;
static bool								classicDefinitionsApplied = false;

/*
================
ClassicDictText

A resolved entityDef dictionary as entityDef text: its "inherit" keys were
consumed when it was parsed, so the flat key list parses back to the same
dictionary.  Declaration strings carry no escapes, so quoting is enough.
================
*/
static void ClassicDictText( const idDict &dict, idStr &text ) {
	text = "{\n";
	for ( int i = 0; i < dict.GetNumKeyVals(); i++ ) {
		const idKeyValue *kv = dict.GetKeyVal( i );
		text += "\t\"";
		text += kv->GetKey();
		text += "\" \"";
		text += kv->GetValue();
		text += "\"\n";
	}
	text += "}\n";
}

/*
================
ClassicReparse

Replaces a definition's dictionary with the one the text describes.  The
dictionary belongs to the engine's decl manager and its strings to the engine's
string pools, so it is cleared and rebuilt by the engine's own FreeData and
Parse, never through the game module's idDict code: that would mix the two
modules' pools and heaps, and the engine would later free strings it no longer
counted - which crashed the client on quit after any Classic map.
================
*/
static void ClassicReparse( idDeclEntityDef *decl, const idStr &text ) {
	decl->FreeData();
	decl->Parse( text.c_str(), text.Length(), true );
}

/*
================
rvClassicRules::RulesetFromServerInfo
================
*/
int rvClassicRules::RulesetFromServerInfo( const idDict &serverInfo ) {
	const int ruleset = serverInfo.GetInt( "si_ruleset", "0" );
	if ( ruleset < 0 || ruleset >= MP_RULESET_COUNT ) {
		return MP_RULESET_QUAKE4;
	}
	return ruleset;
}

/*
================
rvClassicRules::RulesetName
================
*/
const char *rvClassicRules::RulesetName( int ruleset ) {
	return ( ruleset == MP_RULESET_CLASSIC ) ? "Classic" : "Quake 4";
}

/*
================
rvClassicRules::ApplyDefinitions
================
*/
void rvClassicRules::ApplyDefinitions( void ) {
	if ( classicDefinitionsApplied ) {
		return;
	}

	classicApplied.Clear();
	classicApplied.SetGranularity( 32 );
	classicAppliedRows = 0;

	// Gather the rows by definition, in table order.
	int missing = 0;
	const int patchCount = (int)( sizeof( classicDefPatches ) / sizeof( classicDefPatches[ 0 ] ) );
	for ( int i = 0; i < patchCount; i++ ) {
		const classicDefPatch_t &patch = classicDefPatches[ i ];
		const idDeclEntityDef *def = gameLocal.FindEntityDef( patch.def, false );
		if ( def == NULL ) {
			missing++;
			gameLocal.DPrintf( "Classic ruleset: no entity definition '%s'\n", patch.def );
			continue;
		}

		idDeclEntityDef *editable = const_cast<idDeclEntityDef *>( def );
		classicAppliedDecl_t *entry = NULL;
		for ( int j = 0; j < classicApplied.Num(); j++ ) {
			if ( classicApplied[ j ].decl == editable ) {
				entry = &classicApplied[ j ];
				break;
			}
		}
		if ( entry == NULL ) {
			entry = &classicApplied.Alloc();
			entry->decl = editable;
			entry->rows.Clear();
			ClassicDictText( editable->dict, entry->original );
		}

		classicAppliedPatch_t &row = entry->rows.Alloc();
		row.key = patch.key;
		row.deleted = ( patch.value == NULL );
		row.applied = patch.value ? patch.value : "";
		classicAppliedRows++;
	}

	// Rewrite each definition once.  The patched copy is a game-side dictionary,
	// so its edits stay in this module's pools; only its text reaches the engine.
	for ( int i = 0; i < classicApplied.Num(); i++ ) {
		classicAppliedDecl_t &entry = classicApplied[ i ];
		idDict patched;
		patched.Copy( entry.decl->dict );
		for ( int j = 0; j < entry.rows.Num(); j++ ) {
			const classicAppliedPatch_t &row = entry.rows[ j ];
			if ( row.deleted ) {
				patched.Delete( row.key );
			} else {
				patched.Set( row.key, row.applied );
			}
		}
		idStr text;
		ClassicDictText( patched, text );
		ClassicReparse( entry.decl, text );
	}

	classicDefinitionsApplied = true;
	gameLocal.Printf( "Classic ruleset: %d definition values applied%s\n", classicAppliedRows,
		missing ? va( ", %d definitions missing", missing ) : "" );
}

/*
================
rvClassicRules::RevertDefinitions

Puts the shipped definitions back.  A definition whose values no longer read as
the ones written was re-parsed since (reloadDecls) and is left alone.
================
*/
void rvClassicRules::RevertDefinitions( void ) {
	if ( !classicDefinitionsApplied ) {
		return;
	}

	for ( int i = classicApplied.Num() - 1; i >= 0; i-- ) {
		classicAppliedDecl_t &entry = classicApplied[ i ];
		const idDict &dict = entry.decl->dict;
		bool stillOurs = true;
		// the last row on a key is the value it holds
		for ( int j = entry.rows.Num() - 1; j >= 0 && stillOurs; j-- ) {
			const classicAppliedPatch_t &row = entry.rows[ j ];
			bool laterRow = false;
			for ( int k = j + 1; k < entry.rows.Num(); k++ ) {
				if ( !entry.rows[ k ].key.Icmp( row.key ) ) {
					laterRow = true;
					break;
				}
			}
			if ( laterRow ) {
				continue;
			}
			const idKeyValue *kv = dict.FindKey( row.key );
			stillOurs = row.deleted ? ( kv == NULL ) : ( kv != NULL && kv->GetValue() == row.applied );
		}
		if ( stillOurs ) {
			ClassicReparse( entry.decl, entry.original );
		}
	}

	classicApplied.Clear();
	classicAppliedRows = 0;
	classicDefinitionsApplied = false;
}

/*
================
rvClassicRules::DefinitionsApplied
================
*/
bool rvClassicRules::DefinitionsApplied( void ) {
	return classicDefinitionsApplied;
}

/*
===============================================================================

	Grappling hook availability.

===============================================================================
*/

/*
================
rvClassicRules::GrappleAllowed
================
*/
bool rvClassicRules::GrappleAllowed( void ) {
	if ( !gameLocal.isMultiplayer || !gameLocal.IsClassicRuleset() ) {
		return false;
	}
	const int mode = gameLocal.serverInfo.GetInt( "si_classicGrapple", "1" );
	if ( mode == CLASSIC_GRAPPLE_ALL ) {
		return true;
	}
	return mode == CLASSIC_GRAPPLE_CTF && gameLocal.IsFlagGameType();
}

/*
===============================================================================

	Tiered armour.

	Picking up a better armour converts what is left of the old one at the
	ratio of their protection; picking up a weaker one tops up the current
	armour by its worth in the current armour's terms, and is refused when that
	adds nothing.  A shard adds two points to whatever is worn, or makes two
	points of jacket armour.

===============================================================================
*/

typedef struct classicArmorInfo_s {
	const char *	name;
	int				baseCount;
	int				maxCount;
	float			normalProtection;
	float			energyProtection;
} classicArmorInfo_t;

static const classicArmorInfo_t classicArmor[ CLASSIC_ARMOR_TIERS ] = {
	{ "none",		0,		0,		0.0f,	0.0f },
	{ "jacket",		25,		50,		0.30f,	0.0f },
	{ "combat",		50,		100,	0.60f,	0.30f },
	{ "body",		100,	200,	0.80f,	0.60f },
};

static const int CLASSIC_SHARD_ARMOR = 2;

/*
================
rvClassicRules::EffectiveArmorTier

Armour handed out outside the tier system - Clan Arena's round loadout,
"give armor" - has no tier of its own and counts as combat armour.
================
*/
int rvClassicRules::EffectiveArmorTier( int tier, int armor ) {
	if ( armor <= 0 ) {
		return CLASSIC_ARMOR_NONE;
	}
	if ( tier <= CLASSIC_ARMOR_NONE || tier >= CLASSIC_ARMOR_TIERS ) {
		return CLASSIC_ARMOR_COMBAT;
	}
	return tier;
}

/*
================
rvClassicRules::ArmorTierName
================
*/
const char *rvClassicRules::ArmorTierName( int tier ) {
	if ( tier < 0 || tier >= CLASSIC_ARMOR_TIERS ) {
		tier = CLASSIC_ARMOR_NONE;
	}
	return classicArmor[ tier ].name;
}

/*
================
rvClassicRules::GiveArmor
================
*/
bool rvClassicRules::GiveArmor( idPlayer *player, const char *tierName ) {
	if ( player == NULL || tierName == NULL ) {
		return false;
	}

	idInventory &inv = player->inventory;
	const int oldTier = EffectiveArmorTier( inv.armorTier, inv.armor );

	if ( !idStr::Icmp( tierName, "shard" ) ) {
		if ( inv.armor >= CLASSIC_ARMOR_CAP ) {
			return false;
		}
		if ( oldTier == CLASSIC_ARMOR_NONE ) {
			inv.armorTier = CLASSIC_ARMOR_JACKET;
			inv.armor = CLASSIC_SHARD_ARMOR;
		} else {
			inv.armor = Min( inv.armor + CLASSIC_SHARD_ARMOR, CLASSIC_ARMOR_CAP );
		}
		return true;
	}

	int newTier = CLASSIC_ARMOR_NONE;
	for ( int i = CLASSIC_ARMOR_JACKET; i < CLASSIC_ARMOR_TIERS; i++ ) {
		if ( !idStr::Icmp( tierName, classicArmor[ i ].name ) ) {
			newTier = i;
			break;
		}
	}
	if ( newTier == CLASSIC_ARMOR_NONE ) {
		gameLocal.Warning( "Classic ruleset: unknown armour tier '%s'", tierName );
		return false;
	}

	const classicArmorInfo_t &newInfo = classicArmor[ newTier ];
	if ( oldTier == CLASSIC_ARMOR_NONE ) {
		inv.armorTier = newTier;
		inv.armor = newInfo.baseCount;
		return true;
	}

	const classicArmorInfo_t &oldInfo = classicArmor[ oldTier ];
	if ( newInfo.normalProtection > oldInfo.normalProtection ) {
		// upgrade: the old armour is worth less in the new armour's terms
		const int salvage = (int)( oldInfo.normalProtection / newInfo.normalProtection * (float)inv.armor );
		inv.armor = Min( newInfo.baseCount + salvage, newInfo.maxCount );
		inv.armorTier = newTier;
		return true;
	}

	// same or weaker: top the current armour up by the pickup's worth
	const int salvage = (int)( newInfo.normalProtection / oldInfo.normalProtection * (float)newInfo.baseCount );
	const int newCount = Min( inv.armor + salvage, oldInfo.maxCount );
	if ( inv.armor >= newCount ) {
		return false;
	}
	inv.armor = newCount;
	return true;
}

/*
================
rvClassicRules::AbsorbDamage

Returns the points the armour takes.  Unlike Quake 4, armour may soak a hit
completely; energy weapons get through combat and body armour more easily
and through jacket armour untouched.
================
*/
int rvClassicRules::AbsorbDamage( int tier, int armor, int damage, bool energy ) {
	tier = EffectiveArmorTier( tier, armor );
	if ( tier == CLASSIC_ARMOR_NONE || damage <= 0 ) {
		return 0;
	}
	const float protection = energy ? classicArmor[ tier ].energyProtection : classicArmor[ tier ].normalProtection;
	int save = (int)idMath::Ceil( protection * (float)damage );
	if ( save > armor ) {
		save = armor;
	}
	if ( save > damage ) {
		save = damage;
	}
	return save;
}

/*
===============================================================================

	Threewave CTF scoring.

	Quake 2's CTF points, as Threewave CTF awarded them: a capture is worth 15
	to the carrier and 10 to each team mate, fragging the enemy carrier 2,
	returning a flag 1, plus small defence bonuses for guarding the flag or the
	carrier and assists when a capture follows a return or a carrier frag.  The
	points go to the CTF score (teamFragCount), where Quake 4 keeps its own
	flag awards, so the scoreboard and the frag count still separate them.

===============================================================================
*/

static const int	CLASSIC_CTF_CAPTURE_BONUS				= 15;
static const int	CLASSIC_CTF_TEAM_BONUS					= 10;
static const int	CLASSIC_CTF_RECOVERY_BONUS				= 1;
static const int	CLASSIC_CTF_FRAG_CARRIER_BONUS			= 2;
static const int	CLASSIC_CTF_CARRIER_DANGER_PROTECT_BONUS	= 2;
static const int	CLASSIC_CTF_CARRIER_PROTECT_BONUS		= 1;
static const int	CLASSIC_CTF_FLAG_DEFENSE_BONUS			= 1;
static const int	CLASSIC_CTF_RETURN_FLAG_ASSIST_BONUS		= 1;
static const int	CLASSIC_CTF_FRAG_CARRIER_ASSIST_BONUS	= 2;

static const float	CLASSIC_CTF_TARGET_PROTECT_RADIUS		= 400.0f;
static const float	CLASSIC_CTF_ATTACKER_PROTECT_RADIUS		= 400.0f;

static const int	CLASSIC_CTF_CARRIER_DANGER_PROTECT_TIMEOUT	= 8000;
static const int	CLASSIC_CTF_FRAG_CARRIER_ASSIST_TIMEOUT		= 10000;
static const int	CLASSIC_CTF_RETURN_FLAG_ASSIST_TIMEOUT		= 10000;

typedef struct classicCtfClient_s {
	int		lastHurtCarrier;		// this player hurt the other team's flag carrier
	int		lastReturnedFlag;
	int		lastFraggedCarrier;
} classicCtfClient_t;

static classicCtfClient_t classicCtf[ MAX_CLIENTS ];

static bool ClassicCtfActive( void ) {
	return gameLocal.isMultiplayer && !gameLocal.isClient && gameLocal.IsClassicRuleset() && gameLocal.IsFlagGameType();
}

static bool CarriesFlag( const idPlayer *player ) {
	if ( player == NULL ) {
		return false;
	}
	idPlayer *p = const_cast<idPlayer *>( player );
	return p->PowerUpActive( POWERUP_CTF_MARINEFLAG ) || p->PowerUpActive( POWERUP_CTF_STROGGFLAG ) || p->PowerUpActive( POWERUP_CTF_ONEFLAG );
}

static bool ClientValid( const idPlayer *player ) {
	return player != NULL && player->entityNumber >= 0 && player->entityNumber < MAX_CLIENTS;
}

static bool CanSee( const idEntity *from, const idVec3 &start, const idVec3 &end ) {
	trace_t tr;
	gameLocal.TracePoint( from, tr, start, end, MASK_SOLID, from );
	return tr.fraction >= 1.0f;
}

static bool Near( const idEntity *a, const idEntity *b, float radius ) {
	if ( a == NULL || b == NULL ) {
		return false;
	}
	const idVec3 pa = a->GetPhysics()->GetOrigin();
	const idVec3 pb = b->GetPhysics()->GetOrigin();
	if ( ( pa - pb ).LengthSqr() > radius * radius ) {
		return false;
	}
	return CanSee( a, pa + idVec3( 0, 0, 32 ), pb + idVec3( 0, 0, 32 ) );
}

/*
================
rvClassicRules::ResetMatchState
================
*/
void rvClassicRules::ResetMatchState( void ) {
	memset( classicCtf, 0, sizeof( classicCtf ) );
}

/*
================
rvClassicRules::ResetClient
================
*/
void rvClassicRules::ResetClient( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		memset( &classicCtf[ clientNum ], 0, sizeof( classicCtf[ clientNum ] ) );
	}
}

/*
================
rvClassicRules::NoteDamage
================
*/
void rvClassicRules::NoteDamage( idPlayer *victim, idPlayer *attacker ) {
	if ( !ClassicCtfActive() || !ClientValid( victim ) || !ClientValid( attacker ) || victim == attacker ) {
		return;
	}
	if ( victim->team != attacker->team && CarriesFlag( victim ) ) {
		classicCtf[ attacker->entityNumber ].lastHurtCarrier = gameLocal.time;
	}
}

/*
================
rvClassicRules::AwardFragBonuses
================
*/
bool rvClassicRules::AwardFragBonuses( idPlayer *victim, idPlayer *killer ) {
	if ( !ClassicCtfActive() ) {
		return false;
	}
	if ( !ClientValid( victim ) || !ClientValid( killer ) || victim == killer || victim->team == killer->team ) {
		return true;	// nothing to award, and the stock carrier bonus does not apply either
	}

	idMultiplayerGame &mp = gameLocal.mpGame;

	// fragging the enemy flag carrier
	if ( CarriesFlag( victim ) ) {
		mp.AddPlayerTeamScore( killer, CLASSIC_CTF_FRAG_CARRIER_BONUS );
		classicCtf[ killer->entityNumber ].lastFraggedCarrier = gameLocal.time;
		// the danger to the carrier is over for everyone on the killer's side
		for ( int i = 0; i < MAX_CLIENTS; i++ ) {
			idPlayer *p = static_cast<idPlayer *>( gameLocal.entities[ i ] );
			if ( p != NULL && p->IsType( idPlayer::GetClassType() ) && p->team == killer->team ) {
				classicCtf[ i ].lastHurtCarrier = 0;
			}
		}
		return true;
	}

	// the victim had just been hurting the killer's carrier
	const int hurt = classicCtf[ victim->entityNumber ].lastHurtCarrier;
	if ( hurt && gameLocal.time - hurt < CLASSIC_CTF_CARRIER_DANGER_PROTECT_TIMEOUT && !CarriesFlag( killer ) ) {
		mp.AddPlayerTeamScore( killer, CLASSIC_CTF_CARRIER_DANGER_PROTECT_BONUS );
		return true;
	}

	// defending the killer's own flag
	idEntity *flag = mp.GetFlagEntity( killer->team );
	if ( flag != NULL && ( Near( victim, flag, CLASSIC_CTF_TARGET_PROTECT_RADIUS ) || Near( killer, flag, CLASSIC_CTF_ATTACKER_PROTECT_RADIUS ) ) ) {
		mp.AddPlayerTeamScore( killer, CLASSIC_CTF_FLAG_DEFENSE_BONUS );
		return true;
	}

	// guarding a team mate who carries the flag
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		idPlayer *carrier = static_cast<idPlayer *>( gameLocal.entities[ i ] );
		if ( carrier == NULL || carrier == killer || !carrier->IsType( idPlayer::GetClassType() ) ||
			carrier->team != killer->team || !CarriesFlag( carrier ) ) {
			continue;
		}
		if ( Near( victim, carrier, CLASSIC_CTF_TARGET_PROTECT_RADIUS ) || Near( killer, carrier, CLASSIC_CTF_ATTACKER_PROTECT_RADIUS ) ) {
			mp.AddPlayerTeamScore( killer, CLASSIC_CTF_CARRIER_PROTECT_BONUS );
		}
		break;
	}
	return true;
}

/*
================
rvClassicRules::AwardFlagPickup

Threewave CTF gives nothing for lifting the enemy flag.
================
*/
bool rvClassicRules::AwardFlagPickup( idPlayer *player ) {
	return ClassicCtfActive() && ClientValid( player );
}

/*
================
rvClassicRules::AwardFlagReturn
================
*/
bool rvClassicRules::AwardFlagReturn( idPlayer *player ) {
	if ( !ClassicCtfActive() || !ClientValid( player ) ) {
		return false;
	}
	gameLocal.mpGame.AddPlayerTeamScore( player, CLASSIC_CTF_RECOVERY_BONUS );
	classicCtf[ player->entityNumber ].lastReturnedFlag = gameLocal.time;
	return true;
}

/*
================
rvClassicRules::AwardCapture
================
*/
bool rvClassicRules::AwardCapture( idPlayer *capturer ) {
	if ( !ClassicCtfActive() || !ClientValid( capturer ) ) {
		return false;
	}

	idMultiplayerGame &mp = gameLocal.mpGame;
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		idPlayer *p = static_cast<idPlayer *>( gameLocal.entities[ i ] );
		if ( p == NULL || !p->IsType( idPlayer::GetClassType() ) || p->spectating || p->team != capturer->team ) {
			continue;
		}
		if ( p == capturer ) {
			mp.AddPlayerTeamScore( p, CLASSIC_CTF_CAPTURE_BONUS );
			continue;
		}
		mp.AddPlayerTeamScore( p, CLASSIC_CTF_TEAM_BONUS );
		if ( classicCtf[ i ].lastReturnedFlag && gameLocal.time - classicCtf[ i ].lastReturnedFlag < CLASSIC_CTF_RETURN_FLAG_ASSIST_TIMEOUT ) {
			mp.AddPlayerTeamScore( p, CLASSIC_CTF_RETURN_FLAG_ASSIST_BONUS );
		}
		if ( classicCtf[ i ].lastFraggedCarrier && gameLocal.time - classicCtf[ i ].lastFraggedCarrier < CLASSIC_CTF_FRAG_CARRIER_ASSIST_TIMEOUT ) {
			mp.AddPlayerTeamScore( p, CLASSIC_CTF_FRAG_CARRIER_ASSIST_BONUS );
		}
	}
	return true;
}
