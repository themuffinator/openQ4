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

#ifndef __ASYNCCLIENT_H__
#define __ASYNCCLIENT_H__

/*
===============================================================================

  Network Client for asynchronous networking.

===============================================================================
*/

typedef enum {
	CS_DISCONNECTED,
	CS_PURERESTART,
	CS_CHALLENGING,
	CS_CONNECTING,
	CS_CONNECTED,
	CS_INGAME
} clientState_t;

typedef enum {
	AUTHKEY_BADKEY,
	AUTHKEY_GUID
} authKeyMsg_t;

typedef enum {
	RCON_REPLY_NONE,
	RCON_REPLY_LEGACY,
	RCON_REPLY_CHALLENGE,
	RCON_REPLY_OUTPUT
} rconReplyState_t;

typedef struct rcon2ClientRequest_s {
	rconReplyState_t	state;
	netadr_t			address;
	int					startTime;
	int					lastSendTime;
	byte				clientNonce[16];
	byte				serverNonce[16];
	byte				requestDigest[32];
	byte				proof[32];
	char				command[MAX_STRING_CHARS];
} rcon2ClientRequest_t;

typedef enum {
	AUTHKEY_BAD_INVALID,
	AUTHKEY_BAD_BANNED,
	AUTHKEY_BAD_INUSE,
	AUTHKEY_BAD_MSG
} authBadKeyStatus_t;

typedef enum {
	UPDATE_NONE,
	UPDATE_SENT,
	UPDATE_READY,
	UPDATE_DONE
} clientUpdateState_t;

typedef struct {
	idStr	url;
	idStr	filename;
	int		size;
	int		checksum;
} pakDlEntry_t;

class idAsyncClient {
public:
						idAsyncClient();

	void				Shutdown( void );
	bool				InitPort( void );
	void				ClosePort( void );
	void				ConnectToServer( const netadr_t adr );
	void				ConnectToServer( const char *address );
	void				Reconnect( void );
	void				DisconnectFromServer( void );
	void				GetServerInfo( const netadr_t adr );
	void				GetServerInfo( const char *address );
	void				GetLANServers( void );
	void				GetNETServers( void );
	void				ListServers( void );
	void				ClearServers( void );
	void				RemoteConsole( const char *command );
	bool				IsPortInitialized() { return clientPort.GetPort() != 0; }

	bool				IsActive( void ) const;
	// Android: the app returned from suspendedMs in the background. A stay
	// longer than the time-out reconnects at once; after a shorter one the
	// connection must deliver a fresh packet within net_resumeProbe ms.
	void				ApplicationResumed( int suspendedMs );
	int					GetLocalClientNum( void ) const;
	const netadr_t&		GetServerAddress( void ) const { return serverAddress; }
	int					GetPrediction( void ) const;
	int					GetTimeSinceLastPacket( void ) const;
	int					GetOutgoingRate( void ) const;
	int					GetIncomingRate( void ) const;
	float				GetOutgoingCompression( void ) const;
	float				GetIncomingCompression( void ) const;
	float				GetIncomingPacketLoss( void ) const;
	int					GetPredictedFrames( void ) const { return lastFrameDelta; }

	void				RunFrame( bool allowBlocking = true );
	void				SendReliableGameMessage( const idBitMsg &msg );
	bool				SendVoiceMessage( const idBitMsg &msg );

	void				SendVersionCheck( bool fromMenu = false );
	// Legacy no-op in openQ4 (CD key auth removed).
	bool				SendAuthCheck( const char *cdkey, const char *xpkey );

	void				PacifierUpdate( void );

	idServerScan		serverList;

private:
	bool				active;						// true if client is active
	int					realTime;					// absolute time

	int					clientTime;					// client local time
	idPort				clientPort;					// UDP port
	int					clientId;					// client identification
	int					clientDataChecksum;			// checksum of the data used by the client
	int					clientNum;					// client number on server
	clientState_t		clientState;				// client state
	int					clientPrediction;			// how far the client predicts ahead
	int					clientPredictTime;			// prediction time used to send user commands

	netadr_t			serverAddress;				// IP address of server
	// openQ4: every address a typed host name resolved to, in the order they are
	// tried. While challenges go unanswered SetupConnection moves through them,
	// so one unreachable record no longer strands the connection.
	static const int	MAX_CONNECT_CANDIDATES = 8;
	netadr_t			connectCandidates[MAX_CONNECT_CANDIDATES];
	int					connectCandidateCount;
	int					connectCandidateIndex;		// the candidate in serverAddress
	int					connectChallengesSent;		// challenges sent to it so far
	// openQ4: the protocol of the server in serverAddress, from its answer to the
	// getInfo sent with each challenge. A Quake 4 1.4.x server answers the
	// challenge itself exactly as an openQ4 server does, game directory included.
	int					connectInfoRequests;		// getInfo requests sent to it so far
	bool				connectProtocolKnown;		// it answered with our protocol
	bool				connectChallengeDeferred;	// a challenge answer awaits that
	int					serverId;					// server identification
	int					serverChallenge;			// challenge from server
	int					serverMessageSequence;		// sequence number of last server message
	
