//----------------------------------------------------------------
// IdentityHost.cpp
//
// openQ4 player identity (roadmap MP-17): the game's side of mp/Identity.h.
//
// Server: a random id of its own (identity_server.txt), a one-time challenge
// to each player whose game offers an identity (userinfo ui_identity 1, so a
// game that cannot answer is never sent one), the proof checked, identity
// bans applied, a leaver's team and score kept for their return, a ghost code
// told to players without an identity, and an optional local ranking table
// (g_ranking, ranking.txt).
//
// Client: the seed in identity.key, a separate key for each server signed on
// request, identityReset, ghost <code> and the player's own match history
// (profile/history.ndjson, history).
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "GameTypes.h"
#include "Identity.h"

#include <time.h>

// Localized text; a code or a name follows where the text ends with a space.
static const char *const ID_STR_GHOST				= "#str_232830";	// "If you lose your connection, type this when you return to keep your team and score: ghost "
static const char *const ID_STR_RESTORED			= "#str_232831";	// "Welcome back: your team and score are restored."
static const char *const ID_STR_GHOST_UNKNOWN		= "#str_232832";	// "That return code is not known here, or it has expired."
static const char *const ID_STR_GHOST_USAGE			= "#str_232833";	// "Usage: ghost <code>, the six letters the server told you when you joined"
static const char *const ID_STR_RESET_CONFIRM		= "#str_232834";	// "identityReset gives you a new identity on every server ..."
static const char *const ID_STR_RESET_DONE			= "#str_232835";	// "Your identity was reset: every server now sees you as a new player."
static const char *const ID_STR_IDENTITY_OFF		= "#str_232836";	// "You send no identity: ui_identity is 0."
static const char *const ID_STR_BANNED				= "#str_107239";	// stock: "You have been banned from this server."

#define MP_ID_SEED_FILE			"identity.key"
#define MP_ID_SEED_HEADER		"openq4-identity 1"
#define MP_ID_SERVER_FILE		"identity_server.txt"
#define MP_ID_SERVER_HEADER		"openq4-server-identity 1"
#define MP_RANKING_FILE			"ranking.txt"
#define MP_RANKING_STAGED		"ranking.txt.new"
#define MP_RANKING_HEADER		"openq4-ranking 1"
#define MP_HISTORY_FILE			"profile/history.ndjson"

static const int MP_ID_GHOST_AFTER_MS		= 20000;	// a challenge unanswered this long: the ghost code is told (the challenge stays good)
// A challenge sent as the player begins can reach a game that is not yet taking
// game messages, and a map restart can lose one: it is sent again, the same
// nonce, this long after the first send.
static const int MP_ID_RESEND_AT_MS[]		= { 0, 3000, 15000 };
static const int MP_ID_CHALLENGE_SENDS		= sizeof( MP_ID_RESEND_AT_MS ) / sizeof( MP_ID_RESEND_AT_MS[ 0 ] );
static const int MP_ID_FILE_MAX_BYTES		= 4096;
static const int MP_RANKING_MAX_ENTRIES		= 8192;
static const int MP_RANKING_FILE_MAX_BYTES	= 2 * 1024 * 1024;
static const int MP_HISTORY_SHOWN			= 10;

typedef enum {
	MP_ID_MSG_CHALLENGE	= 0,	// server to client: server id, nonce, slot, connection
	MP_ID_MSG_PROOF		= 1,	// client to server: public key, signature
	MP_ID_MSG_GHOST		= 2		// client to server: a ghost code
} mpIdMessage_t;

static const int MP_ID_CHALLENGE_BITS	= ( 1 + MP_ID_SERVER_ID_BYTES + MP_ID_NONCE_BYTES + 1 + 4 ) * 8;
static const int MP_ID_PROOF_BITS		= ( 1 + MP_ID_PUBLIC_KEY_BYTES + MP_ID_SIGNATURE_BYTES ) * 8;

// the player's fingerprint on the server they are connected to, for "identity"
static char identityLocalFingerprint[ MP_ID_FINGERPRINT_TEXT_BYTES ];

/*
===============================================================================

	Small files in the save folder, read by their OS path: a pure server
	refuses loose reads in the middle of a map.

===============================================================================
*/

static bool IdentityReadSaveFile( const char *name, int maxBytes, idStr &text ) {
	text.Clear();
	idFile *file = fileSystem->OpenExplicitFileRead( fileSystem->RelativePathToOSPath( name, "fs_savepath" ) );
	if ( file == NULL ) {
		return false;
	}
	const int length = file->Length();
	bool ok = length >= 0 && length <= maxBytes;
	if ( ok && length > 0 ) {
		char *buffer = static_cast<char *>( Mem_Alloc( length + 1 ) );
		ok = file->Read( buffer, length ) == length;
		buffer[ length ] = '\0';
		if ( ok ) {
			text = buffer;
		}
		idCrypto::SecureWipe( buffer, length );
		Mem_Free( buffer );
	}
	fileSystem->CloseFile( file );
	return ok;
}

// The first line after the header that is exactly count bytes of hex.
static bool IdentityParseHexFile( const idStr &text, const char *header, unsigned char *out, int count ) {
	idStrList lines;
	int start = 0;
	for ( int i = 0; i <= text.Length(); i++ ) {
		if ( i == text.Length() || text[ i ] == '\n' ) {
			idStr line = text.Mid( start, i - start );
			line.StripTrailingWhitespace();
			line.StripLeading( ' ' );
			lines.Append( line );
			start = i + 1;
		}
	}
	if ( lines.Num() == 0 || lines[ 0 ].Cmp( header ) != 0 ) {
		return false;
	}
	for ( int i = 1; i < lines.Num(); i++ ) {
		if ( lines[ i ].Length() == count * 2 && MPIdHexDecode( lines[ i ].c_str(), out, count ) ) {
			return true;
		}
	}
	return false;
}

