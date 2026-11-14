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
//
// The long-frame loops run the production PredictionMargin.h, the user command count per
// packet (AsyncClient_UsercmdsPerPacket) and the prediction cover as well. A client frame
// longer than about two tics holds back the commands that fell due during it; with the
// median target alone they reached the server after the frame that needed them, and a
// client below 30 fps did that on every frame. Long frames of a fixed length must leave no
// late command once the first has set the margin, a client at 30 or 20 fps none at all, and
// a realistic tail (random long frames of +20 to +60 ms, twice a second) a tenth of the late
// commands the target alone leaves. After one long frame, milder ones must not hold its
// margin. Smooth clients must get no margin at all, and neither must hitches the 100 ms
// clamp cut. A table of late commands per long frame against its
// length, with the margin and without it (net_clientPredictionMargin 0), measures the tail.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define ID_INLINE inline

// PRODUCTION_SIGN_MACRO

namespace idMath {
inline int ClampInt( int min, int max, int value ) {
	return value < min ? min : ( value > max ? max : value );
}
}

// PRODUCTION_FAST_ADJUST
// PRODUCTION_PACKET_COMMANDS

// PRODUCTION_PREDICTION_MARGIN

static const int FRAME_MSEC = 16;
static const int MAX_PREDICTION_MSEC = 1000;
static const int FRAME_CLAMP_MSEC = 100;		// idAsyncClient::UpdateTime( CLIENT_FRAME_CLAMP_MSEC )
static const int USERCMD_BACKUP = 5;			// net_clientUsercmdBackup

static int AsyncClient_MaxPredictionMsec() {
	return MAX_PREDICTION_MSEC;
}

// The exact 60 Hz tic ladder (common->GetUserCmdDeltaMsec): 16, 17, 17 ms. Only the
// prediction cover reads it here; the loops below keep 16 ms frames on both ends.
static int AsyncClient_NextGameFrameMsec( int gameFrame ) {
	const int tic = gameFrame + 1;
	return tic * 1000 / 60 - ( tic - 1 ) * 1000 / 60;
}

static int AsyncClient_ConfiguredPredictionMsec( int ) {
	return FRAME_MSEC;
}

// PRODUCTION_PREDICTION_COVER

// PRODUCTION_USERCMDS_PER_PACKET

struct ClientTiming {
	int gameFrame = 0;
	int gameTime = 0;
	int gameTimeResidual = 0;
	int clientPredictTime = 0;
	int clientPrediction = 0;
	int snapshotGameFrame = 0;
	int snapshotGameTime = 0;
	int clientTime = 0;
	idPredictionMargin predictionMargin;
};

// What idAsyncClient::RunFrame does with a frame of elapsed milliseconds before it reads the
// snapshots that came meanwhile: UpdateTime clamps it, and the margin sees it unless
// net_clientPredictionMargin is 0.
static int ClientFrame( ClientTiming &client, int elapsed, bool margin ) {
	const int msec = elapsed < FRAME_CLAMP_MSEC ? elapsed : FRAME_CLAMP_MSEC;
	client.clientTime += msec;
	if ( !margin ) {
		client.predictionMargin.Clear();
	} else {
		client.predictionMargin.Frame( client.clientTime, msec, FRAME_CLAMP_MSEC,
			AsyncClient_PredictionCoverMsec( client.gameFrame ) );
	}
	client.gameTimeResidual += msec;
	return msec;
}

static bool SnapshotTiming( ClientTiming &client, int aheadOfServer ) {
	int &gameFrame = client.gameFrame;
	int &gameTime = client.gameTime;
	int &gameTimeResidual = client.gameTimeResidual;
	int &clientPredictTime = client.clientPredictTime;
	int &clientPrediction = client.clientPrediction;
	int &snapshotGameTime = client.snapshotGameTime;
	idPredictionMargin &predictionMargin = client.predictionMargin;
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
		ClientFrame( client, FRAME_MSEC, true );
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
	std::snprintf( what, sizeof( what ), "%d ms each way: frames of one tic raise no margin (%d)", oneWayMsec,
		client.predictionMargin.PeakMsec() );
	Expect( client.predictionMargin.PeakMsec() == 0, what );
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
	int highestMargin = 0;

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
		ClientFrame( client, elapsed, true );
		highestMargin = std::max( highestMargin, client.predictionMargin.PeakMsec() );
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
	std::snprintf( what, sizeof( what ), "%d ms +-%d, %d hitches of %d ms: hitches the clamp cut raise no margin (%d)",
		oneWayMsec, jitterMsec, stalls, stallMsec, highestMargin );
	Expect( highestMargin == 0, what );
	std::printf( "hitches %3d ms +-%2d, %d x %d ms: target %d, catch-up %d (highest %d), late frames after recovery %d\n",
		oneWayMsec, jitterMsec, stalls, stallMsec, client.clientPrediction, client.clientPredictTime, highestCatchUp,
		lateAfterRecovery );
}

