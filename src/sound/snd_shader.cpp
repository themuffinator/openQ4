/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2014-2016 Robert Beckebans
Copyright (C) 2014-2016 Kot in Action Creative Artel

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "snd_local.h"

extern idCVar s_maxSoundsPerShader;

static const int SOUND_SHADER_MAX_SAMPLES = 32;
static const float SOUND_SHADER_DEFAULT_FREQUENCY_SHIFT = 1.0f;
static const float SOUND_SHADER_MAX_VOLUME_DB = 10.0f;
static const char *SOUND_SHADER_DEFAULT_DESCRIPTION = "<no description>";

class rvSoundShaderEditLocal : public rvSoundShaderEdit {
public:
	virtual const char* GetSampleName( const idSoundShader* sound, int index ) const override {
		return sound != NULL ? sound->GetSampleName( index ) : "";
	}
	virtual int GetSamplesPerSec( const idSoundShader* sound, int index ) const override {
		return sound != NULL ? sound->GetSamplesPerSec( index ) : 0;
	}
	virtual int GetNumChannels( const idSoundShader* sound, int index ) const override {
		return sound != NULL ? sound->GetNumChannels( index ) : 0;
	}
	virtual int GetNumSamples( const idSoundShader* sound, int index ) const override {
		return sound != NULL ? sound->GetNumSamples( index ) : 0;
	}
	virtual int GetMemorySize( const idSoundShader* sound, int index ) const override {
		return sound != NULL ? sound->GetMemorySize( index ) : 0;
	}
	virtual const byte* GetNonCacheData( const idSoundShader* sound, int index ) const override {
		return sound != NULL ? sound->GetNonCacheData( index ) : NULL;
	}
	virtual void LoadSampleData( idSoundShader* sound, int langIndex = -1 ) override {
		if ( sound != NULL ) {
			sound->LoadSampleData( langIndex );
		}
	}
	virtual void ExpandSmallOggs( idSoundShader* sound, bool force ) override {
		if ( sound != NULL ) {
			sound->ExpandSmallOggs( force );
		}
	}
	virtual const char* GetShakeData( idSoundShader* sound, int index ) override {
		return sound != NULL ? sound->GetShakeData( index ) : "";
	}
	virtual void SetShakeData( idSoundShader* sound, int index, const char* ampData ) override {
		if ( sound != NULL ) {
			sound->SetShakeData( index, ampData );
		}
	}
	virtual void Purge( idSoundShader* sound, bool freeBaseBlocks ) override {
		if ( sound != NULL ) {
			sound->Purge( freeBaseBlocks );
		}
	}
};

static rvSoundShaderEditLocal localSoundShaderEdit;
rvSoundShaderEdit* soundShaderEdit = &localSoundShaderEdit;

static bool SND_IsMusicSamplePath( const idToken &token ) {
	return token.IcmpPrefixPath( "sound/musical/" ) == 0 ||
		token.IcmpPrefixPath( "sound/ambience/musical/" ) == 0;
}

//typedef enum
//{
//	SPEAKER_LEFT = 0,
//	SPEAKER_RIGHT,
//	SPEAKER_CENTER,
//	SPEAKER_LFE,
//	SPEAKER_BACKLEFT,
//	SPEAKER_BACKRIGHT
//} speakerLabel;

/*
===============
idSoundShader::Init
===============
*/
void idSoundShader::Init()
{
	desc = SOUND_SHADER_DEFAULT_DESCRIPTION;
	minFrequencyShift = SOUND_SHADER_DEFAULT_FREQUENCY_SHIFT;
	maxFrequencyShift = SOUND_SHADER_DEFAULT_FREQUENCY_SHIFT;
	noShakes = false;
	frequentlyUsed = false;
	leadinVolume = 1.0f;
	altSound = NULL;
	leadins.Clear();
	entries.Clear();
	shakes.Clear();
}

/*
===============
idSoundShader::idSoundShader
===============
*/
idSoundShader::idSoundShader()
{
	Init();
}

/*
===============
idSoundShader::~idSoundShader
===============
*/
idSoundShader::~idSoundShader()
{
}

/*
=================
idSoundShader::Size
=================
*/
size_t idSoundShader::Size() const
{
	size_t bytes = sizeof( idSoundShader ) + desc.Allocated() + leadins.Allocated() + entries.Allocated() + shakes.Allocated();
	for ( int i = 0; i < shakes.Num(); i++ ) {
		bytes += shakes[i].Allocated();
	}
	return bytes;
}

