
#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../Game_local.h"
#include "../mp/RoundGameState.h"
#include "../mp/RoundModes.h"
// RAVEN BEGIN
#include "../ai/AI.h"
#if !defined(__GAME_PROJECTILE_H__)
	#include "../Projectile.h"
#endif
#if !defined(__GAME_WEAPON_H__)
	#include "../Weapon.h"
#endif
#if !defined(__GAME_SPAWNER_H__)
	#include "../spawner.h"
#endif
#if !defined(__GAME_VEHICLE_H__)
	#include "../Vehicle/Vehicle.h"
#endif
#if !defined(__AI_MANAGER_H__)
	#include "../ai/AI_Manager.h"
#endif
#if !defined(__INSTANCE_H__)
	#include "../Instance.h"
#endif
// RAVEN END

#if defined(_WIN32)
	#if defined(__has_include)
		#if __has_include("TypeInfo.h")
			#include "TypeInfo.h"
		#else
			#include "NoGameTypeInfo.h"
		#endif
	#else
		#include "TypeInfo.h"
	#endif
#else
	#include "NoGameTypeInfo.h"
#endif

/*
==================
Cmd_GetFloatArg
==================
*/
float Cmd_GetFloatArg( const idCmdArgs &args, int &argNum ) {
	const char *value;

	value = args.Argv( argNum++ );
	return atof( value );
}

/*
===================
Cmd_EntityList_f
===================
*/
void Cmd_EntityList_f( const idCmdArgs &args ) {
	int			e;
	idEntity	*check;
	int			count;
	size_t		size;
	idStr		match;

	if ( args.Argc() > 1 ) {
		match = args.Args();
		match.Replace( " ", "" );
	} else {
		match = "";
	}

	count = 0;
	size = 0;

	gameLocal.Printf( "%-4s  %-20s %-20s %s\n", " Num", "EntityDef", "Class", "Name" );
	gameLocal.Printf( "--------------------------------------------------------------------\n" );
	for( e = 0; e < MAX_GENTITIES; e++ ) {
		check = gameLocal.entities[ e ];

		if ( !check ) {
			continue;
		}

		if ( !check->name.Filter( match ) ) {
			continue;
		}

		gameLocal.Printf( "%4i: %-20s %-20s %s\n", e,
			check->GetEntityDefName(), check->GetClassname(), check->name.c_str() );

		count++;
		size += check->spawnArgs.Allocated();
	}

	gameLocal.Printf( "...%d entities\n...%d bytes of spawnargs\n", count, size );
}

/*
===================
Cmd_ClientEntityList_f
===================
*/
void Cmd_ClientEntityList_f( const idCmdArgs &args ) {
	int			e;
	rvClientEntity	*check;
	int			count;
	idStr		match;

	if ( args.Argc() > 1 ) {
		match = args.Args();
		match.Replace( " ", "" );
	} else {
		match = "";
	}

	count = 0;

	gameLocal.Printf( "%-4s  %-20s\n", " Num", "Classname" );
	gameLocal.Printf( "--------------------------------------------------------------------\n" );
	for( e = 0; e < MAX_CENTITIES; e++ ) {
		check = gameLocal.clientEntities[ e ];

		idStr name( check->GetClassType().classname );

		if ( !check ) {
			continue;
		}

		if ( !name.Filter( match ) ) {
			continue;
		}
				
		gameLocal.Printf( "%4i: %-20s\n", e, name.c_str() );

		count++;
	}

	gameLocal.Printf( "...%d entities\n", count );
}


/*
===================
Cmd_ActiveEntityList_f
===================
*/
void Cmd_ActiveEntityList_f( const idCmdArgs &args ) {
	idEntity	*check;
	int			count;

	count = 0;

	gameLocal.Printf( "%-4s  %-20s %-20s %s\n", " Num", "EntityDef", "Class", "Name" );
	gameLocal.Printf( "--------------------------------------------------------------------\n" );
	for( check = gameLocal.activeEntities.Next(); check != NULL; check = check->activeNode.Next() ) {
		char	dormant = check->fl.isDormant ? '-' : ' ';
		gameLocal.Printf( "%4i:%c%-20s %-20s %s\n", check->entityNumber, dormant, check->GetEntityDefName(), check->GetClassname(), check->name.c_str() );
		count++;
	}

	gameLocal.Printf( "...%d active entities\n", count );
}

/*
===================
Cmd_ListSpawnArgs_f
===================
*/
void Cmd_ListSpawnArgs_f( const idCmdArgs &args ) {
	int i;
	idEntity *ent;

	ent = gameLocal.FindEntity( args.Argv( 1 ) );
	if ( !ent ) {
		gameLocal.Printf( "entity not found\n" );
		return;
	}

	for ( i = 0; i < ent->spawnArgs.GetNumKeyVals(); i++ ) {
		const idKeyValue *kv = ent->spawnArgs.GetKeyVal( i );
		gameLocal.Printf( "\"%s\"  " S_COLOR_WHITE "\"%s\"\n", kv->GetKey().c_str(), kv->GetValue().c_str() );
	}
}

/*
===================
Cmd_ReloadScript_f
===================
*/
void Cmd_ReloadScript_f( const idCmdArgs &args ) {
	// shutdown the map because entities may point to script objects
	gameLocal.MapShutdown();

	// recompile the scripts
	gameLocal.program.Startup( SCRIPT_DEFAULT );

	// error out so that the user can rerun the scripts
	gameLocal.Error( "Exiting map to reload scripts" );
}

/*
===================
Cmd_Script_f
===================
*/
void Cmd_Script_f( const idCmdArgs &args ) {
	const char *	script;
	idStr			text;
	idStr			funcname;
	static int		funccount = 0;
	idThread *		thread;
	const function_t *func;
	idEntity		*ent;

	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	sprintf( funcname, "ConsoleFunction_%d", funccount++ );

	script = args.Args();
// RAVEN BEGIN
// jscott: fixed sprintf to idStr
	text = va( "void %s() {%s;}\n", funcname.c_str(), script );
// RAVEN END
	if ( gameLocal.program.CompileText( "console", text, true ) ) {
		func = gameLocal.program.FindFunction( funcname );
		if ( func ) {
			// set all the entity names in case the user named one in the script that wasn't referenced in the default script
			for( ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
				gameLocal.program.SetEntity( ent->name, ent );
			}

			thread = new idThread( func );
			thread->Start();
		}
	}
}

// RAVEN BEGIN
// jscott: exports for tracking memory
/*
==================
idGameEdit::ScriptSummary
==================
*/
size_t idGameEdit::ScriptSummary( const idCmdArgs &args ) const {

	return( gameLocal.program.ScriptSummary( args ) );
}

/*
==================
idGameEdit::ClassSummary
==================
*/
size_t idGameEdit::ClassSummary( const idCmdArgs &args ) const {

	common->Printf( "Classes         - %zuK\n", idClass::GetUsedMemory() / 1024 );

	return( idClass::GetUsedMemory() / 1024 );
}

/*
==================
idGameEdit::EntitySummary
==================
*/

size_t idGameEdit::EntitySummary( const idCmdArgs &args ) const {

	common->Printf( "CL & SV ents    - %zuK\n", gameLocal.GetEntityMemoryUsage () / 1024);

	return gameLocal.GetEntityMemoryUsage() / 1024;
}
// RAVEN END

/*
==================
KillEntities

Kills all the entities of the given class in a level.
==================
*/
void KillEntities( const idCmdArgs &args, const idTypeInfo &superClass ) {
	idEntity	*ent;
	idStrList	ignore;
	const char *name;
	int			i;

	if ( !gameLocal.GetLocalPlayer() || !gameLocal.CheatsOk( false ) ) {
		return;
	}

	for( i = 1; i < args.Argc(); i++ ) {
		name = args.Argv( i );
		ignore.Append( name );
	}

	for( ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
		if ( ent->IsType( superClass ) ) {
			for( i = 0; i < ignore.Num(); i++ ) {
				if ( ignore[ i ] == ent->name ) {
					break;
				}
			}

			if ( i >= ignore.Num() ) {
				ent->PostEventMS( &EV_Remove, 0 );
			}
		}
	}
}

/*
==================
Cmd_KillMonsters_f

Kills all the monsters in a level.
==================
*/
void Cmd_KillMonsters_f( const idCmdArgs &args ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	KillEntities( args, idAI::GetClassType() );
// nmckenzie: rvSpawners
	KillEntities( args, rvSpawner::GetClassType() );

	// kill any projectiles as well since they have pointers to the monster that created them
	KillEntities( args, idProjectile::GetClassType() );
// RAVEN END
}

/*
==================
Cmd_KillMovables_f

Kills all the moveables in a level.
==================
*/
void Cmd_KillMovables_f( const idCmdArgs &args ) {
	if ( !gameLocal.GetLocalPlayer() || !gameLocal.CheatsOk( false ) ) {
		return;
	}
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	KillEntities( args, idMoveable::GetClassType() );
// RAVEN END
}

// RAVEN BEGIN
// bdube: vehicle code
/*
==================
Cmd_KillVehicles_f
==================
*/
void Cmd_KillVehicles_f( const idCmdArgs &args ) {
	if ( !gameLocal.GetLocalPlayer() || !gameLocal.CheatsOk( false ) ) {
		return;
	}
	
	rvVehicleController::KillVehicles ( );
}

void Cmd_KillMessage_f( const idCmdArgs &args ) {
	if ( !gameLocal.GetLocalPlayer() || !gameLocal.CheatsOk( false ) ) {
		return;
	}
	
	gameLocal.mpGame.SendDeathMessage( gameLocal.GetLocalPlayer(), gameLocal.GetLocalPlayer(), 2, false );
}

void Cmd_APState_f( const idCmdArgs &args ) {
	if ( !gameLocal.GetLocalPlayer() || !gameLocal.CheatsOk( false ) ) {
		return;
	}
	
	for ( int i = 0; i < gameLocal.mpGame.assaultPoints.Num(); i++ ) {
		gameLocal.Printf ( "Assault point #%d: owner: %d\n", gameLocal.mpGame.assaultPoints[i]->GetIndex(), gameLocal.mpGame.assaultPoints[i]->GetOwner() );
	}
}
// RAVEN END

/*
==================
Cmd_KillRagdolls_f

Kills all the ragdolls in a level.
==================
*/
void Cmd_KillRagdolls_f( const idCmdArgs &args ) {
	if ( !gameLocal.GetLocalPlayer() || !gameLocal.CheatsOk( false ) ) {
		return;
	}
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	KillEntities( args, idAFEntity_Generic::GetClassType() );
	KillEntities( args, idAFEntity_WithAttachedHead::GetClassType() );
// RAVEN END
}


// RITUAL BEGIN
// squirrel: added DeadZone multiplayer mode
/*
==================
GiveStuffToPlayer

Used by the "give" and "buy" command line cmds
==================
*/
void GiveStuffToPlayer( idPlayer* player, const char* name, const char* value )
{
	int			i;
	bool		give_all;
//	idPlayer* player = gameLocal.GetLocalPlayer();

	if( !player || !name )	{
		return;
	}

	if( !value ) {
		value = "";
	}

	if ( idStr::Icmp( name, "all" ) == 0 ) {
		give_all = true;
	} else {
		give_all = false;
	}

	if ( give_all || ( idStr::Cmpn( name, "weapon", 6 ) == 0 ) ) {
		if ( gameLocal.world->spawnArgs.GetBool( "no_Weapons" ) ) {
			gameLocal.world->spawnArgs.SetBool( "no_Weapons", false );
			for( i = 0; i < gameLocal.numClients; i++ ) {
				if ( gameLocal.entities[ i ] ) {
					gameLocal.entities[ i ]->PostEventSec( &EV_Player_SelectWeapon, 0.5f, gameLocal.entities[ i ]->spawnArgs.GetString( "def_weapon1" ) );
				}
			}
		}
	}

	if ( ( idStr::Cmpn( name, "weapon_", 7 ) == 0 ) || ( idStr::Cmpn( name, "item_", 5 ) == 0 ) || ( idStr::Cmpn( name, "ammo_", 5 ) == 0 ) || ( idStr::Icmp( name, "ammorefill" ) == 0 ) ) {
		player->GiveItem( name );
		return;
	}

	if ( give_all || idStr::Icmp( name, "health" ) == 0 )	{
		player->health = player->inventory.maxHealth;
		if ( player->IsInVehicle() ) {
			player->GetVehicleController().Give ( "health", "9999" );
		}
		if ( !give_all ) {
			return;
		}
	}

	if ( give_all || idStr::Icmp( name, "weapons" ) == 0 ) {
		player->inventory.weapons = BIT( MAX_WEAPONS ) - 1;
		player->CacheWeapons();

		if ( !give_all ) {
			return;
		}
	}

	if ( give_all || idStr::Icmp( name, "ammo" ) == 0 ) {
// RAVEN BEGIN
// bdube: define changed
		for ( i = 0 ; i < MAX_AMMOTYPES; i++ ) {
			player->inventory.ammo[ i ] = player->inventory.MaxAmmoForAmmoClass( player, rvWeapon::GetAmmoNameForIndex( i ) );
// RAVEN END		
		}
		if ( !give_all ) {
			return;
		}
	}

	if ( give_all || idStr::Icmp( name, "armor" ) == 0 ) {
		player->inventory.armor = player->inventory.maxarmor;
		if ( !give_all ) {
			return;
		}
	}
// RAVEN BEGIN
	if (idStr::Icmp(name, "quad") == 0) {
		player->GivePowerUp( POWERUP_QUADDAMAGE, SEC2MS( 30.0f ) );
		return;
	}

	if ( idStr::Icmp( name, "invis" ) == 0 ) {
		player->GivePowerUp( POWERUP_INVISIBILITY, SEC2MS( 30.0f ) );
		return;
	}

	if ( idStr::Icmp( name, "regen" ) == 0 ) {
		player->GivePowerUp( POWERUP_REGENERATION, SEC2MS( 30.0f ) );
		return;
	}

	if ( idStr::Icmp( name, "haste" ) == 0 ) {
		player->GivePowerUp( POWERUP_HASTE, SEC2MS( 30.0f ) );
		return;
	}

	if (idStr::Icmp(name, "ammoregen") == 0) {
		player->GivePowerUp( POWERUP_AMMOREGEN, -1 );
		return;
	}
	
	if (idStr::Icmp(name, "scout") == 0) {
		player->GivePowerUp( POWERUP_SCOUT, -1 );
		return;
	}

	if (idStr::Icmp(name, "doubler") == 0) {
		player->GivePowerUp( POWERUP_DOUBLER, -1 );
		return;
	}

	if (idStr::Icmp(name, "guard") == 0) {
		player->GivePowerUp( POWERUP_GUARD, -1 );
		return;
	}

	// Quake 4: The Awakening's powerups exist only where its defs do
	if ( !idStr::Icmp( name, "adrenaline" ) || !idStr::Icmp( name, "fc_armor_regen" ) ) {
		const int powerup = !idStr::Icmp( name, "adrenaline" ) ? POWERUP_ADRENALINE : POWERUP_FC_ARMOR_REGEN;
		const idDict *def = gameLocal.FindEntityDefDict( idPlayer::GetPowerupDefName( powerup ), false );
		if ( def == NULL ) {
			gameLocal.Printf( "%s is not in this game's content\n", idPlayer::GetPowerupDefName( powerup ) );
			return;
		}
		player->GivePowerUp( powerup, SEC2MS( def->GetFloat( "time", "30" ) ) );
		return;
	}
// RAVEN END

	if ( !idStr::Icmp ( name, "wpmod_all" ) ) {
		player->GiveWeaponMods ( 0xFFFFFFFF );
		return;
	} else if ( !idStr::Cmpn( name, "wpmod_", 6 ) ) {
		player->GiveWeaponMod(name);
		return;
	}

	if ( !idStr::Cmpn( name, "stroggmod_", 10 ) ) {
		player->Give ( name, "" );
		return;
	}

	if ( !give_all && !player->Give( name, value ) ) {
		gameLocal.Printf( "unknown item\n" );
	}
}

/*
==================
Cmd_Give_f

Give items to a client
==================
*/
void Cmd_Give_f( const idCmdArgs &args ) {
	idPlayer	*player;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	GiveStuffToPlayer( player, args.Argv(1), args.Argv(2) );
}
// RITUAL END

/*
==================
Cmd_CenterView_f

Centers the players pitch
==================
*/
void Cmd_CenterView_f( const idCmdArgs &args ) {
	idPlayer	*player;
	idAngles	ang;

	player = gameLocal.GetLocalPlayer();
	if ( !player ) {
		return;
	}

	ang = player->viewAngles;
	ang.pitch = 0.0f;
	player->SetViewAngles( ang );
}

/*
==================
Cmd_God_f

Sets client to godmode

argv(0) god
==================
*/
void Cmd_God_f( const idCmdArgs &args ) {
	char		*msg;
	idPlayer	*player;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( player->godmode ) {
		player->godmode = false;
		msg = "godmode OFF\n";
	} else {
		player->godmode = true;
		msg = "godmode ON\n";
	}

	gameLocal.Printf( "%s", msg );
}

/*
==================
Cmd_Undying_f

Sets client to undying

argv(0) undying
==================
*/
void Cmd_Undying_f( const idCmdArgs &args ) {
	char		*msg;
	idPlayer	*player;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( player->undying ) {
		player->undying = false;
		msg = "undying OFF\n";
	} else {
		player->undying = true;
		msg = "undying ON\n";
	}

	gameLocal.Printf( "%s", msg );
}

/*
==================
Cmd_Notarget_f

Sets client to notarget

argv(0) notarget
==================
*/
void Cmd_Notarget_f( const idCmdArgs &args ) {
	char		*msg;
	idPlayer	*player;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( player->fl.notarget ) {
		player->fl.notarget = false;
		msg = "notarget OFF\n";
	} else {
		player->fl.notarget = true;
		msg = "notarget ON\n";
	}

	gameLocal.Printf( "%s", msg );
}

/*
==================
Cmd_Noclip_f

argv(0) noclip
==================
*/
void Cmd_Noclip_f( const idCmdArgs &args ) {
	char		*msg;
	idPlayer	*player;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( player->noclip ) {
		msg = "noclip OFF\n";
	} else {
		msg = "noclip ON\n";
	}
	player->noclip = !player->noclip;

	gameLocal.Printf( "%s", msg );
}

/*
=================
Cmd_Kill_f
=================
*/
void Cmd_Kill_f( const idCmdArgs &args ) {
	idPlayer	*player;

	if ( gameLocal.isMultiplayer ) {
		if ( gameLocal.isClient ) {
			idBitMsg	outMsg;
			byte		msgBuf[ MAX_GAME_MESSAGE_SIZE ];
			outMsg.Init( msgBuf, sizeof( msgBuf ) );
			outMsg.WriteByte( GAME_RELIABLE_MESSAGE_KILL );
			networkSystem->ClientSendReliableMessage( outMsg );
		} else {
			player = gameLocal.GetClientByCmdArgs( args );
			if ( !player ) {
				gameLocal.Printf( "kill <client nickname> or kill <client index>\n" );
				return;
			}
			player->Kill( false, false );
// RAVEN BEGIN
// rhummer: localized this string.. (killed client)
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "say %s %d '%s^0'\n", common->GetLocalizedString( "#str_108022" ), player->entityNumber, gameLocal.userInfo[ player->entityNumber ].GetString( "ui_name" ) ) );
// RAVEN END
		}
	} else {
		player = gameLocal.GetLocalPlayer();
		if ( !player ) {
			return;
		}
		player->Kill( false, false );
	}
}

// RAVEN BEGIN
// bdube: jump points
/*
=================
Cmd_DebugJump_f
=================
*/
void Cmd_DebugJump_f( const idCmdArgs &args ) {
	if (args.Argc() > 1) {
		// going to a specific jump point as specified by second argument
		gameDebug.JumpTo ( args.Argv( 1 ) );
	} else {
		// just go to next jump point as specified
		gameDebug.JumpNext ( );
	}
}

