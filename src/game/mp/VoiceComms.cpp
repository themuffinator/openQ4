#include "../../idlib/precompiled.h"
#pragma hdrstop
#include "../Game_local.h"
#include "../../sound/VoicePacket.h"

static const int MAX_VOICE_PACKET_SIZE = oq4voice::PacketBytes;
idCVar si_voiceChat( "si_voiceChat", "1", CVAR_GAME | CVAR_BOOL | PC_CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_INFO, "enables or disables voice chat through the server" );
idCVar si_voiceProtocol( "si_voiceProtocol", "1", CVAR_GAME | CVAR_INTEGER | CVAR_ROM | CVAR_SERVERINFO, "bounded openQ4 voice wire version" );
idCVar ui_voiceProtocol( "ui_voiceProtocol", "1", CVAR_GAME | CVAR_INTEGER | CVAR_ROM | CVAR_USERINFO, "supported openQ4 voice wire version" );
idCVar g_voiceChatDebug( "g_voiceChatDebug", "0", CVAR_GAME | CVAR_INTEGER | CVAR_NOCHEAT, "display info on voicechat net traffic" );

static oq4voice::Admission voiceAdmission[MAX_CLIENTS];
static oq4voice::AdmissionStats voiceStats[MAX_CLIENTS];
static unsigned int voiceForwarded[MAX_CLIENTS], voiceRefused[MAX_CLIENTS];
static int voiceGeneration[MAX_CLIENTS];
static bool voiceSilenced[MAX_CLIENTS];

static bool ManagedVoiceTransportAllows( idPlayer *from, idPlayer *to, bool echo ) {
	return from != NULL && to != NULL && !to->IsPlayerMuted( from ) && ( from != to || echo );
}

void idMultiplayerGame::ResetVoiceState( int client ) {
	if ( client < 0 ) {
		for ( int i = 0; i < MAX_CLIENTS; ++i ) {
			voiceAdmission[i] = {}; voiceStats[i] = {}; voiceForwarded[i] = voiceRefused[i] = 0;
			voiceGeneration[i] = -1; voiceSilenced[i] = false;
		}
		if ( soundSystem ) soundSystem->ResetVoiceChat();
		return;
	}
	if ( client >= MAX_CLIENTS ) return;
	voiceAdmission[client] = {};
	voiceStats[client] = {};
	voiceForwarded[client] = voiceRefused[client] = 0;
	voiceGeneration[client] = -1;
	voiceSilenced[client] = false;
	if ( soundSystem ) soundSystem->StopVoiceData( client );
	// Mutes belong to this connection, never to the next occupant of its slot.
	for ( int i = 0; i < gameLocal.numClients; ++i ) {
		idEntity* entity = gameLocal.entities[i];
		if ( entity && entity->IsType( idPlayer::GetClassType() ) ) static_cast<idPlayer*>( entity )->MutePlayer( client, false );
	}
}

void idMultiplayerGame::ServerVoiceMute_f( const idCmdArgs& args ) {
	if ( !gameLocal.isServer || args.Argc() != 3 ) { common->Printf( "Usage (server): serverVoiceMute <client slot> <0|1>\n" ); return; }
	if ( !args.Argv( 1 )[0] || strlen( args.Argv( 1 ) ) > 2 ) return;
	for ( const char* digit = args.Argv( 1 ); *digit; ++digit ) if ( *digit < '0' || *digit > '9' ) return;
	const int client = atoi( args.Argv( 1 ) );
	if ( client < 0 || client >= gameLocal.numClients || ( idStr::Cmp( args.Argv( 2 ), "0" ) && idStr::Cmp( args.Argv( 2 ), "1" ) ) ) return;
	voiceSilenced[client] = args.Argv( 2 )[0] == '1';
	common->Printf( "VOICE_MODERATION client=%d muted=%d\n", client, voiceSilenced[client] );
}

void idMultiplayerGame::ServerVoiceStats_f( const idCmdArgs& ) {
	if ( !gameLocal.isServer ) { common->Printf( "Usage (server): serverVoiceStats\n" ); return; }
	// Per sender slot and connection generation; the interval maximum covers
	// the time since the previous report.
	for ( int client = 0; client < gameLocal.numClients && client < MAX_CLIENTS; ++client ) {
		oq4voice::AdmissionStats& stats = voiceStats[client];
		if ( !stats.accepted && !stats.rejected ) continue;
		common->Printf( "VOICE_SERVER client=%d generation=%d accepted=%u rejected=%u forwarded=%u refused=%u added_ms=%u max_added_ms=%u interval_max_added_ms=%u baseline_shift_ms=%d\n",
			client, voiceGeneration[client], stats.accepted, stats.rejected, voiceForwarded[client], voiceRefused[client],
			stats.lastAdded, stats.maxAdded, stats.intervalMaxAdded, int( stats.BaselineShift( voiceAdmission[client] ) ) );
		stats.intervalMaxAdded = 0;
	}
}