static bool IdentityWriteHexFile( const char *name, const char *header, const char *comment, const unsigned char *bytes, int count ) {
	char hex[ 2 * 64 + 1 ];
	if ( count > 64 ) {
		return false;
	}
	MPIdHexEncode( bytes, count, hex );
	idFile *file = fileSystem->OpenFileWrite( name, "fs_savepath" );
	if ( file == NULL ) {
		idCrypto::SecureWipe( hex, sizeof( hex ) );
		return false;
	}
	file->Printf( "%s\n# %s\n%s\n", header, comment, hex );
	fileSystem->CloseFile( file );
	idCrypto::SecureWipe( hex, sizeof( hex ) );
	return true;
}

/*
===============================================================================

	The server's side

===============================================================================
*/

/*
================
idMultiplayerGame::IdentityServerInit

The server's id, made once and kept, so its players keep their identities
across restarts. A server whose save folder cannot be written uses an id for
this run only.
================
*/
void idMultiplayerGame::IdentityServerInit( void ) {
	if ( identityServerIdReady || !gameLocal.isServer ) {
		return;
	}
	idStr text;
	if ( IdentityReadSaveFile( MP_ID_SERVER_FILE, MP_ID_FILE_MAX_BYTES, text ) &&
		 IdentityParseHexFile( text, MP_ID_SERVER_HEADER, identityServerId, MP_ID_SERVER_ID_BYTES ) ) {
		identityServerIdReady = true;
		return;
	}
	if ( !sys->SecureRandomBytes( identityServerId, MP_ID_SERVER_ID_BYTES ) ) {
		common->Warning( "identity: no secure random bytes; players are not asked for an identity" );
		return;
	}
	identityServerIdReady = true;
	if ( !IdentityWriteHexFile( MP_ID_SERVER_FILE, MP_ID_SERVER_HEADER,
			"This server's id. Players' identities here are derived from it: keep it to keep them.",
			identityServerId, MP_ID_SERVER_ID_BYTES ) ) {
		common->Warning( "identity: could not write %s; this server's id lasts until it stops", MP_ID_SERVER_FILE );
	}
}

/*
================
idMultiplayerGame::IdentityCasual

Identity places, ghost codes and the ranking table are for casual play; a
managed match restores its own roster.
================
*/
bool idMultiplayerGame::IdentityCasual( void ) const {
	return gameLocal.isMultiplayer && gameLocal.isServer && !IsManagedMatch();
}

const char *idMultiplayerGame::ClientIdentity( int clientNum ) const {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return "";
	}
	return identityFingerprint[ clientNum ];
}

bool idMultiplayerGame::IdentityRankingEnabled( void ) const {
	return g_ranking.GetBool() && IdentityCasual();
}

/*
================
idMultiplayerGame::IdentityClientConnect

A new connection, or the same one on a new map: it proves its identity again.
================
*/
void idMultiplayerGame::IdentityClientConnect( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	idCrypto::SecureWipe( identityNonce[ clientNum ], sizeof( identityNonce[ clientNum ] ) );
	identityPending[ clientNum ] = false;
	identityChallengeTime[ clientNum ] = 0;
	identityFingerprint[ clientNum ][ 0 ] = '\0';
	identityGhost[ clientNum ][ 0 ] = '\0';
	identityGhostTold[ clientNum ] = false;
	identityRestored[ clientNum ] = false;
	identityAsked[ clientNum ] = false;
	identityBegun[ clientNum ] = false;
}

/*
================
idMultiplayerGame::IdentityClientBegin

The player has the map: a ghost code for the connection, then the challenge
if their game offers an identity.
================
*/
void idMultiplayerGame::IdentityClientBegin( int clientNum, bool isBot ) {
	if ( !gameLocal.isServer || clientNum < 0 || clientNum >= MAX_CLIENTS || isBot ) {
		return;
	}
	IdentityServerInit();
	unsigned char random[ MP_ID_GHOST_CODE_LENGTH ];
	if ( sys->SecureRandomBytes( random, sizeof( random ) ) ) {
		MPIdGhostCode( random, identityGhost[ clientNum ] );
	} else {
		identityGhost[ clientNum ][ 0 ] = '\0';
	}
	identityGhostTold[ clientNum ] = false;
	identityBegun[ clientNum ] = true;
	identityChallengeTime[ clientNum ] = gameLocal.time;
	IdentitySendChallenge( clientNum );
}

/*
================
idMultiplayerGame::IdentityUserInfoChanged

The userinfo that offers an identity can reach the server after the player
has begun; the challenge goes out then. Once a connection, whatever the
answer.
================
*/
void idMultiplayerGame::IdentityUserInfoChanged( int clientNum ) {
	if ( !gameLocal.isServer || clientNum < 0 || clientNum >= MAX_CLIENTS || !identityBegun[ clientNum ] ||
		 identityAsked[ clientNum ] || botManager.IsBot( clientNum ) ) {
		return;
	}
	IdentitySendChallenge( clientNum );
}

/*
================
idMultiplayerGame::IdentitySendChallenge

The challenge, to a player whose game offers an identity (userinfo
ui_identity 1; an older game never offers one, and never sees the message).
The host of a listen server proves its own identity without the network.
================
*/
void idMultiplayerGame::IdentitySendChallenge( int clientNum ) {
	if ( identityAsked[ clientNum ] || !g_identity.GetBool() || !identityServerIdReady ||
		 !gameLocal.userInfo[ clientNum ].GetBool( "ui_identity" ) ) {
		return;
	}
	identityAsked[ clientNum ] = true;
	const unsigned int connection = static_cast<unsigned int>( matchConnectionId[ clientNum ] );
	if ( clientNum == gameLocal.localClientNum ) {
		unsigned char seed[ MP_ID_SEED_BYTES ], nonce[ MP_ID_NONCE_BYTES ];
		unsigned char publicKey[ MP_ID_PUBLIC_KEY_BYTES ], signature[ MP_ID_SIGNATURE_BYTES ];
		if ( !IdentityLoadSeed( seed, true ) || !sys->SecureRandomBytes( nonce, sizeof( nonce ) ) ) {
			idCrypto::SecureWipe( seed, sizeof( seed ) );
			return;
		}
		MPIdSignProof( seed, identityServerId, nonce, clientNum, connection, publicKey, signature );
		idCrypto::SecureWipe( seed, sizeof( seed ) );
		if ( MPIdVerifyProof( identityServerId, nonce, clientNum, connection, publicKey, signature ) ) {
			char fingerprint[ MP_ID_FINGERPRINT_TEXT_BYTES ];
			MPIdFingerprint( publicKey, fingerprint );
			idStr::Copynz( identityLocalFingerprint, fingerprint, sizeof( identityLocalFingerprint ) );
			IdentityProved( clientNum, fingerprint );
		}
		return;
	}
	if ( !sys->SecureRandomBytes( identityNonce[ clientNum ], MP_ID_NONCE_BYTES ) ) {
		identityPending[ clientNum ] = false;
		return;
	}
	identityPending[ clientNum ] = true;
	identityChallengeTime[ clientNum ] = gameLocal.time;
	identityChallengeSends[ clientNum ] = 0;
	IdentityWriteChallenge( clientNum );
}

