//----------------------------------------------------------------
// GameState.cpp
//
// Copyright 2002-2005 Raven Software
//----------------------------------------------------------------

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "GameState.h"

static const int ARENA_CAMPAIGN_ENTRANCE_MIN_MSEC = 3000;

static void ShiftGameStateDeadline( int &deadline, int deltaMsec ) {
	if ( deadline <= 0 || deltaMsec <= 0 ) {
		return;
	}
	const int maxInt = 0x7fffffff;
	deadline = deadline > maxInt - deltaMsec ? maxInt : deadline + deltaMsec;
}

/*
===============================================================================

rvGameState

Game state info for deathmatch, team deathmatch

===============================================================================
*/

/*
================
rvGameState::rvGameState
================
*/
rvGameState::rvGameState( bool allocPrevious ) {
	Clear();

	if( allocPrevious ) {
		previousGameState = new rvGameState( false );
	} else {
		previousGameState = NULL;
	}
	
	trackPrevious = allocPrevious;
}

/*
================
rvGameState::~rvGameState
================
*/
rvGameState::~rvGameState( void ) {
	Clear();
	delete previousGameState;
	previousGameState = NULL;
}

/*
================
rvGameState::Clear
================
*/
void rvGameState::Clear( void ) {
	currentState = INACTIVE;
	nextState = INACTIVE;
	nextStateTime = 0;
	fragLimitTimeout = 0;
	overtimeCount = 0;
	overtimeAccumulatedMsec = 0;
	overtimeStartTime = 0;
}

void rvGameState::ShiftMatchTime( int deltaMsec ) {
	ShiftGameStateDeadline( nextStateTime, deltaMsec );
	ShiftGameStateDeadline( fragLimitTimeout, deltaMsec );
	ShiftGameStateDeadline( overtimeStartTime, deltaMsec );
}

/*
================
rvGameState::StartOvertime

Grants another overtime period by extending the match clock, the way Quake
Live does.  Returns false when the server has overtime turned off, which is
the caller's cue to fall back to Quake 4 sudden death.
================
*/
bool rvGameState::StartOvertime( void ) {
	int overtimeSeconds;

	if ( gameLocal.isClient ) {
		return false;
	}

	// an untimed match has nothing to extend; sudden death is the only way out
	if ( gameLocal.serverInfo.GetInt( "si_timeLimit" ) <= 0 ) {
		return false;
	}

	overtimeSeconds = gameLocal.serverInfo.GetInt( "si_overtime" );
	if ( overtimeSeconds <= 0 ) {
		return false;
	}
	if ( !gameLocal.mpGame.BeginMatchOvertimePeriod() ) {
		return false;
	}

	overtimeCount++;
	overtimeAccumulatedMsec += overtimeSeconds * 1000;
	overtimeStartTime = gameLocal.time;
	gameLocal.Printf( "overtime: period %d extended match by %d seconds\n",
		overtimeCount, overtimeSeconds );

	// the frag limit timeout must not carry into the extension, or the match
	// ends on the stale deadline the instant overtime begins
	fragLimitTimeout = 0;

	if ( overtimeCount > 1 ) {
		gameLocal.mpGame.CenterPrint( -1, "#str_41311", idMultiplayerGame::CPARM_INT, overtimeCount );
	} else {
		gameLocal.mpGame.CenterPrint( -1, "#str_41310" );
	}

	gameLocal.mpGame.ScheduleTimeAnnouncements();

	return true;
}

/*
================
rvGameState::StopOvertime
================
*/
void rvGameState::StopOvertime( void ) {
	overtimeCount = 0;
	overtimeAccumulatedMsec = 0;
	overtimeStartTime = 0;
}

/*
================
rvGameState::SendState
================
*/
void rvGameState::SendState( const idMessageSender &sender, int clientNum ) {
	idBitMsg	outMsg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];

	assert( (gameLocal.isServer || gameLocal.isRepeater) && trackPrevious );

	if( clientNum == -1 && (*this) == (*previousGameState) ) {
		return;
	}

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_GAMESTATE );

	WriteState( outMsg );

	sender.Send( outMsg );
	
	// don't update the state if we are working for a single client
	if ( clientNum == -1 ) {
		outMsg.ReadByte(); // pop off the msg ID
		ReceiveState( outMsg );
	}
}

/*
===============
rvGameState::WriteState
===============
*/
void rvGameState::WriteState( idBitMsg &msg ) {
	PackState( msg );
}

/*
================
rvGameState::SendInitialState
================
*/
void rvGameState::SendInitialState( const idMessageSender &sender, int clientNum ) {
	rvGameState* previousState = previousGameState;

	rvGameState invalidState;

	previousGameState = &invalidState;

	SendState( sender, clientNum );

	previousGameState = previousState;
}

/*
================
rvGameState::ReceiveState
================
*/
void rvGameState::ReceiveState( const idBitMsg& msg ) {
	if ( !BaseUnpackState( msg ) ) {
		return;
	}

	if ( gameLocal.localClientNum >= 0 ) {
		GameStateChanged();
	}

	(*previousGameState) = (*this);
}

/*
================
rvGameState::PackState
================
*/
void rvGameState::PackState( idBitMsg& outMsg ) {
	// server and client changing their rvGameState subclass by guesswork
	// you can't rely on it being in sync at all times, so read and verify the type first
	outMsg.WriteByte( gameLocal.gameType );

	// for now, we only transmit 3 bytes.  If we need to sync more data, we should
	// only transmit the diff
	outMsg.WriteByte( currentState );
	outMsg.WriteByte( nextState );
	outMsg.WriteLong( nextStateTime );
	// openQ4: overtime rides the base header because it applies to every
	// gametype, and the HUD clock cannot be right without it
	outMsg.WriteByte( overtimeCount );
	outMsg.WriteLong( overtimeAccumulatedMsec );
	outMsg.WriteLong( overtimeStartTime );
}

/*
================
rvGameState::BaseUnpackState
================
*/
bool rvGameState::BaseUnpackState( const idBitMsg& inMsg ) {
	gameType_t t = (gameType_t)inMsg.ReadByte();
	if ( t != gameLocal.gameType ) {
		common->Warning( "rvGameState::UnpackState: client gametype (%d) is out of sync with server (%d). Ignoring", gameLocal.gameType, t );
		return false;
	}

	currentState = (mpGameState_t)inMsg.ReadByte();
	nextState = (mpGameState_t)inMsg.ReadByte();
	nextStateTime = inMsg.ReadLong();
	overtimeCount = inMsg.ReadByte();
	overtimeAccumulatedMsec = inMsg.ReadLong();
	overtimeStartTime = inMsg.ReadLong();
	return true;
}

/*
================
rvGameState::GameStateChanged
================
*/
void rvGameState::GameStateChanged( void ) {
	idPlayer* player = gameLocal.GetLocalPlayer();
	if ( !player ) {
		if ( gameLocal.IsServerDemoPlaying() && gameLocal.GetDemoFollowClient() >= 0 ) {
			player = static_cast< idPlayer * >( gameLocal.entities[ gameLocal.GetDemoFollowClient() ] );
		}
	}
	if ( !player ) {
		gameLocal.Warning( "rvGameState::GameStateChanged() - NULL local player\n" ) ;
		return;
	}

	// Check for a currentState change
	if( currentState != previousGameState->currentState ) {
		const bool arenaCampaign = gameLocal.mpGame.IsArenaCampaignMatch();
		if( currentState == WARMUP ) {
			// openQ4: the Arena presentation is reset on the authoritative WARMUP
			// edge in NewState, before Run arms the introduction. This hook runs
			// after Run in the same frame (SendState -> ReceiveState), so clearing
			// here wiped the introduction it had just armed.
			if ( arenaCampaign ) {
				player->GUIMainNotice( "" );
			} else if( gameLocal.gameType != GAME_TOURNEY ) {
				player->GUIMainNotice( common->GetLocalizedString( "#str_107706" ), true );		
			}
			soundSystem->SetActiveSoundWorld( true );
			
			// reset stats on the client-side
			if( gameLocal.isClient ) {
				statManager->Init();
			}
		} else if( currentState == COUNTDOWN ) {
			if ( arenaCampaign ) {
				player->GUIMainNotice( "" );
			} else if( gameLocal.gameType != GAME_TOURNEY ) {
				player->GUIMainNotice( common->GetLocalizedString( "#str_107706" ), true );		
			}
			soundSystem->SetActiveSoundWorld(true);
			if( gameLocal.gameType != GAME_TOURNEY && previousGameState->currentState != INACTIVE ) {
				gameLocal.mpGame.RemoveAnnouncerSound( AS_GENERAL_PREPARE_TO_FIGHT );
				gameLocal.mpGame.RemoveAnnouncerSound( AS_GENERAL_THREE );
				gameLocal.mpGame.RemoveAnnouncerSound( AS_GENERAL_TWO );
				gameLocal.mpGame.RemoveAnnouncerSound( AS_GENERAL_ONE );

				// openQ4: an Arena match counts down over its spawn-in shot, so its
				// "Fight" lands as control returns (OnMatchStarted). Its entrance
				// only calls "prepare to fight", and a round mode, which counts its
				// first round down from GAMEON itself, gets neither here.
				if ( !arenaCampaign ) {
					gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_PREPARE_TO_FIGHT, gameLocal.time );
					gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_THREE, nextStateTime - 3000 );
					gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_TWO, nextStateTime - 2000 );
					gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_ONE, nextStateTime - 1000 );
				} else if ( !gameLocal.IsRoundGameType() ) {
					gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_PREPARE_TO_FIGHT, gameLocal.time );
				}
			}
			if ( arenaCampaign ) {
				gameLocal.mpGame.BeginArenaCampaignEntrancePresentation();
			}
		} else if( currentState == GAMEON ) {
			// openQ4: OnMatchStarted, inside NewState( GAMEON ), has already
			// retired the entrance and armed the Arena spawn-in shot. Clearing the
			// presentation again on this later edge cancelled the shot outright:
			// no camera move, no held control and no "Fight".
			if ( !player->vsMsgState ) {
				player->GUIMainNotice( "" );
				player->GUIFragNotice( "" );
			} else {
				player->vsMsgState = false;
			}
			// openQ4: a round mode is not fighting yet at GAMEON - the first round
			// countdown has not even been scheduled.  Its own "FIGHT" comes from
			// rvRoundGameState::GameStateChanged on the RS_ACTIVE edge, so calling
			// it here shouts go while weapons are still locked, and again a few
			// seconds later when the round really starts.
			// openQ4: the Arena campaign holds control for its match-start camera
			// move, so shouting go on this edge lands while the player is still
			// frozen watching it. AdvanceArenaCampaignCeremony calls it when the
			// shot lands and control actually returns.
			if( gameLocal.gameType != GAME_TOURNEY && !gameLocal.IsRoundGameType() &&
				!gameLocal.mpGame.IsArenaCampaignMatch() ) {
				gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_FIGHT, gameLocal.time );
			}
			//if ( gameLocal.gameType == GAME_DEADZONE ) {
			//	if ( player->team == TEAM_MARINE )
			//		gameLocal.mpGame.ScheduleAnnouncerSound( AS_TEAM_JOIN_MARINE, gameLocal.time );
			//	else
			//		gameLocal.mpGame.ScheduleAnnouncerSound( AS_TEAM_JOIN_STROGG, gameLocal.time );
			//}
			cvarSystem->SetCVarString( "ui_ready", "Not Ready" );
			soundSystem->SetActiveSoundWorld( true );

			// clear stats on client
			if( gameLocal.isClient ) {
				statManager->Init();
			}
 		} else if( currentState == SUDDENDEATH ) {
			soundSystem->SetActiveSoundWorld( true );
			gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_SUDDEN_DEATH, gameLocal.time );
			gameLocal.GetLocalPlayer()->GUIMainNotice( common->GetLocalizedString( "#str_104287" ) );
		} else if( currentState == GAMEREVIEW ) {
			// RITUAL BEGIN
			gameLocal.mpGame.isBuyingAllowedRightNow = false;
			// RITUAL END
			gameLocal.mpGame.ShowStatSummary();
		}

		gameLocal.mpGame.ScheduleTimeAnnouncements( );
	}
}

