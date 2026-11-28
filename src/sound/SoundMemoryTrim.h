// Copyright (C) 2026 openQ4 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_SOUND_MEMORY_TRIM_H
#define OPENQ4_SOUND_MEMORY_TRIM_H

// What an operating-system low-memory notice may take from a loaded sample.
// Nothing a channel references goes, in any world, audible or not: shakes,
// flicker, subtitles and resumed playback still read it. A freed sample keeps
// its shader references and reloads when a sound starts it again.
enum class SoundTrimAction { Keep, ReleaseCopy, Free };

struct SoundTrimSample {
	bool active = false;			// a channel in some world plays it or will
	bool neverPurge = false;		// menus, HUD and samples first used outside a level load
	bool levelReferenced = false;	// the current level loaded it
	bool streamed = false;			// only metadata and an envelope are resident
	bool cpuCopy = false;			// CPU PCM retained beside OpenAL's own complete copy
	bool played = false;			// started at least once since it loaded
	int idleMsec = 0;				// since it last started, when played
};

// idleLimitMsec < 0 keeps the current level's samples. insideLevelLoad keeps
// every loaded sample: the load in progress may reference any of them next.
inline SoundTrimAction SoundTrimDecide( const SoundTrimSample& sample, int idleLimitMsec, bool insideLevelLoad )
{
	if( sample.active )
	{
		return SoundTrimAction::Keep;
	}
	if( !insideLevelLoad && !sample.neverPurge )
	{
		// An earlier level's cache: EndLevelLoad would free it at the next change.
		if( !sample.levelReferenced )
		{
			return SoundTrimAction::Free;
		}
		// Streamed tracks hold almost nothing, and rebuilding their envelope
		// decodes the whole file again.
		if( !sample.streamed && idleLimitMsec >= 0 && ( !sample.played || sample.idleMsec >= idleLimitMsec ) )
		{
			return SoundTrimAction::Free;
		}
	}
	return sample.cpuCopy ? SoundTrimAction::ReleaseCopy : SoundTrimAction::Keep;
}

// Android reports how hard it is pressed (ComponentCallbacks2.onTrimMemory);
// SDL folds every level into one SDL_EVENT_LOW_MEMORY, so the activity records
// the level beside it. iOS, desktop tests and a notice without a recorded level
// use SOUND_TRIM_LEVEL_UNKNOWN.
enum {
	SOUND_TRIM_LEVEL_UNKNOWN = -1,
	SOUND_TRIM_LEVEL_RUNNING_MODERATE = 5,
	SOUND_TRIM_LEVEL_RUNNING_LOW = 10,
	SOUND_TRIM_LEVEL_RUNNING_CRITICAL = 15,
	SOUND_TRIM_LEVEL_UI_HIDDEN = 20,
	SOUND_TRIM_LEVEL_BACKGROUND = 40,
	SOUND_TRIM_LEVEL_MODERATE = 60,
	SOUND_TRIM_LEVEL_COMPLETE = 80
};

// How much a notice takes. Light: earlier levels' samples and CPU copies only;
// Android sends UI_HIDDEN at every trip to the background and RUNNING_MODERATE
// before any real shortage, and a freed sample reloads on the main thread when
// next played. Normal: also this level's samples idle for s_trimIdleSeconds.
// Severe: every sample no channel uses, because the process is next in line to
// be killed (RUNNING_CRITICAL while playing, MODERATE and COMPLETE while in the
// background).
enum class SoundTrimSeverity { Light, Normal, Severe };

constexpr SoundTrimSeverity SoundTrimSeverityForLevel( int level )
{
	return level < 0 ? SoundTrimSeverity::Normal
		: level >= SOUND_TRIM_LEVEL_MODERATE ? SoundTrimSeverity::Severe
		: level >= SOUND_TRIM_LEVEL_BACKGROUND ? SoundTrimSeverity::Normal
		: level >= SOUND_TRIM_LEVEL_UI_HIDDEN ? SoundTrimSeverity::Light
		: level >= SOUND_TRIM_LEVEL_RUNNING_CRITICAL ? SoundTrimSeverity::Severe
		: level >= SOUND_TRIM_LEVEL_RUNNING_LOW ? SoundTrimSeverity::Normal
		: SoundTrimSeverity::Light;
}

// The idleSeconds a notice passes to the trim: configured is s_trimIdleSeconds,
// whose -1 (keep this level's samples) also holds under severe pressure.
constexpr int SoundTrimIdleSecondsForLevel( int level, int configured )
{
	return SoundTrimSeverityForLevel( level ) == SoundTrimSeverity::Light ? -1
		: SoundTrimSeverityForLevel( level ) == SoundTrimSeverity::Severe ? ( configured < 0 ? -1 : 0 )
		: configured;
}

// SDL keeps at most one low-memory event queued, so levels reported before the
// engine answers fold into the most severe; equal severities keep the newer.
constexpr int SoundTrimFoldLevel( int pending, int reported )
{
	return pending < 0 ? reported
		: reported < 0 ? pending
		: static_cast<int>( SoundTrimSeverityForLevel( reported ) ) >= static_cast<int>( SoundTrimSeverityForLevel( pending ) ) ? reported : pending;
}

#endif
