//----------------------------------------------------------------
// BotCharacter.cpp
//
// Reading, storage and resolution of bot personalities.  See BotCharacter.h
// for what the three layers are and why any of this is content instead of
// code.
//
// Nothing in this file decides how a bot behaves.  It decides what the numbers
// are; Bot.cpp decides what to do with them.  That split is the whole point -
// a designer retuning difficulty should never have to open Bot.cpp, and this
// file should never have to know what a usercmd is.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "BotCharacter.h"

rvBotCharacterManager		botCharacterManager;

/*
===============================================================================

	Tuning.

===============================================================================
*/

// Chat has no flood protection anywhere in the engine, and idAsyncServer drops
// a client whose reliable message queue overflows.  Unthrottled bot chatter is
// therefore not merely annoying, it can kick real players off the server.
static const int	BOT_CHAT_CLIENT_THROTTLE_MSEC	= 6000;		// one bot may not speak twice inside this
static const int	BOT_CHAT_GLOBAL_THROTTLE_MSEC	= 1200;		// and no two bots may speak inside this

// Where the content lives.  New subdirectories and new extensions, so a reader
// can never pick up the dead Quake 3 prototypes still sitting in botfiles/.
static const char	BOT_STYLE_DIR[]					= "botfiles/styles";
static const char	BOT_STYLE_EXT[]					= ".style";
static const char	BOT_CHARACTER_DIR[]				= "botfiles/characters";
static const char	BOT_CHARACTER_EXT[]				= ".bot";
static const char	BOT_CHAT_DIR[]					= "botfiles/chats";
static const char	BOT_CHAT_EXT[]					= ".chat";

// An inherit chain longer than this is a content mistake, and a chain that
// loops would recurse until the stack ran out.  Broken at link time so the
// per-frame apply path needs no guard at all.
static const int	BOT_MAX_INHERIT_DEPTH			= 8;

/*
===============================================================================

	The trait table.

	One row per attribute: the word a file writes, where the float lives, the
	range every modifier result is clamped into, and the value at each of the
	five skill levels.  All of it on one line, because the header promises that
	adding an attribute costs one row and nothing else - and two parallel
	arrays would break that promise the first time somebody inserted into one
	and not the other.

	The clamp is not a formality.  It is what stops a typo in a character file
	from producing a bot that turns at 90000 degrees a second.

===============================================================================
*/

typedef struct botTraitDef_s {
	botTraitField_t	field;
	float			curve[BOT_SKILL_LEVELS];		// the value at skill 1, 2, 3, 4, 5
} botTraitDef_t;

