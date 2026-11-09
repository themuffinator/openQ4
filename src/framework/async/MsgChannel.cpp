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
2 bytes		optional fragment start byte if fragment bit is set.
2 bytes		optional fragment length if fragment bit is set. if < FRAGMENT_SIZE, this is the last fragment.

If the id is -1, the packet should be handled as an out-of-band
message instead of as part of the message channel.

All fragments will have the same sequence numbers.

*/


#define	MAX_PACKETLEN			1400		// max size of a network packet
#define	FRAGMENT_SIZE			(MAX_PACKETLEN - 100)
// IPv6 guarantees only a 1280 byte path MTU and forbids routers from
// fragmenting in transit: an oversized datagram is dropped and answered with an
// ICMPv6 Packet Too Big that a filtering path frequently never delivers. A
// 1400 byte fragment would then be silently black-holed while small packets
// kept flowing, which reads as "connects, then hangs awaiting snapshot".
#define	MAX_PACKETLEN6			1232		// 1280 minus the 40 byte IPv6 and 8 byte UDP headers
#define	FRAGMENT_SIZE6			(MAX_PACKETLEN6 - 100)
#define	FRAGMENT_BIT			(1u<<31)

/*
===============
Net_FragmentSize

Both endpoints of a connection see the same remote address family, so they
agree on the fragment size without negotiating it. That agreement is required:
the receiver detects the final fragment by comparing its length against this
value.
===============
*/
static int Net_FragmentSize( const netadr_t &adr ) {
	return adr.type == NA_IP6 ? FRAGMENT_SIZE6 : FRAGMENT_SIZE;
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
	compressor = NULL;
}

/*
==============
idMsgChannel::Init

  Opens a channel to a remote system.
==============
*/
void idMsgChannel::Init( const netadr_t adr, const int id ) {
	this->remoteAddress = adr;
	this->id = id;
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
	return ( ( lastDataBytes - ( deltaTime * maxRate ) / 1000 ) <= 0 );
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

	tmp.Init( tmpBuf, sizeof( tmpBuf ) );

	// write acknowledgement of last received reliable message
	tmp.WriteLong( reliableReceive.GetLast() );

	// write reliable messages
	reliableSend.CopyToBuffer( tmp.GetData() + tmp.GetSize() );
	tmp.SetSize( tmp.GetSize() + reliableSend.GetTotalSize() );
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
	if ( (int)sizeof( short ) + packed.GetSize() > out.GetMaxSize() - out.GetSize() ) {
		return false;
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
	msg.WriteShort( id );
	msg.WriteLong( static_cast<int>( static_cast<unsigned int>( outgoingSequence ) | FRAGMENT_BIT ) );

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
	// mirrors the totalLength test in SendMessage below
	const int overhead = 4 + reliableSend.GetTotalSize() + 4;
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

	totalLength = 4 + reliableSend.GetTotalSize() + 4 + msg.GetSize();

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

		return outgoingSequence;
	}

	// write the header
	unsentMsg.WriteShort( id );
	unsentMsg.WriteLong( outgoingSequence );

	// write out the message data
	WriteMessageData( unsentMsg, msg );

	// send the packet
	port.SendPacket( remoteAddress, unsentMsg.GetData(), unsentMsg.GetSize() );

	// update rate control variables
	UpdateOutgoingRate( time, unsentMsg.GetSize() );

	if ( net_channelShowPackets.GetBool() ) {
		common->Printf( "%d send %4i : s = %i ack = %i\n", id, unsentMsg.GetSize(), outgoingSequence - 1, incomingSequence );
	}

	outgoingSequence++;

	return ( outgoingSequence - 1 );
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

	// the IP port can't be used to differentiate them, because
	// some address translating routers periodically change UDP
	// port assignments
	if ( remoteAddress.port != from.port ) {
		common->Printf( "idMsgChannel::Process: fixing up a translated port\n" );
		remoteAddress.port = from.port;
	}

	// update incoming rate
	UpdateIncomingRate( time, msg.GetSize() );

	// get sequence numbers
	if ( msg.GetRemaingData() < (int)sizeof( int ) ) {
		common->Printf( "%s: runt packet\n", Sys_NetAdrToString( remoteAddress ) );
		return false;
	}
	sequence = msg.ReadLong();

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
	result = reliableSend.Add( msg.GetData(), msg.GetSize(), true );
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
		lastDataBytes -= ( deltaTime * maxRate ) / 1000;
		if ( lastDataBytes < 0 ) {
			lastDataBytes = 0;
		}
	}
	lastDataBytes += size;
	lastSendTime = time;

	// update outgoing rate variables
	if ( time - outgoingRateTime > 1000 ) {
		outgoingRateBytes -= outgoingRateBytes * ( time - outgoingRateTime - 1000 ) / 1000;
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
		incomingRateBytes -= incomingRateBytes * ( time - incomingRateTime - 1000 ) / 1000;
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
