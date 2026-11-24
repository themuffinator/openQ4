//----------------------------------------------------------------
// MatchSeriesRecovery.cpp
//----------------------------------------------------------------

#if defined( MP_MATCH_SERIES_RECOVERY_STANDALONE_TEST )
	#include "MatchSeriesRecovery.h"
#else
	#include "../../../idlib/precompiled.h"
	#pragma hdrstop
	#include "MatchSeriesRecovery.h"
#endif

#include <limits.h>
#include <string.h>

namespace {

static const uint8_t MP_SERIES_RECOVERY_MAGIC[ 8 ] = {
	'O', 'Q', '4', 'S', 'R', 'E', 'C', 'B'
};
static const int MP_SERIES_RECOVERY_HEADER_BYTES = 40;
static const int MP_SERIES_RECOVERY_CHECKSUM_BYTES = 4;
static const uint16_t MP_SERIES_RECOVERY_FLAG_REPORT = 1u << 0;
static const uint16_t MP_SERIES_RECOVERY_KNOWN_FLAGS =
	MP_SERIES_RECOVERY_FLAG_REPORT;
static const char MP_SERIES_RECOVERY_PATH_PREFIX[] = "match-series/series-";
static const char MP_SERIES_RECOVERY_PATH_SUFFIX[] = ".oq4series";
static const uint64_t MP_SERIES_RECOVERY_FNV_OFFSET = UINT64_C( 14695981039346656037 );
static const uint64_t MP_SERIES_RECOVERY_FNV_PRIME = UINT64_C( 1099511628211 );

static_assert( sizeof( int ) == 4, "series recovery requires 32-bit int" );

static void SetReason( mpSeriesRecoveryReason_t *reason,
		mpSeriesRecoveryReason_t value ) {
	if ( reason != NULL ) {
		*reason = value;
	}
}

static void ClearSelectedMap( mpSeriesSelectedMap &selection ) {
	selection.poolIndex = -1;
	selection.selectedBySide = MP_SERIES_SIDE_NONE;
	selection.decider = false;
	selection.hasStartingGameSide = false;
	selection.startingGameSide = MP_SERIES_SIDE_NONE;
	selection.gameSideChosenBy = MP_SERIES_SIDE_NONE;
}

static void ClearAttempt( mpSeriesMapAttempt &attempt ) {
	attempt.selectionIndex = -1;
	attempt.outcome = MP_SERIES_MAP_UNPLAYED;
	attempt.winnerSide = MP_SERIES_SIDE_NONE;
	attempt.score[ 0 ] = 0;
	attempt.score[ 1 ] = 0;
	attempt.matchSessionId = 0;
	attempt.rulesDigest = 0;
}

static void ClearAppliedVeto( mpSeriesAppliedVeto &veto ) {
	veto.action = MP_SERIES_VETO_BAN;
	veto.actingSide = MP_SERIES_SIDE_NONE;
	veto.poolIndex = -1;
	veto.selectedGameSide = MP_SERIES_SIDE_NONE;
}

class mpRecoveryByteWriter {
public:
	mpRecoveryByteWriter( void *destination, int capacity ) :
		buffer( static_cast<uint8_t *>( destination ) ), maximum( capacity ),
		length( 0 ), valid( capacity >= 0 ) {}

	void PutU8( uint8_t value ) {
		if ( length >= maximum ) {
			valid = false;
		} else if ( buffer != NULL ) {
			buffer[ length ] = value;
		}
		++length;
	}

	void PutU16( uint16_t value ) {
		PutU8( static_cast<uint8_t>( value ) );
		PutU8( static_cast<uint8_t>( value >> 8 ) );
	}

	void PutU32( uint32_t value ) {
		for ( int shift = 0; shift < 32; shift += 8 ) {
			PutU8( static_cast<uint8_t>( value >> shift ) );
		}
	}

	void PutU64( uint64_t value ) {
		for ( int shift = 0; shift < 64; shift += 8 ) {
			PutU8( static_cast<uint8_t>( value >> shift ) );
		}
	}

	void PutI32( int value ) {
		PutU32( static_cast<uint32_t>( static_cast<int32_t>( value ) ) );
	}
	void PutI64( int64_t value ) {
		PutU64( static_cast<uint64_t>( value ) );
	}

	void PutBytes( const void *source, int bytes ) {
		if ( source == NULL || bytes < 0 ) {
			valid = false;
			return;
		}
		const uint8_t *input = static_cast<const uint8_t *>( source );
		for ( int index = 0; index < bytes; ++index ) {
			PutU8( input[ index ] );
		}
	}

	void PatchU32( int offset, uint32_t value ) {
		if ( buffer == NULL || offset < 0 || offset + 4 > maximum ||
			offset + 4 > length ) {
			valid = false;
			return;
		}
		for ( int shift = 0; shift < 32; shift += 8 ) {
			buffer[ offset + shift / 8 ] = static_cast<uint8_t>( value >> shift );
		}
	}

	bool Succeeded( void ) const { return valid && length <= maximum; }
	int Length( void ) const { return length; }

private:
	uint8_t *buffer;
	int maximum;
	int length;
	bool valid;
};

class mpRecoveryHashWriter {
public:
	mpRecoveryHashWriter( void ) : digest( MP_SERIES_RECOVERY_FNV_OFFSET ) {}