/*
=================
Cmd_DebugNextJumpPoint_f
=================
*/
void Cmd_DebugNextJumpPoint_f( const idCmdArgs &args ) { 	
	// just go to next jump point as specified
	gameDebug.JumpNext ( );
}

/*
=================
Cmd_DebugPrevJumpPoint_f
=================
*/
void Cmd_DebugPrevJumpPoint_f( const idCmdArgs &args ) {
	// just go to previous jump point as specified
	gameDebug.JumpPrev ( );
}

/*
=================
Cmd_AASExtractTactical_f
=================
*/
void Cmd_AASExtractTactical_f( const idCmdArgs &args ) {
	if (gameLocal.GetLocalPlayer())
	{
		gameLocal.GetLocalPlayer()->aasSensor->SearchDebug();
	}
}

/*
=================
Cmd_CallScriptFunc_f
=================
*/
void Cmd_CallScriptFunc_f( const idCmdArgs& args ) {
	if( args.Argc() <= 1 ) {
		gameLocal.Printf( "usage: call <retKey> FuncName <parm1> <Parm2>...\n" );
		return;
	}

	idDict returnDict;
	rvScriptFuncUtility util;

	if( !util.Init(args) ) {
		return;
	}

	util.CallFunc( &returnDict );

	if( util.ReturnsAVal() && util.GetReturnKey() && util.GetReturnKey()[0] ) {
		gameLocal.Printf( "%s: %s\n", util.GetReturnKey(), returnDict.GetString(util.GetReturnKey()) );
	}
}

void Cmd_SetPlayerGravity_f( const idCmdArgs& args ) {
	if( args.Argc() <= 1 ) {
		gameLocal.Printf( "usage: setPlayerGravity 'x_magnitude y_magnitude z_magnitude\n" );
		return;
	}

	idPlayer* player = gameLocal.GetLocalPlayer();
	if( !player ) {
		return;
	}

	idVec3 gravity;
	sscanf( args.Argv(1), "%f %f %f", &gravity.x, &gravity.y, &gravity.z );
	player->GetPhysics()->SetGravity( gravity );
}
// RAVEN END

/*
=================
Cmd_PlayerModel_f
=================
*/
void Cmd_PlayerModel_f( const idCmdArgs &args ) {
	idPlayer	*player;
	const char *name;
	idVec3		pos;
	idAngles	ang;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc() < 2 ) {
		gameLocal.Printf( "usage: playerModel <modelname>\n" );
		return;
	}

	name = args.Argv( 1 );
	player->spawnArgs.Set( "model", name );

	pos = player->GetPhysics()->GetOrigin();
	ang = player->viewAngles;
	player->SpawnToPoint( pos, ang );
}

/*
==================
Cmd_Say
==================
*/
static void Cmd_Say( bool team, const idCmdArgs &args ) {
	const char *name;
	idStr text;
	const char *cmd = team ? "sayTeam" : "say" ;

	if ( !gameLocal.isMultiplayer ) {
		gameLocal.Printf( "%s can only be used in a multiplayer game\n", cmd );
		return;
	}

	if ( args.Argc() < 2 ) {
		gameLocal.Printf( "usage: %s <text>\n", cmd );
		return;
	}

	text = args.Args();
// RAVEN BEGIN
// bdube: make sure text was specified
	if ( text.Length() == 0 ) {
		gameLocal.Printf( "usage: %s <text>\n", cmd );
		return;
	}
	
	text.Replace( "&bsl;", "\\" );
	text.Replace( "&amp;", "&" );

// asalmon: check to see if the text passes the live decency standard
#ifdef _XENON
	if(!Sys_VerifyString(text.c_str()))
	{
		gameLocal.Printf( "Your message did not pass Xbox decency standards\n");
		return;
	}
#endif


// ddynerman: team speak only in team games
	if ( team && !gameLocal.IsTeamGame() ) {
		team = false;
	}
// RAVEN END
	if ( text[ text.Length() - 1 ] == '\n' ) {
		text[ text.Length() - 1 ] = '\0';
	}
	name = "player";

	idPlayer *	player;

	// here we need to special case a listen server to use the real client name instead of "server"
	// "server" will only appear on a dedicated server
	if ( gameLocal.isClient || cvarSystem->GetCVarInteger( "net_serverDedicated" ) == 0 ) {
		player = gameLocal.GetLocalPlayer();
		if ( player ) {
			if ( player->GetUserInfo() ) {
				name = player->GetUserInfo()->GetString( "ui_name", "player" );
			}

// RAVEN BEGIN
// mekberg: activate the mphud gui so the time is right before receiving the chat message
			if ( player->mphud ) {
				player->mphud->Activate( true, gameLocal.time );
			}

			if (player->IsFakeClient())
			{
				name = cvarSystem->GetCVarString( "ui_name" );
			}
		}
// RAVEN END
	} else {
		name = "server";
	}

	if ( gameLocal.isClient ) {
		idBitMsg	outMsg;
		byte		msgBuf[ 256 ];
		outMsg.Init( msgBuf, sizeof( msgBuf ) );
		outMsg.WriteByte( team ? GAME_RELIABLE_MESSAGE_TCHAT : GAME_RELIABLE_MESSAGE_CHAT );
		outMsg.WriteString( name );
		outMsg.WriteString( text );
		outMsg.WriteString( "" );
		networkSystem->ClientSendReliableMessage( outMsg );
	} else {
		gameLocal.mpGame.ProcessChatMessage( gameLocal.localClientNum, team, name, text, NULL, true );
	}
}

/*
==================
Cmd_Say_f
==================
*/
static void Cmd_Say_f( const idCmdArgs &args ) {
	Cmd_Say( false, args );
}

/*
==================
Cmd_SayTeam_f
==================
*/
static void Cmd_SayTeam_f( const idCmdArgs &args ) {
	Cmd_Say( true, args );
}

/*
==================
Cmd_AddChatLine_f
==================
*/
static void Cmd_AddChatLine_f( const idCmdArgs &args ) {
	gameLocal.mpGame.AddChatLine( "%s", args.Argv( 1 ) );
}

/*
==================
Cmd_Kick_f
==================
*/
static void Cmd_Kick_f( const idCmdArgs &args ) {
	idPlayer *player;

	if ( !gameLocal.isMultiplayer ) {
		gameLocal.Printf( "kick can only be used in a multiplayer game\n" );
		return;
	}

	if ( gameLocal.isClient ) {
		gameLocal.Printf( "You have no such power. This is a server command\n" );
		return;
	}

	player = gameLocal.GetClientByCmdArgs( args );
	if ( !player ) {
		gameLocal.Printf( "usage: kick <client nickname> or kick <client index>\n" );
		return;
	}
	cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "say kicking out client %d '%s^0'\n", player->entityNumber, gameLocal.userInfo[ player->entityNumber ].GetString( "ui_name" ) ) );
	cmdSystem->BufferCommandText( CMD_EXEC_NOW, va( "kick %d\n", player->entityNumber ) );
}

/*
==================
Cmd_HudGet_f

Prints a window variable of the local player's own HUD, such as
p_armor::visible, after any expression evaluation it is subject to.
==================
*/
static void Cmd_HudGet_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( args.Argc() != 2 || player == NULL || player->hud == NULL ) {
		gameLocal.Printf( "usage: hudGet <window::variable> with a local player HUD\n" );
		return;
	}
	idStr value;
	if ( !player->hud->GetPresentationValue( args.Argv( 1 ), value ) ) {
		gameLocal.Printf( "hudGet: unknown HUD variable '%s'\n", args.Argv( 1 ) );
		return;
	}
	gameLocal.Printf( "hudGet %s = %s\n", args.Argv( 1 ), value.c_str() );
}

/*
==================
Cmd_GetViewpos_f
==================
*/
void Cmd_GetViewpos_f( const idCmdArgs &args ) {
	idPlayer	*player;
	idVec3		origin;
	idMat3		axis;

	player = gameLocal.GetLocalPlayer();
	if ( !player ) {
		return;
	}

	const renderView_t *view = player->GetRenderView();
	if ( view ) {
		gameLocal.Printf( "(%s) %.1f\n", view->vieworg.ToString(), view->viewaxis[0].ToYaw() );
	} else {
		player->GetViewPos( origin, axis );
		gameLocal.Printf( "(%s) %.1f\n", origin.ToString(), axis[0].ToYaw() );
	}
}

/*
=================
Cmd_NormalizeLevelshotMapDeclPath
=================
*/
static void Cmd_NormalizeLevelshotMapDeclPath( const char *mapPath, idStr &normalizedPath ) {
	normalizedPath = ( mapPath != NULL ) ? mapPath : "";
	normalizedPath.BackSlashesToSlashes();
	normalizedPath.Strip( ' ' );
	normalizedPath.Strip( '\t' );
	normalizedPath.StripTrailingWhitespace();
	normalizedPath.StripQuotes();
	normalizedPath.StripFileExtension();

	if ( !idStr::Icmpn( normalizedPath.c_str(), "maps/", 5 ) ) {
		normalizedPath = normalizedPath.c_str() + 5;
	}
}

/*
=================
Cmd_NormalizeLevelshotEntityFilterToken
=================
*/
static void Cmd_NormalizeLevelshotEntityFilterToken( const char *entityFilter, idStr &normalizedFilter ) {
	normalizedFilter = ( entityFilter != NULL ) ? entityFilter : "";
	normalizedFilter.Strip( ' ' );
	normalizedFilter.Strip( '\t' );
	normalizedFilter.StripTrailingWhitespace();
	normalizedFilter.StripQuotes();
}

/*
=================
Cmd_IsLevelshotMapFilterWhitespace
=================
*/
static bool Cmd_IsLevelshotMapFilterWhitespace( const char ch ) {
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

/*
=================
Cmd_NormalizeLevelshotMapPathAndEntityFilter
=================
*/
static void Cmd_NormalizeLevelshotMapPathAndEntityFilter( const char *mapPath, const char *entityFilter, idStr &normalizedMapPath, idStr &normalizedEntityFilter ) {
	idStr mapToken = ( mapPath != NULL ) ? mapPath : "";
	mapToken.BackSlashesToSlashes();
	mapToken.Strip( ' ' );
	mapToken.Strip( '\t' );
	mapToken.StripTrailingWhitespace();
	mapToken.StripQuotes();

	Cmd_NormalizeLevelshotEntityFilterToken( entityFilter, normalizedEntityFilter );

	int split = -1;
	for ( int i = mapToken.Length() - 1; i >= 0; --i ) {
		if ( Cmd_IsLevelshotMapFilterWhitespace( mapToken[ i ] ) ) {
			split = i;
			break;
		}
	}

	if ( split > 0 ) {
		idStr mapPart = mapToken.Left( split );
		idStr filterPart = mapToken.Right( mapToken.Length() - split - 1 );
		idStr normalizedFilterPart;
		mapPart.Strip( ' ' );
		mapPart.Strip( '\t' );
		mapPart.StripTrailingWhitespace();
		mapPart.StripQuotes();
		Cmd_NormalizeLevelshotEntityFilterToken( filterPart.c_str(), normalizedFilterPart );

		if ( mapPart.Length() > 0 && normalizedFilterPart.Length() > 0 ) {
			mapToken = mapPart;
			if ( normalizedEntityFilter.Length() == 0 ) {
				normalizedEntityFilter = normalizedFilterPart;
			}
		}
	}

	Cmd_NormalizeLevelshotMapDeclPath( mapToken.c_str(), normalizedMapPath );
}

/*
=================
Cmd_GetLevelshotMapDeclForNormalizedPath
=================
*/
static bool Cmd_GetLevelshotMapDeclForNormalizedPath( const idStr &normalizedPath, idDict &outMapDecl ) {
	if ( normalizedPath.Length() == 0 ) {
		return false;
	}

	const idDecl *mapDecl = declManager->FindType( DECL_MAPDEF, normalizedPath.c_str(), false );
	const idDeclEntityDef *mapDef = static_cast<const idDeclEntityDef *>( mapDecl );
	if ( mapDef != NULL ) {
		outMapDecl = mapDef->dict;
		outMapDecl.Set( "path", mapDef->GetName() );
		return true;
	}

	const int numMaps = fileSystem->GetNumMaps();
	for ( int i = 0; i < numMaps; ++i ) {
		const idDict *candidate = fileSystem->GetMapDecl( i );
		if ( candidate == NULL ) {
			continue;
		}

		idStr candidatePath;
		Cmd_NormalizeLevelshotMapDeclPath( candidate->GetString( "path" ), candidatePath );
		if ( candidatePath.Length() == 0 ) {
			continue;
		}

		if ( !idStr::Icmp( normalizedPath.c_str(), candidatePath.c_str() ) ) {
			outMapDecl = *candidate;
			return true;
		}
	}

	return false;
}

/*
=================
Cmd_GetLevelshotMapDecl
=================
*/
static bool Cmd_GetLevelshotMapDecl( const char *mapPath, const char *entityFilter, idDict &outMapDecl ) {
	outMapDecl.Clear();

	idStr normalizedPath;
	idStr normalizedEntityFilter;
	Cmd_NormalizeLevelshotMapPathAndEntityFilter( mapPath, entityFilter, normalizedPath, normalizedEntityFilter );
	if ( normalizedPath.Length() == 0 ) {
		return false;
	}

	if ( normalizedEntityFilter.Length() > 0 ) {
		idStr filteredPath = normalizedPath;
		filteredPath += "_";
		filteredPath += normalizedEntityFilter;
		filteredPath.Strip( ' ' );
		filteredPath.Strip( '\t' );
		filteredPath.StripTrailingWhitespace();
		filteredPath.StripQuotes();
		filteredPath.BackSlashesToSlashes();
		filteredPath.Replace( " ", "_" );
		if ( Cmd_GetLevelshotMapDeclForNormalizedPath( filteredPath, outMapDecl ) ) {
			return true;
		}
	}

	if ( Cmd_GetLevelshotMapDeclForNormalizedPath( normalizedPath, outMapDecl ) ) {
		return true;
	}

	if ( normalizedEntityFilter.Length() == 0 ) {
		idStr firstSegmentPath = normalizedPath;
		firstSegmentPath += "_first";
		if ( Cmd_GetLevelshotMapDeclForNormalizedPath( firstSegmentPath, outMapDecl ) ) {
			return true;
		}
	}

	return false;
}

/*
=================
Cmd_GetLevelshotBaseName
=================
*/
static bool Cmd_GetLevelshotBaseName( const idCmdArgs &args, idStr &baseName ) {
	if ( args.Argc() > 3 ) {
		return false;
	}

	const char *mapPath = ( args.Argc() == 1 ) ? gameLocal.GetMapName() : args.Argv( 1 );
	const char *entityFilter = "";
	if ( args.Argc() == 1 ) {
		entityFilter = gameLocal.serverInfo.GetString( "si_entityFilter", "" );
	} else if ( args.Argc() == 3 ) {
		entityFilter = args.Argv( 2 );
	}

	idDict mapDeclDict;
	if ( Cmd_GetLevelshotMapDecl( mapPath, entityFilter, mapDeclDict ) ) {
		const char *loadImage = mapDeclDict.GetString( "loadimage", "" );
		if ( loadImage[0] != '\0' ) {
			baseName = loadImage;
			baseName.BackSlashesToSlashes();
			baseName.StripFileExtension();
			return true;
		}
	}

	if ( args.Argc() == 1 ) {
		idStr mapName = mapPath;
		mapName.StripPath();
		mapName.StripFileExtension();
		if ( mapName.Length() <= 0 ) {
			return false;
		}
		baseName = va( "gfx/guis/loadscreens/%s", mapName.c_str() );
		return true;
	}

	baseName = args.Argv( 1 );
	if ( args.Argc() == 3 ) {
		idStr normalizedPath;
		idStr normalizedEntityFilter;
		Cmd_NormalizeLevelshotMapPathAndEntityFilter( args.Argv( 1 ), args.Argv( 2 ), normalizedPath, normalizedEntityFilter );
		if ( normalizedPath.Length() > 0 && normalizedEntityFilter.Length() > 0 ) {
			baseName = normalizedPath;
			baseName += "_";
			baseName += normalizedEntityFilter;
			baseName.StripPath();
		}
	}
	baseName.StripFileExtension();
	if ( baseName.Find( "/", false ) < 0 && baseName.Find( ":", false ) < 0 ) {
		baseName = va( "gfx/guis/loadscreens/%s", baseName.c_str() );
	}
	return true;
}

/*
=================
Cmd_GotoLevelshot_f
=================
*/
void Cmd_GotoLevelshot_f( const idCmdArgs &args ) {
	idPlayer	*player;
	idStr		levelshotFile;
	idVec3		origin;
	idAngles	angles;
	int			parsedCount;
	char		*buffer = NULL;
	int			numRead;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( !Cmd_GetLevelshotBaseName( args, levelshotFile ) ) {
		gameLocal.Printf( "usage: gotolevelshot [levelshot|mapname [first|second]]\n" );
		return;
	}

	levelshotFile.SetFileExtension( ".txt" );
	numRead = fileSystem->ReadFile( levelshotFile, reinterpret_cast< void ** >( &buffer ), NULL );
	if ( numRead <= 0 || buffer == NULL ) {
		gameLocal.Printf( "No levelshot pose file: %s\n", levelshotFile.c_str() );
		return;
	}

	parsedCount = sscanf( buffer, "%f %f %f %f %f %f", &origin.x, &origin.y, &origin.z, &angles.pitch, &angles.yaw, &angles.roll );
	fileSystem->FreeFile( buffer );

	if ( parsedCount != 6 ) {
		gameLocal.Printf( "Could not parse levelshot pose from %s\n", levelshotFile.c_str() );
		return;
	}

	player->noclip = true;
	origin.z -= pm_normalviewheight.GetFloat() - 0.25f;
	player->Teleport( origin, angles, NULL );
	gameLocal.Printf( "Moved to levelshot pose: %s (noclip ON)\n", levelshotFile.c_str() );
}

/*
=================
Cmd_SetViewpos_f
=================
*/
void Cmd_SetViewpos_f( const idCmdArgs &args ) {
	idVec3		origin;
	idAngles	angles;
	int			i;
	idPlayer	*player;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( ( args.Argc() != 4 ) && ( args.Argc() != 5 ) && ( args.Argc() != 7 ) ) {
		gameLocal.Printf( "usage: setviewpos <x> <y> <z> [yaw] | <x> <y> <z> <pitch> <yaw> <roll>\n" );
		return;
	}

	angles.Zero();
	if ( args.Argc() == 5 ) {
		angles.yaw = atof( args.Argv( 4 ) );
	} else if ( args.Argc() == 7 ) {
		angles.pitch = atof( args.Argv( 4 ) );
		angles.yaw = atof( args.Argv( 5 ) );
		angles.roll = atof( args.Argv( 6 ) );
	}

	for ( i = 0 ; i < 3 ; i++ ) {
		origin[i] = atof( args.Argv( i + 1 ) );
	}
	origin.z -= pm_normalviewheight.GetFloat() - 0.25f;

	player->Teleport( origin, angles, NULL );
}

/*
=================
Cmd_Teleport_f
=================
*/
void Cmd_Teleport_f( const idCmdArgs &args ) {
	idVec3		origin;
	idAngles	angles;
	idPlayer	*player;
	idEntity	*ent;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc() != 2 ) {
		gameLocal.Printf( "usage: teleport <name of entity to teleport to>\n" );
		return;
	}

	ent = gameLocal.FindEntity( args.Argv( 1 ) );
	if ( !ent ) {
		gameLocal.Printf( "entity not found\n" );
		return;
	}

	angles.Zero();
	angles.yaw = ent->GetPhysics()->GetAxis()[ 0 ].ToYaw();
	origin = ent->GetPhysics()->GetOrigin();

	player->Teleport( origin, angles, ent );
}

