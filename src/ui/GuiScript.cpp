/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#include <unordered_set>

#include "Window.h"
#include "Winvar.h"
#include "GuiScript.h"
#include "UserInterfaceLocal.h"

idCVar gui_debugScript( "gui_debugScript", "0", CVAR_GUI | CVAR_INTEGER, "log gui script execution details (0=off, 1=key events, 2=verbose, 3=sets/transitions/cmds, 4=state changes, 5=redraw trace)" );

namespace {
	// openQ4: every live script list, for IsValid. This was an idList searched
	// from the front: Execute asks before every statement it runs, so each
	// statement cost a walk over every script list of every loaded GUI, and
	// each list's destruction another to remove itself. Never destroyed, so
	// lists torn down at exit still find it.
	std::unordered_set<const idGuiScriptList*> &GuiScriptLists( void ) {
		static std::unordered_set<const idGuiScriptList*> *lists = new std::unordered_set<const idGuiScriptList*>;
		return *lists;
	}
}

idGuiScriptList::idGuiScriptList() {
	list.SetGranularity( 4 );
	GuiScriptLists().insert( this );
}

idGuiScriptList::~idGuiScriptList() {
	GuiScriptLists().erase( this );
	list.DeleteContents( true );
}

bool idGuiScriptList::IsValid( const idGuiScriptList *list ) {
	if ( list == NULL ) {
		return false;
	}
	return GuiScriptLists().count( list ) != 0;
}


/*
=========================
Script_Set
=========================
*/
void Script_Set(idWindow *window, idList<idGSWinVar> *src) {
	idStr key, val;
	idWinStr *dest = dynamic_cast<idWinStr*>((*src)[0].var);
	if (dest) {
		if (idStr::Icmp(*dest, "cmd") == 0) {
			dest = dynamic_cast<idWinStr*>((*src)[1].var);
			int parmCount = src->Num();
			if (parmCount > 2) {
				val = dest->c_str();
				int i = 2;
				while (i < parmCount) {
					val += " \"";
					val += (*src)[i].var->c_str();
					val += "\"";
					i++;
				}
				window->AddCommand(val);
			} else {
				window->AddCommand(*dest);
			}
			if ( gui_debugScript.GetInteger() > 2 ) {
				common->Printf( "GUI: cmd \"%s\" (caller=%s gui=%s)\n",
					( parmCount > 2 ) ? val.c_str() : ( dest ? dest->c_str() : "<null>" ),
					window ? window->GetName() : "<null>",
					window && window->GetGui() ? window->GetGui()->GetSourceFile() : "<null>" );
			}
			return;
		} 
	}
	(*src)[0].var->Set((*src)[1].var->c_str());
	(*src)[0].var->SetEval(false);
	idWinFloatPtr* floatPtr = dynamic_cast<idWinFloatPtr*>((*src)[0].var);
	if ( floatPtr ) {
		idWinVec4* owner = floatPtr->GetOwnerVec4();
		if ( owner ) {
			owner->SetEval( false );
		}
	}
	if ( gui_debugScript.GetInteger() > 2 ) {
		const char *varName = (*src)[0].var ? (*src)[0].var->GetName() : "<null>";
		const char *varValue = (*src)[1].var ? (*src)[1].var->c_str() : "<null>";
		common->Printf( "GUI: set %s = %s (caller=%s gui=%s)\n",
			varName, varValue,
			window ? window->GetName() : "<null>",
			window && window->GetGui() ? window->GetGui()->GetSourceFile() : "<null>" );
	}
}

/*
=========================
Script_SetFocus
=========================
*/
void Script_SetFocus(idWindow *window, idList<idGSWinVar> *src) {
	idWinStr *parm = dynamic_cast<idWinStr*>((*src)[0].var);
	if (parm) {
		drawWin_t *win = window->GetGui()->GetDesktop()->FindChildByName(*parm);
		if (win && win->win) {
			window->SetFocus(win->win);
		}
	}
}

/*
=========================
Script_ShowCursor
=========================
*/
void Script_ShowCursor(idWindow *window, idList<idGSWinVar> *src) {
	idWinStr *parm = dynamic_cast<idWinStr*>((*src)[0].var);
	if ( parm ) {
		if ( atoi( *parm ) ) {
			window->GetGui()->GetDesktop()->ClearFlag( WIN_NOCURSOR );
		} else {
			window->GetGui()->GetDesktop()->SetFlag( WIN_NOCURSOR );
		}
	}
}

