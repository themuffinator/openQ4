



/*
==============================================================================

  idBitMsg

==============================================================================
*/

/*
================
idBitMsg::idBitMsg
================
*/
idBitMsg::idBitMsg() {
	writeData = NULL;
	readData = NULL;
	maxSize = 0;
	curSize = 0;
	writeBit = 0;
	readCount = 0;
	readBit = 0;
	allowOverflow = false;
	overflowed = false;
}

/*
================
idBitMsg::CheckOverflow
================
*/
bool idBitMsg::CheckOverflow( int numBits ) {
	assert( numBits >= 0 );
	if ( numBits > GetRemainingWriteBits() ) {
		if ( !allowOverflow ) {
			idLib::common->FatalError( "idBitMsg: overflow without allowOverflow set: %d > %d*8", GetNumBitsWritten() + numBits, GetMaxSize() );
		}
		if ( numBits > ( maxSize << 3 ) ) {
			idLib::common->FatalError( "idBitMsg: %i bits is > full message size", numBits );
		}
		idLib::common->Printf( "idBitMsg: overflow\n" );
		BeginWriting();
		overflowed = true;
		return true;
	}
	return false;
}

/*
================
idBitMsg::GetByteSpace
================
*/
byte *idBitMsg::GetByteSpace( int length ) {
	byte *ptr;

	if ( !writeData ) {
		idLib::common->FatalError( "idBitMsg::GetByteSpace: cannot write to message" );
	}
	if ( length < 0 || length > maxSize || length > ( idMath::INT_MAX >> 3 ) ) {
		idLib::common->FatalError( "idBitMsg::GetByteSpace: invalid length %d", length );
		return NULL;
	}

	// round up to the next byte
	WriteByteAlign();

	// check for overflow
	CheckOverflow( length << 3 );

	ptr = writeData + curSize;
	curSize += length;
	return ptr;
}

/*
================
idBitMsg::WriteBits

  If the number of bits is negative a sign is included.
================
*/
void idBitMsg::WriteBits( int value, int numBits ) {
	int		put;
	uint32_t	fraction;

	if ( !writeData ) {
		idLib::common->Error( "idBitMsg::WriteBits: cannot write to message" );
	}

	// check if the number of bits is valid
	if ( numBits == 0 || numBits < -31 || numBits > 32 ) {
		idLib::common->Error( "idBitMsg::WriteBits: bad numBits %i", numBits );
	}

	// check for value overflows
	if ( numBits != 32 ) {
		if ( numBits > 0 ) {
			const uint32_t maximumValue = ( 1u << numBits ) - 1u;
			if ( value < 0 || static_cast<uint32_t>( value ) > maximumValue ) {
				idLib::common->Warning( "idBitMsg::WriteBits: value overflow %d %d", value, numBits );
			}
		} else {
			const int64_t r = static_cast<int64_t>( 1 ) << ( -1 - numBits );
			if ( value > r - 1 ) {
				idLib::common->Warning( "idBitMsg::WriteBits: value overflow %d %d", value, numBits );
			} else if ( value < -r ) {
				idLib::common->Warning( "idBitMsg::WriteBits: value overflow %d %d", value, numBits );
			}
		}
	}

	if ( numBits < 0 ) {
		numBits = -numBits;
	}
	uint32_t unsignedValue = static_cast<uint32_t>( value );

	// check for msg overflow
	if ( CheckOverflow( numBits ) ) {
		return;
	}

	// write the bits
	while( numBits ) {
		if ( writeBit == 0 ) {
			writeData[curSize] = 0;
			curSize++;
		}
		put = 8 - writeBit;
		if ( put > numBits ) {
			put = numBits;
		}
		fraction = unsignedValue & ( ( 1u << put ) - 1u );
		writeData[curSize - 1] |= static_cast<byte>( fraction << writeBit );
		numBits -= put;
		unsignedValue >>= put;
		writeBit = ( writeBit + put ) & 7;
	}
}

/*
================
idBitMsg::WriteString
================
*/
void idBitMsg::WriteString( const char *s, int maxLength ) {
	if ( !s || maxLength == 0 ) {
		WriteData( "", 1 );
	} else {
		int l;
		byte *dataPtr;

		l = idStr::Length( s );
		if ( maxLength >= 0 && l >= maxLength ) {
			l = maxLength - 1;
		}
		dataPtr = GetByteSpace( l + 1 );
		memcpy( dataPtr, s, l );
		dataPtr[l] = '\0';
		// openQ4: ReadString ends a string at byte 255 as well as at 0, so a 255
		// sent as it is cut the string short there and the rest was read as the
		// next field: a name holding one shifted every key of its user info. Send
		// what the reader keeps, as it turns '%' into '.'. UTF-8 never uses 255.
		for ( int i = 0; i < l; i++ ) {
			if ( dataPtr[i] == 255 ) {
				dataPtr[i] = '.';
			}
		}
	}
}

/*
================
idBitMsg::WriteData
================
*/
void idBitMsg::WriteData( const void *data, int length ) {
	if ( length == 0 ) {
		WriteByteAlign();
		return;
	}
	if ( length < 0 || !data ) {
		idLib::common->FatalError( "idBitMsg::WriteData: invalid data or length %d", length );
		return;
	}
	memcpy( GetByteSpace( length ), data, length );
}

/*
================
idBitMsg::WriteNetadr
================
*/
void idBitMsg::WriteNetadr( const netadr_t adr ) {
	byte *dataPtr;
	dataPtr = GetByteSpace( 4 );
	if ( adr.type == NA_IP || adr.type == NA_LOOPBACK ) {
		memcpy( dataPtr, adr.ip, 4 );
	} else {
		memset( dataPtr, 0, 4 );
	}
	WriteUShort( adr.port );
}

