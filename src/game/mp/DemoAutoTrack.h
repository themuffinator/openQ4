// openQ4 multi-view demo camera auto-tracking. SDK EULA: see src/game/LICENSE.
#ifndef OPENQ4_MP_DEMO_AUTO_TRACK_H
#define OPENQ4_MP_DEMO_AUTO_TRACK_H

// Chooses whom a camera follows: a multi-view demo's playback camera, or a
// live spectator's on the server. Killer mode moves to the player who killed
// the one being followed, after a beat so the death is seen. Action mode
// follows the most interesting player: a flag carrier, then powerups and recent
// kills. A switch holds for a minimum dwell, and action mode needs a clear lead
// over the current target, so the camera does not flicker between equals.
// Leader, powerup and flag modes each follow one subject: the top score, the
// player carrying the most powerups, or a flag carrier. While nobody qualifies
// the camera stays where it is, and a subject who only ties the followed
// player does not take the camera.
enum {
	MP_AUTOTRACK_OFF = 0,
	MP_AUTOTRACK_KILLER,
	MP_AUTOTRACK_ACTION,
	MP_AUTOTRACK_LEADER,
	MP_AUTOTRACK_POWERUP,
	MP_AUTOTRACK_FLAG,
	MP_AUTOTRACK_MODE_COUNT
};

static const int MP_AUTOTRACK_CLIENTS = 32;
static const int MP_AUTOTRACK_KILL_MEMORY = 4;
static const int MP_AUTOTRACK_KILL_WINDOW_MS = 10000;
static const int MP_AUTOTRACK_KILLER_DELAY_MS = 1500;
static const int MP_AUTOTRACK_DWELL_MS = 3000;
static const int MP_AUTOTRACK_SWITCH_MARGIN = 100;

struct mpAutoTrackPlayer {
	bool eligible = false;	// the recording lets this camera follow them
	bool alive = false;
	int team = -1;
	bool flag = false;
	int powerups = 0;
	int score = 0;
};

class mpDemoAutoTrack {
public:
	mpDemoAutoTrack() { Reset(); }

	void Reset() {
		lastSwitchTime = -MP_AUTOTRACK_DWELL_MS;
		pendingTarget = -1;
		pendingTime = 0;
		for ( int i = 0; i < MP_AUTOTRACK_CLIENTS; i++ ) {
			for ( int k = 0; k < MP_AUTOTRACK_KILL_MEMORY; k++ ) {
				killTimes[i][k] = -MP_AUTOTRACK_KILL_WINDOW_MS - 1;
			}
		}
	}

	void NoteKill( int attacker, int victim, int followed, int now ) {
		if ( attacker < 0 || attacker >= MP_AUTOTRACK_CLIENTS || attacker == victim ) {
			return;
		}
		int oldest = 0;
		for ( int k = 1; k < MP_AUTOTRACK_KILL_MEMORY; k++ ) {
			if ( killTimes[attacker][k] < killTimes[attacker][oldest] ) {
				oldest = k;
			}
		}
		killTimes[attacker][oldest] = now;
		if ( victim >= 0 && victim == followed ) {
			pendingTarget = attacker;
			pendingTime = now + MP_AUTOTRACK_KILLER_DELAY_MS;
		}
	}

	// After a backward seek, kills from the abandoned future have a negative
	// age and do not count.
	int RecentKills( int client, int now ) const {
		int count = 0;
		for ( int k = 0; k < MP_AUTOTRACK_KILL_MEMORY; k++ ) {
			const int age = now - killTimes[client][k];
			if ( age >= 0 && age <= MP_AUTOTRACK_KILL_WINDOW_MS ) {
				count++;
			}
		}
		return count;
	}

	int Interest( const mpAutoTrackPlayer players[MP_AUTOTRACK_CLIENTS], int client, int leaderScore, int now ) const {
		const mpAutoTrackPlayer &player = players[client];
		int interest = ( player.flag ? 1000 : 0 ) + player.powerups * 300 + RecentKills( client, now ) * 150;
		interest += player.alive ? 50 : 0;
		interest += player.score == leaderScore ? 100 : 0;
		return interest;
	}