/*
===============
idSoundShader::idSoundShader::FreeData
===============
*/
void idSoundShader::FreeData()
{
	desc = SOUND_SHADER_DEFAULT_DESCRIPTION;
	minFrequencyShift = SOUND_SHADER_DEFAULT_FREQUENCY_SHIFT;
	maxFrequencyShift = SOUND_SHADER_DEFAULT_FREQUENCY_SHIFT;
	noShakes = false;
	frequentlyUsed = false;
	altSound = NULL;
	leadins.Clear();
	entries.Clear();
	shakes.Clear();
}

/*
===================
idSoundShader::SetDefaultText
===================
*/
bool idSoundShader::SetDefaultText()
{
	idStr wavname;

	wavname = GetName();
	wavname.DefaultFileExtension( ".wav" );		// if the name has .ogg in it, that will stay

	// if there exists a wav file with the same name
	if( 1 )    //fileSystem->ReadFile( wavname, NULL ) != -1 ) {
	{
		char generated[2048];
		idStr::snPrintf( generated, sizeof( generated ),
						 "sound %s // IMPLICITLY GENERATED\n"
						 "{\n"
						 "%s\n"
						 "}\n", GetName(), wavname.c_str() );
		SetText( generated );
		return true;
	}
	else
	{
		return false;
	}
}

/*
===================
DefaultDefinition
===================
*/
const char* idSoundShader::DefaultDefinition() const
{
	return
		"{\n"
		"\t"	"_default.wav\n"
		"}";
}

/*
===============
idSoundShader::Parse

  this is called by the declManager
===============
*/
bool idSoundShader::Parse( const char* text, const int textLength )
{
	if( soundSystemLocal.currentSoundWorld )
	{
		soundSystemLocal.currentSoundWorld->WriteSoundShaderLoad( this );
	}

	idLexer	src;

	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SetFlags( DECL_LEXER_FLAGS );
	src.SkipUntilString( "{" );

	if( !ParseShader( src ) )
	{
		MakeDefault();
		return false;
	}
	return true;
}

/*
===============
idSoundShader::Parse
===============
*/
bool idSoundShader::Parse( const char* text, const int textLength, bool noCaching )
{
	(void)noCaching;
	return Parse( text, textLength );
}

/*
===============
idSoundShader::Validate
===============
*/
bool idSoundShader::Validate( const char *psText, int iTextLength, idStr &strReportTo ) const {
	(void)strReportTo;

	idDecl *decl = declManager->AllocateDecl( DECL_SOUND );
	const bool valid = DeclManager_ValidateParsedDecl( decl, DECL_SOUND, decl != NULL && decl->Parse( psText, iTextLength, false ) );
	DeclManager_FreeAllocatedDecl( decl );
	return valid;
}

