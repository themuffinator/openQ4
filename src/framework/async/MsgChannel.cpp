/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#include "MsgChannel.h"

/*

packet header
-------------
2 bytes		id
4 bytes		outgoing sequence. high bit will be set if this is a fragmented message.
2 bytes		tag: the sequence keyed with the connection's challenge (Net_PacketTag).
2 bytes		optional fragment start byte if fragment bit is set.
2 bytes		optional fragment length if fragment bit is set. if < FRAGMENT_SIZE, this is the last fragment.

If the id is -1, the packet should be handled as an out-of-band
message instead of as part of the message channel.

All fragments will have the same sequence numbers.

*/


#define	MAX_PACKETLEN			1400		// max size of a network packet
// IPv6 guarantees only a 1280 byte path MTU and forbids routers from
// fragmenting in transit: an oversized datagram is dropped and answered with an
// ICMPv6 Packet Too Big that a filtering path frequently never delivers. A
// 1400 byte fragment would then be silently black-holed while small packets
// kept flowing, which reads as "connects, then hangs awaiting snapshot".
#define	MAX_PACKETLEN6			1232		// 1280 minus the 40 byte IPv6 and 8 byte UDP headers
// openQ4: one fragment size for both families (protocol 2.42). IPv4 used 1,300 bytes, and
// each end took the size from the family it saw the other end by. Behind NAT64 a client
// sees an IPv6 address and the server an IPv4 one, so they disagreed on which fragment is
// the last and no fragmented message arrived; and over IPv4 a path that drops IP fragments
// (a tunnel, an MTU of 1280 to 1400) lost every full fragment. 1,132 bytes fits 1280 on
// either family.
#define	FRAGMENT_SIZE			(MAX_PACKETLEN6 - 100)
// the bytes ahead of a packet's message: id, sequence and tag
#define	PACKET_HEADER_SIZE		8
#define	FRAGMENT_BIT			(1u<<31)

// openQ4: a reliable message beyond this fits no packet (SendReliableMessage)
#define	MAX_RELIABLE_MESSAGE_SIZE	( MAX_MESSAGE_SIZE - 64 )
// the share of a message its sender leaves for queued reliable messages
// (GetMaxSendMessageSize)
#define	RELIABLE_SEND_RESERVE		( MAX_MESSAGE_SIZE / 4 )

/*
===============
Net_FragmentSize

Both endpoints of a connection must agree on the fragment size: the receiver
detects the final fragment by comparing its length against this value. It is
the same for every address family.
===============
*/
static int Net_FragmentSize( const netadr_t &adr ) {
	return FRAGMENT_SIZE;
}

/*
===============
Net_PacketTag

openQ4: what a channel packet carries beside its sequence to show it comes from the
other end of this connection: the sequence mixed with the challenge the server
issued for the connect, which both ends hold and no packet carries in the clear.
A packet used to be accepted on the sender's address (without its port) and a
15-bit id, so at most 32,767 packets sent with a player's address and a far-ahead
sequence made every real packet "out of order" and ended that player's
connection. It is a keyed mix, not a cipher: it stops a sender who cannot see the
connection's traffic, which is the one who could do this.
===============
*/
static unsigned short Net_PacketTag( const int key, const int sequenceWord ) {
	unsigned int hash = static_cast<unsigned int>( key ) * 0x9E3779B1u;
	hash ^= static_cast<unsigned int>( sequenceWord ) + 0x7F4A7C15u + ( hash << 6 ) + ( hash >> 2 );
	hash *= 0x85EBCA6Bu;
	hash ^= hash >> 13;
	hash *= 0xC2B2AE35u;
	hash ^= hash >> 16;
	return static_cast<unsigned short>( hash & 0xFFFFu );
}

idCVar net_channelShowPackets( "net_channelShowPackets", "0", CVAR_SYSTEM | CVAR_BOOL, "show all packets" );
idCVar net_channelShowDrop( "net_channelShowDrop", "0", CVAR_SYSTEM | CVAR_BOOL, "show dropped packets" );

/*
===============
idMsgChannel::idMsgChannel
===============
*/
idMsgChannel::idMsgChannel() {
	id = -1;
	key = 0;
	compressor = NULL;
}