/*
================
rvGameState::SpawnDeadZonePowerup
================
*/
void rvGameState::SpawnDeadZonePowerup( void ) {
	idEntity *ent;
	riDeadZonePowerup* spawnSpot = 0;
	int count = 0;
	for ( ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {

		// If its not a DeadZone powerup then skip it
		if ( !ent->IsType( riDeadZonePowerup::GetClassType() ) ) {
			continue;			
		}
		
		// Make sure its the right type first
		riDeadZonePowerup* flag;
		flag = static_cast<riDeadZonePowerup*>(ent);
		if ( flag->powerup != POWERUP_DEADZONE || flag->IsVisible() ) {
			continue;
		}
		
		if ( flag->spawnArgs.GetBool("dropped", "0") && !flag->IsVisible() ) {			
			flag->PostEventMS( &EV_Remove, 0 );

		} else {
			count++;
			if ( !(rand()%(count)) ) {
				spawnSpot = flag;
			}
		}
	}
	if ( spawnSpot ) {
		spawnSpot->PostEventMS( &EV_RespawnItem, 0 );
		spawnSpot->srvReady = 1; // Go ahead and set this, so the loop works properly.
	} else {
		// openQ4: this used to be a gameLocal.Error, which dropped the whole
		// dedicated server at the instant the match went live just because the map
		// shipped fewer hidden artifact spawn spots than its decl asks for.  A data
		// shortfall degrades to fewer artifacts instead.
		gameLocal.Warning( "Couldn't find enough dead zone spawn spots for the number of dead zone artifacts specified in the map def!" );
	}
}

/*
================
rvGameState::CountDeadZonePowerupSpawns

How many hidden artifact spawn spots SpawnDeadZonePowerup could still consume.
Mirrors that function's eligibility test exactly - anything it would skip must
not be counted here, or the caller asks for one more artifact than exists.
================
*/
int rvGameState::CountDeadZonePowerupSpawns( void ) {
	idEntity *ent;
	int count = 0;

	for ( ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		if ( !ent->IsType( riDeadZonePowerup::GetClassType() ) ) {
			continue;
		}

		riDeadZonePowerup* flag;
		flag = static_cast<riDeadZonePowerup*>(ent);
		if ( flag->powerup != POWERUP_DEADZONE || flag->IsVisible() ) {
			continue;
		}

		// a dropped artifact is removed rather than reused
		if ( flag->spawnArgs.GetBool("dropped", "0") ) {
			continue;
		}

		count++;
	}

	return count;
}

/*
================
rvGameState::SpawnDeadZonePowerups

Puts out the map's full complement of DeadZone artifacts.  Both the match start
and the sudden death restart used to carry their own copy of this block.
================
*/
void rvGameState::SpawnDeadZonePowerups( void ) {
	const char *mapName = gameLocal.serverInfo.GetString( "si_map" );
	const idDict *mapDict = MultiplayerResolveMapDecl( mapName );
	if ( mapDict ) {
		gameLocal.mpGame.deadZonePowerupCount = mapDict->GetInt("deadZonePowerupCount", "3");
	} else {
		gameLocal.mpGame.deadZonePowerupCount = 3;
	}

	int pcount = gameLocal.mpGame.deadZonePowerupCount;
	if ( pcount == -1 ) {
		pcount = 3; // Good default.
	}

	pcount = idMath::ClampInt(1, 12, pcount);

	// openQ4: the decl says how many artifacts the map wants, not how many spawn
	// spots it actually ships.  Ask for more than exist and the last call finds
	// nothing left to consume; take what the map has instead.
	int available = CountDeadZonePowerupSpawns();
	if ( available < pcount ) {
		gameLocal.Warning( "map '%s' asks for %d dead zone artifacts but only has %d spawn spots left", mapName, pcount, available );
		pcount = available;
	}

	for ( int i = 0; i < pcount; i++ ) {
		SpawnDeadZonePowerup();
	}
}

/*
================
rvGameState::Run
================
*/
void rvGameState::Run( void ) {
	if ( currentState == INACTIVE ) {

#ifdef _XENON
		if(Live()->RoundsPlayed() < cvarSystem->GetCVarInteger("si_matchRounds"))
#endif
		{
			NewState( WARMUP );
		}
	}

	if ( nextState != INACTIVE && gameLocal.time > nextStateTime ) {
		if ( NewState( nextState ) ) {
			nextState = INACTIVE;
		}
	}

	switch( currentState ) {
		case INACTIVE:
			break;

		case GAMEREVIEW: {
			if ( nextState == INACTIVE ) {

				statManager->SendAllStats();

				nextState = NEXTGAME;

				// allow a little extra time in tourney since we have to display end brackets
				if( gameLocal.gameType == GAME_TOURNEY ) {
					nextStateTime = gameLocal.time + 5000 + (1000 * cvarSystem->GetCVarInteger( "g_gameReviewPause" ));
				} else {
					nextStateTime = gameLocal.time + (1000 * cvarSystem->GetCVarInteger( "g_gameReviewPause" ));
				}
			}
			break;
		}
		case NEXTGAME: {

			// the core will force a restart at 12 hours max
			// but it's nicer if we can wait for a game transition to perform the restart so the game is not interrupted
			// perform a restart once we are past 8 hours
			if ( networkSystem->ServerGetServerTime() > 8*60*60*1000 ) {
				gameLocal.sessionCommand = "nextMap";
				return;
			}

			if ( nextState == INACTIVE ) {
				// game rotation, new map, gametype etc.
				// only cycle in tourney if tourneylimit is higher than the specified value
				if( gameLocal.gameType != GAME_TOURNEY || ((rvTourneyGameState*)this)->GetTourneyCount() >= gameLocal.serverInfo.GetInt( "si_tourneyLimit" ) ) {
					// whether we switch to the next map or not, reset the tourney count
					if( gameLocal.gameType == GAME_TOURNEY ) {
						((rvTourneyGameState*)this)->SetTourneyCount( 0 );
					}


					if ( gameLocal.NextMap() ) {
						cmdSystem->BufferCommandText( CMD_EXEC_NOW, "serverMapRestart\n" );
						return;
					}
				}

				NewState( WARMUP );

				// put everyone back in from endgame spectate
				for ( int i = 0; i < gameLocal.numClients; i++ ) {
					idEntity *ent = gameLocal.entities[ i ];
					if ( ent && ent->IsType( idPlayer::GetClassType() ) ) {
						if ( !static_cast< idPlayer * >( ent )->wantSpectate ) {
							gameLocal.mpGame.CheckRespawns( static_cast<idPlayer *>( ent ) );
						}
					}
				}
			}
			break;
		}
		case WARMUP: {
			// check to see if we actually want to do a warmup, or if we fall through

//RAVEN BEGIN
//asalmon: Live has its own rules for ending warm up
#ifdef _XENON
			if(!Live()->RollCall())
			{
				break;
			}
			
#endif
//RAVEN END
			if ( gameLocal.mpGame.IsArenaCampaignMatch() ) {
				// The introduction owns warmup until every authored opponent has
				// been presented. Arena warmup has no other dwell: AllPlayersReady
				// is true from the first frame because the roster is populated
				// before any game frame runs.
				if ( gameLocal.mpGame.ArenaCampaignIntroBlocksCountdown() ) {
					break;
				}
				if ( gameLocal.mpGame.AllPlayersReady() && NewState( COUNTDOWN ) ) {
					nextState = GAMEON;
					nextStateTime = gameLocal.time + Max( ARENA_CAMPAIGN_ENTRANCE_MIN_MSEC,
						1000 * gameLocal.serverInfo.GetInt( "si_countDown" ) );
				}
			} else if( !gameLocal.serverInfo.GetBool( "si_warmup" ) && gameLocal.gameType != GAME_TOURNEY ) {
				// tourney always needs a warmup, to ensure that at least 2 players get seeded for the tournament.
				// Keep the legal lifecycle edge even when the presentation countdown
				// is disabled. The next frame consumes this zero-length countdown.
				// Empty or bot-only practice servers still need an active human;
				// wait quietly instead of rejecting an atomic start every frame.
				if ( gameLocal.mpGame.CanCommitMatchPhaseTransition( COUNTDOWN ) &&
						NewState( COUNTDOWN ) ) {
					nextState = GAMEON;
					nextStateTime = gameLocal.time;
				}
			} else if ( gameLocal.mpGame.CanCommitMatchPhaseTransition( COUNTDOWN ) &&
					NewState( COUNTDOWN ) ) {
				nextState = GAMEON;
				nextStateTime = gameLocal.time + 1000 * gameLocal.serverInfo.GetInt( "si_countDown" );
			}
			break;
		}
		case COUNTDOWN: {
			break;
		}
	}
}

/*
================
rvGameState::NewState
================
*/
bool rvGameState::NewState( mpGameState_t newState ) {
	idBitMsg	outMsg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];
	int			i;

	assert( (newState != currentState) && gameLocal.isServer );
	if ( !gameLocal.mpGame.CommitMatchPhaseTransition( newState ) ) {
		return false;
	}
	common->DPrintf( "MP match phase: %d -> %d at %d\n", currentState, newState, gameLocal.time );
	
	switch( newState ) {
		case WARMUP: {
			// A cancelled countdown or completed match must not lend its timers
			// to the next warmup, including restarts on the same map.
			nextState = INACTIVE;
			nextStateTime = 0;
			fragLimitTimeout = 0;
			//	asalmon: start the stat manager as soon as the game starts
			statManager->Init();
			statManager->BeginGame();

			// if shuffle is on, shuffle the teams around
			if( gameLocal.IsTeamGame() && gameLocal.serverInfo.GetBool( "si_shuffle" ) ) {
				gameLocal.mpGame.ShuffleTeams();
			}

			// allow damage in warmup
			//gameLocal.mpGame.EnableDamage( false );

			//asalmon: clear out lingering team scores.
			gameLocal.mpGame.ClearTeamScores();

			// openQ4: warmup is never in overtime
			StopOvertime();

			// openQ4: a fresh Arena warmup (or a countdown that fell back to it)
			// starts from an empty presentation. This is the authoritative edge:
			// Run arms the opponent introduction right after it, in this frame.
			if ( gameLocal.mpGame.IsArenaCampaignMatch() ) {
				gameLocal.mpGame.ClearArenaCampaignPresentation();
			}

			if( gameLocal.gameType != GAME_TOURNEY ) {
				for( i = 0; i < gameLocal.numClients; i++ ) {
					idEntity *ent = gameLocal.entities[ i ];
					if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
						continue;
					}
					((idPlayer*)ent)->JoinInstance( 0 );
				}
			}

			break;
		}
		case COUNTDOWN: {
			// Every accepted entry, including a typed referee/operator force-ready,
			// owns its start deadline. Run's automatic warmup path is only one
			// caller and cannot be the sole place that schedules live play.
			nextState = GAMEON;
			nextStateTime = gameLocal.time + 1000 * gameLocal.serverInfo.GetInt( "si_countDown" );
			break;
		}
		case GAMEON: {
			// allow damage in warmup
			fragLimitTimeout = 0;
			//gameLocal.mpGame.EnableDamage( true );
			gameLocal.LocalMapRestart();
// RITUAL BEGIN
// squirrel: Buying & Deadzone
			for( i = 0; i < gameLocal.numClients; i++ ) {
				idEntity *ent = gameLocal.entities[ i ];
				if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
					continue;
				}
				idPlayer* player = static_cast< idPlayer* >(ent);
				player->inventory.carryOverWeapons = 0;
				player->ResetCash();
				// If the buy menu is up during a server restart,
				// make sure to refresh it.
				gameLocal.mpGame.RedrawLocalBuyMenu();
			}

			if ( gameLocal.mpGame.IsBuyingAllowedInTheCurrentGameMode() )
			{
				gameLocal.mpGame.isBuyingAllowedRightNow = true;

				// Give all the clients full ammo since this is the start of the round.
				for( int i = 0; i < gameLocal.numClients; i++ ) {
					idPlayer* p = (idPlayer*)gameLocal.entities[ i ];
					if( p == NULL || !p->IsType( idPlayer::GetClassType() ) )
						continue;

					GiveStuffToPlayer(p, "ammo", "");
					p->inventory.weapons |= p->inventory.carryOverWeapons & CARRYOVER_WEAPONS_MASK;
				}
			}

			if ( gameLocal.gameType == GAME_DEADZONE ) {
				// Spawn the powerups!
				SpawnDeadZonePowerups();
			}
// RITUAL END

			// asalmon: reset the stats when the warmup period is over
			statManager->Init();
			statManager->BeginGame();

			outMsg.Init( msgBuf, sizeof( msgBuf ) );
			outMsg.WriteByte( GAME_RELIABLE_MESSAGE_RESTART );
			outMsg.WriteBits( 0, 1 );
			networkSystem->ServerSendReliableMessage( -1, outMsg );

			gameLocal.mpGame.SetMatchStartedTime( gameLocal.time );

			fragLimitTimeout = 0;
			// openQ4: a fresh match never inherits the previous one's overtime
			StopOvertime();

			// write server initial reliable messages to give everyone new base
			for( i = 0; i < MAX_CLIENTS; i++ ) {
				// dont send this to server - we have all the data already and this will
				// just trigger extra gamestate detection
				if ( gameLocal.entities[ i ] && i != gameLocal.localClientNum ) {
					gameLocal.mpGame.ServerWriteInitialReliableMessages( serverReliableSender.To( i, true ), i );
				}
			}
			
			if ( gameLocal.isRepeater ) {
				gameLocal.mpGame.ServerWriteInitialReliableMessages( repeaterReliableSender.To( -1 ), ENTITYNUM_NONE );
			}

			// openQ4: the match is live from here on, and the loop below respawns
			// everybody.  idPlayer::SpawnToPoint grants the si_warmupWeapons arsenal
			// whenever GetMPGameState() is WARMUP or COUNTDOWN, so leaving the commit
			// until the end of this function handed every weapon on the map straight
			// back to the player it had just been taken from - permanently, because
			// an MP respawn never clears inventory.weapons.  Publishing the state
			// here closes that gate before the respawn rather than after it.
			// (The assignment at the end of NewState then re-applies the same value.)
			currentState = newState;

			for( i = 0; i < gameLocal.numClients; i++ ) {
				idEntity *ent = gameLocal.entities[ i ];
				if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
					continue;
				}
				idPlayer *p = static_cast<idPlayer *>( ent );
				p->SetLeader( false ); // don't carry the flag from previous games
				// openQ4: warmup hands out the map's arsenal so the pre-match
				// period is practice. Take it back now the match is real -
				// nothing else does, because an MP respawn never clears
				// inventory.weapons, so it would otherwise last all match.
				p->RevokeWarmupArsenal();

				// in normal gameplay modes, spawn the player in.  For tourney, the tourney manager handles spawning
				if( gameLocal.gameType != GAME_TOURNEY ) {
					p->JoinInstance( 0 );
					// in team games, let UserInfoChanged() spawn people on the right teams
					if( !gameLocal.IsTeamGame() || p->team != -1 ) {
						p->ServerSpectate( static_cast<idPlayer *>(ent)->wantSpectate );
					}
				}
			}

			// openQ4: zero the scoreboard AFTER the respawn loop, not inside it.
			// The state is committed above so the arsenal gate closes before the
			// respawns, which also means scoring is no longer suppressed during
			// them - and every MP spawn runs idGameLocal::KillBox, so a player
			// telefragged by a later client's spawn would otherwise start the
			// live match on -1.  Resetting last makes the starting line 0 for
			// everyone regardless of what the spawn wave did.
			for( i = 0; i < gameLocal.numClients; i++ ) {
				idEntity *ent = gameLocal.entities[ i ];
				if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
					continue;
				}
				idPlayer *p = static_cast<idPlayer *>( ent );
				gameLocal.mpGame.SetPlayerScore( p, 0 );
				gameLocal.mpGame.SetPlayerTeamScore( p, 0 );
			}

			gameLocal.mpGame.ClearTeamScores();
			gameLocal.mpGame.OnMatchStarted();

			cvarSystem->SetCVarString( "ui_ready", "Not Ready" );
			gameLocal.mpGame.switchThrottle[ 1 ] = 0;	// passby the throttle
			break;
		}
		case GAMEREVIEW: {
			// Capture every authoritative end-of-match consumer before review
			// spectates the field.  This also covers disconnect/forfeit transitions
			// that happen outside idMultiplayerGame::Run.
			if ( currentState == GAMEON || currentState == SUDDENDEATH ) {
				gameLocal.mpGame.OnMatchEnded();
			}
			statManager->EndGame();

			//statManager->DebugPrint();
			nextState = INACTIVE;	// used to abort a game. cancel out any upcoming state change

			const bool keepArenaTableau = gameLocal.mpGame.IsArenaCampaignMatch();
			for( i = 0; i < gameLocal.numClients; i++ ) {
				idEntity *ent = gameLocal.entities[ i ];
				// RAVEN BEGIN
				// jnewquist: Use accessor for static class type 
				if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
					// RAVEN END
					continue;
				}
// RITUAL BEGIN
// squirrel: support for Buying in multiplayer
				idPlayer* player = static_cast< idPlayer* >(ent);
				player->inventory.carryOverWeapons = 0;
				player->ResetCash();
				player->forcedReady = false;
				if ( !keepArenaTableau ) {
					player->ServerSpectate( true );
				}
// RITUAL END
			}
			break;
		}
		case SUDDENDEATH: {
			gameLocal.mpGame.PrintMessageEvent( -1, MSG_SUDDENDEATH );
			//unmark all leaders, so we make sure we only let the proper people respawn
			for( i = 0; i < gameLocal.numClients; i++ ) {
				idEntity *ent = gameLocal.entities[ i ];
				if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
					continue;
				}
				idPlayer *p = static_cast<idPlayer *>( ent );
				p->SetLeader( false ); // don't carry the flag from previous games	
			}

			// only restart in team games if si_suddenDeathRestart is set.
			if( !gameLocal.IsTeamGame() || gameLocal.serverInfo.GetBool( "si_suddenDeathRestart" ) ) {
				gameLocal.LocalMapRestart();
			}

			// Mark everyone tied for the lead as leaders
			i = 0;
			idPlayer* leader = gameLocal.mpGame.GetRankedPlayer( i );
			if ( !gameLocal.IsTeamGame() && gameLocal.gameType != GAME_TOURNEY ) {
				gameLocal.mpGame.FragLeader();
			} else if( leader ) {
				int highScore = gameLocal.mpGame.GetScore( leader );
				while( leader ) {
					if( gameLocal.mpGame.GetScore( leader ) < highScore ) {
						break;
					}
					leader->SetLeader( true );
					leader = gameLocal.mpGame.GetRankedPlayer( ++i );
				}
			}

// RITUAL BEGIN
// squirrel: Buying & Deadzone
			/// Reset players' cash and inventory if si_suddenDeathRestart is set
			if( !gameLocal.IsTeamGame() || gameLocal.serverInfo.GetBool( "si_suddenDeathRestart" ) )
			{
				for( i = 0; i < gameLocal.numClients; i++ ) {
					idEntity *ent = gameLocal.entities[ i ];
					if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
						continue;
					}
					idPlayer* player = static_cast< idPlayer* >(ent);
					player->inventory.carryOverWeapons = 0;
					player->ResetCash();
				}
			}

			if ( gameLocal.mpGame.IsBuyingAllowedInTheCurrentGameMode() )
			{
				gameLocal.mpGame.isBuyingAllowedRightNow = true;

				// Give all the clients full ammo since this is the start of the round.
				for( int i = 0; i < gameLocal.numClients; i++ ) {
					idPlayer* p = (idPlayer*)gameLocal.entities[ i ];
					if( p == NULL || !p->IsType( idPlayer::GetClassType() ) )
						continue;

					GiveStuffToPlayer(p, "ammo", "");
					p->inventory.weapons |= p->inventory.carryOverWeapons & CARRYOVER_WEAPONS_MASK;
				}
			}

			if ( gameLocal.gameType == GAME_DEADZONE ) {
				// Spawn the powerups!
				SpawnDeadZonePowerups();
			}
