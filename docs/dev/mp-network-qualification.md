# Multiplayer network qualification

## Weapon hit geometry and fragment ordering, 3 November 2026

The MP-07 weapon review found that the gauntlet and lightning restored player
physics after tracing, but kept their effect endpoints at the historical hit.
The gauntlet now translates its contact before effects. Lightning translates
the primary and chain endpoints before restoring players, then rebuilds chain
directions from those present positions. Selection still occurs in the rewound
world, and all players are restored before these weapons apply damage/effects.
Hitscan already translated its impact, but used that translated point to
continue penetrating shots. It now retains the historical continuation point,
so moving a rail impact onto its target cannot bend the remaining ray.

`mp_lag_compensation_state.py` now executes the production gauntlet/lightning
resolution blocks and the hitscan impact/penetration blocks, as well as the
existing production history and estimator. It checks beam/contact geometry,
chain directions, repeated target references, misses, full restoration and
the separate rail continuation. Nine single-reversal controls fail, including
the old weapon blocks and translated rail continuation. The wiring contract
checks that target selection precedes translation and effects follow restoration.
These are executable fixture results; they do not replace remote weapon-play
or same-fire-frame visual qualification. Both enhanced defaults remain off.

MP-08's production fragment assembler had further defects. Once a newer
message began assembly, a delayed fragment from an older uncompleted sequence
could reset the buffer and discard the newer prefix. Older fragments and delayed
unfragmented messages now leave that prefix intact. A fragment larger than the address family's fixed size
was also accepted as a final fragment; it is now refused before
it can change assembly state. The wire format and family limits are unchanged.

The first impaired IPv4 run passed all three pure joins, receiving 497
fragments. The IPv6 run reached gameplay once, then lost its second join to a
reliable-message overflow: fragments reordered within the same message were
discarded rather than assembled. The failing report and original runtime hashes
remain in `.tmp/mp-roadmap/lan-ipv6/`. Assembly now uses a bounded bitmap of aligned
fragment offsets and a separately recorded final length. It accepts the final
fragment first, waits for every missing byte, ignores duplicates without counting
or overwriting them, and refuses misaligned, oversized or conflicting fragments.
There is still only one bounded assembly buffer; a newer sequence supersedes an
incomplete older sequence. This adds no retransmission or wire-format change.

`network_fragment_state.py` executes production `SendNextFragment` and `Process`
with deterministic datagrams. Both families cover one-byte, boundary, exact-
multiple and 16 KiB payloads, empty terminators, interleaved sequences, duplicates,
reverse-order delivery, missing fragments, truncated headers and oversized
fragments. All eight reversal controls fail. The port, byte-message interface and decompression
boundary are substitutes: this qualifies assembly, not compression or a real
path MTU. No WAN or other-architecture qualification is claimed.

The staged Windows x64 build of `47c78ed5e` passes `mp_lan_reconnect_smoke.py
--impaired` with both families. Each peer has 30 ms fixed delay, 0-60 ms jitter,
3% loss and 20% duplication, seeded with 40708. Every join reaches active
gameplay on the pure dedicated server in slot 0, and both processes exit 0.
IPv4 discovery passes; IPv6 loopback correctly has no multicast discovery.

| Final run | Active joins | Received client fragments | Elapsed seconds |
| --- | ---: | ---: | ---: |
| `lan-ipv4-fixed/report.json` (before the delayed unfragmented guard) | 3/3 | 242 | 67.97 |
| `lan-ipv6-fixed/report.json` (before the delayed unfragmented guard) | 3/3 | 320 | 88.00 |
| `lan-ipv4-final/report.json` | 3/3 | 221 | 54.83 |
| `lan-ipv6-final/report.json` | 3/3 | 275 | 71.39 |

Reports retain installed-asset and runtime SHA-256 values and verify that the
runtime did not change during the test. The final client hash starts
`74bac939fd7bc997`, dedicated `c93b1ee10738e787`, and MP module
`e6a7b200e18c7e0c`; full hashes are in the final reports. The build uses MSVC,
`debugoptimized`, `b_ndebug=true`, and `build_game_sp=false` for this MP-only
test stage. These are loopback gameplay checks, not cross-machine or WAN tests,
and elapsed time on this shared host is not a performance qualification.

After integrating main's save/BSE changes, the clean tree `62ee95276` builds
and stages successfully. Its impaired IPv6 regression also passes all three
joins, with 355 received client fragments, both exits 0 and unchanged hashes
(`lan-integrated/report.json`, 131.39 seconds). The integrated client hash starts
`47322dfc491a57a3`, dedicated `63507833e186be86`, and MP module
`ce7ca57fa935d2e9`. This is additional integration evidence, not a timing claim.

The 59 broader match/network contracts and the three extended/new fixtures
pass on Windows. The three fixtures also pass under Linux with Clang and
ASan/UBSan, including all 29 reversal controls. This is native fixture evidence,
not a Linux engine build or peer test. Native logs, the failing IPv6
control, source diff and staged runtime reports are retained in `.tmp/mp-roadmap/`.

Evidence for MP-07 (coupled prediction and rewind) and MP-08 (network
interoperability) gathered on one machine, and the simulated network conditions
that make that possible. Player-facing guidance is in the
[networking guide](../user/multiplayer-networking.md).

## Simulated network conditions

`src/sys/NetworkImpairment.h` is a delay line for UDP datagrams that both socket
layers (`win_net.cpp`, `posix_net.cpp`) use. Each bound `idPort` owns two lines:
received datagrams wait in one before the engine reads them, sent datagrams wait
in the other before they reach the socket.