#define BOTTRAIT( f, lo, hi, s1, s2, s3, s4, s5 )	\
	{ { #f, (int)offsetof( botTraits_t, f ), lo, hi }, { s1, s2, s3, s4, s5 } }

static const botTraitDef_t botTraitTable[] = {
	// -- vision and reaction --
	BOTTRAIT( sightRange,			128.0f,		8192.0f,	1200.0f,	1500.0f,	2200.0f,	2600.0f,	3000.0f ),
	BOTTRAIT( fov,					60.0f,		360.0f,		130.0f,		139.0f,		155.0f,		170.0f,		180.0f ),
	BOTTRAIT( reactionMsec,			0.0f,		2000.0f,	460.0f,		418.0f,		320.0f,		220.0f,		140.0f ),
	BOTTRAIT( reactionVarianceMsec,	0.0f,		1000.0f,	180.0f,		162.0f,		120.0f,		80.0f,		45.0f ),
	BOTTRAIT( reacquireMsec,		0.0f,		4000.0f,	150.0f,		210.0f,		400.0f,		550.0f,		700.0f ),
	BOTTRAIT( reacquireFraction,	0.0f,		1.0f,		1.00f,		0.91f,		0.70f,		0.55f,		0.40f ),
	BOTTRAIT( peripheralAngle,		5.0f,		180.0f,		60.0f,		63.0f,		70.0f,		75.0f,		80.0f ),
	BOTTRAIT( peripheralPenaltyMsec,0.0f,		2000.0f,	340.0f,		310.0f,		240.0f,		160.0f,		90.0f ),

	// -- aim --
	BOTTRAIT( turnSpeed,			20.0f,		2000.0f,	170.0f,		236.0f,		420.0f,		640.0f,		900.0f ),
	BOTTRAIT( turnAccel,			50.0f,		40000.0f,	750.0f,		1110.0f,	2400.0f,	4800.0f,	9000.0f ),
	BOTTRAIT( turnDamping,			0.1f,		3.0f,		0.42f,		0.50f,		0.70f,		0.90f,		1.10f ),
	BOTTRAIT( aimTrackTimeConst,	0.01f,		2.0f,		0.40f,		0.34f,		0.22f,		0.14f,		0.08f ),
	// A pure lag trails a target moving at a steady speed by speed times the
	// time constant, for ever: 26 units at skill 5 and 320 units a second,
	// which is off a 32-unit hull, so the best bot could not hit anyone
	// running in a straight line.  This is how much of the motion it has
	// perceived the aim carries forward instead; what is left still trails.
	BOTTRAIT( aimAnticipation,		0.0f,		1.0f,		0.50f,		0.63f,		0.85f,		0.92f,		0.97f ),
	BOTTRAIT( aimTremorDeg,			0.0f,		30.0f,		4.2f,		3.5f,		2.1f,		1.2f,		0.5f ),
	BOTTRAIT( aimTremorRateHz,		0.05f,		10.0f,		0.6f,		0.72f,		1.1f,		1.4f,		1.8f ),
	BOTTRAIT( aimTrackError,		0.0f,		20.0f,		2.8f,		2.4f,		1.5f,		0.8f,		0.3f ),
	BOTTRAIT( aimSettleMsec,		0.0f,		2000.0f,	240.0f,		216.0f,		170.0f,		110.0f,		60.0f ),
	// Both are fractions of the computed lead.  A lead past 1.0 aims ahead of
	// where the target will be, and an error past 1.0 lets the per-burst roll
	// go negative, so the clamps stop at the header's documented 0..1.
	BOTTRAIT( aimLead,				0.0f,		1.0f,		0.10f,		0.25f,		0.60f,		0.82f,		0.97f ),
	BOTTRAIT( aimLeadError,			0.0f,		1.0f,		0.45f,		0.37f,		0.20f,		0.11f,		0.05f ),

	// -- trigger --
	BOTTRAIT( fireConeDeg,			0.25f,		45.0f,		14.0f,		11.6f,		6.5f,		4.0f,		2.0f ),
	BOTTRAIT( holdFireTurnRate,		10.0f,		4000.0f,	1000.0f,	820.0f,		500.0f,		340.0f,		220.0f ),
	// Long and short at both ends, for opposite reasons.  A novice holds the
	// trigger down whether or not it is on target; a strong player holds it
	// for as long as it is, and the cone gate ends the burst the moment the
	// aim leaves.  The pause is what skill buys down: every one costs the
	// rest and then a fresh settle before the next round.
	BOTTRAIT( burstMinMsec,			30.0f,		4000.0f,	480.0f,		420.0f,		300.0f,		320.0f,		450.0f ),
	BOTTRAIT( burstMaxMsec,			30.0f,		8000.0f,	1400.0f,	1160.0f,	700.0f,		900.0f,		1200.0f ),
	BOTTRAIT( burstPauseMsec,		0.0f,		2000.0f,	220.0f,		196.0f,		140.0f,		100.0f,		60.0f ),

	// -- mistakes --
	BOTTRAIT( mistakeChance,		0.0f,		1.0f,		0.45f,		0.37f,		0.20f,		0.10f,		0.03f ),
	BOTTRAIT( mistakeMsec,			50.0f,		5000.0f,	900.0f,		810.0f,		600.0f,		450.0f,		300.0f ),

	// -- movement and engagement --
	BOTTRAIT( strafeChance,			0.0f,		1.0f,		0.15f,		0.24f,		0.45f,		0.58f,		0.70f ),
	BOTTRAIT( dodgeReactMsec,		0.0f,		3000.0f,	900.0f,		780.0f,		500.0f,		320.0f,		180.0f ),
	BOTTRAIT( jumpChance,			0.0f,		1.0f,		0.05f,		0.09f,		0.22f,		0.34f,		0.45f ),
	BOTTRAIT( combatRange,			32.0f,		4096.0f,	500.0f,		500.0f,		500.0f,		500.0f,		500.0f ),
	BOTTRAIT( rangeDiscipline,		0.0f,		1.0f,		0.20f,		0.29f,		0.50f,		0.68f,		0.85f ),
	BOTTRAIT( aggression,			0.0f,		1.0f,		0.50f,		0.50f,		0.50f,		0.50f,		0.50f ),
	BOTTRAIT( patience,				0.0f,		1.0f,		0.50f,		0.50f,		0.50f,		0.50f,		0.50f ),
	BOTTRAIT( retreatHealth,		0.0f,		1.0f,		0.15f,		0.18f,		0.25f,		0.30f,		0.35f ),
	BOTTRAIT( pursuit,				0.0f,		2.0f,		0.30f,		0.37f,		0.55f,		0.68f,		0.80f ),
	BOTTRAIT( itemFocus,			0.0f,		2.0f,		0.25f,		0.33f,		0.50f,		0.65f,		0.80f ),

	// -- tactical personality --
	// Most stay flat because they decide what kind of fight a character creates,
	// not whether it has the mechanical skill to win it.  initiative and
	// suppression deliberately favor the low end: novices shoot imperfectly
	// and waste rounds through a doorway instead of waiting themselves silent.
	BOTTRAIT( initiative,			0.0f,		1.0f,		0.80f,		0.74f,		0.58f,		0.50f,		0.50f ),
	BOTTRAIT( targetStickiness,	0.0f,		1.0f,		0.50f,		0.50f,		0.50f,		0.50f,		0.50f ),
	BOTTRAIT( opportunism,			0.0f,		1.0f,		1.00f,		1.00f,		1.00f,		1.00f,		1.00f ),
	BOTTRAIT( vengefulness,		0.0f,		1.0f,		0.00f,		0.00f,		0.00f,		0.00f,		0.00f ),
	BOTTRAIT( suppressionMsec,		0.0f,		2000.0f,	1200.0f,	900.0f,		250.0f,		0.0f,		0.0f ),
	BOTTRAIT( strafeRhythmMsec,	100.0f,		3000.0f,	400.0f,		400.0f,		400.0f,		400.0f,		400.0f ),
	BOTTRAIT( strafeRhythmVarianceMsec,0.0f,	3000.0f,	800.0f,		800.0f,		800.0f,		800.0f,		800.0f ),
	BOTTRAIT( weaponSwitchMsec,	100.0f,		3000.0f,	500.0f,		500.0f,		500.0f,		500.0f,		500.0f ),
	BOTTRAIT( aimHeight,			-0.4f,		0.4f,		0.00f,		0.00f,		0.00f,		0.00f,		0.00f ),

	// -- decision quality --
	BOTTRAIT( weaponSkill,			0.0f,		1.0f,		0.20f,		0.31f,		0.55f,		0.75f,		0.92f ),
	BOTTRAIT( targetSelection,		0.0f,		1.0f,		0.25f,		0.34f,		0.55f,		0.72f,		0.88f ),
	// Awareness is competence: a novice does not turn toward gunfire behind it
	// and has no idea when the mega health comes back; a strong player does both.
	BOTTRAIT( hearing,				0.0f,		2.0f,		0.45f,		0.54f,		0.75f,		0.90f,		1.00f ),
	BOTTRAIT( itemTiming,			0.0f,		1.0f,		0.05f,		0.17f,		0.50f,		0.75f,		0.92f ),

	// -- chat --
	// combatRange, aggression, patience, most tactical-personality traits and
	// chatiness are flat on purpose.
	// They are taste, not competence, and a skill curve that moved them would
	// make every high skill bot play identically - the thing this whole system
	// exists to stop.
	BOTTRAIT( chatiness,			0.0f,		1.0f,		0.50f,		0.50f,		0.50f,		0.50f,		0.50f ),
	BOTTRAIT( chatDelayScale,		0.1f,		5.0f,		1.60f,		1.35f,		1.10f,		0.90f,		0.75f )
};

static const int	botNumTraitFields = sizeof( botTraitTable ) / sizeof( botTraitTable[0] );

/*
===============================================================================

	Chat event names.

===============================================================================
*/

static const char *botChatEventNames[] = {
	"entergame",			// BOTCHAT_ENTERGAME
	"levelstart",			// BOTCHAT_LEVELSTART
	"kill",					// BOTCHAT_KILL
	"killGauntlet",			// BOTCHAT_KILL_GAUNTLET
	"killStreak",			// BOTCHAT_KILL_STREAK
	"revenge",				// BOTCHAT_KILL_REVENGE
	"death",				// BOTCHAT_DEATH
	"deathAccident",		// BOTCHAT_DEATH_ACCIDENT
	"itemDenied",			// BOTCHAT_ITEM_DENIED
	"leadTaken",			// BOTCHAT_LEAD_TAKEN
	"leadLost",				// BOTCHAT_LEAD_LOST
	"matchWin",				// BOTCHAT_MATCH_WIN
	"matchLose",			// BOTCHAT_MATCH_LOSE
	"farewell",				// BOTCHAT_FAREWELL
	"order"					// BOTCHAT_ORDER
};

// The table and the enum are indexed against each other in both directions, so
// a missing or extra row would silently shift every event by one and a kill
// line would come out as a death line.  This is one of the few agreements in
// the system the compiler can actually check, so make it.
file_scoped_compile_time_assert( sizeof( botChatEventNames ) / sizeof( botChatEventNames[0] ) == BOTCHAT_NUM );

/*
===============================================================================

	Parsing helpers.

	Every value is read with ReadTokenOnLine rather than ReadToken.  The format
	is one statement per line, and a statement missing its value must not be
	allowed to reach across the newline and swallow the closing brace of the
	block it is in - a single typo would otherwise silently merge two blocks.

===============================================================================
*/

/*
================
BotParseNumber

Reads a number off the rest of the current line, handling the leading minus
that the lexer returns as its own punctuation token.  False means the line did
not have one, and the caller has already been told why.
================
*/
static bool BotParseNumber( idLexer &lexer, float &out, const char *sourceName, const char *key ) {
	idToken	token;
	float	sign = 1.0f;

	out = 0.0f;

	if ( !lexer.ReadTokenOnLine( &token ) ) {
		gameLocal.Warning( "bot file '%s' line %d: '%s' has no value", sourceName, lexer.GetLineNum(), key );
		return false;
	}

	if ( token.type == TT_PUNCTUATION && token == "-" ) {
		sign = -1.0f;
		if ( !lexer.ReadTokenOnLine( &token ) ) {
			gameLocal.Warning( "bot file '%s' line %d: '%s' has no value after the sign", sourceName, lexer.GetLineNum(), key );
			return false;
		}
	}

	if ( token.type != TT_NUMBER ) {
		gameLocal.Warning( "bot file '%s' line %d: '%s' wanted a number, found '%s'", sourceName, lexer.GetLineNum(), key, token.c_str() );
		return false;
	}

	out = sign * token.GetFloatValue();

	return true;
}

/*
================
BotParseString

Reads a quoted string, or a bare word, off the rest of the current line.
================
*/
static bool BotParseString( idLexer &lexer, idStr &out, const char *sourceName, const char *key ) {
	idToken token;

	out.Clear();

	if ( !lexer.ReadTokenOnLine( &token ) ) {
		gameLocal.Warning( "bot file '%s' line %d: '%s' has no value", sourceName, lexer.GetLineNum(), key );
		return false;
	}

	out = token;

	return true;
}

/*
================
BotSkipUnknownKey

An unknown key is a warning and never an error: these files are content, and
content must not be able to kill a server.  A key followed by a brace takes the
whole block with it so the rest of the file still reads cleanly.
================
*/
static void BotSkipUnknownKey( idLexer &lexer, const idToken &token, const char *sourceName, const char *owner ) {
	gameLocal.Warning( "bot file '%s' line %d: %s has unknown key '%s'", sourceName, lexer.GetLineNum(), owner, token.c_str() );

	if ( lexer.PeekTokenString( "{" ) ) {
		lexer.SkipBracedSection();
	} else {
		lexer.SkipRestOfLine();
	}
}

/*
================
BotTraitOpForToken

BOTTRAITOP_NUM when the word is not one of the three modifier verbs.
================
*/
static int BotTraitOpForToken( const idToken &token ) {
	if ( token.Icmp( "set" ) == 0 ) {
		return BOTTRAITOP_SET;
	}
	if ( token.Icmp( "add" ) == 0 ) {
		return BOTTRAITOP_ADD;
	}
	if ( token.Icmp( "scale" ) == 0 ) {
		return BOTTRAITOP_SCALE;
	}

	return BOTTRAITOP_NUM;
}

/*
================
BotParseSkillBlock

Reads "skill <n> { ... }" into the caller's per-level array.  Only the three
modifier verbs are legal inside: a weapon preference that changed with skill
would make a style mean something different at each level, which is exactly
what a style is not allowed to do.
================
*/
static bool BotParseSkillBlock( idLexer &lexer, rvBotTraitMods *skillMods, const char *sourceName, const char *owner ) {
	idToken	token;
	float	level;

	// A level that could not be read still has a block behind it, and skipping
	// only the line would leave the block's own closing brace to be read as the
	// end of the style or character it sits in.
	if ( !BotParseNumber( lexer, level, sourceName, "skill" ) ) {
		lexer.SkipBracedSection();
		return true;
	}

	const int index = (int)level;

	// An out of range level is skipped rather than clamped: silently folding a
	// "skill 7" block into level 5 would be a worse surprise than losing it.
	if ( index < 1 || index > BOT_SKILL_LEVELS ) {
		gameLocal.Warning( "bot file '%s' line %d: %s has skill level %d, expected 1..%d", sourceName, lexer.GetLineNum(), owner, index, BOT_SKILL_LEVELS );
		lexer.SkipBracedSection();
		return true;
	}

	if ( !lexer.ExpectTokenString( "{" ) ) {
		return false;
	}

	while ( lexer.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}

		const int op = BotTraitOpForToken( token );
		if ( op != BOTTRAITOP_NUM ) {
			if ( !skillMods[index - 1].ParseStatement( lexer, op, sourceName ) ) {
				return false;
			}
			continue;
		}

		if ( token.Icmp( "weapon" ) == 0 ) {
			gameLocal.Warning( "bot file '%s' line %d: %s sets a weapon bias inside 'skill %d'; weapon preference is not per level and was ignored", sourceName, lexer.GetLineNum(), owner, index );
			lexer.SkipRestOfLine();
			continue;
		}

		BotSkipUnknownKey( lexer, token, sourceName, owner );
	}

	gameLocal.Warning( "bot file '%s': unexpected end of file inside %s skill %d", sourceName, owner, index );

	return false;
}

/*
================
BotParseWeaponBias

Reads "weapon <class> <bias>" into a layer's list.  A repeated weapon replaces
the earlier opinion instead of stacking, so a file can override its own
inherited value without the reader having to know which line won.
================
*/
static void BotParseWeaponBias( idLexer &lexer, idList<botWeaponBias_t> &list, const char *sourceName, const char *owner ) {
	idStr	weapon;
	float	bias;

	if ( !BotParseString( lexer, weapon, sourceName, "weapon" ) ) {
		lexer.SkipRestOfLine();
		return;
	}
	if ( !BotParseNumber( lexer, bias, sourceName, "weapon" ) ) {
		lexer.SkipRestOfLine();
		return;
	}

	// A negative bias would invert the weapon score rather than suppress the
	// weapon, which is never what an author meant by a minus sign.
	bias = idMath::ClampFloat( 0.0f, 100.0f, bias );

	for ( int i = 0; i < list.Num(); i++ ) {
		if ( list[i].weapon.Icmp( weapon.c_str() ) == 0 ) {
			list[i].bias = bias;
			return;
		}
	}

	if ( list.Num() >= BOT_MAX_WEAPON_BIAS ) {
		gameLocal.Warning( "bot file '%s' line %d: %s has more than %d weapon biases, '%s' was dropped", sourceName, lexer.GetLineNum(), owner, BOT_MAX_WEAPON_BIAS, weapon.c_str() );
		return;
	}

	botWeaponBias_t entry;

	entry.weapon	= weapon;
	entry.bias		= bias;

	list.Append( entry );
}

/*
================
BotMergeWeaponBias

Folds one layer's weapon opinions into the resolved traits.  Merge, not
replace: a character that says something about the rail gun keeps everything
its style said about the other nine weapons.
================
*/
static void BotMergeWeaponBias( botTraits_t &traits, const idList<botWeaponBias_t> &list ) {
	for ( int i = 0; i < list.Num(); i++ ) {
		int slot = -1;

		for ( int j = 0; j < traits.numWeaponBias; j++ ) {
			if ( traits.weaponBias[j].weapon.Icmp( list[i].weapon.c_str() ) == 0 ) {
				slot = j;
				break;
			}
		}

		if ( slot == -1 ) {
			if ( traits.numWeaponBias >= BOT_MAX_WEAPON_BIAS ) {
				// Each layer was checked against the cap on its own at load
				// time; the merged set of a parent style, a style and a
				// character is only known here, and a dropped opinion with no
				// diagnostic is a character that quietly plays someone else.
				gameLocal.Warning( "bot weapon bias for '%s' dropped: more than %d weapons have an opinion once the style and character layers merge",
								   list[i].weapon.c_str(), BOT_MAX_WEAPON_BIAS );
				continue;
			}
			slot = traits.numWeaponBias++;
			traits.weaponBias[slot].weapon = list[i].weapon;
		}

		traits.weaponBias[slot].bias = list[i].bias;
	}
}

/*
================
BotSeededRandom

A private generator seeded from a bot's client number.  The skill roll has to
come out the same every time it is asked for - botlist has to report the number
the bot is actually playing at, and a bot must not re-roll its skill on every
respawn - so it cannot come from the shared gameLocal.random stream, which
advances under it.

idRandom is a plain LCG whose first draw tracks the seed closely, so small
consecutive client numbers would produce nearly the same roll.  Mix the seed
and throw the first draw away.
================
*/
static void BotSeededRandom( idRandom &rnd, int seed ) {
	unsigned int hash = (unsigned int)seed;

	hash ^= 0x9e3779b9u;
	hash *= 0x85ebca6bu;
	hash ^= hash >> 13;
	hash *= 0xc2b2ae35u;
	hash ^= hash >> 16;

	rnd.SetSeed( (int)( hash & 0x7fffffffu ) );
	rnd.RandomFloat();
}

/*
===============================================================================

	rvBotTraitMods

===============================================================================
*/

/*
================
rvBotTraitMods::Append
================
*/
void rvBotTraitMods::Append( int field, int op, float value ) {
	botTraitMod_t mod;

	mod.field	= (short)field;
	mod.op		= (short)op;
	mod.value	= value;

	mods.Append( mod );
}

/*
================
rvBotTraitMods::ParseStatement

"<field> <value>", with the set/add/scale word already consumed.
================
*/
bool rvBotTraitMods::ParseStatement( idLexer &lexer, int op, const char *sourceName ) {
	idToken	token;
	float	value;

	if ( !lexer.ReadTokenOnLine( &token ) ) {
		gameLocal.Warning( "bot file '%s' line %d: modifier with no trait name", sourceName, lexer.GetLineNum() );
		return true;
	}

	const int field = rvBotCharacterManager::FindTraitField( token.c_str() );

	if ( field == -1 ) {
		gameLocal.Warning( "bot file '%s' line %d: unknown trait '%s'", sourceName, lexer.GetLineNum(), token.c_str() );
		lexer.SkipRestOfLine();
		return true;
	}

	if ( !BotParseNumber( lexer, value, sourceName, token.c_str() ) ) {
		lexer.SkipRestOfLine();
		return true;
	}

	Append( field, op, value );

	return true;
}

/*
================
rvBotTraitMods::Apply

In file order, because scale composes with whatever came before it, and
clamped after every single statement rather than once at the end - otherwise a
scale of a value that a previous line had already pushed out of range would
compound the mistake instead of containing it.
================
*/
void rvBotTraitMods::Apply( botTraits_t &traits ) const {
	for ( int i = 0; i < mods.Num(); i++ ) {
		float *value = rvBotCharacterManager::TraitFieldPtr( traits, mods[i].field );

		if ( !value ) {
			continue;
		}

		const float previous = *value;

		switch ( mods[i].op ) {
			case BOTTRAITOP_SET:
				*value = mods[i].value;
				break;
			case BOTTRAITOP_ADD:
				*value += mods[i].value;
				break;
			case BOTTRAITOP_SCALE:
				*value *= mods[i].value;
				break;
			default:
				continue;
		}

		const botTraitField_t *field = rvBotCharacterManager::GetTraitField( mods[i].field );

		// A statement whose result is a NaN is dropped rather than clamped: the
		// clamp is two comparisons and every comparison against a NaN is false,
		// so it would pass straight through and poison the trait for the life of
		// the bot.  A file that looks perfectly in range can produce one, because
		// an overlarge literal parses to infinity and scaling zero by infinity is
		// NaN.  Infinity itself still clamps normally and is left alone.
		if ( FLOAT_IS_NAN( *value ) && !FLOAT_IS_INF( *value ) ) {
			*value = previous;
			continue;
		}

		*value = idMath::ClampFloat( field->min, field->max, *value );
	}
}

/*
===============================================================================

	rvBotStyle

===============================================================================
*/

/*
================
rvBotStyle::rvBotStyle
================
*/
rvBotStyle::rvBotStyle( void ) {
	Clear();
}

/*
================
rvBotStyle::Clear
================
*/
void rvBotStyle::Clear( void ) {
	name.Clear();
	description.Clear();
	fileName.Clear();
	inherit.Clear();

	parent = NULL;

	mods.Clear();
	weaponBias.Clear();

	for ( int i = 0; i < BOT_SKILL_LEVELS; i++ ) {
		skillMods[i].Clear();
	}
}

/*
================
rvBotStyle::Parse

The caller has identified the "style" keyword and pushed it back, so this reads
the whole block and can complain about its own syntax without the caller having
to know the grammar.
================
*/
bool rvBotStyle::Parse( idLexer &lexer, const char *sourceName ) {
	idToken token;

	Clear();

	fileName = sourceName;

	if ( !lexer.ExpectTokenString( "style" ) ) {
		return false;
	}
	if ( !lexer.ReadToken( &token ) ) {
		gameLocal.Warning( "bot file '%s': 'style' with no name", sourceName );
		return false;
	}

	name = token;

	if ( !lexer.ExpectTokenString( "{" ) ) {
		return false;
	}

	// Built once and held, rather than being rebuilt with va() at each use:
	// va's buffer rotates, and this string outlives several nested calls.
	const idStr owner = idStr( "style '" ) + name + "'";

	while ( lexer.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}

		const int op = BotTraitOpForToken( token );
		if ( op != BOTTRAITOP_NUM ) {
			if ( !mods.ParseStatement( lexer, op, sourceName ) ) {
				return false;
			}
			continue;
		}

		if ( token.Icmp( "description" ) == 0 ) {
			BotParseString( lexer, description, sourceName, "description" );
		} else if ( token.Icmp( "inherit" ) == 0 ) {
			BotParseString( lexer, inherit, sourceName, "inherit" );
		} else if ( token.Icmp( "weapon" ) == 0 ) {
			BotParseWeaponBias( lexer, weaponBias, sourceName, owner.c_str() );
		} else if ( token.Icmp( "skill" ) == 0 ) {
			if ( !BotParseSkillBlock( lexer, skillMods, sourceName, owner.c_str() ) ) {
				return false;
			}
		} else {
			BotSkipUnknownKey( lexer, token, sourceName, owner.c_str() );
		}
	}

	gameLocal.Warning( "bot file '%s': unexpected end of file inside style '%s'", sourceName, name.c_str() );

	return false;
}

