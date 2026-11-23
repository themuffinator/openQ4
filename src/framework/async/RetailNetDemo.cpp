/*
===========================================================================

openQ4 reader for retail Quake 4 1.4.2 network demos.
Copyright (C) 2026 DarkMatter Productions

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

The layout below was read from the Quake 4 1.4.2 executable (idDemo::Play,
idDemo::ReadHeader, idFileSystemLocal::ReadDemoHeader,
idAsyncClient::ReadDemoHeader, idMsgChannel::Restore and the client's message
processing) and checked against the two demos 1.4.2 installs ship in
q4mp/demos. Every count and length is checked here before anything is copied
or handed on: the 1.4.2 reader itself trusted them.

===========================================================================
*/

#include "AsyncNetwork.h"
#include "RetailNetDemo.h"

#include <algorithm>

idCVar net_retailDemoDebug( "net_retailDemoDebug", "0", CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT,
	"debug a retail network demo: 1 the decl files numbered, 2 every decl, 3 every entity of every snapshot", 0, 3 );

namespace openq4_retail {

namespace {

// A bounded little-endian reader over the whole file.
class reader_t {
public:
	reader_t( const byte *data, int length ) : data( data ), length( length ), cursor( 0 ), failed( false ) {}

	bool Need( int count ) {
		if ( failed || count < 0 || cursor > length - count ) {
			failed = true;
			return false;
		}
		return true;
	}
	int Int() {
		if ( !Need( 4 ) ) {
			return 0;
		}
		const int value = static_cast<int>( static_cast<unsigned int>( data[cursor] ) |
			( static_cast<unsigned int>( data[cursor + 1] ) << 8 ) |
			( static_cast<unsigned int>( data[cursor + 2] ) << 16 ) |
			( static_cast<unsigned int>( data[cursor + 3] ) << 24 ) );
		cursor += 4;
		return value;
	}
	int Short() {
		if ( !Need( 2 ) ) {
			return 0;
		}
		const int value = static_cast<short>( data[cursor] | ( data[cursor + 1] << 8 ) );
		cursor += 2;
		return value;
	}
	int UShort() {
		return Short() & 0xffff;
	}
	int Byte() {
		if ( !Need( 1 ) ) {
			return 0;
		}
		return data[cursor++];
	}
	// idFile::ReadString: a length, then that many characters
	bool String( idStr &value, int maxLength ) {
		const int count = Int();
		if ( failed || count < 0 || count > maxLength || !Need( count ) ) {
			failed = true;
			return false;
		}
		value.Empty();
		for ( int i = 0; i < count; i++ ) {
			const char c = static_cast<char>( data[cursor + i] );
			if ( c == '\0' ) {
				failed = true;
				return false;
			}
			value.Append( c );
		}
		cursor += count;
		return true;
	}
	// idDemo::ReadDict: a count, then key/value strings
	bool Dict( idDict &dict ) {
		dict.Clear();
		const int count = Int();
		if ( failed || count < 0 || count > 4096 ) {
			failed = true;
			return false;
		}
		for ( int i = 0; i < count; i++ ) {
			idStr key, value;
			if ( !String( key, MAX_STRING_CHARS - 1 ) || !String( value, MAX_STRING_CHARS - 1 ) ) {
				return false;
			}
			dict.Set( key, value );
		}
		return true;
	}
	bool Skip( int count ) {
		if ( !Need( count ) ) {
			return false;
		}
		cursor += count;
		return true;
	}
	// idAsyncClient::ReadDemoUsercmd: false at the -1 end marker
	bool Usercmd( retailUsercmd_t &cmd, bool &end ) {
		memset( &cmd, 0, sizeof( cmd ) );
		cmd.gameFrame = Int();
		end = cmd.gameFrame == -1;
		if ( failed || end ) {
			return !failed;
		}
		cmd.gameTime = Int();
		cmd.realTime = Int();
		cmd.duplicateCount = Int();
		cmd.buttons = static_cast<short>( Short() );
		cmd.forwardmove = static_cast<signed char>( Byte() );
		cmd.rightmove = static_cast<signed char>( Byte() );
		cmd.upmove = static_cast<signed char>( Byte() );
		cmd.angles[0] = static_cast<short>( Short() );
		cmd.angles[1] = static_cast<short>( Short() );
		cmd.angles[2] = static_cast<short>( Short() );
		cmd.impulse = static_cast<signed char>( Byte() );
		cmd.flags = static_cast<unsigned char>( Byte() );
		return !failed;
	}