| CVar | Meaning |
| --- | --- |
| `net_forceLatency` | Fixed milliseconds added to every datagram, each way (0-2000). |
| `net_forceJitter` | Further delay drawn uniformly from 0 to this many milliseconds per copy (0-1000). |
| `net_forceDrop` | Percentage of datagrams lost. |
| `net_forceDuplicate` | Percentage of datagrams delivered twice; each copy has its own jitter. |
| `net_forceSeed` | Seed for the random choices when a port opens; `0` derives one from the clock and port. |
| `net_forceRate` | A bottleneck of this many bytes a second, each way, shared by everything on the port (1,000 to 100,000,000; `0` for none). |
| `net_forceQueue` | Milliseconds of traffic the bottleneck holds before it drops (1-5,000, default 250). |
| `net_forceDropBurst` | Mean number of datagrams lost in a row; `0` or `1` loses them independently (up to 64). |

Datagrams leave in the order they fall due, with ties in arrival order, so jitter
wider than their spacing reorders them as a real link does. Comparisons use the
millisecond clock wrap-safely. Payloads are stored at their real size (up to the
65,535-byte UDP limit); a due datagram larger than the reader's buffer is
discarded and counted, never truncated. A line holds at most 8,192 datagrams or
32 MiB and counts refusals beyond that. Lines keep draining after the settings
return to zero, so turning impairment off never reorders what is still queued.
Due sends are flushed whenever the port is polled, not only when the next send
arrives.

A rate turns a line into a bottleneck. Each datagram, with 28 bytes of IPv4 and
UDP headers, takes its size over the rate to send, and waits behind whatever
the link is still sending. One that would finish later than the queue holds is
dropped, as a full router buffer drops it. An idle link always takes a
datagram, so one larger than the queue still passes. A datagram lost further
along the path still takes its turn on the link. The latency and jitter start
when the datagram leaves the bottleneck. Burst loss is a two-state
(Gilbert-Elliott) chain: it stays in its losing state for `net_forceDropBurst`
datagrams on average and spends `net_forceDrop` percent of the time there. A
loss rate above burst / (burst + 1) cannot be reached and settles at that.
With no rate and a burst length of 0 or 1, the line behaves exactly as before.

`netImpairment` prints the settings and, per open port and direction, the
datagrams offered, dropped, duplicated, released, reordered, refused and
discarded, and the deepest queue so far (`NET_IMPAIR` lines). The line ends
with the payload bytes offered, the loss runs, the datagrams the bottleneck
dropped and its longest wait; these come last so harnesses that read the
leading fields keep working.

The CVars are unarchived and not `CVAR_NOCHEAT`, so like the original two they
count as cheats: hosting or joining a server without `net_allowCheats` or
`sv_cheats` resets them to 0.