// A link with long client frames. The server runs 16 ms frames and sends a snapshot every
// other one; packets take oneWayMsec plus up to jitterMsec each way, so a burst reorders and
// the server's channel discards a packet older than the newest it has taken, as
// idMsgChannel::Process does. A packet carries AsyncClient_UsercmdsPerPacket commands up to
// its own frame. The server uses the command for each frame if one has come and otherwise
// repeats the last: that frame is late. The client presents frameMsec frames (one more or
// less at random); longFrame() gives the length of the next long frame, or 0 for an ordinary
// one. The commands that fell due during a frame are made after it, as RunFrame does.
struct LinkResult {
	int late = 0;				// frames the server ran on a repeated command after settling
	int lateAfterLearn = 0;		// of those, the ones after learnMsec past the first long frame
	int longFrames = 0;			// long frames after settling
	int firstLong = -1;
	double waitMsec = 0;		// how long a command waited at the server, on average
	int highestMargin = 0;
	double meanMargin = 0;
	int rises = 0;				// snapshots that moved the target forward by more than its step
	int finalMargin = 0;
};

struct Link {
	int oneWayMsec = 0;
	int jitterMsec = 0;
	int frameMsec = FRAME_MSEC;
	bool margin = true;
	int settleMsec = 8000;
	int seconds = 60;
	int learnMsec = 1000;
	unsigned int seed = 40708;
};