/*
=================
Cmd_Trigger_f
=================
*/
void Cmd_Trigger_f( const idCmdArgs &args ) {
	idVec3		origin;
	idAngles	angles;
	idPlayer	*player;
	idEntity	*ent;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc() != 2 ) {
		gameLocal.Printf( "usage: trigger <name of entity to trigger>\n" );
		return;
	}

	ent = gameLocal.FindEntity( args.Argv( 1 ) );
	if ( !ent ) {
		gameLocal.Printf( "entity not found\n" );
		return;
	}

	ent->Signal( SIG_TRIGGER );
	ent->ProcessEvent( &EV_Activate, player );
	ent->TriggerGuis();
}

/*
===================
Cmd_Spawn_f
===================
*/
void Cmd_Spawn_f( const idCmdArgs &args ) {
#ifndef _MPBETA
	const char *key, *value;
	int			i;
	float		yaw;
	idVec3		org;
	idPlayer	*player;
	idDict		dict;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk( false ) ) {
		return;
	}

	if ( args.Argc() & 1 ) {	// must always have an even number of arguments
		gameLocal.Printf( "usage: spawn classname [key/value pairs]\n" );
		return;
	}

	yaw = player->viewAngles.yaw;

	value = args.Argv( 1 );
	dict.Set( "classname", value );
	dict.Set( "angle", va( "%f", yaw + 180 ) );

	org = player->GetPhysics()->GetOrigin() + idAngles( 0, yaw, 0 ).ToForward() * 80 + idVec3( 0, 0, 1 );
	dict.Set( "origin", org.ToString() );

	for( i = 2; i < args.Argc() - 1; i += 2 ) {

		key = args.Argv( i );
		value = args.Argv( i + 1 );

		dict.Set( key, value );
	}

// RAVEN BEGIN
// kfuller: want to know the name of the entity I spawned
	idEntity *newEnt = NULL;
	gameLocal.SpawnEntityDef( dict, &newEnt );

	if (newEnt)	{
		gameLocal.Printf("spawned entity '%s'\n", newEnt->name.c_str());
	}
// RAVEN END
#endif // !_MPBETA
}

// RAVEN BEGIN
// ddynerman: MP spawning command for performance testing
/*
===================
Cmd_EvaluateMPPerformance_f
===================
*/
void Cmd_EvaluateMPPerformance_f( const idCmdArgs &args ) {
	float		yaw;
	idVec3		org;
	idPlayer	*player;
	idDict		dict;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk( false ) ) {
		return;
	}

	int num = 15;

	if ( args.Argc() > 1 ) {
		num = atoi( args.Argv( 1 ) );
	}

	float angleStep = 360.0f / num;

	const char* className = "char_marine";
	
	yaw = player->viewAngles.yaw;

	for( int i = 0; i < num; i++ ) {
		dict.Set( "classname", className );
		dict.Set( "angle", va( "%f", yaw + 180 ) );

		org = player->GetPhysics()->GetOrigin() + idAngles( 0, yaw + (i * angleStep), 0 ).ToForward() * 120 + idVec3( 0, 0, 1 );
		dict.Set( "origin", org.ToString() );

		idEntity *newEnt = NULL;
		gameLocal.SpawnEntityDef( dict, &newEnt );

		if (newEnt)	{
			gameLocal.Printf("spawned entity '%s'\n", newEnt->name.c_str());
		}
	}
}
// RAVEN END


/*
==================
Cmd_Damage_f

Damages the specified entity, with damage_moverCrush (1 point) unless a
damage def is named, in which case <damage> scales it
==================
*/
void Cmd_Damage_f( const idCmdArgs &args ) {
	if ( !gameLocal.GetLocalPlayer() || !gameLocal.CheatsOk( false ) ) {
		return;
	}
	if ( args.Argc() != 3 && args.Argc() != 4 ) {
		gameLocal.Printf( "usage: damage <name of entity to damage> <damage> [damage def]\n" );
		return;
	}

	idEntity *ent = gameLocal.FindEntity( args.Argv( 1 ) );
	if ( !ent ) {
		gameLocal.Printf( "entity not found\n" );
		return;
	}

	const char *damageDefName = args.Argc() == 4 ? args.Argv( 3 ) : "damage_moverCrush";
	if ( !gameLocal.FindEntityDefDict( damageDefName, false ) ) {
		gameLocal.Printf( "damage def '%s' not found\n", damageDefName );
		return;
	}

	ent->Damage( gameLocal.world, gameLocal.world, idVec3( 0, 0, 1 ), damageDefName, atof( args.Argv( 2 ) ), INVALID_JOINT );
}


/*
==================
Cmd_Flashlight_f

Toggles flashlight on specified entity
==================
*/
void Cmd_Flashlight_f( const idCmdArgs &args ) {
	if ( gameLocal.IsMultiplayer() || !gameLocal.GetLocalPlayer() || !gameLocal.CheatsOk( false ) ) {
		return;
	}
	if ( args.Argc() != 3 ) {
		gameLocal.Printf( "usage: actorFlashlight <actor name> <0 = off, 1 = on>\n" );
		return;
	}

	idEntity *ent = gameLocal.FindEntity( args.Argv( 1 ) );
	if ( !ent || !ent->IsType( idActor::GetClassType() ) ) {
		gameLocal.Printf( "entity not found or not an actor\n" );
		return;
	}
	ent->ProcessEvent( &AI_Flashlight, atoi( args.Argv( 2 ) ) );
}

/*
==================
Cmd_Remove_f

Removes the specified entity
==================
*/
void Cmd_Remove_f( const idCmdArgs &args ) {
	if ( !gameLocal.GetLocalPlayer() || !gameLocal.CheatsOk( false ) ) {
		return;
	}
	if ( args.Argc() != 2 ) {
		gameLocal.Printf( "usage: remove <name of entity to remove>\n" );
		return;
	}

	idEntity *ent = gameLocal.FindEntity( args.Argv( 1 ) );
	if ( !ent ) {
		gameLocal.Printf( "entity not found\n" );
		return;
	}

	delete ent;
}

/*
==================
Cmd_AI_DebugFilter_f

Makes the targeted entity the only one ai_debugMove & ai_debugTactical cares about
==================
*/
void Cmd_AI_DebugFilter_f( const idCmdArgs &args ) {
	idPlayer* player = gameLocal.GetLocalPlayer();
	if ( !player ) {
		return;
	}
	idEntity *ent = NULL;
	if ( args.Argc() != 2 ) 
	{
		//trace ahead
		trace_t	trace;
		idVec3 start = player->GetEyePosition();
		idVec3 end = start + player->viewAngles.ToForward() * 2048.0f;
		gameLocal.TracePoint( player, trace, start, end, MASK_SHOT_RENDERMODEL, player );
		ent = gameLocal.GetTraceEntity( trace );
	}
	else
	{	
		idEntity *ent = gameLocal.FindEntity( args.Argv( 1 ) );
		if ( !ent ) {
			gameLocal.Printf( "entity not found\n" );
			return;
		}
	}

	if ( !ent || !ent->IsType( idAI::GetClassType() ) ) {
		ai_debugFilterString.SetString( "" );
	} else {
		ai_debugFilterString.SetString( ent->GetName() );
	}
}

/*
===================
Cmd_TestLight_f
===================
*/
void Cmd_TestLight_f( const idCmdArgs &args ) {
	int			i;
	idStr		filename;
	const char *key, *value, *name = NULL;
	idPlayer *	player;
	idDict		dict;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk( false ) ) {
		return;
	}

	renderView_t	*rv = player->GetRenderView();

	float fov = idMath::Tan( idMath::M_DEG2RAD * rv->fov_x / 2 );

	dict.SetMatrix( "rotation", mat3_default );
	dict.SetVector( "origin", rv->vieworg );
	dict.SetVector( "light_target", rv->viewaxis[0] );
	dict.SetVector( "light_right", rv->viewaxis[1] * -fov );
	dict.SetVector( "light_up", rv->viewaxis[2] * fov );
	dict.SetVector( "light_start", rv->viewaxis[0] * 16 );
	dict.SetVector( "light_end", rv->viewaxis[0] * 1000 );

	if ( args.Argc() >= 2 ) {
		value = args.Argv( 1 );
		filename = args.Argv(1);
		filename.DefaultFileExtension( ".tga" );
		dict.Set( "texture", filename );
	}

	dict.Set( "classname", "light" );
	for( i = 2; i < args.Argc() - 1; i += 2 ) {

		key = args.Argv( i );
		value = args.Argv( i + 1 );

		dict.Set( key, value );
	}

	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		name = va( "spawned_light_%d", i );		// not just light_, or it might pick up a prelight shadow
		if ( !gameLocal.FindEntity( name ) ) {
			break;
		}
	}
	dict.Set( "name", name );

	gameLocal.SpawnEntityDef( dict );

	gameLocal.Printf( "Created new light\n");
}

/*
===================
Cmd_TestPointLight_f
===================
*/
void Cmd_TestPointLight_f( const idCmdArgs &args ) {
	const char *key, *value, *name = NULL;
	int			i;
	idPlayer	*player;
	idDict		dict;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk( false ) ) {
		return;
	}

	dict.SetVector("origin", player->GetRenderView()->vieworg);

	if ( args.Argc() >= 2 ) {
		value = args.Argv( 1 );
		dict.Set("light", value);
	} else {
		dict.Set("light", "300");
	}

	dict.Set( "classname", "light" );
	for( i = 2; i < args.Argc() - 1; i += 2 ) {

		key = args.Argv( i );
		value = args.Argv( i + 1 );

		dict.Set( key, value );
	}

	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		name = va( "light_%d", i );
		if ( !gameLocal.FindEntity( name ) ) {
			break;
		}
	}
	dict.Set( "name", name );

	gameLocal.SpawnEntityDef( dict );

	gameLocal.Printf( "Created new point light\n");
}

/*
==================
Cmd_PopLight_f
==================
*/
void Cmd_PopLight_f( const idCmdArgs &args ) {
	idEntity	*ent;
	idMapEntity *mapEnt;
	idMapFile	*mapFile = gameLocal.GetLevelMap();
	idLight		*lastLight;
	int			last;

	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	bool removeFromMap = ( args.Argc() > 1 );

	lastLight = NULL;
	last = -1;
	for( ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( !ent->IsType( idLight::GetClassType() ) ) {
// RAVEN END
			continue;
		}

		if ( gameLocal.spawnIds[ ent->entityNumber ] > last ) {
			last = gameLocal.spawnIds[ ent->entityNumber ];
			lastLight = static_cast<idLight*>( ent );
		}
	}

	if ( lastLight ) {
		// find map file entity
		mapEnt = mapFile->FindEntity( lastLight->name );

		if ( removeFromMap && mapEnt ) {
			mapFile->RemoveEntity( mapEnt );
		}
		gameLocal.Printf( "Removing light %i\n", lastLight->GetLightDefHandle() );
		delete lastLight;
	} else {
		gameLocal.Printf( "No lights to clear.\n" );
	}

}

/*
====================
Cmd_ClearLights_f
====================
*/
void Cmd_ClearLights_f( const idCmdArgs &args ) {
	idEntity *ent;
	idEntity *next;
	idLight *light;
	idMapEntity *mapEnt;
	idMapFile *mapFile = gameLocal.GetLevelMap();

	bool removeFromMap = ( args.Argc() > 1 );

	gameLocal.Printf( "Clearing all lights.\n" );
	for( ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = next ) {
		next = ent->spawnNode.Next();
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( !ent->IsType( idLight::GetClassType() ) ) {
// RAVEN END
			continue;
		}

		light = static_cast<idLight*>( ent );
		mapEnt = mapFile->FindEntity( light->name );

		if ( removeFromMap && mapEnt ) {
			mapFile->RemoveEntity( mapEnt );
		}

		delete light;
	}
}

// RAVEN BEGIN
// bdube: not using id effects
/*
==================
Cmd_TestFx_f
==================
void Cmd_TestFx_f( const idCmdArgs &args ) {
	idVec3		offset;
	const char *name;
	idPlayer *	player;
	idDict		dict;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	// delete the testModel if active
	if ( gameLocal.testFx ) {
		delete gameLocal.testFx;
		gameLocal.testFx = NULL;
	}

	if ( args.Argc() < 2 ) {
		return;
	}

	name = args.Argv( 1 );

	offset = player->GetPhysics()->GetOrigin() + player->viewAngles.ToForward() * 100.0f;

	dict.Set( "origin", offset.ToString() );
	dict.Set( "test", "1");
	dict.Set( "fx", name );
	gameLocal.testFx = ( idEntityFx * )gameLocal.SpawnEntityType( idEntityFx::Type, &dict );
}
*/
// RAVEN END

#define MAX_DEBUGLINES	128

typedef struct {
	bool used;
	idVec3 start, end;
	int color;
	bool blink;
	bool arrow;
} gameDebugLine_t;

gameDebugLine_t debugLines[MAX_DEBUGLINES];

/*
==================
Cmd_AddDebugLine_f
==================
*/
static void Cmd_AddDebugLine_f( const idCmdArgs &args ) {
	int i, argNum;
	const char *value;

	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc () < 7 ) {
		gameLocal.Printf( "usage: addline <x y z> <x y z> <color>\n" );
		return;
	}
	for ( i = 0; i < MAX_DEBUGLINES; i++ ) {
		if ( !debugLines[i].used ) {
			break;
		}
	}
	if ( i >= MAX_DEBUGLINES ) {
		gameLocal.Printf( "no free debug lines\n" );
		return;
	}
	value = args.Argv( 0 );
	if ( !idStr::Icmp( value, "addarrow" ) ) {
		debugLines[i].arrow = true;
	} else {
		debugLines[i].arrow = false;
	}
	debugLines[i].used = true;
	debugLines[i].blink = false;
	argNum = 1;
	debugLines[i].start.x = Cmd_GetFloatArg( args, argNum );
	debugLines[i].start.y = Cmd_GetFloatArg( args, argNum );
	debugLines[i].start.z = Cmd_GetFloatArg( args, argNum );
	debugLines[i].end.x = Cmd_GetFloatArg( args, argNum );
	debugLines[i].end.y = Cmd_GetFloatArg( args, argNum );
	debugLines[i].end.z = Cmd_GetFloatArg( args, argNum );
	debugLines[i].color = Cmd_GetFloatArg( args, argNum );
}

/*
==================
Cmd_RemoveDebugLine_f
==================
*/
static void Cmd_RemoveDebugLine_f( const idCmdArgs &args ) {
	int i, num;
	const char *value;

	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc () < 2 ) {
		gameLocal.Printf( "usage: removeline <num>\n" );
		return;
	}
	value = args.Argv( 1 );
	num = atoi(value);
	for ( i = 0; i < MAX_DEBUGLINES; i++ ) {
		if ( debugLines[i].used ) {
			if ( --num < 0 ) {
				break;
			}
		}
	}
	if ( i >= MAX_DEBUGLINES ) {
		gameLocal.Printf( "line not found\n" );
		return;
	}
	debugLines[i].used = false;
}

/*
==================
Cmd_BlinkDebugLine_f
==================
*/
static void Cmd_BlinkDebugLine_f( const idCmdArgs &args ) {
	int i, num;
	const char *value;

	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc () < 2 ) {
		gameLocal.Printf( "usage: blinkline <num>\n" );
		return;
	}
	value = args.Argv( 1 );
	num = atoi( value );
	for ( i = 0; i < MAX_DEBUGLINES; i++ ) {
		if ( debugLines[i].used ) {
			if ( --num < 0 ) {
				break;
			}
		}
	}
	if ( i >= MAX_DEBUGLINES ) {
		gameLocal.Printf( "line not found\n" );
		return;
	}
	debugLines[i].blink = !debugLines[i].blink;
}

/*
==================
PrintFloat
==================
*/
static void PrintFloat( float f ) {
	char buf[128], i;

	for ( i = sprintf( buf, "%3.2f", f ); i < 7; i++ ) {
		buf[i] = ' ';
	}
	buf[i] = '\0';
	gameLocal.Printf( buf );
}

/*
==================
Cmd_ListDebugLines_f
==================
*/
static void Cmd_ListDebugLines_f( const idCmdArgs &args ) {
	int i, num;

	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	num = 0;
	gameLocal.Printf( "line num: x1     y1     z1     x2     y2     z2     c  b  a\n" );
	for ( i = 0; i < MAX_DEBUGLINES; i++ ) {
		if ( debugLines[i].used ) {
			gameLocal.Printf( "line %3d: ", num );
			PrintFloat( debugLines[i].start.x );
			PrintFloat( debugLines[i].start.y );
			PrintFloat( debugLines[i].start.z );
			PrintFloat( debugLines[i].end.x );
			PrintFloat( debugLines[i].end.y );
			PrintFloat( debugLines[i].end.z );
			gameLocal.Printf( "%d  %d  %d\n", debugLines[i].color, debugLines[i].blink, debugLines[i].arrow );
			num++;
		}
	}
	if ( !num ) {
		gameLocal.Printf( "no debug lines\n" );
	}
}

/*
==================
D_DrawDebugLines
==================
*/
void D_DrawDebugLines( void ) {
// RAVEN BEGIN
// ddynerman: this eats about 5k us in release
#ifdef _DEBUG
	int i;
	idVec3 forward, right, up, p1, p2;
	idVec4 color;
	float l;

	for ( i = 0; i < MAX_DEBUGLINES; i++ ) {
		if ( debugLines[i].used ) {
			if ( !debugLines[i].blink || (gameLocal.time & (1<<9)) ) {
				color = idVec4( debugLines[i].color&1, (debugLines[i].color>>1)&1, (debugLines[i].color>>2)&1, 1 );
				gameRenderWorld->DebugLine( color, debugLines[i].start, debugLines[i].end );
				//
				if ( debugLines[i].arrow ) {
					// draw a nice arrow
					forward = debugLines[i].end - debugLines[i].start;
					l = forward.Normalize() * 0.2f;
					forward.NormalVectors( right, up);

					if ( l > 3.0f ) {
						l = 3.0f;
					}
					p1 = debugLines[i].end - l * forward + (l * 0.4f) * right;
					p2 = debugLines[i].end - l * forward - (l * 0.4f) * right;
					gameRenderWorld->DebugLine( color, debugLines[i].end, p1 );
					gameRenderWorld->DebugLine( color, debugLines[i].end, p2 );
					gameRenderWorld->DebugLine( color, p1, p2 );
				}
			}
		}
	}
#else
	return;
#endif
// RAVEN END
}

/*
==================
Cmd_ListCollisionModels_f
==================
*/
static void Cmd_ListCollisionModels_f( const idCmdArgs &args ) {
	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	collisionModelManager->ListModels();
}

/*
==================
Cmd_CollisionModelInfo_f
==================
*/
static void Cmd_CollisionModelInfo_f( const idCmdArgs &args ) {
	const char *value;

	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc () < 2 ) {
		gameLocal.Printf( "usage: collisionModelInfo <modelNum>\n"
					"use 'all' instead of the model number for accumulated info\n" );
		return;
	}

	value = args.Argv( 1 );
	if ( !idStr::Icmp( value, "all" ) ) {
		collisionModelManager->ModelInfo( -1 );
	} else {
		collisionModelManager->ModelInfo( atoi(value) );
	}
}

/*
==================
Cmd_ExportModels_f
==================
*/
static void Cmd_ExportModels_f( const idCmdArgs &args ) {
	idModelExport	exporter;
	idStr			name;

	// don't allow exporting models when cheats are disabled,
	// but if we're not in the game, it's ok
	if ( gameLocal.GetLocalPlayer() && !gameLocal.CheatsOk( false ) ) {
		return;
	}

	if ( args.Argc() < 2 ) {
		exporter.ExportModels( "def", ".def" );
	} else {
		name = args.Argv( 1 );
		name = "def/" + name;
		name.DefaultFileExtension( ".def" );
		exporter.ExportDefFile( name );
	}
}

