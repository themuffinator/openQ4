/* openQ4 mission corrections; see LICENSES/QUAKE-4-SDK-EULA.rtf. */
#ifndef __AWAKENING_MISSIONSCRIPTS_H__
#define __AWAKENING_MISSIONSCRIPTS_H__

// A virtual source name distinguishes corrected fresh maps from legacy saves.
const char *Awakening_MissionScriptName( const char *filename );
bool Awakening_LoadMissionScript( const char *filename, idStr &source );

#endif