template<typename LongFrame>
static LinkResult RunLink( const Link &link, LongFrame longFrame ) {
	ClientTiming client;
	client.clientPrediction = client.clientPredictTime = 2 * link.oneWayMsec;
	unsigned int random = link.seed;
	auto next = [&]() {
		random = random * 1103515245u + 12345u;
		return random >> 8;
	};
	auto jitter = [&]() {
		return link.jitterMsec ? static_cast<int>( next() % static_cast<unsigned int>( link.jitterMsec + 1 ) ) : 0;
	};
	struct Datagram {
		int arrival;
		int sequence;
		int frame;
		int count;
		int ahead;
	};
	std::vector<Datagram> toServer;
	std::vector<Datagram> toClient;
	std::vector<int> received;		// arrival time of each frame's command, -1 for none yet
	int serverFrame = 0;
	int serverResidual = 0;
	int lastPacketFrame = -1;
	int lastPacketSequence = -1;
	int sendSequence = 0;
	int snapshotSequence = 0;
	int lastSnapshotSequence = -1;
	int nextPresent = 0;
	int lastPresent = 0;
	const int end = link.settleMsec + link.seconds * 1000;
	LinkResult result;
	double waitSum = 0;
	int waitCount = 0;
	double marginSum = 0;
	int marginCount = 0;
	auto byArrival = []( const Datagram &a, const Datagram &b ) { return a.arrival < b.arrival; };

	for ( int now = 0; now < end; now++ ) {
		std::stable_sort( toServer.begin(), toServer.end(), byArrival );
		while ( !toServer.empty() && toServer.front().arrival <= now ) {
			const Datagram datagram = toServer.front();
			toServer.erase( toServer.begin() );
			if ( datagram.sequence <= lastPacketSequence ) {
				continue;	// out of order: the channel discards it
			}
			lastPacketSequence = datagram.sequence;
			lastPacketFrame = datagram.frame;
			if ( static_cast<int>( received.size() ) <= datagram.frame ) {
				received.resize( datagram.frame + 1, -1 );
			}
			for ( int frame = std::max( 0, datagram.frame - datagram.count + 1 ); frame <= datagram.frame; frame++ ) {
				if ( received[frame] < 0 ) {
					received[frame] = now;
				}
			}
		}
		if ( ++serverResidual >= FRAME_MSEC ) {
			serverResidual -= FRAME_MSEC;
			const int arrived = serverFrame < static_cast<int>( received.size() ) ? received[serverFrame] : -1;
			if ( now >= link.settleMsec ) {
				if ( arrived < 0 ) {
					result.late++;
					if ( result.firstLong >= 0 && now >= result.firstLong + link.learnMsec ) {
						result.lateAfterLearn++;
					}
				} else {
					waitSum += now - arrived;
					waitCount++;
				}
			}
			serverFrame++;
			if ( ( serverFrame & 1 ) == 0 ) {
				const int ahead = lastPacketFrame * FRAME_MSEC - serverFrame * FRAME_MSEC - serverResidual;
				toClient.push_back( { now + link.oneWayMsec + jitter(), snapshotSequence++, serverFrame, 0, ahead } );
			}
		}

		if ( now < nextPresent ) {
			continue;
		}
		const int elapsed = now - lastPresent;
		lastPresent = now;
		int frame = link.frameMsec + static_cast<int>( next() % 3u ) - 1;
		if ( now >= link.settleMsec ) {
			const int longMsec = longFrame( now, next );
			if ( longMsec > 0 ) {
				frame = longMsec;
				result.longFrames++;
				if ( result.firstLong < 0 ) {
					result.firstLong = now;
				}
			}
		}
		nextPresent = now + frame;

		ClientFrame( client, elapsed, link.margin );
		std::stable_sort( toClient.begin(), toClient.end(), byArrival );
		while ( !toClient.empty() && toClient.front().arrival <= now ) {
			const Datagram snapshot = toClient.front();
			toClient.erase( toClient.begin() );
			if ( snapshot.sequence <= lastSnapshotSequence ) {
				continue;
			}
			lastSnapshotSequence = snapshot.sequence;
			client.snapshotGameFrame = snapshot.frame;
			client.snapshotGameTime = snapshot.frame * FRAME_MSEC;
			const int before = client.clientPrediction;
			SnapshotTiming( client, snapshot.ahead );
			if ( client.clientPrediction > before + 1 ) {
				result.rises++;
			}
		}
		int usercmdsSentThisFrame = 0;
		while ( client.gameTimeResidual + client.clientPredictTime >= FRAME_MSEC ) {
			const int count = AsyncClient_UsercmdsPerPacket( USERCMD_BACKUP, usercmdsSentThisFrame );
			usercmdsSentThisFrame++;
			toServer.push_back( { now + link.oneWayMsec + jitter(), sendSequence++, client.gameFrame, count, 0 } );
			client.gameFrame++;
			client.gameTime = client.gameFrame * FRAME_MSEC;
			client.gameTimeResidual -= FRAME_MSEC;
		}
		if ( now >= link.settleMsec ) {
			const int margin = client.predictionMargin.Margin( AsyncClient_PredictionCoverMsec( client.gameFrame ) );
			result.highestMargin = std::max( result.highestMargin, margin );
			result.finalMargin = margin;
			marginSum += margin;
			marginCount++;
		}
	}
	result.waitMsec = waitCount ? waitSum / waitCount : 0.0;
	result.meanMargin = marginCount ? marginSum / marginCount : 0.0;
	return result;
}

// A long frame of longMsec every one to three seconds.
static auto EveryFewSeconds( int longMsec ) {
	int due = -1;
	return [longMsec, due]( int now, auto &next ) mutable {
		if ( due < 0 ) {
			due = now + 1000 + static_cast<int>( next() % 2001u );
		}
		if ( now < due ) {
			return 0;
		}
		due = now + 1000 + static_cast<int>( next() % 2001u );
		return longMsec;
	};
}

// Long frames of +minExtra to +maxExtra ms over a 16 ms frame, perSecond of them a second on
// average (exponential spacing).
static auto RandomTail( double perSecond, int minExtra, int maxExtra ) {
	int due = -1;
	return [perSecond, minExtra, maxExtra, due]( int now, auto &next ) mutable {
		auto spacing = [&]() {
			const double uniform = ( static_cast<double>( next() % 1000000u ) + 0.5 ) / 1000000.0;
			return static_cast<int>( -std::log( uniform ) / perSecond * 1000.0 );
		};
		if ( due < 0 ) {
			due = now + spacing();
		}
		if ( now < due ) {
			return 0;
		}
		due = now + spacing();
		return FRAME_MSEC + minExtra + static_cast<int>( next() % static_cast<unsigned int>( maxExtra - minExtra + 1 ) );
	};
}