	const byte *	data;
	int				length;
	int				cursor;
	bool			failed;
};

static bool Fail( idStr &error, const char *text ) {
	if ( error.IsEmpty() ) {
		error = text;
	}
	return false;
}

// One entity state of the game's network info: a state of 1 to 512 bytes.
static bool EntityState( reader_t &r, bool numberFirst, int expectedNumber ) {
	const int number = r.Int();
	const int size = r.Int();
	if ( r.failed || number < 0 || number >= NETDEMO_MAX_ENTITIES ||
		 ( expectedNumber >= 0 && number != expectedNumber ) ||
		 size < 1 || size > NETDEMO_MAX_STATE_BYTES ) {
		r.failed = true;
		return false;
	}
	( void )numberFirst;
	return r.Skip( size );
}

} // namespace

/*
==================
IsRetailNetDemoName
==================
*/
bool IsRetailNetDemoName( const char *path ) {
	if ( path == NULL ) {
		return false;
	}
	idStr extension;
	idStr( path ).ExtractFileExtension( extension );
	if ( extension.Icmp( "netdemo" ) == 0 ) {
		return true;
	}
	if ( extension.Length() < 5 || idStr::Icmpn( extension.c_str(), "ndmo", 4 ) != 0 ) {
		return false;
	}
	for ( int i = 4; i < extension.Length(); i++ ) {
		if ( extension[i] < '0' || extension[i] > '9' ) {
			return false;
		}
	}
	return true;
}

/*
==================
ParseContainer
==================
*/
bool ParseContainer( const byte *data, int length, retailNetDemo_t &demo, idStr &error ) {
	error.Clear();
	if ( data == NULL || length < 16 ) {
		return Fail( error, "the file is too short to be a network demo" );
	}
	if ( length > NETDEMO_MAX_FILE_BYTES ) {
		return Fail( error, "the file is larger than any network demo openQ4 reads" );
	}
	reader_t r( data, length );

	// idDemo::ReadHeader
	const int ident = r.Int();
	if ( ident != NETDEMO_IDENT ) {
		return Fail( error, "not a Quake 4 network demo" );
	}
	demo.version = r.Int();
	const int protocol = r.Int();
	demo.protocolMajor = protocol >> 16;
	demo.protocolMinor = protocol & 0xffff;
	demo.usercmdStart = r.Int();
	if ( demo.version != NETDEMO_VERSION ||
		 demo.protocolMajor != NETDEMO_PROTOCOL_MAJOR || demo.protocolMinor != NETDEMO_PROTOCOL_MINOR ) {
		error = va( "recorded by another Quake 4 release (demo version %d, protocol %d.%d); "
			"openQ4 plays Quake 4 1.4.2 network demos (version %d, protocol %d.%d)",
			demo.version, demo.protocolMajor, demo.protocolMinor,
			NETDEMO_VERSION, NETDEMO_PROTOCOL_MAJOR, NETDEMO_PROTOCOL_MINOR );
		return false;
	}
	if ( demo.usercmdStart <= 16 || demo.usercmdStart > length - 4 ) {
		return Fail( error, "the demo's command stream offset is outside the file" );
	}

	// idFileSystemLocal::ReadDemoHeader: the game directories, the pure pak
	// list and, after it, the recording OS and its game paks
	if ( !r.String( demo.fsGameBase, MAX_OSPATH ) || !r.String( demo.fsGame, MAX_OSPATH ) ) {
		return Fail( error, "the demo's game directory names are damaged" );
	}
	const int pakCount = r.Int();
	if ( r.failed || pakCount < 0 || pakCount > NETDEMO_MAX_PURE_PAKS ) {
		return Fail( error, "the demo's pak list is damaged" );
	}
	demo.pakNames.Clear();
	demo.pakChecksums.Clear();
	demo.recordOS = -1;
	if ( pakCount > 0 ) {
		for ( int i = 0; i < pakCount; i++ ) {
			idStr name;
			if ( !r.String( name, MAX_OSPATH ) ) {
				return Fail( error, "the demo's pak list is damaged" );
			}
			demo.pakNames.Append( name );
			demo.pakChecksums.Append( r.Int() );
		}
		demo.recordOS = r.Int();
		for ( int i = 0; i < NETDEMO_GAME_PAK_SLOTS; i++ ) {
			idStr name;
			if ( !r.String( name, MAX_OSPATH ) ) {
				return Fail( error, "the demo's game pak list is damaged" );
			}
			r.Int();
		}
		if ( r.failed || demo.recordOS < 0 || demo.recordOS >= 6 ) {
			return Fail( error, "the demo's game pak list is damaged" );
		}
	}

	// idDemo::Play: server or client recording
	const int serverDemo = r.Byte();
	if ( r.failed || serverDemo > 1 ) {
		return Fail( error, "the demo's recording kind is damaged" );
	}
	demo.serverDemo = serverDemo != 0;

	// idAsyncClient::ReadDemoHeader
	demo.clientDataChecksum = r.Int();
	demo.clientTime = r.Int();
	demo.clientId = r.Int();
	demo.clientNum = r.Int();
	demo.clientPrediction = r.Int();
	demo.clientPredictTime = r.Int();
	r.Int();	// the server address: type, four address bytes, port
	r.Skip( 4 );
	r.UShort();
	demo.serverId = r.Int();
	demo.serverChallenge = r.Int();
	demo.serverMessageSequence = r.Int();
	demo.snapshotSequence = r.Int();
	demo.snapshotGameFrame = r.Int();
	demo.snapshotGameTime = r.Int();
	demo.gameInitId = r.Int();
	demo.gameFrame = r.Int();
	demo.gameTime = r.Int();
	demo.gameTimeResidual = r.Int();
	if ( r.failed ) {
		return Fail( error, "the demo's client state is truncated" );
	}
	if ( demo.serverDemo ? demo.clientNum != NETDEMO_MAX_CLIENTS :
		 ( demo.clientNum < 0 || demo.clientNum >= NETDEMO_MAX_CLIENTS ) ) {
		return Fail( error, "the demo's recording client is out of range" );
	}
	if ( demo.gameFrame < 0 || demo.gameTime < 0 || demo.snapshotGameFrame < 0 || demo.snapshotGameTime < 0 ||
		 demo.snapshotGameFrame > demo.gameFrame || demo.serverId < 0 || demo.serverId > 0xffff ) {
		return Fail( error, "the demo's client clock is invalid" );
	}
	demo.initialUsercmds.clear();
	demo.initialUsercmdClients.clear();
	if ( !demo.serverDemo ) {
		for ( int client = 0; client < NETDEMO_MAX_CLIENTS; client++ ) {
			for ( int frame = 0; frame < NETDEMO_USERCMD_BACKUP; frame++ ) {
				retailUsercmd_t cmd;
				bool end;
				if ( !r.Usercmd( cmd, end ) ) {
					return Fail( error, "the demo's command table is truncated" );
				}
				if ( !end && cmd.gameFrame > 0 ) {
					demo.initialUsercmds.push_back( cmd );
					demo.initialUsercmdClients.push_back( client );
				}
			}
		}
	}

	// idMsgChannel::Restore and the reliable queue's idMsgQueue::Restore
	demo.incomingSequence = r.Int();
	demo.fragmentSequence = r.Int();
	const int fragmentLength = r.Int();
	if ( r.failed || fragmentLength < 0 || fragmentLength > NETDEMO_MAX_MESSAGE || !r.Need( fragmentLength ) ) {
		return Fail( error, "the demo's channel state is damaged" );
	}
	demo.fragmentBuffer.SetNum( fragmentLength );
	if ( fragmentLength > 0 ) {
		memcpy( demo.fragmentBuffer.Ptr(), data + r.cursor, fragmentLength );
	}
	r.Skip( fragmentLength );
	r.Int();	// the queue's first sequence
	demo.reliableReceiveLast = r.Int();
	const int queueStart = r.Int();
	const int queueEnd = r.Int();
	if ( r.failed || queueStart < 0 || queueEnd < queueStart || queueEnd > NETDEMO_MAX_QUEUE ) {
		return Fail( error, "the demo's reliable queue is damaged" );
	}
	if ( queueEnd != queueStart ) {
		// The client empties its queue in the frame that fills it, so a
		// recording never starts with messages waiting.
		return Fail( error, "the demo starts with unprocessed reliable messages" );
	}

	// idDemo::Play: the map spawn data
	for ( int i = 0; i < NETDEMO_MAX_CLIENTS; i++ ) {
		if ( !r.Dict( demo.userInfo[i] ) ) {
			return Fail( error, "the demo's player info is damaged" );
		}
	}
	if ( !r.Dict( demo.serverInfo ) || !r.Dict( demo.syncedCVars ) ) {
		return Fail( error, "the demo's server info is damaged" );
	}

	// idGameLocal::ReadNetworkInfo, walked without the game
	const int networkInfoStart = r.cursor;
	if ( !demo.serverDemo ) {
		for ( int i = 0; i < NETDEMO_MAX_ENTITIES; i++ ) {
			const int valid = r.Byte();
			if ( r.failed || valid > 1 ) {
				return Fail( error, "the demo's entity states are damaged" );
			}
			if ( valid && !EntityState( r, true, i ) ) {
				return Fail( error, va( "the demo's state of entity %d is damaged", i ) );
			}
		}
		if ( !r.Skip( NETDEMO_MAX_CLIENTS * NETDEMO_PVS_WORDS * 4 ) ) {
			return Fail( error, "the demo's PVS state is truncated" );
		}
	}
	demo.playerCount = r.Int();
	if ( r.failed || demo.playerCount < 0 || demo.playerCount > NETDEMO_MAX_CLIENTS ) {
		return Fail( error, "the demo's player list is damaged" );
	}
	demo.playerSlots.Clear();
	bool recorderPresent = demo.serverDemo;
	for ( int i = 0, previous = -1; i < demo.playerCount; i++ ) {
		const int slot = r.Int();
		r.Int();	// spawn id
		if ( r.failed || slot <= previous || slot >= NETDEMO_MAX_CLIENTS ) {
			return Fail( error, "the demo's player list is damaged" );
		}
		recorderPresent = recorderPresent || slot == demo.clientNum;
		demo.playerSlots.Append( slot );
		previous = slot;
	}
	if ( !recorderPresent ) {
		return Fail( error, "the demo's recording player is not among its players" );
	}
	if ( !demo.serverDemo ) {
		const int snapshots = r.Int();
		if ( r.failed || snapshots < 0 || snapshots > NETDEMO_MAX_SNAPSHOTS ) {
			return Fail( error, "the demo's pending snapshots are damaged" );
		}
		for ( int s = 0; s < snapshots; s++ ) {
			r.Int();	// sequence
			const int states = r.Int();
			if ( r.failed || states < 0 || states > NETDEMO_MAX_ENTITIES ) {
				return Fail( error, "the demo's pending snapshots are damaged" );
			}
			for ( int i = 0; i < states; i++ ) {
				if ( !EntityState( r, true, -1 ) ) {
					return Fail( error, "the demo's pending snapshots are damaged" );
				}
			}
			if ( !r.Skip( NETDEMO_PVS_WORDS * 4 ) ) {
				return Fail( error, "the demo's pending snapshots are truncated" );
			}
		}
	}
	for ( int message = 0; message < 2; message++ ) {
		// the multiplayer start state, then the game state
		const int size = r.Int();
		if ( r.failed || size < 1 || size > NETDEMO_MAX_GAME_MESSAGE || !r.Skip( size ) ) {
			return Fail( error, "the demo's game state is damaged" );
		}
	}
	demo.gameNetworkInfo.SetNum( r.cursor - networkInfoStart );
	memcpy( demo.gameNetworkInfo.Ptr(), data + networkInfoStart, r.cursor - networkInfoStart );

	// idDemo::PlayMessage: time, size and bytes, up to a zero time
	demo.packets.clear();
	int previousTime = demo.clientTime;
	while ( true ) {
		if ( r.cursor > demo.usercmdStart - 4 ) {
			return Fail( error, "the demo's packets run into its command stream" );
		}
		int time = r.Int();
		if ( r.failed ) {
			return Fail( error, "the demo's packet stream is truncated" );
		}
		if ( time == 0 ) {
			if ( r.cursor == demo.usercmdStart ) {
				break;	// idDemo::Stop's end marker
			}
			// 1.4.2 can record a packet with no time (id_perftest.ndmo85 has one
			// mid-stream), and its own player stopped there. The packets after it
			// are as good as the ones before, so it plays at the time before it.
			time = previousTime;
		}
		const int size = r.Int();
		if ( r.failed || size < 0 || size > NETDEMO_MAX_MESSAGE || r.cursor > demo.usercmdStart - size ) {
			return Fail( error, va( "the demo's packet %d has an invalid size", static_cast<int>( demo.packets.size() ) ) );
		}
		if ( time < previousTime ) {
			return Fail( error, va( "the demo's packet %d goes back in time", static_cast<int>( demo.packets.size() ) ) );
		}
		if ( static_cast<int>( demo.packets.size() ) >= NETDEMO_MAX_PACKETS ) {
			return Fail( error, "the demo has more packets than openQ4 reads" );
		}
		retailPacket_t packet;
		packet.time = time;
		packet.offset = r.cursor;
		packet.size = size;
		demo.packets.push_back( packet );
		r.Skip( size );
		previousTime = time;
	}
	if ( r.cursor != demo.usercmdStart ) {
		return Fail( error, "the demo's packet stream does not end where its command stream starts" );
	}
	if ( demo.packets.empty() ) {
		return Fail( error, "the demo holds no packets" );
	}

	// the recorder's own commands, appended by idDemo::Stop, up to -1
	demo.usercmds.clear();
	bool ended = false;
	while ( !ended ) {
		retailUsercmd_t cmd;
		if ( !r.Usercmd( cmd, ended ) ) {
			return Fail( error, "the demo's command stream is truncated" );
		}
		if ( !ended ) {
			if ( demo.serverDemo ) {
				return Fail( error, "a server demo holds player commands" );
			}
			if ( !demo.usercmds.empty() && cmd.gameFrame <= demo.usercmds.back().gameFrame ) {
				return Fail( error, "the demo's command stream goes back in time" );
			}
			if ( static_cast<int>( demo.usercmds.size() ) >= NETDEMO_MAX_PACKETS ) {
				return Fail( error, "the demo has more commands than openQ4 reads" );
			}
			demo.usercmds.push_back( cmd );
		}
	}
	if ( r.cursor != length ) {
		return Fail( error, "the demo has bytes after its command stream" );
	}
	return true;
}

/*
==================
DecodeZeroRun

idCompressor_RunLength_ZeroBased with 3-bit words: a word of 0 is followed by
a count, and stands for count + 1 zero words. Words are packed from the low
bit of each byte up.
==================
*/
bool DecodeZeroRun( const byte *in, int inLength, byte *out, int outLength ) {
	if ( in == NULL || out == NULL || inLength < 0 || outLength < 0 ) {
		return false;
	}
	const int inBits = inLength * 8;
	const int outBits = outLength * 8;
	int readBit = 0;
	int writeBit = 0;
	memset( out, 0, outLength );
	auto readWord = [&]( int &word ) {
		if ( readBit + 3 > inBits ) {
			return false;
		}
		word = 0;
		for ( int i = 0; i < 3; i++, readBit++ ) {
			word |= ( ( in[ readBit >> 3 ] >> ( readBit & 7 ) ) & 1 ) << i;
		}
		return true;
	};
	auto writeWord = [&]( int word ) {
		for ( int i = 0; i < 3 && writeBit < outBits; i++, writeBit++ ) {
			out[ writeBit >> 3 ] |= ( ( word >> i ) & 1 ) << ( writeBit & 7 );
		}
	};
	while ( writeBit < outBits ) {
		int word;
		if ( !readWord( word ) ) {
			return false;
		}
		if ( word == 0 ) {
			int count;
			if ( !readWord( count ) ) {
				return false;
			}
			for ( int i = 0; i <= count; i++ ) {
				writeWord( 0 );
			}
		} else {
			writeWord( word );
		}
	}
	return true;
}

/*
==================
ToUsercmd
==================
*/
void ToUsercmd( const retailUsercmd_t &in, usercmd_t &out ) {
	memset( &out, 0, sizeof( out ) );
	out.gameFrame = in.gameFrame;
	out.gameTime = in.gameTime;
	out.duplicateCount = in.duplicateCount;
	out.buttons = in.buttons;
	out.forwardmove = in.forwardmove;
	out.rightmove = in.rightmove;
	out.upmove = in.upmove;
	out.angles[0] = in.angles[0];
	out.angles[1] = in.angles[1];
	out.angles[2] = in.angles[2];
	out.impulse = in.impulse;
	out.flags = in.flags;
}

/*
==================
ReadRelayedUsercmd

Quake4.exe 1.4.2: buttons, then each movement as two bits (0, +127, -127 or
an 8-bit value to follow), then the angles; against a base each field is a
delta, without one the buttons and the last angle still are (from 0x12 and
0). No impulse, flags or times travel.
==================
*/
bool ReadRelayedUsercmd( const idBitMsg &msg, usercmd_t &cmd, const usercmd_t *base ) {
	memset( &cmd, 0, sizeof( cmd ) );
	// idBitMsg::ReadDelta (private): a changed bit, then the value
	auto delta = [&]( int oldValue, int numBits ) -> int {
		return msg.ReadBits( 1 ) ? msg.ReadBits( numBits ) : oldValue;
	};
	auto move = [&]() -> signed char {
		switch ( msg.ReadBits( 2 ) ) {
			case 1: return 127;
			case 2: return -127;
			case 3: return static_cast<signed char>( msg.ReadBits( -8 ) );
			default: return 0;
		}
	};
	if ( base != NULL ) {
		cmd.buttons = static_cast<short>( delta( base->buttons, -16 ) );
		cmd.forwardmove = move();
		cmd.rightmove = move();
		cmd.upmove = move();
		cmd.angles[0] = static_cast<short>( delta( base->angles[0], -16 ) );
		cmd.angles[1] = static_cast<short>( delta( base->angles[1], -16 ) );
		cmd.angles[2] = static_cast<short>( delta( base->angles[2], -16 ) );
	} else {
		cmd.buttons = static_cast<short>( delta( 0x12, -16 ) );
		cmd.forwardmove = move();
		cmd.rightmove = move();
		cmd.upmove = move();
		cmd.angles[0] = static_cast<short>( msg.ReadBits( -16 ) );
		cmd.angles[1] = static_cast<short>( msg.ReadBits( -16 ) );
		cmd.angles[2] = static_cast<short>( delta( 0, -16 ) );
	}
	return !msg.IsReadOverflowed();
}

/*
==================
MissingPaks
==================
*/
int MissingPaks( const retailNetDemo_t &demo, idStr &names ) {
	names.Clear();
	idStrList loaded;
	FS_ListLoadedPacks( loaded );
	int missing = 0;
	for ( int i = 0; i < demo.pakChecksums.Num(); i++ ) {
		const idStr wanted = va( ":%08x", static_cast<unsigned int>( demo.pakChecksums[i] ) );
		bool found = false;
		for ( int j = 0; j < loaded.Num() && !found; j++ ) {
			found = loaded[j].Length() >= wanted.Length() &&
				idStr::Icmp( loaded[j].c_str() + loaded[j].Length() - wanted.Length(), wanted.c_str() ) == 0;
		}
		if ( !found ) {
			if ( missing > 0 ) {
				names += ", ";
			}
			names += demo.pakNames[i];
			missing++;
		}
	}
	return missing;
}

namespace {

struct declRemap_t {
	bool						active;
	std::vector< std::vector<int> >	tables;	// [type][retail index] = openQ4 index
};

static declRemap_t &DeclRemap() {
	static declRemap_t remap = { false, std::vector< std::vector<int> >() };
	return remap;
}

}

/*
==================
BuildDeclRemap

Retail Quake 4 numbered each decl type in the order it read its decl files.
openQ4 reads the same files in the same order and its own files among them,
so the recording's numbering is openQ4's, with every decl that did not come
from one of the recording's paks left out. Implicit decls (made on first use
rather than read) are never sent, by either game, and are left out too.
==================
*/
void BuildDeclRemap( const retailNetDemo_t &demo ) {
	declRemap_t &remap = DeclRemap();
	remap.tables.clear();
	remap.active = false;
	if ( declManager == NULL ) {
		return;
	}
	// A decl file is the recording's when it comes from one of Quake 4's own
	// paks: the base game's, or the recording's game directory (q4mp holds only
	// game code). The demo's pak list cannot decide it: that is the pure list of
	// the paks the map referenced, and a later Quake 4 patch can supply the copy.
	idHashTable<int> stockFiles;		// file name -> 1 from a Quake 4 pak, 0 not
	auto isStock = [&]( const char *fileName, int type ) {
		int *known = NULL;
		if ( stockFiles.Get( fileName, &known ) && known != NULL ) {
			return *known != 0;
		}
		int checksum = 0;
		int stock = 0;
		idStr gameDir;
		if ( FS_FilePack( fileName, checksum, gameDir ) ) {
			stock = ( gameDir.Icmp( BASE_GAMEDIR ) == 0 ||
				( !demo.fsGame.IsEmpty() && gameDir.Icmp( demo.fsGame ) == 0 ) ) ? 1 : 0;
		}
		stockFiles.Set( fileName, stock );
		if ( net_retailDemoDebug.GetInteger() > 0 ) {
			common->Printf( "retail decl file %s: %s %s %08x\n", fileName, stock ? "recorded" : "openQ4",
				gameDir.c_str(), checksum );
		}
		( void )type;
		return stock != 0;
	};
	struct entry_t {
		int		fileOrder;	// where the file came in the order decls were read
		int		line;
		int		index;		// openQ4's
	};
	const int types = declManager->GetNumDeclTypes();
	remap.tables.resize( Max( 0, types ) );
	for ( int type = 0; type < types; type++ ) {
		const declType_t declType = static_cast<declType_t>( type );
		const int count = declManager->GetNumDecls( declType );
		// Files are read one after another, each decl numbered as it is met, so
		// the first index of a file's decls places the file.
		idHashTable<int> fileOrder;
		for ( int i = 0; i < count; i++ ) {
			const idDecl *decl = declManager->DeclByIndex( declType, i, false );
			const char *fileName = decl != NULL ? decl->GetFileName() : NULL;
			int *known = NULL;
			if ( fileName != NULL && fileName[0] != '\0' && !fileOrder.Get( fileName, &known ) ) {
				fileOrder.Set( fileName, i );
			}
		}
		std::vector<entry_t> entries;
		entries.reserve( Max( 0, count ) );
		for ( int i = 0; i < count; i++ ) {
			const idDecl *decl = declManager->DeclByIndex( declType, i, false );
			if ( decl == NULL || decl->IsImplicit() ) {
				continue;
			}
			const char *fileName = decl->GetFileName();
			if ( fileName == NULL || fileName[0] == '\0' ) {
				continue;
			}
			entry_t entry;
			entry.index = i;
			if ( isStock( fileName, type ) ) {
				entry.fileOrder = i;
				int *order = NULL;
				if ( fileOrder.Get( fileName, &order ) && order != NULL ) {
					entry.fileOrder = *order;
				}
				entry.line = decl->GetLineNum();
			} else {
				// An openQ4 file that defined a recording's decl first: the
				// recording numbered it where its own file defined it.
				idStr shadowFile;
				int shadowLine = 0;
				if ( !DeclManager_ShadowedSource( declType, i, shadowFile, shadowLine ) ||
					 !isStock( shadowFile.c_str(), type ) ) {
					if ( net_retailDemoDebug.GetInteger() > 1 && type == DECL_ENTITYDEF ) {
						common->Printf( "openQ4 entityDef %d %s (%s:%d)\n", i, decl->GetName(), fileName, decl->GetLineNum() );
					}
					continue;
				}
				int *order = NULL;
				entry.fileOrder = fileOrder.Get( shadowFile.c_str(), &order ) && order != NULL ? *order : i;
				entry.line = shadowLine;
			}
			entries.push_back( entry );
		}
		std::stable_sort( entries.begin(), entries.end(), []( const entry_t &a, const entry_t &b ) {
			if ( a.fileOrder != b.fileOrder ) {
				return a.fileOrder < b.fileOrder;
			}
			if ( a.line != b.line ) {
				return a.line < b.line;
			}
			return a.index < b.index;
		} );
		std::vector<int> &table = remap.tables[type];
		table.reserve( entries.size() );
		for ( size_t e = 0; e < entries.size(); e++ ) {
			if ( net_retailDemoDebug.GetInteger() > 1 ) {
				const idDecl *decl = declManager->DeclByIndex( declType, entries[e].index, false );
				common->Printf( "retail %s %d = %d %s (%s:%d)\n", declManager->GetDeclNameFromType( declType ),
					static_cast<int>( e ), entries[e].index,
					decl->GetName(), decl->GetFileName(), entries[e].line );
			}
			table.push_back( entries[e].index );
		}
	}
	remap.active = true;
}

void ClearDeclRemap() {
	declRemap_t &remap = DeclRemap();
	remap.active = false;
	remap.tables.clear();
}

bool DeclRemapActive() {
	return DeclRemap().active;
}

int RemapDeclIndex( int declType, int retailIndex ) {
	const declRemap_t &remap = DeclRemap();
	if ( !remap.active ) {
		return retailIndex;
	}
	if ( declType < 0 || declType >= static_cast<int>( remap.tables.size() ) ||
		 retailIndex < 0 || retailIndex >= static_cast<int>( remap.tables[declType].size() ) ) {
		return -1;
	}
	return remap.tables[declType][retailIndex];
}

int RetailDeclCount( int declType ) {
	const declRemap_t &remap = DeclRemap();
	if ( !remap.active ) {
		return declManager != NULL ? declManager->GetNumDecls( static_cast<declType_t>( declType ) ) : 0;
	}
	if ( declType < 0 || declType >= static_cast<int>( remap.tables.size() ) ) {
		return 0;
	}
	return static_cast<int>( remap.tables[declType].size() );
}

/*
==================
Decode

idAsyncClient::RunDemoFrame fed each packet to ProcessMessage: the channel
(idMsgChannel::Process and ReadMessageData), then the reliable messages it
queued, then the unreliable rest. A server demo's packets went out to its
spectator slot through the same channel, so both kinds decode alike.
==================
*/
bool Decode( const byte *data, int length, retailNetDemo_t &demo, int msec, idStr &error ) {
	error.Clear();
	demo.events.clear();
	demo.snapshotCount = 0;
	demo.reliableCount = 0;
	demo.droppedPackets = 0;
	demo.endReason.Clear();
	if ( msec <= 0 ) {
		return Fail( error, "invalid simulation tic" );
	}

	int incomingSequence = demo.incomingSequence;
	int fragmentSequence = demo.fragmentSequence;
	idList<byte> fragments = demo.fragmentBuffer;
	int reliableLast = demo.reliableReceiveLast;
	idDict userInfo[ NETDEMO_MAX_CLIENTS ];
	for ( int i = 0; i < NETDEMO_MAX_CLIENTS; i++ ) {
		userInfo[i] = demo.userInfo[i];
	}

	// The recording's clock. A server demo runs on the server's snapshots; a
	// client demo on its recorder's frames, which ran on from the header's
	// game time as the client's own time passed.
	int stampFrame = demo.gameFrame;
	int stampTime = demo.gameTime;
	// A client demo starts after the snapshot its header names; a server demo's
	// header names the first one it will write.
	int lastSnapshotSequence = demo.serverDemo ? 0 : demo.snapshotSequence;
	auto clientStamp = [&]( int packetTime ) {
		const int elapsed = Max( 0, packetTime - demo.clientTime + Max( 0, demo.gameTimeResidual ) );
		const int frame = demo.gameFrame + elapsed / msec;
		if ( frame > stampFrame ) {
			stampFrame = frame;
			stampTime = demo.gameTime + ( frame - demo.gameFrame ) * msec;
		}
	};

	byte message[ NETDEMO_MAX_MESSAGE ];
	bool stopped = false;
	size_t nextUsercmd = 0;
	auto flushUsercmds = [&]( int throughFrame ) {
		// the recorder's own commands, each when its frame comes round
		while ( nextUsercmd < demo.usercmds.size() && demo.usercmds[ nextUsercmd ].gameFrame <= throughFrame ) {
			const retailUsercmd_t &cmd = demo.usercmds[ nextUsercmd++ ];
			if ( cmd.gameFrame < demo.gameFrame ) {
				continue;
			}
			retailEvent_t event;
			event.type = RETAIL_EVENT_USERCMD;
			event.frame = Max( cmd.gameFrame, demo.events.empty() ? 0 : demo.events.back().frame );
			event.time = demo.gameTime + ( event.frame - demo.gameFrame ) * msec;
			event.sequence = 0;
			event.snapshotFrame = 0;
			event.snapshotTime = 0;
			event.client = demo.clientNum;
			event.cmd = cmd;
			demo.events.push_back( event );
		}
	};

	for ( size_t p = 0; p < demo.packets.size() && !stopped; p++ ) {
		const retailPacket_t &packet = demo.packets[p];
		if ( !demo.serverDemo ) {
			clientStamp( packet.time );
			flushUsercmds( stampFrame );
		}
		idBitMsg msg;
		msg.Init( data + packet.offset, packet.size );
		msg.SetSize( packet.size );
		msg.BeginReading();
		if ( packet.size < 6 ) {
			demo.droppedPackets++;
			continue;
		}
		const int id = msg.ReadShort();
		if ( id == -1 ) {
			continue;	// out-of-band: nothing a demo plays
		}
		if ( ( id & 0xffff ) != ( demo.serverId & 0xffff ) ) {
			demo.droppedPackets++;
			continue;
		}

		// idMsgChannel::Process
		const unsigned int sequenceBits = static_cast<unsigned int>( msg.ReadLong() );
		const bool fragmented = ( sequenceBits & ( 1u << 31 ) ) != 0;
		const int sequence = static_cast<int>( sequenceBits & ~( 1u << 31 ) );
		int fragStart = 0;
		int fragLength = 0;
		if ( fragmented ) {
			if ( msg.GetRemaingData() < 4 ) {
				demo.droppedPackets++;
				continue;
			}
			fragStart = msg.ReadShort();
			fragLength = msg.ReadShort();
		}
		if ( sequence <= incomingSequence ) {
			continue;	// out of order or duplicated
		}
		const byte *payload = NULL;
		int payloadLength = 0;
		if ( fragmented ) {
			if ( sequence != fragmentSequence ) {
				fragmentSequence = sequence;
				fragments.SetNum( 0 );
			}
			if ( fragStart != fragments.Num() || fragLength < 0 || fragLength > NETDEMO_FRAGMENT_SIZE ||
				 fragLength > msg.GetRemaingData() || fragments.Num() + fragLength > NETDEMO_MAX_MESSAGE ) {
				demo.droppedPackets++;
				continue;
			}
			const int start = fragments.Num();
			fragments.SetNum( start + fragLength );
			if ( fragLength > 0 ) {
				memcpy( fragments.Ptr() + start, msg.GetReadData(), fragLength );
			}
			if ( fragLength == NETDEMO_FRAGMENT_SIZE ) {
				continue;	// more to come
			}
			payload = fragments.Ptr();
			payloadLength = fragments.Num();
		} else {
			payload = msg.GetReadData();
			payloadLength = msg.GetRemaingData();
		}
		incomingSequence = sequence;

		// idMsgChannel::ReadMessageData: the size, then the zero-run coded message
		if ( payloadLength < 2 ) {
			demo.droppedPackets++;
			continue;
		}
		const int messageSize = static_cast<short>( payload[0] | ( payload[1] << 8 ) );
		if ( messageSize < 6 || messageSize > NETDEMO_MAX_MESSAGE ||
			 messageSize > 4 * ( payloadLength - 2 ) + 8 ||
			 !DecodeZeroRun( payload + 2, payloadLength - 2, message, messageSize ) ) {
			error = va( "packet %d holds a damaged message (%d bytes from %d)", static_cast<int>( p ),
				messageSize, payloadLength - 2 );
			return false;
		}
		idBitMsg out;
		out.Init( static_cast<const byte *>( message ), messageSize );
		out.SetSize( messageSize );
		out.BeginReading();
		out.ReadLong();	// the server's acknowledgement of our reliables

		// the reliable messages, each once, in order (ProcessReliableServerMessages)
		while ( true ) {
			if ( out.GetRemaingData() < 2 ) {
				error = va( "packet %d ends inside its reliable messages", static_cast<int>( p ) );
				return false;
			}
			const int reliableSize = out.ReadShort();
			if ( reliableSize == 0 ) {
				break;
			}
			if ( reliableSize < 0 || out.GetRemaingData() < 4 || reliableSize > out.GetRemaingData() - 4 ) {
				error = va( "packet %d holds a damaged reliable message", static_cast<int>( p ) );
				return false;
			}
			const int reliableSequence = out.ReadLong();
			const byte *reliable = out.GetReadData();
			out.ReadData( NULL, reliableSize );
			if ( reliableSequence != reliableLast + 1 ) {
				continue;
			}
			reliableLast = reliableSequence;

			idBitMsg r;
			r.Init( reliable, reliableSize );
			r.SetSize( reliableSize );
			r.BeginReading();
			const int reliableId = r.ReadByte();
			retailEvent_t event;
			event.frame = stampFrame;
			event.time = stampTime;
			event.sequence = 0;
			event.snapshotFrame = 0;
			event.snapshotTime = 0;
			event.client = -1;
			memset( &event.cmd, 0, sizeof( event.cmd ) );
			switch ( reliableId ) {
				case RETAIL_RELIABLE_GAME: {
					const int gameBytes = reliableSize - 1;
					if ( gameBytes < 1 || gameBytes > NETDEMO_MAX_GAME_MESSAGE ) {
						error = va( "packet %d holds a game message of %d bytes", static_cast<int>( p ), gameBytes );
						return false;
					}
					event.type = RETAIL_EVENT_GAME_RELIABLE;
					event.data.SetNum( gameBytes );
					memcpy( event.data.Ptr(), reliable + 1, gameBytes );
					demo.events.push_back( event );
					demo.reliableCount++;
					break;
				}
				case RETAIL_RELIABLE_APPLYSNAPSHOT: {
					event.type = RETAIL_EVENT_APPLY_SNAPSHOT;
					event.sequence = r.ReadLong();
					if ( r.IsReadOverflowed() ) {
						error = va( "packet %d holds a damaged snapshot acknowledgement", static_cast<int>( p ) );
						return false;
					}
					if ( !demo.serverDemo ) {
						demo.events.push_back( event );
						demo.reliableCount++;
					}
					break;
				}
				case RETAIL_RELIABLE_CLIENTINFO: {
					const int client = r.ReadByte();
					const bool delta = r.ReadBits( 1 ) != 0;
					r.ReadLong();	// the info's checksum
					if ( r.IsReadOverflowed() || client >= NETDEMO_MAX_CLIENTS ) {
						error = va( "packet %d holds damaged player info", static_cast<int>( p ) );
						return false;
					}
					idDict info;
					r.ReadDeltaDict( info, delta ? &userInfo[ client ] : NULL );
					if ( r.IsReadOverflowed() ) {
						error = va( "packet %d holds damaged player info", static_cast<int>( p ) );
						return false;
					}
					userInfo[ client ] = info;
					event.type = RETAIL_EVENT_USERINFO;
					event.client = client;
					event.info = info;
					demo.events.push_back( event );
					break;
				}
				case RETAIL_RELIABLE_RELOAD:
					demo.endReason = "the server restarted the map";
					stopped = true;
					break;
				case RETAIL_RELIABLE_DISCONNECT: {
					const int client = r.ReadLong();
					if ( !r.IsReadOverflowed() && client == demo.clientNum ) {
						demo.endReason = "the server disconnected the recording client";
						stopped = true;
					}
					break;
				}
				default:
					// the pure list, the synced cvars (MAP_STATE has them), entering
					// the game and the client tags change nothing a viewer sees
					break;
			}
			if ( stopped ) {
				break;
			}
		}
		if ( stopped ) {
			break;
		}

		// the unreliable rest (ProcessUnreliableServerMessage)
		if ( out.GetRemaingData() < 5 ) {
			continue;
		}
		const int unreliableStart = out.GetReadCount();
		const int gameInitId = out.ReadLong();
		const int unreliableId = out.ReadByte();
		if ( unreliableId == RETAIL_UNRELIABLE_GAMEINIT ) {
			demo.endReason = "the server changed map";
			break;
		}
		if ( unreliableId != RETAIL_UNRELIABLE_SNAPSHOT || gameInitId != demo.gameInitId ) {
			continue;
		}
		if ( out.GetRemaingData() < 4 + 4 + 4 + 1 + 2 ) {
			error = va( "packet %d holds a truncated snapshot", static_cast<int>( p ) );
			return false;
		}
		const int snapshotSequence = out.ReadLong();
		const int snapshotFrame = out.ReadLong();
		const int snapshotTime = out.ReadLong();
		if ( snapshotSequence <= lastSnapshotSequence || snapshotFrame < 0 || snapshotTime < 0 ) {
			continue;
		}
		lastSnapshotSequence = snapshotSequence;
		if ( demo.serverDemo ) {
			stampFrame = Max( stampFrame, snapshotFrame );
			stampTime = Max( stampTime, snapshotTime );
		}
		retailEvent_t event;
		event.type = RETAIL_EVENT_SNAPSHOT;
		event.frame = stampFrame;
		event.time = stampTime;
		event.sequence = snapshotSequence;
		event.snapshotFrame = snapshotFrame;
		event.snapshotTime = snapshotTime;
		event.client = demo.clientNum;
		memset( &event.cmd, 0, sizeof( event.cmd ) );
		event.data.SetNum( messageSize - unreliableStart );
		memcpy( event.data.Ptr(), message + unreliableStart, messageSize - unreliableStart );
		demo.events.push_back( event );
		demo.snapshotCount++;
	}
	if ( !demo.serverDemo ) {
		flushUsercmds( stampFrame );
	}
	if ( demo.snapshotCount == 0 ) {
		return Fail( error, "the demo holds no snapshot of the game" );
	}
	demo.endFrame = demo.events.empty() ? stampFrame : Max( stampFrame, demo.events.back().frame );
	demo.endTime = demo.events.empty() ? stampTime : Max( stampTime, demo.events.back().time );
	if ( demo.endReason.IsEmpty() ) {
		demo.endReason = "complete";
	}
	return true;
}

}
