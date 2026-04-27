// DeclLipSync.cpp
//



/*
===================
rvDeclPlayerModel::SetLipSyncData
===================
*/
void rvDeclLipSync::SetLipSyncData(const char* lsd, const char* lang) {
	if ( lsd == NULL ) {
		lsd = "";
	}
	if ( lang == NULL ) {
		lang = "";
	}

	if ( !strchr( lsd, '%' ) ) {
		mLipSyncData.Set(lang, lsd);
		return;
	}

	common->Warning("SetLipSyncData: language %s for lipsync '%s' has invalid character %% in it", lang, GetName());
}

/*
===================
rvDeclPlayerModel::Size
===================
*/
size_t rvDeclLipSync::Size(void) const {
	return sizeof(rvDeclLipSync)
		+ mDescription.Allocated()
		+ mTranscribeText.Allocated()
		+ mHMM.Allocated()
		+ mLipSyncData.Allocated();
}

class rvDeclLipSyncEditLocal : public rvDeclLipSyncEdit {
public:
	void SetLipSyncDescription(rvDeclLipSync* edit, const char* desc) override {
		if (edit != NULL) {
			edit->SetDescription(desc != NULL ? desc : "");
		}
	}

	void SetLipSyncTranscribeText(rvDeclLipSync* edit, const char* text) override {
		if (edit != NULL) {
			edit->SetTranscribeText(text != NULL ? text : "");
		}
	}

	void SetLipSyncData(rvDeclLipSync* edit, const char* lsd, const char* lang) override {
		if (edit != NULL) {
			edit->SetLipSyncData(lsd, lang);
		}
	}
};

static rvDeclLipSyncEditLocal localDeclLipSyncEdit;
rvDeclLipSyncEdit* declLipSyncEdit = &localDeclLipSyncEdit;

/*
===================
rvDeclPlayerModel::DefaultDefinition
===================
*/
const char* rvDeclLipSync::DefaultDefinition() const {
	return "{ description \"<DEFAULTED>\" }";
}

/*
===================
rvDeclPlayerModel::FreeData
===================
*/
bool rvDeclLipSync::Parse(const char* text, const int textLength) {
	return Parse(text, textLength, false);
}

/*
===================
rvDeclPlayerModel::FreeData
===================
*/
bool rvDeclLipSync::Parse(const char* text, const int textLength, bool noCaching) {
	idLexer src;
	idToken	token, token2;
	bool success = false;

	FreeData();
	mHMM = "male";

	src.LoadMemory(text, textLength, GetFileName(), GetLineNum());
	src.SetFlags(DECL_LEXER_FLAGS);
	src.SkipUntilString("{");

	while (1) {
		if (!src.ReadToken(&token)) {
			break;
		}

		if (!token.Icmp("}")) {
			success = true;
			break;
		}

		if (token == "visemes")
		{
			idToken lang;
			idToken data;

			src.ReadToken(&lang);
			src.ReadToken(&data);

			SetLipSyncData(data.c_str(), lang.c_str());
			continue;
		}
		else if (token == "hmm")
		{
			src.ReadToken(&token);
			mHMM = token;
			continue;
		}
		else if (token == "text")
		{
			src.ReadToken(&token);
			mTranscribeText = token;
			continue;
		}
		else if (token == "description")
		{
			src.ReadToken(&token);
			mDescription = token;
			continue;
		}
	}
	(void)token2;
	(void)noCaching;
	return success;
}

/*
===================
rvDeclLipSync::FreeData
===================
*/
void rvDeclLipSync::FreeData(void) {
	mDescription.Clear();
	mTranscribeText.Clear();
	mHMM.Clear();
	mLipSyncData.Clear();
}

/*
===================
rvDeclPlayerModel::RebuildTextSource
===================
*/
bool rvDeclLipSync::RebuildTextSource(void) {
	idFile_Memory f;

	f.WriteFloatString("\r\nlipSync %s\r\n{\r\n", GetName());
	if (mDescription.Length() > 0) {
		f.WriteFloatString("\tdescription\t\"%s\"\r\n", mDescription.c_str());
	}
	if (mTranscribeText.Length() > 0) {
		f.WriteFloatString("\ttext\t\t\"%s\"\r\n", mTranscribeText.c_str());
	}
	if (mHMM.Icmp("male") != 0) {
		f.WriteFloatString("\thmm\t\t\"%s\"\r\n", mHMM.c_str());
	}

	for (int i = 0; i < mLipSyncData.GetNumKeyVals(); ++i) {
		const idKeyValue* entry = mLipSyncData.GetKeyVal(i);
		if (entry != NULL) {
			f.WriteFloatString("\tvisemes\t\"%s\"\t\"%s\"\r\n", entry->GetKey().c_str(), entry->GetValue().c_str());
		}
	}

	f.WriteFloatString("}\r\n\r\n");
	SetText(f.GetDataPtr());
	return true;
}

/*
===================
rvDeclLipSync::Validate
===================
*/
bool rvDeclLipSync::Validate( const char *psText, int iTextLength, idStr &strReportTo ) const {
	(void)strReportTo;

	idDecl *decl = declManager->AllocateDecl( DECL_LIPSYNC );
	const bool valid = DeclManager_ValidateParsedDecl( decl, DECL_LIPSYNC, decl != NULL && decl->Parse( psText, iTextLength, false ) );
	if ( decl != NULL ) {
		decl->FreeData();
	}
	DeclManager_FreeAllocatedDecl( decl );
	return valid;
}
