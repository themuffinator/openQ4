# Multiplayer refinements — pending release notes

> **Detailed change record.** Everything recorded here up to 11 November 2026 shipped in [0.14.1](v0.14.1.md), whose notes are the player-facing summary. Later additions are unreleased development notes. See [release status](../../user/release-status.md).

## Highlights

- **Weapons remain visible after seeking while paused.** Following a living
  player in a multi-view demo now shows their weapon immediately after a seek,
  instead of leaving it below the camera until playback resumes.
- **Your inputs reach the server on time again.** Since 0.13.0 a client on a
  server could settle a frame behind it, so the server received each of your
  inputs late and repeated the previous one. Clients now stay ahead of the
  server as intended, which removes that extra input delay and the small
  corrections it caused. Update your client; servers need nothing for this fix.
- **Joining by hostname tries every address.** When a server's hostname has
  several IPv4 or IPv6 addresses and the first one never answers, openQ4 now
  tries the others in turn, alternating between IPv4 and IPv6, instead of
  retrying an unreachable address until you cancel. The console names each
  address as it is tried.
- **Prediction and lag compensation no longer correct the same delay twice.**
  Your client tells the server how far ahead it draws other players, and a
  lag-compensated server rewinds only the delay your screen really showed.
  Both settings remain opt-in experiments while they are measured on real
  connections. Server operators can see each player's figures with
  `lagCompStatus`.
- **Multi-view demos with several players play again.** Recordings with two or
  more players made since late September were refused at playback with an
  "invalid start-state client" message. They now play, as do older recordings.
- **Interrupted demo recordings can be saved.** `mvdRecover` turns the complete
  part of a recording that stopped unexpectedly into a new, playable demo,
  without changing the original file.
- **Bound demo recording storage without deleting match evidence.** Server
  operators can set `mvd_libraryQuotaMB` and inspect `mvdLibrary`. A full library
  refuses new recordings; a recording that reaches its remaining budget keeps
  a recoverable partial file. The limit is off by default and never prunes
  existing recordings. Archive files explicitly to make room.
- **Follow any player in a demo.** Step back to the previous player, or follow
  one by name or slot, with `demoFollow prev`, `demoFollow <name>` or
  `demoFollow <slot>`. The playback overlay names the player you follow.
- **No more frozen gibs after seeking in a demo.** Jumping forward or back in a
  multi-view demo could leave gibs and debris hanging where they spawned, often
  as a large distorted chunk right in front of the camera after a death. Seeks
  now leave them out.
- **Competitive votes survive the match moving on.** A passed vote no longer
  fails because someone readied up, joined or left while it was open. Map veto
  turns follow each team's series side, and a spectator's camera is released
  when the followed player's slot is taken by someone new.

## Upgrade notes

- Replace the complete package: the engine and both game modules change
  together. Servers and clients must run the same build, as before.
- Lag compensation and enhanced prediction stay off by default. The
  [networking guide](../../user/multiplayer-networking.md) now describes how
  they fit together and replaces the earlier "recommended presets" that turned
  both on.

## Change log

- Bots can recover from an unplanned walking fall as well as an enemy knock.
  A sloping platform rim could lose support just as the bot braked, leaving it
  steering toward its walking goal through the void. Deliberate jumps, drops
  and pad flights keep their existing movement controls.
- Custom-map AAS compilation now marks water areas consistently, allowing its
  existing water-jump builder to generate exit links. Rebuild affected navigation
  files with the updated compiler; this does not add swimming monster behavior.
- `lagCompStatus` no longer reports a bias-only rewind for bots or the listen
  host when no remote connection delay was measured.
- With a positive `net_mpLagCompBiasMS`, experimental lag compensation no
  longer rewinds the world for bots, a listen server's own player or a
  connection the server has not measured yet. The bias only adjusts measured
  remote connections. Lag compensation stays off by default.
- The client's prediction controller speeds up again when it is behind its
  target. Since 0.13.0 it slowed down instead, so a client ran one frame behind
  every server update and its commands arrived late. `net_verbose 2` now also
  logs the controller's state for each server update.
- Testing under simulated network conditions now covers jitter, out-of-order
  delivery and duplicated packets (`net_forceJitter`, `net_forceDuplicate`,
  repeatable with `net_forceSeed`) as well as latency and loss, on Linux and
  macOS too, and `netImpairment` reports what was simulated. With
  `net_forceLatency` set, a client can connect again: larger packets were
  silently dropped before. A simulated bottleneck (`net_forceRate` bytes a
  second, with `net_forceQueue` milliseconds of buffer) and losses in bursts
  (`net_forceDropBurst`) test congested and unsteady connections, and
  `netImpairment` also reports the data rate, congestion drops and loss runs.
- `listClientEntities` names each client-side entity's real class and position;
  it used to print `rvClientEntity` for every entry.
- Hostname connections try up to eight resolved addresses. The first is the
  address openQ4 always chose (the IPv4 one when there is one); after three
  unanswered connection attempts a second apart it moves to the next, and it
  starts over after the last. Numeric addresses and server-list entries are
  unchanged.
- `net_mpRemoteExtrapolation` (read-only) reports what your client advertises:
  `0` in legacy prediction, otherwise `net_mpPredictMaxFrames`. The server never
  trusts the advertisement to lengthen a rewind; every figure is capped by the
  round trip it measured.
- `lagCompStatus` lists each player's prediction lead, measured round trip,
  advertised extrapolation and the rewind a shot would get. `net_mpLagCompDebug`
  logs the same figures per compensated shot, and `net_mpPredictDebug` logs how
  old each remote player was on your screen.
- `mvdStatus` also reports a recording in progress and the result of the last
  recording, including where a partial file was left.
- `mvdFollow`, `mvdFollowPrev` and the extended `demoFollow` accept
  `next`, `prev`, `free`, a client slot or a player name.
- Swimming underwater on a server with latency no longer runs your local air
  readout and its cues ahead of the server.
- `s_soundWorlds` reports whether the underwater sound muffle is active.