	void PutU8( uint8_t value ) {
		digest ^= value;
		digest *= MP_SERIES_RECOVERY_FNV_PRIME;
	}
	void PutU16( uint16_t value ) {
		PutU8( static_cast<uint8_t>( value ) );
		PutU8( static_cast<uint8_t>( value >> 8 ) );
	}
	void PutU32( uint32_t value ) {
		for ( int shift = 0; shift < 32; shift += 8 ) {
			PutU8( static_cast<uint8_t>( value >> shift ) );
		}
	}
	void PutU64( uint64_t value ) {
		for ( int shift = 0; shift < 64; shift += 8 ) {
			PutU8( static_cast<uint8_t>( value >> shift ) );
		}
	}
	void PutI32( int value ) {
		PutU32( static_cast<uint32_t>( static_cast<int32_t>( value ) ) );
	}
	void PutI64( int64_t value ) {
		PutU64( static_cast<uint64_t>( value ) );
	}
	void PutBytes( const void *source, int bytes ) {
		const uint8_t *input = static_cast<const uint8_t *>( source );
		for ( int index = 0; input != NULL && index < bytes; ++index ) {
			PutU8( input[ index ] );
		}
	}
	uint64_t Digest( void ) const { return digest; }

private:
	uint64_t digest;
};

static int BoundedTokenLength( const char *token ) {
	if ( token == NULL ) {
		return -1;
	}
	for ( int length = 0; length < MP_SERIES_MAP_TOKEN_BYTES; ++length ) {
		if ( token[ length ] == '\0' ) {
			return length;
		}
	}
	return -1;
}

static int BoundedTextLength( const char *text, int maximum ) {
	if ( text == NULL || maximum < 0 || maximum > UINT16_MAX ) {
		return -1;
	}
	for ( int length = 0; length <= maximum; ++length ) {
		if ( text[ length ] == '\0' ) {
			return length;
		}
	}
	return -1;
}

template< class Writer >
static bool WriteText( Writer &writer, const char *text, int maximum ) {
	const int length = BoundedTextLength( text, maximum );
	if ( length < 0 ) {
		return false;
	}
	writer.PutU16( static_cast<uint16_t>( length ) );
	if ( length > 0 ) {
		writer.PutBytes( text, length );
	}
	return true;
}

template< class Writer >
static bool WriteRecoveryPayload( const mpSeriesRecoveryRecord &record,
		Writer &writer ) {
	const mpSeriesRecoveryState &series = record.series;
	const mpSeriesConfiguration &configuration = series.configuration;

	writer.PutU8( static_cast<uint8_t>( series.state ) );
	writer.PutU64( series.revision );
	writer.PutI32( static_cast<int>( configuration.sourceProfile ) );
	writer.PutI32( configuration.gameType );
	writer.PutI32( configuration.bestOf );
	writer.PutU64( configuration.deterministicSeed );
	writer.PutI32( configuration.initialSide );
	writer.PutU8( configuration.requireStartingGameSide ? 1 : 0 );
	writer.PutU32( static_cast<uint32_t>( configuration.mapPoolCount ) );
	for ( int index = 0; index < configuration.mapPoolCount; ++index ) {
		const int length = BoundedTokenLength( configuration.mapPool[ index ] );
		if ( length < 1 || length > UINT8_MAX ) {
			return false;
		}
		writer.PutU8( static_cast<uint8_t>( length ) );
		writer.PutBytes( configuration.mapPool[ index ], length );
	}
	writer.PutU32( static_cast<uint32_t>( configuration.vetoStepCount ) );
	for ( int index = 0; index < configuration.vetoStepCount; ++index ) {
		writer.PutU8( static_cast<uint8_t>( configuration.vetoSteps[ index ].action ) );
		writer.PutI32( configuration.vetoSteps[ index ].expectedSide );
	}

	writer.PutI32( series.currentVetoStep );
	writer.PutU32( static_cast<uint32_t>( series.appliedVetoCount ) );
	for ( int index = 0; index < series.appliedVetoCount; ++index ) {
		const mpSeriesAppliedVeto &veto = series.appliedVetoes[ index ];
		writer.PutU8( static_cast<uint8_t>( veto.action ) );
		writer.PutI32( veto.actingSide );
		writer.PutI32( veto.poolIndex );
		writer.PutI32( veto.selectedGameSide );
	}
	writer.PutU32( static_cast<uint32_t>( configuration.mapPoolCount ) );
	for ( int index = 0; index < configuration.mapPoolCount; ++index ) {
		writer.PutU8( static_cast<uint8_t>( series.mapDisposition[ index ] ) );
	}

	writer.PutU32( static_cast<uint32_t>( series.selectedMapCount ) );
	for ( int index = 0; index < series.selectedMapCount; ++index ) {
		const mpSeriesSelectedMap &selection = series.selectedMaps[ index ];
		writer.PutI32( selection.poolIndex );
		writer.PutI32( selection.selectedBySide );
		writer.PutU8( selection.decider ? 1 : 0 );
		writer.PutU8( selection.hasStartingGameSide ? 1 : 0 );
		writer.PutI32( selection.startingGameSide );
		writer.PutI32( selection.gameSideChosenBy );
	}
	writer.PutI32( series.nextSelectionIndex );
	writer.PutI32( series.currentSelectionIndex );

	writer.PutU32( static_cast<uint32_t>( series.attemptCount ) );
	for ( int index = 0; index < series.attemptCount; ++index ) {
		const mpSeriesMapAttempt &attempt = series.attempts[ index ];
		writer.PutI32( attempt.selectionIndex );
		writer.PutU8( static_cast<uint8_t>( attempt.outcome ) );
		writer.PutI32( attempt.winnerSide );
		writer.PutI32( attempt.score[ 0 ] );
		writer.PutI32( attempt.score[ 1 ] );
		writer.PutU64( attempt.matchSessionId );
		writer.PutU64( attempt.rulesDigest );
	}
	writer.PutI32( series.wins[ 0 ] );
	writer.PutI32( series.wins[ 1 ] );
	writer.PutI32( series.mapLoadFailureCount );
	return true;
}

template< class Writer >
static bool WriteReportPayload( const mpSeriesReportCheckpointState &report,
		Writer &writer ) {
	writer.PutU64( report.reportRevision );
	const mpSeriesReportIdentity &identity = report.identity;
	writer.PutU64( identity.seriesId );
	writer.PutI32( static_cast<int>( identity.profile ) );
	if ( !WriteText( writer, identity.profileKey,
			MP_SERIES_REPORT_PROFILE_KEY_BYTES ) ) {
		return false;
	}
	writer.PutI32( identity.bestOf );
	writer.PutU32( identity.rulesSchema );
	writer.PutU32( identity.rulesRevision );
	writer.PutU64( identity.rulesDigest );
	writer.PutI32( identity.gameType );
	if ( !WriteText( writer, identity.modeToken,
			MP_SERIES_REPORT_MODE_TOKEN_BYTES ) ) {
		return false;
	}
	for ( int contestant = 0; contestant < MP_SERIES_SIDE_COUNT; ++contestant ) {
		const mpSeriesReportContestant &entry = identity.contestants[ contestant ];
		writer.PutU8( static_cast<uint8_t>( entry.kind ) );
		writer.PutU32( entry.participantSequence );
		if ( !WriteText( writer, entry.label,
				MP_SERIES_REPORT_DISPLAY_NAME_BYTES ) ) {
			return false;
		}
	}

	writer.PutU32( static_cast<uint32_t>( report.mapResultCount ) );
	for ( int index = 0; index < report.mapResultCount; ++index ) {
		const mpSeriesReportMapResult &map = report.mapResults[ index ];
		writer.PutU32( map.sequence );
		writer.PutU32( map.attempt );
		writer.PutU64( map.sessionId );
		if ( !WriteText( writer, map.mapToken, MP_SERIES_MAP_TOKEN_BYTES - 1 ) ) {
			return false;
		}
		writer.PutU64( map.rulesDigest );
		writer.PutU8( static_cast<uint8_t>( map.outcome ) );
		writer.PutU16( map.reason );
		writer.PutI32( map.winnerContestant );
		writer.PutI32( map.score[ 0 ] );
		writer.PutI32( map.score[ 1 ] );
		for ( int kind = 0; kind < MP_SERIES_REPORT_ARTIFACT_KIND_COUNT; ++kind ) {
			const mpSeriesReportArtifact &artifact = map.artifacts[ kind ];
			writer.PutU8( static_cast<uint8_t>( artifact.status ) );
			writer.PutU16( artifact.reason );
			if ( !WriteText( writer, artifact.qpath,
					MP_SERIES_REPORT_ARTIFACT_QPATH_BYTES ) ) {
				return false;
			}
		}
	}

	writer.PutU32( static_cast<uint32_t>( report.participantStatsCount ) );
	for ( int index = 0; index < report.participantStatsCount; ++index ) {
		const mpSeriesReportParticipantStats &stats = report.participantStats[ index ];
		writer.PutU32( stats.participantSequence );
		writer.PutI32( stats.contestant );
		if ( !WriteText( writer, stats.displayName,
				MP_SERIES_REPORT_DISPLAY_NAME_BYTES ) ) {
			return false;
		}
		writer.PutU32( stats.mapsPlayed );
		writer.PutU32( stats.mapsWon );
		writer.PutI64( stats.score );
		writer.PutU64( stats.kills );
		writer.PutU64( stats.deaths );
		writer.PutU64( stats.suicides );
		writer.PutU64( stats.damageGiven );
		writer.PutU64( stats.damageReceived );
		writer.PutU64( stats.shots );
		writer.PutU64( stats.hits );
	}

	writer.PutU32( static_cast<uint32_t>( report.teamStatsCount ) );
	for ( int index = 0; index < report.teamStatsCount; ++index ) {
		const mpSeriesReportTeamStats &stats = report.teamStats[ index ];
		writer.PutI32( stats.contestant );
		writer.PutU32( stats.mapsPlayed );
		writer.PutU32( stats.mapsWon );
		writer.PutI64( stats.score );
		writer.PutU64( stats.objectives );
		writer.PutU64( stats.roundsWon );
		writer.PutU64( stats.damageGiven );
	}
	writer.PutU64( report.droppedMapResultCount );
	writer.PutU64( report.droppedParticipantStatsCount );
	writer.PutU64( report.droppedTeamStatsCount );
	writer.PutU8( report.dropCounterSaturated ? 1 : 0 );
	writer.PutU8( static_cast<uint8_t>( report.finalResult.outcome ) );
	writer.PutU16( report.finalResult.reason );
	writer.PutI32( report.finalResult.winnerContestant );
	writer.PutU8( static_cast<uint8_t>( report.finalResult.authorizer.kind ) );
	writer.PutU32( report.finalResult.authorizer.participantSequence );
	return true;
}

class mpRecoveryByteReader {
public:
	mpRecoveryByteReader( const void *source, int bytes, int initial = 0 ) :
		buffer( static_cast<const uint8_t *>( source ) ), maximum( bytes ),
		cursor( initial ), valid( source != NULL && bytes >= 0 && initial >= 0 &&
			initial <= bytes ) {}