/*
================
rvBotStyle::Apply

Parent first and in full - body then its own level overrides - so an inheriting
style scales what its parent chose rather than what the baseline curve chose.
The recursion is unguarded because ResolveInheritance has already broken any
cycle; this runs on every bot spawn and should not be paying for that check.
================
*/
void rvBotStyle::Apply( botTraits_t &traits, int skillLevel ) const {
	if ( parent ) {
		parent->Apply( traits, skillLevel );
	}

	mods.Apply( traits );
	BotMergeWeaponBias( traits, weaponBias );

	if ( skillLevel >= 1 && skillLevel <= BOT_SKILL_LEVELS ) {
		skillMods[skillLevel - 1].Apply( traits );
	}
}

/*
===============================================================================

	rvBotCharacter

===============================================================================
*/

/*
================
rvBotCharacter::rvBotCharacter
================
*/
rvBotCharacter::rvBotCharacter( void ) {
	Clear();
}

/*
================
rvBotCharacter::Clear
================
*/
void rvBotCharacter::Clear( void ) {
	name.Clear();
	description.Clear();
	fileName.Clear();
	styleName.Clear();

	style		= NULL;

	// A character with no band is available at every difficulty, which is the
	// only sane default: a file that forgot skillBand should still be usable.
	minSkill	= 1;
	maxSkill	= BOT_SKILL_LEVELS;

	model.Clear();
	modelMarine.Clear();
	modelStrogg.Clear();

	mods.Clear();
	weaponBias.Clear();

	for ( int i = 0; i < BOT_SKILL_LEVELS; i++ ) {
		skillMods[i].Clear();
	}

	for ( int i = 0; i < BOTCHAT_NUM; i++ ) {
		chat[i].Clear();
		lastChat[i] = -1;
	}
	replies.Clear();

	inUse = false;
}

/*
================
rvBotCharacter::HasChat
================
*/
bool rvBotCharacter::HasChat( rvBotChatEvent event ) const {
	return NumChatLines( event ) > 0;
}

/*
================
rvBotCharacter::NumChatLines
================
*/
int rvBotCharacter::NumChatLines( rvBotChatEvent event ) const {
	// Cast for the comparison: an unscoped enum with no negative enumerator may
	// have an unsigned underlying type, and the bounds check has to survive
	// that as well as hold its meaning for a reader.
	if ( (int)event < 0 || (int)event >= BOTCHAT_NUM ) {
		return 0;
	}

	return chat[event].Num();
}

/*
================
rvBotCharacter::ParseChatBlock

Reads "chat <event> { "line" "line" ... }" from the canonical separate chat
file or a legacy inline character block.  Lines are validated here rather than
at broadcast time, because idMultiplayerGame::ProcessChatMessage drops an
over-long line with nothing but a warning and an author would never find out
which of their lines had gone quiet.
================
*/
bool rvBotCharacter::ParseChatBlock( idLexer &lexer, const char *sourceName ) {
	idToken token;

	if ( !lexer.ReadTokenOnLine( &token ) ) {
		gameLocal.Warning( "bot file '%s' line %d: 'chat' with no event name", sourceName, lexer.GetLineNum() );
		// Consume the block that follows, or its lines read as unknown keys
		// and its closing brace ends the enclosing character early.
		if ( lexer.PeekTokenString( "{" ) ) {
			lexer.SkipBracedSection();
		}
		return true;
	}

	const rvBotChatEvent event = rvBotCharacterManager::ChatEventForName( token.c_str() );

	if ( event == BOTCHAT_NUM ) {
		gameLocal.Warning( "bot file '%s' line %d: character '%s' has unknown chat event '%s'", sourceName, lexer.GetLineNum(), name.c_str(), token.c_str() );
		lexer.SkipBracedSection();
		return true;
	}

	if ( !lexer.ExpectTokenString( "{" ) ) {
		return false;
	}

	while ( lexer.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}

		if ( token.type != TT_STRING ) {
			gameLocal.Warning( "bot file '%s' line %d: character '%s' chat %s expected a quoted line, found '%s'",
							   sourceName, lexer.GetLineNum(), name.c_str(), rvBotCharacterManager::ChatEventName( event ), token.c_str() );
			continue;
		}

		if ( token.Length() == 0 ) {
			gameLocal.Warning( "bot file '%s' line %d: character '%s' has an empty chat line", sourceName, lexer.GetLineNum(), name.c_str() );
			continue;
		}

		// ProcessChatMessage runs the text through common->GetLocalizedString,
		// so a line starting with '#' would be looked up in the string table
		// and replaced out from under the author.
		if ( token[0] == '#' ) {
			gameLocal.Warning( "bot file '%s' line %d: character '%s' has a chat line starting with '#', which would be read as a string table key", sourceName, lexer.GetLineNum(), name.c_str() );
			continue;
		}

		if ( token.Length() > BOT_CHAT_MAX_LEN ) {
			gameLocal.Warning( "bot file '%s' line %d: character '%s' has a chat line of %d characters, the limit is %d", sourceName, lexer.GetLineNum(), name.c_str(), token.Length(), BOT_CHAT_MAX_LEN );
			continue;
		}

		chat[event].Append( token );
	}

	gameLocal.Warning( "bot file '%s': unexpected end of file inside character '%s' chat %s", sourceName, name.c_str(), rvBotCharacterManager::ChatEventName( event ) );

	return false;
}