/*
=========================
Script_RunScript

 run scripts must come after any set cmd set's in the script
=========================
*/
void Script_RunScript(idWindow *window, idList<idGSWinVar> *src) {
	idWinStr *parm = dynamic_cast<idWinStr*>((*src)[0].var);
	if (parm) {
		idStr str = window->cmd;
		str += " ; runScript ";
		str += parm->c_str();
		window->cmd = str;
	}
}

/*
=========================
Script_LocalSound
=========================
*/
void Script_LocalSound(idWindow *window, idList<idGSWinVar> *src) {
	idWinStr *parm = dynamic_cast<idWinStr*>((*src)[0].var);
	if (parm) {
		session->sw->PlayShaderDirectly(*parm);
	}
}

/*
=========================
Script_SetLightColor
=========================
*/
void Script_SetLightColor(idWindow *window, idList<idGSWinVar> *src) {
	idWinVec4 *parm = dynamic_cast<idWinVec4*>((*src)[0].var);
	if (window && window->GetGui()) {
		window->GetGui()->SetLightColorVar(parm);
	}
}

/*
=========================
Script_EvalRegs
=========================
*/
void Script_EvalRegs(idWindow *window, idList<idGSWinVar> *src) {
	window->EvalRegs(-1, true);
}

/*
=========================
Script_EndGame
=========================
*/
void Script_EndGame( idWindow *window, idList<idGSWinVar> *src ) {
	cvarSystem->SetCVarBool( "g_nightmare", true );
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "disconnect\n" );
}

/*
=========================
Script_ResetTime
=========================
*/
void Script_ResetTime(idWindow *window, idList<idGSWinVar> *src) {
	idWinStr *parm = dynamic_cast<idWinStr*>((*src)[0].var);
	drawWin_t *win = NULL;
	if (parm && src->Num() > 1) {
		win = window->GetGui()->GetDesktop()->FindChildByName(*parm);
		parm = dynamic_cast<idWinStr*>((*src)[1].var);
	}
	if (win && win->win) {
		win->win->ResetTime(atoi(*parm));
		win->win->EvalRegs(-1, true);
		if (gui_debugScript.GetInteger() > 0) {
			common->Printf("GUI: resetTime window=%s time=%s (caller=%s gui=%s)\n",
				win->win->GetName(), parm ? parm->c_str() : "<null>", window ? window->GetName() : "<null>",
				window && window->GetGui() ? window->GetGui()->GetSourceFile() : "<null>");
		}
	} else {
		window->ResetTime(atoi(*parm));
		window->EvalRegs(-1, true);
		if (gui_debugScript.GetInteger() > 0) {
			common->Printf("GUI: resetTime window=%s time=%s (self)\n",
				window ? window->GetName() : "<null>", parm ? parm->c_str() : "<null>");
		}
	}
}

/*
=========================
Script_ResetCinematics
=========================
*/
void Script_ResetCinematics(idWindow *window, idList<idGSWinVar> *src) {
	window->ResetCinematics();
}

/*
=========================
GuiScript_ReadTransitionOperand

Retail Quake 4 snapshots a "$var" transition operand at parse time only when it
names a rect. Any other var is read here, when the transition starts, so a
fade can begin from the current colour or end on one set after the gui loaded.
=========================
*/
static bool GuiScript_ReadTransitionOperand( const idGSWinVar &parm, idVec4 &value ) {
	if ( parm.live == NULL ) {
		const idWinVec4 *literal = dynamic_cast<const idWinVec4 *>( parm.var );
		if ( literal == NULL ) {
			return false;
		}
		value = *literal;
		return true;
	}
	const idWinVec4 *vec4 = dynamic_cast<const idWinVec4 *>( parm.live );
	if ( vec4 != NULL ) {
		value = *vec4;
		return true;
	}
	// Other var types convert through their text, as the parse-time copy did.
	idWinVec4 parsed;
	parsed.Set( parm.live->c_str() );
	value = parsed;
	return true;
}

