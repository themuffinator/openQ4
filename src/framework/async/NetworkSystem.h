/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __NETWORKSYSTEM_H__
#define __NETWORKSYSTEM_H__


/*
===============================================================================

  Network System.

===============================================================================
*/

typedef struct {
	idStr		nickname;
	idStr		clan;
	short		ping;
	int			rate;
} scannedClient_t;

typedef struct {
	netadr_t	adr;
	idDict		serverInfo;
	int			ping;

	int			clients;

	int			OSMask;

	//RAVEN BEGIN
	// shouchard:  added favorite flag
	bool		favorite;	// true if this has been marked by a user as a favorite
	bool		dedicated;
	// shouchard:  added performance filtered flag
	bool		performanceFiltered;	// true if the client machine is too wimpy to have good performance
//RAVEN END
} scannedServer_t;

// Fixed-width, append-only game-module ABI for the server MVD publisher.
// The query that fills this value is fallible: false means that no recording
// attempt has produced a result yet.  A pending result names only the staged
// stream; a committed result names only the published stream; and a failed
// result may name the recoverable staged stream that was left behind.
static const int SERVER_MVD_RESULT_QPATH_BYTES = 160;

typedef enum {
	SERVER_MVD_RESULT_PENDING = 0,
	SERVER_MVD_RESULT_COMMITTED,
	SERVER_MVD_RESULT_FAILED,
	SERVER_MVD_RESULT_STATE_COUNT
} serverMVDResultState_t;

typedef enum {
	SERVER_MVD_REASON_NONE = 0,
	SERVER_MVD_REASON_START_REJECTED,
	SERVER_MVD_REASON_NAME_UNAVAILABLE,
	SERVER_MVD_REASON_OPEN_FAILED,
	SERVER_MVD_REASON_SCHEMA_INVALID,
	SERVER_MVD_REASON_INITIALIZATION_FAILED,
	SERVER_MVD_REASON_STREAM_WRITE_FAILED,
	SERVER_MVD_REASON_FINALIZE_WRITE_FAILED,
	SERVER_MVD_REASON_SYNC_FAILED,
	SERVER_MVD_REASON_PROMOTE_FAILED,
	SERVER_MVD_REASON_COUNT
} serverMVDResultReason_t;

typedef struct {
	serverMVDResultState_t	state;
	serverMVDResultReason_t	reason;
	char					finalQPath[ SERVER_MVD_RESULT_QPATH_BYTES + 1 ];
	char					partialQPath[ SERVER_MVD_RESULT_QPATH_BYTES + 1 ];
} serverMVDRecordingResult_t;

// Fixed-width, append-only game-module ABI for structured MVD markers. The
// recorder stamps each marker with the server frame and time; text is display
// data, limited to printable bytes, never an instruction for playback.
static const int SERVER_MVD_MARKER_TEXT_BYTES = 120;

typedef enum {
	SERVER_MVD_MARKER_NONE = 0,
	SERVER_MVD_MARKER_PHASE,		// value0 previous match phase, value1 new phase
	SERVER_MVD_MARKER_ROUND,		// value0 previous round state, value1 new round state
	SERVER_MVD_MARKER_PAUSE,		// value0 new pause state, value1 kind | ( owner side + 1 ) << 8
	SERVER_MVD_MARKER_SCORE,		// value0 team, value1 new team score
	SERVER_MVD_MARKER_KILL,			// client attacker (-1 world), value0 victim, value1 method of death
	SERVER_MVD_MARKER_FLAG,			// client carrier (-1 none), value0 flag team, value1 new flag state
	SERVER_MVD_MARKER_POWERUP,		// client holder, value0 powerup
	SERVER_MVD_MARKER_NOTE,			// text from an operator
	SERVER_MVD_MARKER_KIND_COUNT
} serverMVDMarkerKind_t;

typedef struct {
	int						kind;
	int						client;		// -1 when no player owns the event
	int						instance;	// -1 for every instance
	int						value0;
	int						value1;
	char					text[ SERVER_MVD_MARKER_TEXT_BYTES + 1 ];
} serverMVDMarker_t;

typedef enum {
	SC_NONE = -1,
	SC_FAVORITE,
	SC_LOCKED,
	SC_DEDICATED,
	SC_PB,
	SC_NAME,
	SC_PING,
	SC_REPEATER,
	SC_PLAYERS,
	SC_GAMETYPE,
	SC_MAP,
	SC_ALL,
	NUM_SC
} sortColumn_t;

typedef struct {
	sortColumn_t			column;
	idList<int>::cmp_t* compareFn;
	idList<int>::filter_t* filterFn;
	const char* description;
} sortInfo_t;

class idNetworkSystem {
public:
	virtual					~idNetworkSystem( void ) {}

// jmarshall
	virtual int				AllocateClientSlotForBot(const char *botName, int maxPlayersOnServer);
	virtual int				ServerSetBotUserCommand(int clientNum, int frameNum, const usercmd_t& cmd);
	virtual int				ServerSetBotUserName(int clientNum, const char* playerName);
// jmarshall end

