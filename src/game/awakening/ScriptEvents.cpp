#include "../Game_local.h"

#include "ScriptEvents.h"

const idEventDef EV_SpeederBike_SetSpeed( "setSpeederBikeSpeed", "ff" );
const idEventDef EV_SpeederBike_SetMaxGravityDistance( "setSpeederBikeMaxGravityDistance", "ff" );
const idEventDef EV_SpeederBike_SetBoostEnabled( "setSpeederBikeBoostEnabled", "f" );

const idEventDef EV_Valkaryne_Docked( "docked" );
const idEventDef EV_Valkaryne_Undock( "undock" );

const idEventDef EV_PainLord_RequestProcessing( "requestProcessing" );
const idEventDef EV_PainLord_AtProcessingStation( "atProcessingStation" );

const idEventDef EV_Thread_ControlPointController( "controlPointController", NULL, 'd' );
const idEventDef EV_Thread_ControlPercentToWin( "controlPercentToWin", NULL, 'f' );

bool Awakening_DispatchShipStatus( int guiEnum, const char *eventName ) {
	if ( ( guiEnum != 0 && guiEnum != 2 ) || gameLocal.isMultiplayer ||
		idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) ) {
		return false;
	}
	idStr map = gameLocal.GetMapName();
	map.BackSlashesToSlashes();
	map.StripFileExtension();
	if ( map.Icmp( "maps/game/m03_airassault" ) ) {
		return false;
	}
	bool shipEvent = false;
	idStr persistentKey;
	int persistentValue = 0;
	for ( int i = 1; i <= 5; ++i ) {
		for ( const char *state : { "on", "off" } ) {
			if ( !idStr::Icmp( eventName, va( "ship_health_%02d_%s", i, state ) ) ) {
				persistentKey = va( "oq4.ship.damage%d", i );
			} else if ( !idStr::Icmp( eventName, va( "ship_target_%02d_%s", i, state ) ) ) {
				persistentKey = va( "oq4.ship.target%d", i );
			} else {
				continue;
			}
			shipEvent = true;
			persistentValue = !idStr::Icmp( state, "on" );
		}
	}
	for ( const char *direction : { "N", "NE", "E", "SE", "S", "SW", "W", "NW" } ) {
		shipEvent |= !idStr::Icmp( eventName, va( "warning_%s", direction ) );
	}
	if ( !shipEvent ) {
		return false;
	}

	idPlayer *player = gameLocal.GetLocalPlayer();
	idUserInterface *views[3] = { player ? player->hud : NULL, NULL, NULL };
	const char *guns[] = { "topgun", "bottomgun" };
	for ( int i = 0; i < 2; ++i ) {
		idEntity *entity = gameLocal.FindEntity( guns[i] );
		if ( entity && entity->IsType( rvVehicle::Type ) ) {
			views[i + 1] = static_cast<rvVehicle *>( entity )->GetHud();
		}
	}
	for ( int i = 0; i < 3; ++i ) {
		if ( !views[i] ) {
			continue;
		}
		bool duplicate = false;
		for ( int j = 0; j < i; ++j ) {
			duplicate |= views[i] == views[j];
		}
		if ( !duplicate ) {
			// The on-foot alpha GUI lacks these event handlers. Keep lasting state
			// in its serialized dictionary too, so a restored Remastered view joins
			// the same damage/objectives even if it was never drawn before saving.
			if ( !persistentKey.IsEmpty() ) {
				views[i]->SetStateInt( persistentKey, persistentValue );
				views[i]->SetStateInt( "oq4.ship", 1 );
			}
			views[i]->HandleNamedEvent( eventName );
		}
	}
	return true;
}
