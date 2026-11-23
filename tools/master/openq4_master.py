#!/usr/bin/env python3
"""openQ4 master server: lists the openQ4 servers that announce themselves (roadmap MP-19).

Speaks the Quake 4 master protocol the engine already uses (net_master0..4):

* a server sends ``heartbeat`` from its game port at map start and every five
  minutes; the master answers with ``getInfo`` and a random challenge, and
  lists the server only once its ``infoResponse`` echoes the challenge, so a
  forged heartbeat cannot list somebody else's address;
* a client sends ``getServers`` (protocol and game) and gets ``servers``: IPv4
  records, four address bytes and a little-endian port; ``getServersExt`` gets
  ``serversExt``: dpmaster-style records, ``\\`` and four bytes or ``/`` and
  sixteen, each with a big-endian port, ended by ``EOT``. Only servers of the
  same protocol and game are listed.

With ``--json`` the master also writes the list it holds to a file, for a web
server to publish over HTTPS as the browser's fallback when no master
answers (the engine's ``net_serverListUrl``). See docs/user/server-setup.md.

Standard library only; run it on any machine with Python 3.10 or later:

    python openq4_master.py --port 27650 --json /var/www/openq4/servers.json
"""
from __future__ import annotations

import argparse
import asyncio
import ipaddress
import json
import os
import secrets
import socket
import struct
import sys
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path

CONNECTIONLESS = b"\xff\xff"
MAX_REPLY_BYTES = 1100				# a server list datagram stays under any path's fragment size
HEARTBEAT_SECONDS = 300				# the engine's HEARTBEAT_MSEC
CHALLENGE_SECONDS = 10				# a getInfo answer later than this is ignored


def read_string(data: bytes, at: int) -> tuple[str, int]:
    end = data.index(b"\0", at)
    return data[at:end].decode("latin-1"), end + 1


def message(command: str, payload: bytes = b"") -> bytes:
    return CONNECTIONLESS + command.encode("ascii") + b"\0" + payload


def parse_info_response(data: bytes) -> tuple[int, int, dict[str, str], int] | None:
    """challenge, protocol, server info and player count from an infoResponse body (after the command)."""
    try:
        challenge, protocol = struct.unpack_from("<iI", data, 0)
        at = 8
        info: dict[str, str] = {}
        while True:
            key, at = read_string(data, at)
            if key == "":
                break
            value, at = read_string(data, at)
            info[key] = value
        # a delta dict without a base ends with an empty list of removed keys
        if at < len(data) and data[at] == 0:
            at += 1
        players = 0
        while at < len(data):
            slot = data[at]
            at += 1
            if slot >= 32:
                break
            at += 2 + 4
            _name, at = read_string(data, at)
            players += 1
        return challenge, protocol, info, players
    except (ValueError, struct.error, IndexError):
        return None


@dataclass
class Server:
    host: str
    port: int
    protocol: int = 0
    game: str = ""
    info: dict[str, str] = field(default_factory=dict)
    players: int = 0
    seen: float = 0.0
    verified: bool = False
    challenge: int = 0
    challenged: float = 0.0


