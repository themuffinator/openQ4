#!/usr/bin/env python3
"""Compile the production rcon password check and drive both of its ends.

Quake 4's in-match Admin page sends "rcon verifyRconPass" to check the password
a player typed, then waits for the game to hear the answer
(idGame::ProcessRconReturn). The server answers the check with the stock
"rcon verified" string instead of running it. The client hands that answer, or
the bad-password reply, to the game when it comes from the server asked, and
answers false itself when the check is refused before sending or times out; it
also hands the server's remote console output to the game, as Quake 4 did.
--mutations reverts each rule and requires the harness to catch it.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from async_drop_client_contract import function_body

ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#define ID_INLINE inline
using byte = unsigned char;
constexpr int MAX_STRING_CHARS = 1024, MAX_MESSAGE_SIZE = 256, CONNECTIONLESS_MESSAGE_ID = 0xffff, PORT_SERVER = 28004;
enum { SERVER_PRINT_MISC = 0, SERVER_PRINT_BADPROTOCOL, SERVER_PRINT_RCON, SERVER_PRINT_GAMEDENY, SERVER_PRINT_BADCHALLENGE };
enum { ALLOW_YES = 0, ALLOW_BADPASS, ALLOW_NOTYET, ALLOW_NO };
enum { CS_DISCONNECTED, CS_PURERESTART, CS_CHALLENGING, CS_CONNECTING, CS_CONNECTED, CS_INGAME };
enum { MSG_OK, MSG_PROMPT };
enum { CMD_EXEC_NOW, CMD_EXEC_APPEND };
struct netadr_t { int ip; unsigned short port; };
bool Sys_CompareNetAdrBase( const netadr_t &a, const netadr_t &b ) { return a.ip == b.ip; }
const char *Sys_NetAdrToString( const netadr_t & ) { return "endpoint"; }
bool Sys_StringToNetAdr( const char *s, netadr_t *a, bool ) {
	if ( s == nullptr || s[0] == '\0' ) { return false; }
	a->ip = 9; a->port = 0; return true;
}
bool secureRandomWorks = true;
bool Sys_GetSecureRandomBytes( void *p, std::size_t n ) { memset( p, 7, n ); return secureRandomWorks; }
struct idStr {
	static int Icmp( const char *a, const char *b ) {
		for ( ;; ++a, ++b ) {
			const int ca = tolower( static_cast<unsigned char>( *a ) ), cb = tolower( static_cast<unsigned char>( *b ) );
			if ( ca != cb ) { return ca - cb; }
			if ( ca == 0 ) { return 0; }
		}
	}
	static int Cmp( const char *a, const char *b ) { return strcmp( a, b ); }
	static void Copynz( char *d, const char *s, int n ) {
		int i = 0;
		for ( ; i < n - 1 && s[ i ] != '\0'; ++i ) { d[ i ] = s[ i ]; }
		d[ i ] = '\0';
	}
};
struct idCrypto { static void SecureZero( void *p, std::size_t n ) { memset( p, 0, n ); } };
namespace idRcon2 {
	constexpr std::size_t MIN_PASSWORD_BYTES = @MIN_PASSWORD_BYTES@;
	void HashRequest( const char *, byte * ) {}
}
struct idBitMsg {
	std::vector<int> longs;
	std::vector<std::string> strings;
	mutable std::size_t nextLong = 0, nextString = 0;
	void Init( byte *, int ) {}
	void WriteShort( int ) {}
	void WriteString( const char *s ) { strings.push_back( s ); }
	const byte *GetData() const { return nullptr; }
	int GetSize() const { return 0; }
	int ReadLong() const { return longs[ nextLong++ ]; }
	void ReadString( char *out, int n ) const { idStr::Copynz( out, strings[ nextString++ ].c_str(), n ); }
};
struct CVarString { std::string value; const char *GetString() const { return value.c_str(); } void SetString( const char *s ) { value = s; } };
struct CVarBool { bool value; bool GetBool() const { return value; } };
struct idAsyncNetwork {
	static CVarString clientRemoteConsoleAddress, clientRemoteConsolePassword;
	// MP-18: where the rcon command now running came from, for the game module's admin log
	static CVarString serverRconFrom;
	static CVarBool clientUseLegacyRcon;
};
CVarString idAsyncNetwork::serverRconFrom{ "" };
CVarString idAsyncNetwork::clientRemoteConsoleAddress{ "localhost" };
CVarString idAsyncNetwork::clientRemoteConsolePassword{ "" };
CVarBool idAsyncNetwork::clientUseLegacyRcon{ false };
struct LangDict {
	const char *GetString( const char *s ) const {
		if ( strcmp( s, "#str_107250" ) == 0 ) { return "rcon verified\n"; }
		if ( strcmp( s, "#str_04847" ) == 0 || strcmp( s, "#str_104847" ) == 0 ) { return "Bad rcon password.\n"; }
		if ( strcmp( s, "#str_04848" ) == 0 ) { return "rcon command successful.\n"; }
		return s;
	}
} langDict;
struct Common {
	void Printf( const char *, ... ) {}
	void Warning( const char *, ... ) {}
	const LangDict *GetLanguageDict() { return &langDict; }
	void BeginRedirect( char *, int, void ( * )( const char * ) ) {}
	void EndRedirect() {}
} commonValue, *common = &commonValue;
std::vector<std::string> commands;
std::vector<std::string> commandSources;	// net_serverRconFrom as each command ran
struct CmdSystem { void BufferCommandText( int, const char *text ) { commands.push_back( text ); commandSources.push_back( idAsyncNetwork::serverRconFrom.value ); } } cmdValue, *cmdSystem = &cmdValue;
struct CVarSystem { void SetCVarString( const char *, const char * ) {} } cvarValue, *cvarSystem = &cvarValue;
struct Session {
	const char *MessageBox( int, const char *, const char *, bool, const char * = nullptr ) { return nullptr; }
} sessionValue, *session = &sessionValue;
struct Game {
	std::vector<int> answers;
	std::vector<std::string> output;
	void ProcessRconReturn( bool success ) { answers.push_back( success ? 1 : 0 ); }
	void ReceiveRemoteConsoleOutput( const char *text ) { output.push_back( text ); }
} gameValue, *game = &gameValue;
struct Gui { std::string status; void SetStateString( const char *, const char *value ) { status = value; } } guiValue;
@RCON_TYPES@
@RCON_CONSTANTS@
@HELPERS@
struct idAsyncClient {
	struct Port {
		int sent = 0;
		void SendPacket( const netadr_t &, const byte *, int ) { ++sent; }
	} clientPort;
	bool active = true, portWorks = true;
	int realTime = 0, clientState = CS_INGAME, challenges = 0;
	netadr_t serverAddress{}, lastRconAddress{};
	int lastRconTime = 0;
	rcon2ClientRequest_t rcon2Request{};
	bool rconVerifyPending = false, rconVerifyRefused = false;
	netadr_t rconVerifyAddress{};
	int rconVerifyTime = 0;
	Gui *guiNetMenu = &guiValue;
	bool InitPort() { return portWorks; }
	void ClearRemoteConsoleRequest() {
		memset( &rcon2Request, 0, sizeof( rcon2Request ) );
		rcon2Request.state = RCON_REPLY_NONE;
		memset( &lastRconAddress, 0, sizeof( lastRconAddress ) );
		lastRconTime = 0;
	}
	void SendRemoteConsole2Challenge() { ++challenges; }
	void SendRemoteConsole2Proof() {}
	void ClearPendingPackets() {}
	void RemoteConsole( const char *command );
	void UpdateRemoteConsoleRequest( void );
	void AnswerRconVerify( bool success );
	void ProcessPrintMessage( const netadr_t from, const idBitMsg &msg );
};
void RConRedirect( const char * ) {}
struct idAsyncServer {
	struct Oob { netadr_t to; int opcode; std::string text; };
	std::vector<Oob> oob;
	netadr_t rconAddress{};
	bool noRconOutput = false;
	bool rconCommandExecuted = false;
	void PrintOOB( const netadr_t to, int opcode, const char *text ) { oob.push_back( Oob{ to, opcode, text } ); }
	void ExecuteRemoteConsoleCommand( const netadr_t from, const char *command, bool authenticated );
};
'''

MAIN = r'''
#define CHECK( condition, name ) do { if ( !( condition ) ) { fprintf( stderr, "FAIL %s\n", name ); return 1; } } while ( 0 )
static bool Same( const netadr_t &a, const netadr_t &b ) { return a.ip == b.ip && a.port == b.port; }
static idBitMsg Print( int opcode, const char *text, int gameOpcode = ALLOW_YES ) {
	idBitMsg msg;
	msg.longs.push_back( opcode );
	if ( opcode == SERVER_PRINT_GAMEDENY ) { msg.longs.push_back( gameOpcode ); }
	msg.strings.push_back( text );
	return msg;
}
static const char *const PASSWORD = "correct horse battery staple";
struct Fixture {
	idAsyncClient client;
	netadr_t server{ 1, 28004 };
	explicit Fixture( bool legacy = false, const char *password = PASSWORD ) {
		gameValue.answers.clear(); gameValue.output.clear(); guiValue.status.clear(); commands.clear();
		idAsyncNetwork::clientUseLegacyRcon.value = legacy;
		idAsyncNetwork::clientRemoteConsolePassword.value = password;
		idAsyncNetwork::clientRemoteConsoleAddress.value = "localhost";
		secureRandomWorks = true;
		client.serverAddress = server;
		client.realTime = 5000;
		client.ClearRemoteConsoleRequest();
	}
};

int main() {
	{	// the server answers the check and runs nothing, whatever its case
		for ( const char *check : { "verifyRconPass", "VERIFYRCONPASS" } ) {
			idAsyncServer server;
			const netadr_t from{ 5, 27960 };
			commands.clear();
			server.ExecuteRemoteConsoleCommand( from, check, true );
			CHECK( server.oob.size() == 1 && Same( server.oob[0].to, from ) && server.oob[0].opcode == SERVER_PRINT_RCON &&
				server.oob[0].text == "#str_107250", "server-answers-check" );
			CHECK( commands.empty(), "server-runs-nothing-for-check" );
		}
		idAsyncServer server;
		commands.clear();
		server.ExecuteRemoteConsoleCommand( netadr_t{ 5, 27960 }, "status", false );
		CHECK( commands.size() == 1 && commands[0] == "status", "server-runs-commands" );
		// the command ran with its source published, and the source is withdrawn afterwards
		CHECK( commandSources.size() >= 1 && commandSources.back() == "endpoint" && idAsyncNetwork::serverRconFrom.value.empty(),
			"server-publishes-rcon-source" );
		CHECK( server.oob.size() == 1 && server.oob[0].text == "#str_04848", "server-quiet-command-reply" );
	}
	{	// verified: answered once, as true, and shown as console output rather than status
		Fixture f;
		f.client.RemoteConsole( "verifyRconPass" );
		CHECK( f.client.rconVerifyPending && !f.client.rconVerifyRefused && Same( f.client.rconVerifyAddress, f.server ),
			"client-check-pending" );
		CHECK( f.client.challenges == 1, "client-check-sent" );
		f.client.realTime += 100;
		f.client.UpdateRemoteConsoleRequest();
		CHECK( gameValue.answers.empty(), "client-waits-for-answer" );
		f.client.ProcessPrintMessage( f.server, Print( SERVER_PRINT_RCON, "#str_107250" ) );
		CHECK( gameValue.answers == std::vector<int>{ 1 }, "client-verified-answers-true" );
		CHECK( gameValue.output.size() == 1 && gameValue.output[0] == "rcon verified\n", "client-forwards-rcon-output" );
		CHECK( guiValue.status.empty(), "client-answer-not-status" );
		f.client.ProcessPrintMessage( f.server, Print( SERVER_PRINT_RCON, "#str_107250" ) );
		f.client.realTime += 2 * RCON2_CLIENT_TIMEOUT_MSEC;
		f.client.UpdateRemoteConsoleRequest();
		CHECK( gameValue.answers.size() == 1, "client-answers-once" );
		CHECK( guiValue.status == "rcon verified\n", "client-unasked-print-is-status" );
	}
	for ( const char *reply : { "#str_04847", "#str_104847" } ) {	// openQ4's bad password id, then retail's
		Fixture f;
		f.client.RemoteConsole( "verifyRconPass" );
		f.client.ProcessPrintMessage( f.server, Print( SERVER_PRINT_MISC, reply ) );
		CHECK( gameValue.answers == std::vector<int>{ 0 },
			strcmp( reply, "#str_04847" ) == 0 ? "client-bad-password-openq4" : "client-bad-password-retail" );
		CHECK( gameValue.output.empty(), "client-misc-not-console-output" );
	}
	{	// only the server asked may answer; silence times out as false, once
		Fixture f;
		f.client.RemoteConsole( "verifyRconPass" );
		f.client.ProcessPrintMessage( netadr_t{ 2, 28004 }, Print( SERVER_PRINT_RCON, "#str_107250" ) );
		f.client.ProcessPrintMessage( netadr_t{ 1, 28005 }, Print( SERVER_PRINT_RCON, "#str_107250" ) );
		CHECK( gameValue.answers.empty() && f.client.rconVerifyPending, "client-other-endpoint-ignored" );
		f.client.ProcessPrintMessage( f.server, Print( SERVER_PRINT_RCON, "Unknown command 'verifyRconPass'" ) );
		CHECK( gameValue.answers.empty() && f.client.rconVerifyPending, "client-older-server-waits" );
		f.client.realTime += RCON2_CLIENT_TIMEOUT_MSEC;
		f.client.UpdateRemoteConsoleRequest();
		CHECK( gameValue.answers.empty(), "client-waits-until-timeout" );
		f.client.realTime += 1;
		f.client.UpdateRemoteConsoleRequest();
		CHECK( gameValue.answers == std::vector<int>{ 0 }, "client-timeout-answers-false" );
		f.client.realTime += 1;
		f.client.UpdateRemoteConsoleRequest();
		CHECK( gameValue.answers.size() == 1, "client-timeout-answers-once" );
	}
	struct Refusal { const char *name; bool legacy; const char *password; bool port; bool random; bool active; const char *address; };
	for ( const Refusal &r : {
			Refusal{ "client-refused-no-password", false, "", true, true, true, "localhost" },
			Refusal{ "client-refused-short-password", false, "short", true, true, true, "localhost" },
			Refusal{ "client-refused-no-port", false, PASSWORD, false, true, true, "localhost" },
			Refusal{ "client-refused-no-random", false, PASSWORD, true, false, true, "localhost" },
			Refusal{ "client-refused-no-address", false, PASSWORD, true, true, false, "" },
			Refusal{ "client-refused-legacy-no-password", true, "", true, true, true, "localhost" } } ) {
		Fixture f( r.legacy, r.password );
		f.client.portWorks = r.port;
		f.client.active = r.active;
		secureRandomWorks = r.random;
		idAsyncNetwork::clientRemoteConsoleAddress.value = r.address;
		f.client.RemoteConsole( "verifyRconPass" );
		CHECK( f.client.challenges == 0 && f.client.clientPort.sent == 0 && f.client.rconVerifyPending, r.name );
		CHECK( gameValue.answers.empty(), "client-refusal-answered-next-frame" );
		f.client.UpdateRemoteConsoleRequest();
		CHECK( gameValue.answers == std::vector<int>{ 0 }, "client-refused-answers-false" );
	}
	{	// the legacy plaintext request is checked the same way
		Fixture f( true, "pw" );
		f.client.RemoteConsole( "verifyRconPass" );
		CHECK( f.client.clientPort.sent == 1 && f.client.rconVerifyPending && !f.client.rconVerifyRefused, "client-legacy-check-sent" );
		f.client.ProcessPrintMessage( f.server, Print( SERVER_PRINT_MISC, "#str_04847" ) );
		CHECK( gameValue.answers == std::vector<int>{ 0 }, "client-legacy-bad-password" );
	}
	{	// other commands are not checks, and their replies answer nothing
		Fixture f;
		f.client.RemoteConsole( "status" );
		CHECK( !f.client.rconVerifyPending, "client-command-not-check" );
		f.client.ProcessPrintMessage( f.server, Print( SERVER_PRINT_RCON, "#str_107250" ) );
		f.client.UpdateRemoteConsoleRequest();
		CHECK( gameValue.answers.empty(), "client-unasked-never-answered" );
		CHECK( gameValue.output.size() == 1, "client-command-output-forwarded" );
	}
	{	// a connection refusal never answers the check
		Fixture f;
		f.client.RemoteConsole( "verifyRconPass" );
		f.client.ProcessPrintMessage( f.server, Print( SERVER_PRINT_GAMEDENY, "#str_107250", ALLOW_YES ) );
		CHECK( gameValue.answers.empty() && f.client.rconVerifyPending, "client-gamedeny-ignored" );
	}
	{
		Fixture f;
		f.client.RemoteConsole( "VerifyRconPass" );
		CHECK( f.client.rconVerifyPending && f.client.challenges == 1, "client-check-any-case" );
	}
	printf( "async rcon verify: PASS\n" );
	return 0;
}
'''

# name: (production text, replacement, the check that must catch it)
MUTATIONS = {
    "server-runs-check": ('if ( idStr::Icmp( command, "verifyRconPass" ) == 0 ) {', "if ( false ) {", "server-answers-check"),
    "server-misc-answer": ('PrintOOB( from, SERVER_PRINT_RCON, "#str_107250" );', 'PrintOOB( from, SERVER_PRINT_MISC, "#str_107250" );',
                           "server-answers-check"),
    "client-any-endpoint": ("AsyncClient_SameEndpoint( from, rconVerifyAddress )", "true", "client-other-endpoint-ignored"),
    "client-no-timeout": ("rconVerifyRefused ||\n\t\tAsyncClient_Elapsed( realTime, rconVerifyTime ) > RCON2_CLIENT_TIMEOUT_MSEC )",
                          "rconVerifyRefused )", "client-timeout-answers-false"),
    "client-refusal-unanswered": ("\t\trconVerifyRefused = true;", "\t\trconVerifyRefused = false;", "client-refused-answers-false"),
    "client-openq4-bad-password-ignored": ('idStr::Cmp( raw, "#str_04847" ) == 0 || ', "", "client-bad-password-openq4"),
    "client-no-console-output": ("\t\tgame->ReceiveRemoteConsoleOutput( string );", "", "client-forwards-rcon-output"),
    "client-gamedeny-classified": ("opcode != SERVER_PRINT_GAMEDENY && rconVerifyPending", "rconVerifyPending", "client-gamedeny-ignored"),
    "client-answers-twice": ("void idAsyncClient::AnswerRconVerify( bool success ) {\n\trconVerifyPending = false;",
                             "void idAsyncClient::AnswerRconVerify( bool success ) {", "client-answers-once"),
}


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def single(pattern: str, text: str, context: str) -> str:
    matches = re.findall(pattern, text, re.S)
    if len(matches) != 1:
        raise AssertionError(f"expected one {context}, found {len(matches)}")
    return matches[0]


def build_source() -> str:
    client = read("src/framework/async/AsyncClient.cpp")
    header = read("src/framework/async/AsyncClient.h")
    server = read("src/framework/async/AsyncServer.cpp")
    protocol = read("src/framework/async/Rcon2Protocol.h")
    clear = function_body(client, "void idAsyncClient::Clear(")
    for token in ("rconVerifyPending = false;", "rconVerifyRefused = false;", "memset( &rconVerifyAddress"):
        if token not in clear:
            raise AssertionError(f"idAsyncClient::Clear must reset the password check ({token!r})")
    types = (single(r"typedef enum \{[^}]*RCON_REPLY_NONE[^}]*\} rconReplyState_t;", header, "rcon reply states") + "\n" +
             single(r"typedef struct rcon2ClientRequest_s \{.*?\} rcon2ClientRequest_t;", header, "rcon2 client request"))
    constants = "\n".join(single(rf"static const int {name} = \d+;", client, name)
                          for name in ("RCON2_CLIENT_TIMEOUT_MSEC", "RCON2_CLIENT_RESEND_MSEC"))
    helpers = "\n".join(function_body(client, signature) for signature in (
        "static ID_INLINE bool AsyncClient_SameEndpoint(", "static ID_INLINE std::uint32_t AsyncClient_Elapsed("))
    support = (SUPPORT.replace("@MIN_PASSWORD_BYTES@", single(r"MIN_PASSWORD_BYTES = (\d+);", protocol, "rcon2 minimum"))
               .replace("@RCON_TYPES@", types).replace("@RCON_CONSTANTS@", constants).replace("@HELPERS@", helpers))
    bodies = [function_body(client, signature) for signature in (
        "void idAsyncClient::RemoteConsole(", "void idAsyncClient::UpdateRemoteConsoleRequest(",
        "void idAsyncClient::AnswerRconVerify(", "void idAsyncClient::ProcessPrintMessage(")]
    bodies.append(function_body(server, "void idAsyncServer::ExecuteRemoteConsoleCommand("))
    return support + "\n".join(bodies) + MAIN


def compile_and_run(compiler: str, text: str, directory: Path, name: str) -> subprocess.CompletedProcess:
    source, binary = directory / f"{name}.cpp", directory / f"{name}.exe"
    source.write_text(text, encoding="utf-8")
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-sign-compare"]
    subprocess.run([compiler, *flags, str(source), "-o", str(binary)], check=True)
    return subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--mutations", action="store_true", help="also require each reverted rule to fail")
    args = parser.parse_args()
    compiler = args.compiler or next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    text = build_source()
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rcon-verify-", dir=ROOT / ".tmp") as scratch:
        directory = Path(scratch)
        result = compile_and_run(compiler, text, directory, "positive")
        print(result.stdout, end="")
        if result.returncode != 0:
            raise SystemExit(f"async_rcon_verify_contract: {result.stderr.strip()}")
        if args.mutations:
            for name, (old, new, expected) in MUTATIONS.items():
                if text.count(old) != 1:
                    raise SystemExit(f"async_rcon_verify_contract: mutation {name} no longer matches the production source")
                mutated = compile_and_run(compiler, text.replace(old, new), directory, name)
                if mutated.returncode == 0 or f"FAIL {expected}" not in mutated.stderr:
                    raise SystemExit(f"async_rcon_verify_contract: mutation {name} was not caught "
                                     f"(exit {mutated.returncode}: {mutated.stderr.strip()})")
                print(f"async_rcon_verify_contract: mutation {name} caught ({expected})")


if __name__ == "__main__":
    main()
