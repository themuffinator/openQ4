//----------------------------------------------------------------
// MatchRules.h
//
// Typed, transactional competitive match rules.  This layer deliberately
// owns no cvars, console commands, GUI state or filesystem access: adapters
// translate those inputs into a draft and publish a committed snapshot.
//----------------------------------------------------------------

#ifndef __MP_MATCH_RULES_H__
#define __MP_MATCH_RULES_H__

#include <stdint.h>
#include <stddef.h>

#include "../GameTypeIds.h"

class idStr;

static const uint32_t MP_MATCH_RULES_SCHEMA_VERSION = 2;

/*
===============================================================================

	Stable rule schema

	Field ids are serialized into the canonical representation and are therefore
	append-only.  All values use a fixed signed-integer representation; the
	descriptor type determines which typed setter may write a field.

===============================================================================
*/

typedef enum {
	MP_RULE_GAME_TYPE = 0,
	MP_RULE_MANAGED_MATCH,
	MP_RULE_WARMUP_ENABLED,
	MP_RULE_READINESS_POLICY,
	MP_RULE_READY_THRESHOLD_BASIS_POINTS,
	MP_RULE_BOTS_CAN_READY,
	MP_RULE_MIN_ACTIVE_HUMANS,
	MP_RULE_MIN_TEAM_SIZE,
	MP_RULE_REQUIRE_BOTH_TEAMS,
	MP_RULE_ROSTER_SIZE_PER_TEAM,
	MP_RULE_COUNTDOWN_SECONDS,
	MP_RULE_TIME_LIMIT_MINUTES,
	MP_RULE_FRAG_LIMIT,
	MP_RULE_CAPTURE_LIMIT,
	MP_RULE_CONTROL_TIME_SECONDS,
	MP_RULE_ROUND_LIMIT,
	MP_RULE_ROUND_TIME_LIMIT_SECONDS,
	MP_RULE_ROUND_COUNTDOWN_SECONDS,
	MP_RULE_ROUND_REVIEW_SECONDS,
	MP_RULE_MERCY_LIMIT,
	MP_RULE_OVERTIME_POLICY,
	MP_RULE_OVERTIME_PERIOD_SECONDS,
	MP_RULE_OVERTIME_MAX_PERIODS,
	MP_RULE_SUDDEN_DEATH_RESPAWN_DELAY,
	MP_RULE_SUDDEN_DEATH_RESPAWN_INCREASE,
	MP_RULE_SUDDEN_DEATH_RESPAWN_MAX,
	MP_RULE_TEAM_DAMAGE,
	MP_RULE_FORFEIT_ON_EMPTY_TEAM,
	MP_RULE_BUYING_ENABLED,
	MP_RULE_TEAM_TIMEOUT_COUNT,
	MP_RULE_TEAM_TIMEOUT_SECONDS,
	MP_RULE_TIMEOUT_REQUEST_WINDOW,
	MP_RULE_TIMEOUT_RESUME_POLICY,
	MP_RULE_MIN_ACTIVE_PLAYERS,
	MP_RULE_FIELD_COUNT
} mpRuleFieldId_t;

typedef enum {
	MP_RULE_TYPE_BOOL = 0,
	MP_RULE_TYPE_INTEGER,
	MP_RULE_TYPE_ENUM
} mpRuleFieldType_t;

// Once a map snapshot has frozen, most changes may be staged atomically for
// the next warmup.  A field marked REJECT requires a new map/session adapter.
typedef enum {
	MP_RULE_FROZEN_STAGE = 0,
	MP_RULE_FROZEN_REJECT
} mpRuleFrozenMutation_t;

typedef enum {
	MP_READY_DISABLED = 0,
	MP_READY_INDIVIDUAL,
	MP_READY_TEAM,
	MP_READY_INDIVIDUAL_AND_TEAM
} mpReadinessPolicy_t;

typedef enum {
	MP_OVERTIME_SUDDEN_DEATH = 0,
	MP_OVERTIME_TIMED_PERIODS
} mpOvertimePolicy_t;