/*
==============
idMsgChannel::Init

  Opens a channel to a remote system.
==============
*/
void idMsgChannel::Init( const netadr_t adr, const int id, const int key ) {
	this->remoteAddress = adr;
	this->id = id;
	this->key = key;
	this->maxRate = 50000;
	// openQ4: a channel is opened again without a Shutdown when a client
	// reconnects onto its old slot, when a listen server changes map and when a
	// pure restart reconnects, and each used to leak the previous compressor
	// and its 64 KiB buffer. The compressor is re-initialised per message, so
	// the one already here serves.
	if ( this->compressor == NULL ) {
		this->compressor = idCompressor::AllocRunLength_ZeroBased();
	}

	lastSendTime = 0;
	lastDataBytes = 0;
	outgoingRateTime = 0;
	outgoingRateBytes = 0;
	incomingRateTime = 0;
	incomingRateBytes = 0;
	incomingReceivedPackets = 0.0f;
	incomingDroppedPackets = 0.0f;
	incomingPacketLossTime = 0;
	outgoingCompression = 0.0f;
	incomingCompression = 0.0f;
	outgoingSequence = 1;
	incomingSequence = 0;
	unsentFragments = false;
	unsentFragmentStart = 0;
	fragmentSequence = 0;
	fragmentLength = 0;
	fragmentExpectedLength = -1;
	fragmentReceived = 0;
	reliableSend.Init( 1 );
	reliableReceive.Init( 0 );
}

/*
===============
idMsgChannel::Shutdown
================
*/
void idMsgChannel::Shutdown( void ) {
	delete compressor;
	compressor = NULL;
}

/*
=================
idMsgChannel::ResetRate
=================
*/
void idMsgChannel::ResetRate( void ) {
	lastSendTime = 0;
	lastDataBytes = 0;
	outgoingRateTime = 0;
	outgoingRateBytes = 0;
	incomingRateTime = 0;
	incomingRateBytes = 0;
}

/*
=================
idMsgChannel::ReadyToSend
=================
*/
bool idMsgChannel::ReadyToSend( const int time ) const {
	int deltaTime;

	if ( !maxRate ) {
		return true;
	}
	deltaTime = time - lastSendTime;
	if ( deltaTime > 1000 ) {
		return true;
	}
	// openQ4: the rate cvars are unbounded, and above 2,147,483 bytes a second the
	// product left an int and stalled the channel for up to a second.
	return ( ( lastDataBytes - ( static_cast<int64>( deltaTime ) * maxRate ) / 1000 ) <= 0 );
}

/*
=================
idMsgChannel::ReadyToSendFragment

openQ4: whether the next fragment of a message already under way may leave now.
ReadyToSend makes each packet wait until the one before it is paid for, which for
a fragmented message meant one fragment a server frame: a 2.9 KB snapshot reached
its client three frames after it was taken, 33 ms late and that much older than
its header says, and the wait counted against the next snapshot too. The rest of
a message may follow at once while the debt stays within burstMsec of the rate.
Every fragment is still charged, so the average rate holds, and a rate too low
for the burst keeps the old pacing.
=================
*/
bool idMsgChannel::ReadyToSendFragment( const int time, const int burstMsec ) const {
	if ( !maxRate ) {
		return true;
	}
	const int deltaTime = time - lastSendTime;
	if ( deltaTime > 1000 || deltaTime < 0 ) {
		return true;
	}
	const int64 debt = Max<int64>( 0, int64( lastDataBytes ) - int64( deltaTime ) * maxRate / 1000 );
	if ( debt <= 0 ) {
		return true;
	}
	const int64 budget = int64( maxRate ) * idMath::ClampInt( 0, 1000, burstMsec ) / 1000;
	return debt + Net_FragmentSize( remoteAddress ) + 10 <= budget;
}

/*
===============
idMsgChannel::ReliableSendSize

openQ4: the bytes of queued reliable messages a message with this much data
carries: all of them when they fit, otherwise the longest run of whole messages
from the front of the queue. The receiver takes reliable messages in order and
acknowledges the last one it has, so a front run is delivered and the rest
follows. SendMessage used to refuse the whole packet once the queue and the data
together passed MAX_MESSAGE_SIZE: above about 15 KiB queued no snapshot could
leave, and above 16,371 bytes nothing could, not even the empty message that
would have delivered the queue, so the connection sat silent until it timed out.
===============
*/
int idMsgChannel::ReliableSendSize( const int dataBytes ) const {
	return ReliablePrefixSize( MAX_MESSAGE_SIZE - 8 - dataBytes );
}