// RITUAL END

			if ( gameLocal.gameType == GAME_DM ) {
				//send all the non-leaders to spectatormode?
				for( i = 0; i < gameLocal.numClients; i++ ) {
					idEntity *ent = gameLocal.entities[ i ];
					// RAVEN BEGIN
					// jnewquist: Use accessor for static class type 
					if ( !ent || !ent->IsType( idPlayer::GetClassType() ) ) {
						// RAVEN END
						continue;
					}
					// openQ4: the Arena campaign never benches a combatant. Sudden
					// death there would put the trailing human into spectator for
					// the rest of their own single-player match.
					if ( static_cast< idPlayer *>( ent )->IsLeader() ||
						 gameLocal.mpGame.IsArenaCampaignMatch() ) {
 						static_cast<idPlayer *>(ent)->ServerSpectate( false );
						continue;
					}
					static_cast< idPlayer *>( ent )->forcedReady = false;
					static_cast<idPlayer *>(ent)->ServerSpectate( true );
				}
			}
			
			break;
		}
		default: {
			break;
		}
	}

	currentState = newState;
	return true;
}

/*
================
rvGameState::ClientDisconnect
================
*/
void rvGameState::ClientDisconnect( idPlayer* player ) {
	return;
}

/*
================
rvGameState::Spectate
================
*/
void rvGameState::Spectate( idPlayer* player ) {
	if( player->spectating && player->wantSpectate ) {
		gameLocal.mpGame.ClearFrags( player->entityNumber );
	}
	return;
}

// openQ4 BEGIN
/*
================
rvGameState::AllowRespawn
================
*/
bool rvGameState::AllowRespawn( idPlayer* player ) {
	return true;
}

/*
================
rvGameState::PlayerDeath
================
*/
void rvGameState::PlayerDeath( idPlayer* dead, idPlayer* killer ) {
}

/*
================
rvGameState::PlayerWithdrew
================
*/
void rvGameState::PlayerWithdrew( idPlayer* player ) {
}

/*
================
rvGameState::PlayerDamage
================
*/
void rvGameState::PlayerDamage( idPlayer* attacker, idPlayer* victim, int damage, int armorSave ) {
}

/*
================
rvGameState::WeaponsLocked
================
*/
bool rvGameState::WeaponsLocked( void ) const {
	return gameLocal.mpGame.ArenaCampaignLocksPlayers();
}
// openQ4 END

/*
================
rvGameState::operator==
================
*/
bool rvGameState::operator==( const rvGameState& rhs ) const {
	return	(
		( currentState == rhs.currentState ) &&
		( nextState	== rhs.nextState ) &&
		( nextStateTime == rhs.nextStateTime ) &&
		( overtimeCount == rhs.overtimeCount ) &&
		( overtimeAccumulatedMsec == rhs.overtimeAccumulatedMsec ) &&
		( overtimeStartTime == rhs.overtimeStartTime )
		);
}

/*
================
rvGameState::operator!=
================
*/
bool rvGameState::operator!=( const rvGameState& rhs ) const {
	return	(
		( currentState != rhs.currentState ) ||
		( nextState	!= rhs.nextState ) ||
		( nextStateTime != rhs.nextStateTime ) ||
		( overtimeCount != rhs.overtimeCount ) ||
		( overtimeAccumulatedMsec != rhs.overtimeAccumulatedMsec ) ||
		( overtimeStartTime != rhs.overtimeStartTime )
		);
}

/*
================
rvGameState::operator=
================
*/
rvGameState& rvGameState::operator=( const rvGameState& rhs ) {
	currentState = rhs.currentState;
	nextState = rhs.nextState;
	nextStateTime = rhs.nextStateTime;
	overtimeCount = rhs.overtimeCount;
	overtimeAccumulatedMsec = rhs.overtimeAccumulatedMsec;
	overtimeStartTime = rhs.overtimeStartTime;
	return (*this);
}

/*
===============
rvGameState::WriteNetworkInfo
===============
*/
void rvGameState::WriteNetworkInfo( idFile *file, int clientNum ) {
	idBitMsg	msg;
	byte		msgBuf[ MAX_GAME_MESSAGE_SIZE ];
	
	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.BeginWriting();
	WriteState( msg );
	file->WriteInt( msg.GetSize() );
	file->Write( msg.GetData(), msg.GetSize() );
}

/*
===============
rvGameState::ReadNetworkInfo
===============
*/
void rvGameState::ReadNetworkInfo( idFile *file, int clientNum ) {
	idBitMsg	msg;
	byte		msgBuf[ MAX_GAME_MESSAGE_SIZE ];
	int			size;

	if ( file->ReadInt( size ) != sizeof( size ) ||
		 size <= 0 || size > static_cast<int>( sizeof( msgBuf ) ) ) {
		common->Warning( "rvGameState::ReadNetworkInfo: invalid state size %d", size );
		return;
	}
	msg.Init( msgBuf, sizeof( msgBuf ) );
	msg.SetSize( size );
	if ( file->Read( msg.GetData(), size ) != size ) {
		common->Warning( "rvGameState::ReadNetworkInfo: truncated state" );
		return;
	}
	ReceiveState( msg );
}

/*
===============================================================================

rvDMGameState

Game state info for DM

===============================================================================
*/
rvDMGameState::rvDMGameState( bool allocPrevious ) : rvGameState( allocPrevious ) {
	trackPrevious = allocPrevious;
}

void rvDMGameState::Run( void ) {
	idPlayer* player = NULL;

	rvGameState::Run();
	
	switch( currentState ) {
		case GAMEON: {
			player = gameLocal.mpGame.FragLimitHit();
			int leadingScore = 0;
			const bool tiedForFirst = player == NULL &&
				gameLocal.mpGame.ScoreIsTied( &leadingScore );

			if ( player ) {
				// delay between detecting frag limit and ending game. let the death anims play
				if ( !fragLimitTimeout ) {
					common->DPrintf( "enter FragLimit timeout, player %d is leader\n", player->entityNumber );
					fragLimitTimeout = gameLocal.time + FRAGLIMIT_DELAY;
				}
				if ( gameLocal.time > fragLimitTimeout ) {
					NewState( GAMEREVIEW );

					gameLocal.mpGame.PrintMessageEvent( -1, MSG_FRAGLIMIT, player->entityNumber );

				}
			} else if ( fragLimitTimeout ) {
				// frag limit was hit and cancelled. means the two teams got even during FRAGLIMIT_DELAY
				// enter sudden death, the next frag leader will win
				//
				// jshepard: OR it means that the winner killed himself during the fraglimit delay, and the
				// game needs to roll on.
				if ( tiedForFirst && gameLocal.serverInfo.GetInt( "si_fragLimit" ) > 0 &&
					leadingScore >= gameLocal.serverInfo.GetInt( "si_fragLimit" ) ) {
					if ( gameLocal.time <= fragLimitTimeout ) {
						break;
					}
					// A score-limit tie is decided by the next lead. Timed
					// overtime belongs to expiry of the match clock below.
					NewState( SUDDENDEATH );
				}
				//otherwise, just keep playing as normal.
				fragLimitTimeout = 0;

			} else if ( gameLocal.mpGame.TimeLimitHit() ) {
				gameLocal.mpGame.PrintMessageEvent( -1, MSG_TIMELIMIT );
				if( tiedForFirst ) {
					fragLimitTimeout = 0;
					if ( !StartOvertime() ) {
						NewState( SUDDENDEATH );
					}
				} else {
					// or just end the game
					NewState( GAMEREVIEW );
				}
			} else if( tiedForFirst && gameLocal.serverInfo.GetInt( "si_fragLimit" ) > 0 && leadingScore >= gameLocal.serverInfo.GetInt( "si_fragLimit" ) ) {
				// check for the rare case that two players both hit the fraglimit the same frame
				// two people tied at fraglimit, advance to sudden death after a delay
				fragLimitTimeout = gameLocal.time + FRAGLIMIT_DELAY;
			}
	
			break;			 
		}

		case SUDDENDEATH: {
			player = gameLocal.mpGame.FragLeader();

			if ( player ) {
				if ( !fragLimitTimeout ) {
					common->DPrintf( "enter sudden death FragLeader timeout, player %d is leader\n", player->entityNumber );
					fragLimitTimeout = gameLocal.time + FRAGLIMIT_DELAY;
				}
				if ( gameLocal.time > fragLimitTimeout ) {
					NewState( GAMEREVIEW );
					gameLocal.mpGame.PrintMessageEvent( -1, MSG_FRAGLIMIT, player->entityNumber );
				}
			} else if ( fragLimitTimeout ) {
				gameLocal.mpGame.PrintMessageEvent( -1, MSG_HOLYSHIT );
				fragLimitTimeout = 0;
			}
			break;
		}

	}
}

/*
===============================================================================

rvTeamDMGameState

Game state info for Team DM

===============================================================================
*/
rvTeamDMGameState::rvTeamDMGameState( bool allocPrevious ) : rvGameState( allocPrevious ) {
	trackPrevious = allocPrevious;
}

void rvTeamDMGameState::Run( void ) {
	rvGameState::Run();

	switch( currentState ) {
		case GAMEON: {
			const int marineScore = gameLocal.mpGame.GetScoreForTeam( TEAM_MARINE );
			const int stroggScore = gameLocal.mpGame.GetScoreForTeam( TEAM_STROGG );
			const int limit = gameLocal.serverInfo.GetInt( "si_fragLimit" );
			const int leadingTeam = marineScore >= stroggScore ? TEAM_MARINE : TEAM_STROGG;
			const int team = limit > 0 && Max( marineScore, stroggScore ) >= limit ? leadingTeam : -1;
			const bool tiedForFirst = marineScore == stroggScore;
			if ( team >= 0 && !tiedForFirst ) {
				if ( !fragLimitTimeout ) {
					common->DPrintf( "enter FragLimit timeout, team %d is leader\n", team );
					fragLimitTimeout = gameLocal.time + FRAGLIMIT_DELAY;
				}
				if ( gameLocal.time > fragLimitTimeout ) {
					NewState( GAMEREVIEW );

					gameLocal.mpGame.PrintMessageEvent( -1, MSG_FRAGLIMIT, team );

				}
			} else if ( fragLimitTimeout ) {
				// frag limit was hit and cancelled. means the two teams got even during FRAGLIMIT_DELAY
				// enter sudden death, the next frag leader will win
				//
				// jshepard: OR it means that the winner killed himself during the fraglimit delay, and the
				// game needs to roll on.
				if ( tiedForFirst && team >= 0 ) {
					if ( gameLocal.time <= fragLimitTimeout ) {
						break;
					}
					NewState( SUDDENDEATH );
				}
				// openQ4: this used to be cleared only on the tied branch, so a
				// leader who suicided during FRAGLIMIT_DELAY left the timeout
				// armed and the match ended on the next frame anyway.
				fragLimitTimeout = 0;
			} else if ( gameLocal.mpGame.MercyLimitHit() >= 0 ) {
				// openQ4: mercy rule, carried over from Quake Live
				int mercyTeam = gameLocal.mpGame.MercyLimitHit();
				gameLocal.mpGame.CenterPrint( -1, "#str_41317", idMultiplayerGame::CPARM_TEAM, mercyTeam );
				gameLocal.mpGame.PrintMessageEvent( -1, MSG_FRAGLIMIT, mercyTeam );
				NewState( GAMEREVIEW );
			} else if ( gameLocal.mpGame.TimeLimitHit() ) {
				gameLocal.mpGame.PrintMessageEvent( -1, MSG_TIMELIMIT );
				if( tiedForFirst ) {
					fragLimitTimeout = 0;
					if ( !StartOvertime() ) {
						NewState( SUDDENDEATH );
					}
				} else {
					// or just end the game
					NewState( GAMEREVIEW );
				}
			} else if( tiedForFirst && team >= 0 ) {
				// check for the rare case that two teams both hit the fraglimit the same frame
				// two people tied at fraglimit, advance to sudden death after a delay
				fragLimitTimeout = gameLocal.time + FRAGLIMIT_DELAY;
			}
			break;
		}

		case SUDDENDEATH: {
			int team = gameLocal.mpGame.GetScoreForTeam( TEAM_MARINE ) > gameLocal.mpGame.GetScoreForTeam( TEAM_STROGG ) ? TEAM_MARINE : TEAM_STROGG;
			bool tiedForFirst = false;
			if( gameLocal.mpGame.GetScoreForTeam( TEAM_MARINE ) == gameLocal.mpGame.GetScoreForTeam( TEAM_STROGG ) ) {
				team = -1;
				tiedForFirst = true;
			}

			if ( team >= 0 && !tiedForFirst ) {
				if ( !fragLimitTimeout ) {
					common->DPrintf( "enter sudden death FragLeader timeout, team %d is leader\n", team );
					fragLimitTimeout = gameLocal.time + FRAGLIMIT_DELAY;
				}
				if ( gameLocal.time > fragLimitTimeout ) {
					NewState( GAMEREVIEW );
					gameLocal.mpGame.PrintMessageEvent( -1, MSG_FRAGLIMIT, team );
				}
			} else if ( fragLimitTimeout ) {
				gameLocal.mpGame.PrintMessageEvent( -1, MSG_HOLYSHIT );
				fragLimitTimeout = 0;
			} 

			break;
		}
	}
}

/*
===============================================================================

rvCTFGameState

Game state info for CTF

===============================================================================
*/

/*
================
rvCTFGameState::rvCTFGameState
================
*/
rvCTFGameState::rvCTFGameState( bool allocPrevious ) : rvGameState( false ) {
	Clear();

	if( allocPrevious ) {
		previousGameState = new rvCTFGameState( false );
	} else {
		previousGameState = NULL;
	}

	trackPrevious = allocPrevious;
}

/*
================
rvCTFGameState::Clear
================
*/
void rvCTFGameState::Clear( void ) {
	rvGameState::Clear();

	// mekberg: clear previous game state.
	if ( previousGameState ) {
		previousGameState->Clear( );
	}		

	for( int i = 0; i < MAX_CTF_FLAGS; i++ ) {
		flagStatus[ i ].state = FS_AT_BASE;
		flagStatus[ i ].clientNum = -1;
	}

	for( int i = 0; i < MAX_AP; i++ ) {
		apState[ i ] = AS_NEUTRAL;
	}
}

/*
================
rvCTFGameState::SendState
================
*/
void rvCTFGameState::SendState( const idMessageSender &sender, int clientNum ) {
	idBitMsg	outMsg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];

	assert( (gameLocal.isServer || gameLocal.isRepeater) && trackPrevious && IsType( rvCTFGameState::GetClassType() ) );

	if( clientNum == -1 && (rvCTFGameState&)(*this) == (rvCTFGameState&)(*previousGameState) ) {
		return;
	}

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_GAMESTATE );

	WriteState( outMsg );

	sender.Send( outMsg );

	// don't update the state if we are working for a single client
	if ( clientNum == -1 ) {
		outMsg.ReadByte(); // pop off the msg ID
		ReceiveState( outMsg );
	}
}

/*
===============
rvCTFGameState::WriteState
===============
*/
void rvCTFGameState::WriteState( idBitMsg &msg ) {
	// send off base info
	rvGameState::PackState( msg );
	// add CTF info
	PackState( msg );
}

/*
================
rvCTFGameState::ReceiveState
================
*/
void rvCTFGameState::ReceiveState( const idBitMsg& msg ) {
	assert( IsType( rvCTFGameState::GetClassType() ) );

	if ( !rvGameState::BaseUnpackState( msg ) ) {
		return;
	}
	UnpackState( msg );

	if ( gameLocal.localClientNum >= 0 ) {
		GameStateChanged();
	}

	(rvCTFGameState&)(*previousGameState) = (rvCTFGameState&)(*this);
}

/*
================
rvCTFGameState::PackState
================
*/
void rvCTFGameState::PackState( idBitMsg& outMsg ) {
	// use indexing to pack in info
	int index = 0;

	// openQ4: MAX_CTF_FLAGS, not TEAM_MAX - the neutral one flag slot is part of
	// the index space now.  UnpackState below moves with it; both sides of this
	// wire format live in this file and ship in the same game library.
	for( int i = 0; i < MAX_CTF_FLAGS; i++ ) {
		if( flagStatus[ i ] != ((rvCTFGameState*)previousGameState)->flagStatus[ i ] ) {
			outMsg.WriteByte( index );
			outMsg.WriteByte( flagStatus[ i ].state );
			outMsg.WriteByte( flagStatus[ i ].clientNum );
		}
		index++;
	}

	for( int i = 0; i < MAX_AP; i++ ) {
		if( apState[ i ] != ((rvCTFGameState*)previousGameState)->apState[ i ] ) {
			outMsg.WriteByte( index );
			outMsg.WriteByte( apState[ i ] );
		}
		index++;
	}
}

