#!/usr/bin/env python3
"""Execute the production message channel against a reliable backlog and its rate.

idMsgChannel's WriteMessageData, ReadMessageData, ReliableSendSize, ReliablePrefixSize,
GetMaxSendMessageSize, ReadyToSend and ReadyToSendFragment run verbatim from
src/framework/async/MsgChannel.cpp. The harness supplies the byte message, a queue with
idMsgQueue's record layout (a 16 bit size, a 32 bit sequence, the message) and a stand-in
coder that copies its input and can grow it, as the zero-run coder grows data it cannot
shorten.

It checks that:
  - a message carries the whole reliable queue when it fits and otherwise the longest run
    of whole messages from the front, never part of one;
  - a queue larger than a message still drains: two channels trade packets and
    acknowledgements until every reliable message has arrived once and in order, beside a
    3 KB payload on every packet. A message used to be refused outright once the queue
    and the payload together passed MAX_MESSAGE_SIZE, so above 16,371 queued bytes
    nothing left at all;
  - packing that grows the message carries fewer reliable messages, down to none, and
    still sends the payload;
  - a packet claiming more than four times its packed bytes is refused before decoding;
  - the rest of a fragmented message leaves at once within 50 ms of the rate: four
    fragments at 128,000 bytes a second, one at 25,600, and more as the debt is paid.

Single-reversal controls re-run it against mutated source and must fail.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from sound_settings import body

ROOT = Path(__file__).resolve().parents[2]
SOURCE = "src/framework/async/MsgChannel.cpp"

FUNCTIONS = (
    "bool idMsgChannel::ReadyToSend(",
    "bool idMsgChannel::ReadyToSendFragment(",
    "int idMsgChannel::ReliableSendSize(",
    "int idMsgChannel::ReliablePrefixSize(",
    "bool idMsgChannel::WriteMessageData(",
    "bool idMsgChannel::ReadMessageData(",
    "int idMsgChannel::GetMaxSendMessageSize(",
)

MUTATIONS = (
    ("the whole queue or nothing (a full queue wedges the channel)",
     "return ReliablePrefixSize( MAX_MESSAGE_SIZE - 8 - dataBytes );",
     "return reliableSend.GetTotalSize();"),
    ("a reliable message is cut in two",
     "if ( record <= 6 || record > total - used || record > budget - used ) {",
     "if ( record <= 6 || record > total - used ) { used = budget; break; } if ( false ) {"),
    ("packing growth is not retried with fewer reliable messages",
     "reliableBytes = ReliablePrefixSize( reliableBytes / 2 );",
     "return false;"),
    ("a claimed size beyond the packed bytes is decoded",
     "if ( messageSize > 4 * msg.GetRemaingData() + 8 ) {",
     "if ( false ) {"),
    ("a fragment waits for the one before it to be paid for",
     "return debt + Net_FragmentSize( remoteAddress ) + 10 <= budget;",
     "return false;"),
    ("fragments ignore the rate",
     "return debt + Net_FragmentSize( remoteAddress ) + 10 <= budget;",
     "return true;"),
)

HARNESS = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// PRODUCTION_DEFINITIONS

using byte = unsigned char;
using int64 = int64_t;
template<class T> T Max( T a, T b ) { return std::max( a, b ); }
template<class T> T Min( T a, T b ) { return std::min( a, b ); }
struct idMath { static int ClampInt( int low, int high, int value ) { return std::clamp( value, low, high ); } };
struct netadr_t { int type = 0; };
static int Net_FragmentSize( const netadr_t & ) { return 1300; }
static const char *Sys_NetAdrToString( const netadr_t & ) { return "fixture"; }
struct Common { void Printf( const char *, ... ) {} } commonStorage;
static Common *common = &commonStorage;

class idBitMsg {
public:
	void Init( byte *data, int capacity ) { buffer = data; maximum = capacity; size = read = 0; }
	void SetSize( int n ) { if ( n < 0 || n > maximum ) { std::abort(); } size = n; }
	int GetSize() const { return size; }
	int GetMaxSize() const { return maximum; }
	int GetReadCount() const { return read; }
	int GetRemaingData() const { return size - read; }
	byte *GetData() const { return buffer; }
	void BeginReading() const { read = 0; }
	void WriteData( const void *data, int n ) {
		if ( n < 0 || n > maximum - size ) { std::abort(); }
		std::memcpy( buffer + size, data, n ); size += n;
	}
	void WriteShort( int value ) { WriteInteger( static_cast<uint32_t>( value ), 2 ); }
	void WriteUShort( int value ) { WriteInteger( static_cast<uint32_t>( value ), 2 ); }
	void WriteLong( int value ) { WriteInteger( static_cast<uint32_t>( value ), 4 ); }
	int ReadShort() const { return static_cast<int16_t>( ReadInteger( 2 ) ); }
	int ReadUShort() const { return static_cast<uint16_t>( ReadInteger( 2 ) ); }
	int ReadLong() const { return static_cast<int32_t>( ReadInteger( 4 ) ); }
	void ReadData( void *data, int n ) const {
		if ( n < 0 || n > size - read ) { std::abort(); }
		if ( data ) { std::memcpy( data, buffer + read, n ); }
		read += n;
	}
private:
	void WriteInteger( uint32_t value, int n ) {
		for ( int i = 0; i < n; i++ ) { byte b = static_cast<byte>( value >> ( i * 8 ) ); WriteData( &b, 1 ); }
	}
	uint32_t ReadInteger( int n ) const {
		if ( n > size - read ) { std::abort(); }
		uint32_t value = 0;
		for ( int i = 0; i < n; i++ ) { value |= uint32_t( buffer[read++] ) << ( i * 8 ); }
		return value;
	}
	byte *buffer = nullptr;
	int maximum = 0, size = 0;
	mutable int read = 0;
};

// idMsgQueue's record layout and accessors over a flat store
struct Queue {
	std::vector<byte> store;
	int first = 0, last = 0;
	void Init( int sequence ) { first = last = sequence; store.clear(); }
	int GetFirst() const { return first; }
	int GetLast() const { return last; }
	int GetTotalSize() const { return static_cast<int>( store.size() ); }
	void CopyToBuffer( byte *out ) const { if ( !store.empty() ) { std::memcpy( out, store.data(), store.size() ); } }
	bool Add( const byte *data, int size, bool sequencing ) {
		if ( static_cast<int>( store.size() ) + size + 8 > MAX_MSG_QUEUE_SIZE - 1 ) { return false; }
		store.push_back( static_cast<byte>( size & 255 ) ); store.push_back( static_cast<byte>( size >> 8 ) );
		if ( sequencing ) {
			for ( int i = 0; i < 4; i++ ) { store.push_back( static_cast<byte>( static_cast<uint32_t>( last ) >> ( i * 8 ) ) ); }
		}
		last++;
		store.insert( store.end(), data, data + size );
		return true;
	}
	bool Get( byte *data, int dataSize, int &size, bool sequencing ) {
		if ( first == last ) { size = 0; return false; }
		const int header = sequencing ? 6 : 2;
		size = store[0] | ( store[1] << 8 );
		if ( data ) { if ( size > dataSize ) { std::abort(); } std::memcpy( data, store.data() + header, size ); }
		store.erase( store.begin(), store.begin() + header + size );
		first++;
		return true;
	}
};

static int growthPercent = 0;
struct idFile_BitMsg {
	idBitMsg *writer = nullptr; const idBitMsg *reader = nullptr;
	explicit idFile_BitMsg( idBitMsg &msg ) : writer( &msg ) {}
	explicit idFile_BitMsg( const idBitMsg &msg ) : reader( &msg ) {}
};
// copies its input, then growthPercent more bytes, as packing that cannot shorten grows
struct Compressor {
	idFile_BitMsg *file = nullptr;
	void Init( idFile_BitMsg *f, bool, int ) { file = f; }
	int Write( const void *data, int size ) {
		file->writer->WriteData( data, size );
		const int extra = size * growthPercent / 100;
		for ( int i = 0; i < extra; i++ ) { const byte filler = 0x88; file->writer->WriteData( &filler, 1 ); }
		return size;
	}
	void FinishCompress() {}
	float GetCompressionRatio() const { return 0.0f; }
	int Read( void *out, int size ) {
		const int available = std::min( size, file->reader->GetRemaingData() );
		file->reader->ReadData( out, available );
		return available;
	}
};

class idMsgChannel {
public:
	netadr_t remoteAddress;
	int maxRate = 128000, lastSendTime = 0, lastDataBytes = 0;
	float outgoingCompression = 0.0f, incomingCompression = 0.0f;
	Queue reliableSend, reliableReceive;
	Compressor compressorStorage, *compressor = &compressorStorage;
	idMsgChannel() { reliableSend.Init( 1 ); reliableReceive.Init( 0 ); }
	bool ReadyToSend( const int time ) const;
	bool ReadyToSendFragment( const int time, const int burstMsec ) const;
	int ReliableSendSize( const int dataBytes ) const;
	int ReliablePrefixSize( const int budget ) const;
	bool WriteMessageData( idBitMsg &out, const idBitMsg &msg );
	bool ReadMessageData( idBitMsg &out, const idBitMsg &msg );
	int GetMaxSendMessageSize( void ) const;
};

// PRODUCTION_FUNCTIONS

static int failures = 0;
static void Check( bool passed, const char *label ) {
	if ( !passed ) { std::fprintf( stderr, "FAILED: %s\n", label ); failures++; }
}

static void Queue500( idMsgChannel &channel, int count ) {
	for ( int i = 0; i < count; i++ ) {
		byte data[500];
		for ( int j = 0; j < 500; j++ ) { data[j] = static_cast<byte>( i * 7 + j ); }
		data[0] = static_cast<byte>( i );
		if ( !channel.reliableSend.Add( data, 500, true ) ) { std::abort(); }
	}
}

// One packet from sender to receiver with this much payload; false when the sender refuses.
static bool Trade( idMsgChannel &sender, idMsgChannel &receiver, int payloadBytes, int *carried = nullptr ) {
	static byte payload[MAX_MESSAGE_SIZE], wire[MAX_MESSAGE_SIZE], decoded[MAX_MESSAGE_SIZE];
	std::memset( payload, 0x5a, sizeof( payload ) );
	idBitMsg data; data.Init( payload, sizeof( payload ) ); data.SetSize( payloadBytes );
	idBitMsg out; out.Init( wire, sizeof( wire ) );
	if ( !sender.WriteMessageData( out, data ) ) { return false; }
	if ( carried ) { *carried = out.GetSize(); }
	out.BeginReading();
	idBitMsg in; in.Init( decoded, sizeof( decoded ) );
	if ( !receiver.ReadMessageData( in, out ) ) { return false; }
	Check( in.GetRemaingData() == payloadBytes, "the payload arrives whole behind the reliable messages" );
	return true;
}

static void TestPrefix() {
	idMsgChannel channel;
	Queue500( channel, 4 );		// four records of 506 bytes
	Check( channel.ReliablePrefixSize( 4 * 506 ) == 4 * 506, "a queue that fits is carried whole" );
	Check( channel.ReliablePrefixSize( 4 * 506 - 1 ) == 3 * 506, "one byte short carries three whole messages" );
	Check( channel.ReliablePrefixSize( 506 ) == 506, "room for one message carries one" );
	Check( channel.ReliablePrefixSize( 505 ) == 0, "no room for the first message carries none" );
	Check( channel.ReliablePrefixSize( 0 ) == 0 && channel.ReliablePrefixSize( -100 ) == 0, "no budget carries none" );
	Check( channel.ReliableSendSize( MAX_MESSAGE_SIZE - 8 - 506 ) == 506, "the data's share comes off the message size" );
	idMsgChannel empty;
	Check( empty.ReliablePrefixSize( 100 ) == 0 && empty.GetMaxSendMessageSize() == MAX_MESSAGE_SIZE - 8, "an empty queue costs nothing" );
}

static void TestDrain() {
	idMsgChannel server, client;
	Queue500( server, 32 );		// 16,192 bytes: more than a message holds beside any payload
	Check( server.reliableSend.GetTotalSize() > MAX_MESSAGE_SIZE - 8 - 3000, "the backlog exceeds a message" );
	Check( server.GetMaxSendMessageSize() >= 12000, "a full queue still leaves room for a snapshot" );
	int delivered = 0, cycles = 0;
	bool refused = false, ordered = true;
	while ( delivered < 32 && cycles < 16 ) {
		cycles++;
		if ( !Trade( server, client, 3000 ) ) { refused = true; break; }
		byte message[600]; int size = 0;
		while ( client.reliableReceive.Get( message, sizeof( message ), size, false ) ) {
			ordered = ordered && size == 500 && message[0] == static_cast<byte>( delivered );
			delivered++;
		}
		if ( !Trade( client, server, 40 ) ) { refused = true; break; }		// carries the acknowledgement
	}
	Check( !refused, "a packet leaves while the reliable queue is over a message long" );
	Check( delivered == 32 && ordered, "every reliable message arrives once and in order" );
	Check( cycles <= 3, "the queue drains in the packets its size needs" );
	Check( server.reliableSend.GetTotalSize() == 0, "acknowledged messages leave the queue" );
}

static void TestGrowth() {
	idMsgChannel server, client;
	Queue500( server, 24 );		// 12,144 bytes; doubled by packing they fit no message
	growthPercent = 100;
	int carried = 0;
	const bool sent = Trade( server, client, 2000, &carried );
	growthPercent = 0;
	Check( sent, "packing growth sends the payload with fewer reliable messages" );
	byte message[600]; int size = 0, delivered = 0;
	while ( client.reliableReceive.Get( message, sizeof( message ), size, false ) ) { delivered++; }
	Check( delivered > 0 && delivered < 24, "some of the queue still rides the grown packet" );

	idMsgChannel alone, other;
	growthPercent = 100;		// even the payload alone no longer fits
	const bool refused = !Trade( alone, other, 9000 );
	growthPercent = 0;
	Check( refused, "a payload that packs past a message is refused" );
}

static void TestClaimedSize() {
	idMsgChannel receiver;
	static byte wire[64], decoded[MAX_MESSAGE_SIZE];
	idBitMsg out; out.Init( wire, sizeof( wire ) );
	out.WriteShort( 16000 );		// claims 16,000 bytes
	const byte six[6] = { 0, 0, 0, 0, 0, 0 };
	out.WriteData( six, 6 );		// and brings six
	out.BeginReading();
	idBitMsg in; in.Init( decoded, sizeof( decoded ) );
	Check( !receiver.ReadMessageData( in, out ), "a size beyond four times the packed bytes is refused" );
	Check( out.GetRemaingData() == 6, "and refused before any of it is decoded" );

	idBitMsg small; small.Init( wire, sizeof( wire ) );
	small.WriteShort( 6 );
	small.WriteData( six, 6 );
	small.BeginReading();
	idBitMsg ok; ok.Init( decoded, sizeof( decoded ) );
	Check( receiver.ReadMessageData( ok, small ), "an honest empty message is read" );
}

static int Burst( idMsgChannel &channel, int time ) {
	int sent = 0;
	while ( sent < 20 && channel.ReadyToSendFragment( time, 50 ) ) {
		// what idMsgChannel::UpdateOutgoingRate charges a full fragment
		const int elapsed = time - channel.lastSendTime;
		const int64 left = int64( channel.lastDataBytes ) - int64( elapsed ) * channel.maxRate / 1000;
		channel.lastDataBytes = ( left > 0 ? static_cast<int>( left ) : 0 ) + 1310;
		channel.lastSendTime = time;
		sent++;
	}
	return sent;
}

static void TestFragmentBurst() {
	idMsgChannel fast;
	fast.maxRate = 128000; fast.lastSendTime = 1000; fast.lastDataBytes = 0;
	Check( Burst( fast, 1000 ) == 4, "four fragments leave at once at 128,000 bytes a second" );
	Check( !fast.ReadyToSend( 1000 ), "the burst is charged, so the next message waits" );
	Check( Burst( fast, 1016 ) == 2, "a frame later the debt paid lets two more go" );
	Check( fast.ReadyToSend( 1100 ), "and the channel is clear once the burst is paid for" );

	idMsgChannel slow;
	slow.maxRate = 25600; slow.lastSendTime = 1000; slow.lastDataBytes = 0;
	Check( Burst( slow, 1000 ) == 1, "at 25,600 bytes a second a fragment still waits for the one before" );
	Check( Burst( slow, 1016 ) == 0 && Burst( slow, 1060 ) == 1, "and goes when that one is paid for" );

	idMsgChannel unlimited;
	unlimited.maxRate = 0;
	Check( unlimited.ReadyToSendFragment( 5, 50 ), "no rate means no wait" );

	idMsgChannel huge;
	huge.maxRate = 2000000000; huge.lastSendTime = 1000; huge.lastDataBytes = 1310;
	Check( huge.ReadyToSend( 1500 ) && huge.ReadyToSendFragment( 1500, 50 ), "a rate near the int limit does not overflow" );
}

int main() {
	TestPrefix();
	TestDrain();
	TestGrowth();
	TestClaimedSize();
	TestFragmentBurst();
	if ( failures ) { return 1; }
	std::puts( "PASS" );
	return 0;
}
'''


def fixture(source: str) -> str:
    import re
    header = (ROOT / "src/framework/async/MsgChannel.h").read_text(encoding="utf-8")
    definitions = "\n".join(re.search(r"^#define\s+" + name + r"\b.*", text, re.M).group(0) for name, text in (
        ("MAX_MESSAGE_SIZE", header), ("MAX_MSG_QUEUE_SIZE", header),
        ("MAX_RELIABLE_MESSAGE_SIZE", source), ("RELIABLE_SEND_RESERVE", source)))
    bodies = "\n".join(body(source, name) for name in FUNCTIONS)
    return HARNESS.replace("// PRODUCTION_DEFINITIONS", definitions).replace("// PRODUCTION_FUNCTIONS", bodies)


def main() -> None:
    source = (ROOT / SOURCE).read_text(encoding="utf-8")
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("a C++ compiler is required")
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="network-backlog-", dir=output) as temporary:
        directory = Path(temporary)

        def run(code: str, name: str) -> subprocess.CompletedProcess:
            cpp = directory / f"{name}.cpp"
            exe = directory / (name + (".exe" if os.name == "nt" else ""))
            cpp.write_text(fixture(code), encoding="utf-8")
            command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-sign-compare",
                       "-Wno-unused-function", "-Wno-unused-variable", str(cpp), "-o", str(exe)]
            if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, cwd=directory, check=True)
            return subprocess.run([str(exe)], cwd=directory, capture_output=True, text=True)

        result = run(source, "backlog")
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
        for index, (label, original, replacement) in enumerate(MUTATIONS):
            if source.count(original) != 1:
                raise SystemExit(f"mutation anchor not unique: {label}")
            result = run(source.replace(original, replacement, 1), f"control{index}")
            if result.returncode == 0:
                raise SystemExit(f"control did not fail: {label}")
    print(f"network_channel_backlog_state: ok ({len(MUTATIONS)} single-reversal controls fail)")


if __name__ == "__main__":
    main()
