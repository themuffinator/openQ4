//----------------------------------------------------------------
// MatchControlLocalization.h
//
// Closed localization boundary for competitive match protocol and view values.
// Every accepted value maps to a fixed language-table key.  Callers may pass
// hostile or future wire values; those fail closed to a generic localized key.
//----------------------------------------------------------------

#ifndef __MP_MATCH_CONTROL_LOCALIZATION_H__
#define __MP_MATCH_CONTROL_LOCALIZATION_H__

#include "MatchControlModel.h"
#include "MatchSession.h"

const char *MPMatchControlLocalizationKey( mpMatchLocalizationId_t localizationId );
const char *MPMatchControlProtocolReasonKey( mpMatchProtocolReason_t reason );
const char *MPMatchControlPhaseKey( mpGameState_t phase );
const char *MPMatchControlRoundKey( roundState_t round );
const char *MPMatchControlPauseStateKey( mpMatchViewPauseState_t state );
const char *MPMatchControlPauseKindKey( mpMatchViewPauseKind_t kind );
const char *MPMatchControlPauseReasonKey( mpMatchViewPauseReason_t reason );
const char *MPMatchControlResumePolicyKey( mpMatchViewResumePolicy_t policy );
const char *MPMatchControlPublicRoleKey( mpMatchViewPublicRole_t role );
const char *MPMatchControlRosterRoleKey( mpMatchViewRosterRole_t role );
const char *MPMatchControlProtocolRosterRoleKey( mpMatchProtocolRosterRole_t role );
const char *MPMatchControlQueueStateKey( mpMatchViewQueueState_t state );
const char *MPMatchControlProposalScopeKey( mpMatchViewProposalScope_t scope );
const char *MPMatchControlBallotKey( mpMatchViewBallot_t ballot );
const char *MPMatchControlProtocolBallotKey( mpMatchBallotChoice_t ballot );
const char *MPMatchControlSeriesStateKey( mpMatchViewSeriesState_t state );
const char *MPMatchControlVetoActionKey( mpMatchViewVetoAction_t action );
const char *MPMatchControlProtocolVetoActionKey( mpMatchVetoAction_t action );
const char *MPMatchControlMapDispositionKey( mpMatchViewMapDisposition_t disposition );
const char *MPMatchControlMapOutcomeKey( mpMatchViewMapOutcome_t outcome );
const char *MPMatchControlRuleTypeKey( mpMatchViewRuleType_t type );
const char *MPMatchControlRulesBoundaryKey( mpMatchViewRulesBoundary_t boundary );
const char *MPMatchControlEvidenceStateKey( mpMatchViewEvidenceState_t state );
const char *MPMatchControlMVDStateKey( mpMatchViewMVDState_t state );
const char *MPMatchControlReportStateKey( mpMatchViewReportState_t state );
const char *MPMatchControlEvidenceEventKindKey( mpMatchViewEvidenceEventKind_t kind );
const char *MPMatchControlOperationResultStatusKey( mpMatchOperationResultStatus_t status );
const char *MPMatchControlTeamKey( mpMatchTeam_t team );
const char *MPMatchControlStartingSideKey( mpMatchStartingSide_t side );
const char *MPMatchControlReadinessBlockerKey( mpMatchReadinessBlocker_t blocker );
const char *MPMatchControlRuleFieldKey( unsigned char fieldId );
const char *MPMatchControlMatchProfileKey( mpMatchProfileId_t profile );
const char *MPMatchControlSeriesProfileKey( mpSeriesProfileId_t profile );
const char *MPMatchControlErrorReasonKey( mpMatchControlErrorReason_t reason );

#endif // __MP_MATCH_CONTROL_LOCALIZATION_H__
