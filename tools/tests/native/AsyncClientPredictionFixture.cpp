// Executes the production client prediction controller from src/framework/async/AsyncClient.cpp.
//
// tools/tests/mp_client_prediction_state.py splices the snapshot timing block - the resync
// and the clientPrediction/clientPredictTime adjustment - into SnapshotTiming() below.  The
// harness checks each step's direction, then closes the loop: a server and a client trade
// commands and snapshots over a fixed one-way delay, and the controller must bring the
// client's commands to the server ahead of the frame that uses them, with no resyncs once
// it has settled.  Before the fix a client behind its target was slammed to full slowdown
// and ended one frame behind the server with every command late.  A last loop stalls the
// client's frame again and again, as a hitch does, under delay and jitter: before the
// 13 November fix the catch-up climbed with each hitch to its clamp, and from then on every
// command the client sent reached the server late.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// PRODUCTION_SIGN_MACRO

namespace idMath {
inline int ClampInt( int min, int max, int value ) {
	return value < min ? min : ( value > max ? max : value );
}
}

// PRODUCTION_FAST_ADJUST

static const int FRAME_MSEC = 16;
static const int MAX_PREDICTION_MSEC = 1000;

static int AsyncClient_MaxPredictionMsec() {
	return MAX_PREDICTION_MSEC;
}

static int AsyncClient_ConfiguredPredictionMsec( int ) {
	return FRAME_MSEC;
}

struct ClientTiming {
	int gameFrame = 0;
	int gameTime = 0;
	int gameTimeResidual = 0;
	int clientPredictTime = 0;
	int clientPrediction = 0;
	int snapshotGameFrame = 0;
	int snapshotGameTime = 0;
};

static bool SnapshotTiming( ClientTiming &client, int aheadOfServer ) {
	int &gameFrame = client.gameFrame;
	int &gameTime = client.gameTime;
	int &gameTimeResidual = client.gameTimeResidual;
	int &clientPredictTime = client.clientPredictTime;
	int &clientPrediction = client.clientPrediction;
	int &snapshotGameTime = client.snapshotGameTime;
	const int snapshotGameFrame = client.snapshotGameFrame;
	int delta = 0;
	(void)snapshotGameFrame;
	(void)delta;

	// PRODUCTION_SNAPSHOT_TIMING

	return predictionResynced;
}

static int failures = 0;

static void Expect( bool condition, const char *what ) {
	if ( !condition ) {
		std::printf( "FAILED: %s\n", what );
		failures++;
	}
}

// One snapshot read with the client predicted to gameTime and the server reporting how early
// this client's commands arrive.
static ClientTiming Step( int lead, int prediction, int predictTime, int aheadOfServer ) {
	ClientTiming client;
	client.snapshotGameFrame = 1000;
	client.snapshotGameTime = 1000 * FRAME_MSEC;
	client.gameTime = client.snapshotGameTime + lead;
	client.gameFrame = client.gameTime / FRAME_MSEC;
	client.clientPrediction = prediction;
	client.clientPredictTime = predictTime;
	SnapshotTiming( client, aheadOfServer );
	return client;
}

