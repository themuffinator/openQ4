# Multiplayer chat

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

openQ4 gives you three ways to talk in a multiplayer match: text chat, a
quick-chat wheel of ready-made phrases and push-to-talk voice chat. All three
work in local games with bots too.

## Text chat

Press your **Chat** key to talk to everyone, or your **Team Chat** key to talk
to your team. Existing bindings keep working. The key you press chooses the
channel every time; in a game without teams, chat always opens on All. In a
team game, team chat also gives your bots orders, such as `everyone follow me`;
see [Giving bots orders](server-setup.md#giving-bots-orders).

### What you see

The look depends on your **HUD Layout** (in **Settings → Game Options**):

- **Remastered** (the default): the four latest messages sit at the left edge
  above the status bar, each on its own plate, and leave one by one after a few
  seconds. Opening chat shows up to eight recent messages above an entry field
  that names the channel (ALL or TEAM), keeps your cursor in view, counts the
  characters you have left near the limit and shows the Send, Channel and
  Cancel keys. The history shows only whole messages and never covers the
  Competitive layout's team list.
- **Classic**: chat grows upward above the lower HUD in Quake 4's olive frame.
  New messages show for three seconds and then fade, and opening chat shows the
  conversation again. The settings below change its size and position.
- **The classic interface** (`ui_retained 0`): in the Remastered and Minimal
  layouts, resting messages appear as described for Remastered above, with
  names in white and team messages behind a TEAM tag in your team's colour.
  Opening chat uses the original Quake 4 entry field.

Chat keeps the latest 64 messages and wraps long lines. A message can be up to
128 bytes long, as in Quake 4. Menu and end-of-match transcripts keep up to 32 KB
of text.

### Keys while typing

| Key | Action |
| --- | --- |
| Enter | Send to the displayed channel and close |
| Escape | Cancel and close |
| Tab | Switch between All and Team in a team game; each channel keeps its own draft |
| Page Up / Page Down | Scroll back through messages a page at a time |
| Mouse wheel | Scroll a line at a time (a message at a time in the Remastered layout) |
| Ctrl+Home / Ctrl+End | Jump to the oldest / newest messages |
| Up / Down | Recall messages you sent in this channel; Down past the newest restores your draft |
| Home / End, Ctrl+Left / Ctrl+Right | Move within the line you are typing |

Switching channels never sends anything. Team messages follow the server's
rules for teams, spectators, mutes and managed matches.

When you scroll back, new messages do not move your reading position, even if
the panel or font size changes. Ctrl+End goes back to following new messages,
and closing chat returns the display to the newest ones. Your message history
and your last 32 sent messages per channel are kept in memory only: they are
cleared when a new map loads and are never written to disk.

### Classic layout settings

These console variables change the Classic layout's chat panel. They apply at
once and are saved automatically.

| Setting | Default | Range and meaning |
| --- | --- | --- |
| `ui_chatScale` | `1` | `0.75`–`2`; text and spacing size |
| `ui_chatWidth` | `320` | `200`–`600`, in the HUD's 640×480 units |
| `ui_chatLines` | `6` | `2`–`16` visible lines, limited by the space on screen |
| `ui_chatAlpha` | `0.5` | `0`–`1`; background opacity |
| `ui_chatTime` | `3` | `0`–`60` seconds before messages fade; `0` hides resting chat but keeps the history |
| `ui_chatOffsetX` | `0` | Horizontal offset; positive moves right |
| `ui_chatOffsetY` | `0` | Vertical offset; positive moves down |

The panel and entry line sit against the left edge of the whole screen, so on a
wide monitor they stay in the corner instead of floating inward. Offsets are
measured from that edge and kept on screen. The default position clears the
lower HUD; your own offsets can move chat over other HUD elements.

### Chat console commands

| Command | What it does |
| --- | --- |
| `messagemode`, `messagemode2` | Open chat for everyone or for your team (the same as `clientMessageMode` and `clientMessageMode 1`). |
| `say_team` | The same as `sayTeam`. |
| `chatHistory up`, `down`, `top`, `bottom` | Scroll an open chat panel, for use in key bindings. |
| `chatHistory status` | Reports message counts and your scroll position without printing any messages. |

## Quick chat

Hold **Quick Chat** for a wheel of eight phrases: *Attack!*, *Defend!*, *Need
backup!*, *Enemy incoming!*, *On my way!*, *Thanks!*, *Nice shot!* and *Good
game!*. Push the right stick or move the mouse toward a phrase and release to
send it; releasing without pointing at one sends nothing.

In a team game the wheel sends to your team. Press zoom while it is open to
switch to everyone and back; the centre of the wheel names the channel. The
phrase goes out in your language. The wheel works whether you are alive, dead
or spectating. It is part of the new interface and is not available with
`ui_retained 0`.

New configurations bind Quick Chat to **D-pad Down** on a controller and **V** on
the keyboard. Otherwise, bind it under **Settings → Controls → Weapons**. Its
console action is `_quickChat`.

The wheel's design draws on [Q2REX](https://github.com/themuffinator/Q2REX),
adapted to openQ4 without its engine, interface code, fonts or artwork.

## Voice chat

### Set it up

1. During a multiplayer match, open the in-game menu's **Voice** tab
   (**Voice Config** in the classic menus) and turn on **Microphone access**.
   It starts off, including after upgrading from an older version, and turning
   it on does not start recording by itself.
2. Bind **Voice Chat** under **Settings → Controls → Other**. It has no default
   key.
3. Hold that key to speak.

openQ4 uses your system's default recording device. Allow microphone access
when your operating system asks.

**Local microphone test** lets you hear your microphone and see its level
without sending anything to the server. It stops when you leave the Voice page,
turn off microphone access or leave the match. **Mic Input Volume** sets your
microphone level. **Receive Volume** sets other players' voices, separately
from campaign dialogue and music. **Voice Echo** has the server send your own
speech back to you.

### Mute players

Use **Mute** on the **Players** tab to silence one player, or turn off
**Receive Voice** to silence everyone. A mute applies to that player's current
connection: it does not carry over to someone who later takes the same slot.

### What to expect

- Voice follows the server's rules for teams, Tourney arenas, spectators and
  managed matches.
- Voice is not recorded in multi-view demos or sent to repeaters.
- Speech is sent at telephone quality, and audio that arrives too late is
  dropped, so a congested connection can cause gaps in speech while the game
  carries on.
- The server and every player need openQ4 0.14.1.
- Voice chat has been tested on Windows, but not yet with a wide range of real
  microphones or on other operating systems. Please report problems.

### If it does not work

The Voice page shows whether your microphone input is off, ready, recording or
unavailable. If it is unavailable, check your system's default recording device
and its microphone permissions. `listVoiceChat` in the console reports the
recording and playback state without playing any audio.

### For server operators

Set `si_voiceChat 0` to turn voice chat off on your server. To silence one
player, use `serverVoiceMute <slot> 1`; `serverVoiceMute <slot> 0` lets them
speak again.