/*
================
idBitMsg::WriteDelta
================
*/
void idBitMsg::WriteDelta( int oldValue, int newValue, int numBits ) {
	if ( oldValue == newValue ) {
		WriteBits( 0, 1 );
		return;
	}
	WriteBits( 1, 1 );
	WriteBits( newValue, numBits );
}

/*
================
idBitMsg::WriteDeltaByteCounter
================
*/
void idBitMsg::WriteDeltaByteCounter( int oldValue, int newValue ) {
	int i;
	const uint32_t x = static_cast<uint32_t>( oldValue ^ newValue );

	if ( x & ( 1u << 7 ) ) {
		idLib::common->Warning( "idBitMsg::WriteDeltaByteCounter: unencodable high-bit change" );
		WriteBits( 0, 3 );
		return;
	}
	for ( i = 6; i >= 0; i-- ) {
		if ( x & ( 1u << i ) ) {
			i++;
			break;
		}
	}
	if ( i < 0 ) {
		i = 0;
	}
	WriteBits( i, 3 );
	if ( i ) {
		WriteBits( static_cast<int>( ( ( 1u << i ) - 1u ) & static_cast<uint32_t>( newValue ) ), i );
	}
}

/*
================
idBitMsg::WriteDeltaShortCounter
================
*/
void idBitMsg::WriteDeltaShortCounter( int oldValue, int newValue ) {
	int i;
	const uint32_t x = static_cast<uint32_t>( oldValue ^ newValue );

	if ( x & ( 1u << 15 ) ) {
		idLib::common->Warning( "idBitMsg::WriteDeltaShortCounter: unencodable high-bit change" );
		WriteBits( 0, 4 );
		return;
	}
	for ( i = 14; i >= 0; i-- ) {
		if ( x & ( 1u << i ) ) {
			i++;
			break;
		}
	}
	if ( i < 0 ) {
		i = 0;
	}
	WriteBits( i, 4 );
	if ( i ) {
		WriteBits( static_cast<int>( ( ( 1u << i ) - 1u ) & static_cast<uint32_t>( newValue ) ), i );
	}
}

/*
================
idBitMsg::WriteDeltaLongCounter
================
*/
void idBitMsg::WriteDeltaLongCounter( int oldValue, int newValue ) {
	int i;
	const uint32_t x = static_cast<uint32_t>( oldValue ^ newValue );

	if ( x & ( 1u << 31 ) ) {
		idLib::common->Warning( "idBitMsg::WriteDeltaLongCounter: unencodable high-bit change" );
		WriteBits( 0, 5 );
		return;
	}
	for ( i = 30; i >= 0; i-- ) {
		if ( x & ( 1u << i ) ) {
			i++;
			break;
		}
	}
	if ( i < 0 ) {
		i = 0;
	}
	WriteBits( i, 5 );
	if ( i ) {
		WriteBits( static_cast<int>( ( ( 1u << i ) - 1u ) & static_cast<uint32_t>( newValue ) ), i );
	}
}

/*
==================
idBitMsg::WriteDeltaDict
==================
*/
bool idBitMsg::WriteDeltaDict( const idDict &dict, const idDict *base ) {
	int i;
	const idKeyValue *kv, *basekv;
	bool changed = false;

	if ( base != NULL ) {

		for ( i = 0; i < dict.GetNumKeyVals(); i++ ) {
			kv = dict.GetKeyVal( i );
			basekv = base->FindKey( kv->GetKey() );
			if ( basekv == NULL || basekv->GetValue().Cmp( kv->GetValue() ) != 0 ) {
				WriteString( kv->GetKey() );
				WriteString( kv->GetValue() );
				changed = true;
			}
		}

		WriteString( "" );

		for ( i = 0; i < base->GetNumKeyVals(); i++ ) {
			basekv = base->GetKeyVal( i );
			kv = dict.FindKey( basekv->GetKey() );
			if ( kv == NULL ) {
				WriteString( basekv->GetKey() );
				changed = true;
			}
		}

		WriteString( "" );

	} else {

		for ( i = 0; i < dict.GetNumKeyVals(); i++ ) {
			kv = dict.GetKeyVal( i );
			WriteString( kv->GetKey() );
			WriteString( kv->GetValue() );
			changed = true;
		}
		WriteString( "" );

		WriteString( "" );

	}

	return changed;
}

/*
================
idBitMsg::ReadBits

  If the number of bits is negative a sign is included.
================
*/
int idBitMsg::ReadBits( int numBits ) const {
	uint32_t	value;
	int		valueBits;
	int		get;
	uint32_t	fraction;
	bool	sgn;

	if ( !readData ) {
		idLib::common->FatalError( "idBitMsg::ReadBits: cannot read from message" );
	}

	// check if the number of bits is valid
	if ( numBits == 0 || numBits < -31 || numBits > 32 ) {
		idLib::common->FatalError( "idBitMsg::ReadBits: bad numBits %i", numBits );
	}

	value = 0;
	valueBits = 0;

	if ( numBits < 0 ) {
		numBits = -numBits;
		sgn = true;
	} else {
		sgn = false;
	}

	// check for overflow
	if ( IsReadOverflowed() || numBits > GetRemainingReadBits() ) {
		MarkReadOverflowed();
		return -1;
	}

	while ( valueBits < numBits ) {
		if ( readBit == 0 ) {
			readCount++;
		}
		get = 8 - readBit;
		if ( get > (numBits - valueBits) ) {
			get = numBits - valueBits;
		}
		fraction = readData[readCount - 1];
		fraction >>= readBit;
		fraction &= ( 1u << get ) - 1u;
		value |= fraction << valueBits;

		valueBits += get;
		readBit = ( readBit + get ) & 7;
	}

	if ( sgn ) {
		if ( value & ( 1u << ( numBits - 1 ) ) ) {
			value |= ~( ( 1u << numBits ) - 1u );
		}
	}

	int result;
	memcpy( &result, &value, sizeof( result ) );
	return result;
}

