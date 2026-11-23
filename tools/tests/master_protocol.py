#!/usr/bin/env python3
"""Exercise tools/master/openq4_master.py over loopback (MP-19), without the game.

Starts the master on a free loopback port and plays both sides of the Quake 4
master protocol against it with the byte layouts the engine writes and reads
(src/framework/async/AsyncServer.cpp MasterHeartbeat and ProcessGetInfoMessage,
AsyncClient.cpp GetNETServers and ProcessServersList(Ext)Message):

* a server's heartbeat is answered by getInfo, and the server is listed only
  after its infoResponse echoes the challenge;
* a server that answers with the wrong challenge, or never answers, is not
  listed;
* getServers lists matching servers as six-byte little-endian-port records,
  getServersExt as dpmaster records ended by EOT; another protocol or game
  sees none;
* --json writes the list, with each server's name, map and player count.
"""
from __future__ import annotations

import json
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MASTER = ROOT / "tools/master/openq4_master.py"
PROTOCOL = (2 << 16) + 42
GAME = "baseoq4"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def oob(command: str, payload: bytes = b"") -> bytes:
    return b"\xff\xff" + command.encode() + b"\0" + payload


def info_response(challenge: int, info: dict[str, str], players: list[str]) -> bytes:
    body = struct.pack("<iI", challenge, PROTOCOL)
    for key, value in info.items():
        body += key.encode() + b"\0" + value.encode() + b"\0"
    body += b"\0\0"						# WriteDeltaDict( dict, NULL ): the pairs' end, then no removed keys
    for slot, name in enumerate(players):
        body += bytes([slot]) + struct.pack("<hi", 40, 25000) + name.encode() + b"\0"
    body += bytes([32]) + struct.pack("<i", 1)
    return oob("infoResponse", body)


def get_servers(command: str, protocol: int = PROTOCOL, game: str = GAME) -> bytes:
    return oob(command, struct.pack("<I", protocol) + game.encode() + b"\0" + b"\0")


def main() -> int:
    port = free_port()
    with tempfile.TemporaryDirectory() as scratch:
        listing, ready = Path(scratch) / "servers.json", Path(scratch) / "ready"
        master = subprocess.Popen([sys.executable, str(MASTER), "--port", str(port), "--bind", "127.0.0.1", "--allow-private",
                                   "--json", str(listing), "--json-interval", "0.5", "--ready-file", str(ready)],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            deadline = time.monotonic() + 15
            while not ready.exists():
                require(time.monotonic() < deadline and master.poll() is None, "the master did not start")
                time.sleep(0.05)
            address = ("127.0.0.1", port)

            def server(name: str, answer: str, info_game: str = GAME) -> socket.socket:
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.bind(("127.0.0.1", 0))
                sock.settimeout(3)
                sock.sendto(oob("heartbeat"), address)
                data, source = sock.recvfrom(2048)
                require(source == address and data.startswith(oob("getInfo")) and len(data) == len(oob("getInfo")) + 4,
                        f"{name}: a heartbeat is answered by getInfo and a challenge")
                challenge = struct.unpack_from("<i", data, len(oob("getInfo")))[0]
                if answer == "right":
                    sock.sendto(info_response(challenge, {"si_name": name, "si_map": "mp/q4dm1", "si_maxPlayers": "8",
                                                          "fs_game": info_game}, ["A", "B"]), address)
                elif answer == "wrong":
                    sock.sendto(info_response(challenge ^ 1, {"si_name": name, "fs_game": info_game}, []), address)
                return sock

            good = server("Good", "right")
            forged = server("Forged", "wrong")
            silent = server("Silent", "none")
            other_game = server("Mod", "right", info_game="mymod")
            time.sleep(0.3)

            client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            client.bind(("127.0.0.1", 0))
            client.settimeout(3)
            client.sendto(get_servers("getServers"), address)
            reply, _ = client.recvfrom(2048)
            head = oob("servers")
            require(reply.startswith(head), "getServers is answered by servers")
            records = reply[len(head):]
            require(len(records) == 6, f"one server is listed, not the forged, silent or other-game ones: {records!r}")
            listed_port = struct.unpack_from("<H", records, 4)[0]
            require(records[:4] == bytes([127, 0, 0, 1]) and listed_port == good.getsockname()[1],
                    "the record is the server's address and its little-endian port")

            client.sendto(get_servers("getServersExt"), address)
            reply, _ = client.recvfrom(2048)
            head = oob("serversExt")
            require(reply.startswith(head) and reply.endswith(b"EOT\0\0\0"), "getServersExt is answered by serversExt, ended by EOT")
            body = reply[len(head):-6]
            require(body == b"\\" + bytes([127, 0, 0, 1]) + struct.pack(">H", good.getsockname()[1]),
                    f"an extended record is a backslash, four bytes and a big-endian port: {body!r}")

            client.sendto(get_servers("getServers", protocol=PROTOCOL - 1), address)
            reply, _ = client.recvfrom(2048)
            require(reply == oob("servers"), "another protocol sees no servers")
            client.sendto(get_servers("getServers", game="mymod"), address)
            reply, _ = client.recvfrom(2048)
            require(len(reply) == len(oob("servers")) + 6, "a mod's players see the mod's server")

            deadline = time.monotonic() + 5
            document = {}
            while time.monotonic() < deadline:
                if listing.exists():
                    document = json.loads(listing.read_text(encoding="utf-8"))
                    if len(document.get("servers", [])) == 2:
                        break
                time.sleep(0.1)
            names = sorted(entry["name"] for entry in document.get("servers", []))
            require(document.get("format") == "openq4-server-list" and names == ["Good", "Mod"],
                    f"the JSON list holds the two verified servers: {document}")
            entry = next(entry for entry in document["servers"] if entry["name"] == "Good")
            require(entry["address"] == f"127.0.0.1:{good.getsockname()[1]}" and entry["players"] == 2 and entry["maxPlayers"] == 8
                    and entry["map"] == "mp/q4dm1" and entry["game"] == GAME and entry["protocol"] == PROTOCOL, f"the entry's fields: {entry}")
            for sock in (good, forged, silent, other_game, client):
                sock.close()
        finally:
            master.terminate()
            try:
                master.wait(timeout=5)
            except subprocess.TimeoutExpired:
                master.kill()
    print("master_protocol: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