/*
================
BotReplyLineTokensValid

Reply lines have a deliberately smaller vocabulary than event lines.  A reply
knows who spoke and where the match is, but it did not necessarily involve a
weapon or an item.  Validate every token-looking word now so an author's typo
does not leak to the server as literal "$othre".
================
*/
static bool BotReplyLineTokensValid( const idStr &line, idStr &badToken ) {
	badToken.Clear();

	for ( int i = 0; i < line.Length(); i++ ) {
		if ( line[i] != '$' || i + 1 >= line.Length() ) {
			continue;
		}

		int end = i + 1;
		while ( end < line.Length() &&
				( idStr::CharIsAlpha( (byte)line[end] ) ||
				  idStr::CharIsNumeric( (byte)line[end] ) ||
				  line[end] == '_' ) ) {
			end++;
		}

		// A bare currency sign or punctuation is ordinary text, not a token.
		if ( end == i + 1 ) {
			continue;
		}

		const idStr token = line.Mid( i, end - i );

		if ( token != "$self" && token != "$other" && token != "$map" ) {
			badToken = token;
			return false;
		}

		i = end - 1;
	}

	return true;
}

/*
================
rvBotCharacter::ParseReplyBlock

Reads:

	reply <name> {
		priority 0..100;
		source any|player|bot;
		addressed either|required|forbidden;
		trigger "whole phrase";
		"response"
	}

The rule is local until the closing brace arrives.  That makes both this rule
and the containing characterChat owner safe to abandon on malformed input.
================
*/
bool rvBotCharacter::ParseReplyBlock( idLexer &lexer, const char *sourceName ) {
	idToken			token;
	botReplyRule_t	rule;

	rule.Clear();

	if ( !lexer.ReadTokenOnLine( &token ) ) {
		gameLocal.Warning( "bot file '%s' line %d: 'reply' with no rule name", sourceName, lexer.GetLineNum() );
		if ( lexer.PeekTokenString( "{" ) ) {
			lexer.SkipBracedSection();		// see ParseChatBlock
		}
		return true;
	}
	rule.name = token;

	if ( !lexer.ExpectTokenString( "{" ) ) {
		return false;
	}

	const idStr owner = idStr( "character '" ) + name + "' reply '" + rule.name + "'";

	while ( lexer.ReadToken( &token ) ) {
		if ( token == "}" ) {
			if ( !rule.triggers.Num() ) {
				gameLocal.Warning( "bot file '%s' line %d: %s has no valid triggers and was ignored",
								   sourceName, lexer.GetLineNum(), owner.c_str() );
				return true;
			}
			if ( !rule.lines.Num() ) {
				gameLocal.Warning( "bot file '%s' line %d: %s has no valid response lines and was ignored",
								   sourceName, lexer.GetLineNum(), owner.c_str() );
				return true;
			}
			if ( replies.Num() >= BOT_MAX_REPLY_RULES ) {
				gameLocal.Warning( "bot file '%s' line %d: character '%s' has more than %d reply rules; '%s' was dropped",
								   sourceName, lexer.GetLineNum(), name.c_str(), BOT_MAX_REPLY_RULES, rule.name.c_str() );
				return true;
			}

			replies.Append( rule );
			return true;
		}

		// Semicolons are optional statement separators.  Newlines remain the
		// natural format, while accepting the compact grammar used in docs.
		if ( token == ";" ) {
			continue;
		}

		if ( token.Icmp( "priority" ) == 0 ) {
			float value;
			if ( BotParseNumber( lexer, value, sourceName, "priority" ) ) {
				const int requested = (int)value;
				if ( requested < 0 || requested > 100 ) {
					gameLocal.Warning( "bot file '%s' line %d: %s priority %d was clamped to 0..100",
									   sourceName, lexer.GetLineNum(), owner.c_str(), requested );
				}
				rule.priority = idMath::ClampInt( 0, 100, requested );
			}
			continue;
		}

		if ( token.Icmp( "source" ) == 0 ) {
			idStr value;
			if ( !BotParseString( lexer, value, sourceName, "source" ) ) {
				continue;
			}

			if ( value.Icmp( "any" ) == 0 ) {
				rule.source = BOTREPLYSOURCE_ANY;
			} else if ( value.Icmp( "player" ) == 0 ) {
				rule.source = BOTREPLYSOURCE_PLAYER;
			} else if ( value.Icmp( "bot" ) == 0 ) {
				rule.source = BOTREPLYSOURCE_BOT;
			} else {
				gameLocal.Warning( "bot file '%s' line %d: %s source '%s' must be any, player or bot",
								   sourceName, lexer.GetLineNum(), owner.c_str(), value.c_str() );
			}
			continue;
		}

		if ( token.Icmp( "addressed" ) == 0 ) {
			idStr value;
			if ( !BotParseString( lexer, value, sourceName, "addressed" ) ) {
				continue;
			}

			if ( value.Icmp( "either" ) == 0 ) {
				rule.addressed = BOTREPLYADDRESS_EITHER;
			} else if ( value.Icmp( "required" ) == 0 ) {
				rule.addressed = BOTREPLYADDRESS_REQUIRED;
			} else if ( value.Icmp( "forbidden" ) == 0 ) {
				rule.addressed = BOTREPLYADDRESS_FORBIDDEN;
			} else {
				gameLocal.Warning( "bot file '%s' line %d: %s addressed '%s' must be either, required or forbidden",
								   sourceName, lexer.GetLineNum(), owner.c_str(), value.c_str() );
			}
			continue;
		}

		if ( token.Icmp( "trigger" ) == 0 ) {
			idToken phrase;
			if ( !lexer.ReadTokenOnLine( &phrase ) ) {
				gameLocal.Warning( "bot file '%s' line %d: %s trigger has no phrase",
								   sourceName, lexer.GetLineNum(), owner.c_str() );
				continue;
			}
			if ( phrase.type != TT_STRING ) {
				gameLocal.Warning( "bot file '%s' line %d: %s trigger expected a quoted phrase, found '%s'",
								   sourceName, lexer.GetLineNum(), owner.c_str(), phrase.c_str() );
				continue;
			}

			idStr normalized;
			rvBotCharacterManager::NormalizeReplyText( phrase.c_str(), normalized );

			if ( normalized.IsEmpty() ) {
				gameLocal.Warning( "bot file '%s' line %d: %s has an empty normalized trigger",
								   sourceName, lexer.GetLineNum(), owner.c_str() );
				continue;
			}
			if ( normalized.Length() > BOT_REPLY_TRIGGER_MAX_LEN ) {
				gameLocal.Warning( "bot file '%s' line %d: %s trigger is %d characters after normalization, limit %d",
								   sourceName, lexer.GetLineNum(), owner.c_str(), normalized.Length(), BOT_REPLY_TRIGGER_MAX_LEN );
				continue;
			}
			if ( rule.triggers.Num() >= BOT_MAX_REPLY_TRIGGERS ) {
				gameLocal.Warning( "bot file '%s' line %d: %s has more than %d triggers; '%s' was dropped",
								   sourceName, lexer.GetLineNum(), owner.c_str(), BOT_MAX_REPLY_TRIGGERS, phrase.c_str() );
				continue;
			}

			bool duplicate = false;
			for ( int i = 0; i < rule.triggers.Num(); i++ ) {
				if ( rule.triggers[i] == normalized ) {
					duplicate = true;
					break;
				}
			}
			if ( !duplicate ) {
				rule.triggers.Append( normalized );
			}
			continue;
		}

		if ( token.type == TT_STRING ) {
			if ( token.IsEmpty() ) {
				gameLocal.Warning( "bot file '%s' line %d: %s has an empty response line",
								   sourceName, lexer.GetLineNum(), owner.c_str() );
				continue;
			}
			if ( token[0] == '#' ) {
				gameLocal.Warning( "bot file '%s' line %d: %s response starts with '#', which would be read as a string table key",
								   sourceName, lexer.GetLineNum(), owner.c_str() );
				continue;
			}
			if ( token.Length() > BOT_CHAT_MAX_LEN ) {
				gameLocal.Warning( "bot file '%s' line %d: %s response is %d characters, limit %d",
								   sourceName, lexer.GetLineNum(), owner.c_str(), token.Length(), BOT_CHAT_MAX_LEN );
				continue;
			}

			idStr badToken;
			if ( !BotReplyLineTokensValid( token, badToken ) ) {
				gameLocal.Warning( "bot file '%s' line %d: %s response uses unsupported token '%s'; replies allow only $self, $other and $map",
								   sourceName, lexer.GetLineNum(), owner.c_str(), badToken.c_str() );
				continue;
			}
			if ( rule.lines.Num() >= BOT_MAX_REPLY_LINES ) {
				gameLocal.Warning( "bot file '%s' line %d: %s has more than %d response lines; one was dropped",
								   sourceName, lexer.GetLineNum(), owner.c_str(), BOT_MAX_REPLY_LINES );
				continue;
			}

			bool duplicate = false;
			for ( int i = 0; i < rule.lines.Num(); i++ ) {
				if ( rule.lines[i].Icmp( token.c_str() ) == 0 ) {
					duplicate = true;
					break;
				}
			}
			if ( !duplicate ) {
				rule.lines.Append( token );
			}
			continue;
		}

		BotSkipUnknownKey( lexer, token, sourceName, owner.c_str() );
	}

	gameLocal.Warning( "bot file '%s': unexpected end of file inside %s", sourceName, owner.c_str() );
	return false;
}