static auto NoLongFrames() {
	return []( int, auto & ) { return 0; };
}

// One 96 ms frame, as a join or a level's first fight can make, then 40 ms frames every
// one to three seconds.
static auto SpikeThenMild() {
	bool spiked = false;
	auto mild = EveryFewSeconds( 40 );
	return [spiked, mild]( int now, auto &next ) mutable {
		if ( !spiked ) {
			spiked = true;
			return 96;
		}
		return mild( now, next );
	};
}

static void CheckLongFrames() {
	char what[220];
	// Long frames of one length: once the first has set the margin, none may leave a late
	// command, over a clean link or 80 ms +-20 each way. The 96 ms frames make bursts of six
	// commands and more, which reorder on the jittery link.
	const int lengths[] = { 36, 56, 76, 96 };
	const int links[][2] = { { 0, 0 }, { 80, 20 } };
	for ( const auto &delay : links ) {
		for ( const int length : lengths ) {
			Link link;
			link.oneWayMsec = delay[0];
			link.jitterMsec = delay[1];
			const LinkResult result = RunLink( link, EveryFewSeconds( length ) );
			std::snprintf( what, sizeof( what ), "%d ms +-%d, %d ms frames every few seconds: no late command once the "
				"margin is set (%d late frames after the first, %d in all, margin %d)", delay[0], delay[1], length,
				result.lateAfterLearn, result.late, result.highestMargin );
			Expect( result.longFrames > 10 && result.lateAfterLearn == 0, what );
			// Frames of one length set the margin once; the cover's 16/17 ms swings must not
			// push the target forward again and again.
			std::snprintf( what, sizeof( what ), "%d ms +-%d, %d ms frames every few seconds: the target jumps only "
				"when the margin first grows (%d jumps)", delay[0], delay[1], length, result.rises );
			Expect( result.rises <= 2, what );
			std::printf( "long frames %3d ms +-%2d, %2d ms: %d long, late %d (after the first %d), margin %d, wait %.1f ms, "
				"%d jumps\n", delay[0], delay[1], length, result.longFrames, result.late, result.lateAfterLearn,
				result.highestMargin, result.waitMsec, result.rises );
		}
	}

	// A client below 30 fps makes every command late with the target alone.
	const int slowFrames[] = { 33, 50 };
	for ( const auto &delay : links ) {
		for ( const int frameMsec : slowFrames ) {
			Link link;
			link.oneWayMsec = delay[0];
			link.jitterMsec = delay[1];
			link.frameMsec = frameMsec;
			const LinkResult result = RunLink( link, NoLongFrames() );
			std::snprintf( what, sizeof( what ), "%d ms +-%d, steady %d ms frames: commands reach the server in time "
				"(%d late frames, margin %d)", delay[0], delay[1], frameMsec, result.late, result.highestMargin );
			Expect( result.late == 0, what );
			std::printf( "steady %3d ms +-%2d, %d ms frames: late %d, margin %d, wait %.1f ms\n", delay[0], delay[1],
				frameMsec, result.late, result.highestMargin, result.waitMsec );
		}
	}

	// Smooth clients pay nothing: no margin, and commands wait no longer than with the target alone.
	const int smoothFrames[] = { 16, 7 };
	for ( const auto &delay : links ) {
		for ( const int frameMsec : smoothFrames ) {
			Link link;
			link.oneWayMsec = delay[0];
			link.jitterMsec = delay[1];
			link.frameMsec = frameMsec;
			const LinkResult with = RunLink( link, NoLongFrames() );
			link.margin = false;
			const LinkResult without = RunLink( link, NoLongFrames() );
			std::snprintf( what, sizeof( what ), "%d ms +-%d, smooth %d ms frames: no margin and no extra wait (margin %d, "
				"wait %.1f against %.1f ms)", delay[0], delay[1], frameMsec, with.highestMargin, with.waitMsec, without.waitMsec );
			Expect( with.highestMargin == 0 && with.waitMsec <= without.waitMsec + 0.5, what );
		}
	}

	// One long frame must not hold its margin while milder long frames keep coming: they
	// keep only their own. Once the 96 ms frame has decayed (10 s held, then 1 ms per
	// 250 ms), the margin covers the 40 ms frames and no more.
	for ( const auto &delay : links ) {
		Link link;
		link.oneWayMsec = delay[0];
		link.jitterMsec = delay[1];
		const LinkResult result = RunLink( link, SpikeThenMild() );
		std::snprintf( what, sizeof( what ), "%d ms +-%d, one 96 ms frame then 40 ms frames: the margin falls back to what "
			"the 40 ms frames need (%d, highest %d) and they stay on time (%d late after the first)", delay[0], delay[1],
			result.finalMargin, result.highestMargin, result.lateAfterLearn );
		Expect( result.highestMargin >= 60 && result.finalMargin > 0 && result.finalMargin <= 8 &&
			result.lateAfterLearn == 0, what );
		std::printf( "spike then mild %3d ms +-%2d: margin %d at first, %d at the end, late %d (after the first %d), "
			"wait %.1f ms\n", delay[0], delay[1], result.highestMargin, result.finalMargin, result.late,
			result.lateAfterLearn, result.waitMsec );
	}

	// A realistic tail: long frames of +20 to +60 ms, twice a second on average, over two
	// minutes. With the margin, at most a tenth of the late commands the target alone leaves.
	for ( const auto &delay : links ) {
		Link link;
		link.oneWayMsec = delay[0];
		link.jitterMsec = delay[1];
		link.seconds = 120;
		const LinkResult with = RunLink( link, RandomTail( 0.5, 20, 60 ) );
		link.margin = false;
		const LinkResult without = RunLink( link, RandomTail( 0.5, 20, 60 ) );
		std::snprintf( what, sizeof( what ), "%d ms +-%d, random long frames of +20 to +60 ms: the margin keeps nine in ten "
			"late commands on time (%d late against %d)", delay[0], delay[1], with.late, without.late );
		Expect( without.late > 0 && with.late * 10 <= without.late, what );
		std::printf( "random tail %3d ms +-%2d: late %d with the margin (mean %.1f ms, wait %.1f ms), %d without "
			"(wait %.1f ms), %d long frames\n", delay[0], delay[1], with.late, with.meanMargin, with.waitMsec,
			without.late, without.waitMsec, with.longFrames );
	}
}