/*
===============
idMsgChannel::ReliablePrefixSize

Bytes of the whole queued reliable messages, from the front, that fit budget.
===============
*/
int idMsgChannel::ReliablePrefixSize( const int budget ) const {
	const int total = reliableSend.GetTotalSize();
	if ( total <= budget ) {
		return total;
	}
	if ( budget < 6 ) {
		return 0;
	}
	// each record is a 16 bit size, a 32 bit sequence and the message (idMsgQueue::Add)
	byte queued[MAX_MSG_QUEUE_SIZE];
	reliableSend.CopyToBuffer( queued );
	int used = 0;
	while ( used + 6 <= total ) {
		const int record = 6 + ( queued[used] | ( queued[used + 1] << 8 ) );
		if ( record <= 6 || record > total - used || record > budget - used ) {
			break;
		}
		used += record;
	}
	return used;
}

/*
===============
idMsgChannel::WriteMessageData
================
*/
bool idMsgChannel::WriteMessageData( idBitMsg &out, const idBitMsg &msg ) {
	idBitMsg tmp;
	byte tmpBuf[MAX_MESSAGE_SIZE];
	idBitMsg packed;
	// openQ4: the zero-run coder spends six bits on a lone three-bit zero word,
	// so data chosen for it grows - 0x88 bytes by a quarter, and never by more
	// than double. Packing straight into the caller's MAX_MESSAGE_SIZE buffer
	// made a 13 KiB backlog of such bytes a fatal idBitMsg overflow, which a
	// client could arrange in a server's queues. Pack here first and refuse
	// what does not fit.
	byte packedBuf[MAX_MESSAGE_SIZE * 2 + 64];
	byte queued[MAX_MSG_QUEUE_SIZE];

	if ( msg.GetSize() > MAX_MESSAGE_SIZE - 8 ) {
		return false;
	}

	// openQ4: carry the reliable messages that fit beside the data (ReliableSendSize).
	// Packing can grow them past the buffer; then carry fewer, down to none, so the
	// data and whatever of the queue still fits go out and the queue keeps draining.
	// One refusal here used to stop every later packet as well, with the queue still
	// in the way.
	int reliableBytes = ReliableSendSize( msg.GetSize() );
	if ( reliableBytes > 0 ) {
		reliableSend.CopyToBuffer( queued );
	}

	while ( true ) {
		tmp.Init( tmpBuf, sizeof( tmpBuf ) );

		// write acknowledgement of last received reliable message
		tmp.WriteLong( reliableReceive.GetLast() );

		// write reliable messages
		if ( reliableBytes > 0 ) {
			tmp.WriteData( queued, reliableBytes );
		}
		tmp.WriteShort( 0 );

		// write data
		tmp.WriteData( msg.GetData(), msg.GetSize() );

		// compress message
		packed.Init( packedBuf, sizeof( packedBuf ) );
		idFile_BitMsg file( packed );
		compressor->Init( &file, true, 3 );
		compressor->Write( tmp.GetData(), tmp.GetSize() );
		compressor->FinishCompress();
		outgoingCompression = compressor->GetCompressionRatio();

		// the receiver reassembles into MAX_MESSAGE_SIZE as well
		if ( !( (int)sizeof( short ) + packed.GetSize() > out.GetMaxSize() - out.GetSize() ) ) {
			break;
		}
		if ( reliableBytes <= 0 ) {
			return false;
		}
		reliableBytes = ReliablePrefixSize( reliableBytes / 2 );
	}

	// write message size
	out.WriteShort( tmp.GetSize() );
	out.WriteData( packed.GetData(), packed.GetSize() );
	return true;
}