/*
================
idBitMsg::ReadString
================
*/
int idBitMsg::ReadString( char *buffer, int bufferSize ) const {
	int	l, c;
	
	ReadByteAlign();
	l = 0;
	while( 1 ) {
		c = ReadByte();
		if ( c <= 0 || c >= 255 ) {
			break;
		}
		// translate all fmt spec to avoid crash bugs in string routines
		if ( c == '%' ) {
			c = '.';
		}

		// we will read past any excessively long string, so
		// the following data can be read, but the string will
		// be truncated
		if ( buffer && bufferSize > 0 && l < bufferSize - 1 ) {
			buffer[l] = c;
			l++;
		}
	}
	
	if ( buffer && bufferSize > 0 ) {
		buffer[l] = 0;
	}
	return l;
}

/*
================
idBitMsg::ReadData
================
*/
int idBitMsg::ReadData( void *data, int length ) const {
	int cnt;

	ReadByteAlign();
	if ( IsReadOverflowed() || length < 0 ) {
		if ( data && length > 0 ) {
			memset( data, 0, length );
		}
		MarkReadOverflowed();
		return 0;
	}
	cnt = readCount;

	if ( length > curSize - readCount ) {
		const int remaining = curSize - readCount;
		if ( data ) {
			memcpy( data, readData + readCount, remaining );
			memset( static_cast<byte *>( data ) + remaining, 0, length - remaining );
		}
		MarkReadOverflowed();
		return remaining;
	} else {
		if ( data && length > 0 ) {
			memcpy( data, readData + readCount, length );
		}
		readCount += length;
	}

	return ( readCount - cnt );
}

/*
================
idBitMsg::ReadNetadr
================
*/
void idBitMsg::ReadNetadr( netadr_t *adr ) const {
	int i;
 
	memset( adr, 0, sizeof( *adr ) );
	adr->type = NA_IP;
	for ( i = 0; i < 4; i++ ) {
		adr->ip[ i ] = ReadByte();
	}
	adr->port = ReadUShort();
}

/*
================
idBitMsg::ReadDelta
================
*/
int idBitMsg::ReadDelta( int oldValue, int numBits ) const {
	if ( ReadBits( 1 ) ) {
		return ReadBits( numBits );
	}
	return oldValue;
}

/*
================
idBitMsg::ReadDeltaByteCounter
================
*/
int idBitMsg::ReadDeltaByteCounter( int oldValue ) const {
	int i, newValue;

	i = ReadBits( 3 );
	if ( IsReadOverflowed() || i <= 0 ) {
		return oldValue;
	}
	newValue = ReadBits( i );
	if ( IsReadOverflowed() ) {
		return oldValue;
	}
	const unsigned int mask = ( 1u << i ) - 1u;
	return static_cast<int>( ( static_cast<unsigned int>( oldValue ) & ~mask ) |
		( static_cast<unsigned int>( newValue ) & mask ) );
}

/*
================
idBitMsg::ReadDeltaShortCounter
================
*/
int idBitMsg::ReadDeltaShortCounter( int oldValue ) const {
	int i, newValue;

	i = ReadBits( 4 );
	if ( IsReadOverflowed() || i <= 0 ) {
		return oldValue;
	}
	newValue = ReadBits( i );
	if ( IsReadOverflowed() ) {
		return oldValue;
	}
	const unsigned int mask = ( 1u << i ) - 1u;
	return static_cast<int>( ( static_cast<unsigned int>( oldValue ) & ~mask ) |
		( static_cast<unsigned int>( newValue ) & mask ) );
}

/*
================
idBitMsg::ReadDeltaLongCounter
================
*/
int idBitMsg::ReadDeltaLongCounter( int oldValue ) const {
	int i, newValue;

	i = ReadBits( 5 );
	if ( IsReadOverflowed() || i <= 0 || i > 31 ) {
		return oldValue;
	}
	newValue = ReadBits( i );
	if ( IsReadOverflowed() ) {
		return oldValue;
	}
	const unsigned int mask = ( 1u << i ) - 1u;
	return static_cast<int>( ( static_cast<unsigned int>( oldValue ) & ~mask ) |
		( static_cast<unsigned int>( newValue ) & mask ) );
}

/*
==================
idBitMsg::ReadDeltaDict
==================
*/
bool idBitMsg::ReadDeltaDict( idDict &dict, const idDict *base ) const {
	char		key[MAX_STRING_CHARS];
	char		value[MAX_STRING_CHARS];
	bool		changed = false;
	idDict		decoded;

	if ( base != NULL ) {
		decoded = *base;
	} else {
		decoded.Clear();
	}

	while ( true ) {
		ReadString( key, sizeof( key ) );
		if ( IsReadOverflowed() ) {
			return false;
		}
		if ( key[ 0 ] == '\0' ) {
			break;
		}
		ReadString( value, sizeof( value ) );
		if ( IsReadOverflowed() ) {
			return false;
		}
		decoded.Set( key, value );
		changed = true;
	}

	while ( true ) {
		ReadString( key, sizeof( key ) );
		if ( IsReadOverflowed() ) {
			return false;
		}
		if ( key[ 0 ] == '\0' ) {
			break;
		}
		decoded.Delete( key );
		changed = true;
	}

	dict = decoded;
	return changed;
}

