// openQ4 MP-22: autoaction words and the match basename (src/game/mp/AutoAction.h).
#include <cstdio>
#include <cstring>
#include <string>

#include "../../../src/game/mp/AutoAction.h"

static int failures = 0;

static void Check( bool condition, const char *what ) {
	if ( !condition ) {
		std::printf( "FAILED: %s\n", what );
		failures++;
	}
}

static std::string Name( const char *gameType, const char *map, const char *player ) {
	char out[ 128 ];
	MPBuildMatchBasename( out, sizeof( out ), 2026, 11, 23, 16, 15, 2, gameType, map, player );
	return out;
}

int main() {
	Check( MPAutoActionWords( "demo ss stats" ) == ( MP_AUTOACTION_DEMO | MP_AUTOACTION_SHOT | MP_AUTOACTION_STATS ),
		"all three words" );
	Check( MPAutoActionWords( "DEMO,screenshot" ) == ( MP_AUTOACTION_DEMO | MP_AUTOACTION_SHOT ), "commas, case and the long word" );
	Check( MPAutoActionWords( "" ) == 0 && MPAutoActionWords( NULL ) == 0, "nothing" );
	Check( MPAutoActionWords( "  stats   video demo2 " ) == MP_AUTOACTION_STATS, "unknown words are ignored" );
	Check( MPAutoActionWords( "demodemodemodemodemo demo" ) == MP_AUTOACTION_DEMO, "an overlong word is not a prefix match" );

	Check( Name( "Team DM", "mp/q4dm1", "^1Re^7corder" ) == "20261123-161502_team_dm_q4dm1_recorder", "the usual name" );
	Check( Name( "DM", "mp/q4dm1", NULL ) == "20261123-161502_dm_q4dm1", "a server's name has no player" );
	Check( Name( "CTF", "q4ctf1", "!!!" ) == "20261123-161502_ctf_q4ctf1", "a name with nothing left adds nothing" );
	Check( Name( "", "mp/q4dm2", "a  b" ) == "20261123-161502_q4dm2_a_b", "no doubled separators" );
	const std::string longName = Name( "Tourney", "mp/q4dm1", "an extraordinarily long player name that keeps going on" );
	Check( longName.size() <= static_cast<size_t>( MP_MATCH_BASENAME_MAX ) && longName.back() != '_', "kept to 64 characters" );
	char small[ 8 ];
	MPBuildMatchBasename( small, sizeof( small ), 2026, 1, 1, 0, 0, 0, "DM", "mp/q4dm1", "p" );
	Check( small[ 0 ] == '\0', "a buffer too small for the date stays empty" );

	if ( failures != 0 ) {
		std::printf( "mp autoaction: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "mp autoaction: all checks passed\n" );
	return 0;
}
