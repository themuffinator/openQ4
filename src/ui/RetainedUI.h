// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

struct sysEvent_s;
struct retainedUIInput_t {
	enum kind_t : int { KEY, POINTER, POINTER_BUTTON, FOCUS, POINTER_LEAVE, CANCEL } kind = KEY;
	int source = 0, key = 0;
	int down = 0, repeated = 0;
	float x = 0, y = 0;
};

void RetainedUI_Init();
void RetainedUI_Shutdown();
void RetainedUI_Draw();
void RetainedUI_Close();
// IsOpen is safe for the async usercmd thread; all other entry points run on
// the engine thread. Preview documents do not acquire application input.
bool RetainedUI_IsOpen();
unsigned RetainedUI_InputGeneration();
void RetainedUI_FrameInput();
bool RetainedUI_ProcessEvent(const sysEvent_s* event);
void RetainedUI_QueueInput(const retainedUIInput_t& input, int time);
// Engine-owned bridge, called with the usercmd critical section held.
void Usercmd_RetainedInputChanged();
