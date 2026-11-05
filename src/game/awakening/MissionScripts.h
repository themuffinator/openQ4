/* openQ4 mission corrections; see LICENSES/QUAKE-4-SDK-EULA.rtf. */
#ifndef __AWAKENING_MISSIONSCRIPTS_H__
#define __AWAKENING_MISSIONSCRIPTS_H__

// A virtual source name distinguishes corrected fresh maps from legacy saves.
const char *Awakening_MissionScriptName( const char *filename );
bool Awakening_LoadMissionScript( const char *filename, idStr &source );
void Awakening_PrepareM05M06( idDict &worldArgs );
void Awakening_CorrectM05M06Entity( idDict &args );
void Awakening_PrepareLateMission( idDict &worldArgs );
void Awakening_DirectLateMissionEntity( idDict &args );

void Awakening_CorrectLateMissionMedia( idDict &args );
void Awakening_StartLateMissionDetails();

#endif
