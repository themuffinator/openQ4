#!/usr/bin/env python3
"""Sample overlay for openQ4's live match event stream.

Set `g_matchEventStream 1` on a server, or on a client that spectates a match,
and the game appends typed events to `<save path>/baseoq4/match-events/live.ndjson`
(docs/dev/mp-live-event-stream.md). This tool follows that file and shows the
match three ways:

  python tools/overlay/match_overlay.py --file <live.ndjson>            serve a page
  python tools/overlay/match_overlay.py --file <live.ndjson> --print    print a feed
  python tools/overlay/match_overlay.py --file <live.ndjson> --once     print the state

The page (http://127.0.0.1:27980/ by default) has a transparent background and
can be added to a broadcast as a browser source; `/state.json` is the same
state for a tool of your own. Only the Python standard library is used, and the
server listens on the loopback address unless `--host` says otherwise.

`MatchState` is the part to reuse: feed it one decoded line at a time. It keeps
only what the stream states, ignores event types and fields it does not know,
and starts again when a new stream begins.
"""
from __future__ import annotations

import argparse
import json
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

SCHEMA = 1
FEED_LINES = 8


class MatchState:
    """The match as the stream has described it so far."""

    def __init__(self) -> None:
        self.reset()
        self.streams = 0
        self.rejected = 0

    def reset(self) -> None:
        self.source = None
        self.map = None
        self.game_type = None
        self.phase = None
        self.team_game = False
        self.teams: dict[str, int] = {}
        self.players: dict[int, dict] = {}
        self.flags: dict[str, dict] = {}
        self.feed: list[dict] = []
        self.sequence = -1
        self.gaps = 0
        self.time = 0
        self.standings = None

    def apply(self, event) -> bool:
        """Applies one decoded line. Returns False when the line is not an event of this schema."""
        if not isinstance(event, dict) or event.get("v") != SCHEMA or not isinstance(event.get("type"), str) \
                or not isinstance(event.get("seq"), int) or not isinstance(event.get("t"), int):
            self.rejected += 1
            return False
        kind = event["type"]
        if kind == "stream":
            self.reset()
            self.streams += 1
            self.source = event.get("source")
            self.map = event.get("map")
            self.game_type = event.get("gameType")
            self._roster(event)
        elif self.sequence >= 0 and event["seq"] != self.sequence + 1:
            # a reader that joined late or lost lines says so and carries on
            self.gaps += 1
        self.sequence = event["seq"]
        self.time = event["t"]

        if kind == "phase":
            self.phase = event.get("to")
        elif kind == "score":
            self.teams[event.get("team")] = event.get("score", 0)
        elif kind == "playerScore":
            self._player(event)["score"] = event.get("score", 0)
        elif kind == "player":
            if event.get("event") == "leave":
                self.players.pop(event.get("slot"), None)
            else:
                player = self._player(event)
                for key in ("team", "spectator", "score", "bot"):
                    if key in event:
                        player[key] = event[key]
        elif kind == "powerup":
            powerups = self._player(event).setdefault("powerups", [])
            name = event.get("powerup")
            if event.get("event") == "gain" and name not in powerups:
                powerups.append(name)
            elif event.get("event") == "end" and name in powerups:
                powerups.remove(name)
        elif kind == "flag":
            self.flags[event.get("flag")] = {
                "state": event.get("state"), "by": event.get("by"),
                "carrier": event.get("carrier"), "carrierName": event.get("carrierName")}
        elif kind == "kill":
            self.feed.append({key: event.get(key) for key in (
                "t", "killer", "killerName", "victim", "victimName", "weapon", "cause", "suicide", "teamKill", "quad")})
            del self.feed[:-FEED_LINES]
        elif kind == "standings":
            self._roster(event)
            self.standings = self.snapshot()["players"]
        return True

    def _roster(self, event) -> None:
        self.phase = event.get("phase")
        self.team_game = bool(event.get("teamGame"))
        self.teams = {row.get("team"): row.get("score", 0) for row in event.get("teams", []) if isinstance(row, dict)}
        known = self.players
        self.players = {}
        for row in event.get("players", []):
            if isinstance(row, dict) and isinstance(row.get("slot"), int):
                player = dict(row)
                # a roster line lists no powerups; keep what the events said
                if "powerups" in known.get(row["slot"], {}):
                    player["powerups"] = known[row["slot"]]["powerups"]
                self.players[row["slot"]] = player

    def _player(self, event) -> dict:
        slot = event.get("slot")
        player = self.players.setdefault(slot, {"slot": slot, "score": 0})
        if isinstance(event.get("name"), str):
            player["name"] = event["name"]
        return player

    def snapshot(self) -> dict:
        players = sorted(self.players.values(), key=lambda row: (bool(row.get("spectator")), -row.get("score", 0), row.get("slot", 0)))
        return {
            "schema": SCHEMA, "source": self.source, "map": self.map, "gameType": self.game_type,
            "phase": self.phase, "teamGame": self.team_game, "teams": self.teams, "players": players,
            "flags": self.flags, "feed": list(self.feed), "time": self.time, "sequence": self.sequence,
            "gaps": self.gaps, "streams": self.streams, "rejected": self.rejected,
        }