/*
===============
idSoundShader::RebuildTextSource
===============
*/
bool idSoundShader::RebuildTextSource() {
	idFile_Memory file;

	file.WriteFloatString( "\r\n\r\nsound %s\r\n{\r\n", GetName() );

	if ( desc.Length() > 0 && desc.Icmp( SOUND_SHADER_DEFAULT_DESCRIPTION ) != 0 ) {
		file.WriteFloatString( "\tdescription\t\"%s\"\r\n\r\n", desc.c_str() );
	}

	file.WriteFloatString( "\tminDistance\t%d\r\n", (int)parms.minDistance );
	file.WriteFloatString( "\tmaxDistance\t%d\r\n", (int)parms.maxDistance );
	file.WriteFloatString( "\tvolumeDb\t%.2g\r\n", idMath::ScaleToDb( parms.volume ) );

	if ( minFrequencyShift != SOUND_SHADER_DEFAULT_FREQUENCY_SHIFT ||
			maxFrequencyShift != SOUND_SHADER_DEFAULT_FREQUENCY_SHIFT ) {
		file.WriteFloatString( "\tfrequencyshift\t%.4g,%.4g\r\n", minFrequencyShift, maxFrequencyShift );
	}
	if ( leadinVolume != 1.0f ) {
		file.WriteFloatString( "\tleadinVolume\t%.4g\r\n", leadinVolume );
	}
	if ( parms.shakes != 0.0f ) {
		file.WriteFloatString( "\tshakes\t\t%.2g\r\n", parms.shakes );
	}
	if ( parms.soundClass != 0 ) {
		file.WriteFloatString( "\tsoundClass\t%d\r\n", parms.soundClass );
	}
	if ( altSound != NULL ) {
		file.WriteFloatString( "\taltSound\t%s\r\n", altSound->GetName() );
	}

	if ( parms.soundShaderFlags & SSF_PRIVATE_SOUND ) {
		file.WriteFloatString( "\tprivate\r\n" );
	}
	if ( parms.soundShaderFlags & SSF_ANTI_PRIVATE_SOUND ) {
		file.WriteFloatString( "\tantiPrivate\r\n" );
	}
	if ( parms.soundShaderFlags & SSF_NO_OCCLUSION ) {
		file.WriteFloatString( "\tno_occlusion\r\n" );
	}
	if ( parms.soundShaderFlags & SSF_GLOBAL ) {
		file.WriteFloatString( "\tglobal\r\n" );
	}
	if ( parms.soundShaderFlags & SSF_OMNIDIRECTIONAL ) {
		file.WriteFloatString( "\tomnidirectional\r\n" );
	}
	if ( parms.soundShaderFlags & SSF_LOOPING ) {
		file.WriteFloatString( "\tlooping\r\n" );
	}
	if ( parms.soundShaderFlags & SSF_PLAY_ONCE ) {
		file.WriteFloatString( "\tplayonce\r\n" );
	}
	if ( parms.soundShaderFlags & SSF_UNCLAMPED ) {
		file.WriteFloatString( "\tunclamped\r\n" );
	}
	if ( parms.soundShaderFlags & SSF_NO_FLICKER ) {
		file.WriteFloatString( "\tno_flicker\r\n" );
	}
	if ( parms.soundShaderFlags & SSF_NO_DUPS ) {
		file.WriteFloatString( "\tno_dups\r\n" );
	}
	if ( parms.soundShaderFlags & SSF_USEDOPPLER ) {
		file.WriteFloatString( "\tuseDoppler\r\n" );
	}
	if ( parms.soundShaderFlags & SSF_NO_RANDOMSTART ) {
		file.WriteFloatString( "\tnoRandomStart\r\n" );
	}
	if ( parms.soundShaderFlags & SSF_VO_FOR_PLAYER ) {
		file.WriteFloatString( "\tvoForPlayer\r\n" );
	}
	if ( noShakes ) {
		file.WriteFloatString( "\tno_shakes\r\n" );
	}
	if ( frequentlyUsed ) {
		file.WriteFloatString( "\tfrequentlyUsed\r\n" );
	}

	file.WriteFloatString( "\r\n" );

	for ( int i = 0; i < leadins.Num(); i++ ) {
		if ( leadins[i] != NULL ) {
			file.WriteFloatString( "\tleadin\t%s\r\n", leadins[i]->GetName() );
		}
	}

	for ( int i = 0; i < entries.Num(); i++ ) {
		if ( entries[i] != NULL ) {
			file.WriteFloatString( "\t%s\r\n", entries[i]->GetName() );
		}
		if ( i < shakes.Num() && shakes[i].Length() > 0 ) {
			file.WriteFloatString( "\tshakeData %d %s\r\n", i, shakes[i].c_str() );
		}
	}

	file.WriteFloatString( "}\r\n" );
	SetText( file.GetDataPtr() );
	return true;
}