	bool ReadU8( uint8_t &value ) {
		if ( !valid || cursor >= maximum ) {
			valid = false;
			return false;
		}
		value = buffer[ cursor++ ];
		return true;
	}
	bool ReadU16( uint16_t &value ) {
		uint8_t low = 0, high = 0;
		if ( !ReadU8( low ) || !ReadU8( high ) ) {
			return false;
		}
		value = static_cast<uint16_t>( low | static_cast<uint16_t>( high ) << 8 );
		return true;
	}
	bool ReadU32( uint32_t &value ) {
		value = 0;
		for ( int shift = 0; shift < 32; shift += 8 ) {
			uint8_t byte = 0;
			if ( !ReadU8( byte ) ) {
				return false;
			}
			value |= static_cast<uint32_t>( byte ) << shift;
		}
		return true;
	}
	bool ReadU64( uint64_t &value ) {
		value = 0;
		for ( int shift = 0; shift < 64; shift += 8 ) {
			uint8_t byte = 0;
			if ( !ReadU8( byte ) ) {
				return false;
			}
			value |= static_cast<uint64_t>( byte ) << shift;
		}
		return true;
	}
	bool ReadI32( int &value ) {
		uint32_t encoded = 0;
		if ( !ReadU32( encoded ) ) {
			return false;
		}
		int32_t signedValue = 0;
		memcpy( &signedValue, &encoded, sizeof( signedValue ) );
		value = static_cast<int>( signedValue );
		return true;
	}
	bool ReadI64( int64_t &value ) {
		uint64_t encoded = 0;
		if ( !ReadU64( encoded ) ) {
			return false;
		}
		memcpy( &value, &encoded, sizeof( value ) );
		return true;
	}
	bool ReadBytes( void *destination, int bytes ) {
		if ( !valid || destination == NULL || bytes < 0 || bytes > maximum - cursor ) {
			valid = false;
			return false;
		}
		memcpy( destination, buffer + cursor, bytes );
		cursor += bytes;
		return true;
	}
	bool Succeeded( void ) const { return valid; }
	int Cursor( void ) const { return cursor; }

private:
	const uint8_t *buffer;
	int maximum;
	int cursor;
	bool valid;
};

static bool ReadText( mpRecoveryByteReader &reader, char *destination,
		int maximum ) {
	uint16_t length = 0;
	if ( destination == NULL || maximum < 0 || maximum > UINT16_MAX ||
		!reader.ReadU16( length ) || length > maximum ) {
		return false;
	}
	if ( length > 0 && !reader.ReadBytes( destination, length ) ) {
		return false;
	}
	destination[ length ] = '\0';
	return true;
}

static uint32_t ReadLittleU32( const uint8_t *bytes ) {
	return static_cast<uint32_t>( bytes[ 0 ] ) |
		( static_cast<uint32_t>( bytes[ 1 ] ) << 8 ) |
		( static_cast<uint32_t>( bytes[ 2 ] ) << 16 ) |
		( static_cast<uint32_t>( bytes[ 3 ] ) << 24 );
}

static uint32_t ComputeChecksum( const uint8_t *data, int bytes ) {
	uint32_t checksum = UINT32_C( 0xffffffff );
	for ( int index = 0; data != NULL && index < bytes; ++index ) {
		checksum ^= data[ index ];
		for ( int bit = 0; bit < 8; ++bit ) {
			const uint32_t mask = 0u - ( checksum & 1u );
			checksum = ( checksum >> 1 ) ^ ( UINT32_C( 0xedb88320 ) & mask );
		}
	}
	return ~checksum;
}

static bool ValidateRecoveryRevision( const mpSeriesRecoveryState &series ) {
	const uint64_t vetoes = static_cast<uint64_t>( series.currentVetoStep );
	const uint64_t failures = static_cast<uint64_t>( series.mapLoadFailureCount );
	const uint64_t attempts = static_cast<uint64_t>( series.attemptCount );
	const bool vetoComplete = series.currentVetoStep ==
		series.configuration.vetoStepCount;
	if ( !vetoComplete && ( series.mapLoadFailureCount != 0 ||
		series.attemptCount != 0 ) ) {
		return false;
	}
	if ( series.state == MP_SERIES_SETUP ) {
		return series.revision == 1;
	}
	if ( series.state == MP_SERIES_VETO ) {
		return series.revision == UINT64_C( 2 ) + vetoes;
	}
	const uint64_t completedBase = UINT64_C( 2 ) + vetoes + failures +
		UINT64_C( 3 ) * attempts;
	switch ( series.state ) {
		case MP_SERIES_READY:
		case MP_SERIES_COMPLETE:
			return vetoComplete && series.revision == completedBase;
		case MP_SERIES_MAP_ACTIVE:
			return vetoComplete && series.revision == completedBase + 1;
		case MP_SERIES_MAP_COMPLETE:
			return vetoComplete && attempts > 0 &&
				series.revision + 1 == completedBase;
		case MP_SERIES_CANCELLED:
			if ( !vetoComplete ) {
				if ( series.currentVetoStep == 0 ) {
					return series.revision == 2 || series.revision == 3;
				}
				return series.revision == UINT64_C( 3 ) + vetoes;
			}
			if ( attempts == 0 ) {
				return series.revision == completedBase + 1 ||
					series.revision == completedBase + 2;
			}
			return series.revision == completedBase ||
				series.revision == completedBase + 1 ||
				series.revision == completedBase + 2;
		default:
			return false;
	}
}

static mpSeriesReportMapOutcome_t ReportOutcomeForSeries(
		mpSeriesMapOutcome_t outcome ) {
	switch ( outcome ) {
		case MP_SERIES_MAP_DECIDED: return MP_SERIES_REPORT_MAP_DECIDED;
		case MP_SERIES_MAP_FORFEIT: return MP_SERIES_REPORT_MAP_FORFEIT;
		case MP_SERIES_MAP_ABORTED: return MP_SERIES_REPORT_MAP_ABORTED;
		default: return MP_SERIES_REPORT_MAP_OUTCOME_COUNT;
	}
}

static bool ValidateReportAgainstSeries( const mpCompetitionSeries &series,
		uint64_t seriesId, const mpSeriesReportCheckpointState &state ) {
	mpCompetitionSeriesReport report;
	if ( !report.RestoreCheckpointState( state ) ) {
		return false;
	}
	const mpSeriesReportIdentity &identity = report.GetIdentity();
	const mpSeriesConfiguration &configuration = series.GetConfiguration();
	if ( identity.seriesId != seriesId ||
		identity.profile != configuration.sourceProfile ||
		identity.bestOf != configuration.bestOf ||
		identity.gameType != configuration.gameType ||
		report.GetMapResultCount() != series.GetAttemptCount() ) {
		return false;
	}
	for ( int index = 0; index < series.GetAttemptCount(); ++index ) {
		const mpSeriesMapAttempt *attempt = series.GetAttempt( index );
		const mpSeriesReportMapResult *map = report.GetMapResult( index );
		if ( attempt == NULL || map == NULL || map->attempt !=
				static_cast<uint32_t>( index ) + 1 ||
			attempt->selectionIndex < 0 ||
			attempt->selectionIndex >= series.GetSelectedMapCount() ) {
			return false;
		}
		const mpSeriesSelectedMap *selection = series.GetSelectedMap(
			attempt->selectionIndex );
		if ( selection == NULL || selection->poolIndex < 0 ||
			selection->poolIndex >= configuration.mapPoolCount ||
			strcmp( map->mapToken,
				configuration.mapPool[ selection->poolIndex ] ) != 0 ||
			map->sessionId != attempt->matchSessionId ||
			map->rulesDigest != attempt->rulesDigest ||
			map->outcome != ReportOutcomeForSeries( attempt->outcome ) ||
			map->winnerContestant != attempt->winnerSide ||
			map->score[ 0 ] != attempt->score[ 0 ] ||
			map->score[ 1 ] != attempt->score[ 1 ] ) {
			return false;
		}
	}
	if ( series.GetState() == MP_SERIES_COMPLETE ) {
		return report.IsFinalized() &&
			report.GetFinal().outcome == MP_SERIES_REPORT_FINAL_COMPLETE;
	}
	if ( series.GetState() == MP_SERIES_CANCELLED ) {
		return report.IsFinalized() &&
			report.GetFinal().outcome == MP_SERIES_REPORT_FINAL_CANCELLED;
	}
	return !report.IsFinalized();
}

static bool ValidateLogicalRecord( const mpSeriesRecoveryRecord &record,
		mpSeriesRecoveryReason_t *reason ) {
	SetReason( reason, MP_SERIES_RECOVERY_REASON_NONE );
	if ( record.seriesId == 0 || record.linkedSessionId == 0 ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_INVALID_IDENTITY );
		return false;
	}
	if ( record.series.schemaVersion != MP_SERIES_RECOVERY_SCHEMA_VERSION ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_UNSUPPORTED_SCHEMA );
		return false;
	}
	if ( record.series.state == MP_SERIES_DISABLED || record.series.revision == 0 ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_INVALID_SERIES );
		return false;
	}
	mpCompetitionSeries candidate;
	if ( !candidate.RestoreRecoveryState( record.series ) ||
		!candidate.ValidateInvariants() || !ValidateRecoveryRevision( record.series ) ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_INVALID_SERIES );
		return false;
	}
	if ( record.hasReport && !ValidateReportAgainstSeries( candidate,
			record.seriesId, record.report ) ) {
		SetReason( reason, record.report.schemaVersion !=
			MP_SERIES_REPORT_CHECKPOINT_STATE_VERSION ?
			MP_SERIES_RECOVERY_REASON_INVALID_REPORT :
			MP_SERIES_RECOVERY_REASON_SERIES_REPORT_MISMATCH );
		return false;
	}
	return true;
}

