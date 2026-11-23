/*
===========================================================================

openQ4 reader for retail Quake 4 1.4.2 network demos (.ndmo85, .netdemo).

A retail network demo is the packet stream one client received from a 1.4.2
server (or, for a server demo, the stream a server wrote for a spectator in
slot 32), behind a header holding the recording client's network state. This
module checks every length in the file before anything else reads it, runs
the 1.4.2 message channel (fragments, zero-run compression, the reliable
queue) and turns the packets into time-stamped events: game snapshots, game
reliables, acknowledged snapshots, user info and the recorder's own commands.
idMultiViewDemo plays those events through its own client-view and
server-view players, with the game module reading the 1.4.2 layouts.

===========================================================================
*/

#ifndef __RETAILNETDEMO_H__
#define __RETAILNETDEMO_H__

#include <vector>

class idBitMsg;
class usercmd_t;

namespace openq4_retail {

// idDemo::WriteHeader in Quake4.exe 1.4.2
const int NETDEMO_IDENT				= 0x4F4D444E;	// "NDMO"
const int NETDEMO_VERSION			= 12;
const int NETDEMO_PROTOCOL_MAJOR	= 2;
const int NETDEMO_PROTOCOL_MINOR	= 85;

// Bounds the 1.4.2 executable and game module use for what the header and
// stream hold. None of them can grow without breaking the recorded layout.
const int NETDEMO_MAX_CLIENTS		= 32;			// MAX_ASYNC_CLIENTS
const int NETDEMO_USERCMD_BACKUP	= 256;			// MAX_USERCMD_BACKUP
const int NETDEMO_MAX_ENTITIES		= 1 << 12;		// MAX_GENTITIES
const int NETDEMO_PVS_WORDS			= ( ( 1 << 12 ) + 31 ) >> 5;	// ENTITY_PVS_SIZE
const int NETDEMO_MAX_STATE_BYTES	= 512;			// MAX_ENTITY_STATE_SIZE
const int NETDEMO_MAX_SNAPSHOTS		= 64;
const int NETDEMO_MAX_GAME_MESSAGE	= 8192;			// MAX_GAME_MESSAGE_SIZE
const int NETDEMO_MAX_MESSAGE		= 16384;		// MAX_MESSAGE_SIZE
const int NETDEMO_MAX_QUEUE			= 16384;		// MAX_MSG_QUEUE_SIZE
const int NETDEMO_FRAGMENT_SIZE		= 1400 - 100;	// MAX_PACKETLEN - 100
const int NETDEMO_MAX_PURE_PAKS		= 128;
const int NETDEMO_GAME_PAK_SLOTS	= 6 * 10;		// six OS ids, ten game paks each
const int NETDEMO_MAX_RELAYED_CMDS	= 10;			// net_serverMaxUsercmdRelay's ceiling
const int NETDEMO_MAX_FILE_BYTES	= 512 * 1024 * 1024;
const int NETDEMO_MAX_PACKETS		= 4000000;

// The 1.4.2 SERVER_RELIABLE_MESSAGE ids (idAsyncClient::ProcessReliableServerMessages).
enum retailReliable_t {
	RETAIL_RELIABLE_PURE = 0,
	RETAIL_RELIABLE_RELOAD,
	RETAIL_RELIABLE_CLIENTINFO,
	RETAIL_RELIABLE_SYNCEDCVARS,
	RETAIL_RELIABLE_DISCONNECT,
	RETAIL_RELIABLE_APPLYSNAPSHOT,
	RETAIL_RELIABLE_GAME,
	RETAIL_RELIABLE_ENTERGAME,
	RETAIL_RELIABLE_CLIENT_TAGS,		// per-client 12-character strings, not needed to watch
	RETAIL_RELIABLE_RESEND_USERINFO
};

// The 1.4.2 SERVER_UNRELIABLE_MESSAGE ids.
enum retailUnreliable_t {
	RETAIL_UNRELIABLE_EMPTY = 0,
	RETAIL_UNRELIABLE_PING,
	RETAIL_UNRELIABLE_GAMEINIT,
	RETAIL_UNRELIABLE_SNAPSHOT
};

// A user command as the 1.4.2 demo header and command stream store it.
struct retailUsercmd_t {
	int				gameFrame;
	int				gameTime;
	int				realTime;
	int				duplicateCount;
	short			buttons;
	signed char		forwardmove;
	signed char		rightmove;
	signed char		upmove;
	short			angles[3];
	signed char		impulse;
	unsigned char	flags;
};

enum retailEventType_t {
	RETAIL_EVENT_SNAPSHOT = 0,		// data: the unreliable message, from its game id on
	RETAIL_EVENT_GAME_RELIABLE,		// data: the game's reliable message, after the engine id
	RETAIL_EVENT_APPLY_SNAPSHOT,	// sequence
	RETAIL_EVENT_USERINFO,			// client, info
	RETAIL_EVENT_USERCMD			// cmd, the recorder's own
};

struct retailEvent_t {
	int					type;
	int					frame;			// the recording's clock: the server's for a
	int					time;			// server demo, the recorder's own for a client demo
	int					sequence;		// snapshot sequence
	int					snapshotFrame;	// the snapshot's own frame and time
	int					snapshotTime;
	int					client;
	idList<byte>		data;
	idDict				info;
	retailUsercmd_t		cmd;
};

struct retailPacket_t {
	int					time;
	int					offset;
	int					size;
};

struct retailNetDemo_t {
	int					version;
	int					protocolMajor;
	int					protocolMinor;
	int					usercmdStart;
	idStr				fsGameBase;
	idStr				fsGame;
	idStrList			pakNames;
	idList<int>			pakChecksums;
	int					recordOS;
	bool				serverDemo;