def feed_line(event: dict) -> str | None:
    """One readable line for an event worth a line in a feed, else None."""
    kind = event.get("type")
    clock = "%2d:%02d" % divmod(max(event.get("t", 0), 0) // 1000, 60)
    if kind == "stream":
        return f"{clock}  {event.get('gameType')} on {event.get('map')} ({event.get('source')})"
    if kind == "phase":
        return f"{clock}  {event.get('from')} -> {event.get('to')}"
    if kind == "kill":
        how = event.get("weapon") or event.get("cause")
        if event.get("suicide"):
            return f"{clock}  {event.get('victimName')} died ({how})"
        team = " [team kill]" if event.get("teamKill") else ""
        return f"{clock}  {event.get('killerName')} > {event.get('victimName')} ({how}){team}"
    if kind == "score":
        return f"{clock}  {event.get('team')} {event.get('score')}"
    if kind == "flag":
        who = event.get("carrierName") or event.get("wasCarrierName") or ""
        return f"{clock}  {event.get('flag')} flag {event.get('state')} {who}".rstrip()
    if kind == "player" and event.get("event") in ("join", "leave"):
        return f"{clock}  {event.get('name')} {'joined' if event['event'] == 'join' else 'left'}"
    return None


class Follower:
    """Reads complete lines from a file that grows, and starts over when it is replaced."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.offset = 0
        self.pending = b""

    def read(self) -> list[bytes]:
        try:
            size = self.path.stat().st_size
        except OSError:
            return []
        if size < self.offset:
            # the game truncates the file when a new map starts
            self.offset = 0
            self.pending = b""
        if size == self.offset:
            return []
        try:
            with self.path.open("rb") as handle:
                handle.seek(self.offset)
                chunk = handle.read(size - self.offset)
        except OSError:
            return []
        self.offset += len(chunk)
        lines = (self.pending + chunk).split(b"\n")
        self.pending = lines.pop()  # an event still being written
        return [line for line in lines if line.strip()]


def decode(line: bytes):
    try:
        return json.loads(line.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return None


PAGE = """<!doctype html>
<meta charset="utf-8">
<title>openQ4 match overlay</title>
<style>
  html, body { margin: 0; background: transparent; color: #f2f2f2;
    font: 600 20px/1.3 "Segoe UI", "Noto Sans", sans-serif; text-shadow: 0 1px 3px #000; }
  #top { display: flex; justify-content: center; gap: 18px; padding: 14px; font-size: 28px; }
  #top span { padding: 2px 14px; background: rgba(0, 0, 0, .55); border-radius: 4px; }
  .marine { color: #7fd0ff; } .strogg { color: #ffb070; } .dim { opacity: .65; font-weight: 400; }
  #feed { position: fixed; right: 16px; top: 80px; text-align: right; }
  #feed div, #board div { margin: 3px 0; padding: 1px 10px; background: rgba(0, 0, 0, .45); border-radius: 4px; }
  #board { position: fixed; left: 16px; top: 80px; }
</style>
<div id="top"></div><div id="board"></div><div id="feed"></div>
<script>
const text = value => value == null ? "" : String(value);
function line(parent, parts) {
  const row = document.createElement("div");
  for (const [value, style] of parts) {
    const span = document.createElement("span");
    span.textContent = text(value);
    if (style) span.className = style;
    row.append(span, " ");
  }
  parent.append(row);
}
async function refresh() {
  try {
    const state = await (await fetch("state.json", {cache: "no-store"})).json();
    const top = document.getElementById("top"); top.replaceChildren();
    const cell = (value, style) => { const span = document.createElement("span"); span.textContent = text(value); if (style) span.className = style; top.append(span); };
    if (state.teamGame) { cell("Marine " + text(state.teams.marine ?? 0), "marine"); cell(text(state.phase), "dim"); cell("Strogg " + text(state.teams.strogg ?? 0), "strogg"); }
    else { cell(text(state.gameType) + " - " + text(state.map)); cell(text(state.phase), "dim"); }
    const board = document.getElementById("board"); board.replaceChildren();
    for (const player of state.players.filter(row => !row.spectator).slice(0, 8))
      line(board, [[player.score, ""], [player.name, player.team], [(player.powerups || []).join(" "), "dim"]]);
    const feed = document.getElementById("feed"); feed.replaceChildren();
    for (const kill of state.feed)
      line(feed, kill.suicide ? [[kill.victimName, ""], [kill.weapon || kill.cause, "dim"]]
        : [[kill.killerName, ""], [kill.weapon || kill.cause, "dim"], [kill.victimName, ""]]);
  } catch (error) { /* the game is not running yet */ }
}
setInterval(refresh, 250); refresh();
</script>
"""


def serve(state: MatchState, lock: threading.Lock, host: str, port: int) -> ThreadingHTTPServer:
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:  # noqa: N802 (http.server API)
            if self.path.split("?")[0] == "/state.json":
                with lock:
                    body = json.dumps(state.snapshot()).encode("utf-8")
                kind = "application/json"
            elif self.path.split("?")[0] in ("/", "/index.html"):
                body, kind = PAGE.encode("utf-8"), "text/html; charset=utf-8"
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *_args) -> None:
            pass

    server = ThreadingHTTPServer((host, port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Follow an openQ4 live match event stream.")
    parser.add_argument("--file", type=Path, required=True, help="match-events/live.ndjson under the game's save path")
    parser.add_argument("--host", default="127.0.0.1", help="address the page is served on (default: loopback only)")
    parser.add_argument("--port", type=int, default=27980)
    parser.add_argument("--print", action="store_true", dest="feed", help="print a readable feed instead of serving a page")
    parser.add_argument("--once", action="store_true", help="read what the file holds, print the state as JSON and exit")
    parser.add_argument("--interval", type=float, default=0.1, help="seconds between reads of the file")
    args = parser.parse_args(argv)

    # player names are UTF-8 whatever the console's code page is
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    state = MatchState()
    lock = threading.Lock()
    follower = Follower(args.file)

    def pump() -> None:
        for raw in follower.read():
            event = decode(raw)
            with lock:
                accepted = state.apply(event)
            if accepted and args.feed:
                text = feed_line(event)
                if text is not None:
                    print(text, flush=True)

    if args.once:
        pump()
        print(json.dumps(state.snapshot(), indent=2, ensure_ascii=False))
        return 0 if state.streams > 0 else 1

    server = None
    if not args.feed:
        server = serve(state, lock, args.host, args.port)
        print(f"match overlay: http://{args.host}:{server.server_address[1]}/  following {args.file}", flush=True)
    try:
        while True:
            pump()
            time.sleep(args.interval)
    except KeyboardInterrupt:
        return 0
    finally:
        if server is not None:
            server.shutdown()


if __name__ == "__main__":
    sys.exit(main())
