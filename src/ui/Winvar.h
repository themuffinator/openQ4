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

#ifndef __WINVAR_H__
#define __WINVAR_H__

#include <cmath>
#include "Rectangle.h"

static const char VAR_GUIPREFIX[] = "gui::";
static const int VAR_GUIPREFIX_LEN = static_cast<int>( sizeof( VAR_GUIPREFIX ) - 1 );

static ID_INLINE bool OpenQ4_WriteSaveGameBytes( idFile *savefile, const void *buffer, int len, const char *context, const char *fieldName ) {
	if ( savefile == NULL || len < 0 || ( buffer == NULL && len > 0 ) ) {
		common->Error( "%s: invalid output while writing %s", context ? context : "savegame write", fieldName ? fieldName : "data" );
		return false;
	}
	const int offset = savefile->Tell();
	const int bytesWritten = savefile->Write( buffer, len );
	if ( bytesWritten != len ) {
		common->Error( "%s: failed to write %s at offset %d (wrote %d of %d)",
			context ? context : "savegame write", fieldName ? fieldName : "data", offset, bytesWritten, len );
		return false;
	}
	return true;
}

static ID_INLINE bool OpenQ4_ReadSaveGameBytes( idFile *savefile, void *buffer, int len, const char *context, const char *fieldName ) {
	if ( savefile == NULL || len < 0 || ( buffer == NULL && len > 0 ) ) {
		if ( buffer != NULL && len > 0 ) {
			memset( buffer, 0, len );
		}
		common->Error( "%s: invalid input while reading %s", context ? context : "savegame restore", fieldName ? fieldName : "data" );
		return false;
	}
	const int offset = savefile->Tell();
	const int bytesRead = savefile->Read( buffer, len );
	if ( bytesRead != len ) {
		if ( buffer != NULL && len > 0 ) {
			memset( buffer, 0, len );
		}
		common->Error( "%s: truncated %s at offset %d (read %d of %d)",
			context ? context : "savegame restore", fieldName ? fieldName : "data", offset, bytesRead, len );
		return false;
	}
	return true;
}

static ID_INLINE bool OpenQ4_WriteSaveGameBool( idFile *savefile, bool value, const char *context, const char *fieldName ) {
	const unsigned char savedValue = value ? 1 : 0;
	const int offset = savefile != NULL ? savefile->Tell() : -1;
	const int bytesWritten = savefile != NULL ? savefile->WriteUnsignedChar( savedValue ) : 0;
	if ( bytesWritten != 1 ) {
		common->Error( "%s: failed to write %s at offset %d (wrote %d of 1)",
			context ? context : "savegame write", fieldName ? fieldName : "boolean", offset, bytesWritten );
		return false;
	}
	return true;
}

static ID_INLINE bool OpenQ4_ReadSaveGameBool( idFile *savefile, bool &value, const char *context, const char *fieldName ) {
	unsigned char savedValue = 0;
	const int offset = savefile != NULL ? savefile->Tell() : -1;
	const int bytesRead = savefile != NULL ? savefile->ReadUnsignedChar( savedValue ) : 0;
	if ( bytesRead != 1 || savedValue > 1 ) {
		value = false;
		common->Error( "%s: invalid %s at offset %d (read %d bytes, value %u)",
			context ? context : "savegame restore", fieldName ? fieldName : "boolean", offset, bytesRead,
			static_cast<unsigned int>( savedValue ) );
		return false;
	}
	value = savedValue != 0;
	return true;
}

static ID_INLINE bool OpenQ4_WriteSaveGameInt( idFile *savefile, int value, const char *context, const char *fieldName ) {
	const int offset = savefile != NULL ? savefile->Tell() : -1;
	const int bytesWritten = savefile != NULL ? savefile->WriteInt( value ) : 0;
	if ( bytesWritten != static_cast<int>( sizeof( value ) ) ) {
		common->Error( "%s: failed to write %s at offset %d (wrote %d of %d)",
			context ? context : "savegame write", fieldName ? fieldName : "integer", offset, bytesWritten,
			static_cast<int>( sizeof( value ) ) );
		return false;
	}
	return true;
}

static ID_INLINE bool OpenQ4_ReadSaveGameInt( idFile *savefile, int &value, const char *context, const char *fieldName ) {
	const int offset = savefile != NULL ? savefile->Tell() : -1;
	const int bytesRead = savefile != NULL ? savefile->ReadInt( value ) : 0;
	if ( bytesRead != static_cast<int>( sizeof( value ) ) ) {
		value = 0;
		common->Error( "%s: truncated %s at offset %d (read %d of %d)",
			context ? context : "savegame restore", fieldName ? fieldName : "integer", offset, bytesRead,
			static_cast<int>( sizeof( value ) ) );
		return false;
	}
	return true;
}

static ID_INLINE bool OpenQ4_WriteSaveGameFloat( idFile *savefile, float value, const char *context, const char *fieldName ) {
	if ( !std::isfinite( value ) ) {
		common->Error( "%s: refusing non-finite %s", context ? context : "savegame write",
			fieldName ? fieldName : "float" );
		return false;
	}
	const int offset = savefile != NULL ? savefile->Tell() : -1;
	const int bytesWritten = savefile != NULL ? savefile->WriteFloat( value ) : 0;
	if ( bytesWritten != static_cast<int>( sizeof( value ) ) ) {
		common->Error( "%s: failed to write %s at offset %d (wrote %d of %d)",
			context ? context : "savegame write", fieldName ? fieldName : "float", offset, bytesWritten,
			static_cast<int>( sizeof( value ) ) );
		return false;
	}
	return true;
}