/*
================
idMultiplayerGame::IdentityWriteChallenge

The outstanding challenge, to its player. The frame sends it again, the same
nonce, when no answer has come: a map restart can lose a message in flight.
================
*/
void idMultiplayerGame::IdentityWriteChallenge( int clientNum ) {
	identityChallengeSends[ clientNum ]++;
	common->DPrintf( "identity: challenged client %d (%d)\n", clientNum, identityChallengeSends[ clientNum ] );
	idBitMsg	outMsg;
	byte		msgBuf[ 1 + MP_ID_CHALLENGE_BITS / 8 + 8 ];
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.BeginWriting();
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_IDENTITY );
	outMsg.WriteByte( MP_ID_MSG_CHALLENGE );
	outMsg.WriteData( identityServerId, MP_ID_SERVER_ID_BYTES );
	outMsg.WriteData( identityNonce[ clientNum ], MP_ID_NONCE_BYTES );
	outMsg.WriteByte( clientNum );
	outMsg.WriteLong( static_cast<int>( static_cast<unsigned int>( matchConnectionId[ clientNum ] ) ) );
	networkSystem->ServerSendReliableMessage( clientNum, outMsg );
}

/*
================
idMultiplayerGame::IdentityServerFrame

A player with no identity is told their ghost code once, when the server
keeps places for returning players: at once when no challenge was sent, and
when one goes unanswered for a while (it stays good, and a late proof still
counts).
================
*/
void idMultiplayerGame::IdentityServerFrame( void ) {
	if ( !gameLocal.isServer ) {
		return;
	}
	if ( !identityServerIdReady ) {
		IdentityServerInit();
	}
	const bool keepsPlaces = IdentityCasual() && g_reconnectRestore.GetInteger() > 0 &&
		!MPGameTypeHasAny( gameLocal.gameType, GTF_BRACKET );
	for ( int i = 0; i < MAX_CLIENTS && i < gameLocal.numClients; i++ ) {
		// A challenge stays good for the whole connection: a game still loading, or
		// slow on a busy machine, may answer late. Only the ghost code waits on it.
		const bool waiting = identityPending[ i ] && gameLocal.time - identityChallengeTime[ i ] < MP_ID_GHOST_AFTER_MS;
		if ( identityPending[ i ] && identityChallengeSends[ i ] > 0 && identityChallengeSends[ i ] < MP_ID_CHALLENGE_SENDS &&
			 gameLocal.time - identityChallengeTime[ i ] >= MP_ID_RESEND_AT_MS[ identityChallengeSends[ i ] ] ) {
			IdentityWriteChallenge( i );
		}
		// a game older than MP-17 has no ghost command: telling it a code would only puzzle its player
		if ( keepsPlaces && !identityGhostTold[ i ] && !waiting && identityGhost[ i ][ 0 ] != '\0' &&
			 identityFingerprint[ i ][ 0 ] == '\0' && i != gameLocal.localClientNum && !botManager.IsBot( i ) &&
			 gameLocal.entities[ i ] != NULL && gameLocal.userInfo[ i ].FindKey( "ui_identity" ) != NULL ) {
			IdentityTellGhost( i );
		}
	}
}

void idMultiplayerGame::IdentityTellGhost( int clientNum ) {
	identityGhostTold[ clientNum ] = true;
	gameLocal.ServerSendChatMessage( clientNum, "server", ID_STR_GHOST, identityGhost[ clientNum ] );
}

/*
================
idMultiplayerGame::ServerReceiveIdentity
================
*/
void idMultiplayerGame::ServerReceiveIdentity( int clientNum, const idBitMsg &msg ) {
	if ( !gameLocal.isServer || clientNum < 0 || clientNum >= MAX_CLIENTS || msg.GetRemainingReadBits() < 8 ) {
		return;
	}
	const int kind = msg.ReadByte();
	if ( kind == MP_ID_MSG_PROOF ) {
		if ( msg.GetRemainingReadBits() != MP_ID_PROOF_BITS - 8 || !identityPending[ clientNum ] ) {
			common->DPrintf( "identity: client %d sent a proof the server did not ask for\n", clientNum );
			return;
		}
		unsigned char publicKey[ MP_ID_PUBLIC_KEY_BYTES ], signature[ MP_ID_SIGNATURE_BYTES ];
		msg.ReadData( publicKey, sizeof( publicKey ) );
		msg.ReadData( signature, sizeof( signature ) );
		// one answer to one challenge, whatever it says
		identityPending[ clientNum ] = false;
		const unsigned int connection = static_cast<unsigned int>( matchConnectionId[ clientNum ] );
		const bool valid = MPIdVerifyProof( identityServerId, identityNonce[ clientNum ], clientNum, connection, publicKey, signature );
		idCrypto::SecureWipe( identityNonce[ clientNum ], sizeof( identityNonce[ clientNum ] ) );
		if ( !valid ) {
			common->Printf( "identity: client %d sent a proof that does not verify; it has no identity on this map\n", clientNum );
			return;
		}
		char fingerprint[ MP_ID_FINGERPRINT_TEXT_BYTES ];
		MPIdFingerprint( publicKey, fingerprint );
		IdentityProved( clientNum, fingerprint );
		return;
	}
	if ( kind == MP_ID_MSG_GHOST ) {
		char text[ MP_ID_GHOST_CODE_LENGTH + 2 ];
		char code[ MP_ID_GHOST_CODE_LENGTH + 1 ];
		msg.ReadString( text, sizeof( text ) );
		if ( !MPIdParseGhostCode( text, code ) || !IdentityRestore( clientNum, "", code ) ) {
			gameLocal.ServerSendChatMessage( clientNum, "server", ID_STR_GHOST_UNKNOWN );
		}
		return;
	}
}

