//----------------------------------------------------------------
// MatchRules.cpp
//
// Side-effect-free competitive match rule schema and transaction service.
//----------------------------------------------------------------

#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "MatchRules.h"
#include "../GameTypes.h"

namespace {

#define MP_RULE_ARRAY_COUNT( value ) ( static_cast< int >( sizeof( value ) / sizeof( ( value )[ 0 ] ) ) )
#define MP_RULE_GAME_BIT( value ) ( static_cast< uint32_t >( 1u ) << static_cast< uint32_t >( value ) )

static_assert( NUM_GAME_TYPES <= 32, "match-rule mode masks require gameType_t to fit in 32 bits" );
static_assert( MP_RULE_FIELD_COUNT < 256, "match-rule field ids are intended to remain byte-sized on the wire" );
static_assert( sizeof( int ) == 4, "match-rule canonical integers require a 32-bit int" );

static const int MP_RULE_INT_MAX = 2147483647;
static const int MP_RULE_INT_MIN = ( -2147483647 - 1 );
static const uint32_t MP_RULE_REVISION_MAX = ~static_cast< uint32_t >( 0 );

static const uint32_t MP_RULE_MODES_FRAG =
	MP_RULE_GAME_BIT( GAME_DM ) |
	MP_RULE_GAME_BIT( GAME_TOURNEY ) |
	MP_RULE_GAME_BIT( GAME_TDM ) |
	MP_RULE_GAME_BIT( GAME_DUEL );

static const uint32_t MP_RULE_MODES_CAPTURE =
	MP_RULE_GAME_BIT( GAME_CTF ) |
	MP_RULE_GAME_BIT( GAME_1F_CTF ) |
	MP_RULE_GAME_BIT( GAME_ARENA_CTF ) |
	MP_RULE_GAME_BIT( GAME_ARENA_1F_CTF );

static const uint32_t MP_RULE_MODES_ROUND =
	MP_RULE_GAME_BIT( GAME_CA ) |
	MP_RULE_GAME_BIT( GAME_FREEZETAG ) |
	MP_RULE_GAME_BIT( GAME_REDROVER );

static const uint32_t MP_RULE_MODES_TEAM =
	MP_RULE_GAME_BIT( GAME_TDM ) |
	MP_RULE_MODES_CAPTURE |
	MP_RULE_GAME_BIT( GAME_DEADZONE ) |
	MP_RULE_MODES_ROUND;

// The modes whose game type allows buying (GTF_BUYING): retail's, the flag
// modes included. A casual CTF server with buying on imported a rule this
// mask refused, and its match never left warmup.
static const uint32_t MP_RULE_MODES_BUYING =
	MP_RULE_GAME_BIT( GAME_DM ) |
	MP_RULE_GAME_BIT( GAME_TDM ) |
	MP_RULE_MODES_CAPTURE |
	MP_RULE_GAME_BIT( GAME_DEADZONE );

static const uint32_t MP_RULE_MODES_TIMEOUT =
	MP_RULE_MODES_TEAM |
	MP_RULE_GAME_BIT( GAME_DUEL );

static const uint32_t MP_RULE_MODES_ALL_PUBLIC =
	MP_RULE_MODES_FRAG |
	MP_RULE_MODES_CAPTURE |
	MP_RULE_GAME_BIT( GAME_DEADZONE ) |
	MP_RULE_MODES_ROUND;

static bool ValidateAnyField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure );
static bool ValidateGameTypeField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure );
static bool ValidateMinActiveHumansField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure );
static bool ValidateMinActivePlayersField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure );
static bool ValidateMinTeamSizeField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure );
static bool ValidateRosterSizeField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure );
static bool ValidateCountdownField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure );
static bool ValidateOvertimeSecondsField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure );
static bool ValidateOvertimePeriodsField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure );
static bool ValidateTimeoutCountField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure );
static bool ValidateTimeoutSecondsField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure );

static const mpRuleEnumValueDescriptor_t gameTypeValues[] = {
	{ GAME_DM,             "DM",                    "#str_107679" },
	{ GAME_TOURNEY,        "Tourney",               "#str_107676" },
	{ GAME_TDM,            "Team DM",               "#str_107677" },
	{ GAME_CTF,            "CTF",                   "#str_107678" },
	{ GAME_1F_CTF,         "One Flag CTF",          "#str_107680" },
	{ GAME_ARENA_CTF,      "Arena CTF",             "#str_107681" },
	{ GAME_ARENA_1F_CTF,   "Arena One Flag CTF",    "#str_107682" },
	{ GAME_DEADZONE,       "DeadZone",              "#str_122001" },
	{ GAME_DUEL,           "Duel",                  "#str_41300" },
	{ GAME_CA,             "Clan Arena",            "#str_41301" },
	{ GAME_FREEZETAG,      "Freeze Tag",            "#str_41302" },
	{ GAME_REDROVER,       "Red Rover",             "#str_41303" }
};

static const mpRuleEnumValueDescriptor_t readinessValues[] = {
	{ MP_READY_DISABLED,                "disabled",             "#str_41666" },
	{ MP_READY_INDIVIDUAL,              "individual",           "#str_41667" },
	{ MP_READY_TEAM,                    "team",                 "#str_41668" },
	{ MP_READY_INDIVIDUAL_AND_TEAM,     "individual_and_team",  "#str_41669" }
};

static const mpRuleEnumValueDescriptor_t overtimeValues[] = {
	{ MP_OVERTIME_SUDDEN_DEATH,     "sudden_death",    "#str_41670" },
	{ MP_OVERTIME_TIMED_PERIODS,    "timed_periods",   "#str_41671" }
};

static const mpRuleEnumValueDescriptor_t timeoutWindowValues[] = {
	{ MP_TIMEOUT_DURING_LIVE_PLAY,             "live_play",             "#str_41672" },
	{ MP_TIMEOUT_DURING_COUNTDOWN_OR_LIVE,     "countdown_or_live",      "#str_41673" }
};

static const mpRuleEnumValueDescriptor_t timeoutResumeValues[] = {
	{ MP_TIMEOUT_RESUME_OWNER_OR_REFEREE,          "owner_or_referee",          "#str_41674" },
	{ MP_TIMEOUT_RESUME_BOTH_SIDES_OR_REFEREE,     "both_sides_or_referee",     "#str_41675" },
	{ MP_TIMEOUT_RESUME_REFEREE_ONLY,              "referee_only",              "#str_41676" }
};

#define MP_RULE_FIELD( id, key, type, minimum, maximum, defaultValue, enumTable, modeMask, frozen, nameId, descriptionId, callback ) \
	{ id, key, type, minimum, maximum, defaultValue, enumTable, \
	  ( enumTable != NULL ? static_cast< int >( sizeof( enumTable ) / sizeof( mpRuleEnumValueDescriptor_t ) ) : 0 ), \
	  modeMask, frozen, nameId, descriptionId, callback }

