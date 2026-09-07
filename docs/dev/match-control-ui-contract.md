# Match Control UI adapter contract

`guis/matchcontrol.gui` is a presentation-only fragment included by the existing multiplayer menu. It emits fixed `matchControl` tokens and displays one already-validated, recipient-scoped `mpSessionView`. It must never interpret a display name, localized label, map label, or list-row string as an operation argument.

## Refresh boundary

The multiplayer adapter should refresh Match Control when the in-game menu opens, after `ClientReceiveMatchSessionView` accepts an advanced or replacement view, after an operation result is accepted, and after a selection-only command changes the selected row. Set `match_surface_available` to `1` only while the current accepted view has a nonzero session, participant binding, session revision, control revision, and view revision.

All user-facing values written by the adapter are localized before `SetStateString`. Dynamic clocks may be refreshed at the view cadence. Call `StateChanged` once after a complete projection rather than exposing a partially updated panel.

Core scalar states are:

| GUI state | Source |
| --- | --- |
| `match_surface_available` | Accepted recipient view is usable |
| `match_phase` | Localized lifecycle phase and round |
| `match_status_lines` | Newline-separated localized values: lifecycle, retained last result when present, readiness during warmup/countdown, pause, recipient role/side, timeout budgets. Personal readiness appears only for active recipients before play. Omitted readiness does not leave a blank line. |
| `match_follow_visible` | Current local entity is an in-game human spectator; excludes fake clients, bots, demo/repeater playback and invalid local slots. Updated independently of the accepted-view revision. |
| `match_ready_action` | `#str_41713` or `#str_41714` from `recipient.ready` |
| `match_action_side_*` | One explicit, model-authorized side selector shared by team ready, timeout, lock and forfeit; Marine/Strogg in team modes and Contestant A/B in Duel |
| `match_team_lock_action` | `#str_41734` or `#str_41735` for the currently selected side |
| `match_broadcaster_control_visible` | `1` only for the local authoritative listen-server operator; this control does not disclose private operator state to remote recipients |
| `match_broadcaster_action` | `#str_41795` (grant) or `#str_41796` (revoke), selected solely from the selected participant row's structured public-role mask |
| `match_referee_authenticated` | Recipient has the referee public role |
| `match_global_proposal` | Localized global proposal summary or localized empty state |
| `match_side_proposal` | Localized recipient-authorized side proposal summary or localized empty state |
| `match_proposal_scope_choice` | Fixed `global` or `side` selector for choosing a current proposal ID; it is never serialized |
| `match_rules_summary` | Committed profile, revision/digest, customization and frozen/open boundary |
| `match_staged_summary` | Recipient-authorized staged revision/digest and changed-field count |
| `match_series_summary` | State, best-of, score, current/next map, and veto turn |
| `match_evidence_summary` | Evidence policy/status, final report qpath status, and MVD status; never claim a file exists until persistence reports success |
| `match_result_message` | Localized latest authoritative result, including rejection/no-change/pending/committed status; empty until a result belongs to the accepted session |

## Terminal match outcomes

The schema-4 `publicState.terminalResult` is a frozen public match outcome,
independent of optional evidence recording. Its outcome, reason, result revision,
winner identity and bounded UTF-8 display name survive review, nextgame and the
following warmup. The next countdown or session reset clears it. Team winners
carry the gameplay team; individual winners carry their historical participant
identity and name. Current scores, ranks, names and roster membership cannot
reconstruct that historical result.

The summary publishes `summary_result_visible`, `summary_result_outcome` and
localized `summary_result_text`. Accepted views arriving after the summary opens
refresh the banner and team headings. Aborted, drawn and pending results keep
neutral headings; a forfeit identifies the actual winner without changing the
raw score rows. Session or recipient-binding replacement clears the presentation.
Personal victory audio requires the frozen winner identity to match the current
accepted recipient during review, and runs once per session/result revision.
Late joiners and mutable team membership do not establish a personal loss or win.

The complete encoded view is capped at 7,936 bytes, with the maximum legal native
fixture using 7,728 bytes. The outer reliable-message buffer is 8,192 bytes.
Clients and servers must use matching schema-4 builds.

The managed summary reserves a three-line, wrapped result banner so a bounded
wide winner name cannot hide the separate forfeit notice. Its vertical offset
retains ten 22-pixel ranking rows and the existing chat-entry and footer controls.
Chat remains scrollable beneath the scores.

The Status page's Previous, Next and Free camera buttons emit fixed
`follow_prev`, `follow_next` and `follow_free` tokens. A live-player check gates
the local presentation and request entry, while the ordinary owner-checked
spectator event enforces camera permissions on the server. GUI text and role
labels never select or authorize a target. The console's direct-slot request
also includes the target spawn identity so replacement occupants cannot inherit
an in-flight follow request.

`match_referee_credential` is local input only. Never populate it from a view or result. Copy it into the authentication request through the secure challenge/proof path, then clear the GUI state and wipe every temporary buffer on success or failure. The GUI also clears it when the panel closes.