/*
================
idMultiplayerGame::IdentityProved

The player holds the key for this identity: an identity ban removes them,
and a place kept for them is given back.
================
*/
void idMultiplayerGame::IdentityProved( int clientNum, const char *fingerprint ) {
	idStr::Copynz( identityFingerprint[ clientNum ], fingerprint, sizeof( identityFingerprint[ clientNum ] ) );
	common->Printf( "client %d (%s^0) has identity %s on this server\n", clientNum,
		gameLocal.userInfo[ clientNum ].GetString( "ui_name" ), fingerprint );
	if ( clientNum != gameLocal.localClientNum && gameLocal.FindIdentityBan( fingerprint ) != NULL ) {
		common->Printf( "identity: %s is banned; removing client %d\n", fingerprint, clientNum );
		gameLocal.ModerationLogClient( "kick", "server", clientNum, "banned identity", 0 );
		BeginServerDrop( clientNum );
		networkSystem->ServerDropClient( clientNum, ID_STR_BANNED );
		EndServerDrop( clientNum );
		return;
	}
	IdentityRestore( clientNum, fingerprint, "" );
}

/*
================
idMultiplayerGame::IdentityRestore

A returning player's team and score, from the place kept when they left this
match: by identity, or by the ghost code they type. One claim a connection.
Bracket modes (Tourney) keep no places: their arenas and seeds are the
bracket's own.
================
*/
bool idMultiplayerGame::IdentityRestore( int clientNum, const char *fingerprint, const char *ghost ) {
	if ( !IdentityCasual() || identityRestored[ clientNum ] || g_reconnectRestore.GetInteger() <= 0 ||
		 MPGameTypeHasAny( gameLocal.gameType, GTF_BRACKET ) || gameLocal.entities[ clientNum ] == NULL ||
		 !gameLocal.entities[ clientNum ]->IsType( idPlayer::GetClassType() ) ) {
		return false;
	}
	mpIdReturn_t entry;
	if ( !MPIdReturnClaim( identityReturns, fingerprint, ghost, static_cast<mpIdTime_t>( ::time( NULL ) ), identityMatchStamp, entry ) ) {
		return false;
	}
	idPlayer *player = static_cast<idPlayer *>( gameLocal.entities[ clientNum ] );
	identityRestored[ clientNum ] = true;
	if ( gameLocal.IsTeamGame() && ( entry.team == TEAM_MARINE || entry.team == TEAM_STROGG ) && player->team != entry.team ) {
		// as ForceTeamChange does: the userinfo the server holds, then its update
		player->GetUserInfo()->Set( "ui_team", entry.team == TEAM_MARINE ? "Marine" : "Strogg" );
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "updateUI %d\n", clientNum ) );
	}
	SetPlayerScore( player, entry.score );
	SetPlayerTeamScore( player, entry.teamScore );
	common->Printf( "client %d (%s^0) returned: team %d, score %d\n", clientNum,
		gameLocal.userInfo[ clientNum ].GetString( "ui_name" ), entry.team, entry.score );
	gameLocal.ServerSendChatMessage( clientNum, "server", ID_STR_RESTORED );
	return true;
}

/*
================
idMultiplayerGame::IdentityClientDisconnect

A player who leaves a match in play keeps their place for g_reconnectRestore
seconds, by identity and by ghost code; then the slot forgets them.
================
*/
void idMultiplayerGame::IdentityClientDisconnect( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	if ( IdentityCasual() && g_reconnectRestore.GetInteger() > 0 && clientNum != gameLocal.localClientNum &&
		 !botManager.IsBot( clientNum ) && gameState != NULL && gameState->GetMPGameState() != GAMEREVIEW &&
		 !MPGameTypeHasAny( gameLocal.gameType, GTF_BRACKET ) && gameLocal.entities[ clientNum ] != NULL &&
		 gameLocal.entities[ clientNum ]->IsType( idPlayer::GetClassType() ) &&
		 ( identityFingerprint[ clientNum ][ 0 ] != '\0' || identityGhost[ clientNum ][ 0 ] != '\0' ) ) {
		const idPlayer *player = static_cast<const idPlayer *>( gameLocal.entities[ clientNum ] );
		if ( !player->spectating ) {
			mpIdReturn_t entry;
			memset( &entry, 0, sizeof( entry ) );
			idStr::Copynz( entry.identity, identityFingerprint[ clientNum ], sizeof( entry.identity ) );
			idStr::Copynz( entry.ghost, identityGhost[ clientNum ], sizeof( entry.ghost ) );
			entry.team = player->team;
			entry.score = playerState[ clientNum ].fragCount;
			entry.teamScore = playerState[ clientNum ].teamFragCount;
			entry.matchStamp = identityMatchStamp;
			const mpIdTime_t now = static_cast<mpIdTime_t>( ::time( NULL ) );
			entry.expires = now + g_reconnectRestore.GetInteger();
			MPIdReturnSave( identityReturns, entry, now );
		}
	}
	IdentityClientConnect( clientNum );
}

/*
================
idMultiplayerGame::IdentityNewMatch
================
*/
void idMultiplayerGame::IdentityNewMatch( void ) {
	identityMatchStamp++;
	MPIdReturnClear( identityReturns );
	identityMatchRated = false;
	for ( int i = 0; i < MAX_CLIENTS; i++ ) {
		identityRestored[ i ] = false;
	}
}

/*
===============================================================================

	The local ranking table (g_ranking)

===============================================================================
*/

typedef struct mpRankingEntry_s {
	idStr		gameType;
	char		identity[ MP_ID_FINGERPRINT_TEXT_BYTES ];
	double		rating;
	int			matches;
	idStr		name;
} mpRankingEntry_t;