static const mpRuleFieldDescriptor_t ruleFields[] = {
	MP_RULE_FIELD( MP_RULE_GAME_TYPE, "game_type", MP_RULE_TYPE_ENUM,
		GAME_DM, GAME_REDROVER, GAME_DM, gameTypeValues, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_REJECT,
		"#str_41600", "#str_41601", ValidateGameTypeField ),
	MP_RULE_FIELD( MP_RULE_MANAGED_MATCH, "managed_match", MP_RULE_TYPE_BOOL,
		0, 1, 0, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41602", "#str_41603", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_WARMUP_ENABLED, "warmup_enabled", MP_RULE_TYPE_BOOL,
		0, 1, 1, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41604", "#str_41605", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_READINESS_POLICY, "readiness_policy", MP_RULE_TYPE_ENUM,
		MP_READY_DISABLED, MP_READY_INDIVIDUAL_AND_TEAM, MP_READY_INDIVIDUAL, readinessValues,
		MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41606", "#str_41607", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_READY_THRESHOLD_BASIS_POINTS, "ready_threshold_basis_points", MP_RULE_TYPE_INTEGER,
		0, 10000, 5100, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41608", "#str_41609", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_BOTS_CAN_READY, "bots_can_ready", MP_RULE_TYPE_BOOL,
		0, 1, 1, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41610", "#str_41611", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_MIN_ACTIVE_HUMANS, "min_active_humans", MP_RULE_TYPE_INTEGER,
		1, 64, 1, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41612", "#str_41613", ValidateMinActiveHumansField ),
	MP_RULE_FIELD( MP_RULE_MIN_TEAM_SIZE, "min_team_size", MP_RULE_TYPE_INTEGER,
		1, 32, 1, NULL, MP_RULE_MODES_TEAM, MP_RULE_FROZEN_STAGE,
		"#str_41614", "#str_41615", ValidateMinTeamSizeField ),
	MP_RULE_FIELD( MP_RULE_REQUIRE_BOTH_TEAMS, "require_both_teams", MP_RULE_TYPE_BOOL,
		0, 1, 1, NULL, MP_RULE_MODES_TEAM, MP_RULE_FROZEN_STAGE,
		"#str_41616", "#str_41617", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_ROSTER_SIZE_PER_TEAM, "roster_size_per_team", MP_RULE_TYPE_INTEGER,
		0, 32, 0, NULL, MP_RULE_MODES_TEAM, MP_RULE_FROZEN_STAGE,
		"#str_41618", "#str_41619", ValidateRosterSizeField ),
	MP_RULE_FIELD( MP_RULE_COUNTDOWN_SECONDS, "countdown_seconds", MP_RULE_TYPE_INTEGER,
		4, 3600, 10, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41620", "#str_41621", ValidateCountdownField ),
	MP_RULE_FIELD( MP_RULE_TIME_LIMIT_MINUTES, "time_limit_minutes", MP_RULE_TYPE_INTEGER,
		0, 60, 10, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41622", "#str_41623", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_FRAG_LIMIT, "frag_limit", MP_RULE_TYPE_INTEGER,
		0, 999, 10, NULL, MP_RULE_MODES_FRAG, MP_RULE_FROZEN_STAGE,
		"#str_41624", "#str_41625", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_CAPTURE_LIMIT, "capture_limit", MP_RULE_TYPE_INTEGER,
		1, 999, 5, NULL, MP_RULE_MODES_CAPTURE, MP_RULE_FROZEN_STAGE,
		"#str_41626", "#str_41627", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_CONTROL_TIME_SECONDS, "control_time_seconds", MP_RULE_TYPE_INTEGER,
		1, 999, 120, NULL, MP_RULE_GAME_BIT( GAME_DEADZONE ), MP_RULE_FROZEN_STAGE,
		"#str_41628", "#str_41629", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_ROUND_LIMIT, "round_limit", MP_RULE_TYPE_INTEGER,
		0, 99, 8, NULL, MP_RULE_MODES_ROUND, MP_RULE_FROZEN_STAGE,
		"#str_41630", "#str_41631", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_ROUND_TIME_LIMIT_SECONDS, "round_time_limit_seconds", MP_RULE_TYPE_INTEGER,
		0, 3600, 180, NULL, MP_RULE_MODES_ROUND, MP_RULE_FROZEN_STAGE,
		"#str_41632", "#str_41633", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_ROUND_COUNTDOWN_SECONDS, "round_countdown_seconds", MP_RULE_TYPE_INTEGER,
		0, 60, 10, NULL, MP_RULE_MODES_ROUND, MP_RULE_FROZEN_STAGE,
		"#str_41634", "#str_41635", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_ROUND_REVIEW_SECONDS, "round_review_seconds", MP_RULE_TYPE_INTEGER,
		1, 30, 4, NULL, MP_RULE_MODES_ROUND, MP_RULE_FROZEN_STAGE,
		"#str_41636", "#str_41637", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_MERCY_LIMIT, "mercy_limit", MP_RULE_TYPE_INTEGER,
		0, 999, 0, NULL, MP_RULE_MODES_TEAM, MP_RULE_FROZEN_STAGE,
		"#str_41638", "#str_41639", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_OVERTIME_POLICY, "overtime_policy", MP_RULE_TYPE_ENUM,
		MP_OVERTIME_SUDDEN_DEATH, MP_OVERTIME_TIMED_PERIODS, MP_OVERTIME_TIMED_PERIODS, overtimeValues,
		MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41640", "#str_41641", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_OVERTIME_PERIOD_SECONDS, "overtime_period_seconds", MP_RULE_TYPE_INTEGER,
		0, 3600, 120, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41642", "#str_41643", ValidateOvertimeSecondsField ),
	MP_RULE_FIELD( MP_RULE_OVERTIME_MAX_PERIODS, "overtime_max_periods", MP_RULE_TYPE_INTEGER,
		0, 64, 0, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41644", "#str_41645", ValidateOvertimePeriodsField ),
	MP_RULE_FIELD( MP_RULE_SUDDEN_DEATH_RESPAWN_DELAY, "sudden_death_respawn_delay", MP_RULE_TYPE_INTEGER,
		0, 60, 3, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41646", "#str_41647", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_SUDDEN_DEATH_RESPAWN_INCREASE, "sudden_death_respawn_increase", MP_RULE_TYPE_INTEGER,
		0, 60, 1, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41648", "#str_41649", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_SUDDEN_DEATH_RESPAWN_MAX, "sudden_death_respawn_max", MP_RULE_TYPE_INTEGER,
		0, 60, 10, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_41650", "#str_41651", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_TEAM_DAMAGE, "team_damage", MP_RULE_TYPE_BOOL,
		0, 1, 0, NULL, MP_RULE_MODES_TEAM, MP_RULE_FROZEN_STAGE,
		"#str_41652", "#str_41653", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_FORFEIT_ON_EMPTY_TEAM, "forfeit_on_empty_team", MP_RULE_TYPE_BOOL,
		0, 1, 1, NULL, MP_RULE_MODES_TEAM, MP_RULE_FROZEN_STAGE,
		"#str_41654", "#str_41655", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_BUYING_ENABLED, "buying_enabled", MP_RULE_TYPE_BOOL,
		0, 1, 0, NULL, MP_RULE_MODES_BUYING, MP_RULE_FROZEN_STAGE,
		"#str_41656", "#str_41657", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_TEAM_TIMEOUT_COUNT, "team_timeout_count", MP_RULE_TYPE_INTEGER,
		0, 10, 0, NULL, MP_RULE_MODES_TIMEOUT, MP_RULE_FROZEN_STAGE,
		"#str_41658", "#str_41659", ValidateTimeoutCountField ),
	MP_RULE_FIELD( MP_RULE_TEAM_TIMEOUT_SECONDS, "team_timeout_seconds", MP_RULE_TYPE_INTEGER,
		0, 600, 0, NULL, MP_RULE_MODES_TIMEOUT, MP_RULE_FROZEN_STAGE,
		"#str_41660", "#str_41661", ValidateTimeoutSecondsField ),
	MP_RULE_FIELD( MP_RULE_TIMEOUT_REQUEST_WINDOW, "timeout_request_window", MP_RULE_TYPE_ENUM,
		MP_TIMEOUT_DURING_LIVE_PLAY, MP_TIMEOUT_DURING_COUNTDOWN_OR_LIVE, MP_TIMEOUT_DURING_LIVE_PLAY,
		timeoutWindowValues, MP_RULE_MODES_TIMEOUT, MP_RULE_FROZEN_STAGE,
		"#str_41662", "#str_41663", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_TIMEOUT_RESUME_POLICY, "timeout_resume_policy", MP_RULE_TYPE_ENUM,
		MP_TIMEOUT_RESUME_OWNER_OR_REFEREE, MP_TIMEOUT_RESUME_REFEREE_ONLY,
		MP_TIMEOUT_RESUME_OWNER_OR_REFEREE, timeoutResumeValues, MP_RULE_MODES_TIMEOUT, MP_RULE_FROZEN_STAGE,
		"#str_41664", "#str_41665", ValidateAnyField ),
	MP_RULE_FIELD( MP_RULE_MIN_ACTIVE_PLAYERS, "min_active_players", MP_RULE_TYPE_INTEGER,
		0, 32, 2, NULL, MP_RULE_MODES_ALL_PUBLIC, MP_RULE_FROZEN_STAGE,
		"#str_42680", "#str_42681", ValidateMinActivePlayersField )
};

#undef MP_RULE_FIELD

static_assert( MP_RULE_ARRAY_COUNT( ruleFields ) == MP_RULE_FIELD_COUNT,
	"every stable match-rule field requires exactly one descriptor" );
static_assert( MP_RULE_ARRAY_COUNT( gameTypeValues ) == 12,
	"the match-rule game type enum must list exactly the supported public modes" );

typedef struct mpRuleProfileOverride_s {
	mpRuleFieldId_t	field;
	int				value;
} mpRuleProfileOverride_t;

static const mpRuleProfileOverride_t competitiveDMOverrides[] = {
	{ MP_RULE_MANAGED_MATCH, 1 },
	{ MP_RULE_READY_THRESHOLD_BASIS_POINTS, 10000 },
	{ MP_RULE_BOTS_CAN_READY, 0 },
	{ MP_RULE_MIN_ACTIVE_HUMANS, 2 },
	{ MP_RULE_FRAG_LIMIT, 0 }
};

static const mpRuleProfileOverride_t competitiveTourneyOverrides[] = {
	{ MP_RULE_MANAGED_MATCH, 1 },
	{ MP_RULE_READY_THRESHOLD_BASIS_POINTS, 10000 },
	{ MP_RULE_BOTS_CAN_READY, 0 },
	{ MP_RULE_MIN_ACTIVE_HUMANS, 2 }
};

static const mpRuleProfileOverride_t competitiveDuelOverrides[] = {
	{ MP_RULE_MANAGED_MATCH, 1 },
	{ MP_RULE_READY_THRESHOLD_BASIS_POINTS, 10000 },
	{ MP_RULE_BOTS_CAN_READY, 0 },
	{ MP_RULE_MIN_ACTIVE_HUMANS, 2 },
	{ MP_RULE_FRAG_LIMIT, 0 },
	{ MP_RULE_TEAM_TIMEOUT_COUNT, 2 },
	{ MP_RULE_TEAM_TIMEOUT_SECONDS, 60 }
};

static const mpRuleProfileOverride_t competitiveTDMOverrides[] = {
	{ MP_RULE_MANAGED_MATCH, 1 },
	{ MP_RULE_READINESS_POLICY, MP_READY_INDIVIDUAL },
	{ MP_RULE_READY_THRESHOLD_BASIS_POINTS, 10000 },
	{ MP_RULE_BOTS_CAN_READY, 0 },
	{ MP_RULE_MIN_ACTIVE_HUMANS, 2 },
	{ MP_RULE_TIME_LIMIT_MINUTES, 15 },
	{ MP_RULE_FRAG_LIMIT, 50 },
	{ MP_RULE_TEAM_DAMAGE, 1 },
	{ MP_RULE_TEAM_TIMEOUT_COUNT, 2 },
	{ MP_RULE_TEAM_TIMEOUT_SECONDS, 60 }
};

static const mpRuleProfileOverride_t competitiveCTFOverrides[] = {
	{ MP_RULE_MANAGED_MATCH, 1 },
	{ MP_RULE_READINESS_POLICY, MP_READY_INDIVIDUAL },
	{ MP_RULE_READY_THRESHOLD_BASIS_POINTS, 10000 },
	{ MP_RULE_BOTS_CAN_READY, 0 },
	{ MP_RULE_MIN_ACTIVE_HUMANS, 2 },
	{ MP_RULE_TIME_LIMIT_MINUTES, 20 },
	{ MP_RULE_CAPTURE_LIMIT, 5 },
	{ MP_RULE_TEAM_DAMAGE, 1 },
	{ MP_RULE_TEAM_TIMEOUT_COUNT, 2 },
	{ MP_RULE_TEAM_TIMEOUT_SECONDS, 60 }
};

static const mpRuleProfileOverride_t competitiveDeadZoneOverrides[] = {
	{ MP_RULE_MANAGED_MATCH, 1 },
	{ MP_RULE_READINESS_POLICY, MP_READY_INDIVIDUAL },
	{ MP_RULE_READY_THRESHOLD_BASIS_POINTS, 10000 },
	{ MP_RULE_BOTS_CAN_READY, 0 },
	{ MP_RULE_MIN_ACTIVE_HUMANS, 2 },
	{ MP_RULE_TIME_LIMIT_MINUTES, 15 },
	{ MP_RULE_CONTROL_TIME_SECONDS, 120 },
	{ MP_RULE_TEAM_DAMAGE, 1 },
	{ MP_RULE_TEAM_TIMEOUT_COUNT, 2 },
	{ MP_RULE_TEAM_TIMEOUT_SECONDS, 60 }
};

static const mpRuleProfileOverride_t competitiveRoundOverrides[] = {
	{ MP_RULE_MANAGED_MATCH, 1 },
	{ MP_RULE_READINESS_POLICY, MP_READY_INDIVIDUAL },
	{ MP_RULE_READY_THRESHOLD_BASIS_POINTS, 10000 },
	{ MP_RULE_BOTS_CAN_READY, 0 },
	{ MP_RULE_MIN_ACTIVE_HUMANS, 2 },
	{ MP_RULE_TIME_LIMIT_MINUTES, 0 },
	{ MP_RULE_ROUND_LIMIT, 8 },
	{ MP_RULE_ROUND_TIME_LIMIT_SECONDS, 180 },
	{ MP_RULE_OVERTIME_POLICY, MP_OVERTIME_SUDDEN_DEATH },
	{ MP_RULE_OVERTIME_PERIOD_SECONDS, 0 },
	{ MP_RULE_OVERTIME_MAX_PERIODS, 0 },
	{ MP_RULE_TEAM_TIMEOUT_COUNT, 2 },
	{ MP_RULE_TEAM_TIMEOUT_SECONDS, 60 }
};

typedef struct mpRuleProfileDefinition_s {
	mpMatchProfileDescriptor_t			descriptor;
	const mpRuleProfileOverride_t *	overrides;
	int								numOverrides;
} mpRuleProfileDefinition_t;

static const mpRuleProfileDefinition_t profileDefinitions[] = {
	{ { MP_MATCH_PROFILE_CASUAL, "casual", "#str_41677", "#str_41678",
		MP_RULE_MODES_ALL_PUBLIC, false }, NULL, 0 },
	{ { MP_MATCH_PROFILE_COMPETITIVE_DM, "competitive_dm", "#str_41679", "#str_41680",
		MP_RULE_GAME_BIT( GAME_DM ), true }, competitiveDMOverrides, MP_RULE_ARRAY_COUNT( competitiveDMOverrides ) },
	{ { MP_MATCH_PROFILE_COMPETITIVE_TOURNEY, "competitive_tourney", "#str_41681", "#str_41682",
		MP_RULE_GAME_BIT( GAME_TOURNEY ), true }, competitiveTourneyOverrides, MP_RULE_ARRAY_COUNT( competitiveTourneyOverrides ) },
	{ { MP_MATCH_PROFILE_COMPETITIVE_DUEL, "competitive_duel", "#str_41683", "#str_41684",
		MP_RULE_GAME_BIT( GAME_DUEL ), true }, competitiveDuelOverrides, MP_RULE_ARRAY_COUNT( competitiveDuelOverrides ) },
	{ { MP_MATCH_PROFILE_COMPETITIVE_TDM, "competitive_tdm", "#str_41685", "#str_41686",
		MP_RULE_GAME_BIT( GAME_TDM ), true }, competitiveTDMOverrides, MP_RULE_ARRAY_COUNT( competitiveTDMOverrides ) },
	{ { MP_MATCH_PROFILE_COMPETITIVE_CTF, "competitive_ctf", "#str_41687", "#str_41688",
		MP_RULE_MODES_CAPTURE, true }, competitiveCTFOverrides, MP_RULE_ARRAY_COUNT( competitiveCTFOverrides ) },
	{ { MP_MATCH_PROFILE_COMPETITIVE_DEADZONE, "competitive_deadzone", "#str_41689", "#str_41690",
		MP_RULE_GAME_BIT( GAME_DEADZONE ), true }, competitiveDeadZoneOverrides, MP_RULE_ARRAY_COUNT( competitiveDeadZoneOverrides ) },
	{ { MP_MATCH_PROFILE_COMPETITIVE_ROUND, "competitive_round", "#str_41691", "#str_41692",
		MP_RULE_MODES_ROUND, true }, competitiveRoundOverrides, MP_RULE_ARRAY_COUNT( competitiveRoundOverrides ) }
};

static_assert( MP_RULE_ARRAY_COUNT( profileDefinitions ) == MP_MATCH_PROFILE_COUNT,
	"every built-in match profile requires exactly one definition" );

static void SetFailure( mpRuleValidationFailure_t &failure, mpRuleValidationReason_t reason,
	mpRuleFieldId_t field, int actual = 0, int minimum = 0, int maximum = 0 ) {
	failure.reason = reason;
	failure.field = field;
	failure.actual = actual;
	failure.minimum = minimum;
	failure.maximum = maximum;
}

static bool IsKnownField( int field ) {
	return field >= 0 && field < MP_RULE_FIELD_COUNT;
}

static bool IsKnownProfile( int profile ) {
	return profile >= 0 && profile < MP_MATCH_PROFILE_COUNT;
}

static bool ModeInMask( int gameType, uint32_t mask ) {
	if ( gameType < 0 || gameType >= 32 ) {
		return false;
	}
	return ( mask & MP_RULE_GAME_BIT( gameType ) ) != 0;
}

static bool IsLocalizationId( const char *value ) {
	if ( value == NULL ) {
		return false;
	}
	const char prefix[] = "#str_";
	for ( int i = 0; i < 5; i++ ) {
		if ( value[ i ] == '\0' || value[ i ] != prefix[ i ] ) {
			return false;
		}
	}
	return value[ 5 ] != '\0';
}

static bool IsMachineKey( const char *value ) {
	if ( value == NULL ) {
		return false;
	}
	for ( int i = 0; i <= 64; i++ ) {
		const unsigned char character = static_cast< unsigned char >( value[ i ] );
		if ( character == '\0' ) {
			return i > 0;
		}
		if ( i == 64 || !( ( character >= 'a' && character <= 'z' ) ||
				( character >= '0' && character <= '9' ) || character == '_' ) ) {
			return false;
		}
	}
	return false;
}

static bool EnumContainsValue( const mpRuleFieldDescriptor_t &descriptor, int value ) {
	for ( int i = 0; i < descriptor.numEnumValues; i++ ) {
		if ( descriptor.enumValues[ i ].value == value ) {
			return true;
		}
	}
	return false;
}

static int BoundedStringLength( const char *text, int maximum ) {
	if ( text == NULL || maximum < 0 ) {
		return -1;
	}
	for ( int i = 0; i <= maximum; i++ ) {
		if ( text[ i ] == '\0' ) {
			return i;
		}
	}
	return -1;
}

static bool SnapshotMatchesDraft( const mpMatchRulesSnapshot &snapshot, const mpMatchRulesDraft &draft ) {
	for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
		if ( snapshot.GetInteger( static_cast< mpRuleFieldId_t >( i ) ) !=
				draft.GetInteger( static_cast< mpRuleFieldId_t >( i ) ) ) {
			return false;
		}
	}
	return snapshot.SourceProfile() == draft.SourceProfile() &&
		snapshot.IsCustomized() == draft.IsCustomized();
}

static bool ValidateAnyField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure ) {
	(void)draft;
	(void)context;
	(void)failure;
	return true;
}

static bool ValidateGameTypeField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure ) {
	const int gameType = draft.GetInteger( MP_RULE_GAME_TYPE );
	if ( !MPGameTypeIsSelectable( gameType ) ) {
		SetFailure( failure, MP_RULE_ERROR_MODE_UNAVAILABLE, MP_RULE_GAME_TYPE, gameType );
		return false;
	}
	if ( context.requireMapSupport ) {
		if ( context.mapSupportCheckedGameType != gameType ) {
			SetFailure( failure, MP_RULE_ERROR_MAP_CHECK_MISMATCH, MP_RULE_GAME_TYPE,
				context.mapSupportCheckedGameType, gameType, gameType );
			return false;
		}
		if ( !context.mapSupportsCheckedGameType ) {
			SetFailure( failure, MP_RULE_ERROR_MAP_UNSUPPORTED, MP_RULE_GAME_TYPE, gameType );
			return false;
		}
	}
	return true;
}

static bool ValidateMinActiveHumansField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure ) {
	const int value = draft.GetInteger( MP_RULE_MIN_ACTIVE_HUMANS );
	if ( value > context.maxClients ) {
		SetFailure( failure, MP_RULE_ERROR_SERVER_CAPACITY, MP_RULE_MIN_ACTIVE_HUMANS,
			value, 1, context.maxClients );
		return false;
	}
	return true;
}