static void CheckDirections() {
	// Behind its target (delta < 0): the catch-up grows so more frames are predicted.
	ClientTiming behind = Step( 16, 60, 0, 0 );
	Expect( behind.clientPrediction == 61, "late commands raise the target by one" );
	Expect( behind.clientPredictTime > 0 && behind.clientPredictTime < 20,
		"a client behind its target speeds up by a bounded step" );

	// A small shortfall moves the catch-up by the one-unit step alone.
	ClientTiming nudge = Step( 16, 18, 0, 40 );
	Expect( nudge.clientPrediction == 17, "early commands lower the target by one" );
	Expect( nudge.clientPredictTime == 1, "a one-millisecond shortfall still speeds up" );

	// Ahead of its target (delta > 0): the catch-up shrinks.
	ClientTiming ahead = Step( 160, 40, 0, 40 );
	Expect( ahead.clientPrediction == 39, "early commands lower the target" );
	Expect( ahead.clientPredictTime < 0 && ahead.clientPredictTime > -40,
		"a client ahead of its target slows down by a bounded step" );

	// Exactly on target: the one-unit step slows it, as the original controller did.
	ClientTiming exact = Step( 39, 40, 0, 40 );
	Expect( exact.clientPredictTime == -1, "on target, the step is -1" );

	// A snapshot newer than the client's predicted time, or one the client has run more than the
	// maximum past, puts the client back on the snapshot's frame.
	ClientTiming late = Step( -64, 60, 0, -80 );
	Expect( late.gameFrame == 1000 && late.gameTime == 1000 * FRAME_MSEC, "a client behind the snapshot resyncs to it" );
	ClientTiming runaway = Step( MAX_PREDICTION_MSEC + 16, 60, 0, 900 );
	Expect( runaway.gameFrame == 1000 && runaway.gameTime == 1000 * FRAME_MSEC,
		"a client past the maximum prediction resyncs to the snapshot" );

	// The clamps hold at both ends.
	ClientTiming far = Step( 16, 1000, 990, 0 );
	Expect( far.clientPredictTime == MAX_PREDICTION_MSEC, "the catch-up clamps at the maximum" );
	ClientTiming early = Step( 990, 16, -990, 900 );
	Expect( early.clientPredictTime == -MAX_PREDICTION_MSEC, "the catch-up clamps at the minimum" );
}

struct Packet {
	int arrival;
	int frame;
	int time;
	int ahead;
	int sequence = 0;
};

// A server running 16 ms frames, sending a snapshot every other frame, and a client presenting
// every 16 ms, both seeing the other through a fixed one-way delay.
static void CheckClosedLoop( int oneWayMsec, int seconds ) {
	ClientTiming client;
	client.clientPrediction = client.clientPredictTime = 2 * oneWayMsec;	// the connect round trip
	int serverFrame = 0;
	int serverResidual = 0;
	int lastCommandFrame = -1;
	std::vector<Packet> toServer;
	std::vector<Packet> toClient;
	int lateCommands = 0;
	int settledSnapshots = 0;
	int settledResyncs = 0;
	int settledLate = 0;
	const int settleMsec = 5000;

	for ( int now = 0; now < seconds * 1000; now++ ) {
		// server
		for ( size_t i = 0; i < toServer.size(); ) {
			if ( toServer[i].arrival <= now ) {
				if ( toServer[i].frame > lastCommandFrame ) {
					lastCommandFrame = toServer[i].frame;
				}
				toServer.erase( toServer.begin() + i );
			} else {
				i++;
			}
		}
		if ( ++serverResidual >= FRAME_MSEC ) {
			serverResidual -= FRAME_MSEC;
			if ( lastCommandFrame < serverFrame ) {
				lateCommands++;		// the server would duplicate this client's last command
				if ( now >= settleMsec ) {
					settledLate++;
				}
			}
			serverFrame++;
			if ( ( serverFrame & 1 ) == 0 ) {
				const int ahead = lastCommandFrame * FRAME_MSEC - serverFrame * FRAME_MSEC - serverResidual;
				toClient.push_back( { now + oneWayMsec, serverFrame, serverFrame * FRAME_MSEC, ahead } );
			}
		}

		// client, once per presented frame
		if ( now % FRAME_MSEC != 0 ) {
			continue;
		}
		client.gameTimeResidual += FRAME_MSEC;
		for ( size_t i = 0; i < toClient.size(); ) {
			if ( toClient[i].arrival <= now ) {
				client.snapshotGameFrame = toClient[i].frame;
				client.snapshotGameTime = toClient[i].time;
				const bool resynced = SnapshotTiming( client, toClient[i].ahead );
				if ( now >= settleMsec ) {
					settledSnapshots++;
					settledResyncs += resynced ? 1 : 0;
				}
				toClient.erase( toClient.begin() + i );
			} else {
				i++;
			}
		}
		while ( client.gameTimeResidual + client.clientPredictTime >= FRAME_MSEC ) {
			toServer.push_back( { now + oneWayMsec, client.gameFrame, client.gameTime, 0 } );
			client.gameFrame++;
			client.gameTime = client.gameFrame * FRAME_MSEC;
			client.gameTimeResidual -= FRAME_MSEC;
		}
	}

	char what[160];
	std::snprintf( what, sizeof( what ), "%d ms each way: settled snapshots arrive (%d)", oneWayMsec, settledSnapshots );
	Expect( settledSnapshots > 0, what );
	std::snprintf( what, sizeof( what ), "%d ms each way: no resync once settled (%d of %d)", oneWayMsec,
		settledResyncs, settledSnapshots );
	Expect( settledResyncs == 0, what );
	std::snprintf( what, sizeof( what ), "%d ms each way: commands reach the server in time once settled (%d late frames)",
		oneWayMsec, settledLate );
	Expect( settledLate == 0, what );
	std::snprintf( what, sizeof( what ), "%d ms each way: the target stays near the round trip (%d)", oneWayMsec,
		client.clientPrediction );
	Expect( client.clientPrediction <= 2 * oneWayMsec + 4 * FRAME_MSEC, what );
	std::printf( "loop %3d ms: target %d, catch-up %d, late frames %d (settled %d), resyncs %d of %d\n", oneWayMsec,
		client.clientPrediction, client.clientPredictTime, lateCommands, settledLate, settledResyncs, settledSnapshots );
}