static idList<mpRankingEntry_t>	rankingTable;
static bool						rankingLoaded = false;
static bool						rankingBlocked = false;		// a file this build cannot read is never written over

// "Team DM" -> "team_dm": one token for the file.
static idStr RankingGameTypeToken( const char *gameType ) {
	idStr token = gameType != NULL ? gameType : "";
	token.ToLower();
	for ( int i = 0; i < token.Length(); i++ ) {
		const char c = token[ i ];
		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) ) ) {
			token[ i ] = '_';
		}
	}
	return token.Length() > 0 ? token : idStr( "unknown" );
}

static void RankingLoad( void ) {
	if ( rankingLoaded ) {
		return;
	}
	rankingLoaded = true;
	rankingTable.Clear();
	idStr text;
	idFile *probe = fileSystem->OpenExplicitFileRead( fileSystem->RelativePathToOSPath( MP_RANKING_FILE, "fs_savepath" ) );
	if ( probe == NULL ) {
		return;
	}
	fileSystem->CloseFile( probe );
	if ( !IdentityReadSaveFile( MP_RANKING_FILE, MP_RANKING_FILE_MAX_BYTES, text ) || idStr::Cmpn( text.c_str(), MP_RANKING_HEADER, static_cast<int>( sizeof( MP_RANKING_HEADER ) - 1 ) ) != 0 ) {
		rankingBlocked = true;
		common->Warning( "ranking: %s is not a ranking file this build reads; it is left as it is and not updated", MP_RANKING_FILE );
		return;
	}
	int start = 0;
	for ( int i = 0; i <= text.Length(); i++ ) {
		if ( i != text.Length() && text[ i ] != '\n' ) {
			continue;
		}
		idStr line = text.Mid( start, i - start );
		start = i + 1;
		line.StripTrailingWhitespace();
		char token[ MP_MOD_REASON_BYTES ];
		const char *at = MPModReadToken( line.c_str(), token, sizeof( token ) );
		if ( at == 0 || idStr::Cmp( token, "rating" ) != 0 || rankingTable.Num() >= MP_RANKING_MAX_ENTRIES ) {
			continue;
		}
		mpRankingEntry_t entry;
		entry.rating = MP_RATING_INITIAL;
		entry.matches = 0;
		bool ok = ( at = MPModReadToken( at, token, sizeof( token ) ) ) != 0;
		if ( ok ) {
			entry.gameType = RankingGameTypeToken( token );
		}
		ok = ok && ( at = MPModReadToken( at, token, sizeof( token ) ) ) != 0 && MPIdParseFingerprint( token, entry.identity );
		ok = ok && ( at = MPModReadToken( at, token, sizeof( token ) ) ) != 0 && idStr::IsNumeric( token );
		if ( ok ) {
			entry.rating = atoi( token ) / 100.0;
		}
		ok = ok && ( at = MPModReadToken( at, token, sizeof( token ) ) ) != 0 && idStr::IsNumeric( token );
		if ( ok ) {
			entry.matches = Max( 0, atoi( token ) );
			if ( MPModReadToken( at, token, sizeof( token ) ) != 0 ) {
				entry.name = token;
			}
			rankingTable.Append( entry );
		}
	}
}

static void RankingSave( void ) {
	if ( rankingBlocked ) {
		return;
	}
	idFile *file = fileSystem->OpenFileWrite( MP_RANKING_STAGED, "fs_savepath" );
	if ( file == NULL ) {
		common->Warning( "ranking: could not write %s", MP_RANKING_STAGED );
		return;
	}
	file->Printf( "%s\n", MP_RANKING_HEADER );
	file->Printf( "# rating <game type> <identity> <rating x 100> <rated matches> \"<last name seen>\"\n" );
	for ( int i = 0; i < rankingTable.Num(); i++ ) {
		const mpRankingEntry_t &entry = rankingTable[ i ];
		idStr name;
		for ( int c = 0; c < entry.name.Length(); c++ ) {
			if ( entry.name[ c ] == '"' || entry.name[ c ] == '\\' ) {
				name += '\\';
			}
			if ( entry.name[ c ] != '\n' && entry.name[ c ] != '\r' ) {
				name += entry.name[ c ];
			}
		}
		file->Printf( "rating %s %s %d %d \"%s\"\n", entry.gameType.c_str(), entry.identity,
			idMath::FtoiFast( static_cast<float>( entry.rating * 100.0 ) ), entry.matches, name.c_str() );
	}
	fileSystem->CloseFile( file );
	if ( !fileSystem->PromoteFile( MP_RANKING_STAGED, MP_RANKING_FILE, "fs_savepath" ) ) {
		fileSystem->RemoveFileChecked( MP_RANKING_STAGED, "fs_savepath" );
		common->Warning( "ranking: could not replace %s", MP_RANKING_FILE );
	}
}

static mpRankingEntry_t &RankingEntry( const idStr &gameType, const char *identity, const char *name ) {
	for ( int i = 0; i < rankingTable.Num(); i++ ) {
		if ( rankingTable[ i ].gameType == gameType && idStr::Cmp( rankingTable[ i ].identity, identity ) == 0 ) {
			if ( name != NULL && name[ 0 ] != '\0' ) {
				rankingTable[ i ].name = name;
			}
			return rankingTable[ i ];
		}
	}
	mpRankingEntry_t entry;
	entry.gameType = gameType;
	idStr::Copynz( entry.identity, identity, sizeof( entry.identity ) );
	entry.rating = MP_RATING_INITIAL;
	entry.matches = 0;
	entry.name = name != NULL ? name : "";
	rankingTable.Append( entry );
	return rankingTable[ rankingTable.Num() - 1 ];
}