class Master:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.servers: dict[tuple[str, int], Server] = {}
        self.buckets: dict[str, tuple[float, float]] = {}
        self.transports: list[asyncio.DatagramTransport] = []

    def log(self, text: str) -> None:
        print(time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), text, flush=True)

    def allowed(self, host: str) -> bool:
        try:
            address = ipaddress.ip_address(host.split("%")[0])
        except ValueError:
            return False
        if isinstance(address, ipaddress.IPv6Address) and address.ipv4_mapped is not None:
            address = address.ipv4_mapped
        if address.is_unspecified or address.is_multicast:
            return False
        return self.args.allow_private or address.is_global

    def rate(self, host: str, cost: float) -> bool:
        """A token bucket for each source address: --rate tokens a second, a burst of four seconds' worth."""
        now = time.monotonic()
        tokens, stamp = self.buckets.get(host, (self.args.rate * 4, now))
        tokens = min(self.args.rate * 4, tokens + (now - stamp) * self.args.rate)
        if tokens < cost:
            self.buckets[host] = (tokens, now)
            return False
        self.buckets[host] = (tokens - cost, now)
        if len(self.buckets) > 100000:
            self.buckets.clear()
        return True

    def heartbeat(self, transport: asyncio.DatagramTransport, host: str, port: int) -> None:
        key = (host, port)
        server = self.servers.get(key)
        if server is None:
            if sum(1 for other in self.servers.values() if other.host == host) >= self.args.max_per_address or \
                    len(self.servers) >= self.args.max_servers:
                return
            server = self.servers[key] = Server(host, port)
        server.challenge = secrets.randbits(31)
        server.challenged = time.monotonic()
        transport.sendto(message("getInfo", struct.pack("<i", server.challenge)), (host, port))

    def info_response(self, host: str, port: int, body: bytes) -> None:
        server = self.servers.get((host, port))
        parsed = parse_info_response(body)
        if server is None or parsed is None or server.challenge == 0:
            return
        challenge, protocol, info, players = parsed
        if challenge != server.challenge or time.monotonic() - server.challenged > CHALLENGE_SECONDS:
            return
        first = not server.verified
        server.challenge = 0
        server.verified = True
        server.protocol = protocol
        server.game = info.get("fs_game", "")
        server.info = {key: info[key] for key in ("si_name", "si_map", "si_gameType", "si_maxPlayers", "si_pure") if key in info}
        server.players = players
        server.seen = time.time()
        if first:
            self.log(f"listed {host}:{port} protocol {protocol >> 16}.{protocol & 0xffff} game '{server.game}' "
                     f"name '{server.info.get('si_name', '')}'")

    def listed(self, protocol: int, game: str) -> list[Server]:
        cutoff = time.time() - self.args.expire
        return [server for server in self.servers.values()
                if server.verified and server.seen >= cutoff and server.protocol == protocol
                and server.game.lower() == game.lower()]

    def get_servers(self, transport: asyncio.DatagramTransport, source: tuple, body: bytes, extended: bool) -> None:
        try:
            protocol = struct.unpack_from("<I", body, 0)[0]
            game, _ = read_string(body, 4)
        except (struct.error, ValueError):
            return
        servers = self.listed(protocol, game)
        if extended:
            records = []
            for server in servers:
                address = ipaddress.ip_address(server.host.split("%")[0])
                if isinstance(address, ipaddress.IPv6Address) and address.ipv4_mapped is not None:
                    address = address.ipv4_mapped
                if isinstance(address, ipaddress.IPv4Address):
                    records.append(b"\\" + address.packed + struct.pack(">H", server.port))
                else:
                    records.append(b"/" + address.packed + struct.pack(">H", server.port))
            head = message("serversExt")
            packet = head
            for record in records:
                if len(packet) + len(record) + 6 > MAX_REPLY_BYTES:
                    transport.sendto(packet + b"EOT\0\0\0", source)
                    packet = head
                packet += record
            transport.sendto(packet + b"EOT\0\0\0", source)
        else:
            head = message("servers")
            packet = head
            for server in servers:
                address = ipaddress.ip_address(server.host.split("%")[0])
                if isinstance(address, ipaddress.IPv6Address):
                    if address.ipv4_mapped is None:
                        continue	# the legacy list cannot carry IPv6
                    address = address.ipv4_mapped
                record = address.packed + struct.pack("<H", server.port)
                if len(packet) + len(record) > MAX_REPLY_BYTES:
                    transport.sendto(packet, source)
                    packet = head
                packet += record
            transport.sendto(packet, source)

    def datagram(self, transport: asyncio.DatagramTransport, data: bytes, source: tuple) -> None:
        host, port = source[0], source[1]
        if not data.startswith(CONNECTIONLESS) or not self.allowed(host):
            return
        try:
            command, at = read_string(data, 2)
        except ValueError:
            return
        body = data[at:]
        if command == "heartbeat":
            if self.rate(host, 1.0):
                self.heartbeat(transport, host, port)
        elif command == "infoResponse":
            self.info_response(host, port, body)
        elif command in ("getServers", "getServersExt"):
            if self.rate(host, 0.5):
                self.get_servers(transport, source, body, command == "getServersExt")

    def prune(self) -> None:
        cutoff = time.time() - self.args.expire
        for key, server in list(self.servers.items()):
            if (server.verified and server.seen < cutoff) or (not server.verified and time.monotonic() - server.challenged > CHALLENGE_SECONDS):
                del self.servers[key]

    def write_json(self) -> None:
        if not self.args.json:
            return
        cutoff = time.time() - self.args.expire
        entries = []
        for server in self.servers.values():
            if not server.verified or server.seen < cutoff:
                continue
            host = f"[{server.host}]" if ":" in server.host else server.host
            entries.append({"address": f"{host}:{server.port}", "protocol": server.protocol, "game": server.game,
                            "name": server.info.get("si_name", ""), "map": server.info.get("si_map", ""),
                            "gametype": server.info.get("si_gameType", ""), "players": server.players,
                            "maxPlayers": int(server.info.get("si_maxPlayers", "0") or 0), "seen": int(server.seen)})
        entries.sort(key=lambda entry: entry["address"])
        document = {"format": "openq4-server-list", "version": 1,
                    "generated": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "servers": entries}
        target = Path(self.args.json)
        target.parent.mkdir(parents=True, exist_ok=True)
        handle, staged = tempfile.mkstemp(prefix=target.name + ".", dir=target.parent)
        with os.fdopen(handle, "w", encoding="utf-8") as staged_file:
            json.dump(document, staged_file, indent=1)
        os.replace(staged, target)


