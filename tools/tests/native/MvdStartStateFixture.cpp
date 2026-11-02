// Executes the production multi-view demo start-state checks: the engine's
// structural validator (idMultiViewDemo::ValidateNetworkStateRecord with
// MVD_StartStateMatchesPlayers) and the game's powerup-width probe
// (MP_StartStatePowerupBits). tools/tests/mvd_start_state_contract.py splices
// them in verbatim. The game's powerup mask grew from 16 to 18 bits with The
// Awakening's powerups; a validator that assumed 16 rejected every recording
// with two or more players, reading client 1 as client 4.

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

typedef unsigned char byte;

static const int MAX_ASYNC_CLIENTS = 32;
static const int MAX_CLIENTS = 32;
static const int MAX_INSTANCES = 8;
static const int ASYNC_PLAYER_INSTANCE_BITS = 4;
static const int POWERUP_MAX = 18;

static const char *va( const char *format, ... ) {
	static char buffers[4][256];
	static int index = 0;
	char *buffer = buffers[index++ & 3];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, 256, format, args );
	va_end( args );
	return buffer;
}

class idStr {
public:
	std::string s;
	idStr() {}
	idStr( const char *text ) : s( text ) {}
	idStr &operator=( const char *text ) { s = text; return *this; }
	bool IsEmpty() const { return s.empty(); }
	void Clear() { s.clear(); }
	const char *c_str() const { return s.c_str(); }
};

template<class T> class idList {
public:
	std::vector<T> v;
	int Num() const { return static_cast<int>( v.size() ); }
	const T *Ptr() const { return v.data(); }
};

// LSB-first bit message, the order idBitMsg uses.
class idBitMsg {
public:
	const byte *readData = nullptr;
	int size = 0;
	mutable int readBit = 0;
	void Init( const byte *data, int length ) { readData = data; size = length; readBit = 0; }
	void SetSize( int length ) { size = length; }
	void BeginReading() const { readBit = 0; }
	int GetRemainingReadBits() const { return size * 8 - readBit; }
	int ReadBits( int bits ) const {
		unsigned int value = 0;
		for ( int i = 0; i < bits; i++ ) {
			if ( readBit >= size * 8 ) {
				return -1;
			}
			value |= static_cast<unsigned int>( ( readData[readBit >> 3] >> ( readBit & 7 ) ) & 1 ) << i;
			readBit++;
		}
		return static_cast<int>( value );
	}
	int ReadShort() const { const int v = ReadBits( 16 ); return v < 0 ? -1 : static_cast<short>( v ); }
	int ReadLong() const { return ReadBits( 32 ); }
};

struct BitWriter {
	std::vector<byte> bytes;
	int bit = 0;
	void Bits( unsigned int value, int count ) {
		for ( int i = 0; i < count; i++, bit++ ) {
			if ( ( bit >> 3 ) >= static_cast<int>( bytes.size() ) ) {
				bytes.push_back( 0 );
			}
			if ( ( value >> i ) & 1 ) {
				bytes[bit >> 3] |= static_cast<byte>( 1 << ( bit & 7 ) );
			}
		}
	}
};

static unsigned int MVD_ReadLittleUInt( const byte *data ) {
	unsigned int value;
	memcpy( &value, data, sizeof( value ) );
	return value;
}

// PRODUCTION_CONSTANTS

// PRODUCTION_ENGINE_HELPER

class idMultiViewDemo {
public:
	struct record_t {
		unsigned short type = 3, version = 1, flags = 0;
		idList<byte> payload;
	};
	bool ValidateNetworkStateRecord( const record_t &record, idStr &error ) const;
	bool ValidateNetworkStatePayload( const byte *bytes, int length, idStr &error ) const;
};

// PRODUCTION_FUNCTIONS

static int failures = 0;
#define CHECK( condition ) do { if ( !( condition ) ) { std::fprintf( stderr, "FAILED line %d: %s\n", __LINE__, #condition ); failures++; } } while ( 0 )

struct Client { int number, spawnId, instance, spectating; unsigned int powerups; };

// A start state as idMultiplayerGame::WriteStartState writes it.
static std::vector<byte> StartState( const std::vector<Client> &clients, int powerupBits, int trailingBits = 0 ) {
	BitWriter writer;
	writer.Bits( 1234, 32 );
	for ( const Client &client : clients ) {
		writer.Bits( static_cast<unsigned int>( client.number ), 16 );
		writer.Bits( client.powerups, powerupBits );
		writer.Bits( static_cast<unsigned int>( client.instance ), ASYNC_PLAYER_INSTANCE_BITS );
		writer.Bits( static_cast<unsigned int>( client.spectating ), 1 );
	}
	writer.Bits( MAX_CLIENTS, 16 );
	writer.Bits( 0, trailingBits );
	return writer.bytes;
}

