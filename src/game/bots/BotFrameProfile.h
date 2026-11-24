/*
Quake 4 SDK EULA applies. See src/game/LICENSE and repository provenance.
*/
#ifndef __GAME_MP_BOT_FRAME_PROFILE_H__
#define __GAME_MP_BOT_FRAME_PROFILE_H__

#include <cmath>

// Bounded diagnostic storage. Percentiles report conservative quarter-ms
// bucket ceilings; the overflow bucket reports the observed maximum.
class rvBotFrameProfile {
public:
	rvBotFrameProfile() { Clear(); }
	void Clear() {
		for ( unsigned int &count : buckets ) { count = 0; }
		samples = 0;
		maximum = 0.0;
	}
	void Add( double milliseconds ) {
		if ( !std::isfinite( milliseconds ) || milliseconds < 0.0 ) { return; }
		const unsigned int bucket = milliseconds >= 64.0 ? 256u :
			static_cast<unsigned int>( milliseconds * 4.0 );
		++buckets[ bucket ];
		++samples;
		if ( milliseconds > maximum ) { maximum = milliseconds; }
	}
	double PercentileUpperMS( unsigned int percent ) const {
		if ( samples == 0 ) { return 0.0; }
		if ( percent > 100 ) { percent = 100; }
		const unsigned long long rank =
			( static_cast<unsigned long long>( samples ) * percent + 99 ) / 100;
		unsigned long long count = 0;
		for ( unsigned int bucket = 0; bucket != 257; ++bucket ) {
			count += buckets[ bucket ];
			if ( count >= ( rank > 0 ? rank : 1 ) ) {
				return bucket == 256 ? maximum : ( bucket + 1 ) * 0.25;
			}
		}
		return maximum;
	}
	unsigned int Samples() const { return samples; }

private:
	unsigned int buckets[257];
	unsigned int samples;
	double maximum;
};

#endif
