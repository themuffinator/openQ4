/*
 * openQ4 Awakening sound pacing. See LICENSES/QUAKE-4-SDK-EULA.rtf.
 * Uses the installed recordings; no replacement sound declarations or save fields.
 */
#ifndef __AWAKENING_PRISON_ALARMS_H__
#define __AWAKENING_PRISON_ALARMS_H__

static bool Awakening_RepairPrisonAlarm( idDict &args ) {
	if ( gameLocal.isMultiplayer || idStr::Icmp( fileSystem->GetActiveGameDir(), "q4xbase" ) ||
		args.GetBool( "oq4_prisonAlarm" ) || idStr::Icmp( args.GetString( "classname" ), "speaker" ) ||
		args.GetFloat( "wait" ) != 0.0f ) {
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
	const bool security = !idStr::Icmp( name, "security_alarm_1" ) &&
		!idStr::Icmp( shader, "sound/ambience/alarms_buzzers/1shot_alarm01_q4xdm7.wav" );
	const bool computer3 = ( !idStr::Icmp( name, "compy_alarm1" ) || !idStr::Icmp( name, "compy_alarm2" ) ) &&
		!idStr::Icmp( shader, "q4x_computer_alarm3" );
	const bool computer1 = !idStr::Icmp( name, "alarm_compy2" ) &&
		!idStr::Icmp( shader, "q4x_computer_alarm1" );
	if ( !security && !computer3 && !computer1 ) {
		return false;
	}
	// These computer declarations contain their own LOOPING flag. A false
	// s_looping cannot clear it: use the same recording's implicit one-shot decl.
	if ( !security ) {
		args.Set( "s_shader", computer3 ? "sound/ambience/m05/computer_alarm3.ogg" :
			"sound/ambience/m05/computer_alarm1.ogg" );
	}
	args.SetBool( "oq4_prisonAlarm", true );
	args.SetBool( "s_looping", false );
	args.SetBool( "s_global", false );
	args.SetFloat( "s_volume", security ? -12.0f : -18.0f );
	args.SetFloat( "s_mindistance", security ? 96.0f : 40.0f );
	args.SetFloat( "s_maxdistance", security ? 1400.0f : 360.0f );
	args.SetFloat( "wait", security ? 14.0f : 24.0f );
	args.SetFloat( "random", security ? 2.0f : 5.0f );
	// idSound's timer owns the on/off state even during a quiet interval.
	// Preserve waitfortrigger so the authored security callbacks still toggle it.
	return true;
}

#endif