static ID_INLINE bool OpenQ4_ReadSaveGameFloat( idFile *savefile, float &value, const char *context, const char *fieldName ) {
	const int offset = savefile != NULL ? savefile->Tell() : -1;
	const int bytesRead = savefile != NULL ? savefile->ReadFloat( value ) : 0;
	if ( bytesRead != static_cast<int>( sizeof( value ) ) ) {
		common->Error( "%s: truncated %s at offset %d (read %d of %d)",
			context ? context : "savegame restore", fieldName ? fieldName : "float", offset, bytesRead,
			static_cast<int>( sizeof( value ) ) );
		value = 0.0f;
		return false;
	}
	if ( !std::isfinite( value ) ) {
		// A GUI variable can hold a non-finite expression result. Writers zero it
		// (OpenQ4_SanitizeSaveGameWinVar), but builds before 40922f767 saved it
		// as is; the value is still a whole float in step, so zero it here too.
		common->DPrintf( "%s: zeroing non-finite %s at offset %d\n", context ? context : "savegame restore",
			fieldName ? fieldName : "float", offset );
		value = 0.0f;
	}
	return true;
}

/*
================
OpenQ4_WriteSaveGameField

One GUI layout field through an explicit little-endian encoder. These are the
bytes a raw copy of the field wrote on every supported target. Floats keep
their bits, non-finite values included, as the raw copy did; callers check
them. A field type without an overload does not compile (roadmap Q4-06).
================
*/
static ID_INLINE bool OpenQ4_CheckSaveGameField( int transferred, int expected, int offset, bool writing, const char *context, const char *fieldName ) {
	if ( transferred != expected ) {
		common->Error( "%s: %s %s at offset %d (%d of %d bytes)", context ? context : ( writing ? "savegame write" : "savegame restore" ),
			writing ? "failed to write" : "truncated", fieldName ? fieldName : "field", offset, transferred, expected );
		return false;
	}
	return true;
}

static ID_INLINE bool OpenQ4_WriteSaveGameField( idFile *savefile, float value, const char *context, const char *fieldName ) {
	const int offset = savefile->Tell();
	return OpenQ4_CheckSaveGameField( savefile->WriteFloat( value ), 4, offset, true, context, fieldName );
}

static ID_INLINE bool OpenQ4_WriteSaveGameField( idFile *savefile, int value, const char *context, const char *fieldName ) {
	const int offset = savefile->Tell();
	return OpenQ4_CheckSaveGameField( savefile->WriteInt( value ), 4, offset, true, context, fieldName );
}

static ID_INLINE bool OpenQ4_WriteSaveGameField( idFile *savefile, unsigned int value, const char *context, const char *fieldName ) {
	const int offset = savefile->Tell();
	return OpenQ4_CheckSaveGameField( savefile->WriteUnsignedInt( value ), 4, offset, true, context, fieldName );
}

static ID_INLINE bool OpenQ4_WriteSaveGameField( idFile *savefile, signed char value, const char *context, const char *fieldName ) {
	const int offset = savefile->Tell();
	return OpenQ4_CheckSaveGameField( savefile->WriteChar( static_cast<char>( value ) ), 1, offset, true, context, fieldName );
}

static ID_INLINE bool OpenQ4_WriteSaveGameField( idFile *savefile, unsigned char value, const char *context, const char *fieldName ) {
	const int offset = savefile->Tell();
	return OpenQ4_CheckSaveGameField( savefile->WriteUnsignedChar( value ), 1, offset, true, context, fieldName );
}

static ID_INLINE bool OpenQ4_WriteSaveGameField( idFile *savefile, bool value, const char *context, const char *fieldName ) {
	return OpenQ4_WriteSaveGameBool( savefile, value, context, fieldName );
}

static ID_INLINE bool OpenQ4_WriteSaveGameField( idFile *savefile, const idVec2 &value, const char *context, const char *fieldName ) {
	return OpenQ4_WriteSaveGameField( savefile, value.x, context, fieldName ) &&
		OpenQ4_WriteSaveGameField( savefile, value.y, context, fieldName );
}

static ID_INLINE bool OpenQ4_WriteSaveGameField( idFile *savefile, const idVec3 &value, const char *context, const char *fieldName ) {
	return OpenQ4_WriteSaveGameField( savefile, value.x, context, fieldName ) &&
		OpenQ4_WriteSaveGameField( savefile, value.y, context, fieldName ) &&
		OpenQ4_WriteSaveGameField( savefile, value.z, context, fieldName );
}

static ID_INLINE bool OpenQ4_WriteSaveGameField( idFile *savefile, const idVec4 &value, const char *context, const char *fieldName ) {
	return OpenQ4_WriteSaveGameField( savefile, value.x, context, fieldName ) &&
		OpenQ4_WriteSaveGameField( savefile, value.y, context, fieldName ) &&
		OpenQ4_WriteSaveGameField( savefile, value.z, context, fieldName ) &&
		OpenQ4_WriteSaveGameField( savefile, value.w, context, fieldName );
}

static ID_INLINE bool OpenQ4_WriteSaveGameField( idFile *savefile, const idBounds &value, const char *context, const char *fieldName ) {
	return OpenQ4_WriteSaveGameField( savefile, value[ 0 ], context, fieldName ) &&
		OpenQ4_WriteSaveGameField( savefile, value[ 1 ], context, fieldName );
}

static ID_INLINE bool OpenQ4_WriteSaveGameField( idFile *savefile, const idRectangle &value, const char *context, const char *fieldName ) {
	return OpenQ4_WriteSaveGameField( savefile, value.x, context, fieldName ) &&
		OpenQ4_WriteSaveGameField( savefile, value.y, context, fieldName ) &&
		OpenQ4_WriteSaveGameField( savefile, value.w, context, fieldName ) &&
		OpenQ4_WriteSaveGameField( savefile, value.h, context, fieldName );
}

template< class type >
bool OpenQ4_WriteSaveGameField( idFile *savefile, const type &value, const char *context, const char *fieldName ) = delete;

static ID_INLINE bool OpenQ4_ReadSaveGameField( idFile *savefile, float &value, const char *context, const char *fieldName ) {
	const int offset = savefile->Tell();
	return OpenQ4_CheckSaveGameField( savefile->ReadFloat( value ), 4, offset, false, context, fieldName );
}

static ID_INLINE bool OpenQ4_ReadSaveGameField( idFile *savefile, int &value, const char *context, const char *fieldName ) {
	const int offset = savefile->Tell();
	return OpenQ4_CheckSaveGameField( savefile->ReadInt( value ), 4, offset, false, context, fieldName );
}