class Protocol(asyncio.DatagramProtocol):
    def __init__(self, master: Master):
        self.master = master
        self.transport: asyncio.DatagramTransport | None = None

    def connection_made(self, transport):
        self.transport = transport
        self.master.transports.append(transport)

    def datagram_received(self, data, addr):
        self.master.datagram(self.transport, data, addr)


async def serve(args: argparse.Namespace) -> None:
    master = Master(args)
    loop = asyncio.get_running_loop()
    for host in args.bind:
        family = socket.AF_INET6 if ":" in host else socket.AF_INET
        sock = socket.socket(family, socket.SOCK_DGRAM)
        if family == socket.AF_INET6:
            # IPv4 has its own socket; Linux would otherwise map it onto this one and refuse the second bind
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        sock.bind((host, args.port))
        await loop.create_datagram_endpoint(lambda: Protocol(master), sock=sock)
        master.log(f"listening on {host} port {args.port}")
    if args.ready_file:
        Path(args.ready_file).write_text("ready\n", encoding="utf-8")
    while True:
        await asyncio.sleep(args.json_interval)
        master.prune()
        master.write_json()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=27650)
    parser.add_argument("--bind", action="append", help="an address to listen on (default: 0.0.0.0 and ::)")
    parser.add_argument("--json", help="write the list here for the HTTPS fallback")
    parser.add_argument("--json-interval", type=float, default=60.0, help="seconds between list writes")
    parser.add_argument("--expire", type=float, default=HEARTBEAT_SECONDS * 3, help="seconds a server stays listed without a heartbeat")
    parser.add_argument("--rate", type=float, default=4.0, help="requests a second from one address (bursts of four seconds)")
    parser.add_argument("--max-servers", type=int, default=4096)
    parser.add_argument("--max-per-address", type=int, default=16, help="servers listed for one address")
    parser.add_argument("--allow-private", action="store_true", help="list servers on private and loopback addresses (LAN and tests)")
    parser.add_argument("--ready-file", help="write this file once listening (for tests)")
    args = parser.parse_args()
    args.bind = args.bind or ["0.0.0.0", "::"]
    try:
        asyncio.run(serve(args))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