/*
================
rvBotCharacter::Parse
================
*/
bool rvBotCharacter::Parse( idLexer &lexer, const char *sourceName ) {
	idToken	token;
	float	number;

	Clear();

	fileName = sourceName;

	if ( !lexer.ExpectTokenString( "character" ) ) {
		return false;
	}
	if ( !lexer.ReadToken( &token ) ) {
		gameLocal.Warning( "bot file '%s': 'character' with no name", sourceName );
		return false;
	}

	name = token;

	if ( !lexer.ExpectTokenString( "{" ) ) {
		return false;
	}

	const idStr owner = idStr( "character '" ) + name + "'";

	while ( lexer.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}

		const int op = BotTraitOpForToken( token );
		if ( op != BOTTRAITOP_NUM ) {
			if ( !mods.ParseStatement( lexer, op, sourceName ) ) {
				return false;
			}
			continue;
		}

		if ( token.Icmp( "description" ) == 0 ) {
			BotParseString( lexer, description, sourceName, "description" );
		} else if ( token.Icmp( "inherit" ) == 0 ) {
			BotParseString( lexer, styleName, sourceName, "inherit" );
		} else if ( token.Icmp( "model" ) == 0 ) {
			BotParseString( lexer, model, sourceName, "model" );
		} else if ( token.Icmp( "modelMarine" ) == 0 ) {
			BotParseString( lexer, modelMarine, sourceName, "modelMarine" );
		} else if ( token.Icmp( "modelStrogg" ) == 0 ) {
			BotParseString( lexer, modelStrogg, sourceName, "modelStrogg" );
		} else if ( token.Icmp( "skillBand" ) == 0 ) {
			if ( BotParseNumber( lexer, number, sourceName, "skillBand" ) ) {
				minSkill = idMath::ClampInt( 1, BOT_SKILL_LEVELS, (int)number );
				maxSkill = minSkill;

				// "skillBand 5" is a one-level band and must not warn that the
				// second value is missing; only a value that is there is read.
				idToken second;
				if ( lexer.ReadTokenOnLine( &second ) ) {
					lexer.UnreadToken( &second );
					if ( BotParseNumber( lexer, number, sourceName, "skillBand" ) ) {
						maxSkill = idMath::ClampInt( minSkill, BOT_SKILL_LEVELS, (int)number );
					}
				}
			}
			lexer.SkipRestOfLine();
		} else if ( token.Icmp( "weapon" ) == 0 ) {
			BotParseWeaponBias( lexer, weaponBias, sourceName, owner.c_str() );
		} else if ( token.Icmp( "skill" ) == 0 ) {
			if ( !BotParseSkillBlock( lexer, skillMods, sourceName, owner.c_str() ) ) {
				return false;
			}
		} else if ( token.Icmp( "chat" ) == 0 ) {
			if ( !ParseChatBlock( lexer, sourceName ) ) {
				return false;
			}
		} else {
			BotSkipUnknownKey( lexer, token, sourceName, owner.c_str() );
		}
	}

	gameLocal.Warning( "bot file '%s': unexpected end of file inside character '%s'", sourceName, name.c_str() );

	return false;
}

/*
================
rvBotCharacter::Apply

Style first, then this character's body, then this character's overrides for
the level being played.  A character with no style is a pure skill curve with a
name and a voice, which is a legitimate thing to ship.
================
*/
void rvBotCharacter::Apply( botTraits_t &traits, int skillLevel ) const {
	if ( style ) {
		style->Apply( traits, skillLevel );
	}

	mods.Apply( traits );
	BotMergeWeaponBias( traits, weaponBias );

	if ( skillLevel >= 1 && skillLevel <= BOT_SKILL_LEVELS ) {
		skillMods[skillLevel - 1].Apply( traits );
	}
}

/*
===============================================================================

	rvBotCharacterManager

===============================================================================
*/

/*
================
rvBotCharacterManager::rvBotCharacterManager
================
*/
rvBotCharacterManager::rvBotCharacterManager( void ) {
	loaded				= false;
	nextGlobalChatTime	= 0;

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		nextClientChatTime[i] = 0;
	}
}

/*
================
rvBotCharacterManager::FreeAll
================
*/
void rvBotCharacterManager::FreeAll( void ) {
	characters.DeleteContents( true );
	styles.DeleteContents( true );

	loaded = false;
}

/*
================
rvBotCharacterManager::Init

Styles are read first, then characters, then the separate chat files that name
their owning character.  A character's inherit is resolved by name in a second
pass anyway, so neither file order nor which directory a mod adds to matters.
================
*/
void rvBotCharacterManager::Init( void ) {
	MEM_SCOPED_TAG( tag, MA_AI );

	FreeAll();
	ResetChatThrottle();

	styles.SetGranularity( 8 );
	characters.SetGranularity( 16 );

	idFileList *files = fileSystem->ListFiles( BOT_STYLE_DIR, BOT_STYLE_EXT );

	if ( files ) {
		for ( int i = 0; i < files->GetNumFiles(); i++ ) {
			idStr path = idStr( BOT_STYLE_DIR ) + "/" + files->GetFile( i );

			LoadStyleFile( path.c_str() );
		}

		// The list is engine allocated and the game module runs under its own
		// allocator; releasing it any other way corrupts the heap.
		fileSystem->FreeFileList( files );
	}

	files = fileSystem->ListFiles( BOT_CHARACTER_DIR, BOT_CHARACTER_EXT );

	if ( files ) {
		for ( int i = 0; i < files->GetNumFiles(); i++ ) {
			idStr path = idStr( BOT_CHARACTER_DIR ) + "/" + files->GetFile( i );

			LoadCharacterFile( path.c_str() );
		}

		fileSystem->FreeFileList( files );
	}

	files = fileSystem->ListFiles( BOT_CHAT_DIR, BOT_CHAT_EXT );

	if ( files ) {
		for ( int i = 0; i < files->GetNumFiles(); i++ ) {
			idStr path = idStr( BOT_CHAT_DIR ) + "/" + files->GetFile( i );

			LoadChatFile( path.c_str() );
		}

		fileSystem->FreeFileList( files );
	}

	ResolveInheritance();

	loaded = true;

	// Not a warning when there is nothing to load: a mod is entitled to ship no
	// characters at all, and bot_characters 0 is a supported configuration.
	gameLocal.Printf( "bot characters: %d style%s, %d character%s\n",
					  styles.Num(), styles.Num() == 1 ? "" : "s",
					  characters.Num(), characters.Num() == 1 ? "" : "s" );
}

/*
================
rvBotCharacterManager::Shutdown
================
*/
void rvBotCharacterManager::Shutdown( void ) {
	FreeAll();
	ResetChatThrottle();
}

/*
================
rvBotCharacterManager::Reload

Everything this manager has ever handed out is invalidated here: the parsed
objects are deleted and re-read, so every rvBotCharacter pointer a bot is
holding dangles the moment this returns.  The caller is responsible for
re-binding each live bot by name before the next frame - which is why the
botreload command has to do the rebinding and this function cannot.

ReleaseCharacter is deliberately written to survive being handed one of those
stale pointers, so the ordering mistake degrades into a no-op rather than a
crash.
================
*/
bool rvBotCharacterManager::Reload( void ) {
	Init();

	return ( styles.Num() > 0 || characters.Num() > 0 );
}

/*
================
rvBotCharacterManager::LoadStyleFile
================
*/
bool rvBotCharacterManager::LoadStyleFile( const char *fileName ) {
	// A fresh lexer per file.  idLexer::LoadFile calls common->Error when a
	// script is already loaded, so one reused instance would kill the game on
	// the second file.
	idLexer	lexer;
	idToken	token;

	lexer.SetFlags( DECL_LEXER_FLAGS );

	if ( !lexer.LoadFile( fileName ) ) {
		gameLocal.Warning( "bot style file '%s' could not be opened", fileName );
		return false;
	}

	while ( lexer.ReadToken( &token ) ) {
		if ( token.Icmp( "style" ) != 0 ) {
			BotSkipUnknownKey( lexer, token, fileName, "top level" );
			continue;
		}

		lexer.UnreadToken( &token );

		rvBotStyle *style = new rvBotStyle;

		if ( !style->Parse( lexer, fileName ) ) {
			delete style;
			return false;
		}

		if ( !style->GetName()[0] ) {
			gameLocal.Warning( "bot style file '%s' has a style with an empty name", fileName );
			delete style;
			continue;
		}

		// First definition wins.  Replacing would make the result depend on the
		// order the file system happened to enumerate two files in, which is
		// not something a mod author can control or predict.
		if ( FindStyle( style->GetName() ) ) {
			gameLocal.Warning( "bot style '%s' in '%s' is already defined, ignored", style->GetName(), fileName );
			delete style;
			continue;
		}

		styles.Append( style );
	}

	return true;
}

/*
================
rvBotCharacterManager::LoadCharacterFile
================
*/
bool rvBotCharacterManager::LoadCharacterFile( const char *fileName ) {
	idLexer	lexer;
	idToken	token;

	lexer.SetFlags( DECL_LEXER_FLAGS );

	if ( !lexer.LoadFile( fileName ) ) {
		gameLocal.Warning( "bot character file '%s' could not be opened", fileName );
		return false;
	}

	while ( lexer.ReadToken( &token ) ) {
		if ( token.Icmp( "character" ) != 0 ) {
			BotSkipUnknownKey( lexer, token, fileName, "top level" );
			continue;
		}

		lexer.UnreadToken( &token );

		rvBotCharacter *character = new rvBotCharacter;

		if ( !character->Parse( lexer, fileName ) ) {
			delete character;
			return false;
		}

		if ( !character->GetName()[0] ) {
			gameLocal.Warning( "bot character file '%s' has a character with an empty name", fileName );
			delete character;
			continue;
		}

		// The name becomes ui_name, and two players with the same name get
		// suffixed by idGameLocal::SetUserInfo, so a duplicate is not merely
		// redundant - it would produce a scoreboard nobody can read.
		if ( FindCharacter( character->GetName() ) ) {
			gameLocal.Warning( "bot character '%s' in '%s' is already defined, ignored", character->GetName(), fileName );
			delete character;
			continue;
		}

		characters.Append( character );
	}

	return true;
}

