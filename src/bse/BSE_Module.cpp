#include "BSE_Envelope.h"
#include "BSE_Particle.h"
#include "BSE.h"
#include "BSE_SpawnDomains.h"
#include "BSE_API.h"

class rvDeclEffectEditLocal : public rvDeclEffectEdit {
public:
	void Finish(rvDeclEffect* edit) override {
		if (edit != NULL) {
			edit->Finish();
		}
	}

	rvSegmentTemplate* GetSegmentTemplate(rvDeclEffect* edit, const char* name) override {
		return edit != NULL ? edit->GetSegmentTemplate(name) : NULL;
	}

	rvSegmentTemplate* GetSegmentTemplate(rvDeclEffect* edit, int i) override {
		return edit != NULL ? edit->GetSegmentTemplate(i) : NULL;
	}

	void CopyData(rvDeclEffect* edit, rvDeclEffect* copy) override {
		if (edit != NULL && copy != NULL) {
			edit->CopyData(*copy);
		}
	}

	int AddSegment(rvDeclEffect* edit, rvSegmentTemplate* add) override {
		if (edit == NULL || add == NULL) {
			return -1;
		}
		return edit->AddSegment(*add);
	}

	void DeleteSegment(rvDeclEffect* edit, int index) override {
		if (edit != NULL) {
			edit->DeleteSegment(index);
		}
	}

	void SwapSegments(rvSegmentTemplate* seg1, rvSegmentTemplate* seg2) override {
		if (seg1 == NULL || seg2 == NULL || seg1 == seg2) {
			return;
		}

		rvSegmentTemplate temp;
		temp = *seg1;
		*seg1 = *seg2;
		*seg2 = temp;
	}

	void CreateEditorOriginal(rvDeclEffect* edit) override {
		if (edit != NULL) {
			edit->CreateEditorOriginal();
		}
	}

	void DeleteEditorOriginal(rvDeclEffect* edit) override {
		if (edit != NULL) {
			edit->DeleteEditorOriginal();
		}
	}

	bool CompareToEditorOriginal(rvDeclEffect* edit) override {
		return edit != NULL && edit->CompareToEditorOriginal();
	}

	void RevertToEditorOriginal(rvDeclEffect* edit) override {
		if (edit != NULL) {
			edit->RevertToEditorOriginal();
		}
	}

	void Init(rvSegmentTemplate* edit, rvDeclEffect* effect) override {
		if (edit != NULL) {
			edit->Init(effect);
		}
	}

	bool Parse(rvSegmentTemplate* edit, rvDeclEffect* effect, int type, idParser* lexer) override {
		return edit != NULL && lexer != NULL && edit->Parse(effect, type, lexer);
	}

	void Finish(rvSegmentTemplate* edit, rvDeclEffect* effect) override {
		if (edit != NULL) {
			edit->Finish(effect);
		}
	}

	bool Compare(rvSegmentTemplate* edit, const rvSegmentTemplate* other) const override {
		return edit != NULL && other != NULL && edit->Compare(*other);
	}

	void SetName(rvSegmentTemplate* edit, const char* name) override {
		if (edit != NULL) {
			edit->mSegmentName = name != NULL ? name : "";
		}
	}

	void Finish(rvParticleTemplate* edit) override {
		if (edit != NULL) {
			edit->Finish();
		}
	}

	bool Compare(rvParticleTemplate* edit, const rvParticleTemplate* other) const override {
		return edit != NULL && other != NULL && edit->Compare(*other);
	}

	void Init(rvParticleTemplate* edit) override {
		if (edit != NULL) {
			edit->Init();
		}
	}

	void FixupParms(rvParticleTemplate* edit, rvParticleParms* parms) override {
		if (edit != NULL) {
			edit->FixupParms(parms);
		}
	}

	void SetMaterialName(rvParticleTemplate* edit, const char* name) override {
		if (edit != NULL) {
			edit->mMaterial = declManager->FindMaterial(name != NULL ? name : "_default");
		}
	}

	void SetModelName(rvParticleTemplate* edit, const char* name) override {
		if (edit != NULL) {
			edit->mModel = renderModelManager->FindModel(name != NULL ? name : "_default");
		}
	}

	void SetEntityDefName(rvParticleTemplate* edit, const char* name) override {
		if (edit != NULL) {
			edit->mEntityDefName = name != NULL ? name : "";
		}
	}

	void SetTrailTypeName(rvParticleTemplate* edit, const char* name) override {
		if (edit != NULL) {
			edit->AllocTrail();
			if (name == NULL || name[0] == '\0') {
				edit->mTrailInfo->mTrailType = 0;
				edit->mTrailInfo->mTrailTypeName = "";
			} else if (idStr::Icmp(name, "burn") == 0) {
				edit->mTrailInfo->mTrailType = 1;
				edit->mTrailInfo->mTrailTypeName = "";
			} else if (idStr::Icmp(name, "motion") == 0) {
				edit->mTrailInfo->mTrailType = 2;
				edit->mTrailInfo->mTrailTypeName = "";
			} else {
				edit->mTrailInfo->mTrailType = 3;
				edit->mTrailInfo->mTrailTypeName = name;
			}
		}
	}

	void SetTrailMaterialName(rvParticleTemplate* edit, const char* name) override {
		if (edit != NULL) {
			edit->AllocTrail();
			edit->mTrailInfo->mTrailMaterial = declManager->FindMaterial(name != NULL ? name : "_default");
		}
	}

	bool Compare(rvParticleParms* edit, const rvParticleParms* other) const override {
		return edit != NULL && other != NULL && *edit == *other;
	}

	void CalcRate(rvEnvParms* edit, float* rate, float duration, int count) override {
		if (edit != NULL) {
			edit->CalcRate(rate, duration, count);
		}
	}

	void Evaluate3(rvEnvParms* edit, float time, const float* start, const float* rate, const float* end, float* dest) override {
		if (edit != NULL) {
			edit->Evaluate3(time, start, rate, end, dest);
		}
	}
};

namespace {
rvBSEManagerLocal bseLocal;
rvDeclEffectEditLocal declEffectEditLocal;
}

idDecl* OpenQ4_AllocIntegratedBSEDeclEffect( void ) {
	return new rvDeclEffect();
}

bool OpenQ4_IsIntegratedBSEDeclEffect( const idDecl *decl ) {
	return dynamic_cast<const rvDeclEffect *>( decl ) != NULL;
}

rvBSEManager* OpenQ4_GetIntegratedBSEManager( void ) {
	return &bseLocal;
}

rvDeclEffectEdit* OpenQ4_GetIntegratedBSEDeclEffectEdit( void ) {
	return &declEffectEditLocal;
}
