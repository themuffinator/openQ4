// DeclPlayerModel.cpp
//



rvDeclPlayerModel::rvDeclPlayerModel() {
	FreeData();
}

/*
===================
rvDeclPlayerModel::Size
===================
*/
size_t rvDeclPlayerModel::Size(void) const {
	return sizeof(rvDeclPlayerModel);
}

/*
===================
rvDeclPlayerModel::DefaultDefinition
===================
*/
const char* rvDeclPlayerModel::DefaultDefinition() const {
	return "{\n\t\"model\"\t\"model_player_marine\"\n\t\"def_head\"\t\"char_marinehead_kane2_client\"\n\t\"def_head_ui\"\t\"char_marinehead_kane2_mp\"\n\t\"skin\"\t\"skins/multiplayer/marine\"\n}";
}

/*
===================
rvDeclPlayerModel::FreeData
===================
*/
bool rvDeclPlayerModel::Parse(const char* text, const int textLength) {
	idLexer src;
	idToken	token;

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

		if ( !token.Icmp( "head_offset" ) ) {
			src.Parse1DMatrix( 3, headOffset.ToFloatPtr() );
			continue;
		}

		idToken value;
		if ( !src.ReadToken( &value ) ) {
			src.Error( "Unexpected end of file parsing key '%s' in playerModel decl '%s'", token.c_str(), GetName() );
			return false;
		}

		if ( !token.Icmp( "model" ) ) {
			model = value;
		} else if ( !token.Icmp( "def_head" ) || !token.Icmp( "head" ) ) {
			head = value;
		} else if ( !token.Icmp( "def_head_ui" ) || !token.Icmp( "ui_head" ) ) {
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

	if ( model.Length() <= 0 ) {
		src.Error( "playerModel decl '%s' without model declaration", GetName() );
		return false;
	}

	if ( !head.Length() ) {
		head = uiHead;
	}

	if ( !uiHead.Length() ) {
		uiHead = head;
	}


	return true;
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

}