static ID_INLINE bool OpenQ4_ReadSaveGameField( idFile *savefile, unsigned int &value, const char *context, const char *fieldName ) {
	const int offset = savefile->Tell();
	return OpenQ4_CheckSaveGameField( savefile->ReadUnsignedInt( value ), 4, offset, false, context, fieldName );
}

static ID_INLINE bool OpenQ4_ReadSaveGameField( idFile *savefile, signed char &value, const char *context, const char *fieldName ) {
	const int offset = savefile->Tell();
	char raw = 0;
	const int bytesRead = savefile->ReadChar( raw );
	value = static_cast<signed char>( raw );
	return OpenQ4_CheckSaveGameField( bytesRead, 1, offset, false, context, fieldName );
}

static ID_INLINE bool OpenQ4_ReadSaveGameField( idFile *savefile, unsigned char &value, const char *context, const char *fieldName ) {
	const int offset = savefile->Tell();
	return OpenQ4_CheckSaveGameField( savefile->ReadUnsignedChar( value ), 1, offset, false, context, fieldName );
}

static ID_INLINE bool OpenQ4_ReadSaveGameField( idFile *savefile, bool &value, const char *context, const char *fieldName ) {
	return OpenQ4_ReadSaveGameBool( savefile, value, context, fieldName );
}

static ID_INLINE bool OpenQ4_ReadSaveGameField( idFile *savefile, idVec2 &value, const char *context, const char *fieldName ) {
	return OpenQ4_ReadSaveGameField( savefile, value.x, context, fieldName ) &&
		OpenQ4_ReadSaveGameField( savefile, value.y, context, fieldName );
}

static ID_INLINE bool OpenQ4_ReadSaveGameField( idFile *savefile, idVec3 &value, const char *context, const char *fieldName ) {
	return OpenQ4_ReadSaveGameField( savefile, value.x, context, fieldName ) &&
		OpenQ4_ReadSaveGameField( savefile, value.y, context, fieldName ) &&
		OpenQ4_ReadSaveGameField( savefile, value.z, context, fieldName );
}

static ID_INLINE bool OpenQ4_ReadSaveGameField( idFile *savefile, idVec4 &value, const char *context, const char *fieldName ) {
	return OpenQ4_ReadSaveGameField( savefile, value.x, context, fieldName ) &&
		OpenQ4_ReadSaveGameField( savefile, value.y, context, fieldName ) &&
		OpenQ4_ReadSaveGameField( savefile, value.z, context, fieldName ) &&
		OpenQ4_ReadSaveGameField( savefile, value.w, context, fieldName );
}

static ID_INLINE bool OpenQ4_ReadSaveGameField( idFile *savefile, idBounds &value, const char *context, const char *fieldName ) {
	return OpenQ4_ReadSaveGameField( savefile, value[ 0 ], context, fieldName ) &&
		OpenQ4_ReadSaveGameField( savefile, value[ 1 ], context, fieldName );
}

static ID_INLINE bool OpenQ4_ReadSaveGameField( idFile *savefile, idRectangle &value, const char *context, const char *fieldName ) {
	return OpenQ4_ReadSaveGameField( savefile, value.x, context, fieldName ) &&
		OpenQ4_ReadSaveGameField( savefile, value.y, context, fieldName ) &&
		OpenQ4_ReadSaveGameField( savefile, value.w, context, fieldName ) &&
		OpenQ4_ReadSaveGameField( savefile, value.h, context, fieldName );
}

template< class type >
bool OpenQ4_ReadSaveGameField( idFile *savefile, type &value, const char *context, const char *fieldName ) = delete;

// Aggregates as an earlier build laid them out (transition interpolators in
// payloads before version 5, which writes SaveCodec_WriteInterpolate).
template< class type >
static ID_INLINE bool OpenQ4_ReadSaveGameRawField( idFile *savefile, type &value, const char *context, const char *fieldName ) {
	return OpenQ4_ReadSaveGameBytes( savefile, &value, sizeof( value ), context, fieldName );
}

class idWindow;
class idWinVar {
public:
	idWinVar();
	virtual ~idWinVar();

	void SetGuiInfo(idDict *gd, const char *_name);
	const char *GetName() const { 
		if (name) {
			if (guiDict && *name == '*') {
				return guiDict->GetString(&name[1]);
			}
			return name;
		}
		return ""; 
	}
	void SetName(const char *_name) { 
		delete []name; 
		name = NULL;
		if (_name) {
			name = new char[strlen(_name)+1]; 
			strcpy(name, _name); 
		}
	}

	idWinVar &operator=( const idWinVar &other ) {
		guiDict = other.guiDict;
		SetName(other.name);
		return *this;
	}

	idDict *GetDict() const { return guiDict; }
	bool NeedsUpdate() { return (guiDict != NULL); }

	virtual void Init(const char *_name, idWindow* win) = 0;
	virtual void Set(const char *val) = 0;
	virtual void Update() = 0;
	virtual const char *c_str() const = 0;
	virtual size_t Size() {	size_t sz = (name) ? strlen(name) : 0; return sz + sizeof(*this); }

	virtual void WriteToSaveGame( idFile *savefile ) = 0;
	// A GUI register can end up holding a non-finite expression result. Saving must
	// not fail over one, so the register asks before writing and replaces the
	// affected components instead of refusing the whole save.
	virtual bool IsFinite() const { return true; }
	virtual void SanitizeNonFinite() {}
	virtual void ReadFromSaveGame( idFile *savefile ) = 0;

	virtual float x( void ) const = 0;

	void SetEval(bool b) {
		eval = b;
	}
	bool GetEval() {
		return eval;
	}
	
protected:
	idDict *guiDict;
	char *name;
	bool eval;
};

// Window-owned variables are plain members rather than named registers, so a
// non-finite value in one has no name of its own to report. The window does have
// a name, so it sanitizes on the way out and says which variable it was. Losing a
// cosmetic colour or rect component beats losing the save and the running map.
ID_INLINE void OpenQ4_SanitizeSaveGameWinVar( idWinVar &var, const char *context, const char *windowName, const char *varName ) {
	if ( var.IsFinite() ) {
		return;
	}
	common->Warning( "%s: window '%s' %s holds a non-finite value; saving zero for the affected components",
		context ? context : "savegame write", windowName ? windowName : "", varName ? varName : "variable" );
	var.SanitizeNonFinite();
}