/*
================
idMultiplayerGame::IdentityMatchEnded

The final board rates the players who proved an identity: Elo, by game type.
Two teams: each player against the other team's average, by the team result.
Free for all: every rated player against every other, by score. Bracket
modes are not rated, nor is a match with fewer than two rated players (one on
each side in a team game).
================
*/
void idMultiplayerGame::IdentityMatchEnded( void ) {
	if ( !IdentityRankingEnabled() || identityMatchRated || MPGameTypeHasAny( gameLocal.gameType, GTF_BRACKET ) ) {
		return;
	}
	identityMatchRated = true;
	RankingLoad();
	if ( rankingBlocked ) {
		return;
	}
	const idStr gameType = RankingGameTypeToken( gameLocal.serverInfo.GetString( "si_gameType" ) );
	idList<int> rated;
	for ( int i = 0; i < gameLocal.numClients && i < MAX_CLIENTS; i++ ) {
		const idEntity *ent = gameLocal.entities[ i ];
		if ( ent == NULL || !ent->IsType( idPlayer::GetClassType() ) || botManager.IsBot( i ) || identityFingerprint[ i ][ 0 ] == '\0' ) {
			continue;
		}
		const idPlayer *player = static_cast<const idPlayer *>( ent );
		if ( player->spectating && !gameLocal.IsTeamGame() ) {
			continue;
		}
		rated.Append( i );
	}
	if ( rated.Num() < 2 ) {
		return;
	}
	idList<double> changes;
	if ( gameLocal.IsTeamGame() ) {
		double sum[ 2 ] = { 0.0, 0.0 };
		int count[ 2 ] = { 0, 0 };
		for ( int i = 0; i < rated.Num(); i++ ) {
			const int team = static_cast<const idPlayer *>( gameLocal.entities[ rated[ i ] ] )->team;
			if ( team == TEAM_MARINE || team == TEAM_STROGG ) {
				sum[ team ] += RankingEntry( gameType, identityFingerprint[ rated[ i ] ], gameLocal.userInfo[ rated[ i ] ].GetString( "ui_name" ) ).rating;
				count[ team ]++;
			}
		}
		if ( count[ TEAM_MARINE ] == 0 || count[ TEAM_STROGG ] == 0 ) {
			return;
		}
		// the teams' scores (GetTeamScore is one player's share of them)
		const int marine = GetScoreForTeam( TEAM_MARINE ), strogg = GetScoreForTeam( TEAM_STROGG );
		const double average[ 2 ] = { sum[ 0 ] / count[ 0 ], sum[ 1 ] / count[ 1 ] };
		for ( int i = 0; i < rated.Num(); i++ ) {
			const int team = static_cast<const idPlayer *>( gameLocal.entities[ rated[ i ] ] )->team;
			if ( team != TEAM_MARINE && team != TEAM_STROGG ) {
				changes.Append( 0.0 );
				continue;
			}
			const int own = team == TEAM_MARINE ? marine : strogg, other = team == TEAM_MARINE ? strogg : marine;
			const double result = own > other ? 1.0 : ( own == other ? 0.5 : 0.0 );
			const mpRankingEntry_t &entry = RankingEntry( gameType, identityFingerprint[ rated[ i ] ], NULL );
			changes.Append( MPRatingTeamChange( average[ team ], average[ 1 - team ], result, entry.matches ) );
		}
	} else {
		idList<double> ratings;
		idList<int> scores, matches;
		for ( int i = 0; i < rated.Num(); i++ ) {
			const mpRankingEntry_t &entry = RankingEntry( gameType, identityFingerprint[ rated[ i ] ], gameLocal.userInfo[ rated[ i ] ].GetString( "ui_name" ) );
			ratings.Append( entry.rating );
			matches.Append( entry.matches );
			scores.Append( GetScore( rated[ i ] ) );
		}
		changes.SetNum( rated.Num() );
		MPRatingFreeForAll( ratings.Ptr(), scores.Ptr(), matches.Ptr(), rated.Num(), changes.Ptr() );
	}
	for ( int i = 0; i < rated.Num(); i++ ) {
		mpRankingEntry_t &entry = RankingEntry( gameType, identityFingerprint[ rated[ i ] ], NULL );
		entry.rating += changes[ i ];
		entry.matches++;
		common->Printf( "ranking: %s^0 %s %.1f (%+.1f)\n", entry.name.c_str(), gameType.c_str(), entry.rating, changes[ i ] );
	}
	RankingSave();
}

/*
================
idMultiplayerGame::Rankings_f
"rankings [game type]": the server's local table, best first.
================
*/
void idMultiplayerGame::Rankings_f( const idCmdArgs &args ) {
	if ( !gameLocal.isMultiplayer || !gameLocal.isServer ) {
		common->Printf( "rankings: the server keeps the table; from a client use 'rcon rankings'\n" );
		return;
	}
	RankingLoad();
	const idStr gameType = RankingGameTypeToken( args.Argc() > 1 ? args.Args( 1 ) : gameLocal.serverInfo.GetString( "si_gameType" ) );
	idList<int> rows;
	for ( int i = 0; i < rankingTable.Num(); i++ ) {
		if ( rankingTable[ i ].gameType == gameType ) {
			rows.Append( i );
		}
	}
	for ( int i = 1; i < rows.Num(); i++ ) {
		for ( int j = i; j > 0 && rankingTable[ rows[ j ] ].rating > rankingTable[ rows[ j - 1 ] ].rating; j-- ) {
			const int swap = rows[ j ];
			rows[ j ] = rows[ j - 1 ];
			rows[ j - 1 ] = swap;
		}
	}
	common->Printf( "rankings for %s%s:\n", gameType.c_str(), g_ranking.GetBool() ? "" : " (g_ranking is 0: not being updated)" );
	for ( int i = 0; i < rows.Num() && i < 50; i++ ) {
		const mpRankingEntry_t &entry = rankingTable[ rows[ i ] ];
		common->Printf( "%3d  %7.1f  %4d  %s  %s\n", i + 1, entry.rating, entry.matches, entry.identity, entry.name.c_str() );
	}
	common->Printf( "%d rated player(s)\n", rows.Num() );
}

/*
===============================================================================

	The client's side

===============================================================================
*/