/*
================
rvCTFGameState::UnpackState
================
*/
void rvCTFGameState::UnpackState( const idBitMsg& inMsg ) {
	while( inMsg.GetRemainingData() ) {
		int index = inMsg.ReadByte();

		if( index >= 0 && index < MAX_CTF_FLAGS ) {
			flagStatus[ index ].state = (flagState_t)inMsg.ReadByte();
			flagStatus[ index ].clientNum = inMsg.ReadByte();
		} else if( index >= MAX_CTF_FLAGS && index < ( MAX_CTF_FLAGS + MAX_AP ) ) {
			apState[ index - MAX_CTF_FLAGS ] = (apState_t)inMsg.ReadByte();
		} else {
			// openQ4: this blob also reaches us from a recorded server demo, and
			// the index space widened when the neutral one-flag slot got real
			// storage - so a demo from an older build can land here.  Abandoning
			// the rest of the delta loses a scoreboard update; killing the process
			// loses the demo and everything else with it.
			gameLocal.Warning( "rvCTFGameState::UnpackState() - unknown data identifier '%d', "
				"ignoring the rest of this state block", index );
			return;
		}
	}
}

/*
================
rvCTFGameState::SendInitialState
================
*/
void rvCTFGameState::SendInitialState( const idMessageSender &sender, int clientNum ) {
	assert( IsType( rvCTFGameState::GetClassType() ) );

	rvCTFGameState* previousState = (rvCTFGameState*)previousGameState;

	rvCTFGameState invalidState;

	previousGameState = &invalidState;

	SendState( sender, clientNum );

	previousGameState = previousState;
}

/*
================
rvCTFGameState::GameStateChanged
================
*/
void rvCTFGameState::GameStateChanged( void ) {
	// detect any base state changes
	rvGameState::GameStateChanged();

	// CTF specific stuff
	idPlayer* player = gameLocal.GetLocalPlayer();
	if ( !player ) {
		if ( gameLocal.IsServerDemoPlaying() && gameLocal.GetDemoFollowClient() >= 0 ) {
			player = static_cast< idPlayer * >( gameLocal.entities[ gameLocal.GetDemoFollowClient() ] );
		}
	}
	if ( !player ) {
		gameLocal.Warning( "rvCTFGameState::GameStateChanged() - NULL local player\n" ) ;
		return;
	}

	bool noSounds = false;

	// openQ4: deliberately still only the two team flags, even though flagStatus
	// now has a third slot for the neutral one-flag entity.  Every branch below
	// reads flagTeam as the team that OWNS the flag ("flagTeam == player->team"
	// means "my flag"), while the one-flag remap at the top of the loop sets it
	// to the team that HOLDS it - so feeding the neutral slot through here plays
	// each announcement to exactly the wrong side, and FS_DROPPED/FS_AT_BASE are
	// not remapped at all and would push team index 2 into the MP HUD.  The
	// neutral flag's HUD display is driven after this loop instead; announcer
	// lines and notices for it still mean reworking the owner/holder sense of
	// this whole loop.  The widened storage is what stops it corrupting apState.
	for( int i = 0; i < TEAM_MAX; i++ ) {
		if( flagStatus[ i ] == ((rvCTFGameState*)previousGameState)->flagStatus[ i ] ) {
			continue;
		}

		// don't play flag messages when flag state changes as a result of the gamestate changing
		if( currentState != ((rvCTFGameState*)previousGameState)->currentState && ((rvCTFGameState*)previousGameState)->currentState != INACTIVE ) {
			continue;
		}

		if ( ((rvCTFGameState*)previousGameState)->currentState == INACTIVE ) {
			noSounds = true;
		}

		// flagTeam - used to tell the HUD which flag to update
		int flagTeam = i;

		// in one flag CTF flagTeam is set to whichever team has the flag
		if( gameLocal.gameType == GAME_1F_CTF || gameLocal.gameType == GAME_ARENA_1F_CTF ) {
			if( flagStatus[ i ].state == FS_TAKEN_MARINE ) {
				flagTeam = TEAM_MARINE;
			} else if( flagStatus[ i ].state == FS_TAKEN_STROGG ) {
				flagTeam = TEAM_STROGG;
			}
		}

		if( flagStatus[ i ].state == FS_DROPPED && !noSounds ) {
			if ( !player->spectating ) {
				if( flagTeam == player->team ) {
					// our flag was dropped, so the enemy dropped it
					gameLocal.mpGame.ScheduleAnnouncerSound ( AS_CTF_ENEMY_DROPS_FLAG, gameLocal.time, -1, true );
				} else {
					gameLocal.mpGame.ScheduleAnnouncerSound ( AS_CTF_YOUR_TEAM_DROPS_FLAG, gameLocal.time, -1, true );
				}
			}

		if( player->mphud ) {
				player->mphud->SetStateInt( "team", flagTeam );
				player->mphud->HandleNamedEvent( "flagDrop" );

				if ( !player->spectating ) {
					if ( flagTeam == player->team ) {
						player->mphud->SetStateString( "main_notice_text", common->GetLocalizedString( "#str_107723" ) );
					} else {
						player->mphud->SetStateString( "main_notice_text", common->GetLocalizedString( "#str_104420" ) );
					}

					player->mphud->HandleNamedEvent( "main_notice" );
				}
			}
		} else if( flagStatus[ i ].state == FS_AT_BASE ) {
			if( ((rvCTFGameState*)previousGameState)->flagStatus[ i ].state == FS_TAKEN && !noSounds ) {
				// team scores
				if ( !player->spectating ) {
					if( flagTeam == player->team ) {
						if( gameLocal.mpGame.CanCapture( gameLocal.mpGame.OpposingTeam( flagTeam ) ) ) {
							gameLocal.mpGame.ScheduleAnnouncerSound ( AS_TEAM_ENEMY_SCORES, gameLocal.time );
						}
					} else {
						if( gameLocal.mpGame.CanCapture( gameLocal.mpGame.OpposingTeam( flagTeam ) ) ) {
							gameLocal.mpGame.ScheduleAnnouncerSound ( AS_TEAM_YOU_SCORE, gameLocal.time );
						}
					}
				}
			} else if( ((rvCTFGameState*)previousGameState)->flagStatus[ i ].state == FS_DROPPED && !noSounds ) {
				// flag returned
				if ( !player->spectating ) {
					if( flagTeam == player->team ) {
						gameLocal.mpGame.ScheduleAnnouncerSound ( AS_CTF_YOUR_FLAG_RETURNED, gameLocal.time, -1, true );
					} else {
						gameLocal.mpGame.ScheduleAnnouncerSound ( AS_CTF_ENEMY_RETURNS_FLAG, gameLocal.time, -1, true );
					}
				}
			}
			
			if( player->mphud ) {
				player->mphud->SetStateInt( "team", flagTeam );
				player->mphud->HandleNamedEvent( "flagReturn" );
			}
		} else if( flagStatus[ i ].state == FS_TAKEN || flagStatus[ i ].state == FS_TAKEN_STROGG || flagStatus[ i ].state == FS_TAKEN_MARINE ) {
			// flag taken
			if( flagTeam == player->team ) {
				if ( !player->spectating ) {
					if ( !noSounds ) {
						gameLocal.mpGame.ScheduleAnnouncerSound ( AS_CTF_ENEMY_HAS_FLAG, gameLocal.time, -1, true );
					}
				}

				if ( !player->spectating ) {
					if ( player->mphud ) {
						player->mphud->SetStateString( "main_notice_text", common->GetLocalizedString( "#str_107722" ) );
						player->mphud->HandleNamedEvent( "main_notice" );
					}
				}
			} else {
				if ( flagStatus[ i ].clientNum == gameLocal.localClientNum ) {
					if ( !noSounds ) {
						gameLocal.mpGame.ScheduleAnnouncerSound ( AS_CTF_YOU_HAVE_FLAG, gameLocal.time, -1, true );
					}					

					if ( !player->spectating ) {
						// shouchard:  inform the GUI that you've taken the flag
						player->mphud->SetStateString( "main_notice_text", common->GetLocalizedString( "#str_104419" ) );
						player->mphud->HandleNamedEvent( "main_notice" );
					}
				} else if ( !noSounds ) {
					if ( !player->spectating ) {
						gameLocal.mpGame.ScheduleAnnouncerSound ( AS_CTF_YOUR_TEAM_HAS_FLAG, gameLocal.time, -1, true );
					}
				}
			}

			if( player->mphud ) {
				player->mphud->SetStateInt( "team", flagTeam );
				player->mphud->HandleNamedEvent ( "flagTaken" );
			}
		}
	}

	// openQ4: One Flag CTF's neutral flag, which the loop above leaves out.  It
	// has no owner, so only the MP HUD hears about it, and gui::team is the team
	// holding it: the one flag display pulses toward that team's colour.  Unlike
	// the loop, a change that lands with a match state change still reaches the
	// HUD, so a restart or a capture that ends the match cannot leave it stale.
	if ( ( gameLocal.gameType == GAME_1F_CTF || gameLocal.gameType == GAME_ARENA_1F_CTF ) && player->mphud &&
		flagStatus[ TEAM_MAX ] != ((rvCTFGameState*)previousGameState)->flagStatus[ TEAM_MAX ] ) {
		switch ( flagStatus[ TEAM_MAX ].state ) {
			case FS_TAKEN_MARINE:
				player->mphud->SetStateInt( "team", TEAM_MARINE );
				player->mphud->HandleNamedEvent( "flagTaken" );
				break;
			case FS_TAKEN_STROGG:
				player->mphud->SetStateInt( "team", TEAM_STROGG );
				player->mphud->HandleNamedEvent( "flagTaken" );
				break;
			case FS_DROPPED:
				player->mphud->HandleNamedEvent( "flagDrop" );
				break;
			case FS_AT_BASE:
				player->mphud->HandleNamedEvent( "flagReturn" );
				break;
			default:
				break;
		}
	}
}

/*
================
rvCTFGameState::Run
================
*/
void rvCTFGameState::Run( void ) {
	// run common stuff	
	rvGameState::Run();

	switch( currentState ) {
		case GAMEON: {
			const int marineScore = gameLocal.mpGame.GetScoreForTeam( TEAM_MARINE );
			const int stroggScore = gameLocal.mpGame.GetScoreForTeam( TEAM_STROGG );
			const int limit = gameLocal.serverInfo.GetInt( "si_captureLimit" );
			const int leadingTeam = marineScore >= stroggScore ? TEAM_MARINE : TEAM_STROGG;
			const int team = limit > 0 && Max( marineScore, stroggScore ) >= limit ? leadingTeam : -1;
			const bool tiedForFirst = marineScore == stroggScore;
			if ( team >= 0 && !tiedForFirst ) {
				if ( !fragLimitTimeout ) {
					common->DPrintf( "enter capture limit timeout, team %d is leader\n", team );
					fragLimitTimeout = gameLocal.time + CAPTURELIMIT_DELAY;
				}
				if ( gameLocal.time > fragLimitTimeout ) {
					NewState( GAMEREVIEW );

					gameLocal.mpGame.PrintMessageEvent( -1, MSG_CAPTURELIMIT, team );

				}
			} else if ( fragLimitTimeout ) {
				// frag limit was hit and cancelled. means the two teams got even during FRAGLIMIT_DELAY
				// enter sudden death, the next frag leader will win
				// OR the winner lost a point in the frag delay, and there's no tie, so no one wins, game on.
				if ( tiedForFirst && team >= 0 ) {
					if ( gameLocal.time <= fragLimitTimeout ) {
						break;
					}
					NewState( SUDDENDEATH );
				}
				fragLimitTimeout = 0;
			} else if ( gameLocal.mpGame.MercyLimitHit() >= 0 ) {
				// openQ4: mercy rule, carried over from Quake Live
				int mercyTeam = gameLocal.mpGame.MercyLimitHit();
				gameLocal.mpGame.CenterPrint( -1, "#str_41317", idMultiplayerGame::CPARM_TEAM, mercyTeam );
				gameLocal.mpGame.PrintMessageEvent( -1, MSG_CAPTURELIMIT, mercyTeam );
				NewState( GAMEREVIEW );
			} else if ( gameLocal.mpGame.TimeLimitHit() ) {
				gameLocal.mpGame.PrintMessageEvent( -1, MSG_TIMELIMIT );
				if( tiedForFirst ) {
					fragLimitTimeout = 0;
					if ( !StartOvertime() ) {
						NewState( SUDDENDEATH );
					}
				} else {
					// or just end the game
					NewState( GAMEREVIEW );
				}
			} else if( tiedForFirst && team >= 0 ) {
				// check for the rare case that two teams both hit the fraglimit the same frame
				// two people tied at fraglimit, advance to sudden death after a delay
				fragLimitTimeout = gameLocal.time + CAPTURELIMIT_DELAY;
			}
			break;
		}

		case SUDDENDEATH: {
			int team = gameLocal.mpGame.GetScoreForTeam( TEAM_MARINE ) > gameLocal.mpGame.GetScoreForTeam( TEAM_STROGG ) ? TEAM_MARINE : TEAM_STROGG;
			bool tiedForFirst = false;
			if( gameLocal.mpGame.GetScoreForTeam( TEAM_MARINE ) == gameLocal.mpGame.GetScoreForTeam( TEAM_STROGG ) ) {
				team = -1;
				tiedForFirst = true;
			}

			if ( team >= 0 && !tiedForFirst ) {
				if ( !fragLimitTimeout ) {
					common->DPrintf( "enter sudden death FragLeader timeout, team %d is leader\n", team );
					fragLimitTimeout = gameLocal.time + CAPTURELIMIT_DELAY;
				}
				if ( gameLocal.time > fragLimitTimeout ) {
					NewState( GAMEREVIEW );
					gameLocal.mpGame.PrintMessageEvent( -1, MSG_CAPTURELIMIT, team );
				}
			} else if ( fragLimitTimeout ) {
				gameLocal.mpGame.PrintMessageEvent( -1, MSG_HOLYSHIT );
				fragLimitTimeout = 0;
			} 

			break;
		}
	}
}

/*
================
rvCTFGameState::SetFlagState
================
*/
void rvCTFGameState::SetFlagState( int flag, flagState_t newState ) {
	if ( !gameLocal.isServer ) {
		return;
	}

	assert( IsType( rvCTFGameState::GetClassType() ) );

	// openQ4: a real bound.  rvItemCTFFlag::ResetFlag passes -1 for a powerup it
	// cannot map back to a flag, and the assert this replaces was compiled out of
	// a release build, so the write landed on whatever follows the table.
	if ( flag < 0 || flag >= MAX_CTF_FLAGS ) {
		gameLocal.Warning( "rvCTFGameState::SetFlagState() - flag index %d out of range", flag );
		return;
	}

	flagStatus[ flag ].state = newState;
}

/*
================
rvCTFGameState::SetFlagCarrier
================
*/
void rvCTFGameState::SetFlagCarrier( int flag, int clientNum ) {
	assert( gameLocal.isServer && IsType( rvCTFGameState::GetClassType() ) );

	// openQ4: see SetFlagState - the one flag entity carries team == TEAM_MAX and
	// this used to write past the end of the table with only an assert to catch it.
	if ( flag < 0 || flag >= MAX_CTF_FLAGS || clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		gameLocal.Warning( "rvCTFGameState::SetFlagCarrier() - flag %d / client %d out of range", flag, clientNum );
		return;
	}

	flagStatus[ flag ].clientNum = clientNum;
}

/*
================
rvCTFGameState::operator==
================
*/
bool rvCTFGameState::operator==( const rvCTFGameState& rhs ) const {
	if( (rvGameState&)(*this) != (rvGameState&)rhs ) {
		return false;
	}

	for( int i = 0; i < MAX_CTF_FLAGS; i++ ) {
		if( flagStatus[ i ] != rhs.flagStatus[ i ] ) {
			return false;
		}
	}

	for( int i = 0; i < MAX_AP; i++ ) {
		if( apState[ i ] != rhs.apState[ i ] ) {
			return false;
		}
	}

	return true;
}

