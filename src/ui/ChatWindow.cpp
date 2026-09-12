// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
// Original implementation of the Q2REX chat interaction design, adapted to
// Quake 4's GUI system. No Q2REX/KEX source or assets are incorporated.
#include "DeviceContext.h"
#include "UserInterfaceLocal.h"
#include "ChatWindow.h"

static idCVar ui_chatScale("ui_chatScale", "1", CVAR_GUI | CVAR_ARCHIVE | CVAR_FLOAT, "chat font and spacing scale", 0.75f, 2.0f);
static idCVar ui_chatWidth("ui_chatWidth", "320", CVAR_GUI | CVAR_ARCHIVE | CVAR_FLOAT, "chat width in 640x480 GUI units", 200.0f, 600.0f);
static idCVar ui_chatLines("ui_chatLines", "6", CVAR_GUI | CVAR_ARCHIVE | CVAR_INTEGER, "maximum visible chat history lines", 2, 16);
static idCVar ui_chatAlpha("ui_chatAlpha", "0.5", CVAR_GUI | CVAR_ARCHIVE | CVAR_FLOAT, "chat background opacity", 0.0f, 1.0f);
static idCVar ui_chatTime("ui_chatTime", "3", CVAR_GUI | CVAR_ARCHIVE | CVAR_FLOAT, "seconds to show incoming chat before fading; 0 hides passive chat", 0.0f, 60.0f);
static idCVar ui_chatOffsetX("ui_chatOffsetX", "0", CVAR_GUI | CVAR_ARCHIVE | CVAR_FLOAT, "horizontal chat offset in GUI units, positive moves right", -640.0f, 640.0f);
static idCVar ui_chatOffsetY("ui_chatOffsetY", "0", CVAR_GUI | CVAR_ARCHIVE | CVAR_FLOAT, "vertical chat offset in GUI units, positive moves down", -480.0f, 480.0f);

static oq4chat::History chatHistory;
static oq4chat::SentHistory sentHistory[2];
static idChatWindow *activeChat = NULL;
static int lastMessageTime = 0;

static float ChatLabelScale(idDeviceContext *dc, const char *label, float scale, float width) {
    // Keep localized chrome legible inside deliberately narrow/custom layouts.
    // Conversation and input text retain the player's requested font size.
    while (scale > 0.1f && dc->TextWidth(label, scale, -1) > width) { scale *= 0.95f; }
    return scale;
}

idChatWindow::idChatWindow(idDeviceContext *context, idUserInterfaceLocal *owner, bool editable)
    : idEditWindow(context, owner), input(editable), cachedRevision(~std::uint64_t(0)),
      cachedWidth(0), cachedScale(0), cachedScreenWidth(0), cachedScreenHeight(0),
      visibleRows(6), lastScrollTime(0), lastScrollKey(0), channelKeyDown(false) {
    // Keep the registered edit control's text/IME/cursor and GUI command path.
    const char *definition =
        "oq4_chat { rect 0,0,640,480 text \"gui::chattext\" maxchars 128 "
        "font \"fonts/lowpixel\" textscale 0.25 textspacing -1 textstyle 1 "
        "forecolor 1,1,1,1 hovercolor 1,1,1,1 nocursor 1 noclip 1 "
        "onEnter { set \"cmd\" \"chatmessage\"; } onESC { set \"cmd\" \"close\"; } }";
    idParser parser(definition, idStr::Length(definition), "<openQ4 chat>",
        LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWMULTICHARLITERALS);
    Parse(&parser);
    noEvents = !input;
    if (!input) { flags &= ~WIN_CANFOCUS; }
    frameEdge = declManager->FindMaterial("gfx/guis/mainmenu/tooltip_edge");
    frameMid = declManager->FindMaterial("gfx/guis/mainmenu/tooltip_mid");
    screen = declManager->FindMaterial("gfx/guis/mainmenu/screen");
}

idChatWindow::~idChatWindow() {
    if (activeChat == this) { activeChat = NULL; }
}

void idChatWindow::Reset() {
    chatHistory.Clear();
    sentHistory[0] = oq4chat::SentHistory();
    sentHistory[1] = oq4chat::SentHistory();
    activeChat = NULL;
    lastMessageTime = 0;
}