// A network-state record: the engine's player list, then the start state and
// a game-state message.
static idMultiViewDemo::record_t Record( const std::vector<Client> &listed, const std::vector<byte> &start ) {
	idMultiViewDemo::record_t record;
	auto put = [&]( unsigned int value ) {
		const byte *b = reinterpret_cast<const byte *>( &value );
		record.payload.v.insert( record.payload.v.end(), b, b + 4 );
	};
	put( static_cast<unsigned int>( listed.size() ) );
	for ( const Client &client : listed ) {
		put( static_cast<unsigned int>( client.number ) );
		put( static_cast<unsigned int>( client.spawnId ) );
	}
	put( static_cast<unsigned int>( start.size() ) );
	record.payload.v.insert( record.payload.v.end(), start.begin(), start.end() );
	put( 16 );
	record.payload.v.insert( record.payload.v.end(), 16, 0x5a );
	return record;
}

static bool Valid( const idMultiViewDemo::record_t &record, std::string *message = nullptr ) {
	idMultiViewDemo demo;
	idStr error;
	const bool valid = demo.ValidateNetworkStateRecord( record, error );
	if ( message ) {
		*message = error.s;
	}
	return valid;
}

static int ProbeWidth( const std::vector<byte> &start ) {
	idBitMsg msg;
	msg.Init( start.data(), static_cast<int>( start.size() ) );
	msg.BeginReading();
	return MP_StartStatePowerupBits( msg );
}

int main() {
	std::vector<Client> players;
	for ( int count = 1; count <= 6; count++ ) {
		players.push_back( { count == 1 ? 0 : count * 2 - 1, 300 + count, count % MAX_INSTANCES, count == 4, 0x2a5u & ( ( 1u << 16 ) - 1 ) } );
		// The current writer (18 bits) and recordings made before The Awakening's
		// powerups (16 bits) both validate, and the game reads each at its width.
		for ( int width : { POWERUP_MAX, 16 } ) {
			const std::vector<byte> start = StartState( players, width );
			std::string why;
			CHECK( Valid( Record( players, start ), &why ) );
			if ( !why.empty() && count > 1 ) {
				// A narrower width may fail first; the accepted width must still win.
			}
			CHECK( ProbeWidth( start ) == width );
		}
	}

	// The exact regression: three players at 18 bits. A fixed 16-bit reader
	// misaligned two bits and read client 1 as client 4.
	{
		const std::vector<Client> three = { { 0, 353, 0, 0, 0 }, { 1, 358, 0, 0, 0 }, { 2, 359, 0, 0, 0 } };
		CHECK( Valid( Record( three, StartState( three, 18 ) ) ) );
		CHECK( ProbeWidth( StartState( three, 18 ) ) == 18 );
	}

	// Malformed start states are still refused at every width.
	{
		const std::vector<Client> two = { { 0, 10, 0, 0, 1 }, { 3, 11, 1, 0, 2 } };
		std::vector<Client> unlisted = two;
		unlisted[1].number = 5;
		CHECK( !Valid( Record( two, StartState( unlisted, 18 ) ) ) );		// a client the list omits
		std::vector<Client> reversed = { two[1], two[0] };
		CHECK( !Valid( Record( two, StartState( reversed, 18 ) ) ) );		// out of order
		std::vector<Client> badInstance = two;
		badInstance[0].instance = 9;
		CHECK( !Valid( Record( two, StartState( badInstance, 18 ) ) ) );		// instance past MAX_INSTANCES
		CHECK( !Valid( Record( two, StartState( { two[0] }, 18 ) ) ) );		// one listed player missing
		CHECK( !Valid( Record( two, StartState( two, 18, 16 ) ) ) );			// trailing data after the sentinel
		std::vector<byte> unterminated = StartState( two, 18 );
		unterminated.resize( unterminated.size() - 2 );
		CHECK( !Valid( Record( two, unterminated ) ) );						// no sentinel
		CHECK( !Valid( Record( two, { 1, 2, 3 } ) ) );						// shorter than time + sentinel
	}

	// The game probe falls back to POWERUP_MAX when neither width parses, so a
	// live start state is never read at a narrower width by mistake.
	{
		const std::vector<Client> two = { { 0, 10, 0, 0, 1 }, { 3, 11, 1, 0, 2 } };
		std::vector<byte> garbage = StartState( two, 18 );
		garbage.push_back( 0xff );
		garbage.push_back( 0xff );
		CHECK( ProbeWidth( garbage ) == POWERUP_MAX );
	}

	if ( failures != 0 ) {
		std::fprintf( stderr, "mvd start-state fixture: %d failure(s)\n", failures );
		return 1;
	}
	std::printf( "mvd start-state fixture: ok\n" );
	return 0;
}