class idWinBool : public idWinVar {
public:
	idWinBool() : idWinVar(), data( false ) {};
	~idWinBool() {};
	virtual void Init(const char *_name, idWindow *win) { idWinVar::Init(_name, win);
		if (guiDict) {
			data = guiDict->GetBool(GetName());
		}
	}
	int	operator==(	const bool &other ) { return (other == data); }
	bool &operator=(	const bool &other ) {
		data = other;
		if (guiDict) {
			guiDict->SetBool(GetName(), data);
		}
		return data;
	}
	idWinBool &operator=( const idWinBool &other ) {
		idWinVar::operator=(other);
		data = other.data;
		return *this;
	}

	operator bool() const { return data; }

	virtual void Set(const char *val) { 
		data = ( atoi( val ) != 0 );
		if (guiDict) {
			guiDict->SetBool(GetName(), data);
		}
	}

	virtual void Update() {	
		const char *s = GetName();
		if ( guiDict && s[0] != '\0' ) {
			data = guiDict->GetBool( s );
		}
	}

	virtual const char *c_str() const {return va("%i", data); }

	// SaveGames
	virtual void WriteToSaveGame( idFile *savefile ) {
		OpenQ4_WriteSaveGameBool( savefile, eval, "idWinBool::WriteToSaveGame", "eval flag" );
		OpenQ4_WriteSaveGameBool( savefile, data, "idWinBool::WriteToSaveGame", "value" );
	}
	virtual void ReadFromSaveGame( idFile *savefile ) {
		OpenQ4_ReadSaveGameBool( savefile, eval, "idWinBool::ReadFromSaveGame", "eval flag" );
		OpenQ4_ReadSaveGameBool( savefile, data, "idWinBool::ReadFromSaveGame", "value" );
	}

	virtual float x( void ) const { return data ? 1.0f : 0.0f; };

protected:
	bool data;
};

class idWinStr : public idWinVar {
public:
	idWinStr() : idWinVar() {};
	~idWinStr() {};
	virtual void Init(const char *_name, idWindow *win) {
		idWinVar::Init(_name, win);
		if (guiDict) {
			data = guiDict->GetString(GetName());
		} 
	}
	int	operator==(	const idStr &other ) const {
		return (other == data);
	}
	int	operator==(	const char *other ) const {
		return (data == other);
	}
	idStr &operator=(	const idStr &other ) {
		data = other;
		if (guiDict) {
			guiDict->Set(GetName(), data);
		}
		return data;
	}
	idWinStr &operator=( const idWinStr &other ) {
		idWinVar::operator=(other);
		data = other.data;
		return *this;
	}
	operator const char *() const {
		return data.c_str();
	}
	operator const idStr &() const {
		return data;
	}
	int LengthWithoutColors() {
		if (guiDict && name && *name) {
			data = guiDict->GetString(GetName());
		}
		return data.Length();
	}
	int Length() {
		if (guiDict && name && *name) {
			data = guiDict->GetString(GetName());
		}
		return data.Length();
	}
	void RemoveColors() {
		if (guiDict && name && *name) {
			data = guiDict->GetString(GetName());
		}
		//data.RemoveColors();
	}
	virtual const char *c_str() const {
		return data.c_str();
	}

	virtual void Set(const char *val) {
		data = val;
		if ( guiDict ) {
			guiDict->Set(GetName(), data);
		}
	}

	virtual void Update() {
		const char *s = GetName();
		if ( guiDict && s[0] != '\0' ) {
			data = guiDict->GetString( s );
		}
	}

	virtual size_t Size() {
		size_t sz = idWinVar::Size();
		return sz +data.Allocated();
	}

	// SaveGames
	virtual void WriteToSaveGame( idFile *savefile ) {
		if ( !OpenQ4_WriteSaveGameBool( savefile, eval, "idWinStr::WriteToSaveGame", "eval flag" ) ) {
			return;
		}

		const int len = data.Length();
		const int maxSavedStringLength = 64 * 1024;
		if ( len < 0 || len > maxSavedStringLength ) {
			common->Error( "idWinStr::WriteToSaveGame: invalid string length %d", len );
			return;
		}
		if ( !OpenQ4_WriteSaveGameInt( savefile, len, "idWinStr::WriteToSaveGame", "string length" ) ) {
			return;
		}
		if ( len > 0 ) {
			OpenQ4_WriteSaveGameBytes( savefile, data.c_str(), len, "idWinStr::WriteToSaveGame", "string" );
		}
	}
	virtual void ReadFromSaveGame( idFile *savefile ) {
		if ( !OpenQ4_ReadSaveGameBool( savefile, eval, "idWinStr::ReadFromSaveGame", "eval flag" ) ) {
			data.Clear();
			return;
		}

		int len = 0;
		const int offset = savefile->Tell();
		if ( !OpenQ4_ReadSaveGameInt( savefile, len, "idWinStr::ReadFromSaveGame", "string length" ) ) {
			data.Clear();
			return;
		}
		const int remainingBytes = Max( 0, savefile->Length() - savefile->Tell() );
		const int maxSavedStringLength = 64 * 1024;
		if ( len < 0 || len > maxSavedStringLength || len > remainingBytes ) {
			common->Error( "idWinStr::ReadFromSaveGame: invalid string length %d at offset %d (remaining %d)",
				len, offset, remainingBytes );
			data.Clear();
			return;
		}
		if ( len > 0 ) {
			data.Fill( ' ', len );
			if ( !OpenQ4_ReadSaveGameBytes( savefile, &data[0], len, "idWinStr::ReadFromSaveGame", "string" ) ) {
				data.Clear();
				return;
			}
		} else {
			data.Clear();
		}
	}

	// return wether string is emtpy
	virtual float x( void ) const { return data[0] ? 1.0f : 0.0f; };

protected:
	idStr data;
};

