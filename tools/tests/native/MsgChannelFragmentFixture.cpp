// Production fragment boundaries and sequencing; the port and decompression
// boundary are deterministic substitutes. See network_fragment_state.py.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// PRODUCTION_DEFINITIONS

using byte = unsigned char;
enum { NA_BAD, NA_IP, NA_IP6 };
struct netadr_t { int type = NA_IP; int port = 28004; };
static const char *Sys_NetAdrToString( const netadr_t & ) { return "fixture"; }
struct Common { void Printf( const char *, ... ) {} } commonStorage;
static Common *common = &commonStorage;
struct Cvar { bool GetBool() const { return false; } } net_channelShowPackets, net_channelShowDrop;

class idBitMsg {
public:
	void Init( byte *data, int capacity ) { buffer = data; maximum = capacity; size = read = 0; }
	void SetSize( int n ) { if ( n < 0 || n > maximum ) { std::abort(); } size = n; }
	int GetSize() const { return size; }
	int GetReadCount() const { return read; }
	int GetRemaingData() const { return size - read; }
	byte *GetData() const { return buffer; }
	void BeginReading() { read = 0; }
	void WriteData( const void *data, int n ) {
		if ( n < 0 || n > maximum - size ) { std::abort(); }
		std::memcpy( buffer + size, data, n ); size += n;
	}
	void WriteShort( int value ) { WriteInteger( static_cast<uint32_t>( value ), 2 ); }
	void WriteLong( int value ) { WriteInteger( static_cast<uint32_t>( value ), 4 ); }
	int ReadShort() { return static_cast<int16_t>( ReadInteger( 2 ) ); }
	int ReadLong() { return static_cast<int32_t>( ReadInteger( 4 ) ); }
private:
	void WriteInteger( uint32_t value, int n ) {
		for ( int i = 0; i < n; i++ ) { byte b = static_cast<byte>( value >> ( i * 8 ) ); WriteData( &b, 1 ); }
	}
	uint32_t ReadInteger( int n ) {
		if ( n > size - read ) { std::abort(); }
		uint32_t value = 0;
		for ( int i = 0; i < n; i++ ) { value |= uint32_t( buffer[read++] ) << ( i * 8 ); }
		return value;
	}
	byte *buffer = nullptr;
	int maximum = 0, size = 0, read = 0;
};

struct idPort {
	std::vector<std::vector<byte>> packets;
	void SendPacket( const netadr_t &, const void *data, int size ) {
		const byte *bytes = static_cast<const byte *>( data );
		packets.emplace_back( bytes, bytes + size );
	}
};

class idMsgChannel {
public:
	netadr_t remoteAddress;
	int id = 7, outgoingSequence = 1, incomingSequence = 0;
	bool unsentFragments = true;
	int unsentFragmentStart = 0, fragmentSequence = -1, fragmentLength = 0;
	int fragmentExpectedLength = -1;
	unsigned int fragmentReceived = 0;
	byte fragmentBuffer[MAX_MESSAGE_SIZE] = {};
	idBitMsg unsentMsg;
	std::vector<byte> received;
	int deliveries = 0;
	void SendNextFragment( idPort &port, const int time );
	bool Process( const netadr_t from, int time, idBitMsg &msg, int &sequence );
	void UpdateOutgoingRate( int, int ) {}
	void UpdateIncomingRate( int, int ) {}
	void UpdatePacketLoss( int, int, int ) {}
	bool ReadMessageData( idBitMsg &, const idBitMsg &msg ) {
		received.assign( msg.GetData(), msg.GetData() + msg.GetSize() ); deliveries++; return true;
	}
};

// PRODUCTION_FUNCTIONS

static int failures = 0;
static void Check( bool passed, const char *label ) {
	if ( !passed ) { std::fprintf( stderr, "FAILED: %s\n", label ); failures++; }
}

static std::vector<byte> Payload( int size, byte seed = 19 ) {
	std::vector<byte> data( size );
	for ( int i = 0; i < size; i++ ) { data[i] = static_cast<byte>( i * 71 + seed ); }
	return data;
}