/*
================
idMultiplayerGame::IdentityLoadSeed

The player's seed from identity.key, made on first use. The file holds the
only copy: deleting it, or identityReset, makes the player new everywhere.
================
*/
bool idMultiplayerGame::IdentityLoadSeed( unsigned char seed[ MP_ID_SEED_BYTES ], bool create ) {
	idStr text;
	if ( IdentityReadSaveFile( MP_ID_SEED_FILE, MP_ID_FILE_MAX_BYTES, text ) ) {
		const bool parsed = IdentityParseHexFile( text, MP_ID_SEED_HEADER, seed, MP_ID_SEED_BYTES );
		idCrypto::SecureWipe( const_cast<char *>( text.c_str() ), text.Length() );
		if ( parsed ) {
			return true;
		}
		if ( !create ) {
			return false;
		}
		common->Warning( "identity: %s is damaged; a new identity replaces it", MP_ID_SEED_FILE );
	}
	if ( !create || !sys->SecureRandomBytes( seed, MP_ID_SEED_BYTES ) ) {
		return false;
	}
	if ( !IdentityWriteHexFile( MP_ID_SEED_FILE, MP_ID_SEED_HEADER,
			"Your multiplayer identity. Keep this file private; delete it, or run identityReset, to become a new player everywhere.",
			seed, MP_ID_SEED_BYTES ) ) {
		common->Warning( "identity: could not write %s; servers see a new player each time the game starts", MP_ID_SEED_FILE );
	}
	return true;
}

/*
================
idMultiplayerGame::ClientReceiveIdentityChallenge

Sign the server's challenge with this player's key for that server, unless
the player turned identity off.
================
*/
void idMultiplayerGame::ClientReceiveIdentityChallenge( const idBitMsg &msg ) {
	if ( !gameLocal.isClient || msg.GetRemainingReadBits() != MP_ID_CHALLENGE_BITS ) {
		return;
	}
	if ( msg.ReadByte() != MP_ID_MSG_CHALLENGE ) {
		return;
	}
	unsigned char serverId[ MP_ID_SERVER_ID_BYTES ], nonce[ MP_ID_NONCE_BYTES ];
	msg.ReadData( serverId, sizeof( serverId ) );
	msg.ReadData( nonce, sizeof( nonce ) );
	const int slot = msg.ReadByte();
	const unsigned int connection = static_cast<unsigned int>( msg.ReadLong() );
	if ( slot != gameLocal.localClientNum || !ui_identity.GetBool() ) {
		return;
	}
	unsigned char seed[ MP_ID_SEED_BYTES ];
	if ( !IdentityLoadSeed( seed, true ) ) {
		idCrypto::SecureWipe( seed, sizeof( seed ) );
		return;
	}
	unsigned char publicKey[ MP_ID_PUBLIC_KEY_BYTES ], signature[ MP_ID_SIGNATURE_BYTES ];
	MPIdSignProof( seed, serverId, nonce, slot, connection, publicKey, signature );
	idCrypto::SecureWipe( seed, sizeof( seed ) );
	MPIdFingerprint( publicKey, identityLocalFingerprint );

	idBitMsg	outMsg;
	byte		msgBuf[ 1 + MP_ID_PROOF_BITS / 8 + 8 ];
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.BeginWriting();
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_IDENTITY );
	outMsg.WriteByte( MP_ID_MSG_PROOF );
	outMsg.WriteData( publicKey, sizeof( publicKey ) );
	outMsg.WriteData( signature, sizeof( signature ) );
	networkSystem->ClientSendReliableMessage( outMsg );
	common->DPrintf( "identity: answered the server's challenge as %s\n", identityLocalFingerprint );
}

/*
================
idMultiplayerGame::Identity_f
"identity": the player's identity on the server they are connected to.
================
*/
void idMultiplayerGame::Identity_f( const idCmdArgs &args ) {
	if ( !ui_identity.GetBool() ) {
		common->Printf( "%s\n", common->GetLocalizedString( ID_STR_IDENTITY_OFF ) );
		return;
	}
	if ( gameLocal.isMultiplayer && gameLocal.isServer && gameLocal.localClientNum >= 0 ) {
		const char *own = gameLocal.mpGame.ClientIdentity( gameLocal.localClientNum );
		common->Printf( "identity on this server: %s\n", own[ 0 ] != '\0' ? own : "(none yet)" );
		return;
	}
	common->Printf( "identity on this server: %s\n", identityLocalFingerprint[ 0 ] != '\0' ? identityLocalFingerprint : "(none yet; the server has not asked)" );
}

/*
================
idMultiplayerGame::IdentityReset_f
"identityReset confirm": a new seed, so every server sees a new player.
================
*/
void idMultiplayerGame::IdentityReset_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 || idStr::Icmp( args.Argv( 1 ), "confirm" ) != 0 ) {
		common->Printf( "%s\n", common->GetLocalizedString( ID_STR_RESET_CONFIRM ) );
		return;
	}
	unsigned char seed[ MP_ID_SEED_BYTES ];
	if ( !sys->SecureRandomBytes( seed, sizeof( seed ) ) ||
		 !IdentityWriteHexFile( MP_ID_SEED_FILE, MP_ID_SEED_HEADER,
			"Your multiplayer identity. Keep this file private; delete it, or run identityReset, to become a new player everywhere.",
			seed, MP_ID_SEED_BYTES ) ) {
		idCrypto::SecureWipe( seed, sizeof( seed ) );
		common->Printf( "identityReset: could not write %s\n", MP_ID_SEED_FILE );
		return;
	}
	idCrypto::SecureWipe( seed, sizeof( seed ) );
	identityLocalFingerprint[ 0 ] = '\0';
	common->Printf( "%s\n", common->GetLocalizedString( ID_STR_RESET_DONE ) );
}

/*
================
idMultiplayerGame::Ghost_f
"ghost <code>": a returning player without an identity claims their place.
================
*/
void idMultiplayerGame::Ghost_f( const idCmdArgs &args ) {
	char code[ MP_ID_GHOST_CODE_LENGTH + 1 ];
	if ( args.Argc() != 2 || !MPIdParseGhostCode( args.Argv( 1 ), code ) ) {
		common->Printf( "%s\n", common->GetLocalizedString( ID_STR_GHOST_USAGE ) );
		return;
	}
	if ( !gameLocal.isMultiplayer || !gameLocal.isClient ) {
		common->Printf( "ghost: only a player who joined a server needs this\n" );
		return;
	}
	idBitMsg	outMsg;
	byte		msgBuf[ 16 ];
	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.BeginWriting();
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_IDENTITY );
	outMsg.WriteByte( MP_ID_MSG_GHOST );
	outMsg.WriteString( code );
	networkSystem->ClientSendReliableMessage( outMsg );
}