	// idAsyncClient's state when the recording started
	int					clientDataChecksum;
	int					clientTime;
	int					clientId;
	int					clientNum;
	int					clientPrediction;
	int					clientPredictTime;
	int					serverId;
	int					serverChallenge;
	int					serverMessageSequence;
	int					snapshotSequence;
	int					snapshotGameFrame;
	int					snapshotGameTime;
	int					gameInitId;
	int					gameFrame;
	int					gameTime;
	int					gameTimeResidual;
	std::vector<retailUsercmd_t> initialUsercmds;	// the valid entries of the [256][32] table, client demos only
	std::vector<int>	initialUsercmdClients;

	// the message channel's state
	int					incomingSequence;
	int					fragmentSequence;
	idList<byte>		fragmentBuffer;
	int					reliableReceiveLast;

	idDict				userInfo[ NETDEMO_MAX_CLIENTS ];
	idDict				serverInfo;
	idDict				syncedCVars;

	// idGameLocal::WriteNetworkInfo's bytes, already walked: the client demo's
	// entity states, PVS, players and snapshots (a server demo has only the
	// players), then the multiplayer start state and game state
	idList<byte>		gameNetworkInfo;
	int					playerCount;
	idList<int>			playerSlots;

	std::vector<retailPacket_t> packets;
	std::vector<retailUsercmd_t> usercmds;			// the recorder's own commands, client demos only

	// filled in by Decode
	std::vector<retailEvent_t> events;
	int					snapshotCount;
	int					reliableCount;
	int					droppedPackets;
	int					endFrame;
	int					endTime;
	idStr				endReason;
};

// Whether a file name is a retail network demo's: .netdemo (1.0 to 1.4.1) or
// .ndmo<protocol> (1.4.2 writes .ndmo85).
bool		IsRetailNetDemoName( const char *path );

// Reads and checks everything but the packets' contents: the header, the
// recording client's state, the spawn data, the game's network info and the
// packet and command framing. No game code is involved.
bool		ParseContainer( const byte *data, int length, retailNetDemo_t &demo, idStr &error );

// Runs the 1.4.2 message channel over the packets of a parsed demo and fills
// in its events. msec is the length of one simulation tic.
bool		Decode( const byte *data, int length, retailNetDemo_t &demo, int msec, idStr &error );

// The 1.4.2 codec of the other clients' commands a client snapshot carries
// after the game's part (idAsyncNetwork::ReadUserCmdDelta in Quake4.exe 1.4.2).
bool		ReadRelayedUsercmd( const idBitMsg &msg, usercmd_t &cmd, const usercmd_t *base );

// A demo command as openQ4's usercmd_t.
void		ToUsercmd( const retailUsercmd_t &in, usercmd_t &out );

// Decodes the channel's zero-run coding (idCompressor_RunLength_ZeroBased, 3-bit words).
bool		DecodeZeroRun( const byte *in, int inLength, byte *out, int outLength );

// The demo's paks that are not loaded now, by name, ", " between.
int			MissingPaks( const retailNetDemo_t &demo, idStr &names );

// Quake 4 sends decls by their index, which openQ4's own decls would shift.
// While a retail demo plays, its numbering is rebuilt from the decls of the
// paks it was recorded with, in the order openQ4 loaded them, and the game
// module asks for each index through idNetworkSystem::RetailDemoDeclIndex.
void		BuildDeclRemap( const retailNetDemo_t &demo );
void		ClearDeclRemap();
bool		DeclRemapActive();
int			RemapDeclIndex( int declType, int retailIndex );	// -1 for none
int			RetailDeclCount( int declType );

}

#endif /* !__RETAILNETDEMO_H__ */
