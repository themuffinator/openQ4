#include "src/game/bots/BotFrameProfile.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static void Check( bool condition ) {
	if ( !condition ) { std::abort(); }
}

int main() {
	rvBotFrameProfile profile;
	Check( profile.Samples() == 0 && profile.PercentileUpperMS( 95 ) == 0.0 );
	for ( int i = 0; i < 95; ++i ) { profile.Add( 1.1 ); }
	for ( int i = 0; i < 4; ++i ) { profile.Add( 7.99 ); }
	profile.Add( 80.5 );
	Check( profile.Samples() == 100 );
	Check( profile.PercentileUpperMS( 50 ) == 1.25 );
	Check( profile.PercentileUpperMS( 95 ) == 1.25 );
	Check( profile.PercentileUpperMS( 99 ) == 8.0 );
	Check( profile.PercentileUpperMS( 100 ) == 80.5 );
	Check( profile.PercentileUpperMS( 101 ) == 80.5 );
	profile.Add( -1 );
	profile.Add( std::numeric_limits<double>::quiet_NaN() );
	profile.Add( std::numeric_limits<double>::infinity() );
	Check( profile.Samples() == 100 );
	profile.Clear();
	Check( profile.Samples() == 0 && profile.PercentileUpperMS( 95 ) == 0.0 );
	profile.Add( 0.0 );
	Check( profile.PercentileUpperMS( 0 ) == 0.25 );
	profile.Clear();
	profile.Add( 64.0 );
	Check( profile.PercentileUpperMS( 95 ) == 64.0 );
	std::puts( "bot frame profile: PASS" );
}