/*
===============
idMsgChannel::ReadMessageData
================
*/
bool idMsgChannel::ReadMessageData( idBitMsg &out, const idBitMsg &msg ) {
	int reliableAcknowledge, reliableMessageSize, reliableSequence, messageSize;

	// read message size
	messageSize = msg.ReadShort();
	if ( messageSize < (int)sizeof( int ) + (int)sizeof( short ) || messageSize > out.GetMaxSize() ) {
		common->Printf( "%s: bad message size %d\n", Sys_NetAdrToString( remoteAddress ), messageSize );
		return false;
	}
	// openQ4: the zero-run coder turns six bits into at most twenty-four, so a packet
	// decodes to no more than four times its packed bytes. A size beyond that used to
	// be decoded anyway, from zero fill: eight bytes claiming a 16 KiB message cost the
	// receiver about 131,000 bit reads, the result parsed as an empty message, and the
	// sender was never dropped for it.
	if ( messageSize > 4 * msg.GetRemaingData() + 8 ) {
		common->Printf( "%s: message size %d exceeds its %d packed bytes\n", Sys_NetAdrToString( remoteAddress ),
			messageSize, msg.GetRemaingData() );
		return false;
	}
	out.SetSize( messageSize );

	// decompress message
	idFile_BitMsg file( msg );
	compressor->Init( &file, false, 3 );
	const int decompressedBytes = compressor->Read( out.GetData(), out.GetSize() );
	// Bitstream decompressors report their cursor, which can step past the logical
	// output size on the final partial word.
	if ( decompressedBytes < out.GetSize() ) {
		common->Printf( "%s: truncated compressed message\n", Sys_NetAdrToString( remoteAddress ) );
		return false;
	}
	incomingCompression = compressor->GetCompressionRatio();
	out.BeginReading();

	// read acknowledgement of sent reliable messages
	reliableAcknowledge = out.ReadLong();

	// remove acknowledged reliable messages
	while( reliableSend.GetFirst() <= reliableAcknowledge ) {
		if ( !reliableSend.Get( NULL, 0, reliableMessageSize, true ) ) {
			break;
		}
	}

	// read reliable messages
	reliableMessageSize = out.ReadShort();
	while( reliableMessageSize != 0 ) {
		const int remaining = out.GetSize() - out.GetReadCount();
		if ( reliableMessageSize <= 0 || remaining < (int)sizeof( int ) || reliableMessageSize > remaining - (int)sizeof( int ) ) {
			common->Printf( "%s: bad reliable message\n", Sys_NetAdrToString( remoteAddress ) );
			return false;
		}
		reliableSequence = out.ReadLong();
		if ( reliableSequence == reliableReceive.GetLast() + 1 ) {
			reliableReceive.Add( out.GetData() + out.GetReadCount(), reliableMessageSize, false );
		}
		out.ReadData( NULL, reliableMessageSize );
		reliableMessageSize = out.ReadShort();
	}

	return true;
}

/*
=================
idMsgChannel::SendNextFragment

  Sends one fragment of the current message.
=================
*/
void idMsgChannel::SendNextFragment( idPort &port, const int time ) {
	idBitMsg	msg;
	byte		msgBuf[MAX_PACKETLEN];
	int			fragLength;

	if ( remoteAddress.type == NA_BAD ) {
		return;
	}

	if ( !unsentFragments ) {
		return;
	}

	// write the packet
	msg.Init( msgBuf, sizeof( msgBuf ) );
	const int sequenceWord = static_cast<int>( static_cast<unsigned int>( outgoingSequence ) | FRAGMENT_BIT );
	msg.WriteShort( id );
	msg.WriteLong( sequenceWord );
	msg.WriteUShort( Net_PacketTag( key, sequenceWord ) );

	fragLength = Net_FragmentSize( remoteAddress );
	if ( unsentFragmentStart + fragLength > unsentMsg.GetSize() ) {
		fragLength = unsentMsg.GetSize() - unsentFragmentStart;
	}

	msg.WriteShort( unsentFragmentStart );
	msg.WriteShort( fragLength );
	msg.WriteData( unsentMsg.GetData() + unsentFragmentStart, fragLength );

	// send the packet
	port.SendPacket( remoteAddress, msg.GetData(), msg.GetSize() );

	// update rate control variables
	UpdateOutgoingRate( time, msg.GetSize() );

	if ( net_channelShowPackets.GetBool() ) {
		common->Printf( "%d send %4i : s = %i fragment = %i,%i\n", id, msg.GetSize(), outgoingSequence - 1, unsentFragmentStart, fragLength );
	}

	unsentFragmentStart += fragLength;

	// this exit condition is a little tricky, because a packet
	// that is exactly the fragment length still needs to send
	// a second packet of zero length so that the other side
	// can tell there aren't more to follow
	if ( unsentFragmentStart == unsentMsg.GetSize() && fragLength != Net_FragmentSize( remoteAddress ) ) {
		outgoingSequence++;
		unsentFragments = false;
	}
}