## Operation availability

Every button consumes the matching `mpMatchViewOperationAvailability` decision. For each prefix below, set `match_op_<prefix>_available` to `1` only for `MP_MATCH_PROTOCOL_REASON_OK`, and set `match_op_<prefix>_reason` to the decision's localized reason. Do not infer availability from role masks in the UI adapter.

Both hover and focus publish that reason into the persistent feedback field.
Reason and result fields wrap and scroll without overlapping the global Back
control; do not reduce them to a clipped single-line label.

| Prefix | Opcode |
| --- | --- |
| `ready_set` | `MP_MATCH_OP_READY_SET` |
| `team_ready_set` | `MP_MATCH_OP_TEAM_READY_SET` |
| `force_ready` | `MP_MATCH_OP_FORCE_READY` |
| `team_join` | `MP_MATCH_OP_TEAM_JOIN` |
| `team_lock_set` | `MP_MATCH_OP_TEAM_LOCK_SET` |
| `queue_join` / `queue_defer` / `queue_leave` | Corresponding queue opcode |
| `roster_leave` | `MP_MATCH_OP_ROSTER_LEAVE` |
| `timeout_request` / `tech_pause_request` / `resume_request` | Corresponding pause opcode |
| `ref_authenticate` / `ref_logout` | Corresponding referee opcode |
| `rules_select_profile` / `rules_stage_field` / `rules_commit` / `rules_discard` | Corresponding rules opcode |
| `proposal_create` / `proposal_cast` / `proposal_cancel` | Corresponding proposal opcode |
| `roster_invite` / `roster_accept` / `roster_remove` / `roster_substitute` / `role_assign` | Corresponding roster opcode |
| `broadcaster_set` | `MP_MATCH_OP_BROADCASTER_SET` |
| `series_stage_profile` / `series_start` / `series_cancel` / `series_advance` | Corresponding series opcode |
| `veto_select` | `MP_MATCH_OP_VETO_SELECT` |
| `forfeit` / `abort` | Corresponding match-result opcode |

An unavailable button remains visible, uses reduced opacity, does not emit an operation command, and exposes its exact localized denial in the persistent Action line.

Every operation with descriptor-advertised confirmation passes through the modal `match_confirm` presentation state before its fixed token is emitted: `FORCE_READY`, `RULES_COMMIT`, `ROSTER_REMOVE`, `ROSTER_SUBSTITUTE`, `SERIES_CANCEL`, `FORFEIT`, and `ABORT`. Escape, menu activation, panel exit, and Cancel clear that state. The adapter must still apply the same current-view, availability, selection, and compare-and-swap checks when the confirmed token arrives; the modal grants no authority.

## Lists and selection safety

Populate each list with the normal `name_item_N` state convention, delete the first unused item after refresh, clamp or clear `name_sel_0`, and retain a parallel bounded C++ row model built from the current accepted view. A selection index addresses that model only; do not parse tab-separated display text.

| List | Required row model |
| --- | --- |
| `match_team_rows` | Public participants plus recipient-authorized roster seats, queue entries, and invitations; stable participant/invitation/side/seat identifiers |
| `match_replacement_rows` | Bounded candidate superset: connected human participants with no roster seat, plus connected inactive same-side persistent substitute seats; invitation and substitution apply their stricter command-specific checks before submission |
| `match_proposal_rows` | Legal proposable operation template with typed nested arguments and scope |
| `match_profile_rows` | Supported rules profile ID plus its stable protocol key for the current game type |
| `match_rule_rows` | Rule field ID, type, committed value, optional staged value, and editability |
| `match_series_map_rows` | Full series pool index and map token from the accepted view |
| `match_series_history_rows` | Read-only applied veto and map-attempt history |
| `match_evidence_rows` | Read-only bounded evidence summaries; no hidden server-only observer data |

`match_role_choice` stores only protocol roster-role values 1 through 4 from the fixed choice list. `match_proposal_scope_choice` stores only `global` or `side`; the adapter uses it to select the current view's global proposal ID or recipient-authorized own-side proposal ID. Proposal scope is never serialized for cast or cancellation: the server resolves it from `MP_MATCH_ARG_PROPOSAL_ID`. Every currently exposed proposal-creation template is global and omits all targets; adding a targeted proposable descriptor requires an explicit schema, model, UI, and authorization review rather than inferring a target from the scope selector. `match_rule_value` is parsed according to the selected rule's declared type and descriptor range. `match_series_profile_choice` accepts only the three fixed profile tokens. Invalid, absent, stale, ineligible, or out-of-range selections produce `#str_41772` locally and submit nothing.

## Fixed command mapping

Selection and refresh tokens never send a network operation: `refresh`, `select_team_row`, `select_replacement_row`, `select_proposal_row`, `select_profile_row`, `select_rule_row`, `select_series_map`, `action_side_a`, and `action_side_b`. The last two update the typed action-side choice only; neutral referees/operators must choose explicitly, while contestants cannot select an opponent side.

