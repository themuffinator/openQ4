# Multiplayer chat

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

## Voice chat

In a multiplayer match, open the **Voice** tab (or **Voice Config** in the
Classic menu) and enable **Microphone access**. Access starts off, including
after upgrading an older installation. Enabling access alone does not record.
Bind **Voice Chat** under **Settings > Controls > Other**, then hold that key
to speak. There is no default key binding. The default recording device is
selected through SDL; allow microphone access in your operating system when
prompted.

**Local microphone test** lets you hear the input and see its level without
sending it to the server. It stops when you leave the Voice tab or menu, revoke
microphone access, or leave the match. **Mic Input Volume** adjusts the input;
**Receive Volume** adjusts other players independently of campaign dialogue and
music. **Voice Echo** lets the server return your transmitted speech to you.

Use **Players > Mute** to silence a participant, or turn off **Receive Voice**
to silence all incoming chat. A mute applies to that connection: it does not
follow a disconnected player or affect someone who later occupies the same
slot. Voice follows the server's team, arena, spectator and managed-match
rules. It is not included in multi-view demos or repeater feeds.

Voice uses narrowband speech and discards audio that cannot be delivered
promptly; congestion can cause gaps while gameplay continues. Both server and
clients need this version's voice support. A server operator can disable it
with `si_voiceChat 0`, or silence a connected slot with
`serverVoiceMute <slot> 1` (`0` restores it). Replace the engine and game
modules together when upgrading.

The page reports whether input is off, ready, recording or unavailable. If
input is unavailable, check the default recording device and operating-system
permissions. `listVoiceChat` reports capture/playback state without printing
audio. Windows automated tests cover real local peers and SDL's dummy input;
physical microphones and other operating systems still need qualification.

## Text chat

Chat keeps the latest 64 received messages and wraps long lines.

With the Remastered HUD layout (the default), the four latest messages sit at
the left edge above the status bar, each on its own plate, and leave one by one
after a few seconds. Opening chat shows up to eight recent messages above an
entry field that names the channel (ALL or TEAM), keeps your cursor in view as
you type, counts the characters you have left near the limit and shows the
Send, Channel and Cancel keys. The history shows only whole messages and never
covers the team list of the Competitive layout.

The stock interface (`ui_retained 0`) shows the resting messages the same way
in the Remastered and Minimal layouts: the latest four at the left edge above
the status bar, each on its own plate, with names in white and team messages
behind a TEAM tag in your team's colour. Opening chat there keeps the stock
entry field and its history.

With the Classic layout, chat grows upward above the lower HUD in Quake 4's
olive frame: new messages appear for three seconds and fade out over another
0.75 seconds, and opening chat reveals the retained conversation again.

Your existing **Chat** and **Team Chat** bindings still work. The console
commands `messagemode` and `messagemode2` are aliases for
`clientMessageMode` and `clientMessageMode 1`. `say_team` also aliases `sayTeam`.
Chat remains a multiplayer feature, including local games with bots. In a
team game, team chat also gives your bots orders, such as `everyone follow me`;
see [Giving bots orders](server-setup.md#giving-bots-orders).

| While typing | Action |
| --- | --- |
| Enter | Send to the displayed channel and close |
| Escape | Cancel and close |
| Tab | Switch All/Team in a team game; each channel keeps its own draft |
| Page Up / Page Down | Scroll received history by a page |
| Mouse wheel | Scroll received history by a line (by a message in the Remastered layout) |
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

These settings shape the Classic layout's chat panel; they apply immediately
and are saved automatically:

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

## Quick chat

Hold **Quick Chat** for a wheel of eight phrases: *Attack!*, *Defend!*,
*Need backup!*, *Enemy incoming!*, *On my way!*, *Thanks!*, *Nice shot!* and
*Good game!*. Push the right stick or move the mouse toward a phrase and
release to send it; releasing without pointing at one sends nothing. In a team
game the wheel sends to your team, and pressing zoom while it is open switches
to everyone and back; the wheel's centre names the channel. The phrase goes
out in your language. The wheel works alive, dead or spectating, and is part
of the new interface (it is absent with `ui_retained 0`).

New configurations bind Quick Chat to **D-pad Down** on a controller and **V**
on the keyboard; otherwise bind it under Settings > Controls > Weapons. Its
console action is `_quickChat`.

The interaction design draws on [Q2REX](https://github.com/themuffinator/Q2REX),
adapted to openQ4 without its engine, UI dependencies, fonts or artwork.