/*
===============
idMsgChannel::GetMaxSendMessageSize

  How large a message SendMessage will still accept right now.
================
*/
int idMsgChannel::GetMaxSendMessageSize( void ) const {
	// mirrors the totalLength test in SendMessage below. openQ4: a message no longer
	// has to leave room for the whole reliable queue (ReliableSendSize), only for
	// the queue's next few messages, so the queue still drains under large payloads.
	const int overhead = 4 + ReliablePrefixSize( RELIABLE_SEND_RESERVE ) + 4;
	return Max( 0, MAX_MESSAGE_SIZE - overhead );
}

bool idMsgChannel::CanSendUnfragmented( int time, int payloadBytes, int burstMsec ) const {
	const int64 bytes = 8 + int64( reliableSend.GetTotalSize() ) + payloadBytes;
	if ( payloadBytes < 0 || unsentFragments || bytes >= Net_FragmentSize( remoteAddress ) ) return false;
	if ( burstMsec <= 0 || !maxRate ) return ReadyToSend( time );
	const int elapsed = idMath::ClampInt( 0, 1000, time - lastSendTime );
	const int64 debt = Max<int64>( 0, int64( lastDataBytes ) - int64( elapsed ) * maxRate / 1000 );
	return debt + bytes <= int64( maxRate ) * idMath::ClampInt( 0, 20, burstMsec ) / 1000;
}

/*
===============
idMsgChannel::SendMessage

  Sends a message to a connection, fragmenting if necessary
  A 0 length will still generate a packet.
================
*/
int idMsgChannel::SendMessage( idPort &port, const int time, const idBitMsg &msg ) {
	int totalLength;

	// openQ4: a bot slot has a channel but no remote end.  Without this every
	// unreliable send aimed at one reached idPort::SendPacket and produced an
	// "unsupported address type" warning - a wall of them on every map load.
	if ( remoteAddress.type == NA_BAD || remoteAddress.type == NA_BOT ) {
		return -1;
	}

	if ( unsentFragments ) {
		common->Error( "idMsgChannel::SendMessage: called with unsent fragments left" );
		return -1;
	}

	// openQ4: the message's own sequence. The fragment paths below returned
	// outgoingSequence after the send, which is already the next message's when the
	// packed form fits one short fragment, so a lost game-init was noticed one
	// resend late.
	const int sequence = outgoingSequence;

	totalLength = 4 + ReliableSendSize( msg.GetSize() ) + 4 + msg.GetSize();

	if ( totalLength > MAX_MESSAGE_SIZE ) {
		common->Printf( "idMsgChannel::SendMessage: message too large, length = %i\n", totalLength );
		return -1;
	}

	unsentMsg.Init( unsentBuffer, sizeof( unsentBuffer ) );
	unsentMsg.BeginWriting();

	// fragment large messages
	if ( totalLength >= Net_FragmentSize( remoteAddress ) ) {
		// write out the message data
		if ( !WriteMessageData( unsentMsg, msg ) ) {
			common->Printf( "idMsgChannel::SendMessage: message too large once packed, length = %i\n", totalLength );
			return -1;
		}

		unsentFragments = true;
		unsentFragmentStart = 0;

		// send the first fragment now
		SendNextFragment( port, time );

		return sequence;
	}

	// write the header
	unsentMsg.WriteShort( id );
	unsentMsg.WriteLong( outgoingSequence );
	unsentMsg.WriteUShort( Net_PacketTag( key, outgoingSequence ) );

	// write out the message data
	// openQ4: the length test above is taken before packing, and packing can grow
	// a message (WriteMessageData). One that no longer fits a fragment would leave
	// as a datagram larger than the family allows, over IPv6's 1232 bytes a size a
	// 1280-byte path drops. Fragment it instead; receivers assemble either form.
	if ( !WriteMessageData( unsentMsg, msg ) || unsentMsg.GetSize() - PACKET_HEADER_SIZE >= Net_FragmentSize( remoteAddress ) ) {
		unsentMsg.BeginWriting();
		if ( !WriteMessageData( unsentMsg, msg ) ) {
			common->Printf( "idMsgChannel::SendMessage: message too large once packed, length = %i\n", totalLength );
			return -1;
		}
		unsentFragments = true;
		unsentFragmentStart = 0;
		SendNextFragment( port, time );
		return sequence;
	}

	// send the packet
	port.SendPacket( remoteAddress, unsentMsg.GetData(), unsentMsg.GetSize() );

	// update rate control variables
	UpdateOutgoingRate( time, unsentMsg.GetSize() );

	if ( net_channelShowPackets.GetBool() ) {
		common->Printf( "%d send %4i : s = %i ack = %i\n", id, unsentMsg.GetSize(), outgoingSequence - 1, incomingSequence );
	}

	outgoingSequence++;

	return sequence;
}