/*
================
rvCTFGameState::operator=
================
*/
rvCTFGameState& rvCTFGameState::operator=( const rvCTFGameState& rhs ) {
	(rvGameState&)(*this) = (rvGameState&)rhs;

	for( int i = 0; i < MAX_CTF_FLAGS; i++ ) {
		flagStatus[ i ] = rhs.flagStatus[ i ];
	}

	for( int i = 0; i < MAX_AP; i++ ) {
		apState[ i ] = rhs.apState[ i ];
	}

 	return (*this);
}

/*
===============================================================================

rvTourneyGameState

Game state info for tourney

===============================================================================
*/

/*
================
rvTourneyGameState::rvTourneyGameState
================
*/
rvTourneyGameState::rvTourneyGameState( bool allocPrevious ) : rvGameState( false ) {
	Clear();

	if( allocPrevious ) {
		previousGameState = new rvTourneyGameState( false );
	} else {
		previousGameState = NULL;
	}

	packTourneyHistory = false;

	trackPrevious = allocPrevious;
}

/*
================
rvTourneyGameState::Clear
================
*/
void rvTourneyGameState::Clear( void ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	// mekberg: clear previous game state.
	if ( previousGameState ) {
		previousGameState->Clear();
	}		

	rvGameState::Clear();

	tourneyState = TS_INVALID;

	for( int i = 0; i < MAX_ARENAS; i++ ) {
		arenas[ i ].Clear( false );
		arenas[ i ].SetArenaID( i );
	}

	for( int i = 0; i < MAX_ROUNDS; i++ ) {
		for( int j = 0; j < MAX_ARENAS; j++ ) {
			tourneyHistory[ i ][ j ].playerOne[ 0 ] = '\0';
			tourneyHistory[ i ][ j ].playerTwo[ 0 ] = '\0';
			tourneyHistory[ i ][ j ].playerOneNum = -1;
			tourneyHistory[ i ][ j ].playerTwoNum = -1;
			tourneyHistory[ i ][ j ].playerOneScore = -1;
			tourneyHistory[ i ][ j ].playerTwoScore = -1;
		}
	}

	startingRound = 0;
	maxRound = 0;
	round = -1;
	byeArena = -1;
	tourneyCount = 0;
	roundTimeout = 0;
}

void rvTourneyGameState::ShiftMatchTime( int deltaMsec ) {
	rvGameState::ShiftMatchTime( deltaMsec );
	ShiftGameStateDeadline( roundTimeout, deltaMsec );
	for ( int index = 0; index <= MAX_ARENAS; ++index ) {
		arenas[ index ].ShiftMatchTime( deltaMsec );
	}
}

/*
================
rvTourneyGameState::Reset
================
*/
void rvTourneyGameState::Reset( void ) {
	for( int i = 0; i < MAX_ARENAS; i++ ) {
		arenas[ i ].Clear( false );
		arenas[ i ].SetArenaID( i );
	}

	for( int i = 0; i < MAX_ROUNDS; i++ ) {
		for( int j = 0; j < MAX_ARENAS; j++ ) {
			tourneyHistory[ i ][ j ].playerOne[ 0 ] = '\0';
			tourneyHistory[ i ][ j ].playerTwo[ 0 ] = '\0';
			tourneyHistory[ i ][ j ].playerOneNum = -1;
			tourneyHistory[ i ][ j ].playerTwoNum = -1;
			tourneyHistory[ i ][ j ].playerOneScore = -1;
			tourneyHistory[ i ][ j ].playerTwoScore = -1;
		}
	}

	startingRound = 0;
	maxRound = 0;
	round = -1;
	byeArena = -1;
	roundTimeout = 0;
}

/*
================
rvTourneyGameState::Run
================
*/
void rvTourneyGameState::Run( void ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	rvGameState::Run();

	switch( currentState ) {
		case GAMEON: {
			// check to see if we need to advance to the next round
			if( nextState == GAMEREVIEW ) {
				return;
			}

			bool roundDone = true;

			for( int i = 0; i < MAX_ARENAS; i++ ) {
				if( arenas[ i ].GetState() == AS_INACTIVE ) {
					continue;
				}

				arenas[ i ].UpdateState();

				if( arenas[ i ].GetState() != AS_DONE ) {
					// check to see if we're done
					roundDone = false;
				}
			}
			if( roundDone && !roundTimeout ) {
				roundTimeout = gameLocal.time + FRAGLIMIT_DELAY;
			}

			if( roundDone && gameLocal.time > roundTimeout ) {
				int pastRound = round - 1; //rounds are 1 indexed
				idList<rvPair<idPlayer*, int> > advancingPlayers;

				round++;
				roundTimeout = 0;

				assert( round >= 2 );

				// copy over tourney history for the previous round
				UpdateTourneyHistory( pastRound );			

				// transmit history
				forceTourneyHistory = true;

				// add who will advance to the next round
				for( int i = 0; i < MAX_ARENAS; i++ ) {
					if( arenas[ i ].GetState() == AS_DONE && arenas[ i ].GetWinner() ) {
						advancingPlayers.Append( rvPair<idPlayer*, int>( arenas[ i ].GetWinner(), i ) );
						gameLocal.mpGame.AddPlayerWin( arenas[ i ].GetWinner(), 1 );
					} 
				}

				// when only one player advances, that player has won the tournament
				if( advancingPlayers.Num() == 1 ) {
					idPlayer* winner = advancingPlayers[ 0 ].First();
					assert( winner );

					for( int i = 0; i < MAX_ARENAS; i++ ) {
						arenas[ i ].Clear();
					}

					gameLocal.Printf( "Round %d: player %d (%s) has won the tourney!\n", round - 1, winner->entityNumber, gameLocal.userInfo[ winner->entityNumber ].GetString( "ui_name" ) );
					// award 5 wins for winning the entire tournament
					gameLocal.mpGame.AddPlayerWin( winner, 5 );

					nextState = GAMEREVIEW;
					nextStateTime = gameLocal.time + 5000;

					return;
				}

				// see if we have to swap a player from the byeArena this round
				bool swapBye = false;
				int thisByeArena = -1;

				if( byeArena >= 0 ) {
					// look at the bye arena from the round that just ended
					thisByeArena = byeArena / (round - 1);

					// if the bye arena is playing in the next round, there's no need to switch players
					if( !(thisByeArena % 2) ) {
						if( arenas[ thisByeArena ].GetState() == AS_DONE && arenas[ thisByeArena + 1 ].GetState() == AS_DONE ) {
							assert( arenas[ thisByeArena ].GetWinner() && arenas[ thisByeArena + 1 ].GetWinner() );
							swapBye = false;
						} else {
							swapBye = true;
						}
					} else {
 						if( arenas[ thisByeArena ].GetState() == AS_DONE && arenas[ thisByeArena - 1 ].GetState() == AS_DONE ) {
							assert( arenas[ thisByeArena ].GetWinner() && arenas[ thisByeArena - 1 ].GetWinner() );
							swapBye = false;
						} else {
							swapBye = true;
						}
					}
				}

				idPlayer* oldByePlayer = NULL;
				idPlayer* newByePlayer = NULL;				

				if( swapBye ) {
					oldByePlayer = arenas[ thisByeArena ].GetWinner();

					// pick a new bye player only from players who will be fighting next round
					// i.e., don't swap the bye player with a player who will have a disconnect bye this upcoming round
					idList<rvPair<idPlayer*, int> > nextRoundPlayers;
					for ( int i = 0; i < MAX_ARENAS; i += 2 ) {
						if( arenas[ i ].GetState() == AS_DONE && arenas[ i ].GetWinner() && arenas[ i + 1 ].GetState() == AS_DONE && arenas[ i + 1 ].GetWinner() ) {
							nextRoundPlayers.Append( rvPair<idPlayer*, int>( arenas[ i ].GetWinner(), i ) );
							nextRoundPlayers.Append( rvPair<idPlayer*, int>( arenas[ i + 1 ].GetWinner(), i + 1 ) );
						}
					}
						
					if ( nextRoundPlayers.Num() ) {
						newByePlayer = nextRoundPlayers[ gameLocal.random.RandomInt( nextRoundPlayers.Num() ) ].First();
					}
				} 

				// assign arenas for the next round
				for ( int i = 0; i < MAX_ARENAS; i += 2 ) {
					idPlayer* advanceOne = arenas[ i ].GetWinner();
					idPlayer* advanceTwo = arenas[ i + 1 ].GetWinner();

					// #13266 - bug: bystander from prev round is spectator in his match arena after round change
					// we call rvTourneyInstance::Clear when setting up rounds (below), which sets it's players as spectator
					// if the former bystander is placed in one of the new arenas and is still referenced in the bye arena then he'll get set spectating
					// so just clear him up once identified
					// #13631 - we used to call RemovePlayer before entering that loop, but that was messing up the selection of the next bystander
					// so have to do it inside the loop now, just before it happens					
					if ( ( i == thisByeArena || i + 1 == thisByeArena ) && arenas[ thisByeArena ].HasPlayer( oldByePlayer ) ) {
						arenas[ thisByeArena ].RemovePlayer( oldByePlayer );
					}

					arenas[ i ].Clear();
					arenas[ i + 1 ].Clear();

					// assign new arenas, swapping oldByePlayer with newByePlayer
					if ( newByePlayer && oldByePlayer ) {
						if ( advanceOne == newByePlayer ) {
							advanceOne = oldByePlayer;
						} else if ( advanceTwo == newByePlayer ) {
							advanceTwo = oldByePlayer;
						} else if ( advanceOne == oldByePlayer ) {
							advanceOne = newByePlayer;
						} else if ( advanceTwo == oldByePlayer ) {
							advanceTwo = newByePlayer;
						}
					}
					
					if ( advanceOne || advanceTwo ) {
						arenas[ (i / 2) ].AddPlayers( advanceOne, advanceTwo );

						if ( advanceOne ) {
							advanceOne->JoinInstance( (i / 2) );
							advanceOne->ServerSpectate( false );
						}

						if ( advanceTwo ) {
							advanceTwo->JoinInstance( (i / 2) );
							advanceTwo->ServerSpectate( false );
						}

						gameLocal.Printf( "Round %d: Arena %d is starting play with players %d (%s) and %d (%s)\n", round, (i / 2), advanceOne ? advanceOne->entityNumber : -1, advanceOne ? advanceOne->GetUserInfo()->GetString( "ui_name" ) : "NULL", advanceTwo ? advanceTwo->entityNumber : -1, advanceTwo ? advanceTwo->GetUserInfo()->GetString( "ui_name" ) : "NULL" );

						arenas[ (i / 2) ].Ready();
					}
				}

				// once the new round is setup, go through and make sure all the spectators are in valid arena
				for( int i = 0; i < gameLocal.numClients; i++ ) {
					idPlayer* p = (idPlayer*)gameLocal.entities[ i ];

					// re-select our arena since round changed
					if( p && p->spectating ) {
						rvTourneyArena& thisArena = arenas[ p->GetArena() ];
						if( thisArena.GetState() != AS_WARMUP ) {
							p->JoinInstance( GetNextActiveArena( p->GetArena() ) );
						}
					}
				}
			}

			break;
		}
	}
}

/*
================
rvTourneyGameState::NewState
================
*/
bool rvTourneyGameState::NewState( mpGameState_t newState ) {
	assert( (newState != currentState) && gameLocal.isServer );
	// Tourney prepares brackets before the base adapter applies the transition.
	// Preflight first so a rejected authoritative edge has no bracket/spectate
	// side effects; rvGameState::NewState performs the actual atomic commit.
	if ( gameLocal.mpGame.GetMatchSession().GetPhase() != newState &&
		!gameLocal.mpGame.CanCommitMatchPhaseTransition( newState ) ) {
		return false;
	}

	switch( newState ) {
		case WARMUP: {
			Reset();
			// force everyone to spectate
			for( int i = 0; i < MAX_CLIENTS; i++ ) {
				if( gameLocal.entities[ i ] ) {
					((idPlayer*)gameLocal.entities[ i ])->ServerSpectate( true );
				}
			}
			break;
		}
		case GAMEON: {
			tourneyCount++;
			SetupInitialBrackets();
			break;
		}
	}

	return rvGameState::NewState( newState );
}