static bool ReadRecoveryPayload( mpRecoveryByteReader &reader,
		mpSeriesRecoveryRecord &record ) {
	mpSeriesRecoveryState &series = record.series;
	mpSeriesConfiguration &configuration = series.configuration;
	uint8_t byte = 0;
	uint32_t count = 0;
	int sourceProfile = 0;
	uint8_t requireStartingGameSide = 0;

	if ( !reader.ReadU8( byte ) || byte >= MP_SERIES_STATE_COUNT ) {
		return false;
	}
	series.state = static_cast<mpSeriesState_t>( byte );
	if ( !reader.ReadU64( series.revision ) ||
		!reader.ReadI32( sourceProfile ) ||
		!reader.ReadI32( configuration.gameType ) ||
		!reader.ReadI32( configuration.bestOf ) ||
		!reader.ReadU64( configuration.deterministicSeed ) ||
		!reader.ReadI32( configuration.initialSide ) ||
		!reader.ReadU8( requireStartingGameSide ) ||
		requireStartingGameSide > 1 ||
		!reader.ReadU32( count ) || count > MP_SERIES_MAX_MAP_POOL ) {
		return false;
	}
	configuration.sourceProfile = static_cast<mpSeriesProfileId_t>( sourceProfile );
	configuration.requireStartingGameSide = requireStartingGameSide != 0;
	configuration.mapPoolCount = static_cast<int>( count );
	for ( int index = 0; index < configuration.mapPoolCount; ++index ) {
		if ( !reader.ReadU8( byte ) || byte < 1 || byte >= MP_SERIES_MAP_TOKEN_BYTES ||
			!reader.ReadBytes( configuration.mapPool[ index ], byte ) ) {
			return false;
		}
		configuration.mapPool[ index ][ byte ] = '\0';
	}
	if ( !reader.ReadU32( count ) || count > MP_SERIES_MAX_VETO_STEPS ) {
		return false;
	}
	configuration.vetoStepCount = static_cast<int>( count );
	for ( int index = 0; index < configuration.vetoStepCount; ++index ) {
		int side = 0;
		if ( !reader.ReadU8( byte ) || byte >= MP_SERIES_VETO_ACTION_COUNT ||
			!reader.ReadI32( side ) ) {
			return false;
		}
		configuration.vetoSteps[ index ].action =
			static_cast<mpSeriesVetoAction_t>( byte );
		configuration.vetoSteps[ index ].expectedSide = side;
	}

	if ( !reader.ReadI32( series.currentVetoStep ) ||
		!reader.ReadU32( count ) || count > MP_SERIES_MAX_VETO_STEPS ) {
		return false;
	}
	series.appliedVetoCount = static_cast<int>( count );
	for ( int index = 0; index < series.appliedVetoCount; ++index ) {
		mpSeriesAppliedVeto &veto = series.appliedVetoes[ index ];
		if ( !reader.ReadU8( byte ) || byte >= MP_SERIES_VETO_ACTION_COUNT ||
			!reader.ReadI32( veto.actingSide ) ||
			!reader.ReadI32( veto.poolIndex ) ||
			!reader.ReadI32( veto.selectedGameSide ) ) {
			return false;
		}
		veto.action = static_cast<mpSeriesVetoAction_t>( byte );
	}
	if ( !reader.ReadU32( count ) ||
		count != static_cast<uint32_t>( configuration.mapPoolCount ) ) {
		return false;
	}
	for ( int index = 0; index < configuration.mapPoolCount; ++index ) {
		if ( !reader.ReadU8( byte ) || byte >= MP_SERIES_MAP_DISPOSITION_COUNT ) {
			return false;
		}
		series.mapDisposition[ index ] =
			static_cast<mpSeriesMapDisposition_t>( byte );
	}

	if ( !reader.ReadU32( count ) || count > MP_SERIES_MAX_BEST_OF ) {
		return false;
	}
	series.selectedMapCount = static_cast<int>( count );
	for ( int index = 0; index < series.selectedMapCount; ++index ) {
		mpSeriesSelectedMap &selection = series.selectedMaps[ index ];
		uint8_t decider = 0;
		uint8_t hasSide = 0;
		if ( !reader.ReadI32( selection.poolIndex ) ||
			!reader.ReadI32( selection.selectedBySide ) ||
			!reader.ReadU8( decider ) || decider > 1 ||
			!reader.ReadU8( hasSide ) || hasSide > 1 ||
			!reader.ReadI32( selection.startingGameSide ) ||
			!reader.ReadI32( selection.gameSideChosenBy ) ) {
			return false;
		}
		selection.decider = decider != 0;
		selection.hasStartingGameSide = hasSide != 0;
	}
	if ( !reader.ReadI32( series.nextSelectionIndex ) ||
		!reader.ReadI32( series.currentSelectionIndex ) ||
		!reader.ReadU32( count ) || count > MP_SERIES_MAX_MAP_ATTEMPTS ) {
		return false;
	}
	series.attemptCount = static_cast<int>( count );
	for ( int index = 0; index < series.attemptCount; ++index ) {
		mpSeriesMapAttempt &attempt = series.attempts[ index ];
		if ( !reader.ReadI32( attempt.selectionIndex ) ||
			!reader.ReadU8( byte ) || byte >= MP_SERIES_MAP_OUTCOME_COUNT ||
			!reader.ReadI32( attempt.winnerSide ) ||
			!reader.ReadI32( attempt.score[ 0 ] ) ||
			!reader.ReadI32( attempt.score[ 1 ] ) ||
			!reader.ReadU64( attempt.matchSessionId ) ||
			!reader.ReadU64( attempt.rulesDigest ) ) {
			return false;
		}
		attempt.outcome = static_cast<mpSeriesMapOutcome_t>( byte );
	}
	return reader.ReadI32( series.wins[ 0 ] ) &&
		reader.ReadI32( series.wins[ 1 ] ) &&
		reader.ReadI32( series.mapLoadFailureCount );
}