/*
=========================
Script_Transition
=========================
*/
void Script_Transition(idWindow *window, idList<idGSWinVar> *src) {
	// transitions always affect rect or vec4 vars
	if (src->Num() >= 4) {
		idWinRectangle *rect = NULL;
		idWinVec4 *vec4 = dynamic_cast<idWinVec4*>((*src)[0].var);
		// 
		//  added float variable
		idWinFloat* val = NULL;
		idWinFloatPtr* valp = NULL;
		// 
		if (vec4 == NULL) {
			rect = dynamic_cast<idWinRectangle*>((*src)[0].var);
			// 
			//  added float variable					
			if ( NULL == rect ) {
				val = dynamic_cast<idWinFloat*>((*src)[0].var);
			}
			// 
		}

		if (val == NULL) {
			valp = dynamic_cast<idWinFloatPtr*>((*src)[0].var);
		}

		idVec4 from = vec4_zero;
		idVec4 to = vec4_zero;
		const bool hasFrom = GuiScript_ReadTransitionOperand( (*src)[1], from );
		const bool hasTo = GuiScript_ReadTransitionOperand( (*src)[2], to );
		idWinStr *timeStr = dynamic_cast<idWinStr*>((*src)[3].var);
		// 
		//  added float variable					
		if (!((vec4 || rect || val || valp) && hasFrom && hasTo && timeStr)) {
			// 
			common->Warning("Bad transition in gui %s in window %s\n", window->GetGui()->GetSourceFile(), window->GetName());
			return;
		}
		int time = atoi(*timeStr);
		float ac = 0.0f;
		float dc = 0.0f;
		if (src->Num() > 4) {
			idWinStr *acv = dynamic_cast<idWinStr*>((*src)[4].var);
			idWinStr *dcv = dynamic_cast<idWinStr*>((*src)[5].var);
			assert(acv && dcv);
			ac = atof(*acv);
			dc = atof(*dcv);
		}
				
		if (vec4) {
			vec4->SetEval(false);
			window->AddTransition(vec4, from, to, time, ac, dc);
			// 
			//  added float variable					
		} else if ( val ) {
			val->SetEval ( false );
			window->AddTransition(val, from, to, time, ac, dc);
			// 
		}
		else if (valp) {
			valp->SetEval(false);
			idWinVec4* owner = valp->GetOwnerVec4();
			if ( owner ) {
				owner->SetEval( false );
			}
			window->AddTransition(valp, from, to, time, ac, dc);
			// 
		} else {
			rect->SetEval(false);
			window->AddTransition(rect, from, to, time, ac, dc);
		}
		window->StartTransition();
		if ( gui_debugScript.GetInteger() > 2 ) {
			const char *varName = (*src)[0].var ? (*src)[0].var->GetName() : "<null>";
			common->Printf( "GUI: transition %s from=%s to=%s time=%d ac=%.3f dc=%.3f (caller=%s gui=%s)\n",
				varName,
				from.ToString(),
				to.ToString(),
				time, ac, dc,
				window ? window->GetName() : "<null>",
				window && window->GetGui() ? window->GetGui()->GetSourceFile() : "<null>" );
		}
	}
}

/*
=========================
Script_EvalRegs
=========================
*/
// jmarshall - Quake 4 gui implementation
void Script_NamedEvent(idWindow* window, idList<idGSWinVar>* src) {
	idWinStr* parm = dynamic_cast<idWinStr*>((*src)[0].var);
	idStr parmStr = parm->c_str();

	if (gui_debugScript.GetInteger() > 1) {
		common->Printf("GUI: namedEvent '%s' (caller=%s gui=%s)\n",
			parm ? parm->c_str() : "<null>",
			window ? window->GetName() : "<null>",
			window && window->GetGui() ? window->GetGui()->GetSourceFile() : "<null>");
	}

	int p = idStr::FindText(parm->c_str(), "::");
	if (p <= 0)
	{
		if ( window && window->GetGui() ) {
			window->GetGui()->HandleNamedEvent( parm->c_str() );
		} else if ( window ) {
			window->RunNamedEvent( parm->c_str() );
		}
	}
	else
	{
		idStr windowName = parmStr.Mid(0, p);
		idStr varName = parmStr.Mid(p + 2, parmStr.Length() - (p + 2));

		drawWin_t* childWindow = NULL;
		if ( window && window->GetGui() && window->GetGui()->GetDesktop() ) {
			childWindow = window->GetGui()->GetDesktop()->FindChildByName( windowName );
		} else if ( window ) {
			childWindow = window->FindChildByName( windowName );
		}
		if (childWindow)
		{
			childWindow->win->RunNamedEvent(varName);
		}
		else
		{
			if ( cvarSystem->GetCVarBool( "gui_debug" ) ) {
				common->Warning("GUI: %s: unknown window %s for named event %s\n", window->GetName(), windowName.c_str(), varName.c_str());
			}
		}
	}
}