/*
================
rvBotCharacterManager::LoadChatFile

Reads one or more:

	characterChat "Name" {
		chat <event> {
			"line"
		}
		reply <name> {
			priority 50;
			source any;
			addressed either;
			trigger "hello";
			"Hello, $other."
		}
	}

blocks and appends their dialogue to the named character.  Characters have already
been loaded, so ownership is resolved immediately and case-insensitively.  Each
owner block is staged in a temporary character and committed only after its
closing brace is read, so a malformed owner block cannot leave its early lines
attached to an otherwise valid character.
Unknown owners and keys are content warnings, never a reason to abort loading
the rest of the roster.
================
*/
bool rvBotCharacterManager::LoadChatFile( const char *fileName ) {
	idLexer	lexer;
	idToken	token;

	lexer.SetFlags( DECL_LEXER_FLAGS );

	if ( !lexer.LoadFile( fileName ) ) {
		gameLocal.Warning( "bot chat file '%s' could not be opened", fileName );
		return false;
	}

	while ( lexer.ReadToken( &token ) ) {
		if ( token.Icmp( "characterChat" ) != 0 ) {
			BotSkipUnknownKey( lexer, token, fileName, "top level" );
			continue;
		}

		if ( !lexer.ReadToken( &token ) ) {
			gameLocal.Warning( "bot chat file '%s': 'characterChat' with no character name", fileName );
			return false;
		}

		const idStr characterName = token;

		if ( !lexer.ExpectTokenString( "{" ) ) {
			return false;
		}

		rvBotCharacter *character = NULL;

		for ( int i = 0; i < characters.Num(); i++ ) {
			if ( idStr::Icmp( characters[i]->GetName(), characterName.c_str() ) == 0 ) {
				character = characters[i];
				break;
			}
		}

		if ( !character ) {
			gameLocal.Warning( "bot chat file '%s' line %d: characterChat names unknown character '%s', block ignored",
							   fileName, lexer.GetLineNum(), characterName.c_str() );

			if ( !lexer.SkipBracedSection( false ) ) {
				return false;
			}

			continue;
		}

		const idStr owner = idStr( "characterChat '" ) + characterName + "'";
		rvBotCharacter stagedChat;

		// The block parsers name their owner in diagnostics.  Keeping the staged
		// object otherwise empty also means only dialogue can be merged below.
		stagedChat.name = characterName;

		bool		closed = false;

		while ( lexer.ReadToken( &token ) ) {
			if ( token == "}" ) {
				closed = true;
				break;
			}

			if ( token.Icmp( "chat" ) == 0 ) {
				if ( !stagedChat.ParseChatBlock( lexer, fileName ) ) {
					return false;
				}
			} else if ( token.Icmp( "reply" ) == 0 ) {
				if ( !stagedChat.ParseReplyBlock( lexer, fileName ) ) {
					return false;
				}
			} else {
				BotSkipUnknownKey( lexer, token, fileName, owner.c_str() );
			}
		}

		if ( !closed ) {
			gameLocal.Warning( "bot chat file '%s': unexpected end of file inside characterChat '%s'", fileName, characterName.c_str() );
			return false;
		}

		for ( int event = 0; event < BOTCHAT_NUM; event++ ) {
			for ( int line = 0; line < stagedChat.chat[event].Num(); line++ ) {
				character->chat[event].Append( stagedChat.chat[event][line] );
			}
		}

		for ( int rule = 0; rule < stagedChat.replies.Num(); rule++ ) {
			if ( character->replies.Num() >= BOT_MAX_REPLY_RULES ) {
				gameLocal.Warning( "bot chat file '%s' line %d: character '%s' exceeds the %d reply-rule limit; remaining rules in this block were dropped",
								   fileName, lexer.GetLineNum(), characterName.c_str(), BOT_MAX_REPLY_RULES );
				break;
			}
			character->replies.Append( stagedChat.replies[rule] );
		}
	}

	return true;
}

/*
================
rvBotCharacterManager::ResolveInheritance

A second pass so file order does not matter, and the only place an inherit
cycle can be caught.  Breaking it here means rvBotStyle::Apply, which runs on
every bot spawn, needs no depth counter of its own.
================
*/
void rvBotCharacterManager::ResolveInheritance( void ) {
	int i;

	for ( i = 0; i < styles.Num(); i++ ) {
		rvBotStyle *style = styles[i];

		style->parent = NULL;

		if ( !style->inherit.Length() ) {
			continue;
		}

		if ( style->inherit.Icmp( style->name.c_str() ) == 0 ) {
			gameLocal.Warning( "bot style '%s' in '%s' inherits itself", style->GetName(), style->GetFileName() );
			continue;
		}

		style->parent = FindStyle( style->inherit.c_str() );

		if ( !style->parent ) {
			gameLocal.Warning( "bot style '%s' in '%s' inherits unknown style '%s'", style->GetName(), style->GetFileName(), style->inherit.c_str() );
		}
	}

	// Now that every link exists, walk each chain and cut any that loops or
	// simply runs too deep to be anything but a mistake.
	for ( i = 0; i < styles.Num(); i++ ) {
		const rvBotStyle *walk	= styles[i]->parent;
		int				 depth	= 0;

		while ( walk && depth < BOT_MAX_INHERIT_DEPTH ) {
			if ( walk == styles[i] ) {
				break;
			}
			walk = walk->parent;
			depth++;
		}

		if ( walk ) {
			gameLocal.Warning( "bot style '%s' in '%s' has a circular or over-deep inherit chain, its parent was dropped", styles[i]->GetName(), styles[i]->GetFileName() );
			styles[i]->parent = NULL;
		}
	}

	for ( i = 0; i < characters.Num(); i++ ) {
		rvBotCharacter *character = characters[i];

		character->style = NULL;

		if ( !character->styleName.Length() ) {
			continue;
		}

		character->style = FindStyle( character->styleName.c_str() );

		if ( !character->style ) {
			gameLocal.Warning( "bot character '%s' in '%s' inherits unknown style '%s'", character->GetName(), character->GetFileName(), character->styleName.c_str() );
		}
	}
}

/*
================
rvBotCharacterManager::FindCharacter
================
*/
const rvBotCharacter *rvBotCharacterManager::FindCharacter( const char *name ) const {
	if ( !name || !name[0] ) {
		return NULL;
	}

	for ( int i = 0; i < characters.Num(); i++ ) {
		if ( idStr::Icmp( characters[i]->GetName(), name ) == 0 ) {
			return characters[i];
		}
	}

	return NULL;
}

/*
================
rvBotCharacterManager::FindStyle
================
*/
const rvBotStyle *rvBotCharacterManager::FindStyle( const char *name ) const {
	if ( !name || !name[0] ) {
		return NULL;
	}

	for ( int i = 0; i < styles.Num(); i++ ) {
		if ( idStr::Icmp( styles[i]->GetName(), name ) == 0 ) {
			return styles[i];
		}
	}

	return NULL;
}

/*
================
rvBotCharacterManager::PickCharacter

Three passes, widening each time, because a full roster must never be the
reason a bot cannot be added.  A free identity outranks a matching skill band
in the second pass on purpose: a bot playing one level outside its band is a
tuning detail nobody will notice, while two bots wearing the same name produce
a scoreboard with "Voss" and "Voss(1)" on it.
================
*/
const rvBotCharacter *rvBotCharacterManager::PickCharacter( int skillLevel, bool markUsed ) {
	int i;

	// Characters are identity, and identity is what this cvar switches off; do
	// it here rather than at each of the call sites that would have to remember.
	if ( !bot_characters.GetBool() || !characters.Num() ) {
		return NULL;
	}

	const char *forced = bot_forceCharacter.GetString();

	if ( forced && forced[0] ) {
		rvBotCharacter *character = NULL;

		for ( i = 0; i < characters.Num(); i++ ) {
			if ( idStr::Icmp( characters[i]->GetName(), forced ) == 0 ) {
				character = characters[i];
				break;
			}
		}

		if ( !character ) {
			gameLocal.Warning( "bot_forceCharacter names unknown character '%s'", forced );
		} else {
			// Deliberately ignores the in-use flag and the band: a server that
			// forces one character is asking for a field of clones, and would
			// otherwise get exactly one bot and then silence.
			if ( markUsed ) {
				character->inUse = true;
			}
			return character;
		}
	}

	for ( int pass = 0; pass < 3; pass++ ) {
		rvBotCharacter *chosen	= NULL;
		int				seen	= 0;

		for ( i = 0; i < characters.Num(); i++ ) {
			const bool available	= !characters[i]->IsInUse();
			const bool inBand		= characters[i]->FitsSkill( skillLevel );

			if ( pass == 0 && !( available && inBand ) ) {
				continue;
			}
			if ( pass == 1 && !available ) {
				continue;
			}

			// Reservoir sampling: an even choice among the candidates in one
			// walk, with no list to size and therefore no cap on how many
			// characters a mod may ship.  Random rather than first match so a
			// server that fills and empties does not seat the same roster in
			// the same order every time.
			seen++;

			if ( gameLocal.random.RandomInt( seen ) == 0 ) {
				chosen = characters[i];
			}
		}

		if ( !chosen ) {
			continue;
		}

		if ( markUsed ) {
			chosen->inUse = true;
		}

		return chosen;
	}

	return NULL;
}

/*
================
rvBotCharacterManager::MarkCharacterUsed

Same defensive shape as ReleaseCharacter: the pointer is matched against the
roster rather than written through, so a stale one is ignored instead of
followed into freed memory.
================
*/
void rvBotCharacterManager::MarkCharacterUsed( const rvBotCharacter *character ) {
	if ( !character ) {
		return;
	}

	for ( int i = 0; i < characters.Num(); i++ ) {
		if ( characters[i] == character ) {
			characters[i]->inUse = true;
			return;
		}
	}
}

/*
================
rvBotCharacterManager::ReleaseCharacter

Compares against the owned pointers instead of writing through the one it was
handed, so a pointer left over from before a Reload is quietly ignored rather
than followed into freed memory.
================
*/
void rvBotCharacterManager::ReleaseCharacter( const rvBotCharacter *character ) {
	if ( !character ) {
		return;
	}

	for ( int i = 0; i < characters.Num(); i++ ) {
		if ( characters[i] == character ) {
			characters[i]->inUse = false;
			return;
		}
	}
}

/*
================
rvBotCharacterManager::ReleaseAllCharacters
================
*/
void rvBotCharacterManager::ReleaseAllCharacters( void ) {
	for ( int i = 0; i < characters.Num(); i++ ) {
		characters[i]->inUse = false;
	}
}

/*
================
rvBotCharacterManager::NumTraitFields
================
*/
int rvBotCharacterManager::NumTraitFields( void ) {
	return botNumTraitFields;
}

/*
================
rvBotCharacterManager::GetTraitField
================
*/
const botTraitField_t *rvBotCharacterManager::GetTraitField( int index ) {
	if ( index < 0 || index >= botNumTraitFields ) {
		return NULL;
	}

	return &botTraitTable[index].field;
}

/*
================
rvBotCharacterManager::FindTraitField
================
*/
int rvBotCharacterManager::FindTraitField( const char *name ) {
	if ( !name || !name[0] ) {
		return -1;
	}

	for ( int i = 0; i < botNumTraitFields; i++ ) {
		if ( idStr::Icmp( botTraitTable[i].field.name, name ) == 0 ) {
			return i;
		}
	}

	return -1;
}