This replaced the inherited Windows-only lag queue (POSIX had none). That queue
stored at most 1,400 bytes per datagram and silently dropped anything larger,
so with `net_forceLatency` set before connecting the handshake looped ("packet
exceeds latency queue capacity", "Bad challenge"). It released queued sends only
when another send came along, and had no jitter, reordering or duplication.

Tests: `tools/tests/native/NetworkImpairmentCases.h` drives the header
directly: fixed delay and order, jitter bounds and reordering, loss and
duplication rates, seed repeatability, oversize discards, a 9,000-byte
datagram, clock wrap and capacity. It also covers the bottleneck's pacing to
the millisecond, its tail drop, an idle link taking a large datagram, draining,
pacing across the clock wrap, the byte count, burst loss (rate and mean run
length) and the new settings changing nothing while off. The native core
safety test runs the cases, and `tools/tests/network_impairment_state.py` also
runs them against 14 single-reversal copies of the header, each of which must
fail. `tools/tests/network_impairment_contract.py` checks that both platforms
define the same CVars, open and release the lines with the port, queue
receives and sends, flush due sends while polling and report, with the
report's leading fields unchanged; it carries eight single-reversal controls
of its own.

## Prediction and lag compensation under impairment (24 October 2026)

`.tmp/mp-round2/impair_probe.py` runs a loopback listen server with three bots
and one remote client on the candidate build of this round. The client steps
through six phases, settling 4 s before each 6 s measurement with `net_verbose 2`
(controller state per snapshot) and `net_mpPredictDebug 1` (remote pose ages);
the server samples `lagCompStatus` every 3 s. Impairment is set on the client's
port, so both directions of its link are affected. `net_forceSeed 4242` makes the
runs repeatable. Reports: `.tmp/mp-round2/runtime/{legacy,enhanced,handshake}/report.json`.

| Phase | Settings (client port) | Resyncs | Client lead median | Server lead / RTT | Duplicated-command reports |
| --- | --- | ---: | ---: | --- | ---: |
| clean | none | 0 | 48 ms | 40 / 16 ms | 0 |
| latency50 | 50 ms | 0 | 144 ms | 140-144 / 117 ms | 0 |
| jitter50x40 | 50 ms + 0-40 ms | 0 | 192 ms | 181-188 / 117-150 ms | 2 |
| loss10 | 50 ms, 10% loss | 0 | 144 ms | 140-151 / 117-150 ms | 4 |
| duplicate20 | 50 ms, 20% duplication | 0 | 144 ms | 146-153 / 117-136 ms | 0 |
| combined | 60 ms + 0-40 ms, 5% loss, 5% duplication | 0 | 208 ms | 199-214 / 150-201 ms | 6 |

Legacy prediction (`net_mpPredictMode 0`), from the `legacy` run; the server
columns are the `lagCompStatus` samples taken during each phase. The
`enhanced` run (`net_mpPredictMode 1`) matches it phase for phase: no resyncs,
the same leads, and at most four duplicated-command reports in a phase.

- The delay lines did what was asked: in `loss10` the client's receive line
  dropped 35 of 361 datagrams (9.7%); in `duplicate20` it duplicated 66 of 362
  (18%); jitter reordered 13 received and 133 sent datagrams in its phase.
- The prediction controller held through every phase. The lead follows the
  round trip plus about one frame, the target settles within the 4 s, and
  commands arrive early (median 5-20 ms ahead). Duplicated commands appear
  only in a handful of snapshots under jitter and loss, where a late datagram
  arrives after the server needed it.
- The channel discards the reordered and duplicated datagrams
  (`net_channelShowDrop` "out of order packet"); usercmd redundancy covers the
  lost ones. No malformed, truncated or overflowing messages were logged.
- Lag compensation follows the real lead. Legacy rewinds rise from 56 ms clean
  to 156-169 ms at 50 ms and reach the 200 ms cap with jitter or the combined
  conditions. Enhanced prediction's advertised 16-tic extrapolation covers the
  whole lead, so its pose age is 0 and its rewinds stay at 0-16 ms (the frame
  bias), and its remote pose error is the deliberate forward extrapolation.
- A client that connected with 80 ms + 0-40 ms, 10% loss and 5% duplication
  already set (`handshake` run) joined on its first challenge, with no "Bad
  challenge" and no capacity refusals, then behaved as above.

## High player count (24 October 2026)

`impair_probe.py --dedicated --bots 20 --max-players 24 --quiet-client` hosts 20
bots on `openQ4-ded_x64.exe` with one remote client. The client records frame
times (`com_speeds`) instead of verbose logs, and the server runs `net_verbose 1`,
which reports a snapshot deferred behind a reliable backlog. Reports:
`.tmp/mp-round2/runtime/crowd-quiet/` and, with three bots for comparison,
`few-quiet/`.

| Phase | Client frame median | Prediction target the server sees | Round trip |
| --- | ---: | ---: | ---: |
| clean | 16 ms | 34-39 ms | 2-5 ms |
| latency50 | 17 ms | 142-146 ms | 104-122 ms |
| loss10 | 17 ms | 140-143 ms | 103-136 ms |
| duplicate20 | 16 ms | 142-148 ms | 103-121 ms |

No snapshot was deferred and no message overflowed; the three-bot run matches.

Earlier 20-bot runs had bursts of resyncs and commands up to 1.6 s late. Each
time the client process was starved of CPU, not the network path. The first
hosted the 20 bots on a listen server that also rendered, on the same machine
as the client. The second logged `net_verbose 2` and `net_mpPredictDebug` with
a flush per line, about 900 lines a second with 20 remote players. A third
overlapped another session's full-core build, which reproduced the symptom on
demand: client frames rose to a 38-57 ms median and the prediction target
climbed steadily toward its 1 s cap (`crowd-quiet-overlapped-build/`). A client
that cannot hold the 60 Hz tic rate falls behind the server in the same way;
this was measured, not changed. The one reliable-message overflow logged came
at server shutdown, after the client had already quit.

## Congestion, rate control and burst loss (24 October 2026)

`.tmp/mp-round3/congest_probe.py` (derived from `impair_probe.py`) hosts three
bots on `openQ4-ded_x64.exe` with one remote client in legacy prediction. It
steps through 14-second phases, measures the last 8 s of each with
`net_verbose 2`, and diffs the client's cumulative `NET_IMPAIR` counters per
phase. The bottleneck is on the client's port, so it limits the download
(server to client) and the upload alike. `net_forceSeed 4242`. Reports:
`.tmp/mp-round3/runtime/{congest-quiet,congest-matched}/` (`phases.json`).

| Phase | Settings (client port) | Download offered | Bottleneck drops | Longest wait | Snapshots | Lead median | Duplicated-command reports |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| clean | none | - | - | - | 183 | 48 ms | 0 |
| rate64k | 30 ms, 64,000 B/s | 5,706 B/s | 0 | 2 ms | 182 | 112 ms | 1 |
| rate24k | 30 ms, 24,000 B/s | 6,071 B/s | 0 | 40 ms | 180 | 128 ms | 0 |
| rate12k | 30 ms, 12,000 B/s | 6,352 B/s | 0 | 159 ms | 183 | 144 ms | 43 |
| rate12kdeep | as rate12k, 1,000 ms queue | 6,921 B/s | 0 | 175 ms | 181 | 144 ms | 48 |
| rate6k | 30 ms, 6,000 B/s | 10,622 B/s | 56 | 199 ms | 132 | 224 ms | 105 |
| burst10x4 | 50 ms, 10% loss in runs of 4 | 5,956 B/s | - | - | 144 | 176 ms | 15 |
| burst10x1 | 50 ms, 10% independent loss | 6,199 B/s | - | - | 158 | 144 ms | 7 |

No phase resynced. The upload stayed at about 2.7 KB/s throughout.

- A three-bot match needs about 6 KB/s downstream. A link with spare capacity
  adds only the time to send a snapshot burst: the lead rises by the latency
  plus up to 160 ms of queueing, and a deeper queue changes nothing while the
  link keeps up.
- A link below that demand congests and then feeds itself. At 6 KB/s the
  bottleneck dropped 56 datagrams in 8 s, and the server offered 10.6 KB/s, 1.7
  times the demand, because lost acknowledgements make it resend reliable data
  and delta snapshots against older bases. Snapshots fell by 28%, the lead
  reached 224 ms with the prediction target climbing (194 to 295 ms), and the
  server duplicated late commands in 105 snapshots.
- Rate control prevents it. The `congest-matched` run connects with
  `net_clientMaxRate 5000`, so the server keeps under the 6 KB/s link. It then
  sent 4.2 KB/s with no bottleneck drops and a 15 ms longest wait. The lead was
  160 ms with a steady target (167 to 169 ms), and 16 snapshots reported
  duplicated commands. The price is snapshot rate: 133 in the window, against
  151 with the same limit on a 12 KB/s link and 182 unlimited.
- Losses in runs of about 5.4 datagrams (38 lost in 7 runs) cost more than the
  same rate of single losses (19 lost, runs of 1.06): 14 fewer snapshots and a
  lead 32 ms higher. Neither resynced.

An earlier run of the same phases while another session built with every core
and two other clients ran (`congest-ded`) resynced 15 times on the clean
connection alone, and the target climbed toward its 1 s cap. As in the
high-player-count runs, that is CPU starvation, not the network. The rates and
drops above come from the quiet run; one other session's client was still
running at 24% total CPU.

## Discovery, reconnects, content and retail refusal (3 November 2026)

MP-08 checks one Windows x64 machine can qualify. Every peer binds IPv4
loopback, IPv6 loopback or this host's own scoped link-local IPv6 address
(which Windows delivers internally), with hidden windows and no input. The
final runtime is source `1e1f3e88a` plus this change; the control runtime is
`1e1f3e88a` as committed. Both are debugoptimized builds with retail Steam
assets. Reports are under `.tmp/mp08/final/` (control reports under
`.tmp/mp08/final/*-control/` and `.tmp/mp08/lan-ipv6scoped-r1/`).

| Runtime | Engine | Dedicated | `game-mp` | `pak0.pk4` |
| --- | --- | --- | --- | --- |
| Final | `87c8a999…acbf36` | `f9c231b1…6c22c0` | `005dfd4d…857d91` | `b5bf454b…d8a044` |
| Control (`1e1f3e88a`) | `ccc6a010…e3b1d3` | `ef99e2e4…3bbd92` | `18b1191d…51da3f` | `1f4f336b…5275dc` |

Full runtime hashes are in each report's `runtime_sha256`. The final runtime
was built from `1e1f3e88a` plus the engine and content diff
`.tmp/mp08/final/runtime-source.diff` (SHA-256 `c35e102ec29d`); `pak1.pk4` is
`1a7c763f…6570a9` in both. The scoped IPv6 runs used a byte-identical copy at
the firewall-registered `.tmp/mp-refinement/runtime/`.

### LAN discovery and successive reconnects

`tools/tests/mp_lan_reconnect_smoke.py` starts a pure dedicated server
(`si_pure 1`, q4dm1) on the first free port of 28004-28011 and one client, both
bound to the same address. The client runs `LANScan` and `listServers`, joins
through the scan entry, disconnects, connects again by endpoint, then uses
`reconnect`. Each join must reach active play (`openq4_reportMPState`), and the
server must admit the client each time.

| Family | Server | Scan | Joins (scan, endpoint, `reconnect`) | Result | Report SHA-256 |
| --- | --- | --- | --- | --- | --- |
| IPv4 loopback | `127.0.0.1:28004` | listed as `localhost:28004` (scan 1) | 3 in slot 0 (29.2, 5.9, 5.8 s) | pass | `b751e8c102f0` |
| IPv6 loopback | `[::1]:28004` | empty, as expected (3 scans) | 3 in slot 0 (46.5, 5.5, 5.7 s) | pass | `57226bf4e011` |
| IPv6 scoped link-local | `[fe80::93c2:3db5:5734:d3a3%22]:28004` | listed as `[fe80::93c2:3db5:5734:d3a3%22]:28004` (scan 1) | 3 in slot 0 (23.0, 5.3, 5.4 s) | pass | `0394692ae38b` |
| IPv6 scoped, control | `[fe80::93c2:3db5:5734:d3a3%22]:28004` | not listed (3 scans) | none | fail | `866c0610548d` |

- **Defect: Windows servers never heard IPv6 scans.** The scan reaches IPv6
  servers by sending to `ff02::1` on every link. Windows delivers a multicast
  group only to sockets that joined it, even the all-nodes group, while the
  socket code skipped that join because Linux delivers `ff02::1` to every
  socket. A Python probe on the same link-local address received the scan only
  after `IPV6_JOIN_GROUP`, whether bound to the address or to `::`. The control
  run (`lan-ipv6scoped-r1`: engine `b80688c7…f5ea06`, dedicated
  `e168d49b…562c94`, a build identical except for the join) scanned three
  times without the server logging one info response. `win_net.cpp` now
  joins the discovery group on every link, `ff02::1` included, and the first
  final scan listed the server. The POSIX layer is unchanged; Linux and macOS
  IPv6 discovery were not run here.
- IPv6 loopback has no link to scan: Windows rejects `::1` with a scope and
  multicast through the loopback interface (`WSAENETUNREACH`), so that run
  records an empty scan and joins by endpoint. IPv4 broadcast does reach a
  socket bound to `127.0.0.1`.
- The engine names loopback endpoints `localhost:<port>` in scan results.
- `g_autoExecAfterMapLoad` is one-shot (it clears itself), so the harness arms
  it before each join.

### Pure content and local game modules

`tools/tests/mp_mod_admission_smoke.py` (started by an earlier session and
completed here) runs eight scenarios, each a fresh pure dedicated server and
one client on IPv4 loopback. The server runs a test mod whose package is named
`pak*`, so the pure list always carries it.

| Scenario | Final | Control |
| --- | --- | --- |
| a matching client joins; its extra local package stays off the pure list | pass alone with a 300 s join timeout (`2afae01a99d5`); the full run timed out reloading under load | pass |
| a client started without the mod switches to it and joins | pass | pass |
| a client missing the package is refused | pass | pass |
| a client with an altered package of the same name is refused | pass | pass |
| a garbage `game-mp` in the save path and in the package is ignored | pass | pass |
| an incomplete `mod.json` is ignored; the server runs openQ4 and a plain client joins | pass | fail (openq4_game_dir, openq4_strings_resolve, join_as_expected, clean_exits) |
| a mod module in the trusted root is refused for pure startup | pass | pass |
| the same module loads once `net_serverAllowServerMod 1` is set | pass | pass |
| Report SHA-256 | `de046848d8a7` | `3e572a007f66` |

- **Defect: an incomplete `mod.json` ran retail-only content.** The engine
  ignored the mod but set `fs_game ""`, which also drops the openQ4 runtime
  underlay. Logs went to `q4base/logs`, and openQ4 strings were unknown (about
  8,000 warnings on a dedicated server). It now falls back to `baseoq4`;
  `filesystem_mod_manifest.py` pins it, and the control fails exactly this
  scenario.
- In the full final run the matching-content client was still reloading after
  its pure restart when the 120 s join limit ran out, with other sessions'
  builds and clients loading the machine; its challenge had been answered at
  once. Alone it joined in under a minute. An earlier session saw a client's
  first challenges go unanswered under similar load; that did not recur here.

The same-platform pure token (`0x68fb90b1`) is a compatibility check between
openQ4 builds, not a cryptographic measurement of the loaded module or an
anti-cheat system ([server security](../user/server-security.md#pure-multiplayer-and-local-game-modules)).
Standard builds still download no assets or code.

The slow reload after the pure restart noted above was the engine restart
that every first pure connect paid because of the generated image cache. Since
9 November that cache's images are reloaded instead
([decision](#generated-image-cache-on-pure-servers-9-november-2026)).

### Retail 2.85 refusal

`tools/tests/mp_retail_protocol_smoke.py` runs the installed retail
`Quake4Ded.exe` under `fs_game q4mp`, bound to `127.0.0.1` with its writable
files in the run directory and stopped with rcon `quit`. An openQ4 client
LAN-scans and then connects to it directly. Then an openQ4 dedicated server
receives the challenge a 1.4.2 `Quake4.exe` sends: the client id and one byte
(whether it is a dedicated server), read from the executable. The retail
client itself is not run, because it opens a visible game window.

| Check | Final | Control |
| --- | --- | --- |
| Retail server in the LAN scan | ignored (2.85) | ignored (2.85) |
| openQ4 client's refusal reason | protocol 2.85 | missing `q4mp` mod |
| Network protocol mismatch dialog | yes | no |
| Followed into `q4mp` or sent `connect` | no | no |
| 1.4.2 client challenge to an openQ4 server | print 1, `server uses protocol 2.41` | no answer |
| openQ4 challenge to the same server | answered | answered |
| Result | pass | fail |
| Report SHA-256 | `fdafa262c713` | `8e8dc26635c6` |

- **Defect: openQ4 clients misreported a retail server.** Retail answers the
  challenge exactly as openQ4 does, naming its `q4mp` game directory, and
  openQ4 followed that before learning the protocol. The client now sends
  `getInfo` ahead of each challenge (the reply echoes the connection id) and
  waits for the protocol before following a server into another game
  directory. After three unanswered requests it proceeds as before. A
  different protocol disconnects with **Network protocol mismatch**
  (`#str_231570`, in all twelve openQ4 language tables).
- **Defect: openQ4 servers ignored retail clients.** Any challenge that was not
  exactly four bytes was dropped. The five-byte 1.4.x challenge is now
  answered, within the challenge reply budget, with the protocol print a
  retail client shows.
- `network_security.py` pins both paths. Its mutation controls reject
  dropping the retail answer, the missing protocol print, a missing protocol
  check, following a game directory first, and a challenge without the query.

## Generated image cache on pure servers, 9 November 2026

**Decision: openQ4's generated caches are never trusted on a pure server, but
reading the image cache before connecting no longer restarts the engine.** The
client reloads only the images that came from that cache, once the pure lock is
in place. Any other file read from a directory before the lock still forces the
full restart, as in the original engine.

### Why the cache cannot be trusted

A `generated/images/**/*.bimage` entry records the source it was derived from
in its header. For a pak member that record is the member's CRC-32 and length,
folded into a content stamp (`FS_PakMemberContentStamp`). It keeps a changed
pak from reusing stale pixels, but it does not prove that the pixels came from
that source:

- Both values sit in the pak's central directory, readable by anyone. CRC-32 is
  not a cryptographic hash.
- Nothing ties the payload to the header. An entry that keeps its real header
  and has edited pixels passes every load check: transparent walls, bright
  player skins, or no smoke. The payload run below shows the menu accepting
  28 such entries as cache hits.
- A digest or key stored next to the cache adds nothing, because the person
  editing the file can recompute it. The only check that works is deriving the
  image again, which is what a cache miss already does.

With `image_usePrecompressedTextures 0` (a player setting), every world texture
goes through this cache. Trusting it would undo pure mode for the content
cheats it exists to stop. So the cache stays unreadable while pure: the
directory gate in `OpenFileReadSearch` refuses it, and `FileAllowedFromDir`
never admits a `.bimage`. Images needed after the lock are rebuilt from the
pure paks, about 110 writes on `mp/q4dm1`. That cost is inherent.

### Why the restart went

Pure mode restarts because content read from a directory before the lock could
still be in use afterwards. On a stock install, the only such reads before a
connect were image cache entries, in every session. That included the first
session on a fresh save path, because images written at the title screen are
read back later in the same session. So practically every client paid a full
`ReloadEngine` on its first pure connect. The reload alone took 29–35 s here,
and the cold reload made the map load that followed slower too.

Replacing those images is enough, and it does not weaken the lock (render
API 33):

- A cache read from a directory sets `loadedImageCacheFromDir`, not
  `loadedFileFromDir`. When `SetPureServerChecksums` takes the lock over such
  reads, it leaves an eviction pending
  (`idFileSystem::TakePureImageCacheEviction`).
- The client then calls `idRenderSystem::ReloadGeneratedCacheImages`. Each
  image records whether its resident pixels came from the cache (set in
  `idImage::ActuallyLoadImage`), and every such image is force-reloaded.
  Because the lock already refuses the cache, each one is derived from the pure
  paks, and its cache entry is rewritten.
- If any image still holds cache pixels afterwards, the client restarts as
  before.
- Unchanged: loose source files, other generated trees (sound, animation),
  referenced paks outside the server's list, and missing paks keep their
  existing outcome. `pure_image_cache_eviction_contract.py` compiles the path
  classifier and pins each link, with mutation controls.

### Evidence

A `builddir` dedicated server (`si_pure 1`, `net_LANServer 1`, `mp/q4dm1`) and
a `builddir` client, each with its own save path under
`.tmp/pure-cache-restart/`. The client ran in a hidden 640x360 window with
`ui_autoJoin 1`, on OpenGL unless noted, and did two sessions on one save path:
the first with an empty cache, the second with a warm one. The base path held hard links to the
retail PK4s only. Times run from `connect` until the client is an active
player.

| Build and case | Session | Before the lock | Engine reloads | Connect to gameplay |
| --- | --- | --- | --- | --- |
| Before | cold | restart triggered by `bigchars` cache entry | 1 (34.8 s) | 57.7 s |
| Before | warm | restart triggered by `makeintensity_..._squarelight1a` | 1 (28.9 s) | 51.6 s |
| After | cold | 10 cached images reloaded | 0 | 7.9 s |
| After | warm | 36 cached images reloaded | 0 | 7.1 s |
| After, 111 payloads tampered | warm | 28 forged entries accepted at the menu, 26 images reloaded at the lock | 0 | 9.8 s |
| After, 111 headers tampered | warm | 26 entries rejected (stamp mismatch), 5 reloaded | 0 | 7.1 s |
| After, Vulkan client | warm | 26 cached images reloaded | 0 | 9.7 s |

- Engine screenshots in gameplay on OpenGL and Vulkan show the world, HUD,
  crosshair and lights intact after the reload.
- In all ten sessions (five runs), no cache entry was read after the eviction. A few cache
  hits fall between `connect` and the pure reply: the connect screen loads the
  addon splash images. The eviction replaces them.
- In both tamper runs, the pure session rewrote 108 of the 111 entries
  byte-identical to their originals. The other three belong to a size variant
  that the second session never loaded.
- Every client became an active player, and every process exited cleanly.
- Pure-session warnings: none.
- On this machine the Steam install's `q4base` also holds loose `maps/` files,
  left by earlier map-compiler work. They force the restart as before; a stock
  install has none.

### Outside this decision

- The level-load caches of the experimental `com_levelLoadModernization 1`
  follow the same rule since the
  [change below](#level-load-caches-on-pure-servers-9-november-2026).
- The Vulkan pipeline cache is opened by explicit OS path and validated by the
  driver. It never reaches the VFS, before or after this change.

## Level-load caches on pure servers, 9 November 2026

**Decision: the experimental level-load caches follow the image cache's rule.
None is read while pure, and what was decoded from them before the lock is
derived again at the next level load, without an engine restart.** With
`com_levelLoadModernization 1` (off by default), openQ4 keeps render-model,
render-world and collision caches, and the game modules keep an animation
cache, under `generated/` in the save path. Their owners open these files by OS
path, outside the search paths, so pure mode's directory gate never saw them: a
pure client read them, and a forged entry would have passed, for the reasons
[given above](#why-the-cache-cannot-be-trusted).

What changed ([contract](loading-cache-modernization.md#pure-servers)):

- While a pure lock or `fs_restrict` holds,
  `idFileSystem::GeneratedCacheReadsEnabled` answers false for every kind, so
  `OpenGeneratedCacheRead` returns nothing. The game's animation reader asks
  the same gate, as the appended `GENERATED_CACHE_ANIMATION` kind; no
  interface slot changed.
- A resident decoded from one of these caches before the lock is derived again
  at the next level load, which always follows the lock. The model manager
  purges cache-sourced models, including MD5R models converted from one and
  whatever reloadable flag the payload carried. The render world is reloaded
  instead of retained, the collision manager frees cache-sourced models
  (normally already purged at map shutdown), and the game reloads
  cache-sourced animations in place before the map spawns anything. The lock
  is only taken when every pak the session has read from is in the server's
  list, so every source can be read again.
- Model, world and collision entries are no longer written under a pure lock.
  Their content key includes the lock, so no session could read them back.
  Animation entries are keyed by their source alone and are still written from
  the pure paks, as image cache entries are.
- Learned manifests stay readable while pure. They only schedule reads: each
  entry is opened again through the pure search paths and must match its
  recorded identity before any of its bytes are used.
- `pure_level_load_cache_contract.py` compiles the gate functions against
  doubles, runs a truth table (open, pure lock, `fs_restrict`, each cvar), and
  pins every eviction link in both game trees. Its `--mutations` mode breaks
  each of 18 links and requires the check that owns it to fail. Both CI
  workflows run it.

### Evidence

A `builddir` dedicated server (`si_pure 1`, `net_LANServer 1`, `mp/q4dm1`) and
a `builddir` client with `com_levelLoadModernization 1`, each with its own save
path under `.tmp/pure-levelload-cache/`, and a base path of hard links to the
retail PK4s only. The client ran two sessions on one save path, in a hidden
window with `ui_autoJoin 1`. The first spawned a local non-pure listen server
on `mp/q4dm1`, which wrote 94 caches (92 model files, the world, the collision
set) and the animation caches. The second spawned the same local server again,
now loading from those caches, then disconnected and joined the pure server,
with `fs_debug 1` listing every explicit file open.

| Client | Before the connect | After the connect | Engine reloads | Result |
| --- | --- | --- | --- | --- |
| OpenGL 640x480 (two runs) | 90 cache hits: 95 model, 1 world, 1 collision and 222 animation opens | No cache opened; 91 models purged, world reloaded, 222 animations reloaded, 100 cached images reloaded | 0 | Active player, no warnings |
| Vulkan 640x480 | Same | Same as OpenGL 640x480 | 0 | Active player, no warnings |
| OpenGL 640x360 | Same | No cache opened; the restart below flushed everything | 1 | Active player, no warnings |

- In every pure session the level-load report for the pure map load showed
  0 hits, 0 misses and 0 writes, so the coordinator was never consulted. The
  only file opened under `generated/` after the connect was the learned
  manifest for the pure search state. The game rewrote the 222 animation
  caches from the pure paks.
- Engine screenshots in pure gameplay on OpenGL and Vulkan show the world,
  static props, the weapon view model and the HUD intact after the purge.
- Every client became an active player, and every process exited cleanly.

### Outside this decision

- A client that loaded a map in a non-4:3 window before connecting restarted
  on its pure connect. The session publishes an expanded loading background
  (`guis/assets/generated/loadscreens/<map>_<w>x<h>.tga`) under the save path
  and loads it back through the search paths, which counted as a directory
  read. That is independent of the level-load caches and of
  `com_levelLoadModernization`; it is why the 640x360 run restarted. The
  [next decision](#expanded-loading-backgrounds-on-pure-servers-9-november-2026)
  resolves it.

## Expanded loading backgrounds on pure servers, 9 November 2026

**Decision: the session composes the expanded loading background in memory.
Nothing is written to the save path or read back from it, so pure clients show
the wide background like everyone else and a pure connect has nothing to
evict. Files that earlier builds saved are still purged at a pure lock rather
than restarting the engine.** In a window that is not 4:3, the session
composes the map's levelshot and any `_left`, `_right`, `_top` and `_bottom`
tiles into one picture of the window's shape
(`Session_PrepareExpandedLoadingBackground`). Until this change it wrote that
picture under the save path as
`guis/assets/generated/loadscreens/<map>_<w>x<h>.tga` and loaded it back
through the search paths. That made it openQ4's own derived data read from a
directory: a client that had loaded any map in such a window ran a full
`ReloadEngine` on its first pure connect, and the first revision below
replaced that restart with a purge but left pure clients on the framed 4:3
levelshot.

The inputs were never the problem: the levelshot and its tiles are VFS reads,
and under a pure lock the VFS serves them only from paks the lock admits (the
client-only loadscreen pack is allowed). Only the round trip through a
directory was. What changed:

- The session uploads the composite with `renderSystem->CreateImage` and
  `UploadImage` to a scratch image named `_expandedLoadingBackground_<w>x<h>`
  (one RGBA8 level, persistent), as the retained interface's generated
  textures are, and gives that name to `loading_bkgnd` and `loading_levelshot`.
  `idImageManager::ImageFromFile` returns a `_` image by name whatever the
  material asks for, so the stock GUI's implicit material and the retained
  screen's `_retained/` material both bind it. No interface changed. The size
  is part of the name because the retained interface keeps an image's
  dimensions by its name, and a window of another shape needs its own.
- Nothing in the session asks the pure gate any more:
  `FS_GeneratedDataReadsRefused`, added for the first revision, is gone.
- The image has no mip chain, so the session halves the composite, as a mip
  level would, while it is at least twice the window: a 640x360 window holds
  910x512 rather than the levelshot's 1820x1024. Larger windows keep the
  levelshot's resolution. The texture size limit halves it too.
- A video restart reallocates scratch images without their contents. The
  session keeps the pixels and uploads them again when
  `GetVideoRestartCount` moves, checked at the top of every session `Draw`
  before any GUI draws, so a loading screen that outlives a restart (the
  retained screen held over a multiplayer join) keeps its picture. Checked
  image-policy restarts and recoverable device restarts move the count too. A
  console `reloadImages` does not; it blanks the picture as it does the
  retained interface's own generated textures, until the next load composes a
  new one. Level loads keep the image (it is persistent) and the texture
  reduction reload skips scratch images.
- A load that shows the stock levelshot shrinks the held image to one black
  pixel and drops the pixels. A failed upload or a defaulted material leaves
  the stock levelshot selected, with a warning for the upload
  (`Could not upload expanded loading background`, or `restore` after a video
  restart). The gameplay benchmark, the validation matrix and the shadow
  compatibility contract count both as the `expandedLoadscreenUpload`
  signature.
- `FS_IsGeneratedLoadscreenPath` and the renderer's purge of
  `guis/assets/generated/loadscreens/` stay for files earlier builds left in
  players' save folders. This build never names them, but a stray reference
  can still open one before a lock (a level-load manifest learned by an
  earlier build with `com_levelLoadModernization 1`, for example); without the
  classifier that read would restart the engine again.
- `com_captureLoadingScreen 1` is a new diagnostic: it captures each map's
  finished loading screen with the engine `screenshot` command, as
  `screenshots/loading_<n>.tga`. Console commands do not run inside a load, so
  nothing else can capture one; `loadscreen_format_audit.py` uses it in place
  of the TGA it used to read.
- `pure_expanded_loadscreen_contract.py` now pins the in-memory path (no
  save-path writer or pure gate in the session, the `_`-prefixed sized name,
  the upload, the restart restore before the loading GUIs draw, the stock
  levelshot after a failure, the built-in lookup) as well as the legacy
  classifier and purge. Its `--mutations` mode breaks 20 links.
  `filesystem_write_qpath_safety.py` drops the publication and staging-path
  checks, keeps the TGA writer's result contract, and rejects any save-path
  write in the session's composer.

### Evidence

The same setup as the first revision below: a `builddir` dedicated server
(`si_pure 1`, `net_LANServer 1`, `mp/q4dm1`) and a `builddir` client with
`com_levelLoadModernization 1` in a hidden 640x360 window, each with its own
save path under `.tmp/pure-loadscreen/`, on a base path of hard links to the
retail PK4s only. The client's first session hosted `mp/q4dm1` locally; its
second hosted it again, then disconnected and joined the pure server with
`fs_debug 1`. `com_captureLoadingScreen 1` captured every finished loading
screen. Driver: `.tmp/pure-loadscreen/inmemory_probe.py`.

| Client, 640x360 | Pure map's loading screen | Engine reloads | Pure session | Connect to gameplay | Result |
| --- | --- | --- | --- | --- | --- |
| OpenGL, retained | Wide background, `_expandedLoadingBackground_910x512` | 0 | 51.8 s | 28.7 s | Active player, no warnings |
| OpenGL, stock (`ui_retained 0`) | Wide background in `mplevel.gui` | 0 | 46.7 s | 25.3 s | Active player, no warnings |
| Vulkan, stock | Same | 0 | 49.1 s | 26.1 s | Active player, no warnings |
| Vulkan, retained | Same as OpenGL, retained | 0 | 162.2 s* | 81.4 s* | Active player, no warnings |

- Every load composed `_expandedLoadingBackground_910x512` (the 1820x1024
  composite halved for the 360-pixel window) from `gfx/guis/loadscreens/q4dm1`
  and its `_left`/`_right` tiles. After the connect, `fs_debug` shows those
  reads served from `pak2.pk4`, the client-only loadscreen pack, and no open of
  anything under `guis/assets/generated/loadscreens/`. Neither that folder nor
  `_oq4/` exists in the client's save path after both sessions.
- The pure lock reloaded the 100 cached images, as the image cache decision
  requires, and evicted no loading background: there is none to evict.
- Captures of the pure map's loading screen (`screenshots/loading_2.tga`) show
  the wide picture filling the 16:9 window on both renderers, in the retained
  screen and in the stock one.
- Timings are comparable with the first revision's 45.8 s OpenGL pure session,
  and well under the 88.0 s of the old restart. \* The Vulkan retained run
  overlapped other sessions' emulator and benchmark runs and used the build
  just before the restore log line was added; it shows behaviour, not timing.
- A video restart: `.tmp/pure-loadscreen/restart_probe.py` hosted `mp/q4dm1`
  in a 640x360 window, read the held image back with `screenshot image`, ran
  `vid_restart` and read it again. On OpenGL and Vulkan the log shows the image
  reallocated (`reallocating scratch`) and then `restored after a video
  restart`, and both readbacks are byte-identical.
- The picture is the one the previous build published: the 910x512 readback
  against that build's `q4dm1_1820x1024.tga`, box-filtered to the same size,
  scores 51.3 dB PSNR.
- A 4:3 window composes nothing; its loading screen shows the stock levelshot as
  before, and its pure join also ran without an engine reload.

### First revision: purge at the lock

Earlier the same day (86bc257f4) the session still published the picture under
the save path. That revision stopped composing while pure, made a read before
the lock an eviction instead of a restart, and left pure clients on the framed
4:3 levelshot. Its evidence, with the
[level-load probe](#level-load-caches-on-pure-servers-9-november-2026)
setup, unchanged except for its window and counters: a `builddir` dedicated server (`si_pure 1`, `net_LANServer 1`,
`mp/q4dm1`), a `builddir` client with `com_levelLoadModernization 1` in a
hidden 640x360 window, each with its own save path under
`.tmp/pure-loadscreen/`, and a base path of hard links to the retail PK4s only.
The client's first session hosted `mp/q4dm1` locally. Its second hosted it
again, published and read the 1820x1024 background, then disconnected and
joined the pure server with `fs_debug 1` listing every open.

| Client, 640x360 | At the lock | Engine reloads | Pure map load | Pure session | Result |
| --- | --- | --- | --- | --- | --- |
| OpenGL, before (77c519cdc) | Restart required by `q4dm1_1820x1024.tga` | 1 | 22.6 s, after the reload | 88.0 s | Active player |
| OpenGL, after | 100 cached images reloaded, 2 loading backgrounds purged | 0 | 16.1 s | 45.8 s | Active player, no warnings |
| Vulkan, after | Same | 0 | 49.0 s* | 83.4 s* | Active player, no warnings |
| OpenGL, after, repeat | Same | 0 | 44.7 s* | 94.3 s* | Active player, no warnings |

- Every run read the background from the save path five times before the
  connect. After the connect, no file under
  `guis/assets/generated/loadscreens/` was opened or written; the pure map's
  loading screen used the stock levelshot, logged as
  `Expanded loading background ... skipped while pure`.
- Without the restart, the 16:9 pure map load (16.1 s) is in line with the
  4:3 run's 18.1 s in the level-load evidence above.
- \* The Vulkan run and the repeat overlapped another session's build and a
  benchmark on the same machine; every load phase took two to three times
  longer, image probing included (9.2 s against 3.5 s). They show the
  behaviour, not timing.
- Engine screenshots in pure gameplay on OpenGL and Vulkan show the world, the
  weapon view model and the HUD intact.
- "Pure session" runs from the client's start to active play and includes the
  local load before the connect.

## Still open

Real remote peers (WAN routes, NAT, other operating systems and architectures)
and the defaults decision for enhanced prediction and lag compensation remain
open under MP-07/MP-08; the user deferred the peer gates on 28 October. So do
LAN discovery across machines and on Linux and macOS, real multi-record DNS,
and running the retail client binary (its challenge was reproduced instead). The server does not adapt its rate to a congested link
on its own: a player whose connection is slower than the match needs has to
lower `net_clientMaxRate`. Whether to adapt automatically is a design decision.
Simulated conditions do not model route changes, cross-traffic sharing a
bottleneck, or asymmetric links (the rate applies both ways).

## Remote weapon play and live spectator follows, 5 November 2026

`mp_remote_weapon_capture.py` runs a listen host on q4dm1, playing with god
mode and `notarget`, with three bots. A second process joins over loopback as
a spectator and follows a bot. It writes bursts of engine captures, first
person and then third person, while the host logs the server's camera verdict
for every observer/target pair (`MP_CAMERA ... allowed=`). No operating-system
input is used.

The first runs found a casual-match defect. During live play the server
refused every bot as a follow target (`allowed=0`), while it allowed the human
host. A bot added after warmup is bound to the match session before it is in
game, and participation was re-synchronized only in warmup and countdown, so
the bot stayed inactive until the next warmup. An interim fix that synced on
entering the game exposed the same gap from the other side: a client that
switched to spectating during live play kept an active player's state, and
with it no follow rights. A casual session has no roster transactions, so
`idMultiplayerGame::Run` now mirrors participation in every phase for
non-managed sessions. Managed matches still freeze participation outside
warmup. `mp_match_spectator_follow_contract.py` pins the rule, and its
single-reversal control fails.

After the fix (`.tmp/mp-side/weapon-fix3/report.json`, SHA-256
`df9b035ed1a661ab6cb5d5c1a74c5b70d7ecc10c91ae05cba190f93c792471ed`, OpenGL,
skill-5 bots, 120 captures per view), the server allows the host and all three
bots, and the spectator follows bot 3 for the whole run. Inspected captures of
that bot's lightning-gun fight show the beam leaving the drawn gun and ending
on the target. Impact sparks, damage numbers and the hit marker sit on the
target under the crosshair. Third-person captures show the followed body from
behind. This qualifies remote weapon presentation as a loopback spectator sees
it. The firing player's own same-fire-frame alignment needs real input and
stays in the [human checklist](mp-human-qualification.md#mp-07-weapon-visuals-under-prediction),
together with impaired and WAN peers. Defaults are unchanged.