static bool ReadReportPayload( mpRecoveryByteReader &reader,
		mpSeriesReportCheckpointState &report ) {
	report.Clear();
	report.initialized = true;
	mpSeriesReportIdentity &identity = report.identity;
	int value = 0;
	uint8_t byte = 0;
	uint32_t count = 0;
	if ( !reader.ReadU64( report.reportRevision ) ||
		!reader.ReadU64( identity.seriesId ) || !reader.ReadI32( value ) ) {
		return false;
	}
	identity.profile = static_cast<mpSeriesProfileId_t>( value );
	if ( !ReadText( reader, identity.profileKey,
			MP_SERIES_REPORT_PROFILE_KEY_BYTES ) ||
		!reader.ReadI32( identity.bestOf ) ||
		!reader.ReadU32( identity.rulesSchema ) ||
		!reader.ReadU32( identity.rulesRevision ) ||
		!reader.ReadU64( identity.rulesDigest ) ||
		!reader.ReadI32( identity.gameType ) ||
		!ReadText( reader, identity.modeToken,
			MP_SERIES_REPORT_MODE_TOKEN_BYTES ) ) {
		return false;
	}
	for ( int contestant = 0; contestant < MP_SERIES_SIDE_COUNT; ++contestant ) {
		mpSeriesReportContestant &entry = identity.contestants[ contestant ];
		if ( !reader.ReadU8( byte ) ||
			!reader.ReadU32( entry.participantSequence ) ||
			!ReadText( reader, entry.label,
				MP_SERIES_REPORT_DISPLAY_NAME_BYTES ) ) {
			return false;
		}
		entry.kind = static_cast<mpSeriesReportContestantKind_t>( byte );
	}

	if ( !reader.ReadU32( count ) || count > MP_SERIES_REPORT_MAX_MAP_RESULTS ) {
		return false;
	}
	report.mapResultCount = static_cast<int>( count );
	for ( int index = 0; index < report.mapResultCount; ++index ) {
		mpSeriesReportMapResult &map = report.mapResults[ index ];
		if ( !reader.ReadU32( map.sequence ) ||
			!reader.ReadU32( map.attempt ) ||
			!reader.ReadU64( map.sessionId ) ||
			!ReadText( reader, map.mapToken, MP_SERIES_MAP_TOKEN_BYTES - 1 ) ||
			!reader.ReadU64( map.rulesDigest ) ||
			!reader.ReadU8( byte ) ) {
			return false;
		}
		map.outcome = static_cast<mpSeriesReportMapOutcome_t>( byte );
		if ( !reader.ReadU16( map.reason ) || !reader.ReadI32( value ) ) {
			return false;
		}
		map.winnerContestant = static_cast<int8_t>( value );
		for ( int side = 0; side < MP_SERIES_SIDE_COUNT; ++side ) {
			if ( !reader.ReadI32( value ) ) {
				return false;
			}
			map.score[ side ] = static_cast<int32_t>( value );
		}
		for ( int kind = 0; kind < MP_SERIES_REPORT_ARTIFACT_KIND_COUNT; ++kind ) {
			mpSeriesReportArtifact &artifact = map.artifacts[ kind ];
			if ( !reader.ReadU8( byte ) ) {
				return false;
			}
			artifact.status = static_cast<mpSeriesReportArtifactStatus_t>( byte );
			if ( !reader.ReadU16( artifact.reason ) ||
				!ReadText( reader, artifact.qpath,
					MP_SERIES_REPORT_ARTIFACT_QPATH_BYTES ) ) {
				return false;
			}
		}
	}

	if ( !reader.ReadU32( count ) ||
		count > MP_SERIES_REPORT_MAX_PARTICIPANTS ) {
		return false;
	}
	report.participantStatsCount = static_cast<int>( count );
	for ( int index = 0; index < report.participantStatsCount; ++index ) {
		mpSeriesReportParticipantStats &stats = report.participantStats[ index ];
		if ( !reader.ReadU32( stats.participantSequence ) ||
			!reader.ReadI32( value ) ) {
			return false;
		}
		stats.contestant = static_cast<int8_t>( value );
		if ( !ReadText( reader, stats.displayName,
				MP_SERIES_REPORT_DISPLAY_NAME_BYTES ) ||
			!reader.ReadU32( stats.mapsPlayed ) ||
			!reader.ReadU32( stats.mapsWon ) ||
			!reader.ReadI64( stats.score ) ||
			!reader.ReadU64( stats.kills ) ||
			!reader.ReadU64( stats.deaths ) ||
			!reader.ReadU64( stats.suicides ) ||
			!reader.ReadU64( stats.damageGiven ) ||
			!reader.ReadU64( stats.damageReceived ) ||
			!reader.ReadU64( stats.shots ) ||
			!reader.ReadU64( stats.hits ) ) {
			return false;
		}
	}

	if ( !reader.ReadU32( count ) || count > MP_SERIES_REPORT_MAX_TEAMS ) {
		return false;
	}
	report.teamStatsCount = static_cast<int>( count );
	for ( int index = 0; index < report.teamStatsCount; ++index ) {
		mpSeriesReportTeamStats &stats = report.teamStats[ index ];
		if ( !reader.ReadI32( value ) ) {
			return false;
		}
		stats.contestant = static_cast<int8_t>( value );
		if ( !reader.ReadU32( stats.mapsPlayed ) ||
			!reader.ReadU32( stats.mapsWon ) ||
			!reader.ReadI64( stats.score ) ||
			!reader.ReadU64( stats.objectives ) ||
			!reader.ReadU64( stats.roundsWon ) ||
			!reader.ReadU64( stats.damageGiven ) ) {
			return false;
		}
	}
	uint8_t saturated = 0;
	if ( !reader.ReadU64( report.droppedMapResultCount ) ||
		!reader.ReadU64( report.droppedParticipantStatsCount ) ||
		!reader.ReadU64( report.droppedTeamStatsCount ) ||
		!reader.ReadU8( saturated ) || saturated > 1 ||
		!reader.ReadU8( byte ) ) {
		return false;
	}
	report.dropCounterSaturated = saturated != 0;
	report.finalResult.outcome = static_cast<mpSeriesReportFinalOutcome_t>( byte );
	if ( !reader.ReadU16( report.finalResult.reason ) ||
		!reader.ReadI32( value ) ) {
		return false;
	}
	report.finalResult.winnerContestant = static_cast<int8_t>( value );
	if ( !reader.ReadU8( byte ) ||
		!reader.ReadU32( report.finalResult.authorizer.participantSequence ) ) {
		return false;
	}
	report.finalResult.authorizer.kind =
		static_cast<mpSeriesReportAuthorizerKind_t>( byte );
	return true;
}

class mpRecoveryPathBuilder {
public:
	mpRecoveryPathBuilder( char *destination, int capacity ) :
		buffer( destination ), maximum( capacity ), length( 0 ),
		valid( destination != NULL && capacity > 0 ) {
		if ( valid ) {
			buffer[ 0 ] = '\0';
		}
	}
	void PutCharacter( char value ) {
		if ( !valid || length >= maximum - 1 ) {
			valid = false;
			return;
		}
		buffer[ length++ ] = value;
		buffer[ length ] = '\0';
	}
	void PutLiteral( const char *value ) {
		if ( value == NULL ) {
			valid = false;
			return;
		}
		for ( int index = 0; value[ index ] != '\0'; ++index ) {
			PutCharacter( value[ index ] );
		}
	}
	void PutUnsigned64( uint64_t value ) {
		char reverse[ 20 ];
		int count = 0;
		do {
			reverse[ count++ ] = static_cast<char>( '0' + value % 10u );
			value /= 10u;
		} while ( value != 0 && count < static_cast<int>( sizeof( reverse ) ) );
		for ( int index = count - 1; index >= 0; --index ) {
			PutCharacter( reverse[ index ] );
		}
	}
	bool Succeeded( void ) const { return valid; }

private:
	char *buffer;
	int maximum;
	int length;
	bool valid;
};

static bool GetBoundedPathLength( const char *path, int &length ) {
	if ( path == NULL ) {
		return false;
	}
	for ( length = 0; length <= MP_SERIES_RECOVERY_QPATH_BYTES; ++length ) {
		if ( path[ length ] == '\0' ) {
			return length > 0;
		}
	}
	return false;
}

static bool ReadLiteral( const char *path, int length, int &cursor,
		const char *literal ) {
	for ( int index = 0; literal != NULL && literal[ index ] != '\0'; ++index ) {
		if ( cursor >= length || path[ cursor++ ] != literal[ index ] ) {
			return false;
		}
	}
	return literal != NULL;
}