/*
=========================
Script_StopTransitions
=========================
*/
void Script_StopTransitions(idWindow* window, idList<idGSWinVar>* src) {
	idWinStr* parm = dynamic_cast<idWinStr*>((*src)[0].var);
	idStr parmStr = parm->c_str();

	drawWin_t* childWindow = window->FindChildByName(parmStr);
	if (childWindow)
	{
		childWindow->win->ClearTransitions();
		if (gui_debugScript.GetInteger() > 1) {
			common->Printf("GUI: stopTransitions window=%s (caller=%s gui=%s)\n",
				childWindow->win ? childWindow->win->GetName() : "<null>",
				window ? window->GetName() : "<null>",
				window && window->GetGui() ? window->GetGui()->GetSourceFile() : "<null>");
		}
	}
}

/*
=========================
Script_ConsoleCmd
=========================
*/
void Script_ConsoleCmd(idWindow* window, idList<idGSWinVar>* src) {
	idWinStr* parm = dynamic_cast<idWinStr*>((*src)[0].var);
	idStr parmStr = parm->c_str();

	cmdSystem->BufferCommandText(CMD_EXEC_NOW, parmStr.c_str());
	if ( gui_debugScript.GetInteger() > 2 ) {
		common->Printf( "GUI: consoleCmd \"%s\" (caller=%s gui=%s)\n",
			parmStr.c_str(),
			window ? window->GetName() : "<null>",
			window && window->GetGui() ? window->GetGui()->GetSourceFile() : "<null>" );
	}
}

/*
===================
Script_ResetVideo
===================
*/
void Script_ResetVideo(idWindow* window, idList<idGSWinVar>* src) {
	idWinStr* parm = dynamic_cast<idWinStr*>((*src)[0].var);
	idStr parmStr = parm->c_str();
	drawWin_t* childWindow = window->FindChildByName(parmStr);
	if (childWindow)
	{
		if (childWindow->win)
		{
			childWindow->win->ResetCinematics();
			childWindow->win->EvalRegs(-1, true);
		}
		else
		{
			childWindow->simp->ResetCinematics();
		}
	}
	else
	{
		window->ResetCinematics();
		window->EvalRegs(-1, true);
	}
}

/*
===================
Script_VideoCompleteEvent
===================
*/
void Script_VideoCompleteEvent( idWindow *window, idList<idGSWinVar> *src ) {
	idWinStr *windowName = dynamic_cast<idWinStr *>( (*src)[0].var );
	idWinStr *eventName = dynamic_cast<idWinStr *>( (*src)[1].var );

	if ( windowName == NULL || eventName == NULL || window == NULL || window->GetGui() == NULL ) {
		return;
	}

	drawWin_t *childWindow = NULL;
	idWindow *desktop = window->GetGui()->GetDesktop();
	if ( desktop ) {
		childWindow = desktop->FindChildByName( windowName->c_str() );
	}
	if ( childWindow == NULL ) {
		childWindow = window->FindChildByName( windowName->c_str() );
	}
	if ( childWindow == NULL ) {
		return;
	}

	bool completed = false;
	if ( childWindow->win ) {
		completed = childWindow->win->IsBackgroundCinematicComplete();
	} else if ( childWindow->simp ) {
		completed = childWindow->simp->IsBackgroundCinematicComplete();
	}

	if ( completed ) {
		window->GetGui()->HandleNamedEvent( eventName->c_str() );
	}
}

