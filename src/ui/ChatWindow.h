// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_CHAT_WINDOW_H
#define OPENQ4_CHAT_WINDOW_H

#include "EditWindow.h"
#include "ChatHistory.h"

// Native replacement for the stock HUD history and message-mode panel. Uses
// the existing edit control, GUI state protocol, fonts and retail frame art.
class idChatWindow : public idEditWindow {
public:
    idChatWindow(idDeviceContext *dc, idUserInterfaceLocal *gui, bool input);
    ~idChatWindow() override;
    void Draw(int time, float x, float y) override;
    void Activate(bool activate, idStr &act) override;
    const char *HandleEvent(const sysEvent_t *event, bool *updateVisuals) override;
    void RunNamedEvent(const char *eventName) override;
    bool IsInput() const { return input; }
    static void Reset();
    static void History_f(const idCmdArgs &args);
    static void Input_f(const idCmdArgs &args);
private:
    const char *HandleInput(const sysEvent_t *event, bool *updateVisuals);
    void PublishCaret();
    void ScrollMessages(int delta);
    void Reflow(float width, float scale);
    void DrawRemasteredLines(const float edges[4]);
    void SetInput(const char *value);
    bool input;
    std::uint64_t cachedRevision;
    float cachedWidth;
    float cachedScale;
    int cachedScreenWidth;
    int cachedScreenHeight;
    std::vector<oq4chat::Row> rows;
    int visibleRows;
    int lastScrollTime;
    int lastScrollKey;
    bool channelKeyDown;
    idStr channelDraft[2];
    const idMaterial *frameEdge;
    const idMaterial *frameMid;
    const idMaterial *screen;
};
#endif