class idWinInt : public idWinVar {
public:
	idWinInt() : idWinVar(), data( 0 ) {};
	~idWinInt() {};
	virtual void Init(const char *_name, idWindow *win) {
		idWinVar::Init(_name,  win);
		if (guiDict) {
			data = guiDict->GetInt(GetName());
		} 
	}
	int &operator=(	const int &other ) {
		data = other;
		if (guiDict) {
			guiDict->SetInt(GetName(), data);
		}
		return data;
	}
	idWinInt &operator=( const idWinInt &other ) {
		idWinVar::operator=(other);
		data = other.data;
		return *this;
	}
	operator int () const {
		return data;
	}
	virtual void Set(const char *val) {
		data = atoi(val);;
		if (guiDict) {
			guiDict->SetInt(GetName(), data);
		}
	}

	virtual void Update() {
		const char *s = GetName();
		if ( guiDict && s[0] != '\0' ) {
			data = guiDict->GetInt( s );
		}
	}
	virtual const char *c_str() const {
		return va("%i", data);
	}

	// SaveGames
	virtual void WriteToSaveGame( idFile *savefile ) {
		OpenQ4_WriteSaveGameBool( savefile, eval, "idWinInt::WriteToSaveGame", "eval flag" );
		OpenQ4_WriteSaveGameInt( savefile, data, "idWinInt::WriteToSaveGame", "value" );
	}
	virtual void ReadFromSaveGame( idFile *savefile ) {
		OpenQ4_ReadSaveGameBool( savefile, eval, "idWinInt::ReadFromSaveGame", "eval flag" );
		OpenQ4_ReadSaveGameInt( savefile, data, "idWinInt::ReadFromSaveGame", "value" );
	}

	// no suitable conversion
	virtual float x( void ) const { assert( false ); return 0.0f; };

protected:
	int data;
};

class idWinFloat : public idWinVar {
public:
	virtual bool IsFinite() const { return std::isfinite( data ); }
	virtual void SanitizeNonFinite() {
		if ( !std::isfinite( data ) ) { data = 0.0f; }
	}
	friend class idWinFloatPtr;

	idWinFloat() : idWinVar(), data( 0.0f ) {};
	~idWinFloat() {};
	virtual void Init(const char *_name, idWindow *win) {
		idWinVar::Init(_name, win);
		if (guiDict) {
			data = guiDict->GetFloat(GetName());
		} 
	}
	idWinFloat &operator=( const idWinFloat &other ) {
		idWinVar::operator=(other);
		data = other.data;
		return *this;
	}
	float &operator=(	const float &other ) {
		data = other;
		if (guiDict) {
			guiDict->SetFloat(GetName(), data);
		}
		return data;
	}
	operator float() const {
		return data;
	}
	virtual void Set(const char *val) {
		data = atof(val);
		if (guiDict) {
			guiDict->SetFloat(GetName(), data);
		}
	}
	virtual void Update() {
		const char *s = GetName();
		if ( guiDict && s[0] != '\0' ) {
			data = guiDict->GetFloat( s );
		}
	}
	virtual const char *c_str() const {
		return va("%f", data);
	}

	virtual void WriteToSaveGame( idFile *savefile ) {
		OpenQ4_WriteSaveGameBool( savefile, eval, "idWinFloat::WriteToSaveGame", "eval flag" );
		OpenQ4_WriteSaveGameFloat( savefile, data, "idWinFloat::WriteToSaveGame", "value" );
	}
	virtual void ReadFromSaveGame( idFile *savefile ) {
		OpenQ4_ReadSaveGameBool( savefile, eval, "idWinFloat::ReadFromSaveGame", "eval flag" );
		OpenQ4_ReadSaveGameFloat( savefile, data, "idWinFloat::ReadFromSaveGame", "value" );
	}

	virtual float x( void ) const { return data; };
protected:
	float data;
};



class idWinRectangle : public idWinVar {
public:
	virtual bool IsFinite() const { return std::isfinite( data.x ) && std::isfinite( data.y ) && std::isfinite( data.w ) && std::isfinite( data.h ); }
	virtual void SanitizeNonFinite() {
		if ( !std::isfinite( data.x ) ) { data.x = 0.0f; }
		if ( !std::isfinite( data.y ) ) { data.y = 0.0f; }
		if ( !std::isfinite( data.w ) ) { data.w = 0.0f; }
		if ( !std::isfinite( data.h ) ) { data.h = 0.0f; }
	}
	idWinRectangle() : idWinVar() {};
	~idWinRectangle() {};
	virtual void Init(const char *_name, idWindow *win) {
		idWinVar::Init(_name, win);
		if (guiDict) {
			idVec4 v = guiDict->GetVec4(GetName());
			data.x = v.x;
			data.y = v.y;
			data.w = v.z;
			data.h = v.w;
		} 
	}
	
	int	operator==(	const idRectangle &other ) const {
		return (other == data);
	}

	idWinRectangle &operator=( const idWinRectangle &other ) {
		idWinVar::operator=(other);
		data = other.data;
		return *this;
	}
	idRectangle &operator=(	const idVec4 &other ) {
		data = other;
		if (guiDict) {
			guiDict->SetVec4(GetName(), other);
		}
		return data;
	}

	idRectangle &operator=(	const idRectangle &other ) {
		data = other;
		if (guiDict) {
			idVec4 v = data.ToVec4();
			guiDict->SetVec4(GetName(), v);
		}
		return data;
	}
	
	operator const idRectangle&() const {
		return data;
	}

	float x() const {
		return data.x;
	}
	float y() const {
		return data.y;
	}
	float w() const {
		return data.w;
	}
	float h() const {
		return data.h;
	}
	float Right() const {
		return data.Right();
	}
	float Bottom() const {
		return data.Bottom();
	}
	idVec4 &ToVec4() {
		static idVec4 ret;
		ret = data.ToVec4();
		return ret;
	}
	virtual void Set(const char *val) {
		if ( strchr ( val, ',' ) ) {
			sscanf( val, "%f,%f,%f,%f", &data.x, &data.y, &data.w, &data.h );
		} else {
			sscanf( val, "%f %f %f %f", &data.x, &data.y, &data.w, &data.h );
		}
		if (guiDict) {
			idVec4 v = data.ToVec4();
			guiDict->SetVec4(GetName(), v);
		}
	}
	virtual void Update() {
		const char *s = GetName();
		if ( guiDict && s[0] != '\0' ) {
			idVec4 v = guiDict->GetVec4( s );
			data.x = v.x;
			data.y = v.y;
			data.w = v.z;
			data.h = v.w;
		}
	}