/*
===================
Script_NonInteractive
===================
*/
void Script_NonInteractive(idWindow* window, idList<idGSWinVar>* src) {
	idWinStr* parm = dynamic_cast<idWinStr*>((*src)[0].var);
	int val = atoi(parm->c_str());

	// Quake 4 GUI scripts use "nonInteractive 1" to disable input.
	window->GetGui()->SetInteractive( val == 0 );
}
// jmarshall end

typedef struct {
	const char *name;
	void (*handler) (idWindow *window, idList<idGSWinVar> *src);
	int mMinParms;
	int mMaxParms;
} guiCommandDef_t;

guiCommandDef_t commandList[] = {
	{ "set", Script_Set, 2, 999 },
	{ "setFocus", Script_SetFocus, 1, 1 },
	{ "endGame", Script_EndGame, 0, 0 },
	{ "resetTime", Script_ResetTime, 0, 2 },
	{ "showCursor", Script_ShowCursor, 1, 1 },
	{ "resetCinematics", Script_ResetCinematics, 0, 2 },
	{ "transition", Script_Transition, 4, 6 },
	{ "localSound", Script_LocalSound, 1, 1 },
	{ "setlightcolor", Script_SetLightColor, 1, 1 },
	{ "runScript", Script_RunScript, 1, 1 },
	{ "evalRegs", Script_EvalRegs, 0, 0 },
// jmarshall - Quake 4 gui implementation
	{ "namedevent", Script_NamedEvent, 1, 1},
	{ "stoptransitions", Script_StopTransitions, 1, 1},
	{ "consolecmd", Script_ConsoleCmd, 1, 1},
	{ "resetVideo", Script_ResetVideo, 1, 1},
	{ "videoCompleteEvent", Script_VideoCompleteEvent, 2, 2},
	{ "nonInteractive", Script_NonInteractive, 1, 1}
// jmarshall end
};

int	scriptCommandCount = sizeof(commandList) / sizeof(guiCommandDef_t);


/*
=========================
idGuiScript::idGuiScript
=========================
*/
idGuiScript::idGuiScript() {
	ifList = NULL;
	elseList = NULL;
	conditionReg = -1;
	handler = NULL;
	parms.SetGranularity( 2 );
}

/*
=========================
idGuiScript::~idGuiScript
=========================
*/
idGuiScript::~idGuiScript() {
	delete ifList;
	delete elseList;
	int c = parms.Num();
	for ( int i = 0; i < c; i++ ) {
		if ( parms[i].own ) {
			delete parms[i].var;
		}
	}
}

/*
=========================
idGuiScript::WriteToSaveGame
=========================
*/
void idGuiScript::WriteToSaveGame( idFile *savefile, const char *context ) {
	int i;
	if ( savefile == NULL ) {
		common->Error( "idGuiScript::WriteToSaveGame: NULL output file for %s", context );
	}

	if ( ifList ) {
		ifList->WriteToSaveGame( savefile, context );
	}
	if ( elseList ) {
		elseList->WriteToSaveGame( savefile, context );
	}

	if ( conditionReg < -1 || conditionReg >= MAX_EXPRESSION_REGISTERS ) {
		common->Error( "idGuiScript::WriteToSaveGame: invalid condition register %d for %s", conditionReg, context );
	}
	const int conditionOffset = savefile->Tell();
	const int conditionBytes = savefile->WriteInt( conditionReg );
	if ( conditionBytes != static_cast<int>( sizeof( conditionReg ) ) ) {
		common->Error( "idGuiScript::WriteToSaveGame: failed to write condition register at offset %d (%d of %d bytes)",
			conditionOffset, conditionBytes, static_cast<int>( sizeof( conditionReg ) ) );
	}

	for ( i = 0; i < parms.Num(); i++ ) {
		if ( parms[i].own ) {
			if ( parms[i].var == NULL ) {
				common->Error( "idGuiScript::WriteToSaveGame: owned parameter %d has no variable for %s", i, context );
			}
			// A script parameter is an evaluated GUI expression result and owns no name.
			// Zero the affected components rather than refusing the save over one.
			if ( !parms[i].var->IsFinite() ) {
				common->Warning( "idGuiScript::WriteToSaveGame: %s owned parameter %d holds a non-finite value; "
					"saving zero for the affected components", context, i );
				parms[i].var->SanitizeNonFinite();
			}
			parms[i].var->WriteToSaveGame( savefile );
		}
	}
}