/*
================
idBitMsg::DirToBits
================
*/
int idBitMsg::DirToBits( const idVec3 &dir, int numBits ) {
	int max, bits;
	float bias;

	assert( numBits >= 6 && numBits <= 32 );
	assert( dir.LengthSqr() - 1.0f < 0.01f );

	numBits /= 3;
	max = ( 1 << ( numBits - 1 ) ) - 1;
	bias = 0.5f / max;

	bits = FLOATSIGNBITSET( dir.x ) << ( numBits * 3 - 1 );
	bits |= ( idMath::Ftoi( ( idMath::Fabs( dir.x ) + bias ) * max ) ) << ( numBits * 2 );
	bits |= FLOATSIGNBITSET( dir.y ) << ( numBits * 2 - 1 );
	bits |= ( idMath::Ftoi( ( idMath::Fabs( dir.y ) + bias ) * max ) ) << ( numBits * 1 );
	bits |= FLOATSIGNBITSET( dir.z ) << ( numBits * 1 - 1 );
	bits |= ( idMath::Ftoi( ( idMath::Fabs( dir.z ) + bias ) * max ) ) << ( numBits * 0 );
	return bits;
}

/*
================
idBitMsg::BitsToDir
================
*/
idVec3 idBitMsg::BitsToDir( int bits, int numBits ) {
	static float sign[2] = { 1.0f, -1.0f };
	int max;
	float invMax;
	idVec3 dir;

	assert( numBits >= 6 && numBits <= 32 );

	numBits /= 3;
	max = ( 1 << ( numBits - 1 ) ) - 1;
	invMax = 1.0f / max;

	dir.x = sign[( bits >> ( numBits * 3 - 1 ) ) & 1] * ( ( bits >> ( numBits * 2 ) ) & max ) * invMax;
	dir.y = sign[( bits >> ( numBits * 2 - 1 ) ) & 1] * ( ( bits >> ( numBits * 1 ) ) & max ) * invMax;
	dir.z = sign[( bits >> ( numBits * 1 - 1 ) ) & 1] * ( ( bits >> ( numBits * 0 ) ) & max ) * invMax;
	dir.NormalizeFast();
	return dir;
}


/*
==============================================================================

  idBitMsgDelta

==============================================================================
*/

const int MAX_DATA_BUFFER		= 1024;

/*
================
idBitMsgDelta::WriteBits
================
*/
void idBitMsgDelta::WriteBits( int value, int numBits ) {
	if ( newBase ) {
		newBase->WriteBits( value, numBits );
	}

	if ( !base ) {
		writeDelta->WriteBits( value, numBits );
		changed = true;
	} else {
		int baseValue = base->ReadBits( numBits );
		if ( !base->IsReadOverflowed() && baseValue == value ) {
			writeDelta->WriteBits( 0, 1 );
		} else {
			writeDelta->WriteBits( 1, 1 );
			writeDelta->WriteBits( value, numBits );
			changed = true;
		}
	}
}

/*
================
idBitMsgDelta::WriteDelta
================
*/
void idBitMsgDelta::WriteDelta( int oldValue, int newValue, int numBits ) {
	if ( newBase ) {
		newBase->WriteBits( newValue, numBits );
	}

	if ( !base ) {
		if ( oldValue == newValue ) {
			writeDelta->WriteBits( 0, 1 );
		} else {
			writeDelta->WriteBits( 1, 1 );
			writeDelta->WriteBits( newValue, numBits );
		}
		changed = true;
	} else {
		int baseValue = base->ReadBits( numBits );
		if ( !base->IsReadOverflowed() && baseValue == newValue ) {
			writeDelta->WriteBits( 0, 1 );
		} else {
			writeDelta->WriteBits( 1, 1 );
			if ( oldValue == newValue ) {
				writeDelta->WriteBits( 0, 1 );
				changed = true;
			} else {
				writeDelta->WriteBits( 1, 1 );
				writeDelta->WriteBits( newValue, numBits );
				changed = true;
			}
		}
	}
}

/*
================
idBitMsgDelta::ReadBits
================
*/
int idBitMsgDelta::ReadBits( int numBits ) const {
	int value;

	if ( !base ) {
		value = readDelta->ReadBits( numBits );
		changed = true;
	} else {
		int baseValue = base->ReadBits( numBits );
		const bool baseOverflowed = base->IsReadOverflowed();
		if ( !readDelta ) {
			value = baseValue;
		} else if ( readDelta->ReadBits( 1 ) == 0 ) {
			value = baseValue;
			if ( baseOverflowed ) {
				readDelta->MarkReadOverflowed();
			}
		} else {
			value = readDelta->ReadBits( numBits );
			changed = true;
		}
	}

	if ( newBase ) {
		newBase->WriteBits( value, numBits );
	}
	return value;
}

/*
================
idBitMsgDelta::ReadDelta
================
*/
int idBitMsgDelta::ReadDelta( int oldValue, int numBits ) const {
	int value;

	if ( !base ) {
		if ( readDelta->ReadBits( 1 ) == 0 ) {
			value = oldValue;
		} else {
			value = readDelta->ReadBits( numBits );
		}
		changed = true;
	} else {
		int baseValue = base->ReadBits( numBits );
		const bool baseOverflowed = base->IsReadOverflowed();
		if ( !readDelta ) {
			value = baseValue;
		} else if ( readDelta->ReadBits( 1 ) == 0 ) {
			value = baseValue;
			if ( baseOverflowed ) {
				readDelta->MarkReadOverflowed();
			}
		} else if ( readDelta->ReadBits( 1 ) == 0 ) {
			value = oldValue;
			changed = true;
		} else {
			value = readDelta->ReadBits( numBits );
			changed = true;
		}
	}

	if ( newBase ) {
		newBase->WriteBits( value, numBits );
	}
	return value;
}