static idPort Send( int family, int sequence, std::vector<byte> &data ) {
	idMsgChannel sender;
	sender.remoteAddress.type = family;
	sender.outgoingSequence = sequence;
	sender.unsentMsg.Init( data.data(), static_cast<int>( data.size() ) );
	sender.unsentMsg.SetSize( static_cast<int>( data.size() ) );
	idPort port;
	while ( sender.unsentFragments && port.packets.size() < 32 ) { sender.SendNextFragment( port, 0 ); }
	Check( !sender.unsentFragments && sender.outgoingSequence == sequence + 1, "sender finishes and advances once" );
	for ( const auto &packet : port.packets ) {
		Check( packet.size() + ( family == NA_IP6 ? 48 : 28 ) <= ( family == NA_IP6 ? 1280 : 1500 ), "datagrams fit the family's MTU budget" );
	}
	return port;
}

static bool Receive( idMsgChannel &receiver, const std::vector<byte> &packet ) {
	byte storage[MAX_MESSAGE_SIZE + 64] = {};
	idBitMsg msg;
	msg.Init( storage, sizeof( storage ) );
	msg.WriteData( packet.data(), static_cast<int>( packet.size() ) );
	msg.ReadShort(); // the caller consumes the channel id
	int sequence = -1;
	return receiver.Process( receiver.remoteAddress, 0, msg, sequence );
}

static void SetShort( std::vector<byte> &packet, int offset, int value ) {
	packet[offset] = static_cast<byte>( value );
	packet[offset + 1] = static_cast<byte>( static_cast<unsigned int>( value ) >> 8 );
}