/*
==================
Cmd_ReexportModels_f
==================
*/
static void Cmd_ReexportModels_f( const idCmdArgs &args ) {
	idModelExport	exporter;
	idStr			name;

	// don't allow exporting models when cheats are disabled,
	// but if we're not in the game, it's ok
	if ( gameLocal.GetLocalPlayer() && !gameLocal.CheatsOk( false ) ) {
		return;
	}

	idAnimManager::forceExport = true;
	if ( args.Argc() < 2 ) {
		exporter.ExportModels( "def", ".def" );
	} else {
		name = args.Argv( 1 );
		name = "def/" + name;
		name.DefaultFileExtension( ".def" );
		exporter.ExportDefFile( name );
	}
	idAnimManager::forceExport = false;
}

/*
==================
Cmd_ReloadAnims_f
==================
*/
static void Cmd_ReloadAnims_f( const idCmdArgs &args ) {

	// don't allow reloading anims when cheats are disabled,
	// but if we're not in the game, it's ok
	if ( gameLocal.GetLocalPlayer() && !gameLocal.CheatsOk( false ) ) {
		return;
	}

// RAVEN BEGIN
// mekberg: disable non pre-cached warnings
	fileSystem->SetIsFileLoadingAllowed( true );

// jsinger: animationLib changed to a pointer
	animationLib->ReloadAnims();

// mekberg: enable non pre-cached warnings
	fileSystem->SetIsFileLoadingAllowed( false );
// RAVEN END
}

/*
==================
Cmd_ListAnims_f
==================
*/
static void Cmd_ListAnims_f( const idCmdArgs &args ) {
	idEntity *		ent;
	int				num;
	size_t			size;
	size_t			alloced;
	idAnimator *	animator;
	const char *	classname;
	const idDict *	dict;
	int				i;

	if ( args.Argc() > 1 ) {
		idAnimator animator;

		classname = args.Argv( 1 );

		dict = gameLocal.FindEntityDefDict( classname, false );
		if ( !dict ) {
			gameLocal.Printf( "Entitydef '%s' not found\n", classname );
			return;
		}
		animator.SetModel( dict->GetString( "model" ) );

		gameLocal.Printf( "----------------\n" );
		num = animator.NumAnims();
// RAVEN BEGIN
// rjohnson: more output for animators
		idFile *FH = fileSystem->OpenFileAppend( "animations.txt" );
		for( i = 0; i < num; i++ ) {
			for( int j = 0; ; j++ ) {
				const char *fileName = animator.AnimMD5Name( i, j );

				if ( !fileName[0] ) {
					break;
				}
				gameLocal.Printf( "%s\t%s\n", animator.AnimFullName( i ), animator.AnimMD5Name( i, 0 ) );
				if ( FH ) {
					FH->Printf( "%s\t%s\n", animator.AnimFullName( i ), animator.AnimMD5Name( i, 0 ) );
				}
			}
		}
		gameLocal.Printf( "%d anims\n", num );
		fileSystem->CloseFile( FH );
// RAVEN END
	} else {
// RAVEN BEGIN
// jsinger: animationLib changed to a pointer
		animationLib->ListAnims();
// RAVEN END

		size = 0;
		num = 0;
		for( ent = gameLocal.spawnedEntities.Next(); ent != NULL; ent = ent->spawnNode.Next() ) {
			animator = ent->GetAnimator();
			if ( animator ) {
				alloced = animator->Allocated();
				size += alloced;
				num++;
			}
		}

		gameLocal.Printf( "%d memory used in %d entity animators\n", size, num );
	}
}

// RAVEN BEGIN
// jscott: export for memory tracking
/*
================
idGameEdit::ListAnims
================
*/
void idGameEdit::PrintMemInfo( MemInfo *mi ) {

	int		i, count;
	size_t	totalSize;
	idAAS	*aas;

	totalSize = 0;
	count = 0;
	for( i = 0; i < gameLocal.GetNumAAS(); i++ ) {

		aas = gameLocal.GetAAS( i );
		if( aas ) {

			totalSize += aas->StatsSummary();
			count++;
		}
	}

	mi->aasAssetsTotal = idLib::SizeToInt( totalSize, "idGameEdit::PrintMemInfo" );
	mi->aasAssetsCount = count;

	// jsinger: animationLib changed to a pointer
	animationLib->PrintMemInfo( mi );
}
// RAVEN END

/*
==================
Cmd_AASStats_f
==================
*/
static void Cmd_AASStats_f( const idCmdArgs &args ) {
	int aasNum;

	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	aasNum = aas_test.GetInteger();
	idAAS *aas = gameLocal.GetAAS( aasNum );
	if ( !aas ) {
		gameLocal.Printf( "No aas #%d loaded\n", aasNum );
	} else {
		aas->Stats();
	}
}

/*
==================
Cmd_TestDamage_f
==================
*/
static void Cmd_TestDamage_f( const idCmdArgs &args ) {
	idPlayer *player;
	const char *damageDefName;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc() < 2 || args.Argc() > 3 ) {
		gameLocal.Printf( "usage: testDamage <damageDefName> [angle]\n" );
		return;
	}

	damageDefName = args.Argv( 1 );

	idVec3	dir;
	if ( args.Argc() == 3 ) {
		float angle = atof( args.Argv( 2 ) );

		idMath::SinCos( DEG2RAD( angle ), dir[1], dir[0] );
		dir[2] = 0;
	} else {
		dir.Zero();
	}

	// give the player full health before and after
	// running the damage
	player->health = player->inventory.maxHealth;
	player->Damage( NULL, NULL, dir, damageDefName, 1.0f, INVALID_JOINT );
	player->health = player->inventory.maxHealth;
}

/*
==================
Cmd_TestBoneFx_f
==================
*/
// RAVEN BEGIN
// bdube: not using
/*
static void Cmd_TestBoneFx_f( const idCmdArgs &args ) {
	idPlayer *player;
	const char *bone, *fx;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc() < 3 || args.Argc() > 4 ) {
		gameLocal.Printf( "usage: testBoneFx <fxName> <boneName>\n" );
		return;
	}

	fx = args.Argv( 1 );
	bone = args.Argv( 2 );

	player->StartFxOnBone( fx, bone );
}
*/
// RAVEN END

/*
==================
Cmd_TestClassicLight_f

Raises the classic dynamic lights in front of the player so the muzzle flash and
explosion flashes can be inspected and tuned without firing a shot.

	testClassicLight [ muzzle | explosion ] [ def name ]
==================
*/
static void Cmd_TestClassicLight_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( !g_classicDynamicLights.GetBool() ) {
		gameLocal.Printf( "g_classicDynamicLights is 0; nothing to show.\n" );
		return;
	}

	idVec3 origin;
	idMat3 axis;
	player->GetViewPos( origin, axis );

	const char *what = ( args.Argc() > 1 ) ? args.Argv( 1 ) : "";
	const char *defName = ( args.Argc() > 2 ) ? args.Argv( 2 ) : NULL;
	const bool wantMuzzle = !*what || !idStr::Icmp( what, "muzzle" );
	const bool wantExplosion = !*what || !idStr::Icmp( what, "explosion" );

	if ( wantMuzzle ) {
		const char *weaponName = defName ? defName : "weapon_rocketlauncher";
		const idDict *weaponDef = gameLocal.FindEntityDefDict( weaponName, false );
		if ( !weaponDef ) {
			gameLocal.Printf( "testClassicLight: no such weapon def '%s'\n", weaponName );
		} else {
			G_ClassicMuzzleFlash( *weaponDef, origin + axis[0] * 48.0f );
			gameLocal.Printf( "testClassicLight: muzzle flash from '%s'\n", weaponName );
		}
	}

	if ( wantExplosion ) {
		const char *projectileName = defName ? defName : "projectile_rocket";
		const idDict *projectileDef = gameLocal.FindEntityDefDict( projectileName, false );
		if ( !projectileDef ) {
			gameLocal.Printf( "testClassicLight: no such projectile def '%s'\n", projectileName );
		} else {
			G_ClassicExplosionFlash( *projectileDef, origin + axis[0] * 160.0f );
			gameLocal.Printf( "testClassicLight: explosion flash from '%s'\n", projectileName );
		}
	}
}

/*
==================
Cmd_TestDamage_f
==================
*/
static void Cmd_TestDeath_f( const idCmdArgs &args ) {
	idPlayer *player;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	idVec3 dir;
	idMath::SinCos( DEG2RAD( 45.0f ), dir[1], dir[0] );
	dir[2] = 0;

	g_testDeath.SetBool( 1 );
	player->Damage( NULL, NULL, dir, "damage_triggerhurt_1000", 1.0f, INVALID_JOINT );
	if ( args.Argc() >= 2) {
		player->SpawnGibs( dir, "damage_triggerhurt_1000" );
	}

}

/*
==================
Cmd_TestHitMarker_f

Stages one crosshair hit marker, so its timing and its damage tiers can be seen
without having to line up a real shot.  An amount picks the tier; the named
arguments ask for the other lanes.
==================
*/
static void Cmd_TestHitMarker_f( const idCmdArgs &args ) {
	int		damage = HITMARKER_DAMAGE_UNKNOWN;
	int		flags = 0;
	int		i;

	if ( gameLocal.GetLocalPlayer() == NULL || !gameLocal.CheatsOk() ) {
		return;
	}

	for ( i = 1; i < args.Argc(); i++ ) {
		const char *arg = args.Argv( i );

		if ( !idStr::Icmp( arg, "kill" ) ) {
			flags |= HITMARKER_KILL;
		} else if ( !idStr::Icmp( arg, "team" ) ) {
			flags |= HITMARKER_TEAM;
		} else if ( !idStr::Icmp( arg, "self" ) ) {
			flags |= HITMARKER_SELF;
		} else if ( !idStr::Icmp( arg, "armor" ) ) {
			flags |= HITMARKER_ARMOR;
		} else if ( idStr::IsNumeric( arg ) ) {
			damage = atoi( arg );
		} else {
			gameLocal.Printf( "usage: testHitMarker [damage] [kill] [team] [self] [armor]\n" );
			return;
		}
	}

	rvHitMarker::Trigger( damage, flags, HITMARKER_PRECISE );
}

static void Cmd_TestHudOverlays_f(const idCmdArgs& args) {
	idPlayer* player=gameLocal.GetLocalPlayer();
	if (!player || !gameLocal.CheatsOk()) return;
	idVec3 origin;idMat3 axis;player->GetViewPos(origin,axis);
	gameLocal.mpGame.damageNumbers.Add(origin+axis[0]*128.0f,args.Argc()>1?atoi(args.Argv(1)):75,1,0);
	gameLocal.lagometer.Update(20,2);
	rvHitMarker::Trigger(75,0,HITMARKER_PRECISE);
}

/*
==================
Cmd_WeaponSplat_f
==================
*/
static void Cmd_WeaponSplat_f( const idCmdArgs &args ) {
	idPlayer *player;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	player->weapon->BloodSplat( 2.0f );
}

/*
==================
Cmd_SaveSelected_f
==================
*/
static void Cmd_SaveSelected_f( const idCmdArgs &args ) {
	int i;
	idPlayer *player;
	idEntity *s;
	idMapEntity *mapEnt;
	idMapFile *mapFile = gameLocal.GetLevelMap();
	idDict dict;
	idStr mapName;
	const char *name = NULL;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	s = player->dragEntity.GetSelected();
	if ( !s ) {
		gameLocal.Printf( "no entity selected, set g_dragShowSelection 1 to show the current selection\n" );
		return;
	}

	if ( args.Argc() > 1 ) {
		mapName = args.Argv( 1 );
		mapName = "maps/" + mapName;
	}
	else {
		mapName = mapFile->GetName();
	}

	// find map file entity
	mapEnt = mapFile->FindEntity( s->name );
	// create new map file entity if there isn't one for this articulated figure
	if ( !mapEnt ) {
		mapEnt = new idMapEntity();
		mapFile->AddEntity( mapEnt );
		for ( i = 0; i < 9999; i++ ) {
			name = va( "%s_%d", s->GetEntityDefName(), i );
			if ( !gameLocal.FindEntity( name ) ) {
				break;
			}
		}
		s->name = name;
		mapEnt->epairs.Set( "classname", s->GetEntityDefName() );
		mapEnt->epairs.Set( "name", s->name );
	}

// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	if ( s->IsType( idMoveable::GetClassType() ) ) {
// RAVEN END
		// save the moveable state
		mapEnt->epairs.Set( "origin", s->GetPhysics()->GetOrigin().ToString( 8 ) );
		mapEnt->epairs.Set( "rotation", s->GetPhysics()->GetAxis().ToString( 8 ) );
	}
// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
	else if ( s->IsType( idAFEntity_Generic::GetClassType() ) || s->IsType( idAFEntity_WithAttachedHead::GetClassType() ) ) {
// RAVEN END
		// save the articulated figure state
		dict.Clear();
		static_cast<idAFEntity_Base *>(s)->SaveState( dict );
		mapEnt->epairs.Copy( dict );
	}

	// write out the map file
	mapFile->Write( mapName, ".map" );
}

/*
==================
Cmd_DeleteSelected_f
==================
*/
static void Cmd_DeleteSelected_f( const idCmdArgs &args ) {
	idPlayer *player;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( player ) {
		player->dragEntity.DeleteSelected();
	}
}

/*
==================
Cmd_SaveMoveables_f
==================
*/
static void Cmd_SaveMoveables_f( const idCmdArgs &args ) {
	int e, i;
	idMoveable *m;
	idMapEntity *mapEnt;
	idMapFile *mapFile = gameLocal.GetLevelMap();
	idStr mapName;
	const char *name = NULL;

	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	for( e = 0; e < MAX_GENTITIES; e++ ) {
		m = static_cast<idMoveable *>(gameLocal.entities[ e ]);

// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( !m || !m->IsType( idMoveable::GetClassType() ) ) {
// RAVEN END
			continue;
		}

		if ( m->IsBound() ) {
			continue;
		}

		if ( !m->IsAtRest() ) {
			break;
		}
	}

	if ( e < MAX_GENTITIES ) {
		gameLocal.Warning( "map not saved because the moveable entity %s is not at rest", gameLocal.entities[ e ]->name.c_str() );
		return;
	}

	if ( args.Argc() > 1 ) {
		mapName = args.Argv( 1 );
		mapName = "maps/" + mapName;
	}
	else {
		mapName = mapFile->GetName();
	}

	for( e = 0; e < MAX_GENTITIES; e++ ) {
		m = static_cast<idMoveable *>(gameLocal.entities[ e ]);

// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( !m )
		{
			continue;
		}
// jdischler: need to check for idMoveableItem as well
		if ( !m->IsType( idMoveable::GetClassType()) && !m->IsType( idMoveableItem::GetClassType()) )
		{
// RAVEN END
			continue;
		}

		if ( m->IsBound() ) {
			continue;
		}

		// find map file entity
		mapEnt = mapFile->FindEntity( m->name );
		// create new map file entity if there isn't one for this articulated figure
		if ( !mapEnt ) {
			mapEnt = new idMapEntity();
			mapFile->AddEntity( mapEnt );
			for ( i = 0; i < 9999; i++ ) {
				name = va( "%s_%d", m->GetEntityDefName(), i );
				if ( !gameLocal.FindEntity( name ) ) {
					break;
				}
			}
			m->name = name;
			mapEnt->epairs.Set( "classname", m->GetEntityDefName() );
			mapEnt->epairs.Set( "name", m->name );
		}
		// save the moveable state
		mapEnt->epairs.Set( "origin", m->GetPhysics()->GetOrigin().ToString( 8 ) );
		mapEnt->epairs.Set( "rotation", m->GetPhysics()->GetAxis().ToString( 8 ) );
	}

	// write out the map file
	mapFile->Write( mapName, ".map" );
}

/*
==================
Cmd_SaveRagdolls_f
==================
*/
static void Cmd_SaveRagdolls_f( const idCmdArgs &args ) {
	int e, i;
	idAFEntity_Base *af;
	idMapEntity *mapEnt;
	idMapFile *mapFile = gameLocal.GetLevelMap();
	idDict dict;
	idStr mapName;
	const char *name = NULL;

	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc() > 1 ) {
		mapName = args.Argv( 1 );
		mapName = "maps/" + mapName;
	}
	else {
		mapName = mapFile->GetName();
	}

	for( e = 0; e < MAX_GENTITIES; e++ ) {
		af = static_cast<idAFEntity_Base *>(gameLocal.entities[ e ]);

		if ( !af ) {
			continue;
		}

// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( !af->IsType( idAFEntity_WithAttachedHead::GetClassType() ) && !af->IsType( idAFEntity_Generic::GetClassType() ) ) {
// RAVEN END
			continue;
		}

		if ( af->IsBound() ) {
			continue;
		}

		if ( !af->IsAtRest() ) {
			gameLocal.Warning( "the articulated figure for entity %s is not at rest", gameLocal.entities[ e ]->name.c_str() );
		}

		dict.Clear();
		af->SaveState( dict );

		// find map file entity
		mapEnt = mapFile->FindEntity( af->name );
		// create new map file entity if there isn't one for this articulated figure
		if ( !mapEnt ) {
			mapEnt = new idMapEntity();
			mapFile->AddEntity( mapEnt );
			for ( i = 0; i < 9999; i++ ) {
				name = va( "%s_%d", af->GetEntityDefName(), i );
				if ( !gameLocal.FindEntity( name ) ) {
					break;
				}
			}
			af->name = name;
			mapEnt->epairs.Set( "classname", af->GetEntityDefName() );
			mapEnt->epairs.Set( "name", af->name );
		}
		// save the articulated figure state
		mapEnt->epairs.Copy( dict );
	}

	// write out the map file
	mapFile->Write( mapName, ".map" );
}

/*
==================
Cmd_BindRagdoll_f
==================
*/
static void Cmd_BindRagdoll_f( const idCmdArgs &args ) {
	idPlayer *player;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( player ) {
		player->dragEntity.BindSelected();
	}
}

/*
==================
Cmd_UnbindRagdoll_f
==================
*/
static void Cmd_UnbindRagdoll_f( const idCmdArgs &args ) {
	idPlayer *player;

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( player ) {
		player->dragEntity.UnbindSelected();
	}
}

/*
==================
Cmd_GameError_f
==================
*/
static void Cmd_GameError_f( const idCmdArgs &args ) {
	gameLocal.Error( "game error" );
}

// RAVEN BEGIN
// rjohnson: entity usage stats
/*
==================
Cmd_ListEntityStats_f
==================
*/
static void Cmd_ListEntityStats_f( const idCmdArgs &args ) {
	gameLocal.ListEntityStats( args );
}
// RAVEN END

/*
==================
Cmd_SaveLights_f
==================
*/
static void Cmd_SaveLights_f( const idCmdArgs &args ) {
	int e, i;
	idLight *light;
	idMapEntity *mapEnt;
	idMapFile *mapFile = gameLocal.GetLevelMap();
	idDict dict;
	idStr mapName;
	const char *name = NULL;

	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc() > 1 ) {
		mapName = args.Argv( 1 );
		mapName = "maps/" + mapName;
	}
	else {
		mapName = mapFile->GetName();
	}

	for( e = 0; e < MAX_GENTITIES; e++ ) {
		light = static_cast<idLight*>(gameLocal.entities[ e ]);

// RAVEN BEGIN
// jnewquist: Use accessor for static class type 
		if ( !light || !light->IsType( idLight::GetClassType() ) ) {
// RAVEN END
			continue;
		}

		dict.Clear();
		light->SaveState( &dict );

		// find map file entity
		mapEnt = mapFile->FindEntity( light->name );
		// create new map file entity if there isn't one for this light
		if ( !mapEnt ) {
			mapEnt = new idMapEntity();
			mapFile->AddEntity( mapEnt );
			for ( i = 0; i < 9999; i++ ) {
				name = va( "%s_%d", light->GetEntityDefName(), i );
				if ( !gameLocal.FindEntity( name ) ) {
					break;
				}
			}
			light->name = name;
			mapEnt->epairs.Set( "classname", light->GetEntityDefName() );
			mapEnt->epairs.Set( "name", light->name );
		}
		// save the light state
		mapEnt->epairs.Copy( dict );
	}

	// write out the map file
	mapFile->Write( mapName, ".map" );
}