/*
================
idBitMsgDelta::WriteString
================
*/
void idBitMsgDelta::WriteString( const char *s, int maxLength ) {
	if ( newBase ) {
		newBase->WriteString( s, maxLength );
	}

	if ( !base ) {
		writeDelta->WriteString( s, maxLength );
		changed = true;
	} else {
		char baseString[MAX_DATA_BUFFER];
		base->ReadString( baseString, sizeof( baseString ) );
		if ( !base->IsReadOverflowed() && idStr::Cmp( s, baseString ) == 0 ) {
			writeDelta->WriteBits( 0, 1 );
		} else {
			writeDelta->WriteBits( 1, 1 );
			writeDelta->WriteString( s, maxLength );
			changed = true;
		}
	}
}

/*
================
idBitMsgDelta::WriteData
================
*/
void idBitMsgDelta::WriteData( const void *data, int length ) {
	if ( length < 0 || ( length > 0 && !data ) ) {
		idLib::common->FatalError( "idBitMsgDelta::WriteData: invalid data or length %d", length );
		return;
	}
	if ( newBase ) {
		newBase->WriteData( data, length );
	}

	if ( !base ) {
		writeDelta->WriteData( data, length );
		changed = true;
	} else {
		// Compare directly with the byte-aligned base. A fixed stack copy both
		// wasted work and imposed a hidden 1024-byte limit on binary fields.
		base->ReadByteAlign();
		const byte *baseData = length > 0 && length <= base->GetRemainingData() ? base->GetReadData() : NULL;
		base->ReadData( NULL, length );
		if ( !base->IsReadOverflowed() && ( length == 0 || memcmp( data, baseData, length ) == 0 ) ) {
			writeDelta->WriteBits( 0, 1 );
		} else {
			writeDelta->WriteBits( 1, 1 );
			writeDelta->WriteData( data, length );
			changed = true;
		}
	}
}

/*
================
idBitMsgDelta::WriteDict
================
*/
void idBitMsgDelta::WriteDict( const idDict &dict ) {
	if ( newBase ) {
		newBase->WriteDeltaDict( dict, NULL );
	}

	if ( !base ) {
		writeDelta->WriteDeltaDict( dict, NULL );
		changed = true;
	} else {
		idDict baseDict;
		base->ReadDeltaDict( baseDict, NULL );
		changed = writeDelta->WriteDeltaDict( dict, &baseDict );
	}
}

/*
================
idBitMsgDelta::WriteDeltaByteCounter
================
*/
void idBitMsgDelta::WriteDeltaByteCounter( int oldValue, int newValue ) {
	if ( newBase ) {
		newBase->WriteBits( newValue, 8 );
	}

	if ( !base ) {
		writeDelta->WriteDeltaByteCounter( oldValue, newValue );
		changed = true;
	} else {
		int baseValue = base->ReadBits( 8 );
		if ( !base->IsReadOverflowed() && baseValue == newValue ) {
			writeDelta->WriteBits( 0, 1 );
		} else {
			writeDelta->WriteBits( 1, 1 );
			writeDelta->WriteDeltaByteCounter( oldValue, newValue );
			changed = true;
		}
	}
}

/*
================
idBitMsgDelta::WriteDeltaShortCounter
================
*/
void idBitMsgDelta::WriteDeltaShortCounter( int oldValue, int newValue ) {
	if ( newBase ) {
		newBase->WriteBits( newValue, 16 );
	}

	if ( !base ) {
		writeDelta->WriteDeltaShortCounter( oldValue, newValue );
		changed = true;
	} else {
		int baseValue = base->ReadBits( 16 );
		if ( !base->IsReadOverflowed() && baseValue == newValue ) {
			writeDelta->WriteBits( 0, 1 );
		} else {
			writeDelta->WriteBits( 1, 1 );
			writeDelta->WriteDeltaShortCounter( oldValue, newValue );
			changed = true;
		}
	}
}

/*
================
idBitMsgDelta::WriteDeltaLongCounter
================
*/
void idBitMsgDelta::WriteDeltaLongCounter( int oldValue, int newValue ) {
	if ( newBase ) {
		newBase->WriteBits( newValue, 32 );
	}

	if ( !base ) {
		writeDelta->WriteDeltaLongCounter( oldValue, newValue );
		changed = true;
	} else {
		int baseValue = base->ReadBits( 32 );
		if ( !base->IsReadOverflowed() && baseValue == newValue ) {
			writeDelta->WriteBits( 0, 1 );
		} else {
			writeDelta->WriteBits( 1, 1 );
			writeDelta->WriteDeltaLongCounter( oldValue, newValue );
			changed = true;
		}
	}
}

/*
================
idBitMsgDelta::ReadString
================
*/
void idBitMsgDelta::ReadString( char *buffer, int bufferSize ) const {
	if ( !base ) {
		readDelta->ReadString( buffer, bufferSize );
		changed = true;
	} else {
		char baseString[MAX_DATA_BUFFER];
		base->ReadString( baseString, sizeof( baseString ) );
		const bool baseOverflowed = base->IsReadOverflowed();
		if ( !readDelta ) {
			idStr::Copynz( buffer, baseString, bufferSize );
		} else if ( readDelta->ReadBits( 1 ) == 0 ) {
			idStr::Copynz( buffer, baseString, bufferSize );
			if ( baseOverflowed ) {
				readDelta->MarkReadOverflowed();
			}
		} else {
			readDelta->ReadString( buffer, bufferSize );
			changed = true;
		}
	}

	if ( newBase ) {
		newBase->WriteString( buffer );
	}
}