// The measurement: late commands per long frame against its length, one long frame every one
// to three seconds over 80 ms +-20, with the margin and without it. Printed only.
static void MeasureTail() {
	std::printf( "late commands per long frame, 80 ms +-20, against frame length (target alone / with margin, "
		"mean wait at the server):\n" );
	const int lengths[] = { 21, 26, 31, 36, 41, 46, 56, 66, 76, 86, 96 };
	for ( const int length : lengths ) {
		Link link;
		link.oneWayMsec = 80;
		link.jitterMsec = 20;
		link.margin = false;
		const LinkResult without = RunLink( link, EveryFewSeconds( length ) );
		link.margin = true;
		const LinkResult with = RunLink( link, EveryFewSeconds( length ) );
		std::printf( "  %2d ms (+%2d): %.2f / %.2f   wait %5.1f / %5.1f ms\n", length, length - FRAME_MSEC,
			without.longFrames ? static_cast<double>( without.late ) / without.longFrames : 0.0,
			with.longFrames ? static_cast<double>( with.late ) / with.longFrames : 0.0, without.waitMsec, with.waitMsec );
	}
}

int main() {
	CheckDirections();
	CheckClosedLoop( 0, 20 );
	CheckClosedLoop( 30, 20 );
	CheckClosedLoop( 75, 20 );
	CheckHitches( 0, 0, 350, 9 );
	CheckHitches( 80, 20, 350, 9 );
	CheckHitches( 150, 20, 500, 4 );
	CheckLongFrames();
	MeasureTail();
	if ( failures ) {
		std::printf( "%d failure(s)\n", failures );
		return 1;
	}
	std::printf( "client prediction controller: ok\n" );
	return 0;
}