void idChatWindow::History_f(const idCmdArgs &args) {
    if (activeChat == NULL) {
        common->Printf("chatHistory: open message mode first\n");
        return;
    }
    const char *action = args.Argv(1);
    int delta = 0;
    if (!idStr::Icmp(action, "up")) { delta = -Max(1, activeChat->visibleRows - 1); }
    else if (!idStr::Icmp(action, "down")) { delta = Max(1, activeChat->visibleRows - 1); }
    else if (!idStr::Icmp(action, "top")) { delta = -static_cast<int>(activeChat->rows.size()); }
    else if (!idStr::Icmp(action, "bottom")) { delta = static_cast<int>(activeChat->rows.size()); }
    else if (*action && idStr::Icmp(action, "status")) {
        common->Printf("usage: chatHistory [up|down|top|bottom|status]\n");
        return;
    }
    if (delta) { chatHistory.Scroll(activeChat->rows, activeChat->visibleRows, delta); }
    const std::size_t first = chatHistory.First(activeChat->rows, activeChat->visibleRows);
    const std::uint64_t serial = first < activeChat->rows.size() ? activeChat->rows[first].serial : 0;
    common->Printf("chatHistory: messages=%d rows=%d first=%d serial=%llu following=%d channel=%d\n",
        static_cast<int>(chatHistory.messages.size()), static_cast<int>(activeChat->rows.size()),
        static_cast<int>(first), static_cast<unsigned long long>(serial), chatHistory.Following() ? 1 : 0,
        activeChat->gui->GetStateBool("messagemode") ? 1 : 0);
}

void idChatWindow::Activate(bool activate, idStr &act) {
    idEditWindow::Activate(activate, act);
    if (!input) { return; }
    if (activate) {
        activeChat = this;
        channelDraft[0].Clear();
        channelDraft[1].Clear();
        sentHistory[0].Begin();
        sentHistory[1].Begin();
        lastScrollKey = 0;
        channelKeyDown = false;
        GainFocus();
    } else if (activeChat == this) {
        activeChat = NULL;
        // Closing returns the passive surface to new messages. An open reader
        // remains anchored until they explicitly return to the newest row.
        chatHistory.Follow();
        lastMessageTime = Sys_Milliseconds();
    }
}

void idChatWindow::RunNamedEvent(const char *eventName) {
    if (!idStr::Icmp(eventName, "chatReset")) {
        Reset();
    } else if (!idStr::Icmp(eventName, "chatLine")) {
        chatHistory.Append(gui->GetStateString("chatline"), gui->GetStateBool("chatteam"));
        lastMessageTime = Sys_Milliseconds();
    } else {
        idEditWindow::RunNamedEvent(eventName);
    }
}

void idChatWindow::SetInput(const char *value) {
    text = value;
    gui->SetStateString("chattext", value);
    GainFocus();
}

const char *idChatWindow::HandleEvent(const sysEvent_t *event, bool *updateVisuals) {
    cmd.Clear();
    if (!input || activeChat != this) { return ""; }
    const int key = event->evValue;
    const int channel = gui->GetStateBool("messagemode") ? 1 : 0;
    if (event->evType == SE_KEY) {
        if (!event->evValue2) {
            if (key == lastScrollKey) { lastScrollKey = 0; }
            if (key == K_TAB) { channelKeyDown = false; }
            return "";
        }
        if (key == K_TAB) {
            if (channelKeyDown) { return ""; }
            channelKeyDown = true;
            if (gui->GetStateBool("chatTeamAvailable")) {
                channelDraft[channel] = text.c_str();
                gui->SetStateBool("messagemode", !channel);
                SetInput(channelDraft[!channel]);
            }
            if (updateVisuals) { *updateVisuals = true; }
            return "";
        }
        if (key == K_PGUP || key == K_PGDN || key == K_MWHEELUP || key == K_MWHEELDOWN ||
            ((key == K_HOME || key == K_END) && idKeyInput::IsDown(K_CTRL))) {
            const int now = Sys_Milliseconds();
            if (key != lastScrollKey || now - lastScrollTime >= 120) {
                int delta = (key == K_PGUP || key == K_MWHEELUP) ? -1 : 1;
                if (key == K_PGUP || key == K_PGDN) { delta *= Max(1, visibleRows - 1); }
                if (key == K_HOME) { delta = -static_cast<int>(rows.size()); }
                if (key == K_END) { delta = static_cast<int>(rows.size()); }
                chatHistory.Scroll(rows, visibleRows, delta);
                lastScrollTime = now;
                lastScrollKey = key;
            }
            if (updateVisuals) { *updateVisuals = true; }
            return "";
        }
        if (key == K_UPARROW || key == K_DOWNARROW) {
            SetInput(sentHistory[channel].Recall(key == K_UPARROW ? -1 : 1, text.c_str()).c_str());
            if (updateVisuals) { *updateVisuals = true; }
            return "";
        }
        if (key == K_ENTER || key == K_KP_ENTER) { sentHistory[channel].Commit(text.c_str()); }
    }
    return idEditWindow::HandleEvent(event, updateVisuals);
}