static bool ValidateMinActivePlayersField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure ) {
	const int value = draft.GetInteger( MP_RULE_MIN_ACTIVE_PLAYERS );
	const int maximum = draft.GetInteger( MP_RULE_GAME_TYPE ) == GAME_DUEL ?
		Min( 2, context.maxClients ) : context.maxClients;
	if ( value > maximum ) {
		SetFailure( failure, MP_RULE_ERROR_SERVER_CAPACITY, MP_RULE_MIN_ACTIVE_PLAYERS,
			value, 0, maximum );
		return false;
	}
	return true;
}

static bool ValidateMinTeamSizeField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure ) {
	const int gameType = draft.GetInteger( MP_RULE_GAME_TYPE );
	if ( !MPGameTypeHasAny( gameType, GTF_TEAM ) ) {
		return true;
	}
	const int value = draft.GetInteger( MP_RULE_MIN_TEAM_SIZE );
	if ( value > context.maxTeamSize ) {
		SetFailure( failure, MP_RULE_ERROR_TEAM_CAPACITY, MP_RULE_MIN_TEAM_SIZE,
			value, 1, context.maxTeamSize );
		return false;
	}
	if ( draft.GetBool( MP_RULE_REQUIRE_BOTH_TEAMS ) && value > context.maxClients / 2 ) {
		SetFailure( failure, MP_RULE_ERROR_TEAM_CAPACITY, MP_RULE_MIN_TEAM_SIZE,
			value, 1, context.maxClients / 2 );
		return false;
	}
	return true;
}

static bool ValidateRosterSizeField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure ) {
	const int value = draft.GetInteger( MP_RULE_ROSTER_SIZE_PER_TEAM );
	if ( value == 0 ) {
		return true;
	}
	if ( !draft.GetBool( MP_RULE_MANAGED_MATCH ) ||
			!MPGameTypeHasAny( draft.GetInteger( MP_RULE_GAME_TYPE ), GTF_TEAM ) ) {
		SetFailure( failure, MP_RULE_ERROR_ROSTER_POLICY, MP_RULE_ROSTER_SIZE_PER_TEAM, value );
		return false;
	}
	if ( value < draft.GetInteger( MP_RULE_MIN_TEAM_SIZE ) ||
			value > context.maxRosterSizePerTeam || value > context.maxTeamSize ||
			value > context.maxClients / 2 ) {
		SetFailure( failure, MP_RULE_ERROR_ROSTER_POLICY, MP_RULE_ROSTER_SIZE_PER_TEAM,
			value, draft.GetInteger( MP_RULE_MIN_TEAM_SIZE ),
			Min( context.maxRosterSizePerTeam, Min( context.maxTeamSize, context.maxClients / 2 ) ) );
		return false;
	}
	return true;
}

