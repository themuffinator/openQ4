// openQ4 live match event stream writer. SDK EULA: see src/mpgame/LICENSE.
#ifndef OPENQ4_MP_LIVE_EVENT_STREAM_H
#define OPENQ4_MP_LIVE_EVENT_STREAM_H

#include "LiveEvents.h"

class idFile;
class idPlayer;

// Writes the events of the match this process is in to
// <fs_savepath>/<game>/match-events/live.ndjson while g_matchEventStream is
// set, one flushed line per event, so an overlay tool can follow the file as
// it grows. A server writes the match it runs; a client writes the match it is
// shown, which is how a caster who does not host feeds an overlay. The file
// starts again with every map, and with the cvar turned on.
class mpLiveEventStream {
public:
							mpLiveEventStream();
							~mpLiveEventStream();

	// Once a frame, on servers and clients: publishes what changed.
	void					Update( void );
	// A death as the server scored it, or as a client was told of it.
	void					Kill( const idPlayer *killer, const idPlayer *victim, int methodOfDeath, bool quad );
	// The match is going away (map change, disconnect, shutdown).
	void					Close( void );

private:
	idFile *				file;
	bool					failed;			// the file could not be opened; do not retry every frame
	int						sequence;
	int						lastTime;		// the last "t" written; a stream's time never steps back
	mpLiveEventState		state;
	mpLiveEventLine			line;

	bool					Wanted( void ) const;
	int						Stamp( void );
	void					Capture( mpLiveEventState &current ) const;
	bool					Open( const mpLiveEventState &current );
	void					Write( const mpLiveEventLine &complete );
};

#endif
