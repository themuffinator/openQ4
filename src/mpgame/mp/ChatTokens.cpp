//----------------------------------------------------------------
// ChatTokens.cpp
//
// openQ4 chat tokens (MP-15): reads the sending player for the values the
// tokens stand for. The expansion itself is the engine-free core in
// ChatTokens.h.
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "ChatTokens.h"

// how close a team mate has to be to count as "with me"
static const float	CHAT_NEARBY_DISTANCE	= 768.0f;
static const int	CHAT_NEARBY_MAX_NAMES	= 3;

/*
================
ChatWeaponText

The weapon in hand by its localized name, with the ammunition it has left
unless it needs none.
================
*/
static idStr ChatWeaponText( idPlayer *player ) {
	const int current = player->GetCurrentWeapon();
	const idDeclEntityDef *def = player->GetWeaponDef( current );
	if ( def == NULL ) {
		return "";
	}
	idStr name = common->GetLocalizedString( def->dict.GetString( "inv_name" ) );
	if ( name.Length() == 0 ) {
		return "";
	}
	const int ammo = ( player->weapon != NULL ) ? player->weapon->AmmoAvailable() : -1;
	if ( ammo >= 0 && def->dict.GetInt( "ammoRequired" ) > 0 ) {
		name += va( " (%d)", ammo );
	}
	return name;
}

/*
================
ChatNearbyText

Living team mates this end can currently see near the player, nearest first.
A client only knows the players in its snapshot, and the host of a listen
server is held to the same distance, so the token never says more than the
sender could have typed.
================
*/
static idStr ChatNearbyText( idPlayer *player ) {
	idStr text;
	if ( !gameLocal.IsTeamGame() ) {
		return text;
	}

	int		order[ MAX_CLIENTS ];
	float	distances[ MAX_CLIENTS ];
	int		count = 0;
	const idVec3 &origin = player->GetPhysics()->GetOrigin();

	for ( int i = 0; i < Min( gameLocal.numClients, MAX_CLIENTS ); i++ ) {
		idEntity *ent = gameLocal.entities[ i ];
		if ( ent == NULL || ent == player || !ent->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}
		idPlayer *mate = static_cast< idPlayer * >( ent );
		if ( mate->team != player->team || mate->spectating || mate->health <= 0 || mate->fl.networkStale ||
			 mate->GetInstance() != player->GetInstance() ) {
			continue;
		}
		const float distance = ( mate->GetPhysics()->GetOrigin() - origin ).LengthSqr();
		if ( distance > Square( CHAT_NEARBY_DISTANCE ) ) {
			continue;
		}
		int slot = count++;
		while ( slot > 0 && distances[ slot - 1 ] > distance ) {
			order[ slot ] = order[ slot - 1 ];
			distances[ slot ] = distances[ slot - 1 ];
			slot--;
		}
		order[ slot ] = i;
		distances[ slot ] = distance;
	}

	for ( int i = 0; i < Min( count, CHAT_NEARBY_MAX_NAMES ); i++ ) {
		idStr name = gameLocal.userInfo[ order[ i ] ].GetString( "ui_name" );
		name.RemoveEscapes();
		if ( name.Length() == 0 ) {
			continue;
		}
		if ( text.Length() ) {
			text += ", ";
		}
		text += name;
	}
	return text;
}

/*
================
MPChatExpandForPlayer
================
*/
int MPChatExpandForPlayer( idPlayer *player, const char *text, bool consoleWords, idStr &expanded ) {
	expanded = ( text != NULL ) ? text : "";
	if ( player == NULL || text == NULL || strchr( text, '#' ) == NULL ) {
		return 0;
	}
	if ( player->spectating ) {
		// someone watching has no health, weapon or place of their own to report
		return 0;
	}

	char words[ MAX_STRING_CHARS ];
	if ( consoleWords ) {
		MPChatRejoinTokens( text, words, sizeof( words ) );
		text = words;
	}

	const bool dead = player->health <= 0;
	idStr health = dead ? common->GetLocalizedString( "#str_232300" ) : va( "%d", player->health );
	idStr armor = va( "%d", dead ? 0 : Max( 0, player->inventory.armor ) );
	idStr weapon = dead ? "" : ChatWeaponText( player ).c_str();
	idStr location = gameLocal.mpGame.locations.NameForPoint( player->GetPhysics()->GetOrigin() );
	idStr nearby = ChatNearbyText( player );
	if ( nearby.Length() == 0 ) {
		nearby = common->GetLocalizedString( "#str_232301" );
	}

	mpChatTokenValues_t values;
	values.health = health.c_str();
	values.armor = armor.c_str();
	values.weapon = weapon.c_str();
	values.location = location.c_str();
	values.nearby = nearby.c_str();

	char buffer[ MP_CHAT_TOKEN_TEXT_BYTES ];
	const int count = MPChatExpandTokens( text, values, buffer, sizeof( buffer ) );
	if ( count > 0 ) {
		expanded = buffer;
	}
	return count;
}
