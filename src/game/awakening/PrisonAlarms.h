/*
 * openQ4 Awakening sound pacing. See LICENSES/QUAKE-4-SDK-EULA.rtf.
 * Uses the installed recordings; no replacement sound declarations or save fields.
 */
#ifndef __AWAKENING_PRISON_ALARMS_H__
#define __AWAKENING_PRISON_ALARMS_H__

// Revision 1 (4 November 2026) made the alarms sparse and quiet; revision 2
// restores a steady siren rhythm. The revision lives in the speaker's spawn
// args, so saves made under an older revision are re-paced on load.
static const int AWAKENING_PRISON_ALARM_REVISION = 2;

static bool Awakening_RepairPrisonAlarm( idDict &args ) {
	if ( gameLocal.isMultiplayer || !FS_AwakeningActive() ||
		idStr::Icmp( args.GetString( "classname" ), "speaker" ) ) {
		return false;
	}
	// An unrevised speaker with its own wait is custom pacing: leave it alone.
	const int revision = args.GetInt( "oq4_prisonAlarm" );
	if ( revision >= AWAKENING_PRISON_ALARM_REVISION ||
		( revision == 0 && args.GetFloat( "wait" ) != 0.0f ) ) {
		return false;
	}
	idStr map = gameLocal.GetMapName();
	map.BackSlashesToSlashes();
	map.StripFileExtension();
	if ( map.Icmp( "maps/game/m04_prison" ) ||
		!gameLocal.program.FindFunction( "unlockSecRoomDoors" ) ) {
		return false;
	}
	const char *name = args.GetString( "name" );
	const char *shader = args.GetString( "s_shader" );
	// Earlier revisions already swapped the computers to their one-shot decls.
	const bool security = !idStr::Icmp( name, "security_alarm_1" ) &&
		!idStr::Icmp( shader, "sound/ambience/alarms_buzzers/1shot_alarm01_q4xdm7.wav" );
	const bool computer3 = ( !idStr::Icmp( name, "compy_alarm1" ) || !idStr::Icmp( name, "compy_alarm2" ) ) &&
		( !idStr::Icmp( shader, "q4x_computer_alarm3" ) ||
		( revision > 0 && !idStr::Icmp( shader, "sound/ambience/m05/computer_alarm3.ogg" ) ) );
	const bool computer1 = !idStr::Icmp( name, "alarm_compy2" ) &&
		( !idStr::Icmp( shader, "q4x_computer_alarm1" ) ||
		( revision > 0 && !idStr::Icmp( shader, "sound/ambience/m05/computer_alarm1.ogg" ) ) );
	if ( !security && !computer3 && !computer1 ) {
		return false;
	}
	// These computer declarations contain their own LOOPING flag. A false
	// s_looping cannot clear it: use the same recording's implicit one-shot decl.
	if ( !security ) {
		args.Set( "s_shader", computer3 ? "sound/ambience/m05/computer_alarm3.ogg" :
			"sound/ambience/m05/computer_alarm1.ogg" );
	}
	args.SetInt( "oq4_prisonAlarm", AWAKENING_PRISON_ALARM_REVISION );
	args.SetBool( "s_looping", false );
	args.SetBool( "s_global", false );
	// The 1.5-second siren repeats every 5-6 seconds: a clear alarm rhythm with
	// room between pulses for dialogue. Its wide inner radius keeps it present
	// across the security wing; the original was global, looping and at 0 dB.
	// The four-second computer alarms stay below it as local machinery.
	args.SetFloat( "s_volume", security ? -6.0f : -12.0f );
	args.SetFloat( "s_mindistance", security ? 320.0f : 96.0f );
	args.SetFloat( "s_maxdistance", security ? 2000.0f : 560.0f );
	args.SetFloat( "wait", security ? 5.5f : 12.0f );
	args.SetFloat( "random", security ? 0.5f : 3.0f );
	// idSound's timer owns the on/off state even during a quiet interval.
	// Preserve waitfortrigger so the authored security callbacks still toggle it.
	return true;
}

#endif
