/*
===========================================================================

openQ4 arena campaign

Framework-side orchestration for the single-player arena ladder.  The actual
matches run in game_mp; this class owns campaign data, menu state, progression,
listen-server setup, bot population, and the authoritative result handoff.

===========================================================================
*/

#ifndef __ARENA_CAMPAIGN_H__
#define __ARENA_CAMPAIGN_H__

class idUserInterface;
class idCmdArgs;

struct arenaCampaignState_t;

class idArenaCampaign {
public:
							idArenaCampaign();
							~idArenaCampaign();

	void					Init();
	void					Shutdown();
	void					Frame();

	bool					IsGui( const idUserInterface *gui ) const;
	bool					IsActive() const;
	bool					NeedsCleanup() const;
	// The entrance holds the browser: its commands wait until the launch.
	bool					IsEntering() const;

	void					OpenSelector();
	void					OpenBrowser();
	void					CloseMenu();
	bool					HandleGuiCommand( const char *menuCommand );
	void					UpdateMainMenuGui( idUserInterface *gui );
	// The levelshot of the match the browser would open on; empty with no
	// campaign loaded.
	idStr					SelectedLevelshot();

	// Called by SpawnServer on both sides of a game-module reload. Returns
	// false only when an Arena launch was active but can no longer be prepared.
	bool					PrepareServer();
	void					PopulateBots();

	// Presents the shared "match aborted" report for a launch that failed
	// outside this class, before any listen server existed.
	void					ReportStartFailure();

	// The live match's one-based token, or 0. Read it before AbortMatch, which
	// clears it.
	int						ActiveMatchToken() const;
	// After the player leaves a match in progress, reopens the Arena browser on
	// a "match abandoned" report that names the match and offers it again,
	// instead of dropping them on the title screen with nothing said.
	void					ReportAbandoned( int token );
	// The pause menu's Restart match: the live match again, recording nothing.
	// It restarts in place on the running server when the authored roster is
	// intact; otherwise it leaves the match and starts the same token again
	// from its entrance.
	void					RestartMatch();

	// Queues the narrow, strictly parsed game_mp -> framework result handoff.
	// Outcome values are 0 = loss, 1 = win, 2 = draw, and 3 = aborted (the
	// opponents left or the match was forfeited; nothing is recorded).
	bool					QueueCompletion( const idCmdArgs &args );
	void					AbortMatch();
	// A match left during its end-of-match ceremony keeps the result game_mp
	// already decided. FinishDecidedResult records it and reports it in the
	// browser (false when there is none, so the caller abandons the match as
	// before); RecordDecidedResult only records it, for a quit or an error drop
	// that tears the session down next. Both run before AbortMatch.
	bool					FinishDecidedResult();
	void					RecordDecidedResult();

private:
	void					AcceptCompletion( int token, int outcome, int playerScore, int opponentScore, int awards );
	bool					TakeDecidedResult();
	bool					RecordPendingCompletion();
	void					FinishPendingCompletion();
	bool					LoadCampaign();
	void					UpdateGui();
	void					PublishGui( idUserInterface *gui );
	idUserInterface *		PresentedGui() const;
	void					StartSelectedMatch();
	void					CommitPendingMatch();
	void					QueueFailedCompletionCleanup();
	void					RetryResult();
	bool					RestartMatchInPlace( int token );
	void					RequestResetProgress();
	void					CancelResetProgress();
	void					ResetProgress();
	void					DismissResult();
	void					ReturnToBrowserAfterStartFailure( int failure );

	arenaCampaignState_t *	state;

	friend void				ArenaCampaign_Finish_f( const idCmdArgs &args );
};

extern idArenaCampaign arenaCampaign;

#endif
