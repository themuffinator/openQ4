// Quake 4 SDK EULA applies to the game headers this test compiles.
// Exercises the engine-free cores behind the MP-15 client options: chat tokens,
// the weapon preference order, hit sounds by damage, landmark place names, team
// overlay packing and the item-timer policy. Run by tools/tests/mp_client_options.py.
#define MP_CLIENT_OPTIONS_CORE_ONLY
#include "../../../src/mpgame/mp/ChatTokens.h"
#include "../../../src/mpgame/mp/WeaponOrder.h"
#include "../../../src/mpgame/mp/HitSounds.h"
#include "../../../src/mpgame/mp/Locations.h"
#include "../../../src/mpgame/mp/TeamInfo.h"
#include "../../../src/mpgame/mp/ItemTimers.h"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;

#define CHECK( condition ) \
	do { \
		if ( !( condition ) ) { \
			std::printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition ); \
			failures++; \
		} \
	} while ( 0 )

static std::string Expand( const char *text, const mpChatTokenValues_t &values, int bytes = MP_CHAT_TOKEN_TEXT_BYTES, int *count = 0 ) {
	char buffer[ 512 ];
	std::memset( buffer, 'X', sizeof( buffer ) );
	const int expanded = MPChatExpandTokens( text, values, buffer, bytes );
	if ( count ) {
		*count = expanded;
	}
	// nothing past the terminator may have been written
	CHECK( bytes <= 0 || buffer[ bytes ] == 'X' );
	return bytes > 0 ? std::string( buffer ) : std::string();
}

static void TestChatTokens( void ) {
	mpChatTokenValues_t values = { "87", "50", "Railgun (12)", "Quad Damage", "Ada, Bo" };
	int count = 0;

	CHECK( Expand( "need help #h/#a at #l", values, MP_CHAT_TOKEN_TEXT_BYTES, &count ) == "need help 87/50 at Quad Damage" );
	CHECK( count == 3 );
	CHECK( Expand( "#w with #n", values ) == "Railgun (12) with Ada, Bo" );
	// either case, at the end of the line and before punctuation
	CHECK( Expand( "#H, #A. #L", values ) == "87, 50. Quad Damage" );
	CHECK( Expand( "hp #h", values ) == "hp 87" );
	// a hash that is not a token is left as typed
	CHECK( Expand( "#help me #1 #str_100000 # #x", values, MP_CHAT_TOKEN_TEXT_BYTES, &count ) == "#help me #1 #str_100000 # #x" );
	CHECK( count == 0 );
	CHECK( Expand( "#hp #armor", values ) == "#hp #armor" );
	// a doubled hash is one literal hash and stops the token
	CHECK( Expand( "##h is #h", values, MP_CHAT_TOKEN_TEXT_BYTES, &count ) == "#h is 87" );
	CHECK( count == 1 );
	CHECK( Expand( "", values ) == "" );
	CHECK( Expand( "#", values ) == "#" );

	// a missing value never makes the token vanish
	mpChatTokenValues_t empty = { 0, "", 0, 0, 0 };
	CHECK( Expand( "#h #a #w #l #n", empty, MP_CHAT_TOKEN_TEXT_BYTES, &count ) == "- - - - -" );
	CHECK( count == 5 );

	// control characters in a value become spaces: a value is one line
	mpChatTokenValues_t hostile = { "1\n2", "a\tb", "", "", "" };
	CHECK( Expand( "#h #a", hostile ) == "1 2 a b" );

	// the budget holds, and the cut respects UTF-8 and colour escapes
	std::string longText( 300, 'a' );
	CHECK( Expand( longText.c_str(), values ).size() == (size_t)( MP_CHAT_TOKEN_TEXT_BYTES - 1 ) );
	mpChatTokenValues_t accent = { "\xC3\xA9\xC3\xA9\xC3\xA9", "", "", "", "" };
	CHECK( Expand( "ab#h", accent, 6 ) == "ab\xC3\xA9" );		// 2 + 2 bytes fit, the next pair does not
	CHECK( Expand( "ab#h", accent, 5 ) == "ab\xC3\xA9" );
	CHECK( Expand( "ab#h", accent, 4 ) == "ab" );				// never half a character
	mpChatTokenValues_t wide = { "\xE2\x82\xAC", "", "", "", "" };	// three bytes
	CHECK( Expand( "a#h", wide, 4 ) == "a" );
	CHECK( Expand( "a#h", wide, 5 ) == "a\xE2\x82\xAC" );
	CHECK( Expand( "abc^1def", values, 5 ) == "abc" );			// not "abc^"
	CHECK( Expand( "ab^^1def", values, 5 ) == "ab^^" );			// an escaped caret is whole
	CHECK( Expand( "#h", values, 1 ) == "" );
	CHECK( Expand( "#h", values, 0 ) == "" );
	char untouched[ 4 ] = { 'k', 'e', 'e', 'p' };
	CHECK( MPChatExpandTokens( "#h", values, untouched, 0 ) == 0 && untouched[ 0 ] == 'k' );
	CHECK( MPChatExpandTokens( 0, values, untouched, 4 ) == 0 && untouched[ 0 ] == '\0' );

	// the console's words: "# h" is the "#h" that was typed, and nothing else is touched
	char joined[ 64 ];
	MPChatRejoinTokens( "status # h / # a # w at # l with # n", joined, sizeof( joined ) );
	CHECK( std::string( joined ) == "status #h / #a #w at #l with #n" );
	MPChatRejoinTokens( "# H", joined, sizeof( joined ) );
	CHECK( std::string( joined ) == "#H" );
	MPChatRejoinTokens( "# help # x a# h #  h ## h # hh", joined, sizeof( joined ) );
	CHECK( std::string( joined ) == "# help # x a# h #  h ## h # hh" );
	MPChatRejoinTokens( "# h", joined, 3 );
	CHECK( std::string( joined ) == "#h" );
	MPChatRejoinTokens( "abcdef", joined, 4 );
	CHECK( std::string( joined ) == "abc" );
	MPChatRejoinTokens( 0, joined, sizeof( joined ) );
	CHECK( joined[ 0 ] == '\0' );
	MPChatRejoinTokens( "need # h", joined, sizeof( joined ) );
	CHECK( Expand( joined, values ) == "need 87" );
}