/*
================
idBitMsgDelta::ReadData
================
*/
void idBitMsgDelta::ReadData( void *data, int length ) const {
	if ( length < 0 || ( newBase && length > 0 && !data ) ) {
		idLib::common->FatalError( "idBitMsgDelta::ReadData: invalid data or length %d", length );
		return;
	}
	if ( !base ) {
		readDelta->ReadData( data, length );
		changed = true;
	} else {
		if ( !readDelta ) {
			base->ReadData( data, length );
		} else if ( readDelta->ReadBits( 1 ) == 0 ) {
			base->ReadData( data, length );
			if ( base->IsReadOverflowed() ) {
				readDelta->MarkReadOverflowed();
			}
		} else {
			base->ReadData( NULL, length );
			readDelta->ReadData( data, length );
			changed = true;
		}
	}

	if ( newBase ) {
		newBase->WriteData( data, length );
	}
}

/*
================
idBitMsgDelta::ReadDict
================
*/
void idBitMsgDelta::ReadDict( idDict &dict ) {
	if ( !base ) {
		readDelta->ReadDeltaDict( dict, NULL );
		changed = true;
	} else {
		idDict baseDict;
		base->ReadDeltaDict( baseDict, NULL );
		if ( !readDelta ) {
			dict = baseDict;
		} else {
			changed = readDelta->ReadDeltaDict( dict, &baseDict );
		}
	}

	if ( newBase ) {
		newBase->WriteDeltaDict( dict, NULL );
	}
}

/*
================
idBitMsgDelta::ReadDeltaByteCounter
================
*/
int idBitMsgDelta::ReadDeltaByteCounter( int oldValue ) const {
	int value;

	if ( !base ) {
		value = readDelta->ReadDeltaByteCounter( oldValue );
		changed = true;
	} else {
		int baseValue = base->ReadBits( 8 );
		const bool baseOverflowed = base->IsReadOverflowed();
		if ( !readDelta ) {
			value = baseValue;
		} else if ( readDelta->ReadBits( 1 ) == 0 ) {
			value = baseValue;
			if ( baseOverflowed ) {
				readDelta->MarkReadOverflowed();
			}
		} else {
			value = readDelta->ReadDeltaByteCounter( oldValue );
			changed = true;
		}
	}

	if ( newBase ) {
		newBase->WriteBits( value, 8 );
	}
	return value;
}

/*
================
idBitMsgDelta::ReadDeltaShortCounter
================
*/
int idBitMsgDelta::ReadDeltaShortCounter( int oldValue ) const {
	int value;

	if ( !base ) {
		value = readDelta->ReadDeltaShortCounter( oldValue );
		changed = true;
	} else {
		int baseValue = base->ReadBits( 16 );
		const bool baseOverflowed = base->IsReadOverflowed();
		if ( !readDelta ) {
			value = baseValue;
		} else if ( readDelta->ReadBits( 1 ) == 0 ) {
			value = baseValue;
			if ( baseOverflowed ) {
				readDelta->MarkReadOverflowed();
			}
		} else {
			value = readDelta->ReadDeltaShortCounter( oldValue );
			changed = true;
		}
	}

	if ( newBase ) {
		newBase->WriteBits( value, 16 );
	}
	return value;
}

/*
================
idBitMsgDelta::ReadDeltaLongCounter
================
*/
int idBitMsgDelta::ReadDeltaLongCounter( int oldValue ) const {
	int value;

	if ( !base ) {
		value = readDelta->ReadDeltaLongCounter( oldValue );
		changed = true;
	} else {
		int baseValue = base->ReadBits( 32 );
		const bool baseOverflowed = base->IsReadOverflowed();
		if ( !readDelta ) {
			value = baseValue;
		} else if ( readDelta->ReadBits( 1 ) == 0 ) {
			value = baseValue;
			if ( baseOverflowed ) {
				readDelta->MarkReadOverflowed();
			}
		} else {
			value = readDelta->ReadDeltaLongCounter( oldValue );
			changed = true;
		}
	}

	if ( newBase ) {
		newBase->WriteBits( value, 32 );
	}
	return value;
}

/*
===========================================================================
idMsgQueue
===========================================================================
*/

/*
===============
idMsgQueue::idMsgQueue
===============
*/
idMsgQueue::idMsgQueue( void ) {
	Init( 0 );
}

/*
===============
idMsgQueue::Init
===============
*/
void idMsgQueue::Init( int sequence ) {
	first = last = sequence;
	startIndex = endIndex = 0;
}

/*
===============
idMsgQueue::Add
===============
*/
bool idMsgQueue::Add( const byte *data, const int size, bool sequencing ) {
	if ( GetSpaceLeft() < size + 8 ) {
		return false;
	}

	assert( size );

	WriteUShort( size );
	if ( sequencing ) {
		WriteLong( last );
	}
	last++;
	WriteData( data, size );
	return true;
}

/*
===============
idMsgQueue::AddConcat
===============
*/
bool idMsgQueue::AddConcat( const byte *data1, const int size1, const byte *data2, const int size2, bool sequencing ) {
	if ( GetSpaceLeft() < size1 + size2 + 8 ) {
		return false;
	}

	assert( size1 && size2 );
	
	WriteUShort( size1 + size2 );
	if ( sequencing ) {
		WriteLong( last );
	}
	last++;
	WriteData( data1, size1 );
	WriteData( data2, size2 );
	return true;
}

