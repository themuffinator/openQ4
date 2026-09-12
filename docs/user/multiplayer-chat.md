# Multiplayer chat

Chat keeps the latest 64 received messages, wraps long lines, and grows upward
above the lower HUD. New messages appear for three seconds and fade out over
another 0.75 seconds. Opening chat reveals the retained conversation again.
The panel uses Quake 4's fonts, olive frame and aspect-correct GUI scaling.

Your existing **Chat** and **Team Chat** bindings still work. The console
commands `messagemode` and `messagemode2` are aliases for
`clientMessageMode` and `clientMessageMode 1`. `say_team` also aliases `sayTeam`.
Chat remains a multiplayer feature, including local games with bots.

| While typing | Action |
| --- | --- |
| Enter | Send to the displayed channel and close |
| Escape | Cancel and close |
| Tab | Switch All/Team in a team game; each channel keeps its own draft |
| Page Up / Page Down | Scroll received history by a page |
| Mouse wheel | Scroll received history by a line |
| Ctrl+Home / Ctrl+End | Oldest / newest received lines |
| Up / Down | Recall sent messages in the current channel; Down past the newest restores your draft |
| Home / End, Ctrl+Left / Ctrl+Right | Move within the input line |

The key used to open chat chooses the channel every time. In a game without
teams, chat opens on All. Changing channels does not send anything. Team
messages retain Quake 4's server-controlled team, spectator, mute and managed
match restrictions.

When you scroll upward, incoming messages leave your reading position alone.
The same message stays at the top when the panel width or font size changes.
Ctrl+End resumes following new messages. Closing chat returns the passive
display to the newest messages. Received history and the last 32 distinct
consecutive submissions per channel are held in memory and cleared when a new
map initializes or the UI shuts down; they are not written to disk.

Settings apply immediately and are saved automatically:

| Console setting | Default | Range / meaning |
| --- | --- | --- |
| `ui_chatScale` | `1` | `0.75`–`2`; font and spacing multiplier |
| `ui_chatWidth` | `320` | `200`–`600` in the shared 640×480 GUI canvas |
| `ui_chatLines` | `6` | `2`–`16` visible history lines, limited by available screen space |
| `ui_chatAlpha` | `0.5` | `0`–`1`; background opacity |
| `ui_chatTime` | `3` | `0`–`60` seconds before fading; `0` hides passive chat while retaining history |
| `ui_chatOffsetX` | `0` | Horizontal offset in GUI units; positive moves right |
| `ui_chatOffsetY` | `0` | Vertical offset in GUI units; positive moves down |

Both the passive panel and the input line sit against the left edge of the
whole display, not the left edge of the centred 4:3 HUD canvas, so on a wide
monitor they stay in the corner instead of floating inward. Offsets are
measured from that edge and clamped to keep the panel on screen, so the full
width of the display is reachable. The default position clears the lower HUD;
custom offsets can deliberately move it over other HUD elements.
The input limit remains Quake 4's 128 bytes. Menu and end-of-match transcripts
also retain more text, up to 32 KB.

For console binds, `chatHistory up`, `down`, `top` and `bottom` browse an open
panel. `chatHistory status` reports counts and position without printing messages.

The interaction design draws on [Q2REX](https://github.com/themuffinator/Q2REX),
adapted to openQ4 without its engine, UI dependencies, fonts or artwork.