void idChatWindow::Reflow(float width, float scale) {
    const int screenWidth = renderSystem->GetScreenWidth();
    const int screenHeight = renderSystem->GetScreenHeight();
    if (cachedRevision == chatHistory.revision && cachedWidth == width && cachedScale == scale &&
        cachedScreenWidth == screenWidth && cachedScreenHeight == screenHeight) { return; }
    rows.clear();
    SetFont();
    for (const oq4chat::Message &message : chatHistory.messages) {
        idStr remaining = message.text.c_str();
        if (message.team) {
            const int separator = remaining.Find("^0: ");
            if (separator >= 0) {
                idStr body = remaining.Mid(separator + 4, remaining.Length() - separator - 4);
                body.RemoveEscapes(S_ESCAPE_COLOR | S_ESCAPE_COLORINDEX);
                remaining = remaining.Left(separator + 4) + "^c487" + body;
            }
        }
        // Measure with the very same proportional-font path used to draw.
        // GetMaxTextIndex rounds total font units instead of each drawn glyph;
        // at fractional scales that can underestimate a line's actual width.
        idList<int> breaks;
        dc->DrawText(remaining, scale, 0, colorWhite, idRectangle(0, 0, width, 480),
            true, -1, true, &breaks, 0, 0, 1, true);
        idStr color;
        for (int i = 0; i < breaks.Num(); ++i) {
            const int start = breaks[i];
            const int end = i + 1 < breaks.Num() ? breaks[i + 1] : remaining.Length();
            idStr line = color + remaining.Mid(start, Max(0, end - start));
            line.StripTrailingWhitespace();
            rows.push_back({message.serial, static_cast<std::size_t>(start), line.c_str(), message.team});
            color = line.GetLastColorCode();
        }
    }
    cachedRevision = chatHistory.revision;
    cachedWidth = width;
    cachedScale = scale;
    cachedScreenWidth = screenWidth;
    cachedScreenHeight = screenHeight;
}