/*
===============
idSoundShader::ParseShader
===============
*/
bool idSoundShader::ParseShader( idLexer& src )
{
	idToken		token;

	// Quake 4 shaders author these distances in world units; keep stock defaults.
	parms.minDistance = 40.0f;
	parms.maxDistance = 400.0f;
	parms.volume = 1;
	parms.shakes = 0;
	parms.soundShaderFlags = 0;
	parms.soundClass = 0;
	parms.frequencyShift = 1.0f;
	parms.wetLevel = 0.0f;
	parms.dryLevel = 1.0f;
	minFrequencyShift = SOUND_SHADER_DEFAULT_FREQUENCY_SHIFT;
	maxFrequencyShift = SOUND_SHADER_DEFAULT_FREQUENCY_SHIFT;
	desc = SOUND_SHADER_DEFAULT_DESCRIPTION;
	noShakes = false;
	frequentlyUsed = false;

	speakerMask = 0;
	altSound = NULL;
	leadinVolume = 1.0f;

	leadins.Clear();
	entries.Clear();
	shakes.Clear();
	int maxSamples = s_maxSoundsPerShader.GetInteger();
	if ( com_makingBuild.GetBool() || maxSamples <= 0 || maxSamples > SOUND_SHADER_MAX_SAMPLES ) {
		maxSamples = SOUND_SHADER_MAX_SAMPLES;
	}

	while( 1 )
	{
		if( !src.ExpectAnyToken( &token ) )
		{
			return false;
		}
		// end of definition
		else if( token == "}" )
		{
			break;
		}
		// minimum number of sounds
		else if( !token.Icmp( "minSamples" ) )
		{
			const int requestedSamples = src.ParseInt();
			if ( requestedSamples > maxSamples ) {
				maxSamples = requestedSamples;
				if ( maxSamples > SOUND_SHADER_MAX_SAMPLES ) {
					maxSamples = SOUND_SHADER_MAX_SAMPLES;
				}
			}
		}
		// description
		else if( !token.Icmp( "description" ) )
		{
			src.ReadTokenOnLine( &token );
			desc = token.c_str();
		}
		else if (!token.Icmp("mindistance")) {
			parms.minDistance = src.ParseFloat();
		}
		// maxdistance
		else if (!token.Icmp("maxdistance")) {
			parms.maxDistance = src.ParseFloat();
		}
// jmarshall - quake 4 sound shader
		else if (!token.Icmp("frequencyshift")) {
			const float minShift = src.ParseFloat();
			src.ExpectTokenString(",");
			const float maxShift = src.ParseFloat();
			minFrequencyShift = minShift;
			maxFrequencyShift = maxShift;
			parms.frequencyShift = 0.5f * (minShift + maxShift);
		}
		else if (!token.Icmp("volumeDb")) {
			float db = src.ParseFloat();
			if ( db > SOUND_SHADER_MAX_VOLUME_DB ) {
				common->Warning( "Clamping volume of '%s' to +10dB (3 times louder)", GetName() );
				db = SOUND_SHADER_MAX_VOLUME_DB;
			}
			parms.volume = idMath::dBToScale(db);
		}
		else if (!token.Icmp("useDoppler")) {
			parms.soundShaderFlags |= SSF_USEDOPPLER;
		}
		else if (!token.Icmp("noRandomStart")) {
			parms.soundShaderFlags |= SSF_NO_RANDOMSTART;
		}
		else if (!token.Icmp("voForPlayer")) {
			parms.soundShaderFlags |= SSF_VO_FOR_PLAYER;
		}
		else if (!token.Icmp("frequentlyUsed")) {
			frequentlyUsed = true;
		}
		else if (!token.Icmp("causeRumble")) {
			parms.soundShaderFlags |= SSF_CAUSE_RUMBLE;
		}
		else if (!token.Icmp("center")) {
			parms.soundShaderFlags |= SSF_CENTER;
		}
// jmarshall end
		else if (!token.Icmp("shakeData"))
		{
			const int shakeIndex = src.ParseInt();
			if( !src.ExpectAnyToken( &token ) )
			{
				return false;
			}
			if( shakeIndex < 0 )
			{
				src.Warning( "shakeData index out of range" );
			}
			else
			{
				if( shakes.Num() <= shakeIndex )
				{
					shakes.SetNum( shakeIndex + 1 );
				}
				shakes[shakeIndex] = token.c_str();
			}
		}
		else if( !token.Icmp( "shakes" ) )
		{
			src.ExpectAnyToken( &token );
			if( token.type == TT_NUMBER )
			{
				parms.shakes = token.GetFloatValue();
			}
			else
			{
				src.UnreadToken( &token );
				parms.shakes = 1.0f;
			}
		}
		// reverb
		else if( !token.Icmp( "reverb" ) )
		{
			parms.wetLevel = src.ParseFloat();
			if( !src.ExpectTokenString( "," ) )
			{
				src.FreeSource();
				return false;
			}
			parms.dryLevel = src.ParseFloat();
		}
		// volume
		else if( !token.Icmp( "volume" ) )
		{
			parms.volume = src.ParseFloat();
		}
		// leadinVolume is used to allow light breaking leadin sounds to be much louder than the broken loop
		else if( !token.Icmp( "leadinVolume" ) )
		{
			leadinVolume = src.ParseFloat();
		}
		// speaker mask
		else if( !token.Icmp( "mask_center" ) )
		{
			speakerMask |= 1 << SPEAKER_CENTER;
		}
		// speaker mask
		else if( !token.Icmp( "mask_left" ) )
		{
			speakerMask |= 1 << SPEAKER_LEFT;
		}
		// speaker mask
		else if( !token.Icmp( "mask_right" ) )
		{
			speakerMask |= 1 << SPEAKER_RIGHT;
		}
		// speaker mask
		else if( !token.Icmp( "mask_backright" ) )
		{
			speakerMask |= 1 << SPEAKER_BACKRIGHT;
		}
		// speaker mask
		else if( !token.Icmp( "mask_backleft" ) )
		{
			speakerMask |= 1 << SPEAKER_BACKLEFT;
		}
		// speaker mask
		else if( !token.Icmp( "mask_lfe" ) )
		{
			speakerMask |= 1 << SPEAKER_LFE;
		}
		// soundClass
		else if( !token.Icmp( "soundClass" ) )
		{
			parms.soundClass = src.ParseInt();
			if( parms.soundClass < 0 || parms.soundClass >= SOUND_MAX_CLASSES )
			{
				src.Warning( "SoundClass out of range" );
				return false;
			}
		}
		// altSound
		else if( !token.Icmp( "altSound" ) )
		{
			if( !src.ExpectAnyToken( &token ) )
			{
				return false;
			}
			altSound = declManager->FindSound( token.c_str() );
		}
		// ordered
		else if( !token.Icmp( "ordered" ) )
		{
			// no longer supported
		}
		// no_dups
		else if( !token.Icmp( "no_dups" ) )
		{
			parms.soundShaderFlags |= SSF_NO_DUPS;
		}
		// no_flicker
		else if( !token.Icmp( "no_flicker" ) )
		{
			parms.soundShaderFlags |= SSF_NO_FLICKER;
		}
		else if( !token.Icmp( "no_shakes" ) )
		{
			noShakes = true;
			parms.shakes = 0.0f;
		}
		// plain
		else if( !token.Icmp( "plain" ) )
		{
			// no longer supported
		}
		// looping
		else if( !token.Icmp( "looping" ) )
		{
			parms.soundShaderFlags |= SSF_LOOPING;
		}
		// no occlusion
		else if( !token.Icmp( "no_occlusion" ) )
		{
			parms.soundShaderFlags |= SSF_NO_OCCLUSION;
		}
		// private
		else if( !token.Icmp( "private" ) )
		{
			parms.soundShaderFlags |= SSF_PRIVATE_SOUND;
		}
		// antiPrivate
		else if( !token.Icmp( "antiPrivate" ) )
		{
			parms.soundShaderFlags |= SSF_ANTI_PRIVATE_SOUND;
		}
		// once
		else if( !token.Icmp( "playonce" ) )
		{
			parms.soundShaderFlags |= SSF_PLAY_ONCE;
		}
		// global
		else if( !token.Icmp( "global" ) )
		{
			parms.soundShaderFlags |= SSF_GLOBAL;
		}
		// unclamped
		else if( !token.Icmp( "unclamped" ) )
		{
			parms.soundShaderFlags |= SSF_UNCLAMPED;
		}
		// omnidirectional
		else if( !token.Icmp( "omnidirectional" ) )
		{
			parms.soundShaderFlags |= SSF_OMNIDIRECTIONAL;
		}
		else if( !token.Icmp( "onDemand" ) )
		{
			// no longer loading sounds on demand
		}
		// the wave files
		else if( !token.Icmp( "leadin" ) )
		{
			if( !src.ExpectAnyToken( &token ) )
			{
				src.Warning( "Expected sound after leadin" );
				return false;
			}

			idStr sampleExtension;
			token.ExtractFileExtension( sampleExtension );
			if( sampleExtension.Length() == 0 )
			{
				token.SetFileExtension( ".wav" );
			}

			if( token.IcmpPrefixPath( "sound/vo/" ) == 0 || token.IcmpPrefixPath( "sound/guis/" ) == 0 )
			{
				parms.soundShaderFlags |= SSF_VO;
				parms.soundShaderFlags |= SSF_IS_VO;
			}
			if( SND_IsMusicSamplePath( token ) )
			{
				parms.soundShaderFlags |= SSF_MUSIC;
			}

			if( leadins.Num() < maxSamples )
			{
				leadins.Append( soundSystemLocal.LoadSample( token.c_str() ) );
			}
		}
		else 
		{
			idStr sampleExtension;
			token.ExtractFileExtension( sampleExtension );
			if( sampleExtension.Length() == 0 )
			{
				token.SetFileExtension( ".wav" );
			}
			if( token.IcmpPrefixPath( "sound/vo/" ) == 0 || token.IcmpPrefixPath( "sound/guis/" ) == 0 )
			{
				parms.soundShaderFlags |= SSF_VO;
				parms.soundShaderFlags |= SSF_IS_VO;
			}
			if( SND_IsMusicSamplePath( token ) )
			{
				parms.soundShaderFlags |= SSF_MUSIC;
			}
			// add to the wav list
			if( entries.Num() < maxSamples )
			{
				entries.Append( soundSystemLocal.LoadSample( token.c_str() ) );
			}
		}
		//else
		//{
		//	src.Warning( "unknown token '%s'", token.c_str() );
		//	return false;
		//}
	}

	return true;
}