	virtual void			ServerSendReliableMessage( int clientNum, const idBitMsg &msg );
	virtual void			ServerSendReliableMessageExcluding( int clientNum, const idBitMsg &msg );
	// Instance-scoped game helpers fan out one logical message to several
	// clients. These variants avoid recording each physical delivery.
	virtual void			ServerSendReliableMessageNoDemo( int clientNum, const idBitMsg &msg );
	virtual void			ServerSendReliableMessageExcludingNoDemo( int clientNum, const idBitMsg &msg );
	virtual void			ServerRecordInstanceReliableMessage( int instance, int excludeClient, const idBitMsg &msg );
	virtual int				ServerGetClientPing( int clientNum );
	virtual int				ServerGetClientPrediction( int clientNum );
	virtual int				ServerGetClientTimeSinceLastPacket( int clientNum );
	virtual int				ServerGetClientTimeSinceLastInput( int clientNum );
	virtual int				ServerGetClientOutgoingRate( int clientNum );
	virtual int				ServerGetClientIncomingRate( int clientNum );
	virtual float			ServerGetClientIncomingPacketLoss( int clientNum );
	// Typed game-side moderation must not synthesize console command text.
	// Returns false for an inactive server, invalid slot, local listen host or
	// invalid reason; successful calls synchronously retire the connection.
	virtual bool			ServerDropClient( int clientNum, const char *reason );

	virtual void			ClientSendReliableMessage( const idBitMsg &msg );
	virtual int				ClientGetPrediction( void );
	virtual int				ClientGetTimeSinceLastPacket( void );
	virtual int				ClientGetOutgoingRate( void );
	virtual int				ClientGetIncomingRate( void );
	virtual float			ClientGetIncomingPacketLoss( void );

public:
	// RAVEN BEGIN
// ddynerman: added some utility functions
	// uses a static buffer, copy it before calling in game again
	virtual const char* GetServerAddress(void);
	virtual const char* GetClientAddress(int clientNum) { return 0; }
	virtual	void			AddFriend(int clientNum) { }
	virtual void			RemoveFriend(int clientNum) { }
	// for MP games
	virtual void			SetLoadingText(const char* loadingText);
	virtual void			AddLoadingIcon(const char* icon);
	virtual const char* GetClientGUID(int clientNum) { return 0; }
	// RAVEN END

	virtual void			GetTrafficStats(int& bytesSent, int& packetsSent, int& bytesReceived, int& packetsReceived) const { }

	// server browser
	virtual int				GetNumScannedServers(void) { return 0; }
	virtual const scannedServer_t* GetScannedServerInfo(int serverNum) { return 0; }
	virtual const scannedClient_t* GetScannedServerClientInfo(int serverNum, int clientNum) { return 0; }
	virtual void			AddSortFunction(const sortInfo_t& sortInfo) { }
	virtual bool			RemoveSortFunction(const sortInfo_t& sortInfo) { return 0; }
	virtual void			UseSortFunction(const sortInfo_t& sortInfo, bool use = true) { }
	virtual bool			SortFunctionIsActive(const sortInfo_t& sortInfo) { return 0; }

	// returns true if enabled
	virtual bool			HTTPEnable(bool enable) { return 0; }

	virtual void			ClientSetServerInfo(const idDict& serverSI) { }
	virtual void			RepeaterSetInfo(const idDict& info) { }

	virtual const char* GetViewerGUID(int clientNum) { return 0; }

	virtual int				ServerGetClientNum(int clientId) { return 0; }
	virtual	int				ServerGetServerTime(void) { return 0; }

	// returns the new clientNum or -1 if there weren't any free.
	virtual int				ServerConnectBot(void) { return 0; }

	virtual int				RepeaterGetClientNum(int clientId) { return 0; }

	// Append-only game-module ABI: server-owned multi-view demo lifecycle.
	virtual bool			ServerStartMVDRecording( const char *name );
	virtual bool			ServerStopMVDRecording( const char *reason );
	virtual bool			ServerIsMVDRecording( void ) const;
	virtual bool			ServerCopyMVDRecordingQPath( char *buffer, int bufferSize ) const;
	virtual bool			ServerCopyMVDRecordingResult( serverMVDRecordingResult_t &result ) const;
	// Voice has exact recipients, no spectator/demo fanout and no reliable
	// delivery. The server batches only the current frame, after snapshots.
	// False means dropped; true never promises delivery of disposable media.
	virtual bool			ClientSendVoice( const idBitMsg &msg );
	virtual bool			ServerSendVoice( int clientNum, const idBitMsg &msg );
	// False when no recording is active or the marker is malformed.
	virtual bool			ServerRecordMVDMarker( const serverMVDMarker_t &marker );
};

extern idNetworkSystem *	networkSystem;

#endif /* !__NETWORKSYSTEM_H__ */