static void TestWeaponOrder( void ) {
	// Quake 4's multiplayer slots
	const char *names[ 16 ] = {
		"weapon_gauntlet", "weapon_machinegun", "weapon_shotgun", "weapon_hyperblaster",
		"weapon_grenadelauncher", "weapon_nailgun", "weapon_rocketlauncher", "weapon_railgun",
		"weapon_lightninggun", "weapon_dmg", "weapon_napalmgun", "", 0, "", "", "weapon_blaster_mp"
	};
	mpWeaponOrder_t order;

	MPWeaponOrderParse( "", names, 16, order );
	CHECK( !MPWeaponOrderActive( order ) );
	CHECK( !MPWeaponOrderSwitchOnPickup( order, 1, 7 ) );
	CHECK( MPWeaponOrderBest( order, 0xffff ) == -1 );
	MPWeaponOrderParse( 0, names, 16, order );
	CHECK( !MPWeaponOrderActive( order ) );

	MPWeaponOrderParse( "rg rl, LG;weapon_hyperblaster nailgun 4 bogus rg 11 12 99 blaster", names, 16, order );
	CHECK( MPWeaponOrderActive( order ) && order.listed == 7 );
	CHECK( MPWeaponOrderRank( order, 7 ) == 0 );	// rg
	CHECK( MPWeaponOrderRank( order, 6 ) == 1 );	// rl
	CHECK( MPWeaponOrderRank( order, 8 ) == 2 );	// LG, without case
	CHECK( MPWeaponOrderRank( order, 3 ) == 3 );	// full definition name
	CHECK( MPWeaponOrderRank( order, 5 ) == 4 );	// short name
	CHECK( MPWeaponOrderRank( order, 4 ) == 5 );	// slot number
	CHECK( MPWeaponOrderRank( order, 15 ) == 6 );	// "_mp" suffix dropped
	CHECK( MPWeaponOrderRank( order, 1 ) == MP_WEAPON_ORDER_UNRANKED );
	CHECK( MPWeaponOrderRank( order, 11 ) == MP_WEAPON_ORDER_UNRANKED );	// empty slots cannot be named
	CHECK( MPWeaponOrderRank( order, 12 ) == MP_WEAPON_ORDER_UNRANKED );
	CHECK( MPWeaponOrderRank( order, -1 ) == MP_WEAPON_ORDER_UNRANKED );
	CHECK( MPWeaponOrderRank( order, 99 ) == MP_WEAPON_ORDER_UNRANKED );

	// a pickup switches only upward
	CHECK( MPWeaponOrderSwitchOnPickup( order, 6, 7 ) );		// rl in hand, rg picked
	CHECK( !MPWeaponOrderSwitchOnPickup( order, 7, 6 ) );		// rg in hand, rl picked
	CHECK( !MPWeaponOrderSwitchOnPickup( order, 7, 7 ) );
	CHECK( MPWeaponOrderSwitchOnPickup( order, 1, 4 ) );		// unlisted in hand, listed picked
	CHECK( !MPWeaponOrderSwitchOnPickup( order, 4, 1 ) );		// never to a weapon the order does not name
	CHECK( !MPWeaponOrderSwitchOnPickup( order, 1, 2 ) );
	CHECK( MPWeaponOrderSwitchOnPickup( order, -1, 4 ) );		// holding nothing

	// the fallback takes the best ranked usable weapon
	CHECK( MPWeaponOrderBest( order, ( 1u << 7 ) | ( 1u << 6 ) | ( 1u << 1 ) ) == 7 );
	CHECK( MPWeaponOrderBest( order, ( 1u << 6 ) | ( 1u << 4 ) | ( 1u << 1 ) ) == 6 );
	CHECK( MPWeaponOrderBest( order, ( 1u << 15 ) | ( 1u << 0 ) ) == 15 );
	CHECK( MPWeaponOrderBest( order, ( 1u << 1 ) | ( 1u << 2 ) ) == -1 );	// none listed: Quake 4 decides
	CHECK( MPWeaponOrderBest( order, 0 ) == -1 );

	// smart switching (ui_autoSwitch 2): which weapons a pickup may take over from
	CHECK( MPSmartSwitchRole( "weapon_gauntlet_mp", false ) == MP_SWITCH_ROLE_UNARMED );
	CHECK( MPSmartSwitchRole( "weapon_blaster", false ) == MP_SWITCH_ROLE_UNARMED );
	CHECK( MPSmartSwitchRole( "weapon_machinegun_mp", true ) == MP_SWITCH_ROLE_STARTER );
	CHECK( MPSmartSwitchRole( "weapon_machinegun", true ) == MP_SWITCH_ROLE_STARTER );
	CHECK( MPSmartSwitchRole( "WEAPON_MACHINEGUN_MP", true ) == MP_SWITCH_ROLE_STARTER );
	CHECK( MPSmartSwitchRole( "weapon_machinegun_upgraded", true ) == MP_SWITCH_ROLE_ARMED );
	CHECK( MPSmartSwitchRole( "weapon_railgun_mp", true ) == MP_SWITCH_ROLE_ARMED );
	CHECK( MPSmartSwitchRole( 0, true ) == MP_SWITCH_ROLE_ARMED );
	CHECK( MPSmartSwitchAllows( true, MP_SWITCH_ROLE_UNARMED, MP_SWITCH_ROLE_ARMED ) );		// gauntlet -> rocket launcher
	CHECK( MPSmartSwitchAllows( true, MP_SWITCH_ROLE_UNARMED, MP_SWITCH_ROLE_STARTER ) );	// blaster -> machinegun
	CHECK( MPSmartSwitchAllows( true, MP_SWITCH_ROLE_STARTER, MP_SWITCH_ROLE_ARMED ) );		// machinegun -> shotgun
	CHECK( !MPSmartSwitchAllows( true, MP_SWITCH_ROLE_STARTER, MP_SWITCH_ROLE_STARTER ) );
	CHECK( !MPSmartSwitchAllows( true, MP_SWITCH_ROLE_ARMED, MP_SWITCH_ROLE_ARMED ) );		// railgun stays in hand
	CHECK( !MPSmartSwitchAllows( true, MP_SWITCH_ROLE_ARMED, MP_SWITCH_ROLE_STARTER ) );
	CHECK( !MPSmartSwitchAllows( true, MP_SWITCH_ROLE_STARTER, MP_SWITCH_ROLE_UNARMED ) );	// never down to the gauntlet
	CHECK( !MPSmartSwitchAllows( true, MP_SWITCH_ROLE_UNARMED, MP_SWITCH_ROLE_UNARMED ) );
	CHECK( MPSmartSwitchAllows( false, MP_SWITCH_ROLE_ARMED, MP_SWITCH_ROLE_ARMED ) );		// an empty weapon gives way
	CHECK( MPSmartSwitchAllows( false, MP_SWITCH_ROLE_ARMED, MP_SWITCH_ROLE_UNARMED ) );
	CHECK( MP_AUTO_SWITCH_NEVER == 0 && MP_AUTO_SWITCH_ALWAYS == 1 && MP_AUTO_SWITCH_SMART == 2 );	// Quake 4's 0 and 1 keep their meaning

	// aliases
	MPWeaponOrderParse( "gl mg sg hb ng nap bfg g bl", names, 16, order );
	CHECK( order.listed == 9 );
	CHECK( MPWeaponOrderRank( order, 4 ) == 0 && MPWeaponOrderRank( order, 1 ) == 1 && MPWeaponOrderRank( order, 2 ) == 2 );
	CHECK( MPWeaponOrderRank( order, 3 ) == 3 && MPWeaponOrderRank( order, 5 ) == 4 && MPWeaponOrderRank( order, 10 ) == 5 );
	CHECK( MPWeaponOrderRank( order, 9 ) == 6 && MPWeaponOrderRank( order, 0 ) == 7 && MPWeaponOrderRank( order, 15 ) == 8 );

	// more slots than the order holds are ignored, not overrun
	const char *many[ 40 ];
	for ( int i = 0; i < 40; i++ ) {
		many[ i ] = "weapon_railgun";
	}
	MPWeaponOrderParse( "39 35 31 railgun", many, 40, order );
	CHECK( order.listed == 2 && MPWeaponOrderRank( order, 31 ) == 0 && MPWeaponOrderRank( order, 0 ) == 1 );
}