static void TestFamily( int family ) {
	netadr_t address; address.type = family;
	const int fragment = Net_FragmentSize( address );
	for ( int size : { 1, fragment - 1, fragment, fragment + 1, 2 * fragment, MAX_MESSAGE_SIZE } ) {
		auto data = Payload( size );
		auto port = Send( family, 1, data );
		Check( port.packets.size() == size_t( size / fragment + 1 ), "exact multiples require an empty terminator" );
		idMsgChannel receiver; receiver.remoteAddress = address;
		for ( size_t i = 0; i < port.packets.size(); i++ ) {
			Check( Receive( receiver, port.packets[i] ) == ( i + 1 == port.packets.size() ), "only the final fragment delivers" );
		}
		Check( receiver.received == data && receiver.deliveries == 1, "payload reconstructed exactly once" );
		for ( const auto &packet : port.packets ) { Check( !Receive( receiver, packet ), "completed message duplicates are rejected" ); }
		idMsgChannel reverse; reverse.remoteAddress = address;
		for ( size_t i = port.packets.size(); i > 0; i-- ) {
			Check( Receive( reverse, port.packets[i - 1] ) == ( i == 1 ), "reverse-order fragments deliver when the last missing piece arrives" );
			Check( !Receive( reverse, port.packets[i - 1] ), "duplicate fragments never deliver twice" );
		}
		Check( reverse.received == data && reverse.deliveries == 1, "tail-first assembly preserves the entire payload" );
	}

	auto oldData = Payload( fragment + 31 );
	auto newData = Payload( fragment * 2 + 17, 91 );
	auto old = Send( family, 1, oldData );
	auto newer = Send( family, 2, newData );
	idMsgChannel receiver; receiver.remoteAddress = address;
	Check( !Receive( receiver, old.packets[0] ), "old assembly begins" );
	Check( !Receive( receiver, newer.packets[0] ), "new assembly supersedes it" );
	auto duplicate = newer.packets[0]; duplicate[10] ^= 0xff;
	Check( !Receive( receiver, duplicate ), "duplicate new head cannot overwrite accepted bytes" );
	Check( !Receive( receiver, old.packets[1] ), "delayed old tail is ignored" );
	Check( !Receive( receiver, old.packets[0] ), "delayed old head is ignored" );
	std::vector<byte> delayedPlain( 7 );
	idBitMsg delayed; delayed.Init( delayedPlain.data(), static_cast<int>( delayedPlain.size() ) );
	delayed.WriteShort( 7 ); delayed.WriteLong( 1 ); delayed.SetSize( 7 );
	Check( !Receive( receiver, delayedPlain ), "older unfragmented message cannot overwrite a newer assembly" );
	Check( !Receive( receiver, newer.packets[2] ), "out-of-order tail waits for the missing middle" );
	Check( Receive( receiver, newer.packets[1] ), "middle completes the new message despite reordered old fragments" );
	Check( !Receive( receiver, newer.packets[2] ), "a duplicate tail cannot deliver again" );
	Check( receiver.received == newData, "interleaved sequences never corrupt the new payload" );

	for ( int invalidCase = 0; invalidCase < 5; invalidCase++ ) {
		idMsgChannel invalid; invalid.remoteAddress = address;
		auto bad = newer.packets[0];
		switch ( invalidCase ) {
			case 0: SetShort( bad, 6, -1 ); break;
			case 1: SetShort( bad, 8, -1 ); break;
			case 2: SetShort( bad, 6, 1 ); break;
			case 3: SetShort( bad, 6, ( MAX_MESSAGE_SIZE / fragment ) * fragment ); break;
			case 4: bad.pop_back(); break;
		}
		Check( !Receive( invalid, bad ) && invalid.fragmentSequence == -1 && invalid.fragmentReceived == 0,
			"negative, misaligned, over-capacity and truncated fragments cannot mutate assembly" );
	}
	for ( bool tailFirst : { false, true } ) {
		idMsgChannel conflict; conflict.remoteAddress = address;
		Receive( conflict, newer.packets[tailFirst ? 2 : 1] );
		auto falseEnd = newer.packets[2]; SetShort( falseEnd, 6, 0 );
		Check( !Receive( conflict, falseEnd ), "conflicting terminal fragment refused" );
		Check( conflict.fragmentExpectedLength == ( tailFirst ? static_cast<int>( newData.size() ) : -1 ), "conflicting end cannot replace the final length" );
		Receive( conflict, newer.packets[0] );
		Check( Receive( conflict, newer.packets[tailFirst ? 1 : 2] ) && conflict.received == newData,
			"valid fragments recover after a conflicting terminal fragment" );
	}

	// An overlarge first fragment must not be mistaken for a final fragment.
	std::vector<byte> oversized( fragment + 11, 0 );
	idBitMsg malformed; malformed.Init( oversized.data(), static_cast<int>( oversized.size() ) );
	malformed.WriteShort( 7 ); malformed.WriteLong( static_cast<int>( FRAGMENT_BIT | 3u ) );
	malformed.WriteShort( 0 ); malformed.WriteShort( fragment + 1 );
	malformed.SetSize( static_cast<int>( oversized.size() ) );
	Check( !Receive( receiver, oversized ), "fragment above the family limit is refused" );
	Check( receiver.deliveries == 1, "invalid fragment cannot publish a payload" );
	Check( receiver.fragmentSequence == 2, "invalid fragment cannot replace the assembly state" );

	for ( int bytes = 2; bytes < 10; bytes++ ) {
		auto runt = newer.packets[0]; runt.resize( bytes );
		Check( !Receive( receiver, runt ), "truncated headers are refused" );
	}

	// Unfragmented duplicates use the same sequence gate without the fragment bitmap.
	std::vector<byte> plain( 7 );
	idBitMsg packet; packet.Init( plain.data(), static_cast<int>( plain.size() ) );
	packet.WriteShort( 7 ); packet.WriteLong( 4 ); packet.SetSize( 7 );
	Check( Receive( receiver, plain ), "unfragmented message after fragments delivers" );
	Check( !Receive( receiver, plain ) && receiver.deliveries == 2, "unfragmented duplicates cannot deliver twice" );
}

int main() {
	TestFamily( NA_IP ); TestFamily( NA_IP6 );
	return failures ? 1 : 0;
}
