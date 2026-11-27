// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
// VR at the session (Session_vr.cpp; roadmap VR-08 and VR-09): the one-time
// question at the title when an OpenXR runtime is installed and VR Mode is
// off, the notices of a headset found after launch and of vr_detect, and VR
// SETUP, the comfort dialog the first VR entry opens over the title or the
// pause.

class idUserInterface;
class idCmdArgs;

// Every menu frame, with the menus' notices.
void Session_UpdateVR( void );
// A message box's fired answer: true when it was the VR question's
// (vrOfferYes or vrOfferNo), which this applies.
bool Session_VROfferAnswer( const char *fired );
// The retained menus' VR verbs from any page: vrRecenter, vrCalibrateHeight
// and vrDetect. True when `request` was one of them.
bool Session_VRRequest( idUserInterface *gui, const char *request );
// A notice's Play in VR: Automatic plays in VR on the offered headset.
void Session_VRTakeOffer( void );
// vr_offer [status|yes|no|open|reset]
void Session_VROffer_f( const idCmdArgs &args );
// vr_setup: VR SETUP over the title or the pause that shows
void Session_VRSetup_f( const idCmdArgs &args );