/*
==================
Cmd_SaveParticles_f
==================
*/
static void Cmd_SaveParticles_f( const idCmdArgs &args ) {
	int e;
	idEntity *ent;
	idMapEntity *mapEnt;
	idMapFile *mapFile = gameLocal.GetLevelMap();
	idDict dict;
	idStr mapName, strModel;

	if ( !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc() > 1 ) {
		mapName = args.Argv( 1 );
		mapName = "maps/" + mapName;
	}
	else {
		mapName = mapFile->GetName();
	}

	for( e = 0; e < MAX_GENTITIES; e++ ) {

		ent = static_cast<idStaticEntity*> ( gameLocal.entities[ e ] );

		if ( !ent ) {
			continue;
		}

		strModel = ent->spawnArgs.GetString( "model" );
		if ( strModel.Length() && strModel.Find( ".prt") > 0 ) {
			dict.Clear();
			dict.Set( "model", ent->spawnArgs.GetString( "model" ) );
			dict.SetVector( "origin", ent->GetPhysics()->GetOrigin() );

			// find map file entity
			mapEnt = mapFile->FindEntity( ent->name );
			// create new map file entity if there isn't one for this entity
			if ( !mapEnt ) {
				continue;
			}
			// save the particle state
			mapEnt->epairs.Copy( dict );
		}
	}

	// write out the map file
	mapFile->Write( mapName, ".map" );
}


/*
==================
Cmd_DisasmScript_f
==================
*/
static void Cmd_DisasmScript_f( const idCmdArgs &args ) {
	gameLocal.program.Disassemble();
}

/*
==================
Cmd_TestSave_f
==================
*/
static void Cmd_TestSave_f( const idCmdArgs &args ) {
	idFile *f;

	f = fileSystem->OpenFileWrite( "test.sav" );
	gameLocal.SaveGame( f );
	fileSystem->CloseFile( f );
}

// RAVEN BEGIN
#if 0
/*
==================
Cmd_RecordViewNotes_f
==================
*/
static void Cmd_RecordViewNotes_f( const idCmdArgs &args ) {
	idPlayer *player;
	idVec3 origin;
	idMat3 axis;

	if ( args.Argc() <= 3 ) {
		return;
	}

	player = gameLocal.GetLocalPlayer();
	if ( !player ) {
		return;
	}

	player->GetViewPos( origin, axis );

	// Argv(1) = filename for map (viewnotes/mapname/person)
	// Argv(2) = note number (person0001)
	// Argv(3) = comments

	idStr str = args.Argv(1);
	str.SetFileExtension( ".txt" );
	idFile *file = fileSystem->OpenFileAppend( str );
	if ( file ) {
		file->WriteFloatString( "\"view\"\t( %s )\t( %s )\r\n", origin.ToString(), axis.ToString() );
		file->WriteFloatString( "\"comments\"\t\"%s: %s\"\r\n\r\n", args.Argv(2), args.Argv(3) );
		fileSystem->CloseFile( file );
	}

	idStr viewComments = args.Argv(1);
	viewComments.StripLeading("viewnotes/");
	viewComments += " -- Loc: ";
	viewComments += origin.ToString();
	viewComments += "\n";
	viewComments += args.Argv(3);
	player->hud->SetStateString( "viewcomments", viewComments );
	player->hud->HandleNamedEvent( "showViewComments" );
}

/*
==================
Cmd_CloseViewNotes_f
==================
*/
static void Cmd_CloseViewNotes_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();

	if ( !player ) {
		return;
	}
	
	player->hud->SetStateString( "viewcomments", "" );
	player->hud->HandleNamedEvent( "hideViewComments" );
}

/*
==================
Cmd_ShowViewNotes_f
==================
*/
static void Cmd_ShowViewNotes_f( const idCmdArgs &args ) {
	static idLexer parser( LEXFL_ALLOWPATHNAMES | LEXFL_NOSTRINGESCAPECHARS | LEXFL_NOSTRINGCONCAT | LEXFL_NOFATALERRORS );
	idToken	token;
	idPlayer *player;
	idVec3 origin;
	idMat3 axis;

	player = gameLocal.GetLocalPlayer();

	if ( !player ) {
		return;
	}

	if ( !parser.IsLoaded() ) {
		idStr str = "viewnotes/";
		str += gameLocal.GetMapName();
		str.StripFileExtension();
		str += "/";
		if ( args.Argc() > 1 ) {
			str += args.Argv( 1 );
		} else {
			str += "comments";
		}
		str.SetFileExtension( ".txt" );
		if ( !parser.LoadFile( str ) ) {
			gameLocal.Printf( "No view notes for %s\n", gameLocal.GetMapName() );
			return;
		}
	}

	if ( parser.ExpectTokenString( "view" ) && parser.Parse1DMatrix( 3, origin.ToFloatPtr() ) && 
		parser.Parse1DMatrix( 9, axis.ToFloatPtr() ) && parser.ExpectTokenString( "comments" ) && parser.ReadToken( &token ) ) {
		player->hud->SetStateString( "viewcomments", token );
		player->hud->HandleNamedEvent( "showViewComments" );
		player->Teleport( origin, axis.ToAngles(), NULL );
	} else {
		parser.FreeSource();
		player->hud->HandleNamedEvent( "hideViewComments" );
		return;
	}
}
#endif
// RAVEN END

/*
=================
FindEntityGUIs

helper function for Cmd_NextGUI_f.  Checks the passed entity to determine if it
has any valid gui surfaces.
=================
*/
bool FindEntityGUIs( idEntity *ent, const modelSurface_t ** surfaces,  int maxSurfs, int &guiSurfaces ) {
	renderEntity_t			*renderEnt;
	idRenderModel			*renderModel;
	const modelSurface_t	*surf;
	const idMaterial		*shader;
	int						i;

	assert( surfaces != NULL );
	assert( ent != NULL );

	memset( surfaces, 0x00, sizeof( modelSurface_t *) * maxSurfs );
	guiSurfaces = 0;

	renderEnt  = ent->GetRenderEntity();
	renderModel = renderEnt->hModel;
	if ( renderModel == NULL ) {
		return false;
	}

	for( i = 0; i < renderModel->NumSurfaces(); i++ ) {
		surf = renderModel->Surface( i );
		if ( surf == NULL ) {
			continue;
		}
		shader = surf->shader;
		if ( shader == NULL ) {
			continue;
		}
		if ( shader->GetEntityGui() > 0 ) {
			surfaces[ guiSurfaces++ ] = surf;
		}
	}

	return ( guiSurfaces != 0 );
}

/*
=================
Cmd_NextGUI_f
=================
*/
void Cmd_NextGUI_f( const idCmdArgs &args ) {
	idVec3					origin;
	idAngles				angles;
	idPlayer				*player;
	idEntity				*ent;
	int						guiSurfaces;
	bool					newEnt;
	renderEntity_t			*renderEnt;
	int						surfIndex;
	srfTriangles_t			*geom;
	idMat4					modelMatrix;
	idVec3					normal;
	idVec3					center;
	const modelSurface_t	*surfaces[ MAX_RENDERENTITY_GUI ];

	player = gameLocal.GetLocalPlayer();
	if ( !player || !gameLocal.CheatsOk() ) {
		return;
	}

	if ( args.Argc() != 1 ) {
		gameLocal.Printf( "usage: nextgui\n" );
		return;
	}

	// start at the last entity
	ent = gameLocal.lastGUIEnt.GetEntity();

	// see if we have any gui surfaces left to go to on the current entity.
	guiSurfaces = 0;
	newEnt = false;
	if ( ent == NULL ) {
		newEnt = true;
	} else if ( FindEntityGUIs( ent, surfaces, MAX_RENDERENTITY_GUI, guiSurfaces ) == true ) {
		if ( gameLocal.lastGUI >= guiSurfaces ) {
			newEnt = true;
		}
	} else {
		// no actual gui surfaces on this ent, so skip it
		newEnt = true;
	}

	if ( newEnt == true ) {
		// go ahead and skip to the next entity with a gui...
		if ( ent == NULL ) {
			ent = gameLocal.spawnedEntities.Next();
		} else {
			ent = ent->spawnNode.Next();
		}

		for ( ; ent != NULL; ent = ent->spawnNode.Next() ) {
			if ( ent->spawnArgs.GetString( "gui", NULL ) != NULL ) {
				break;
			}
			
			if ( ent->spawnArgs.GetString( "gui2", NULL ) != NULL ) {
				break;
			}

			if ( ent->spawnArgs.GetString( "gui3", NULL ) != NULL ) {
				break;
			}
			
			// try the next entity
			gameLocal.lastGUIEnt = ent;
		}

		gameLocal.lastGUIEnt = ent;
		gameLocal.lastGUI = 0;

		if ( !ent ) {
			gameLocal.Printf( "No more gui entities. Starting over...\n" );
			return;
		}
	}

	if ( FindEntityGUIs( ent, surfaces, MAX_RENDERENTITY_GUI, guiSurfaces ) == false ) {
		gameLocal.Printf( "Entity \"%s\" has gui properties but no gui surfaces.\n", ent->name.c_str() );
	}

	if ( guiSurfaces == 0 ) {
		gameLocal.Printf( "Entity \"%s\" has gui properties but no gui surfaces!\n", ent->name.c_str() );
		return;
	}

	gameLocal.Printf( "Teleporting to gui entity \"%s\", gui #%d.\n" , ent->name.c_str (), gameLocal.lastGUI );

	renderEnt = ent->GetRenderEntity();
	surfIndex = gameLocal.lastGUI++;
	geom = surfaces[ surfIndex ]->geometry;
	if ( geom == NULL ) {
		gameLocal.Printf( "Entity \"%s\" has gui surface %d without geometry!\n", ent->name.c_str(), surfIndex );
		return;
	}

	assert( geom->facePlanes != NULL );

	modelMatrix = idMat4( renderEnt->axis, renderEnt->origin );	
	normal = geom->facePlanes[ 0 ].Normal() * renderEnt->axis;
	center = geom->bounds.GetCenter() * modelMatrix;

	origin = center + (normal * 32.0f);
	origin.z -= player->EyeHeight();
	normal *= -1.0f;
	angles = normal.ToAngles ();

	//	make sure the player is in noclip
	player->noclip = true;
	player->Teleport( origin, angles, NULL );
}

static void ArgCompletion_DefFile( const idCmdArgs &args, void(*callback)( const char *s ) ) {
	cmdSystem->ArgCompletion_FolderExtension( args, callback, "def/", true, ".def", nullptr );
}

/*
===============
Cmd_TestId_f
outputs a string from the string table for the specified id
===============
*/
void Cmd_TestId_f( const idCmdArgs &args ) {
	idStr	id;
	int		i;
	if ( args.Argc() == 1 ) {
		common->Printf( "usage: testid <string id>\n" );
		return;
	}

	for ( i = 1; i < args.Argc(); i++ ) {
		id += args.Argv( i );
	}
	if ( idStr::Cmpn( id, STRTABLE_ID, STRTABLE_ID_LENGTH ) != 0 ) {
		id = STRTABLE_ID + id;
	}
	gameLocal.mpGame.AddChatLine( common->GetLocalizedString( id ), "<nothing>", "<nothing>", "<nothing>" );	
}

// RAVEN BEGIN
// ddynerman: instance testing commands
void Cmd_SetInstance_f( const idCmdArgs& args ) {
	if( !gameLocal.isServer ) {
		gameLocal.Warning( "Cmd_SetInstance_f(): setInstance can only be run on a server\n" );
		return;
	}

	if ( args.Argc() <= 2 ) {
		common->Printf( "usage: setInstance <client> <instance id>\n" );
		return;
	}

	int client = atoi( args.Argv( 1 ) );
	int instanceID = atoi( args.Argv( 2 ) );

	if( client < 0 || client >= MAX_CLIENTS || !gameLocal.entities[ client ] ) {
		gameLocal.Warning( "Cmd_SetInstance_f(): Invalid clientnum %d\n", client );
		return;
	}

	idPlayer* player = (idPlayer*)gameLocal.entities[ client ];

	gameLocal.Printf( "Cmd_SetInstance_f(): Switching %d: %s to instance %d\n", client, gameLocal.userInfo[ client ].GetString( "ui_name", "unknown" ), instanceID );

	player->JoinInstance( instanceID );
}

void Cmd_ListInstances_f( const idCmdArgs& args ) {
	if( !gameLocal.isServer ) {
		gameLocal.Warning( "Cmd_ListInstances_f(): listInstances can only be run on a server\n" );
		return;
	}

	for( int i = 0; i < gameLocal.GetNumInstances(); i++ ) {
		gameLocal.Printf("Instance %d:\n", i );
		for( int j = 0; j < MAX_CLIENTS; j++ ) {
			idPlayer* player = (idPlayer*)gameLocal.entities[ j ];
			if( player && player->GetInstance() == i ) {
				gameLocal.Printf( "\t%d: %s\n", j, player->GetUserInfo()->GetString( "ui_name" ) );
			}
		}
	}
}

void Cmd_AddIcon_f( const idCmdArgs& args ) {
	if ( args.Argc() <= 1 ) {
		common->Printf( "usage: addIcon <client>\n" );
		return;
	}
	
	int client = atoi( args.Argv( 1 ) );
	
	iconManager->AddIcon( client, "textures/mp/awards/capture" );
}
// RAVEN END

// RITUAL BEGIN
// squirrel: Mode-agnostic buymenus
void Cmd_ToggleBuyMenu_f( const idCmdArgs& args ) {
	idPlayer* player = gameLocal.GetLocalPlayer();
	if ( player && player->CanBuy() )
	{
		gameLocal.mpGame.OpenLocalBuyMenu();
	}
}

void Cmd_BuyItem_f( const idCmdArgs& args ) {
	idPlayer* player = gameLocal.GetLocalPlayer();
	if ( !player ) {
		common->Printf( "ERROR: Cmd_BuyItem_f() failed, since GetLocalPlayer() was NULL.\n" );
		return;
	}

	player->GenerateImpulseForBuyAttempt( args.Argv(1) );
}
// RITUAL END

/*
===============================================================================

	openQ4 multiplayer bots

===============================================================================
*/

/*
==================
Cmd_AddBot_f

addbot [name] [skill] [exact].  The skill argument is a per-bot override that
bypasses bot_skill and bot_skillVariance entirely, which is how a server
operator puts one hard opponent in an otherwise gentle match without editing a
character file.  "exact" is used by authored matches: the named character must
exist and be available, so a typo can never silently seat a different opponent.
==================
*/
void Cmd_AddBot_f( const idCmdArgs &args ) {
	const char *name = ( args.Argc() > 1 ) ? args.Argv( 1 ) : NULL;
	int skill = -1;			// -1 means no override: follow bot_skill
	bool requireExactCharacter = false;

	if ( args.Argc() > 2 ) {
		skill = atoi( args.Argv( 2 ) );
		if ( skill < 1 || skill > BOT_SKILL_LEVELS ) {
			gameLocal.Printf( "usage: addbot [name] [skill 1-%d]\n", BOT_SKILL_LEVELS );
			return;
		}
	}
	if ( args.Argc() > 3 ) {
		if ( idStr::Icmp( args.Argv( 3 ), "exact" ) != 0 || args.Argc() > 4 ) {
			gameLocal.Printf( "usage: addbot [name] [skill 1-%d] [exact]\n", BOT_SKILL_LEVELS );
			return;
		}
		requireExactCharacter = true;
	}

	botManager.AddBot( name, skill, requireExactCharacter );
}

/*
==================
Cmd_RemoveBot_f
==================
*/
void Cmd_RemoveBot_f( const idCmdArgs &args ) {
	const char *name = ( args.Argc() > 1 ) ? args.Argv( 1 ) : NULL;

	if ( !botManager.RemoveBot( name ) ) {
		if ( name ) {
			gameLocal.Printf( "removebot: no bot called '%s'\n", name );
		} else {
			gameLocal.Printf( "removebot: no bots in the match\n" );
		}
	}
}

/*
==================
Cmd_KickBots_f
==================
*/
void Cmd_KickBots_f( const idCmdArgs &args ) {
	botManager.RemoveAll();
}

/*
==================
Cmd_BotList_f
==================
*/
void Cmd_BotList_f( const idCmdArgs &args ) {
	botManager.ListBots();
}

/*
==================
Cmd_BotCharacters_f
==================
*/
void Cmd_BotCharacters_f( const idCmdArgs &args ) {
	botCharacterManager.ListCharacters();
}

/*
==================
Cmd_BotReload_f

Re-reads the style and character files in place.  Bots keep the character they
are already wearing where the name survives the reload, so an author can retune
a personality and watch it change without restarting the match.
==================
*/
void Cmd_BotReload_f( const idCmdArgs &args ) {
	const bool loaded = botCharacterManager.Reload();

	// Unconditional, and before the early return: Reload has already freed the
	// old roster either way, so every live bot is holding a dangling pointer
	// from here until it is re-bound.  Skipping this on the failure path would
	// leave the bots reading freed memory for the rest of the match.
	botManager.RebindCharacters();

	if ( !loaded ) {
		gameLocal.Printf( "botreload: no bot style or character files were loaded\n" );
		return;
	}

	gameLocal.Printf( "botreload: %d characters, %d styles\n",
					  botCharacterManager.NumCharacters(), botCharacterManager.NumStyles() );
}

/*
==================
Cmd_BotProbeTraversal_f
==================
*/
void Cmd_BotProbeTraversal_f( const idCmdArgs &args ) {
	const bool padProbe = !idStr::Icmp( args.Argv( 0 ), "bot_probePad" );
	const bool dodgeProbe = !idStr::Icmp( args.Argv( 0 ), "bot_probeDodge" );
	const bool airProbe = !idStr::Icmp( args.Argv( 0 ), "bot_probeAir" );
	const navTravelType_t travel = padProbe ? NAVTRAVEL_JUMPPAD :
		( dodgeProbe || airProbe || !idStr::Icmp( args.Argv( 0 ), "bot_probeMove" ) ? NAVTRAVEL_WALK : NAVTRAVEL_JUMP );
	if ( args.Argc() != ( padProbe ? 12 : 11 ) ) {
		gameLocal.Printf( "usage: %s client x y z vx vy vz %s%s\n", args.Argv( 0 ),
			airProbe ? "inputX inputY inputZ" : "targetX targetY targetZ", padProbe ? " padName" : "" );
		return;
	}
	int clientNum = 0;
	const char *clientText = args.Argv( 1 );
	if ( !*clientText ) {
		return;
	}
	for ( const char *digit = clientText; *digit; digit++ ) {
		if ( *digit < '0' || *digit > '9' ||
			 ( clientNum = clientNum * 10 + ( *digit - '0' ) ) >= MAX_CLIENTS ) {
			gameLocal.Printf( "%s: client must be an integer from 0 to %d\n", args.Argv( 0 ), MAX_CLIENTS - 1 );
			return;
		}
	}
	float values[9];
	for ( int i = 0; i < 9; i++ ) {
		if ( !idStr::IsNumeric( args.Argv( i + 2 ) ) ) {
			gameLocal.Printf( "%s: coordinates must be numeric\n", args.Argv( 0 ) );
			return;
		}
		values[i] = atof( args.Argv( i + 2 ) );
		if ( idMath::Fabs( values[i] ) > 65536.0f ) {
			gameLocal.Printf( "%s: coordinate or speed outside diagnostic range\n", args.Argv( 0 ) );
			return;
		}
	}
	idEntity *pad = padProbe ? gameLocal.FindEntity( args.Argv( 11 ) ) : NULL;
	if ( padProbe && !pad ) {
		gameLocal.Printf( "bot_probePad: pad entity not found\n" );
		return;
	}
	// bot_probeAir's last three values are a fixed input direction, not a
	// target; the bot only faces along it.
	const idVec3 origin( values[0], values[1], values[2] );
	const idVec3 airInput( values[6], values[7], 0.0f );
	const idVec3 target = airProbe ? origin + airInput * 64.0f : idVec3( values[6], values[7], values[8] );
	if ( !botManager.ProbeTraversal( clientNum, origin, idVec3( values[3], values[4], values[5] ), target,
			travel, pad, dodgeProbe, airProbe ? &airInput : NULL ) ) {
		gameLocal.Printf( "%s: requires an isolated local cheat server with one live bot and a separate observer%s\n",
			args.Argv( 0 ), padProbe ? ", and a pad with a proven natural landing" : "" );
	}
}