	// How strongly a single-subject mode wants this player; 0 is not at all.
	static int SubjectRank( int mode, const mpAutoTrackPlayer &player, int leaderScore ) {
		switch ( mode ) {
		case MP_AUTOTRACK_LEADER:
			return player.score == leaderScore ? 1 : 0;
		case MP_AUTOTRACK_POWERUP:
			return player.alive ? player.powerups : 0;
		case MP_AUTOTRACK_FLAG:
			return player.alive && player.flag ? 1 : 0;
		default:
			return 0;
		}
	}

	// The client to follow from now, or `followed` to keep the camera, or -1
	// when nobody eligible matches the team filter (-1 accepts every team).
	int Choose( int mode, int teamFilter, int followed, const mpAutoTrackPlayer players[MP_AUTOTRACK_CLIENTS], int now ) {
		if ( mode <= MP_AUTOTRACK_OFF || mode >= MP_AUTOTRACK_MODE_COUNT ) {
			return followed;
		}
		const auto candidate = [&]( int client ) {
			return client >= 0 && client < MP_AUTOTRACK_CLIENTS && players[client].eligible &&
				( teamFilter < 0 || players[client].team == teamFilter );
		};
		int leaderScore = -0x7fffffff;
		for ( int i = 0; i < MP_AUTOTRACK_CLIENTS; i++ ) {
			if ( candidate( i ) && players[i].score > leaderScore ) {
				leaderScore = players[i].score;
			}
		}
		int best = -1;
		int bestInterest = -1;
		for ( int i = 0; i < MP_AUTOTRACK_CLIENTS; i++ ) {
			if ( candidate( i ) ) {
				const int interest = Interest( players, i, leaderScore, now );
				if ( interest > bestInterest ) {
					best = i;
					bestInterest = interest;
				}
			}
		}
		if ( best < 0 ) {
			pendingTarget = -1;
			return -1;
		}
		if ( mode >= MP_AUTOTRACK_LEADER ) {
			// A kill noted meanwhile must not fire when killer mode returns.
			pendingTarget = -1;
			int subject = -1;
			int subjectRank = 0;
			for ( int i = 0; i < MP_AUTOTRACK_CLIENTS; i++ ) {
				if ( !candidate( i ) ) {
					continue;
				}
				const int rank = SubjectRank( mode, players[i], leaderScore );
				if ( rank > subjectRank || ( rank > 0 && rank == subjectRank && i == followed ) ) {
					subject = i;
					subjectRank = rank;
				}
			}
			if ( !candidate( followed ) ) {
				return Switch( subject >= 0 ? subject : best, now );
			}
			if ( subject >= 0 && subject != followed && Settled( now ) ) {
				return Switch( subject, now );
			}
			return followed;
		}
		if ( !candidate( followed ) ) {
			return Switch( best, now );
		}
		if ( mode == MP_AUTOTRACK_KILLER ) {
			if ( pendingTarget >= 0 && now >= pendingTime ) {
				// A kill noted long ago, or while another mode ran, is stale.
				const int target = pendingTarget;
				const bool fresh = now - pendingTime <= MP_AUTOTRACK_DWELL_MS;
				pendingTarget = -1;
				if ( fresh && candidate( target ) && players[target].alive && target != followed ) {
					return Switch( target, now );
				}
			}
			return followed;
		}
		// Action mode does not follow killers; never carry one into killer mode.
		pendingTarget = -1;
		if ( best != followed && now - lastSwitchTime >= MP_AUTOTRACK_DWELL_MS &&
			 bestInterest >= Interest( players, followed, leaderScore, now ) + MP_AUTOTRACK_SWITCH_MARGIN ) {
			return Switch( best, now );
		}
		return followed;
	}

private:
	int lastSwitchTime = -MP_AUTOTRACK_DWELL_MS;
	int pendingTarget = -1;
	int pendingTime = 0;
	int killTimes[MP_AUTOTRACK_CLIENTS][MP_AUTOTRACK_KILL_MEMORY] = {};

	int Switch( int target, int now ) {
		lastSwitchTime = now;
		return target;
	}

	bool Settled( int now ) const {
		return now - lastSwitchTime >= MP_AUTOTRACK_DWELL_MS;
	}
};
#endif
