//----------------------------------------------------------------
// Buying.cpp
//
// Copyright 2005 Ritual Entertainment
//
// This file essentially serves as an extension to the Game DLL
// source files Multiplayer.cpp and Player.cpp, in an attempt
// to isolate, as much as possible, these changes from the main
// body of code (for merge simplification, etc).
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "Buying.h"

/*
===============================================================================

	The catalog

	Every item either set of rules sells, in the buy menu's order: weapons and
	their mods by weapon slot, then armour and ammunition, then the team
	specials. A buy travels from client to server as the item's impulse.
	Retail's items keep retail's numbers; the Awakening's take numbers retail
	left unused.

	The Awakening's prices are the expansion's own, which it kept in code. Its
	buy menu named the nailgun's rate-of-fire mod wpmod_nailgun_rof, a def no
	content has; its player def and retail call that mod wpmod_nailgun_power
	(the nailgun_mod_rof part), so that is what it sells here. Two of its mods
	are left out because multiplayer weapons already outdo them: the shotgun's
	10-round clip (the multiplayer shotgun has no clip, and a mod never gives
	a clipless weapon one: rvWeapon::Init) and the railgun's penetration (the
	multiplayer slug already passes through players at full damage, the mod
	at 85 %). Its concussion grenades, core cannon, fire cannon and freeze gun
	and their mods need the expansion's own content.

===============================================================================
*/

const buyCatalogItem_t buyCatalog[] = {
	// item								impulse	category				quake4	awakening	name			detail
	{ "weapon_shotgun",					100,	BUY_CATEGORY_WEAPONS,	true,	500,		"#str_200105",	NULL },
	{ "weapon_hyperblaster",			102,	BUY_CATEGORY_WEAPONS,	true,	700,		"#str_200107",	NULL },
	{ "weapon_grenadelauncher",			103,	BUY_CATEGORY_WEAPONS,	true,	1000,		"#str_200108",	NULL },
	{ "weapon_nailgun",					104,	BUY_CATEGORY_WEAPONS,	true,	900,		"#str_200109",	NULL },
	{ "weapon_rocketlauncher",			105,	BUY_CATEGORY_WEAPONS,	true,	2000,		"#str_200110",	NULL },
	{ "weapon_railgun",					106,	BUY_CATEGORY_WEAPONS,	true,	3000,		"#str_200111",	NULL },
	{ "weapon_lightninggun",			107,	BUY_CATEGORY_WEAPONS,	true,	1500,		"#str_200112",	NULL },
	{ "weapon_dmg",						110,	BUY_CATEGORY_WEAPONS,	false,	5000,		"#str_200113",	NULL },
	{ "weapon_napalmgun",				109,	BUY_CATEGORY_WEAPONS,	true,	0,			"#str_222001",	NULL },

	{ "wpmod_hyperblaster_bounce1",		112,	BUY_CATEGORY_MODS,		false,	8000,		"#str_107432",	"#str_231903" },
	{ "wpmod_nailgun_power",			113,	BUY_CATEGORY_MODS,		false,	5000,		"#str_107436",	"#str_231904" },
	{ "wpmod_rocketlauncher_burst",		117,	BUY_CATEGORY_MODS,		false,	12000,		"#str_231902",	"#str_231906" },
	{ "wpmod_lightninggun_chain",		114,	BUY_CATEGORY_MODS,		false,	6000,		"#str_107434",	"#str_231905" },

	{ "item_armor_small",				118,	BUY_CATEGORY_GEAR,		true,	300,		"#str_222008",	"#str_231907" },
	{ "item_armor_large",				119,	BUY_CATEGORY_GEAR,		true,	700,		"#str_222009",	"#str_231908" },
	{ "ammorefill",						120,	BUY_CATEGORY_GEAR,		true,	500,		"#str_222007",	"#str_231909" },

	{ "ammo_regen",						123,	BUY_CATEGORY_TEAM,		true,	10000,		"#str_122004",	"#str_231910" },
	{ "health_regen",					124,	BUY_CATEGORY_TEAM,		true,	12500,		"#str_122005",	"#str_231911" },
	{ "damage_boost",					125,	BUY_CATEGORY_TEAM,		true,	15000,		"#str_122006",	"#str_231912" },
	{ "fc_armor_regen",					126,	BUY_CATEGORY_TEAM,		false,	3000,		"#str_122007",	"#str_231913" },
};
const int buyCatalogCount = sizeof( buyCatalog ) / sizeof( buyCatalog[0] );

/*
==============
MP_FindBuyCatalogItem
==============
*/
const buyCatalogItem_t *MP_FindBuyCatalogItem( const char *item ) {
	if ( item == NULL ) {
		return NULL;
	}
	for ( int i = 0; i < buyCatalogCount; i++ ) {
		if ( !idStr::Icmp( buyCatalog[ i ].item, item ) ) {
			return &buyCatalog[ i ];
		}
	}
	return NULL;
}