/*
==================
Cmd_BotProbeHealth_f
==================
*/
void Cmd_BotProbeHealth_f( const idCmdArgs &args ) {
	if ( ( args.Argc() != 3 && args.Argc() != 4 ) || !idStr::IsNumeric( args.Argv( 1 ) ) ||
		 !idStr::IsNumeric( args.Argv( 2 ) ) || ( args.Argc() == 4 && !idStr::IsNumeric( args.Argv( 3 ) ) ) ) {
		gameLocal.Printf( "usage: bot_probeHealth client health [knockbackMsec]\n" );
		return;
	}
	if ( !botManager.SetProbeHealth( atoi( args.Argv( 1 ) ), atoi( args.Argv( 2 ) ),
			args.Argc() == 4 ? atoi( args.Argv( 3 ) ) : 0 ) ) {
		gameLocal.Printf( "bot_probeHealth: requires an isolated local cheat server with one live bot, 1-1000 health and 0-1000 ms\n" );
	}
}

/*
==================
Cmd_NavMesh_f
==================
*/
void Cmd_NavMesh_f( const idCmdArgs &args ) {
	const char *sub = ( args.Argc() > 1 ) ? args.Argv( 1 ) : "info";

	if ( !idStr::Icmp( sub, "build" ) ) {
		// Generation is a long collision sweep and only the server routes bots,
		// so a connected client must not be able to run it.
		if ( !gameLocal.isServer || gameLocal.isClient ) {
			gameLocal.Printf( "navmesh build: only the server can build a navmesh\n" );
			return;
		}
		navMesh.Build();
		return;
	}

	if ( !idStr::Icmp( sub, "info" ) ) {
		if ( !navMesh.IsValid() ) {
			gameLocal.Printf( "navmesh: not built\n" );
			return;
		}

		gameLocal.Printf( "navmesh: %d nodes, %d links, built in %d ms\n",
						  navMesh.NumNodes(), navMesh.NumLinks(), navMesh.GetBuildMilliseconds() );
		return;
	}

	if ( !idStr::Icmp( sub, "probe" ) && args.Argc() >= 5 ) {
		if ( !navMesh.IsValid() ) {
			gameLocal.Printf( "navmesh: not built\n" );
			return;
		}
		const idVec3 point( atof( args.Argv( 2 ) ), atof( args.Argv( 3 ) ), atof( args.Argv( 4 ) ) );
		const float radius = ( args.Argc() > 5 ) ? atof( args.Argv( 5 ) ) : 24.0f;
		navMesh.Probe( point, idMath::ClampFloat( 4.0f, 64.0f, radius ) );
		return;
	}

	gameLocal.Printf( "usage: navmesh <build|info|probe x y z [radius]>\n" );
}

void Cmd_PlayerEmote_f( const idCmdArgs& args ) {
	if( gameLocal.GetLocalPlayer() == NULL ) {
		gameLocal.Warning( "Cmd_Emote_f() - local player is NULL" );
		return;
	}

	if ( args.Argc() <= 1 ) {
		gameLocal.Printf( "usage: emote <emote>\n" );
		gameLocal.Printf( "\ttry 'taunt', 'salute', 'grab_a'\n" );
		return;
	}

	if( !idStr::Icmp( args.Argv( 1 ), "taunt" ) ) {
		gameLocal.GetLocalPlayer()->SetEmote( PE_TAUNT );
	} else if( !idStr::Icmp( args.Argv( 1 ), "grab_a" ) ) {
		gameLocal.GetLocalPlayer()->SetEmote( PE_GRAB_A );
	} else if( !idStr::Icmp( args.Argv( 1 ), "grab_b" ) ) {
		gameLocal.GetLocalPlayer()->SetEmote( PE_GRAB_B );
	} else if( !idStr::Icmp( args.Argv( 1), "salute" ) ) {
		gameLocal.GetLocalPlayer()->SetEmote( PE_SALUTE );
	} else if( !idStr::Icmp( args.Argv( 1), "cheer" ) ) {
		gameLocal.GetLocalPlayer()->SetEmote( PE_CHEER );
	} else {
		gameLocal.Printf( "Invalid emote '%s'\n", args.Argv( 1 ) );
		gameLocal.Printf( "\ttry 'taunt', 'salute', 'grab'\n" );
	}
	
}

// mekberg: added
void Cmd_SetPMCVars_f ( const idCmdArgs &args ) {
	if ( gameLocal.isMultiplayer ) {
		return;
	}

	if ( gameLocal.GetLocalPlayer( ) ) {
		gameLocal.GetLocalPlayer( )->SetPMCVars( );
	}
}

#include "../../game/gamesys/AudioGraphDebug.inl"

void Cmd_FadeSound_f( const idCmdArgs &args )	{

	if( args.Argc() < 2)	{
		return;
	}

	float fadeDB = 0.0f;
	float fadeTime = 0.0f;

	idStr _fadeDB = args.Argv( 1);
	fadeDB = atof( _fadeDB );

	idStr _fadeTime = args.Argv( 2);
	fadeTime = atof( _fadeTime );

	soundSystem->FadeSoundClasses( SOUNDWORLD_GAME, SOUND_CLASS_MUSICAL, 0.0f - fadeDB, fadeTime );

}

void Cmd_TestClientModel_f( const idCmdArgs& args ) {
	rvClientEntity* face;
	const idDict* dict = gameLocal.FindEntityDefDict( "player_marine_client", false );

	gameLocal.SpawnClientEntityDef( *dict, &face, false );

//	face = new rvClientAFEntity( *dict );
//	face->Spawn( dict );
//	face->SetOrigin( vec3_zero );
//	face->SetAxis( mat3_identity );
//	face->SetModel( "model_player_marine" );
}


// RAVEN END

void Cmd_CheckSave_f( const idCmdArgs &args );

void Cmd_ShuffleTeams_f( const idCmdArgs& args ) {
	gameLocal.mpGame.ShuffleTeams();
}

#ifndef _FINAL
void Cmd_ClientOverflowReliable_f( const idCmdArgs& args ) {
	idBitMsg	outMsg;
	byte		msgBuf[ 114688 ];

	for( int i = 0; i < 10; i++ ) {
		outMsg.Init( msgBuf, sizeof( msgBuf ) );
		outMsg.WriteByte( -1 );
		for( int j = 0; j < 8190; j++ ) {
			outMsg.WriteByte( j );
		}
		networkSystem->ClientSendReliableMessage( outMsg );
	}
}
#endif

void Cmd_ListMaps_f( const idCmdArgs& args ) {
	gameLocal.mpGame.ListMaps();
}

// Drives the local single-player Arena match without device input: ends the
// live match with a chosen result through the normal scoring path, or skips
// the current ceremony beat. A cheat, because it changes a live match's score.
static void Cmd_OpenQ4ArenaCeremony_f( const idCmdArgs &args ) {
	if ( !gameLocal.isMultiplayer || !gameLocal.isServer ) {
		gameLocal.Printf( "openq4_arenaCeremony requires the local Arena server\n" );
		return;
	}
	gameLocal.mpGame.DebugArenaCampaignCeremony( args );
}

// Server-local diagnostics for scripted gameplay validation. This reads the
// actual game/session state and never changes players or synthesizes input.
// These diagnostics exercise the same accepted-view GUI adapter as a local
// menu action.  They never manufacture participants, revisions or authority.
static void Cmd_OpenQ4MatchControl_f( const idCmdArgs &args ) {
	idMultiplayerGame &mp = gameLocal.mpGame;
	idUserInterface *gui = mp.GetMainGUI();
	if ( !gameLocal.isMultiplayer || gameLocal.GetLocalPlayer() == NULL || gui == NULL ) {
		gameLocal.Printf( "openq4_matchControl requires a local multiplayer client\n" );
		return;
	}
	if ( args.Argc() == 2 && idStr::Icmp( args.Argv( 1 ), "open" ) == 0 ) {
		// Review owns a separate GUI and its players are intentionally spectating.
		// Exercise the normal menu toggle, without synthesizing device input.
		if ( mp.GetCurrentMenu() != 1 ) {
			if ( mp.GetCurrentMenu() != 0 ) {
				mp.StartMenu();
			}
			gameLocal.sessionCommand = "game_startmenu";
		}
		gameLocal.Printf( "MP_CONTROL requested multiplayer menu\n" );
		return;
	}
	if ( args.Argc() == 4 && idStr::Icmp( args.Argv( 1 ), "set" ) == 0 ) {
		static const char *fields[] = {
			"match_team_rows_sel_0", "match_replacement_rows_sel_0",
			"match_proposal_rows_sel_0", "match_profile_rows_sel_0",
			"match_rule_rows_sel_0", "match_series_map_rows_sel_0",
			"match_role_choice", "match_proposal_scope_choice",
			"match_series_profile_choice", "match_rule_value"
		};
		for ( int i = 0; i < static_cast<int>( sizeof( fields ) / sizeof( fields[ 0 ] ) ); ++i ) {
			if ( strcmp( args.Argv( 2 ), fields[ i ] ) == 0 && strlen( args.Argv( 3 ) ) <= 64 ) {
				gui->SetStateString( fields[ i ], args.Argv( 3 ) );
				return;
			}
		}
		gameLocal.Printf( "MP_CONTROL rejected unsupported choice\n" );
		return;
	}
	if ( args.Argc() == 3 && idStr::Icmp( args.Argv( 1 ), "action" ) == 0 ) {
		const char *token = args.Argv( 2 );
		const int length = static_cast<int>( strlen( token ) );
		if ( length < 1 || length > 64 || mp.GetCurrentMenu() != 1 ) {
			gameLocal.Printf( "MP_CONTROL action requires the active multiplayer menu\n" );
			return;
		}
		for ( int i = 0; i < length; ++i ) {
			if ( ( token[ i ] < 'a' || token[ i ] > 'z' ) && token[ i ] != '_' ) {
				gameLocal.Printf( "MP_CONTROL rejected malformed action\n" );
				return;
			}
		}
		mp.HandleGuiCommands( va( "matchControl %s", token ) );
		gameLocal.Printf( "MP_CONTROL action=%s\n", token );
		return;
	}
	if ( args.Argc() != 2 || idStr::Icmp( args.Argv( 1 ), "report" ) != 0 ) {
		gameLocal.Printf( "usage: openq4_matchControl <open|report|action token|set choice value>\n" );
		return;
	}
	const mpSessionView *view = mp.GetClientMatchView();
	if ( view != NULL ) {
		const mpMatchViewPublicState_t &state = view->publicState;
		gameLocal.Printf( "MP_VIEW slot=%d side=%d roles=%u active=%d phase=%d round=%d pause=%d engine=%llu match=%llu vitals=%d items=%d follows=%d session=%llu revision=%llu\n",
			state.recipient.slot, state.recipient.side, state.recipient.publicRoleMask,
			state.recipient.active, state.lifecycle.phase, state.lifecycle.round, state.lifecycle.pauseState,
			state.clocks.engineTimeMsec, state.clocks.matchTimeMsec,
			view->teamVitalCount, view->itemTimingCount, view->followTargetCount,
			state.sessionId, state.sessionRevision );
		for ( int i = 0; i < view->teamVitalCount; ++i ) {
			gameLocal.Printf( "MP_VITAL participant=%u side=%d health=%d armor=%d\n",
				view->teamVitals[ i ].participantId, view->teamVitals[ i ].participantSide,
				view->teamVitals[ i ].health, view->teamVitals[ i ].armor );
		}
		for ( int i = 0; i < view->followTargetCount; ++i ) {
			gameLocal.Printf( "MP_FOLLOW participant=%u side=%d selectable=%d\n",
				view->followTargets[ i ].participantId, view->followTargets[ i ].participantSide,
				view->followTargets[ i ].selectable );
		}
		byte buffer[ MP_MATCH_VIEW_MAX_MESSAGE_BYTES ];
		idBitMsg encoded;
		encoded.Init( buffer, sizeof( buffer ) );
		encoded.BeginWriting();
		mpMatchViewError_t error;
		if ( MPMatchViewEncode( encoded, *view, &error ) ) {
			fileSystem->WriteFile( va( "match-probe/view-%d.bin", state.recipient.slot ),
				buffer, encoded.GetSize() );
		}
	}
	const idDict &state = gui->State();
	for ( int i = 0; i < state.GetNumKeyVals(); ++i ) {
		const idKeyValue *entry = state.GetKeyVal( i );
		if ( entry != NULL && idStr::Icmpn( entry->GetKey(), "match_", 6 ) == 0 &&
			idStr::Icmp( entry->GetKey(), "match_referee_credential" ) != 0 ) {
			gameLocal.Printf( "MP_CONTROL %s=%s\n", entry->GetKey().c_str(), entry->GetValue().c_str() );
		}
	}
}

static void Cmd_OpenQ4ReportMPState_f( const idCmdArgs &args ) {
	if ( !gameLocal.isMultiplayer ) {
		gameLocal.Printf( "openq4_reportMPState requires multiplayer gameplay\n" );
		return;
	}
	idMultiplayerGame &mp = gameLocal.mpGame;
	rvGameState *state = mp.GetGameState();
	if ( state == NULL ) {
		return;
	}
	const rvRoundGameState *round = MPGameTypeHasAny( gameLocal.gameType, GTF_ROUND ) ?
		static_cast< const rvRoundGameState * >( state ) : NULL;
	gameLocal.Printf( "MP_STATE time=%d gametype=%d phase=%d session_phase=%d round=%d round_number=%d marine=%d strogg=%d timelimit=%d session_round=%d round_remaining=%d\n",
		gameLocal.time, gameLocal.gameType, state->GetMPGameState(), mp.GetMatchSession().GetPhase(),
		round != NULL ? round->GetRoundState() : RS_INACTIVE, round != NULL ? round->GetRoundNumber() : 0,
		mp.GetScoreForTeam( TEAM_MARINE ), mp.GetScoreForTeam( TEAM_STROGG ),
		gameLocal.serverInfo.GetInt( "si_timeLimit" ), mp.GetMatchSession().GetRoundState(),
		round != NULL && round->GetRoundStateTime() > 0 ? round->GetRoundStateTime() - gameLocal.time : 0 );
	idPlayer *local = gameLocal.GetLocalPlayer();
	if ( local != NULL ) {
		gameLocal.Printf( "MP_LOCAL client=%d entity=%d demo=%d serverDemo=%d repeater=%d spectator=%d fake=%d spectating=%d follow0=%d\n",
			gameLocal.localClientNum, local->entityNumber, gameLocal.GetDemoState(),
			gameLocal.IsServerDemoPlaying(), gameLocal.isRepeater, local->spectator,
			local->IsFakeClient(), local->spectating,
			mp.CanSpectatorFollow( local->entityNumber, 0 ) );
	}
	for ( int slot = 0; slot < gameLocal.numClients && slot < MAX_CLIENTS; ++slot ) {
		idEntity *entity = gameLocal.entities[ slot ];
		if ( entity == NULL || !entity->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}
		idPlayer *player = static_cast< idPlayer * >( entity );
		gameLocal.Printf( "MP_PLAYER slot=%d bot=%d team=%d health=%d spectating=%d wantSpectate=%d ingame=%d score=%d teamScore=%d\n",
			slot, botManager.IsBot( slot ), player->team, player->health, player->spectating,
			player->wantSpectate, mp.IsInGame( slot ), mp.GetScore( slot ), mp.GetTeamScore( slot ) );
		gameLocal.Printf( "MP_POSE slot=%d entity=%s origin=%s hidden=%d\n",
			slot, player->GetName(), player->GetPhysics()->GetOrigin().ToString( 2 ),
			player->IsHidden() );
		if ( gameLocal.isServer && player->spectating ) {
			for ( int target = 0; target < gameLocal.numClients && target < MAX_CLIENTS; ++target ) {
				// Live follow authority belongs to the server. A remote client's
				// local permission query intentionally cannot grant a camera.
				gameLocal.Printf( "MP_CAMERA observer=%d target=%d allowed=%d following=%d\n",
					slot, target, mp.CanSpectatorFollow( slot, target ), player->spectator == target );
			}
		}
	}
	if ( MPGameTypeHasAny( gameLocal.gameType, GTF_FREEZE ) ) {
		static_cast<rvFreezeTagGameState *>( state )->ReportThawState();
	}
	if ( gameLocal.gameType == GAME_TOURNEY ) {
		rvTourneyGameState *tourney = static_cast<rvTourneyGameState *>( state );
		for ( int arena = 0; arena < MAX_ARENAS; ++arena ) {
			rvTourneyArena &bracket = tourney->GetArena( arena );
			idPlayer **players = bracket.GetPlayers();
			if ( players[ 0 ] != NULL || players[ 1 ] != NULL ) {
				gameLocal.Printf( "MP_TOURNEY round=%d arena=%d state=%d first=%d second=%d winner=%d remaining=%d\n",
					tourney->GetRound(), arena, bracket.GetState(),
					players[ 0 ] != NULL ? players[ 0 ]->entityNumber : -1,
					players[ 1 ] != NULL ? players[ 1 ]->entityNumber : -1,
					bracket.GetWinner() != NULL ? bracket.GetWinner()->entityNumber : -1,
					bracket.GetNextStateTime() > 0 ? bracket.GetNextStateTime() - gameLocal.time : 0 );
			}
		}
	}
}

static void OpenQ4ReportMPAnimation( idEntity *entity, const char *surface ) {
	idAnimator *animator = entity != NULL ? entity->GetAnimator() : NULL;
	if ( animator == NULL ) {
		return;
	}
	for ( int channel = 0; channel < ANIM_NumAnimChannels; ++channel ) {
		const idAnimBlend *animation = animator->CurrentAnim( channel );
		if ( animation != NULL && animation->AnimNum() != 0 ) {
			gameLocal.Printf( "MP_WORLD_ANIM surface=%s entity=%d channel=%d anim=%d time=%d frame=%d\n",
				surface, entity->entityNumber, channel, animation->AnimNum(),
				animation->AnimTime( gameLocal.time ), animation->GetFrameNumber( gameLocal.time ) );
		}
	}
}

static void Cmd_OpenQ4LaunchMPTestProjectile_f( const idCmdArgs &args ) {
	if ( !gameLocal.isMultiplayer || !gameLocal.isServer ||
		!gameLocal.CheatsOk( false ) || args.Argc() != 7 ) {
		gameLocal.Printf( "usage: openq4_launchMPTestProjectile <def> <name> <origin> <direction> <speed> <fuse>\n" );
		return;
	}
	if ( gameLocal.FindEntity( args.Argv( 2 ) ) != NULL ) {
		gameLocal.Printf( "MP_WORLD_PROJECTILE name already exists\n" );
		return;
	}
	idDict spawn;
	spawn.Set( "classname", args.Argv( 1 ) );
	spawn.Set( "name", args.Argv( 2 ) );
	spawn.Set( "origin", args.Argv( 3 ) );
	spawn.Set( "direction", args.Argv( 4 ) );
	spawn.Set( "speed", args.Argv( 5 ) );
	spawn.Set( "fuse", args.Argv( 6 ) );
	idVec3 direction = spawn.GetVector( "direction" );
	if ( direction.Normalize() == 0.0f ) {
		gameLocal.Printf( "MP_WORLD_PROJECTILE direction must be nonzero\n" );
		return;
	}
	idEntity *entity = NULL;
	if ( !gameLocal.SpawnEntityDef( spawn, &entity ) || entity == NULL ) {
		gameLocal.Printf( "MP_WORLD_PROJECTILE spawn failed\n" );
		return;
	}
	if ( !entity->IsType( idProjectile::GetClassType() ) ) {
		gameLocal.Printf( "MP_WORLD_PROJECTILE definition is not a projectile\n" );
		entity->PostEventMS( &EV_Remove, 0 );
		return;
	}
	idProjectile *projectile = static_cast<idProjectile *>( entity );
	const idVec3 origin = spawn.GetVector( "origin" );
	projectile->Create( gameLocal.GetLocalPlayer(), origin, direction );
	projectile->Launch( origin, direction, vec3_origin );
	gameLocal.Printf( "MP_WORLD_PROJECTILE launched name=%s\n", projectile->GetName() );
}