typedef enum {
	MP_TIMEOUT_DURING_LIVE_PLAY = 0,
	MP_TIMEOUT_DURING_COUNTDOWN_OR_LIVE
} mpTimeoutRequestWindow_t;

typedef enum {
	MP_TIMEOUT_RESUME_OWNER_OR_REFEREE = 0,
	MP_TIMEOUT_RESUME_BOTH_SIDES_OR_REFEREE,
	MP_TIMEOUT_RESUME_REFEREE_ONLY
} mpTimeoutResumePolicy_t;

typedef struct mpRuleEnumValueDescriptor_s {
	int			value;
	const char *key;
	const char *localizationId;
} mpRuleEnumValueDescriptor_t;

class mpMatchRulesDraft;
struct mpMatchRulesValidationContext_s;
struct mpRuleValidationFailure_s;

typedef bool ( *mpRuleValidationCallback_t )(
	const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_s &context,
	mpRuleValidationFailure_s &failure );

typedef struct mpRuleFieldDescriptor_s {
	mpRuleFieldId_t				id;
	const char *					key;
	mpRuleFieldType_t				type;
	int							minimumValue;
	int							maximumValue;
	int							defaultValue;
	const mpRuleEnumValueDescriptor_t *enumValues;
	int							numEnumValues;
	uint32_t					applicableGameTypes;
	mpRuleFrozenMutation_t		frozenMutation;
	const char *					nameLocalizationId;
	const char *					descriptionLocalizationId;
	mpRuleValidationCallback_t	validationCallback;
} mpRuleFieldDescriptor_t;

/*
===============================================================================

	Profiles

	A profile is typed data, not executable configuration.  The casual profile
	preserves current openQ4 defaults.  Each supported mode also has exactly one
	recommended managed profile through MPRecommendedMatchProfileForGameType().

===============================================================================
*/

typedef enum {
	MP_MATCH_PROFILE_CASUAL = 0,
	MP_MATCH_PROFILE_COMPETITIVE_DM,
	MP_MATCH_PROFILE_COMPETITIVE_TOURNEY,
	MP_MATCH_PROFILE_COMPETITIVE_DUEL,
	MP_MATCH_PROFILE_COMPETITIVE_TDM,
	MP_MATCH_PROFILE_COMPETITIVE_CTF,
	MP_MATCH_PROFILE_COMPETITIVE_DEADZONE,
	MP_MATCH_PROFILE_COMPETITIVE_ROUND,
	MP_MATCH_PROFILE_COUNT,
	MP_MATCH_PROFILE_CUSTOM = -1
} mpMatchProfileId_t;

typedef struct mpMatchProfileDescriptor_s {
	mpMatchProfileId_t	id;
	const char *		key;
	const char *		nameLocalizationId;
	const char *		descriptionLocalizationId;
	uint32_t		applicableGameTypes;
	bool			managed;
} mpMatchProfileDescriptor_t;

/*
===============================================================================

	Validation and transaction results

	Reasons are machine values.  Presentation maps them to localized text and
	may use field/minimum/maximum/actual as typed substitution parameters.

===============================================================================
*/

typedef enum {
	MP_RULE_VALID = 0,
	MP_RULE_ERROR_DESCRIPTOR_TABLE,
	MP_RULE_ERROR_UNKNOWN_FIELD,
	MP_RULE_ERROR_WRONG_FIELD_TYPE,
	MP_RULE_ERROR_VALUE_OUT_OF_RANGE,
	MP_RULE_ERROR_UNKNOWN_ENUM_VALUE,
	MP_RULE_ERROR_EMPTY_VALUE,
	MP_RULE_ERROR_MALFORMED_VALUE,
	MP_RULE_ERROR_UNKNOWN_PROFILE,
	MP_RULE_ERROR_PROFILE_MODE_MISMATCH,
	MP_RULE_ERROR_MODE_UNAVAILABLE,
	MP_RULE_ERROR_MAP_CHECK_MISMATCH,
	MP_RULE_ERROR_MAP_UNSUPPORTED,
	MP_RULE_ERROR_INVALID_SERVER_BOUNDS,
	MP_RULE_ERROR_SERVER_CAPACITY,
	MP_RULE_ERROR_TEAM_CAPACITY,
	MP_RULE_ERROR_ROSTER_POLICY,
	MP_RULE_ERROR_READINESS_POLICY,
	MP_RULE_ERROR_WIN_CONDITION,
	MP_RULE_ERROR_OVERTIME_POLICY,
	MP_RULE_ERROR_TIMEOUT_POLICY,
	MP_RULE_ERROR_MODE_POLICY,
	MP_RULE_ERROR_FROZEN_FIELD,
	MP_RULE_ERROR_REVISION_EXHAUSTED,
	MP_RULE_ERROR_NO_STAGED_SNAPSHOT
} mpRuleValidationReason_t;