static bool ValidateCountdownField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure ) {
	const int value = draft.GetInteger( MP_RULE_COUNTDOWN_SECONDS );
	if ( value > context.maxCountdownSeconds ) {
		SetFailure( failure, MP_RULE_ERROR_VALUE_OUT_OF_RANGE, MP_RULE_COUNTDOWN_SECONDS,
			value, ruleFields[ MP_RULE_COUNTDOWN_SECONDS ].minimumValue, context.maxCountdownSeconds );
		return false;
	}
	return true;
}

static bool ValidateOvertimeSecondsField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure ) {
	const int value = draft.GetInteger( MP_RULE_OVERTIME_PERIOD_SECONDS );
	if ( value > context.maxOvertimeSeconds ) {
		SetFailure( failure, MP_RULE_ERROR_VALUE_OUT_OF_RANGE, MP_RULE_OVERTIME_PERIOD_SECONDS,
			value, 0, context.maxOvertimeSeconds );
		return false;
	}
	return true;
}

static bool ValidateOvertimePeriodsField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure ) {
	const int value = draft.GetInteger( MP_RULE_OVERTIME_MAX_PERIODS );
	if ( value > context.maxOvertimePeriods ) {
		SetFailure( failure, MP_RULE_ERROR_VALUE_OUT_OF_RANGE, MP_RULE_OVERTIME_MAX_PERIODS,
			value, 0, context.maxOvertimePeriods );
		return false;
	}
	return true;
}

static bool ValidateTimeoutCountField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure ) {
	const int value = draft.GetInteger( MP_RULE_TEAM_TIMEOUT_COUNT );
	if ( value > context.maxTimeoutCountPerTeam ) {
		SetFailure( failure, MP_RULE_ERROR_VALUE_OUT_OF_RANGE, MP_RULE_TEAM_TIMEOUT_COUNT,
			value, 0, context.maxTimeoutCountPerTeam );
		return false;
	}
	return true;
}

static bool ValidateTimeoutSecondsField( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure ) {
	const int value = draft.GetInteger( MP_RULE_TEAM_TIMEOUT_SECONDS );
	if ( value > context.maxTimeoutSeconds ) {
		SetFailure( failure, MP_RULE_ERROR_VALUE_OUT_OF_RANGE, MP_RULE_TEAM_TIMEOUT_SECONDS,
			value, 0, context.maxTimeoutSeconds );
		return false;
	}
	return true;
}

} // namespace

/*
================
mpRuleValidationFailure_t
================
*/
mpRuleValidationFailure_s::mpRuleValidationFailure_s( void ) {
	Clear();
}

void mpRuleValidationFailure_s::Clear( void ) {
	reason = MP_RULE_VALID;
	field = MP_RULE_FIELD_COUNT;
	actual = 0;
	minimum = 0;
	maximum = 0;
}

/*
================
mpMatchRulesValidationContext_t
================
*/
mpMatchRulesValidationContext_s::mpMatchRulesValidationContext_s( void ) {
	maxClients = 16;
	maxTeamSize = 16;
	maxRosterSizePerTeam = 16;
	maxCountdownSeconds = 3600;
	maxTimeoutCountPerTeam = 10;
	maxTimeoutSeconds = 600;
	maxOvertimeSeconds = 3600;
	maxOvertimePeriods = 64;
	requireMapSupport = false;
	mapSupportCheckedGameType = -1;
	mapSupportsCheckedGameType = false;
}

/*
================
mpRuleCommitResult_t
================
*/
mpRuleCommitResult_s::mpRuleCommitResult_s( void ) {
	disposition = MP_RULE_COMMIT_REJECTED;
	committedRevision = 0;
	committedDigest = 0;
	candidateDigest = 0;
}

bool mpRuleCommitResult_s::Succeeded( void ) const {
	return disposition == MP_RULE_COMMIT_UNCHANGED ||
		disposition == MP_RULE_COMMIT_APPLIED ||
		disposition == MP_RULE_COMMIT_STAGED;
}

/*
================
Schema discovery
================
*/
int MPMatchRuleFieldCount( void ) {
	return MP_RULE_FIELD_COUNT;
}

const mpRuleFieldDescriptor_t *MPMatchRuleField( int field ) {
	return IsKnownField( field ) ? &ruleFields[ field ] : NULL;
}

const mpRuleFieldDescriptor_t *MPMatchRuleFieldByKey( const char *key ) {
	if ( key == NULL || BoundedStringLength( key, 64 ) <= 0 ) {
		return NULL;
	}
	for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
		if ( idStr::Icmp( ruleFields[ i ].key, key ) == 0 ) {
			return &ruleFields[ i ];
		}
	}
	return NULL;
}

int MPMatchProfileCount( void ) {
	return MP_MATCH_PROFILE_COUNT;
}

const mpMatchProfileDescriptor_t *MPMatchProfile( int profile ) {
	return IsKnownProfile( profile ) ? &profileDefinitions[ profile ].descriptor : NULL;
}

const mpMatchProfileDescriptor_t *MPMatchProfileByKey( const char *key ) {
	if ( key == NULL || BoundedStringLength( key, 64 ) <= 0 ) {
		return NULL;
	}
	for ( int i = 0; i < MP_MATCH_PROFILE_COUNT; i++ ) {
		if ( idStr::Icmp( profileDefinitions[ i ].descriptor.key, key ) == 0 ) {
			return &profileDefinitions[ i ].descriptor;
		}
	}
	return NULL;
}

mpMatchProfileId_t MPRecommendedMatchProfileForGameType( int gameType ) {
	switch ( gameType ) {
		case GAME_DM:             return MP_MATCH_PROFILE_COMPETITIVE_DM;
		case GAME_TOURNEY:        return MP_MATCH_PROFILE_COMPETITIVE_TOURNEY;
		case GAME_DUEL:           return MP_MATCH_PROFILE_COMPETITIVE_DUEL;
		case GAME_TDM:            return MP_MATCH_PROFILE_COMPETITIVE_TDM;
		case GAME_CTF:
		case GAME_1F_CTF:
		case GAME_ARENA_CTF:
		case GAME_ARENA_1F_CTF:   return MP_MATCH_PROFILE_COMPETITIVE_CTF;
		case GAME_DEADZONE:       return MP_MATCH_PROFILE_COMPETITIVE_DEADZONE;
		case GAME_CA:
		case GAME_FREEZETAG:
		case GAME_REDROVER:       return MP_MATCH_PROFILE_COMPETITIVE_ROUND;
		default:                  return MP_MATCH_PROFILE_CUSTOM;
	}
}