/*
=================
idMsgChannel::Process

  Returns false if the message should not be processed due to being out of order or a fragment.

  msg must be large enough to hold MAX_MESSAGE_SIZE, because if this is the final
  fragment of a multi-part message, the entire thing will be copied out.
=================
*/
bool idMsgChannel::Process( const netadr_t from, int time, idBitMsg &msg, int &sequence ) {
	int			fragStart, fragLength, dropped;
	bool		fragmented;
	idBitMsg	fragMsg;

	// get sequence numbers
	if ( msg.GetRemaingData() < (int)sizeof( int ) + (int)sizeof( short ) ) {
		// openQ4: this runs before the tag check below, so anyone can draw it
		if ( net_channelShowDrop.GetBool() || net_channelShowPackets.GetBool() ) {
			common->Printf( "%s: runt packet\n", Sys_NetAdrToString( remoteAddress ) );
		}
		return false;
	}
	sequence = msg.ReadLong();

	// openQ4: a packet without this connection's tag is not from its other end
	// (Net_PacketTag). Nothing about the channel changes for one: not the port below,
	// the rates, nor the sequence, which is what such a packet was sent to move.
	if ( msg.ReadUShort() != Net_PacketTag( key, sequence ) ) {
		if ( net_channelShowDrop.GetBool() || net_channelShowPackets.GetBool() ) {
			common->Printf( "%s: packet without this connection's tag\n", Sys_NetAdrToString( from ) );
		}
		return false;
	}

	// the IP port can't be used to differentiate them, because
	// some address translating routers periodically change UDP
	// port assignments
	if ( remoteAddress.port != from.port ) {
		common->Printf( "idMsgChannel::Process: fixing up a translated port\n" );
		remoteAddress.port = from.port;
	}

	// update incoming rate
	UpdateIncomingRate( time, msg.GetSize() );

	// check for fragment information
	const unsigned int sequenceBits = static_cast<unsigned int>( sequence );
	if ( sequenceBits & FRAGMENT_BIT ) {
		sequence = static_cast<int>( sequenceBits & ~FRAGMENT_BIT );
		fragmented = true;
	} else {
		fragmented = false;
	}

	// read the fragment information
	if ( fragmented ) {
		if ( msg.GetRemaingData() < (int)sizeof( short ) * 2 ) {
			common->Printf( "%s: runt fragment header\n", Sys_NetAdrToString( remoteAddress ) );
			return false;
		}
		fragStart = msg.ReadShort();
		fragLength = msg.ReadShort();
	} else {
		fragStart = 0;		// stop warning message
		fragLength = 0;
	}

	if ( net_channelShowPackets.GetBool() ) {
		if ( fragmented ) {
			common->Printf( "%d recv %4i : s = %i fragment = %i,%i\n", id, msg.GetSize(), sequence, fragStart, fragLength );
		} else {
			common->Printf( "%d recv %4i : s = %i\n", id, msg.GetSize(), sequence );
		}
	}

	//
	// discard out of order or duplicated packets
	//
	if ( sequence <= incomingSequence ) {
		if ( net_channelShowDrop.GetBool() || net_channelShowPackets.GetBool() ) {
			common->Printf( "%s: out of order packet %i at %i\n", Sys_NetAdrToString( remoteAddress ),  sequence, incomingSequence );
		}
		return false;
	}

	// An older message must not overwrite the assembly buffer of a newer one,
	// whether the delayed packet is fragmented or carries its entire payload.
	if ( sequence < fragmentSequence ) {
		return false;
	}

	//
	// dropped packets don't keep this message from being used
	//
	dropped = sequence - (incomingSequence+1);
	if ( dropped > 0 ) {
		if ( net_channelShowDrop.GetBool() || net_channelShowPackets.GetBool() ) {
			common->Printf( "%s: dropped %i packets at %i\n", Sys_NetAdrToString( remoteAddress ), dropped, sequence );
		}
		UpdatePacketLoss( time, 0, dropped );
	}

	//
	// if the message is fragmented
	//
	if ( fragmented ) {
		const int fragmentSize = Net_FragmentSize( remoteAddress );
		if ( fragStart < 0 || fragLength < 0 || fragLength > Net_FragmentSize( remoteAddress ) || fragLength > msg.GetRemaingData() || fragStart + fragLength > (int)sizeof( fragmentBuffer ) || fragStart % fragmentSize != 0 || fragStart / fragmentSize >= 32 ) {
			if ( net_channelShowDrop.GetBool() || net_channelShowPackets.GetBool() ) {
				common->Printf( "%s: illegal fragment range\n", Sys_NetAdrToString( remoteAddress ) );
			}
			UpdatePacketLoss( time, 0, 1 );
			return false;
		}

		// make sure we have the correct sequence number
		if ( sequence != fragmentSequence ) {
			fragmentSequence = sequence;
			fragmentLength = 0;
			fragmentExpectedLength = -1;
			fragmentReceived = 0;
		}

		const unsigned int fragmentBit = 1u << ( fragStart / fragmentSize );
		if ( fragmentReceived & fragmentBit ) {
			// A duplicate cannot count twice or overwrite an accepted fragment.
			return false;
		}

		const int fragmentEnd = fragStart + fragLength;
		if ( fragmentExpectedLength >= 0 && fragmentEnd > fragmentExpectedLength ) {
			return false;
		}
		if ( fragLength < fragmentSize ) {
			// The final fragment may arrive first, including an empty terminator.
			// Refuse a conflicting second end or an end before buffered data.
			if ( fragmentExpectedLength >= 0 || ( fragmentReceived & ~( fragmentBit | ( fragmentBit - 1u ) ) ) != 0 ) {
				return false;
			}
			fragmentExpectedLength = fragmentEnd;
		}

		memcpy( fragmentBuffer + fragStart, msg.GetData() + msg.GetReadCount(), fragLength );
		fragmentReceived |= fragmentBit;
		fragmentLength += fragLength;
		UpdatePacketLoss( time, 1, 0 );

		// All non-final fragments have the family's fixed size and distinct,
		// aligned offsets. Their byte sum reaches the final length only when
		// every preceding fragment arrived, in any order and without duplicates.
		if ( fragmentExpectedLength < 0 || fragmentLength != fragmentExpectedLength ) {
			return false;
		}

	} else {
		if ( msg.GetRemaingData() > (int)sizeof( fragmentBuffer ) ) {
			if ( net_channelShowDrop.GetBool() || net_channelShowPackets.GetBool() ) {
				common->Printf( "%s: packet payload too large\n", Sys_NetAdrToString( remoteAddress ) );
			}
			UpdatePacketLoss( time, 0, 1 );
			return false;
		}
		memcpy( fragmentBuffer, msg.GetData() + msg.GetReadCount(), msg.GetRemaingData() );
		fragmentLength = msg.GetRemaingData();
		UpdatePacketLoss( time, 1, 0 );
	}

	fragMsg.Init( fragmentBuffer, fragmentLength );
	fragMsg.SetSize( fragmentLength );
	fragMsg.BeginReading();

	incomingSequence = sequence;

	// read the message data
	if ( !ReadMessageData( msg, fragMsg ) ) {
		return false;
	}

	return true;
}