/*
=========================
idGuiScript::ReadFromSaveGame
=========================
*/
void idGuiScript::ReadFromSaveGame( idFile *savefile ) {
	int i;
	if ( savefile == NULL ) {
		common->Error( "idGuiScript::ReadFromSaveGame: NULL input file" );
	}

	if ( ifList ) {
		ifList->ReadFromSaveGame( savefile );
	}
	if ( elseList ) {
		elseList->ReadFromSaveGame( savefile );
	}

	int savedConditionReg = -1;
	const int conditionOffset = savefile->Tell();
	const int conditionBytes = savefile->ReadInt( savedConditionReg );
	if ( conditionBytes != static_cast<int>( sizeof( savedConditionReg ) ) ) {
		common->Error( "idGuiScript::ReadFromSaveGame: truncated condition register at offset %d (%d of %d bytes)",
			conditionOffset, conditionBytes, static_cast<int>( sizeof( savedConditionReg ) ) );
	}
	if ( savedConditionReg < -1 || savedConditionReg >= MAX_EXPRESSION_REGISTERS ) {
		common->Error( "idGuiScript::ReadFromSaveGame: invalid saved condition register %d at offset %d",
			savedConditionReg, conditionOffset );
	}
	if ( conditionReg < -1 || conditionReg >= MAX_EXPRESSION_REGISTERS ) {
		common->Error( "idGuiScript::ReadFromSaveGame: invalid parsed condition register %d", conditionReg );
	}
	if ( savedConditionReg != conditionReg ) {
		common->Error( "idGuiScript::ReadFromSaveGame: saved condition register %d does not match parsed register %d; GUI script structure changed",
			savedConditionReg, conditionReg );
	}

	for ( i = 0; i < parms.Num(); i++ ) {
		if ( parms[i].own ) {
			if ( parms[i].var == NULL ) {
				common->Error( "idGuiScript::ReadFromSaveGame: owned parameter %d has no parsed variable", i );
			}
			parms[i].var->ReadFromSaveGame( savefile );
		}
	}
}

/*
=========================
idGuiScript::Parse
=========================
*/
bool idGuiScript::Parse(idParser *src) {
	int i;
	
	// first token should be function call
	// then a potentially variable set of parms
	// ended with a ;
	idToken token;
	if ( !src->ReadToken(&token) ) {
		src->Error( "Unexpected end of file" );
		return false;
	}

	handler	= NULL;
	
	for ( i = 0; i < scriptCommandCount ; i++ ) {
		if ( idStr::Icmp(token, commandList[i].name) == 0 ) {
			handler = commandList[i].handler;
			break;
		}
	}

	if (handler == NULL) {
		src->Error("Uknown script call %s", token.c_str());
	}
	// now read parms til ;
	// all parms are read as idWinStr's but will be fixed up later 
	// to be proper types
	while (1) {
		if ( !src->ReadToken(&token) ) {
			src->Error( "Unexpected end of file" );
			return false;
		}
		
		if (idStr::Icmp(token, ";") == 0) {
			break;
		}

		if (idStr::Icmp(token, "}") == 0) {
			src->UnreadToken(&token);
			break;
		}

		idWinStr *str = new idWinStr();
		*str = token;
		idGSWinVar wv;
		wv.own = true;
		wv.var = str;
		parms.Append( wv );
	}

	// 
	//  verify min/max params
	if ( handler && (parms.Num() < commandList[i].mMinParms || parms.Num() > commandList[i].mMaxParms ) ) {
		src->Error("incorrect number of parameters for script %s", commandList[i].name );
	}
	// 

	return true;
}

/*
=========================
idGuiScriptList::Execute
=========================
*/
void idGuiScriptList::Execute(idWindow *win) {
	// Script handlers can trigger GUI changes that mutate or even destroy this
	// list while we are iterating. Re-evaluate list validity/count each step.
	for ( int i = 0; ; i++ ) {
		if ( !idGuiScriptList::IsValid( this ) ) {
			return;
		}
		if ( i >= list.Num() ) {
			break;
		}

		idGuiScript *gs = list[i];
		if ( !gs ) {
			continue;
		}

		if ( gs->conditionReg >= 0 ) {
			if ( win->HasOps() ) {
				float f = win->EvalRegs( gs->conditionReg );
				if ( f ) {
					if ( gs->ifList ) {
						win->RunScriptList( gs->ifList );
					}
				} else if ( gs->elseList ) {
					win->RunScriptList( gs->elseList );
				}
			}
		}

		gs->Execute( win );
	}
}