/*
==============
MP_FindBuyCatalogItemForImpulse
==============
*/
const buyCatalogItem_t *MP_FindBuyCatalogItemForImpulse( int impulse ) {
	for ( int i = 0; i < buyCatalogCount; i++ ) {
		if ( buyCatalog[ i ].impulse == impulse ) {
			return &buyCatalog[ i ];
		}
	}
	return NULL;
}

/*
==============
MP_BuyingModeFromServerInfo
==============
*/
int MP_BuyingModeFromServerInfo( const idDict &serverInfo ) {
	if ( !serverInfo.GetBool( "si_isBuyingEnabled" ) ) {
		return BUYING_MODE_OFF;
	}
	return serverInfo.GetInt( "si_buyingRules" ) == BUYING_RULES_AWAKENING ? BUYING_MODE_AWAKENING : BUYING_MODE_QUAKE4;
}

/*
==============
MP_BuyingModeFromCVars
==============
*/
int MP_BuyingModeFromCVars( void ) {
	if ( !si_isBuyingEnabled.GetBool() ) {
		return BUYING_MODE_OFF;
	}
	return si_buyingRules.GetInteger() == BUYING_RULES_AWAKENING ? BUYING_MODE_AWAKENING : BUYING_MODE_QUAKE4;
}

/*
==============
MP_SetBuyingModeCVars

Off leaves the rules as they were, so turning buying back on sells by them.
==============
*/
void MP_SetBuyingModeCVars( int mode ) {
	mode = idMath::ClampInt( BUYING_MODE_OFF, BUYING_MODE_COUNT - 1, mode );
	si_isBuyingEnabled.SetBool( mode != BUYING_MODE_OFF );
	if ( mode != BUYING_MODE_OFF ) {
		si_buyingRules.SetInteger( mode == BUYING_MODE_AWAKENING ? BUYING_RULES_AWAKENING : BUYING_RULES_QUAKE4 );
	}
}

/*
==============
MP_BuyingModeFromOnOff
==============
*/
int MP_BuyingModeFromOnOff( int on, int currentMode ) {
	if ( !on ) {
		return BUYING_MODE_OFF;
	}
	return currentMode != BUYING_MODE_OFF ? currentMode : BUYING_MODE_QUAKE4;
}

/*
==============
MP_BuyingModeName
==============
*/
const char *MP_BuyingModeName( int mode ) {
	switch ( mode ) {
		case BUYING_MODE_QUAKE4:	return "#str_41584";
		case BUYING_MODE_AWAKENING:	return "#str_231900";
		default:					return "#str_41200";
	}
}

/*
==============
MP_BuyingVoteLine
==============
*/
idStr MP_BuyingVoteLine( int mode ) {
	const char *value;
	switch ( mode ) {
		case BUYING_MODE_QUAKE4:	value = "#str_230039"; break;
		case BUYING_MODE_AWAKENING:	value = "#str_230040"; break;
		default:					value = "#str_41121"; break;
	}
	return va( common->GetLocalizedString( "#str_231920" ), common->GetLocalizedString( value ) );
}

/*
===============================================================================

	The Awakening's cash awards

	From the expansion's BuyingGameBalanceConstants. It pays more for a flag
	and less for a kill, takes nothing for a team kill, and pays both teams at
	the end of a round. What it does not set, the content's constants pay as
	under the Quake 4 rules.

===============================================================================
*/

typedef struct buyingConstant_s {
	const char *	key;
	int				value;
} buyingConstant_t;

static const buyingConstant_t awakeningBalanceConstants[] = {
	{ "teamCashAward_flagCapture",					1000 },
	{ "teamCashAward_gameModeWin",					3000 },
	{ "teamCashAward_gameModeTie",					2000 },
	{ "teamCashAward_gameModeLoss",					2000 },
	{ "playerCashAward_flagCapture",				1000 },
	{ "playerCashAward_flagReturned",				500 },
	{ "playerCashAward_flagStolen",					200 },
	{ "playerCashAward_killingOpponent",			500 },
	{ "playerCashAward_killingTeammate",			0 },
	{ "playerCashAward_killingOpponentFlagCarrier",	500 },
	{ "ammo_rocketlauncher",						20 },
	{ "ammo_dmg",									8 },
};

riBuyingManager::riBuyingManager() :
	_buyingGameBalanceConstants( NULL ),
	_itemCostConstants( NULL ),
	opponentKillCashAward( 0 ),
	opponentKillFragCount( -1 ),
	opponentKillRules( -1 ) { }

riBuyingManager::~riBuyingManager() { }

/*
==============
riBuyingManager::FindConstants
==============
*/
const idDeclEntityDef *riBuyingManager::FindConstants( const idDeclEntityDef *&constants, const char *name ) {
	if ( constants == NULL ) {
		constants = static_cast<const idDeclEntityDef*>( declManager->FindType( DECL_ENTITYDEF, name, false ) );
	}
	return constants;
}