/*
================
rvTourneyGameState::GameStateChanged
================
*/
void rvTourneyGameState::GameStateChanged( void ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );
	rvGameState::GameStateChanged();
	idPlayer* localPlayer = gameLocal.GetLocalPlayer();

	int oldRound = ((rvTourneyGameState*)previousGameState)->round;

	if( round != oldRound ) {
		if ( round > maxRound && gameLocal.GetLocalPlayer() ) {
			gameLocal.GetLocalPlayer()->ForceScoreboard( true, gameLocal.time + 5000 );
			gameLocal.mpGame.ScheduleAnnouncerSound( AS_TOURNEY_DONE, gameLocal.time );
		}

		// we're starting a new round
		gameLocal.mpGame.tourneyGUI.RoundStart();
		
		// skip announce if the round number doesn't make sense
		if ( round >= 1 && round <= MAX_ROUNDS ) {
			// play the sound a bit after round restart to let spawn sounds settle
			gameLocal.mpGame.ScheduleAnnouncerSound( (announcerSound_t)( AS_TOURNEY_PRELIMS + round - 1 ), gameLocal.time + 1500);
		}
	}

	for( int i = 0; i < MAX_ARENAS; i++ ) {
		rvTourneyArena& thisArena = arenas[ i ];
		rvTourneyArena& previousArena = ((rvTourneyGameState*)previousGameState)->arenas[ i ];
		
		if( ((thisArena.GetPlayers()[ 0 ] != previousArena.GetPlayers()[ 0 ]) ||
			(thisArena.GetPlayers()[ 1 ] != previousArena.GetPlayers()[ 1 ]) ||
			(round != ((rvTourneyGameState*)previousGameState)->round )) /*&& 
			!((thisArena.GetPlayers()[ 0 ] == NULL && thisArena.GetPlayers()[ 1 ] != NULL) ||
			  (thisArena.GetPlayers()[ 0 ] != NULL && thisArena.GetPlayers()[ 1 ] == NULL) ) */) {

			// don't re-init arenas that have changed and been done for a while
			if( thisArena.GetState() != AS_DONE || previousArena.GetState() != AS_DONE ) {
				gameLocal.mpGame.tourneyGUI.ArenaStart( i );
			}

			if( localPlayer && thisArena.GetPlayers()[ 0 ] == localPlayer ) {
				gameLocal.mpGame.tourneyGUI.ArenaSelect( i, TGH_PLAYER_ONE );
			}
			if( localPlayer && thisArena.GetPlayers()[ 1 ] == localPlayer ) {
				gameLocal.mpGame.tourneyGUI.ArenaSelect( i, TGH_PLAYER_TWO );
			}
		}

		if( localPlayer->GetArena() == thisArena.GetArenaID() && thisArena.GetState() == AS_SUDDEN_DEATH && thisArena.GetState() != previousArena.GetState() ) {
			gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_SUDDEN_DEATH, gameLocal.time, thisArena.GetArenaID() );
			gameLocal.GetLocalPlayer()->GUIMainNotice( common->GetLocalizedString( "#str_104287" ) );
		}

		if( thisArena.GetState() == AS_DONE && thisArena.GetState() != previousArena.GetState() ) {
			if( thisArena.GetWinner() != previousArena.GetWinner() && thisArena.GetWinner() == gameLocal.GetLocalPlayer() ) {
				if( round >= maxRound ) {
					gameLocal.mpGame.ScheduleAnnouncerSound( AS_TOURNEY_WON, gameLocal.time );
				} else {
 					gameLocal.mpGame.ScheduleAnnouncerSound( AS_TOURNEY_ADVANCE, gameLocal.time );
				}
			} else if( thisArena.GetLoser() != previousArena.GetLoser() && thisArena.GetLoser() == gameLocal.GetLocalPlayer() ) {
				gameLocal.mpGame.ScheduleAnnouncerSound( AS_TOURNEY_ELIMINATED, gameLocal.time );
			}

			if( previousArena.GetWinner() == NULL && thisArena.GetWinner() != NULL ) {
				gameLocal.mpGame.tourneyGUI.ArenaDone( i );
			}

			// on the client, add this result to our tourneyHistory
			//if( gameLocal.isClient && thisArena.GetPlayers()[ 0 ] && thisArena.GetPlayers()[ 1 ] && (round - 1 ) >= 0 && (round - 1) < MAX_ROUNDS ) {
			//	tourneyHistory[ round - 1 ][ i ].playerOneScore = gameLocal.mpGame.GetTeamScore( thisArena.GetPlayers()[ 0 ] );
			//	tourneyHistory[ round - 1 ][ i ].playerTwoScore = gameLocal.mpGame.GetTeamScore( thisArena.GetPlayers()[ 1 ] );
			//}
		}

		if( localPlayer && (thisArena.GetState() == AS_WARMUP) && (thisArena.GetState() != previousArena.GetState()) && localPlayer->GetArena() == thisArena.GetArenaID() ) {
			gameLocal.mpGame.RemoveAnnouncerSound( AS_GENERAL_PREPARE_TO_FIGHT );
			gameLocal.mpGame.RemoveAnnouncerSound( AS_GENERAL_THREE );
			gameLocal.mpGame.RemoveAnnouncerSound( AS_GENERAL_TWO );
			gameLocal.mpGame.RemoveAnnouncerSound( AS_GENERAL_ONE );
			
			int warmupEndTime = gameLocal.time + ( gameLocal.serverInfo.GetInt( "si_countdown" ) * 1000 ) + 5000; 
			gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_PREPARE_TO_FIGHT, gameLocal.time + 5000 );
			gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_THREE, warmupEndTime - 3000 );
			gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_TWO, warmupEndTime - 2000 );
			gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_ONE, warmupEndTime - 1000 );

			localPlayer->vsMsgState = true;
			localPlayer->GUIMainNotice( va( "%s^0 vs. %s^0", thisArena.GetPlayerName( 0 ), thisArena.GetPlayerName( 1 ) ), true );
		}

		if( (thisArena.GetState() == AS_ROUND) && (thisArena.GetState() != previousArena.GetState()) ) {
  			if( localPlayer && localPlayer->GetArena() == thisArena.GetArenaID() ) {
  				gameLocal.mpGame.ScheduleAnnouncerSound( AS_GENERAL_FIGHT, gameLocal.time );
				gameLocal.mpGame.ScheduleTimeAnnouncements();
  			}
		}
	}

	if( ((rvTourneyGameState*)previousGameState)->currentState != currentState ) {
		if( currentState == WARMUP ) {
			gameLocal.mpGame.tourneyGUI.PreTourney();
		} else if( currentState == COUNTDOWN ) {	
			if( currentState == COUNTDOWN && ((rvTourneyGameState*)previousGameState)->currentState != INACTIVE ) {
				gameLocal.mpGame.ScheduleAnnouncerSound( AS_TOURNEY_START, gameLocal.time);
			}
		} else if( currentState == GAMEON ) {
			gameLocal.mpGame.tourneyGUI.TourneyStart();
		}
	}

	if( packTourneyHistory ) {
		// apply history
		gameLocal.mpGame.tourneyGUI.SetupTourneyHistory( startingRound, round - 1, tourneyHistory );
		packTourneyHistory = false;
	}

	if( localPlayer && localPlayer->spectating ) {

		rvTourneyArena& thisArena = arenas[ localPlayer->GetArena() ];
		if( thisArena.GetPlayers()[ 0 ] == NULL || thisArena.GetPlayers()[ 1 ] == NULL || (localPlayer->spectating && localPlayer->spectator != thisArena.GetPlayers()[ 0 ]->entityNumber && localPlayer->spectator != thisArena.GetPlayers()[ 1 ]->entityNumber) ) {
			gameLocal.mpGame.tourneyGUI.ArenaSelect( localPlayer->GetArena(), TGH_BRACKET );
		} else if ( thisArena.GetPlayers()[ 0 ] == localPlayer || (localPlayer->spectating && thisArena.GetPlayers()[ 0 ]->entityNumber == localPlayer->spectator) ) {
			gameLocal.mpGame.tourneyGUI.ArenaSelect( localPlayer->GetArena(), TGH_PLAYER_ONE );
		} else if ( thisArena.GetPlayers()[ 1 ] == localPlayer || (localPlayer->spectating && thisArena.GetPlayers()[ 1 ]->entityNumber == localPlayer->spectator) ) {
			gameLocal.mpGame.tourneyGUI.ArenaSelect( localPlayer->GetArena(), TGH_PLAYER_TWO );
		}
	}

}

/*
================
msgTourney_t
Identifiers for transmitted state
================
*/
typedef enum {
	MSG_TOURNEY_TOURNEYSTATE,
	MSG_TOURNEY_STARTINGROUND,
	MSG_TOURNEY_ROUND,
	MSG_TOURNEY_MAXROUND,
	MSG_TOURNEY_HISTORY,
	MSG_TOURNEY_TOURNEYCOUNT,
	MSG_TOURNEY_ARENAINFO
} msgTourney_t;

/*
================
rvTourneyGameState::PackState
================
*/
void rvTourneyGameState::PackState( idBitMsg& outMsg ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );
	
	if ( tourneyState != ((rvTourneyGameState*)previousGameState)->tourneyState ) {
		outMsg.WriteByte( MSG_TOURNEY_TOURNEYSTATE );
		outMsg.WriteByte( tourneyState );
	}

	if ( startingRound != ((rvTourneyGameState*)previousGameState)->startingRound ) {
		outMsg.WriteByte( MSG_TOURNEY_STARTINGROUND );
		outMsg.WriteByte( startingRound );
	}

	if( round != ((rvTourneyGameState*)previousGameState)->round ) {
		outMsg.WriteByte( MSG_TOURNEY_ROUND );
		// -1 is the reset sentinel; WriteChar keeps its existing 0xff wire
		// representation without overflowing the unsigned-byte writer.
		outMsg.WriteChar( round );
	}

	if( maxRound != ((rvTourneyGameState*)previousGameState)->maxRound ) {
		outMsg.WriteByte( MSG_TOURNEY_MAXROUND );
		outMsg.WriteByte( maxRound );
	}
	
	for ( int i = 0; i < MAX_ARENAS; i++ ) {
		if ( arenas[ i ] != ((rvTourneyGameState*)previousGameState)->arenas[ i ] ) {
			outMsg.WriteByte( MSG_TOURNEY_ARENAINFO + i );
			arenas[ i ].PackState( outMsg );
		}
	}

	if ( packTourneyHistory || forceTourneyHistory ) {
		if ( forceTourneyHistory ) {
			common->Warning( "forcing down tourney history\n" );
		}
		// don't write out uninitialized tourney history
		if ( startingRound > 0 && round > 1 ) {
			outMsg.WriteByte( MSG_TOURNEY_HISTORY );

			// client might not yet have startingRound or round
			outMsg.WriteByte( startingRound ); 
			outMsg.WriteChar( round ); 
			outMsg.WriteByte( maxRound );

			for( int i = startingRound - 1; i <= Min( (round - 1), (maxRound - 1) ); i++ ) {
				for( int j = 0; j < MAX_ARENAS / (i + 1); j++ ) {
					outMsg.WriteString( tourneyHistory[ i ][ j ].playerOne, MAX_TOURNEY_HISTORY_NAME_LEN );
					outMsg.WriteChar( idMath::ClampChar( tourneyHistory[ i ][ j ].playerOneScore ) );
					outMsg.WriteString( tourneyHistory[ i ][ j ].playerTwo, MAX_TOURNEY_HISTORY_NAME_LEN );
					outMsg.WriteChar( idMath::ClampChar( tourneyHistory[ i ][ j ].playerTwoScore ) );
					outMsg.WriteChar( tourneyHistory[ i ][ j ].playerOneNum );
					outMsg.WriteChar( tourneyHistory[ i ][ j ].playerTwoNum );
				}
			}
			packTourneyHistory = false;
		}
	}

	if ( tourneyCount != ((rvTourneyGameState*)previousGameState)->tourneyCount ) {
		outMsg.WriteByte( MSG_TOURNEY_TOURNEYCOUNT );
		outMsg.WriteByte( tourneyCount );
	}
}

/*
================
rvTourneyGameState::UnpackState
================
*/
void rvTourneyGameState::UnpackState( const idBitMsg& inMsg ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	while( inMsg.GetRemainingData() ) {
		int index = inMsg.ReadByte();
		
		switch( index ) {
			case MSG_TOURNEY_TOURNEYSTATE: {
				tourneyState = (tourneyState_t)inMsg.ReadByte();
				break;
			}
			case MSG_TOURNEY_STARTINGROUND: {
				startingRound = inMsg.ReadByte();
				break;
			}
			case MSG_TOURNEY_ROUND: {
				round = inMsg.ReadByte();
				// not a great way of doing this, should clamp the values
				if ( round == 255 ) {
					round = -1;
				}
				break;
			}
			case MSG_TOURNEY_MAXROUND: {
				maxRound = inMsg.ReadByte();
				if ( maxRound == 255 ) {
					maxRound = -1;
				}
				break;
			}
			case MSG_TOURNEY_HISTORY: {
				int startRound = inMsg.ReadByte();
				int rnd = inMsg.ReadChar();
				int maxr = inMsg.ReadByte();
				
				// openQ4: these index tourneyHistory's rounds, of which there are
				// MAX_ROUNDS. The check used MAX_ARENAS, twice as many, so a server
				// or demo could write four rows of names past the array.
				if ( startRound < 1 || startRound > MAX_ROUNDS ||
					 rnd < 1 || rnd > MAX_ROUNDS ||
					 maxr < 1 || maxr > MAX_ROUNDS ) {
					common->Warning( "Ignoring invalid tourney history range %d/%d/%d",
						startRound, rnd, maxr );
					break;
				}

				for( int i = startRound - 1; i <= Min( (rnd - 1), (maxr - 1) ); i++ ) {
					for( int j = 0; j < MAX_ARENAS / (i + 1); j++ ) {
						inMsg.ReadString( tourneyHistory[ i ][ j ].playerOne, MAX_TOURNEY_HISTORY_NAME_LEN );
						tourneyHistory[ i ][ j ].playerOneScore = inMsg.ReadChar();
						inMsg.ReadString( tourneyHistory[ i ][ j ].playerTwo, MAX_TOURNEY_HISTORY_NAME_LEN );
						tourneyHistory[ i ][ j ].playerTwoScore = inMsg.ReadChar();
						tourneyHistory[ i ][ j ].playerOneNum = inMsg.ReadChar();
						tourneyHistory[ i ][ j ].playerTwoNum = inMsg.ReadChar();

						if( tourneyHistory[ i ][ j ].playerOneNum < 0 || tourneyHistory[ i ][ j ].playerOneNum >= MAX_CLIENTS ) {
							tourneyHistory[ i ][ j ].playerOneNum = -1;
						}

						if( tourneyHistory[ i ][ j ].playerTwoNum < 0 || tourneyHistory[ i ][ j ].playerTwoNum >= MAX_CLIENTS ) {
							tourneyHistory[ i ][ j ].playerTwoNum = -1;
						}
					}
				}

				// client side (and listen server) no reason to check for history change all the time
				packTourneyHistory = true;
				break;
			}
			case MSG_TOURNEY_TOURNEYCOUNT: {
				tourneyCount = inMsg.ReadByte();
				break;
			}
			default: {
				if ( index >= MSG_TOURNEY_ARENAINFO && index < MSG_TOURNEY_ARENAINFO + MAX_ARENAS ) {
					arenas[ index - MSG_TOURNEY_ARENAINFO ].UnpackState( inMsg );
				} else {
					gameLocal.Error( "rvTourneyGameState::UnpackState() - Unknown data identifier '%d'\n", index );
				}
				break;
			}
		}

	}
}

/*
================
rvTourneyGameState::SendState
================
*/
void rvTourneyGameState::SendState( const idMessageSender &sender, int clientNum ) {
	idBitMsg	outMsg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];

	assert( (gameLocal.isServer || gameLocal.isRepeater) && trackPrevious && IsType( rvTourneyGameState::GetClassType() ) );

	if ( clientNum == -1 && (rvTourneyGameState&)(*this) == (rvTourneyGameState&)(*previousGameState) ) {
		return;
	}

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_GAMESTATE );

	WriteState( outMsg );

	if( clientNum == -1 && forceTourneyHistory ) {
		forceTourneyHistory = false;
	}

	sender.Send( outMsg );

	// don't update the state if we are working for a single client
	if ( clientNum == -1 ) {
		outMsg.ReadByte(); // pop off the msg ID
		ReceiveState( outMsg );
	}
}

/*
===============
rvTourneyGameState::WriteState
===============
*/
void rvTourneyGameState::WriteState( idBitMsg &msg ) {
	// send off base info
	rvGameState::PackState( msg );
	// add Tourney info
	PackState( msg );
}

/*
================
rvTourneyGameState::ReceiveState
================
*/
void rvTourneyGameState::ReceiveState( const idBitMsg& msg ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	if ( !rvGameState::BaseUnpackState( msg ) ) {
		return;
	}
	UnpackState( msg );

	if ( gameLocal.localClientNum >= 0 ) {
		GameStateChanged();
	}

	(rvTourneyGameState&)(*previousGameState) = (rvTourneyGameState&)(*this);
}

/*
================
rvTourneyGameState::SendInitialState
================
*/
void rvTourneyGameState::SendInitialState( const idMessageSender &sender, int clientNum ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	rvTourneyGameState* previousState = (rvTourneyGameState*)previousGameState;

	rvTourneyGameState invalidState;

	previousGameState = &invalidState;

	
	// if the tourney has been going on, transmit the tourney history
	if( round > 0 ) {
		// comparing the tourney history for all state changes is wasteful when we really just want to send it to new clients
		packTourneyHistory = true;
	}
	
	SendState( sender, clientNum );

	previousGameState = previousState;
}


/*
================
rvTourneyGameState::operator==
================
*/
bool rvTourneyGameState::operator==( const rvTourneyGameState& rhs ) const {
	assert( IsType( rvTourneyGameState::GetClassType() ) && rhs.IsType( rvTourneyGameState::GetClassType() ) );

	if( (rvGameState&)(*this) != (rvGameState&)rhs ) {
		return false;
	}

	if( round != rhs.round || startingRound != rhs.startingRound || maxRound != rhs.maxRound || tourneyState != rhs.tourneyState ) {
		return false;
	}

	for( int i = 0; i < MAX_ARENAS; i++ ) {
		if( arenas[ i ] != rhs.arenas[ i ] ) {
			return false;
		}
	}

	return true;
}

/*
================
rvTourneyGameState::operator=
================
*/
rvTourneyGameState& rvTourneyGameState::operator=( const rvTourneyGameState& rhs ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) && rhs.IsType( rvTourneyGameState::GetClassType() ) );

	(rvGameState&)(*this) = (rvGameState&)rhs;

	round = rhs.round;
	startingRound = rhs.startingRound;
	maxRound = rhs.maxRound;
	tourneyState = rhs.tourneyState;

	for( int i = 0; i < MAX_ARENAS; i++ ) {
		arenas[ i ] = rhs.arenas[ i ];
	}
	
	return (*this);
}

/*
================
rvTourneyGameState::GetNumArenas
Returns number of active arenas
================
*/
int rvTourneyGameState::GetNumArenas( void ) const {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	int num = 0;
	
	for( int i = 0; i < MAX_ARENAS; i++ ) {
		if( arenas[ i ].GetState() != AS_INACTIVE && arenas[ i ].GetState() != AS_DONE ) {
			num++;
		}
	}

	return num;
}