/*
===============
idSoundShader::List
===============
*/
void idSoundShader::List() const
{
	idStrList	shaders;

	common->Printf( "%4i: %s\n", Index(), GetName() );
	if ( desc.Icmp( SOUND_SHADER_DEFAULT_DESCRIPTION ) != 0 ) {
		common->Printf( "      description: %s\n", desc.c_str() );
	}
	for( int k = 0; k < leadins.Num(); k++ )
	{
		const idSoundSample* objectp = leadins[k];
		if( objectp )
		{
			common->Printf( "      %5dms %4dKb leadin %s\n", objectp->LengthInMsec(), ( objectp->BufferSize() / 1024 ), objectp->GetName() );
		}
	}
	for( int k = 0; k < entries.Num(); k++ )
	{
		const idSoundSample* objectp = entries[k];
		if( objectp )
		{
			common->Printf( "      %5dms %4dKb %s\n", objectp->LengthInMsec(), ( objectp->BufferSize() / 1024 ), objectp->GetName() );
		}
	}
}

/*
===============
idSoundShader::SetReferencedThisLevel
===============
*/
void idSoundShader::SetReferencedThisLevel()
{
	idDecl::SetReferencedThisLevel();

	for( int i = 0; i < leadins.Num(); i++ )
	{
		if( leadins[i] )
		{
			leadins[i]->SetLevelLoadReferenced();
		}
	}
	for( int i = 0; i < entries.Num(); i++ )
	{
		if( entries[i] )
		{
			entries[i]->SetLevelLoadReferenced();
		}
	}
}