/*
===============
idMsgQueue::Get
===============
*/
bool idMsgQueue::Get( byte *data, int dataSize, int &size, bool sequencing ) {
	if ( sequencing ? ( first == last ) : ( startIndex == endIndex ) ) {
		size = 0;
		return false;
	}
	const int headerSize = sequencing ? 6 : 2;
	const int totalSize = GetTotalSize();
	if ( totalSize < headerSize ) {
		common->Warning( "idMsgQueue::Get: truncated queue record header" );
		size = 0;
		first = last;
		startIndex = endIndex;
		return false;
	}
	size = ReadUShort();
	if ( size <= 0 || size > totalSize - headerSize || ( data && size > dataSize ) ) {
		common->Warning( "idMsgQueue::Get: invalid record size %d (available %d, destination %d)",
			size, totalSize - headerSize, data ? dataSize : 0 );
		size = 0;
		first = last;
		startIndex = endIndex;
		return false;
	}

	if ( sequencing ) {
		const int sequence = ReadLong();
		if ( sequence != first ) {
			common->Warning( "idMsgQueue::Get: invalid record sequence %d (expected %d)", sequence, first );
			size = 0;
			first = last;
			startIndex = endIndex;
			return false;
		}
	}
	ReadData( data, size );
	first++;
	return true;
}

/*
===============
idMsgQueue::GetTotalSize
===============
*/
int idMsgQueue::GetTotalSize( void ) const {
	if ( startIndex <= endIndex ) {
		return ( endIndex - startIndex );
	} else {
		return ( sizeof( buffer ) - startIndex + endIndex );
	}
}

/*
===============
idMsgQueue::GetSpaceLeft
===============
*/
int idMsgQueue::GetSpaceLeft( void ) const {
	if ( startIndex <= endIndex ) {
		return sizeof( buffer ) - ( endIndex - startIndex ) - 1;
	} else {
		return ( startIndex - endIndex ) - 1;
	}
}

/*
===============
idMsgQueue::CopyToBuffer
===============
*/
void idMsgQueue::CopyToBuffer( byte *buf ) const {
	if ( startIndex <= endIndex ) {
// RAVEN BEGIN
// JSinger: Changed to call optimized memcpy
		SIMDProcessor->Memcpy( buf, buffer + startIndex, endIndex - startIndex );
// RAVEN END
	} else {
// RAVEN BEGIN
// JSinger: Changed to call optimized memcpy
		SIMDProcessor->Memcpy( buf, buffer + startIndex, sizeof( buffer ) - startIndex );
		SIMDProcessor->Memcpy( buf + sizeof( buffer ) - startIndex, buffer, endIndex );
// RAVEN END
	}
}

/*
===============
idMsgQueue::WriteByte
===============
*/
void idMsgQueue::WriteByte( byte b ) {
	buffer[endIndex] = b;
	endIndex = ( endIndex + 1 ) & ( MAX_MSG_QUEUE_SIZE - 1 );
}

/*
===============
idMsgQueue::ReadByte
===============
*/
byte idMsgQueue::ReadByte( void ) {
	byte b = buffer[startIndex];
	startIndex = ( startIndex + 1 ) & ( MAX_MSG_QUEUE_SIZE - 1 );
	return b;
}

/*
===============
idMsgQueue::WriteShort
===============
*/
void idMsgQueue::WriteShort( int s ) {
	WriteByte( ( s >>  0 ) & 255 );
	WriteByte( ( s >>  8 ) & 255 );
}

/*
===============
idMsgQueue::WriteUShort
===============
*/
void idMsgQueue::WriteUShort( int s ) {
	WriteByte( ( s >>  0 ) & 255 );
	WriteByte( ( s >>  8 ) & 255 );
}

/*
===============
idMsgQueue::ReadShort
===============
*/
int idMsgQueue::ReadShort( void ) {
// RAVEN BEGIN
// ddynerman: removed side-effecting bitwise or
	byte l = ReadByte();
	byte h = ReadByte();
	return (short)(l | ( h << 8 )); // sign extend
// RAVEN LOW
}

/*
===============
idMsgQueue::ReadUShort
===============
*/
int idMsgQueue::ReadUShort( void ) {
// RAVEN BEGIN
// ddynerman: removed side-effecting bitwise or
	byte l = ReadByte();
	byte h = ReadByte();
	return l | ( h << 8 );
// RAVEN LOW
}

/*
===============
idMsgQueue::WriteLong
===============
*/
void idMsgQueue::WriteLong( int l ) {
	WriteByte( ( l >>  0 ) & 255 );
	WriteByte( ( l >>  8 ) & 255 );
	WriteByte( ( l >> 16 ) & 255 );
	WriteByte( ( l >> 24 ) & 255 );
}

/*
===============
idMsgQueue::ReadLong
===============
*/
int idMsgQueue::ReadLong( void ) {
// RAVEN BEGIN
// ddynerman: removed side-effecting bitwise or
	byte ll = ReadByte();
	byte lh = ReadByte();
	byte hl = ReadByte();
	byte hh = ReadByte();
	return ll | ( lh << 8 ) | ( hl << 16 ) | ( hh << 24 );
// RAVEN END
}

/*
===============
idMsgQueue::WriteData
===============
*/
void idMsgQueue::WriteData( const byte *data, const int size ) {
	for ( int i = 0; i < size; i++ ) {
		WriteByte( data[i] );
	}
}

/*
===============
idMsgQueue::ReadData
===============
*/
void idMsgQueue::ReadData( byte *data, const int size ) {
	if ( data ) {
		for ( int i = 0; i < size; i++ ) {
			data[i] = ReadByte();
		}
	} else {
		for ( int i = 0; i < size; i++ ) {
			ReadByte();
		}
	}
}

/*
===============
idMsgQueue::WriteTo
===============
*/
void idMsgQueue::WriteTo( idBitMsg &msg ) {
	msg.WriteUShort( GetTotalSize() );
	assert( startIndex == 0 );
	msg.WriteData( buffer + startIndex, endIndex - startIndex );
}

/*
===============
idMsgQueue::FlushTo
===============
*/
void idMsgQueue::FlushTo( idBitMsg &msg ) {
	WriteTo( msg );
	Init( 0 );
}