static bool ReadCanonicalUnsigned64( const char *path, int length, int &cursor ) {
	static const char maximumUnsigned64[] = "18446744073709551615";
	const int start = cursor;
	while ( cursor < length && path[ cursor ] >= '0' && path[ cursor ] <= '9' ) {
		++cursor;
	}
	const int digits = cursor - start;
	if ( digits < 1 || digits > 20 || path[ start ] == '0' ) {
		return false;
	}
	if ( digits == 20 ) {
		for ( int index = 0; index < digits; ++index ) {
			if ( path[ start + index ] < maximumUnsigned64[ index ] ) {
				break;
			}
			if ( path[ start + index ] > maximumUnsigned64[ index ] ) {
				return false;
			}
		}
	}
	return true;
}

static bool IsCanonicalRecoveryQPath( const char *path, bool temporary ) {
	int length = 0;
	if ( !GetBoundedPathLength( path, length ) ) {
		return false;
	}
	int cursor = 0;
	if ( !ReadLiteral( path, length, cursor, MP_SERIES_RECOVERY_PATH_PREFIX ) ||
		!ReadCanonicalUnsigned64( path, length, cursor ) ||
		!ReadLiteral( path, length, cursor, MP_SERIES_RECOVERY_PATH_SUFFIX ) ) {
		return false;
	}
	if ( temporary &&
		( !ReadLiteral( path, length, cursor, ".pending-" ) ||
		!ReadCanonicalUnsigned64( path, length, cursor ) ) ) {
		return false;
	}
	return cursor == length;
}

static void RecordCleanup( mpMatchSeriesRecoveryWriter &writer,
		mpSeriesRecoveryStorageResult &result ) {
	if ( !writer.RemoveTemp( result.paths.temporaryQPath ) ) {
		result.cleanupReason = MP_SERIES_RECOVERY_REASON_TEMP_CLEANUP_FAILED;
	}
}

} // namespace

void mpSeriesRecoveryState::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	schemaVersion = MP_SERIES_RECOVERY_SCHEMA_VERSION;
	state = MP_SERIES_DISABLED;
	configuration.sourceProfile = MP_SERIES_PROFILE_CUSTOM;
	configuration.gameType = -1;
	configuration.initialSide = MP_SERIES_SIDE_NONE;
	currentSelectionIndex = -1;
	for ( int index = 0; index < MP_SERIES_MAX_MAP_POOL; ++index ) {
		mapDisposition[ index ] = MP_SERIES_MAP_AVAILABLE;
	}
	for ( int index = 0; index < MP_SERIES_MAX_VETO_STEPS; ++index ) {
		ClearAppliedVeto( appliedVetoes[ index ] );
	}
	for ( int index = 0; index < MP_SERIES_MAX_BEST_OF; ++index ) {
		ClearSelectedMap( selectedMaps[ index ] );
	}
	for ( int index = 0; index < MP_SERIES_MAX_MAP_ATTEMPTS; ++index ) {
		ClearAttempt( attempts[ index ] );
	}
}

void mpSeriesRecoveryRecord::Clear( void ) {
	seriesId = 0;
	linkedSessionId = 0;
	series.Clear();
	hasReport = false;
	report.Clear();
	contentDigest = 0;
}

void mpSeriesRecoveryCodecResult::Clear( void ) {
	reason = MP_SERIES_RECOVERY_REASON_NONE;
	bytes = 0;
	requiredCapacity = 0;
	contentDigest = 0;
	checksum = 0;
}

bool mpSeriesRecoveryCodecResult::Succeeded( void ) const {
	return reason == MP_SERIES_RECOVERY_REASON_NONE && bytes > 0;
}

void mpSeriesRecoveryPaths::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
}

void mpSeriesRecoveryStorageResult::Clear( void ) {
	memset( this, 0, sizeof( *this ) );
	code = MP_SERIES_RECOVERY_STORAGE_REJECTED;
	reason = MP_SERIES_RECOVERY_REASON_NONE;
	cleanupReason = MP_SERIES_RECOVERY_REASON_NONE;
	backendBytes = -1;
	paths.Clear();
}

bool mpSeriesRecoveryStorageResult::Succeeded( void ) const {
	return code == MP_SERIES_RECOVERY_STORAGE_STORED &&
		reason == MP_SERIES_RECOVERY_REASON_NONE &&
		cleanupReason == MP_SERIES_RECOVERY_REASON_NONE;
}

bool MPMatchSeriesRecoveryCapture( const mpCompetitionSeries &series,
	uint64_t seriesId, uint64_t linkedSessionId, mpSeriesRecoveryRecord &record,
	mpSeriesRecoveryReason_t *reason ) {
	SetReason( reason, MP_SERIES_RECOVERY_REASON_NONE );
	mpSeriesRecoveryRecord candidate;
	candidate.Clear();
	candidate.seriesId = seriesId;
	candidate.linkedSessionId = linkedSessionId;
	if ( !series.ExportRecoveryState( candidate.series ) ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_INVALID_SERIES );
		return false;
	}
	if ( !ValidateLogicalRecord( candidate, reason ) ) {
		return false;
	}
	candidate.contentDigest = MPMatchSeriesRecoveryComputeContentDigest( candidate );
	if ( candidate.contentDigest == 0 ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_INVALID_SERIES );
		return false;
	}
	record = candidate;
	return true;
}

bool MPMatchSeriesRecoveryCapture( const mpCompetitionSeries &series,
		const mpCompetitionSeriesReport &report, uint64_t seriesId,
		uint64_t linkedSessionId, mpSeriesRecoveryRecord &record,
		mpSeriesRecoveryReason_t *reason ) {
	SetReason( reason, MP_SERIES_RECOVERY_REASON_NONE );
	mpSeriesRecoveryRecord candidate;
	candidate.Clear();
	candidate.seriesId = seriesId;
	candidate.linkedSessionId = linkedSessionId;
	candidate.hasReport = true;
	if ( !series.ExportRecoveryState( candidate.series ) ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_INVALID_SERIES );
		return false;
	}
	if ( !report.ExportCheckpointState( candidate.report ) ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_INVALID_REPORT );
		return false;
	}
	if ( !ValidateLogicalRecord( candidate, reason ) ) {
		return false;
	}
	candidate.contentDigest = MPMatchSeriesRecoveryComputeContentDigest( candidate );
	if ( candidate.contentDigest == 0 ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_SERIES_REPORT_MISMATCH );
		return false;
	}
	record = candidate;
	return true;
}

bool MPMatchSeriesRecoveryRestoreCores( const mpSeriesRecoveryRecord &record,
		mpCompetitionSeries &series, mpCompetitionSeriesReport &report,
		mpSeriesRecoveryReason_t *reason ) {
	if ( !MPMatchSeriesRecoveryValidate( record, reason ) ) {
		return false;
	}
	if ( !record.hasReport ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_INVALID_REPORT );
		return false;
	}
	mpCompetitionSeries candidateSeries;
	mpCompetitionSeriesReport candidateReport;
	if ( !candidateSeries.RestoreRecoveryState( record.series ) ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_INVALID_SERIES );
		return false;
	}
	if ( !candidateReport.RestoreCheckpointState( record.report ) ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_INVALID_REPORT );
		return false;
	}
	if ( !ValidateReportAgainstSeries( candidateSeries, record.seriesId,
			record.report ) ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_SERIES_REPORT_MISMATCH );
		return false;
	}
	series = candidateSeries;
	report = candidateReport;
	SetReason( reason, MP_SERIES_RECOVERY_REASON_NONE );
	return true;
}

bool MPMatchSeriesRecoveryValidate( const mpSeriesRecoveryRecord &record,
		mpSeriesRecoveryReason_t *reason ) {
	if ( !ValidateLogicalRecord( record, reason ) ) {
		return false;
	}
	const uint64_t expected = MPMatchSeriesRecoveryComputeContentDigest( record );
	if ( record.contentDigest == 0 || record.contentDigest != expected ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_DIGEST_MISMATCH );
		return false;
	}
	SetReason( reason, MP_SERIES_RECOVERY_REASON_NONE );
	return true;
}

