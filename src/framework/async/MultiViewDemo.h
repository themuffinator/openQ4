/*
===========================================================================

openQ4 multi-view demo recording and playback.

===========================================================================
*/

#ifndef __MULTIVIEWDEMO_H__
#define __MULTIVIEWDEMO_H__

#include "NetworkSystem.h"
#include "MvdNavigation.h"

#include <vector>

class idFile;
class idFile_Memory;
class idBitMsg;
class idCmdArgs;
namespace openq4_retail { struct retailNetDemo_t; }

struct mvdFileInfo_t {
							mvdFileInfo_t();

	bool					valid;
	bool					compatible;
	bool					cleanEnd;
	bool					hasTimelineIndex;
	unsigned short			formatMajor;
	unsigned short			formatMinor;
	unsigned short			protocolMajor;
	unsigned short			protocolMinor;
	int						durationMS;
	int						snapshotCount;
	int						reliableCount;
	int						recordCount;
	int						markerCount;
	int						keyframeCount;
	int						fileSize;
	idStr					mapName;
	idStr					gameType;
	idStr					serverName;
	idStr					recordedAt;
	idStr					error;
	// openQ4 MP-22: recorded by a player on a server they joined; it shows that
	// player's view only (no free camera, follow or auto-tracking)
	bool					clientView;
	idStr					viewName;
	idStr					packs;			// "<dir>/<pack>:<checksum>;..." the game data came from
	idStr					players;		// openQ4 MP-22: every name seen while recording, ", " between
	// openQ4 MP-22: a retail Quake 4 network demo (.ndmo85, .netdemo), played through this player
	bool					retail;
};

class idMultiViewDemo {
public:
							idMultiViewDemo();

	void					Init();
	void					Shutdown();
	void					SessionStop();

	bool					IsRecording() const;
	// openQ4 MP-22: a recording of this client's own view on a server it joined
	bool					IsClientRecording() const;
	// playback of such a recording: the game runs as its recorder's client slot
	bool					IsClientViewPlayback() const;
	// openQ4 MP-22: timeDemo for a .mvd: fixed match time a frame, uncapped, timed
	bool					StartTimeDemo( const char *name, bool quitAfter );
	bool					IsTimeDemo() const;
	// openQ4 MP-22: a PNG frame a step, at a fixed frame rate (demoExport)
	bool					StartExport( const char *name, int fps, bool quitAfter );
	static void				Export_f( const idCmdArgs &args );
	int						GetPlaybackLocalClient() const;
	bool					StartNamedRecording( const char *name );
	bool					StopRecordingCleanly( const char *reason );
	bool					CopyRecordingQPath( char *buffer, int bufferSize ) const;
	bool					CopyRecordingResult( serverMVDRecordingResult_t &result ) const;
	bool					IsPlaying() const;
	bool					IsPaused() const;
	bool					IsSeeking() const;
	float					GetPlaybackScale() const;
	int						GetPlaybackTimeMS() const;
	int						GetPlaybackDurationMS() const;
	float					GetPlaybackFraction() const;
	const char *			GetPlaybackName() const;
	int						GetFollowClient() const;

	void					SetPaused( bool paused );
	void					TogglePaused();
	void					SetPlaybackScale( float scale );
	bool					SeekToMS( int relativeTimeMS );
	bool					SeekByMS( int deltaMS );
	void					StepFrames( int frames );
	void					FollowNext();
	void					FollowPrev();
	bool					FollowClient( int clientNum );
	int						FindFollowClient( const char *text ) const;
	bool					CopyFollowName( int clientNum, idStr &name ) const;
	void					FreeRoam();
	bool					QueryFileInfo( const char *name, mvdFileInfo_t &info );
	int						GetMarkerCount() const;
	bool					SeekToMarker( int direction, int kindFilter );

	void					CaptureServerFrame( int gameFrame, int gameTime );
	void					CaptureReliableMessage( const idBitMsg &msg, int routeType, int routeClient, int routeInstance = -1 );
	void					CaptureUserInfo( int clientNum, const idDict &info );
	bool					RecordMarker( const serverMVDMarker_t &marker );
	void					RunPlaybackFrame();
	void					OnServerMapChange();

	// openQ4 MP-22: what the connected client gave its game, in arrival order
	void					CaptureClientSnapshot( const idBitMsg &msg, int messageStart, int clientGameFrame );
	void					CaptureClientApplySnapshot( int sequence );
	void					CaptureClientReliable( const idBitMsg &msg );
	void					CaptureClientUserInfo( int clientNum, const idDict &info );
	void					CaptureClientUsercmd( const usercmd_t &cmd );
	void					OnClientGameEnded( const char *reason );
	// openQ4 MP-22: a recording's thumbnail, taken where console commands run
	void					ServiceThumbnail();