/*
================
MPValidateMatchRulesDescriptorTable

Pure startup/contract validation.  The integration adapter decides how to
report a failure; this layer returns typed evidence and never logs UI prose.
================
*/
bool MPValidateMatchRulesDescriptorTable( mpRuleValidationFailure_t &failure ) {
	failure.Clear();

	uint32_t selectableMask = 0;
	for ( int gameType = 0; gameType < NUM_GAME_TYPES; gameType++ ) {
		if ( MPGameTypeIsSelectable( gameType ) ) {
			selectableMask |= MP_RULE_GAME_BIT( gameType );
		}
	}
	if ( selectableMask != MP_RULE_MODES_ALL_PUBLIC ) {
		SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, MP_RULE_GAME_TYPE,
			static_cast< int >( selectableMask ), static_cast< int >( MP_RULE_MODES_ALL_PUBLIC ),
			static_cast< int >( MP_RULE_MODES_ALL_PUBLIC ) );
		return false;
	}

	for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
		const mpRuleFieldDescriptor_t &field = ruleFields[ i ];
		if ( field.id != i || !IsMachineKey( field.key ) ||
				!IsLocalizationId( field.nameLocalizationId ) ||
				!IsLocalizationId( field.descriptionLocalizationId ) ||
				field.type < MP_RULE_TYPE_BOOL || field.type > MP_RULE_TYPE_ENUM ||
				field.frozenMutation < MP_RULE_FROZEN_STAGE ||
				field.frozenMutation > MP_RULE_FROZEN_REJECT ||
				field.minimumValue > field.maximumValue ||
				field.defaultValue < field.minimumValue || field.defaultValue > field.maximumValue ||
				field.applicableGameTypes == 0 ||
				( field.applicableGameTypes & ~MP_RULE_MODES_ALL_PUBLIC ) != 0 ||
				field.validationCallback == NULL ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE,
				static_cast< mpRuleFieldId_t >( i ), i );
			return false;
		}

		if ( field.type == MP_RULE_TYPE_BOOL &&
				( field.minimumValue != 0 || field.maximumValue != 1 || field.enumValues != NULL ) ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, field.id, i );
			return false;
		}
		if ( field.type == MP_RULE_TYPE_ENUM ) {
			if ( field.enumValues == NULL || field.numEnumValues <= 0 ||
					!EnumContainsValue( field, field.defaultValue ) ) {
				SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, field.id, i );
				return false;
			}
			for ( int valueIndex = 0; valueIndex < field.numEnumValues; valueIndex++ ) {
				const mpRuleEnumValueDescriptor_t &enumValue = field.enumValues[ valueIndex ];
				if ( enumValue.key == NULL || BoundedStringLength( enumValue.key, 64 ) <= 0 ||
						!IsLocalizationId( enumValue.localizationId ) ||
						enumValue.value < field.minimumValue || enumValue.value > field.maximumValue ) {
					SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, field.id, valueIndex );
					return false;
				}
				for ( int otherValue = valueIndex + 1; otherValue < field.numEnumValues; otherValue++ ) {
					if ( enumValue.value == field.enumValues[ otherValue ].value ||
							idStr::Icmp( enumValue.key, field.enumValues[ otherValue ].key ) == 0 ) {
						SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, field.id, valueIndex );
						return false;
					}
				}
			}
		} else if ( field.enumValues != NULL || field.numEnumValues != 0 ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, field.id, i );
			return false;
		}

		for ( int other = i + 1; other < MP_RULE_FIELD_COUNT; other++ ) {
			if ( idStr::Icmp( field.key, ruleFields[ other ].key ) == 0 ) {
				SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, field.id, other );
				return false;
			}
		}
	}

	for ( int i = 0; i < MP_MATCH_PROFILE_COUNT; i++ ) {
		const mpRuleProfileDefinition_t &profile = profileDefinitions[ i ];
		if ( profile.descriptor.id != i || !IsMachineKey( profile.descriptor.key ) ||
				!IsLocalizationId( profile.descriptor.nameLocalizationId ) ||
				!IsLocalizationId( profile.descriptor.descriptionLocalizationId ) ||
				profile.descriptor.applicableGameTypes == 0 ||
				( profile.descriptor.applicableGameTypes & ~MP_RULE_MODES_ALL_PUBLIC ) != 0 ||
				( profile.numOverrides > 0 && profile.overrides == NULL ) ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, MP_RULE_GAME_TYPE, i );
			return false;
		}

		bool managedOverride = false;
		bool managedValue = ruleFields[ MP_RULE_MANAGED_MATCH ].defaultValue != 0;
		for ( int overrideIndex = 0; overrideIndex < profile.numOverrides; overrideIndex++ ) {
			const mpRuleProfileOverride_t &overrideValue = profile.overrides[ overrideIndex ];
			if ( !IsKnownField( overrideValue.field ) ) {
				SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, MP_RULE_GAME_TYPE, i );
				return false;
			}
			const mpRuleFieldDescriptor_t &field = ruleFields[ overrideValue.field ];
			if ( overrideValue.value < field.minimumValue || overrideValue.value > field.maximumValue ||
					( field.type == MP_RULE_TYPE_ENUM && !EnumContainsValue( field, overrideValue.value ) ) ) {
				SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, overrideValue.field, overrideValue.value );
				return false;
			}
			for ( int other = overrideIndex + 1; other < profile.numOverrides; other++ ) {
				if ( overrideValue.field == profile.overrides[ other ].field ) {
					SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, overrideValue.field, i );
					return false;
				}
			}
			if ( overrideValue.field == MP_RULE_MANAGED_MATCH ) {
				managedOverride = true;
				managedValue = overrideValue.value != 0;
			}
		}
		if ( profile.descriptor.managed != managedValue ||
				( profile.descriptor.managed && !managedOverride ) ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, MP_RULE_MANAGED_MATCH, i );
			return false;
		}

		for ( int other = i + 1; other < MP_MATCH_PROFILE_COUNT; other++ ) {
			if ( idStr::Icmp( profile.descriptor.key, profileDefinitions[ other ].descriptor.key ) == 0 ) {
				SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, MP_RULE_GAME_TYPE, other );
				return false;
			}
		}
	}

	if ( profileDefinitions[ MP_MATCH_PROFILE_CASUAL ].descriptor.applicableGameTypes !=
			MP_RULE_MODES_ALL_PUBLIC ) {
		SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, MP_RULE_GAME_TYPE,
			MP_MATCH_PROFILE_CASUAL );
		return false;
	}

	for ( int gameType = 0; gameType < NUM_GAME_TYPES; gameType++ ) {
		if ( !MPGameTypeIsSelectable( gameType ) ) {
			continue;
		}
		const mpMatchProfileId_t recommended = MPRecommendedMatchProfileForGameType( gameType );
		if ( !IsKnownProfile( recommended ) || recommended == MP_MATCH_PROFILE_CASUAL ||
				!ModeInMask( gameType, profileDefinitions[ recommended ].descriptor.applicableGameTypes ) ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, MP_RULE_GAME_TYPE, gameType );
			return false;
		}
	}

	return true;
}

/*
================
Strict parsing
================
*/
bool MPParseBoundedRuleInteger( const char *text, int minimum, int maximum, int &value ) {
	if ( minimum > maximum ) {
		return false;
	}
	const int length = BoundedStringLength( text, 12 );
	if ( length <= 0 ) {
		return false;
	}

	int index = 0;
	bool negative = false;
	if ( text[ index ] == '-' || text[ index ] == '+' ) {
		negative = text[ index ] == '-';
		index++;
		if ( index == length ) {
			return false;
		}
	}

	const uint64_t positiveLimit = static_cast< uint64_t >( MP_RULE_INT_MAX );
	const uint64_t negativeLimit = positiveLimit + 1u;
	const uint64_t magnitudeLimit = negative ? negativeLimit : positiveLimit;
	uint64_t magnitude = 0;
	for ( ; index < length; index++ ) {
		const unsigned char character = static_cast< unsigned char >( text[ index ] );
		if ( character < '0' || character > '9' ) {
			return false;
		}
		const uint64_t digit = static_cast< uint64_t >( character - '0' );
		if ( magnitude > ( magnitudeLimit - digit ) / 10u ) {
			return false;
		}
		magnitude = magnitude * 10u + digit;
	}

	int parsed;
	if ( negative ) {
		parsed = ( magnitude == negativeLimit ) ? MP_RULE_INT_MIN : -static_cast< int >( magnitude );
	} else {
		parsed = static_cast< int >( magnitude );
	}
	if ( parsed < minimum || parsed > maximum ) {
		return false;
	}
	value = parsed;
	return true;
}

bool MPParseMatchRuleValue( mpRuleFieldId_t field, const char *text, int &value,
	mpRuleValidationFailure_t &failure ) {
	failure.Clear();
	if ( !IsKnownField( field ) ) {
		SetFailure( failure, MP_RULE_ERROR_UNKNOWN_FIELD, field );
		return false;
	}
	const int length = BoundedStringLength( text, 64 );
	if ( length == 0 ) {
		SetFailure( failure, MP_RULE_ERROR_EMPTY_VALUE, field );
		return false;
	}
	if ( length < 0 ) {
		SetFailure( failure, MP_RULE_ERROR_MALFORMED_VALUE, field );
		return false;
	}

	const mpRuleFieldDescriptor_t &descriptor = ruleFields[ field ];
	if ( descriptor.type == MP_RULE_TYPE_BOOL ) {
		if ( idStr::Icmp( text, "1" ) == 0 || idStr::Icmp( text, "true" ) == 0 ||
				idStr::Icmp( text, "on" ) == 0 || idStr::Icmp( text, "yes" ) == 0 ) {
			value = 1;
			return true;
		}
		if ( idStr::Icmp( text, "0" ) == 0 || idStr::Icmp( text, "false" ) == 0 ||
				idStr::Icmp( text, "off" ) == 0 || idStr::Icmp( text, "no" ) == 0 ) {
			value = 0;
			return true;
		}
		SetFailure( failure, MP_RULE_ERROR_MALFORMED_VALUE, field );
		return false;
	}

	if ( descriptor.type == MP_RULE_TYPE_ENUM ) {
		for ( int i = 0; i < descriptor.numEnumValues; i++ ) {
			if ( idStr::Icmp( text, descriptor.enumValues[ i ].key ) == 0 ) {
				value = descriptor.enumValues[ i ].value;
				return true;
			}
		}
		int parsed = 0;
		if ( MPParseBoundedRuleInteger( text, MP_RULE_INT_MIN, MP_RULE_INT_MAX, parsed ) ) {
			if ( parsed < descriptor.minimumValue || parsed > descriptor.maximumValue ) {
				SetFailure( failure, MP_RULE_ERROR_VALUE_OUT_OF_RANGE, field, parsed,
					descriptor.minimumValue, descriptor.maximumValue );
				return false;
			}
			if ( EnumContainsValue( descriptor, parsed ) ) {
				value = parsed;
				return true;
			}
		}
		SetFailure( failure, MP_RULE_ERROR_UNKNOWN_ENUM_VALUE, field );
		return false;
	}

	int parsed = 0;
	if ( !MPParseBoundedRuleInteger( text, MP_RULE_INT_MIN, MP_RULE_INT_MAX, parsed ) ) {
		SetFailure( failure, MP_RULE_ERROR_MALFORMED_VALUE, field );
		return false;
	}
	if ( parsed < descriptor.minimumValue || parsed > descriptor.maximumValue ) {
		SetFailure( failure, MP_RULE_ERROR_VALUE_OUT_OF_RANGE, field, parsed,
			descriptor.minimumValue, descriptor.maximumValue );
		return false;
	}
	value = parsed;
	return true;
}

/*
================
mpMatchRulesDraft
================
*/
mpMatchRulesDraft::mpMatchRulesDraft( void ) {
	for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
		values[ i ] = ruleFields[ i ].defaultValue;
	}
	sourceProfile = MP_MATCH_PROFILE_CUSTOM;
	customized = true;
}

bool mpMatchRulesDraft::SetTypedValue( mpRuleFieldId_t field, mpRuleFieldType_t expectedType,
	int value, mpRuleValidationFailure_t &failure, bool markCustomized ) {
	failure.Clear();
	if ( !IsKnownField( field ) ) {
		SetFailure( failure, MP_RULE_ERROR_UNKNOWN_FIELD, field );
		return false;
	}
	const mpRuleFieldDescriptor_t &descriptor = ruleFields[ field ];
	if ( descriptor.type != expectedType ) {
		SetFailure( failure, MP_RULE_ERROR_WRONG_FIELD_TYPE, field,
			expectedType, descriptor.type, descriptor.type );
		return false;
	}
	if ( value < descriptor.minimumValue || value > descriptor.maximumValue ) {
		SetFailure( failure, MP_RULE_ERROR_VALUE_OUT_OF_RANGE, field, value,
			descriptor.minimumValue, descriptor.maximumValue );
		return false;
	}
	if ( descriptor.type == MP_RULE_TYPE_ENUM && !EnumContainsValue( descriptor, value ) ) {
		SetFailure( failure, MP_RULE_ERROR_UNKNOWN_ENUM_VALUE, field, value );
		return false;
	}

	if ( values[ field ] != value ) {
		values[ field ] = value;
		if ( markCustomized ) {
			customized = true;
		}
	}
	return true;
}

void mpMatchRulesDraft::SetRawProfileValue( mpRuleFieldId_t field, int value ) {
	if ( IsKnownField( field ) ) {
		values[ field ] = value;
	}
}