static uint64_t ComputeLegacyV2ContentDigest(
		const mpSeriesRecoveryRecord &record ) {
	mpRecoveryHashWriter writer;
	writer.PutU16( MP_SERIES_RECOVERY_LEGACY_SCHEMA_VERSION );
	writer.PutU64( record.seriesId );
	writer.PutU64( record.linkedSessionId );
	if ( !WriteRecoveryPayload( record, writer ) ) {
		return 0;
	}
	return writer.Digest();
}

uint64_t MPMatchSeriesRecoveryComputeContentDigest(
		const mpSeriesRecoveryRecord &record ) {
	if ( !ValidateLogicalRecord( record, NULL ) ) {
		return 0;
	}
	mpRecoveryHashWriter writer;
	writer.PutU16( MP_SERIES_RECOVERY_SCHEMA_VERSION );
	writer.PutU64( record.seriesId );
	writer.PutU64( record.linkedSessionId );
	writer.PutU8( record.hasReport ? 1 : 0 );
	if ( !WriteRecoveryPayload( record, writer ) ) {
		return 0;
	}
	if ( record.hasReport && !WriteReportPayload( record.report, writer ) ) {
		return 0;
	}
	return writer.Digest();
}

static uint64_t ComputePreviousV3ContentDigest(
		const mpSeriesRecoveryRecord &record ) {
	mpRecoveryHashWriter writer;
	writer.PutU16( MP_SERIES_RECOVERY_PREVIOUS_SCHEMA_VERSION );
	writer.PutU64( record.seriesId );
	writer.PutU64( record.linkedSessionId );
	writer.PutU8( record.hasReport ? 1 : 0 );
	if ( !WriteRecoveryPayload( record, writer ) ) {
		return 0;
	}
	if ( record.hasReport && !WriteReportPayload( record.report, writer ) ) {
		return 0;
	}
	return writer.Digest();
}

mpSeriesRecoveryCodecResult MPMatchSeriesRecoveryEncode(
		const mpSeriesRecoveryRecord &record, void *destination, int capacity ) {
	mpSeriesRecoveryCodecResult result;
	result.Clear();
	if ( destination == NULL || capacity < 0 ) {
		result.reason = MP_SERIES_RECOVERY_REASON_INVALID_ARGUMENT;
		return result;
	}
	if ( !MPMatchSeriesRecoveryValidate( record, &result.reason ) ) {
		return result;
	}

	mpRecoveryByteWriter writer( destination, capacity );
	writer.PutBytes( MP_SERIES_RECOVERY_MAGIC, sizeof( MP_SERIES_RECOVERY_MAGIC ) );
	writer.PutU16( MP_SERIES_RECOVERY_SCHEMA_VERSION );
	writer.PutU16( record.hasReport ? MP_SERIES_RECOVERY_FLAG_REPORT : 0 );
	writer.PutU32( 0 );
	writer.PutU64( record.seriesId );
	writer.PutU64( record.linkedSessionId );
	writer.PutU64( record.contentDigest );
	if ( !WriteRecoveryPayload( record, writer ) ) {
		result.reason = MP_SERIES_RECOVERY_REASON_INVALID_SERIES;
		return result;
	}
	if ( record.hasReport && !WriteReportPayload( record.report, writer ) ) {
		result.reason = MP_SERIES_RECOVERY_REASON_INVALID_REPORT;
		return result;
	}
	writer.PutU32( 0 );
	result.requiredCapacity = writer.Length();
	if ( !writer.Succeeded() || result.requiredCapacity > MP_SERIES_RECOVERY_MAX_BYTES ) {
		result.reason = MP_SERIES_RECOVERY_REASON_BUFFER_TOO_SMALL;
		return result;
	}
	writer.PatchU32( 12, static_cast<uint32_t>( result.requiredCapacity ) );
	if ( !writer.Succeeded() ) {
		result.reason = MP_SERIES_RECOVERY_REASON_BUFFER_TOO_SMALL;
		return result;
	}
	uint8_t *encoded = static_cast<uint8_t *>( destination );
	result.checksum = ComputeChecksum( encoded,
		result.requiredCapacity - MP_SERIES_RECOVERY_CHECKSUM_BYTES );
	const int checksumOffset = result.requiredCapacity - MP_SERIES_RECOVERY_CHECKSUM_BYTES;
	for ( int shift = 0; shift < 32; shift += 8 ) {
		encoded[ checksumOffset + shift / 8 ] =
			static_cast<uint8_t>( result.checksum >> shift );
	}
	result.reason = MP_SERIES_RECOVERY_REASON_NONE;
	result.bytes = result.requiredCapacity;
	result.contentDigest = record.contentDigest;
	return result;
}

mpSeriesRecoveryCodecResult MPMatchSeriesRecoveryDecode( const void *source,
		int bytes, mpSeriesRecoveryRecord &output ) {
	mpSeriesRecoveryCodecResult result;
	result.Clear();
	if ( source == NULL || bytes < 0 ) {
		result.reason = MP_SERIES_RECOVERY_REASON_INVALID_ARGUMENT;
		return result;
	}
	if ( bytes < MP_SERIES_RECOVERY_HEADER_BYTES +
		MP_SERIES_RECOVERY_CHECKSUM_BYTES ) {
		result.reason = MP_SERIES_RECOVERY_REASON_TRUNCATED_RECORD;
		return result;
	}
	if ( bytes > MP_SERIES_RECOVERY_MAX_BYTES ) {
		result.reason = MP_SERIES_RECOVERY_REASON_TRAILING_DATA;
		return result;
	}
	const uint8_t *encoded = static_cast<const uint8_t *>( source );
	if ( memcmp( encoded, MP_SERIES_RECOVERY_MAGIC,
			sizeof( MP_SERIES_RECOVERY_MAGIC ) ) != 0 ) {
		result.reason = MP_SERIES_RECOVERY_REASON_MALFORMED_RECORD;
		return result;
	}

	mpRecoveryByteReader header( source, bytes, 8 );
	uint16_t schema = 0;
	uint16_t flags = 0;
	uint32_t totalBytes = 0;
	if ( !header.ReadU16( schema ) || !header.ReadU16( flags ) ||
		!header.ReadU32( totalBytes ) ) {
		result.reason = MP_SERIES_RECOVERY_REASON_TRUNCATED_RECORD;
		return result;
	}
	const bool legacyV2 = schema == MP_SERIES_RECOVERY_LEGACY_SCHEMA_VERSION;
	const bool previousV3 =
		schema == MP_SERIES_RECOVERY_PREVIOUS_SCHEMA_VERSION;
	if ( !legacyV2 && !previousV3 &&
		schema != MP_SERIES_RECOVERY_SCHEMA_VERSION ) {
		result.reason = MP_SERIES_RECOVERY_REASON_UNSUPPORTED_SCHEMA;
		return result;
	}
	if ( ( legacyV2 && flags != 0 ) ||
		( !legacyV2 && ( flags & ~MP_SERIES_RECOVERY_KNOWN_FLAGS ) != 0 ) ||
		totalBytes < static_cast<uint32_t>(
			MP_SERIES_RECOVERY_HEADER_BYTES + MP_SERIES_RECOVERY_CHECKSUM_BYTES ) ||
		totalBytes > MP_SERIES_RECOVERY_MAX_BYTES ) {
		result.reason = MP_SERIES_RECOVERY_REASON_MALFORMED_RECORD;
		return result;
	}
	if ( totalBytes > static_cast<uint32_t>( bytes ) ) {
		result.reason = MP_SERIES_RECOVERY_REASON_TRUNCATED_RECORD;
		return result;
	}
	if ( totalBytes < static_cast<uint32_t>( bytes ) ) {
		result.reason = MP_SERIES_RECOVERY_REASON_TRAILING_DATA;
		return result;
	}

	const uint32_t storedChecksum = ReadLittleU32( encoded + bytes - 4 );
	result.checksum = ComputeChecksum( encoded, bytes - 4 );
	if ( storedChecksum != result.checksum ) {
		result.reason = MP_SERIES_RECOVERY_REASON_CHECKSUM_MISMATCH;
		return result;
	}

	mpSeriesRecoveryRecord candidate;
	candidate.Clear();
	if ( !header.ReadU64( candidate.seriesId ) ||
		!header.ReadU64( candidate.linkedSessionId ) ||
		!header.ReadU64( candidate.contentDigest ) ||
		header.Cursor() != MP_SERIES_RECOVERY_HEADER_BYTES ) {
		result.reason = MP_SERIES_RECOVERY_REASON_MALFORMED_RECORD;
		return result;
	}
	mpRecoveryByteReader payload( source, bytes - 4,
		MP_SERIES_RECOVERY_HEADER_BYTES );
	candidate.hasReport = !legacyV2 &&
		( flags & MP_SERIES_RECOVERY_FLAG_REPORT ) != 0;
	if ( !ReadRecoveryPayload( payload, candidate ) ||
		( candidate.hasReport && !ReadReportPayload( payload, candidate.report ) ) ||
		!payload.Succeeded() ||
		payload.Cursor() != bytes - 4 ) {
		result.reason = MP_SERIES_RECOVERY_REASON_MALFORMED_RECORD;
		return result;
	}
	mpSeriesRecoveryReason_t validationReason = MP_SERIES_RECOVERY_REASON_NONE;
	if ( !ValidateLogicalRecord( candidate, &validationReason ) ) {
		result.reason = validationReason;
		return result;
	}
	const uint64_t storedContentDigest = candidate.contentDigest;
	const uint64_t decodedContentDigest = legacyV2 ?
		ComputeLegacyV2ContentDigest( candidate ) :
		( previousV3 ? ComputePreviousV3ContentDigest( candidate ) :
			MPMatchSeriesRecoveryComputeContentDigest( candidate ) );
	if ( storedContentDigest == 0 ||
		storedContentDigest != decodedContentDigest ) {
		result.reason = MP_SERIES_RECOVERY_REASON_DIGEST_MISMATCH;
		return result;
	}
	// Normalize an authenticated older record to the current logical digest.
	// Re-persisting it produces a canonical v4 checkpoint; a v2 series-only
	// record remains diagnosis-only until a real report draft is supplied.
	candidate.contentDigest = MPMatchSeriesRecoveryComputeContentDigest( candidate );
	if ( candidate.contentDigest == 0 ) {
		result.reason = MP_SERIES_RECOVERY_REASON_DIGEST_MISMATCH;
		return result;
	}
	result.contentDigest = candidate.contentDigest;
	output = candidate;
	result.reason = MP_SERIES_RECOVERY_REASON_NONE;
	result.bytes = bytes;
	result.requiredCapacity = bytes;
	return result;
}

