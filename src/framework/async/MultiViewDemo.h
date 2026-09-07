/*
===========================================================================

openQ4 multi-view demo recording and playback.

===========================================================================
*/

#ifndef __MULTIVIEWDEMO_H__
#define __MULTIVIEWDEMO_H__

#include "NetworkSystem.h"

class idFile;
class idBitMsg;
class idCmdArgs;

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
	int						fileSize;
	idStr					mapName;
	idStr					gameType;
	idStr					serverName;
	idStr					recordedAt;
	idStr					error;
};

class idMultiViewDemo {
public:
							idMultiViewDemo();

	void					Init();
	void					Shutdown();
	void					SessionStop();

	bool					IsRecording() const;
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
	void					FreeRoam();
	bool					QueryFileInfo( const char *name, mvdFileInfo_t &info );

	void					CaptureServerFrame( int gameFrame, int gameTime );
	void					CaptureReliableMessage( const idBitMsg &msg, int routeType, int routeClient, int routeInstance = -1 );
	void					RunPlaybackFrame();
	void					OnServerMapChange();

	static void				Record_f( const idCmdArgs &args );
	static void				Stop_f( const idCmdArgs &args );
	static void				Play_f( const idCmdArgs &args );
	static void				Info_f( const idCmdArgs &args );
	static void				Status_f( const idCmdArgs &args );
	static void				Pause_f( const idCmdArgs &args );
	static void				Seek_f( const idCmdArgs &args );
	static void				Skip_f( const idCmdArgs &args );
	static void				Speed_f( const idCmdArgs &args );
	static void				Step_f( const idCmdArgs &args );
	static void				FollowNext_f( const idCmdArgs &args );
	static void				FreeRoam_f( const idCmdArgs &args );

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
	serverMVDRecordingResult_t recordingResult;
	bool					recordingResultValid;

	void					Clear();
	bool					StartRecording( const idCmdArgs &args );
	bool					StopRecording( const char *reason, bool finalize,
							serverMVDResultReason_t failureReason =
								SERVER_MVD_REASON_STREAM_WRITE_FAILED );
	bool					StartPlayback( const idCmdArgs &args );
	void					StopPlayback( const char *reason );
	void					FinishPlayback( const char *reason, bool warning );
	bool					Inspect( const idCmdArgs &args );

	bool					WriteHeader();
	bool					ReadHeader( idFile *source, header_t &outHeader );
	bool					WriteRecord( unsigned short type, unsigned short version, unsigned short flags, const void *payload, int payloadLength, bool enforceLimits = true );
	readResult_t			ReadRecord( idFile *source, record_t &record );
	bool					RecordIsSupported( const record_t &record, bool duringPlayback );

	bool					WriteMetadataRecord();
	bool					WriteMapStateRecord();
	bool					WriteNetworkStateRecord();
	bool					WriteIndexRecord();
	bool					ReadMapStateRecord( const record_t &record );
	bool					ValidateNetworkStateRecord( const record_t &record, idStr &error ) const;
	bool					ProcessPlaybackRecord( const record_t &record, bool &finished );
	bool					RecordTimestamp( const record_t &record, int &gameTime ) const;
	bool					LoadNextPlaybackRecord();
	bool					BuildPlaybackIndex();
	bool					ResetPlaybackStream();
	bool					ProcessSeekBudget();
	bool					ReadMetadataRecord( const record_t &record, mvdFileInfo_t &info );

	idStr					BuildRecordingName( const idCmdArgs &args ) const;
	idStr					BuildPlaybackName( const char *name ) const;
	void					SetRecordingResult( serverMVDResultState_t resultState,
							serverMVDResultReason_t resultReason,
							const char *finalQPath, const char *partialQPath );
	bool					CommitRecording();
	bool					WouldExceedLimits( int additionalBytes ) const;
};

#endif
