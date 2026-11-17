// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/sound/SoundDialogue.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

static void Check( bool ok, const char* message ) {
	if ( !ok ) { std::fprintf( stderr, "%s\n", message ); std::exit( 1 ); }
}

int main() {
	for ( const char* path : { "sound/vo/airdefense/line", "sound/vo_english/ai/line.ogg",
		"sound/vo_french/radio.wav", "SOUND\\VO_GERMAN\\line.wav", "sound/vo/awakening/line" } ) {
		Check( SoundIsDialogue( false, false, path ), path );
		Check( !SoundIsDialogue( true, false, path ), "Music bus has precedence" );
	}
	for ( const char* path : { "", "sound/vo", "sound/vo_/bad", "sound/vo_english", "sound/voicegun/fire",
		"sound/guis/menu.wav", "sound/weapons/darkmatter/rings", "sound/marine/player/pain",
		"sound/musical/score", "sound/ambience/room", "other/sound/vo/line" } ) {
		Check( !SoundIsDialogue( false, false, path ), path );
		Check( SoundIsDialogue( false, true, path ), "Explicit lip-sync/VO-for-player speech" );
	}
	// Script lines from shader descriptions: words survive, directions and notes do not.
	struct Case { const char* description; const char* caption; };
	for ( const Case& c : std::initializer_list<Case>{
		{ "HQ, this is Eagle 8. Everything is in the green.", "HQ, this is Eagle 8. Everything is in the green." },
		{ "  Defenses compromised.  All units\tproceed. ", "Defenses compromised. All units proceed." },
		{ "<pilot_2:> Battle stations.", "Battle stations." },
		{ "<Yoshida :> What's that?", "What's that?" },
		{ "(whispering) Get down. [beat] Now!", "Get down. Now!" },
		{ "Kane (coughs) , wait.", "Kane, wait." },
		{ "Detonation in 3 . . 2 . . . 1.", "Detonation in 3 . . 2 . . . 1." },
		{ "We are in transit now. 1_2_1_105_2MARINE #1Roger that.", "We are in transit now." },
		{ "Warning: Bridge security compromised.", "Warning: Bridge security compromised." },
		{ "At 95%.", "At 95%." },
		{ "Ooorah!", "Ooorah!" },
		{ "Shit.", "Shit." },
		{ "<dekard:> .And so do you.", "And so do you." },
		{ "...and then", "...and then" },
		{ "Corporal, go see Lt.Voss now!", "Corporal, go see Lt.Voss now!" },
		{ "I\xe2\x80\x99ve got signal lock.", "I\xe2\x80\x99ve got signal lock." } } ) {
		std::string caption;
		Check( SoundScriptCaption( c.description, caption ) && caption == c.caption, c.description );
	}
	for ( const char* description : { "", "   ", "(pain grunt)", "(death yell)", "<Eagle15 Pilot:> (Wilhelm scream)",
		"[static]", "laugh", "Laughter", "death", "Pain", "Slow, ragged breathing #1.", "...", "- -", "(unclosed direction",
		"1_1_1_150_2" } ) {
		std::string caption = "stale";
		Check( !SoundScriptCaption( description, caption ) && caption.empty(), description );
	}
	std::puts( "Sound dialogue classification passed" );
}