/*
=========================
idGuiScriptList::FixupParms
=========================
*/
void idGuiScript::FixupParms(idWindow *win) {
	if (handler == &Script_Set) {
		bool precacheBackground = false;
		bool precacheSounds = false;
		idWinStr *str = dynamic_cast<idWinStr*>(parms[0].var);
		assert(str);
		idWinVar *dest = win->GetWinVarByName(*str, true);
		if (dest) {
			delete parms[0].var;
			parms[0].var = dest;
			parms[0].own = false;

			if ( dynamic_cast<idWinBackground *>(dest) != NULL ) {
				precacheBackground = true;
			}
		} else if ( idStr::Icmp( str->c_str(), "cmd" ) == 0 ) {
			precacheSounds = true;
		}
		int parmCount = parms.Num();
		for (int i = 1; i < parmCount; i++) {
			idWinStr *str = dynamic_cast<idWinStr*>(parms[i].var);		
			if (idStr::Icmpn(*str, "gui::", 5) == 0) {

				//  always use a string here, no point using a float if it is one
				//  FIXME: This creates duplicate variables, while not technically a problem since they
				//  are all bound to the same guiDict, it does consume extra memory and is generally a bad thing
				idWinStr* defvar = new idWinStr();
				defvar->Init ( *str, win );
				win->AddDefinedVar ( defvar );
				delete parms[i].var;
				parms[i].var = defvar;
				parms[i].own = false;

				//dest = win->GetWinVarByName(*str, true);
				//if (dest) {
				//	delete parms[i].var;
				//	parms[i].var = dest;
				//	parms[i].own = false;
				//}
				// 
			} else if ((*str[0]) == '$') {
				// 
				//  dont include the $ when asking for variable
				dest = win->GetGui()->GetDesktop()->GetWinVarByName((const char*)(*str) + 1, true);
				// 					
				if (dest) {
					delete parms[i].var;
					parms[i].var = dest;
					parms[i].own = false;
				}
			} else if ( idStr::Cmpn( str->c_str(), STRTABLE_ID, STRTABLE_ID_LENGTH ) == 0 ) {
				str->Set( common->GetLanguageDict()->GetString( str->c_str() ) );
			} else if ( precacheBackground ) {
				const idMaterial *mat = declManager->FindMaterial( str->c_str() );
				mat->SetSort( SS_GUI );
			} else if ( precacheSounds ) {
				// Search for "play <...>"
				idToken token;
				idParser parser( LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWMULTICHARLITERALS | LEXFL_ALLOWBACKSLASHSTRINGCONCAT );
				parser.LoadMemory(str->c_str(), str->Length(), "command");

				while ( parser.ReadToken(&token) ) {
					if ( token.Icmp("play") == 0 ) {
						if ( parser.ReadToken(&token) && ( token != "" ) ) {
							declManager->FindSound( token.c_str() );
						}
					}
				}
			}
		}
	} else if (handler == &Script_SetLightColor) {
		idWinStr *str = dynamic_cast<idWinStr*>(parms[0].var);
		if (str) {
			idWinVar *dest = win->GetWinVarByName(*str, true);
			if (dest) {
				delete parms[0].var;
				parms[0].var = dest;
				parms[0].own = false;
			} else {
				common->Warning( "Window %s in gui %s: setLightColor command does not specify a valid var %s",
					win->GetName(), win->GetGui()->GetSourceFile(), str->c_str() );
			}
		}
	} else if (handler == &Script_Transition) {
		if (parms.Num() < 4) {
			common->Warning("Window %s in gui %s has a bad transition definition", win->GetName(), win->GetGui()->GetSourceFile()); 
		}
		idWinStr *str = dynamic_cast<idWinStr*>(parms[0].var);
		assert(str);

		// 
		drawWin_t *destowner;
		idWinVar *dest = win->GetWinVarByName(*str, true, &destowner );
		// 

		if (dest) {
			delete parms[0].var;
			parms[0].var = dest;
			parms[0].own = false;
		} else {
			common->Warning("Window %s in gui %s: a transition does not have a valid destination var %s", win->GetName(), win->GetGui()->GetSourceFile(),str->c_str());
		}

		// 
		//  support variables as parameters		
		int c;
		for ( c = 1; c < 3; c ++ ) {
			str = dynamic_cast<idWinStr*>(parms[c].var);

			idWinVec4 *v4 = new idWinVec4;
			parms[c].var = v4;
			parms[c].own = true;

			drawWin_t* owner;

			if ( (*str[0]) == '$' ) {
				dest = win->GetWinVarByName ( (const char*)(*str) + 1, true, &owner );
				if ( !dest ) {
					// The unresolved token stays the operand, and none of it parses as a number,
					// so the transition runs from zero rather than from the var it names.
					common->Warning( "Window %s in gui %s: transition parameter %d references %s, which is not a valid var; the transition uses zero for it",
						win->GetName(), win->GetGui()->GetSourceFile(), c, str->c_str() );
				}
			} else {
				dest = NULL;
			}

			if ( dest ) {	
				idWindow* ownerparent;
				idWindow* destparent;
// jmarshall - quake 4 guis
				if ( owner && destowner) {
// jmarshall end
					ownerparent = owner->simp?owner->simp->GetParent():owner->win->GetParent();
					destparent  = destowner->simp?destowner->simp->GetParent():destowner->win->GetParent();

					// If its the rectangle they are referencing then adjust it 
					if ( ownerparent && destparent && 
						(dest == (owner->simp?owner->simp->GetWinVarByName ( "rect" ):owner->win->GetWinVarByName ( "rect" ) ) ) )
					{
						idRectangle rect;
						rect = *(dynamic_cast<idWinRectangle*>(dest));
						ownerparent->ClientToScreen ( &rect );
						destparent->ScreenToClient ( &rect );
						*v4 = rect.ToVec4 ( );
					} else {
						v4->Set ( dest->c_str ( ) );
					}
				} else {
					v4->Set ( dest->c_str ( ) );
				}
				// Retail keeps this parse-time copy only for a rect. Any other var
				// is read when the transition starts (GuiScript_ReadTransitionOperand).
				if ( dynamic_cast<idWinRectangle*>( dest ) == NULL ) {
					parms[c].live = dest;
				}
			} else {
				v4->Set(*str);
			}			
			
			delete str;
		}		
		// 

	} else {
		int c = parms.Num();
		for (int i = 0; i < c; i++) {
			parms[i].var->Init(parms[i].var->c_str(), win);
		}
	}
}

