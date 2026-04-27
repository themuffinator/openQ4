// DeclPlayerModel.cpp
//



rvDeclPlayerModel::rvDeclPlayerModel() {
	FreeData();
}

static void DeclPlayerModel_CacheMedia(const rvDeclPlayerModel* decl) {
	idDict media;

	media.Set("model", decl->model.c_str());
	media.Set("def_head", decl->head.c_str());
	media.Set("def_head_ui", decl->uiHead.c_str());
	media.Set("skin", decl->skin.c_str());
	media.SetDefaults(&decl->sounds);
	game->CacheDictionaryMedia(&media);
}

/*
===================
rvDeclPlayerModel::Size
===================
*/
size_t rvDeclPlayerModel::Size(void) const {
	return sizeof(rvDeclPlayerModel)
		+ model.Allocated()
		+ head.Allocated()
		+ uiHead.Allocated()
		+ team.Allocated()
		+ skin.Allocated()
		+ description.Allocated()
		+ sounds.Allocated();
}

/*
===================
rvDeclPlayerModel::DefaultDefinition
===================
*/
const char* rvDeclPlayerModel::DefaultDefinition() const {
	return "{\n\t\"model\"\t\"model_player_marine\"\n\t\"def_head\"\t\"char_marinehead_kane2_client\"\n}";
}

/*
===================
rvDeclPlayerModel::Parse
===================
*/
bool rvDeclPlayerModel::Parse(const char* text, const int textLength) {
	return Parse(text, textLength, false);
}

/*
===================
rvDeclPlayerModel::Parse
===================
*/
bool rvDeclPlayerModel::Parse(const char* text, const int textLength, bool noCaching) {
	idLexer src;
	idToken	token;
	idToken	value;
	bool parsed = true;

	FreeData();

	src.LoadMemory(text, textLength, GetFileName(), GetLineNum());
	src.SetFlags(DECL_LEXER_FLAGS);
	src.SkipUntilString("{");

	while (1) {
		if (!src.ReadToken(&token)) {
			break;
		}

		if ( !token.Icmp( "}" ) ) {
			break;
		}

		if ( token.type != TT_STRING ) {
			src.Warning( "Expected quoted string, but found '%s'", token.c_str() );
			MakeDefault();
			parsed = false;
			break;
		}

		if ( !token.Icmp( "head_offset" ) ) {
			src.Parse1DMatrix( 3, headOffset.ToFloatPtr() );
			continue;
		}

		if ( !src.ReadToken( &value ) ) {
			src.Warning( "Unexpected end of file" );
			MakeDefault();
			parsed = false;
			break;
		}

		if ( !token.Icmp( "model" ) ) {
			model = value;
		} else if ( !token.Icmp( "def_head" ) ) {
			head = value;
		} else if ( !token.Icmp( "def_head_ui" ) ) {
			uiHead = value;
		} else if ( !token.Icmp( "skin" ) ) {
			skin = value;
		} else if ( !token.Icmp( "team" ) ) {
			team = value;
		} else if ( !token.Icmp( "description" ) ) {
			description = value;
		} else if ( !idStr::Cmpn( token.c_str(), "snd_", 4 ) ) {
			sounds.Set( token.c_str(), value.c_str() );
		}
	}

	if ( !parsed ) {
		return false;
	}

	if ( model.Length() <= 0 ) {
		src.Warning( "playerModel decl '%s' without model declaration", GetName() );
		MakeDefault();
	}

	if ( !noCaching ) {
		DeclPlayerModel_CacheMedia(this);
	}

	return parsed;
}

/*
===================
rvDeclPlayerModel::FreeData
===================
*/
void rvDeclPlayerModel::FreeData(void) {
	model.Clear();
	head.Clear();
	headOffset.Zero();
	uiHead.Clear();
	team.Clear();
	skin.Clear();
	description.Clear();
	sounds.Clear();
}

/*
===================
rvDeclPlayerModel::DefaultDefinition
===================
*/
void rvDeclPlayerModel::Print(void) {
	common->Printf("model = %s\n", model.c_str());
	common->Printf("head = %s\n", head.c_str());
	common->Printf("ui head = %s\n", uiHead.c_str());
	common->Printf("skin = %s\n", skin.c_str());
	common->Printf("team = %s\n", team.c_str());
	common->Printf("description = %s\n", description.c_str());
	common->Printf("sounds (count) = %d\n", sounds.GetNumKeyVals());
	sounds.Print();
}

/*
===================
rvDeclPlayerModel::Validate
===================
*/
bool rvDeclPlayerModel::Validate( const char *psText, int iTextLength, idStr &strReportTo ) const {
	(void)strReportTo;

	idDecl *decl = declManager->AllocateDecl( DECL_PLAYER_MODEL );
	const bool valid = DeclManager_ValidateParsedDecl( decl, DECL_PLAYER_MODEL, decl != NULL && decl->Parse( psText, iTextLength, false ) );
	if ( decl != NULL ) {
		decl->FreeData();
	}
	DeclManager_FreeAllocatedDecl( decl );
	return valid;
}
