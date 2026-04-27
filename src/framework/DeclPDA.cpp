/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code.

===========================================================================
*/

static const int DECL_PDA_RECORD_LEXER_FLAGS =
	LEXFL_NOSTRINGCONCAT |
	LEXFL_ALLOWPATHNAMES |
	LEXFL_ALLOWMULTICHARLITERALS |
	LEXFL_ALLOWBACKSLASHSTRINGCONCAT |
	LEXFL_NOFATALERRORS;

static size_t DeclPDA_StringListSize( const idStrList &list ) {
	return list.Allocated();
}

static const idDecl *DeclPDA_FindReferencedDecl( declType_t declType, const char *name, bool makeDefault ) {
	return declManager->FindType( declType, name, makeDefault );
}

static const idDecl *DeclPDA_FindReferencedDeclByIndex( const idStrList &list, declType_t declType, int index ) {
	if ( index < 0 || index >= list.Num() ) {
		return NULL;
	}
	return DeclPDA_FindReferencedDecl( declType, list[index], false );
}

static void DeclPDA_AddRuntimeReferencedDecl( idStrList &list, const char *name, bool unique, declType_t declType, const char *notFoundFormat ) {
	if ( unique && list.Find( name ) != NULL ) {
		return;
	}
	if ( DeclPDA_FindReferencedDecl( declType, name, false ) == NULL ) {
		common->Printf( notFoundFormat, name );
		return;
	}
	list.Append( name );
}

static void DeclPDA_RemoveAddedStringRefs( idStrList &list, int originalCount ) {
	if ( originalCount < 0 ) {
		originalCount = 0;
	}
	if ( list.Num() > originalCount ) {
		list.SetNum( originalCount, false );
	}
}

static void DeclPDA_PrintStub() {
	common->Printf( "Implement me\n" );
}

static bool DeclPDA_FinishParse( idLexer &src, const idDecl *decl, const char *typeName ) {
	if ( src.HadError() ) {
		src.Warning( "%s decl '%s' had a parse error", typeName, decl->GetName() );
		return false;
	}
	return true;
}

/*
=================
idDeclPDA::Size
=================
*/
size_t idDeclPDA::Size( void ) const {
	return sizeof( idDeclPDA )
		+ DeclPDA_StringListSize( videos )
		+ DeclPDA_StringListSize( audios )
		+ DeclPDA_StringListSize( emails )
		+ pdaName.Allocated()
		+ fullName.Allocated()
		+ icon.Allocated()
		+ id.Allocated()
		+ post.Allocated()
		+ title.Allocated()
		+ security.Allocated();
}

/*
===============
idDeclPDA::Print
===============
*/
void idDeclPDA::Print( void ) const {
	DeclPDA_PrintStub();
}

/*
===============
idDeclPDA::List
===============
*/
void idDeclPDA::List( void ) const {
	DeclPDA_PrintStub();
}

/*
================
idDeclPDA::Parse
================
*/
bool idDeclPDA::Parse( const char *text, const int textLength ) {
	return Parse( text, textLength, false );
}