/*
==================
Cmd_LagCompStatus_f

openQ4: what lag compensation would rewind for each connected client, and why: its reported
lead, the round trip the server measured, the remote extrapolation it advertises and the
displayed pose age that leaves.  Read-only, and meaningful with net_mpLagCompensation off.
==================
*/
static void Cmd_LagCompStatus_f( const idCmdArgs &args ) {
	if ( !gameLocal.isMultiplayer || !gameLocal.isServer ) {
		gameLocal.Printf( "lagCompStatus: only a multiplayer server can report lag compensation\n" );
		return;
	}
	const int maxRewindMS = idMath::ClampInt( 0, 1000, net_mpLagCompMaxMS.GetInteger() );
	gameLocal.Printf( "LAGCOMP_STATUS enabled=%d max=%dms slack=%dms bias=%dms frame=%dms\n",
		net_mpLagCompensation.GetBool() ? 1 : 0, maxRewindMS, net_mpLagCompSlackMS.GetInteger(),
		net_mpLagCompBiasMS.GetInteger(), gameLocal.GetMSec() );
	for ( int clientNum = 0; clientNum < MAX_CLIENTS; clientNum++ ) {
		const idEntity *entity = gameLocal.entities[ clientNum ];
		if ( entity == NULL || !entity->IsType( idPlayer::GetClassType() ) ) {
			continue;
		}
		idGameLocal::mpLagCompEstimate_t estimate;
		gameLocal.EstimateMPLagCompensation( clientNum, estimate );
		const int rewindMS = idMath::ClampInt( 0, maxRewindMS, estimate.poseAgeMS + net_mpLagCompBiasMS.GetInteger() );
		gameLocal.Printf( "LAGCOMP_CLIENT client=%d name=\"%s\" lead=%dms rtt=%dms advertised=%dtics extrap=%dms age=%dms rewind=%dms\n",
			clientNum, gameLocal.userInfo[ clientNum ].GetString( "ui_name" ), estimate.leadMS, estimate.measuredMS,
			estimate.advertisedTics, estimate.extrapolationMS, estimate.poseAgeMS, rewindMS );
	}
}

// Read actual simulation and animation state without moving the camera or
// injecting player commands. This complements engine render-target captures
// when qualifying repeated competitive pauses and subsequent resumption.
static void Cmd_OpenQ4ReportMPWorld_f( const idCmdArgs &args ) {
	if ( !gameLocal.isMultiplayer || !gameLocal.CheatsOk( false ) || args.Argc() != 2 ) {
		gameLocal.Printf( "usage: openq4_reportMPWorld <entity name>\n" );
		return;
	}
	idEntity *entity = gameLocal.FindEntity( args.Argv( 1 ) );
	if ( entity == NULL || entity->GetPhysics() == NULL ) {
		gameLocal.Printf( "MP_WORLD_MISSING name=%s\n", args.Argv( 1 ) );
		return;
	}
	const idPhysics *physics = entity->GetPhysics();
	gameLocal.Printf( "MP_WORLD entity=%d name=%s type=%s engine=%d frozen=%d physics=%d hidden=%d health=%d origin=\"%s\" velocity=\"%s\" angles=\"%s\"\n",
		entity->entityNumber, entity->GetName(), entity->GetClassname(), gameLocal.time,
		gameLocal.mpGame.IsGameplayFrozen(), physics->GetTime(), entity->IsHidden(), entity->health,
		physics->GetOrigin().ToString( 4 ), physics->GetLinearVelocity().ToString( 4 ),
		physics->GetAxis().ToAngles().ToString( 4 ) );
	OpenQ4ReportMPAnimation( entity, "world" );
	if ( entity->IsType( idPlayer::GetClassType() ) ) {
		idPlayer *player = static_cast<idPlayer *>( entity );
		idVec3 viewOrigin;
		idMat3 viewAxis;
		player->GetViewPos( viewOrigin, viewAxis );
		const renderView_t *view = player->GetRenderView();
		gameLocal.Printf( "MP_WORLD_VIEW entity=%d think=%d eye=\"%s\" render=\"%s\"\n",
			entity->entityNumber, entity->thinkFlags, viewOrigin.ToString( 2 ),
			view != NULL ? view->vieworg.ToString( 2 ) : "unavailable" );
		for ( int powerup = 0; powerup < POWERUP_MAX; ++powerup ) {
			if ( ( player->inventory.powerups & ( 1 << powerup ) ) != 0 ) {
				gameLocal.Printf( "MP_WORLD_POWERUP entity=%d kind=%d remaining=%d\n",
					entity->entityNumber, powerup,
					player->inventory.powerupEndTime[ powerup ] - gameLocal.time );
			}
		}
		gameLocal.Printf( "MP_WORLD_INVENTORY entity=%d armor=%d maxHealth=%d maxArmor=%d credits=%d weapons=%d\n",
			entity->entityNumber, player->inventory.armor, player->inventory.maxHealth,
			player->inventory.maxarmor, (int)player->buyMenuCash, player->inventory.weapons );
		if ( player->weapon != NULL ) {
			gameLocal.Printf( "MP_WORLD_WEAPON entity=%d clip=%d ready=%d reloading=%d\n",
				entity->entityNumber, player->weapon->AmmoInClip(),
				player->weapon->IsReady(), player->weapon->IsReloading() );
		}
		OpenQ4ReportMPAnimation( player->GetWeaponViewModel(), "weapon" );
	}
}

static void Cmd_OpenQ4AssertMPClientActive_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	const bool inGame = player != NULL && gameLocal.mpGame.IsInGame( player->entityNumber );
	const bool menuClosed = gameLocal.mpGame.GetCurrentMenu() == 0;
	const bool disableHud = player != NULL && player->disableHud;
	if ( player == NULL || player->spectating || player->wantSpectate || !inGame ||
			!menuClosed || disableHud ) {
		gameLocal.Error(
			"openq4_assertMPClientActive failed: client=%d spectating=%d wantSpectate=%d ingame=%d menu=%d disableHud=%d",
			player != NULL ? player->entityNumber : -1,
			player != NULL && player->spectating ? 1 : 0,
			player != NULL && player->wantSpectate ? 1 : 0,
			inGame ? 1 : 0,
			menuClosed ? 0 : 1,
			disableHud ? 1 : 0
		);
		return;
	}

	gameLocal.Printf(
		"OPENQ4_STOCK_BASELINE_MP_CLIENT_ACTIVE client=%d spectating=0 wantSpectate=0 ingame=1 menu=0 disableHud=0\n",
		player->entityNumber
	);
}

// openQ4: scrolls the held Remastered scoreboard by whole rows
// (ui-visual-design.md 14.14). The engine sends it in place of the
// weapon-cycle controls and the right stick while ui_scoreboardScrolls says
// the board lists more rows than it shows; it never leaves this client.
static void Cmd_ScoreboardScroll_f( const idCmdArgs &args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: scoreboardScroll <rows>\n" );
		return;
	}
	gameLocal.mpGame.ScrollScoreboard( atoi( args.Argv( 1 ) ) );
}

// Fixed camera verbs and numeric slots keep the reliable request compact and
// independent of mutable display names. The server revalidates every target.
static void Cmd_Follow_f( const idCmdArgs &args ) {
	idPlayer::spectatorFollow_t operation = idPlayer::SPECTATOR_FOLLOW_NEXT;
	int targetSlot = -1;
	const char *choice = args.Argv( 1 );
	if ( !idStr::Icmp( args.Argv( 0 ), "follownext" ) && args.Argc() == 1 ) {
		choice = "next";
	} else if ( !idStr::Icmp( args.Argv( 0 ), "followprev" ) && args.Argc() == 1 ) {
		choice = "prev";
	} else if ( !idStr::Icmp( args.Argv( 0 ), "followfree" ) && args.Argc() == 1 ) {
		choice = "free";
	} else if ( idStr::Icmp( args.Argv( 0 ), "follow" ) || args.Argc() != 2 ) {
		common->Printf( "%s\n", common->GetLocalizedString( "#str_42880" ) );
		return;
	}
	if ( !idStr::Icmp( choice, "next" ) ) {
		operation = idPlayer::SPECTATOR_FOLLOW_NEXT;
	} else if ( !idStr::Icmp( choice, "prev" ) ) {
		operation = idPlayer::SPECTATOR_FOLLOW_PREV;
	} else if ( !idStr::Icmp( choice, "free" ) ) {
		operation = idPlayer::SPECTATOR_FOLLOW_FREE;
	} else {
		operation = idPlayer::SPECTATOR_FOLLOW_PLAYER;
		targetSlot = 0;
		for ( int i = 0; choice[i]; ++i ) {
			if ( choice[i] < '0' || choice[i] > '9' || targetSlot >= MAX_CLIENTS ) {
				targetSlot = -1;
				break;
			}
			targetSlot = targetSlot * 10 + choice[i] - '0';
		}
		if ( !choice[0] || targetSlot < 0 || targetSlot >= MAX_CLIENTS ) {
			common->Printf( "%s\n", common->GetLocalizedString( "#str_42880" ) );
			return;
		}
	}
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player == NULL ) {
		common->Printf( "%s\n", common->GetLocalizedString( "#str_42881" ) );
		return;
	}
	player->RequestSpectatorFollow( operation, targetSlot );
}

static void Cmd_TestStatusBar_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player ) player->DebugStatusBar( args );
}

static void Cmd_TestWeaponWheel_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player ) player->DebugWeaponWheel( args );
}

// openQ4: the quick-chat wheel without host input (open, slot N, channel,
// release, cancel); release sends the selected phrase.
static void Cmd_TestQuickChat_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( player ) player->DebugQuickChat( args );
}

// testScoreboard <msec>: holds the local player's scoreboard open, as a tournament's
// end does, so headless captures can show it without input.
static void Cmd_TestScoreboard_f( const idCmdArgs &args ) {
	idPlayer *player = gameLocal.GetLocalPlayer();
	if ( !player || args.Argc() < 2 ) {
		gameLocal.Printf( "usage: testScoreboard <msec>\n" );
		return;
	}
	player->forceScoreBoard = true;
	player->forceScoreBoardTime = gameLocal.time + idMath::ClampInt( 1, 60000, atoi( args.Argv( 1 ) ) );
}