bool mpMatchRulesDraft::SetBool( mpRuleFieldId_t field, bool value,
	mpRuleValidationFailure_t &failure ) {
	return SetTypedValue( field, MP_RULE_TYPE_BOOL, value ? 1 : 0, failure, true );
}

bool mpMatchRulesDraft::SetInteger( mpRuleFieldId_t field, int value,
	mpRuleValidationFailure_t &failure ) {
	return SetTypedValue( field, MP_RULE_TYPE_INTEGER, value, failure, true );
}

bool mpMatchRulesDraft::SetEnum( mpRuleFieldId_t field, int value,
	mpRuleValidationFailure_t &failure ) {
	return SetTypedValue( field, MP_RULE_TYPE_ENUM, value, failure, true );
}

bool mpMatchRulesDraft::SetParsedValue( mpRuleFieldId_t field, const char *text,
	mpRuleValidationFailure_t &failure ) {
	int value = 0;
	if ( !MPParseMatchRuleValue( field, text, value, failure ) ) {
		return false;
	}
	const mpRuleFieldDescriptor_t &descriptor = ruleFields[ field ];
	switch ( descriptor.type ) {
		case MP_RULE_TYPE_BOOL:       return SetBool( field, value != 0, failure );
		case MP_RULE_TYPE_INTEGER:    return SetInteger( field, value, failure );
		case MP_RULE_TYPE_ENUM:       return SetEnum( field, value, failure );
		default:
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, field );
			return false;
	}
}

int mpMatchRulesDraft::GetInteger( mpRuleFieldId_t field ) const {
	return IsKnownField( field ) ? values[ field ] : 0;
}

bool mpMatchRulesDraft::GetBool( mpRuleFieldId_t field ) const {
	return GetInteger( field ) != 0;
}

mpMatchProfileId_t mpMatchRulesDraft::SourceProfile( void ) const {
	return sourceProfile;
}

bool mpMatchRulesDraft::IsCustomized( void ) const {
	return customized;
}

/*
================
mpMatchRulesSnapshot
================
*/
mpMatchRulesSnapshot::mpMatchRulesSnapshot( void ) {
	memset( values, 0, sizeof( values ) );
	schemaVersion = MP_MATCH_RULES_SCHEMA_VERSION;
	revision = 0;
	digest = 0;
	sourceProfile = MP_MATCH_PROFILE_CUSTOM;
	customized = true;
}

int mpMatchRulesSnapshot::GetInteger( mpRuleFieldId_t field ) const {
	return IsKnownField( field ) ? values[ field ] : 0;
}

bool mpMatchRulesSnapshot::GetBool( mpRuleFieldId_t field ) const {
	return GetInteger( field ) != 0;
}

uint32_t mpMatchRulesSnapshot::SchemaVersion( void ) const {
	return schemaVersion;
}

uint32_t mpMatchRulesSnapshot::Revision( void ) const {
	return revision;
}

uint64_t mpMatchRulesSnapshot::Digest( void ) const {
	return digest;
}

void mpMatchRulesSnapshot::DigestString( idStr &out ) const {
	char text[ 24 ];
	idStr::snPrintf( text, sizeof( text ), "%016llx", static_cast< unsigned long long >( digest ) );
	out = text;
}

void mpMatchRulesSnapshot::BuildCanonicalText( idStr &out ) const {
	out.Clear();
	char line[ 128 ];
	idStr::snPrintf( line, sizeof( line ), "schema=%u\n", schemaVersion );
	out.Append( line );
	for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
		idStr::snPrintf( line, sizeof( line ), "%d:%s=%d\n", i, ruleFields[ i ].key, values[ i ] );
		out.Append( line );
	}
}

mpMatchProfileId_t mpMatchRulesSnapshot::SourceProfile( void ) const {
	return sourceProfile;
}

bool mpMatchRulesSnapshot::IsCustomized( void ) const {
	return customized;
}

bool mpMatchRulesSnapshot::SameRuleValues( const mpMatchRulesSnapshot &other ) const {
	if ( schemaVersion != other.schemaVersion ) {
		return false;
	}
	for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
		if ( values[ i ] != other.values[ i ] ) {
			return false;
		}
	}
	return true;
}

void mpMatchRulesSnapshot::AssignFromDraft( const mpMatchRulesDraft &draft, uint32_t newRevision ) {
	for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
		values[ i ] = draft.GetInteger( static_cast< mpRuleFieldId_t >( i ) );
	}
	schemaVersion = MP_MATCH_RULES_SCHEMA_VERSION;
	revision = newRevision;
	sourceProfile = draft.SourceProfile();
	customized = draft.IsCustomized();
	RebuildDigest();
}

void mpMatchRulesSnapshot::RebuildDigest( void ) {
	idStr canonical;
	BuildCanonicalText( canonical );
	uint64_t value = UINT64_C( 14695981039346656037 );
	for ( int i = 0; i < canonical.Length(); i++ ) {
		value ^= static_cast< unsigned char >( canonical[ i ] );
		value *= UINT64_C( 1099511628211 );
	}
	digest = value;
}

/*
================
mpCompetitiveRules
================
*/
mpCompetitiveRules::mpCompetitiveRules( void ) {
	hasStaged = false;
	mpMatchRulesDraft draft;
	mpRuleValidationFailure_t failure;
	if ( BuildProfileDraft( MP_MATCH_PROFILE_CASUAL, GAME_DM, draft, failure ) ) {
		committed.AssignFromDraft( draft, 1 );
	}
}

const mpMatchRulesSnapshot &mpCompetitiveRules::Committed( void ) const {
	return committed;
}

mpMatchRulesDraft mpCompetitiveRules::BeginDraftFromCommitted( void ) const {
	mpMatchRulesDraft draft;
	for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
		draft.values[ i ] = committed.values[ i ];
	}
	draft.sourceProfile = committed.sourceProfile;
	draft.customized = committed.customized;
	return draft;
}

mpMatchRulesDraft mpCompetitiveRules::BeginDraftForNextWarmup( void ) const {
	if ( !hasStaged ) {
		return BeginDraftFromCommitted();
	}

	mpMatchRulesDraft draft;
	for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
		draft.values[ i ] = staged.values[ i ];
	}
	draft.sourceProfile = staged.sourceProfile;
	draft.customized = staged.customized;
	return draft;
}

bool mpCompetitiveRules::BuildProfileDraft( mpMatchProfileId_t profile, int gameType,
	mpMatchRulesDraft &draft, mpRuleValidationFailure_t &failure ) {
	failure.Clear();
	if ( !IsKnownProfile( profile ) ) {
		SetFailure( failure, MP_RULE_ERROR_UNKNOWN_PROFILE, MP_RULE_GAME_TYPE, profile );
		return false;
	}
	if ( !MPGameTypeIsSelectable( gameType ) ) {
		SetFailure( failure, MP_RULE_ERROR_MODE_UNAVAILABLE, MP_RULE_GAME_TYPE, gameType );
		return false;
	}
	const mpRuleProfileDefinition_t &definition = profileDefinitions[ profile ];
	if ( !ModeInMask( gameType, definition.descriptor.applicableGameTypes ) ) {
		SetFailure( failure, MP_RULE_ERROR_PROFILE_MODE_MISMATCH, MP_RULE_GAME_TYPE,
			gameType, profile, profile );
		return false;
	}

	for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
		draft.SetRawProfileValue( static_cast< mpRuleFieldId_t >( i ), ruleFields[ i ].defaultValue );
	}
	for ( int i = 0; i < definition.numOverrides; i++ ) {
		draft.SetRawProfileValue( definition.overrides[ i ].field, definition.overrides[ i ].value );
	}
	draft.SetRawProfileValue( MP_RULE_GAME_TYPE, gameType );
	draft.sourceProfile = profile;
	draft.customized = false;
	return true;
}

bool mpCompetitiveRules::BeginDraftFromProfile( mpMatchProfileId_t profile, int gameType,
	mpMatchRulesDraft &draft, mpRuleValidationFailure_t &failure ) const {
	if ( !MPValidateMatchRulesDescriptorTable( failure ) ) {
		return false;
	}
	return BuildProfileDraft( profile, gameType, draft, failure );
}