/*
=================
idMsgChannel::SendReliableMessage
=================
*/
bool idMsgChannel::SendReliableMessage( const idBitMsg &msg ) {
	bool result;

// jmarshall
	if (remoteAddress.type == NA_BOT)
		return false;
// jmarshall end

	assert( remoteAddress.type != NA_BAD );
	if ( remoteAddress.type == NA_BAD ) {
		return false;
	}
	// openQ4: the queue takes a message of up to 16,375 bytes, but one over
	// MAX_RELIABLE_MESSAGE_SIZE fits no packet beside its record header and the
	// smallest payload; it sat at the front of the queue for good, with everything
	// behind it.
	result = msg.GetSize() <= MAX_RELIABLE_MESSAGE_SIZE && reliableSend.Add( msg.GetData(), msg.GetSize(), true );
	if ( !result ) {
		common->Warning( "idMsgChannel::SendReliableMessage: overflowed" );
		return false;
	}
	return result;
}

/*
=================
idMsgChannel::GetReliableMessage
=================
*/
bool idMsgChannel::GetReliableMessage( idBitMsg &msg ) {
	int size;
	bool result;
// jmarshall
	result = reliableReceive.Get( msg.GetData(), msg.GetMaxSize(), size, false );
// jmarshall end
	msg.SetSize( size );
	msg.BeginReading();
	return result;
}

