//----------------------------------------------------------------
// Buying.h
//
// Copyright 2005 Ritual Entertainment
//
// This file essentially serves as an extension to the Game DLL
// source files Multiplayer.h and Player.h, in an attempt
// to isolate, as much as possible, these changes from the main
// body of code (for merge simplification, etc).
//----------------------------------------------------------------

#ifndef __BUYING_H__
#define __BUYING_H__

#include "../Game_local.h"
#include "../MultiplayerGame.h"

/*
===============================================================================

	Buying rules

	A server that allows buying sells by one of two sets of rules, the
	"Buying" choice of Create Server, the vote and the admin page:

	- Quake 4: retail's buy menu. Its prices and cash awards come from the
	  content (ItemCostConstants and BuyingGameBalanceConstants).
	- The Awakening: the expansion's buy menu as far as the base content
	  defines it. Its prices, its weapon mods for the weapon in hand, the Dark
	  Matter Gun, FC Armor Regen, and its own cash awards over the content's.
	  The expansion kept these in code; its core cannon, fire cannon, freeze
	  gun and their mods need its content, which multiplayer never mounts.

	si_isBuyingEnabled turns buying on, as in retail; si_buyingRules picks the
	rules. The three-way choice the menus offer is buyingMode_t.

===============================================================================
*/

typedef enum {
	BUYING_RULES_QUAKE4 = 0,
	BUYING_RULES_AWAKENING,
	BUYING_RULES_COUNT
} buyingRules_t;

typedef enum {
	BUYING_MODE_OFF = 0,
	BUYING_MODE_QUAKE4,
	BUYING_MODE_AWAKENING,
	BUYING_MODE_COUNT
} buyingMode_t;

// the buy menu's sections, in the order its tabs show them
typedef enum {
	BUY_CATEGORY_WEAPONS = 0,
	BUY_CATEGORY_MODS,
	BUY_CATEGORY_GEAR,
	BUY_CATEGORY_TEAM,
	BUY_CATEGORY_COUNT
} buyCategory_t;

typedef struct buyCatalogItem_s {
	const char *	item;			// the def, and what "buy" names
	int				impulse;		// the buy impulse, IMPULSE_100 to IMPULSE_127
	buyCategory_t	category;
	bool			quake4;			// retail sells it, at the ItemCostConstants price
	int				awakeningPrice;	// the Awakening's price; 0 where it does not sell it
	const char *	name;			// #str_ of its name
	const char *	detail;			// #str_ of what it does, or NULL
} buyCatalogItem_t;

// every item either set of rules sells, in the buy menu's order
extern const buyCatalogItem_t	buyCatalog[];
extern const int				buyCatalogCount;

const buyCatalogItem_t *	MP_FindBuyCatalogItem( const char *item );
const buyCatalogItem_t *	MP_FindBuyCatalogItemForImpulse( int impulse );

// the three-way choice from the two server settings, and back
int			MP_BuyingModeFromServerInfo( const idDict &serverInfo );
int			MP_BuyingModeFromCVars( void );
void		MP_SetBuyingModeCVars( int mode );
// a stock menu's on/off value as a mode: on keeps the rules a server already sells by
int			MP_BuyingModeFromOnOff( int on, int currentMode );
const char *MP_BuyingModeName( int mode );		// #str_ of Off, Quake 4 or The Awakening
// a vote's line for the mode: BUYING OFF, BUYING QUAKE 4 or BUYING THE AWAKENING
idStr		MP_BuyingVoteLine( int mode );

class riBuyingManager
{
private:
	const idDeclEntityDef*	_buyingGameBalanceConstants;
	const idDeclEntityDef*	_itemCostConstants;
	int						opponentKillCashAward;	// latch
	int						opponentKillFragCount;
	int						opponentKillRules;

	const idDeclEntityDef *	FindConstants( const idDeclEntityDef *&constants, const char *name );

public:
	riBuyingManager();
	~riBuyingManager();

	// the rules the server sells by
	int						GetRules( void ) const;

	// a cash award or balance constant: the Awakening's where its rules set
	// one, else the content's
	int						GetIntValueForKey( const char* keyName, int defaultValue );
	int						GetOpponentKillCashAward( void );

	// the item's price under the current rules
	int						GetItemPrice( const char *item );
	// true when the current rules sell the item and the content defines it
	bool					IsItemSold( const char *item );

	void					Reset( void ) { opponentKillFragCount = -1; opponentKillRules = -1; }
};


#endif // __BUYING_H__