Operation tokens map exactly to the canonical descriptors and argument schema:

| Token | Canonical descriptor | Exact typed request |
| --- | --- | --- |
| `ready_toggle` | `ready_set` | No target; required `enabled` is the inverse of `recipient.ready` |
| `team_ready_toggle` | `team_ready_set` | Current model-authorized action `teamTarget`; required `enabled` is the inverse of projected team readiness |
| `force_ready` | `force_ready` | No target; required `enabled = true`; optional reason omitted; confirmed |
| `team_join_marine` / `team_join_strogg` / `team_spectate` | `team_join` | Required fixed `teamTarget` is `MARINE`, `STROGG`, or `SPECTATOR`; no arguments |
| `team_lock_toggle` | `team_lock_set` | Selected playable `teamTarget`; required `enabled` is the inverse projected lock state |
| `queue_join` | `queue_join` | No target and no arguments; queue admission is side-neutral and the authoritative admission path selects a playable team |
| `queue_defer` / `queue_leave` | `queue_defer` / `queue_leave` | No target and no arguments |
| `roster_leave` | `roster_leave` | No target and no arguments; only the recipient's inactive coach or substitute seat can be withdrawn |
| `timeout` | `timeout_request` | Current model-authorized action `teamTarget`; no arguments |
| `tech_pause` | `tech_pause_request` | Recipient's playable `teamTarget` when applicable; required bounded printable `reason` uses an adapter-owned canonical value |
| `resume` | `resume_request` | No target and no arguments |
| `referee_login` / `referee_logout` | `ref_authenticate` / `ref_logout` | Challenge/proof credential or no arguments; never submit a raw stored credential |
| `rules_select_profile` | `rules_select_profile` | Required `profile` is the selected stable profile key, not a localized label or enum ordinal |
| `rules_stage_field` | `rules_stage_field` | Required stable `setting_id` plus a descriptor-validated typed scalar `setting_value` |
| `rules_commit` / `rules_discard` | `rules_commit` / `rules_discard` | No target and no arguments; commit is confirmed |
| `proposal_create` | `proposal_create` | Required `proposed_opcode` plus the selected adapter-owned template's descriptor-valid nested arguments; every current template is global and carries no participant or team target |
| `proposal_yes` / `proposal_no` / `proposal_abstain` | `proposal_cast` | Required current `proposal_id` selected through the fixed scope selector plus fixed `YES`, `NO`, or `ABSTAIN` ballot; no scope/team target |
| `proposal_cancel` | `proposal_cancel` | Required current `proposal_id` selected through the fixed scope selector; no scope/team target; available only to its proposer or projected referee/local-operator moderation authority |
| `roster_invite` | `roster_invite` | Selected eligible participant target, selected playable `teamTarget`, and optional role enum 1 through 4 |
| `roster_accept` | `roster_accept` | Required recipient-authorized `invitation_id`; no target |
| `roster_remove` | `roster_remove` | Selected roster participant target plus matching playable `teamTarget`; no arguments; confirmed |
| `roster_substitute` | `roster_substitute` | Selected outgoing roster participant target plus matching playable `teamTarget` and required `MP_MATCH_ARG_REPLACEMENT_PARTICIPANT`; inherits the outgoing seat's role and ignores `match_role_choice`; confirmed |
| `role_assign` | `role_assign` | Selected participant target plus matching playable `teamTarget` and required role enum 1 through 4, including substitute |
| `broadcaster_set` | `broadcaster_set` | Local operator only; selected connected human, inactive, neutral, unrostered participant target; no team target; required `MP_MATCH_ARG_ENABLED` bool grants from exactly Player or revokes from exactly Broadcaster according to the structured public-role mask |
| `series_stage` | `series_stage_profile` | Required exact key `best_of_one`, `best_of_three`, or `best_of_five`, with matching optional `best_of` value 1, 3, or 5 |
| `series_start` / `series_advance` | `series_start` / `series_advance` | No target and no arguments |
| `series_cancel` | `series_cancel` | No target; optional reason omitted; confirmed |
| `veto_ban` / `veto_pick` / `veto_decider` | `veto_select` | Required fixed veto action plus selected stable map token; no starting-side argument |
| `veto_side_marine` / `veto_side_strogg` | `veto_select` | Required `SIDE` action, selected stable map token, and fixed `MARINE` or `STROGG` starting-side argument |
| `forfeit` | `forfeit` | Current model-authorized gameplay side, or stable competition side for Duel; ordinary contestants remain own-side-only and neutral authorities require an explicit choice; optional reason omitted; confirmed |
| `abort` | `abort` | No target; required bounded printable `reason` uses an adapter-owned canonical value; confirmed |

Every request is built from the current accepted view and carries its session ID, session revision, aggregate control revision, recipient participant ID, slot, and binding generation. Recheck selection and operation availability immediately before `SubmitMatchOperation`; the server remains authoritative and rejects stale compare-and-swap state.