typedef struct mpRuleValidationFailure_s {
	mpRuleValidationReason_t	reason;
	mpRuleFieldId_t			field;
	int						actual;
	int						minimum;
	int						maximum;

	mpRuleValidationFailure_s( void );
	void Clear( void );
} mpRuleValidationFailure_t;

typedef struct mpMatchRulesValidationContext_s {
	int		maxClients;
	int		maxTeamSize;
	int		maxRosterSizePerTeam;
	int		maxCountdownSeconds;
	int		maxTimeoutCountPerTeam;
	int		maxTimeoutSeconds;
	int		maxOvertimeSeconds;
	int		maxOvertimePeriods;

	// Bind map compatibility to the exact mode that was checked.  When
	// requireMapSupport is false, profile preparation can occur before a map is
	// selected without pretending compatibility was verified.
	bool		requireMapSupport;
	int		mapSupportCheckedGameType;
	bool		mapSupportsCheckedGameType;

	mpMatchRulesValidationContext_s( void );
} mpMatchRulesValidationContext_t;

typedef enum {
	MP_RULES_OPEN_FOR_COMMIT = 0,
	MP_RULES_FROZEN_FOR_MAP
} mpRuleCommitBoundary_t;

typedef enum {
	MP_RULE_COMMIT_REJECTED = 0,
	MP_RULE_COMMIT_UNCHANGED,
	MP_RULE_COMMIT_APPLIED,
	MP_RULE_COMMIT_STAGED
} mpRuleCommitDisposition_t;

typedef struct mpRuleCommitResult_s {
	mpRuleCommitDisposition_t	disposition;
	mpRuleValidationFailure_t	failure;
	uint32_t					committedRevision;
	uint64_t					committedDigest;
	uint64_t					candidateDigest;

	mpRuleCommitResult_s( void );
	bool Succeeded( void ) const;
} mpRuleCommitResult_t;

/*
===============================================================================

	Draft and immutable snapshot values

===============================================================================
*/

class mpMatchRulesDraft {
public:
	mpMatchRulesDraft( void );

	bool SetBool( mpRuleFieldId_t field, bool value, mpRuleValidationFailure_t &failure );
	bool SetInteger( mpRuleFieldId_t field, int value, mpRuleValidationFailure_t &failure );
	bool SetEnum( mpRuleFieldId_t field, int value, mpRuleValidationFailure_t &failure );
	bool SetParsedValue( mpRuleFieldId_t field, const char *text, mpRuleValidationFailure_t &failure );

	int GetInteger( mpRuleFieldId_t field ) const;
	bool GetBool( mpRuleFieldId_t field ) const;

	mpMatchProfileId_t SourceProfile( void ) const;
	bool IsCustomized( void ) const;

private:
	friend class mpCompetitiveRules;

	bool SetTypedValue( mpRuleFieldId_t field, mpRuleFieldType_t expectedType, int value,
		mpRuleValidationFailure_t &failure, bool markCustomized );
	void SetRawProfileValue( mpRuleFieldId_t field, int value );

	int				values[ MP_RULE_FIELD_COUNT ];
	mpMatchProfileId_t	sourceProfile;
	bool				customized;
};