// Hitches: the client's frame stalls stallMsec, every second, stalls times, once it has settled.
// idAsyncClient::UpdateTime( 100 ) gives a stalled frame 100 ms of it, and the snapshots that
// queued meanwhile are read in one go before the frame predicts. Packets take oneWayMsec plus
// up to jitterMsec, so snapshots reorder and the channel drops the ones older than the last it
// took. The server uses the command for each frame, if one has come, and reports how early the
// newest packet's last command arrived. During a stall commands are necessarily late; a few
// seconds after the last one the client must send them in time again. Before the fix each hitch
// over 100 ms left the catch-up that much higher, the clamp held it at the maximum, and every
// command the client sent from then on reached the server late.
static void CheckHitches( int oneWayMsec, int jitterMsec, int stallMsec, int stalls ) {
	ClientTiming client;
	client.clientPrediction = client.clientPredictTime = 2 * oneWayMsec;
	unsigned int random = 40708;
	auto jitter = [&]() {
		random = random * 1103515245u + 12345u;
		return jitterMsec ? static_cast<int>( ( random >> 16 ) % static_cast<unsigned int>( jitterMsec + 1 ) ) : 0;
	};
	int serverFrame = 0;
	int serverResidual = 0;
	int lastPacketFrame = -1;
	int snapshotSequence = 0;
	int lastSequence = -1;
	std::vector<int> received;	// the newest command frame the server has for each frame
	std::vector<Packet> toServer;
	std::vector<Packet> toClient;
	const int firstStall = 6000;
	const int recovered = firstStall + ( stalls - 1 ) * 1000 + stallMsec + 4000;
	const int end = recovered + 10000;
	int stalledUntil = -1;
	int lastPresent = 0;
	int lateAfterRecovery = 0;
	int highestCatchUp = 0;

	for ( int now = 0; now < end; now++ ) {
		for ( size_t i = 0; i < toServer.size(); ) {
			if ( toServer[i].arrival <= now ) {
				if ( static_cast<int>( received.size() ) <= toServer[i].frame ) {
					received.resize( toServer[i].frame + 1, 0 );
				}
				received[ toServer[i].frame ] = 1;
				lastPacketFrame = toServer[i].frame;
				toServer.erase( toServer.begin() + i );
			} else {
				i++;
			}
		}
		if ( ++serverResidual >= FRAME_MSEC ) {
			serverResidual -= FRAME_MSEC;
			const bool have = serverFrame < static_cast<int>( received.size() ) && received[ serverFrame ];
			if ( !have && now >= recovered ) {
				lateAfterRecovery++;
			}
			serverFrame++;
			if ( ( serverFrame & 1 ) == 0 ) {
				const int ahead = lastPacketFrame * FRAME_MSEC - serverFrame * FRAME_MSEC - serverResidual;
				toClient.push_back( { now + oneWayMsec + jitter(), serverFrame, serverFrame * FRAME_MSEC, ahead, snapshotSequence++ } );
			}
		}

		for ( int stall = 0; stall < stalls; stall++ ) {
			if ( now == firstStall + stall * 1000 ) {
				stalledUntil = now + stallMsec;
			}
		}
		if ( now < stalledUntil || now - lastPresent < FRAME_MSEC ) {
			continue;
		}
		const int elapsed = now - lastPresent;
		lastPresent = now;
		client.gameTimeResidual += elapsed < 100 ? elapsed : 100;	// idAsyncClient::UpdateTime( 100 )
		std::vector<Packet> ready;
		for ( size_t i = 0; i < toClient.size(); ) {
			if ( toClient[i].arrival <= now ) {
				ready.push_back( toClient[i] );
				toClient.erase( toClient.begin() + i );
			} else {
				i++;
			}
		}
		for ( size_t a = 0; a < ready.size(); a++ ) {
			for ( size_t b = a + 1; b < ready.size(); b++ ) {
				if ( ready[b].arrival < ready[a].arrival ) {
					const Packet swap = ready[a];
					ready[a] = ready[b];
					ready[b] = swap;
				}
			}
		}
		for ( const Packet &packet : ready ) {
			if ( packet.sequence <= lastSequence ) {
				continue;
			}
			lastSequence = packet.sequence;
			client.snapshotGameFrame = packet.frame;
			client.snapshotGameTime = packet.time;
			SnapshotTiming( client, packet.ahead );
			if ( now >= recovered && client.clientPredictTime > highestCatchUp ) {
				highestCatchUp = client.clientPredictTime;
			}
		}
		while ( client.gameTimeResidual + client.clientPredictTime >= FRAME_MSEC ) {
			toServer.push_back( { now + oneWayMsec + jitter(), client.gameFrame, client.gameTime, 0, 0 } );
			client.gameFrame++;
			client.gameTime = client.gameFrame * FRAME_MSEC;
			client.gameTimeResidual -= FRAME_MSEC;
		}
	}

	char what[200];
	std::snprintf( what, sizeof( what ), "%d ms +-%d, %d hitches of %d ms: commands reach the server in time again (%d late frames)",
		oneWayMsec, jitterMsec, stalls, stallMsec, lateAfterRecovery );
	Expect( lateAfterRecovery == 0, what );
	std::snprintf( what, sizeof( what ), "%d ms +-%d, %d hitches of %d ms: the catch-up stays off its clamp (%d)",
		oneWayMsec, jitterMsec, stalls, stallMsec, highestCatchUp );
	Expect( highestCatchUp < MAX_PREDICTION_MSEC, what );
	std::printf( "hitches %3d ms +-%2d, %d x %d ms: target %d, catch-up %d (highest %d), late frames after recovery %d\n",
		oneWayMsec, jitterMsec, stalls, stallMsec, client.clientPrediction, client.clientPredictTime, highestCatchUp,
		lateAfterRecovery );
}

int main() {
	CheckDirections();
	CheckClosedLoop( 0, 20 );
	CheckClosedLoop( 30, 20 );
	CheckClosedLoop( 75, 20 );
	CheckHitches( 0, 0, 350, 9 );
	CheckHitches( 80, 20, 350, 9 );
	CheckHitches( 150, 20, 500, 4 );
	if ( failures ) {
		std::printf( "%d failure(s)\n", failures );
		return 1;
	}
	std::printf( "client prediction controller: ok\n" );
	return 0;
}