void idGameLocal::ServerReceiveVoice( int client, const idBitMsg& msg ) {
	if ( !isMultiplayer || !isServer || msg.GetRemainingData() != MAX_VOICE_PACKET_SIZE + 1 ) return;
	const int flags = msg.ReadByte();
	if ( flags < 0 || flags > 1 ) return;
	mpGame.ReceiveAndForwardVoiceData( client, msg, flags );
}

void idGameLocal::ClientReceiveVoice( const idBitMsg& msg ) {
	if ( isMultiplayer && !IsServerDemoPlaying() && !isRepeater ) mpGame.ReceiveAndPlayVoiceData( msg );
}

void idMultiplayerGame::ReceiveAndForwardVoiceData( int clientNum, const idBitMsg &inMsg, int messageType ) {
	if ( clientNum < 0 || clientNum >= gameLocal.numClients || clientNum >= MAX_CLIENTS ||
		!gameLocal.serverInfo.GetBool( "si_voiceChat" ) || voiceSilenced[clientNum] ) return;
	idEntity* entity = gameLocal.entities[clientNum];
	if ( !entity || !entity->IsType( idPlayer::GetClassType() ) ) return;
	idPlayer* from = static_cast<idPlayer*>( entity );
	if ( !from->GetUserInfo() || !from->GetUserInfo()->GetBool( "s_voiceChatSend" ) ||
		from->GetUserInfo()->GetInt( "ui_voiceProtocol" ) != oq4voice::Version ) return;

	const bool managedTeamCommunicationRequested = gameLocal.isServer && matchRules.Committed().GetBool( MP_RULE_MANAGED_MATCH );
	bool managedTeamCommunication = false;
	mpMatchTeamCommunicationBinding_t managedSender;
	if ( managedTeamCommunicationRequested ) {
		if ( !IsManagedTeamCommunicationActive() ||
			!BuildManagedTeamCommunicationBinding( clientNum, managedSender ) ||
			!MPMatchMayReceiveManagedTeamVoice( matchSession, managedSender, managedSender ) ) return;
		managedTeamCommunication = true;
	}
	const int payloadBytes = inMsg.GetRemainingData();
	if ( payloadBytes <= 0 || payloadBytes > MAX_VOICE_PACKET_SIZE ) return;
	if ( voiceGeneration[clientNum] != gameLocal.spawnIds[clientNum] ) {
		voiceAdmission[clientNum] = {};
		voiceStats[clientNum] = {};
		voiceForwarded[clientNum] = voiceRefused[clientNum] = 0;
		voiceGeneration[clientNum] = gameLocal.spawnIds[clientNum];
	}
	const uint32_t now = uint32_t( Sys_Milliseconds() );
	const bool admitted = voiceAdmission[clientNum].Accept( inMsg.GetReadData(), payloadBytes, now );
	voiceStats[clientNum].Record( voiceAdmission[clientNum], inMsg.GetReadData(), payloadBytes, now, admitted );
	if ( !admitted ) {
		if ( g_voiceChatDebug.GetInteger() & 2 ) common->Printf( "VOICE_REJECT client=%d bytes=%d\n", clientNum, payloadBytes );
		return;
	}
	byte buffer[MAX_VOICE_PACKET_SIZE + 5];
	idBitMsg outMsg;
	outMsg.Init( buffer, sizeof( buffer ) );
	outMsg.WriteByte( clientNum );
	outMsg.WriteLong( gameLocal.spawnIds[clientNum] );
	outMsg.WriteData( inMsg.GetReadData(), payloadBytes );

	// Exact recipients, including the listen host. The generic snapshot event
	// helper would also fan out to following spectators and demo/repeater feeds.
	for ( int i = 0; i < gameLocal.numClients; ++i ) {
		idEntity* toEntity = gameLocal.entities[i];
		if ( !toEntity || !toEntity->IsType( idPlayer::GetClassType() ) || botManager.IsBot( i ) ) continue;
		idPlayer* to = static_cast<idPlayer*>( toEntity );
		if ( !to->GetUserInfo() || !to->GetUserInfo()->GetBool( "s_voiceChatReceive" ) ||
			to->GetUserInfo()->GetInt( "ui_voiceProtocol" ) != oq4voice::Version ) continue;
		bool mayReceive;
		if ( managedTeamCommunication ) {
			mpMatchTeamCommunicationBinding_t managedRecipient;
			mayReceive = BuildManagedTeamCommunicationBinding( i, managedRecipient ) &&
				MPMatchMayReceiveManagedTeamVoice( matchSession, managedSender, managedRecipient ) &&
				ManagedVoiceTransportAllows( from, to, !!( messageType & 1 ) );
		} else mayReceive = CanTalk( from, to, !!( messageType & 1 ) );
		if ( !mayReceive || !to->AllowedVoiceDest( from->entityNumber ) ) continue;
		const bool accepted = networkSystem->ServerSendVoice( i, outMsg );
		++( accepted ? voiceForwarded[clientNum] : voiceRefused[clientNum] );
		if ( g_voiceChatDebug.GetInteger() & 2 ) common->Printf( "VOICE_FORWARD from=%d to=%d accepted=%d bytes=%d\n", clientNum, i, accepted, payloadBytes );
	}
}