	static void				Record_f( const idCmdArgs &args );
	static void				Stop_f( const idCmdArgs &args );
	static void				Play_f( const idCmdArgs &args );
	static void				TimeDemo_f( const idCmdArgs &args );
	static void				Info_f( const idCmdArgs &args );
	static void				Status_f( const idCmdArgs &args );
	static void				Library_f( const idCmdArgs &args );
	static void				Pause_f( const idCmdArgs &args );
	static void				Seek_f( const idCmdArgs &args );
	static void				Skip_f( const idCmdArgs &args );
	static void				Speed_f( const idCmdArgs &args );
	static void				Step_f( const idCmdArgs &args );
	static void				Recover_f( const idCmdArgs &args );
	static void				FollowNext_f( const idCmdArgs &args );
	static void				FollowPrev_f( const idCmdArgs &args );
	static void				Follow_f( const idCmdArgs &args );
	static void				FreeRoam_f( const idCmdArgs &args );
	static void				Mark_f( const idCmdArgs &args );
	static void				Markers_f( const idCmdArgs &args );
	static void				Marker_f( const idCmdArgs &args );
	static void				AutoTrack_f( const idCmdArgs &args );

private:
	enum state_t {
		MVD_IDLE,
		MVD_RECORDING,
		MVD_PLAYING
	};

	struct header_t {
		unsigned short		formatMajor;
		unsigned short		formatMinor;
		unsigned int		requiredFeatures;
		unsigned int		optionalFeatures;
		unsigned short		protocolMajor;
		unsigned short		protocolMinor;
		// Format 1.0/1.1 stored GAME_API_VERSION here. Format 1.2 and newer
		// stores the independently versioned MVD game schema as major:minor.
		unsigned int		gameSchemaVersion;
		unsigned int		usercmdHz;
		int					startGameFrame;
		int					startGameTime;
		unsigned int		snapshotDelay;
		unsigned int		contentChecksum;
		unsigned int		indexOffset;
	};

	struct record_t {
		unsigned short		type;
		unsigned short		version;
		unsigned short		flags;
		idList<byte>		payload;
	};

	enum readResult_t {
		MVD_READ_OK,
		MVD_READ_EOF,
		MVD_READ_ERROR
	};

	struct indexEntry_t {
		int					gameTime;
		int					fileOffset;
		int					sequence;
	};

	state_t				state;
	idFile *				file;
	idStr					fileName;
	idStr					tempFileName;
	idStr					lastError;
	header_t				header;

	int						recordingStartRealTime;
	int64_t					recordingLibraryByteLimit;
	int						lastSnapshotGameFrame;
	int						lastSnapshotGameTime;
	int						lastSnapshotFileOffset;
	int						snapshotSequence;
	int						snapshotCount;
	int						reliableCount;
	int						recordCount;
	bool					finalizing;

	int						playbackLastRealTime;
	double					playbackGameTime;
	int						latestSnapshotGameFrame;
	int						latestSnapshotGameTime;
	int						predictionGameFrame;
	bool					haveSnapshot;
	bool					havePendingRecord;
	bool					sawCleanEnd;
	bool					resettingPlayback;
	bool					seekInProgress;
	bool					forcePresentationFrame;
	int						seekTargetGameTime;
	int						preferredFollowClient;
	int						playbackStreamOffset;
	int						playbackInitializationRecordCount;
	int						playbackEndGameTime;
	int						playbackLastRecordGameFrame;
	int						playbackLastRecordGameTime;
	record_t				pendingRecord;
	record_t				playbackMapState;
	record_t				playbackNetworkState;
	idList<indexEntry_t>	recordingIndex;
	// openQ4 MP-22: a client-view recording or its playback
	bool					clientView;
	int						viewClient;
	int						latestSnapshotClientFrame;	// the recorder's own frame when the snapshot was read
	int						latestSnapshotStampTime;
	usercmd_t				playbackUsercmds[MAX_USERCMD_BACKUP][MAX_ASYNC_CLIENTS];
	int						clientStartSequence;		// the recorder's last snapshot when it started
	int						clientStartSnapshotFrame;
	int						clientStartSnapshotTime;
	int						clientStartGameFrame;
	bool					timeDemo;				// playing as a benchmark
	bool					timeDemoQuit;
	int						timeDemoFrames;
	int						timeDemoStartTime;		// real time of the first timed frame
	int						timeDemoLastFrameTime;
	int						timeDemoSlowestFrame;
	bool					exporting;
	int						exportFps;
	int						exportSteps;			// match steps taken, a picture each after the first
	int						exportFrames;
	bool					exportSound;			// the sound track is recording (SoundCapture.h)
	void					FinishExportSound();
	idStrList				recordingPlayers;		// names seen while recording, for the summary
	bool					thumbnailTaken;
	idStr					pendingThumbnail;
	void					TakeRecordingThumbnail();
	void					NoteRecordingPlayer( const idDict &info );
	bool					WriteSummaryRecord();
	idStr					exportDirectory;
	// Format 1.3 navigation. While recording these collect the directories
	// written at finalization; during playback they hold what was read back.
	std::vector<openq4_mvd::Marker>	markers;
	std::vector<openq4_mvd::Keyframe> keyframes;
	int						markersDropped;
	int						lastKeyframeGameTime;
	int						lastMarkerIndex;
	int						nextPlaybackMarker;
	bool					keyframeWritesDisabled;
	serverMVDRecordingResult_t recordingResult;
	bool					recordingResultValid;
	// openQ4 MP-22: a retail Quake 4 1.4.2 network demo, decoded into this
	// player's own records in memory and played from there
	bool					retailSource;
	int						retailEntityDefs;		// the entity definitions the recording numbered
	idList<byte>			retailStream;

