// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.

/*
===============================================================================

	VR at the session (SessionVR.h; roadmap VR-08 and VR-09).

	The one-time question. When stage one of headset detection found an
	OpenXR runtime (VR_RuntimeInstalled, which reads it without loading it),
	VR Mode is off and the player never answered (vr_offerAnswered 0), the
	title asks once, after its intro, in the session's message box: play in
	VR by itself whenever a headset is connected? YES sets VR Mode Automatic,
	whose look for a headset starts at once; NO keeps it off. Either answer is
	kept, and the question never returns; a player who launched with VR Mode
	on or Automatic already chose.

	A headset that Automatic VR Mode finds after it settled on the desktop is
	a notice in the menus' stack, kept until it is taken or VR Mode changes,
	whose Play in VR (vr_start) takes it; play is never switched to VR in the
	middle of a fight. Look for Headset (vr_detect) answers in a notice too.

	VR SETUP. The first time VR presents (vr_setupDone 0), the title or the
	pause that shows next opens the setup dialog over itself (retained_vr.py,
	vrSetupModalShow): play space, height, recentre, turning, weapon hand and
	the comfort vignette. The pause's VR SETUP link, the Virtual Reality
	tab's Comfort Setup and vr_setup open it again.

	Each outcome is logged with a VR_OFFER or VR_SETUP line for support.

===============================================================================
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Session_local.h"
#include "SessionVR.h"
#include "VRSystemLocal.h"

#ifndef ID_DEDICATED
namespace {

const char *const OFFER_YES = "vrOfferYes";
const char *const OFFER_NO = "vrOfferNo";
const int OFFER_SETTLE_MSEC = 1500;		// the title rests this long before it asks

struct vrSessionState_t {
	bool			offerAsked = false;		// asked this run, or found it not due
	bool			offerShowing = false;	// the box stands
	int				titleSince = -1;		// when the title came to rest with nothing over it
	bool			offerNotice = false;	// the headset notice stands
	bool			setupPending = false;	// VR presented with the setup not yet shown
	bool			wasActive = false;
};
vrSessionState_t vrState;

// Why the question is not asked; NULL when it is due.
const char *OfferBlocked( void ) {
#if defined( __ANDROID__ )
	return "a standalone headset build plays in VR from its launcher";
#else
	if ( vr_offerAnswered.GetBool() ) {
		return "answered";
	}
	if ( VR_Mode() != VR_MODE_OFF ) {
		return "VR Mode is already on or automatic";
	}
	if ( !VR_RuntimeInstalled() ) {
		return "no OpenXR runtime is installed";
	}
	return NULL;
#endif
}

void ShowOffer( void ) {
	const idLangDict *strings = common->GetLanguageDict();
	const idStr runtime = VR_RuntimeName();
	const idStr message = va( strings->GetString( "#str_233508" ), runtime.Length() > 0 ? runtime.c_str() : "OpenXR" );
	common->Printf( "VR_OFFER show runtime=\"%s\"\n", runtime.c_str() );
	vrState.offerAsked = true;
	vrState.offerShowing = true;
	sessLocal.MessageBox( MSG_YESNO, message.c_str(), strings->GetString( "#str_230120" ), false, OFFER_YES, OFFER_NO );
}

void Answer( bool yes, const char *how ) {
	vrState.offerShowing = false;
	vr_offerAnswered.SetBool( true );
	if ( yes ) {
		// Automatic looks for a headset at once and plays in VR when one answers
		vr_enable.SetInteger( VR_MODE_AUTOMATIC );
	}
	common->Printf( "VR_OFFER answer=%s by=%s mode=%d\n", yes ? "yes" : "no", how, VR_Mode() );
	cvarSystem->SetModifiedFlags( CVAR_ARCHIVE );
}

// The title or the pause showing, the retained home over the legacy menu.
idUserInterface *RetainedHome( void ) {
	return sessLocal.guiActive != NULL && sessLocal.guiActive == sessLocal.guiMainMenu ? sessLocal.guiRetainedHome : NULL;
}

bool OpenSetup( const char *reason ) {
	idUserInterface *home = RetainedHome();
	if ( home == NULL || sessLocal.guiMsgRestore != NULL || console->Active() ) {
		return false;
	}
	home->HandleNamedEvent( "vrSetupModalShow" );
	if ( !vr_setupDone.GetBool() ) {
		vr_setupDone.SetBool( true );
		cvarSystem->SetModifiedFlags( CVAR_ARCHIVE );
	}
	common->Printf( "VR_SETUP shown reason=%s\n", reason );
	return true;
}

} // namespace
#endif

void Session_UpdateVR( void ) {
#ifndef ID_DEDICATED
	const int now = common->GetPresentationTime();

	// The one-time question, on the title at rest: no level, no box, no
	// intro, no console, nothing over the home.
	const bool atTitle = !sessLocal.mapSpawned && sessLocal.guiMainMenu != NULL && sessLocal.guiActive == sessLocal.guiMainMenu &&
		sessLocal.guiMsgRestore == NULL && !sessLocal.IsMainMenuIntroPlaying() && !console->Active();
	if ( !atTitle ) {
		vrState.titleSince = -1;
	} else if ( vrState.titleSince < 0 ) {
		vrState.titleSince = now;
	}
	if ( !vrState.offerAsked && atTitle && now - vrState.titleSince >= OFFER_SETTLE_MSEC ) {
		const char *blocked = OfferBlocked();
		if ( blocked != NULL ) {
			vrState.offerAsked = true;
			common->Printf( "VR_OFFER skip reason=\"%s\"\n", blocked );
		} else {
			ShowOffer();
		}
	}
	// A box that went away without an answer (the session took it down).
	if ( vrState.offerShowing && sessLocal.guiActive != sessLocal.guiMsg ) {
		vrState.offerShowing = false;
	}

	// A headset that Automatic found after launch, offered until taken.
	if ( VR_ConsumeHeadsetOffer() ) {
		sessLocal.PushRetainedNotice( "#str_233509", RETAINED_NOTICE_VR_HEADSET, "#str_233504", "", RETAINED_NOTICE_UNDO_VR_START, true );
		vrState.offerNotice = true;
		common->Printf( "VR: a headset is offered in the menus (Play in VR, or vr_start)\n" );
	}
	if ( vrState.offerNotice && !VR_HeadsetOffered() ) {
		sessLocal.ResolveRetainedNotices( RETAINED_NOTICE_VR_HEADSET );
		vrState.offerNotice = false;
	}
	// vr_detect's answer, as a notice.
	const int detected = VR_ConsumeDetectResult();
	if ( detected >= 0 ) {
		static const char *const keys[] = { "#str_233510", "#str_233511", "#str_233512" };
		if ( detected != VR_DETECT_HEADSET || VR_Mode() == VR_MODE_OFF ) {
			sessLocal.PushRetainedNotice( keys[ idMath::ClampInt( 0, 2, detected ) ], RETAINED_NOTICE_VR_HEADSET, "", "",
				RETAINED_NOTICE_UNDO_NONE, false );
		}
	}

	// VR SETUP at the first VR entry, over the next title or pause.
	const bool active = vrSystem != NULL && vrSystem->IsActive();
	if ( active && !vrState.wasActive && !vr_setupDone.GetBool() ) {
		vrState.setupPending = true;
	}
	vrState.wasActive = active;
	if ( vrState.setupPending && ( !active || vr_setupDone.GetBool() ) ) {
		vrState.setupPending = false;
	}
	if ( vrState.setupPending && OpenSetup( "first-entry" ) ) {
		vrState.setupPending = false;
	}
#endif
}

bool Session_VROfferAnswer( const char *fired ) {
#ifndef ID_DEDICATED
	if ( fired == NULL || ( idStr::Icmp( fired, OFFER_YES ) && idStr::Icmp( fired, OFFER_NO ) ) ) {
		return false;
	}
	Answer( !idStr::Icmp( fired, OFFER_YES ), "box" );
	return true;
#else
	(void)fired;
	return false;
#endif
}

bool Session_VRRequest( idUserInterface *gui, const char *request ) {
#ifndef ID_DEDICATED
	(void)gui;
	if ( request == NULL ) {
		return false;
	}
	if ( !idStr::Icmp( request, "vrRecenter" ) ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "vr_recenter\n" );
		return true;
	}
	if ( !idStr::Icmp( request, "vrCalibrateHeight" ) ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "vr_calibrateHeight\n" );
		return true;
	}
	if ( !idStr::Icmp( request, "vrDetect" ) ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "vr_detect\n" );
		return true;
	}
#else
	(void)gui; (void)request;
#endif
	return false;
}

void Session_VRTakeOffer( void ) {
#ifndef ID_DEDICATED
	VR_TakeHeadsetOffer();
#endif
}

void Session_VROffer_f( const idCmdArgs &args ) {
#ifndef ID_DEDICATED
	const char *verb = args.Argc() > 1 ? args.Argv( 1 ) : "status";
	if ( !idStr::Icmp( verb, "yes" ) || !idStr::Icmp( verb, "no" ) ) {
		if ( !vrState.offerShowing || sessLocal.guiActive != sessLocal.guiMsg ) {
			common->Printf( "vr_offer: the question is not showing\n" );
			return;
		}
		// as the box's button would: the box closes, then the answer applies
		sessLocal.StopBox();
		Answer( !idStr::Icmp( verb, "yes" ), "console" );
		return;
	}
	if ( !idStr::Icmp( verb, "open" ) ) {
		if ( sessLocal.mapSpawned || sessLocal.guiActive != sessLocal.guiMainMenu || sessLocal.guiMsgRestore != NULL ) {
			common->Printf( "vr_offer: the question opens only over the title\n" );
			return;
		}
		ShowOffer();
		return;
	}
	if ( !idStr::Icmp( verb, "reset" ) ) {
		vr_offerAnswered.SetBool( false );
		vrState.offerAsked = false;
		common->Printf( "vr_offer: the question will be asked at the title again\n" );
		return;
	}
	const char *blocked = OfferBlocked();
	common->Printf( "VR_OFFER status due=%d asked=%d showing=%d answered=%d runtime=%d mode=%d%s%s\n", blocked == NULL ? 1 : 0,
		vrState.offerAsked ? 1 : 0, vrState.offerShowing ? 1 : 0, vr_offerAnswered.GetBool() ? 1 : 0, VR_RuntimeInstalled() ? 1 : 0,
		VR_Mode(), blocked != NULL ? " because=" : "", blocked != NULL ? blocked : "" );
#else
	(void)args;
#endif
}

void Session_VRSetup_f( const idCmdArgs &args ) {
	(void)args;
#ifndef ID_DEDICATED
	if ( !OpenSetup( "command" ) ) {
		common->Printf( "vr_setup: VR SETUP opens over the retained title or pause menu\n" );
	}
#endif
}