class mpMatchRulesSnapshot {
public:
	mpMatchRulesSnapshot( void );

	int GetInteger( mpRuleFieldId_t field ) const;
	bool GetBool( mpRuleFieldId_t field ) const;

	uint32_t SchemaVersion( void ) const;
	uint32_t Revision( void ) const;
	uint64_t Digest( void ) const;
	void DigestString( idStr &out ) const;
	void BuildCanonicalText( idStr &out ) const;

	mpMatchProfileId_t SourceProfile( void ) const;
	bool IsCustomized( void ) const;
	bool SameRuleValues( const mpMatchRulesSnapshot &other ) const;

private:
	friend class mpCompetitiveRules;

	void AssignFromDraft( const mpMatchRulesDraft &draft, uint32_t newRevision );
	void RebuildDigest( void );

	int				values[ MP_RULE_FIELD_COUNT ];
	uint32_t		schemaVersion;
	uint32_t		revision;
	uint64_t		digest;
	mpMatchProfileId_t	sourceProfile;
	bool				customized;
};

/*
===============================================================================

	mpCompetitiveRules

	The sole mutation service for committed and staged rule snapshots.  Every
	commit constructs and validates a candidate before replacing any state.

===============================================================================
*/

class mpCompetitiveRules {
public:
	mpCompetitiveRules( void );

	const mpMatchRulesSnapshot &Committed( void ) const;
	mpMatchRulesDraft BeginDraftFromCommitted( void ) const;
	mpMatchRulesDraft BeginDraftForNextWarmup( void ) const;
	bool BeginDraftFromProfile( mpMatchProfileId_t profile, int gameType,
		mpMatchRulesDraft &draft, mpRuleValidationFailure_t &failure ) const;

	mpRuleCommitResult_t Commit( const mpMatchRulesDraft &draft,
		const mpMatchRulesValidationContext_t &context, mpRuleCommitBoundary_t boundary );
	mpRuleCommitResult_t ApplyStagedAtWarmup( const mpMatchRulesValidationContext_t &context );

	bool HasStagedSnapshot( void ) const;
	const mpMatchRulesSnapshot *StagedSnapshot( void ) const;
	bool DiscardStagedSnapshot( void );

	static bool ValidateDraft( const mpMatchRulesDraft &draft,
		const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure );

private:
	static bool BuildProfileDraft( mpMatchProfileId_t profile, int gameType,
		mpMatchRulesDraft &draft, mpRuleValidationFailure_t &failure );
	static bool FieldChanged( const mpMatchRulesSnapshot &snapshot,
		const mpMatchRulesDraft &draft, mpRuleFieldId_t field );

	mpMatchRulesSnapshot	committed;
	mpMatchRulesSnapshot	staged;
	bool					hasStaged;
};

// Schema/profile discovery and startup/test invariants.
int MPMatchRuleFieldCount( void );
const mpRuleFieldDescriptor_t *MPMatchRuleField( int field );
const mpRuleFieldDescriptor_t *MPMatchRuleFieldByKey( const char *key );
int MPMatchProfileCount( void );
const mpMatchProfileDescriptor_t *MPMatchProfile( int profile );
const mpMatchProfileDescriptor_t *MPMatchProfileByKey( const char *key );
mpMatchProfileId_t MPRecommendedMatchProfileForGameType( int gameType );
bool MPValidateMatchRulesDescriptorTable( mpRuleValidationFailure_t &failure );
bool MPValidateBuiltInMatchProfiles( mpRuleValidationFailure_t &failure );

// Strict bounded parsers.  They consume the entire input and never dispatch
// console text or read a cvar.  Enum parsing accepts descriptor keys (and a
// strict decimal value for compatibility adapters).
bool MPParseBoundedRuleInteger( const char *text, int minimum, int maximum, int &value );
bool MPParseMatchRuleValue( mpRuleFieldId_t field, const char *text, int &value,
	mpRuleValidationFailure_t &failure );

#endif // __MP_MATCH_RULES_H__
