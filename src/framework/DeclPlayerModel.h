//----------------------------------------------------------------
// DeclPlayerModel.h
//
// Copyright 2002-2006 Raven Software
//----------------------------------------------------------------

#ifndef __DECLPLAYERMODEL_H__
#define __DECLPLAYERMODEL_H__

/*
===============================================================================

rvDeclPlayerModel

===============================================================================
*/

class rvDeclPlayerModel : public idDecl {
public:
	rvDeclPlayerModel();

	idStr					model;
	idStr					head;
	idVec3					headOffset;
	idStr					uiHead;
	idStr					team;
	idStr					skin;
	idStr					description;
	idDict					sounds;

	virtual size_t			Size( void ) const;
	virtual const char *	DefaultDefinition() const;
	virtual bool			Parse( const char *text, const int textLength ) override;
	virtual bool			Parse( const char *text, const int textLength, bool noCaching ) override;
	virtual void			FreeData( void );
	virtual void			Print( void );

	virtual	bool			RebuildTextSource( void ) { return( false ); }
	virtual bool			Validate( const char *psText, int iTextLength, idStr &strReportTo ) const;
};

/*
===============================================================================

Parsing a playerModel decl precaches its model, head, skin and sounds. That is
the right default when the decl is about to be used, but the main menu parses
every one of them purely to read their names and teams for a dropdown, which
loaded every multiplayer character's media before the menu could draw.

Suppress the precache around bulk reads of that kind, then cache the one model
that is actually shown.

===============================================================================
*/
class idSuppressPlayerModelMediaCaching {
public:
			idSuppressPlayerModelMediaCaching();
			~idSuppressPlayerModelMediaCaching();
};

void DeclPlayerModel_CacheMediaForDecl( const rvDeclPlayerModel *decl );

#endif 
