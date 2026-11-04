#ifndef __AWAKENING_SCRIPTEVENTS_H__
#define __AWAKENING_SCRIPTEVENTS_H__

/*
===============================================================================

	Script events the expansion adds to Quake 4's (q4xbase/scripts/events.script).

	Both game modules need every one of them: scripts/main.script declares them
	all, and the script compiler refuses an unknown scriptEvent before a single
	map can load. resetTalkCount belongs to idAI and is defined by openQ4-game.

===============================================================================
*/

extern const idEventDef EV_SpeederBike_SetSpeed;
extern const idEventDef EV_SpeederBike_SetMaxGravityDistance;
extern const idEventDef EV_SpeederBike_SetBoostEnabled;

extern const idEventDef EV_Valkaryne_Docked;
extern const idEventDef EV_Valkaryne_Undock;

extern const idEventDef EV_PainLord_RequestProcessing;
extern const idEventDef EV_PainLord_AtProcessingStation;

// Control-zone queries of the unfinished Control Zone multiplayer mode. No
// shipped script calls them, so no thread answers them.
extern const idEventDef EV_Thread_ControlPointController;
extern const idEventDef EV_Thread_ControlPercentToWin;

// The Falcon has three views of one ship: on foot and in either turret.
bool Awakening_DispatchShipStatus( int guiEnum, const char *eventName );

#endif /* !__AWAKENING_SCRIPTEVENTS_H__ */