/*
===============
idMsgQueue::ReadFrom
===============
*/
void idMsgQueue::ReadFrom( const idBitMsg &msg ) {
	Init( 0 );
	const int encodedSize = msg.ReadUShort();
	const int available = msg.GetRemainingData();
	const int remaining = available > 0 ? available : 0;
	if ( encodedSize < 0 || encodedSize >= MAX_MSG_QUEUE_SIZE ||
		 encodedSize > remaining ) {
		common->Warning( "idMsgQueue::ReadFrom: invalid encoded queue size %d", encodedSize );
		const int discard = idMath::ClampInt( 0, remaining, encodedSize );
		if ( discard > 0 ) {
			msg.ReadData( NULL, discard );
		}
		msg.MarkReadOverflowed();
		endIndex = 0;
		return;
	}
	endIndex = encodedSize;
	if ( msg.ReadData( buffer, endIndex ) != endIndex ) {
		common->Warning( "idMsgQueue::ReadFrom: truncated encoded queue" );
		endIndex = 0;
		return;
	}

	// ReadFrom is used for serialized unreliable-message queues, whose nested
	// records are a little-endian ushort length followed by that payload.
	// Validate every nested boundary now so Get cannot later walk uninitialized
	// ring storage when a snapshot supplied a forged record size.
	for ( int offset = 0; offset < endIndex; ) {
		if ( endIndex - offset < 2 ) {
			common->Warning( "idMsgQueue::ReadFrom: truncated nested record header" );
			msg.MarkReadOverflowed();
			Init( 0 );
			return;
		}
		const int recordSize = buffer[ offset ] | ( buffer[ offset + 1 ] << 8 );
		offset += 2;
		if ( recordSize <= 0 || recordSize > endIndex - offset ) {
			common->Warning( "idMsgQueue::ReadFrom: invalid nested record size %d", recordSize );
			msg.MarkReadOverflowed();
			Init( 0 );
			return;
		}
		offset += recordSize;
	}
}

/*
===============
idMsgQueue::Save
===============
*/
void idMsgQueue::Save( idFile *file ) const {
	file->WriteInt( first );
	file->WriteInt( last );
	file->WriteInt( startIndex );
	file->WriteInt( endIndex );
	file->Write( buffer + startIndex, endIndex - startIndex );
}
/*
===============
idMsgQueue::Restore
===============
*/
void idMsgQueue::Restore( idFile *file ) {
	file->ReadInt( first );
	file->ReadInt( last );
	file->ReadInt( startIndex );
	file->ReadInt( endIndex );
	file->Read( buffer + startIndex, endIndex - startIndex );
}

/*
===============
idBitMsgQueue::idBitMsgQueue
===============
*/
idBitMsgQueue::idBitMsgQueue( void ) {
	Init();
}

/*
===============
idBitMsgQueue::Init
===============
*/
void idBitMsgQueue::Init( void ) {
	readTimestamp = false;
	writeList.Clear();
	readList.Clear();
}

/*
===============
idBitMsgQueue::Add
===============
*/
void idBitMsgQueue::Add( const idBitMsg &msg, const int timestamp ) {
	while ( writeList.NextNode() && writeList.Next()->GetSpaceLeft() < ( msg.GetSize() + 8+4 ) ) {
		writeList.NextNode()->AddToEnd( readList );
	}

	if ( !writeList.NextNode() ) {
		idLinkList< idMsgQueue > *node = new idLinkList< idMsgQueue >;
		node->SetOwner( new idMsgQueue );
		node->AddToEnd( writeList );
	}

	writeList.Next()->WriteLong( timestamp );
	if ( !writeList.Next()->Add( msg.GetData(), msg.GetSize(), false ) ) {
		assert( false );
	}
}

/*
===============
idBitMsgQueue::Get
===============
*/
bool idBitMsgQueue::Get( idBitMsg &msg, int &timestamp ) {
	while ( readList.NextNode() && !readList.Next()->GetTotalSize() ) {
		readList.Next()->Init( 0 );
		readList.NextNode()->AddToEnd( writeList );
	}

	if ( readList.NextNode() ) {
		int size;

		timestamp = readTimestamp ? nextTimestamp : readList.Next()->ReadLong();
		readTimestamp = true;
		if ( readList.Next()->Get( msg.GetData(), msg.GetMaxSize(), size, false ) ) {
			msg.SetSize( size );
			msg.BeginReading();
			readTimestamp = false;
			return true;
		}

		assert( false );
	} else if ( writeList.NextNode() && writeList.Next()->GetTotalSize() ) {
		int size;

		timestamp = readTimestamp ? nextTimestamp : writeList.Next()->ReadLong();
		readTimestamp = true;
		if ( writeList.Next()->Get( msg.GetData(), msg.GetMaxSize(), size, false ) ) {
			msg.SetSize( size );
			msg.BeginReading();
			readTimestamp = false;
			return true;
		}

		assert( false );
	}

	msg.SetSize( 0 );
	return false;
}

/*
===============
idBitMsgQueue::GetTimestamp
===============
*/
bool idBitMsgQueue::GetTimestamp( int &timestamp ) {
	if ( readTimestamp ) {
		timestamp = nextTimestamp;
		return true;
	}

	while ( readList.NextNode() && !readList.Next()->GetTotalSize() ) {
		readList.Next()->Init( 0 );
		readList.NextNode()->AddToEnd( writeList );
	}

	if ( readList.NextNode() ) {
		timestamp = nextTimestamp = readList.Next()->ReadLong();
		readTimestamp = true;
		return true;
	} else if ( writeList.NextNode() && writeList.Next()->GetTotalSize() ) {
		timestamp = nextTimestamp = writeList.Next()->ReadLong();
		readTimestamp = true;
		return true;
	}

	return false;
}