/*
================
rvTourneyGameState::SetupInitialBrackets

Sets up the brackets for a new match.  fragCount in playerstate is the player's
persistant frag count over an entire level.  In teamFragCount we store this
rounds score.

Initial bracket seeds are random
================
*/
void rvTourneyGameState::SetupInitialBrackets( void ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );
	idList<idPlayer*> unseededPlayers;
	int numRankedPlayers = gameLocal.mpGame.GetNumRankedPlayers();

	// all this crazy math does is figure out: 
	//	8 arenas to 4 rounds ( round 1: 8 arenas, round 2: 4 arenas, round 3: 2 arenas, round 4: 1 arena )
	//	16 arenas to 5 rounds ( round 1: 16 arenas, round 2: 8 arenas, round 3: 4 arenas, round 4: 2 arenas, round 5: 1 arena )
	//  etc
	int newMaxRound = idMath::ILog2( Max( idMath::CeilPowerOfTwo( MAX_ARENAS * 2 ), 2 ) );

	// we start at a round appropriate for our # of people
	// If you have 1-2 players, start in maxRound,  if you have 3-4 players start in maxRound - 1, if you have 5-8 players
	// start in maxRound - 2, if you have 9 - 16 players start in maxRound - 3, etc.
	int newRound = newMaxRound - idMath::ILog2( Max( idMath::CeilPowerOfTwo( gameLocal.mpGame.GetNumRankedPlayers() ), 2 ) ) + 1;

	round = newRound;
	maxRound = newMaxRound;
	startingRound = round;

	for( int i = 0; i < numRankedPlayers; i++ ) {
		unseededPlayers.Append( gameLocal.mpGame.GetRankedPlayer( i ) );
	}

	int numArenas = 0;

	while ( unseededPlayers.Num() > 1 ) {
		idPlayer* playerOne = unseededPlayers[ gameLocal.random.RandomInt( unseededPlayers.Num() ) ];
		unseededPlayers.Remove( playerOne );

		idPlayer* playerTwo = unseededPlayers[ gameLocal.random.RandomInt( unseededPlayers.Num() ) ];
		unseededPlayers.Remove( playerTwo );

		rvTourneyArena& arena = arenas[ numArenas ];

		arena.Clear();
		arena.AddPlayers( playerOne, playerTwo );

		// place the players in the correct instance
		playerOne->JoinInstance( numArenas );
		playerTwo->JoinInstance( numArenas );

		playerOne->ServerSpectate( false );
		playerTwo->ServerSpectate( false );

		gameLocal.mpGame.SetPlayerTeamScore( playerOne, 0 );
		gameLocal.mpGame.SetPlayerTeamScore( playerTwo, 0 );

		gameLocal.Printf( "rvTourneyGameState::SetupInitialBrackets() - %s will face %s in arena %d\n", playerOne->GetUserInfo()->GetString( "ui_name" ), playerTwo->GetUserInfo()->GetString( "ui_name" ), numArenas );

		// this arena is ready to play
		arena.Ready();

		numArenas++;
	}

	assert( unseededPlayers.Num() == 0 || unseededPlayers.Num() == 1 );

	if( unseededPlayers.Num() ) {
		assert( unseededPlayers[ 0 ] );
		// the mid player gets a bye
		unseededPlayers[ 0 ]->SetTourneyStatus( PTS_UNKNOWN );
		unseededPlayers[ 0 ]->ServerSpectate( true );

		arenas[ numArenas ].AddPlayers( unseededPlayers[ 0 ], NULL );
		arenas[ numArenas ].Ready();

		// mark the ancestor arena of the bye player
		byeArena = numArenas * startingRound;
	} else {
		byeArena = -1;
	}
}

/*
================
rvTourneyGameState::ClientDisconnect
A player has disconnected from the server
================
*/
void rvTourneyGameState::ClientDisconnect( idPlayer* player ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );
	if( gameLocal.isClient ) {
		return;
	}
	
	// go through the tourney history and copy over the disconnecting player's name
	if( startingRound > 0 && round <= MAX_ROUNDS ) {
		for( int i = startingRound - 1; i < round - 1; i++ ) {
			for( int j = 0; j < MAX_ARENAS / (i + 1); j++ ) {
				if( tourneyHistory[ i ][ j ].playerOneNum == player->entityNumber ) {
					idStr::Copynz( tourneyHistory[ i ][ j ].playerOne, player->GetUserInfo()->GetString( "ui_name" ), MAX_TOURNEY_HISTORY_NAME_LEN );
					tourneyHistory[ i ][ j ].playerOneNum = -1;
				} else if( tourneyHistory[ i ][ j ].playerTwoNum == player->entityNumber ) {
					idStr::Copynz( tourneyHistory[ i ][ j ].playerTwo, player->GetUserInfo()->GetString( "ui_name" ), MAX_TOURNEY_HISTORY_NAME_LEN );
					tourneyHistory[ i ][ j ].playerTwoNum = -1;
				}
			}
		}

		// copy over the current rounds info if the player is playing
		for( int i = 0; i < MAX_ARENAS; i++ ) {
			if( arenas[ i ].GetPlayers()[ 0 ] == player ) {
				tourneyHistory[ round - 1 ][ i ].playerOneNum = -1;
				idStr::Copynz( tourneyHistory[ round - 1 ][ i ].playerOne, player->GetUserInfo()->GetString( "ui_name" ), MAX_TOURNEY_HISTORY_NAME_LEN );
				tourneyHistory[ round - 1 ][ i ].playerOneScore = gameLocal.mpGame.GetTeamScore( player );
			} else if( arenas[ i ].GetPlayers()[ 1 ] == player ) {
				tourneyHistory[ round - 1 ][ i ].playerTwoNum = -1;
				idStr::Copynz( tourneyHistory[ round - 1 ][ i ].playerTwo, player->GetUserInfo()->GetString( "ui_name" ), MAX_TOURNEY_HISTORY_NAME_LEN );
				tourneyHistory[ round - 1 ][ i ].playerTwoScore = gameLocal.mpGame.GetTeamScore( player );
			}
		}

		// retransmit tourney history to everyone
		packTourneyHistory = true;
	}

	RemovePlayer( player );
	
	// give RemovePlayer() a chance to update tourney history if needed
	if( packTourneyHistory ) {
		for( int i = 0; i < gameLocal.numClients; i++ ) {
			if( i == player->entityNumber ) {
				continue;
			}

			if( gameLocal.entities[ i ] ) {
				packTourneyHistory = true;
				SendState( serverReliableSender.To( i, true ), i );
			}
		}

		if ( gameLocal.isRepeater ) {
			packTourneyHistory = true;
			SendState( repeaterReliableSender.To( -1 ), ENTITYNUM_NONE );
		}
		packTourneyHistory = false;
	}
}

/*
================
rvTourneyGameState::Spectate
================
*/
void rvTourneyGameState::Spectate( idPlayer* player ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );
	assert( gameLocal.isServer || player->IsFakeClient() );

	if( player->spectating && player->wantSpectate ) {
		RemovePlayer( player );
	}
}

/*
================
rvTourneyGameState::RemovePlayer
Removes the specified player from the arena
================
*/
void rvTourneyGameState::RemovePlayer( idPlayer* player ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	if( round < 1 || round > maxRound ) {
		return;
	}

	for( int i = 0; i < MAX_ARENAS; i++ ) {
		idPlayer** players = GetArenaPlayers( i );

		if( players[ 0 ] == player || players[ 1 ] == player ) {
			rvTourneyArena& arena = arenas[ i ];

			idPlayer* remainingPlayer = players[ 0 ] == player ? players[ 1 ] : players[ 0 ];

			bool arenaInProgress = arena.GetState() == AS_ROUND || arena.GetState() == AS_WARMUP || arena.GetState() == AS_SUDDEN_DEATH;		
			bool remainingIsWinner = (remainingPlayer == arena.GetWinner());
			int remainingIndex = (remainingPlayer == arena.GetPlayers()[ 0 ]) ? 0 : 1;

			arena.RemovePlayer( player );

			// both players have disconnected from this arena, or the winner has disconnected, just return
			if( (!arena.GetPlayers()[ 0 ] && !arena.GetPlayers()[ 1 ]) || (!arenaInProgress && remainingPlayer && !remainingIsWinner) ) {
				arena.Clear();
				tourneyHistory[ round - 1 ][ i ].playerOneNum = -1;
				tourneyHistory[ round - 1 ][ i ].playerOne[ 0 ] = '\0';
				tourneyHistory[ round - 1 ][ i ].playerTwoNum = -1;
				tourneyHistory[ round - 1 ][ i ].playerTwo[ 0 ] = '\0';
				return;
			}

			assert( remainingPlayer );

			// if this arena is in progress, try restarting
			// ATVI DevTrack #13196 - do not put the bye player into a game with the abandonned player anymore
			//			if ( arenaInProgress && byeArena >= 0 && arenas[ byeArena / round ].GetWinner() ) {
			if ( 0 ) {
				idPlayer* byePlayer = arenas[ byeArena / round ].GetWinner();

				// reset history for this new round
				tourneyHistory[ round - 1 ][ i ].playerOneNum = -1;
				tourneyHistory[ round - 1 ][ i ].playerOne[ 0 ] = '\0';
				tourneyHistory[ round - 1 ][ i ].playerTwoNum = -1;
				tourneyHistory[ round - 1 ][ i ].playerTwo[ 0 ] = '\0';

				arena.Clear();
				arenas[ byeArena / round ].Clear();

				arena.AddPlayers( remainingPlayer, byePlayer );

				// place the players in the correct instance
				remainingPlayer->JoinInstance( i );
				byePlayer->JoinInstance( i );

				remainingPlayer->ServerSpectate( false );
				byePlayer->ServerSpectate( false );

				gameLocal.mpGame.SetPlayerTeamScore( remainingPlayer, 0 );
				gameLocal.mpGame.SetPlayerTeamScore( byePlayer, 0 );

				gameLocal.Printf( "rvTourneyManager::RemovePlayer() - %s will face %s in arena %d\n", remainingPlayer->GetUserInfo()->GetString( "ui_name" ), byePlayer->GetUserInfo()->GetString( "ui_name" ), i );

				byeArena = -1;
				// this arena is ready to play
				arena.Ready();
			} else {
				// if the arena was in progress and didn't get a bye player to step in, this becomes a bye
				// arena - clear the tourney history
				if( arenaInProgress ) {
					tourneyHistory[ round - 1 ][ i ].playerOneNum = -1;
					tourneyHistory[ round - 1 ][ i ].playerOne[ 0 ] = '\0';
					tourneyHistory[ round - 1 ][ i ].playerOneScore = -1;

					tourneyHistory[ round - 1 ][ i ].playerTwoNum = -1;
					tourneyHistory[ round - 1 ][ i ].playerTwo[ 0 ] = '\0';
					tourneyHistory[ round - 1 ][ i ].playerTwoScore = -1;
				} else {
					// since the player is disconnecting, write the history for this round
					if( remainingIndex == 0 ) {
						tourneyHistory[ round - 1 ][ i ].playerOneNum = remainingPlayer->entityNumber;
						tourneyHistory[ round - 1 ][ i ].playerOne[ 0 ] = '\0';
						tourneyHistory[ round - 1 ][ i ].playerOneScore = gameLocal.mpGame.GetTeamScore( remainingPlayer );
					} else {
						tourneyHistory[ round - 1 ][ i ].playerTwoNum = remainingPlayer->entityNumber;
						tourneyHistory[ round - 1 ][ i ].playerTwo[ 0 ] = '\0';
						tourneyHistory[ round - 1 ][ i ].playerTwoScore = gameLocal.mpGame.GetTeamScore( remainingPlayer );
					}
				}
			}

			return;
		}
	}
}

int rvTourneyGameState::GetNextActiveArena( int arena ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	for( int i = arena + 1; i < MAX_ARENAS; i++ ) {
		if( arenas[ i ].GetState() != AS_INACTIVE && arenas[ i ].GetState() != AS_DONE ) {
			return i;
		}
	}

	for( int i = 0; i < arena; i++ ) {
		if( arenas[ i ].GetState() != AS_INACTIVE && arenas[ i ].GetState() != AS_DONE ) {
			return i;
		}
	}

	return arena;
}

int rvTourneyGameState::GetPrevActiveArena( int arena ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	for( int i = arena - 1; i >= 0; i-- ) {
		if( arenas[ i ].GetState() != AS_INACTIVE && arenas[ i ].GetState() != AS_DONE ) {
			return i;
		}
	}

	for( int i = MAX_ARENAS - 1; i > arena; i-- ) {
		if( arenas[ i ].GetState() != AS_INACTIVE && arenas[ i ].GetState() != AS_DONE ) {
			return i;
		}
	}

	return arena;
}

/*
================
rvTourneyGameState::CycleSpectatorTarget

Walk the fixed arena/seat space in display order, skipping inactive brackets
and every camera target rejected by the same fresh disclosure gate used by the
normal spectator path.  The bounded scan cannot loop forever when all targets
are private or stale.
================
*/
bool rvTourneyGameState::CycleSpectatorTarget( idPlayer* player,
		int direction ) {
	if ( player == NULL || ( direction != 1 && direction != -1 ) ) {
		return false;
	}

	int currentArena = player->GetArena();
	if ( currentArena < 0 || currentArena >= MAX_ARENAS ) {
		currentArena = 0;
	}
	int currentSeat = -1;
	idPlayer **currentPlayers = arenas[ currentArena ].GetPlayers();
	for ( int seat = 0; seat < 2; ++seat ) {
		if ( currentPlayers[ seat ] != NULL &&
			player->spectator == currentPlayers[ seat ]->entityNumber ) {
			currentSeat = seat;
			break;
		}
	}

	const int targetCount = MAX_ARENAS * 2;
	const int baseTarget = currentSeat >= 0 ?
		currentArena * 2 + currentSeat :
		( direction > 0 ? currentArena * 2 - 1 : currentArena * 2 );
	const bool mayChangeArena = player->GetArena() < 0 ||
		player->GetArena() >= MAX_ARENAS ||
		gameLocal.time > player->lastArenaChange;

	for ( int step = 1; step <= targetCount; ++step ) {
		int flatTarget = ( baseTarget + direction * step ) % targetCount;
		if ( flatTarget < 0 ) {
			flatTarget += targetCount;
		}
		const int targetArena = flatTarget / 2;
		const int targetSeat = flatTarget % 2;
		if ( arenas[ targetArena ].GetState() == AS_INACTIVE ||
			arenas[ targetArena ].GetState() == AS_DONE ||
			( targetArena != player->GetArena() && !mayChangeArena ) ) {
			continue;
		}
		idPlayer *target = arenas[ targetArena ].GetPlayers()[ targetSeat ];
		if ( target == NULL || target->spectating ||
			!gameLocal.mpGame.CanSpectatorFollow( player->entityNumber,
				target->entityNumber ) ) {
			continue;
		}

		if ( targetArena != player->GetArena() ) {
			player->JoinInstance( targetArena );
			player->lastArenaChange = gameLocal.time + 2000;
		}
		player->spectator = target->entityNumber;
		return true;
	}

	// A private, disconnected or empty bracket must never leave a stale POV.
	player->spectator = player->entityNumber;
	return false;
}

void rvTourneyGameState::SpectateCycleNext( idPlayer* player ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );
	assert( gameLocal.isServer || player->IsFakeClient() );
	CycleSpectatorTarget( player, 1 );

	// this is where the listen server updates it's gui spectating elements
	if ( gameLocal.GetLocalPlayer() == player ) {
		rvTourneyArena& arena = arenas[ player->GetArena() ];

		if( arena.GetPlayers()[ 0 ] == NULL || arena.GetPlayers()[ 1 ] == NULL || (player->spectating && player->spectator != arena.GetPlayers()[ 0 ]->entityNumber && player->spectator != arena.GetPlayers()[ 1 ]->entityNumber) ) {
			gameLocal.mpGame.tourneyGUI.ArenaSelect( player->GetArena(), TGH_BRACKET );
		} else if( arena.GetPlayers()[ 0 ] == player || player->spectator == arena.GetPlayers()[ 0 ]->entityNumber ) {
			gameLocal.mpGame.tourneyGUI.ArenaSelect( player->GetArena(), TGH_PLAYER_ONE );
		} else if( arena.GetPlayers()[ 1 ] == player || player->spectator == arena.GetPlayers()[ 1 ]->entityNumber ) {
			gameLocal.mpGame.tourneyGUI.ArenaSelect( player->GetArena(), TGH_PLAYER_TWO );
		}

		gameLocal.mpGame.tourneyGUI.UpdateScores();
	}
}

