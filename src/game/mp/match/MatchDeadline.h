/*
===============================================================================

  Competitive-match gameplay time helpers

  The engine clock keeps advancing during a competitive pause.  Gameplay
  objects which retain absolute engine-time origins or deadlines therefore
  move those values forward by the frozen frame delta.  These helpers keep
  the operation consistent and avoid signed overflow on long-running servers.

===============================================================================
*/

#ifndef __MP_MATCH_DEADLINE_H__
#define __MP_MATCH_DEADLINE_H__

inline int mpMatchAddMillisecondsClamped( int value, int deltaMsec ) {
	if ( deltaMsec <= 0 ) {
		return value;
	}

	const int maxTime = 0x7fffffff;
	if ( value > maxTime - deltaMsec ) {
		return maxTime;
	}
	return value + deltaMsec;
}

// Optional deadlines use zero (and sometimes -1) as an inactive sentinel.
inline void mpMatchShiftOptionalDeadline( int &deadline, int deltaMsec ) {
	if ( deadline > 0 ) {
		deadline = mpMatchAddMillisecondsClamped( deadline, deltaMsec );
	}
}

// Time origins may legitimately be zero when an object begins on the first
// map frame.  Negative values remain reserved sentinels.
inline void mpMatchShiftTimeOrigin( int &origin, int deltaMsec ) {
	if ( origin >= 0 ) {
		origin = mpMatchAddMillisecondsClamped( origin, deltaMsec );
	}
}

inline void mpMatchShiftTimeOrigin( float &origin, int deltaMsec ) {
	if ( origin < 0.0f || deltaMsec <= 0 ) {
		return;
	}
	const float maxTime = 2147483647.0f;
	origin = origin > maxTime - static_cast<float>( deltaMsec )
		? maxTime
		: origin + static_cast<float>( deltaMsec );
}

#endif /* !__MP_MATCH_DEADLINE_H__ */