/*
================
rvBotCharacterManager::TraitFieldPtr
================
*/
float *rvBotCharacterManager::TraitFieldPtr( botTraits_t &traits, int field ) {
	if ( field < 0 || field >= botNumTraitFields ) {
		return NULL;
	}

	return (float *)( (byte *)&traits + botTraitTable[field].field.offset );
}

/*
================
rvBotCharacterManager::WeaponBias
================
*/
float rvBotCharacterManager::WeaponBias( const botTraits_t &traits, const char *weaponName ) {
	if ( !weaponName || !weaponName[0] ) {
		return 1.0f;
	}

	const int num = idMath::ClampInt( 0, BOT_MAX_WEAPON_BIAS, traits.numWeaponBias );

	for ( int i = 0; i < num; i++ ) {
		if ( traits.weaponBias[i].weapon.Icmp( weaponName ) == 0 ) {
			return traits.weaponBias[i].bias;
		}
	}

	// Neutral, not zero: a personality that says nothing about a weapon has no
	// opinion about it, and must not accidentally forbid it.
	return 1.0f;
}

/*
================
rvBotCharacterManager::EffectiveSkill
================
*/
float rvBotCharacterManager::EffectiveSkill( int skillLevel, float variance, int seed ) {
	const float level = (float)idMath::ClampInt( 1, BOT_SKILL_LEVELS, skillLevel );

	variance = idMath::ClampFloat( 0.0f, 2.0f, variance );

	if ( variance <= 0.0f ) {
		return level;
	}

	idRandom rnd;

	BotSeededRandom( rnd, seed );

	// Reflected off the ends rather than clamped: clamping bot_skill 1 with a
	// variance of 1 left half the roster at exactly 1.0, when "either side"
	// means a spread.  A roll that would leave the range comes back in by the
	// same amount, so the size of the spread is kept and only its side changes.
	float effective = level + rnd.CRandomFloat() * variance;
	if ( effective < 1.0f ) {
		effective = 2.0f - effective;
	} else if ( effective > (float)BOT_SKILL_LEVELS ) {
		effective = 2.0f * (float)BOT_SKILL_LEVELS - effective;
	}

	return idMath::ClampFloat( 1.0f, (float)BOT_SKILL_LEVELS, effective );
}

/*
================
rvBotCharacterManager::BaselineTraits

Layer 1, evaluated at a fractional level.  This alone is a complete, playable
bot; everything above it only nudges these numbers around.
================
*/
void rvBotCharacterManager::BaselineTraits( float skillLevel, botTraits_t &traits ) {
	int i;

	// botTraits_t carries idStr, so it can never be memset - the strings would
	// be left pointing at their own freed heap buffers.  Clear the weapon list
	// by hand; every scalar is unconditionally written from the curve below.
	traits.numWeaponBias = 0;

	for ( i = 0; i < BOT_MAX_WEAPON_BIAS; i++ ) {
		traits.weaponBias[i].weapon.Clear();
		traits.weaponBias[i].bias = 1.0f;
	}

	skillLevel = idMath::ClampFloat( 1.0f, (float)BOT_SKILL_LEVELS, skillLevel );

	// The lower of the two bracketing rows, held one short of the end so that
	// lo + 1 is always a real row and skill 5.0 lands exactly on the last one.
	const int	lo		= idMath::ClampInt( 0, BOT_SKILL_LEVELS - 2, (int)idMath::Floor( skillLevel ) - 1 );
	const float frac	= skillLevel - (float)( lo + 1 );

	for ( i = 0; i < botNumTraitFields; i++ ) {
		const botTraitDef_t &def = botTraitTable[i];

		float *value = (float *)( (byte *)&traits + def.field.offset );

		*value = def.curve[lo] + ( def.curve[lo + 1] - def.curve[lo] ) * frac;
	}
}

/*
================
rvBotCharacterManager::ClampTraits
================
*/
void rvBotCharacterManager::ClampTraits( botTraits_t &traits ) {
	for ( int i = 0; i < botNumTraitFields; i++ ) {
		const botTraitDef_t &def = botTraitTable[i];

		float *value = (float *)( (byte *)&traits + def.field.offset );

		// A NaN has to be answered before the clamp, not by it.  idMath::ClampFloat
		// is two comparisons, and every comparison against a NaN is false, so a
		// NaN is returned untouched by the one thing this format relies on to
		// keep bad content from producing a nonsense bot.  Infinity is fine - it
		// compares and clamps normally - which is why this is not FLOAT_IS_NAN on
		// its own, that macro is an exponent test and answers true for both.
		if ( FLOAT_IS_NAN( *value ) && !FLOAT_IS_INF( *value ) ) {
			*value = def.field.min;
			continue;
		}

		*value = idMath::ClampFloat( def.field.min, def.field.max, *value );
	}

	traits.numWeaponBias = idMath::ClampInt( 0, BOT_MAX_WEAPON_BIAS, traits.numWeaponBias );

	// A burst window whose minimum is above its maximum would make the trigger
	// logic pick a negative length.  Content can express that in two lines that
	// each look reasonable on their own, so fix it here rather than asking
	// every reader of the traits to defend itself.
	if ( traits.burstMaxMsec < traits.burstMinMsec ) {
		traits.burstMaxMsec = traits.burstMinMsec;
	}
}

/*
================
rvBotCharacterManager::ResolveTraits

The single place difficulty is decided.  Everything downstream reads the flat
struct this produces and never asks what skill level, style or character it
came from.
================
*/
void rvBotCharacterManager::ResolveTraits( const rvBotCharacter *character, int skillLevel, float variance, int seed, botTraits_t &traits ) const {
	const float effective = EffectiveSkill( skillLevel, variance, seed );

	BaselineTraits( effective, traits );

	if ( character ) {
		// The skill override blocks are discrete, so a bot that variance has
		// put at 3.4 takes the level 3 blocks and one at 3.6 takes level 4.
		// Rounding rather than truncating keeps the blocks centred on the level
		// they are named for.
		const int level = idMath::ClampInt( 1, BOT_SKILL_LEVELS, (int)idMath::Floor( effective + 0.5f ) );

		character->Apply( traits, level );
	}

	// bot_chat 2 is "chatty", and the only thing that means is that a
	// chat-worthy event is more likely to produce a line.  That bonus is NOT
	// folded in here.  Traits are resolved once per spawn, so baking a live
	// cvar into them would leave an operator who sets bot_chat 2 mid-match with
	// the halved throttles - which are read at the point of use - but the old
	// chatiness until everybody had died once.  Both chat sites apply it to
	// their own copy instead; see rvBot::QueueChat and rvBot::TryQueueReply.
	ClampTraits( traits );
}

/*
================
rvBotCharacterManager::ChatEventName
================
*/
const char *rvBotCharacterManager::ChatEventName( rvBotChatEvent event ) {
	if ( (int)event < 0 || (int)event >= BOTCHAT_NUM ) {
		return "<none>";
	}

	return botChatEventNames[event];
}

/*
================
rvBotCharacterManager::ChatEventForName
================
*/
rvBotChatEvent rvBotCharacterManager::ChatEventForName( const char *name ) {
	if ( name && name[0] ) {
		for ( int i = 0; i < BOTCHAT_NUM; i++ ) {
			if ( idStr::Icmp( botChatEventNames[i], name ) == 0 ) {
				return (rvBotChatEvent)i;
			}
		}
	}

	return BOTCHAT_NUM;
}

/*
================
BotChatTokenTable

The tokens a line may name, paired with the value this event carries for each.
One table so substitution and the can-this-line-be-used test can never drift
apart - a line rejected for an empty token is exactly a line substitution would
have mangled.
================
*/
struct botChatTokenBinding_t {
	const char *	token;
	const idStr *	value;
};

static int BotChatTokenTable( const botChatTokens_t &tokens, botChatTokenBinding_t *out, int maxOut ) {
	const botChatTokenBinding_t table[] = {
		{ "$self",		&tokens.self	},
		{ "$other",		&tokens.other	},
		{ "$weapon",	&tokens.weapon	},
		{ "$map",		&tokens.map		},
		{ "$item",		&tokens.item	},
	};

	const int count = Min( (int)( sizeof( table ) / sizeof( table[0] ) ), maxOut );

	for ( int i = 0; i < count; i++ ) {
		out[i] = table[i];
	}

	return count;
}

/*
================
BotChatLineIsUsable

False when the line names a token this event has no value for.  Substituting
nothing does not read "sensibly" the way it first appears to: the punctuation
the author wrote around the token survives, so "Taking notes, $other." comes
out as "Taking notes, ." on the frag that had no other player in it.  Rejecting
the line and picking a different one keeps the authored punctuation honest, and
a character whose every line for an event needs a missing token simply stays
quiet - silence is already a valid answer here.
================
*/
static bool BotChatLineIsUsable( const idStr &line, const botChatTokens_t &tokens ) {
	botChatTokenBinding_t	bindings[8];
	const int				count = BotChatTokenTable( tokens, bindings, 8 );

	for ( int i = 0; i < count; i++ ) {
		if ( bindings[i].value->IsEmpty() && line.Find( bindings[i].token, false ) != -1 ) {
			return false;
		}
	}

	return true;
}

/*
================
BotSubstituteChatTokens
================
*/
static void BotSubstituteChatTokens( idStr &line, const botChatTokens_t &tokens ) {
	botChatTokenBinding_t	bindings[8];
	const int				count = BotChatTokenTable( tokens, bindings, 8 );

	for ( int i = 0; i < count; i++ ) {
		line.Replace( bindings[i].token, bindings[i].value->c_str() );
	}
}

/*
================
rvBotCharacterManager::NormalizeReplyText

Both authored triggers and live chat pass through this exact function.  Escape
sequences disappear, case folds, and every run of punctuation becomes one
space, leaving a stable word stream for phrase and addressed-name matching.
================
*/
void rvBotCharacterManager::NormalizeReplyText( const char *text, idStr &out ) {
	out.Clear();

	if ( !text || !text[0] ) {
		return;
	}

	idStr clean = text;
	clean.RemoveEscapes( S_ESCAPE_ALL );

	bool needSpace = false;

	for ( int i = 0; i < clean.Length(); i++ ) {
		const byte c = (byte)clean[i];

		if ( idStr::CharIsAlpha( c ) || idStr::CharIsNumeric( c ) ) {
			if ( needSpace && out.Length() ) {
				out.Append( ' ' );
			}
			out.Append( idStr::ToLower( c ) );
			needSpace = false;
		} else if ( out.Length() ) {
			needSpace = true;
		}
	}
}

