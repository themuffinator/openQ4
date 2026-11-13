# Multiplayer network qualification

## Connection replies that fit one packet, 13 November 2026

The [path MTU run](#a-1280-byte-path-mtu-13-november-2026) found that a path
dropping IP fragments stopped every join, because `infoResponse` and
`connectResponse` carried the whole server-info dictionary (about 1.8 KB)
outside the fragmenting channel. Both now fit one packet on a 1280-byte path:
1,232 bytes of UDP payload, the IPv6 minimum less its headers, which also fits
IPv4 (`src/framework/async/OutOfBandReply.h`).

- **The info reply** (server browser, LAN scan, and the protocol check a join
  makes first) always carries the keys the browser's rows, filters and details
  card read, and every player. The other server-info keys follow in order while
  the reply fits, so a quiet server still sends most of them. Player names are
  cut only when the browser's keys and the player list alone would not fit
  (more than about 24 players with long names). The wire format is unchanged,
  so every client reads it as before.
- **The connect reply** stays as it was for a client that does not ask.
  A connect request may now end with a capability tag (`OQ4C`) after the
  two-byte field no server has ever read; servers that do not know it stop
  reading before it. To a client that names the capability, the server replies
  with no game-init id (`-1`, which no server sends, since a game-init id is
  always positive), only the keys the loading screen names and the connect
  roster trimmed to the space left. It then sends the existing game-init
  message, which carries the full server info, through the channel, which
  fragments it. A lost game-init message is sent again while the client still
  reports no game, as on a map change, without restarting the pure checks the
  client passed before connecting. The client waits for that message, then
  loads the map without the pure prompt and keeps its connect roster.

Mixed builds: an older client never asks, so it still gets the full connect
reply; a newer client facing an older server gets the full reply and loads as
before. One edge remains: if the server changes map in the fraction of a second
while a newer client waits for its game-init message, that client loads, then
answers the map change's pure check and loads again. It still joins.

Results with the staged Windows x64 build of `f22ea7b98` plus this change
(client `d365a8435da6a535`, dedicated `bbf0e40b355de946`, MP module
`1ec8f7187b5032e4`), each a 12-player q4ctf1 CTF match on a pure dedicated
server, the client joining over the simulated 1280-byte path:

| Run | Result | Datagrams received | Larger than the path | Largest |
| --- | --- | ---: | ---: | ---: |
| IPv6, dropping, 300 s | pass, joined in 33.1 s | 9,042 | 0 | 1,230 bytes |
| IPv6, fragmenting, 300 s | pass, joined in 23.0 s | 9,012 | 0 | 1,226 bytes |
| IPv4, fragmenting, 300 s | pass, joined in 24.6 s | 9,002 | 36 (72 IP fragments) | 1,310 bytes |
| IPv4, dropping | fail: never loads the map | - | - | - |

Over IPv6 nothing crosses the path in fragments any more, joining included;
the server logged its deferred connect reply and an info reply that left out
24 keys. Over IPv4 the join now gets as far as the connect reply, but the
game-init message, like every large IPv4 message, travels in 1,300-byte channel
fragments (1,310-byte datagrams), which a fragment-dropping 1280 path loses.
That is the IPv4 fragment size, unchanged here because changing it would
change the wire format. The LAN reconnect smoke (IPv4 scan and IPv6 endpoint,
three pure joins each) and the retail protocol smoke pass on the same stage.
Reports are in `.tmp/mp08-mtu/runs2/`.

`oob_reply_budget_state.py` builds a server-info dictionary from every
`CVAR_SERVERINFO` declaration in the source (81 at their defaults), with a
63-byte server name, 0 to 32 players with names up to the 47-byte maximum, and
40 further mod keys. It checks that both replies fit 1,232 bytes, that the info
reply keeps the browser's keys and fills towards the limit, and that the
deferred connect reply's trimmed roster decodes. Eight single-reversal copies
of the sources fail it. `oob_reply_contract.py` checks the production wiring
on both ends, with nine controls. This remains a one-machine simulation; it
does not replace the two-machine run in checklist row 8.1.

## A 1280-byte path MTU, 13 November 2026

MP-08's checklist row 8.1 asks for a full match over a 1280-byte MTU. One
machine cannot lower a real path, so the network simulator gained one
(`net_forceMTU`, see [simulated network conditions](#simulated-network-conditions)).
It models the two ways a path treats a packet larger than its MTU: it carries
it in IP fragments, as hosts and IPv4 routers do (`net_forceMTUDrop 0`), or it
loses it, as a path does that filters IP fragments or never delivers IPv6's
Packet Too Big (`net_forceMTUDrop 1`). Only the client's port carries the path,
so both directions of its link see it, as when one machine's interface MTU is
lowered.

`tools/tests/mp_path_mtu_soak.py` runs checklist row 8.1's match: a pure
dedicated server on `mp/q4ctf1` in CTF, filled by bots to 12 players, and one
client that joins it and must stay an active player for the whole run. Every
30 seconds it reads the client's state and delay-line counts and checks both
logs for reliable-message overflows, snapshot overflows, disconnects and the
server dropping the client. Results with the staged Windows x64 build of
`6d3f1a566` plus this change (client `7a045e9e92b1dd71`, dedicated
`a572191936593cf2`, MP module `e804081afbca9925`):

| Run | Result | Players | Datagrams received | Larger than the path | Largest |
| --- | --- | ---: | ---: | ---: | ---: |
| IPv4, fragmenting, 300 s | pass, joined in 25.4 s | 12 | 9,495 | 73 (146 IP fragments) | 1,823 bytes |
| IPv6, fragmenting, 300 s | pass, joined in 25.0 s | 12 | 9,432 | 2 (4 IP fragments) | 1,823 bytes |
| IPv4, dropping | fail: never connects | - | - | - | - |
| IPv6, dropping | fail: never connects | - | - | - | - |
| IPv6, no path, 20 s (control) | pass | 12 | - | - | - |

Both fragmenting runs held the match for five minutes with no overflow, drop or
disconnect, and every process exited cleanly. Reports are in
`.tmp/mp08-mtu/runs/`.

**Inside the match, IPv6 never needs IP fragmentation; the connection replies
do.** Every channel datagram over IPv6 stayed within 1,232 bytes, the payload a
1280-byte path carries whole. The only two larger datagrams were out-of-band
replies, which the channel cannot fragment. Before the challenge the client
asks for `getInfo`, to learn the server's protocol, and the server's
`infoResponse` carries the whole server-info dictionary. `connectResponse`
carries it again. With about 73 server-info CVars at their defaults, each
reply was about 1.8 KB (both now fit one packet; see
[connection replies](#connection-replies-that-fit-one-packet-13-november-2026)).
A path that drops IP fragments lost them both: in the
dropping runs the server sent 179 info replies, the client received none and
sent 200 challenges without an answer. The same replies feed the server
browser and the LAN scan, so such a path also hides the server from them.

IPv4 relies on IP fragmentation by design: its fragments are 1,300 bytes, and
its 73 oversized datagrams include those as well as the two replies. IPv4
hosts and routers fragment by default, so the fragmenting run is the normal
case there. Reducing the IPv4 fragment size would change the wire format: the
receiver identifies the last fragment by its length.

Fixed in the same change: `SendMessage` decided whether to fragment from a
message's unpacked length, but packing can grow it, by up to double for data
the zero-run coder cannot shorten. A grown message then left as one datagram
up to about twice the family's limit, over IPv6 as much as roughly 2.3 KB on
a path that carries 1,232. It is now sent in fragments; receivers already
assemble either form. `network_fragment_state.py` now also executes production
`SendMessage` with a packer that grows its input by 0, 25 and 100 percent over
both families. It checks that no datagram exceeds a full fragment and that the
receiver gets the packed message intact, and a ninth reversal control
(without the check) fails it.

This is a one-machine simulation on loopback. It does not replace checklist
row 8.1 on two physical machines, and it does not cover real path MTU
discovery, ICMP behaviour or middleboxes.

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
| `net_forceMTU` | Path MTU in bytes, each way (576-65,535; `0` for none). A datagram larger than the path crosses in IP fragments. |
| `net_forceMTUDrop` | `1` drops a datagram larger than `net_forceMTU` instead, as a path that filters IP fragments or loses Packet Too Big does. |

Datagrams leave in the order they fall due, with ties in arrival order, so jitter
wider than their spacing reorders them as a real link does. Comparisons use the
millisecond clock wrap-safely. Payloads are stored at their real size (up to the
65,535-byte UDP limit); a due datagram larger than the reader's buffer is
discarded and counted, never truncated. A line holds at most 8,192 datagrams or
32 MiB and counts refusals beyond that. Lines keep draining after the settings
return to zero, so turning impairment off never reorders what is still queued.
Due sends are flushed whenever the port is polled, not only when the next send
arrives.

A rate turns a line into a bottleneck. Each datagram, with its UDP header and
the IP header of every packet it takes (20 bytes for IPv4, 40 for IPv6, plus an
8-byte Fragment header on each IPv6 fragment), takes its size over the rate to send, and waits behind whatever
the link is still sending. One that would finish later than the queue holds is
dropped, as a full router buffer drops it. An idle link always takes a
datagram, so one larger than the queue still passes. A datagram lost further
along the path still takes its turn on the link. The latency and jitter start
when the datagram leaves the bottleneck. Burst loss is a two-state
(Gilbert-Elliott) chain: it stays in its losing state for `net_forceDropBurst`
datagrams on average and spends `net_forceDrop` percent of the time there. A
loss rate above burst / (burst + 1) cannot be reached and settles at that.
With no rate and a burst length of 0 or 1, the line behaves exactly as before.
Until 13 November the bottleneck charged IPv6 datagrams IPv4's 28 header bytes;
it now charges 48.

A path MTU checks each datagram against the path with its own family's headers:
over a 1,280-byte path a UDP payload of up to 1,252 bytes fits whole on IPv4 and
1,232 on IPv6. A larger one is either dropped (`net_forceMTUDrop 1`) or split
into IP fragments, as the sending host would: every fragment but the last carries
a multiple of 8 bytes of the UDP datagram, the bottleneck pays every fragment's
headers, and the datagram is lost if any fragment is.

`netImpairment` prints the settings and, per open port and direction, the
datagrams offered, dropped, duplicated, released, reordered, refused and
discarded, and the deepest queue so far (`NET_IMPAIR` lines). The line ends
with the payload bytes offered, the loss runs, the datagrams the bottleneck
dropped, its longest wait, the datagrams larger than the path MTU, the IP
fragments those took and the largest payload offered; these come last so
harnesses that read the leading fields keep working. `NET_IMPAIR_SETTINGS`
likewise ends with `mtu=` and `mtuDrop=`.

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
length), the path MTU's limits per family, its dropping and fragmenting modes,
the loss of a fragmented datagram with any of its fragments, every fragment's
headers on the bottleneck, and the new settings changing nothing while off.
The native core safety test runs the cases, and
`tools/tests/network_impairment_state.py` also runs them against 20
single-reversal copies of the header, each of which must fail.
`tools/tests/network_impairment_contract.py` checks that both platforms define
the same CVars, open and release the lines with the port, queue receives and
sends with each datagram's family, flush due sends while polling and report,
with the report's leading fields unchanged; it carries 11 single-reversal
controls of its own.

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
Over IPv4, a path that drops IP fragments still cannot carry a match: large
messages travel in 1,300-byte channel fragments
([connection replies](#connection-replies-that-fit-one-packet-13-november-2026)).
Simulated conditions do not model route changes, cross-traffic sharing a
bottleneck, or asymmetric links (the rate applies both ways).

## The firing player's own shots under prediction, 12 November 2026

A client used to draw its own machinegun, shotgun and railgun shots only from
the server's copy (`GAME_UNRELIABLE_MESSAGE_HITSCAN`), so each tracer and wall
mark arrived a round trip after the muzzle flash and started from where the
server had the gun. Firing while turning drew them well off the crosshair. The
client now draws them on the predicted frame it fires them
(`net_mpPredictHitscan 1`, the default): `rvWeapon::Attack` runs
`rvWeapon::Hitscan` on the newest predicted frame, which traces and plays the
path, tracers and world impacts with no damage. The server's copy of a
predicted shot adds only impacts on actors, which stay its verdict. A note
ring pairs copies with predictions, oldest first per hit-scan, with a
one-second expiry; a copy with no waiting note draws in full. Multiplayer
spread comes from `idGameLocal::HitscanSpreadSeed(shooter, game time)`, so
both sides draw the same pellets. The lightning gun and the gauntlet already
traced on the client and are unchanged.

`mp_firing_view_capture.py` measures this without input. A listen host on
q4dm1 stays in warmup with the warmup arsenal and three paused bots, and a
second process joins over loopback as the shooter. The shooter selects
weapons with `testWeaponWheel`, turns at 220 degrees a second with
`in_presentationViewProbe` and fires with `in_presentationViewProbeAttack`.
The steps are:

- target: railgun, machinegun and gauntlet at the host, which the harness
  stands still in front of the shooter;
- sweep: machinegun, railgun, shotgun and lightning gun, four seconds each,
  turning;
- respawn: the machinegun, turning, through a `kill` and the respawn.

Both processes log every shot with `net_mpHitscanDebug` (`MP_HITSCAN`): the
server's shots, the client's predictions, and the copies it received, each
with its game time, seed, direction, distance from the gun's flash joint
(plus the hit-scan's `fxOriginOffset`) and angle from the aim the client held
at that moment. The report pairs server shots with predictions by game time
and hit-scan. `mp_predicted_hitscan_state.py` runs the note ring and the seed
in a native fixture spliced from the production source, and fails 14
single-reversal controls.

Five runs on one staged build (`game-mp_x64.dll` SHA-256
`027622b3d9b3bd143cd0f28f1dff4bf2a8e9fe78ea64461337a238058015345c`), under
`.tmp/mp07-firing/`, all pass:

| Run | Server shots | Same frame, outside the respawn | Copies drawn in full | Drawn shot vs aim (median, sweeps) |
| --- | --- | --- | --- | --- |
| `p1-loopback`, OpenGL | 112 | 84 of 84 | 0 | spread only: machinegun 0.9°, railgun 0°, shotgun 4.9° |
| `p0-loopback`, OpenGL, prediction off | 112 | – | 112 | machinegun 12.1°, railgun 10.6°, shotgun 15.8° |
| `p1-impaired`, 80 ms ±20, enhanced prediction and lag compensation | 113 | 84 of 84 | 0 | spread only |
| `p0-impaired`, same, prediction off | 111 | – | 109 | machinegun 53.8°, railgun 51.3°, shotgun 58.7° |
| `p1-vulkan`, Vulkan | 115 | 85 of 85 | 0 | spread only |

Report SHA-256s: `p1-loopback`
`8134c2f6b8354d5aa23d16299078d8be483feaedf10da8e63bc21e163a396562`,
`p0-loopback`
`ac570b0f0ee414d5f4b4bef833e762a9cdc2ec67d81cbe584c679fa6cb1159b8`,
`p1-impaired`
`898038d8aee481a7c9a5eb73856d2b939ed64deefd2fc20884ca7037192405b3`,
`p0-impaired`
`0d00eca6d5e913556168e30557df0e443b578f9ad088e801ccc6b339ad86bf29`,
`p1-vulkan`
`2e5137208c03e9c31eab956586761cb2427caf82a712fb6c87e84b9f204c75a1`.

- **Same shot on both sides.** Every paired prediction has the server's seed
  and a direction identical to the log's five decimals. Every prediction
  starts at the gun drawn on that frame (gun gap 0.00).
- **Old behaviour.** With prediction off, the copies started a median 2.9
  units from the gun on loopback and 13.5 units with 80 ms of latency. They
  were 7.8° off the aim at the median and 17.7° at p95 on loopback, and 51.6°
  and 59.4° with 80 ms of latency.
- **Hits on the body.** The machinegun hit the standing host with every server
  shot (37 of 37 on OpenGL loopback). The copies drew 29 of those impacts on
  the client's copy of the host (34 on Vulkan); the other replays missed it by
  the small pose difference a remote body always has. The copy's replay
  predates this work. With 80 ms of latency, two of 113 copies never arrived;
  they are unreliable messages.
- **Respawn (7.4).** No copy was drawn in full around a death. The client's
  first shots after the respawn started 2124 units (loopback), 2630 units
  (impaired) and 2207 units (Vulkan) from where it died: nothing is drawn
  from the old position. The client did fire out of step with the server
  after the respawn. Its new weapon is caught up straight to ready
  (`weaponCatchup` → `rvWeapon::NetCatchup`) while the server's raises, so it
  fired four or five shots the server never fired. For the rest of a burst
  held through the respawn, it then drew each shot one frame (16 ms,
  loopback) or three frames (48 ms, impaired) before the server fired it. Its
  muzzle flash and sound already did this before; now its tracer and wall
  mark follow them, while the body impacts come from the server's shots.
  Fixed on 13 November: the catch-up came from a false state hitch after the
  respawn, not from new weapon entities
  ([below](#the-weapon-for-a-new-life-raises-with-the-servers-13-november-2026)).
- **Captures.** The captures agree. The machinegun and railgun sweeps show
  each tracer or rail leaving the barrel for the crosshair on the capture it
  fires in, with impacts at the crosshair. With prediction off, the same
  captures show the muzzle flash alone, and the beam or impacts a moment
  later beside the crosshair, where the aim had been. The machinegun target
  shows tracers from the gun to blood on the body under the crosshair.
- **Gauntlet target.** In those five runs the gauntlet target strafed, steered
  through the host's 100 ms control file. The strafe drifted out of the
  gauntlet's reach, so the captures show the host beside the crosshair. With
  the host standing 44 units away, a sixth run (`p1-gauntlet`, SHA-256
  `b29dcbb99f2d2e7fd1cb8c6f27be6277f22acd624ce6bdd6066ca2ac0c126885`)
  passes with the same hit-scan results: 85 of 85 same-frame, no copy drawn
  in full, 37 body hits. Its captures show the blade on the host's chest under
  the crosshair. The contact effect is too small to judge at 960×540, so the
  gauntlet on a moving bot stays by eye.

A set run earlier the same evening is superseded. A stopped runner had
outlived its task and started a second host on the same port, so one host
never started, and one Vulkan pass lost four frames of its fire schedule to a
screenshot stall under that load. The harness now refuses a busy port.
Remaining for MP-07: walking through a teleporter while firing, a moving
gauntlet target and the feel with a real mouse
([checklist](mp-human-qualification.md#mp-07-weapon-visuals-under-prediction)),
and real WAN peers. The post-respawn fire phase above is fixed (next section).

## The weapon for a new life raises with the server's, 13 November 2026

A client's weapon for a new life now comes up with the server's, so a burst
held through a respawn stays on the server's frames. Before, the client caught
that weapon straight up to ready while the server's raised it, so the client
fired shots the server never fired. It then drew the rest of the burst one to
three frames ahead of the server's shots.

The cause was the client's own snapshot bookkeeping. The respawn branch of
`idPlayer::ReadFromSnapshot` calls `Init()`, which sets `lastSnapshotSequence`
back to 0 and clears `idealWeapon`. The next snapshot therefore read as a
state hitch (missed snapshots) carrying a change of weapon, and the client
marked its weapon for catch-up. Raven's `UpdateWeapon` dropped that request
when it made the weapon, so the weapon raised. On 28 October openQ4 made
`UpdateWeapon` honour it with `NetCatchup`, so a paused demo seek would draw a
ready weapon ([multiview demos](multiview-demos.md#paused-seek-weapon-catch-up-28-october-2026)).
That made every respawn catch up. Joining again after spectating did the same
by another route. `Spectate( true )` also calls `Init()`, so its next snapshot
read as a hitch and left a catch-up request standing for the whole time the
player spectated. The join then caught the weapon for the new life up. The
server spawns new weapon entities for a joining player, and the client's
spawn-id check (`SetSpawnId`) asked for catch-up as well.

The server makes the weapon for a new life, and starts its raise, on the
spawn's own frame. The `MP_WEAPON` logs below show the respawn and the raise
at the same game time in every run. The fix is in `src/mpgame/` only:

- The respawn branch and `Spectate( true )` keep the snapshot sequence across
  `Init()`. Entering spectating also drops any catch-up still waiting for the
  last life's weapon.
- A respawn, or new weapon entities, catch up only when that snapshot itself
  came after missed snapshots.
- Otherwise the client records the frame the server's weapon began its raise:
  - A snapshot without the server's weapon state means that weapon is made on
    the next frame.
  - A snapshot that carries it was taken on the spawn's frame or a frame or two
    later, since snapshots go out every 33 ms. The spawn's knockback
    (`SPAWN_KNOCKBACK_MSEC`, 100 ms) tells which. The server's physics runs it
    down 16 ms a frame from the spawn's frame, and the snapshot carries it. A
    value that doesn't read as that clock falls back to the snapshot's frame.
- `UpdateWeapon` hands that time to the new weapon
  (`rvWeapon::SetNetRaiseStartTime`). `rvWeapon::PlayAnim` plays the view and
  world raise from it, so `AnimDone` reaches ready, and the weapon fires, on
  the server's frames.
- A catch-up the seek path asks for still wins. A weapon swap the client missed
  across a hitch still catches up. `LoadDeferredModel` keeps Raven's catch-up:
  in every run, the model reload a join triggers ran before the new weapon
  existed on both sides.

`net_mpHitscanDebug` now also logs `MP_WEAPON` lines:

- each spawn on the server;
- each respawn or new set of weapon entities on the client, with the raise
  time it chose;
- each catch-up;
- each raise's start and end, on both sides.

`mp_firing_view_capture.py` gained a rejoin step. The shooter spectates, joins
again and fires while turning. The harness pairs each scripted new life (the
respawn and the rejoin) with the client's raise for it. It fails if that raise
is caught up or starts on another frame, and it holds both bursts to
same-frame matching. It reports the map-start join without judging it. The
shooter's script turns logging on after that join, and the join can fall in
the gaps while a connection starts.

`mp_predicted_hitscan_state.py` adds a native fixture
(`tools/tests/native/MpRespawnWeaponRaiseFixture.cpp`) spliced from
production. It covers:

- the snapshot reader's hitch test, weapon-change and weapon-entity blocks, and
  its respawn branch;
- the snapshot bookkeeping in `Spectate( true )`;
- the weapon `UpdateWeapon` makes;
- the raise `rvWeapon::PlayAnim` plays.

It runs these cases:

- a death and respawn with and without the server's weapon in the respawn
  snapshot, with spawn clocks of one, two and three frames, plus a clock that
  ran out, one that never ran, and a knockback that is not the clock;
- a respawn across a hitch;
- a join with new weapon entities;
- spectating and joining again, with and without a catch-up still waiting;
- missed and seen weapon swaps.

Its 19 single-reversal controls fail. One of them restores the original defect
by dropping the sequence latch. `mvd_seek_weapon_state.py` checks that a
seek's catch-up still wins over a respawn's raise time.

Two runs on one staged build (`game-mp_x64.dll` SHA-256
`e4878ca2607a29d3db2c98b540b47db8dedb6c83a90ef48b12d5f777c1a95ad0`), with
`--predict-hitscan 1`, under `.tmp/mp07-respawn/`, both pass:

| Run | Server shots, same frame | Respawn burst | Rejoin burst | Respawn raise, server and client | Rejoin raise, server and client |
| --- | --- | --- | --- | --- | --- |
| `evidence-loopback`, OpenGL | 135 of 136 | 28 of 28 | 23 of 23 | 77056-77765 both | 89584-90293 both |
| `evidence-impaired`, 80 ms ±20, enhanced prediction and lag compensation | 136 of 136 | 29 of 29 | 22 of 22 | 77424-78133 both | 90000-90709 both |

Report SHA-256s: `evidence-loopback`
`39a1c4fd2fceeae59fcdb3338ff9cd68e2d5fb230c4b1ed0c5c1457de70fc5e3`,
`evidence-impaired`
`fb61ed70235b182623034bd3f24f05a8680e3892e1b366682c5e4fcec183e0bd`.

- **No shots the server never fired after a respawn.** In both runs, every
  prediction without a server shot came before the respawn: one shot on
  loopback, two with latency. A held burst keeps predicting until the client
  hears of its own death, a round trip after the server killed it. The client's
  first shots after the respawn started 2634 and 2158 units from where it
  died. Every matched direction is the server's to the log's five decimals.
- **The miss on loopback** is one server machinegun shot in the standing target
  step that the client drew from the server's copy. It has nothing to do with a
  respawn.
- **The knockback clock is needed.** On the build before it, a respawn on the
  frame before its snapshot left the client's raise one frame (16 ms) late
  (`r2-loopback`). The snapshot carried 68 ms of knockback, two frames down,
  and the clock now places the spawn on the right frame.
- **The map-start join** is reported, not judged. In the impaired run the
  client's raise for it came 160 ms late. It landed in the gaps while the
  connection started, with the knockback clock run out. Late means the client
  fires late, never shots the server does not fire.
- **Open: aim after a join under latency.** In two of four runs at 80 ms ±20,
  the server's aim drifted from the client's about a second into the burst
  after the rejoin. In `r9-impaired-diag` it drifted 119°, with the server's
  fire four frames behind, and snapshots had been lost there. In
  `final-impaired` it drifted 56° over two shots, which still fell on the same
  frames. `r10-impaired-diag` and `evidence-impaired` were clean, and no
  respawn burst drifted. The weapon raise matched in every run, so the cause
  lies elsewhere. A likely place is the view angles the server sets on its
  first think after a spawn (`SetViewAngles( spawnAngles )` from whatever
  command it has then). The harness reports the rejoin's directions under
  simulated latency and holds them only without it.

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