	virtual const char *c_str() const {
		return data.ToVec4().ToString();
	}

	virtual void WriteToSaveGame( idFile *savefile ) {
		OpenQ4_WriteSaveGameBool( savefile, eval, "idWinRectangle::WriteToSaveGame", "eval flag" );
		OpenQ4_WriteSaveGameFloat( savefile, data.x, "idWinRectangle::WriteToSaveGame", "x" );
		OpenQ4_WriteSaveGameFloat( savefile, data.y, "idWinRectangle::WriteToSaveGame", "y" );
		OpenQ4_WriteSaveGameFloat( savefile, data.w, "idWinRectangle::WriteToSaveGame", "width" );
		OpenQ4_WriteSaveGameFloat( savefile, data.h, "idWinRectangle::WriteToSaveGame", "height" );
	}
	virtual void ReadFromSaveGame( idFile *savefile ) {
		OpenQ4_ReadSaveGameBool( savefile, eval, "idWinRectangle::ReadFromSaveGame", "eval flag" );
		OpenQ4_ReadSaveGameFloat( savefile, data.x, "idWinRectangle::ReadFromSaveGame", "x" );
		OpenQ4_ReadSaveGameFloat( savefile, data.y, "idWinRectangle::ReadFromSaveGame", "y" );
		OpenQ4_ReadSaveGameFloat( savefile, data.w, "idWinRectangle::ReadFromSaveGame", "width" );
		OpenQ4_ReadSaveGameFloat( savefile, data.h, "idWinRectangle::ReadFromSaveGame", "height" );
	}

protected:
	idRectangle data;
};

class idWinVec2 : public idWinVar {
public:
	virtual bool IsFinite() const { return std::isfinite( data.x ) && std::isfinite( data.y ); }
	virtual void SanitizeNonFinite() {
		if ( !std::isfinite( data.x ) ) { data.x = 0.0f; }
		if ( !std::isfinite( data.y ) ) { data.y = 0.0f; }
	}
	idWinVec2() : idWinVar(), data( 0.0f, 0.0f ) {};
	~idWinVec2() {};
	virtual void Init(const char *_name, idWindow *win) {
		idWinVar::Init(_name, win);
		if (guiDict) {
			data = guiDict->GetVec2(GetName());
		} 
	}
	int	operator==(	const idVec2 &other ) const {
		return (other == data);
	}
	idWinVec2 &operator=( const idWinVec2 &other ) {
		idWinVar::operator=(other);
		data = other.data;
		return *this;
	}
	
	idVec2 &operator=(	const idVec2 &other ) {
		data = other;
		if (guiDict) {
			guiDict->SetVec2(GetName(), data);
		}
		return data;
	}
	float x() const {
		return data.x;
	}
	float y() const {
		return data.y;
	}
	virtual void Set(const char *val) {
		if ( strchr ( val, ',' ) ) {
			sscanf( val, "%f,%f", &data.x, &data.y );
		} else {
		sscanf( val, "%f %f", &data.x, &data.y);
		}
		if (guiDict) {
			guiDict->SetVec2(GetName(), data);
		}
	}
	operator const idVec2&() const {
		return data;
	}
	virtual void Update() {
		const char *s = GetName();
		if ( guiDict && s[0] != '\0' ) {
			data = guiDict->GetVec2( s );
		}
	}
	virtual const char *c_str() const {
		return data.ToString();
	}
	void Zero() {
		data.Zero();
	}

	virtual void WriteToSaveGame( idFile *savefile ) {
		OpenQ4_WriteSaveGameBool( savefile, eval, "idWinVec2::WriteToSaveGame", "eval flag" );
		OpenQ4_WriteSaveGameFloat( savefile, data.x, "idWinVec2::WriteToSaveGame", "x" );
		OpenQ4_WriteSaveGameFloat( savefile, data.y, "idWinVec2::WriteToSaveGame", "y" );
	}
	virtual void ReadFromSaveGame( idFile *savefile ) {
		OpenQ4_ReadSaveGameBool( savefile, eval, "idWinVec2::ReadFromSaveGame", "eval flag" );
		OpenQ4_ReadSaveGameFloat( savefile, data.x, "idWinVec2::ReadFromSaveGame", "x" );
		OpenQ4_ReadSaveGameFloat( savefile, data.y, "idWinVec2::ReadFromSaveGame", "y" );
	}

protected:
	idVec2 data;
};

class idWinVec4 : public idWinVar {
	friend class idWinFloatPtr;

public:	
	virtual bool IsFinite() const { return std::isfinite( data.x ) && std::isfinite( data.y ) && std::isfinite( data.z ) && std::isfinite( data.w ); }
	virtual void SanitizeNonFinite() {
		if ( !std::isfinite( data.x ) ) { data.x = 0.0f; }
		if ( !std::isfinite( data.y ) ) { data.y = 0.0f; }
		if ( !std::isfinite( data.z ) ) { data.z = 0.0f; }
		if ( !std::isfinite( data.w ) ) { data.w = 0.0f; }
	}
	idWinVec4() : idWinVar(), data( 0.0f, 0.0f, 0.0f, 0.0f ) {};
	~idWinVec4() {};
	virtual void Init(const char *_name, idWindow *win) {
		idWinVar::Init(_name, win);
		if (guiDict) {
			data = guiDict->GetVec4(GetName());
		} 
	}
	int	operator==(	const idVec4 &other ) const {
		return (other == data);
	}
	idWinVec4 &operator=( const idWinVec4 &other ) {
		idWinVar::operator=(other);
		data = other.data;
		return *this;
	}
	idVec4 &operator=(	const idVec4 &other ) {
		data = other;
		if (guiDict) {
			guiDict->SetVec4(GetName(), data);
		}
		return data;
	}
	operator const idVec4&() const {
		return data;
	}