/*
===============================================================================

	The player's match history (profile/history.ndjson)

===============================================================================
*/

static void HistoryAppendString( idStr &line, const char *key, const char *value ) {
	line += va( ",\"%s\":\"", key );
	for ( const char *c = value; c != NULL && *c != '\0'; c++ ) {
		const unsigned char u = static_cast<unsigned char>( *c );
		if ( *c == '"' || *c == '\\' ) {
			line += '\\';
			line += *c;
		} else if ( u < 0x20 ) {
			line += va( "\\u%04x", u );
		} else {
			line += *c;
		}
	}
	line += "\"";
}

/*
================
idMultiplayerGame::IdentityRecordHistory

When a match ends, the local player's line: when, where, the map and game
type, their score and place, and the result. Kept on this computer only.
================
*/
void idMultiplayerGame::IdentityRecordHistory( void ) {
	idPlayer *local = gameLocal.GetLocalPlayer();
	if ( local == NULL || !gameLocal.isMultiplayer || IsArenaCampaignMatch() ) {
		return;
	}
	const int self = local->entityNumber;
	const int score = GetScore( self );
	int place = 1, players = 0;
	for ( int i = 0; i < gameLocal.numClients && i < MAX_CLIENTS; i++ ) {
		const idEntity *ent = gameLocal.entities[ i ];
		if ( ent == NULL || !ent->IsType( idPlayer::GetClassType() ) || static_cast<const idPlayer *>( ent )->spectating ) {
			continue;
		}
		players++;
		if ( i != self && GetScore( i ) > score ) {
			place++;
		}
	}
	const char *result = "played";
	if ( gameLocal.IsTeamGame() && ( local->team == TEAM_MARINE || local->team == TEAM_STROGG ) ) {
		const int own = GetScoreForTeam( local->team ), other = GetScoreForTeam( 1 - local->team );
		result = own > other ? "won" : ( own == other ? "drew" : "lost" );
	} else if ( !local->spectating ) {
		result = place == 1 ? "won" : "lost";
	} else {
		result = "watched";
	}
	char utc[ MP_MOD_UTC_TEXT_BYTES ];
	MPModFormatUtc( static_cast<mpModTime_t>( ::time( NULL ) ), utc );
	idStr line = va( "{\"v\":1,\"utc\":\"%s\"", utc );
	HistoryAppendString( line, "server", gameLocal.serverInfo.GetString( "si_name" ) );
	HistoryAppendString( line, "map", gameLocal.serverInfo.GetString( "si_map" ) );
	HistoryAppendString( line, "gametype", gameLocal.serverInfo.GetString( "si_gameType" ) );
	HistoryAppendString( line, "name", gameLocal.userInfo[ self ].GetString( "ui_name" ) );
	HistoryAppendString( line, "result", result );
	line += va( ",\"score\":%d,\"place\":%d,\"players\":%d", score, place, players );
	if ( gameLocal.IsTeamGame() ) {
		line += va( ",\"team\":%d", local->team );
	}
	line += "}";
	idFile *file = fileSystem->OpenFileAppend( MP_HISTORY_FILE, false, "fs_savepath" );
	if ( file == NULL ) {
		file = fileSystem->OpenFileWrite( MP_HISTORY_FILE, "fs_savepath" );
	}
	if ( file == NULL ) {
		return;
	}
	file->Printf( "%s\n", line.c_str() );
	fileSystem->CloseFile( file );
}

/*
================
idMultiplayerGame::History_f
"history": the player's last matches, newest last.
================
*/
void idMultiplayerGame::History_f( const idCmdArgs &args ) {
	idStr text;
	if ( !IdentityReadSaveFile( MP_HISTORY_FILE, 16 * 1024 * 1024, text ) || text.Length() == 0 ) {
		common->Printf( "history: no matches recorded yet (%s)\n", MP_HISTORY_FILE );
		return;
	}
	idStrList lines;
	int start = 0;
	for ( int i = 0; i <= text.Length(); i++ ) {
		if ( i == text.Length() || text[ i ] == '\n' ) {
			idStr line = text.Mid( start, i - start );
			line.StripTrailingWhitespace();
			if ( line.Length() > 0 ) {
				lines.Append( line );
			}
			start = i + 1;
		}
	}
	const auto field = []( const idStr &line, const char *key ) -> idStr {
		const idStr quoted = va( "\"%s\":", key );
		const int at = line.Find( quoted.c_str() );
		if ( at < 0 ) {
			return idStr();
		}
		int begin = at + quoted.Length();
		if ( begin < line.Length() && line[ begin ] == '"' ) {
			begin++;
			int end = begin;
			while ( end < line.Length() && line[ end ] != '"' ) {
				end += line[ end ] == '\\' ? 2 : 1;
			}
			return line.Mid( begin, Min( end, line.Length() ) - begin );
		}
		int end = begin;
		while ( end < line.Length() && line[ end ] != ',' && line[ end ] != '}' ) {
			end++;
		}
		return line.Mid( begin, end - begin );
	};
	for ( int i = Max( 0, lines.Num() - MP_HISTORY_SHOWN ); i < lines.Num(); i++ ) {
		const idStr &line = lines[ i ];
		common->Printf( "%s  %-8s %-14s %-10s score %-4s place %s of %s  %s\n", field( line, "utc" ).c_str(), field( line, "result" ).c_str(),
			field( line, "map" ).c_str(), field( line, "gametype" ).c_str(), field( line, "score" ).c_str(), field( line, "place" ).c_str(),
			field( line, "players" ).c_str(), field( line, "server" ).c_str() );
	}
	common->Printf( "%d match(es) recorded in %s\n", lines.Num(), MP_HISTORY_FILE );
}
