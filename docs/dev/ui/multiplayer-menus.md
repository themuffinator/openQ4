# Retained multiplayer menus

The retained multiplayer menus replace the stock full-screen multiplayer menu
with one card over the live, softened match (section 14.18 of the
[visual specification](../ui-visual-design.md), requirement `FLOW-046`). The
Escape card's shell came first: the card, its header, the eight-tab strip, the
prompt bar, the Disconnect confirmation, the softening and its release, and the
session and game plumbing that let the card cover the game's own menu. The
Team, Players, Vote, Settings, Voice, Server, Match and Admin pages are built
on it, the Match page with all six of Match Control's sections and Admin with
the remote console password, Players, Server and Console. With no page left
handing off, the Escape card presents with `ui_retained` alone. The Welcome
card covers the connect-time join offer with its Join, Server, Players and
Settings pages, all built, so it presents when `ui_retained 1` is selected.
The loading screen hands a join over to the Welcome card (B8). The card's text
fields carry the remote console password, the console's commands, the Match
page's referee password and the Settings pages' name and clan.

## The gate

`ui_retainedMultiplayer` (default 0) opts into both cards. It is not archived,
so it lasts until the player quits, and it is `CVAR_NOCHEAT`, so a join or a
hosted spawn without cheats, which resets every unarchived cheat CVar, keeps
it, and it can change during a match. `ui_retained` alone
includes a card only once none of its pages hands off any more: the session
lists the pages that still do in `RETAINED_MP_ESCAPE_MISSING_PAGES` and
`RETAINED_MP_WELCOME_MISSING_PAGES`, both now empty, and
`tools/tests/ui_retained_gate.py` keeps each list equal to its document's
hand-off pages (its `stock_<page>` programs). Both cards therefore present
with `ui_retained` alone; `ui_retained 0` keeps the classic join panel and
the stock in-match menu. A page that is built may still hand off what it
cannot show (`classic_<page>`: the Match page's team rows or series history
past a full server's)
without counting as missing. Players never see a mix of card and stock pages
unless they opt in.

## The cover

The game keeps its menu. `mpmain.gui` stays the active GUI, with its state, its
commands, its legacy pages and the Match Control adapter, so the game sees no
difference except that the menu stops drawing. The session presents the card
over it while all of these hold (`idSessionLocal::UpdateRetainedMultiplayer`):

- the gate or the opt-in is on, a multiplayer map is spawned, and the active
  GUI is the game's multiplayer menu (`guis/mpmain.gui`, drawn over the game),
  or its match summary (`guis/summary.gui`), which takes the summary card
  ([below](#the-summary-card)); the chat and buy screens are other GUIs and are
  never covered. A card covers the GUI it opened over: another taking the
  screen (the summary closing into the menu) retires it and asks again;
- no test GUI or retained preview is open, and no stock page has taken this
  opening over (below);
- the game names the card (`retainedMultiplayerVariant`): Welcome while the
  player has not answered the join offer (the offer stands, or `ui_joined` is
  still 0 while a player without `ui_autoJoin` spectates; a player with
  `ui_autoJoin` never sees the offer), Escape otherwise; and that card's gate
  is on;
- the game answers `retainedMultiplayerCover` with the protocol the session
  expects (`mp.protocol` 2) and allows the cover now (`mp.cover_allowed`). It
  refuses in an Arena Campaign match, which keeps its own menu. Over the join
  offer the card's softening replaces the join panel's blur, which comes back
  if the stock menu takes the offer over.

Otherwise the stock menu presents. Like the other retained screens, the card
falls back to the stock menu for the session when its document is not
installed (quietly), cannot load (one warning) or stops drawing (one warning),
when a mod supplies its own `guis/mpmain.gui`, and when the game module does
not answer the protocol, as an older or a mod's game module would not. A
game module that does not name the card gets Escape, which its join offer
refuses. Whatever keeps the stock menu keeps it only for that opening; the
next asks the game again.

Each frame the session asks the game for the card's state
(`retainedMultiplayerState`). The game writes only the keys that changed and a
new `mp.revision`, and the session publishes the card only when the revision
moves. Closing the menu, opening the main menu, or anything else that takes the
screen retires the card (`retainedMultiplayerUncover`); the game then draws its
menu again. A map change retires it without a release and opens the next
map's card on its first page; the document itself loads inside each
multiplayer level load.

`ui_retainedStatus` reports each card's gate as `multiplayer=` (Escape) and
`welcome=`, and the covering card as `mp=escape`, `mp=welcome` or
`mp=summary` (`mp=-` otherwise); `openq4_retainedGui` addresses the card while
it covers.

## The card

The card is the section 6 card: a black body at 0.94 inside one
`marine.rail.card` rail, 12 dp cuts at the top-trailing and bottom-leading
corners and 3 dp cuts at the other two. It is centered on the projection
center at every view, 477 dp tall.

- **Header.** The white 0.16 wash in the light plate's shape, the 1 dp rule,
  the 8 dp triangle and the map in Marine 20 dp; trailing it the mode, the
  clock in the value color and the score, "MARINES 12 - 9 STROGG" with the
  player's team first in team modes and the player's place and score in the
  others. The title takes whatever the trailing texts leave and clips there.
- **Tab strip.** Eight tabs, Team, Players, Vote, Match, Settings, Voice,
  Server and Admin, as short labels written for the strip in every language,
  Marine 14 dp. The active tab rises 30 dp with its 3 dp leading flare and its
  18 dp 45-degree shoulder, #5A652A from 1.00 to 0.60 inside a white outline,
  and breaks the baseline under it; a #5A652A wash fades below the strip over
  one tab height. Inactive labels sit at 0.55 and rise to 0.85 on hover; focus
  turns a label orange. Q and E keycaps at the strip's ends switch tabs.
  After a controller they show its shoulder buttons in the same 20x22 dp
  ([prompt glyphs](retained-screens.md#prompt-glyphs)), and the prompt bar
  shows its east button, shoulders and south button.
- **Pages.** Content is inset 24 dp. Actions are in-game plates (b6_light,
  0.62 for the primary action and 0.28 for others, 1.00 with the orange marker
  when focused). A page not built yet says it opens in the classic
  multiplayer menu and offers that as its primary action.
- **Prompt bar.** Inside the card's bottom inset: Resume on Escape, Tabs on Q
  and E, Select on Enter, then Main Menu and Disconnect as actions at the
  trailing end. Disconnect asks first in the stock confirmation, which
  soft-focuses the card and the view beneath it.

### The Team page

The game decides what the Team page offers, from the local player's state:

| Player | Actions |
| --- | --- |
| On a team | Switch team, with the team sizes it would leave; Spectate; Ready |
| Spectating a team mode | Join the team auto join would pick; join the other team |
| Playing another mode | Spectate; Ready |
| Spectating another mode | Join game |

Above them a band in the team's color (#6AA42B Marine, #FF7B04 Strogg,
#999999 for spectators, #8B964B elsewhere) names the team with its score and
both teams' sizes; in deathmatch it gives the player's score and place. The
last three chat lines close the page.

An action the game cannot take now stays in place, dimmed, with a lock after
its label and the reason under it in #E46D56: a team the balance rule
refuses or the player is already on (the stock "too many players" and
"already on" messages), spectating a server that does not allow it, and Ready
outside a warm-up that uses it. Choosing one shakes it for 300 ms and pulses
its reason, and nothing else happens. An available action records its slot
and runs `mpTeamAction`; the game derives the slot's action again from the
player's state, refuses it if it is no longer available, and otherwise joins,
spectates or readies and closes the menu, as the stock buttons do.

### The Players page

The team lists stand beside the selected player's statistics. Each list opens
with a band in its team's color (#6AA42B Marine, #FF7B04 Strogg, #999999 for
spectators, #8B964B for everyone outside team modes) holding the title and the
team's score (the Marine and Strogg bands lead with their faction marks), then
lists its players in the scoreboard's rows (section 14.14):
a band at 0.08, or 0.29 with the orange marker for the player's own row; the speaker,
crossed in red when the player is muted; the friend silhouette, 0.125 when the
player is not a friend; a skull while the player is dead or out of the
Tourney, with the whole row at 0.50; the name, followed for a bot by the BOT
tag on its olive plate; the score; the network-quality bars (four steps by
ping, as on the held scoreboard, empty for a bot); and the ping. The name, score and ping headings run once above the lists. The player's own team comes first
(Marines while spectating), then the other team, then the spectators, whose
band shows only while there are any. Each row keeps its readable 24 dp pitch.
The rows and team bands scroll below fixed column headings, with the existing
editable list scrollbar in a separate 36 dp gutter. Every list declares all 32
engine connection slots, so a crowded team or spectator list is not truncated.
The same construction serves Welcome and the summary's Statistics and Scoreboard
pages. Short read-only lists keep focus on their tab; overflowing ones focus the
scrollbar. Interactive lists reveal the selected row when the page opens.

A list keeps its order while the menu stays open and sorts again by score only
when the menu opens or the list's players change, so a row never moves under
the cursor while scores change. The page opens on the player's own row and
statistics. Choosing a row shows that player's statistics and lights the row in
olive: their name in a band in their team's color; kills, deaths and score;
accuracy for the machinegun through the napalm launcher, each icon in its
weapon color code (Appendix B.4) and dimmed until the weapon has fired; and the
awards' medals with their counts, dimmed until earned, with Capture, Assist and
Defense only in flag modes. Medals stay bitmaps, and the document's pictures
load with it inside the level load. A remote client asks the server for
statistics older than five seconds, one request at a time, as the stock page
does, and shows "-" until the answer arrives.

Mute and Friend follow the statistics as in-game plates labelled as the stock page
labels them (MUTE PLAYER or UNMUTE PLAYER, ADD FRIEND or REMOVE FRIEND). On the
player's own row both are unavailable, dimmed with their lock and reason, and
shake when chosen. Muting is the stock mute, which also tells the server.
There is no friends service on PC (the engine's is empty), so a friend is the
player's own mark, shown in the lists and on the scoreboard while the map runs;
the stock page's Friend button keeps the same mark now.

### The Vote page

The running vote comes first, beside the call a player can make:

- **The running vote.** Who called it ("CALLED BY" and the player), what it
  changes, one line each in the stock vote menu's words and order (up to
  six), the time left in the value color and the tally, then Vote Yes and
  Vote No, each with the key bound to it (F1 and F2 by default). On a remote
  client the time left counts the vote's 30 seconds (20 for a console vote)
  from when the vote reached it. Once the player has voted, both ballots lock
  and say so; spectators and a managed match get their own reasons. A ballot
  closes the menu, as the stock buttons do. With no vote running the column
  says so.
- **The call.** NEW VOTE lists the fields the drafted game type uses, one
  setting row each: the map and the game type, each a list that unfolds under
  its row; the time limit and the mode's own limit (frags, captures, the
  tournament's rounds or the Dead Zone's control time), each a slider that
  Left and Right step (Page Up and Page Down by ten); team balance and
  shuffle in team modes, restart, and buying where the mode has a buy menu,
  each a check box; and the player to kick, a list that leads with "No one".
  At most nine rows show. A field the server forbids (`si_voteFlags`) stays,
  dimmed with a padlock after its label, and the heading says it is locked by
  the server. Call Vote follows, unavailable with its reason while voting is
  off on the server, in a managed match (whose votes are Match Control
  proposals), for a spectator, while a vote runs, or until something differs
  from the server's settings.

The call is the game's: each opening of the menu drafts it again from the
server's settings, and a row's control asks the game to change one field.
The game checks every value against its field's rules (a map the drafted
game type plays, a game type the vote offers, a limit inside its setting's
range, a player on the server), so the row shows only what the game accepts.
Changing the game type lists only the maps it plays; a drafted map it cannot
play gives way to the first that can. Call Vote sends only the fields that
differ from the server's, since the server refuses a whole vote for one
unchanged field. The page opens on Vote Yes while the player can vote, and on
its first row otherwise.

### The Match page

Match Control's six sections, the stock page's Status, Teams, Proposals,
Rules, Series and Evidence, form a second, smaller strip under the card's
(decision D3): the triggers page through them, Left and Right move along the
strip and accept chooses; the bumpers, Q and E keep the main strip. The page
asks the game to project its view again as it opens (`refresh`) and opens on
the section it showed last.

Status is built:

- **The match.** The short phase as the heading, then the state lines
  Match Control writes (lifecycle and round, the last result, readiness,
  pause, the player's role and side, the timeout budgets), each wrapping, and
  the latest result under the actions.
- **The action target.** Where the player chooses the side an action
  applies to (a referee, the operator), the two sides, the chosen one
  underlined; a side the player may not choose dims.
- **The actions.** Set ready (or Set not ready), Team ready, Request
  timeout, Technical pause, Resume, Force ready, Forfeit and Abort match,
  each a row with its label and, where Match Control refuses it, the padlock
  and the model's reason beside it; choosing a refused action shakes its row
  and asks nothing. Force ready, Forfeit and Abort arm Match Control's
  confirmation and ask in the stock confirmation, with the stock page's text;
  Yes confirms it and No, or back, cancels it.
- **The referee.** While the player may sign in, a masked field takes the
  referee password, and Enter signs in with it: the session moves it to the
  game's menu (`match_referee_credential`), which the game clears as it
  reads it, and asks for `referee_login`, as the stock field and button did.
  Sign out asks the game. Either says why it is unavailable.
- **A spectator's camera.** Previous, Next and Free camera, for a human
  spectator.

Teams is built:

- **The rows.** Match Control's team and roster rows: each side's own row,
  in its team's color, with its readiness and lock, then the players with
  their side and roles, the roster seats, the invitations and the queue, each
  in the columns Match Control writes; the row chosen is lit in olive. Below
  them, the participants a substitution can bring in, and the latest result,
  so an action's outcome shows where it was taken. Choosing a row tells the
  game, which checks every action against it. The card holds 64 team rows,
  a full 32-player server's sides, participants and 30 seats, invitations or
  queue places, and all 32 participants a substitution can bring in; past
  the team rows the list ends with the way to the stock page.
- **The role.** The role an invitation or an assignment gives (Player,
  Captain, Coach or Substitute), a list that keeps its value on the game's
  menu as the stock choice does. Where a Duel side can be bound, the two
  sides take its place and Bind side shows.
- **The actions.** Join Marines, Join Strogg, Spectate, Join queue, Defer,
  Leave queue, Accept invite and Leave roster, then the actions a captain, a
  referee or the operator takes on the row chosen: Invite, Remove,
  Substitute, Assign role, Lock (or Unlock) team, Grant (or Revoke)
  broadcaster, for the operator, and Expel. Each says why it is unavailable,
  as on Status, in a column that scrolls as focus moves down it. Remove,
  Substitute and Expel ask first in the stock confirmation.

Proposals is built:

- **The running proposals.** The global proposal and the player's team's,
  each as Match Control writes it (what it changes, its ballot, the tally,
  the time left and the player's own ballot), then the ballot target, the
  proposal a ballot or a cancellation goes to, a list that keeps its value
  on the game's menu as the stock choice does, and Vote yes, Vote no, Abstain
  and Cancel proposal.
- **Making one.** The proposals the player may make, each with its ballot, a
  row chosen as on Teams, and Create proposal, which takes the team row,
  rule and value chosen where the proposal needs one, and the latest result.

Rules is built:

- **The rules.** The committed rules (the profile, whether customized, the
  boundary they apply at and their revision), the profiles and the rule
  fields with their value, a staged value after its arrow (the stock page's
  type column is left out, for the names' room); a row chosen as on Teams.
- **Staging.** The value to stage for the field chosen, a whole-number field
  the player types into (the game fills in the field's value as it is
  chosen), which keeps its value on the game's menu as the stock field does
  and refuses a number past the largest any rule takes; the staged changes;
  Select profile, Stage value, Commit rules, which asks first in the stock
  confirmation, and Discard changes; and the latest result. The game checks
  the value against the field's own bounds as it stages it.

Series is built:

- **The series.** Its state, format, score and the map in play or next, the
  map pool with each map's disposition and the side that chose it, and the
  veto and map history, each veto or map with its action or outcome and its
  side or score (the stock page's fourth columns, the starting and winning
  sides, are left out for the names' room). A map is chosen as a row is on
  Teams; the history is only to read, though its rows take focus so it
  scrolls.
- **The actions.** The format a staged series takes (best of one, three or
  five), a list that keeps its value on the game's menu as the stock choice
  does, then Stage series, Start series, Cancel series, Advance map and the
  veto's Ban map, Pick map, Decider, Start as Marines and Start as Strogg,
  each saying why it is unavailable, in a column that scrolls. All but
  Stage series ask first in the stock confirmation, with the stock page's
  text. The latest result shows under the lists.

Evidence is built: the evidence's state (recording, the match report, the
multi-view demo and the events recorded) and the most recent evidence, only
to read, with Refresh, which asks the game to project its view again.

The page asks for each action by its index in the session's
`RETAINED_MP_MATCH_TOKENS`, which holds the stock page's own fixed tokens,
and for a list's row by the list's index in `RETAINED_MP_MATCH_LISTS` and
the row's: the session sets the stock list's own selection on the game's
menu and sends its token, as the stock list does. The card never sends text
of its own, and the game checks every action again against its accepted
view. The lists hold a full server's rows
([Match list capacities](#match-list-capacities-5-november-2026)); past the
team rows or the series history's a list's link opens the stock page
(`classic_match`).

### The Settings page

The player's appearance, the other players' and the way to the rest of the
settings:

- **Player.** The name and the clan tag, text fields of the stock fields'
  23 and 12 characters (the clan may be empty) holding the settings colour
  codes and all, as the game publishes them (`mp.settings.name_edit`,
  `.clan_edit`); Enter sets the setting (`mpSettingsName`,
  `mpSettingsClan`); the model, a
  list of the models the mode and the player's team allow; the rail color,
  the seven stock swatches, the one the player's tint matches by hue standing
  taller; and the handicap, a slider from 1 to 100.
- **Controls, Game Options and System.** Plates that leave for the main
  menu's pages, as the classic page's buttons do (decision D13).
- **Opponents and teammates.** A table: the model forced on them (Off or a
  model), their outline, rim light and brightskin (Off, Low, Medium or High),
  the effect and brightskin colors (eight each), then the outline's width for
  both. The teammates' column shows only in team modes; elsewhere a note says
  why. Each list holds the classic page's own values.

The model lists and the swatches belong to the game, which builds the lists as
the classic page does and sets the model for a row (`retained appearance
<slot> <row>`) or the tint for a swatch (`retained rail <row>`). The other
rows set their settings through the adapter's `settings.player.set` and read
them back from the settings themselves. The page opens on the name.

### The Voice page

Send Voice, Receive Voice and Voice Echo as check boxes, the receive volume
(0 to 1) and the microphone's input volume (0 to 10) as sliders; then the key
bound to push to talk ("Not bound" when there is none) with Controls to
rebind it, and the microphone's level and Test Microphone. These now use the
[voice backend](../audio-voice-evidence.md#multiplayer-voice); unavailable
devices or disabled microphone access retain an explanatory disabled state.

### The Welcome card

A join's loading screen stays up, reading JOINING, until the card presents,
then fades out over it ([hand-off](retained-screens.md#hand-off-to-the-welcome-card)).
The Welcome card covers the menu the game opens when a player joins without
`ui_autoJoin`, and the menu key opens it again until the player answers: Esc
(Spectate on the prompt bar) closes it and leaves the player spectating, while
joining a team, the game or the tournament, or choosing Spectate on the page,
answers the offer, and the menu key opens Escape from then on. The header holds
the server's welcome ("WELCOME TO" and its name) and the player count; the
prompt bar has Spectate, Tabs and Select, then Main Menu and Leave Server,
which asks first in the stock confirmation.

- **Join.** The mode and map, then the match state, the time left in the value
  color, the limit and the player count. Team modes show the two team cards,
  each the plate that joins its team: the header band in the team's color with
  its faction mark (the HUD's Marine or Strogg emblem), name and score, its player count and, in CTF, its flag's state (at
  base, taken or dropped). A team the stock balance rule refuses dims with its
  lock and reason and shakes when chosen. Auto join follows, focused, naming
  the team it picks, then Spectate. Deathmatch shows the three leaders with
  their scores, then Join game, focused, and Spectate; Tourney shows the arenas
  in play with their scores and joining or leaving the tournament.
- **Server.** The Escape card's Server page.
- **Players.** The Escape card's lists, the speaker and friend symbols, score
  and ping, spectators below, with nothing to choose: its tab keeps the focus.
- **Settings.** The name, clan, model and rail color rows of the Escape
  card's Settings page, then the crosshair: a slider through the stock
  picker's twenty crosshairs, 0 for each weapon's own ("By weapon"), the
  chosen one shown beside it (`retained crosshair <index>`). Controls, Game
  Options and System follow, standing for All settings.

The game publishes the Welcome card's keys with the rest: `mp.welcome.title`,
`mp.welcome.players`, `mp.welcome.match`, `mp.welcome.state`,
`mp.welcome.limit`, `mp.welcome.team_mode`, `mp.welcome.team<0-1>.{score,count,flag}`,
`mp.welcome.leader<0-2>.{name,score}`, `mp.welcome.arena<0-3>` and
`mp.welcome.arena_count`, and the Join page's four actions as
`mp.join<0-3>.{shown,available,label,reason,detail}`, which the game derives
again when one is chosen.

### The summary card

At the end of a match the summary card covers the game's match summary
(`guis/summary.gui`, section 14.14) as the Escape card covers its menu
(`guis/menu/mp_summary.q4ui`, built by `tools/ui/retained_mp_summary.py`). It
presents with `ui_retained` alone, since all its pages are built, and the
stock summary stays for `ui_retained 0`, a mod's own `summary.gui` and an
Arena Campaign match. It takes the Escape card's size, so the Players page it
shares fits, and opens on SUMMARY with the chat field focused; Esc returns to
the game, as the stock summary's does. The header holds the map, the mode, the
time to the next map in the value color (the review's end,
`g_gameReviewPause`) and the score.

- **SUMMARY.** The result in a header band across the page, in the winners'
  color, olive for deathmatch, Tourney and a draw: MARINE TEAM WINS, STROGG
  TEAM WINS, DRAW, the leader's "<name> wins!" or a managed match's own result
  (pending, aborted, a draw or its winner, with MATCH FORFEITED as its detail).
  Outside team modes your place follows ("2ND OF 4"), then the final score in
  the value color. Under it the standings and the chat stand side by side.
  The standings take the stock summary's rows: outside team modes the top ten
  with their places, the first three on the stock's blue, red and yellow bands
  at 0.15; in team modes each team's top five, the winners first, under their
  team bands. You take the last row when you would not be listed, with the
  orange marker on a band at 0.29. Each row holds the name, the clan, the
  player's three most accurate weapons of more than ten shots as icons in their
  color code, and the score. The chat shows CHAT HISTORY over the newest ten
  messages that fit, oldest first and wrapping, in the chat box's lines: white
  names over #FFFF8D all-chat, a team message's TEAM tag and text in its team's
  color, server lines after the information symbol, your lines after the
  marker. A message cut at the top fades under the card's body.
- **The chat field.** The stock chat plate with its channel tag, inviting a
  message while it rests empty. Choosing it (`mpSummaryChat`) starts typing,
  which the session owns: typed characters enter as UTF-8 at the caret, up to
  the chat input's 128 bytes; Backspace, Delete, the arrows (by words with
  Ctrl), Home, End and Ctrl+V edit; Tab switches to team chat in team modes;
  Enter sends through the stock summary's own `chatmessage` and keeps typing,
  an empty Enter stops; Esc stops and clears; a click elsewhere stops and keeps
  the entry. The text lives in the stock summary's `chattext`, whose entry
  takes focus while the player types so the platform's text input runs then
  only (`oq4_chatFocus`, `oq4_chatBlur`). While typing, the prompt bar reads
  Send, Channel (team modes) and Cancel, and from 80% of the limit the
  characters left show.
- **STATISTICS.** The Players page's lists and the selected player's
  statistics, with the player's end-of-match awards (Rail Master, Sniper and
  the rest) where the Players page has Mute and Friend.
- **SCOREBOARD.** The lists in the held scoreboard's rows and columns: the
  speaker, friend, name, the clan or a BOT tag, SCORE, KILLS in the flag modes
  (as the scoreboard decides), DEATHS, minutes and ping beside the
  network-quality bars; each list opens with its band, the spectators' only
  while there are any. Fixed headings stay above the scrolling rows. The scrollbar
  takes navigation when the list overflows; otherwise the tab keeps focus.

The game publishes the summary with the lists: `mp.summary.result`,
`_color`, `_detail` and `_score`, `mp.summary.teams`, the panels
`mp.summary.p<0-1>.{shown,band,title,color,score,count}` and their rows
`mp.summary.p<p>r<r>.{name,clan,score,place,you,tint,w0,w1,w2}` (the weapons
as indices into the Players page's accuracy order, -1 for none), the chat
`mp.summary.chat<0-9>.{live,name,text,kind,team,you}`,
`mp.summary.my_team` and `mp.summary.channels`; the lists add
`mp.players.<row>.{clan,kills,deaths,time,bot,ping_ms}` (the ping as a number
for the bars), `mp.players.kills_shown`
and the selected player's `mp.stat.match_award<0-2>`. The session publishes
the field: `mp.summary.typing`, `mp.summary.entry_before` and `_after`
(around the caret), `mp.summary.entry_left` and `mp.summary.entry_team`.
`openq4_summaryChat <text> | caret <n> | stop | send` drives the field
without device input.

### The Server page

The server's name and address, its message (`si_motd`, three lines) in
#FFFF8D, the rules in two columns (mode, map, limits, players, friendly fire,
team balance and the next map in `si_mapCycle`) and the map rotation, the
current map in orange. Each column's labels take the width of the longest in
any language. The page has no actions; its tab keeps the focus.

### The Admin page

The stock page (`p_admin`) asked for the server's remote console password,
then offered Players, Server and Console. The card's page
(`tools/ui/retained_mp_admin.py`) keeps that order:

- **The password.** Until the player is an admin the page shows the server's
  name and address, ENTER RCON PASSWORD over a masked field (64 bytes) and
  ACCEPT. Enter, or ACCEPT, asks the session (`mpAdminPass`), which moves the
  password to `net_clientRemoteConsolePassword`, where the stock field kept
  it, clears the card's copy at once and asks the game to check it
  (`retained adminPass`, the stock `checkAdminPass`). The page says it is
  checking, and says so when the server refuses it. A listen server's host
  is the admin from the start; a client stays one, once the server accepts
  its password, until the map changes.
- **Players.** The players by client number, each row the scoreboard's with
  the player's team, the chosen one lit; a row's Accept names its client to
  the game (`retained adminSelect <client>`). Opening the page on Players
  focuses the chosen player's row, which scrolls a long list to it, as the
  Players page reveals its selected row. Kick, Ban and Force team switch
  are the card's action plates, each with the chosen player beside it or,
  unavailable, why: no player chosen, the player's own row, a mode without
  teams, a managed match. They run the stock page's own actions, and the menu
  stays open. Kick and Ban ask first in the stock confirmation, which the
  stock page did not (section 14.18: destructive actions use the stock
  modal): titled as the plate, the question (`#str_231590`, `#str_231591`)
  and under it the player's name in the value color. The dialog captures the
  chosen player's client, connection identity and name as it opens, so YES
  acts on exactly the player it names even if the selection changes beneath
  it, and the game refuses the request if that player has left or their slot
  was taken. NO and Back send nothing. View ban list asks a client's server for its list and opens
  PLAYER BAN LIST, the Create Server page's dialog over the game's list,
  where a row's Accept chooses a ban and REMOVE BAN lifts it.
- **Server.** The stock page's settings drafted as the Vote page drafts a
  call: map and game type lists, the time limit and the limit the drafted
  game type uses, then balance, shuffle, restart and buying where it uses
  them, each value checked by the game against its setting's range. Apply
  changes, unavailable until something would change, sends the draft as the
  stock page's Apply did (`HandleServerAdminCommands`) and closes the menu;
  the server restarts the map for a new map, game type or buying. The
  controller's Menu button presses Apply changes while there is something
  to apply, and otherwise closes the card ([Menu button](input-routing.md#menu-button-24-october-2026));
  for a controller the plate ends with the Menu button's glyph.
- **Console.** The remote console's twelve newest lines in the stock
  console's frame, and a field whose Enter runs the command through the
  stock console's own input (`admin command`): on a listen server, the
  command itself, its output redirected to the page; on a client, through
  the server's remote console.

The triggers step the sections, as on the Match page. The game publishes the
page as `mp.admin.*` (`PublishRetainedAdmin`) and checks every command again
(`HandleRetainedAdminCommand`): every one but the password check needs the
player to be an admin.

The password check itself needed engine work. The stock page sends
`rcon verifyRconPass` when the player chooses Accept, then waits for the game
to hear the answer (`idGame::ProcessRconReturn`), which opens the admin
controls or returns to Join Team. Quake 4's engine answered that check;
openQ4's ran it as an unknown command and never passed an answer on, so the
page waited for good. Now:

- the server answers the check with the stock "rcon verified" string
  (`#str_107250`) without running anything, once the password is accepted
  (`idAsyncServer::ExecuteRemoteConsoleCommand`, rcon2 and legacy alike);
- the client hands that answer, or the bad-password reply (`#str_04847`, or
  Quake 4's `#str_104847`), to the game when it comes from the server the
  check went to, and answers false itself on the next frame when it refuses
  the check before sending (no password, one shorter than rcon2's 12 bytes,
  no port or address) or after rcon2's 10-second request timeout
  (`idAsyncClient::AnswerRconVerify`);
- the client hands the server's remote-console output to the game, as Quake 4
  did, so the stock page's console shows it.

The stock field takes 16 characters; a longer password set at the console
works when the player chooses Accept without typing in the field. The card's
field takes 64.
`tools/tests/async_rcon_verify_contract.py` compiles the client and server
code with stand-ins and drives both ends; `--mutations` reverts each rule.

### The game's data

The game publishes everything the card shows through one cache, writing a key
only when it changes and moving `mp.revision` when anything did:

- the header: `mp.title`, `mp.mode`, `mp.clock`, `mp.score`;
- the Team page: `mp.team.band`, `mp.team.band_color`, `mp.team.band_score`,
  `mp.team.band_detail`, `mp.action<0-2>.{shown,available,label,reason,detail}`
  and `mp.chat<0-2>`;
- the Server page: `mp.server.{name,address,message}`, `mp.rule<0-6>`,
  `mp.rotation<0-15>`, `mp.rotation_count` and `mp.rotation_current`;
- the Players page: per list (`a` the player's own team or everyone, `b` the
  other team, `s` the spectators) `mp.players.<list>.{shown,title,color,score,count}`
  and 32 rows `mp.players.<list><0-31>.{client,name,score,ping,local,friend,muted}`;
  the statistics `mp.stat.{client,name,color,kills,deaths,score,flag_mode}`,
  `mp.stat.acc<0-9>` and `mp.stat.award<0-7>`; and Mute and Friend as
  `mp.mute.*` and `mp.friend.*`, shaped like the Team page's actions;
- the Vote page: the running vote as `mp.vote.{running,caller,time,tally}`,
  `mp.vote.line<0-5>` and `mp.vote.line_count`, the ballots as
  `mp.vote.yes.*` and `mp.vote.no.*`, Call Vote as `mp.vote.call.*` (shaped
  like the Team page's actions); and the draft, each field's value as
  `mp.vote.<field>` with `mp.vote.<field>.{row_shown,row_allowed}`, the lists
  as `mp.vote.{map,gametype,kick}<row>` with their counts
  `mp.vote.{map,gametype,kick}_count` (the kick count includes "No one"),
  and `mp.vote.locked`.

- the Settings pages: `mp.settings.{name,clan,rail}`, each model list as
  `mp.model<slot>.<row>` with `mp.model<slot>_count`, the row it holds as
  `mp.model<slot>` and `mp.model<slot>.row_shown` (slot 2, the teammates', in
  team modes only), and the crosshair as `mp.crosshair`,
  `mp.crosshair_count` and `mp.crosshair_image`.
- the Match page (Escape only): Match Control's own projection of its view
  on the game's menu (`MPMatchControlProjectMenu`: localized, recipient-
  scoped, each action's availability and reason decided by the typed model)
  mirrored as `mp.match.{available,phase,result,follow}`,
  `mp.match.status<0-5>`, `mp.match.side.{shown,label}` with
  `mp.match.side<0-1>.{label,available,selected}`, and for each action
  `mp.match.op.<name>.{shown,available,label,reason,detail}`
  (`RETAINED_MATCH_OPERATIONS`); the team and roster rows as
  `mp.match.team.{count,more,selected}` with
  `mp.match.team<row>.{c0,c1,c2,kind,side}`, the participants a substitution
  can bring in as `mp.match.replacement.{count,more,selected}` with
  `mp.match.replacement<row>.c0` (each list's columns as the projection
  writes them, split at its tabs; up to `RETAINED_MATCH_TEAM_ROWS` and
  `RETAINED_MATCH_REPLACEMENT_ROWS`), and the role choice's value as
  `mp.match.role`; the running proposals as
  `mp.match.proposal.{global,side}`, the ballot target as `mp.match.scope`
  (0 global, 1 the team's) and the proposals to make as
  `mp.match.proposal.{count,more,selected}` with
  `mp.match.proposal<row>.{c0,c1}`; the committed and staged rules as
  `mp.match.rules.{summary,staged}`, the value to stage as
  `mp.match.rule_value`, the profiles as `mp.match.profile.{count,more,selected}`
  with `mp.match.profile<row>.c0` and the rule fields as
  `mp.match.rule.{count,more,selected}` with `mp.match.rule<row>.{c0,c1,c2}`;
  the series as `mp.match.series.summary`, its format as
  `mp.match.series_profile` (0 to 2), the map pool as
  `mp.match.series_map.{count,more,selected}` with
  `mp.match.series_map<row>.{c0,c1,c2}` and the history as
  `mp.match.series_history.{count,more,selected}` with
  `mp.match.series_history<row>.{c0,c1,c2}` (the first three of the stock
  page's four columns); the evidence's state as
  `mp.match.evidence.summary` and the recent evidence as
  `mp.match.evidence.{count,more,selected}` with `mp.match.evidence<row>.c0`.
  Players' names reach the card as plain text, which never translates. The
  game projects its menu again whenever the accepted view has moved since it
  last did.

The session writes the names of the keys bound to the ballots and to push to
talk, `mp.keys.vote_yes`, `mp.keys.vote_no` and `mp.keys.voice_chat` with
their `_bound` flags, when the card opens: the engine's key lists name keys by
number, which only the stock GUIs draw. The pages read the player settings
they change from the settings themselves (`cvar.<name>` state, the lists' as
text).

Player and server text (player names, the server's name and message, the
chat) is cleaned as the loading screen's server card cleans it: colour codes,
controls and malformed UTF-8 dropped, its lines and bytes bounded, and a
leading `#str_` broken so a name never translates.

### Width

The specified 648 dp cannot hold eight tabs: with the active tab's flare and
shoulder each tab needs its label plus 31 dp, and even the shortest English
labels need about 800 dp at 14 dp, so the Escape card grows to the widest
language's fitted strip. Neither card's prompt bar fits the specified widths
either. `tools/ui/retained_mp_menus.py` measures every language's labels in
the shipped faces and sizes each card to the wider of its widest tab strip
(plus a 2 % margin) and its widest prompt bar (plus 4 dp) inside the card's
insets: today Escape is 907 dp and Welcome 873 dp, both set by the Spanish,
German and Ukrainian prompt bars. A controller's glyphs are 22 dp each,
narrower than the keycaps they replace in every language's bar (Escape's
widest is 780 dp against 854 dp). The generator fails if a glyph bar would
outgrow its keycap bar. The build also fails if a language would need more
than a 4:3 view allows (928 dp, 16 dp from each side). Each tab is as
wide as its label in the running language, and the tabs share the remainder.
The Welcome card keeps Escape's 477 dp height instead of its specified 402 dp,
since its Players and Server pages need the room.

Evidence for the cards' controller glyphs: `.tmp/ui/retained-card-glyphs-evidence/validation-evidence.json`,
SHA-256 `a8373b906eb04c74ddcd5de1d8d3e1dc6edcead176b5ccdcd2769b16f8a4cd75`.

## Input

The card owns the menu's input while it covers it. Q and E, and the gamepad
shoulders, switch tabs (the document's `onTabPrevious` and `onTabNext`), as do
the strip's keycaps and the tabs themselves; the strip wraps both ways. A
switch is at once: the arriving page fades in over 150 ms while the leaving
page fades out and takes no input, and the arriving page's primary action
takes focus. Back closes the Disconnect confirmation first, then resumes the
match. A function key the card leaves alone runs its binding, as over the
stock menu, so F1 and F2 still vote.

The card's verbs are the stock menu's own commands with its selection sound:

| Verb | Game command |
| --- | --- |
| `mpClose` (Resume, Back) | `play main_menu_selection ; close` |
| `mpMainMenu` | `play main_menu_selection ; mainMenu` |
| `mpDisconnect` (after the confirmation) | `play main_menu_selection ; disconnect` |
| `mpStockPage` | the stock page's own button, by `card.stock_page` |
| `mpTeamAction` | `play main_menu_selection ; retained team <slot>`, by `card.team_action` |
| `mpSelectPlayer` | `play main_menu_selection ; retained select <client> <identity>`, frozen event operands |
| `mpMute` | `play main_menu_selection ; retained mute <client> <identity>`, frozen event operands |
| `mpFriend` | `play main_menu_selection ; retained friend <client> <identity>`, frozen event operands |
| `mpWelcomeAction` | `play main_menu_selection ; retained welcome <slot>`, by `card.welcome_action` |
| `mpVoteYes`, `mpVoteNo` | `play main_menu_selection ; retained vote yes\|no` |
| `mpCallVote` | `play main_menu_selection ; retained callVote` |
| `mpRail` | `play main_menu_selection ; retained rail <row>`, by `card.rail` |
| `mpSettingsControls`, `mpSettingsGame`, `mpSettingsSystem` | `play main_menu_selection ; mainMenu fromMp_toControls\|fromMp_toGameoptions\|fromMp_toSystem` |
| `mpMatch` | `play main_menu_selection ; matchControl <token>`, by `card.match_op` (the refresh without the sound) |
| `mpMatchSelect` | the stock list's `<list>_sel_0` on the game's menu set to `card.match_row`, then `play main_menu_selection ; matchControl <select token>`, by `card.match_list` |
| `mpMatchRole <role>` | the game's menu's `match_role_choice` set to the role, 1 to 4, quietly; nothing reaches the game |
| `mpMatchScope <target>` | the game's menu's `match_proposal_scope_choice` set to `global` (0) or `side` (1), quietly |
| `mpMatchRuleValue <value>` | the game's menu's `match_rule_value` set to the value, 0 to 10000, quietly |
| `mpMatchSeriesProfile <format>` | the game's menu's `match_series_profile_choice` set to `best_of_one` (0), `best_of_three` (1) or `best_of_five` (2), quietly |

The Vote page's rows are value controls. Their actions use the adapter's
`session.menuValue` operation, which carries the control's new value with a
verb: `mpVoteMap`, `mpVoteGameType`, `mpVoteTimeLimit`, `mpVoteFragLimit`,
`mpVoteCaptureLimit`, `mpVoteTourneyLimit`, `mpVoteControlTime`,
`mpVoteBalance`, `mpVoteShuffle`, `mpVoteRestart`, `mpVoteBuying` and
`mpVoteKick`. The adapter accepts only these verbs, with a whole number from
-1 to 999 or a Boolean, and queues `<verb> <value>`; the session maps the
verb to its field (`RETAINED_MP_VOTE_FIELDS`, in the game's field order) and
sends `retained voteSet <field> <value>` without a sound, keeping the menu
open. A list sends its row (the kick list's "No one" is -1), a check box 1 or
0, a slider its value. The Settings pages' model lists and crosshair use the
same operation with `mpModelSelf`, `mpModelEnemy`, `mpModelTeam` and
`mpCrosshair` (`RETAINED_MP_APPEARANCE_VALUES`): the session sends `retained
appearance <slot> <row>` or `retained crosshair <index>` for a value of 0 or
more. The Match page's role, ballot target and series format use it with
`mpMatchRole`, `mpMatchScope` and `mpMatchSeriesProfile`, and its rule value
with `mpMatchRuleValue`, whose value runs
from 0 to 10000; the session keeps each on the game's menu, as the stock
controls do. A number field's value comes back through the card's state a
frame or two after the session takes it, and the runtime accepts a number only
once it reads it back, so the adapter acknowledges a number field's request
then, and refuses it if the value has not come back within a second; a list or
a check box is acknowledged as its request is queued.

The other settings rows use the adapter's `settings.player.set` operation, a
literal setting with the control's value. The adapter allows only the
player settings these pages change, each with what its classic control
offers: the handicap's whole numbers from 1 to 100, the voice volumes' ranges,
the voice switches' Booleans, and each appearance list's own values (`0`,
`0.35`, `0.7` and `1` for the strengths, the eight stock colors, `1.0` to
`6.0` for the outline's width), which it compares as text.

A row records its player's client number before it asks, and Mute and Friend
record the selected player's. The session refuses a client outside the
server's slots; the game acts only on a client with a row now, never mutes or
befriends the player's own row, and none of the three closes the menu.

A hand-off presents the stock menu at once and presses the page's button as
soon as the menu's opening shows it, within 500 ms; the stock menu then keeps
the menu until it closes. The session refuses the card's verbs from any other
GUI and every other verb from the card.

## Motion

Opening ramps the softening (`modal.softfocus`: a 5 u blur at 0.80 saturation,
never dimmed) over 250 ms while the card rises 12 dp and fades in over 150 ms,
easing out. Every way out closes the card at once. Closing the menu over the
running game releases the softening over 250 ms while the session keeps
drawing the closed card's backdrop, as the pause screen does; the next
activation brings it back. Under reduced motion the card does not rise, and
the softening and the fade take 80 ms. Without softening (the renderer cannot,
or `ui_retainedOpaqueBacking 1`) a centred darkening scrim with a vignette
stands in.

## Decisions taken provisionally

The plan left these to the owner. The card is now the default in-match menu,
so changing one of these changes what players see; each stays open to the
owner's review:

- **Width (D8).** As above: the card grows past 648 dp to fit the strip.
- **Match sections (D3).** The triggers page Match Control's sections, as
  Left and Right do along the inner strip; section 14.18's bumpers keep the
  main strip. The inner strip ends with the triggers' glyphs for a
  controller ([trigger glyphs](input-routing.md#trigger-glyphs-24-october-2026)),
  as the Admin page's does.
- **Match actions.** Rows with their reasons beside them rather than the
  stock page's buttons and shared Action line: the labels run too long for
  plates in French and German, and a reason beside its action needs no
  pointing. Teams' sixteen actions share one column that scrolls, as do
  Series' nine. Every section's reason column holds every reason a row can
  show, in every language: in two lines where the column is wide, in three
  on the scrolling columns' taller rows (the generator refuses a layout
  where one would be cut off).
- **Match lists.** Match Control's lists as the scoreboard's rows in a
  scrolling column, up to a set number of rows each (32 team and roster rows,
  16 participants, every proposal, profile and rule field, 16 maps, 24
  history entries and all the recent evidence), after which the stock page
  shows the rest. The Series lists leave out the stock page's fourth columns
  and the rule list its type column, for the names' room.
- **Rule value.** A whole-number field from 0 to 10000, the largest value a
  rule takes; the stock field takes any text and the game checks it.
- **Tab labels (D7).** Marine 14 dp, short labels written for the strip; the
  active label is white.
- **Softening (D1).** `modal.softfocus` values, with a centred scrim and
  vignette in one layer as the fallback.
- **Prompt bar (D4).** Only the global prompts, with Main Menu and Disconnect
  as actions; section 14.18 governs over `WID-019`.
- **Motion (D6).** No card exit motion; reduced motion as above.
- **Tabs per map (D18).** The card remembers its tab while a map runs and
  starts each map on Team.
- **Welcome (D10).** Welcome while `ui_joined` is 0 and the player spectates,
  or while the join offer stands; it re-offers after every map change, as the
  join card did.
- **Card sizes.** Each card fits its widest prompt bar; Welcome takes
  Escape's height (above).
- **Settings pages.** Opponents and teammates share one table rather than
  the classic page's tabs; labels take two lines at 14 dp, so the values
  keep their room in every language; the model lists hold 24 rows (the stock
  game has 18 models); the crosshair is a slider, not a picker with arrows;
  Welcome's All settings is Controls, Game Options and System, since the
  main menu has no settings landing to open; and push to talk hands off
  until key binding fields exist.
- **Vote page.** The rows are the runtime's value controls: lists for the
  map, game type and kick, check boxes for balance, shuffle, restart and
  buying, sliders for the limits (time 0 to 60 minutes, frags 0 to 100,
  captures 1 to 50, tournament rounds 1 to 20, control time 10 to 600 seconds
  in tens; a server's setting outside a slider shows as it is until changed).
  Rows show only for the fields the drafted game type uses. The ballots and
  Call Vote close the menu, as the stock buttons do; the rows keep it open.
  The heading of the call reads NEW VOTE, so it never repeats the button.
- **Players page.** Rows keep a 24 dp pitch and scroll below fixed headings;
  the earlier 14-to-24 dp compression was replaced on 23 October. The lists
  hold their order while the menu stays open; Mute and Friend refuse the
  player's own row; and Friend is the player's own mark, since PC has no
  friends service.

## Full roster scrolling, 23 October 2026

This UI-03 increment begins at engine
`f5bd60838ae25961fb9392ba71a9ab6c6a9c1437`. The game publishes up to all 32
connection slots per player list and kick-choice catalog. Welcome, Escape's
Players page and the summary's Statistics and Scoreboard share fixed headings,
24 dp rows and the existing vector scrollbar. Their body clips separately from
the headings and statistics. Home/End, paging and focus reveal reach the late
rows; score refreshes keep scroll position, adapter snapshots restore it, and
roster shrinkage clamps obsolete offsets. No new display strings or bitmap art
are introduced.

`UiRetainedRosterChecks.inl` executes the production documents through the real
Runtime/Rml layout. It covers 32 entries in one list, ten players on each team
plus twelve spectators, the selected last row, fixed headings, score refresh,
short-list clamping, and the last of 31 other clients in the kick chooser. The
existing card, vote/match, settings/Welcome, summary and pointer-reach suites
remain part of the regression set. The session gate checks the game/document
catalog agreement and existing routing.

`tools/ui/capture_multiplayer_rosters.py` uses the staged engine on `mp/q4dm1`
with a live three-player menu, then supplies explicit 32-slot fixtures to the
same production documents through `testGUI`. Every capture comes from the
engine's `screenshot` command. Its readback oracle checks all four views at the
top, end, restored end, mixed-team end and after the list shrinks. The companion
oracle tests reject missing stages, stale offsets, clipped rows, moving headings
and failed semantic operations.

The full Windows build and staging pass, followed by all six native suites,
the session gate, four capture-oracle tests and a clean generated-document
check. OpenGL at 1280×720 and Vulkan at 1920×1080 each pass all 20 stage
readbacks. All 30 engine screenshots were visually reviewed: the final row and
spectator remain fully visible, the headings stay fixed and the scrollbar
does not cover the rows. Runtime binaries and source documents remained
unchanged during each capture run. The source-bound summary, logs, captures
and review sheets are retained under `.tmp/ui-roster-capacity/`, with
`validation-evidence.json` binding the completed checks. Both runs report zero
errors and zero retained-UI warnings; each retains 190 existing stock/bot
precache warnings, including missing AAS data. The debug build does not provide
optimized performance evidence.

This is bounded menu coverage. The server's current `si_maxPlayers` CVar still
caps configured matches at 16; the 32-slot fixtures do not qualify a 32-client
network session. Full slot-identity safety, map catalogs beyond 128, physical
devices, portrait/long-language layouts and optimized performance remain with
UI-03/UI-02/UI-07. No requirement, resource or final gate is accepted here.

## Faction marks, 5 November 2026

The Welcome card's team cards and the Players lists' Marine and Strogg bands
now carry their faction marks, as section 6 and the scoreboard's panels do:
the HUD's `marinelogo` and `strogglogo` vectors in white, 20 dp on a team
card before its name and 14 dp on a band before its title. Spectator and
deathmatch bands carry none; every band's title starts after the mark's
room, so the titles align. The Escape card grows by about 35 KB.

Native checks place each team card's mark in its band before the name and
show only the band's own team's mark. A Vulkan Team DM listen server with
four bots, in German with `ui_autoJoin 0`, shows both marks on the Join page's
cards and on the Players page's bands (`.tmp/ui-roadmap/faction-vk/`).

## Players list marks and team refusals, 5 November 2026

The Players lists (Escape, Welcome and the summary's Statistics page) now
carry the held scoreboard's marks. The game publishes more states per row
beside its existing `bot` (`PublishRetainedPlayers`): `dead`, for a playing
player at no health or a Tourney player eliminated (spectators never count
as dead); `bars`, the network-quality step with the held scoreboard's
thresholds (4 to 60 ms, 3 to 120, 2 to 200, then 1; 0 for a bot or an empty
row); and `bars_r`, `bars_g` and `bars_b`, that step's color (#B5C784 for
three or four, #E3AD36 for two, #E46D56 for one).

- A dead row shows the skull in its own 18 dp slot after the friend symbol
  and stands at 0.50.
- A bot's name is followed by the BOT tag (`#str_231231`), dark Lowpixel on
  an olive plate at 0.85. Here the plate is the label's own background, so
  it lacks the held scoreboard's 4 dp cut corner: a vector plate would cost
  each row another node.
- Every other row shows the four bars between the score and the ping, lit to
  its step, unlit bars at 0.20 of the same color.
- The name keeps at least 150 dp (155.7 dp on the Escape card), which the
  generator enforces in every language.

Each card declares 96 rows (32 for each list), so the marks are built for
size. Each bar is one rectangle whose color binds to the game's published
color: the held scoreboard's four drawn variants cost about 8 KB a row, and
choosing the color in the document about 3 KB more. The marks still add about
11 KB of source and seven nodes a row: Escape grows from 12.55 to 13.85 MB
(6,932 to 7,604 nodes), the summary from 7.52 to 8.69 MB (4,142 to 4,814) and
Welcome from 2.45 to 3.50 MB (1,555 to 2,227). Document loads
(`openq4-retained-ui-benchmark --load`) grow with them. On this machine
Welcome went from about 85 to 125-150 ms of document time in debugoptimized
builds. The measurements ran beside other sessions' builds and spread
widely, so an idle-machine figure remains part of UI-07.

The stock team-refusal messages (`#str_202039` to `#str_202042`: a team with
too many players, or the one the player is already on) were missing from the
French and Italian tables, so those players read them in English on the
Welcome card's team cards. The openQ4 tables now supply them.

The native card test publishes a dead player, a bot and the bars' four steps
with their colors. It checks the skull and dimming, the tag in place of the
bars, each step's lit count and color, and the columns' order under their
headings.

In the engine, `tools/ui/capture_multiplayer_rosters.py` on OpenGL at 1280x720
(staged at local `94b9daad5` with the working tree) passed all its checks
in 15 captures. Its live deathmatch on `mp/q4dm1` shows both bots with BOT
tags and no bars, and the local player, just killed, with the skull, the
dimmed row and four lit bars. The 32-slot fixture views publish no step and
show no bars. Evidence: `.tmp/ui-roadmap/rosters-gl/result.json`, SHA-256
`db2201fbc8c93e614410a1f9e962fbb10fad3ffa0a2e60f7473fabe738941673`.

## Kick and Ban confirmations, 5 November 2026

Kick and Ban on the Admin page asked nothing first, as on the stock page, so
one press removed or banned a player. They now open the stock confirmation
described under [the Admin page](#the-admin-page), naming the player the
dialog captured. The new questions are in all twelve shipped languages and fit
two lines at 100 % text in each; the generator refuses one that would not. Above
100 % the question and the name grow and the dialog with them.
Force team switch still acts at once.

The native Admin test opens each dialog, checks the name it shows, answers
NO (nothing is sent), opens it again, changes the selection beneath it and
answers YES: the request carries the player the dialog named. A fresh press
on either plate opens its dialog rather than sending, alongside the existing
held-press identity checks.

The Admin page's player list did not bring the chosen player into view when it
opened, so with a long list the selection could sit below the fold. Opening the
page on Players now focuses that row (focus reveal scrolls it into view). A
native check reopens the page with 30 players and the 29th chosen; the same
check fails against the earlier card.

The `mpadmin` engine capture now stops at the dialog and then presses YES:
on Vulkan and OpenGL at 1280x720, on a listen server with bots, the dialog
names the chosen bot, YES sends `mpAdminKick` and the server kicks it. The
logs carry no retained-UI warnings. Evidence: `.tmp/ui-roadmap/admin-vk/` and
`.tmp/ui-roadmap/match-gl/mpadmin/`.

## Match list capacities, 5 November 2026

The Match page's lists held fewer rows than Match Control can publish: 32
team rows, though a full server's two sides and 32 participants alone need
34; 16 replacements of 32; 6 proposal templates, so the 7th (the side-scoped
Forfeit ballot of `94b9daad5`) handed off; and 16 of a map pool's 32 maps.
The card now holds 64 team rows, 32 replacements, every proposal template
(7), 32 pool maps and, as before, 24 steps of series history. The generator
reads the counts from `MultiplayerGame.h`, and `PublishRetainedMatch`
asserts each against Match Control's model at compile time: equal for the
replacements, proposals, profiles, rule fields, pool and evidence; at least
both sides and every client for the team rows; and no more than the model
for the history.

The model's own bounds are larger (128 team rows and 128 history steps), but
the Escape card cannot hold them. At those counts it measured 16.4 MB, past
the runtime's 16 MiB document source limit, and it no longer loaded. The card
is now 14.9 MB (13.5 MB before). Team rows past a full server's, or a longer
series history, still end their list with the way to the stock page.

The native Match checks confirm each list's last declared row and that none
follows it.
The 14.9 MB card loads and presents in the engine: the `mpcard-triggers`
capture opens the Match page on an OpenGL listen server
(`.tmp/ui-roadmap/match-gl/mpcard-triggers/`).

## Map catalogs, 24 October 2026

The Vote and Admin pages' map choices held 48 maps a game type, so a server
with a larger rotation offered only its first 48. The game's cap
(`idMultiplayerGame::RETAINED_VOTE_MAPS`) is now 128, and the generators read
it from the header for both choices' option slots, so the document and the
game cannot drift. The choice's popup still shows eight rows and scrolls
through the rest. The cap only bounds the host's local lists and their
publication; nothing on the wire depends on it. The Escape card grows by
about 0.8 MB (11.1 to 11.9 MB of source). A native check confirms both
choices hold 128 options; stock content has fewer maps than the old cap, so
no capture shows a list past it.

Evidence: `.tmp/ui/map-catalogs/validation-evidence.json`, SHA-256
`0739e6a2c678b065b424e8653d28ba5265f4f726807452b5fa5c7865ef31fe45`.

## Validation

- `tools/tests/ui_retained_gate.py` compiles the cover, the retirement, the
  verbs and the hand-off against counted doubles: the gate and the opt-in,
  every fallback, a refused cover, the revision-driven publishing, Resume's
  release in the same frame, Main Menu without one, the hand-off and its
  500 ms wait, out-of-range pages, a map change, the screens that are never
  covered and the refused verbs. It also pins the missing-pages list to the
  document, the stock buttons to the generator, the game's side of the
  protocol, and the frame, input and draw order.
- `openq4-ui-retained-screens-mp-card` loads the card into the real runtime: centered
  and the same size at 1280x720, 1920x1080 and 1024x768, the opening and its
  reduced-motion form, the tab programs and their wrap, the cross-fade with
  the leaving page out of reach, the hand-off and Back actions, the prompt bar
  and the confirmation, the header's clipping title, and the release.
- A live probe on a Team DM listen server with bots (hidden window, console
  verbs only) opens the card over the match on OpenGL in English, Hungarian
  and Russian and on Vulkan in German: the card covers the menu, the tabs
  switch and wrap, Back closes the confirmation and then resumes with the
  release, and a hand-off opens the stock Players page.

Evidence: `.tmp/ui/retained-mp-escape/validation-evidence.json`, SHA-256
`39b8f759d2e6445b321c64507f125dcc015334dbbcb6ffaf0bb363df6e3255cf`.

The Team and Server pages add:

- gate cases for `mpTeamAction`: the slot the card records reaches the game as
  `retained team <slot>`, the menu closes when the game acts and stays open
  when it refuses, and slots outside the three are refused; pins that the
  game derives the slot's action again before acting and closes the menu
  after it, and that the session, the game and the document agree on three
  slots;
- native checks of the Team page (the band's color, an available action with
  its detail, an unavailable one dimmed with its lock and reason, an empty
  slot hidden, the action an available slot asks for, the shake instead for
  an unavailable one) and the Server page (the rotation's current map in
  orange, only the rotation's maps shown, the message above the rules, the
  tab keeping the focus);
- a generator check that every action label, with each team's name, fits its
  plate in every language;
- a live probe on a Team DM listen server with a server message and a map
  cycle: the band and the actions as a team player, the balance rule
  refusing a switch with its reason, Ready during the warm-up, Spectate
  taken through the game and closing the menu, the spectator's two join
  actions, a join closing the menu again, and the Server page's message,
  rules, next map and rotation.

Evidence: `.tmp/ui/retained-mp-team-server/validation-evidence.json`, SHA-256
`470d27af66c563373136d5db16f862eb0d3f1c935e3b1dfe6609896f18b56fb3`.

The Players page adds:

- gate cases for `mpSelectPlayer`, `mpMute` and `mpFriend`: the client the
  card records reaches the game as `retained select|mute|friend <client> <identity>`
  without closing the menu, and a client outside the server's slots is
  refused; pins that the game acts only on a listed client, never mutes or
  befriends the player's own row and keeps the menu open, that the game and
  the document agree on the lists, rows, weapons and awards, and that the
  stock Friend button keeps the same mark;
- native checks of the page: each list's rows and band color, the player's
  own row brighter and marked, the selected player lit, muted and friend
  symbols, the statistics beside the lists, the widest pitch for a few
  players, sixteen players sharing what the bands leave, rows holding 14 dp
  past sixteen, an idle weapon and an unearned award dimmed, flag awards only in flag modes, the statistics in
  the player's team color, Mute refusing the player's own row with its
  reason, a row asking for its player, and Mute and Friend asking for the
  selected one; arriving on the page focuses the selected player's row;
- generator checks that Mute and Friend in either form, their reasons and the
  statistics headings fit in every language, and that sixteen players and
  three bands fit the page;
- live probes with bots on listen servers: Team DM in English, CTF in
  Hungarian and DM in German on Vulkan (the lists, the statistics with
  accuracy and awards, selecting a bot, muting and befriending it and taking
  both back, the selection back on the player when the menu reopens), the
  spectators' band during a warm-up, and a second client over loopback whose
  statistics come from the server.

Evidence: `.tmp/ui/retained-mp-players/validation-evidence.json`, SHA-256
`6ef9ecdea0cd0d453a3485185bdffa0562cd091d703969aa96244f2febccefb6`.

The Welcome card adds:

- gate cases: the game naming the card for each opening, the Welcome card
  covering and latching its variant, `mpWelcomeAction` reaching the game as
  `retained welcome <slot>` and closing the menu when the game acts, slots
  outside the four refused, the Settings hand-off pressing the join panel's
  button while the offer stands and the menu column's afterwards, the Join
  page having no stock page, Escape again once the player answers, each
  card's own gate, and fallbacks holding for the opening; pins for the game's
  variant rule, the cover allowing the join offer and dropping its blur, the
  action runner closing the menu, both cards' missing pages and stock
  buttons, and four Join slots in the session, the game and the document;
- native checks of the card: centered at Escape's height inside a 4:3 view,
  Auto join focused, the team cards side by side, a refused card dimmed with
  its lock and reason and shaking instead of asking, an open card and Auto
  join asking with their slots, deathmatch's ranked leaders with Join game
  focused, Tourney's arenas, the Players rows without choices, the Settings
  hand-off, Back, Leave Server asking first and the prompt bar inside the
  card;
- the generator fitting each card to its widest tab strip and prompt bar in
  every language, and a gate audit that measures each card's prompt bar again
  from the generated document (the native test's host cannot see real text
  widths);
- live probes with bots and `ui_autoJoin 0`: Team DM in English (the join
  offer opening Welcome, a balance refusal, Back, the menu key opening
  Welcome again, Auto join, then Escape), DM in German on Vulkan, Tourney in
  French with the Settings hand-off after Back, and CTF with its flag states
  and the hand-off during the offer.

Evidence: `.tmp/ui/retained-mp-welcome/validation-evidence.json`, SHA-256
`1620a52f64b2971dabf170c00bbe91d50b1041ccdbad74a66e67feaa503d0887`.

The Vote page adds:

- gate cases: a row's request reaching the game quietly as
  `retained voteSet <field> <value>` with the menu open, the ballots and Call
  Vote closing it when the game acts, and malformed, unknown, prefixed and
  out-of-range requests refused; the keys bound to the ballots reaching the
  card by name when it opens, and an unnamed key left out; pins that the
  adapter's value verbs, the session's field table, the game's field order,
  its publisher and the document's rows agree, that the game casts a ballot
  only while the player can, checks each field without closing the menu,
  calls only the fields that differ and closes the menu after, and drafts the
  call again at each opening; and that the Vote page no longer hands off;
- adapter checks that a value verb carries a whole number from -1 to 999 or a
  Boolean, and only the Vote page's verbs may carry one;
- native checks of the page: no vote running and no ballots, the first row
  focused, the rows the drafted game type uses stacked above Call Vote, a
  locked row dimmed with its padlock and out of reach, Call Vote waiting with
  its reason, a slider, a check box and both lists requesting their verbs
  with the new values, the running vote's caller, lines, time left and tally,
  Vote Yes focused with the keys shown, the ballots locking once the player
  has voted, and the page out of reach from another tab;
- generator checks that every row label, ballot, reason, heading and running
  vote line fits in every language, and the stock maps' long names in the
  collapsed lists;
- live probes: a host and a remote client over loopback (Team DM, English on
  OpenGL and German on Vulkan) where the host steps the time limit up five
  minutes and calls the vote, the client sees the caller, the line, the time
  left and the tally, votes yes through the card and the vote passes; and a
  listen server drafting a CTF call from Team DM (the map giving way to a CTF
  map and the capture limit replacing the frag limit, the kick list with the
  bots), in Polish on Vulkan while spectating with two fields locked by the
  server, and in Russian with voting off and a Tourney draft.

Evidence: `.tmp/ui/retained-mp-vote/validation-evidence.json`, SHA-256
`c43517ab3160ddb9ff865380dc2a83374597aa5b0bb5d4f0daf575d3b0d08aa2`.

The Settings and Voice pages add:

- gate cases: a model list's row and the crosshair reaching the game quietly
  as `retained appearance <slot> <row>` and `retained crosshair <index>`, a
  swatch as `retained rail <row>`, a negative row and a swatch outside the
  seven refused, Controls, Game Options and System opening the main menu's
  pages, the push-to-talk key reaching the card by name, and a multiplayer
  level loading each card whose pages are all built; pins for the Welcome
  card's empty missing pages, Welcome only for players without
  `ui_autoJoin`, the Settings hand-off for name and clan, the model lists,
  swatches and appearance values agreeing between the game, the session, the
  adapter and the document, and the appearance commands keeping the menu
  open;
- adapter checks that a player setting takes only its classic control's
  values (whole numbers, ranges, Booleans, the lists' own text) and that no
  other setting may be set;
- native checks of the pages: the Escape card's Settings page opening on the
  name, its rows stacking above the plates, name and clan handing off, the
  chosen swatch standing taller and a swatch asking for its color, the
  handicap and an outline setting their settings, a teammate model asking for
  its row, the teammates' column only in team modes with the note otherwise,
  and System leaving for the main menu; the Voice page's switch and volume,
  its push-to-talk key or "Not bound", and its unavailable test; Welcome's
  Settings page and its crosshair slider and preview;
- generator checks that every label fits in two lines and every list value
  in its cell, in every language;
- live probes on listen servers with bots and `ui_autoJoin 0`: in English on
  OpenGL without the opt-in, the Welcome card presenting by default (its
  crosshair, swatch and model list), then the stock in-match menu; and in
  German on Vulkan with the opt-in, Welcome's Settings, then the Escape
  card's Settings page setting an outline and the handicap, and its Voice
  page.

Evidence: `.tmp/ui/retained-mp-settings/validation-evidence.json`, SHA-256
`ef740505998f2b64c757928e2c62e08fd34dda5a735a1645c369e9d817e83f8a`.

The Match page adds:

- gate cases: a Match Control action by index reaching the game as
  `matchControl <token>` with the selection sound, the refresh without it,
  and an index past the table refused; pins that the session's tokens equal
  the card's and are each one of the game's own Match Control commands, that
  the game's mirrored operations, labels and state lines equal the page's,
  that only the Escape card mirrors Match Control, that the page asks first
  exactly where Match Control's descriptors confirm, with the stock modal's
  text, and that the triggers page sections;
- adapter checks that the triggers run a document's `onSectionPrevious` and
  `onSectionNext` once per press, and nothing where it declares none;
- native checks of the page: the refresh as it opens, waiting for a view,
  the phase, state lines and result, the actions stacking beside the state,
  a refused action's padlock and reason and its choice asking nothing, each
  action's token, Force ready and Forfeit asking first with Yes confirming
  and No cancelling, an unavailable Abort opening nothing, the action target,
  Sign in opening the stock page or saying why, Sign out, a spectator's
  camera, and the triggers paging the sections around the strip on the Match
  page only;
- live probes on listen servers with bots and the opt-in, in English on
  OpenGL and German on Vulkan: the Status section, a technical pause and the
  resume taking effect with Match Control's result, Forfeit's confirmation
  answered No, and a trigger paging to Teams.

Evidence: `.tmp/ui/retained-mp-match/validation-evidence.json`, SHA-256
`861b55c1226ee7c57e37954faefacfa38824f5f33b7f2b85fc422f38e6bc89b7`.

The Match page's Teams section adds:

- gate cases: a list's row reaching the game's menu as the stock list's own
  selection, sent with the list's token, for the team, participant and series
  map lists, and a list or a row outside the tables refused without a
  selection; the role waiting on the game's menu quietly, and a role outside
  1 to 4 refused; the Teams actions by index; pins that the session's lists
  are the game's own selection commands in the card's order, that a row index
  spans the longest list, that the game mirrors as many rows as the page has,
  that the roles are the protocol's with the projection's names for them,
  that the mirrored operations follow each section's actions with the
  projection's alternative labels, that players' names reach the page as
  plain text, and that every confirmation matches Match Control's
  descriptors and the stock modal's text;
- native checks of the section: the rows and their columns, each side's own
  row in its team's color, the row chosen lit, the latest result under the
  lists, choosing a team row or a participant naming it to the game, the
  lists beside the actions, the actions flowing in their column with a
  hidden one taking no room, a refused action's reason and its choice asking
  nothing, Join Strogg and Spectate asking the game, Remove asking first and
  No cancelling, the role list's new value reaching the session, the Duel
  sides taking the role's place with Bind side, the stock page past the
  card's rows, and Proposals still handing off;
- live probes on listen servers with bots in a warm-up and the opt-in, in
  English on OpenGL and German on Vulkan: the Teams section, choosing a bot's
  row, the role through its list, Join Strogg reaching the game as its token
  with Match Control's committed result under the lists (the stock page
  commits it alike in the same warm-up, where the player also stays on their
  team), and the action column scrolling to Expel as focus reaches it.

Evidence: `.tmp/ui/retained-mp-match-teams/validation-evidence.json`, SHA-256
`39a117a008411b46d33444719522c720b0a498a66cc15e190feaa9add95e76df`.

The Match page's Proposals and Rules sections add:

- gate cases: the Proposals and Rules actions by index and an index past the
  table refused; the ballot target on the game's menu in the stock choice's
  own words and a target outside the two refused; the rule value on the
  game's menu, with a value past 10000, a negative one, one of six digits or
  more and text refused; pins that the mirrored operations follow each
  section's actions, that the proposal, profile and rule rows match Match
  Control's own bounds, that the ballot target matches the stock choice and
  the game's parse, that the rule value's bound is the largest value a rule
  takes in the session and the adapter, and that the adapter acknowledges a
  number field's request only once its value reads back;
- adapter checks that the rule value takes 0 to 10000 and the ballot target
  the usual range;
- native checks of the sections: the running proposals, the proposals to
  make and their selection, the ballots beside them with Create proposal
  under the list, a refused ballot's reason, the ballot target's new value,
  the committed and staged rules, the profiles and rule fields and their
  selection, the value to stage, a value past the largest held as out of
  range and asking nothing, a typed value reaching the session and settling
  without a conflict once it reads back, Stage value, Select profile, Commit
  rules asking first with Yes confirming, the latest result under each
  section, and Series still handing off;
- live probes on listen servers with bots in a warm-up and the opt-in, in
  English on OpenGL and German on Vulkan: the Proposals section, choosing a
  proposal, the ballot target through its list; the Rules section, choosing
  the time limit, which fills in its value, typing 15 through the field's
  semantic edit verbs and staging it (the casual profile applies it at once:
  the clock moves to 15 minutes and the result says so), and 20000 refused
  in the field with its message.

Evidence: `.tmp/ui/retained-mp-match-proposals-rules/validation-evidence.json`, SHA-256
`830d701016bab145731404a0e018a3619ff5a24694f3737b140f13406e0d619c`.

The Match page's Series and Evidence sections add:

- gate cases: the Series actions by index and an index past the table
  refused; the series format on the game's menu in the stock choice's own
  words and a format outside the three refused; pins that the mirrored
  operations follow each section's actions, that the map pool, history and
  evidence rows match Match Control's own bounds, that the history and the
  evidence have no selection on either side, that the formats match the
  stock choice, the game's parse and the projection's names, that every
  reason a row can show is one the generator fits, that the generator
  refuses a reason column too narrow for them, and that the Match page,
  built, leaves the missing-pages list while its referee credential and
  rows past the card's open the stock page;
- native checks of the sections: the series, the map pool and its
  selection, the history only to read, Stage series, Start series asking
  first with No cancelling, Ban map asking first with Yes confirming, a
  refused action's reason, the format's new value, the evidence and the
  recent evidence only to read, and Refresh;
- live probes on a listen server and a remote client, each side with a
  human, in English on OpenGL and German on Vulkan: the competitive profile
  chosen from Rules, then the format through its list, staging a best of
  one and starting it through the stock confirmation, choosing a map and
  banning it through the confirmation (the map marked banned, the history
  filled, the turn passing to the other side), the action column scrolling
  to its last row, and the Evidence section with Refresh; and the stock
  page's proposal list, now in its two columns.

Evidence: `.tmp/ui/retained-mp-match-series-evidence/validation-evidence.json`, SHA-256
`6ee747ef06a2c9d6d90984021912bb53e22b4c354be2c6380d5bb9aedbeddf23`.

The card's acceptance continues under `FLOW-046`.

## Known limitations

- The Match page's team rows past 64 and series history past 24 steps hand
  off to the stock page, and the stock menu then keeps the menu until it
  closes. The Escape card is near the runtime's 16 MiB document limit.
- The Admin page's console completes nothing with Tab, as the stock console
  did.
- Choosing an unavailable action does not announce its reason to the
  accessibility text backing yet, and a controller gives no rumble.
- The Team page has no Tourney queue or arena controls; Tourney offers what
  deathmatch does.
- [Player identity guards](player-identity.md) now protect local friend/mute
  marks, selections and retained player requests across slot reuse. Delayed
  remote voice-mute packets still name only a slot; their server-side race and
  identity protection for kick/admin targets remain under UI-03.
- Left and right move focus between the lists and the statistics; they do not
  switch tabs as section 14.18 asks (decision D2 is open).
- The Welcome card's Join page has no Watch for Tourney's arenas and no
  place in the queue (neither has a model yet).
- The Settings pages have no model preview (retained documents have no 3D
  view).
- Voice capture/playback, the meter and local test have since been implemented;
  see [current voice evidence](../audio-voice-evidence.md#multiplayer-voice).
  Physical microphones, permissions and device qualification remain separate.
- The crosshair slider runs to twenty, the stock list; a mod's longer list
  is reached only by the classic picker.
- On a remote client a running vote's time left counts from when the vote
  reached it, so it can read a little long; no message carries the server's.
- The kick list names its rows, and the game reads the row at the moment of
  the choice: a player leaving in that instant can shift the rows. The row
  then shows whom the call would kick before Call Vote sends it.
- The Vote and Admin pages' map lists hold 128 maps a game type
  ([map catalogs](#map-catalogs-24-october-2026)); a server with more lists
  the first 128.
- Check boxes take Left and Right as focus moves and change only on accept;
  sliders and closed lists step with them, as section 13.5 asks of cycling
  values. A list's value shows one frame after the game accepts it.
- A managed match's refusals on the Vote page have no live probe yet.
- The Vote page's Vote Yes and Vote No keys and the Voice page's push-to-talk
  key show the keyboard bindings the session publishes, not a controller's
  (`INP-011`).
- In views narrower than the card (5:4, portrait) the card now shrinks to
  fit with its side margins ([narrow views](retained-screens.md#narrow-views-24-october-2026));
  the portrait layouts of `LAY-021` beyond that remain.
- The multiplayer smokes join with `ui_autoJoin 1`, so they never see the
  Welcome card, and the ones that drive and capture the stock in-match menu
  pin `ui_retained 0`, since the Escape card now covers it by default.
