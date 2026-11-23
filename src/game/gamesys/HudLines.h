// HUD line feeds for the Remastered multiplayer HUD. Quake 4 SDK EULA.
//
// The stock kill feed and chat keep four lines: the top line holds for
// 2000 ms, fades over 1000 ms and leaves at 3100 ms, when the next line
// becomes the top and starts its own clock; a line arriving at a full feed
// drops the top line at once. The stock GUIs time this in their own scripts;
// this queue keeps the same timing for the Remastered documents and
// publishes each slot as fields (<prefix><slot>_<field>, _live, _alpha).
// openQ4: given an event prefix, a publish also names what changed since the
// last one, so a document can move its lines: <prefix>Shift when lines moved
// up a slot, then <prefix>In<slot> for each line that arrived.
#pragma once

class rvHudLines {
public:
	static const int MaxLines = 8;

	rvHudLines() { Clear(); }

	void Clear() {
		count = 0;
		topSince = 0;
		added = 0;
		shifted = false;
		for ( int i = 0; i < MaxLines; i++ ) {
			lines[ i ].Clear();
		}
	}

	void Add( const idDict &fields, int now, int capacity ) {
		capacity = idMath::ClampInt( 1, MaxLines, capacity );
		while ( count >= capacity ) {
			RemoveTop( now );
		}
		lines[ count++ ] = fields;
		added = Min( added + 1, MaxLines );
		if ( count == 1 ) {
			topSince = now;
		}
	}

	void Update( int now, int leaveMs ) {
		// A long hitch can expire several lines; each top line keeps its own clock.
		while ( count > 0 && now - topSince >= leaveMs ) {
			RemoveTop( topSince + leaveMs );
		}
	}

	void Publish( idUserInterface *gui, const char *prefix, int now, int holdMs, int fadeMs, int leaveMs, int capacity,
				  const char *eventPrefix = NULL ) {
		if ( gui == NULL ) {
			return;
		}
		Update( now, leaveMs );
		capacity = idMath::ClampInt( 1, MaxLines, capacity );
		for ( int slot = 0; slot < capacity; slot++ ) {
			const bool live = slot < count;
			gui->SetStateBool( va( "%s%d_live", prefix, slot ), live );
			float alpha = 1.0f;
			if ( live && slot == 0 && fadeMs > 0 ) {
				alpha = idMath::ClampFloat( 0.0f, 1.0f, 1.0f - (float)( now - topSince - holdMs ) / (float)fadeMs );
				// twentieths: the HUD's state changes 20 times over a fade, not every frame
				alpha = ceilf( alpha * 20.0f ) / 20.0f;
			}
			gui->SetStateFloat( va( "%s%d_alpha", prefix, slot ), live ? alpha : 0.0f );
			if ( !live ) {
				continue;
			}
			for ( int i = 0; i < lines[ slot ].GetNumKeyVals(); i++ ) {
				const idKeyValue *kv = lines[ slot ].GetKeyVal( i );
				gui->SetStateString( va( "%s%d_%s", prefix, slot, kv->GetKey().c_str() ), kv->GetValue().c_str() );
			}
		}
		if ( eventPrefix != NULL ) {
			// the lines that stayed moved up first; the newest lines are the last slots
			const int arrived = Min( added, Min( count, capacity ) );
			if ( shifted && count > arrived ) {
				gui->HandleNamedEvent( va( "%sShift", eventPrefix ) );
			}
			for ( int slot = Min( count, capacity ) - arrived; slot < Min( count, capacity ); slot++ ) {
				gui->HandleNamedEvent( va( "%sIn%d", eventPrefix, slot ) );
			}
		}
		added = 0;
		shifted = false;
	}

	// `count` lines at full strength, oldest first, and the slots after them up
	// to `slots` empty: a window of a history that never times out (the open
	// chat input's).
	static void PublishWindow( idUserInterface *gui, const char *prefix, const idDict *window, int count, int slots ) {
		if ( gui == NULL ) {
			return;
		}
		slots = idMath::ClampInt( 1, MaxLines, slots );
		for ( int slot = 0; slot < slots; slot++ ) {
			const bool live = slot < count;
			gui->SetStateBool( va( "%s%d_live", prefix, slot ), live );
			gui->SetStateFloat( va( "%s%d_alpha", prefix, slot ), live ? 1.0f : 0.0f );
			if ( !live ) {
				continue;
			}
			for ( int i = 0; i < window[ slot ].GetNumKeyVals(); i++ ) {
				const idKeyValue *kv = window[ slot ].GetKeyVal( i );
				gui->SetStateString( va( "%s%d_%s", prefix, slot, kv->GetKey().c_str() ), kv->GetValue().c_str() );
			}
		}
	}

	int Count() const { return count; }

private:
	void RemoveTop( int now ) {
		for ( int i = 1; i < count; i++ ) {
			lines[ i - 1 ] = lines[ i ];
		}
		if ( count > 0 ) {
			lines[ --count ].Clear();
			shifted = true;
			added = Min( added, count );
		}
		topSince = now;
	}

	idDict	lines[ MaxLines ];
	int		count;
	int		topSince;
	int		added;		// lines added since the last publish, still queued
	bool	shifted;	// lines moved up a slot since the last publish
};