/*
=================
idGameLocal::InitConsoleCommands

Let the system know about all of our commands
so it can perform tab completion
=================
*/
void idGameLocal::InitConsoleCommands( void ) {
// RAVEN BEGIN
// jscott: typeinfo gone - didn't work, it was unfinished
//	cmdSystem->AddCommand( "listTypeInfo",			ListTypeInfo_f,				CMD_FL_GAME,				"list type info" );
//	cmdSystem->AddCommand( "writeGameState",		WriteGameState_f,			CMD_FL_GAME,				"write game state" );
//	cmdSystem->AddCommand( "testSaveGame",			TestSaveGame_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"test a save game for a level" );
// RAVEN END
	cmdSystem->AddCommand( "game_memory",			idClass::DisplayInfo_f,		CMD_FL_GAME,				"displays game class info" );
	cmdSystem->AddCommand( "listClasses",			idClass::ListClasses_f,		CMD_FL_GAME,				"lists game classes" );
	cmdSystem->AddCommand( "listThreads",			idThread::ListThreads_f,	CMD_FL_GAME|CMD_FL_CHEAT,	"lists script threads" );
	cmdSystem->AddCommand( "listEntities",			Cmd_EntityList_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"lists game entities" );
	cmdSystem->AddCommand( "listClientEntities",	Cmd_ClientEntityList_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"lists client entities" );
	cmdSystem->AddCommand( "listActiveEntities",	Cmd_ActiveEntityList_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"lists active game entities" );
	cmdSystem->AddCommand( "listMonsters",			idAI::List_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"lists monsters" );
	cmdSystem->AddCommand( "listSpawnArgs",			Cmd_ListSpawnArgs_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"list the spawn args of an entity", idGameLocal::ArgCompletion_EntityName );
	cmdSystem->AddCommand( "listMaps",				Cmd_ListMaps_f,				CMD_FL_GAME,				"list the available maps" );
	cmdSystem->AddCommand( "say",					Cmd_Say_f,					CMD_FL_GAME,				"text chat" );
	cmdSystem->AddCommand( "sayTeam",				Cmd_SayTeam_f,				CMD_FL_GAME,				"team text chat" );
	cmdSystem->AddCommand( "addChatLine",			Cmd_AddChatLine_f,			CMD_FL_GAME,				"internal use - core to game chat lines" );
	cmdSystem->AddCommand( "gameKick",				Cmd_Kick_f,					CMD_FL_GAME,				"same as kick, but recognizes player names" );
	cmdSystem->AddCommand( "give",					Cmd_Give_f,					CMD_FL_GAME|CMD_FL_CHEAT,	"gives one or more items" );
	cmdSystem->AddCommand( "centerview",			Cmd_CenterView_f,			CMD_FL_GAME,				"centers the view" );
	cmdSystem->AddCommand( "god",					Cmd_God_f,					CMD_FL_GAME|CMD_FL_CHEAT,	"enables god mode" );
	cmdSystem->AddCommand( "undying",				Cmd_Undying_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"enables undying mode (take damage down to 1 health, but do not die)" );
	cmdSystem->AddCommand( "notarget",				Cmd_Notarget_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"disables the player as a target" );
	cmdSystem->AddCommand( "noclip",				Cmd_Noclip_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"disables collision detection for the player" );
	cmdSystem->AddCommand( "kill",					Cmd_Kill_f,					CMD_FL_GAME,				"kills the player" );
	cmdSystem->AddCommand( "where",					Cmd_GetViewpos_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"prints the current view position" );
	cmdSystem->AddCommand( "getviewpos",			Cmd_GetViewpos_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"prints the current view position" );
	cmdSystem->AddCommand( "hudGet",				Cmd_HudGet_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"prints a window variable of the local player's HUD" );
	cmdSystem->AddCommand( "setviewpos",			Cmd_SetViewpos_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"sets the current view position" );
	cmdSystem->AddCommand( "gotolevelshot",			Cmd_GotoLevelshot_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"moves the view to the levelshot position" );
	cmdSystem->AddCommand( "teleport",				Cmd_Teleport_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"teleports the player to an entity location", idGameLocal::ArgCompletion_EntityName );
	cmdSystem->AddCommand( "trigger",				Cmd_Trigger_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"triggers an entity", idGameLocal::ArgCompletion_EntityName );
	cmdSystem->AddCommand( "spawn",					Cmd_Spawn_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"spawns a game entity", idCmdSystem::ArgCompletion_Decl<DECL_ENTITYDEF> );
	cmdSystem->AddCommand( "damage",				Cmd_Damage_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"apply damage to an entity", idGameLocal::ArgCompletion_EntityName );
	cmdSystem->AddCommand( "remove",				Cmd_Remove_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"removes an entity", idGameLocal::ArgCompletion_EntityName );
	cmdSystem->AddCommand( "killMonsters",			Cmd_KillMonsters_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"removes all monsters" );
	cmdSystem->AddCommand( "killMoveables",			Cmd_KillMovables_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"removes all moveables" );
	cmdSystem->AddCommand( "killRagdolls",			Cmd_KillRagdolls_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"removes all ragdolls" );
	cmdSystem->AddCommand( "addline",				Cmd_AddDebugLine_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"adds a debug line" );
	cmdSystem->AddCommand( "addarrow",				Cmd_AddDebugLine_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"adds a debug arrow" );
	cmdSystem->AddCommand( "removeline",			Cmd_RemoveDebugLine_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"removes a debug line" );
	cmdSystem->AddCommand( "blinkline",				Cmd_BlinkDebugLine_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"blinks a debug line" );
	cmdSystem->AddCommand( "listLines",				Cmd_ListDebugLines_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"lists all debug lines" );
	cmdSystem->AddCommand( "playerModel",			Cmd_PlayerModel_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"sets the given model on the player", idCmdSystem::ArgCompletion_Decl<DECL_MODELDEF> );
	cmdSystem->AddCommand( "actorFlashlight",		Cmd_Flashlight_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"toggle actor's flashlight", idGameLocal::ArgCompletion_AIName );
	
	cmdSystem->AddCommand( "shuffleTeams",			Cmd_ShuffleTeams_f,			CMD_FL_GAME,				"shuffle teams" );
// RAVEN BEGIN
// bdube: not using id effect system
//	cmdSystem->AddCommand( "testFx",				Cmd_TestFx_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"tests an FX system", idCmdSystem::ArgCompletion_Decl<DECL_FX> );
//	cmdSystem->AddCommand( "testBoneFx",			Cmd_TestBoneFx_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"tests an FX system bound to a joint", idCmdSystem::ArgCompletion_Decl<DECL_FX> );
// RAVEN END
	cmdSystem->AddCommand( "testLight",				Cmd_TestLight_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"tests a light" );
	cmdSystem->AddCommand( "testPointLight",		Cmd_TestPointLight_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"tests a point light" );
	cmdSystem->AddCommand( "popLight",				Cmd_PopLight_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"removes the last created light" );
	cmdSystem->AddCommand( "testClassicLight",	Cmd_TestClassicLight_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"raises the classic dynamic lights in front of the player" );
	cmdSystem->AddCommand( "testDeath",				Cmd_TestDeath_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"tests death" );
	cmdSystem->AddCommand( "testHitMarker",		Cmd_TestHitMarker_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"tests the crosshair hit marker" );
	cmdSystem->AddCommand("testHudOverlays",Cmd_TestHudOverlays_f,CMD_FL_GAME|CMD_FL_CHEAT,"stages damage numbers, hit feedback and one network graph sample without input");
	cmdSystem->AddCommand( "testSave",				Cmd_TestSave_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"writes out a test savegame" );
	cmdSystem->AddCommand( "testModel",				idTestModel::TestModel_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"tests a model", idTestModel::ArgCompletion_TestModel );
	cmdSystem->AddCommand( "testSkin",				idTestModel::TestSkin_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"tests a skin on an existing testModel", idCmdSystem::ArgCompletion_Decl<DECL_SKIN> );
	cmdSystem->AddCommand( "testShaderParm",		idTestModel::TestShaderParm_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"sets a shaderParm on an existing testModel" );
	cmdSystem->AddCommand( "keepTestModel",			idTestModel::KeepTestModel_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"keeps the last test model in the game" );
	cmdSystem->AddCommand( "testAnim",				idTestModel::TestAnim_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"tests an animation", idTestModel::ArgCompletion_TestAnim );
	cmdSystem->AddCommand( "testParticleStopTime",	idTestModel::TestParticleStopTime_f,CMD_FL_GAME|CMD_FL_CHEAT,	"tests particle stop time on a test model" );
	cmdSystem->AddCommand( "nextAnim",				idTestModel::TestModelNextAnim_f,	CMD_FL_GAME|CMD_FL_CHEAT,	"shows next animation on test model" );
	cmdSystem->AddCommand( "prevAnim",				idTestModel::TestModelPrevAnim_f,	CMD_FL_GAME|CMD_FL_CHEAT,	"shows previous animation on test model" );
	cmdSystem->AddCommand( "nextFrame",				idTestModel::TestModelNextFrame_f,	CMD_FL_GAME|CMD_FL_CHEAT,	"shows next animation frame on test model" );
	cmdSystem->AddCommand( "prevFrame",				idTestModel::TestModelPrevFrame_f,	CMD_FL_GAME|CMD_FL_CHEAT,	"shows previous animation frame on test model" );
	cmdSystem->AddCommand( "testBlend",				idTestModel::TestBlend_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"tests animation blending" );
	cmdSystem->AddCommand( "reloadScript",			Cmd_ReloadScript_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"reloads scripts" );
	cmdSystem->AddCommand( "script",				Cmd_Script_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"executes a line of script" );
	cmdSystem->AddCommand( "listCollisionModels",	Cmd_ListCollisionModels_f,	CMD_FL_GAME,				"lists collision models" );
	cmdSystem->AddCommand( "collisionModelInfo",	Cmd_CollisionModelInfo_f,	CMD_FL_GAME,				"shows collision model info" );
	cmdSystem->AddCommand( "reexportmodels",		Cmd_ReexportModels_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"reexports models", ArgCompletion_DefFile );
	cmdSystem->AddCommand( "reloadanims",			Cmd_ReloadAnims_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"reloads animations" );
	cmdSystem->AddCommand( "listAnims",				Cmd_ListAnims_f,			CMD_FL_GAME,				"lists all animations" );
	cmdSystem->AddCommand( "aasStats",				Cmd_AASStats_f,				CMD_FL_GAME,				"shows AAS stats" );
	cmdSystem->AddCommand( "testDamage",			Cmd_TestDamage_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"tests a damage def", idCmdSystem::ArgCompletion_Decl<DECL_ENTITYDEF> );
	cmdSystem->AddCommand( "weaponSplat",			Cmd_WeaponSplat_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"projects a blood splat on the player weapon" );
	cmdSystem->AddCommand( "saveSelected",			Cmd_SaveSelected_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"saves the selected entity to the .map file" );
	cmdSystem->AddCommand( "deleteSelected",		Cmd_DeleteSelected_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"deletes selected entity" );
	cmdSystem->AddCommand( "saveMoveables",			Cmd_SaveMoveables_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"save all moveables to the .map file" );
	cmdSystem->AddCommand( "saveRagdolls",			Cmd_SaveRagdolls_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"save all ragdoll poses to the .map file" );
	cmdSystem->AddCommand( "bindRagdoll",			Cmd_BindRagdoll_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"binds ragdoll at the current drag position" );
	cmdSystem->AddCommand( "unbindRagdoll",			Cmd_UnbindRagdoll_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"unbinds the selected ragdoll" );
	cmdSystem->AddCommand( "saveLights",			Cmd_SaveLights_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"saves all lights to the .map file" );
	cmdSystem->AddCommand( "saveParticles",			Cmd_SaveParticles_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"saves all lights to the .map file" );
	cmdSystem->AddCommand( "clearLights",			Cmd_ClearLights_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"clears all lights" );
	cmdSystem->AddCommand( "gameError",				Cmd_GameError_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"causes a game error" );

// RAVEN BEGIN
// rjohnson: entity usage stats
	cmdSystem->AddCommand( "listEntityStats",		Cmd_ListEntityStats_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"lists global entity stats" );
// ddynerman: mp spawning test command
	cmdSystem->AddCommand( "evaluateMPPerformance",	Cmd_EvaluateMPPerformance_f,CMD_FL_GAME|CMD_FL_CHEAT,	"spawns serveral player models", idCmdSystem::ArgCompletion_Decl<DECL_ENTITYDEF> );
	cmdSystem->AddCommand( "listMapEntities",		idGameLocal::Cmd_PrintMapEntityNumbers_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"lists map entity numbers" );
	cmdSystem->AddCommand( "listSpawnIds",			idGameLocal::Cmd_PrintSpawnIds_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"lists map entity numbers" );
// RAVEN END

#ifndef	ID_DEMO_BUILD
	cmdSystem->AddCommand( "disasmScript",			Cmd_DisasmScript_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"disassembles script" );
// RAVEN BEGIN
// rjohnson: removed old not taking system
/*
	cmdSystem->AddCommand( "recordViewNotes",		Cmd_RecordViewNotes_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"record the current view position with notes" );
	cmdSystem->AddCommand( "showViewNotes",			Cmd_ShowViewNotes_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"show any view notes for the current map, successive calls will cycle to the next note" );
	cmdSystem->AddCommand( "closeViewNotes",		Cmd_CloseViewNotes_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"close the view showing any notes for this map" );
*/
// RAVEN END
	cmdSystem->AddCommand( "exportmodels",			Cmd_ExportModels_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"exports models", ArgCompletion_DefFile );

	// multiplayer client commands ( replaces old impulses stuff )
	//cmdSystem->AddCommand( "clientDropWeapon",		idMultiplayerGame::DropWeapon_f, CMD_FL_GAME,			"drop current weapon" );
	cmdSystem->AddCommand( "clientMessageMode",		idMultiplayerGame::MessageMode_f, CMD_FL_GAME,			"ingame gui message mode" );
	cmdSystem->AddCommand( "messagemode", idMultiplayerGame::MessageMode_f, CMD_FL_GAME, "open all-player chat" );
	cmdSystem->AddCommand( "messagemode2", idMultiplayerGame::MessageMode_f, CMD_FL_GAME, "open team chat" );
	cmdSystem->AddCommand( "say_team", Cmd_SayTeam_f, CMD_FL_GAME, "team text chat" );
	cmdSystem->AddCommand( "follow", Cmd_Follow_f, CMD_FL_GAME, "spectator camera: follow <next|prev|free|client slot>" );
	cmdSystem->AddCommand( "follownext", Cmd_Follow_f, CMD_FL_GAME, "follow the next authorized player" );
	cmdSystem->AddCommand( "followprev", Cmd_Follow_f, CMD_FL_GAME, "follow the previous authorized player" );
	cmdSystem->AddCommand( "followfree", Cmd_Follow_f, CMD_FL_GAME, "leave player follow for a free spectator camera" );
	cmdSystem->AddCommand( "scoreboardScroll", Cmd_ScoreboardScroll_f, CMD_FL_GAME, "scrolls the held scoreboard by whole rows: scoreboardScroll <rows>" );
	// FIXME: implement
	cmdSystem->AddCommand( "clientVote",			idMultiplayerGame::Vote_f,	CMD_FL_GAME,				"cast your vote: clientVote yes | no" );
	cmdSystem->AddCommand( "clientCallVote",		idMultiplayerGame::CallVote_f,	CMD_FL_GAME,			"call a vote: clientCallVote si_.. proposed_value" );
	cmdSystem->AddCommand( "clientVoiceChat",		idMultiplayerGame::VoiceChat_f,	CMD_FL_GAME,			"voice chats: clientVoiceChat <sound shader>" );
	cmdSystem->AddCommand( "clientVoiceChatTeam",	idMultiplayerGame::VoiceChatTeam_f,	CMD_FL_GAME,		"team voice chats: clientVoiceChat <sound shader>" );
	cmdSystem->AddCommand( "serverVoiceMute", idMultiplayerGame::ServerVoiceMute_f, CMD_FL_GAME, "silence a voice sender for this connection: <client slot> <0|1>" );
	cmdSystem->AddCommand( "serverVoiceStats", idMultiplayerGame::ServerVoiceStats_f, CMD_FL_GAME, "report per-sender voice admission, forwarding and added delay" );
// RAVEN BEGIN
	// jshepard
	cmdSystem->AddCommand( "forceTeamChange",				idMultiplayerGame::ForceTeamChange_f,			CMD_FL_GAME,		"force team change: forceTeamChange <id>" );
	cmdSystem->AddCommand( "removeClientFromBanList",		idMultiplayerGame::RemoveClientFromBanList_f,	CMD_FL_GAME,		"removes a client id from the ban list: removeClientFromBanList <client id>" );

#ifndef _XBOX
// shouchard:  more voice chat stuff (non-XBOX)
	cmdSystem->AddCommand( "clientvoicemute",		idMultiplayerGame::VoiceMute_f, CMD_FL_GAME,			"mute the specified player's incoming voicechat" );
	cmdSystem->AddCommand( "clientvoiceunmute",		idMultiplayerGame::VoiceUnmute_f, CMD_FL_GAME,			"unmute the specified player's incoming voicechat" );
#endif // _XBOX
// RAVEN END

	// multiplayer server commands
	cmdSystem->AddCommand( "verifyServerSettings",	idGameLocal::VerifyServerSettings_f,	CMD_FL_GAME,	"verifies the game type can be played on the map" );
	cmdSystem->AddCommand( "serverMapRestart",		idGameLocal::MapRestart_f,	CMD_FL_GAME,				"restart the current game" );
	cmdSystem->AddCommand( "serverForceReady",		idMultiplayerGame::ForceReady_f,CMD_FL_GAME,				"force all players ready" );
	cmdSystem->AddCommand( "serverNextMap",			idGameLocal::NextMap_f,		CMD_FL_GAME,				"change to the next map" );
#endif

	cmdSystem->AddCommand( "CheckTeamBalance", idMultiplayerGame::CheckTeamBalance_f, CMD_FL_GAME, "helper for team switching in the guis - <team to switch to> <named event for yes> <named event for no> <named event for same team>" );

	// openQ4: explicit ready commands.  Quake 4 only exposed ready through
	// _impulse17 and the menu, both of which route through the throttled
	// ui_ready userinfo key.
	cmdSystem->AddCommand( "ready",					idMultiplayerGame::Ready_f,		CMD_FL_GAME,	"declare yourself ready to start the match" );
	cmdSystem->AddCommand( "notready",				idMultiplayerGame::NotReady_f,	CMD_FL_GAME,	"withdraw your ready state" );
	cmdSystem->AddCommand( "unready",				idMultiplayerGame::NotReady_f,	CMD_FL_GAME,	"withdraw your ready state" );
	cmdSystem->AddCommand( "readyup",				idMultiplayerGame::ReadyUp_f,	CMD_FL_GAME,	"toggle your ready state" );
	cmdSystem->AddCommand( "forceReady",			idMultiplayerGame::ForceReady_f,	CMD_FL_GAME,	"force a managed match through readiness using the typed authority path" );
	cmdSystem->AddCommand( "allready",				idMultiplayerGame::ForceReady_f,	CMD_FL_GAME,	"force all players ready" );
	cmdSystem->AddCommand( "matchSeriesBind",		idMultiplayerGame::SeriesBind_f,	CMD_FL_GAME,	"bind a current Duel connection after series recovery: matchSeriesBind <a|b> <client slot>" );
	cmdSystem->AddCommand( "matchBroadcaster",		idMultiplayerGame::Broadcaster_f,	CMD_FL_GAME,	"grant or revoke broadcaster observation for a current spectator: matchBroadcaster <client slot> <on|off>" );
	cmdSystem->AddCommand( "openq4_assertMPClientActive", Cmd_OpenQ4AssertMPClientActive_f, CMD_FL_GAME | CMD_FL_CHEAT, "fail validation unless the local multiplayer player is active and not spectating" );
	cmdSystem->AddCommand( "openq4_reportMPState", Cmd_OpenQ4ReportMPState_f, CMD_FL_GAME | CMD_FL_CHEAT, "report local multiplayer phases, scores and player state to the console" );
	cmdSystem->AddCommand( "openq4_reportMPWorld", Cmd_OpenQ4ReportMPWorld_f, CMD_FL_GAME | CMD_FL_CHEAT, "report one multiplayer entity's simulation, animation and powerup state" );
	cmdSystem->AddCommand( "lagCompStatus", Cmd_LagCompStatus_f, CMD_FL_GAME, "report each client's lag-compensation lead, round trip, advertised remote extrapolation and rewind" );
	cmdSystem->AddCommand( "openq4_launchMPTestProjectile", Cmd_OpenQ4LaunchMPTestProjectile_f, CMD_FL_GAME | CMD_FL_CHEAT, "launch a named stock projectile for multiplayer world-clock diagnostics" );
	cmdSystem->AddCommand( "openq4_matchControl", Cmd_OpenQ4MatchControl_f, CMD_FL_GAME | CMD_FL_CHEAT, "exercise and inspect the local accepted-view Match Control adapter without device input" );
	cmdSystem->AddCommand( "openq4_arenaCeremony", Cmd_OpenQ4ArenaCeremony_f, CMD_FL_GAME | CMD_FL_CHEAT, "drive the local Arena Campaign match without device input: openq4_arenaCeremony <win|loss|draw|skip|status>" );

	// localization help commands
	cmdSystem->AddCommand( "nextGUI",				Cmd_NextGUI_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"teleport the player to the next func_static with a gui" );
	cmdSystem->AddCommand( "testid",				Cmd_TestId_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"output the string for the specified id." );

// RAVEN BEGIN
// bdube: vehicle code
	cmdSystem->AddCommand( "killVehicles",			Cmd_KillVehicles_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"kills all vehicles" );
	cmdSystem->AddCommand( "killMessage",			Cmd_KillMessage_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"prints a fake death message" );
	cmdSystem->AddCommand( "apState",				Cmd_APState_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"prints AP state" );
// bdube: jump points
	cmdSystem->AddCommand( "jump",					Cmd_DebugJump_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"jumps to a specific debug jump point" );
	cmdSystem->AddCommand( "nextjumppoint",			Cmd_DebugNextJumpPoint_f,	CMD_FL_GAME|CMD_FL_CHEAT,	"jumps to the next debug jump point " );
	cmdSystem->AddCommand( "prevjumppoint",			Cmd_DebugPrevJumpPoint_f,	CMD_FL_GAME|CMD_FL_CHEAT,	"jumps to the previous debug jump point" );
// cdr: Added Extract Tactical
	cmdSystem->AddCommand( "extract_tactical",		Cmd_AASExtractTactical_f,	CMD_FL_GAME,				"pulls tactical information for the current position." );
// RAVEN END

// RAVEN BEGIN
// abahr
	cmdSystem->AddCommand( "call",					Cmd_CallScriptFunc_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"calls script function and prints out return val" );
	cmdSystem->AddCommand( "setPlayerGravity",		Cmd_SetPlayerGravity_f,		CMD_FL_GAME|CMD_FL_CHEAT,	"sets players local gravity" );
// cdr
	cmdSystem->AddCommand( "ai_debugFilter",		Cmd_AI_DebugFilter_f,		CMD_FL_GAME,				"ai_debugMove and ai_debugTactical only work on the specified entity (if none, does one you're looking at)", idGameLocal::ArgCompletion_AIName );
// ddynerman: multiple arena/CW stuff
	cmdSystem->AddCommand( "setInstance",			Cmd_SetInstance_f,			CMD_FL_GAME,				"sets a player's world instance" );
	cmdSystem->AddCommand( "addIcon",				Cmd_AddIcon_f,				CMD_FL_GAME|CMD_FL_CHEAT,	"adds a test icon" );
	cmdSystem->AddCommand( "listInstances",			Cmd_ListInstances_f,		CMD_FL_GAME,				"lists instances" );
// ddynerman: emote anims
	cmdSystem->AddCommand( "emote",					Cmd_PlayerEmote_f,			CMD_FL_GAME,				"plays an emote" );

	cmdSystem->AddCommand( "checkSave",				Cmd_CheckSave_f,			CMD_FL_GAME,				"tests save system" );

// jshepard: fade music in / out
	cmdSystem->AddCommand( "fadeSound",				Cmd_FadeSound_f,			CMD_FL_GAME|CMD_FL_CHEAT,	"fades all sound by X decibles over Y seconds" );
	cmdSystem->AddCommand( "testRadioChatter", Cmd_TestRadioChatter_f, CMD_FL_GAME|CMD_FL_CHEAT, "plays or probes the radio HUD audio graph", idCmdSystem::ArgCompletion_SoundName );
	cmdSystem->AddCommand( "testStatusBar", Cmd_TestStatusBar_f, CMD_FL_GAME|CMD_FL_CHEAT, "status bar: probe or stats <health> <armor>" );
	cmdSystem->AddCommand( "testWeaponWheel", Cmd_TestWeaponWheel_f, CMD_FL_GAME|CMD_FL_CHEAT, "weapon wheel: open, slot N, release, cancel, family marine|strogg, probe" );
	cmdSystem->AddCommand( "testQuickChat", Cmd_TestQuickChat_f, CMD_FL_GAME|CMD_FL_CHEAT, "quick-chat wheel: open, slot N, channel, release, cancel" );
	cmdSystem->AddCommand( "testScoreboard", Cmd_TestScoreboard_f, CMD_FL_GAME|CMD_FL_CHEAT, "holds the scoreboard open for <msec>" );

// mekberg: added.
	cmdSystem->AddCommand( "setPMCVars",			Cmd_SetPMCVars_f,			CMD_FL_GAME,				"Resets player movement cvars" );

	cmdSystem->AddCommand( "testClientModel",		Cmd_TestClientModel_f,		CMD_FL_GAME,				"" );
#ifndef _FINAL
	cmdSystem->AddCommand( "clientOverflowReliable", Cmd_ClientOverflowReliable_f, CMD_FL_GAME,				"" );
#endif
// RAVEN END
// RITUAL START
// squirrel: Mode-agnostic buymenus
	cmdSystem->AddCommand( "buyMenu",				Cmd_ToggleBuyMenu_f,		CMD_FL_GAME,				"Toggle buy menu (if in a buy zone and the game type supports it)" );
	cmdSystem->AddCommand( "buy",					Cmd_BuyItem_f,				CMD_FL_GAME,				"Buy an item (if in a buy zone and the game type supports it)" );
	// the Awakening's names for the same two commands (its buy menu and default binds)
	cmdSystem->AddCommand( "sq_buyMenu",				Cmd_ToggleBuyMenu_f,		CMD_FL_GAME,				"Toggle buy menu (if in a buy zone and the game type supports it)" );
	cmdSystem->AddCommand( "sq_buy",					Cmd_BuyItem_f,				CMD_FL_GAME,				"Buy an item (if in a buy zone and the game type supports it)" );
// RITUAL END

	// openQ4: multiplayer bots
	cmdSystem->AddCommand( "addbot",				Cmd_AddBot_f,				CMD_FL_GAME,				"addbot [name] [skill] [exact] - add a bot; exact requires that named character" );
	cmdSystem->AddCommand( "removebot",				Cmd_RemoveBot_f,			CMD_FL_GAME,				"remove a bot from the match, optionally by name" );
	cmdSystem->AddCommand( "kickbots",				Cmd_KickBots_f,				CMD_FL_GAME,				"remove every bot from the match" );
	cmdSystem->AddCommand( "botlist",				Cmd_BotList_f,				CMD_FL_GAME,				"list the bots in the match and the state of the navmesh" );
	cmdSystem->AddCommand( "botcharacters",			Cmd_BotCharacters_f,		CMD_FL_GAME,				"list the loaded bot characters, their play style and their skill band" );
	cmdSystem->AddCommand( "botreload",				Cmd_BotReload_f,			CMD_FL_GAME,				"re-read the bot style and character files without a map change" );
	cmdSystem->AddCommand( "navmesh",				Cmd_NavMesh_f,				CMD_FL_GAME,				"navmesh <build|info|probe x y z [radius]> - generate, report or probe the bot navigation mesh" );
	cmdSystem->AddCommand( "bot_probeJump", Cmd_BotProbeTraversal_f, CMD_FL_GAME, "replay a bot jump on an isolated local cheat server" );
	cmdSystem->AddCommand( "bot_probePad", Cmd_BotProbeTraversal_f, CMD_FL_GAME, "replay a bot pad flight on an isolated local cheat server" );
	cmdSystem->AddCommand( "bot_probeMove", Cmd_BotProbeTraversal_f, CMD_FL_GAME, "replay bot movement on an isolated local cheat server" );
	cmdSystem->AddCommand( "bot_probeDodge", Cmd_BotProbeTraversal_f, CMD_FL_GAME, "replay bot dodge admission on an isolated local cheat server" );
	cmdSystem->AddCommand( "bot_probeAir", Cmd_BotProbeTraversal_f, CMD_FL_GAME, "replay a fall under one fixed full air input on an isolated local cheat server" );
	cmdSystem->AddCommand( "bot_probeHealth", Cmd_BotProbeHealth_f, CMD_FL_GAME, "give the next traversal probe's bot this health on an isolated local cheat server" );
}

/*
=================
idGameLocal::ShutdownConsoleCommands
=================
*/
void idGameLocal::ShutdownConsoleCommands( void ) {
	cmdSystem->RemoveFlaggedCommands( CMD_FL_GAME );
}