bool mpCompetitiveRules::ValidateDraft( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleValidationFailure_t &failure ) {
	if ( !MPValidateMatchRulesDescriptorTable( failure ) ) {
		return false;
	}

	if ( context.maxClients < 1 || context.maxTeamSize < 1 ||
			context.maxTeamSize > context.maxClients || context.maxRosterSizePerTeam < 0 ||
			context.maxCountdownSeconds < ruleFields[ MP_RULE_COUNTDOWN_SECONDS ].minimumValue ||
			context.maxTimeoutCountPerTeam < 0 || context.maxTimeoutSeconds < 0 ||
			context.maxOvertimeSeconds < 0 || context.maxOvertimePeriods < 0 ) {
		SetFailure( failure, MP_RULE_ERROR_INVALID_SERVER_BOUNDS, MP_RULE_FIELD_COUNT );
		return false;
	}

	for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
		const mpRuleFieldDescriptor_t &field = ruleFields[ i ];
		const int value = draft.GetInteger( field.id );
		if ( value < field.minimumValue || value > field.maximumValue ) {
			SetFailure( failure, MP_RULE_ERROR_VALUE_OUT_OF_RANGE, field.id, value,
				field.minimumValue, field.maximumValue );
			return false;
		}
		if ( field.type == MP_RULE_TYPE_ENUM && !EnumContainsValue( field, value ) ) {
			SetFailure( failure, MP_RULE_ERROR_UNKNOWN_ENUM_VALUE, field.id, value );
			return false;
		}
		if ( !field.validationCallback( draft, context, failure ) ) {
			return false;
		}
	}

	const int gameType = draft.GetInteger( MP_RULE_GAME_TYPE );
	const bool teamMode = MPGameTypeHasAny( gameType, GTF_TEAM );
	const bool roundMode = MPGameTypeHasAny( gameType, GTF_ROUNDLIMIT );
	const bool managed = draft.GetBool( MP_RULE_MANAGED_MATCH );
	const bool warmup = draft.GetBool( MP_RULE_WARMUP_ENABLED );
	const int readiness = draft.GetInteger( MP_RULE_READINESS_POLICY );
	const int readyThreshold = draft.GetInteger( MP_RULE_READY_THRESHOLD_BASIS_POINTS );

	if ( managed && ( !warmup || draft.GetInteger( MP_RULE_MIN_ACTIVE_HUMANS ) < 2 ) ) {
		SetFailure( failure, MP_RULE_ERROR_READINESS_POLICY, MP_RULE_MANAGED_MATCH,
			managed ? 1 : 0 );
		return false;
	}
	if ( !warmup && ( readiness != MP_READY_DISABLED || readyThreshold != 0 ) ) {
		SetFailure( failure, MP_RULE_ERROR_READINESS_POLICY, MP_RULE_WARMUP_ENABLED,
			readiness );
		return false;
	}
	if ( readiness == MP_READY_DISABLED ) {
		if ( readyThreshold != 0 ) {
			SetFailure( failure, MP_RULE_ERROR_READINESS_POLICY,
				MP_RULE_READY_THRESHOLD_BASIS_POINTS, readyThreshold, 0, 0 );
			return false;
		}
	} else if ( readyThreshold <= 0 ) {
		SetFailure( failure, MP_RULE_ERROR_READINESS_POLICY,
			MP_RULE_READY_THRESHOLD_BASIS_POINTS, readyThreshold, 1, 10000 );
		return false;
	}
	if ( ( readiness == MP_READY_TEAM || readiness == MP_READY_INDIVIDUAL_AND_TEAM ) && !teamMode ) {
		SetFailure( failure, MP_RULE_ERROR_READINESS_POLICY, MP_RULE_READINESS_POLICY, readiness );
		return false;
	}
	const bool usesTeamReady = readiness == MP_READY_TEAM ||
		readiness == MP_READY_INDIVIDUAL_AND_TEAM;
	const int rosterSizePerTeam = draft.GetInteger( MP_RULE_ROSTER_SIZE_PER_TEAM );
	if ( usesTeamReady && !draft.GetBool( MP_RULE_REQUIRE_BOTH_TEAMS ) ) {
		SetFailure( failure, MP_RULE_ERROR_READINESS_POLICY,
			MP_RULE_REQUIRE_BOTH_TEAMS, 0, 1, 1 );
		return false;
	}
	if ( usesTeamReady && rosterSizePerTeam == 0 ) {
		// Team-ready is captain authority, not a second anonymous vote.  A
		// positive roster size selects the canonical roster adapter, which
		// declares seat zero on each side as its captain seat.  Rosterless team
		// matches use individual readiness and cannot deadlock on absent captains.
		SetFailure( failure, MP_RULE_ERROR_READINESS_POLICY,
			MP_RULE_ROSTER_SIZE_PER_TEAM, rosterSizePerTeam, 1,
			context.maxRosterSizePerTeam );
		return false;
	}

	if ( gameType == GAME_DUEL && draft.GetInteger( MP_RULE_MIN_ACTIVE_HUMANS ) > 2 ) {
		SetFailure( failure, MP_RULE_ERROR_SERVER_CAPACITY, MP_RULE_MIN_ACTIVE_HUMANS,
			draft.GetInteger( MP_RULE_MIN_ACTIVE_HUMANS ), 1, 2 );
		return false;
	}

	if ( MPGameTypeHasAny( gameType, GTF_FRAGLIMIT ) &&
			draft.GetInteger( MP_RULE_TIME_LIMIT_MINUTES ) == 0 &&
			draft.GetInteger( MP_RULE_FRAG_LIMIT ) == 0 ) {
		SetFailure( failure, MP_RULE_ERROR_WIN_CONDITION, MP_RULE_FRAG_LIMIT );
		return false;
	}
	if ( roundMode && draft.GetInteger( MP_RULE_TIME_LIMIT_MINUTES ) == 0 &&
			draft.GetInteger( MP_RULE_ROUND_LIMIT ) == 0 ) {
		SetFailure( failure, MP_RULE_ERROR_WIN_CONDITION, MP_RULE_ROUND_LIMIT );
		return false;
	}

	const int overtimePolicy = draft.GetInteger( MP_RULE_OVERTIME_POLICY );
	const int overtimeSeconds = draft.GetInteger( MP_RULE_OVERTIME_PERIOD_SECONDS );
	const int overtimePeriods = draft.GetInteger( MP_RULE_OVERTIME_MAX_PERIODS );
	if ( overtimePolicy == MP_OVERTIME_TIMED_PERIODS ) {
		if ( overtimeSeconds <= 0 || draft.GetInteger( MP_RULE_TIME_LIMIT_MINUTES ) <= 0 ) {
			SetFailure( failure, MP_RULE_ERROR_OVERTIME_POLICY,
				MP_RULE_OVERTIME_PERIOD_SECONDS, overtimeSeconds, 1, context.maxOvertimeSeconds );
			return false;
		}
	} else if ( overtimeSeconds != 0 || overtimePeriods != 0 ) {
		SetFailure( failure, MP_RULE_ERROR_OVERTIME_POLICY,
			MP_RULE_OVERTIME_PERIOD_SECONDS, overtimeSeconds, 0, 0 );
		return false;
	}
	if ( draft.GetInteger( MP_RULE_SUDDEN_DEATH_RESPAWN_MAX ) <
			draft.GetInteger( MP_RULE_SUDDEN_DEATH_RESPAWN_DELAY ) ) {
		SetFailure( failure, MP_RULE_ERROR_OVERTIME_POLICY, MP_RULE_SUDDEN_DEATH_RESPAWN_MAX,
			draft.GetInteger( MP_RULE_SUDDEN_DEATH_RESPAWN_MAX ),
			draft.GetInteger( MP_RULE_SUDDEN_DEATH_RESPAWN_DELAY ), 60 );
		return false;
	}

	const int timeoutCount = draft.GetInteger( MP_RULE_TEAM_TIMEOUT_COUNT );
	const int timeoutSeconds = draft.GetInteger( MP_RULE_TEAM_TIMEOUT_SECONDS );
	if ( ( timeoutCount == 0 ) != ( timeoutSeconds == 0 ) ) {
		SetFailure( failure, MP_RULE_ERROR_TIMEOUT_POLICY,
			timeoutCount == 0 ? MP_RULE_TEAM_TIMEOUT_SECONDS : MP_RULE_TEAM_TIMEOUT_COUNT,
			timeoutCount == 0 ? timeoutSeconds : timeoutCount );
		return false;
	}
	if ( timeoutCount > 0 && ( !managed || !ModeInMask( gameType, MP_RULE_MODES_TIMEOUT ) ) ) {
		SetFailure( failure, MP_RULE_ERROR_TIMEOUT_POLICY, MP_RULE_TEAM_TIMEOUT_COUNT, timeoutCount );
		return false;
	}

	if ( draft.GetBool( MP_RULE_TEAM_DAMAGE ) && !teamMode ) {
		SetFailure( failure, MP_RULE_ERROR_MODE_POLICY, MP_RULE_TEAM_DAMAGE, 1 );
		return false;
	}
	if ( draft.GetBool( MP_RULE_BUYING_ENABLED ) && !ModeInMask( gameType, MP_RULE_MODES_BUYING ) ) {
		SetFailure( failure, MP_RULE_ERROR_MODE_POLICY, MP_RULE_BUYING_ENABLED, 1 );
		return false;
	}
	if ( draft.GetInteger( MP_RULE_ROSTER_SIZE_PER_TEAM ) > 0 && !teamMode ) {
		SetFailure( failure, MP_RULE_ERROR_ROSTER_POLICY, MP_RULE_ROSTER_SIZE_PER_TEAM,
			draft.GetInteger( MP_RULE_ROSTER_SIZE_PER_TEAM ) );
		return false;
	}

	failure.Clear();
	return true;
}

bool mpCompetitiveRules::FieldChanged( const mpMatchRulesSnapshot &snapshot,
	const mpMatchRulesDraft &draft, mpRuleFieldId_t field ) {
	return snapshot.GetInteger( field ) != draft.GetInteger( field );
}

mpRuleCommitResult_t mpCompetitiveRules::Commit( const mpMatchRulesDraft &draft,
	const mpMatchRulesValidationContext_t &context, mpRuleCommitBoundary_t boundary ) {
	mpRuleCommitResult_t result;
	result.committedRevision = committed.Revision();
	result.committedDigest = committed.Digest();
	if ( boundary != MP_RULES_OPEN_FOR_COMMIT && boundary != MP_RULES_FROZEN_FOR_MAP ) {
		SetFailure( result.failure, MP_RULE_ERROR_MALFORMED_VALUE, MP_RULE_FIELD_COUNT,
			boundary, MP_RULES_OPEN_FOR_COMMIT, MP_RULES_FROZEN_FOR_MAP );
		return result;
	}

	if ( !ValidateDraft( draft, context, result.failure ) ) {
		return result;
	}
	if ( committed.Revision() == MP_RULE_REVISION_MAX ) {
		SetFailure( result.failure, MP_RULE_ERROR_REVISION_EXHAUSTED, MP_RULE_FIELD_COUNT );
		return result;
	}
	if ( SnapshotMatchesDraft( committed, draft ) ) {
		result.disposition = MP_RULE_COMMIT_UNCHANGED;
		result.candidateDigest = committed.Digest();
		return result;
	}

	mpMatchRulesSnapshot candidate;
	candidate.AssignFromDraft( draft, committed.Revision() + 1 );
	result.candidateDigest = candidate.Digest();

	if ( boundary == MP_RULES_FROZEN_FOR_MAP ) {
		for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
			const mpRuleFieldId_t field = static_cast< mpRuleFieldId_t >( i );
			if ( FieldChanged( committed, draft, field ) &&
					ruleFields[ i ].frozenMutation == MP_RULE_FROZEN_REJECT ) {
				SetFailure( result.failure, MP_RULE_ERROR_FROZEN_FIELD, field,
					draft.GetInteger( field ) );
				return result;
			}
		}
		staged = candidate;
		hasStaged = true;
		result.disposition = MP_RULE_COMMIT_STAGED;
		return result;
	}

	committed = candidate;
	hasStaged = false;
	result.disposition = MP_RULE_COMMIT_APPLIED;
	result.committedRevision = committed.Revision();
	result.committedDigest = committed.Digest();
	return result;
}