/*
================
idDeclPDA::Parse
================
*/
bool idDeclPDA::Parse( const char *text, const int textLength, bool noCaching ) {
	idLexer src;
	idToken token;

	(void)noCaching;

	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SetFlags( DECL_LEXER_FLAGS );
	src.SkipUntilString( "{" );

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			break;
		}

		if ( !token.Icmp( "name" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			pdaName = token;
			continue;
		}

		if ( !token.Icmp( "fullname" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			fullName = token;
			continue;
		}

		if ( !token.Icmp( "icon" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			icon = token;
			continue;
		}

		if ( !token.Icmp( "id" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			id = token;
			continue;
		}

		if ( !token.Icmp( "post" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			post = token;
			continue;
		}

		if ( !token.Icmp( "title" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			title = token;
			continue;
		}

		if ( !token.Icmp( "security" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			security = token;
			continue;
		}

		if ( !token.Icmp( "pda_email" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			emails.Append( token );
			DeclPDA_FindReferencedDecl( DECL_EMAIL, token, true );
			continue;
		}

		if ( !token.Icmp( "pda_audio" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			audios.Append( token );
			DeclPDA_FindReferencedDecl( DECL_AUDIO, token, true );
			continue;
		}

		if ( !token.Icmp( "pda_video" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			videos.Append( token );
			DeclPDA_FindReferencedDecl( DECL_VIDEO, token, true );
		}
	}

	if ( !DeclPDA_FinishParse( src, this, "PDA" ) ) {
		return false;
	}

	originalVideos = videos.Num();
	originalEmails = emails.Num();
	return true;
}

/*
===================
idDeclPDA::DefaultDefinition
===================
*/
const char *idDeclPDA::DefaultDefinition( void ) const {
	return "{\n\tname  \"default pda\"\n}";
}

/*
===================
idDeclPDA::FreeData
===================
*/
void idDeclPDA::FreeData( void ) {
	videos.Clear();
	audios.Clear();
	emails.Clear();
	originalEmails = 0;
	originalVideos = 0;
}

/*
=================
idDeclPDA::AddVideo
=================
*/
void idDeclPDA::AddVideo( const char *name, bool unique ) const {
	DeclPDA_AddRuntimeReferencedDecl( videos, name, unique, DECL_VIDEO, "Video %s not found\n" );
}

/*
=================
idDeclPDA::AddAudio
=================
*/
void idDeclPDA::AddAudio( const char *name, bool unique ) const {
	DeclPDA_AddRuntimeReferencedDecl( audios, name, unique, DECL_AUDIO, "Audio log %s not found\n" );
}

/*
=================
idDeclPDA::AddEmail
=================
*/
void idDeclPDA::AddEmail( const char *name, bool unique ) const {
	DeclPDA_AddRuntimeReferencedDecl( emails, name, unique, DECL_EMAIL, "Email %s not found\n" );
}

/*
=================
idDeclPDA::RemoveAddedEmailsAndVideos
=================
*/
void idDeclPDA::RemoveAddedEmailsAndVideos() const {
	DeclPDA_RemoveAddedStringRefs( emails, originalEmails );
	DeclPDA_RemoveAddedStringRefs( videos, originalVideos );
}

/*
=================
idDeclPDA::SetSecurity
=================
*/
void idDeclPDA::SetSecurity( const char *sec ) const {
	security = sec;
}

/*
=================
idDeclPDA::GetNumVideos
=================
*/
const int idDeclPDA::GetNumVideos() const {
	return videos.Num();
}

/*
=================
idDeclPDA::GetNumAudios
=================
*/
const int idDeclPDA::GetNumAudios() const {
	return audios.Num();
}

/*
=================
idDeclPDA::GetNumEmails
=================
*/
const int idDeclPDA::GetNumEmails() const {
	return emails.Num();
}

/*
=================
idDeclPDA::GetVideoByIndex
=================
*/
const idDeclVideo *idDeclPDA::GetVideoByIndex( int index ) const {
	return static_cast<const idDeclVideo *>( DeclPDA_FindReferencedDeclByIndex( videos, DECL_VIDEO, index ) );
}

/*
=================
idDeclPDA::GetAudioByIndex
=================
*/
const idDeclAudio *idDeclPDA::GetAudioByIndex( int index ) const {
	return static_cast<const idDeclAudio *>( DeclPDA_FindReferencedDeclByIndex( audios, DECL_AUDIO, index ) );
}

/*
=================
idDeclPDA::GetEmailByIndex
=================
*/
const idDeclEmail *idDeclPDA::GetEmailByIndex( int index ) const {
	return static_cast<const idDeclEmail *>( DeclPDA_FindReferencedDeclByIndex( emails, DECL_EMAIL, index ) );
}

/*
=================
idDeclEmail::Size
=================
*/
size_t idDeclEmail::Size( void ) const {
	return sizeof( idDeclEmail )
		+ text.Allocated()
		+ subject.Allocated()
		+ date.Allocated()
		+ to.Allocated()
		+ from.Allocated()
		+ image.Allocated();
}

/*
===============
idDeclEmail::Print
===============
*/
void idDeclEmail::Print( void ) const {
	DeclPDA_PrintStub();
}

/*
===============
idDeclEmail::List
===============
*/
void idDeclEmail::List( void ) const {
	DeclPDA_PrintStub();
}

/*
================
idDeclEmail::Parse
================
*/
bool idDeclEmail::Parse( const char *text, const int textLength ) {
	return Parse( text, textLength, false );
}

/*
================
idDeclEmail::Parse
================
*/
bool idDeclEmail::Parse( const char *_text, const int textLength, bool noCaching ) {
	idLexer src;
	idToken token;

	(void)noCaching;

	src.LoadMemory( _text, textLength, GetFileName(), GetLineNum() );
	src.SetFlags( DECL_PDA_RECORD_LEXER_FLAGS );
	src.SkipUntilString( "{" );

	text = "";
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			break;
		}

		if ( !token.Icmp( "subject" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			subject = token;
			continue;
		}

		if ( !token.Icmp( "to" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			to = token;
			continue;
		}

		if ( !token.Icmp( "from" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			from = token;
			continue;
		}

		if ( !token.Icmp( "date" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			date = token;
			continue;
		}

		if ( !token.Icmp( "text" ) ) {
			if ( !src.ReadToken( &token ) || token != "{" ) {
				src.Warning( "Email decl '%s' had a parse error", GetName() );
				return false;
			}
			while ( src.ReadToken( &token ) && token != "}" ) {
				text += token;
			}
			continue;
		}

		if ( !token.Icmp( "image" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			image = token;
		}
	}

	return DeclPDA_FinishParse( src, this, "Email" );
}

/*
===================
idDeclEmail::DefaultDefinition
===================
*/
const char *idDeclEmail::DefaultDefinition( void ) const {
	return "{\n\t{\n\t\tto\t5Mail recipient\n\t\tsubject\t5Nothing\n\t\tfrom\t5No one\n\t}\n}";
}

/*
===================
idDeclEmail::FreeData
===================
*/
void idDeclEmail::FreeData( void ) {
}

/*
=================
idDeclVideo::Size
=================
*/
size_t idDeclVideo::Size( void ) const {
	return sizeof( idDeclVideo )
		+ preview.Allocated()
		+ video.Allocated()
		+ videoName.Allocated()
		+ info.Allocated()
		+ audio.Allocated();
}

/*
===============
idDeclVideo::Print
===============
*/
void idDeclVideo::Print( void ) const {
	DeclPDA_PrintStub();
}

/*
===============
idDeclVideo::List
===============
*/
void idDeclVideo::List( void ) const {
	DeclPDA_PrintStub();
}

/*
================
idDeclVideo::Parse
================
*/
bool idDeclVideo::Parse( const char *text, const int textLength ) {
	return Parse( text, textLength, false );
}

/*
================
idDeclVideo::Parse
================
*/
bool idDeclVideo::Parse( const char *text, const int textLength, bool noCaching ) {
	idLexer src;
	idToken token;

	(void)noCaching;

	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SetFlags( DECL_PDA_RECORD_LEXER_FLAGS );
	src.SkipUntilString( "{" );

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			break;
		}

		if ( !token.Icmp( "name" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			videoName = token;
			continue;
		}

		if ( !token.Icmp( "preview" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			preview = token;
			continue;
		}

		if ( !token.Icmp( "video" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			video = token;
			declManager->FindMaterial( video );
			continue;
		}

		if ( !token.Icmp( "info" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			info = token;
			continue;
		}

		if ( !token.Icmp( "audio" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			audio = token;
			declManager->FindSound( audio );
		}
	}

	return DeclPDA_FinishParse( src, this, "Video" );
}

/*
===================
idDeclVideo::DefaultDefinition
===================
*/
const char *idDeclVideo::DefaultDefinition( void ) const {
	return "{\n\t{\n\t\tname\t5Default Video\n\t}\n}";
}

/*
===================
idDeclVideo::FreeData
===================
*/
void idDeclVideo::FreeData( void ) {
}

/*
=================
idDeclAudio::Size
=================
*/
size_t idDeclAudio::Size( void ) const {
	return sizeof( idDeclAudio )
		+ audio.Allocated()
		+ audioName.Allocated()
		+ info.Allocated()
		+ preview.Allocated();
}

/*
===============
idDeclAudio::Print
===============
*/
void idDeclAudio::Print( void ) const {
	DeclPDA_PrintStub();
}

/*
===============
idDeclAudio::List
===============
*/
void idDeclAudio::List( void ) const {
	DeclPDA_PrintStub();
}

/*
================
idDeclAudio::Parse
================
*/
bool idDeclAudio::Parse( const char *text, const int textLength ) {
	return Parse( text, textLength, false );
}

/*
================
idDeclAudio::Parse
================
*/
bool idDeclAudio::Parse( const char *text, const int textLength, bool noCaching ) {
	idLexer src;
	idToken token;

	(void)noCaching;

	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SetFlags( DECL_PDA_RECORD_LEXER_FLAGS );
	src.SkipUntilString( "{" );

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			break;
		}

		if ( !token.Icmp( "name" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			audioName = token;
			continue;
		}

		if ( !token.Icmp( "audio" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			audio = token;
			declManager->FindSound( audio );
			continue;
		}

		if ( !token.Icmp( "info" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			info = token;
			continue;
		}

		if ( !token.Icmp( "preview" ) ) {
			if ( !src.ReadToken( &token ) ) {
				break;
			}
			preview = token;
		}
	}

	return DeclPDA_FinishParse( src, this, "Audio" );
}

/*
===================
idDeclAudio::DefaultDefinition
===================
*/
const char *idDeclAudio::DefaultDefinition( void ) const {
	return "{\n\t{\n\t\tname\t5Default Audio\n\t}\n}";
}

/*
===================
idDeclAudio::FreeData
===================
*/
void idDeclAudio::FreeData( void ) {
}