static void TestHitSounds( void ) {
	CHECK( MPHitSoundBucket( 0 ) == -1 && MPHitSoundBucket( -5 ) == -1 );
	CHECK( MPHitSoundBucket( 1 ) == 0 && MPHitSoundBucket( 25 ) == 0 );
	CHECK( MPHitSoundBucket( 26 ) == 1 && MPHitSoundBucket( 50 ) == 1 );
	CHECK( MPHitSoundBucket( 51 ) == 2 && MPHitSoundBucket( 75 ) == 2 );
	CHECK( MPHitSoundBucket( 76 ) == 3 && MPHitSoundBucket( 5000 ) == 3 );

	// falling: more damage, lower pitch; rising: the reverse; the same four steps
	float last = 10.0f;
	for ( int damage = 10; damage <= 100; damage += 25 ) {
		const float falling = MPHitSoundPitch( HITSOUND_FALLING, damage );
		const float rising = MPHitSoundPitch( HITSOUND_RISING, 110 - damage );
		CHECK( falling > 0.25f && falling < 4.0f && falling < last );
		CHECK( falling == rising );
		last = falling;
	}
	CHECK( MPHitSoundPitch( HITSOUND_OFF, 50 ) == 0.0f );
	CHECK( MPHitSoundPitch( HITSOUND_STOCK, 50 ) == 0.0f );
	CHECK( MPHitSoundPitch( HITSOUND_FALLING, 0 ) == 0.0f );
	CHECK( MPHitSoundPitch( HITSOUND_SET_COUNT, 50 ) == 0.0f );

	mpHitSoundState_t state;
	bool armor = false;

	// Quake 4's set never waits
	MPHitSoundClear( state );
	CHECK( MPHitSoundOnPulse( state, HITSOUND_STOCK, true, false, 1000 ) == MP_HITSOUND_PLAY_STOCK );
	CHECK( MPHitSoundOnAmount( state, HITSOUND_STOCK, 40, 1000 ) == MP_HITSOUND_PLAY_NONE );
	CHECK( MPHitSoundFrame( state, 5000, armor ) == MP_HITSOUND_PLAY_NONE );
	// off is silent on both paths
	CHECK( MPHitSoundOnPulse( state, HITSOUND_OFF, true, false, 1000 ) == MP_HITSOUND_PLAY_NONE );
	CHECK( MPHitSoundOnAmount( state, HITSOUND_OFF, 40, 1000 ) == MP_HITSOUND_PLAY_NONE );
	CHECK( MPHitSoundFrame( state, 5000, armor ) == MP_HITSOUND_PLAY_NONE );

	// tones, amount first: the pulse for the same hit is covered
	MPHitSoundClear( state );
	CHECK( MPHitSoundOnAmount( state, HITSOUND_FALLING, 40, 1000 ) == MP_HITSOUND_PLAY_TONE );
	CHECK( MPHitSoundOnPulse( state, HITSOUND_FALLING, true, false, 1010 ) == MP_HITSOUND_PLAY_NONE );
	CHECK( MPHitSoundFrame( state, 1500, armor ) == MP_HITSOUND_PLAY_NONE );

	// tones, pulse first: it waits, and the amount answers it
	MPHitSoundClear( state );
	CHECK( MPHitSoundOnPulse( state, HITSOUND_RISING, true, true, 2000 ) == MP_HITSOUND_PLAY_NONE );
	CHECK( MPHitSoundFrame( state, 2000 + MP_HITSOUND_PULSE_WAIT_MSEC - 1, armor ) == MP_HITSOUND_PLAY_NONE );
	CHECK( MPHitSoundOnAmount( state, HITSOUND_RISING, 90, 2030 ) == MP_HITSOUND_PLAY_TONE );
	CHECK( MPHitSoundFrame( state, 2500, armor ) == MP_HITSOUND_PLAY_NONE );

	// tones, the amount never arrives: Quake 4's cue, with the armour it knew, once
	MPHitSoundClear( state );
	CHECK( MPHitSoundOnPulse( state, HITSOUND_FALLING, true, true, 3000 ) == MP_HITSOUND_PLAY_NONE );
	CHECK( MPHitSoundFrame( state, 3000 + MP_HITSOUND_PULSE_WAIT_MSEC, armor ) == MP_HITSOUND_PLAY_STOCK );
	CHECK( armor );
	CHECK( MPHitSoundFrame( state, 3200, armor ) == MP_HITSOUND_PLAY_NONE );

	// a server that withholds amounts: the pulse is all there is, so it sounds at once
	MPHitSoundClear( state );
	CHECK( MPHitSoundOnPulse( state, HITSOUND_FALLING, false, false, 4000 ) == MP_HITSOUND_PLAY_STOCK );
	CHECK( MPHitSoundOnAmount( state, HITSOUND_FALLING, 0, 4000 ) == MP_HITSOUND_PLAY_NONE );
	CHECK( MPHitSoundFrame( state, 4500, armor ) == MP_HITSOUND_PLAY_NONE );

	// a later hit is a new hit
	MPHitSoundClear( state );
	CHECK( MPHitSoundOnAmount( state, HITSOUND_FALLING, 40, 5000 ) == MP_HITSOUND_PLAY_TONE );
	CHECK( MPHitSoundOnPulse( state, HITSOUND_FALLING, true, false, 5000 + MP_HITSOUND_COVER_MSEC ) == MP_HITSOUND_PLAY_NONE );
	CHECK( MPHitSoundFrame( state, 5000 + MP_HITSOUND_COVER_MSEC + MP_HITSOUND_PULSE_WAIT_MSEC, armor ) == MP_HITSOUND_PLAY_STOCK );
	CHECK( !armor );
}