/*
===============
idMsgChannel::ClearReliableMessages
================
*/
void idMsgChannel::ClearReliableMessages( void ) {
	reliableSend.Init( 1 );
	reliableReceive.Init( 0 );
}

/*
=================
idMsgChannel::UpdateOutgoingRate
=================
*/
void idMsgChannel::UpdateOutgoingRate( const int time, const int size ) {
	// update the outgoing rate control variables
	int deltaTime = time - lastSendTime;
	if ( deltaTime > 1000 ) {
		lastDataBytes = 0;
	} else {
		const int64 remaining = int64( lastDataBytes ) - ( int64( deltaTime ) * maxRate ) / 1000;
		lastDataBytes = remaining > 0 ? static_cast<int>( remaining ) : 0;
	}
	lastDataBytes += size;
	lastSendTime = time;

	// update outgoing rate variables
	if ( time - outgoingRateTime > 1000 ) {
		outgoingRateBytes -= static_cast<int>( int64( outgoingRateBytes ) * Min( time - outgoingRateTime - 1000, 1000 ) / 1000 );
		if ( outgoingRateBytes < 0 ) {
			outgoingRateBytes = 0;
		}
	}
	outgoingRateTime = time - 1000;
	outgoingRateBytes += size;
}

/*
=================
idMsgChannel::UpdateIncomingRate
=================
*/
void idMsgChannel::UpdateIncomingRate( const int time, const int size ) {
	// update incoming rate variables
	if ( time - incomingRateTime > 1000 ) {
		incomingRateBytes -= static_cast<int>( int64( incomingRateBytes ) * Min( time - incomingRateTime - 1000, 1000 ) / 1000 );
		if ( incomingRateBytes < 0 ) {
			incomingRateBytes = 0;
		}
	}
	incomingRateTime = time - 1000;
	incomingRateBytes += size;
}

/*
=================
idMsgChannel::UpdatePacketLoss
=================
*/
void idMsgChannel::UpdatePacketLoss( const int time, const int numReceived, const int numDropped ) {
	// update incoming packet loss variables
	if ( time - incomingPacketLossTime > 5000 ) {
		float scale = ( time - incomingPacketLossTime - 5000 ) * ( 1.0f / 5000.0f );
		incomingReceivedPackets -= incomingReceivedPackets * scale;
		if ( incomingReceivedPackets < 0.0f ) {
			incomingReceivedPackets = 0.0f;
		}
		incomingDroppedPackets -= incomingDroppedPackets * scale;
		if ( incomingDroppedPackets < 0.0f ) {
			incomingDroppedPackets = 0.0f;
		}
	}
	incomingPacketLossTime = time - 5000;
	incomingReceivedPackets += numReceived;
	incomingDroppedPackets += numDropped;
}

/*
=================
idMsgChannel::GetIncomingPacketLoss
=================
*/
float idMsgChannel::GetIncomingPacketLoss( void ) const {
	if ( incomingReceivedPackets == 0.0f && incomingDroppedPackets == 0.0f ) {
		return 0.0f;
	}
	return incomingDroppedPackets * 100.0f / ( incomingReceivedPackets + incomingDroppedPackets );
}