/*
==============
riBuyingManager::GetRules
==============
*/
int riBuyingManager::GetRules( void ) const {
	return gameLocal.serverInfo.GetInt( "si_buyingRules" ) == BUYING_RULES_AWAKENING ? BUYING_RULES_AWAKENING : BUYING_RULES_QUAKE4;
}

int riBuyingManager::GetIntValueForKey( const char* keyName, int defaultValue ) {
	if( !keyName )
	{
		return defaultValue;
	}

	if ( GetRules() == BUYING_RULES_AWAKENING ) {
		for ( int i = 0; i < (int)( sizeof( awakeningBalanceConstants ) / sizeof( awakeningBalanceConstants[0] ) ); i++ ) {
			if ( !idStr::Icmp( awakeningBalanceConstants[ i ].key, keyName ) ) {
				return awakeningBalanceConstants[ i ].value;
			}
		}
	}

	const idDeclEntityDef *constants = FindConstants( _buyingGameBalanceConstants, "BuyingGameBalanceConstants" );
	if( !constants )
	{
		return defaultValue;
	}

	for( int i = 0; i < constants->dict.GetNumKeyVals(); i++ )
	{
		const idKeyValue* keyValuePair = constants->dict.GetKeyVal( i );
		if( !keyValuePair->GetKey().Icmp( keyName ) )
		{
			return atoi( keyValuePair->GetValue() );
		}
	}

	return defaultValue;
}

int riBuyingManager::GetOpponentKillCashAward( void ) {
	int targetFragCount = gameLocal.serverInfo.GetInt( "si_fragLimit" );
	const int rules = GetRules();
	if ( opponentKillFragCount != targetFragCount || opponentKillRules != rules ) {
		opponentKillFragCount = targetFragCount;
		opponentKillRules = rules;
		if ( idStr::Icmp( gameLocal.serverInfo.GetString( "si_gameType" ), "DM" ) && idStr::Icmp( gameLocal.serverInfo.GetString( "si_gameType" ), "Team DM" ) ) {
			// only do frag reward scaling in DM/TDM
			opponentKillCashAward = GetIntValueForKey( "playerCashAward_killingOpponent", 600 );
		} else {
			targetFragCount = idMath::ClampInt( GetIntValueForKey( "killingOpponent_minFragAdjust", 10 ), GetIntValueForKey( "killingOpponent_maxFragAdjust",50 ), targetFragCount );
			int baseVal = GetIntValueForKey( "playerCashAward_killingOpponent", 600 );
			int fragTarget = GetIntValueForKey( "killingOpponent_bestFragCount", 25 );
			opponentKillCashAward = ( baseVal * fragTarget ) / targetFragCount;
		}
	}
	return opponentKillCashAward;
}

/*
==============
riBuyingManager::GetItemPrice

The Awakening's price under its rules; retail's from the content's
ItemCostConstants, which leaves out what retail never sold.
==============
*/
int riBuyingManager::GetItemPrice( const char *item ) {
	const buyCatalogItem_t *entry = MP_FindBuyCatalogItem( item );
	if ( GetRules() == BUYING_RULES_AWAKENING ) {
		return ( entry != NULL && entry->awakeningPrice > 0 ) ? entry->awakeningPrice : 99999;
	}
	const idDeclEntityDef *costs = FindConstants( _itemCostConstants, "ItemCostConstants" );
	if ( costs == NULL || item == NULL ) {
		return 99999;
	}
	return costs->dict.GetInt( item, "99999" );
}

/*
==============
riBuyingManager::IsItemSold

The current rules sell the item and the content defines what it gives: the
item def, and for a weapon its class, for a weapon mod the weapon it fits.
Armour, the refill and the team specials are the game's own.
==============
*/
bool riBuyingManager::IsItemSold( const char *item ) {
	const buyCatalogItem_t *entry = MP_FindBuyCatalogItem( item );
	if ( entry == NULL ) {
		return false;
	}
	if ( GetRules() == BUYING_RULES_AWAKENING ? entry->awakeningPrice <= 0 : !entry->quake4 ) {
		return false;
	}
	if ( entry->category == BUY_CATEGORY_WEAPONS ) {
		const idDict *weaponDict = gameLocal.FindEntityDefDict( entry->item, false );
		return weaponDict != NULL && idClass::GetClass( weaponDict->GetString( "weaponclass", "rvWeapon" ) ) != NULL;
	}
	if ( entry->category == BUY_CATEGORY_MODS ) {
		const idDict *modDict = gameLocal.FindEntityDefDict( entry->item, false );
		const idDict *weaponDict = modDict != NULL ? gameLocal.FindEntityDefDict( modDict->GetString( "weapon" ), false ) : NULL;
		if ( weaponDict == NULL ) {
			return false;
		}
		for ( int m = 0; m < MAX_WEAPONMODS; m++ ) {
			if ( !idStr::Icmp( weaponDict->GetString( va( "def_mod%d", m + 1 ) ), entry->item ) ) {
				return true;
			}
		}
		return false;
	}
	return true;
}