static void TestLocations( void ) {
	const float landmarks[] = {
		0.0f, 0.0f, 0.0f,			// 0: ground floor
		0.0f, 0.0f, 300.0f,			// 1: straight above
		1000.0f, 0.0f, 0.0f,		// 2
		0.0f, 0.0f, 0.0f			// 3: same place as 0
	};
	float point[ 3 ] = { 10.0f, 0.0f, 0.0f };
	CHECK( MPLocationNearest( landmarks, 4, point, MP_LOCATION_MAX_DISTANCE, MP_LOCATION_VERTICAL_WEIGHT ) == 0 );	// tie keeps the lower index
	point[ 0 ] = 600.0f;
	CHECK( MPLocationNearest( landmarks, 4, point, MP_LOCATION_MAX_DISTANCE, MP_LOCATION_VERTICAL_WEIGHT ) == 2 );
	// the same floor wins over the floor above
	point[ 0 ] = 450.0f; point[ 2 ] = 20.0f;
	CHECK( MPLocationNearest( landmarks, 3, point, MP_LOCATION_MAX_DISTANCE, MP_LOCATION_VERTICAL_WEIGHT ) == 0 );
	point[ 0 ] = 0.0f; point[ 2 ] = 200.0f;
	CHECK( MPLocationNearest( landmarks, 3, point, MP_LOCATION_MAX_DISTANCE, MP_LOCATION_VERTICAL_WEIGHT ) == 1 );
	// nothing near enough names nothing
	point[ 0 ] = 9000.0f; point[ 2 ] = 0.0f;
	CHECK( MPLocationNearest( landmarks, 4, point, MP_LOCATION_MAX_DISTANCE, MP_LOCATION_VERTICAL_WEIGHT ) == -1 );
	CHECK( MPLocationNearest( landmarks, 0, point, MP_LOCATION_MAX_DISTANCE, MP_LOCATION_VERTICAL_WEIGHT ) == -1 );
	CHECK( MPLocationNearest( 0, 4, point, MP_LOCATION_MAX_DISTANCE, MP_LOCATION_VERTICAL_WEIGHT ) == -1 );
}