/*
=========================
idGuiScriptList::FixupParms
=========================
*/
void idGuiScriptList::FixupParms(idWindow *win) {
	int c = list.Num();
	for (int i = 0; i < c; i++) {
		idGuiScript *gs = list[i];
		gs->FixupParms(win);
		if (gs->ifList) {
			gs->ifList->FixupParms(win);
		}
		if (gs->elseList) {
			gs->elseList->FixupParms(win);
		}
	}
}

/*
=========================
idGuiScriptList::WriteToSaveGame
=========================
*/
void idGuiScriptList::WriteToSaveGame( idFile *savefile, const char *context ) {
	int i;
	if ( savefile == NULL ) {
		common->Error( "idGuiScriptList::WriteToSaveGame: NULL output file for %s", context );
	}

	for ( i = 0; i < list.Num(); i++ ) {
		if ( list[i] == NULL ) {
			common->Error( "idGuiScriptList::WriteToSaveGame: NULL script at index %d for %s", i, context );
		}
		list[i]->WriteToSaveGame( savefile, context );
	}
}

/*
=========================
idGuiScriptList::ReadFromSaveGame
=========================
*/
void idGuiScriptList::ReadFromSaveGame( idFile *savefile ) {
	int i;
	if ( savefile == NULL ) {
		common->Error( "idGuiScriptList::ReadFromSaveGame: NULL input file" );
	}

	for ( i = 0; i < list.Num(); i++ ) {
		if ( list[i] == NULL ) {
			common->Error( "idGuiScriptList::ReadFromSaveGame: NULL parsed script at index %d", i );
		}
		list[i]->ReadFromSaveGame( savefile );
	}
}