/*
================
rvBotCharacterManager::ReplyPhraseMatches

The input is normalized, but boundaries remain explicit here rather than
assuming every caller remembered to pad its strings.
================
*/
bool rvBotCharacterManager::ReplyPhraseMatches( const idStr &normalizedText, const idStr &normalizedPhrase ) {
	if ( normalizedText.IsEmpty() || normalizedPhrase.IsEmpty() ) {
		return false;
	}

	int start = 0;

	while ( start < normalizedText.Length() ) {
		const int found = normalizedText.Find( normalizedPhrase.c_str(), true, start );

		if ( found == -1 ) {
			return false;
		}

		const int end = found + normalizedPhrase.Length();
		const bool leftBoundary = ( found == 0 || normalizedText[found - 1] == ' ' );
		const bool rightBoundary = ( end == normalizedText.Length() || normalizedText[end] == ' ' );

		if ( leftBoundary && rightBoundary ) {
			return true;
		}

		start = found + 1;
	}

	return false;
}

/*
================
rvBotCharacterManager::ReplyNameMatches
================
*/
bool rvBotCharacterManager::ReplyNameMatches( const char *name, const idStr &normalizedText ) {
	idStr normalizedName;
	NormalizeReplyText( name, normalizedName );

	return ReplyPhraseMatches( normalizedText, normalizedName );
}

/*
================
BotFindReplyRule

Priority is the author's first discriminator; among equal priorities the most
specific (longest) matching trigger wins.  File order is the deterministic
tie-breaker after both.
================
*/
static int BotFindReplyRule( const idList<botReplyRule_t> &rules, const idStr &normalizedText,
							 bool sourceIsBot, bool addressed ) {
	int bestRule = -1;
	int bestPriority = -1;
	int bestTriggerLength = -1;

	for ( int i = 0; i < rules.Num(); i++ ) {
		const botReplyRule_t &rule = rules[i];

		if ( !rule.lines.Num() ) {
			continue;
		}
		if ( rule.source == BOTREPLYSOURCE_PLAYER && sourceIsBot ) {
			continue;
		}
		if ( rule.source == BOTREPLYSOURCE_BOT && !sourceIsBot ) {
			continue;
		}
		if ( rule.addressed == BOTREPLYADDRESS_REQUIRED && !addressed ) {
			continue;
		}
		if ( rule.addressed == BOTREPLYADDRESS_FORBIDDEN && addressed ) {
			continue;
		}

		int triggerLength = -1;

		for ( int trigger = 0; trigger < rule.triggers.Num(); trigger++ ) {
			if ( rvBotCharacterManager::ReplyPhraseMatches( normalizedText, rule.triggers[trigger] ) ) {
				triggerLength = Max( triggerLength, rule.triggers[trigger].Length() );
			}
		}

		if ( triggerLength == -1 ) {
			continue;
		}

		if ( rule.priority > bestPriority ||
			 ( rule.priority == bestPriority && triggerLength > bestTriggerLength ) ) {
			bestRule = i;
			bestPriority = rule.priority;
			bestTriggerLength = triggerLength;
		}
	}

	return bestRule;
}

/*
================
rvBotCharacterManager::HasReply
================
*/
bool rvBotCharacterManager::HasReply( const rvBotCharacter *character, const idStr &normalizedText,
									  bool sourceIsBot, bool addressed ) const {
	if ( !character || !character->HasReply() ) {
		return false;
	}

	return BotFindReplyRule( character->replies, normalizedText, sourceIsBot, addressed ) != -1;
}

/*
================
rvBotCharacterManager::ReplyLine

Chooses a response from the best matching rule without repeating its last line.
================
*/
bool rvBotCharacterManager::ReplyLine( const rvBotCharacter *character, const idStr &normalizedText,
									   bool sourceIsBot, bool addressed, const botChatTokens_t &tokens,
									   idStr &out ) const {
	out.Clear();

	if ( !character ) {
		return false;
	}

	const int ruleIndex = BotFindReplyRule( character->replies, normalizedText, sourceIsBot, addressed );

	if ( ruleIndex == -1 ) {
		return false;
	}

	const botReplyRule_t &rule = character->replies[ruleIndex];
	idList<int> usable;
	int usableLast = -1;

	for ( int i = 0; i < rule.lines.Num(); i++ ) {
		if ( !BotChatLineIsUsable( rule.lines[i], tokens ) ) {
			continue;
		}

		if ( i == rule.lastLine ) {
			usableLast = i;
		} else {
			usable.Append( i );
		}
	}

	if ( !usable.Num() ) {
		if ( usableLast == -1 ) {
			return false;
		}
		usable.Append( usableLast );
	}

	const int lineIndex = usable[gameLocal.random.RandomInt( usable.Num() )];
	rule.lastLine = lineIndex;
	out = rule.lines[lineIndex];

	BotSubstituteChatTokens( out, tokens );

	if ( out.IsEmpty() ) {
		return false;
	}
	if ( out.Length() > BOT_CHAT_MAX_LEN ) {
		out.CapLength( BOT_CHAT_MAX_LEN );
	}
	if ( out[0] == '#' ) {
		out.Clear();
		return false;
	}

	return true;
}

/*
================
rvBotCharacterManager::ChatLine
================
*/
bool rvBotCharacterManager::ChatLine( const rvBotCharacter *character, rvBotChatEvent event, const botChatTokens_t &tokens, idStr &out ) const {
	out.Clear();

	if ( !character || (int)event < 0 || (int)event >= BOTCHAT_NUM ) {
		return false;
	}

	const idStrList &lines = character->chat[event];

	// Silence is a valid answer.  A character with nothing to say about an
	// event says nothing, and the caller must not invent a fallback line.
	if ( !lines.Num() ) {
		return false;
	}

	const int last = character->lastChat[event];

	// Gather the lines this event can actually fill in, preferring to leave out
	// the one that came out last so two lines alternate instead of one of them
	// repeating.  The last line is only reconsidered when it is the sole usable
	// candidate, which beats going silent over a repeat.
	idList<int>	usable;
	int			usableLast = -1;

	for ( int i = 0; i < lines.Num(); i++ ) {
		if ( !BotChatLineIsUsable( lines[i], tokens ) ) {
			continue;
		}

		if ( i == last ) {
			usableLast = i;
			continue;
		}

		usable.Append( i );
	}

	if ( !usable.Num() ) {
		if ( usableLast == -1 ) {
			return false;
		}

		usable.Append( usableLast );
	}

	const int index = usable[gameLocal.random.RandomInt( usable.Num() )];

	character->lastChat[event] = index;

	out = lines[index];

	BotSubstituteChatTokens( out, tokens );

	if ( !out.Length() ) {
		return false;
	}

	// A player name pasted into a line can push it over the limit that
	// ProcessChatMessage silently drops on.  A clipped line is a far better
	// failure than a line nobody ever sees.
	if ( out.Length() > BOT_CHAT_MAX_LEN ) {
		out.CapLength( BOT_CHAT_MAX_LEN );
	}

	// The '#' check at load time only saw the author's text; a substituted name
	// could have put one at the front.
	if ( out[0] == '#' ) {
		out.Clear();
		return false;
	}

	return true;
}

/*
================
rvBotCharacterManager::AllowChat

Both throttles are stamped together and only when the answer is yes, so a bot
that is refused does not push the next opportunity further away.
================
*/
bool rvBotCharacterManager::AllowChat( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return false;
	}

	const int mode = bot_chat.GetInteger();

	if ( mode <= 0 ) {
		return false;
	}

	int clientThrottle	= BOT_CHAT_CLIENT_THROTTLE_MSEC;
	int globalThrottle	= BOT_CHAT_GLOBAL_THROTTLE_MSEC;

	if ( mode >= 2 ) {
		clientThrottle	/= 2;
		globalThrottle	/= 2;
	}

	const int now = gameLocal.time;

	if ( now < nextGlobalChatTime || now < nextClientChatTime[clientNum] ) {
		return false;
	}

	nextGlobalChatTime				= now + globalThrottle;
	nextClientChatTime[clientNum]	= now + clientThrottle;

	return true;
}

/*
================
rvBotCharacterManager::ResetChatThrottle

gameLocal.time restarts at zero on a map change, so stamps taken on the last
map are in the future on this one and would gag every bot for the first several
seconds of the match.  Call this whenever the clock is reset.
================
*/
void rvBotCharacterManager::ResetChatThrottle( void ) {
	nextGlobalChatTime = 0;

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		nextClientChatTime[i] = 0;
	}
}

/*
================
rvBotCharacterManager::ShiftChatThrottle

The other half of the same problem: a competitive pause advances gameLocal.time
without running the bots, so these stamps have to move with it or the whole
server falls silent for the length of the pause once play resumes.
================
*/
void rvBotCharacterManager::ShiftChatThrottle( int deltaMsec ) {
	if ( deltaMsec <= 0 ) {
		return;
	}

	const int maxInt = 0x7fffffff;
#define SHIFT_CHAT_TIME( value ) \
	do { if ( ( value ) > 0 ) { ( value ) = ( value ) > maxInt - deltaMsec ? maxInt : ( value ) + deltaMsec; } } while ( 0 )

	SHIFT_CHAT_TIME( nextGlobalChatTime );

	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		SHIFT_CHAT_TIME( nextClientChatTime[i] );
	}

#undef SHIFT_CHAT_TIME
}

/*
================
rvBotCharacterManager::ListCharacters
================
*/
void rvBotCharacterManager::ListCharacters( void ) const {
	int i;

	if ( !loaded ) {
		gameLocal.Printf( "bot characters have not been loaded\n" );
		return;
	}

	gameLocal.Printf( "styles:\n" );

	for ( i = 0; i < styles.Num(); i++ ) {
		const rvBotStyle *style = styles[i];

		gameLocal.Printf( "  %-14s %-14s %s\n",
						  style->GetName(),
						  style->inherit.Length() ? style->inherit.c_str() : "-",
						  style->GetDescription() );
	}

	gameLocal.Printf( "characters:\n" );
	gameLocal.Printf( "  %-14s %-12s %-7s %-6s %s\n", "name", "style", "skill", "in use", "file" );

	for ( i = 0; i < characters.Num(); i++ ) {
		const rvBotCharacter *character = characters[i];

		gameLocal.Printf( "  %-14s %-12s %d - %-3d %-6s %s\n",
						  character->GetName(),
						  character->GetStyleName()[0] ? character->GetStyleName() : "-",
						  character->GetMinSkill(), character->GetMaxSkill(),
						  character->IsInUse() ? "yes" : "no",
						  character->GetFileName() );
	}

	gameLocal.Printf( "%d style%s, %d character%s\n",
					  styles.Num(), styles.Num() == 1 ? "" : "s",
					  characters.Num(), characters.Num() == 1 ? "" : "s" );
}