/*
===============
idSoundShader::GetAltSound
===============
*/
const idSoundShader* idSoundShader::GetAltSound() const
{
	return altSound;
}

/*
===============
idSoundShader::GetMinDistance
===============
*/
float idSoundShader::GetMinDistance() const
{
	return parms.minDistance;
}

/*
===============
idSoundShader::GetMaxDistance
===============
*/
float idSoundShader::GetMaxDistance() const
{
	return parms.maxDistance;
}

/*
===============
idSoundShader::HasDefaultSound
===============
*/
bool idSoundShader::HasDefaultSound() const
{
	for( int i = 0; i < entries.Num(); i++ )
	{
		if( entries[i] && entries[i]->IsDefault() )
		{
			return true;
		}
	}
	return false;
}

/*
===============
idSoundShader::GetParms
===============
*/
const soundShaderParms_t* idSoundShader::GetParms() const
{
	return &parms;
}

/*
===============
idSoundShader::GetSampleByIndex
===============
*/
idSoundSample* idSoundShader::GetSampleByIndex( int index ) const
{
	if( index < 0 )
	{
		return NULL;
	}
	if( index < leadins.Num() )
	{
		return leadins[index];
	}
	index -= leadins.Num();
	if( index < entries.Num() )
	{
		return entries[index];
	}
	return NULL;
}

/*
===============
idSoundShader::GetNumSounds
===============
*/
int idSoundShader::GetNumSounds() const
{
	return leadins.Num() + entries.Num();
}

/*
===============
idSoundShader::GetSound
===============
*/
const char* idSoundShader::GetSound( int index ) const
{
	return GetSampleName( index );
}