bool idMultiplayerGame::CanTalk( idPlayer *from, idPlayer *to, bool echo ) {
	if ( !from || !to || from->spectating != to->spectating || to->IsPlayerMuted( from ) ||
		to->GetArena() != from->GetArena() || ( gameLocal.IsTeamGame() && from->team != to->team ) ) return false;
	return from != to || echo;
}

void idMultiplayerGame::XmitVoiceData() {
	idPlayer* player = gameLocal.GetLocalPlayer();
	const bool active = gameLocal.isMultiplayer && player && !gameLocal.IsServerDemoPlaying() &&
		!gameLocal.isRepeater && gameLocal.serverInfo.GetBool( "si_voiceChat" ) &&
		gameLocal.serverInfo.GetInt( "si_voiceProtocol" ) == oq4voice::Version;
	const bool send = soundSystem->UpdateVoiceChat( active, player && ( player->usercmd.buttons & BUTTON_VOICECHAT ),
		cvarSystem->GetCVarBool( "s_voiceChatTest" ) );
	if ( mainGui ) {
		static const char* statusKeys[] = { "#str_231306", "#str_231302", "#str_231303", "#str_231304", "#str_231305" };
		mainGui->SetStateFloat( "s_micLevel", soundSystem->GetVoiceLevel() );
		mainGui->SetStateInt( "voiceCaptureAllowed", cvarSystem->GetCVarBool( "s_voiceChatCapture" ) );
		mainGui->SetStateString( "voiceStatusText", common->GetLocalizedString( statusKeys[idMath::ClampInt( 0, 4, cvarSystem->GetCVarInteger( "s_voiceChatStatus" ) )] ) );
	}
	if ( !active ) return;
	// Flush locally on mute/receive changes, including already queued samples.
	const bool receive = cvarSystem->GetCVarBool( "s_voiceChatReceive" );	// once, not by name per client
	for ( int i = 0; i < gameLocal.numClients; ++i ) {
		if ( !gameLocal.entities[i] || player->IsPlayerMuted( i ) || !receive ) soundSystem->StopVoiceData( i );
	}
	if ( send && cvarSystem->GetCVarBool( "s_voiceChatSend" ) ) {
		byte packet[MAX_VOICE_PACKET_SIZE];
		byte buffer[MAX_VOICE_PACKET_SIZE + 1];
		for ( int i = 0; i < 3; ++i ) {
			const int count = soundSystem->GetVoiceData( packet, sizeof( packet ) );
			if ( !count ) break;
			idBitMsg outMsg;
			outMsg.Init( buffer, sizeof( buffer ) );
			outMsg.WriteByte( cvarSystem->GetCVarBool( "s_voiceChatEcho" ) ? 1 : 0 );
			outMsg.WriteData( packet, count );
			const bool sent = networkSystem->ClientSendVoice( outMsg );
			if ( g_voiceChatDebug.GetInteger() & 1 ) common->Printf( "VOICE_SEND bytes=%d sent=%d\n", count, sent );
		}
	}
	if ( player->mphud ) {
		const int transmitSelf = send ? 1 : 0;
		if ( hudTransmitSelf != transmitSelf ) {
			player->mphud->HandleNamedEvent( transmitSelf ? "show_transmit_self" : "hide_transmit_self" );
			hudTransmitSelf = transmitSelf;
		}
		const int talker = soundSystem->GetVoiceTalker();
		if ( talker != hudTransmitOther ) {
			if ( talker >= 0 && talker < gameLocal.numClients && gameLocal.entities[talker] ) {
				idPlayer* from = static_cast<idPlayer*>( gameLocal.entities[talker] );
				player->mphud->SetStateString( "audio_name", from->GetUserInfo()->GetString( "ui_name" ) );
				player->mphud->SetStateInt( "oq4_audio_team", gameLocal.IsTeamGame() ? from->team : -1 );
				player->mphud->HandleNamedEvent( "show_transmit" );
			} else player->mphud->HandleNamedEvent( "hide_transmit" );
			hudTransmitOther = talker;
		}
	}
}

void idMultiplayerGame::ReceiveAndPlayVoiceData( const idBitMsg &inMsg ) {
	if ( !gameLocal.serverInfo.GetBool( "si_voiceChat" ) || inMsg.GetRemainingData() != MAX_VOICE_PACKET_SIZE + 5 ) return;
	const int client = inMsg.ReadByte();
	const int generation = inMsg.ReadLong();
	idPlayer* local = gameLocal.GetLocalPlayer();
	if ( !local || client < 0 || client >= gameLocal.numClients || client >= MAX_CLIENTS ||
		!gameLocal.entities[client] || gameLocal.spawnIds[client] != generation || local->IsPlayerMuted( client ) ) return;
	soundSystem->PlayVoiceData( client, generation, inMsg.GetReadData(), inMsg.GetRemainingData() );
}