void rvTourneyGameState::SpectateCyclePrev( idPlayer* player ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );
	assert( gameLocal.isServer || player->IsFakeClient() );
	CycleSpectatorTarget( player, -1 );

	// this is where the listen server updates it gui spectating elements
	if( gameLocal.GetLocalPlayer() == player ) {
		rvTourneyArena& arena = arenas[ player->GetArena() ];

		if( arena.GetPlayers()[ 0 ] == NULL || arena.GetPlayers()[ 1 ] == NULL || (player->spectating && player->spectator != arena.GetPlayers()[ 0 ]->entityNumber && player->spectator != arena.GetPlayers()[ 1 ]->entityNumber) ) {
			gameLocal.mpGame.tourneyGUI.ArenaSelect( player->GetArena(), TGH_BRACKET );
		} else if( arena.GetPlayers()[ 0 ] == player || player->spectator == arena.GetPlayers()[ 0 ]->entityNumber ) {
			gameLocal.mpGame.tourneyGUI.ArenaSelect( player->GetArena(), TGH_PLAYER_ONE );
		} else if( arena.GetPlayers()[ 1 ] == player || player->spectator == arena.GetPlayers()[ 1 ]->entityNumber ) {
			gameLocal.mpGame.tourneyGUI.ArenaSelect( player->GetArena(), TGH_PLAYER_TWO );
		}

		gameLocal.mpGame.tourneyGUI.UpdateScores();
	}
}

void rvTourneyGameState::UpdateTourneyBrackets( void ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	gameLocal.mpGame.tourneyGUI.SetupTourneyHistory( startingRound, round - 1, tourneyHistory );
}

void rvTourneyGameState::UpdateTourneyHistory( int round ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	if ( round >= MAX_ROUNDS ) {
		assert( false );
		gameLocal.Error( "rvTourneyGameState::UpdateTourneyHistory: MAX_ROUNDS exceeded" );
		return;
	}

	for( int i = 0; i < MAX_ARENAS; i++ ) {
		// sometimes tourney history might have been updated for the current
		// round, so don't clobber any data

		if( (tourneyHistory[ round ][ i ].playerOne[ 0 ] != '\0' || tourneyHistory[ round ][ i ].playerOneNum != -1) ||
			(tourneyHistory[ round ][ i ].playerTwo[ 0 ] != '\0' || tourneyHistory[ round ][ i ].playerTwoNum != -1) ) {
			continue;
		}
		
		tourneyHistory[ round ][ i ].playerOne[ 0 ] = '\0';
		tourneyHistory[ round ][ i ].playerOneNum = -1;
		tourneyHistory[ round ][ i ].playerOneScore = 0;

		tourneyHistory[ round ][ i ].playerTwo[ 0 ] = '\0';
		tourneyHistory[ round ][ i ].playerTwoNum = -1;
		tourneyHistory[ round ][ i ].playerTwoScore = 0;

		if( arenas[ i ].GetState() == AS_DONE ) {
			idPlayer* playerOne = arenas[ i ].GetPlayers()[ 0 ];
			idPlayer* playerTwo = arenas[ i ].GetPlayers()[ 1 ];

			if( playerOne ) {
				tourneyHistory[ round ][ i ].playerOneNum = playerOne->entityNumber;
				tourneyHistory[ round ][ i ].playerOne[ MAX_TOURNEY_HISTORY_NAME_LEN - 1 ] = '\0';
				tourneyHistory[ round ][ i ].playerOneScore = gameLocal.mpGame.GetTeamScore( playerOne );
			}

			if( playerTwo ) {
				tourneyHistory[ round ][ i ].playerTwoNum = playerTwo->entityNumber;
				tourneyHistory[ round ][ i ].playerTwo[ MAX_TOURNEY_HISTORY_NAME_LEN - 1 ] = '\0';
				tourneyHistory[ round ][ i ].playerTwoScore = gameLocal.mpGame.GetTeamScore( playerTwo );
			}
		} 
	}
}


/*
================
rvTourneyGameState::FirstAvailableArena
Returns the first non-WARMUP arena available for use in the next round
================
*/
int rvTourneyGameState::FirstAvailableArena( void ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	for( int i = 0; i < MAX_ARENAS; i++ ) {
		if( arenas[ i ].GetState() != AS_WARMUP ) {
			return i;
		}
	}

	// no available arenas
	assert( 0 );
	return 0;
}

arenaOutcome_t* rvTourneyGameState::GetArenaOutcome( int arena ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	if( arenas[ arena ].GetState() != AS_DONE || (arenas[ arena ].GetPlayers()[ 0 ] && arenas[ arena ].GetPlayers()[ 1 ]) ) {
		return NULL;
	}

	return &(tourneyHistory[ round - 1 ][ arena ]);
}

bool rvTourneyGameState::HasBye( idPlayer* player ) {
	assert( IsType( rvTourneyGameState::GetClassType() ) );

	if( player == NULL || player->GetArena() < 0 || player->GetArena() >= MAX_ARENAS ) {
		return false;
	}

	// if we're one of the players in the arena we're in, and the other player is NULL, we have a bye
	if( (arenas[ player->GetArena() ].GetPlayers()[ 0 ] == player || arenas[ player->GetArena() ].GetPlayers()[ 1 ] == player) &&
		(arenas[ player->GetArena() ].GetPlayers()[ 0 ] == NULL || arenas[ player->GetArena() ].GetPlayers()[ 1 ] == NULL) ) {
		return true;
	}

	return false;
}

// simple RTTI
gameStateType_t rvGameState::type = GS_BASE;
gameStateType_t rvDMGameState::type = GS_DM;
gameStateType_t rvTeamDMGameState::type = GS_TEAMDM;
gameStateType_t rvCTFGameState::type = GS_CTF;
gameStateType_t rvTourneyGameState::type = GS_TOURNEY;
gameStateType_t riDZGameState::type = GS_DZ;

bool rvGameState::IsType( gameStateType_t type ) const {
	return ( type == rvGameState::type );
}

bool rvDMGameState::IsType( gameStateType_t type ) const {
	return ( type == rvDMGameState::type );
}

bool rvTeamDMGameState::IsType( gameStateType_t type ) const {
	return ( type == rvTeamDMGameState::type );
}

bool rvCTFGameState::IsType( gameStateType_t type ) const {
	return ( type == rvCTFGameState::type );
}

bool rvTourneyGameState::IsType( gameStateType_t type ) const {
	return ( type == rvTourneyGameState::type );
}

gameStateType_t rvGameState::GetClassType( void ) {
	return rvGameState::type;
}

gameStateType_t rvDMGameState::GetClassType( void ) {
	return rvDMGameState::type;
}

gameStateType_t rvTeamDMGameState::GetClassType( void ) {
	return rvTeamDMGameState::type;
}

gameStateType_t rvCTFGameState::GetClassType( void ) {
	return rvCTFGameState::type;
}

gameStateType_t rvTourneyGameState::GetClassType( void ) {
	return rvTourneyGameState::type;
}

bool riDZGameState::IsType( gameStateType_t type ) const {
	return ( type == riDZGameState::type );
}

gameStateType_t riDZGameState::GetClassType( void ) {
	return riDZGameState::type;
}

/*
===============================================================================

riDZGameState

Game state info for Dead Zone

===============================================================================
*/

/*
================
riDZGameState::riDZGameState
================
*/
riDZGameState::riDZGameState( bool allocPrevious ) : rvGameState( false ) {
	Clear();

	if( allocPrevious ) {
		previousGameState = new riDZGameState( false );
	} else {
		previousGameState = NULL;
	}

	trackPrevious = allocPrevious;
}

/*
================
riDZGameState::~riDZGameState
================
*/
riDZGameState::~riDZGameState( void ) {
	Clear();
	delete previousGameState;
	previousGameState = NULL;
}

/*
================
riDZGameState::Clear
================
*/
void riDZGameState::Clear( void ) {
	rvGameState::Clear();

	if ( previousGameState ) {
		riDZGameState* prevState = (riDZGameState*)previousGameState;
		prevState->Clear( );
	}	

	for( int i = 0; i < TEAM_MAX; i++ ) {
		dzStatus[ i ].state = DZ_NONE;
		dzStatus[ i ].clientNum = -1;
	}

	dzTriggerEnt = -1;
}

/*
================
riDZGameState::SendState
================
*/
void riDZGameState::SendState( const idMessageSender &sender, int clientNum ) {
	idBitMsg	outMsg;
	byte		msgBuf[MAX_GAME_MESSAGE_SIZE];

	assert( (gameLocal.isServer || gameLocal.isRepeater) && trackPrevious && type == GS_DZ );

	if( clientNum == -1 && (riDZGameState&)(*this) == (riDZGameState&)(*previousGameState) ) {
		return;
	}

	outMsg.Init( msgBuf, sizeof( msgBuf ) );
	outMsg.WriteByte( GAME_RELIABLE_MESSAGE_GAMESTATE );

	WriteState( outMsg );

	sender.Send( outMsg );

	// don't update the state if we are working for a single client
	if ( clientNum == -1 ) {
		outMsg.ReadByte(); // pop off the msg ID
		ReceiveState( outMsg );
	}
}

/*
===============
riDZGameState::WriteState
===============
*/
void riDZGameState::WriteState( idBitMsg &msg ) {
	// send off base info
	rvGameState::PackState( msg );
	// add DZ info
	PackState( msg );
}

/*
================
riDZGameState::ReceiveState
================
*/
void riDZGameState::ReceiveState( const idBitMsg& msg ) {
	assert( type == GS_DZ );

	if ( !rvGameState::BaseUnpackState( msg ) ) {
		return;
	}
	UnpackState( msg );

	if ( gameLocal.localClientNum >= 0 ) {
		GameStateChanged();
	}

	(riDZGameState&)(*previousGameState) = (riDZGameState&)(*this);
}

/*
================
riDZGameState::PackState
================
*/
void riDZGameState::PackState( idBitMsg& outMsg ) {
	int i;
	for( i = 0; i < TEAM_MAX; i++ ) {
		outMsg.WriteByte( dzStatus[ i ].state );
		outMsg.WriteBits( dzStatus[ i ].clientNum, -( idMath::BitsForInteger( MAX_CLIENTS ) + 1 ) );
	}

	outMsg.WriteLong(dzTriggerEnt);
	outMsg.WriteLong(dzShaderParm);
}

/*
================
riDZGameState::UnpackState
================
*/
void riDZGameState::UnpackState( const idBitMsg& inMsg ) {
	int i;
	for( i = 0; i < TEAM_MAX; i++ ) {
		dzStatus[ i ].state = (dzState_t)inMsg.ReadByte();
		dzStatus[ i ].clientNum = inMsg.ReadBits( -( idMath::BitsForInteger( MAX_CLIENTS ) + 1 ) );
	}
	dzTriggerEnt = inMsg.ReadLong();
	dzShaderParm = inMsg.ReadLong();	
}

/*
================
riDZGameState::SendInitialState
================
*/
void riDZGameState::SendInitialState( const idMessageSender &sender,  int clientNum ) {
	assert( type == GS_DZ );

	riDZGameState* previousState = (riDZGameState*)previousGameState;

	riDZGameState invalidState;

	previousGameState = &invalidState;

	SendState( sender, clientNum );

	previousGameState = previousState;
}


/*
================
riDZGameState::ControlZoneStateChanged
================
*/
void riDZGameState::ControlZoneStateChanged( int team ) {
	if( !gameLocal.isClient && !gameLocal.isListenServer ) {
		return;
	}
	
	idPlayer* player = gameLocal.GetLocalPlayer();

	if( player == NULL ) {
		return;
	}

	if ( dzTriggerEnt < 0 )
		return;

	idEntity* ent = gameLocal.FindEntity(dzTriggerEnt);
	if ( ent ) {
		ent->SetShaderParm(7, dzShaderParm);
	}

	dzTriggerEnt = -1;
}


/*
================
riDZGameState::GameStateChanged
================
*/
void riDZGameState::GameStateChanged( void ) {
	// detect any base state changes
	rvGameState::GameStateChanged();

	// DZ specific stuff
	idPlayer* player = gameLocal.GetLocalPlayer();

	if( player == NULL ) {
		//gameLocal.Error( "riDZGameState::GameStateChanged() - NULL local player\n" );
		return;
	}

	if( currentState == GAMEREVIEW )
	{
		// Need to clear the deadzone state at this point.
		((riDZGameState*)previousGameState)->Clear();
		for( int i = 0; i < TEAM_MAX; i++ ) {
			dzStatus[ i ].state = DZ_NONE;
			dzStatus[ i ].clientNum = -1;
		}
	}

	for( int i = 0; i < TEAM_MAX; i++ ) {
		if( dzStatus[ i ] == ((riDZGameState*)previousGameState)->dzStatus[ i ] ) {
			continue;
		}

		ControlZoneStateChanged(i);
	}
}

/*
================
riDZGameState::Run
================
*/
void riDZGameState::Run( void ) {
	// run common stuff	
	rvGameState::Run();

	switch( currentState ) {
		case GAMEON:
		{
 			/// Check if we're frozen (in buy mode, etc.)
 			if( gameLocal.GetIsFrozen() )
 			{
 				/// Check if it's time for freeze to wear off
 				int unFreezeTime = gameLocal.GetUnFreezeTime();
 				if( gameLocal.time >= unFreezeTime )
 				{
 					gameLocal.SetIsFrozen( false );
 					// FIXME: say "fight"
 				}
 			}
 			else
 			{
				/// Check if time limit has been reached
 				if ( gameLocal.mpGame.TimeLimitHit() )
				{
					int marineTeamScore = gameLocal.mpGame.GetScoreForTeam( TEAM_MARINE );
					int stroggTeamScore = gameLocal.mpGame.GetScoreForTeam( TEAM_STROGG );

					gameLocal.mpGame.PrintMessageEvent( -1, MSG_TIMELIMIT );
					if( marineTeamScore > stroggTeamScore )
					{
						/// Marines win!
	 					gameLocal.mpGame.OnDeadZoneTeamVictory( TEAM_MARINE );
					}
					else if( marineTeamScore < stroggTeamScore )
					{
						/// Strogg win!
	 					gameLocal.mpGame.OnDeadZoneTeamVictory( TEAM_STROGG );
					}
					else
					{
						/// Teams are tied and time limit was hit - go to sudden death!
						fragLimitTimeout = 0;
						NewState( SUDDENDEATH );
					}
				}
 			}

			break;
		}

		case SUDDENDEATH:
		{
			int marineTeamScore = gameLocal.mpGame.GetScoreForTeam( TEAM_MARINE );
			int stroggTeamScore = gameLocal.mpGame.GetScoreForTeam( TEAM_STROGG );

			if( marineTeamScore > stroggTeamScore )
			{
				/// Marines win!
	 			gameLocal.mpGame.OnDeadZoneTeamVictory( TEAM_MARINE );
			}
			else if( marineTeamScore < stroggTeamScore )
			{
				/// Strogg win!
	 			gameLocal.mpGame.OnDeadZoneTeamVictory( TEAM_STROGG );
			}

			break;
		}
	}
}

/*
================
riDZGameState::SetDZState
================
*/
void riDZGameState::SetDZState( int dz, dzState_t newState ) {
	assert( gameLocal.isServer && ( dz >= 0 && dz < TEAM_MAX ) && type == GS_DZ );

	dzStatus[ dz ].state = newState;
}

/*
================
riDZGameState::operator==
================
*/
bool riDZGameState::operator==( const riDZGameState& rhs ) const {
	if( (rvGameState&)(*this) != (rvGameState&)rhs ) {
		return false;
	}

	for( int i = 0; i < TEAM_MAX; i++ ) {
		if( dzStatus[ i ] != rhs.dzStatus[ i ] ) {
			return false;
		}
	}

	return true;
}

/*
================
riDZGameState::operator=
================
*/
riDZGameState& riDZGameState::operator=( const riDZGameState& rhs ) {
	(rvGameState&)(*this) = (rvGameState&)rhs;

	for( int i = 0; i < TEAM_MAX; i++ ) {
		dzStatus[ i ] = rhs.dzStatus[ i ];
	}

 	return (*this);
}