	float x() const {
		return data.x;
	}

	float y() const {
		return data.y;
	}

	float z() const {
		return data.z;
	}

	float w() const {
		return data.w;
	}
	virtual void Set(const char *val) {
		if ( strchr ( val, ',' ) ) {
			sscanf( val, "%f,%f,%f,%f", &data.x, &data.y, &data.z, &data.w );
		} else {
			sscanf( val, "%f %f %f %f", &data.x, &data.y, &data.z, &data.w);
		}
		if ( guiDict ) {
			guiDict->SetVec4( GetName(), data );
		}
	}
	virtual void Update() {
		const char *s = GetName();
		if ( guiDict && s[0] != '\0' ) {
			data = guiDict->GetVec4( s );
		}
	}
	virtual const char *c_str() const {
		return data.ToString();
	}

	void Zero() {
		data.Zero();
		if ( guiDict ) {
			guiDict->SetVec4(GetName(), data);
		}
	}

	const idVec3 &ToVec3() const {
		return data.ToVec3();
	}

	virtual void WriteToSaveGame( idFile *savefile ) {
		OpenQ4_WriteSaveGameBool( savefile, eval, "idWinVec4::WriteToSaveGame", "eval flag" );
		OpenQ4_WriteSaveGameFloat( savefile, data.x, "idWinVec4::WriteToSaveGame", "x" );
		OpenQ4_WriteSaveGameFloat( savefile, data.y, "idWinVec4::WriteToSaveGame", "y" );
		OpenQ4_WriteSaveGameFloat( savefile, data.z, "idWinVec4::WriteToSaveGame", "z" );
		OpenQ4_WriteSaveGameFloat( savefile, data.w, "idWinVec4::WriteToSaveGame", "w" );
	}
	virtual void ReadFromSaveGame( idFile *savefile ) {
		OpenQ4_ReadSaveGameBool( savefile, eval, "idWinVec4::ReadFromSaveGame", "eval flag" );
		OpenQ4_ReadSaveGameFloat( savefile, data.x, "idWinVec4::ReadFromSaveGame", "x" );
		OpenQ4_ReadSaveGameFloat( savefile, data.y, "idWinVec4::ReadFromSaveGame", "y" );
		OpenQ4_ReadSaveGameFloat( savefile, data.z, "idWinVec4::ReadFromSaveGame", "z" );
		OpenQ4_ReadSaveGameFloat( savefile, data.w, "idWinVec4::ReadFromSaveGame", "w" );
	}

protected:
	idVec4 data;
};

class idWinVec3 : public idWinVar {
public:
	virtual bool IsFinite() const { return std::isfinite( data.x ) && std::isfinite( data.y ) && std::isfinite( data.z ); }
	virtual void SanitizeNonFinite() {
		if ( !std::isfinite( data.x ) ) { data.x = 0.0f; }
		if ( !std::isfinite( data.y ) ) { data.y = 0.0f; }
		if ( !std::isfinite( data.z ) ) { data.z = 0.0f; }
	}
	idWinVec3() : idWinVar(), data( 0.0f, 0.0f, 0.0f ) {};
	~idWinVec3() {};
	virtual void Init(const char *_name, idWindow *win) {
		idWinVar::Init(_name, win);
		if (guiDict) {
			data = guiDict->GetVector(GetName());
		} 
	}
	int	operator==(	const idVec3 &other ) const {
		return (other == data);
	}
	idWinVec3 &operator=( const idWinVec3 &other ) {
		idWinVar::operator=(other);
		data = other.data;
		return *this;
	}
	idVec3 &operator=(	const idVec3 &other ) {
		data = other;
		if (guiDict) {
			guiDict->SetVector(GetName(), data);
		}
		return data;
	}
	operator const idVec3&() const {
		return data;
	}

	float x() const {
		return data.x;
	}

	float y() const {
		return data.y;
	}

	float z() const {
		return data.z;
	}

	virtual void Set(const char *val) {
		sscanf( val, "%f %f %f", &data.x, &data.y, &data.z);
		if (guiDict) {
			guiDict->SetVector(GetName(), data);
		}
	}
	virtual void Update() {
		const char *s = GetName();
		if ( guiDict && s[0] != '\0' ) {
			data = guiDict->GetVector( s );
		}
	}
	virtual const char *c_str() const {
		return data.ToString();
	}

	void Zero() {
		data.Zero();
		if (guiDict) {
			guiDict->SetVector(GetName(), data);
		}
	}

	virtual void WriteToSaveGame( idFile *savefile ) {
		OpenQ4_WriteSaveGameBool( savefile, eval, "idWinVec3::WriteToSaveGame", "eval flag" );
		OpenQ4_WriteSaveGameFloat( savefile, data.x, "idWinVec3::WriteToSaveGame", "x" );
		OpenQ4_WriteSaveGameFloat( savefile, data.y, "idWinVec3::WriteToSaveGame", "y" );
		OpenQ4_WriteSaveGameFloat( savefile, data.z, "idWinVec3::WriteToSaveGame", "z" );
	}
	virtual void ReadFromSaveGame( idFile *savefile ) {
		OpenQ4_ReadSaveGameBool( savefile, eval, "idWinVec3::ReadFromSaveGame", "eval flag" );
		OpenQ4_ReadSaveGameFloat( savefile, data.x, "idWinVec3::ReadFromSaveGame", "x" );
		OpenQ4_ReadSaveGameFloat( savefile, data.y, "idWinVec3::ReadFromSaveGame", "y" );
		OpenQ4_ReadSaveGameFloat( savefile, data.z, "idWinVec3::ReadFromSaveGame", "z" );
	}

protected:
	idVec3 data;
};