/*
===============
idSoundShader::GetTimeLength
===============
*/
float idSoundShader::GetTimeLength() const
{
	int maxLengthMs = 0;
	for( int i = 0; i < entries.Num(); ++i )
	{
		if( !entries[i] )
		{
			continue;
		}
		maxLengthMs = Max( maxLengthMs, entries[i]->LengthInMsec() );
	}

	return maxLengthMs * 0.001f;
}

/*
===============
idSoundShader::GetShakeData
===============
*/
const char* idSoundShader::GetShakeData( int index ) const
{
	if( index < 0 || index >= shakes.Num() )
	{
		return "";
	}
	return shakes[index].c_str();
}

/*
===============
idSoundShader::SetShakeData
===============
*/
void idSoundShader::SetShakeData( int index, const char* ampData )
{
	if( index < 0 )
	{
		return;
	}
	if( shakes.Num() <= index )
	{
		shakes.SetNum( index + 1 );
	}
	shakes[index] = ampData != NULL ? ampData : "";
}

/*
===============
idSoundShader::Purge
===============
*/
void idSoundShader::Purge( bool freeBaseBlocks )
{
	(void)freeBaseBlocks;

	for( int i = 0; i < leadins.Num(); i++ )
	{
		if( leadins[i] != NULL )
		{
			leadins[i]->FreeData();
		}
	}
	for( int i = 0; i < entries.Num(); i++ )
	{
		if( entries[i] != NULL )
		{
			entries[i]->FreeData();
		}
	}
}

/*
===============
idSoundShader::LoadSampleData
===============
*/
void idSoundShader::LoadSampleData( int langIndex )
{
	(void)langIndex;

	for( int i = 0; i < leadins.Num(); i++ )
	{
		if( leadins[i] != NULL )
		{
			leadins[i]->LoadResource();
		}
	}
	for( int i = 0; i < entries.Num(); i++ )
	{
		if( entries[i] != NULL )
		{
			entries[i]->LoadResource();
		}
	}
}

/*
===============
idSoundShader::GetSampleName
===============
*/
const char* idSoundShader::GetSampleName( int index ) const
{
	const idSoundSample* sample = GetSampleByIndex( index );
	return sample != NULL ? sample->GetName() : "";
}

/*
===============
idSoundShader::GetSamplesPerSec
===============
*/
int idSoundShader::GetSamplesPerSec( int index ) const
{
	const idSoundSample* sample = GetSampleByIndex( index );
	return sample != NULL ? sample->SampleRate() : 0;
}

/*
===============
idSoundShader::GetNumChannels
===============
*/
int idSoundShader::GetNumChannels( int index ) const
{
	const idSoundSample* sample = GetSampleByIndex( index );
	return sample != NULL ? sample->NumChannels() : 0;
}

/*
===============
idSoundShader::GetNumSamples
===============
*/
int idSoundShader::GetNumSamples( int index ) const
{
	const idSoundSample* sample = GetSampleByIndex( index );
	return sample != NULL ? sample->NumSamples() : 0;
}

/*
===============
idSoundShader::GetMemorySize
===============
*/
int idSoundShader::GetMemorySize( int index ) const
{
	const idSoundSample* sample = GetSampleByIndex( index );
	return sample != NULL ? sample->BufferSize() : 0;
}

/*
===============
idSoundShader::GetNonCacheData
===============
*/
const byte* idSoundShader::GetNonCacheData( int index ) const
{
	const idSoundSample* sample = GetSampleByIndex( index );
	return sample != NULL ? sample->GetNonCacheData() : NULL;
}

/*
===============
idSoundShader::ExpandSmallOggs
===============
*/
void idSoundShader::ExpandSmallOggs( bool force )
{
	if( !force && !frequentlyUsed )
	{
		return;
	}

	for( int i = 0; i < leadins.Num(); i++ )
	{
		if( leadins[i] != NULL )
		{
			leadins[i]->LoadResource();
		}
	}
	for( int i = 0; i < entries.Num(); i++ )
	{
		if( entries[i] != NULL )
		{
			entries[i]->LoadResource();
		}
	}
}

/*
===============
idSoundShader::IsVO_ForPlayer
===============
*/
bool idSoundShader::IsVO_ForPlayer() const
{
	return (parms.soundShaderFlags & SSF_VO_FOR_PLAYER) != 0;
}