mpRuleCommitResult_t mpCompetitiveRules::ApplyStagedAtWarmup(
	const mpMatchRulesValidationContext_t &context ) {
	mpRuleCommitResult_t result;
	result.committedRevision = committed.Revision();
	result.committedDigest = committed.Digest();
	if ( !hasStaged ) {
		SetFailure( result.failure, MP_RULE_ERROR_NO_STAGED_SNAPSHOT, MP_RULE_FIELD_COUNT );
		return result;
	}
	if ( committed.Revision() == MP_RULE_REVISION_MAX ) {
		SetFailure( result.failure, MP_RULE_ERROR_REVISION_EXHAUSTED, MP_RULE_FIELD_COUNT );
		return result;
	}

	mpMatchRulesDraft draft;
	for ( int i = 0; i < MP_RULE_FIELD_COUNT; i++ ) {
		draft.values[ i ] = staged.values[ i ];
	}
	draft.sourceProfile = staged.sourceProfile;
	draft.customized = staged.customized;
	if ( !ValidateDraft( draft, context, result.failure ) ) {
		return result;
	}

	staged.AssignFromDraft( draft, committed.Revision() + 1 );
	result.candidateDigest = staged.Digest();
	committed = staged;
	hasStaged = false;
	result.disposition = MP_RULE_COMMIT_APPLIED;
	result.committedRevision = committed.Revision();
	result.committedDigest = committed.Digest();
	return result;
}

bool mpCompetitiveRules::HasStagedSnapshot( void ) const {
	return hasStaged;
}

const mpMatchRulesSnapshot *mpCompetitiveRules::StagedSnapshot( void ) const {
	return hasStaged ? &staged : NULL;
}

bool mpCompetitiveRules::DiscardStagedSnapshot( void ) {
	if ( !hasStaged ) {
		return false;
	}
	hasStaged = false;
	return true;
}

/*
================
MPValidateBuiltInMatchProfiles

Runs every built-in profile for every mode it advertises through the same
cross-field validator used by real transactions.  Map support is intentionally
left to the selected-map adapter; profile data itself has no map dependency.
================
*/
bool MPValidateBuiltInMatchProfiles( mpRuleValidationFailure_t &failure ) {
	if ( !MPValidateMatchRulesDescriptorTable( failure ) ) {
		return false;
	}

	const mpMatchRulesValidationContext_t context;
	mpCompetitiveRules rules;
	for ( int profileIndex = 0; profileIndex < MP_MATCH_PROFILE_COUNT; profileIndex++ ) {
		const mpMatchProfileDescriptor_t &profile = profileDefinitions[ profileIndex ].descriptor;
		for ( int gameType = 0; gameType < NUM_GAME_TYPES; gameType++ ) {
			if ( !ModeInMask( gameType, profile.applicableGameTypes ) ) {
				continue;
			}
			mpMatchRulesDraft draft;
			if ( !rules.BeginDraftFromProfile( profile.id, gameType, draft, failure ) ||
					!mpCompetitiveRules::ValidateDraft( draft, context, failure ) ||
					draft.GetBool( MP_RULE_MANAGED_MATCH ) != profile.managed ) {
				if ( failure.reason == MP_RULE_VALID ) {
					SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE,
						MP_RULE_MANAGED_MATCH, profileIndex );
				}
				return false;
			}
			if ( profile.managed && MPGameTypeHasAny( gameType, GTF_TEAM ) &&
				draft.GetInteger( MP_RULE_ROSTER_SIZE_PER_TEAM ) == 0 &&
				( draft.GetInteger( MP_RULE_READINESS_POLICY ) != MP_READY_INDIVIDUAL ||
					draft.GetInteger( MP_RULE_READY_THRESHOLD_BASIS_POINTS ) != 10000 ) ) {
				SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE,
					MP_RULE_READINESS_POLICY, profileIndex );
				return false;
			}
		}
	}

	// A semantic failure must preserve the published snapshot and pending slot.
	{
		mpCompetitiveRules transactionRules;
		const uint32_t baselineRevision = transactionRules.Committed().Revision();
		const uint64_t baselineDigest = transactionRules.Committed().Digest();
		mpMatchRulesDraft invalidDraft = transactionRules.BeginDraftFromCommitted();
		if ( !invalidDraft.SetEnum( MP_RULE_OVERTIME_POLICY,
				MP_OVERTIME_SUDDEN_DEATH, failure ) ) {
			return false;
		}
		const mpRuleCommitResult_t rejected = transactionRules.Commit(
			invalidDraft, context, MP_RULES_OPEN_FOR_COMMIT );
		if ( rejected.disposition != MP_RULE_COMMIT_REJECTED ||
				transactionRules.Committed().Revision() != baselineRevision ||
				transactionRules.Committed().Digest() != baselineDigest ||
				transactionRules.HasStagedSnapshot() ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE,
				MP_RULE_OVERTIME_POLICY, 1 );
			return false;
		}
	}

	// Captain readiness is accepted only when the rules explicitly select the
	// roster workflow.  Both rejection and the corresponding safe alternative
	// are checked through the real transaction path.
	{
		mpCompetitiveRules transactionRules;
		const uint32_t baselineRevision = transactionRules.Committed().Revision();
		const uint64_t baselineDigest = transactionRules.Committed().Digest();
		mpMatchRulesDraft captainDraft;
		if ( !transactionRules.BeginDraftFromProfile(
				MP_MATCH_PROFILE_COMPETITIVE_TDM, GAME_TDM, captainDraft, failure ) ||
			!captainDraft.SetEnum( MP_RULE_READINESS_POLICY, MP_READY_TEAM, failure ) ) {
			return false;
		}
		const mpRuleCommitResult_t rosterless = transactionRules.Commit(
			captainDraft, context, MP_RULES_OPEN_FOR_COMMIT );
		if ( rosterless.disposition != MP_RULE_COMMIT_REJECTED ||
			rosterless.failure.reason != MP_RULE_ERROR_READINESS_POLICY ||
			rosterless.failure.field != MP_RULE_ROSTER_SIZE_PER_TEAM ||
			transactionRules.Committed().Revision() != baselineRevision ||
			transactionRules.Committed().Digest() != baselineDigest ||
			transactionRules.HasStagedSnapshot() ||
			!captainDraft.SetInteger( MP_RULE_ROSTER_SIZE_PER_TEAM, 1, failure ) ||
			!captainDraft.SetBool( MP_RULE_REQUIRE_BOTH_TEAMS, false, failure ) ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE,
				MP_RULE_READINESS_POLICY, 5 );
			return false;
		}
		const mpRuleCommitResult_t optionalSideCaptain = transactionRules.Commit(
			captainDraft, context, MP_RULES_OPEN_FOR_COMMIT );
		if ( optionalSideCaptain.disposition != MP_RULE_COMMIT_REJECTED ||
			optionalSideCaptain.failure.reason != MP_RULE_ERROR_READINESS_POLICY ||
			optionalSideCaptain.failure.field != MP_RULE_REQUIRE_BOTH_TEAMS ||
			transactionRules.Committed().Revision() != baselineRevision ||
			transactionRules.Committed().Digest() != baselineDigest ||
			!captainDraft.SetBool( MP_RULE_REQUIRE_BOTH_TEAMS, true, failure ) ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE,
				MP_RULE_REQUIRE_BOTH_TEAMS, 6 );
			return false;
		}
		const mpRuleCommitResult_t rostered = transactionRules.Commit(
			captainDraft, context, MP_RULES_OPEN_FOR_COMMIT );
		if ( rostered.disposition != MP_RULE_COMMIT_APPLIED ||
			transactionRules.Committed().Revision() != baselineRevision + 1 ||
			transactionRules.Committed().GetInteger( MP_RULE_READINESS_POLICY ) !=
				MP_READY_TEAM ||
			transactionRules.Committed().GetInteger( MP_RULE_ROSTER_SIZE_PER_TEAM ) != 1 ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE,
				MP_RULE_ROSTER_SIZE_PER_TEAM, 7 );
			return false;
		}
	}

	// A frozen mode change is rejected, while a stageable field publishes only
	// when the warmup boundary explicitly applies it.
	{
		mpCompetitiveRules transactionRules;
		const uint32_t baselineRevision = transactionRules.Committed().Revision();
		const uint64_t baselineDigest = transactionRules.Committed().Digest();
		mpMatchRulesDraft modeDraft;
		if ( !transactionRules.BeginDraftFromProfile( MP_MATCH_PROFILE_COMPETITIVE_TDM,
				GAME_TDM, modeDraft, failure ) ) {
			return false;
		}
		const mpRuleCommitResult_t rejected = transactionRules.Commit(
			modeDraft, context, MP_RULES_FROZEN_FOR_MAP );
		if ( rejected.disposition != MP_RULE_COMMIT_REJECTED ||
				rejected.failure.reason != MP_RULE_ERROR_FROZEN_FIELD ||
				transactionRules.Committed().Revision() != baselineRevision ||
				transactionRules.Committed().Digest() != baselineDigest ||
				transactionRules.HasStagedSnapshot() ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE, MP_RULE_GAME_TYPE, 2 );
			return false;
		}

		mpMatchRulesDraft stagedDraft = transactionRules.BeginDraftForNextWarmup();
		if ( !stagedDraft.SetInteger( MP_RULE_TIME_LIMIT_MINUTES, 11, failure ) ) {
			return false;
		}
		const mpRuleCommitResult_t stagedResult = transactionRules.Commit(
			stagedDraft, context, MP_RULES_FROZEN_FOR_MAP );
		if ( stagedResult.disposition != MP_RULE_COMMIT_STAGED ||
				!transactionRules.HasStagedSnapshot() ||
				transactionRules.Committed().Revision() != baselineRevision ||
				transactionRules.Committed().Digest() != baselineDigest ||
				transactionRules.StagedSnapshot() == NULL ||
				transactionRules.StagedSnapshot()->Digest() != stagedResult.candidateDigest ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE,
				MP_RULE_TIME_LIMIT_MINUTES, 3 );
			return false;
		}

		const mpRuleCommitResult_t applied = transactionRules.ApplyStagedAtWarmup( context );
		if ( applied.disposition != MP_RULE_COMMIT_APPLIED ||
				transactionRules.HasStagedSnapshot() ||
				transactionRules.Committed().Revision() != baselineRevision + 1 ||
				transactionRules.Committed().Digest() != stagedResult.candidateDigest ||
				transactionRules.Committed().GetInteger( MP_RULE_TIME_LIMIT_MINUTES ) != 11 ) {
			SetFailure( failure, MP_RULE_ERROR_DESCRIPTOR_TABLE,
				MP_RULE_TIME_LIMIT_MINUTES, 4 );
			return false;
		}
	}

	failure.Clear();
	return true;
}

#undef MP_RULE_GAME_BIT
#undef MP_RULE_ARRAY_COUNT