	netadr_t			lastRconAddress;			// last rcon address we emitted to
	int					lastRconTime;				// when last rcon emitted
	rcon2ClientRequest_t	rcon2Request;
	// The Admin page's password check ("rcon verifyRconPass"): the game hears
	// whether the server accepted the password, as it did in Quake 4.
	bool				rconVerifyPending;			// a check awaits its answer
	bool				rconVerifyRefused;			// refused before sending; answered next frame
	netadr_t			rconVerifyAddress;			// the server asked
	int					rconVerifyTime;				// when the check was asked

	idMsgChannel		channel;					// message channel to server
	int					lastConnectTime;			// last time a connect message was sent
	int					lastEmptyTime;				// last time an empty message was sent
	int					lastPacketTime;				// last time a packet was received from the server
	int					resumeProbeTime;			// clientTime a resume began awaiting its first packet, or -1
	int					resumeReconnectTime;		// Sys_Milliseconds of a resume's reconnect; survives Clear()
	int					lastSnapshotTime;			// last time a snapshot was received

	int					snapshotSequence;			// sequence number of the last received snapshot
	int					snapshotGameFrame;			// game frame number of the last received snapshot
	int					snapshotGameTime;			// game time of the last received snapshot

	int					gameInitId;					// game initialization identification
	int					gameFrame;					// local game frame
	int					gameTime;					// local game time
	int					gameTimeResidual;			// left over time from previous frame

	usercmd_t			userCmds[MAX_USERCMD_BACKUP][MAX_ASYNC_CLIENTS];

	idUserInterface *	guiNetMenu;

	clientUpdateState_t updateState;
	int					updateSentTime;
	idStr				updateMSG;
	bool				showUpdateMessage;

	backgroundDownload_t	backgroundDownload;

	int					lastFrameDelta;

	int					dlRequest;		// randomized number to keep track of the requests
	int					dlChecksums[ MAX_PURE_PAKS ]; // 0-terminated, first element is the game pak checksum or 0
	int					dlCount;		// download-info slots expected: game pak slot plus asset paks
	idList<pakDlEntry_t>dlList;			// list of paks to download, with url and name
	int					currentDlSize;
	int					totalDlSize;	// for partial progress stuff

	void				Clear( void );
	void				ClearPendingPackets( void );
	void				DuplicateUsercmds( int frame, int time );
	void				SendUserInfoToServer( void );
	void				SendEmptyToServer( bool force = false, bool mapLoad = false );
	void				SendPingResponseToServer( int time );
	void				SendUsercmdsToServer( void );
	void				InitGame( int serverGameInitId, int serverGameFrame, int serverGameTime, const idDict &serverSI );
	void				ProcessUnreliableServerMessage( const idBitMsg &msg );
	void				ProcessReliableServerMessages( void );
	void				ProcessChallengeResponseMessage( const netadr_t from, const idBitMsg &msg );
	void				ProcessConnectResponseMessage( const netadr_t from, const idBitMsg &msg );
	void				ProcessDisconnectMessage( const netadr_t from, const idBitMsg &msg );
	void				ProcessInfoResponseMessage( const netadr_t from, const idBitMsg &msg );
	bool				CheckConnectServerProtocol( const netadr_t from, int challenge, int protocol );
	void				ProcessPrintMessage( const netadr_t from, const idBitMsg &msg );
	void				ProcessRemoteConsole2ChallengeResponse( const netadr_t from, const idBitMsg &msg );
	void				ProcessRemoteConsole2Complete( const netadr_t from, const idBitMsg &msg );
	void				ProcessServersListMessage( const netadr_t from, const idBitMsg &msg );
	void				ProcessServersListExtMessage( const netadr_t from, const idBitMsg &msg );
	void				ProcessAuthKeyMessage( const netadr_t from, const idBitMsg &msg );
	void				ProcessVersionMessage( const netadr_t from, const idBitMsg &msg );
	void				ConnectionlessMessage( const netadr_t from, const idBitMsg &msg );
	void				ProcessMessage( const netadr_t from, idBitMsg &msg );
	void				SetupConnection( void );
	void				ProcessPureMessage( const netadr_t from, const idBitMsg &msg );
	bool				ValidatePureServerChecksums( const netadr_t from, const idBitMsg &msg );
	void				ProcessReliableMessagePure( const idBitMsg &msg );
	static const char*	HandleGuiCommand( const char *cmd );
	const char*			HandleGuiCommandInternal( const char *cmd );
	void				HandleDownloads( void );
	void				Idle( void );
	int					UpdateTime( int clamp );
	void				ReadLocalizedServerString( const idBitMsg &msg, char* out, int maxLen );
	bool				CheckTimeout( void );
	bool				CheckResume( void );
	void				ResumeReconnect( const char *reason );
	bool				StaleDisconnectAfterResume( void ) const;
	void				ProcessDownloadInfoMessage( const netadr_t from, const idBitMsg &msg );
	int					GetDownloadRequest( const int checksums[ MAX_PURE_PAKS ], int count, int gamePakChecksum );
	void				ClearRemoteConsoleRequest( void );
	void				SendRemoteConsole2Challenge( void );
	void				SendRemoteConsole2Proof( void );
	void				UpdateRemoteConsoleRequest( void );
	void				AnswerRconVerify( bool success );
};

#endif /* !__ASYNCCLIENT_H__ */