	void					Clear();
	void					ClearState();
	bool					StartRecording( const idCmdArgs &args );
	bool					WriteClientNetworkStateRecord();
	bool					BuildClientNetworkState( idFile_Memory &payload );
	bool					WriteClientKeyframe( int gameFrame, int gameTime );
	bool					JumpToClientKeyframe( int index, bool &worldReset );
	bool					ValidateClientNetworkState( const byte *bytes, int length, idStr &error ) const;
	bool					ApplyPlaybackNetworkState( const record_t &networkState, const char *context );
	bool					ProcessClientPlaybackRecord( const record_t &record, bool &handled );
	bool					WriteClientRecord( unsigned short type, const void *body, int bodyLength, bool required );
	void					ClientStamp( int &frame, int &time ) const;
	void					RunClientPrediction();
	bool					StopRecording( const char *reason, bool finalize,
							serverMVDResultReason_t failureReason =
								SERVER_MVD_REASON_STREAM_WRITE_FAILED );
	bool					StartPlayback( const idCmdArgs &args );
	void					StopPlayback( const char *reason );
	void					FinishPlayback( const char *reason, bool warning );
	bool					Inspect( const idCmdArgs &args );
	bool					Recover( const idCmdArgs &args );

	bool					WriteHeader();
	bool					ReadHeader( idFile *source, header_t &outHeader );
	bool					WriteRecord( unsigned short type, unsigned short version, unsigned short flags, const void *payload, int payloadLength, bool enforceLimits = true );
	readResult_t			ReadRecord( idFile *source, record_t &record );
	bool					RecordIsSupported( const record_t &record, bool duringPlayback );

	bool					WriteMetadataRecord();
	bool					WriteMapStateRecord();
	bool					WriteNetworkStateRecord();
	bool					WriteIndexRecord();
	bool					WriteNavigationRecords();
	bool					WriteKeyframeRecord( int gameFrame, int gameTime );
	void					WriteMapStatePayload( idFile *payload ) const;
	bool					ReadMapStateRecord( const record_t &record );
	bool					ReadMapStatePayload( const byte *data, int length, idDict &serverInfo,
								idDict &syncedCVars, idDict userInfo[MAX_ASYNC_CLIENTS] );
	bool					ValidateNetworkStateRecord( const record_t &record, idStr &error ) const;
	bool					ValidateNetworkStatePayload( const byte *bytes, int length, idStr &error ) const;
	bool					NoteNavigationRecord( const record_t &record, int fileOffset );
	bool					ParseNavigationTable( const record_t &record, int streamStart, int tableOffset );
	bool					ApplyUserInfoRecord( const record_t &record );
	bool					JumpToKeyframe( int index, bool &worldReset );
	bool					ProcessPlaybackRecord( const record_t &record, bool &finished );
	bool					RecordTimestamp( const record_t &record, int &gameTime ) const;
	bool					LoadNextPlaybackRecord();
	bool					BuildPlaybackIndex();
	bool					ResetPlaybackStream();
	bool					ProcessSeekBudget();
	bool					ReadMetadataRecord( const record_t &record, mvdFileInfo_t &info );
	bool					OpenRetailNetDemo();
	bool					BuildRetailStream( openq4_retail::retailNetDemo_t &demo, idFile_Memory &out );
	bool					QueryRetailFileInfo( const char *name, mvdFileInfo_t &info );

	idStr					BuildRecordingName( const idCmdArgs &args ) const;
	idStr					BuildPlaybackName( const char *name ) const;
	void					SetRecordingResult( serverMVDResultState_t resultState,
							serverMVDResultReason_t resultReason,
							const char *finalQPath, const char *partialQPath );
	bool					CommitRecording();
	bool					WouldExceedLimits( int additionalBytes ) const;
};

#endif