bool MPMatchSeriesRecoveryBuildPaths( const mpSeriesRecoveryRecord &record,
		mpSeriesRecoveryPaths &paths, mpSeriesRecoveryReason_t *reason ) {
	SetReason( reason, MP_SERIES_RECOVERY_REASON_NONE );
	if ( !MPMatchSeriesRecoveryValidate( record, reason ) ) {
		return false;
	}
	mpSeriesRecoveryPaths candidate;
	candidate.Clear();
	if ( !MPMatchSeriesRecoveryBuildFinalQPath( record.seriesId,
			candidate.finalQPath, static_cast<int>( sizeof( candidate.finalQPath ) ),
			reason ) ) {
		return false;
	}
	mpRecoveryPathBuilder temporaryPath( candidate.temporaryQPath,
		static_cast<int>( sizeof( candidate.temporaryQPath ) ) );
	temporaryPath.PutLiteral( candidate.finalQPath );
	temporaryPath.PutLiteral( ".pending-" );
	temporaryPath.PutUnsigned64( record.series.revision );
	if ( !temporaryPath.Succeeded() ||
		!MPMatchSeriesRecoveryIsPromotionPair( candidate.temporaryQPath,
			candidate.finalQPath ) ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_PATH_TOO_LONG );
		return false;
	}
	paths = candidate;
	return true;
}

bool MPMatchSeriesRecoveryBuildFinalQPath( uint64_t seriesId,
		char *destination, int capacity, mpSeriesRecoveryReason_t *reason ) {
	SetReason( reason, MP_SERIES_RECOVERY_REASON_NONE );
	if ( seriesId == 0 || destination == NULL || capacity < 1 ) {
		SetReason( reason, seriesId == 0 ?
			MP_SERIES_RECOVERY_REASON_INVALID_IDENTITY :
			MP_SERIES_RECOVERY_REASON_INVALID_ARGUMENT );
		return false;
	}
	char candidate[ MP_SERIES_RECOVERY_QPATH_BYTES + 1 ];
	memset( candidate, 0, sizeof( candidate ) );
	mpRecoveryPathBuilder builder( candidate,
		static_cast<int>( sizeof( candidate ) ) );
	builder.PutLiteral( MP_SERIES_RECOVERY_PATH_PREFIX );
	builder.PutUnsigned64( seriesId );
	builder.PutLiteral( MP_SERIES_RECOVERY_PATH_SUFFIX );
	int length = 0;
	if ( !builder.Succeeded() || !GetBoundedPathLength( candidate, length ) ||
		!MPMatchSeriesRecoveryIsFinalQPath( candidate ) || length >= capacity ) {
		SetReason( reason, MP_SERIES_RECOVERY_REASON_PATH_TOO_LONG );
		return false;
	}
	memcpy( destination, candidate, length + 1 );
	return true;
}

bool MPMatchSeriesRecoveryIsFinalQPath( const char *finalQPath ) {
	return IsCanonicalRecoveryQPath( finalQPath, false );
}

bool MPMatchSeriesRecoveryIsTemporaryQPath( const char *temporaryQPath ) {
	return IsCanonicalRecoveryQPath( temporaryQPath, true );
}

bool MPMatchSeriesRecoveryIsPromotionPair( const char *temporaryQPath,
		const char *finalQPath ) {
	if ( !MPMatchSeriesRecoveryIsTemporaryQPath( temporaryQPath ) ||
		!MPMatchSeriesRecoveryIsFinalQPath( finalQPath ) ) {
		return false;
	}
	int finalLength = 0;
	int temporaryLength = 0;
	if ( !GetBoundedPathLength( finalQPath, finalLength ) ||
		!GetBoundedPathLength( temporaryQPath, temporaryLength ) ||
		temporaryLength <= finalLength ) {
		return false;
	}
	for ( int index = 0; index < finalLength; ++index ) {
		if ( temporaryQPath[ index ] != finalQPath[ index ] ) {
			return false;
		}
	}
	return temporaryQPath[ finalLength ] == '.';
}

mpSeriesRecoveryStorageResult MPMatchSeriesRecoveryPersist(
		const mpSeriesRecoveryRecord &record,
		mpMatchSeriesRecoveryWriter &writer,
		mpSeriesRecoveryWorkspace &workspace ) {
	mpSeriesRecoveryStorageResult result;
	result.Clear();
	if ( !MPMatchSeriesRecoveryBuildPaths( record, result.paths, &result.reason ) ) {
		return result;
	}
	result.code = MP_SERIES_RECOVERY_STORAGE_FAILED;
	const mpSeriesRecoveryCodecResult encoded = MPMatchSeriesRecoveryEncode(
		record, workspace.bytes, static_cast<int>( sizeof( workspace.bytes ) ) );
	if ( !encoded.Succeeded() ) {
		result.reason = encoded.reason;
		return result;
	}
	result.serializedBytes = encoded.bytes;
	result.contentDigest = encoded.contentDigest;
	result.checksum = encoded.checksum;
	result.backendBytes = writer.WriteTemp( result.paths.temporaryQPath,
		workspace.bytes, result.serializedBytes );
	if ( result.backendBytes != result.serializedBytes ) {
		result.reason = result.backendBytes < 0 ?
			MP_SERIES_RECOVERY_REASON_TEMP_WRITE_FAILED :
			MP_SERIES_RECOVERY_REASON_TEMP_WRITE_PARTIAL;
		RecordCleanup( writer, result );
		return result;
	}
	if ( !writer.Promote( result.paths.temporaryQPath, result.paths.finalQPath ) ) {
		result.reason = MP_SERIES_RECOVERY_REASON_PROMOTION_FAILED;
		RecordCleanup( writer, result );
		return result;
	}
	result.code = MP_SERIES_RECOVERY_STORAGE_STORED;
	result.reason = MP_SERIES_RECOVERY_REASON_NONE;
	return result;
}
