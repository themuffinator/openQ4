#!/usr/bin/env python3
# Quake 4 SDK EULA applies to the game sources this test reads.
"""Exercise and pin server discovery (MP-19).

Compiles tools/tests/native/ServerListParseTest.cpp (the HTTPS server list
reader in src/framework/async/ServerListParse.h), runs
tools/tests/master_protocol.py (tools/master/openq4_master.py over loopback),
and pins, without assets, how the browser uses both: the HTTPS list is read on
a worker thread at each Internet refresh beside the master query and merges
into one list, only https:// addresses are read, a dedicated server builds
none of it, and the defaults are the user's decision of 23 November 2026: id's
master stays net_master0 and net_serverListUrl is empty until the project's
hosts are chosen. See docs/user/server-setup.md.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / ".tmp" / "mp-server-discovery-test"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_native() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    source = ROOT / "tools/tests/native/ServerListParseTest.cpp"
    exe = OUT / ("server_list_parse" + (".exe" if os.name == "nt" else ""))
    if os.name == "nt":
        command = OUT / "build.cmd"
        command.write_text('@echo off\ncall "' + str(ROOT / "tools/build/openq4_devcmd.cmd") + '"\n'
                           "if errorlevel 1 exit /b 1\n" + subprocess.list2cmdline([
                               "cl", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", str(source),
                               "/Fo:" + str(OUT / "server_list_parse.obj"), "/Fe:" + str(exe)]) + "\n", encoding="utf-8")
        subprocess.run(["cmd", "/d", "/c", str(command)], cwd=OUT, check=True)
    else:
        subprocess.run([os.environ.get("CXX", shutil.which("c++") or "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], cwd=OUT, check=True)
    subprocess.run([sys.executable, str(ROOT / "tools/tests/master_protocol.py")], cwd=ROOT, check=True)


def check_wiring() -> None:
    network = read("src/framework/async/AsyncNetwork.cpp")
    require(re.search(r'master0\( "net_master0", IDNET_HOST ":" IDNET_MASTER_PORT,', network) is not None,
            "id's master stays the default master (the user's decision of 23 November 2026)")
    fallback = read("src/framework/async/ServerListFallback.cpp")
    declaration = re.search(r'idCVar net_serverListUrl\( "net_serverListUrl", "([^"]*)", ([^,]+),', fallback)
    require(declaration is not None and declaration.group(1) == "" and "CVAR_ARCHIVE" in declaration.group(2),
            "net_serverListUrl is an archived setting that ships empty until the project's hosts are chosen")
    require(fallback.index("#ifndef ID_DEDICATED") < fallback.index("idCVar net_serverListUrl"),
            "a dedicated server, which has no browser and no HTTPS client, builds none of it")
    require('idStr::Icmpn( url, "https://", 8 ) != 0' in fallback, "only an https:// list is read")
    require("std::thread( RunServerListFetch, job ).detach();" in fallback, "the list is fetched on a worker thread")
    require("serverListParse::ParseServerList(" in fallback, "the reader the native test checks is the one the game uses")
    client = read("src/framework/async/AsyncClient.cpp")
    refresh = client[client.index("void idAsyncClient::GetNETServers( void )"):]
    refresh = refresh[:refresh.index("\n}\n")]
    require('serverListFallback.Start( cvarSystem->GetCVarString( "net_serverListUrl" ) );' in refresh,
            "an Internet refresh asks the HTTPS list as well as the master")
    require('cmdSystem->AddCommand( "NETScan", GetNETServers_f, CMD_FL_SYSTEM,' in network and
            "void idAsyncNetwork::GetNETServers_f( const idCmdArgs &args ) {\n\tclient.GetNETServers();\n}" in network,
            "NETScan is the browser's Internet search from the console (mp_master_smoke.py drives it)")
    frame = client[client.index("void idAsyncClient::RunFrame( bool allowBlocking )"):]
    require("serverListFallback.Poll( listed, listUrl, listError )" in frame and "serverList.AddServer( serverList.Num(), listed[ i ].c_str() );" in frame,
            "the list's servers join the browser's, which lists an address once")
    scan = read("src/framework/async/ServerScan.cpp")
    add = scan[scan.index("void idServerScan::AddServer( int id, const char *srv )"):]
    require("Sys_CompareNetAdrBase( net_servers[ i ].adr, s.adr ) && net_servers[ i ].adr.port == s.adr.port" in add[:1500],
            "a server both sources name is listed once")
    master = read("tools/master/openq4_master.py")
    require('message("getInfo"' in master and "challenge != server.challenge" in master,
            "the master lists a server only after it answers the master's challenge")


def main() -> int:
    run_native()
    check_wiring()
    print("mp_server_discovery: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