static void TestTeamInfo( void ) {
	CHECK( MPTeamInfoPackVital( -20 ) == 0 && MPTeamInfoPackVital( 125 ) == 125 && MPTeamInfoPackVital( 999 ) == 255 );
	CHECK( MPTeamInfoPackCoord( 12.4f ) == 12 && MPTeamInfoPackCoord( 12.6f ) == 13 );
	CHECK( MPTeamInfoPackCoord( -12.4f ) == -12 && MPTeamInfoPackCoord( -12.6f ) == -13 );
	CHECK( MPTeamInfoPackCoord( 1.0e9f ) == 32767 && MPTeamInfoPackCoord( -1.0e9f ) == -32768 );
	CHECK( MPTeamInfoPackWeapon( 7, 16 ) == 7 && MPTeamInfoPackWeapon( -1, 16 ) == MP_TEAMINFO_NO_WEAPON );
	CHECK( MPTeamInfoPackWeapon( 16, 16 ) == MP_TEAMINFO_NO_WEAPON );
	CHECK( MPTeamInfoUnpackWeapon( 7, 16 ) == 7 && MPTeamInfoUnpackWeapon( MP_TEAMINFO_NO_WEAPON, 16 ) == -1 );
	CHECK( MPTeamInfoUnpackWeapon( 16, 16 ) == -1 );
	CHECK( !MPTeamInfoFresh( 0, 1000 ) );
	CHECK( MPTeamInfoFresh( 1000, 1000 ) && MPTeamInfoFresh( 1000, 1000 + MP_TEAMINFO_FRESH_MSEC ) );
	CHECK( !MPTeamInfoFresh( 1000, 1001 + MP_TEAMINFO_FRESH_MSEC ) );
	CHECK( !MPTeamInfoFresh( 2000, 1000 ) );	// a record from the future is a clock reset, not news
	CHECK( MP_TEAMINFO_FRESH_MSEC > 2 * MP_TEAMINFO_SEND_MSEC );
}