class idWinBackground : public idWinStr {
public:
	idWinBackground() : idWinStr() {
		mat = NULL;
	};
	~idWinBackground() {};
	virtual void Init(const char *_name, idWindow *win) {
		idWinStr::Init(_name, win);
		if (guiDict) {
			data = guiDict->GetString(GetName());
		} 
	}
	int	operator==(	const idStr &other ) const {
		return (other == data);
	}
	int	operator==(	const char *other ) const {
		return (data == other);
	}
	idStr &operator=(	const idStr &other ) {
		data = other;
		if (guiDict) {
			guiDict->Set(GetName(), data);
		}
		if (mat) {
			if ( data == "" ) {
				(*mat) = NULL;
			} else {
				(*mat) = declManager->FindMaterial(data);
			}
		}
		return data;
	}
	idWinBackground &operator=( const idWinBackground &other ) {
		idWinVar::operator=(other);
		data = other.data;
		mat = other.mat;
		if (mat) {
			if ( data == "" ) {
				(*mat) = NULL;
			} else {
				(*mat) = declManager->FindMaterial(data);
			}
		}
		return *this;
	}
	operator const char *() const {
		return data.c_str();
	}
	operator const idStr &() const {
		return data;
	}
	int Length() {
		if (guiDict) {
			data = guiDict->GetString(GetName());
		}
		return data.Length();
	}
	virtual const char *c_str() const {
		return data.c_str();
	}

	virtual void Set(const char *val) {
		data = val;
		if (guiDict) {
			guiDict->Set(GetName(), data);
		}
		if (mat) {
			if ( data == "" ) {
				(*mat) = NULL;
			} else {
				(*mat) = declManager->FindMaterial(data);
			}
		}
	}

	virtual void Update() {
		const char *s = GetName();
		if ( guiDict && s[0] != '\0' ) {
			data = guiDict->GetString( s );
			if (mat) {
				if ( data == "" ) {
					(*mat) = NULL;
				} else {
					(*mat) = declManager->FindMaterial(data);
				}
			}
		}
	}

	virtual size_t Size() {
		size_t sz = idWinVar::Size();
		return sz +data.Allocated();
	}

	void SetMaterialPtr( const idMaterial **m ) {
		mat = m;
	}

	virtual void WriteToSaveGame( idFile *savefile ) {
		if ( !OpenQ4_WriteSaveGameBool( savefile, eval, "idWinBackground::WriteToSaveGame", "eval flag" ) ) {
			return;
		}

		const int len = data.Length();
		const int maxSavedStringLength = 64 * 1024;
		if ( len < 0 || len > maxSavedStringLength ) {
			common->Error( "idWinBackground::WriteToSaveGame: invalid material name length %d", len );
			return;
		}
		if ( !OpenQ4_WriteSaveGameInt( savefile, len, "idWinBackground::WriteToSaveGame", "material name length" ) ) {
			return;
		}
		if ( len > 0 ) {
			OpenQ4_WriteSaveGameBytes( savefile, data.c_str(), len, "idWinBackground::WriteToSaveGame", "material name" );
		}
	}
	virtual void ReadFromSaveGame( idFile *savefile ) {
		if ( !OpenQ4_ReadSaveGameBool( savefile, eval, "idWinBackground::ReadFromSaveGame", "eval flag" ) ) {
			data.Clear();
			if ( mat != NULL ) {
				*mat = NULL;
			}
			return;
		}

		int len = 0;
		const int offset = savefile->Tell();
		if ( !OpenQ4_ReadSaveGameInt( savefile, len, "idWinBackground::ReadFromSaveGame", "material name length" ) ) {
			data.Clear();
			if ( mat != NULL ) {
				*mat = NULL;
			}
			return;
		}
		const int remainingBytes = Max( 0, savefile->Length() - savefile->Tell() );
		const int maxSavedStringLength = 64 * 1024;
		if ( len < 0 || len > maxSavedStringLength || len > remainingBytes ) {
			common->Error( "idWinBackground::ReadFromSaveGame: invalid material name length %d at offset %d (remaining %d)",
				len, offset, remainingBytes );
			data.Clear();
			if ( mat != NULL ) {
				*mat = NULL;
			}
			return;
		}
		if ( len > 0 ) {
			data.Fill( ' ', len );
			if ( !OpenQ4_ReadSaveGameBytes( savefile, &data[0], len, "idWinBackground::ReadFromSaveGame", "material name" ) ) {
				data.Clear();
				if ( mat != NULL ) {
					*mat = NULL;
				}
				return;
			}
		} else {
			data.Clear();
		}
		if ( mat ) {
			if ( len > 0 ) {
				(*mat) = declManager->FindMaterial( data );
			} else {
				(*mat) = NULL;
			}
		}
	}

protected:
	idStr data;
	const idMaterial **mat;
};

/*
================
idMultiWinVar
multiplexes access to a list if idWinVar*
================
*/
class idMultiWinVar : public idList< idWinVar * > {
public:
	void Set( const char *val );
	void Update( void );
	void SetGuiInfo( idDict *dict );
};



class idWinFloatPtr : public idWinVar {
public:
	idWinFloatPtr() : idWinVar(), data(NULL), ownerVec4(NULL) {};
	~idWinFloatPtr() {};
	virtual void Init(const char* _name, idWindow* win) {

	}

	float& operator=(const float& other) {
		*data = other;
		if (guiDict) {
			guiDict->SetFloat(GetName(), *data);
		}
		return *data;
	}

	void Bind(idWinVec4& ptr, int component)
	{
		ownerVec4 = &ptr;
		switch (component)
		{
		case 0:
			data = &ptr.data[0];
			break;
		case 1:
			data = &ptr.data[1];
			break;
		case 2:
			data = &ptr.data[2];
			break;
		case 3:
			data = &ptr.data[3];
			break;
		}
	}

	idWinVec4* GetOwnerVec4() const {
		return ownerVec4;
	}

	virtual void Set(const char* val) {
		*data = atof(val);
		if (guiDict) {
			guiDict->SetFloat(GetName(), *data);
		}
	}
	virtual void Update() {
		const char* s = GetName();
		if (guiDict && s[0] != '\0') {
			*data = guiDict->GetFloat(s);
		}
	}
	virtual const char* c_str() const {
		return va("%f", *data);
	}
	virtual void WriteToSaveGame(idFile* savefile) { }
	virtual void ReadFromSaveGame(idFile* savefile) { }
	virtual float x(void) const { return *data; };
protected:
	float* data;
	idWinVec4* ownerVec4;
};

#endif /* !__WINVAR_H__ */