void idChatWindow::Draw(int time, float x, float y) {
    const bool editing = input && activeChat == this;
    if (input && !editing) { return; }
    if (!input && activeChat != NULL) { return; }
    float alpha = 1.0f;
    if (!editing) {
        if (chatHistory.messages.empty() || ui_chatTime.GetFloat() <= 0.0f) { return; }
        const int age = Max(0, Sys_Milliseconds() - lastMessageTime);
        alpha = idMath::ClampFloat(0.0f, 1.0f, 1.0f - (age - ui_chatTime.GetFloat() * 1000.0f) / 750.0f);
        if (alpha <= 0.0f) { return; }
    }
    const float scale = ui_chatScale.GetFloat();
    textScale = 0.25f * scale;
    SetFont();
    const float lineHeight = Max(12.0f * scale, dc->MaxCharHeight(textScale) * 1.25f);
    const float padding = 8.0f * scale;
    const float width = ui_chatWidth.GetFloat();
    // Both chat surfaces hug the left edge of the whole virtual screen, not of
    // the centred 640-wide HUD canvas: on a display wider than 4:3 the canvas
    // is pillarboxed into the middle, and a panel pinned to it would float
    // inward. The expansion is the extra virtual width the desktop already
    // clips to, so -xExpand/640+xExpand is the visible screen in these units.
    float xExpand = 0.0f;
    float yExpand = 0.0f;
    dc->GetVirtualScreenExpansion(forceAspectWidth, forceAspectHeight, xExpand, yExpand);
    const float screenLeft = -xExpand;
    const float screenRight = 640.0f + xExpand;
    // The 640x480 canvas uses the engine's shared aspect correction. Reserve
    // the status/weapon region and clamp offsets so the editor stays reachable.
    const float left = idMath::ClampFloat(screenLeft + 4.0f, screenRight - 4.0f - width,
        screenLeft + 8.0f + ui_chatOffsetX.GetFloat());
    const float footerHeight = editing ? lineHeight * 2.0f : 0.0f;
    const float reserve = lineHeight * 3.0f + padding * 2.0f;
    const float bottom = idMath::ClampFloat(reserve + 4.0f, 476.0f, 372.0f + ui_chatOffsetY.GetFloat());
    const int room = Max(1, static_cast<int>((bottom - 4.0f - padding * 2.0f - footerHeight) / lineHeight));
    visibleRows = Min(ui_chatLines.GetInteger(), room);
    Reflow(width - padding * 2.0f, textScale);
    const int count = Min(visibleRows, static_cast<int>(rows.size()));
    const float height = Max(32.0f, count * lineHeight + footerHeight + padding * 2.0f);
    const float top = bottom - height;
    const float opacity = ui_chatAlpha.GetFloat() * alpha;

    // Native Quake 4 chamfered frame, with a softer upper edge for older lines.
    const float cap = Min(16.0f, height * 0.5f);
    const idVec4 rail(0.545f, 0.588f, 0.294f, opacity);
    dc->DrawMaterial(left, top, width, cap, frameEdge, rail);
    dc->DrawMaterial(left, top + cap, width, height - cap * 2.0f, frameMid, rail);
    dc->DrawMaterial(left, bottom - cap, width, cap, frameEdge, rail, -1, -1);
    for (float band = 4.0f; band < height - 4.0f;) {
        const bool fadingTop = !editing && count > 2 && band < lineHeight * 2.0f;
        const float fade = fadingTop ? idMath::ClampFloat(0.15f, 1.0f, band / (lineHeight * 2.0f)) : 1.0f;
        const float bandHeight = fadingTop ? Min(4.0f, height - 4 - band) : height - 4 - band;
        dc->PushClipRect(left + 4, top + band, width - 8, bandHeight);
        dc->DrawMaterial(left + 4, top + 4, width - 8, height - 8, screen,
            idVec4(0.09f, 0.11f, 0.07f, opacity * fade));
        dc->PopClipRect();
        band += bandHeight;
    }
    const std::size_t first = chatHistory.First(rows, visibleRows);
    for (int i = 0; i < count; ++i) {
        const oq4chat::Row &row = rows[first + i];
        const float fade = !editing && count > 2 ? Min(1.0f, (i + 1) / 3.0f) : 1.0f;
        idVec4 color = row.team ? idVec4(0.45f, 0.85f, 0.8f, alpha * fade) : idVec4(0.95f, 0.95f, 0.88f, alpha * fade);
        dc->DrawText(row.text.c_str(), textScale, 0, color,
            idRectangle(left + padding, top + padding + i * lineHeight, width - padding * 2, lineHeight), false, -1, false, NULL, 0, 0, 1);
    }
    if (!editing) { return; }
    const float inputY = top + padding + count * lineHeight;
    const char *label = common->GetLocalizedString(gui->GetStateBool("messagemode") ? "#str_42831" : "#str_42830");
    const float labelWidth = Min(width * 0.4f, static_cast<float>(dc->TextWidth(label, textScale, -1)) + padding);
    dc->DrawText(label, ChatLabelScale(dc, label, textScale, labelWidth - padding), 0, idVec4(0.890f, 0.537f, 0, 1),
        idRectangle(left + padding, inputY, labelWidth, lineHeight), false);
    textRect = idRectangle(left + padding + labelWidth, inputY, width - padding * 2 - labelWidth, lineHeight);
    EnsureCursorVisible();
    dc->PushClipRect(textRect);
    idEditWindow::Draw(time, x, y);
    dc->PopClipRect();
    const char *hint = common->GetLocalizedString(!chatHistory.Following() ? "#str_42833" :
        (gui->GetStateBool("chatTeamAvailable") ? "#str_42832" : "#str_42834"));
    dc->DrawText(hint, ChatLabelScale(dc, hint, textScale * 0.7f, width - padding * 2), 0, idVec4(0.72f, 0.76f, 0.63f, 1),
        idRectangle(left + padding, inputY + lineHeight, width - padding * 2, lineHeight), false);
}