static void TestItemTimers( void ) {
	// nobody
	CHECK( !MPItemTimersAdmit( ITEMTIMERS_OFF, false, true ) && !MPItemTimersAdmit( ITEMTIMERS_OFF, false, false ) );
	// spectators only: a player is never admitted
	CHECK( MPItemTimersAdmit( ITEMTIMERS_SPECTATORS, false, true ) );
	CHECK( !MPItemTimersAdmit( ITEMTIMERS_SPECTATORS, false, false ) );
	// everyone
	CHECK( MPItemTimersAdmit( ITEMTIMERS_EVERYONE, false, true ) && MPItemTimersAdmit( ITEMTIMERS_EVERYONE, false, false ) );
	// a managed match keeps its own disclosure policy, whatever the cvar says
	for ( int policy = 0; policy < ITEMTIMERS_POLICY_COUNT; policy++ ) {
		CHECK( !MPItemTimersAdmit( policy, true, true ) && !MPItemTimersAdmit( policy, true, false ) );
	}
	// an out-of-range policy admits nobody
	CHECK( !MPItemTimersAdmit( 7, false, true ) && !MPItemTimersAdmit( -1, false, true ) );

	CHECK( MPItemTimerPriority( ITEMTIMER_QUAD_DAMAGE ) < MPItemTimerPriority( ITEMTIMER_LARGE_ARMOR ) );
	CHECK( MPItemTimerPriority( ITEMTIMER_MEGA_HEALTH ) < MPItemTimerPriority( ITEMTIMER_LARGE_ARMOR ) );
	CHECK( MPItemTimerPriority( ITEMTIMER_LARGE_ARMOR ) < MPItemTimerPriority( ITEMTIMER_SMALL_ARMOR ) );
	CHECK( MPItemTimerPriority( ITEMTIMER_SMALL_ARMOR ) < MPItemTimerPriority( ITEMTIMER_NONE ) );

	CHECK( MPItemTimerPackState( 0, 5000 ) == MP_ITEMTIMER_STATE_AVAILABLE );
	CHECK( MPItemTimerPackState( 5000, 5000 ) == MP_ITEMTIMER_STATE_AVAILABLE );
	CHECK( MPItemTimerPackState( 5001, 5000 ) == 1 );		// reads 1 until the item is there
	CHECK( MPItemTimerPackState( 6000, 5000 ) == 1 && MPItemTimerPackState( 6001, 5000 ) == 2 );
	CHECK( MPItemTimerPackState( 35000, 5000 ) == 30 );
	CHECK( MPItemTimerPackState( 5000 + 1000 * 1000, 5000 ) == MP_ITEMTIMER_STATE_MAX_SECONDS );
	CHECK( MPItemTimerPackState( -1, 5000 ) == MP_ITEMTIMER_STATE_HELD );
	CHECK( MP_ITEMTIMER_STATE_MAX_SECONDS < MP_ITEMTIMER_STATE_HELD );
	CHECK( MP_ITEMTIMERS_HUD_ROWS <= MP_ITEMTIMERS_MAX_ROWS );
	CHECK( MP_ITEMTIMERS_FRESH_MSEC > 2 * MP_ITEMTIMERS_SEND_MSEC );
}

int main( void ) {
	TestChatTokens();
	TestWeaponOrder();
	TestHitSounds();
	TestLocations();
	TestTeamInfo();
	TestItemTimers();
	if ( failures ) {
		std::printf( "mp client options: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "mp client options: ok\n" );
	return 0;
}
